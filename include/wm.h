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
/* Mark a window's content as needing a repaint. The next wm_update()
 * will invoke its paint() callback. The WM calls this automatically
 * for the focused window after every key and click; apps only need to
 * call it if they want to invalidate from a different code path. */
void wm_invalidate(struct window *w);
/* Mark every window as needing a repaint. Used when the desktop
 * background or taskbar chrome changes globally. */
void wm_invalidate_all(void);

#endif
