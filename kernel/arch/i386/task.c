#include <asm/task.h>
#include <asm/mm.h>
#include <asm/processor.h>
#include <asm/tss.h>
#include <asm/timer.h>
#include <stddef.h>
#include <string.h>

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

extern void context_switch(uint32_t *old_esp, uint32_t new_esp, uint32_t new_cr3);

static inline uint32_t get_esp(void) {
    uint32_t esp;
    __asm__ __volatile__("mov %%esp, %0" : "=r"(esp));
    return esp;
}

static inline uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ __volatile__("pushf; pop %0" : "=r"(flags) :: "memory");
    cli();
    return flags;
}

static inline void irq_restore(uint32_t flags) {
    if (flags & (1u << 9)) sti();
    else cli();
}

static void task_release_resources(task_t *task) {
    if (!task) return;
    if (task->cr3 && task->cr3 != mm_kernel_cr3()) {
        mm_user_cr3_destroy(task->cr3);
    }
    if (task->user_slot != (uint32_t)-1) {
        mm_user_slot_free(task->user_slot);
    }
    if (task->stack) {
        kfree(task->stack);
    }
    task->stack = NULL;
    task->cr3 = mm_kernel_cr3();
    task->user_slot = (uint32_t)-1;
    task->user_phys_base = 0;
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
    idle->ppid = 0;
    idle->state = TASK_READY;
    idle->cr3 = mm_kernel_cr3();
    idle->user_slot = (uint32_t)-1;
    idle->user_phys_base = 0;
    idle->stack = idle_stack;
    idle->esp = (uint32_t)idle_top;
    idle->esp0 = (uint32_t)(idle_stack + STACK_SIZE);
    idle->wake_tick = 0;
    idle->exit_status = 0;
    idle->term_signal = 0;
    idle->tty_path[0] = '\0';
    idle->prog_path[0] = '\0';
    idle->cmdline[0] = '\0';
    idle->next = idle;

    _idle_task = idle;

    task_t *init_task = &tasks[task_count++];
    init_task->pid = next_pid++;
    init_task->ppid = 0;
    init_task->state = TASK_RUNNING;
    init_task->cr3 = mm_kernel_cr3();
    init_task->user_slot = (uint32_t)-1;
    init_task->user_phys_base = 0;
    init_task->stack = NULL;
    init_task->esp = get_esp();
    init_task->esp0 = get_esp();
    init_task->wake_tick = 0;
    init_task->exit_status = 0;
    init_task->term_signal = 0;
    init_task->tty_path[0] = '\0';
    init_task->prog_path[0] = '\0';
    init_task->cmdline[0] = '\0';

    init_task->next = idle;
    idle->next = init_task;

    current_task = init_task;
    ready_head = idle;
    ready_tail = init_task;

    (void)main_task;
}

int task_create(void (*entry)(void*), void *arg) {
    int slot = -1;
    task_t *task = NULL;
    uint32_t irq_flags;

    uint8_t *stack = (uint8_t*)kmalloc(STACK_SIZE);
    if (!stack) return -1;

    uint32_t *top = (uint32_t*)(stack + STACK_SIZE);

    *(--top) = (uint32_t)arg;
    *(--top) = (uint32_t)task_exit;
    *(--top) = (uint32_t)entry;
    *(--top) = 0x202;
    for (int i = 0; i < 7; i++) *(--top) = 0;

    irq_flags = irq_save();
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].state == TASK_TERMINATED) {
            task_release_resources(&tasks[i]);
            slot = i;
            break;
        }
    }

    if (slot >= 0) {
        task = &tasks[slot];
    } else {
        if (task_count >= MAX_TASKS) {
            irq_restore(irq_flags);
            kfree(stack);
            return -1;
        }
        task = &tasks[task_count++];
        if (ready_head == NULL) {
            ready_head = task;
            ready_tail = task;
            task->next = task;
        } else {
            task->next = ready_head;
            ready_tail->next = task;
            ready_tail = task;
        }
    }

    task->pid = next_pid++;
    task->ppid = current_task ? current_task->pid : 0;
    task->state = TASK_READY;
    task->cr3 = mm_kernel_cr3();
    task->user_slot = (uint32_t)-1;
    task->user_phys_base = 0;
    task->stack = stack;
    task->esp = (uint32_t)top;
    task->esp0 = (uint32_t)(stack + STACK_SIZE);
    task->wake_tick = 0;
    task->exit_status = 0;
    task->term_signal = 0;
    task->tty_path[0] = '\0';
    task->prog_path[0] = '\0';
    task->cmdline[0] = '\0';

    irq_restore(irq_flags);
    return task->pid;
}

void task_yield(void) {
    uint32_t flags;
    __asm__ __volatile__("pushf; pop %0" : "=r"(flags) :: "memory");
    cli();
    schedule();
    if (flags & (1u << 9)) sti();
    else cli();
}

void task_exit(void) {
    current_task->state = TASK_TERMINATED;
    
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
        tss.esp0 = current_task->esp0;
        if (prev->state == TASK_RUNNING) {
            prev->state = TASK_READY;
        }
        context_switch(&prev->esp, next->esp, (current_task->cr3 ? current_task->cr3 : mm_kernel_cr3()));
    } else {
        sti();
        hlt();
        cli();
        schedule();
    }
}

int task_state_by_pid(uint32_t pid) {
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].pid == pid) return (int)tasks[i].state;
    }
    return -1;
}

task_t *task_find_by_pid(uint32_t pid) {
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].pid == pid) return &tasks[i];
    }
    return NULL;
}

int task_terminate_by_pid(uint32_t pid, int32_t exit_status, uint32_t term_signal) {
    task_t *task = task_find_by_pid(pid);
    if (!task || task == _idle_task) return -1;
    if (task->state == TASK_TERMINATED) return 0;
    if (task == current_task) {
        current_task->exit_status = exit_status;
        current_task->term_signal = term_signal;
        task_exit();
        return 0;
    }
    task->exit_status = exit_status;
    task->term_signal = term_signal;
    task->state = TASK_TERMINATED;
    return 0;
}

int task_waitpid(int32_t pid, int32_t *status_out, uint32_t options) {
    int has_child = 0;
    if (!current_task) return -1;
    for (;;) {
        has_child = 0;
        for (int i = 0; i < task_count; i++) {
            task_t *t = &tasks[i];
            if (t == _idle_task) continue;
            if (t->pid == 0) continue;
            if (t->ppid != current_task->pid) continue;
            if (pid > 0 && (uint32_t)pid != t->pid) continue;
            has_child = 1;
            if (t->state == TASK_TERMINATED) {
                int32_t st;
                uint32_t ret_pid = t->pid;
                if (t->term_signal != 0) st = (int32_t)(t->term_signal & 0x7Fu);
                else st = (int32_t)((t->exit_status & 0xFF) << 8);
                task_release_resources(t);
                t->pid = 0;
                t->ppid = 0;
                t->state = TASK_TERMINATED;
                t->exit_status = 0;
                t->term_signal = 0;
                t->tty_path[0] = '\0';
                t->prog_path[0] = '\0';
                t->cmdline[0] = '\0';
                if (status_out) *status_out = st;
                return (int)ret_pid;
            }
        }
        if (!has_child) return -1;
        if (options & 1u) return 0;
        task_yield();
    }
}
