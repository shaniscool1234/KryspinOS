#ifndef CURSOROS_KEYBOARD_H
#define CURSOROS_KEYBOARD_H

#include <types.h>

void keyboard_init(void);
int  keyboard_read(char *out); /* 1 if a character was consumed */
bool keyboard_shift(void);

#endif
