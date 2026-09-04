#include <ata.h>
#include <ports.h>
#include <string.h>
#include <kstdio.h>

#define ATA_DATA    0x1F0
#define ATA_ERR     0x1F1
#define ATA_SECCOUNT 0x1F2
#define ATA_LBA0    0x1F3
#define ATA_LBA1    0x1F4
#define ATA_LBA2    0x1F5
#define ATA_DRIVE   0x1F6
#define ATA_CMD     0x1F7
#define ATA_STATUS  0x1F7
#define ATA_ALT     0x3F6

#define ATA_BSY  0x80
#define ATA_DRQ  0x08
#define ATA_ERRB 0x01

static bool drive_ok;
static u32  drive_sectors;

static int ata_wait_bsy(void) {
    u32 t = 1000000;
    while (t--) {
        if (!(inb(ATA_STATUS) & ATA_BSY)) {
            return 0;
        }
    }
    return -1;
}

static int ata_wait_drq(void) {
    u32 t = 1000000;
    while (t--) {
        u8 s = inb(ATA_STATUS);
        if (s & ATA_ERRB) {
            return -1;
        }
        if (s & ATA_DRQ) {
            return 0;
        }
    }
    return -1;
}

static void ata_select(u32 lba) {
    outb(ATA_DRIVE, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
}

bool ata_init(void) {
    u16 id[256];
    u32 i;
    drive_ok = false;
    drive_sectors = 0;

    outb(ATA_DRIVE, 0xE0);
    io_wait();
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_CMD, 0xEC); /* IDENTIFY */

    if (inb(ATA_STATUS) == 0) {
        kprintf("ata: no primary master\n");
        return false;
    }
    if (ata_wait_bsy() < 0) {
        kprintf("ata: timeout\n");
        return false;
    }
    if (inb(ATA_LBA1) || inb(ATA_LBA2)) {
        kprintf("ata: not ATA (ATAPI?)\n");
        return false;
    }
    if (ata_wait_drq() < 0) {
        kprintf("ata: IDENTIFY failed\n");
        return false;
    }
    insw(ATA_DATA, id, 256);

    drive_sectors = (u32)id[60] | ((u32)id[61] << 16);
    if (drive_sectors == 0) {
        drive_sectors = 2048;
    }
    drive_ok = true;
    kprintf("ata: %u sectors (%u KiB)\n", drive_sectors, drive_sectors / 2);
    (void)i;
    return true;
}

bool ata_present(void) {
    return drive_ok;
}

u32 ata_sector_count(void) {
    return drive_sectors;
}

bool ata_read(u32 lba, u8 sectors, void *buf) {
    u8 i;
    if (!drive_ok || sectors == 0) {
        return false;
    }
    ata_wait_bsy();
    ata_select(lba);
    outb(ATA_SECCOUNT, sectors);
    outb(ATA_LBA0, (u8)lba);
    outb(ATA_LBA1, (u8)(lba >> 8));
    outb(ATA_LBA2, (u8)(lba >> 16));
    outb(ATA_CMD, 0x20);
    for (i = 0; i < sectors; i++) {
        if (ata_wait_bsy() < 0 || ata_wait_drq() < 0) {
            return false;
        }
        insw(ATA_DATA, (u8 *)buf + i * 512, 256);
    }
    return true;
}

bool ata_write(u32 lba, u8 sectors, const void *buf) {
    u8 i;
    if (!drive_ok || sectors == 0) {
        return false;
    }
    ata_wait_bsy();
    ata_select(lba);
    outb(ATA_SECCOUNT, sectors);
    outb(ATA_LBA0, (u8)lba);
    outb(ATA_LBA1, (u8)(lba >> 8));
    outb(ATA_LBA2, (u8)(lba >> 16));
    outb(ATA_CMD, 0x30);
    for (i = 0; i < sectors; i++) {
        if (ata_wait_bsy() < 0 || ata_wait_drq() < 0) {
            return false;
        }
        outsw(ATA_DATA, (const u8 *)buf + i * 512, 256);
    }
    ata_wait_bsy();
    return true;
}
