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
    uint32_t esp0;
    uint32_t pid;
    uint32_t cr3;
    uint32_t user_slot;
    uint32_t user_phys_base;
    task_state_t state;
    uint8_t *stack;
    struct task *next;
    uint32_t wake_tick;
    int32_t exit_status;
    uint32_t term_signal;
    char tty_path[64];
    char prog_path[256];
    char cmdline[512];
} task_t;

void task_init(void (*main_task)(void));
int task_create(void (*entry)(void*), void *arg);
void task_yield(void);
void task_exit(void);
void schedule(void);
int task_state_by_pid(uint32_t pid);
task_t *task_find_by_pid(uint32_t pid);
int task_terminate_by_pid(uint32_t pid, int32_t exit_status, uint32_t term_signal);

extern task_t *current_task;
extern task_t *_idle_task;
extern task_t tasks[MAX_TASKS];
extern int task_count;
