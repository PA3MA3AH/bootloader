#include "cfe.h"
#include "fat32.h"
#include "kheap.h"
#include <stdint.h>

/* --------------------------------------------------------------------------
 * Simple INI parser — only two constructs:
 *   key = value       (assignment)
 *   # comment         (ignored)
 *
 * All keys are lowercase ASCII, values are hex (0xRRGGBB) or command names.
 * -------------------------------------------------------------------------- */

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static uint32_t parse_hex_color(const char *s) {
    if (!s || s[0] != '0' || s[1] != 'x') return 0;
    s += 2;
    uint32_t v = 0;
    for (int i = 0; i < 6; i++) {
        int d = hex_digit(s[i]);
        if (d < 0) return 0;
        v = (v << 4) | d;
    }
    return v;
}

static int str_equal(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return (*a == '\0' && *b == '\0');
}

static void skip_whitespace(const char **s) {
    while (**s == ' ' || **s == '\t') (*s)++;
}

static int read_line(const uint8_t *buf, uint32_t len, uint32_t *pos, char *out, int maxlen) {
    if (*pos >= len) return 0;
    int i = 0;
    while (*pos < len && i < maxlen - 1) {
        char c = (char)buf[*pos];
        (*pos)++;
        if (c == '\n') break;
        if (c == '\r') continue;
        out[i++] = c;
    }
    out[i] = '\0';
    return 1;
}

static int parse_config_line(CFE_CONFIG *cfg, const char *line) {
    const char *p = line;
    skip_whitespace(&p);
    if (*p == '#' || *p == '\0') return 1;

    char key[64];
    int ki = 0;
    while (*p && *p != '=' && ki < 63) key[ki++] = *p++;
    key[ki] = '\0';

    if (*p != '=') return 1;
    p++;
    skip_whitespace(&p);

    char val[256];
    int vi = 0;
    while (*p && vi < 255) val[vi++] = *p++;
    val[vi] = '\0';

    while (vi > 0 && (val[vi-1] == ' ' || val[vi-1] == '\t'))
        val[--vi] = '\0';

    if (str_equal(key, "title_fg")) { cfg->title_fg = parse_hex_color(val); return 1; }
    if (str_equal(key, "title_bg")) { cfg->title_bg = parse_hex_color(val); return 1; }
    if (str_equal(key, "status_fg")) { cfg->status_fg = parse_hex_color(val); return 1; }
    if (str_equal(key, "status_bg")) { cfg->status_bg = parse_hex_color(val); return 1; }
    if (str_equal(key, "text_fg")) { cfg->text_fg = parse_hex_color(val); return 1; }
    if (str_equal(key, "text_bg")) { cfg->text_bg = parse_hex_color(val); return 1; }
    if (str_equal(key, "cursor_bg")) { cfg->cursor_bg = parse_hex_color(val); return 1; }
    if (str_equal(key, "tilde_fg")) { cfg->tilde_fg = parse_hex_color(val); return 1; }
    if (str_equal(key, "cmd_fg")) { cfg->cmd_fg = parse_hex_color(val); return 1; }
    if (str_equal(key, "cmd_bg")) { cfg->cmd_bg = parse_hex_color(val); return 1; }
    if (str_equal(key, "tab_size")) { cfg->tab_size = (uint8_t)(val[0] - '0'); return 1; }

    if (cfg->keybind_count >= 32) return 1;
    CFE_COMMAND action = CFE_CMD_NONE;
    if      (str_equal(val, "save_quit"))  action = CFE_CMD_SAVE_QUIT;
    else if (str_equal(val, "force_quit")) action = CFE_CMD_FORCE_QUIT;
    else if (str_equal(val, "find_mode"))  action = CFE_CMD_FIND_MODE;
    else if (str_equal(val, "write_mode")) action = CFE_CMD_WRITE_MODE;
    else if (str_equal(val, "save"))       action = CFE_CMD_SAVE;
    else if (str_equal(val, "quit"))       action = CFE_CMD_QUIT;
    else if (str_equal(val, "goto_line"))  action = CFE_CMD_GOTO_LINE;
    else if (str_equal(val, "next_match")) action = CFE_CMD_NEXT_MATCH;

    if (action != CFE_CMD_NONE) {
        CFE_KEYBIND *kb = &cfg->keybinds[cfg->keybind_count];
        int i = 0;
        while (key[i] && i < 15) { kb->shortcut[i] = key[i]; i++; }
        kb->shortcut[i] = '\0';
        kb->action = action;
        cfg->keybind_count++;
    }
    return 1;
}

/* --------------------------------------------------------------------------
 * Built-in presets
 * -------------------------------------------------------------------------- */

void cfe_load_defaults(CFE_CONFIG *cfg) {
    cfg->title_fg = 0x000000; cfg->title_bg = 0x008800;
    cfg->status_fg = 0x000000; cfg->status_bg = 0x008800;
    cfg->text_fg = 0xDDDDDD; cfg->text_bg = 0x000000;
    cfg->cursor_bg = 0x1a1a2e;
    cfg->tilde_fg = 0x555555;
    cfg->cmd_fg = 0x000000; cfg->cmd_bg = 0xAAAA00;
    cfg->tab_size = 4;
    cfg->keybind_count = 0;
    cfg->vim_preset = 0;
}

void cfe_load_preset_vim(CFE_CONFIG *cfg) {
    cfe_load_defaults(cfg);
    cfg->vim_preset = 1;
}

/* --------------------------------------------------------------------------
 * Load config from cfe.cfg on the FAT32 partition
 * -------------------------------------------------------------------------- */

void cfe_try_load_config_file(CFE_CONFIG *cfg, PARTITION_INFO *part) {
    uint8_t *buf = NULL;
    uint32_t size = 0;
    if (!fat32_read_file(part, "cfe.cfg", &buf, &size)) return;

    uint32_t pos = 0;
    char line[256];
    while (read_line(buf, size, &pos, line, sizeof(line)))
        parse_config_line(cfg, line);

    kfree(buf);
}
