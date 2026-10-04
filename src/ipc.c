  /*################################################*\
  ## § ----------------|  IPC.C  |--------------- § ##
  ## §                                            § ##
  ## §     ~ IPC broadcaster for GalleryShell ~   § ##
  ## §                                            § ##
  ## § ------------------------------------------ § ##
  \*################################################*/

#include "hevel.h"
#include "ipc.h"
#include "neuipc.h"
// #include "input.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <unistd.h>


#define MAX_CLIENTS 8
static int listen_fd = -1;
static int client_fds[MAX_CLIENTS];
static struct wl_event_source *client_sources[MAX_CLIENTS];
static struct wl_event_loop *saved_evloop = NULL;

  /*┌───────────────┐*\
  |*│    Helpers    │*|
  \*└───────────────┘*/

static const char *
chord_mode_string(chord_mode mode)
{
    switch (mode) {
    case MODE_NONE:
        return "none";
    case MODE_KILL:
        return "kill";
    case MODE_SCROLL:
        return "scroll";
    case MODE_MOVE:
        return "move";
    case MODE_RESIZE:
        return "resize";
    case MODE_JUMP:
        return "jump";
    case MODE_SELECT:
        return "select";
    case MODE_FULLSCREEN:
        return "fullscreen";
    default:
        return "unknown";
    }
}

static int
json_escape_string(char *dst, size_t size, const char *src)
{
  size_t n = 0;

  if (!src) src = "";

  while (*src)
  {
    unsigned char c = (unsigned char)*src++;
    const char *escaped = NULL;
    switch (c)
    {
      case '\"':
        escaped = "\\\"";
        break;
      case '\\':
        escaped = "\\\\";
        break;
      case '\b':
        escaped = "\\b";
        break;
      case '\n':
        escaped = "\\n";
        break;
      case '\r':
        escaped = "\\r";
        break;
      case '\t':
        escaped = "\\t";
        break;
      default:
        if (c < 0x20)
        {
          if (n + 6 >= size) return -1;

          int written = snprintf(dst + n, size - n,
                                 "\\u%04x", c);

          if (written < 0 || (size_t)written >= size - n)
            return -1;

          n += written;
          continue;
        }

        if (n + 1 >= size) return -1;
        dst[n++] = c;
        continue;
    }

    size_t length = strlen(escaped);
    if (n + length >= size)
      return -1;

    memcpy(dst + n, escaped, length);
    n += length;
  }

  if (n >= size)
    return -1;

  dst[n] = '\0';
  return (int)n;
}

// Clean up
static void
evict_client(int i)
{
  if (client_sources[i]) {
    wl_event_source_remove(client_sources[i]);
    client_sources[i] = NULL;
  }
  if (client_fds[i] >= 0) {
    close(client_fds[i]);
    client_fds[i] = -1;
  }
}

// Serialize the current full compositor state
static int
build_state(char *buf, size_t bufsize)
{
  int32_t cursor_x = 0;
  int32_t cursor_y = 0;

  if (!cursor_position(&cursor_x, &cursor_y)) {
      cursor_x = 0;
      cursor_y = 0;
  }

  int n = snprintf(
            buf, bufsize,
            "{"
              "\"type\":\"state\","
              "\"cursor\":{\"x\":%d, \"y\":%d},"
              "\"chord\":{\"mode\":\"%s\"},"
              "\"pan\":{\"x\":%d, \"y\":%d},"
              "\"windows\":[",
            cursor_x,
            cursor_y,
            chord_mode_string(chord.mode),
            scroll.total_pan_x,
            scroll.total_pan_y);

  if (n < 0 || (size_t)n >= bufsize)
    return -1;

  struct window *w;
  bool first = true;

  //sleep ( 1 );

  wl_list_for_each(w, &compositor.windows, link)
  {
    struct swc_rectangle g;

    if (!w->swc)
      continue;
    if (!swc_window_get_geometry(w->swc, &g))
      continue;
    if (!first)
    {
      if ((size_t)n + 1 >= bufsize)
        return -1;

      buf[n++] = ',';
      buf[n] = '\0';
    }

    char escaped_title[4096];

    if (json_escape_string(
           escaped_title,
           sizeof(escaped_title),
           w->swc->title ? w->swc->title : ""
         ) < 0)
      return -1;

    int written =
      snprintf(
        buf + n,
        bufsize - n,
        "{"
          "\"id\":%lu,"
          "\"x\":%d,"
          "\"y\":%d,"
          "\"w\":%d,"
          "\"h\":%d,"
          "\"title\":\"%s\""
        "}",
        (unsigned long)(uintptr_t)w->swc,
        g.x,
        g.y,
        g.width,
        g.height,
        escaped_title
      );

    if (written < 0 || (size_t)written >= bufsize - n)
      return -1;

    n += written;
    first = false;
  }

  int written =
    snprintf(
      buf + n,
      bufsize - n,
      "]}\n"
    );

  if (written < 0 || (size_t)written >= bufsize - n)
    return -1;

  n += written;
  return n;
}

  /*┌───────────────┐*\
  |*│ IPC transport │*|
  \*└───────────────┘*/

// Broadcasting of message (+ safety conditions & checks) to client(s)
static bool
ipc_data_broadcast(int i, const char *buf, size_t length)
{
  if (i < 0 || i >= MAX_CLIENTS)
    return false;

  if (client_fds[i] < 0)
    return false;

  size_t sent = 0;

  while (sent < length)
  {
    ssize_t n = send
    (
      client_fds[i],
      buf + sent,
      length - sent,
      MSG_NOSIGNAL
    );

    if (n > 0)
    {
      sent += (size_t)n;
      continue;
    }

    if (n < 0 && errno == EINTR)
      continue;

    evict_client(i);
    return false;
  }

  return true;
}


// Distribute message (pan coordinates, title-update, etc) to clients
static void
ipc_data_distribute(const char *buf, size_t length)
{
  for (int i = 0; i < MAX_CLIENTS; i++)
  {
    if (client_fds[i] < 0)
      continue;
    ipc_data_broadcast(i, buf, length);
  }
}


  /*┌──────────────┐*\
  |*│ Broadcasters │*|
  \*└──────────────┘*/

// Ipc handler: Send full compositor state
void
ipc_broadcast_full(void)
{
  char buf[65536];

  int length = build_state(buf, sizeof(buf));

  if (length < 0)
      return;

  ipc_data_distribute(buf, (size_t)length);
}

// Cursor / scroll-movement calls
void
ipc_broadcast_pan(int32_t x, int32_t y)
{
  char buf[128];

  int length =
    snprintf(
      buf,
      sizeof(buf),
      "{"
        "\"type\":\"pan\","
        "\"x\":%d,"
        "\"y\":%d"
      "}\n",
      x,
      y
    );

  if (length < 0 || (size_t)length >= sizeof(buf))
    return;

  ipc_data_distribute(buf, (size_t)length);
}

void
ipc_broadcast_cursor(int mode, int32_t x, int32_t y)
{
  char buf[256];

  int length =
    snprintf(
      buf,
      sizeof(buf),
      "{"
        "\"type\":\"cursor\","
        "\"x\":%d,"
        "\"y\":%d,"
        "\"chord\":{\"mode\":\"%s\"}"
      "}\n",
      x,
      y,
      chord_mode_string((chord_mode)mode)
    );

  if (length < 0 || (size_t)length >= sizeof(buf))
    return;

  ipc_data_distribute(buf, (size_t)length);
}

// Window calls

void
ipc_broadcast_window_add(struct swc_window *window)
{
  if (!window)
    return;

  struct swc_rectangle geometry;
  if (!swc_window_get_geometry(window, &geometry))
    return;

  char escaped_title[4096];
  if (json_escape_string(
        escaped_title,
        sizeof(escaped_title),
        window->title ? window-> title : ""
      ) < 0)
    return;

  char buf[8192];

  int length =
    snprintf(
      buf,
      sizeof(buf),
      "{"
        "\"type\":\"window_add\","
        "\"id\":%lu,"
        "\"x\":%d,"
        "\"y\":%d,"
        "\"w\":%d,"
        "\"h\":%d,"
        "\"title\":\"%s\""
      "}\n",
      (unsigned long)(uintptr_t)window,
      geometry.x,
      geometry.y,
      geometry.width,
      geometry.height,
      escaped_title
    );

  if (length < 0 || (size_t)length >= sizeof(buf))
    return;

  ipc_data_distribute(buf, (size_t)length);
}


void
ipc_broadcast_window_change(struct swc_window *window)
{
  if (!window)
    return;

  struct swc_rectangle geometry;
  if (!swc_window_get_geometry(window, &geometry))
    return;

  char escaped_title[4096];
  if (json_escape_string(
        escaped_title,
        sizeof(escaped_title),
        window->title ? window-> title : ""
      ) < 0)
    return;

  char buf[8192];

  int length =
    snprintf(
      buf,
      sizeof(buf),
      "{"
        "\"type\":\"window_add\","
        "\"id\":%lu,"
        "\"x\":%d,"
        "\"y\":%d,"
        "\"w\":%d,"
        "\"h\":%d,"
        "\"title\":\"%s\""
      "}\n",
      (unsigned long)(uintptr_t)window,
      geometry.x,
      geometry.y,
      geometry.width,
      geometry.height,
      escaped_title
    );

  if (length < 0 || (size_t)length >= sizeof(buf))
    return;

  ipc_data_distribute(buf, (size_t)length);
}

void
ipc_broadcast_window_title(struct swc_window *window)
{
  if (!window)
    return;

  char escaped_title[4096];
  if (json_escape_string(
        escaped_title,
        sizeof(escaped_title),
        window->title ? window-> title : ""
      ) < 0)
    return;

  char buf[8192];

  int length =
    snprintf(
      buf,
      sizeof(buf),
      "{"
        "\"type\":\"window_title\","
        "\"id\":%lu,"
        "\"title\":\"%s\""
      "}\n",
      (unsigned long)(uintptr_t)window,
      escaped_title
    );

  if (length < 0 || (size_t)length >= sizeof(buf))
    return;

  ipc_data_distribute(buf, (size_t)length);
}

void
ipc_broadcast_window_move(struct swc_window *window, int32_t x, int32_t y)
{
  if (!window)
    return;

  char buf[256];

  int length =
    snprintf(
      buf,
      sizeof(buf),
      "{"
        "\"type\":\"window_move\","
        "\"id\":%lu,"
        "\"x\":%d,"
        "\"y\":%d"
      "}\n",
      (unsigned long)(uintptr_t)window,
      x,
      y
    );

  if (length < 0 || (size_t)length >= sizeof(buf))
    return;

  ipc_data_distribute(buf, (size_t)length);
}

void
ipc_broadcast_window_remove(struct swc_window *window)
{
  if (!window)
    return;

  char buf[128];

  int length =
    snprintf(
      buf,
      sizeof(buf),
      "{"
        "\"type\":\"window_remove\","
        "\"id\":%lu"
      "}\n",
      (unsigned long)(uintptr_t)window
    );

  if (length < 0 || (size_t)length >= sizeof(buf))
    return;

  ipc_data_distribute(buf, (size_t)length);
}

  /*┌───────────────┐*\
  |*│ Wayland Calls │*|
  \*└───────────────┘*/

static int
on_client_event(int fd, uint32_t mask, void *data)
{
  int i = (int)(intptr_t)data;
  if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) evict_client(i);
  (void)fd;
  return 0;
}

static int
on_accept(int fd, uint32_t mask, void *data)
{
  (void)mask;
  (void)data;
  int client =
    accept(fd, NULL, NULL);

  if (client < 0)
    return 0;

  for (int i = 0; i < MAX_CLIENTS; i++)
  {
    if (client_fds[i] < 0)
    {
      client_fds[i] = client;
      client_sources[i] =
        wl_event_loop_add_fd(
          saved_evloop,
          client,
          WL_EVENT_HANGUP | WL_EVENT_ERROR,
          on_client_event,
          (void *)(intptr_t)i
        );

      char buf[65536];

      int length = build_state(buf, sizeof(buf));
      if (length >= 0)
        ipc_data_broadcast(i, buf, (size_t)length);
      return 0;
    }
  }
  close(client); /* full up */
  return 0;
}

  /*┌────────────────┐*\
  |*│ Initialization │*|
  \*└────────────────┘*/

void
ipc_init(struct wl_event_loop *evloop)
{
  saved_evloop = evloop;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_fds[i] = -1;
    client_sources[i] = NULL;
  }

  struct sockaddr_un addr = {0};
  char path[108];
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  snprintf(path, sizeof(path), "%s/hevel.sock", runtime ? runtime : "/tmp");
  unlink(path);

  listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0)
  {
    perror("ipc: socket");
    return;
  }

  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  //bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("ipc: bind");
    close(listen_fd);
    listen_fd = -1;
    return;
  }

  //listen(listen_fd, 4);
  if (listen(listen_fd, 4) < 0) {
      perror("ipc: listen");
      close(listen_fd);
      listen_fd = -1;
      return;
  }

  if (!wl_event_loop_add_fd(
        evloop,
        listen_fd,
        WL_EVENT_READABLE,
        on_accept,
        NULL)) {
    fprintf(stderr, "ipc: failed to add listen fd to event loop\n");
    close(listen_fd);
    listen_fd = -1;
    return;
  }
  //fprintf(stderr, "ipc: listening on %s\n", path);
}