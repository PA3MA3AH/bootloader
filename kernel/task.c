#include "task.h"
#include "kheap.h"
#include "pmm.h"
#include "vmm.h"
#include "panic.h"
#include <stddef.h>

static int task_initialized = 0;
static task_t *current_task = NULL;
static task_t *task_list = NULL;
static uint32_t next_tid = 1;

/* Helper: string copy */
static void task_strcpy(char *dst, const char *src, uint32_t max_len) {
    uint32_t i = 0;
    if (!dst || !src || max_len == 0) return;
    while (src[i] && i + 1 < max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Helper: memory zero */
static void task_memzero(void *ptr, uint64_t size) {
    uint8_t *p = (uint8_t*)ptr;
    for (uint64_t i = 0; i < size; i++) {
        p[i] = 0;
    }
}

void task_init(void) {
    current_task = NULL;
    task_list = NULL;
    next_tid = 1;
    task_initialized = 1;
}

int task_is_initialized(void) {
    return task_initialized;
}

task_t* task_create(const char *name, uint64_t entry, int is_kernel, uint32_t priority) {
    task_t *task;
    uint64_t stack_phys;
    uint64_t i;
    
    if (!task_initialized) {
        return NULL;
    }
    
    task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) {
        return NULL;
    }
    
    task_memzero(task, sizeof(task_t));
    
    task->tid = next_tid++;
    if (name) {
        task_strcpy(task->name, name, TASK_NAME_MAX);
    } else {
        task->name[0] = '\0';
    }
    
    task->state = TASK_STATE_READY;
    task->priority = priority;
    task->entry_point = entry;
    task->is_kernel_task = is_kernel;
    task->time_slice = 10;
    task->time_used = 0;
    
    /* Allocate stack */
    uint64_t stack_pages = TASK_STACK_SIZE / PMM_PAGE_SIZE;
    if (TASK_STACK_SIZE % PMM_PAGE_SIZE) {
        stack_pages++;
    }
    
    stack_phys = (uint64_t)pmm_alloc_pages(stack_pages);
    if (!stack_phys) {
        kfree(task);
        return NULL;
    }
    
    task->stack_base = stack_phys;
    task->stack_size = stack_pages * PMM_PAGE_SIZE;
    
    /* Map stack in virtual memory if needed */
    /* For now, assume identity mapping or higher-half kernel */
    task->kernel_stack = stack_phys + task->stack_size;
    
    /* Initialize context */
    task->context.rsp = task->kernel_stack - 16; /* Leave some space */
    task->context.rbp = task->context.rsp;
    task->context.rip = entry;
    task->context.rflags = 0x202; /* IF set */
    
    /* Get current CR3 or create new page table */
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    task->context.cr3 = cr3;
    
    /* Clear all general purpose registers */
    task->context.rax = 0;
    task->context.rbx = 0;
    task->context.rcx = 0;
    task->context.rdx = 0;
    task->context.rsi = 0;
    task->context.rdi = 0;
    task->context.r8 = 0;
    task->context.r9 = 0;
    task->context.r10 = 0;
    task->context.r11 = 0;
    task->context.r12 = 0;
    task->context.r13 = 0;
    task->context.r14 = 0;
    task->context.r15 = 0;
    
    /* Add to task list */
    task->next = task_list;
    task_list = task;
    
    return task;
}

void task_destroy(task_t *task) {
    task_t *prev, *current;
    uint64_t stack_pages;
    
    if (!task_initialized || !task) {
        return;
    }
    
    /* Remove from task list */
    prev = NULL;
    current = task_list;
    
    while (current) {
        if (current == task) {
            if (prev) {
                prev->next = current->next;
            } else {
                task_list = current->next;
            }
            break;
        }
        prev = current;
        current = current->next;
    }
    
    /* Free stack */
    if (task->stack_base) {
        stack_pages = task->stack_size / PMM_PAGE_SIZE;
        for (uint64_t i = 0; i < stack_pages; i++) {
            pmm_free_page((void*)(task->stack_base + i * PMM_PAGE_SIZE));
        }
    }
    
    /* Free task structure */
    kfree(task);
}

task_t* task_get_current(void) {
    return current_task;
}

void task_set_current(task_t *task) {
    current_task = task;
}

task_t* task_get_by_tid(uint32_t tid) {
    task_t *task;
    
    if (!task_initialized) {
        return NULL;
    }
    
    task = task_list;
    while (task) {
        if (task->tid == tid) {
            return task;
        }
        task = task->next;
    }
    
    return NULL;
}

void task_set_state(task_t *task, uint32_t state) {
    if (task) {
        task->state = state;
    }
}

uint32_t task_get_state(task_t *task) {
    if (task) {
        return task->state;
    }
    return TASK_STATE_TERMINATED;
}

/* Context switch assembly stub */
extern void task_switch_asm(task_context_t *from, task_context_t *to);

void task_switch(task_t *from, task_t *to) {
    if (!from || !to) {
        return;
    }
    
    if (from == to) {
        return;
    }
    
    current_task = to;
    task_switch_asm(&from->context, &to->context);
}

void task_yield(void) {
    /* Scheduler will handle this */
    /* For now, do nothing */
}
