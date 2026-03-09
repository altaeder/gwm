#include "input.h"
#include "hevel.h"
#include "scroll.h"
#include "select.h"
#include "window.h"
#include "zoom.h"

int
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

void
click_cancel(void)
{
  if (chord.timer) {
    wl_event_source_remove(chord.timer);
    chord.timer = NULL;
  }
  chord.pending = false;
  chord.forwarded = false;
}

void
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

void
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
        swc_pointer_send_button(chord.time, chord.button,
                                WL_POINTER_BUTTON_STATE_PRESSED);
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

int
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

bool
cursor_position_raw(int32_t *x, int32_t *y)
{
  int32_t fx, fy;

  if (!swc_cursor_position(&fx, &fy)) return false;
  *x = wl_fixed_to_int(fx);
  *y = wl_fixed_to_int(fy);
  return true;
}

bool
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
