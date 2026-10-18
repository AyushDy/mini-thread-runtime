
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

#define MAX_THREADS 8
#define STACK_SIZE (64 * 1024)

typedef enum {
    UNUSED, READY, RUNNING, BLOCKED, FINISHED
} ThreadState;

typedef struct {
    ucontext_t context;
    char stack[STACK_SIZE];
    void (*function)(void);
    ThreadState state;
} Thread;

typedef struct {
    int locked;
    int owner;
    int waiters[MAX_THREADS];
    int head;
    int count;
} Mutex;

static Thread threads[MAX_THREADS];
static ucontext_t scheduler_context;
static int thread_count;
static int current = -1;

static int shared_counter;
static Mutex counter_mutex = { .owner = -1 };

static void scheduler_run(void);

static void trampoline(int id)
{
    threads[id].function();
    threads[id].state = FINISHED;
    current = -1;
}

static int thread_create(void (*fn)(void))
{
    if (fn == NULL || thread_count >= MAX_THREADS)
        return -1;

    int id = thread_count++;
    Thread *t = &threads[id];

    t->function = fn;
    t->state = READY;

    if (getcontext(&t->context) == -1) {
        perror("getcontext");
        exit(EXIT_FAILURE);
    }

    t->context.uc_stack.ss_sp = t->stack;
    t->context.uc_stack.ss_size = sizeof(t->stack);
    t->context.uc_stack.ss_flags = 0;
    t->context.uc_link = &scheduler_context;

    makecontext(&t->context, (void (*)(void))trampoline, 1, id);
    return id;
}

static void thread_yield(void)
{
    int id = current;

    threads[id].state = READY;
    current = -1;

    if (swapcontext(&threads[id].context,
                    &scheduler_context) == -1) {
        perror("swapcontext");
        exit(EXIT_FAILURE);
    }
}

static void mutex_lock(Mutex *m)
{
    if (!m->locked) {
        m->locked = 1;
        m->owner = current;
        return;
    }

    if (m->owner == current) {
        fprintf(stderr, "Recursive mutex lock detected\n");
        exit(EXIT_FAILURE);
    }

    if (m->count >= MAX_THREADS) {
        fprintf(stderr, "Mutex wait queue full\n");
        exit(EXIT_FAILURE);
    }

    int id = current;
    int tail = (m->head + m->count) % MAX_THREADS;

    m->waiters[tail] = id;
    m->count++;

    threads[id].state = BLOCKED;
    current = -1;

    if (swapcontext(&threads[id].context,
                    &scheduler_context) == -1) {
        perror("swapcontext");
        exit(EXIT_FAILURE);
    }

}

static void mutex_unlock(Mutex *m)
{
    if (!m->locked || m->owner != current) {
        fprintf(stderr, "Invalid mutex unlock\n");
        exit(EXIT_FAILURE);
    }

    if (m->count > 0) {
        int next = m->waiters[m->head];
        m->head = (m->head + 1) % MAX_THREADS;
        m->count--;

        m->owner = next;
        threads[next].state = READY;
    } else {
        m->locked = 0;
        m->owner = -1;
    }
}

static void scheduler_run(void)
{
    while (1) {
        int found = 0;

        for (int i = 0; i < thread_count; i++) {
            if (threads[i].state != READY)
                continue;

            found = 1;
            current = i;
            threads[i].state = RUNNING;

            if (swapcontext(&scheduler_context,
                            &threads[i].context) == -1) {
                perror("swapcontext");
                exit(EXIT_FAILURE);
            }

            current = -1;
        }

        if (!found)
            break;
    }
}

static void worker(void)
{
    for (int i = 0; i < 1000; i++) {
        mutex_lock(&counter_mutex);

        int temp = shared_counter;
        thread_yield();
        shared_counter = temp + 1;

        mutex_unlock(&counter_mutex);
    }
}

int main(void)
{
    setbuf(stdout, NULL);

    if (thread_create(worker) < 0 ||
        thread_create(worker) < 0) {
        fprintf(stderr, "Thread creation failed\n");
        return EXIT_FAILURE;
    }

    scheduler_run();

    printf("Expected counter: 2000\n");
    printf("Actual counter:   %d\n", shared_counter);

    if (shared_counter != 2000) {
        fprintf(stderr, "Test failed\n");
        return EXIT_FAILURE;
    }

    puts("Mutex test passed.");
    return EXIT_SUCCESS;
}

