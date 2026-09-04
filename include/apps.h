#ifndef CURSOROS_APPS_H
#define CURSOROS_APPS_H

#include <types.h>

struct window;

void explorer_setup(struct window *w);
void notepad_setup(struct window *w, const char *path);
void notepad_open_path(const char *path);

#endif
