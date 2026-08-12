#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

/* System call numbers */
#define SYS_EXIT         0
#define SYS_READ         1
#define SYS_WRITE        2
#define SYS_OPEN         3
#define SYS_CLOSE        4
#define SYS_GETPID       5
#define SYS_FORK         6
#define SYS_EXEC         7
#define SYS_WAIT         8
#define SYS_SBRK         9
#define SYS_SLEEP        10
#define SYS_YIELD        11
#define SYS_GETTIME      12

/* Maximum number of system calls */
#define SYSCALL_MAX      256

/* System call handler type */
typedef uint64_t (*syscall_handler_t)(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                                       uint64_t arg4, uint64_t arg5, uint64_t arg6);

/* Initialize syscall subsystem */
void syscall_init(void);
int syscall_is_initialized(void);

/* Register a system call handler */
void syscall_register(uint32_t num, syscall_handler_t handler);

/* System call dispatcher (called from assembly) */
uint64_t syscall_dispatch(uint64_t num, uint64_t arg1, uint64_t arg2,
                          uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6);

/* Built-in system call implementations */
uint64_t sys_exit(uint64_t status);
uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count);
uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count);
uint64_t sys_open(uint64_t path, uint64_t flags);
uint64_t sys_close(uint64_t fd);
uint64_t sys_getpid(void);
uint64_t sys_yield(void);

#endif
