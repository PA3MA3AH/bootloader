#ifndef TCC_STDHEADERS_H
#define TCC_STDHEADERS_H

#include <stddef.h>
#include <stdint.h>

/* --------------------------------------------------------------------------
 * va_list -- clang provides __builtin_va_list even in -ffreestanding
 * -------------------------------------------------------------------------- */

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_copy(d, s)      __builtin_va_copy(d, s)

/* --------------------------------------------------------------------------
 * NULL / size_t / ptrdiff_t -- from <stddef.h> already
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * setjmp.h — use clang builtins to avoid macro collision with tcc_setjmp
 * in libtcc.h (which is a 3-arg macro that shadows our name).
 * -------------------------------------------------------------------------- */

/* setjmp/longjmp — declared as real functions that wrap clang builtins.
   Must be functions (not macros) because tcc_setjmp() in libtcc.h uses
   `longjmp` as a plain identifier argument.
   jmp_buf is void* to match _tcc_setjmp's signature. */
typedef void *jmp_buf;
extern int   setjmp(jmp_buf env);
extern void  longjmp(jmp_buf env, int val) __attribute__((noreturn));

/* --------------------------------------------------------------------------
 * stdio.h stubs
 * -------------------------------------------------------------------------- */

typedef struct {
    int _fd;
    int _eof;
    int _err;
} FILE;

#define stdin  ((FILE*)0)
#define stdout ((FILE*)1)
#define stderr ((FILE*)2)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Stub implementations live in tcc_libc.c */
extern FILE *tcc_fopen(const char *path, const char *mode);
extern size_t tcc_fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
extern int    tcc_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
extern FILE  *tcc_freopen(const char *path, const char *mode, FILE *stream);
extern int    tcc_fflush(FILE *stream);
extern int    tcc_fclose(FILE *stream);
extern int    tcc_fseek(FILE *stream, long offset, int whence);
extern long   tcc_ftell(FILE *stream);
extern int    tcc_fprintf(FILE *stream, const char *fmt, ...);
extern int    tcc_printf(const char *fmt, ...);
extern int    tcc_vprintf(const char *fmt, va_list ap);
extern int    tcc_putc(int c, FILE *stream);
extern int    tcc_puts(const char *s);
extern int    tcc_sprintf(char *str, const char *fmt, ...);
extern int    tcc_snprintf(char *str, size_t size, const char *fmt, ...);
extern int    tcc_vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
extern int    tcc_fputc(int c, FILE *stream);
extern int    tcc_fputs(const char *s, FILE *stream);
extern int    tcc_vfprintf(FILE *stream, const char *fmt, va_list ap);
extern int    tcc_fprintf(FILE *stream, const char *fmt, ...);

/* Standard-name wrappers (libtcc.c #undef's the macros above) */
extern void  *malloc(size_t size);
extern void  *calloc(size_t nmemb, size_t size);
extern void  *realloc(void *ptr, size_t size);
extern void   free(void *ptr);
extern char  *strdup(const char *s);
extern char  *strcpy(char *dest, const char *src);
extern char  *strncpy(char *dest, const char *src, size_t n);
extern char  *strcat(char *dest, const char *src);
extern FILE  *fopen(const char *path, const char *mode);
extern int    fclose(FILE *stream);
extern int    fgetc(FILE *stream);
extern int    remove(const char *pathname);
extern char  *realpath(const char *path, char *resolved_path);
/* gettimeofday declared via real function below */
extern int tcc_gettimeofday(void *tv, void *tz);
#define gettimeofday tcc_gettimeofday
extern char **g_environ;

#define fopen   tcc_fopen
#define fread   tcc_fread
#define fwrite  tcc_fwrite
#define freopen  tcc_freopen
#define fflush   tcc_fflush
#define fclose  tcc_fclose
#define fseek   tcc_fseek
#define ftell   tcc_ftell
#define fprintf tcc_fprintf
#define printf  tcc_printf
#define vprintf tcc_vprintf
#define putc    tcc_putc
#define puts    tcc_puts
#define sprintf tcc_sprintf
#define snprintf tcc_snprintf
#define vsnprintf tcc_vsnprintf
#define fputc   tcc_fputc
#define fputs   tcc_fputs
#define vfprintf tcc_vfprintf
#define EOF     (-1)

/* --------------------------------------------------------------------------
 * stdlib.h stubs
 * -------------------------------------------------------------------------- */

extern void *tcc_malloc(size_t size);
extern void *tcc_calloc(size_t nmemb, size_t size);
extern void *tcc_realloc(void *ptr, size_t size);
extern void  tcc_free(void *ptr);
extern void  tcc_exit(int status) __attribute__((noreturn));
extern void  tcc_abort(void) __attribute__((noreturn));
extern char *tcc_getenv(const char *name);
extern char **g_environ;
extern int   tcc_atoi(const char *nptr);
extern long  tcc_strtol(const char *nptr, char **endptr, int base);
extern long long tcc_strtoll(const char *nptr, char **endptr, int base);
extern unsigned long tcc_strtoul(const char *nptr, char **endptr, int base);
extern unsigned long long tcc_strtoull(const char *nptr, char **endptr, int base);
extern double tcc_strtod(const char *nptr, char **endptr);
extern void   tcc_qsort(void *base, size_t nmemb, size_t size,
                        int (*compar)(const void *, const void *));

#define malloc   tcc_malloc
#define calloc   tcc_calloc
#define realloc  tcc_realloc
#define free     tcc_free
#define exit     tcc_exit
#define abort    tcc_abort
#define getenv   tcc_getenv
#define environ  g_environ
#define atoi     tcc_atoi
#define strtol   tcc_strtol
#define strtoll  tcc_strtoll
#define strtoul  tcc_strtoul
#define strtoull tcc_strtoull
#define strtod   tcc_strtod
#define qsort    tcc_qsort
#define abs(x)   ((x) < 0 ? -(x) : (x))
#define labs(x)  ((x) < 0 ? -(x) : (x))

/* --------------------------------------------------------------------------
 * string.h stubs
 * -------------------------------------------------------------------------- */

extern void *tcc_memcpy(void *dest, const void *src, size_t n);
extern void *tcc_memmove(void *dest, const void *src, size_t n);
extern void *tcc_memset(void *s, int c, size_t n);
extern int   tcc_memcmp(const void *s1, const void *s2, size_t n);
extern char *tcc_strcpy(char *dest, const char *src);
extern char *tcc_strncpy(char *dest, const char *src, size_t n);
extern char *tcc_strdup(const char *s);
extern char *tcc_strchr(const char *s, int c);
extern char *tcc_strrchr(const char *s, int c);
extern char *tcc_strstr(const char *haystack, const char *needle);
extern size_t tcc_strlen(const char *s);
extern int    tcc_strcmp(const char *s1, const char *s2);
extern int    tcc_strncmp(const char *s1, const char *s2, size_t n);
extern char *tcc_strpbrk(const char *s, const char *accept);
extern int    tcc_sscanf(const char *str, const char *fmt, ...);

#define memcpy   tcc_memcpy
#define memmove  tcc_memmove
#define memset   tcc_memset
#define memcmp   tcc_memcmp
#define strcpy   tcc_strcpy
#define strncpy  tcc_strncpy
#define strdup   tcc_strdup
#define strchr   tcc_strchr
#define strrchr  tcc_strrchr
#define strstr   tcc_strstr
#define strlen   tcc_strlen
#define strcmp   tcc_strcmp
#define strncmp  tcc_strncmp
#define strpbrk  tcc_strpbrk
#define sscanf   tcc_sscanf

/* --------------------------------------------------------------------------
 * ctype.h stubs (TCC uses isspace, isdigit, etc. from internal checks)
 * -------------------------------------------------------------------------- */

static inline int isspace(int c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f'; }
static inline int isdigit(int c) { return c>='0' && c<='9'; }
static inline int isalpha(int c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
static inline int isalnum(int c) { return isalpha(c)||isdigit(c); }
static inline int isxdigit(int c) { return isdigit(c)||(c>='a'&&c<='f')||(c>='A'&&c<='F'); }
static inline int tolower(int c) { return (c>='A'&&c<='Z') ? c+('a'-'A') : c; }
static inline int toupper(int c) { return (c>='a'&&c<='z') ? c-('a'-'A') : c; }

/* --------------------------------------------------------------------------
 * errno.h stub
 * -------------------------------------------------------------------------- */

extern int tcc_errno;
#define errno tcc_errno
#define ENOENT  2
#define ENOMEM  12
#define EIO     5
#define EINVAL  22

/* --------------------------------------------------------------------------
 * math.h stubs -- TCC only needs these for constant folding
 * -------------------------------------------------------------------------- */

#define M_E        2.71828182845904523536
#define M_LOG2E    1.44269504088896340736
#define M_LOG10E   0.434294481903251827651
#define M_LN2      0.693147180559945309417
#define M_LN10     2.30258509299404568402
#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_PI_4     0.785398163397448309616
#define M_1_PI     0.318309886183790671538
#define M_2_PI     0.636619772367581343076
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2    1.41421356237309504880
#define M_SQRT1_2  0.707106781186547524401

static inline double tcc_math_fmod(double x, double y) { return 0.0; }
static inline double tcc_math_floor(double x) { return (double)(long long)x; }
static inline double tcc_math_ceil(double x) { double v=(double)(long long)x; return v<x?v+1.0:v; }
static inline double tcc_math_sqrt(double x) { return x; }
static inline double tcc_math_pow(double x, double y) { return x; }
static inline double tcc_math_log(double x) { return 0.0; }
static inline double tcc_math_log10(double x) { return 0.0; }
static inline double tcc_math_exp(double x) { return 0.0; }
static inline double tcc_math_sin(double x) { return 0.0; }
static inline double tcc_math_cos(double x) { return 0.0; }
static inline double tcc_math_tan(double x) { return 0.0; }
static inline double tcc_math_asin(double x) { return 0.0; }
static inline double tcc_math_acos(double x) { return 0.0; }
static inline double tcc_math_atan(double x) { return 0.0; }
static inline double tcc_math_atan2(double y, double x) { return 0.0; }
static inline double tcc_math_sinh(double x) { return 0.0; }
static inline double tcc_math_cosh(double x) { return 0.0; }
static inline double tcc_math_tanh(double x) { return 0.0; }
static inline int tcc_math_isnan(double x) { return 0; }
static inline int tcc_math_isinf(double x) { return 0; }

#define fmod   tcc_math_fmod
#define floor  tcc_math_floor
#define ceil   tcc_math_ceil
#define sqrt   tcc_math_sqrt
#define pow    tcc_math_pow
#define log    tcc_math_log
#define log10  tcc_math_log10
#define exp    tcc_math_exp
#define sin    tcc_math_sin
#define cos    tcc_math_cos
#define tan    tcc_math_tan
#define asin   tcc_math_asin
#define acos   tcc_math_acos
#define atan   tcc_math_atan
#define atan2  tcc_math_atan2
#define sinh   tcc_math_sinh
#define cosh   tcc_math_cosh
#define tanh   tcc_math_tanh
#define isnan  tcc_math_isnan
#define isinf  tcc_math_isinf
#define NAN    (0.0/0.0)
#define INFINITY (1.0/0.0)

/* --------------------------------------------------------------------------
 * fcntl.h stubs (TCC uses O_RDONLY etc. in internal code)
 * -------------------------------------------------------------------------- */

#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   0100
#define O_TRUNC   01000
#define O_APPEND  02000
#define O_BINARY  0
#define F_OK      0
#define R_OK      4
#define W_OK      2
#define X_OK      1

/* --------------------------------------------------------------------------
 * Additional POSIX stubs (file I/O, path, errors)
 * -------------------------------------------------------------------------- */

extern int    stub_open(const char *pathname, int flags, ...);
extern int    stub_unlink(const char *pathname);
extern char  *stub_getcwd(char *buf, size_t size);
extern char  *stub_strerror(int errnum);

#define open    stub_open
#define unlink  stub_unlink
#define getcwd  stub_getcwd
#define strerror stub_strerror

/* fdopen is a stdio wrapper around open() */
extern FILE  *stub_fdopen(int fd, const char *mode);
#define fdopen stub_fdopen

/* lseek for file position manipulation */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
extern long stub_lseek(int fd, long offset, int whence);
#define lseek stub_lseek

/* --------------------------------------------------------------------------
 * time.h stub
 * -------------------------------------------------------------------------- */

typedef long time_t;
typedef int clock_t;
typedef long ssize_t;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

extern struct tm *localtime(const time_t *timep);
extern time_t time(time_t *t);

/* --------------------------------------------------------------------------
 * unistd.h stub
 * -------------------------------------------------------------------------- */

#define STDOUT_FILENO 1
#define STDERR_FILENO 2
extern ssize_t read(int fd, void *buf, size_t count);

/* --------------------------------------------------------------------------
 * stdio.h additional stubs
 * -------------------------------------------------------------------------- */

extern int fflush(FILE *stream);

/* --------------------------------------------------------------------------
 * math.h additional stubs
 * -------------------------------------------------------------------------- */

extern long double ldexpl(long double x, int exp);

/* --------------------------------------------------------------------------
 * dirent.h stub (TCC may reference in elf loading)
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * unistd.h stub (included on non-WIN32)
 * -------------------------------------------------------------------------- */

#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define F_OK 0
extern int tcc_access(const char *path, int mode);
#define access tcc_access

/* --------------------------------------------------------------------------
 * sys/time.h stub
 * -------------------------------------------------------------------------- */

struct timeval {
    long tv_sec;
    long tv_usec;
};
struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

/* --------------------------------------------------------------------------
 * dispatch/dispatch.h stub (macOS, won't be used)
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * semaphore.h stub
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * glob.h stub
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * dlfcn.h stub (CONFIG_TCC_STATIC disables dlopen)
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * float.h
 * -------------------------------------------------------------------------- */

#define FLT_MAX  3.4028234663852886e+38F
#define DBL_MAX  1.7976931348623157e+308
#define FLT_MIN  1.1754943508222875e-38F
#define DBL_MIN  2.2250738585072014e-308
#define FLT_EPSILON 1.1920928955078125e-07F
#define DBL_EPSILON 2.2204460492503131e-16

#endif /* TCC_STDHEADERS_H */
