#include <apps.h>
#include <window.h>
#include <vfs.h>
#include <gfx.h>
#include <rtc.h>
#include <string.h>
#include <heap.h>

#define TERM_OUTPUT_CAP 3072
#define TERM_INPUT_CAP 96

struct terminal_state {
    char output[TERM_OUTPUT_CAP];
    u32 output_len;
    char input[TERM_INPUT_CAP];
    u32 input_len;
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

static void term_list(struct terminal_state *st) {
    struct vfs_dirent ents[32];
    int i;
    int count = vfs_list(ents, 32);
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

static void term_execute(struct terminal_state *st) {
    const char *cmd = st->input;
    term_append(st, "\n");
    if (cmd[0] == 0) {
        return;
    }
    if (strcmp(cmd, "help") == 0) {
        term_line(st, "help  ls  cat <file>  date  hardware");
        term_line(st, "clear  about  echo <text>");
    } else if (strcmp(cmd, "ls") == 0) {
        term_list(st);
    } else if (strcmp(cmd, "date") == 0) {
        term_date(st);
    } else if (strcmp(cmd, "hardware") == 0) {
        term_line(st, "CPU: i386-compatible 32-bit");
        term_number_line(st, "Display width: ", gfx_width());
        term_number_line(st, "Display height: ", gfx_height());
        term_line(st, "Filesystem: CursorFS (ramdisk/ATA)");
    } else if (strcmp(cmd, "about") == 0) {
        term_line(st, "KryspinOS - a small 32-bit graphical OS");
        term_line(st, "Built-in shell for files, date, and diagnostics.");
    } else if (strcmp(cmd, "clear") == 0) {
        st->output_len = 0;
        st->output[0] = 0;
    } else if (strncmp(cmd, "cat ", 4) == 0) {
        term_cat(st, cmd + 4);
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        term_line(st, cmd + 5);
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
        gfx_text_transparent(x, y, "$ ", COLOR_RGB(110, 190, 255));
        gfx_text_transparent(x + 16, y, st->input, COLOR_RGB(236, 238, 242));
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
    w->data = st;
    w->paint = terminal_paint;
    w->key = terminal_key;
    w->click = NULL;
    term_line(st, "KryspinOS Terminal");
    term_line(st, "Type help for available commands.");
}