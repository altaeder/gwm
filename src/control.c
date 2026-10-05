  /*################################################*\
  ## § --------------|  control.c  |------------- § ##
  ## §                                            § ##
  ## §     ~ IPC Listener for Gallery Control ~   § ##
  ## §                                            § ##
  ## § ------------------------------------------ § ##
  \*################################################*/

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hevel.h"
#include "window.h"
#include "neuipc.h"

#define SOCKET_PATH "/tmp/gwm.sock"

//uint32_t og_border_act_out = outer_border_color_active;
//uint32_t og_border_act_in = inner_border_color_active;
//uint32_t og_border_inact_out = outer_border_color_inactive;
//uint32_t og_border_inact_in = inner_border_color_inactive;

static struct window *
find_window_by_pid(pid_t pid)
{
  struct window *w;

  wl_list_for_each(w, &compositor.windows, link)
  {
    if (w->pid == pid) return w;
  }
  return NULL;
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

static int
control_socket_event(int fd, uint32_t mask, void *data)
{
  swc_ipc_dispatch();
  return 0;
}

static swc_ipc_status
control_test(char **args, void *userdata)
{
  swc_ipc_status status;
  status.ok = true;
	snprintf(status.msg, sizeof(status.msg), "sup comrade");
  return status;
}

static swc_ipc_status
control_borders(char **args, void *userdata)
{
  swc_ipc_status status = {0};

  if (!args[1])
  {
    status.ok = false;
  	snprintf(status.msg, sizeof(status.msg), "\nSpecify borders.\nActive, inactive, or reset");
  	return status;
  }

  if (strcmp(args[1], "reset") == 0)
  {
    status.ok = true;
    outer_border_color_active = og_border_act_out;
    inner_border_color_active = og_border_act_in;
    outer_border_color_inactive = og_border_inact_out;
    inner_border_color_inactive = og_border_inact_in;
    select_box_color = og_select_color;

    struct window *oborders;
    wl_list_for_each(oborders, &compositor.windows, link)
    {
      if (!oborders->swc)
        continue;

      swc_window_set_border(oborders->swc, inner_border_color_inactive,
                            inner_border_width, outer_border_color_inactive,
                            outer_border_width);
    }

    swc_window_set_border(compositor.focused, inner_border_color_active,
                          inner_border_width, outer_border_color_active,
                          outer_border_width);

  	snprintf(status.msg, sizeof(status.msg), "\nBorders reset to original colors (startup)");
  	return status;
  }

  if (!args[2] || !args[3])
  {
    status.ok = false;
  	snprintf(status.msg, sizeof(status.msg), "\nGIMME COLOR CODES\n[format: 0xff000000]");
  	return status;
  }

  status.ok = true;

  char *end1;
  char *end2;
  uint32_t color1 = strtoul(args[2], &end1, 0);
  uint32_t color2 = strtoul(args[3], &end2, 0);

  if (*end1 != '\0' || *end2 != '\0')
  {
    status.ok = false;
  	snprintf(status.msg, sizeof(status.msg),
	           "\nFailed to format color codes :(");
  	return status;
  }

  if (strcmp(args[1], "active") == 0)
  {
    outer_border_color_active = color1;
    inner_border_color_active = color2;
    select_box_color = color1;
  }
  else if (strcmp(args[1], "inactive") == 0)
  {
    outer_border_color_inactive = color1;
    inner_border_color_inactive = color2;
  }
  else
  {
    status.ok = false;
    snprintf(status.msg, sizeof(status.msg),
            "\nUnknown border type: %s", args[1]);
    return status;
  }

  struct window *nborders;
  wl_list_for_each(nborders, &compositor.windows, link)
  {
    if (!nborders->swc)
      continue;

    swc_window_set_border(nborders->swc, inner_border_color_inactive,
                          inner_border_width, outer_border_color_inactive,
                          outer_border_width);
  }

  swc_window_set_border(compositor.focused, inner_border_color_active,
                        inner_border_width, outer_border_color_active,
                        outer_border_width);

	snprintf(status.msg, sizeof(status.msg),
	         "\nChanged %s border-colors to 0x%08x, 0x%08x",
	         args[1], color1, color2);
  return status;
}

static swc_ipc_status
control_windows(char **args, void *userdata)
{
  swc_ipc_status status = {0};
  char buf[SWC_IPC_MAX_MSG];
  size_t bufsize = sizeof(buf);
  size_t n = 0;

  int written = snprintf(
            buf, bufsize,
            "Window information:");

  if (written < 0 || (size_t)written >= bufsize)
  {
    status.ok = false;
    snprintf(status.msg, sizeof(status.msg), "failed to build window list");
    return status;
  }

  n = (size_t)written;

  struct window *windows;
  wl_list_for_each(windows, &compositor.windows, link)
  {
    if (!windows->swc)
      continue;

    char escaped_title[4096];

    if (json_escape_string(
           escaped_title,
           sizeof(escaped_title),
           windows->swc->title ? windows->swc->title : ""
         ) < 0)
    {
      status.ok = false;
      snprintf(status.msg, sizeof(status.msg),
        "failed to escape window title");
      return status;
    }

    written =
      snprintf(
        buf + n,
        bufsize - n,
        "id: %lu, title: %s, pid: %ld\n",
        (unsigned long)(uintptr_t)windows->swc,
        escaped_title,
        (long)windows->pid
      );

    if (written < 0 || (size_t)written >= bufsize - n)
    {
      status.ok = false;
      snprintf(status.msg, sizeof(status.msg),
        "window list too large");
      return status;
    }

    n += (size_t)written;
  }

  written =
    snprintf(
      buf + n,
      bufsize - n,
      "End of transmission"
    );

  if (written < 0 || (size_t)written >= bufsize - n)
  {
    status.ok = false;
    snprintf(status.msg, sizeof(status.msg),
      "failed to finish window list");
    return status;
  }

  /*
   * neuipc gives us a fixed response buffer, so copy our constructed
   * message into it.
   */
  snprintf(status.msg, sizeof(status.msg), "%s", buf);

  status.ok = true;
  return status;
}

static swc_ipc_status
control_focus(char **args, void *userdata)
{
  swc_ipc_status status;

  if (!args[1])
  {
    status.ok = false;
  	snprintf(status.msg, sizeof(status.msg), "think friend\n focus what exactly, nothing?\n");
  	return status;
  }

  char *end;
  unsigned long window_id = strtoul(args[1], &end, 10);

  if (*end != '\0')
  {
    status.ok = false;
  	snprintf(status.msg, sizeof(status.msg), "invalid window id\n");
  	return status;
  }

  if (switch_to_window(window_id))
  {
    status.ok = true;
    snprintf(
      status.msg,
      sizeof(status.msg),
      "\nfocused window: %lu",
      window_id
    );
  }
  else
  {
    status.ok = false;
  	snprintf(status.msg, sizeof(status.msg), "failed to focus the window\nid: %lu\n", window_id);
  }
  return status;
}

int
control_init(struct wl_event_loop *evloop)
{
	if (swc_ipc_init(SOCKET_PATH) < 0)
	{
		fprintf(stderr, "swc_ipc_init failed\n");
		return 1;
	}

  swc_ipc_register("test", control_test, NULL);
  swc_ipc_register("focus", control_focus, NULL);
  swc_ipc_register("windows", control_windows, NULL);
  swc_ipc_register("border", control_borders, NULL);

  wl_event_loop_add_fd(
    evloop,
    swc_ipc_get_fd(),
    WL_EVENT_READABLE,
    control_socket_event,
    NULL);
}