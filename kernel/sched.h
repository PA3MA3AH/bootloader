#ifndef SCHED_H
#define SCHED_H

#include "task.h"
#include <stdint.h>

/* Scheduler initialization */
void sched_init(void);
int sched_is_initialized(void);

/* Add/remove tasks from scheduler */
void sched_add_task(task_t *task);
void sched_remove_task(task_t *task);

/* Start scheduling (enables timer-based preemption) */
void sched_start(void);
void sched_stop(void);

/* Scheduler tick (called from timer interrupt) */
void sched_tick(void);

/* Manual schedule */
void sched_schedule(void);

/* Yield current task */
void sched_yield(void);

/* Block/unblock tasks */
void sched_block_current(void);
void sched_unblock_task(task_t *task);

#endif
