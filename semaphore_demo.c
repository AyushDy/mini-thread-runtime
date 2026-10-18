
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

#define MAX_THREADS 8
#define STACK_SIZE (64 * 1024)
#define BUFFER_SIZE 3
#define ITEMS 10

typedef enum {
    READY, RUNNING, BLOCKED, FINISHED
} ThreadState;

typedef struct {
    ucontext_t context;
    char stack[STACK_SIZE];
    void (*function)(void);
    ThreadState state;
} Thread;

typedef struct {
    int value;
    int waiters[MAX_THREADS];
    int head;
    int count;
} Semaphore;

static Thread threads[MAX_THREADS];
static ucontext_t scheduler_context;
static int thread_count;
static int current = -1;

static int buffer[BUFFER_SIZE];
static int write_pos;
static int read_pos;

static Semaphore empty_slots = { .value = BUFFER_SIZE };
static Semaphore full_slots = { .value = 0 };

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

static void sem_wait(Semaphore *s)
{
    if (s->value > 0) {
        s->value--;
        return;
    }

    if (s->count >= MAX_THREADS) {
        fprintf(stderr, "Semaphore wait queue full\n");
        exit(EXIT_FAILURE);
    }

    int id = current;
    int tail = (s->head + s->count) % MAX_THREADS;

    s->waiters[tail] = id;
    s->count++;

    threads[id].state = BLOCKED;
    current = -1;

    if (swapcontext(&threads[id].context,
                    &scheduler_context) == -1) {
        perror("swapcontext");
        exit(EXIT_FAILURE);
    }

}

static void sem_post(Semaphore *s)
{
    if (s->count > 0) {
        int next = s->waiters[s->head];

        s->head = (s->head + 1) % MAX_THREADS;
        s->count--;

        threads[next].state = READY;
    } else {
        s->value++;
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

        if (!found) {
            for (int i = 0; i < thread_count; i++) {
                if (threads[i].state == BLOCKED) {
                    fprintf(stderr,
                            "Deadlock: all remaining threads are blocked\n");
                    exit(EXIT_FAILURE);
                }
            }

            return;
        }
    }
}

static void producer(void)
{
    for (int item = 1; item <= ITEMS; item++) {
        sem_wait(&empty_slots);

        buffer[write_pos] = item;
        write_pos = (write_pos + 1) % BUFFER_SIZE;

        printf("Produced: %d\n", item);

        sem_post(&full_slots);
    }
}

static void consumer(void)
{
    for (int i = 0; i < ITEMS; i++) {
        sem_wait(&full_slots);

        int item = buffer[read_pos];
        read_pos = (read_pos + 1) % BUFFER_SIZE;

        printf("Consumed: %d\n", item);

        sem_post(&empty_slots);
    }
}

int main(void)
{
    setbuf(stdout, NULL);

    if (thread_create(producer) < 0 ||
        thread_create(consumer) < 0) {
        fprintf(stderr, "Thread creation failed\n");
        return EXIT_FAILURE;
    }

    scheduler_run();

    printf("Producer-consumer test completed.\n");
    return EXIT_SUCCESS;
}
