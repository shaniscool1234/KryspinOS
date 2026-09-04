#ifndef CURSOROS_PIC_H
#define CURSOROS_PIC_H

#include <types.h>

void pic_remap(u8 offset1, u8 offset2);
void pic_mask_all(void);
void pic_unmask(u8 irq);
void pic_eoi(u8 irq);

#endif
