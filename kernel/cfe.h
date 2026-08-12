#ifndef CFE_H
#define CFE_H

#include "console.h"
#include "partition.h"
#include <stdint.h>

/* --------------------------------------------------------------------------
 * Commands available via Alt-mini-prompt or vim keybindings
 * -------------------------------------------------------------------------- */

typedef enum {
    CFE_CMD_NONE = 0,
    CFE_CMD_SAVE_QUIT,      /* "sq" / :wq  — save & exit            */
    CFE_CMD_FORCE_QUIT,     /* "fq" / :q!  — exit without save       */
    CFE_CMD_FIND_MODE,      /* "f" / /     — enter find mode          */
    CFE_CMD_WRITE_MODE,     /* "w" / i     — enter write mode         */
    CFE_CMD_SAVE,           /* Ctrl+S      — save only                */
    CFE_CMD_QUIT,           /* Ctrl+X      — quit only                */
    CFE_CMD_GOTO_LINE,      /* "g" / :N     — go to line number       */
    CFE_CMD_NEXT_MATCH,     /* "n"         — next find match          */
    CFE_CMD_MAX
} CFE_COMMAND;

typedef struct {
    char shortcut[16];
    CFE_COMMAND action;
} CFE_KEYBIND;

/* --------------------------------------------------------------------------
 * Config loaded from cfe.cfg + preset
 * -------------------------------------------------------------------------- */

typedef struct {
    uint32_t title_fg;
    uint32_t title_bg;
    uint32_t status_fg;
    uint32_t status_bg;
    uint32_t text_fg;
    uint32_t text_bg;
    uint32_t cursor_bg;
    uint32_t tilde_fg;
    uint32_t cmd_fg;
    uint32_t cmd_bg;
    uint8_t  tab_size;
    int keybind_count;
    CFE_KEYBIND keybinds[32];
    int vim_preset;          /* 1 = vim-style, 0 = CFE-style */
} CFE_CONFIG;

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void cfe_run(CONSOLE *con, PARTITION_INFO *part, const char *filename, int vim_preset);
void cfe_load_defaults(CFE_CONFIG *cfg);
void cfe_load_preset_vim(CFE_CONFIG *cfg);
void cfe_try_load_config_file(CFE_CONFIG *cfg, PARTITION_INFO *part);

#endif
