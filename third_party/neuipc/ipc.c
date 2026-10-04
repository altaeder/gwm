/* neuipc src/ipc.c */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
 
#include "neuipc.h"

/*
 * internal types
 */

struct handler {
	char               *name;
	swc_ipc_handler_fn  fn;
	void               *userdata;
	struct handler     *next;
};

struct subscriber {
	int    fd;
	char  *topics[SWC_IPC_MAX_TOPICS];
	int    ntopics;
	struct subscriber *next;
};

/*
 * ipc state
 */

static int 		   ipc_fd      = -1;
static char 	          *ipc_path    = NULL;
static struct handler     *handlers    = NULL;
static struct subscriber *subscribers = NULL;

/*
 * handler registry
 */

void
swc_ipc_register(const char *name, swc_ipc_handler_fn fn, void *userdata)
{
	if (strcmp(name, "sub") == 0)
		return; /* reserved */

	struct handler *h;
	for (h = handlers; h != NULL; h = h->next) {
		if (strcmp(h->name, name) == 0) {
			h->fn	    = fn;
			h->userdata = userdata;
			return;
		}
	}

	h = calloc(1, sizeof(*h));
	if (!h) return;
	h->name      = strdup(name);
	h->fn        = fn;
	h-> userdata = userdata;
	h->next      = handlers;
	handlers     = h;
}

void
swc_ipc_unregister(const char *name)
{
	struct handler **pp = &handlers;
	while (*pp) {
		if (strcmp((*pp)->name, name) == 0) {
			struct handler *dead = *pp;
			*pp = dead->next;
			free(dead->name);
			free(dead);
			return;
		}
		pp = &(*pp)->next;
	}
}

static struct handler *
_find_handler(const char *name)
{
	struct handler *h;
	for (h = handlers; h != NULL; h = h->next)
		if (strcmp(h->name, name) == 0)
			return h;
	return NULL;
}

/*
 * subscriber management
 */

static void
_subscriber_free(struct subscriber *s)
{
	close(s->fd);
	for (int i = 0; i < s->ntopics; i++)
		free(s->topics[i]);
	free(s);
}

static bool
_subscriber_wants(struct subscriber *s, const char *topic)
{
	for (int i = 0; i < s->ntopics; i++) {
		if (strcmp(s->topics[i], "*") == 0) return true;
		if (strcmp(s->topics[i], topic) == 0) return true;
	}
	return false;
}

/*
 * init/finish
 */

int
swc_ipc_init(const char *ctl_path)
{
	struct sockaddr_un addr = {0};

	signal(SIGPIPE, SIG_IGN);

	ipc_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ipc_fd < 0)
	    return -1;
	if (fcntl(ipc_fd, F_SETFL, fcntl(ipc_fd, F_GETFL) | O_NONBLOCK) < 0 ||
	    fcntl(ipc_fd, F_SETFD, FD_CLOEXEC) < 0) {
	    close(ipc_fd);
	    ipc_fd = -1;
	    return -1;
	}

	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, ctl_path, sizeof(addr.sun_path) -1);

	unlink(ctl_path); /* remove stale socket */

	if (bind(ipc_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		goto err;


	if (listen(ipc_fd, 8) < 0)
		goto err;

	ipc_path = strdup(ctl_path);
	return 0;

err:
	close(ipc_fd);
	ipc_fd = -1;
	return -1;
}

void
swc_ipc_finish(void)
{
	/* free subs */
	struct subscriber *s = subscribers;
	while (s) {
		struct subscriber *next = s->next;
		_subscriber_free(s);
		s = next;
	}
	subscribers = NULL;

	/* free handlers */
	struct handler *h = handlers;
	while (h) {
		struct handler *next = h->next;
		free(h->name);
		free(h);
		h = next;
	}
	handlers = NULL;

	if (ipc_fd >= 0) {
		close(ipc_fd);
		ipc_fd = -1;
	}

	if (ipc_path) {
		unlink(ipc_path);
		free(ipc_path);
		ipc_path = NULL;
	}
}

int
swc_ipc_get_fd(void)
{
	return ipc_fd;
}

/*
 * request handling
 */

static int
_parse_args(char *buf, char **args, int maxargs)
{
	int n = 0;
	char *p = buf;

	/* strip trailing newline */
	size_t len = strlen(buf);
	if (len > 0 && buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	while (*p && n < maxargs - 1) {
		while (*p == ' ') p++;
		if (*p == '\0') break;
		args[n++] = p;
		while (*p && *p != ' ') p++;
		if (*p == ' ') *p++ = '\0';
	}
	args[n] = NULL;
	return n;
}

static void
_handle_sub(int fd, char **args)
{
	if (args[1] == NULL) {
		const char *err = "err missing topic\n";
		write(fd, err, strlen(err));
		close(fd);
		return;
	}

	struct subscriber *s = calloc(1, sizeof(*s));
	if (!s) {
		close(fd);
		return;
	}

	s->fd = fd;
	for (int i = 1; args[i] != NULL && s->ntopics < SWC_IPC_MAX_TOPICS; i++)
		s->topics[s->ntopics++] = strdup(args[i]);

	s->next     = subscribers;
	subscribers = s;

	const char *ok = "ok\n";
	write(fd, ok, strlen(ok));
	/* connection stays open */
}

static void
_handle_request(int fd)
{
	char buf[SWC_IPC_MAX_MSG];
	ssize_t n;
	do {
		n = read(fd, buf, sizeof(buf) - 1);
	} while (n < 0 && errno == EINTR);
	if (n <= 0) {
		close(fd);
		return;
	}
	buf[n] = '\0';
 
	char *args[SWC_IPC_MAX_ARGS];
	if (_parse_args(buf, args, SWC_IPC_MAX_ARGS) == 0) {
		close(fd);
		return;
	}
 
	if (strcmp(args[0], "sub") == 0) {
		_handle_sub(fd, args);
		return;
	}
 
	struct handler *h = _find_handler(args[0]);
	swc_ipc_status s;
	if (h) {
		s = h->fn(args, h->userdata);
	} else {
		s.ok = false;
		snprintf(s.msg, sizeof(s.msg), "unknown command: %s", args[0]);
	}
 
	char out[SWC_IPC_MAX_MSG + 8];
	int len = snprintf(out, sizeof(out), "%s %s\n",
	                   s.ok ? "ok" : "err",
	                   s.msg);
	write(fd, out, len);
	close(fd);
}

void
swc_ipc_dispatch(void)
{
	int fd;
	do {
		fd = accept(ipc_fd, NULL, NULL);
		if (fd >= 0) {
		    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
		    fcntl(fd, F_SETFD, FD_CLOEXEC);
		} 
	} while (fd < 0 && errno == EINTR);

	if (fd < 0)
		return;
 
	_handle_request(fd);
}

/*
 * emit
 */

int
swc_ipc_emit(const char *topic, const char *fmt, ...)
{
	char msg[SWC_IPC_MAX_MSG];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
 
	char out[SWC_IPC_MAX_MSG + 64];
	int len = snprintf(out, sizeof(out), "%s %s\n", topic, msg);
 
	int count = 0;
	struct subscriber **pp = &subscribers;
	while (*pp) {
		struct subscriber *s = *pp;
		if (_subscriber_wants(s, topic)) {
			ssize_t w = send(s->fd, out, len, MSG_NOSIGNAL);
			if (w < 0 && (errno == EPIPE || errno == ECONNRESET)) {
				/* dead subscriber — remove */
				*pp = s->next;
				_subscriber_free(s);
				continue;
			}
			count++;
		}
		pp = &(*pp)->next;
	}

	return count;
}
