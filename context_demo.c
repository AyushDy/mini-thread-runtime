
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

#define STACK_SIZE (64 * 1024)

ucontext_t main_context;
ucontext_t child_context;

char child_stack[STACK_SIZE];

void child_function(void)
{
    printf("2. Child context is running\n");
    printf("3. Child context is returning\n");
}

int main(void)
{
    setbuf(stdout, NULL);

    printf("1. Main: program started\n");

    if (getcontext(&child_context) == -1) {
        perror("getcontext");
        return EXIT_FAILURE;
    }

    child_context.uc_stack.ss_sp = child_stack;
    child_context.uc_stack.ss_size = sizeof(child_stack);
    child_context.uc_stack.ss_flags = 0;

    child_context.uc_link = &main_context;

    makecontext(&child_context, child_function, 0);

    printf("Main: switching to child context\n");

    if (swapcontext(&main_context, &child_context) == -1) {
        perror("swapcontext");
        return EXIT_FAILURE;
    }

    printf("4. Main: child context has finished\n");

    return EXIT_SUCCESS;
}
