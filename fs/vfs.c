#include <vfs.h>
#include <cfs.h>
#include <string.h>

static struct file files[VFS_MAX_OPEN];

void vfs_init(void) {
    int i;
    cfs_mount();
    for (i = 0; i < VFS_MAX_OPEN; i++) {
        files[i].used = 0;
    }
}

int vfs_list(struct vfs_dirent *ents, int max) {
    return cfs_list(ents, max);
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
