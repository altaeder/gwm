#include "scroll.h"
#include "hevel.h"
#include "input.h"
#include "select.h"
#include "window.h"

int
move_scroll_tick(void *data)
{
  int32_t x, y;
  struct swc_rectangle geometry;
  int32_t screen_height = 0;
  int32_t screen_width = 0;
  int32_t screen_x = 0;

  (void)data;
  if (chord.mode != MODE_MOVE) return 0;

  /* get screen size*/
  if (compositor.current_screen) {
    screen_height = compositor.current_screen->swc->geometry.height;
    screen_width  = compositor.current_screen->swc->geometry.width;
    screen_x      = compositor.current_screen->swc->geometry.x;
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

  /* check near top/bottom and scroll accordingly */
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

  /* if we are multi axis scrolling also check left/right and scroll accordingly */
  if (scroll_drag_mode && screen_width > 0) {
    if (x < screen_x + move_scroll_edge_threshold) {
      scroll.pending_px_x += move_scroll_speed;
      if (!scroll.timer)
        scroll.timer =
            wl_event_loop_add_timer(compositor.evloop, scroll_tick, NULL);
      if (scroll.timer) wl_event_source_timer_update(scroll.timer, 1);
    } else if (x > screen_x + screen_width - move_scroll_edge_threshold) {
      scroll.pending_px_x -= move_scroll_speed;
      if (!scroll.timer)
        scroll.timer =
            wl_event_loop_add_timer(compositor.evloop, scroll_tick, NULL);
      if (scroll.timer) wl_event_source_timer_update(scroll.timer, 1);
    }
  }

  wl_event_source_timer_update(input.move_scroll_timer, timerms);
  return 0;
}

void
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

int
scroll_tick(void *data)
{
  struct window *w, *tmp;
  struct swc_rectangle geometry;
  int32_t rem = scroll.pending_px;
  int32_t rem_x = scroll.pending_px_x;
  int32_t step, step_x;

  (void)data;

  if (!scroll.timer) return 0;

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
    if (!w->swc) continue;

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

int
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
