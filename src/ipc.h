// ipc.h
//#include "input.h"

#ifndef IPC_H
#define IPC_H

#include <stdint.h>

struct wl_event_loop;
struct swc_window;

void ipc_init(struct wl_event_loop *evloop);

void ipc_init(struct wl_event_loop *evloop);

void ipc_broadcast_full(void);

void ipc_broadcast_pan(int32_t x, int32_t y);
void ipc_broadcast_cursor(int mode, int32_t x, int32_t y);

void ipc_broadcast_window_add(struct swc_window *window);
void ipc_broadcast_window_title(struct swc_window *window);
void ipc_broadcast_window_move(struct swc_window *window, int32_t x, int32_t y);
void ipc_broadcast_window_change(struct swc_window *window);
void ipc_broadcast_window_remove(struct swc_window *window);

#endif