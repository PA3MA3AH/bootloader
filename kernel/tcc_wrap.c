#include "tcc/config.h"
#include "tcc/libtcc.h"
#include "console.h"
#include "kheap.h"
#include "pmm.h"
#include "panic.h"

/* From tcc_libc.c */
extern void tcc_libc_set_console(CONSOLE *con);
extern int  tcc_memcmp(const void *, const void *, size_t);
extern void *tcc_memset(void *, int, size_t);
extern void *tcc_memcpy(void *, const void *, size_t);
extern void *tcc_memmove(void *, const void *, size_t);
extern int  tcc_strcmp(const char *, const char *);
extern int  tcc_strncmp(const char *, const char *, size_t);
extern size_t tcc_strlen(const char *);
extern char *tcc_strcpy(char *, const char *);
extern char *tcc_strncpy(char *, const char *, size_t);
extern char *tcc_strstr(const char *, const char *);
extern char *tcc_strchr(const char *, int);
extern int  tcc_atoi(const char *);
extern long tcc_strtol(const char *, char **, int);
extern long long tcc_strtoll(const char *, char **, int);
extern double tcc_strtod(const char *, char **);
extern void tcc_qsort(void *, size_t, size_t, int (*)(const void *, const void *));

static TCCState *g_tcc = NULL;

/* --------------------------------------------------------------------------
 * Custom allocator -- TCC calls this for all internal allocations
 * -------------------------------------------------------------------------- */

static void *tcc_realloc_func(void *ptr, unsigned long size) {
    if (size == 0) {
        if (ptr) kfree(ptr);
        return NULL;
    }
    if (!ptr) return kmalloc(size);
    return krealloc(ptr, (size_t)size);
}

/* --------------------------------------------------------------------------
 * Error callback
 * -------------------------------------------------------------------------- */

static void tcc_error_cb(void *opaque, const char *msg) {
    CONSOLE *con = (CONSOLE *)opaque;
    if (con) {
        console_printf(con, "  tcc: %s\n", msg);
    }
}

/* --------------------------------------------------------------------------
 * Bind kernel symbols so TCC-compiled code can call them
 * -------------------------------------------------------------------------- */

static void tcc_bind_symbols(TCCState *s) {
    /* Kernel heap */
    tcc_add_symbol(s, "kmalloc", kmalloc);
    tcc_add_symbol(s, "kfree", kfree);
    tcc_add_symbol(s, "kcalloc", kcalloc);
    tcc_add_symbol(s, "krealloc", krealloc);

    /* Console */
    tcc_add_symbol(s, "console_printf", console_printf);
    tcc_add_symbol(s, "console_putchar", console_putchar);
    tcc_add_symbol(s, "console_write", console_write);
    tcc_add_symbol(s, "console_write_hex", console_write_hex);
    tcc_add_symbol(s, "console_write_dec", console_write_dec);
    tcc_add_symbol(s, "console_write_udec", console_write_udec);
    tcc_add_symbol(s, "console_write_ptr", console_write_ptr);

    /* PMM */
    tcc_add_symbol(s, "pmm_alloc_page", pmm_alloc_page);
    tcc_add_symbol(s, "pmm_alloc_pages", pmm_alloc_pages);
    tcc_add_symbol(s, "pmm_free_page", pmm_free_page);
    tcc_add_symbol(s, "pmm_total_pages", pmm_total_pages);
    tcc_add_symbol(s, "pmm_free_pages", pmm_free_pages);
    tcc_add_symbol(s, "pmm_used_pages", pmm_used_pages);

    /* Panic */
    tcc_add_symbol(s, "panic", panic);

    /* Standard C -- map to our implementations or kernel equivalents */
    tcc_add_symbol(s, "printf", console_printf);
    tcc_add_symbol(s, "malloc", kmalloc);
    tcc_add_symbol(s, "free", kfree);
    tcc_add_symbol(s, "realloc", krealloc);
    tcc_add_symbol(s, "calloc", kcalloc);
    tcc_add_symbol(s, "memcmp", tcc_memcmp);
    tcc_add_symbol(s, "memset", tcc_memset);
    tcc_add_symbol(s, "memcpy", tcc_memcpy);
    tcc_add_symbol(s, "memmove", tcc_memmove);
    tcc_add_symbol(s, "strcmp", tcc_strcmp);
    tcc_add_symbol(s, "strncmp", tcc_strncmp);
    tcc_add_symbol(s, "strlen", tcc_strlen);
    tcc_add_symbol(s, "strcpy", tcc_strcpy);
    tcc_add_symbol(s, "strncpy", tcc_strncpy);
    tcc_add_symbol(s, "strstr", tcc_strstr);
    tcc_add_symbol(s, "strchr", tcc_strchr);
    tcc_add_symbol(s, "atoi", tcc_atoi);
    tcc_add_symbol(s, "strtol", tcc_strtol);
    tcc_add_symbol(s, "strtoll", tcc_strtoll);
    tcc_add_symbol(s, "strtod", tcc_strtod);
    tcc_add_symbol(s, "qsort", tcc_qsort);
    tcc_add_symbol(s, "puts", console_write);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int tcc_kernel_init(CONSOLE *con) {
    tcc_libc_set_console(con);

    /* MUST set custom reallocator BEFORE tcc_new(), otherwise tcc_malloc()
     * uses default_reallocator which calls free()/realloc() -> tcc_free()/tcc_realloc()
     * -> allocator again -> infinite recursion */
    tcc_set_realloc(tcc_realloc_func);

    g_tcc = tcc_new();
    if (!g_tcc) return -1;

    tcc_set_error_func(g_tcc, (void *)con, tcc_error_cb);
    tcc_set_output_type(g_tcc, TCC_OUTPUT_MEMORY);

    /* Preprocessor defines needed by x86_64 codegen */
    tcc_define_symbol(g_tcc, "__STDC__", "1");
    tcc_define_symbol(g_tcc, "__STDC_VERSION__", "199901L");
    tcc_define_symbol(g_tcc, "__x86_64__", "1");
    tcc_define_symbol(g_tcc, "__amd64__", "1");
    tcc_define_symbol(g_tcc, "__LP64__", "1");
    tcc_define_symbol(g_tcc, "__SIZE_TYPE__", "unsigned long");
    tcc_define_symbol(g_tcc, "__PTRDIFF_TYPE__", "long");
    tcc_define_symbol(g_tcc, "__WCHAR_TYPE__", "int");
    tcc_define_symbol(g_tcc, "__INTMAX_TYPE__", "long long");
    tcc_define_symbol(g_tcc, "__UINTMAX_TYPE__", "unsigned long long");
    tcc_define_symbol(g_tcc, "__INT64_TYPE__", "long long");
    tcc_define_symbol(g_tcc, "__UINT64_TYPE__", "unsigned long long");
    tcc_define_symbol(g_tcc, "__INT32_TYPE__", "int");
    tcc_define_symbol(g_tcc, "__UINT32_TYPE__", "unsigned int");
    tcc_define_symbol(g_tcc, "__INT16_TYPE__", "short");
    tcc_define_symbol(g_tcc, "__UINT16_TYPE__", "unsigned short");
    tcc_define_symbol(g_tcc, "__INT8_TYPE__", "signed char");
    tcc_define_symbol(g_tcc, "__UINT8_TYPE__", "unsigned char");

    tcc_bind_symbols(g_tcc);

    return 0;
}

int tcc_kernel_compile(const char *source, void **out_entry) {
    if (!g_tcc || !source || !out_entry) return -1;

    *out_entry = NULL;

    if (tcc_compile_string(g_tcc, source) != 0) {
        return -1;
    }

    if (tcc_relocate(g_tcc) != 0) {
        return -1;
    }

    /* Try kernel_entry first (our convention), then main */
    *out_entry = tcc_get_symbol(g_tcc, "kernel_entry");
    if (!*out_entry) {
        *out_entry = tcc_get_symbol(g_tcc, "main");
    }
    if (!*out_entry) {
        return -1;
    }

    return 0;
}

int tcc_kernel_run(void *entry) {
    if (!entry) return -1;
    typedef int (*fn_t)(void);
    return ((fn_t)entry)();
}

void tcc_kernel_cleanup(void) {
    if (g_tcc) {
        tcc_delete(g_tcc);
        g_tcc = NULL;
    }
}
