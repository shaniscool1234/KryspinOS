#ifndef CURSOROS_ATA_H
#define CURSOROS_ATA_H

#include <types.h>

bool ata_init(void);
bool ata_present(void);
bool ata_read(u32 lba, u8 sectors, void *buf);
bool ata_write(u32 lba, u8 sectors, const void *buf);
u32  ata_sector_count(void);

#endif
