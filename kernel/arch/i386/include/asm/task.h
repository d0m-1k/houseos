#pragma once
#include <stdint.h>

#define STACK_SIZE 4096
#define MAX_TASKS 16

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_TERMINATED
} task_state_t;

typedef struct task {
    uint32_t esp;
    uint32_t pid;
    task_state_t state;
    uint8_t *stack;
    struct task *next;
    uint32_t wake_tick;
} task_t;

void task_init(void (*main_task)(void));
int task_create(void (*entry)(void*), void *arg);
void task_yield(void);
void task_exit(void);
void schedule(void);

extern task_t *current_task;
extern task_t *_idle_task;
extern task_t tasks[MAX_TASKS];
extern int task_count;