#ifndef WINDOW_H
#define WINDOW_H

#include <stdbool.h>
#include <swc.h>

struct screen;

void
focus_window(struct swc_window *swc, const char *reason);
bool
is_visible(struct swc_window *w, struct screen *screen);
bool
is_on_screen(struct swc_rectangle *window, struct screen *screen);
bool
is_acme(const struct swc_window *swc);
void
newwindow(struct swc_window *swc);
void
newscreen(struct swc_screen *swc);

#endif
