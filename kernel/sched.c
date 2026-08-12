#include "sched.h"
#include "task.h"
#include "interrupts.h"
#include "panic.h"
#include <stddef.h>

static int sched_initialized = 0;
static int sched_running = 0;
static task_t *ready_queue = NULL;
static uint64_t current_tick = 0;

void sched_init(void) {
    ready_queue = NULL;
    current_tick = 0;
    sched_running = 0;
    sched_initialized = 1;
}

int sched_is_initialized(void) {
    return sched_initialized;
}

void sched_add_task(task_t *task) {
    if (!sched_initialized || !task) {
        return;
    }
    
    task_set_state(task, TASK_STATE_READY);
    
    /* Add to end of ready queue */
    if (!ready_queue) {
        ready_queue = task;
        task->next = NULL;
    } else {
        task_t *current = ready_queue;
        while (current->next) {
            current = current->next;
        }
        current->next = task;
        task->next = NULL;
    }
}

void sched_remove_task(task_t *task) {
    task_t *prev, *current;
    
    if (!sched_initialized || !task) {
        return;
    }
    
    prev = NULL;
    current = ready_queue;
    
    while (current) {
        if (current == task) {
            if (prev) {
                prev->next = current->next;
            } else {
                ready_queue = current->next;
            }
            return;
        }
        prev = current;
        current = current->next;
    }
}

void sched_start(void) {
    if (!sched_initialized) {
        return;
    }
    
    sched_running = 1;
}

void sched_stop(void) {
    sched_running = 0;
}

void sched_tick(void) {
    task_t *current;
    
    if (!sched_initialized || !sched_running) {
        return;
    }
    
    current_tick++;
    
    current = task_get_current();
    if (!current) {
        /* No current task, pick first from ready queue */
        sched_schedule();
        return;
    }
    
    /* Update time used */
    current->time_used++;
    
    /* Check if time slice expired */
    if (current->time_used >= current->time_slice) {
        sched_schedule();
    }
}

void sched_schedule(void) {
    task_t *current;
    task_t *next;
    
    if (!sched_initialized) {
        return;
    }
    
    current = task_get_current();
    
    /* Find next ready task */
    next = ready_queue;
    
    while (next && next->state != TASK_STATE_READY) {
        next = next->next;
    }
    
    if (!next) {
        /* No ready tasks, stay with current if available */
        if (current && current->state == TASK_STATE_RUNNING) {
            return;
        }
        /* Idle - nothing to do */
        return;
    }
    
    /* If no current task, just set next as current */
    if (!current) {
        task_set_state(next, TASK_STATE_RUNNING);
        task_set_current(next);
        next->time_used = 0;
        return;
    }
    
    /* Same task, reset time slice */
    if (current == next) {
        current->time_used = 0;
        return;
    }
    
    /* Switch tasks */
    if (current->state == TASK_STATE_RUNNING) {
        task_set_state(current, TASK_STATE_READY);
    }
    
    task_set_state(next, TASK_STATE_RUNNING);
    next->time_used = 0;
    
    /* Perform context switch */
    task_switch(current, next);
}

void sched_yield(void) {
    task_t *current;
    
    if (!sched_initialized) {
        return;
    }
    
    current = task_get_current();
    if (current) {
        current->time_used = current->time_slice; /* Force reschedule */
    }
    
    sched_schedule();
}

void sched_block_current(void) {
    task_t *current;
    
    if (!sched_initialized) {
        return;
    }
    
    current = task_get_current();
    if (!current) {
        return;
    }
    
    task_set_state(current, TASK_STATE_BLOCKED);
    sched_remove_task(current);
    sched_schedule();
}

void sched_unblock_task(task_t *task) {
    if (!sched_initialized || !task) {
        return;
    }
    
    if (task->state == TASK_STATE_BLOCKED) {
        sched_add_task(task);
    }
}
