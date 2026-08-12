#ifndef TASK_H
#define TASK_H

#include <stdint.h>

#define TASK_MAX_TASKS        256
#define TASK_NAME_MAX         64
#define TASK_STACK_SIZE       (16 * 1024)  // 16 KB default stack

#define TASK_STATE_READY      0
#define TASK_STATE_RUNNING    1
#define TASK_STATE_BLOCKED    2
#define TASK_STATE_TERMINATED 3

typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cr3;
} task_context_t;

typedef struct task {
    uint32_t tid;
    char name[TASK_NAME_MAX];
    uint32_t state;
    uint32_t priority;
    
    task_context_t context;
    
    uint64_t stack_base;
    uint64_t stack_size;
    
    uint64_t kernel_stack;
    uint64_t user_stack;
    
    uint64_t entry_point;
    
    uint64_t time_slice;
    uint64_t time_used;
    
    struct task *next;
    
    int is_kernel_task;
} task_t;

/* Task management */
void task_init(void);
int task_is_initialized(void);

task_t* task_create(const char *name, uint64_t entry, int is_kernel, uint32_t priority);
void task_destroy(task_t *task);

task_t* task_get_current(void);
void task_set_current(task_t *task);

task_t* task_get_by_tid(uint32_t tid);

void task_set_state(task_t *task, uint32_t state);
uint32_t task_get_state(task_t *task);

/* Context switching */
void task_switch(task_t *from, task_t *to);
void task_yield(void);

#endif
