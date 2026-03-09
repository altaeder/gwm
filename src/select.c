#include "select.h"
#include "hevel.h"
#include "input.h"

int
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

void
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

void
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

void
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
