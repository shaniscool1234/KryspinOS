#ifndef CURSOROS_MOUSE_H
#define CURSOROS_MOUSE_H

#include <types.h>

struct mouse_state {
    i32 x;
    i32 y;
    bool left;
    bool right;
    bool middle;
    bool moved;
    bool buttons_changed;
};

void mouse_init(void);
void mouse_poll(struct mouse_state *out);
void mouse_bounds(i32 w, i32 h);

#endif
