#include "cfe.h"
#include "console.h"
#include "fat32.h"
#include "interrupts.h"
#include "keyboard.h"
#include "kheap.h"
#include <stdint.h>

/* Forward declaration — string compare helper used throughout */
static int cfe_str_eq(const char *a, const char *b);

/* --------------------------------------------------------------------------
 * CFE — CFOS Editor
 *
 * A vim-like in-kernel text editor for FAT32 partitions.
 *
 * Usage:  cfe <part> <file> [-kbp-vim]
 *
 * CFE style (default):
 *   Alt+key  → opens mini-command prompt (sq, fq, f, w…)
 *   Ctrl+S   → save
 *   Ctrl+X   → quit
 *   Enter    → execute typed command
 *   Escape   → cancel current mode
 *
 * VIM style (-kbp-vim):
 *   Escape   → normal mode (arrows navigate, no text insertion)
 *   i        → insert/write mode
 *   :wq      → save & quit
 *   :q!      → force quit
 *   /        → find mode
 *   n        → next match
 * -------------------------------------------------------------------------- */

#define CFE_MAX_LINES     1024
#define CFE_MAX_LINE_LEN  512
#define CFE_CMD_BUF_LEN   128
#define CFE_FIND_LEN      256

typedef enum {
    CFE_MODE_WRITE,   /* inserting text (default in CFE style)       */
    CFE_MODE_CMD,     /* typing a command shortcut after Alt prefix   */
    CFE_MODE_FIND,    /* typing find pattern                          */
    CFE_MODE_REPLACE, /* typing replace text after find               */
    CFE_MODE_NORMAL,  /* vim normal mode (navigation only)            */
    CFE_MODE_VISUAL,  /* vim visual mode (selection — stub)           */
    CFE_MODE_COLON,   /* vim :command line                            */
} CFE_MODE;

typedef struct {
    char *data;
    int len;
    int capacity;
} CFE_LINE;

static CONSOLE *g_con;
static PARTITION_INFO *g_part;
static FAT32_FS g_fs;
static int g_fs_mounted;

static CFE_LINE g_lines[CFE_MAX_LINES];
static int g_line_count;

static int g_cursor_line;
static int g_cursor_col;
static int g_scroll_top;
static int g_top_col;
static int g_dirty;
static char g_filename[FAT32_NAME_MAX];

static CFE_MODE g_mode;
static CFE_CONFIG g_cfg;

static char g_cmd_buf[CFE_CMD_BUF_LEN];
static int g_cmd_pos;
/* removed: g_alt_was_pressed — Alt detection (now unused) */

static char g_find_pattern[CFE_FIND_LEN];
static int g_find_len;
static int g_find_last_line;
static int g_find_last_col;
static int g_find_active;

static char g_replace_text[CFE_FIND_LEN];
static int g_replace_len;

static char g_status_msg[256];
static int g_running;

/* removed: g_prev_modifiers, g_alt_was_pressed — Alt detection (now unused) */

/* --------------------------------------------------------------------------
 * Line helpers
 * -------------------------------------------------------------------------- */

static void cfe_init_lines(void) {
    for (int i = 0; i < CFE_MAX_LINES; i++) {
        g_lines[i].data = NULL;
        g_lines[i].len = 0;
        g_lines[i].capacity = 0;
    }
    g_line_count = 0;
}

static void cfe_ensure_line(int idx) {
    if (idx < 0 || idx >= CFE_MAX_LINES) return;
    if (g_lines[idx].data) return;
    g_lines[idx].data = kmalloc(CFE_MAX_LINE_LEN);
    if (g_lines[idx].data) {
        g_lines[idx].data[0] = '\0';
        g_lines[idx].len = 0;
        g_lines[idx].capacity = CFE_MAX_LINE_LEN;
    }
}

static void cfe_set_status(const char *msg) {
    int i = 0;
    while (msg[i] && i < 254) { g_status_msg[i] = msg[i]; i++; }
    g_status_msg[i] = '\0';
}

/* --------------------------------------------------------------------------
 * File I/O
 * -------------------------------------------------------------------------- */

static int cfe_load_file(void) {
    uint8_t *buf = NULL;
    uint32_t size = 0;

    if (!fat32_read_file(g_part, g_filename, &buf, &size)) {
        /* File doesn't exist — start with one empty line */
        cfe_ensure_line(0);
        g_line_count = 1;
        g_dirty = 0;
        cfe_set_status("[New File]");
        return 1;
    }

    cfe_init_lines();

    int line_idx = 0;
    int col = 0;

    for (uint32_t i = 0; i < size && line_idx < CFE_MAX_LINES; i++) {
        if (buf[i] == '\n') {
            cfe_ensure_line(line_idx);
            if (g_lines[line_idx].data) {
                if (col < CFE_MAX_LINE_LEN - 1)
                    g_lines[line_idx].data[col] = '\0';
                else
                    g_lines[line_idx].data[CFE_MAX_LINE_LEN - 1] = '\0';
                g_lines[line_idx].len = col;
            }
            line_idx++;
            col = 0;
        } else if (buf[i] == '\t') {
            cfe_ensure_line(line_idx);
            uint8_t ts = g_cfg.tab_size ? g_cfg.tab_size : 4;
            int spaces = ts - (col % ts);
            if (g_lines[line_idx].data && col + spaces < CFE_MAX_LINE_LEN - 1) {
                for (int s = 0; s < spaces; s++)
                    g_lines[line_idx].data[col++] = ' ';
            }
        } else {
            cfe_ensure_line(line_idx);
            if (g_lines[line_idx].data && col < CFE_MAX_LINE_LEN - 1)
                g_lines[line_idx].data[col++] = (char)buf[i];
        }
    }

    /* Last line without trailing newline */
    if (col > 0) {
        cfe_ensure_line(line_idx);
        if (g_lines[line_idx].data) {
            g_lines[line_idx].data[col] = '\0';
            g_lines[line_idx].len = col;
        }
        line_idx++;
    }

    if (line_idx == 0) {
        cfe_ensure_line(0);
        g_line_count = 1;
    } else {
        g_line_count = line_idx;
    }

    g_dirty = 0;
    kfree(buf);
    return 1;
}

static int cfe_save_file(void) {
    int total = 0;
    for (int i = 0; i < g_line_count; i++) {
        if (g_lines[i].data) total += g_lines[i].len;
        total++; /* newline */
    }

    if (total == 0) total = 1; /* at least one newline for empty file */

    char *buf = kmalloc(total + 1);
    if (!buf) {
        cfe_set_status("Out of memory!");
        return 0;
    }

    int pos = 0;
    for (int i = 0; i < g_line_count; i++) {
        if (g_lines[i].data) {
            for (int j = 0; j < g_lines[i].len; j++)
                buf[pos++] = g_lines[i].data[j];
        }
        buf[pos++] = '\n';
    }

    int ok = fat32_write_file(g_con, g_part, g_filename, buf, pos);

    /* If overwrite failed, file may not exist yet — create it first */
    if (!ok) {
        ok = fat32_create_file(g_con, g_part, g_filename);
        if (ok) {
            ok = fat32_write_file(g_con, g_part, g_filename, buf, pos);
        }
    }

    kfree(buf);

    if (ok) {
        g_dirty = 0;
        cfe_set_status("File saved");
    } else {
        cfe_set_status("Error saving file!");
    }
    return ok;
}

/* --------------------------------------------------------------------------
 * Cursor / scroll
 * -------------------------------------------------------------------------- */

static void cfe_move_cursor(int new_line, int new_col) {
    if (new_line < 0) new_line = 0;
    if (new_line >= CFE_MAX_LINES) new_line = CFE_MAX_LINES - 1;

    cfe_ensure_line(new_line);

    int max_col = g_lines[new_line].len;
    if (new_col < 0) new_col = 0;
    if (new_col > max_col) new_col = max_col;

    g_cursor_line = new_line;
    g_cursor_col = new_col;

    int text_rows = (int)g_con->rows - 2;
    if (text_rows < 1) text_rows = 1;

    if (g_cursor_line < g_scroll_top)
        g_scroll_top = g_cursor_line;
    else if (g_cursor_line >= g_scroll_top + text_rows)
        g_scroll_top = g_cursor_line - text_rows + 1;

    int text_cols = (int)g_con->cols - 2;
    if (g_cursor_col < g_top_col)
        g_top_col = g_cursor_col;
    else if (g_cursor_col >= g_top_col + text_cols)
        g_top_col = g_cursor_col - text_cols + 1;
}

/* --------------------------------------------------------------------------
 * Editing operations
 * -------------------------------------------------------------------------- */

static void cfe_insert_char(char c) {
    if (g_mode != CFE_MODE_WRITE) return;
    if (g_cursor_line >= CFE_MAX_LINES) return;
    cfe_ensure_line(g_cursor_line);

    CFE_LINE *line = &g_lines[g_cursor_line];
    if (!line->data) return;

    if (g_cursor_col > line->len) g_cursor_col = line->len;
    if (g_cursor_col >= CFE_MAX_LINE_LEN - 2) return;

    for (int i = line->len; i > g_cursor_col; i--)
        line->data[i] = line->data[i - 1];
    line->data[g_cursor_col] = c;
    line->len++;
    line[0].data[line->len] = '\0';
    g_cursor_col++;
    g_dirty = 1;
}

static void cfe_insert_newline(void) {
    if (g_mode != CFE_MODE_WRITE) return;
    if (g_cursor_line >= CFE_MAX_LINES - 1) return;
    cfe_ensure_line(g_cursor_line);

    CFE_LINE *old = &g_lines[g_cursor_line];
    if (!old->data) return;

    int len = old->len;
    if (g_cursor_col > len) g_cursor_col = len;

    char *new_data = kmalloc(CFE_MAX_LINE_LEN);
    if (!new_data) return;

    int rest = len - g_cursor_col;
    for (int i = 0; i < rest; i++)
        new_data[i] = old->data[g_cursor_col + i];
    new_data[rest] = '\0';

    old->data[g_cursor_col] = '\0';
    old->len = g_cursor_col;

    /* Shift lines down */
    for (int i = g_line_count; i > g_cursor_line + 1; i--) {
        g_lines[i].data = g_lines[i - 1].data;
        g_lines[i].len = g_lines[i - 1].len;
        g_lines[i].capacity = g_lines[i - 1].capacity;
    }

    g_lines[g_cursor_line + 1].data = new_data;
    g_lines[g_cursor_line + 1].len = rest;
    g_lines[g_cursor_line + 1].capacity = CFE_MAX_LINE_LEN;

    if (g_line_count < CFE_MAX_LINES) g_line_count++;

    g_cursor_line++;
    g_cursor_col = 0;
    g_dirty = 1;
}

static void cfe_backspace(void) {
    if (g_cursor_line >= g_line_count) return;

    if (g_cursor_col > 0) {
        CFE_LINE *line = &g_lines[g_cursor_line];
        if (!line->data) return;
        for (int i = g_cursor_col - 1; i < line->len - 1; i++)
            line->data[i] = line->data[i + 1];
        line->len--;
        line->data[line->len] = '\0';
        g_cursor_col--;
        g_dirty = 1;
    } else if (g_cursor_line > 0) {
        CFE_LINE *prev = &g_lines[g_cursor_line - 1];
        CFE_LINE *cur  = &g_lines[g_cursor_line];
        if (!prev->data || !cur->data) return;

        int prev_len = prev->len;
        int cur_len = cur->len;

        if (prev_len + cur_len < CFE_MAX_LINE_LEN - 1) {
            for (int i = 0; i < cur_len; i++)
                prev->data[prev_len + i] = cur->data[i];
            prev->len = prev_len + cur_len;
            prev->data[prev->len] = '\0';
        }

        kfree(cur->data);
        cur->data = NULL;
        cur->len = 0;
        cur->capacity = 0;

        for (int i = g_cursor_line; i < g_line_count - 1; i++) {
            g_lines[i].data = g_lines[i + 1].data;
            g_lines[i].len = g_lines[i + 1].len;
            g_lines[i].capacity = g_lines[i + 1].capacity;
        }
        g_lines[g_line_count - 1].data = NULL;
        g_lines[g_line_count - 1].len = 0;
        g_lines[g_line_count - 1].capacity = 0;
        g_line_count--;

        g_cursor_line--;
        g_cursor_col = prev_len;
        g_dirty = 1;
    }
}

/* --------------------------------------------------------------------------
 * Find
 * -------------------------------------------------------------------------- */

static int cfe_find_next(void) {
    if (!g_find_active || g_find_len == 0) return 0;

    /* Start from cursor position, search forward */
    int start_line = g_cursor_line;
    int start_col = g_cursor_col + 1;

    for (int attempt = 0; attempt < g_line_count; attempt++) {
        int line = (start_line + attempt) % g_line_count;
        cfe_ensure_line(line);
        CFE_LINE *ln = &g_lines[line];
        if (!ln->data) continue;

        int search_from = (attempt == 0) ? start_col : 0;
        for (int col = search_from; col <= ln->len - g_find_len; col++) {
            int match = 1;
            for (int j = 0; j < g_find_len; j++) {
                if (ln->data[col + j] != g_find_pattern[j]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                g_find_last_line = line;
                g_find_last_col = col;
                g_cursor_line = line;
                g_cursor_col = col;
                cfe_move_cursor(line, col);
                return 1;
            }
        }
    }
    cfe_set_status("Pattern not found");
    return 0;
}

static void cfe_replace_at_cursor(void) {
    if (!g_find_active || g_replace_len == 0) return;

    CFE_LINE *line = &g_lines[g_cursor_line];
    if (!line->data) return;

    /* Verify we're still at a match position */
    int match = 1;
    for (int j = 0; j < g_find_len; j++) {
        if (g_cursor_col + j >= line->len || line->data[g_cursor_col + j] != g_find_pattern[j]) {
            match = 0;
            break;
        }
    }
    if (!match) { cfe_find_next(); return; }

    /* Delete old text, insert new */
    int old_len = g_find_len;
    int new_len = g_replace_len;
    int tail = line->len - old_len - g_cursor_col;

    /* Shift tail */
    for (int i = 0; i < tail; i++)
        line->data[g_cursor_col + new_len + i] = line->data[g_cursor_col + old_len + i];

    /* Insert replacement */
    for (int i = 0; i < new_len; i++)
        line->data[g_cursor_col + i] = g_replace_text[i];

    line->len = line->len - old_len + new_len;
    line->data[line->len] = '\0';
    g_dirty = 1;
}

/* --------------------------------------------------------------------------
 * Rendering
 * -------------------------------------------------------------------------- */

static void cfe_draw_row(int row, const char *text, int text_len, uint32_t fg, uint32_t bg) {
    uint32_t saved_fg = g_con->fg_color;
    uint32_t saved_bg = g_con->bg_color;

    console_set_colors(g_con, fg, bg);
    g_con->cursor_x = 0;
    g_con->cursor_y = (uint32_t)row;

    int col = 0;
    if (text) {
        while (col < text_len && (uint32_t)col < g_con->cols) {
            console_putchar(g_con, text[col]);
            col++;
        }
    }
    while ((uint32_t)col < g_con->cols) {
        console_putchar(g_con, ' ');
        col++;
    }

    console_set_colors(g_con, saved_fg, saved_bg);
}

static void cfe_render(void) {
    uint32_t rows = g_con->rows;
    uint32_t cols = g_con->cols;
    int text_rows = (int)rows - 2;
    if (text_rows < 1) text_rows = 1;

    /* Title bar */
    {
        char title[256];
        int tlen = 0;
        title[tlen++] = ' ';
        title[tlen++] = '~';
        title[tlen++] = ' ';
        const char *fn = g_filename;
        while (*fn && tlen < 250) title[tlen++] = *fn++;
        if (g_dirty && tlen < 250) title[tlen++] = ' ';
        if (g_dirty && tlen < 250) title[tlen++] = '*';

        /* Mode indicator */
        const char *mode_str = "";
        switch (g_mode) {
            case CFE_MODE_WRITE:  mode_str = " [WRITE]"; break;
            case CFE_MODE_CMD:    mode_str = " [CMD]"; break;
            case CFE_MODE_FIND:   mode_str = " [FIND]"; break;
            case CFE_MODE_REPLACE: mode_str = " [REPLACE]"; break;
            case CFE_MODE_NORMAL: mode_str = " [NORMAL]"; break;
            case CFE_MODE_VISUAL: mode_str = " [VISUAL]"; break;
            case CFE_MODE_COLON:  mode_str = " [COLON]"; break;
        }
        const char *mp = mode_str;
        while (*mp && tlen < 255) title[tlen++] = *mp++;

        while ((uint32_t)tlen < cols) title[tlen++] = ' ';
        title[tlen] = '\0';
        cfe_draw_row(0, title, tlen, g_cfg.title_fg, g_cfg.title_bg);
    }

    /* Text area */
    for (int r = 1; r < (int)rows - 1; r++) {
        int file_line = g_scroll_top + (r - 1);

        if (file_line >= g_line_count) {
            char empty[512];
            int el = 0;
            empty[el++] = '~';
            while ((uint32_t)el < cols && el < 511) empty[el++] = ' ';
            empty[el] = '\0';
            cfe_draw_row(r, empty, el, g_cfg.tilde_fg, g_cfg.text_bg);
        } else {
            CFE_LINE *ln = &g_lines[file_line];
            if (!ln->data || ln->len == 0) {
                char empty[2] = { '\0', '\0' };
                cfe_draw_row(r, empty, 0, g_cfg.text_fg, g_cfg.text_bg);
            } else {
                char buf[CFE_MAX_LINE_LEN];
                int bpos = 0;
                for (int c = g_top_col; c < ln->len && bpos < (int)cols && bpos < CFE_MAX_LINE_LEN - 1; c++)
                    buf[bpos++] = ln->data[c];
                buf[bpos] = '\0';

                uint32_t bg = g_cfg.text_bg;
                if (file_line == g_cursor_line) bg = g_cfg.cursor_bg;

                /* Highlight find match */
                if (g_find_active && g_find_last_line == file_line) {
                    /* We'll draw with match highlighting in a smarter way */
                }

                cfe_draw_row(r, buf, bpos, g_cfg.text_fg, bg);
            }
        }
    }

    /* Status bar / command line */
    {
        char status[256];
        int slen = 0;

        if (g_mode == CFE_MODE_CMD) {
            status[slen++] = ':';
            for (int i = 0; i < g_cmd_pos && slen < 254; i++)
                status[slen++] = g_cmd_buf[i];
            while ((uint32_t)slen < cols && slen < 255) status[slen++] = ' ';
            status[slen] = '\0';
            cfe_draw_row((int)rows - 1, status, slen, g_cfg.cmd_fg, g_cfg.cmd_bg);
        } else if (g_mode == CFE_MODE_FIND) {
            status[slen++] = '/';
            for (int i = 0; i < g_find_len && slen < 254; i++)
                status[slen++] = g_find_pattern[i];
            while ((uint32_t)slen < cols && slen < 255) status[slen++] = ' ';
            status[slen] = '\0';
            cfe_draw_row((int)rows - 1, status, slen, g_cfg.cmd_fg, g_cfg.cmd_bg);
        } else if (g_mode == CFE_MODE_REPLACE) {
            status[slen++] = 'R';
            status[slen++] = '/';
            for (int i = 0; i < g_replace_len && slen < 254; i++)
                status[slen++] = g_replace_text[i];
            while ((uint32_t)slen < cols && slen < 255) status[slen++] = ' ';
            status[slen] = '\0';
            cfe_draw_row((int)rows - 1, status, slen, g_cfg.cmd_fg, g_cfg.cmd_bg);
        } else if (g_mode == CFE_MODE_COLON) {
            status[slen++] = ':';
            for (int i = 0; i < g_cmd_pos && slen < 254; i++)
                status[slen++] = g_cmd_buf[i];
            while ((uint32_t)slen < cols && slen < 255) status[slen++] = ' ';
            status[slen] = '\0';
            cfe_draw_row((int)rows - 1, status, slen, g_cfg.cmd_fg, g_cfg.cmd_bg);
        } else {
            /* Normal status bar */
            const char *msg = g_status_msg;
            if (!msg[0]) {
                /* Cursor position + dirty */
                char pos[64];
                int p = 0;
                pos[p++] = 'L';
                /* Simple int to string */
                int ln = g_cursor_line + 1;
                char tmp[16];
                int ti = 0;
                do { tmp[ti++] = (char)('0' + (ln % 10)); ln /= 10; } while (ln > 0);
                while (ti > 0) pos[p++] = tmp[--ti];
                pos[p++] = ' ';
                pos[p++] = 'C';
                int cl = g_cursor_col + 1;
                ti = 0;
                do { tmp[ti++] = (char)('0' + (cl % 10)); cl /= 10; } while (cl > 0);
                while (ti > 0) pos[p++] = tmp[--ti];
                pos[p] = '\0';

                int ml = 0;
                while (pos[ml]) { status[ml] = pos[ml]; ml++; }
                if (g_dirty && ml < 254) status[ml++] = ' ';
                if (g_dirty && ml < 254) status[ml++] = '*';
                while ((uint32_t)ml < cols && ml < 255) status[ml++] = ' ';
                status[ml] = '\0';
            } else {
                int ml = 0;
                while (msg[ml] && ml < 254) status[ml] = msg[ml]; ml++;
                while ((uint32_t)ml < cols && ml < 255) status[ml++] = ' ';
                status[ml] = '\0';
            }
            cfe_draw_row((int)rows - 1, status, slen > 0 ? slen : 256, g_cfg.status_fg, g_cfg.status_bg);
        }
    }
}

/* --------------------------------------------------------------------------
 * Command execution
 * -------------------------------------------------------------------------- */

static int cfe_lookup_command(const char *cmd) {
    for (int i = 0; i < g_cfg.keybind_count; i++)
        if (cfe_str_eq(g_cfg.keybinds[i].shortcut, cmd))
            return i;
    return -1;
}

static void cfe_execute_command(void) {
    int idx = cfe_lookup_command(g_cmd_buf);
    if (idx < 0) {
        cfe_set_status("Unknown command");
        g_mode = CFE_MODE_WRITE;
        g_cmd_pos = 0;
        g_cmd_buf[0] = '\0';
        return;
    }

    CFE_COMMAND action = g_cfg.keybinds[idx].action;
    switch (action) {
        case CFE_CMD_SAVE_QUIT:
            cfe_save_file();
            g_running = 0;
            break;
        case CFE_CMD_FORCE_QUIT:
            g_running = 0;
            break;
        case CFE_CMD_FIND_MODE:
            g_mode = CFE_MODE_FIND;
            g_find_len = 0;
            g_find_pattern[0] = '\0';
            break;
        case CFE_CMD_WRITE_MODE:
            g_mode = CFE_MODE_WRITE;
            cfe_set_status("Write mode");
            break;
        case CFE_CMD_SAVE:
            cfe_save_file();
            g_mode = CFE_MODE_WRITE;
            break;
        case CFE_CMD_QUIT:
            g_running = 0;
            break;
        case CFE_CMD_GOTO_LINE:
            /* Parse line number from command buffer (after the first char) */
            break;
        case CFE_CMD_NEXT_MATCH:
            cfe_find_next();
            break;
        default:
            break;
    }

    g_cmd_pos = 0;
    g_cmd_buf[0] = '\0';
}

/* --------------------------------------------------------------------------
 * Vim-style colon command parsing
 * -------------------------------------------------------------------------- */

static void cfe_execute_colon_cmd(void) {
    /* Parse :wq, :q!, :q, :N */
    if (g_cmd_pos == 0) { g_mode = CFE_MODE_WRITE; return; }

    if (g_cmd_pos == 2 && g_cmd_buf[0] == 'w' && g_cmd_buf[1] == 'q') {
        if (cfe_save_file()) {
            g_running = 0;
        }
    } else if (g_cmd_pos == 2 && g_cmd_buf[0] == 'q' && g_cmd_buf[1] == '!') {
        g_running = 0;
    } else if (g_cmd_pos == 1 && g_cmd_buf[0] == 'q') {
        if (g_dirty) {
            cfe_set_status("No write since last change (use :q! to override)");
            return;
        }
        g_running = 0;
    } else {
        /* Try to parse as line number */
        int ln = 0;
        int valid = 1;
        for (int i = 0; i < g_cmd_pos; i++) {
            if (g_cmd_buf[i] < '0' || g_cmd_buf[i] > '9') { valid = 0; break; }
            ln = ln * 10 + (g_cmd_buf[i] - '0');
        }
        if (valid && ln > 0) {
            cfe_move_cursor(ln - 1, 0);
            g_mode = CFE_MODE_WRITE;
            return;
        }
        cfe_set_status("Unknown command");
    }

    g_mode = CFE_MODE_WRITE;
    g_cmd_pos = 0;
    g_cmd_buf[0] = '\0';
}

/* --------------------------------------------------------------------------
 * Find mode input
 * -------------------------------------------------------------------------- */

static void cfe_handle_find_input(char ch) {
    if (ch == '\n' || ch == '\r') {
        if (g_find_len > 0) {
            g_find_active = 1;
            g_find_pattern[g_find_len] = '\0';
            cfe_find_next();
        }
        g_mode = CFE_MODE_WRITE;
        g_cmd_pos = 0;
    } else if (ch == '\b' || ch == 0x7F) {
        if (g_find_len > 0) g_find_len--;
    } else if ((unsigned char)ch == KEY_ESCAPE) {
        g_mode = CFE_MODE_WRITE;
        g_find_len = 0;
    } else if (g_find_len < CFE_FIND_LEN - 1 && ch >= 0x20 && ch < 0x7F) {
        g_find_pattern[g_find_len++] = ch;
    }
}

static void cfe_handle_replace_input(char ch) {
    if (ch == '\n' || ch == '\r') {
        g_replace_text[g_replace_len] = '\0';
        cfe_replace_at_cursor();
        g_mode = CFE_MODE_WRITE;
        g_cmd_pos = 0;
    } else if (ch == '\b' || ch == 0x7F) {
        if (g_replace_len > 0) g_replace_len--;
    } else if ((unsigned char)ch == KEY_ESCAPE) {
        g_mode = CFE_MODE_WRITE;
        g_replace_len = 0;
    } else if (g_replace_len < CFE_FIND_LEN - 1 && ch >= 0x20 && ch < 0x7F) {
        g_replace_text[g_replace_len++] = ch;
    }
}

/* --------------------------------------------------------------------------
 * Main input dispatch
 * -------------------------------------------------------------------------- */

static void cfe_handle_write_input(char ch, uint8_t mods) {
    if (mods & MOD_CTRL) {
        /* Ctrl+S (0x13) = save */
        if (ch == 0x13) {
            cfe_save_file();
            return;
        }
        /* Ctrl+X (0x18) = quit */
        if (ch == 0x18) {
            g_running = 0;
            return;
        }
        /* Ctrl+other — ignore or handle as regular char */
    }

    if ((unsigned char)ch >= 0x80) {
        /* Special keys */
        switch ((unsigned char)ch) {
            case KEY_ARROW_UP:
                cfe_move_cursor(g_cursor_line - 1, g_cursor_col);
                break;
            case KEY_ARROW_DOWN:
                cfe_move_cursor(g_cursor_line + 1, g_cursor_col);
                break;
            case KEY_ARROW_LEFT:
                if (g_cursor_col > 0)
                    cfe_move_cursor(g_cursor_line, g_cursor_col - 1);
                else if (g_cursor_line > 0) {
                    int prev_len = g_lines[g_cursor_line - 1].len;
                    cfe_move_cursor(g_cursor_line - 1, prev_len);
                }
                break;
            case KEY_ARROW_RIGHT:
                {
                    CFE_LINE *ln = &g_lines[g_cursor_line];
                    if (ln->data && g_cursor_col < ln->len)
                        cfe_move_cursor(g_cursor_line, g_cursor_col + 1);
                    else if (g_cursor_line + 1 < g_line_count)
                        cfe_move_cursor(g_cursor_line + 1, 0);
                }
                break;
            default:
                break;
        }
        return;
    }

    if (ch == '\n' || ch == '\r') {
        cfe_insert_newline();
    } else if (ch == '\b' || ch == 0x7F) {
        cfe_backspace();
    } else if ((unsigned char)ch == KEY_ESCAPE) {
        if (g_cfg.vim_preset) {
            g_mode = CFE_MODE_NORMAL;
        }
        /* else: Escape is just Escape in CFE mode — do nothing */
    } else if (ch >= 0x20 && ch < 0x7F) {
        cfe_insert_char(ch);
    }
}

static void cfe_handle_normal_input(char ch) {
    /* Vim normal mode: arrows navigate, i=insert, :=colon, /=find */
    if ((unsigned char)ch >= 0x80) {
        switch ((unsigned char)ch) {
            case KEY_ARROW_UP:    cfe_move_cursor(g_cursor_line - 1, g_cursor_col); break;
            case KEY_ARROW_DOWN:  cfe_move_cursor(g_cursor_line + 1, g_cursor_col); break;
            case KEY_ARROW_LEFT:
                if (g_cursor_col > 0)
                    cfe_move_cursor(g_cursor_line, g_cursor_col - 1);
                else if (g_cursor_line > 0)
                    cfe_move_cursor(g_cursor_line - 1, g_lines[g_cursor_line - 1].len);
                break;
            case KEY_ARROW_RIGHT:
                {
                    CFE_LINE *ln = &g_lines[g_cursor_line];
                    if (ln->data && g_cursor_col < ln->len)
                        cfe_move_cursor(g_cursor_line, g_cursor_col + 1);
                    else if (g_cursor_line + 1 < g_line_count)
                        cfe_move_cursor(g_cursor_line + 1, 0);
                }
                break;
            default: break;
        }
        return;
    }

    switch (ch) {
        case 'i': case 'a': case 'o':
            g_mode = CFE_MODE_WRITE;
            if (ch == 'o') {
                cfe_move_cursor(g_cursor_line + 1, 0);
                cfe_insert_newline();
            }
            cfe_set_status("Write mode");
            break;
        case ':':
            g_mode = CFE_MODE_COLON;
            g_cmd_pos = 0;
            g_cmd_buf[0] = '\0';
            break;
        case '/':
            g_mode = CFE_MODE_FIND;
            g_find_len = 0;
            g_find_pattern[0] = '\0';
            break;
        case 'n':
            cfe_find_next();
            break;
        case '\b': case 0x7F:
            /* Backspace = move cursor left */
            if (g_cursor_col > 0)
                cfe_move_cursor(g_cursor_line, g_cursor_col - 1);
            else if (g_cursor_line > 0)
                cfe_move_cursor(g_cursor_line - 1, g_lines[g_cursor_line - 1].len);
            break;
        default:
            /* Unknown key — ignore in normal mode */
            break;
    }
}

static void cfe_handle_cmd_input(char ch) {
    if (ch == '\n' || ch == '\r') {
        g_cmd_buf[g_cmd_pos] = '\0';
        cfe_execute_command();
    } else if (ch == '\b' || ch == 0x7F) {
        if (g_cmd_pos > 0) g_cmd_pos--;
    } else if ((unsigned char)ch == KEY_ESCAPE) {
        g_mode = CFE_MODE_WRITE;
        g_cmd_pos = 0;
        g_cmd_buf[0] = '\0';
        cfe_set_status("Cancelled");
    } else if (g_cmd_pos < CFE_CMD_BUF_LEN - 1 && ch >= 0x20 && ch < 0x7F) {
        g_cmd_buf[g_cmd_pos++] = ch;
    }
}

static void cfe_handle_colon_input(char ch) {
    if (ch == '\n' || ch == '\r') {
        g_cmd_buf[g_cmd_pos] = '\0';
        cfe_execute_colon_cmd();
    } else if (ch == '\b' || ch == 0x7F) {
        if (g_cmd_pos > 0) g_cmd_pos--;
    } else if ((unsigned char)ch == KEY_ESCAPE) {
        g_mode = CFE_MODE_WRITE;
        g_cmd_pos = 0;
        g_cmd_buf[0] = '\0';
    } else if (g_cmd_pos < CFE_CMD_BUF_LEN - 1 && ch >= 0x20 && ch < 0x7F) {
        g_cmd_buf[g_cmd_pos++] = ch;
    }
}

/* --------------------------------------------------------------------------
 * Cleanup
 * -------------------------------------------------------------------------- */

static void cfe_free_all(void) {
    for (int i = 0; i < CFE_MAX_LINES; i++) {
        if (g_lines[i].data) { kfree(g_lines[i].data); g_lines[i].data = NULL; }
        g_lines[i].len = 0;
        g_lines[i].capacity = 0;
    }
}

/* --------------------------------------------------------------------------
 * str_equal for command lookup (defined in cfe_config.c, need local copy)
 * -------------------------------------------------------------------------- */

static int cfe_str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return (*a == '\0' && *b == '\0');
}

/* --------------------------------------------------------------------------
 * Main entry
 * -------------------------------------------------------------------------- */

void cfe_run(CONSOLE *con, PARTITION_INFO *part, const char *filename, int vim_preset) {
    if (!con || !part || !filename) return;

    /* Mount filesystem */
    if (!fat32_mount(part, &g_fs)) {
        console_printf(con, "cfe: cannot mount FAT32 on %s\n", part->name);
        return;
    }
    g_fs_mounted = 1;

    /* Copy globals */
    g_con = con;
    g_part = part;
    int flen = 0;
    while (filename[flen] && flen < FAT32_NAME_MAX - 1) {
        g_filename[flen] = filename[flen];
        flen++;
    }
    g_filename[flen] = '\0';

    /* Load config */
    if (vim_preset)
        cfe_load_preset_vim(&g_cfg);
    else
        cfe_load_defaults(&g_cfg);
    cfe_try_load_config_file(&g_cfg, part);

    /* Load file */
    cfe_init_lines();
    cfe_load_file();

    /* Init state */
    g_cursor_line = 0;
    g_cursor_col = 0;
    g_scroll_top = 0;
    g_top_col = 0;
    g_dirty = 0;
    g_status_msg[0] = '\0';
    g_mode = vim_preset ? CFE_MODE_NORMAL : CFE_MODE_WRITE;
    g_running = 1;
    g_cmd_pos = 0;
    g_cmd_buf[0] = '\0';
    g_find_len = 0;
    g_find_active = 0;
    g_replace_len = 0;

    console_clear(con);

    /* Flush any stale keyboard input so the shell doesn't pick up leftover keys */
    while (keyboard_buffer_has_data()) {
        (void)keyboard_buffer_getchar();
    }

    cfe_render();

    /* Main input loop — hlt until IRQ wakes us, then process key */
    while (g_running) {
        interrupts_enable();

        /* Block with hlt so we don't spin and flicker */
        while (!keyboard_buffer_has_data()) {
            interrupts_enable();
            __asm__ __volatile__("hlt");
        }

        char ch = keyboard_buffer_getchar();
        uint8_t mods = keyboard_get_modifiers();

        /* CFE style: Ctrl+S = save, Ctrl+X = quit */
        if (!g_cfg.vim_preset && (mods & MOD_CTRL) && ch != 0) {
            if (ch == 0x13) {
                interrupts_enable();
                cfe_save_file();
                g_cmd_pos = 0;
                g_cmd_buf[0] = '\0';
                cfe_render();
                continue;
            }
            if (ch == 0x18) {
                g_running = 0;
                continue;
            }
            /* Ctrl+other — skip */
            continue;
        }

        switch (g_mode) {
            case CFE_MODE_WRITE:  cfe_handle_write_input(ch, mods);  break;
            case CFE_MODE_CMD:    cfe_handle_cmd_input(ch);          break;
            case CFE_MODE_FIND:   cfe_handle_find_input(ch);         break;
            case CFE_MODE_REPLACE: cfe_handle_replace_input(ch);     break;
            case CFE_MODE_NORMAL: cfe_handle_normal_input(ch);       break;
            case CFE_MODE_COLON:  cfe_handle_colon_input(ch);        break;
            default: break;
        }

        cfe_render();
    }

    cfe_free_all();

    /* Flush keyboard buffer before returning to shell */
    while (keyboard_buffer_has_data()) {
        (void)keyboard_buffer_getchar();
    }

    console_clear(con);
}
