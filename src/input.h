#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include <swc.h>   // <-- add this, before the handle_gesture prototype

void
handle_gesture(void *data, uint32_t time, uint32_t finger_count, enum swc_gesture_phase phase, double dx, double dy);
void
button(void *data, uint32_t time, uint32_t b, uint32_t state);
void
axis(void *data, uint32_t time, uint32_t axis, int32_t value120);
void
click_cancel(void);
bool
cursor_position(int32_t *x, int32_t *y);
bool
cursor_position_raw(int32_t *x, int32_t *y);
int
cursor_tick(void *data);

#endif
