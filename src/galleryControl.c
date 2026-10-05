/* neuipc example/client.c - minimal neuipc client demo */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCKET_PATH "/tmp/gwm.sock"
#define BUF_SIZE    512

static int
connect_to_server(void)
{
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int
send_command(const char *cmd)
{
	int fd = connect_to_server();
	if (fd < 0) {
		fprintf(stderr, "connect failed: %s\n", strerror(errno));
		return -1;
	}

	char buf[BUF_SIZE];
	snprintf(buf, sizeof(buf), "%s\n", cmd);
	write(fd, buf, strlen(buf));

	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		printf("%s", buf);
	}

	close(fd);
	return 0;
}

static int
subscribe(const char *topic)
{
	int fd = connect_to_server();
	if (fd < 0) {
		fprintf(stderr, "connect failed: %s\n", strerror(errno));
		return -1;
	}

	char buf[BUF_SIZE];
	snprintf(buf, sizeof(buf), "sub %s\n", topic);
	write(fd, buf, strlen(buf));

	/* read and print events until the server closes */
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
		buf[n] = '\0';
		printf("%s", buf);
	}

	close(fd);
	return 0;
}

static void
usage(const char *argv0)
{
	fprintf(stderr, "usage: %s <command> [args...]\n", argv0);
	fprintf(stderr, "       %s sub <topic>\n", argv0);
	fprintf(stderr, "\nexamples:\n");
	fprintf(stderr, "  %s test\n", argv0);
	fprintf(stderr, "  %s focus [window-id]\n", argv0);
	fprintf(stderr, "  %s windows\n", argv0);
}

int
main(int argc, char *argv[])
{
	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "sub") == 0) {
		if (argc < 3) {
			fprintf(stderr, "sub requires a topic\n");
			return 1;
		}
		return subscribe(argv[2]);
	}

	/* join remaining args into a single command string */
	char cmd[BUF_SIZE] = {0};
	for (int i = 1; i < argc; i++) {
		if (i > 1) strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
		strncat(cmd, argv[i], sizeof(cmd) - strlen(cmd) - 1);
	}

	return send_command(cmd);
}
