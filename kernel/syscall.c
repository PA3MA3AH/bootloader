#include "syscall.h"
#include "task.h"
#include "sched.h"
#include "console.h"
#include <stddef.h>

static int syscall_initialized = 0;
static syscall_handler_t syscall_table[SYSCALL_MAX];

void syscall_init(void) {
    uint32_t i;
    
    /* Clear syscall table */
    for (i = 0; i < SYSCALL_MAX; i++) {
        syscall_table[i] = NULL;
    }
    
    /* Register built-in syscalls */
    syscall_register(SYS_EXIT, (syscall_handler_t)sys_exit);
    syscall_register(SYS_READ, (syscall_handler_t)sys_read);
    syscall_register(SYS_WRITE, (syscall_handler_t)sys_write);
    syscall_register(SYS_OPEN, (syscall_handler_t)sys_open);
    syscall_register(SYS_CLOSE, (syscall_handler_t)sys_close);
    syscall_register(SYS_GETPID, (syscall_handler_t)sys_getpid);
    syscall_register(SYS_YIELD, (syscall_handler_t)sys_yield);
    
    syscall_initialized = 1;
}

int syscall_is_initialized(void) {
    return syscall_initialized;
}

void syscall_register(uint32_t num, syscall_handler_t handler) {
    if (num >= SYSCALL_MAX) {
        return;
    }
    
    syscall_table[num] = handler;
}

uint64_t syscall_dispatch(uint64_t num, uint64_t arg1, uint64_t arg2,
                          uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    if (!syscall_initialized) {
        return (uint64_t)-1;
    }
    
    if (num >= SYSCALL_MAX) {
        return (uint64_t)-1;
    }
    
    if (!syscall_table[num]) {
        return (uint64_t)-1;
    }
    
    return syscall_table[num](arg1, arg2, arg3, arg4, arg5, arg6);
}

/* Built-in system call implementations */

uint64_t sys_exit(uint64_t status) {
    task_t *current = task_get_current();
    
    if (current) {
        task_set_state(current, TASK_STATE_TERMINATED);
        sched_remove_task(current);
        sched_schedule();
    }
    
    return status;
}

uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count) {
    /* TODO: Implement file descriptor table */
    (void)fd;
    (void)buf;
    (void)count;
    return (uint64_t)-1;
}

uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count) {
    /* TODO: Implement file descriptor table */
    /* For now, if fd == 1 (stdout), write to console */
    (void)fd;
    (void)buf;
    (void)count;
    return (uint64_t)-1;
}

uint64_t sys_open(uint64_t path, uint64_t flags) {
    /* TODO: Implement file descriptor table and VFS integration */
    (void)path;
    (void)flags;
    return (uint64_t)-1;
}

uint64_t sys_close(uint64_t fd) {
    /* TODO: Implement file descriptor table */
    (void)fd;
    return (uint64_t)-1;
}

uint64_t sys_getpid(void) {
    task_t *current = task_get_current();
    
    if (current) {
        return (uint64_t)current->tid;
    }
    
    return 0;
}

uint64_t sys_yield(void) {
    sched_yield();
    return 0;
}
