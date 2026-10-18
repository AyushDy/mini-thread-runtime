
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>

#define MAX_THREADS 4
#define STACK_SIZE (64 * 1024)

typedef struct {
    ucontext_t context;
    char stack[STACK_SIZE];
    void (*function)(void);
    int ready;
} Thread;

static Thread threads[MAX_THREADS];
static ucontext_t scheduler_context;
static int thread_count = 0;
static int current = -1;
static volatile sig_atomic_t tick = 0;

static void timer_handler(int sig)
{
    (void)sig;
    tick = 1;
}

static void trampoline(int id)
{
    threads[id].function();
    threads[id].ready = 0;
    current = -1;
}

static int thread_create(void (*fn)(void))
{
    if (thread_count >= MAX_THREADS || fn == NULL)
        return -1;

    int id = thread_count++;
    Thread *t = &threads[id];

    t->function = fn;
    t->ready = 1;

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
    current = -1;

    if (swapcontext(&threads[id].context, &scheduler_context) == -1) {
        perror("swapcontext");
        exit(EXIT_FAILURE);
    }
}

static void thread_poll(void)
{
    if (tick) {
        tick = 0;
        thread_yield();
    }
}

static void scheduler_run(void)
{
    while (1) {
        int found = 0;

        for (int i = 0; i < thread_count; i++) {
            if (!threads[i].ready)
                continue;

            found = 1;
            current = i;

            if (swapcontext(&scheduler_context,
                            &threads[i].context) == -1) {
                perror("swapcontext");
                exit(EXIT_FAILURE);
            }

            current = -1;
        }

        if (!found)
            return;
    }
}

static void task_a(void)
{
    unsigned long long work = 0;

    while (1) {
        work++;

        /* Check for a timer request at a safe point. */
        thread_poll();
    }
}

static void task_b(void)
{
    unsigned long count = 0;

    while (1) {
        printf("Task B is running: %lu\n", ++count);

        thread_yield();
    }
}

int main(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = timer_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    struct itimerval timer = {0};
    timer.it_value.tv_usec = 100000;
    timer.it_interval.tv_usec = 100000;

    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        perror("setitimer");
        return EXIT_FAILURE;
    }

    if (thread_create(task_a) < 0 || thread_create(task_b) < 0)
        return EXIT_FAILURE;

    puts("Timer-assisted scheduling started.");
    scheduler_run();

    return EXIT_SUCCESS;
}
