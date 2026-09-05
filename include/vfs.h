#ifndef CURSOROS_VFS_H
#define CURSOROS_VFS_H

#include <types.h>

#define VFS_NAME_MAX 32
#define VFS_MAX_OPEN 16
#define VFS_FILE 1
#define VFS_DIR  2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

struct vfs_dirent {
    char name[VFS_NAME_MAX];
    u8   type;
    u32  size;
};

struct file {
    int  used;
    int  idx;
    u32  pos;
    u32  size;
    u8   writable;
};

void vfs_init(void);
int  vfs_list(struct vfs_dirent *ents, int max);
int  vfs_list_dir(struct vfs_dirent *ents, int max, const char *path);
int  vfs_fopen(const char *path, const char *mode);
int  vfs_fread(int fd, void *buf, u32 n);
int  vfs_fwrite(int fd, const void *buf, u32 n);
void vfs_fclose(int fd);
u32  vfs_fsize(int fd);
int  vfs_create(const char *path);
int  vfs_mkdir(const char *path);
int  vfs_fseek(int fd, u32 offset, int whence);
int  vfs_delete(const char *path);
int  vfs_resolve_path(const char *path);
u8  vfs_get_type(const char *path);

#endif
