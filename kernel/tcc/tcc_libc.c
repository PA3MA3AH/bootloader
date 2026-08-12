#include "config.h"
#include "stdheaders.h"
#include "../console.h"
#include "../kheap.h"
#include "../panic.h"

/* --------------------------------------------------------------------------
 * Global console pointer -- set by tcc_wrap.c during init
 * -------------------------------------------------------------------------- */

static CONSOLE *g_tcc_con = NULL;

void tcc_libc_set_console(CONSOLE *con) {
    g_tcc_con = con;
}

/* --------------------------------------------------------------------------
 * Memory allocation -- wraps kernel heap
 * -------------------------------------------------------------------------- */

int tcc_errno = 0;

/* --------------------------------------------------------------------------
 * Memory allocation -- wraps kernel heap.
 * Note: tcc_malloc/tcc_free/tcc_realloc/tcc_strdup are also defined by
 * libtcc.c (in tcc.o via ONE_SOURCE) after its #undef block. We only
 * provide the calloc wrapper here since libtcc.c doesn't define tcc_calloc.
 * -------------------------------------------------------------------------- */

void *tcc_calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) return NULL;
    size_t total = nmemb * size;
    void *p = tcc_malloc(total);
    if (p) tcc_memset(p, 0, total);
    return p;
}

/* --------------------------------------------------------------------------
 * Memory operations
 * -------------------------------------------------------------------------- */

void *tcc_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dest;
}

void *tcc_memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

void *tcc_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

int tcc_memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *a = (const uint8_t *)s1;
    const uint8_t *b = (const uint8_t *)s2;
    while (n--) {
        int d = *a++ - *b++;
        if (d) return d;
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * String operations
 * -------------------------------------------------------------------------- */

size_t tcc_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

char *tcc_strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *tcc_strncpy(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = '\0';
    return dest;
}

char *tcc_strchr(const char *s, int c) {
    while (*s && *s != (char)c) s++;
    return (*s == (char)c) ? (char *)s : NULL;
}

char *tcc_strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) { if (*s == (char)c) last = s; s++; }
    return (char *)last;
}

char *tcc_strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    while (*haystack) {
        const char *h = haystack, *n = needle;
        while (*n && *h == *n) { h++; n++; }
        if (!*n) return (char *)haystack;
        haystack++;
    }
    return NULL;
}

int tcc_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int tcc_strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}

char *tcc_strpbrk(const char *s, const char *accept) {
    while (*s) {
        const char *a = accept;
        while (*a && *a != *s) a++;
        if (*a) return (char *)s;
        s++;
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * Number parsing
 * -------------------------------------------------------------------------- */

int tcc_atoi(const char *s) {
    return (int)tcc_strtol(s, NULL, 10);
}

long tcc_strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    int neg = 0;
    unsigned long val = 0;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;

    if (base == 0) {
        base = 10;
        if (s[0] == '0') {
            if (s[1] == 'x' || s[1] == 'X') { base = 16; s += 2; }
            else { base = 8; s++; }
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    for (;;) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        val = val * base + d;
        s++;
    }

    if (endptr) *endptr = (char *)s;
    return neg ? -(long)val : (long)val;
}

long long tcc_strtoll(const char *nptr, char **endptr, int base) {
    return (long long)tcc_strtol(nptr, endptr, base);
}

double tcc_strtod(const char *nptr, char **endptr) {
    /* Minimal implementation -- enough for TCC literal parsing */
    double val = 0.0;
    double frac = 0.0;
    double div = 1.0;
    const char *s = nptr;
    int neg = 0;

    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;

    while (*s >= '0' && *s <= '9') { val = val * 10.0 + (*s - '0'); s++; }

    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            frac = frac * 10.0 + (*s - '0');
            div *= 10.0;
            s++;
        }
    }

    val += frac / div;

    if (*s == 'e' || *s == 'E') {
        /* Skip exponent -- not implementing for minimal */
        s++;
        if (*s == '-' || *s == '+') s++;
        while (*s >= '0' && *s <= '9') s++;
    }

    if (endptr) *endptr = (char *)s;
    return neg ? -val : val;
}

/* --------------------------------------------------------------------------
 * vprintf engine -- minimal, supports TCC error formatting
 * -------------------------------------------------------------------------- */

typedef struct {
    char *buf;
    size_t size;
    size_t pos;
    CONSOLE *con;
} PrintCtx;

static void ctx_putchar(PrintCtx *ctx, char c) {
    if (ctx->con) {
        console_putchar(ctx->con, c);
    } else if (ctx->buf && ctx->pos < ctx->size) {
        ctx->buf[ctx->pos++] = c;
    }
}

static void ctx_write_str(PrintCtx *ctx, const char *s) {
    while (*s) ctx_putchar(ctx, *s++);
}

static void ctx_write_udec(PrintCtx *ctx, unsigned long v) {
    char buf[24];
    int i = 0;
    do { buf[i++] = '0' + (v % 10); v /= 10; } while (v);
    while (i--) ctx_putchar(ctx, buf[i]);
}

static void ctx_write_sdec(PrintCtx *ctx, long v) {
    if (v < 0) { ctx_putchar(ctx, '-'); v = -v; }
    ctx_write_udec(ctx, (unsigned long)v);
}

static void ctx_write_hex(PrintCtx *ctx, unsigned long v) {
    ctx_write_str(ctx, "0x");
    if (v == 0) { ctx_putchar(ctx, '0'); return; }
    char buf[20];
    int i = 0;
    while (v) {
        int d = v & 0xF;
        buf[i++] = d < 10 ? '0' + d : 'a' + d - 10;
        v >>= 4;
    }
    while (i--) ctx_putchar(ctx, buf[i]);
}

static void ctx_write_ptr(PrintCtx *ctx, const void *ptr) {
    ctx_write_hex(ctx, (unsigned long)(uintptr_t)ptr);
}

int tcc_vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    PrintCtx ctx;
    ctx.buf = str;
    ctx.size = size > 0 ? size - 1 : 0;
    ctx.pos = 0;
    ctx.con = NULL;

    while (*fmt) {
        if (*fmt != '%') {
            ctx_putchar(&ctx, *fmt++);
            continue;
        }
        fmt++;

        if (*fmt == '%') {
            ctx_putchar(&ctx, '%');
            fmt++;
            continue;
        }

        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            ctx_write_str(&ctx, s ? s : "(null)");
            fmt++;
            continue;
        }
        if (*fmt == 'd' || *fmt == 'i') {
            ctx_write_sdec(&ctx, va_arg(ap, long));
            fmt++;
            continue;
        }
        if (*fmt == 'u') {
            ctx_write_udec(&ctx, va_arg(ap, unsigned long));
            fmt++;
            continue;
        }
        if (*fmt == 'x') {
            ctx_write_hex(&ctx, va_arg(ap, unsigned long));
            fmt++;
            continue;
        }
        if (*fmt == 'p') {
            ctx_write_ptr(&ctx, va_arg(ap, void *));
            fmt++;
            continue;
        }
        if (*fmt == 'c') {
            ctx_putchar(&ctx, (char)va_arg(ap, int));
            fmt++;
            continue;
        }

        /* Unknown format -- print as-is */
        ctx_putchar(&ctx, '%');
        if (*fmt) ctx_putchar(&ctx, *fmt++);
    }

    if (str && size > 0) str[ctx.pos] = '\0';
    return (int)ctx.pos;
}

int tcc_snprintf(char *str, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = tcc_vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return r;
}

int tcc_sprintf(char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = tcc_vsnprintf(str, (size_t)-1, fmt, ap);
    va_end(ap);
    return r;
}

int tcc_vprintf(const char *fmt, va_list ap) {
    PrintCtx ctx;
    ctx.buf = NULL;
    ctx.size = 0;
    ctx.pos = 0;
    ctx.con = g_tcc_con;

    while (*fmt) {
        if (*fmt != '%') {
            ctx_putchar(&ctx, *fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') { ctx_putchar(&ctx, '%'); fmt++; continue; }
        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            ctx_write_str(&ctx, s ? s : "(null)"); fmt++; continue;
        }
        if (*fmt == 'd' || *fmt == 'i') {
            ctx_write_sdec(&ctx, va_arg(ap, long)); fmt++; continue;
        }
        if (*fmt == 'u') {
            ctx_write_udec(&ctx, va_arg(ap, unsigned long)); fmt++; continue;
        }
        if (*fmt == 'x') {
            ctx_write_hex(&ctx, va_arg(ap, unsigned long)); fmt++; continue;
        }
        if (*fmt == 'p') {
            ctx_write_ptr(&ctx, va_arg(ap, void *)); fmt++; continue;
        }
        if (*fmt == 'c') {
            ctx_putchar(&ctx, (char)va_arg(ap, int)); fmt++; continue;
        }
        ctx_putchar(&ctx, '%');
        if (*fmt) ctx_putchar(&ctx, *fmt++);
    }
    return 0;
}

int tcc_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = tcc_vprintf(fmt, ap);
    va_end(ap);
    return r;
}

int tcc_fprintf(FILE *stream, const char *fmt, ...) {
    (void)stream;
    va_list ap;
    va_start(ap, fmt);
    int r = tcc_vprintf(fmt, ap);
    va_end(ap);
    return r;
}

int tcc_vfprintf(FILE *stream, const char *fmt, va_list ap) {
    (void)stream;
    return tcc_vprintf(fmt, ap);
}

int tcc_fputc(int c, FILE *stream) {
    (void)stream;
    if (g_tcc_con) console_putchar(g_tcc_con, (char)c);
    return c;
}

int tcc_fputs(const char *s, FILE *stream) {
    (void)stream;
    if (g_tcc_con) { while (*s) console_putchar(g_tcc_con, *s++); }
    return 0;
}

int tcc_putc(int c, FILE *stream) {
    (void)stream;
    if (g_tcc_con) console_putchar(g_tcc_con, (char)c);
    return c;
}

int tcc_puts(const char *s) {
    tcc_printf("%s\n", s);
    return 0;
}

/* --------------------------------------------------------------------------
 * File I/O stubs -- Phase 1: not supported
 * -------------------------------------------------------------------------- */

FILE *tcc_fopen(const char *path, const char *mode) { (void)path; (void)mode; tcc_errno = ENOENT; return NULL; }
FILE *tcc_freopen(const char *path, const char *mode, FILE *stream) { (void)path; (void)mode; (void)stream; tcc_errno = ENOENT; return NULL; }
size_t tcc_fread(void *ptr, size_t size, size_t nmemb, FILE *stream) { (void)ptr; (void)size; (void)nmemb; (void)stream; return 0; }
int tcc_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) { (void)ptr; (void)size; (void)nmemb; (void)stream; return 0; }
int tcc_fclose(FILE *stream) { (void)stream; return 0; }
int tcc_fseek(FILE *stream, long offset, int whence) { (void)stream; (void)offset; (void)whence; return -1; }
long tcc_ftell(FILE *stream) { (void)stream; return -1; }
int tcc_access(const char *path, int mode) { (void)path; (void)mode; return -1; }

/* --------------------------------------------------------------------------
 * Exit / abort
 * -------------------------------------------------------------------------- */

void tcc_exit(int status) {
    (void)status;
    panic("TCC: exit() called");
}

void tcc_abort(void) {
    panic("TCC: abort() called");
}

/* --------------------------------------------------------------------------
 * getenv stub
 * -------------------------------------------------------------------------- */

char *tcc_getenv(const char *name) {
    (void)name;
    return NULL;
}

/* --------------------------------------------------------------------------
 * qsort
 * -------------------------------------------------------------------------- */

void tcc_qsort(void *base, size_t nmemb, size_t size,
               int (*compar)(const void *, const void *)) {
    if (nmemb <= 1) return;

    /* Insertion sort -- simple and works for TCC's small tables */
    uint8_t *arr = (uint8_t *)base;
    uint8_t *tmp = (uint8_t *)tcc_malloc(size);
    if (!tmp) return;

    for (size_t i = 1; i < nmemb; i++) {
        tcc_memcpy(tmp, arr + i * size, size);
        size_t j = i;
        while (j > 0 && compar(arr + (j - 1) * size, tmp) > 0) {
            tcc_memcpy(arr + j * size, arr + (j - 1) * size, size);
            j--;
        }
        tcc_memcpy(arr + j * size, tmp, size);
    }

    tcc_free(tmp);
}

/* --------------------------------------------------------------------------
 * sscanf -- minimal
 * -------------------------------------------------------------------------- */

int tcc_sscanf(const char *str, const char *fmt, ...) {
    /* TCC uses sscanf for parsing config lines like "%d %d" */
    /* Minimal implementation: handles %d %u %s %c only */
    va_list ap;
    va_start(ap, fmt);
    int count = 0;

    while (*fmt) {
        while (*fmt == ' ' || *fmt == '\t') {
            fmt++;
            while (*str == ' ' || *str == '\t') str++;
        }

        if (!*fmt || !*str) break;

        if (*fmt == '%') {
            fmt++;
            if (*fmt == 'd') {
                char *end;
                long val = tcc_strtol(str, &end, 10);
                if (end == str) break;
                str = end;
                int *p = va_arg(ap, int *);
                *p = (int)val;
                count++;
            } else if (*fmt == 'u') {
                char *end;
                unsigned long val = tcc_strtol(str, &end, 10);
                if (end == str) break;
                str = end;
                unsigned int *p = va_arg(ap, unsigned int *);
                *p = (unsigned int)val;
                count++;
            } else if (*fmt == 's') {
                char *p = va_arg(ap, char *);
                while (*str && *str != ' ' && *str != '\t' && *str != '\n')
                    *p++ = *str++;
                *p = '\0';
                count++;
            } else if (*fmt == 'c') {
                char *p = va_arg(ap, char *);
                *p = *str++;
                count++;
            } else {
                break;
            }
            fmt++;
        } else if (*fmt == *str) {
            fmt++; str++;
        } else {
            break;
        }
    }

    va_end(ap);
    return count;
}

/* --------------------------------------------------------------------------
 * Additional stubs for TCC compilation
 * -------------------------------------------------------------------------- */

ssize_t read(int fd, void *buf, size_t count) { (void)fd; (void)buf; (void)count; return -1; }
int tcc_fflush(FILE *stream) { (void)stream; return 0; }
long stub_lseek(int fd, long offset, int whence) { (void)fd; (void)offset; (void)whence; return -1; }
long double ldexpl(long double x, int exp) { (void)exp; return x; }

struct tm *localtime(const time_t *t) {
    (void)t;
    return NULL;
}

time_t time(time_t *t) {
    if (t) *t = 0;
    return 0;
}

unsigned long tcc_strtoul(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long val = 0;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+') s++;

    if (base == 0) {
        base = 10;
        if (s[0] == '0') {
            if (s[1] == 'x' || s[1] == 'X') { base = 16; s += 2; }
            else { base = 8; s++; }
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    for (;;) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        val = val * base + d;
        s++;
    }

    if (endptr) *endptr = (char *)s;
    return val;
}

unsigned long long tcc_strtoull(const char *nptr, char **endptr, int base) {
    /* For our use cases this is fine as 64-bit unsigned */
    const char *s = nptr;
    unsigned long long val = 0;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+') s++;

    if (base == 0) {
        base = 10;
        if (s[0] == '0') {
            if (s[1] == 'x' || s[1] == 'X') { base = 16; s += 2; }
            else { base = 8; s++; }
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    for (;;) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        val = val * base + d;
        s++;
    }

    if (endptr) *endptr = (char *)s;
    return val;
}

/* --------------------------------------------------------------------------
 * POSIX stubs
 * -------------------------------------------------------------------------- */

int stub_open(const char *pathname, int flags, ...) { (void)pathname; (void)flags; tcc_errno = ENOENT; return -1; }
int stub_unlink(const char *pathname) { (void)pathname; tcc_errno = ENOENT; return -1; }
char *stub_getcwd(char *buf, size_t size) { (void)buf; (void)size; return NULL; }
char *stub_strerror(int errnum) { (void)errnum; return "Unknown error"; }
FILE *stub_fdopen(int fd, const char *mode) { (void)fd; (void)mode; tcc_errno = ENOENT; return NULL; }

/* --------------------------------------------------------------------------
 * Standard-name wrappers — libtcc.c #undef's the macros so we need
 * real functions with the original names.
 * -------------------------------------------------------------------------- */

/* Undo macros so function definitions below are not rewritten */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup
#undef strcpy
#undef strncpy
#undef fopen
#undef fclose

void  *malloc(size_t size)       { return tcc_malloc(size); }
void  *calloc(size_t n, size_t s){ return tcc_calloc(n, s); }
void  *realloc(void *p, size_t s){ return tcc_realloc(p, s); }
void   free(void *p)             { tcc_free(p); }
char  *strdup(const char *s)     { return tcc_strdup(s); }
char  *strcpy(char *d, const char *s)   { return tcc_strcpy(d, s); }
char  *strncpy(char *d, const char *s, size_t n) { return tcc_strncpy(d, s, n); }
char  *strcat(char *d, const char *s) {
    char *p = d; while (*p) p++; while ((*p++ = *s++)); return d;
}
FILE  *fopen(const char *p, const char *m)  { return tcc_fopen(p, m); }
int    fclose(FILE *s)           { return tcc_fclose(s); }
int    fgetc(FILE *s)            { (void)s; return EOF; }
int    remove(const char *p)     { (void)p; tcc_errno = ENOENT; return -1; }
char  *realpath(const char *p, char *b) { (void)p; (void)b; return NULL; }

/* gettimeofday — kernel doesn't have one yet, return zeros */
int tcc_gettimeofday(void *tv, void *tz) {
    if (tv) { ((struct timeval *)tv)->tv_sec = 0; ((struct timeval *)tv)->tv_usec = 0; }
    if (tz) { ((struct timezone *)tz)->tz_minuteswest = 0; ((struct timezone *)tz)->tz_dsttime = 0; }
    return 0;
}

/* setjmp / longjmp — wrap clang builtins as real functions.
   These are needed because libtcc.h uses longjmp as a plain identifier
   inside the tcc_setjmp macro.  __builtin_longjmp requires a constant
   integer, so the 'val' parameter is ignored (TCC always passes non-zero
   on error paths).  The return value of setjmp() will be 1. */
int setjmp(jmp_buf env) {
    return __builtin_setjmp(env);
}

void longjmp(jmp_buf env, int val) {
    (void)val;
    __builtin_longjmp(env, 1);
}

/* g_environ — alias for tcc_getenv to satisfy the #define environ g_environ */
char **g_environ = NULL;

/* --------------------------------------------------------------------------
 * Additional POSIX stubs — TCC references these in various code paths
 * -------------------------------------------------------------------------- */

int close(int fd) { (void)fd; return -1; }
int execvp(const char *file, char *const argv[]) { (void)file; (void)argv; return -1; }
int mprotect(void *addr, size_t len, int prot) { (void)addr; (void)len; (void)prot; return 0; }

/* Signal stubs — not used in kernel mode */
typedef struct { unsigned long __val[64 / 8 * 8]; } sigset_t;
struct sigaction {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, void *, void *);
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};
int sigemptyset(sigset_t *set) { (void)set; return 0; }
int sigaddset(sigset_t *set, int signum) { (void)set; (void)signum; return 0; }
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) { (void)how; (void)set; (void)oldset; return 0; }
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) { (void)signum; (void)act; (void)oldact; return 0; }

/* Floating-point conversion stubs */
float strtof(const char *nptr, char **endptr) { (void)endptr; return (float)tcc_strtod(nptr, NULL); }
long double strtold(const char *nptr, char **endptr) { (void)endptr; return (long double)tcc_strtod(nptr, NULL); }

/* Assertion failure handler */
void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function) {
    (void)assertion; (void)file; (void)line; (void)function;
    panic("assertion failed");
}
