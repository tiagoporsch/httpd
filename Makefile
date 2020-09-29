PREFIX?=$(HOME)

httpd: httpd.c
	gcc -Wall -Wextra -O2 -o httpd httpd.c

clean:
	rm --force httpd

install: httpd
	mkdir --parents "$(PREFIX)/bin/"
	rm --force "$(PREFIX)/bin/httpd"
	cp httpd "$(PREFIX)/bin/"

uninstall:
	rm --force "$(PREFIX)/bin/httpd"
	rmdir --ignore-fail-on-non-empty "$(PREFIX)/bin"
