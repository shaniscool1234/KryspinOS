#ifndef CURSOROS_CFS_H
#define CURSOROS_CFS_H

#include <types.h>
#include <vfs.h>

#define CFS_MAGIC      0x53525543u /* 'CURS' */
#define CFS_MAX_FILES  32
#define CFS_SECTOR     512
#define CFS_DATA_START 8
#define CFS_MAX_FILE_SECTORS 16

struct cfs_entry {
    char name[VFS_NAME_MAX];
    u8   type;
    u8   used;
    u16  start_sector;
    u32  size;
    u16  parent;
} PACKED;

struct cfs_super {
    u32 magic;
    u32 version;
    u32 file_count;
    u32 data_start;
    struct cfs_entry files[CFS_MAX_FILES];
} PACKED;

void cfs_mount(void);
int  cfs_list(struct vfs_dirent *ents, int max, int parent_idx);
int  cfs_find(const char *path);
int  cfs_create(const char *path, u8 type);
int  cfs_read(int idx, u32 off, void *buf, u32 n);
int  cfs_write(int idx, u32 off, const void *buf, u32 n);
u32  cfs_size(int idx);
u8   cfs_type(int idx);
void cfs_set_size(int idx, u32 size);
int  cfs_resolve_path(const char *path, int current_parent);
int  cfs_delete(int idx);
int  cfs_set_parent(int idx, int new_parent);
int  cfs_rename(int idx, const char *new_name);

#endif
