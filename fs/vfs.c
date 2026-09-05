#include <vfs.h>
#include <cfs.h>
#include <string.h>
#include <kstdio.h>

static struct file files[VFS_MAX_OPEN];

void vfs_init(void) {
    int i;
    cfs_mount();
    for (i = 0; i < VFS_MAX_OPEN; i++) {
        files[i].used = 0;
    }
}

int vfs_list(struct vfs_dirent *ents, int max) {
    /* List root directory by default */
    return cfs_list(ents, max, 0);
}

int vfs_list_dir(struct vfs_dirent *ents, int max, const char *path) {
    int parent_idx = cfs_resolve_path(path, 0);
    if (parent_idx < 0) return -1;
    return cfs_list(ents, max, parent_idx);
}

int vfs_create(const char *path) {
    return cfs_create(path, VFS_FILE);
}

int vfs_mkdir(const char *path) {
    return cfs_create(path, VFS_DIR);
}

int vfs_fopen(const char *path, const char *mode) {
    int idx, fd;
    u8 wr = 0;
    if (mode && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'))) {
        wr = 1;
    }
    idx = cfs_find(path);
    if (idx < 0) {
        if (!wr) {
            return -1;
        }
        idx = vfs_create(path);
        if (idx < 0) {
            return -1;
        }
    }
    for (fd = 0; fd < VFS_MAX_OPEN; fd++) {
        if (!files[fd].used) {
            files[fd].used = 1;
            files[fd].idx = idx;
            files[fd].pos = (mode && strchr(mode, 'a')) ? cfs_size(idx) : 0;
            files[fd].size = cfs_size(idx);
            files[fd].writable = wr;
            if (mode && strchr(mode, 'w') && !strchr(mode, 'a')) {
                files[fd].size = 0;
                files[fd].pos = 0;
                cfs_set_size(idx, 0);
            }
            return fd;
        }
    }
    return -1;
}

int vfs_fread(int fd, void *buf, u32 n) {
    int r;
    if (fd < 0 || fd >= VFS_MAX_OPEN || !files[fd].used) {
        return -1;
    }
    r = cfs_read(files[fd].idx, files[fd].pos, buf, n);
    if (r > 0) {
        files[fd].pos += (u32)r;
    }
    return r;
}

int vfs_fwrite(int fd, const void *buf, u32 n) {
    int r;
    if (fd < 0 || fd >= VFS_MAX_OPEN || !files[fd].used || !files[fd].writable) {
        return -1;
    }
    r = cfs_write(files[fd].idx, files[fd].pos, buf, n);
    if (r > 0) {
        files[fd].pos += (u32)r;
        if (files[fd].pos > files[fd].size) {
            files[fd].size = files[fd].pos;
        }
    }
    return r;
}

void vfs_fclose(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN) {
        return;
    }
    files[fd].used = 0;
}

u32 vfs_fsize(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !files[fd].used) {
        return 0;
    }
    return files[fd].size;
}

int vfs_fseek(int fd, u32 offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !files[fd].used) {
        return -1;
    }
    if (whence == SEEK_SET) {
        files[fd].pos = offset;
    } else if (whence == SEEK_CUR) {
        files[fd].pos += offset;
    } else if (whence == SEEK_END) {
        files[fd].pos = files[fd].size + offset;
    }
    if (files[fd].pos > files[fd].size) {
        files[fd].pos = files[fd].size;
    }
    return 0;
}

int vfs_delete(const char *path) {
    int idx = cfs_resolve_path(path, 0);
    if (idx < 0) {
        return -1;
    }
    return cfs_delete(idx);
}

int vfs_resolve_path(const char *path) {
    return cfs_resolve_path(path, 0);
}

u8 vfs_get_type(const char *path) {
    int idx = cfs_resolve_path(path, 0);
    if (idx < 0) {
        return 0;
    }
    return cfs_type(idx);
}
