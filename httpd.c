#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

struct http_request {
	char* method;
	char* uri;
	char* version;

	size_t header_count;
	size_t header_capacity;
	char** header_name;
	char** header_value;
};

void send_error(int client_fd, const char* error) {
	dprintf(client_fd,
		"HTTP/1.1 %s\r\n"
		"Connection: close\r\n"
		"Content-Length: %lu\r\n"
		"\r\n"
		"%s",
		error,
		strlen(error),
		error
	);
}

struct http_request* parse_request(int client_fd) {
	static char buffer[4096];
	if (read(client_fd, buffer, sizeof(buffer)) == sizeof(buffer)) {
		send_error(client_fd, "413 Payload Too Large");
		return NULL;
	}

	struct http_request* request = malloc(sizeof(struct http_request));
	request->method = strtok(buffer, " ");
	request->uri = strtok(NULL, " ");
	request->version = strtok(NULL, "\n");
	request->version[strlen(request->version) - 1] = '\0';

	request->header_count = 0;
	request->header_capacity = 8;
	request->header_name = malloc(
		request->header_capacity * sizeof(*request->header_name)
	);
	request->header_value = malloc(
		request->header_capacity * sizeof(*request->header_value)
	);
	for (;;) {
		char* header_name = strtok(NULL, " ");
		char* header_value = strtok(NULL, "\n");
		if (header_name == NULL || header_value == NULL)
			break;
		header_name[strlen(header_name) - 1] = '\0';
		header_value[strlen(header_value) - 1] = '\0';
		request->header_name[request->header_count] = header_name;
		request->header_value[request->header_count] = header_value;
		if (++request->header_count == request->header_capacity) {
			request->header_capacity *= 2;
			request->header_name = realloc(
				request->header_name,
				request->header_capacity * sizeof(*request->header_name)
			);
			request->header_value = realloc(
				request->header_value,
				request->header_capacity * sizeof(*request->header_value)
			);
		}
	}

	return request;
}

void handle_get(int client_fd, struct http_request* request) {
	static char buffer[4096];
	buffer[0] = '.';
	strncpy(buffer + 1, request->uri, sizeof(buffer) - 2);

	struct stat sb;
	if (stat(buffer, &sb) == -1) {
		send_error(client_fd, "404 Not Found");
		return;
	}
	if (S_ISDIR(sb.st_mode)) {
		char* index_file;
		if (buffer[strlen(buffer) - 1] != '/') {
			index_file = "/index.html";
		} else {
			index_file = "index.html";
		}
		if (strlen(buffer) + strlen(index_file) >= sizeof(buffer) - 1) {
			send_error(client_fd, "414 URI Too Long");
			return;
		}
		strcpy(buffer + strlen(buffer), index_file);
	}
	if (stat(buffer, &sb) == -1) {
		send_error(client_fd, "404 Not Found");
		return;
	}

	FILE* file = fopen(buffer, "r");
	if (!file) {
		perror("fopen");
		return;
	}
	fseek(file, 0, SEEK_END);
	size_t file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	dprintf(client_fd,
		"HTTP/1.1 200 OK\r\n"
		"Connection: close\r\n"
		"Content-Length: %lu\r\n"
		"\r\n",
		file_size
	);

	size_t remaining = file_size;
	while (remaining > 0) {
		size_t sent = fread(
			buffer,
			sizeof(*buffer),
			remaining < sizeof(buffer) ? remaining : sizeof(buffer),
			file
		);
		if (write(client_fd, buffer, sent) == -1) {
			perror("write");
			break;
		}
		remaining -= sent;
	}

	fclose(file);
}

void serve(int client_fd) {
	struct http_request* request = parse_request(client_fd);

	if (request == NULL) {
		return;
	} if (strcmp(request->version, "HTTP/1.1")) {
		send_error(client_fd, "505 HTTP Version Not Supported");
	} else if (!strcmp(request->method, "GET")) {
		handle_get(client_fd, request);
	} else {
		send_error(client_fd, "501 Not Implemented");
	}

	free(request->header_name);
	free(request->header_value);
	free(request);
}

int main(int argc, char** argv) {
	int port = 1104;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--directory")) {
			if (chdir(argv[++i]) == -1) {
				perror("chdir");
				exit(EXIT_FAILURE);
			}
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			printf("Usage:\n");
			printf("  httpd [options]\n");
			printf("\n");
			printf("Options:\n");
			printf("  -d, --directory <directory>\tset the root directory\n");
			printf("  -p, --port <port>\t\tset the server port\n");
			printf("\n");
			exit(EXIT_SUCCESS);
		} else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--port")) {
			port = atoi(argv[++i]);
			if (port == 0) {
				fprintf(stderr, "Invalid port number '%s'\n", argv[i]);
				exit(EXIT_FAILURE);
			}
		} else {
			fprintf(stderr, "Invalid argument '%s'\n", argv[i]);
			exit(EXIT_FAILURE);
		}
	}

	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(
		server_fd,
		SOL_SOCKET,
		SO_REUSEADDR | SO_REUSEPORT,
		(void*) &(int){1},
		(socklen_t) sizeof(int)
	)) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in address;
	size_t address_size = sizeof(address);
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port);
	if (bind(server_fd, (struct sockaddr*) &address, address_size) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	if (listen(server_fd, 3) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	for (;;) {
		int client_fd = accept(
			server_fd,
			(struct sockaddr*) &address,
			(socklen_t*) &address_size
		);
		if (client_fd < 0) {
			perror("accept");
			continue;
		}

		int ret = fork();
		if (ret < 0) {
			perror("fork");
			continue;
		} else if (ret == 0) {
			serve(client_fd);
			close(client_fd);
			exit(EXIT_SUCCESS);
		}
	}

	close(server_fd);
	return 0;
}
