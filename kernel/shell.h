#ifndef SHELL_H
#define SHELL_H

#include "../common/bootinfo.h"
#include "console.h"
#include "pmm.h"

#define SHELL_INPUT_MAX 256
#define SHELL_PAGER_LINES 20

typedef struct {
    CONSOLE *con;
    BOOT_INFO *boot_info;
    char input[SHELL_INPUT_MAX];
    unsigned int length;
    /* Pager state */
    int pager_active;
    int pager_line_count;
} SHELL;

void shell_init(SHELL *sh, CONSOLE *con, BOOT_INFO *boot_info);
void shell_prompt(SHELL *sh);
void shell_handle_char(SHELL *sh, char ch);

/* TCC shell commands */
void shell_tcc_dispatch(SHELL *sh);

#endif
