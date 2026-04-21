#include "crashlog.h"
#include <stdint.h>
#include <stddef.h>

static CRASH_INFO *g_crash = 0;

static void text_clear(char *dst, uint64_t size) {
    uint64_t i;
    if (!dst || size == 0) {
        return;
    }

    for (i = 0; i < size; i++) {
        dst[i] = 0;
    }
}

static void text_copy(char *dst, uint64_t size, const char *src) {
    uint64_t i;

    if (!dst || size == 0) {
        return;
    }

    if (!src) {
        dst[0] = 0;
        return;
    }

    for (i = 0; i + 1 < size && src[i]; i++) {
        dst[i] = src[i];
    }

    dst[i] = 0;
}

static void crashlog_ensure_header(void) {
    if (!g_crash) {
        return;
    }

    if (g_crash->magic != CRASH_INFO_MAGIC || g_crash->version != CRASH_INFO_VERSION) {
        uint8_t *p = (uint8_t*)g_crash;
        uint64_t i;

        for (i = 0; i < sizeof(CRASH_INFO); i++) {
            p[i] = 0;
        }

        g_crash->magic = CRASH_INFO_MAGIC;
        g_crash->version = CRASH_INFO_VERSION;
        g_crash->status = CRASH_STATUS_CLEAR;
    }
}

void crashlog_init(BOOT_INFO *boot_info) {
    if (!boot_info || boot_info->crash_info_phys == 0 || boot_info->crash_info_size < sizeof(CRASH_INFO)) {
        g_crash = 0;
        return;
    }

    g_crash = (CRASH_INFO*)(uintptr_t)boot_info->crash_info_phys;
    crashlog_ensure_header();
}

void crashlog_mark_booting(void) {
    if (!g_crash) {
        return;
    }

    crashlog_ensure_header();

    g_crash->boot_counter++;
    g_crash->status = CRASH_STATUS_BOOTING;

    text_clear(g_crash->stage, sizeof(g_crash->stage));
    text_clear(g_crash->action, sizeof(g_crash->action));
    text_clear(g_crash->detail, sizeof(g_crash->detail));

    text_copy(g_crash->stage, sizeof(g_crash->stage), "kernel startup");
    text_copy(g_crash->detail, sizeof(g_crash->detail), "boot in progress");
}

void crashlog_mark_stable(void) {
    if (!g_crash) {
        return;
    }

    crashlog_ensure_header();

    g_crash->status = CRASH_STATUS_STABLE;
    text_copy(g_crash->stage, sizeof(g_crash->stage), "shell runtime");
    text_clear(g_crash->action, sizeof(g_crash->action));
    text_copy(g_crash->detail, sizeof(g_crash->detail), "system stable");
}

void crashlog_set_stage(const char *stage) {
    if (!g_crash) {
        return;
    }

    crashlog_ensure_header();
    text_copy(g_crash->stage, sizeof(g_crash->stage), stage);
}

void crashlog_set_action(const char *action, const char *detail) {
    if (!g_crash) {
        return;
    }

    crashlog_ensure_header();

    g_crash->status = CRASH_STATUS_ACTION_ARMED;
    text_copy(g_crash->action, sizeof(g_crash->action), action);
    text_copy(g_crash->detail, sizeof(g_crash->detail), detail);
}

void crashlog_mark_panic(const char *reason) {
    if (!g_crash) {
        return;
    }

    crashlog_ensure_header();

    g_crash->panic_counter++;
    g_crash->status = CRASH_STATUS_PANIC;
    text_copy(g_crash->detail, sizeof(g_crash->detail), reason ? reason : "kernel panic");
}
