#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

static void sigchild(int sig) {
	int sts;
	wait(&sts);
}

static void die(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	exit(256);
}

static void usage() {
	fprintf(stderr, "usage: ./server <backend port> <front end port>\n");
	exit(1);
}

static void xsplice(int from, int to) {
	char buf[64*1024];
	for (;;) {
		int r,n,w;

		r = read(from, buf, sizeof(buf));
		if (r < 0 && errno == EINTR) {
			continue;
		} else if (r <= 0) {
			shutdown(to, SHUT_WR);
			return;
		}

		n = 0;
		while (n < r) {
			w = write(to, buf + n, r - n);
			if (w < 0 && errno == EINTR) {
				continue;
			} else if (w <= 0) {
				shutdown(from, SHUT_RD);
				return;
			}
			n += w;
		}
	}
}

static int listen_tcp(unsigned short port) {
	int fd;
	struct sockaddr_in6 si6;
	struct sockaddr_in si4;
	struct sockaddr* sa = (struct sockaddr*) &si6;
	size_t salen = sizeof(si6);
	int v6only = 0;

	memset(&si6, 0, sizeof(si6));
	si6.sin6_family = AF_INET6;
	si6.sin6_port = htons(port);

	memset(&si4, 0, sizeof(si4));
	si4.sin_family = AF_INET;
	si4.sin_port = htons(port);

	fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0 || setsockopt(fd, SOL_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only))) {
		close(fd);
		fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sa = (struct sockaddr*) &si4;
		salen = sizeof(si4);
	}
	if (bind(fd, sa, salen)) {
		die("bind to *:%d failed %s", port, strerror(errno));
	}
	if (listen(fd, SOMAXCONN)) {
		die("listen to *:%d failed %s", port, strerror(errno));
	}

	return fd;
}

int main(int argc, char* argv[]) {
	int fd1, fd2;
	int port1, port2;
	char* end;

	if (argc != 3 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		usage();
	}

	signal(SIGCHLD, &sigchild);

	port1 = strtol(argv[1], &end, 10);
	if (*end) usage();

	port2 = strtol(argv[2], &end, 10);
	if (*end) usage();

	fd1 = listen_tcp(port1);
	fd2 = listen_tcp(port2);

	for (;;) {
		int c1, c2;

		/* accept a connection from an incoming client before
		 * accepting the connection from the backend */
		c2 = accept(fd2, NULL, NULL);
		c1 = accept(fd1, NULL, NULL);

		if (!fork()) {
			xsplice(c1, c2);
			exit(0);
		}

		if (!fork()) {
			/* notify the backend that it can connect to its
			 * backend */
			write(c1, "\0", 1);
			xsplice(c2, c1);
			exit(0);
		}

		close(c1);
		close(c2);
	}
}
