#include <cfs.h>
#include <ata.h>
#include <string.h>
#include <kstdio.h>

static u8 ramdisk[(CFS_DATA_START + CFS_MAX_FILES * CFS_MAX_FILE_SECTORS) * CFS_SECTOR];
static struct cfs_super *sb = (struct cfs_super *)ramdisk;
static bool use_ata;

static void disk_read(u32 lba, u32 count, void *buf) {
    if (use_ata && ata_present()) {
        ata_read(lba, (u8)count, buf);
        return;
    }
    memcpy(buf, ramdisk + lba * CFS_SECTOR, count * CFS_SECTOR);
}

static void disk_write(u32 lba, u32 count, const void *buf) {
    if (use_ata && ata_present()) {
        ata_write(lba, (u8)count, buf);
    }
    memcpy(ramdisk + lba * CFS_SECTOR, buf, count * CFS_SECTOR);
}

static void persist_sb(void) {
    disk_write(0, CFS_DATA_START, ramdisk);
}

static void seed_file(int idx, const char *name, const char *text) {
    struct cfs_entry *e = &sb->files[idx];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, VFS_NAME_MAX - 1);
    e->type = VFS_FILE;
    e->used = 1;
    e->start_sector = (u16)(CFS_DATA_START + idx * CFS_MAX_FILE_SECTORS);
    e->size = (u32)strlen(text);
    e->parent = 0;
    memcpy(ramdisk + e->start_sector * CFS_SECTOR, text, e->size);
}

static void format_new(void) {
    int i;
    memset(ramdisk, 0, sizeof(ramdisk));
    sb->magic = CFS_MAGIC;
    sb->version = 1;
    sb->data_start = CFS_DATA_START;
    for (i = 0; i < CFS_MAX_FILES; i++) {
        sb->files[i].used = 0;
        sb->files[i].start_sector = (u16)(CFS_DATA_START + i * CFS_MAX_FILE_SECTORS);
    }
    strncpy(sb->files[0].name, "/", VFS_NAME_MAX - 1);
    sb->files[0].type = VFS_DIR;
    sb->files[0].used = 1;
    sb->files[0].size = 0;
    sb->files[0].parent = 0;
    seed_file(1, "readme.txt",
              "Welcome to KryspinOS!\n"
              "This is a ramdisk CursorFS volume.\n"
              "Open files in Notepad or create new ones.\n");
    seed_file(2, "hello.txt", "Hello from KryspinOS.\n");
    sb->file_count = 3;
}

void cfs_mount(void) {
    use_ata = false;
    if (ata_present()) {
        u8 tmp[CFS_DATA_START * CFS_SECTOR];
        if (ata_read(0, CFS_DATA_START, tmp) &&
            ((struct cfs_super *)tmp)->magic == CFS_MAGIC) {
            memcpy(ramdisk, tmp, sizeof(tmp));
            use_ata = true;
            kprintf("cfs: mounted ATA CursorFS\n");
            return;
        }
        format_new();
        use_ata = true;
        persist_sb();
        {
            int i;
            for (i = 0; i < CFS_MAX_FILES; i++) {
                if (sb->files[i].used && sb->files[i].type == VFS_FILE) {
                    disk_write(sb->files[i].start_sector, CFS_MAX_FILE_SECTORS,
                               ramdisk + sb->files[i].start_sector * CFS_SECTOR);
                }
            }
        }
        kprintf("cfs: formatted ATA with CursorFS\n");
        return;
    }
    format_new();
    kprintf("cfs: ramdisk ready (%u bytes)\n", (u32)sizeof(ramdisk));
}

int cfs_find(const char *path) {
    int i;
    const char *p = path;
    if (p[0] == '/' && p[1]) {
        p++;
    }
    if (strcmp(p, "/") == 0 || p[0] == 0) {
        return 0;
    }
    for (i = 0; i < CFS_MAX_FILES; i++) {
        if (sb->files[i].used && strcmp(sb->files[i].name, p) == 0) {
            return i;
        }
    }
    return -1;
}

int cfs_list(struct vfs_dirent *ents, int max) {
    int i, n = 0;
    for (i = 1; i < CFS_MAX_FILES && n < max; i++) {
        if (!sb->files[i].used) {
            continue;
        }
        strncpy(ents[n].name, sb->files[i].name, VFS_NAME_MAX - 1);
        ents[n].name[VFS_NAME_MAX - 1] = 0;
        ents[n].type = sb->files[i].type;
        ents[n].size = sb->files[i].size;
        n++;
    }
    return n;
}

int cfs_create(const char *path, u8 type) {
    int i, slot = -1;
    const char *p = path;
    if (p[0] == '/') {
        p++;
    }
    if (cfs_find(p) >= 0) {
        return cfs_find(p);
    }
    for (i = 1; i < CFS_MAX_FILES; i++) {
        if (!sb->files[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return -1;
    }
    memset(&sb->files[slot], 0, sizeof(sb->files[slot]));
    strncpy(sb->files[slot].name, p, VFS_NAME_MAX - 1);
    sb->files[slot].type = type;
    sb->files[slot].used = 1;
    sb->files[slot].start_sector = (u16)(CFS_DATA_START + slot * CFS_MAX_FILE_SECTORS);
    sb->files[slot].size = 0;
    sb->files[slot].parent = 0;
    sb->file_count++;
    persist_sb();
    return slot;
}

int cfs_read(int idx, u32 off, void *buf, u32 n) {
    struct cfs_entry *e;
    u32 avail;
    if (idx < 0 || idx >= CFS_MAX_FILES) {
        return -1;
    }
    e = &sb->files[idx];
    if (!e->used || e->type != VFS_FILE) {
        return -1;
    }
    if (off >= e->size) {
        return 0;
    }
    avail = e->size - off;
    if (n > avail) {
        n = avail;
    }
    memcpy(buf, ramdisk + e->start_sector * CFS_SECTOR + off, n);
    return (int)n;
}

int cfs_write(int idx, u32 off, const void *buf, u32 n) {
    struct cfs_entry *e;
    u32 maxb = CFS_MAX_FILE_SECTORS * CFS_SECTOR;
    if (idx < 0 || idx >= CFS_MAX_FILES) {
        return -1;
    }
    e = &sb->files[idx];
    if (!e->used || e->type != VFS_FILE) {
        return -1;
    }
    if (off + n > maxb) {
        if (off >= maxb) {
            return -1;
        }
        n = maxb - off;
    }
    memcpy(ramdisk + e->start_sector * CFS_SECTOR + off, buf, n);
    if (off + n > e->size) {
        e->size = off + n;
    }
    persist_sb();
    disk_write(e->start_sector, CFS_MAX_FILE_SECTORS,
               ramdisk + e->start_sector * CFS_SECTOR);
    return (int)n;
}

u32 cfs_size(int idx) {
    if (idx < 0 || idx >= CFS_MAX_FILES || !sb->files[idx].used) {
        return 0;
    }
    return sb->files[idx].size;
}

u8 cfs_type(int idx) {
    if (idx < 0 || idx >= CFS_MAX_FILES || !sb->files[idx].used) {
        return 0;
    }
    return sb->files[idx].type;
}

void cfs_set_size(int idx, u32 size) {
    u32 maxb = CFS_MAX_FILE_SECTORS * CFS_SECTOR;
    if (idx < 0 || idx >= CFS_MAX_FILES || !sb->files[idx].used) {
        return;
    }
    if (size > maxb) {
        size = maxb;
    }
    sb->files[idx].size = size;
    persist_sb();
}
