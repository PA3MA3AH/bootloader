#ifndef KERNEL_CRASHLOG_H
#define KERNEL_CRASHLOG_H

#include "../common/bootinfo.h"
#include "../common/crashinfo.h"

void crashlog_init(BOOT_INFO *boot_info);
void crashlog_mark_booting(void);
void crashlog_mark_stable(void);
void crashlog_set_stage(const char *stage);
void crashlog_set_action(const char *action, const char *detail);
void crashlog_mark_panic(const char *reason);

#endif
