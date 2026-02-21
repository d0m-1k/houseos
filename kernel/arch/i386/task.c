#include <asm/task.h>
#include <asm/mm.h>
#include <asm/processor.h>
#include <stddef.h>

static void idle_task(void *arg) {
    (void)arg;
    while (1) {
        __asm__ __volatile__("hlt");
    }
}

uint32_t next_pid = 1;
task_t tasks[MAX_TASKS];
int task_count = 0;
task_t *current_task = NULL;
task_t *_idle_task = NULL;
static task_t *ready_head = NULL;
static task_t *ready_tail = NULL;

extern void context_switch(uint32_t *old_esp, uint32_t new_esp);

static inline uint32_t get_esp(void) {
    uint32_t esp;
    __asm__ __volatile__("mov %%esp, %0" : "=r"(esp));
    return esp;
}

void task_init(void (*main_task)(void)) {
    uint8_t *idle_stack = (uint8_t*)kmalloc(STACK_SIZE);
    uint32_t *idle_top = (uint32_t*)(idle_stack + STACK_SIZE);
    *--idle_top = 0;
    *--idle_top = (uint32_t)task_exit;
    *--idle_top = (uint32_t)idle_task;
    *--idle_top = 0x202;
    for (int i = 0; i < 7; i++) *--idle_top = 0;

    task_t *idle = &tasks[task_count++];
    idle->pid = next_pid++;
    idle->state = TASK_READY;
    idle->stack = idle_stack;
    idle->esp = (uint32_t)idle_top;
    idle->wake_tick = 0;
    idle->next = idle;

    _idle_task = idle;

    task_t *init_task = &tasks[task_count++];
    init_task->pid = next_pid++;
    init_task->state = TASK_RUNNING;
    init_task->stack = NULL;
    init_task->esp = get_esp();
    init_task->wake_tick = 0;

    init_task->next = idle;
    idle->next = init_task;

    current_task = init_task;
    ready_head = idle;
    ready_tail = init_task;

    (void)main_task;
}

int task_create(void (*entry)(void*), void *arg) {
    if (task_count >= MAX_TASKS) return -1;

    uint8_t *stack = (uint8_t*)kmalloc(STACK_SIZE);
    if (!stack) return -1;

    uint32_t *top = (uint32_t*)(stack + STACK_SIZE);

    *(--top) = (uint32_t)arg;
    *(--top) = (uint32_t)task_exit;
    *(--top) = (uint32_t)entry;
    *(--top) = 0x202;
    for (int i = 0; i < 7; i++) *(--top) = 0;

    task_t *task = &tasks[task_count++];
    task->pid = next_pid++;
    task->state = TASK_READY;
    task->stack = stack;
    task->esp = (uint32_t)top;
    task->wake_tick = 0;

    if (ready_head == NULL) {
        ready_head = task;
        ready_tail = task;
        task->next = task;
    } else {
        task->next = ready_head;
        ready_tail->next = task;
        ready_tail = task;
    }

    return task->pid;
}

void task_yield(void) {
    cli();
    schedule();
    sti();
}

void task_exit(void) {
    current_task->state = TASK_TERMINATED;
    if (current_task->stack) kfree(current_task->stack);
    schedule();
    while (1);
}

void schedule(void) {
    task_t *next = current_task;
    task_t *first_idle = NULL;
    int found = 0;
    
    do {
        next = next->next;
        if (next->state == TASK_READY) {
            if (next == _idle_task) {
                if (!first_idle) first_idle = next;
                continue;
            }
            found = 1;
            break;
        }
    } while (next != current_task);

    if (!found && first_idle) {
        next = first_idle;
        found = 1;
    }

    if (found) {
        task_t *prev = current_task;
        current_task = next;
        current_task->state = TASK_RUNNING;
        if (prev->state == TASK_RUNNING) {
            prev->state = TASK_READY;
        }
        context_switch(&prev->esp, next->esp);
    } else {
        sti();
        hlt();
        cli();
        schedule();
    }
}