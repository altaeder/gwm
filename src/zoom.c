#include "zoom.h"
#include "hevel.h"

int
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
