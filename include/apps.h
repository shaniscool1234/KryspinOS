#ifndef CURSOROS_APPS_H
#define CURSOROS_APPS_H

#include <types.h>
#include <multiboot.h>

struct window;

void explorer_setup(struct window *w);
void notepad_setup(struct window *w, const char *path);
void notepad_open_path(const char *path);
void terminal_setup(struct window *w);
void system_setup(struct window *w);
void taskmgr_setup(struct window *w);
void apps_set_boot_info(struct multiboot_info *mb);

#endif
