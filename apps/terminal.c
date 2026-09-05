#include <apps.h>
#include <window.h>
#include <vfs.h>
#include <gfx.h>
#include <rtc.h>
#include <string.h>
#include <heap.h>
#include <kstdio.h>

#define TERM_OUTPUT_CAP 8192
#define TERM_INPUT_CAP 128
#define TERM_HISTORY_MAX 16

struct terminal_state {
    char output[TERM_OUTPUT_CAP];
    u32 output_len;
    char input[TERM_INPUT_CAP];
    u32 input_len;
    char history[TERM_HISTORY_MAX][TERM_INPUT_CAP];
    int history_count;
    int history_index;
    char current_dir[VFS_NAME_MAX];
};

static void term_append_char(struct terminal_state *st, char c) {
    if (st->output_len + 1 >= TERM_OUTPUT_CAP) {
        st->output_len = 0;
    }
    st->output[st->output_len++] = c;
    st->output[st->output_len] = 0;
}

static void term_append(struct terminal_state *st, const char *text) {
    while (*text) {
        term_append_char(st, *text++);
    }
}

static void term_line(struct terminal_state *st, const char *text) {
    term_append(st, text);
    term_append_char(st, '\n');
}

static int decimal(u32 value, char *out, int cap) {
    char rev[12];
    int n = 0;
    int i;
    if (value == 0) {
        if (cap > 1) {
            out[0] = '0';
            out[1] = 0;
        }
        return 1;
    }
    while (value && n < (int)sizeof(rev)) {
        rev[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    if (n >= cap) {
        n = cap - 1;
    }
    for (i = 0; i < n; i++) {
        out[i] = rev[n - i - 1];
    }
    out[n] = 0;
    return n;
}

static void term_number_line(struct terminal_state *st, const char *label, u32 value) {
    char number[12];
    term_append(st, label);
    decimal(value, number, sizeof(number));
    term_line(st, number);
}

static void term_append_hex(struct terminal_state *st, u32 value) {
    char hex[12];
    const char *hex_chars = "0123456789ABCDEF";
    int i;
    hex[0] = '0';
    hex[1] = 'x';
    for (i = 0; i < 8; i++) {
        hex[2 + i] = hex_chars[(value >> (28 - i * 4)) & 0xF];
    }
    hex[10] = 0;
    term_append(st, hex);
}

static void trim_whitespace(char *str) {
    char *end;
    while (*str == ' ' || *str == '\t') str++;
    if (*str == 0) return;
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    *(end + 1) = 0;
}

static int split_args(char *cmd, char **args, int max_args) {
    int count = 0;
    char *token = cmd;
    while (*token && count < max_args) {
        while (*token == ' ' || *token == '\t') token++;
        if (*token == 0) break;
        args[count++] = token;
        while (*token && *token != ' ' && *token != '\t') token++;
        if (*token) *token++ = 0;
    }
    return count;
}

static void add_to_history(struct terminal_state *st, const char *cmd) {
    if (st->history_count < TERM_HISTORY_MAX) {
        strncpy(st->history[st->history_count], cmd, TERM_INPUT_CAP - 1);
        st->history[st->history_count][TERM_INPUT_CAP - 1] = 0;
        st->history_count++;
    } else {
        /* Shift history */
        int i;
        for (i = 0; i < TERM_HISTORY_MAX - 1; i++) {
            strncpy(st->history[i], st->history[i + 1], TERM_INPUT_CAP - 1);
        }
        strncpy(st->history[TERM_HISTORY_MAX - 1], cmd, TERM_INPUT_CAP - 1);
        st->history[TERM_HISTORY_MAX - 1][TERM_INPUT_CAP - 1] = 0;
    }
    st->history_index = st->history_count;
}

static void term_list(struct terminal_state *st) {
    struct vfs_dirent ents[32];
    int i;
    int count = vfs_list_dir(ents, 32, st->current_dir);
    if (count < 0) {
        term_line(st, "ls: directory not found");
        return;
    }
    for (i = 0; i < count; i++) {
        term_append(st, ents[i].type == VFS_DIR ? "[DIR]  " : "[FILE] ");
        term_append(st, ents[i].name);
        term_append_char(st, '\n');
    }
}

static void term_cat(struct terminal_state *st, const char *name) {
    char buf[513];
    int fd;
    int n;
    while (*name == ' ') {
        name++;
    }
    if (!*name) {
        term_line(st, "usage: cat <file>");
        return;
    }
    fd = vfs_fopen(name, "r");
    if (fd < 0) {
        term_line(st, "cat: file not found");
        return;
    }
    n = vfs_fread(fd, buf, sizeof(buf) - 1);
    vfs_fclose(fd);
    if (n < 0) {
        term_line(st, "cat: read failed");
        return;
    }
    if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
    buf[n] = 0;
    term_append(st, buf);
    if (n == 0 || buf[n - 1] != '\n') {
        term_append_char(st, '\n');
    }
}

static void term_date(struct terminal_state *st) {
    struct rtc_time now;
    char line[40];
    rtc_read(&now);
    line[0] = '0' + now.day / 10;
    line[1] = '0' + now.day % 10;
    line[2] = '/';
    line[3] = '0' + now.month / 10;
    line[4] = '0' + now.month % 10;
    line[5] = '/';
    decimal(now.year, line + 6, sizeof(line) - 6);
    line[10] = ' ';
    line[11] = '0' + now.hour / 10;
    line[12] = '0' + now.hour % 10;
    line[13] = ':';
    line[14] = '0' + now.minute / 10;
    line[15] = '0' + now.minute % 10;
    line[16] = ':';
    line[17] = '0' + now.second / 10;
    line[18] = '0' + now.second % 10;
    line[19] = 0;
    term_line(st, line);
}

static void term_pwd(struct terminal_state *st) {
    term_append(st, "Current directory: ");
    term_line(st, st->current_dir);
}

static void term_cd(struct terminal_state *st, const char *path) {
    char new_path[VFS_NAME_MAX * 2];
    int idx;
    
    if (strcmp(path, "..") == 0) {
        if (strcmp(st->current_dir, "/") != 0) {
            /* Go to parent directory */
            char *last_slash = strrchr(st->current_dir, '/');
            if (last_slash && last_slash != st->current_dir) {
                *last_slash = 0;
            } else if (last_slash == st->current_dir) {
                st->current_dir[1] = 0; /* Keep root slash */
            }
        }
    } else if (strcmp(path, "/") == 0) {
        strncpy(st->current_dir, "/", VFS_NAME_MAX - 1);
        st->current_dir[VFS_NAME_MAX - 1] = 0;
    } else if (path[0] == '/') {
        /* Absolute path */
        strncpy(new_path, path, sizeof(new_path) - 1);
        new_path[sizeof(new_path) - 1] = 0;
        idx = vfs_resolve_path(new_path);
        if (idx >= 0) {
            /* Check if it's a directory */
            if (vfs_get_type(new_path) == VFS_DIR) {
                strncpy(st->current_dir, new_path, VFS_NAME_MAX - 1);
                st->current_dir[VFS_NAME_MAX - 1] = 0;
            } else {
                term_line(st, "cd: not a directory");
                return;
            }
        } else {
            term_line(st, "cd: directory not found");
            return;
        }
    } else {
        /* Relative path */
        strncpy(new_path, st->current_dir, sizeof(new_path) - 1);
        new_path[sizeof(new_path) - 1] = 0;
        if (strcmp(st->current_dir, "/") != 0) {
            strcat(new_path, "/");
        }
        strncat(new_path, path, sizeof(new_path) - strlen(new_path) - 1);
        
        idx = vfs_resolve_path(new_path);
        if (idx >= 0) {
            /* Check if it's a directory */
            if (vfs_get_type(new_path) == VFS_DIR) {
                strncpy(st->current_dir, new_path, VFS_NAME_MAX - 1);
                st->current_dir[VFS_NAME_MAX - 1] = 0;
            } else {
                term_line(st, "cd: not a directory");
                return;
            }
        } else {
            term_line(st, "cd: directory not found");
            return;
        }
    }
    term_append(st, "Changed to: ");
    term_line(st, st->current_dir);
}

static void term_mkdir(struct terminal_state *st, const char *name) {
    char full_path[VFS_NAME_MAX * 2];
    while (*name == ' ') name++;
    if (!*name) {
        term_line(st, "usage: mkdir <directory>");
        return;
    }
    if (name[0] != '/') {
        strncpy(full_path, st->current_dir, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = 0;
        if (strcmp(st->current_dir, "/") != 0) {
            strcat(full_path, "/");
        }
        strncat(full_path, name, sizeof(full_path) - strlen(full_path) - 1);
    } else {
        strncpy(full_path, name, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = 0;
    }
    if (vfs_mkdir(full_path) >= 0) {
        term_append(st, "Created directory: ");
        term_line(st, name);
    } else {
        term_line(st, "mkdir: failed to create directory");
    }
}

static void term_touch(struct terminal_state *st, const char *name) {
    char full_path[VFS_NAME_MAX * 2];
    while (*name == ' ') name++;
    if (!*name) {
        term_line(st, "usage: touch <file>");
        return;
    }
    if (name[0] != '/') {
        strncpy(full_path, st->current_dir, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = 0;
        if (strcmp(st->current_dir, "/") != 0) {
            strcat(full_path, "/");
        }
        strncat(full_path, name, sizeof(full_path) - strlen(full_path) - 1);
    } else {
        strncpy(full_path, name, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = 0;
    }
    if (vfs_create(full_path) >= 0) {
        term_append(st, "Created file: ");
        term_line(st, name);
    } else {
        term_line(st, "touch: failed to create file");
    }
}

static void term_rm(struct terminal_state *st, const char *name) {
    char full_path[VFS_NAME_MAX * 2];
    while (*name == ' ') name++;
    if (!*name) {
        term_line(st, "usage: rm <file>");
        return;
    }
    if (name[0] != '/') {
        strncpy(full_path, st->current_dir, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = 0;
        if (strcmp(st->current_dir, "/") != 0) {
            strcat(full_path, "/");
        }
        strncat(full_path, name, sizeof(full_path) - strlen(full_path) - 1);
    } else {
        strncpy(full_path, name, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = 0;
    }
    if (vfs_delete(full_path) >= 0) {
        term_append(st, "Deleted: ");
        term_line(st, name);
    } else {
        term_line(st, "rm: failed to delete (may not exist or directory not empty)");
    }
}

static void term_mv(struct terminal_state *st, const char *src __attribute__((unused)), const char *dst __attribute__((unused))) {
    term_append(st, "mv: function temporarily disabled");
    term_line(st, "Use copy and delete instead");
}

static void term_cp(struct terminal_state *st, const char *src __attribute__((unused)), const char *dst __attribute__((unused))) {
    term_append(st, "cp: function temporarily disabled");
    term_line(st, "Use manual copy via cat and echo");
}

static void term_head(struct terminal_state *st, const char *name) {
    char buf[257];
    int fd;
    int n;
    while (*name == ' ') name++;
    if (!*name) {
        term_line(st, "usage: head <file>");
        return;
    }
    fd = vfs_fopen(name, "r");
    if (fd < 0) {
        term_line(st, "head: file not found");
        return;
    }
    n = vfs_fread(fd, buf, sizeof(buf) - 1);
    vfs_fclose(fd);
    if (n < 0) {
        term_line(st, "head: read failed");
        return;
    }
    if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
    buf[n] = 0;
    term_append(st, buf);
    if (n == 0 || buf[n - 1] != '\n') {
        term_append_char(st, '\n');
    }
}

static void term_tail(struct terminal_state *st, const char *name) {
    /* Simplified: just read last 256 bytes - basic implementation */
    char buf[257];
    int fd;
    int n;
    while (*name == ' ') name++;
    if (!*name) {
        term_line(st, "usage: tail <file>");
        return;
    }
    fd = vfs_fopen(name, "r");
    if (fd < 0) {
        term_line(st, "tail: file not found");
        return;
    }
    /* For now, just read from start - full tail needs seek support */
    n = vfs_fread(fd, buf, sizeof(buf) - 1);
    vfs_fclose(fd);
    if (n < 0) {
        term_line(st, "tail: read failed");
        return;
    }
    buf[n] = 0;
    term_append(st, buf);
    if (n == 0 || buf[n - 1] != '\n') {
        term_append_char(st, '\n');
    }
}

static void term_wc(struct terminal_state *st, const char *name) {
    char buf[1025];
    int fd;
    int n;
    int lines = 0, words = 0, chars = 0;
    bool in_word = false;
    int i;
    
    while (*name == ' ') name++;
    if (!*name) {
        term_line(st, "usage: wc <file>");
        return;
    }
    fd = vfs_fopen(name, "r");
    if (fd < 0) {
        term_line(st, "wc: file not found");
        return;
    }
    while ((n = vfs_fread(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = 0;
        for (i = 0; i < n; i++) {
            chars++;
            if (buf[i] == '\n') lines++;
            if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n') {
                in_word = false;
            } else if (!in_word) {
                in_word = true;
                words++;
            }
        }
    }
    vfs_fclose(fd);
    
    term_append(st, "  ");
    decimal(lines, buf, sizeof(buf));
    term_append(st, buf);
    term_append(st, "  ");
    decimal(words, buf, sizeof(buf));
    term_append(st, buf);
    term_append(st, "  ");
    decimal(chars, buf, sizeof(buf));
    term_line(st, buf);
}

static void term_grep(struct terminal_state *st, const char *pattern, const char *filename) {
    char buf[1025];
    int fd;
    int n;
    int line_num = 0;
    char *line_start;
    
    if (!pattern || !filename) {
        term_line(st, "usage: grep <pattern> <file>");
        return;
    }
    
    fd = vfs_fopen(filename, "r");
    if (fd < 0) {
        term_line(st, "grep: file not found");
        return;
    }
    
    while ((n = vfs_fread(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = 0;
        line_start = buf;
        while (*line_start) {
            char *line_end = strchr(line_start, '\n');
            if (line_end) {
                *line_end = 0;
                line_num++;
                if (strstr(line_start, pattern)) {
                    decimal(line_num, buf, sizeof(buf));
                    term_append(st, buf);
                    term_append(st, ":");
                    term_line(st, line_start);
                }
                line_start = line_end + 1;
            } else {
                if (strstr(line_start, pattern)) {
                    decimal(line_num, buf, sizeof(buf));
                    term_append(st, buf);
                    term_append(st, ":");
                    term_line(st, line_start);
                }
                break;
            }
        }
    }
    vfs_fclose(fd);
}

static void term_find(struct terminal_state *st, const char *pattern) {
    struct vfs_dirent ents[32];
    int i;
    int count = vfs_list(ents, 32);
    
    term_append(st, "Searching for: ");
    term_line(st, pattern);
    
    for (i = 0; i < count; i++) {
        if (strstr(ents[i].name, pattern)) {
            term_append(st, ents[i].type == VFS_DIR ? "[DIR]  " : "[FILE] ");
            term_line(st, ents[i].name);
        }
    }
}

static void term_sort(struct terminal_state *st, const char *filename) {
    char buf[1025];
    int fd;
    int n;
    char *lines[64];
    int line_count = 0;
    int i, j;
    
    if (!filename) {
        term_line(st, "usage: sort <file>");
        return;
    }
    
    fd = vfs_fopen(filename, "r");
    if (fd < 0) {
        term_line(st, "sort: file not found");
        return;
    }
    
    n = vfs_fread(fd, buf, sizeof(buf) - 1);
    vfs_fclose(fd);
    if (n < 0) {
        term_line(st, "sort: read failed");
        return;
    }
    buf[n] = 0;
    
    /* Split into lines */
    lines[line_count++] = buf;
    for (i = 0; i < n && line_count < 64; i++) {
        if (buf[i] == '\n') {
            buf[i] = 0;
            if (i + 1 < n) {
                lines[line_count++] = &buf[i + 1];
            }
        }
    }
    
    /* Simple bubble sort */
    for (i = 0; i < line_count - 1; i++) {
        for (j = 0; j < line_count - i - 1; j++) {
            if (strcmp(lines[j], lines[j + 1]) > 0) {
                char *temp = lines[j];
                lines[j] = lines[j + 1];
                lines[j + 1] = temp;
            }
        }
    }
    
    /* Output sorted lines */
    for (i = 0; i < line_count; i++) {
        term_line(st, lines[i]);
    }
}

static void term_history(struct terminal_state *st) {
    int i;
    term_line(st, "Command history:");
    for (i = 0; i < st->history_count; i++) {
        char num[8];
        decimal(i + 1, num, sizeof(num));
        term_append(st, "  ");
        term_append(st, num);
        term_append(st, "  ");
        term_line(st, st->history[i]);
    }
}

static void term_man(struct terminal_state *st, const char *topic) {
    if (!topic || !*topic) {
        term_line(st, "Available commands:");
        term_line(st, "  help        - Show this help");
        term_line(st, "  ls          - List files in current directory");
        term_line(st, "  cd <dir>    - Change directory (.. for parent, / for root)");
        term_line(st, "  pwd         - Print working directory");
        term_line(st, "  mkdir <dir> - Create directory");
        term_line(st, "  touch <file>- Create file");
        term_line(st, "  rm <file>   - Delete file or empty directory");
        term_line(st, "  cat <file>  - View file contents");
        term_line(st, "  head <file> - View first lines");
        term_line(st, "  tail <file> - View last lines");
        term_line(st, "  wc <file>   - Word count");
        term_line(st, "  grep <p> <f>- Search in file");
        term_line(st, "  find <pattern> - Search files");
        term_line(st, "  sort <file> - Sort file lines");
        term_line(st, "  date        - Show date/time");
        term_line(st, "  clear       - Clear screen");
        term_line(st, "  history     - Command history");
        term_line(st, "  about       - About this system");
        term_line(st, "  hardware    - Hardware info");
        term_line(st, "  echo <text> - Echo text");
        term_line(st, "  man <cmd>   - Manual for command");
        term_line(st, "");
        term_line(st, "File system supports hierarchical paths like Windows:");
        term_line(st, "  /Documents/notes.txt");
        term_line(st, "  /Windows/System32/kernel.sys");
        return;
    }
    
    if (strcmp(topic, "ls") == 0) {
        term_line(st, "ls - List directory contents");
        term_line(st, "Usage: ls");
    } else if (strcmp(topic, "cd") == 0) {
        term_line(st, "cd - Change directory");
        term_line(st, "Usage: cd <directory>");
        term_line(st, "Use '..' for parent, '/' for root");
        term_line(st, "Supports relative and absolute paths");
    } else if (strcmp(topic, "cat") == 0) {
        term_line(st, "cat - Concatenate and display files");
        term_line(st, "Usage: cat <filename>");
    } else if (strcmp(topic, "grep") == 0) {
        term_line(st, "grep - Search for patterns in files");
        term_line(st, "Usage: grep <pattern> <filename>");
    } else {
        term_append(st, "No manual entry for ");
        term_line(st, topic);
    }
}

static void term_execute(struct terminal_state *st) {
    char cmd_copy[TERM_INPUT_CAP];
    char *args[8];
    int argc;
    const char *cmd;
    
    strncpy(cmd_copy, st->input, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = 0;
    trim_whitespace(cmd_copy);
    cmd = cmd_copy;
    
    term_append(st, "\n");
    if (cmd[0] == 0) {
        return;
    }
    
    /* Add to history */
    add_to_history(st, cmd);
    
    argc = split_args(cmd_copy, args, 8);
    if (argc == 0) return;
    
    if (strcmp(args[0], "help") == 0 || strcmp(args[0], "?") == 0) {
        term_man(st, NULL);
    } else if (strcmp(args[0], "ls") == 0) {
        term_list(st);
    } else if (strcmp(args[0], "cd") == 0) {
        if (argc > 1) term_cd(st, args[1]);
        else term_cd(st, "/");
    } else if (strcmp(args[0], "pwd") == 0) {
        term_pwd(st);
    } else if (strcmp(args[0], "mkdir") == 0) {
        if (argc > 1) term_mkdir(st, args[1]);
        else term_line(st, "usage: mkdir <directory>");
    } else if (strcmp(args[0], "touch") == 0) {
        if (argc > 1) term_touch(st, args[1]);
        else term_line(st, "usage: touch <file>");
    } else if (strcmp(args[0], "rm") == 0) {
        if (argc > 1) term_rm(st, args[1]);
        else term_line(st, "usage: rm <file>");
    } else if (strcmp(args[0], "mv") == 0) {
        if (argc > 2) term_mv(st, args[1], args[2]);
        else term_line(st, "usage: mv <src> <dst>");
    } else if (strcmp(args[0], "cp") == 0) {
        if (argc > 2) term_cp(st, args[1], args[2]);
        else term_line(st, "usage: cp <src> <dst>");
    } else if (strcmp(args[0], "cat") == 0) {
        if (argc > 1) term_cat(st, args[1]);
        else term_line(st, "usage: cat <file>");
    } else if (strcmp(args[0], "head") == 0) {
        if (argc > 1) term_head(st, args[1]);
        else term_line(st, "usage: head <file>");
    } else if (strcmp(args[0], "tail") == 0) {
        if (argc > 1) term_tail(st, args[1]);
        else term_line(st, "usage: tail <file>");
    } else if (strcmp(args[0], "wc") == 0) {
        if (argc > 1) term_wc(st, args[1]);
        else term_line(st, "usage: wc <file>");
    } else if (strcmp(args[0], "grep") == 0) {
        if (argc > 2) term_grep(st, args[1], args[2]);
        else term_line(st, "usage: grep <pattern> <file>");
    } else if (strcmp(args[0], "find") == 0) {
        if (argc > 1) term_find(st, args[1]);
        else term_line(st, "usage: find <pattern>");
    } else if (strcmp(args[0], "sort") == 0) {
        if (argc > 1) term_sort(st, args[1]);
        else term_line(st, "usage: sort <file>");
    } else if (strcmp(args[0], "date") == 0) {
        term_date(st);
    } else if (strcmp(args[0], "time") == 0) {
        term_date(st);
    } else if (strcmp(args[0], "hardware") == 0) {
        term_line(st, "CPU: i386-compatible 32-bit");
        term_number_line(st, "Display width: ", gfx_width());
        term_number_line(st, "Display height: ", gfx_height());
        term_line(st, "Filesystem: CursorFS (ramdisk/ATA)");
        term_append(st, "Timer frequency: ");
        term_append_hex(st, 1193180 / 200);
        term_line(st, " Hz");
    } else if (strcmp(args[0], "about") == 0) {
        term_line(st, "KryspinOS - a small 32-bit graphical OS");
        term_line(st, "Built-in shell for files, date, and diagnostics.");
        term_line(st, "Enhanced with double buffering and many commands.");
    } else if (strcmp(args[0], "clear") == 0 || strcmp(args[0], "cls") == 0) {
        st->output_len = 0;
        st->output[0] = 0;
    } else if (strcmp(args[0], "history") == 0) {
        term_history(st);
    } else if (strcmp(args[0], "man") == 0) {
        if (argc > 1) term_man(st, args[1]);
        else term_man(st, NULL);
    } else if (strcmp(args[0], "echo") == 0) {
        if (argc > 1) {
            int i;
            for (i = 1; i < argc; i++) {
                if (i > 1) term_append(st, " ");
                term_append(st, args[i]);
            }
            term_line(st, "");
        } else {
            term_line(st, "");
        }
    } else if (strcmp(args[0], "exit") == 0) {
        term_line(st, "Use the window close button instead.");
    } else if (strcmp(args[0], "reboot") == 0) {
        term_line(st, "Rebooting...");
        /* Would call reboot function here */
    } else if (strcmp(args[0], "shutdown") == 0) {
        term_line(st, "Shutting down...");
        /* Would call shutdown function here */
    } else {
        term_line(st, "command not found (try help)");
    }
}

static void terminal_paint(struct window *w) {
    struct terminal_state *st = (struct terminal_state *)w->data;
    i32 x = w->x + 10;
    i32 y = w->y + WM_TITLE_H + 10;
    i32 bottom = w->y + w->h - 14;
    u32 i;
    char prompt[VFS_NAME_MAX + 16];
    
    gfx_rect(w->x + 1, w->y + WM_TITLE_H, w->w - 2, w->h - WM_TITLE_H - 1,
             COLOR_RGB(11, 18, 28));
    for (i = 0; i < st->output_len && y <= bottom; i++) {
        char c = st->output[i];
        if (c == '\n') {
            x = w->x + 10;
            y += 10;
        } else {
            if (x > w->x + w->w - 16) {
                x = w->x + 10;
                y += 10;
            }
            if (y <= bottom) {
                gfx_char(x, y, c, COLOR_RGB(166, 224, 173), 0xFFFFFFFF);
            }
            x += 8;
        }
    }
    if (y <= bottom) {
        /* Show current directory in prompt */
        strncpy(prompt, st->current_dir, sizeof(prompt) - 1);
        prompt[sizeof(prompt) - 1] = 0;
        strcat(prompt, " $ ");
        gfx_text_transparent(x, y, prompt, COLOR_RGB(110, 190, 255));
        gfx_text_transparent(x + strlen(prompt) * 8, y, st->input, COLOR_RGB(236, 238, 242));
    }
}

static void terminal_key(struct window *w, char c) {
    struct terminal_state *st = (struct terminal_state *)w->data;
    if (c == '\b') {
        if (st->input_len) {
            st->input[--st->input_len] = 0;
        }
        return;
    }
    if (c == '\n') {
        term_append(st, "$ ");
        term_append(st, st->input);
        term_execute(st);
        st->input_len = 0;
        st->input[0] = 0;
        return;
    }
    if (c >= 32 && c < 127 && st->input_len + 1 < TERM_INPUT_CAP) {
        st->input[st->input_len++] = c;
        st->input[st->input_len] = 0;
    }
}

void terminal_setup(struct window *w) {
    struct terminal_state *st = kcalloc(1, sizeof(*st));
    if (!st) {
        return; /* Allocation failed */
    }
    w->data = st;
    w->paint = terminal_paint;
    w->key = terminal_key;
    w->click = NULL;
    strncpy(st->current_dir, "/", VFS_NAME_MAX - 1);
    st->current_dir[VFS_NAME_MAX - 1] = 0;
    st->history_count = 0;
    st->history_index = 0;
    term_line(st, "KryspinOS Terminal v2.0");
    term_line(st, "Enhanced with double buffering and many commands.");
    term_line(st, "Type 'help' for available commands.");
}