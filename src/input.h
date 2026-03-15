#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdint.h>

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
