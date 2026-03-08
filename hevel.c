#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>

#ifdef __linux__
#include <linux/input-event-codes.h>
/* define os-agnostic input codes for non-linux systems */
#else
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#endif

#include <swc.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "config.h"
#include "nein_cursor.h"

struct window {
  struct swc_window *swc;
  struct wl_list link;

  /* term spawn prims */
  pid_t pid;
  struct window *spawn_parent;
  struct wl_list spawn_children;
  struct wl_list spawn_link;
  bool hidden_for_spawn;
  struct swc_rectangle saved_geometry;

  bool sticky;
};

struct screen {
  struct swc_screen *swc;
  struct wl_list link;
};

static struct {
  struct wl_display *display;
  struct wl_event_loop *evloop;
  struct wl_list windows;
  struct wl_list screens;

  struct screen *current_screen;
  struct swc_window *focused;
} compositor;

typedef enum {
  MODE_NONE,
  MODE_KILL,
  MODE_SCROLL,
  MODE_MOVE,
  MODE_RESIZE,
  MODE_JUMP,
  MODE_SELECT,
} chord_mode;

static struct {

  bool active;

  int32_t move_start_win_x, move_start_win_y;
  int32_t move_start_cursor_x, move_start_cursor_y;

  struct wl_event_source *move_scroll_timer;
  struct wl_event_source *cursor_timer;

  int32_t scroll_drag_last_x, scroll_drag_last_y;
  struct wl_event_source *scroll_drag_timer;

  bool spawn_pending;
  struct swc_rectangle spawn_geometry;
} input;

static struct {
  chord_mode mode;
  bool left, middle, right;
  bool pending;
  bool forwarded;
  uint32_t button;
  uint32_t time;
  struct wl_event_source *timer;
} chord;

static struct {
  int32_t pending_px, pending_px_x;
  int32_t rem, rem_x;
  int8_t cursor_dir;
  bool active;
  bool auto_scrolling;
  struct wl_event_source *timer;
} scroll;

static struct {
  float target;
  struct wl_event_source *timer;
} zoom;

static struct {
  bool selecting;
  int32_t start_x, start_y;
  int32_t cur_x, cur_y;
  struct wl_event_source *timer;
} sel;

static const int timerms = 16;

static const int scrollpx = 64;
static const int scrollease = 4;
static const int scrollcap = 64;

static int
scroll_tick(void *data);
static void
scroll_stop(void);
static int
zoom_tick(void *data);
static bool
is_visible(struct swc_window *w, struct screen *screen);

static void
focus_window(struct swc_window *swc, const char *reason)
{
  const char *from = compositor.focused && compositor.focused->title
                         ? compositor.focused->title
                         : "";
  const char *to = swc && swc->title ? swc->title : "";

  if (compositor.focused == swc) return;
  printf("focus %p ('%s') -> %p ('%s') (%s)\n", (void *)compositor.focused,
         from, (void *)swc, to, reason);

  if (compositor.focused)
    swc_window_set_border(compositor.focused, inner_border_color_inactive,
                          inner_border_width, outer_border_color_inactive,
                          outer_border_width);

  swc_window_focus(swc);

  /* zoom to default size when focusing a window */
  if (enable_zoom && swc && swc_get_zoom() != 1.0f) {
    zoom.target = 1.0f;
    if (!zoom.timer)
      zoom.timer = wl_event_loop_add_timer(compositor.evloop, zoom_tick, NULL);
    if (zoom.timer) wl_event_source_timer_update(zoom.timer, 1);
  }

  if (swc)
    swc_window_set_border(swc, inner_border_color_active, inner_border_width,
                          outer_border_color_active, outer_border_width);

  compositor.focused = swc;

  /* center the focused window: both axes in drag mode, vertical only in scroll
   * wheel mode, only when visible or jumping to it, else you can center
   * offscreen windows */
  if (focus_center == true && swc && compositor.current_screen &&
      (is_visible(compositor.focused, compositor.current_screen) ||
       chord.mode == MODE_JUMP)) {
    struct swc_rectangle window_geom;

    if (swc_window_get_geometry(swc, &window_geom)) {
      /* skip if window has no size yet (not configured by client) */
      if (window_geom.width == 0 || window_geom.height == 0) return;

      int32_t window_center_x = window_geom.x + (int32_t)window_geom.width / 2;
      int32_t window_center_y = window_geom.y + (int32_t)window_geom.height / 2;
      int32_t screen_center_x =
          compositor.current_screen->swc->geometry.x +
          (int32_t)compositor.current_screen->swc->geometry.width / 2;
      int32_t screen_center_y =
          compositor.current_screen->swc->geometry.y +
          (int32_t)compositor.current_screen->swc->geometry.height / 2;

      /* in drag mode: center on both axes; in scroll wheel mode: vertical only
       */
      int32_t scroll_delta_x =
          scroll_drag_mode ? (screen_center_x - window_center_x) : 0;
      int32_t scroll_delta_y = screen_center_y - window_center_y;

      if (scroll_delta_x != 0 || scroll_delta_y != 0) {
        /* stop scroll before auto-scroll */
        scroll_stop();

        scroll.pending_px = scroll_delta_y;
        scroll.pending_px_x = scroll_delta_x;
        scroll.rem = 0;
        scroll.rem_x = 0;
        scroll.auto_scrolling = true;

        if (!scroll.timer) {
          scroll.timer =
              wl_event_loop_add_timer(compositor.evloop, scroll_tick, NULL);
        }
        wl_event_source_timer_update(scroll.timer, timerms);
      }
    }
  }
}

static bool
cursor_position_raw(int32_t *x, int32_t *y)
{
  int32_t fx, fy;

  if (!swc_cursor_position(&fx, &fy)) return false;
  *x = wl_fixed_to_int(fx);
  *y = wl_fixed_to_int(fy);
  return true;
}

static bool
cursor_position(int32_t *x, int32_t *y)
{
  if (!cursor_position_raw(x, y)) return false;

  if (enable_zoom) {
    float zoom = swc_get_zoom();
    if (zoom != 1.0f && compositor.current_screen) {
      int32_t cx = compositor.current_screen->swc->geometry.x +
                   compositor.current_screen->swc->geometry.width / 2;
      int32_t cy = compositor.current_screen->swc->geometry.y +
                   compositor.current_screen->swc->geometry.height / 2;
      *x = (int32_t)((*x - cx) / zoom) + cx;
      *y = (int32_t)((*y - cy) / zoom) + cy;
    }
  }

  return true;
}

/* hacky sorta, only works for vertical cuz of this */
static bool
is_on_screen(struct swc_rectangle *window, struct screen *screen)
{
  struct swc_rectangle *geom = &screen->swc->geometry;

  return window->x + (int32_t)window->width > geom->x &&
         window->x < geom->x + (int32_t)geom->width;
}

static bool
is_visible(struct swc_window *w, struct screen *screen)
{
  struct swc_rectangle *geom = &screen->swc->geometry;
  struct swc_rectangle wgeom;
  swc_window_get_geometry(w, &wgeom);

  bool h = wgeom.x + (int32_t)wgeom.width > geom->x &&
           wgeom.x < geom->x + (int32_t)geom->width;
  bool v = wgeom.y + (int32_t)wgeom.height > geom->y &&
           wgeom.y < geom->y + (int32_t)geom->height;

  return h && v;
}

static bool
is_acme(const struct swc_window *swc)
{
  return swc && swc->app_id && strcmp(swc->app_id, "acme") == 0;
}

static void
update_mode_cursor(void)
{
  if (chord.mode == MODE_KILL)
    swc_set_cursor(SWC_CURSOR_SIGHT);
  else if (chord.mode == MODE_SCROLL) {
    if (scroll.cursor_dir < 0)
      swc_set_cursor(SWC_CURSOR_UP);
    else
      swc_set_cursor(SWC_CURSOR_DOWN);
  } else if (sel.selecting)
    swc_set_cursor(SWC_CURSOR_CROSS);
  else if (chord.mode == MODE_MOVE || chord.mode == MODE_RESIZE)
    swc_set_cursor(SWC_CURSOR_BOX);
  else
    swc_set_cursor(SWC_CURSOR_DEFAULT);
}

static void
maybe_enable_nein_cursor_theme(void)
{
  const struct nein_cursor_meta *arrow =
      &nein_cursor_metadata[NEIN_CURSOR_WHITEARROW];
  const struct nein_cursor_meta *box =
      &nein_cursor_metadata[NEIN_CURSOR_BOXCURSOR];
  const struct nein_cursor_meta *cross =
      &nein_cursor_metadata[NEIN_CURSOR_CROSSCURSOR];
  const struct nein_cursor_meta *sight =
      &nein_cursor_metadata[NEIN_CURSOR_SIGHTCURSOR];
  const struct nein_cursor_meta *up = &nein_cursor_metadata[NEIN_CURSOR_T];
  const struct nein_cursor_meta *down = &nein_cursor_metadata[NEIN_CURSOR_B];

  if (!cursor_theme || strcmp(cursor_theme, "nein") != 0) return;

  swc_set_cursor_mode(SWC_CURSOR_MODE_COMPOSITOR);
  swc_set_cursor_image(SWC_CURSOR_DEFAULT, &nein_cursor_data[arrow->offset],
                       arrow->width, arrow->height, arrow->hotspot_x,
                       arrow->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_BOX, &nein_cursor_data[box->offset],
                       box->width, box->height, box->hotspot_x, box->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_CROSS, &nein_cursor_data[cross->offset],
                       cross->width, cross->height, cross->hotspot_x,
                       cross->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_SIGHT, &nein_cursor_data[sight->offset],
                       sight->width, sight->height, sight->hotspot_x,
                       sight->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_UP, &nein_cursor_data[up->offset], up->width,
                       up->height, up->hotspot_x, up->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_DOWN, &nein_cursor_data[down->offset],
                       down->width, down->height, down->hotspot_x,
                       down->hotspot_y);

  update_mode_cursor();
}

static void
stop_select(void)
{
  if (sel.timer) {
    wl_event_source_remove(sel.timer);
    sel.timer = NULL;
  }
  sel.selecting = false;
  swc_overlay_clear();
  update_mode_cursor();
}

static int
scroll_tick(void *data);

static int
select_tick(void *data)
{
  int32_t x, y;

  (void)data;
  if (!sel.selecting) return 0;

  if (cursor_position(&x, &y)) {
    sel.cur_x = x;
    sel.cur_y = y;
    swc_overlay_set_box(sel.start_x, sel.start_y, x, y, select_box_color,
                        select_box_border);
  }

  wl_event_source_timer_update(sel.timer, timerms);
  return 0;
}

static int
move_scroll_tick(void *data)
{
  int32_t x, y;
  struct swc_rectangle geometry;
  int32_t screen_height = 0;

  (void)data;
  if (chord.mode != MODE_MOVE) return 0;

  /* get screen size*/
  if (compositor.current_screen) {
    screen_height = compositor.current_screen->swc->geometry.height;
  }

  if (screen_height == 0) {
    wl_event_source_timer_update(input.move_scroll_timer, timerms);
    return 0;
  }

  if (!cursor_position(&x, &y)) {
    wl_event_source_timer_update(input.move_scroll_timer, timerms);
    return 0;
  }

  /* get where the where the window starts [line 558], every 16ms calculate
   * where it should be then move only <config-value>% of that gap, then next
   * frame, move <config-value>% of the new, smaller gap, exponential easing*/
  if (compositor.focused &&
      swc_window_get_geometry(compositor.focused, &geometry)) {
    int32_t target_x = input.move_start_win_x + (x - input.move_start_cursor_x);
    int32_t target_y = input.move_start_win_y + (y - input.move_start_cursor_y);
    int32_t new_x =
        geometry.x + (int32_t)((target_x - geometry.x) * move_ease_factor);
    int32_t new_y =
        geometry.y + (int32_t)((target_y - geometry.y) * move_ease_factor);
    swc_window_set_position(compositor.focused, new_x, new_y);
  }

  /* check near top bottom and scroll accordingly */
  if (y < move_scroll_edge_threshold) {
    scroll.pending_px += move_scroll_speed;
    if (!scroll.timer)
      scroll.timer =
          wl_event_loop_add_timer(compositor.evloop, scroll_tick, NULL);
    if (scroll.timer) wl_event_source_timer_update(scroll.timer, 1);
  } else if (y > screen_height - move_scroll_edge_threshold) {
    scroll.pending_px -= move_scroll_speed;
    if (!scroll.timer)
      scroll.timer =
          wl_event_loop_add_timer(compositor.evloop, scroll_tick, NULL);
    if (scroll.timer) wl_event_source_timer_update(scroll.timer, 1);
  }

  wl_event_source_timer_update(input.move_scroll_timer, timerms);
  return 0;
}

static void
spawn_term_select(const struct swc_rectangle *geometry)
{
  pid_t pid;

  input.spawn_pending = true;
  input.spawn_geometry = *geometry;

  pid = fork();
  if (pid == 0) {
    execlp(term, term, term_flag, select_term_app_id, NULL);
    _exit(127);
  }
}

static void
click_cancel(void);

static int
click_timeout(void *data)
{
  (void)data;

  if (!chord.pending) return 0;

  /* don't forward clicks while move chord is active */
  if (chord.mode == MODE_MOVE) {
    click_cancel();
    return 0;
  }

  if (chord.left && chord.right) return 0;

  if (!chord.forwarded) {
    swc_pointer_send_button(chord.time, chord.button,
                            WL_POINTER_BUTTON_STATE_PRESSED);
    chord.forwarded = true;
  }

  return 0;
}

static void
click_cancel(void)
{
  if (chord.timer) {
    wl_event_source_remove(chord.timer);
    chord.timer = NULL;
  }
  chord.pending = false;
  chord.forwarded = false;
}

static void
scroll_stop(void)
{
  scroll.pending_px = 0;
  scroll.pending_px_x = 0;
  scroll.rem = 0;
  scroll.rem_x = 0;
  scroll.auto_scrolling = false;

  /* stop drag timer */
  if (input.scroll_drag_timer) {
    wl_event_source_remove(input.scroll_drag_timer);
    input.scroll_drag_timer = NULL;
  }
}

static int
cursor_tick(void *data)
{
  (void)data;

  int32_t x, y;
  struct screen *ns = NULL;

  if (!cursor_position_raw(&x, &y)) {
    wl_event_source_timer_update(input.cursor_timer, timerms);
    return 0;
  }

  wl_list_for_each(ns, &compositor.screens, link)
  {
    struct swc_rectangle *geom = &ns->swc->geometry;

    if (x >= geom->x && x < geom->x + (int32_t)geom->width && y >= geom->y &&
        y < geom->y + (int32_t)geom->height) {

      if (compositor.current_screen != ns) compositor.current_screen = ns;

      break;
    }
  }

  wl_event_source_timer_update(input.cursor_timer, timerms);
  return 0;
}

static int
zoom_tick(void *data)
{
  (void)data;

  float current = swc_get_zoom();
  float target = zoom.target;
  float diff = target - current;

  /* Stop if close enough */
  if (diff > -0.01f && diff < 0.01f) {
    swc_set_zoom(target);
    return 0;
  }

  /* Ease toward target */
  float step = diff / 4.0f;
  if (step > 0 && step < 0.01f) step = 0.01f;
  if (step < 0 && step > -0.01f) step = -0.01f;

  swc_set_zoom(current + step);

  /* Continue animation */
  wl_event_source_timer_update(zoom.timer, timerms);
  return 0;
}

static int
scroll_tick(void *data)
{
  struct window *w, *tmp;
  struct swc_rectangle geometry;
  int32_t rem = scroll.pending_px;
  int32_t rem_x = scroll.pending_px_x;
  int32_t step, step_x;

  (void)data;

  if (!scroll.timer) 
    return 0;

  if ((chord.mode != MODE_SCROLL && !scroll.auto_scrolling &&
       chord.mode != MODE_MOVE) ||
      (rem == 0 && rem_x == 0)) {
    scroll_stop();
    return 0;
  }

  /* vertical step */
  step = rem / scrollease;
  if (step == 0 && rem != 0) step = rem > 0 ? 1 : -1;
  if (step > scrollcap) step = scrollcap;
  if (step < -scrollcap) step = -scrollcap;

  /* horizontal step */
  step_x = rem_x / scrollease;
  if (step_x == 0 && rem_x != 0) step_x = rem_x > 0 ? 1 : -1;
  if (step_x > scrollcap) step_x = scrollcap;
  if (step_x < -scrollcap) step_x = -scrollcap;

  wl_list_for_each_safe(w, tmp, &compositor.windows, link)
  {
    if (!w->swc)
      continue;

    if (w->sticky) continue;

    /* when scroll with moving window, dont scroll the moving window, it makes
     * it all jittery and ew */
    if (chord.mode == MODE_MOVE && w->swc == compositor.focused) continue;
    if (!swc_window_get_geometry(w->swc, &geometry)) continue;
    if (!scroll_drag_mode &&
        !is_on_screen(&geometry, compositor.current_screen))
      continue;

    swc_window_set_position(w->swc, geometry.x + step_x, geometry.y + step);
  }

  scroll.pending_px -= step;
  scroll.pending_px_x -= step_x;
  wl_event_source_timer_update(scroll.timer, timerms);
  return 0;
}

static int
scroll_drag_tick(void *data)
{
  int32_t x, y;
  int32_t delta_x, delta_y;

  (void)data;

  if (chord.mode != MODE_SCROLL) {
    return 0;
  }

  if (!cursor_position(&x, &y)) {
    wl_event_source_timer_update(input.scroll_drag_timer, timerms);
    return 0;
  }

  delta_x = x - input.scroll_drag_last_x;
  delta_y = y - input.scroll_drag_last_y;
  input.scroll_drag_last_x = x;
  input.scroll_drag_last_y = y;

  if (delta_x == 0 && delta_y == 0) {
    wl_event_source_timer_update(input.scroll_drag_timer, timerms);
    return 0;
  }

  /* invert */
  scroll.pending_px -= delta_y;
  scroll.pending_px_x -= delta_x;

  /* update cursor direction based on drag direction */
  if (delta_y != 0) {
    scroll.cursor_dir = delta_y > 0 ? 1 : -1;
    update_mode_cursor();
  }

  if (!scroll.timer)
    scroll.timer =
        wl_event_loop_add_timer(compositor.evloop, scroll_tick, NULL);
  if (scroll.timer) wl_event_source_timer_update(scroll.timer, 1);

  wl_event_source_timer_update(input.scroll_drag_timer, timerms);
  return 0;
}

static void
axis(void *data, uint32_t time, uint32_t axis, int32_t value120)
{
  (void)data;

  /* while moving a window swallow scroll events so they don't reach clients */
  if (chord.mode == MODE_MOVE) return;

  /* in drag scroll mode, scroll wheel controls zoom when scrolling active */
  if (scroll_drag_mode) {
    if (enable_zoom && chord.mode == MODE_SCROLL && axis == 0 &&
        value120 != 0) {
      /* vertical scroll wheel controls zoom with easing */
      if (zoom.target == 0) zoom.target = swc_get_zoom();
      float delta = (value120 < 0) ? 0.15f : -0.15f;
      zoom.target += delta;
      if (zoom.target < 0.25f) zoom.target = 0.25f;
      if (zoom.target > 4.0f) zoom.target = 4.0f;

      /* Start or continue zoom animation */
      if (!zoom.timer)
        zoom.timer =
            wl_event_loop_add_timer(compositor.evloop, zoom_tick, NULL);
      if (zoom.timer) wl_event_source_timer_update(zoom.timer, 1);
      return;
    }
    swc_pointer_send_axis(time, axis, value120);
    return;
  }

  if (chord.mode != MODE_SCROLL) {
    swc_pointer_send_axis(time, axis, value120);
    return;
  }

  /* only handle vertical scroll */
  if (axis != 0 || value120 == 0) {
    swc_pointer_send_axis(time, axis, value120);
    return;
  }

  scroll.cursor_dir = value120 < 0 ? -1 : 1;
  update_mode_cursor();

  /* convert scroll wheel to viewport scroll */
  int32_t dy = value120 * scrollpx / 120;
  scroll.pending_px += dy;

  if (!scroll.timer)
    scroll.timer =
        wl_event_loop_add_timer(compositor.evloop, scroll_tick, NULL);
  if (scroll.timer) wl_event_source_timer_update(scroll.timer, 1);
}

static void
windowdestroy(void *data)
{
  struct window *w = data;

  /* cleanup for term spawn*/
  if (w->spawn_parent) {
    struct window *terminal = w->spawn_parent;

    wl_list_remove(&w->spawn_link);

    if (wl_list_empty(&terminal->spawn_children) &&
        terminal->hidden_for_spawn) {
      /* restore term */
      swc_window_show(terminal->swc);
      swc_window_set_geometry(terminal->swc, &terminal->saved_geometry);
      terminal->hidden_for_spawn = false;

      /* focus terminal */
      focus_window(terminal->swc, "spawn_child_destroyed");
    }
  }

  if (!wl_list_empty(&w->spawn_children)) {
    struct window *child, *tmp;
    wl_list_for_each_safe(child, tmp, &w->spawn_children, spawn_link)
    {
      child->spawn_parent = NULL;
      wl_list_remove(&child->spawn_link);
      wl_list_init(&child->spawn_link);
    }
  }

  if (compositor.focused == w->swc) focus_window(NULL, "destroy");
  wl_list_remove(&w->link);
  free(w);
}

static void
windowappidchanged(void *data)
{
  struct window *w = data;
  struct swc_rectangle geometry;
  bool is_select = input.spawn_pending && w->swc->app_id &&
                   strcmp(w->swc->app_id, select_term_app_id) == 0;

  if (!is_select) return;

  geometry = input.spawn_geometry;
  if (geometry.width < 50) geometry.width = 50;
  if (geometry.height < 50) geometry.height = 50;
  swc_window_set_geometry(w->swc, &geometry);
  input.spawn_pending = false;
}

static const struct swc_window_handler windowhandler = {
    .destroy = windowdestroy,
    .app_id_changed = windowappidchanged,
};

static void
screendestroy(void *data)
{
  struct screen *s = data;
  wl_list_remove(&s->link);
  free(s);
}

static const struct swc_screen_handler screenhandler = {
    .destroy = screendestroy,
};

static void
newscreen(struct swc_screen *swc)
{
  struct screen *s;

  s = malloc(sizeof(*s));
  if (!s) return;
  s->swc = swc;
  wl_list_insert(&compositor.screens, &s->link);
  swc_screen_set_handler(swc, &screenhandler, s);
  printf("screen %dx%d\n", swc->geometry.width, swc->geometry.height);

  if (!input.cursor_timer)
    input.cursor_timer =
        wl_event_loop_add_timer(compositor.evloop, cursor_tick, NULL);
  if (input.cursor_timer)
    wl_event_source_timer_update(input.cursor_timer, timerms);
}

/* helpers for pid*/
static pid_t
get_parent_pid(pid_t pid)
{
  char path[64];
  FILE *f;
  pid_t parent_pid = 0;

  snprintf(path, sizeof(path), "/proc/%d/stat", pid);
  f = fopen(path, "r");
  if (!f) return 0;

  /* its like: pid (comm) state ppid ... */
  fscanf(f, "%*d %*s %*c %d", &parent_pid);
  fclose(f);
  return parent_pid;
}

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

static bool
is_terminal_window(struct window *w)
{
  if (!w || !w->swc) return false;

  /* check app_id */
  if (w->swc->app_id) {
    for (const char *const *term = terminal_app_ids; *term; term++) {
      if (strstr(w->swc->app_id, *term)) return true;
    }
  }

  /* check title too, because, paranoia */
  if (w->swc->title) {
    for (const char *const *term = terminal_app_ids; *term; term++) {
      if (strstr(w->swc->title, *term)) return true;
    }
  }

  return false;
}

static void
mk_spawn_link(struct window *terminal, struct window *child)
{
  child->spawn_parent = terminal;
  wl_list_insert(&terminal->spawn_children, &child->spawn_link);

  /* save term geom */
  if (swc_window_get_geometry(terminal->swc, &terminal->saved_geometry)) {
    terminal->hidden_for_spawn = true;
    swc_window_hide(terminal->swc);
    swc_window_set_geometry(child->swc, &terminal->saved_geometry);
  }
}

static void
newwindow(struct swc_window *swc)
{
  struct window *w;
  struct swc_rectangle geometry;
  bool is_select = input.spawn_pending && swc->app_id &&
                   strcmp(swc->app_id, select_term_app_id) == 0;

  w = malloc(sizeof(*w));
  if (!w) return;
  w->swc = swc;
  w->pid = 0;
  w->spawn_parent = NULL;
  wl_list_init(&w->spawn_children);
  wl_list_init(&w->spawn_link);
  w->hidden_for_spawn = false;
  w->sticky = false;

  wl_list_insert(&compositor.windows, &w->link);
  swc_window_set_handler(swc, &windowhandler, w);
  swc_window_set_stacked(swc);
  swc_window_set_border(swc, inner_border_color_inactive, inner_border_width,
                        outer_border_color_inactive, outer_border_width);

  /* get pid and check conf for term spawn */
  if (enable_terminal_spawning) {
    w->pid = swc_window_get_pid(swc);

    if (w->pid > 0) {
      /* im so fucking dumb, we need to walk up the proc tree to get the term,
       * otherwise we just get the shell */
      pid_t current_pid = w->pid;
      struct window *terminal = NULL;
      int depth = 0;

      /* walk up 10 levels */
      while (depth < 10 && current_pid > 1) {
        pid_t parent_pid = get_parent_pid(current_pid);
        if (parent_pid <= 1) break;

        /* check pid against term*/
        struct window *candidate = find_window_by_pid(parent_pid);
        if (candidate && is_terminal_window(candidate)) {
          terminal = candidate;
          break;
        }

        current_pid = parent_pid;
        depth++;
      }

      if (terminal) mk_spawn_link(terminal, w);
    }
  }

  if (is_select) {
    geometry = input.spawn_geometry;
    if (geometry.width < 50) geometry.width = 50;
    if (geometry.height < 50) geometry.height = 50;
    swc_window_set_geometry(swc, &geometry);
    input.spawn_pending = false;
  }
  swc_window_show(swc);
  printf("window '%s'\n", swc->title ? swc->title : "");
  focus_window(swc, "new_window");
}

static void
newdevice(struct libinput_device *dev)
{
  (void)dev;
}

static const struct swc_manager manager = {
    .new_screen = newscreen,
    .new_window = newwindow,
    .new_device = newdevice,
};

static void
button(void *data, uint32_t time, uint32_t b, uint32_t state)
{
  const char *name;
  bool pressed;
  int32_t x, y;
  struct swc_rectangle geometry;
  bool was_left = chord.left;
  bool was_right = chord.right;
  // bool was_middle = chord.middle;
  bool is_lr;
  bool is_chord_button;
  bool acme_passthrough = false;

  (void)data;
  (void)time;

  pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);

  switch (b) {
  case BTN_LEFT:
    name = "left";
    chord.left = pressed;
    break;
  case BTN_MIDDLE:
    name = "middle";
    chord.middle = pressed;
    break;
  case BTN_RIGHT:
    name = "right";
    chord.right = pressed;
    break;
  default:
    name = "unknown";
    break;
  }

  printf("button %s (%d) %s\n", name, b, pressed ? "pressed" : "released");

  is_lr = (b == BTN_LEFT || b == BTN_RIGHT);
  is_chord_button = (is_lr || b == BTN_MIDDLE);

  if (cursor_position(&x, &y)) {
    struct swc_window *target = swc_window_at(x, y);
    if (is_acme(target) && target == compositor.focused)
      acme_passthrough = true;
  }

  /* allow 1-3 chord to go to acme specifically */
  if (acme_passthrough && is_lr && pressed) {
    bool other_down = (b == BTN_LEFT) ? was_right : was_left;

    if (other_down) {
      swc_pointer_send_button(time, b, state);
      return;
    }
  }

  if (b == BTN_LEFT && !pressed && chord.mode == MODE_KILL) {
    if (cursor_position(&x, &y)) {
      struct swc_window *target = swc_window_at(x, y);

      if (target) swc_window_close(target);
    }
    chord.mode = MODE_NONE;
    update_mode_cursor();
    if (!chord.left && !chord.middle && !chord.right) input.active = false;
    return;
  }

  if (b == BTN_LEFT && pressed && was_right && !input.active &&
      !acme_passthrough) {
    click_cancel();
    stop_select();
    input.active = true;
    chord.mode = MODE_KILL;
    update_mode_cursor();
    return;
  }

  if (b == BTN_MIDDLE && pressed && was_right && !input.active) {
    click_cancel();
    stop_select();
    input.active = true;
    chord.mode = MODE_SCROLL;
    scroll.cursor_dir = -1;
    update_mode_cursor();
    scroll_stop();

    /* start drag-to-scroll tracking (if enabled) */
    if (scroll_drag_mode) {
      if (cursor_position(&x, &y)) {
        input.scroll_drag_last_x = x;
        input.scroll_drag_last_y = y;
      }
      if (!input.scroll_drag_timer)
        input.scroll_drag_timer =
            wl_event_loop_add_timer(compositor.evloop, scroll_drag_tick, NULL);
      if (input.scroll_drag_timer)
        wl_event_source_timer_update(input.scroll_drag_timer, timerms);
    }

    return;
  }

  if (b == BTN_MIDDLE && !pressed && was_left && !input.active &&
      !sel.selecting && !acme_passthrough) {
    click_cancel();
    stop_select();
    input.active = true;
    chord.mode = MODE_MOVE;
    update_mode_cursor();

    /* get starting pos to be used for easing calculation*/
    if (compositor.focused && cursor_position(&x, &y)) {
      struct swc_rectangle geometry;
      if (swc_window_get_geometry(compositor.focused, &geometry)) {
        input.move_start_win_x = geometry.x;
        input.move_start_win_y = geometry.y;
        input.move_start_cursor_x = x;
        input.move_start_cursor_y = y;
      }
    }

    /* auto-scroll timer for scroll durin win move */
    if (!input.move_scroll_timer)
      input.move_scroll_timer =
          wl_event_loop_add_timer(compositor.evloop, move_scroll_tick, NULL);
    if (input.move_scroll_timer)
      wl_event_source_timer_update(input.move_scroll_timer, timerms);

    /* forward the release so clients dont see stuck */
    swc_pointer_send_button(time, b, state);

    return;
  }

  if (b == BTN_LEFT && !pressed && chord.mode == MODE_MOVE) {
    chord.mode = MODE_NONE;
    update_mode_cursor();

    /* stop timer */
    if (input.move_scroll_timer) {
      wl_event_source_remove(input.move_scroll_timer);
      input.move_scroll_timer = NULL;
    }

    if (!chord.left && !chord.middle && !chord.right) input.active = false;

    /* forward the release so clients dont see stuk */
    swc_pointer_send_button(time, b, state);

    return;
  }

  if (b == BTN_MIDDLE && !pressed && was_right && !input.active &&
      !sel.selecting) {
    click_cancel();
    stop_select();
    input.active = true;
    chord.mode = MODE_RESIZE;
    update_mode_cursor();

    if (compositor.focused) /* bottom right */
      swc_window_begin_resize(compositor.focused,
                              SWC_WINDOW_EDGE_RIGHT | SWC_WINDOW_EDGE_BOTTOM);

    /* forward the middle release so clients don't see it stuck */
    swc_pointer_send_button(time, b, state);

    return;
  }

  if (b == BTN_RIGHT && !pressed && chord.mode == MODE_RESIZE) {
    chord.mode = MODE_NONE;
    update_mode_cursor();

    if (compositor.focused) swc_window_end_resize(compositor.focused);

    if (!chord.left && !chord.middle && !chord.right) input.active = false;

    /* let clients see the release we swallowed */
    swc_pointer_send_button(time, b, state);

    return;
  }

  if (b == BTN_MIDDLE && pressed && was_left && !input.active) {
    click_cancel();
    stop_select();

    if (compositor.focused) {
      struct window *w;
      wl_list_for_each(w, &compositor.windows, link)
      {
        if (w->swc == compositor.focused) {
#if defined(STICKY)
          w->sticky = !w->sticky;
#elif defined(FULLSCREEN)
          w->sticky = !w->sticky;
          swc_window_set_fullscreen(compositor.focused,
                                    compositor.current_screen->swc);
#elif defined(JUMP)
          bool state = focus_center;
          focus_center = true;
          chord.mode = MODE_JUMP;
          struct window *closest = NULL;
          struct window *n;
          struct swc_rectangle ngeom;

          int32_t x = 0, y = 0;
          cursor_position_raw(&x, &y);
          int64_t mindist = INT64_MAX;
          wl_list_for_each(n, &compositor.windows, link)
          {
            if (!n->swc) continue;

            if (!swc_window_get_geometry(n->swc, &ngeom)) continue;

            /* makes a cool switcher thingy */
            if (n->swc == compositor.focused) continue;

            int64_t dx = (int64_t)x - (int64_t)ngeom.x;
            int64_t dy = (int64_t)y - (int64_t)ngeom.y;

            /* fuck sqrt() */
            int64_t dist = dx * dx + dy * dy;

            if (dist < mindist) {
              closest = n;
              mindist = dist;
            }
          }

          if (closest != NULL) focus_window(closest->swc, "jump");

          chord.mode = MODE_NONE;
          focus_center = state;
#endif
          break;
        }
      }
    }

    input.active = true;
    swc_pointer_send_button(time, b, state);
    return;
  }

  if (b == BTN_MIDDLE && !pressed && chord.mode == MODE_SCROLL) {
    return;
  }

  if (pressed && is_lr && !sel.selecting) {
    bool other_down = (b == BTN_LEFT) ? was_right : was_left;

    /* stop auto-scrolling on any clics */
    if (scroll.auto_scrolling) {
      scroll.auto_scrolling = false;
      scroll_stop();
    }

    /* only left button focuses windows */
    if (b == BTN_LEFT && !other_down && cursor_position(&x, &y)) {
      struct swc_window *target = swc_window_at(x, y);

      if (target) focus_window(target, "click");
    }
  }

  if (chord.left && chord.right && !input.active && !acme_passthrough) {
    click_cancel();
    input.active = true;
    if (cursor_position(&x, &y)) {
      sel.selecting = true;
      update_mode_cursor();
      sel.start_x = x;
      sel.start_y = y;
      sel.cur_x = x;
      sel.cur_y = y;
      swc_overlay_set_box(x, y, x, y, select_box_color, select_box_border);
      if (!sel.timer)
        sel.timer =
            wl_event_loop_add_timer(compositor.evloop, select_tick, NULL);
      if (sel.timer) wl_event_source_timer_update(sel.timer, timerms);
    }
  }

  /* while a chord is active swallow left/right events so they don't go to
   * clients */
  if (is_chord_button && input.active && !sel.selecting) {
    bool was_scrolling = chord.mode == MODE_SCROLL;
    if (!chord.right) chord.mode = MODE_NONE;
    if (was_scrolling && chord.mode != MODE_SCROLL) update_mode_cursor();
    if (chord.mode != MODE_SCROLL) scroll_stop();
    if (!chord.left && !chord.middle && !chord.right) input.active = false;
    return;
  }

  if (b == BTN_MIDDLE) {
    if (chord.mode == MODE_MOVE) return;
    swc_pointer_send_button(time, b, state);
    return;
  }

  /* pass normal clicks through to clients */
  if (is_lr && pressed && !sel.selecting) {
    bool other_down = (b == BTN_LEFT) ? was_right : was_left;
    if (other_down) {
      /* chord will activate via the block above */
    } else if (!chord.pending) {
      chord.pending = true;
      chord.forwarded = false;
      chord.button = b;
      chord.time = time;
      if (!chord.timer)
        chord.timer =
            wl_event_loop_add_timer(compositor.evloop, click_timeout, NULL);
      if (chord.timer)
        wl_event_source_timer_update(chord.timer, chord_click_timeout_ms);
      return;
    }
  }

  if (is_lr && !pressed && !sel.selecting) {
    if (chord.pending && chord.button == b) {
      if (!chord.forwarded) {
        swc_pointer_send_button(
            chord.time, chord.button, WL_POINTER_BUTTON_STATE_PRESSED);
      }
      swc_pointer_send_button(time, b, WL_POINTER_BUTTON_STATE_RELEASED);
      click_cancel();
      return;
    }
    swc_pointer_send_button(time, b, WL_POINTER_BUTTON_STATE_RELEASED);
    return;
  }

  if (b == BTN_RIGHT && !pressed && sel.selecting) {
    int32_t x1, y1, x2, y2;
    uint32_t outer_w, outer_h;
    uint32_t bw = outer_border_width + inner_border_width;

    if (!cursor_position(&x, &y)) {
      x = sel.cur_x;
      y = sel.cur_y;
    }
    stop_select();

    x1 = sel.start_x < x ? sel.start_x : x;
    y1 = sel.start_y < y ? sel.start_y : y;
    x2 = sel.start_x < x ? x : sel.start_x;
    y2 = sel.start_y < y ? y : sel.start_y;
    outer_w = (uint32_t)abs(x2 - x1);
    outer_h = (uint32_t)abs(y2 - y1);
    if (outer_w < (50 + 2 * bw)) outer_w = 50 + 2 * bw;
    if (outer_h < (50 + 2 * bw)) outer_h = 50 + 2 * bw;

    /* swc_window_set_*  content geom */
    geometry.x = x1 + (int32_t)bw;
    geometry.y = y1 + (int32_t)bw;
    geometry.width = outer_w > 2 * bw ? outer_w - 2 * bw : 1;
    geometry.height = outer_h > 2 * bw ? outer_h - 2 * bw : 1;
    spawn_term_select(&geometry);
    printf("spawned terminal at %d,%d %ux%u\n", geometry.x, geometry.y,
           geometry.width, geometry.height);
  }

  if (!is_lr) {
    swc_pointer_send_button(time, b, state);
    return;
  }

  if (!chord.left && !chord.middle && !chord.right) input.active = false;
}

static void
quit(void *data, uint32_t time, uint32_t value, uint32_t state)
{
  (void)data;
  (void)time;
  (void)value;
  (void)state;
  wl_display_terminate(compositor.display);
}

static void
sig(int s)
{
  (void)s;
  wl_display_terminate(compositor.display);
}

int
main(void)
{
  struct wl_event_loop *evloop;
  const char *sock;

  wl_list_init(&compositor.windows);
  wl_list_init(&compositor.screens);

  compositor.current_screen = NULL;
  compositor.display = wl_display_create();
  if (!compositor.display) {
    fprintf(stderr, "cannot create display\n");
    return 1;
  }

  evloop = wl_display_get_event_loop(compositor.display);
  compositor.evloop = evloop;

  if (!swc_initialize(compositor.display, evloop, &manager)) {
    fprintf(stderr, "cannot initialize swc\n");
    return 1;
  }

  maybe_enable_nein_cursor_theme();

  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO | SWC_MOD_SHIFT, XKB_KEY_q,
                  quit, NULL);

  /* we can bind mouse buttons using SWC_MOD_ANY */
  swc_add_binding(SWC_BINDING_BUTTON, SWC_MOD_ANY, BTN_LEFT, button, NULL);
  swc_add_binding(SWC_BINDING_BUTTON, SWC_MOD_ANY, BTN_MIDDLE, button, NULL);
  swc_add_binding(SWC_BINDING_BUTTON, SWC_MOD_ANY, BTN_RIGHT, button, NULL);
  if (swc_add_axis_binding(SWC_MOD_ANY, 0, axis, NULL) < 0)
    fprintf(stderr, "cannot bind vertical scroll axis\n");
  if (swc_add_axis_binding(SWC_MOD_ANY, 1, axis, NULL) < 0)
    fprintf(stderr, "cannot bind horizontal scroll axis\n");

  sock = wl_display_add_socket_auto(compositor.display);
  if (!sock) {
    fprintf(stderr, "cannot add socket\n");
    return 1;
  }

  printf("%s\n", sock);
  setenv("WAYLAND_DISPLAY", sock, 1);

  signal(SIGTERM, sig);
  signal(SIGINT, sig);

  wl_display_run(compositor.display);

  swc_finalize();
  wl_display_destroy(compositor.display);

  return 0;
}
