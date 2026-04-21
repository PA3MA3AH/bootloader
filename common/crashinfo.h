#ifndef CRASHINFO_H
#define CRASHINFO_H

#include <stdint.h>

#define CRASH_INFO_MAGIC   0x4352415348494E46ULL /* "CRASHINF" */
#define CRASH_INFO_VERSION 1

#define CRASH_STATUS_CLEAR        0
#define CRASH_STATUS_BOOTING      1
#define CRASH_STATUS_ACTION_ARMED 2
#define CRASH_STATUS_PANIC        3
#define CRASH_STATUS_STABLE       4

#define CRASH_TEXT_STAGE_LEN  64
#define CRASH_TEXT_ACTION_LEN 64
#define CRASH_TEXT_DETAIL_LEN 160

typedef struct {
    uint64_t magic;
    uint64_t version;
    uint64_t status;

    uint64_t boot_counter;
    uint64_t panic_counter;

    char stage[CRASH_TEXT_STAGE_LEN];
    char action[CRASH_TEXT_ACTION_LEN];
    char detail[CRASH_TEXT_DETAIL_LEN];
} CRASH_INFO;

#endif
