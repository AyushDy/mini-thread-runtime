
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

#define MAX_THREADS 16
#define STACK_SIZE (64 * 1024)

typedef enum {
    THREAD_UNUSED,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_FINISHED
} ThreadState;

typedef struct {
    int id;
    ucontext_t context;
    char stack[STACK_SIZE];
    ThreadState state;
    void (*function)(void);
} Thread;

static Thread threads[MAX_THREADS];
static ucontext_t scheduler_context;
static int current_thread = -1;
static int next_id = 0;

static void scheduler_run(void);

static void thread_trampoline(int id)
{
    threads[id].function();
    threads[id].state = THREAD_FINISHED;
    current_thread = -1;
}

int thread_create(void (*function)(void))
{
    if (function == NULL || next_id >= MAX_THREADS)
        return -1;

    int id = next_id++;
    Thread *t = &threads[id];

    t->id = id;
    t->function = function;
    t->state = THREAD_READY;

    if (getcontext(&t->context) == -1) {
        perror("getcontext");
        t->state = THREAD_UNUSED;
        return -1;
    }

    t->context.uc_stack.ss_sp = t->stack;
    t->context.uc_stack.ss_size = sizeof(t->stack);
    t->context.uc_stack.ss_flags = 0;
    t->context.uc_link = &scheduler_context;

    makecontext(&t->context,
                (void (*)(void))thread_trampoline, 1, id);

    return id;
}

void thread_yield(void)
{
    if (current_thread < 0)
        return;

    int id = current_thread;
    threads[id].state = THREAD_READY;
    current_thread = -1;

    if (swapcontext(&threads[id].context,
                    &scheduler_context) == -1) {
        perror("swapcontext");
        exit(EXIT_FAILURE);
    }
}

void thread_exit(void)
{
    if (current_thread < 0)
        return;

    threads[current_thread].state = THREAD_FINISHED;
    current_thread = -1;

    if (setcontext(&scheduler_context) == -1) {
        perror("setcontext");
        exit(EXIT_FAILURE);
    }

    abort(); /* setcontext should not return */
}

static void scheduler_run(void)
{
    while (1) {
        int found = 0;

        for (int i = 0; i < next_id; i++) {
            if (threads[i].state != THREAD_READY)
                continue;

            found = 1;
            current_thread = i;
            threads[i].state = THREAD_RUNNING;

            if (swapcontext(&scheduler_context,
                            &threads[i].context) == -1) {
                perror("swapcontext");
                exit(EXIT_FAILURE);
            }

            current_thread = -1;
        }

        if (!found)
            break;
    }
}

int thread_join(int id)
{
    if (id < 0 || id >= next_id)
        return -1;

    if (id == current_thread)
        return -1;

    while (threads[id].state != THREAD_FINISHED) {
        if (current_thread >= 0) {
            thread_yield();
        } else {
            scheduler_run();
        }

        if (threads[id].state != THREAD_FINISHED &&
            threads[id].state != THREAD_READY &&
            threads[id].state != THREAD_RUNNING)
            return -1;
    }

    return 0;
}

/* Demonstration threads */

void task_a(void)
{
    for (int i = 1; i <= 3; i++) {
        printf("Thread A: %d\n", i);
        thread_yield();
    }
}

void task_b(void)
{
    for (int i = 1; i <= 3; i++) {
        printf("Thread B: %d\n", i);
        thread_yield();
    }
}

void task_c(void)
{
    for (int i = 1; i <= 3; i++) {
        printf("Thread C: %d\n", i);
        thread_yield();
    }
}

int main(void)
{
    setbuf(stdout, NULL);

    int a = thread_create(task_a);
    int b = thread_create(task_b);
    int c = thread_create(task_c);

    if (a < 0 || b < 0 || c < 0) {
        fprintf(stderr, "Thread creation failed\n");
        return EXIT_FAILURE;
    }

    printf("Created three user-level threads\n");

    if (thread_join(a) != 0 ||
        thread_join(b) != 0 ||
        thread_join(c) != 0) {
        fprintf(stderr, "Thread join failed\n");
        return EXIT_FAILURE;
    }

    printf("All threads finished\n");
    return EXIT_SUCCESS;
}
