#ifndef CURSOROS_WM_H
#define CURSOROS_WM_H

#include <types.h>
#include <multiboot.h>

void wm_init(struct multiboot_info *mb);
void wm_update(void);
void wm_open_notepad(const char *path);
void wm_open_explorer(void);
void wm_open_terminal(void);
void wm_open_system_info(void);
void wm_open_task_manager(void);
int  wm_process_count(void);
const char *wm_process_name(int index);
u32  wm_memory_used_kb(void);
u32  wm_cpu_usage(void);

#endif
