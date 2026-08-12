#include "shell.h"
#include "console.h"
#include "fat32.h"
#include "kheap.h"
#include "partition.h"

/* External functions from tcc_wrap.c */
extern int tcc_kernel_compile(const char *source, void **out_entry);
extern int tcc_kernel_run(void *entry);
extern void tcc_kernel_cleanup(void);

/* --------------------------------------------------------------------------
 * cc — compile and run a C source file from a FAT32 partition
 *
 * Usage: cc <partition> <file.c>
 *   Example: cc sd0p1 hello.c
 *   Example: cc sd0p1 /src/main.c
 * -------------------------------------------------------------------------- */

static int str_eq_cc(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static int str_starts_with_cc(const char *s, const char *prefix) {
    while (*prefix) { if (*s != *prefix) return 0; s++; prefix++; }
    return 1;
}

static PARTITION_INFO *cc_find_partition(SHELL *sh, const char *name) {
    PARTITION_INFO *part = partition_find_by_name(name);
    if (!part) {
        console_printf(sh->con, "cc: partition '%s' not found\n", name);
    }
    return part;
}

static void shell_run_cc(SHELL *sh, const char *args) {
    char part_name[32];
    char file_path[128];
    uint32_t i = 0;
    uint32_t j = 0;

    /* Parse partition name */
    while (args[i] == ' ') i++;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(part_name))
        part_name[j++] = args[i++];
    part_name[j] = '\0';

    if (!part_name[0]) {
        console_printf(sh->con, "Usage: cc <partition> <file.c>\n");
        console_printf(sh->con, "  Example: cc sd0p1 hello.c\n");
        return;
    }

    /* Parse file path */
    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && j + 1 < sizeof(file_path))
        file_path[j++] = args[i++];
    file_path[j] = '\0';

    if (!file_path[0]) {
        console_printf(sh->con, "Usage: cc <partition> <file.c>\n");
        return;
    }

    /* Find and mount partition */
    PARTITION_INFO *part = cc_find_partition(sh, part_name);
    if (!part) return;

    FAT32_FS fs;
    if (!fat32_mount(part, &fs)) {
        console_printf(sh->con, "cc: cannot mount FAT32 on %s\n", part->name);
        return;
    }

    /* Read source file */
    uint8_t *src_buf = NULL;
    uint32_t src_size = 0;

    if (!fat32_read_file(part, file_path, &src_buf, &src_size)) {
        console_printf(sh->con, "cc: cannot read '%s' on %s\n", file_path, part->name);
        return;
    }

    if (src_size == 0) {
        console_printf(sh->con, "cc: '%s' is empty\n", file_path);
        kfree(src_buf);
        return;
    }

    /* Null-terminate source */
    char *source = kmalloc(src_size + 1);
    if (!source) {
        console_printf(sh->con, "cc: out of memory\n");
        kfree(src_buf);
        return;
    }
    for (uint32_t k = 0; k < src_size; k++) source[k] = (char)src_buf[k];
    source[src_size] = '\0';
    kfree(src_buf);

    console_printf(sh->con, "Compiling '%s' (%u bytes)...\n", file_path, src_size);

    /* Compile */
    void *entry = NULL;
    if (tcc_kernel_compile(source, &entry) != 0) {
        console_printf(sh->con, "cc: compilation failed\n");
        kfree(source);
        return;
    }

    kfree(source);

    /* Run */
    console_printf(sh->con, "Running...\n");
    int result = tcc_kernel_run(entry);
    console_printf(sh->con, "Exit code: %d\n", result);
}

void shell_tcc_dispatch(SHELL *sh) {
    if (str_starts_with_cc(sh->input, "cc ")) {
        shell_run_cc(sh, sh->input + 3);
        return;
    }
    if (str_eq_cc(sh->input, "cc")) {
        console_printf(sh->con, "Usage: cc <partition> <file.c>\n");
        console_printf(sh->con, "  Compiles and runs a C source file from a FAT32 partition.\n");
        console_printf(sh->con, "  Example: cc sd0p1 hello.c\n");
        return;
    }
}
