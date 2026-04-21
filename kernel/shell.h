#ifndef SHELL_H
#define SHELL_H

#include "../common/bootinfo.h"
#include "console.h"
#include "pmm.h"

#define SHELL_INPUT_MAX 256

typedef struct {
    CONSOLE *con;
    BOOT_INFO *boot_info;
    PMM_STATE *pmm;
    char input[SHELL_INPUT_MAX];
    unsigned int length;
} SHELL;

void shell_init(SHELL *sh, CONSOLE *con, BOOT_INFO *boot_info, PMM_STATE *pmm);
void shell_prompt(SHELL *sh);
void shell_handle_char(SHELL *sh, char ch);

#endif
