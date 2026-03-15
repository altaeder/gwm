#ifndef SELECT_H
#define SELECT_H

#include <swc.h>

void
stop_select(void);
int
select_tick(void *data);
void
spawn_term_select(const struct swc_rectangle *geometry);
void
update_mode_cursor(void);

#endif
