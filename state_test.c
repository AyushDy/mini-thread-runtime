
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

#define STACK_SIZE (64 * 1024)

static ucontext_t main_context;
static ucontext_t context_a;
static ucontext_t context_b;

static char stack_a[STACK_SIZE];
static char stack_b[STACK_SIZE];

static void task_a(void)
{
    int local = 100;

    for (int i = 0; i < 3; i++) {
        local += 1;
        printf("Task A: local = %d\n", local);

        if (swapcontext(&context_a, &main_context) == -1) {
            perror("swapcontext A");
            exit(EXIT_FAILURE);
        }
    }
}

static void task_b(void)
{
    int local = 200;

    for (int i = 0; i < 3; i++) {
        local += 10;
        printf("Task B: local = %d\n", local);

        if (swapcontext(&context_b, &main_context) == -1) {
            perror("swapcontext B");
            exit(EXIT_FAILURE);
        }
    }
}

int main(void)
{
    setbuf(stdout, NULL);

    if (getcontext(&context_a) == -1) {
        perror("getcontext A");
        return EXIT_FAILURE;
    }

    context_a.uc_stack.ss_sp = stack_a;
    context_a.uc_stack.ss_size = sizeof(stack_a);
    context_a.uc_link = &main_context;
    makecontext(&context_a, task_a, 0);

    if (getcontext(&context_b) == -1) {
        perror("getcontext B");
        return EXIT_FAILURE;
    }

    context_b.uc_stack.ss_sp = stack_b;
    context_b.uc_stack.ss_size = sizeof(stack_b);
    context_b.uc_link = &main_context;
    makecontext(&context_b, task_b, 0);

    for (int i = 0; i < 3; i++) {
        if (swapcontext(&main_context, &context_a) == -1 ||
            swapcontext(&main_context, &context_b) == -1) {
            perror("swapcontext main");
            return EXIT_FAILURE;
        }
    }

    puts("Independent state test completed.");
    return EXIT_SUCCESS;
}
