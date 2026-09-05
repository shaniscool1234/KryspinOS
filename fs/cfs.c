#include <cfs.h>
#include <ata.h>
#include <string.h>
#include <kstdio.h>

static u8 ramdisk[(CFS_DATA_START + CFS_MAX_FILES * CFS_MAX_FILE_SECTORS) * CFS_SECTOR];
static struct cfs_super *sb = (struct cfs_super *)ramdisk;
static bool use_ata;

static void __attribute__((unused)) disk_read(u32 lba, u32 count, void *buf) {
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

static void seed_file(int idx, const char *name, const char *text, int parent) {
    struct cfs_entry *e = &sb->files[idx];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, VFS_NAME_MAX - 1);
    e->name[VFS_NAME_MAX - 1] = 0;
    e->type = VFS_FILE;
    e->used = 1;
    e->start_sector = (u16)(CFS_DATA_START + idx * CFS_MAX_FILE_SECTORS);
    e->size = (u32)strlen(text);
    e->parent = (u16)parent;
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
    /* Create root directory */
    strncpy(sb->files[0].name, "/", VFS_NAME_MAX - 1);
    sb->files[0].name[VFS_NAME_MAX - 1] = 0;
    sb->files[0].type = VFS_DIR;
    sb->files[0].used = 1;
    sb->files[0].size = 0;
    sb->files[0].parent = 0;
    
    /* Create standard directories */
    strncpy(sb->files[1].name, "Documents", VFS_NAME_MAX - 1);
    sb->files[1].name[VFS_NAME_MAX - 1] = 0;
    sb->files[1].type = VFS_DIR;
    sb->files[1].used = 1;
    sb->files[1].size = 0;
    sb->files[1].parent = 0;
    
    strncpy(sb->files[2].name, "Windows", VFS_NAME_MAX - 1);
    sb->files[2].name[VFS_NAME_MAX - 1] = 0;
    sb->files[2].type = VFS_DIR;
    sb->files[2].used = 1;
    sb->files[2].size = 0;
    sb->files[2].parent = 0;
    
    strncpy(sb->files[3].name, "System32", VFS_NAME_MAX - 1);
    sb->files[3].name[VFS_NAME_MAX - 1] = 0;
    sb->files[3].type = VFS_DIR;
    sb->files[3].used = 1;
    sb->files[3].size = 0;
    sb->files[3].parent = 2; /* Inside Windows */
    
    /* Create sample files in root */
    seed_file(4, "readme.txt",
              "Welcome to KryspinOS!\n"
              "This is a ramdisk CursorFS volume.\n"
              "Open files in Notepad or create new ones.\n", 0);
    seed_file(5, "hello.txt", "Hello from KryspinOS.\n", 0);
    
    /* Create sample files in Documents */
    seed_file(6, "notes.txt", "My important notes go here.\n", 1);
    
    sb->file_count = 7;
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
    return cfs_resolve_path(path, 0);
}

/* Parse path and resolve to file index */
int cfs_resolve_path(const char *path, int current_parent) {
    int i;
    const char *p = path;
    int parent = current_parent;
    char component[VFS_NAME_MAX];
    int comp_idx = 0;
    
    if (p[0] == 0) {
        return parent;
    }
    
    /* Handle absolute paths */
    if (p[0] == '/') {
        parent = 0; /* Start from root */
        p++;
        if (*p == 0) return 0; /* Just root */
    }
    
    /* Parse path components */
    while (*p) {
        comp_idx = 0;
        
        /* Extract next component */
        while (*p && *p != '/' && comp_idx < VFS_NAME_MAX - 1) {
            component[comp_idx++] = *p++;
        }
        component[comp_idx] = 0;
        
        /* Handle .. (parent directory) */
        if (strcmp(component, "..") == 0) {
            if (parent > 0) {
                parent = sb->files[parent].parent;
            }
        } 
        /* Handle . (current directory) */
        else if (strcmp(component, ".") == 0) {
            /* Stay in current directory */
        }
        /* Handle normal component */
        else {
            /* Find component in current directory */
            int found = -1;
            for (i = 0; i < CFS_MAX_FILES; i++) {
                if (sb->files[i].used && 
                    sb->files[i].parent == (u16)parent &&
                    strcmp(sb->files[i].name, component) == 0) {
                    found = i;
                    break;
                }
            }
            
            if (found < 0) {
                return -1; /* Not found */
            }
            parent = found;
        }
        
        /* Skip separator */
        if (*p == '/') p++;
    }
    
    return parent;
}

int cfs_list(struct vfs_dirent *ents, int max, int parent_idx) {
    int i, n = 0;
    for (i = 0; i < CFS_MAX_FILES && n < max; i++) {
        if (!sb->files[i].used) {
            continue;
        }
        /* Only list files in the specified directory */
        if (sb->files[i].parent != (u16)parent_idx) {
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
    int parent = 0;
    const char *p = path;
    char *filename;
    char full_path[VFS_NAME_MAX * 2];
    
    /* Copy path to work with it */
    strncpy(full_path, path, sizeof(full_path) - 1);
    full_path[sizeof(full_path) - 1] = 0;
    p = full_path;
    
    /* Skip leading slash if present */
    if (p[0] == '/') {
        parent = 0;
        p++;
    }
    
    /* Find the last component (filename) and parent directory */
    filename = strrchr(p, '/');
    if (filename) {
        *filename = 0; /* Split the path */
        filename++; /* Move to filename */
        if (*p) {
            parent = cfs_resolve_path(p, 0);
            if (parent < 0) parent = 0;
        }
    } else {
        filename = (char *)p;
    }
    
    /* Check if file already exists */
    if (cfs_resolve_path(path, 0) >= 0) {
        return cfs_resolve_path(path, 0);
    }
    
    /* Find free slot */
    for (i = 1; i < CFS_MAX_FILES; i++) {
        if (!sb->files[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return -1;
    }
    
    /* Create the entry */
    memset(&sb->files[slot], 0, sizeof(sb->files[slot]));
    strncpy(sb->files[slot].name, filename, VFS_NAME_MAX - 1);
    sb->files[slot].type = type;
    sb->files[slot].used = 1;
    sb->files[slot].start_sector = (u16)(CFS_DATA_START + slot * CFS_MAX_FILE_SECTORS);
    sb->files[slot].size = 0;
    sb->files[slot].parent = (u16)parent;
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

int cfs_delete(int idx) {
    int i;
    if (idx < 0 || idx >= CFS_MAX_FILES || !sb->files[idx].used) {
        return -1;
    }
    
    /* Cannot delete root directory */
    if (idx == 0) {
        return -1;
    }
    
    /* Check if directory has children */
    if (sb->files[idx].type == VFS_DIR) {
        for (i = 0; i < CFS_MAX_FILES; i++) {
            if (sb->files[i].used && sb->files[i].parent == (u16)idx) {
                return -1; /* Directory not empty */
            }
        }
    }
    
    /* Mark as unused */
    sb->files[idx].used = 0;
    sb->file_count--;
    persist_sb();
    return 0;
}

int cfs_set_parent(int idx, int new_parent) {
    if (idx < 0 || idx >= CFS_MAX_FILES || !sb->files[idx].used) {
        return -1;
    }
    if (new_parent < 0 || new_parent >= CFS_MAX_FILES || !sb->files[new_parent].used) {
        return -1;
    }
    if (sb->files[new_parent].type != VFS_DIR) {
        return -1;
    }
    sb->files[idx].parent = (u16)new_parent;
    persist_sb();
    return 0;
}

int cfs_rename(int idx, const char *new_name) {
    if (idx < 0 || idx >= CFS_MAX_FILES || !sb->files[idx].used) {
        return -1;
    }
    if (!new_name || !*new_name) {
        return -1;
    }
    strncpy(sb->files[idx].name, new_name, VFS_NAME_MAX - 1);
    sb->files[idx].name[VFS_NAME_MAX - 1] = 0;
    persist_sb();
    return 0;
}
