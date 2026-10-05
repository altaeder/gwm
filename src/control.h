#ifndef CONTROL_H
#define CONTROL_H

//#include <stdint.h>
struct wl_event_loop;

int
control_init(struct wl_event_loop *evloop);

#endif