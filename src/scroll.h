#ifndef SCROLL_H
#define SCROLL_H

void
scroll_stop(void);
int
scroll_tick(void *data);
int
scroll_drag_tick(void *data);
int
move_scroll_tick(void *data);

#endif
