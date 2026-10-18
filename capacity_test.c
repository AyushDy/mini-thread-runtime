
#include <stdio.h>
#include <stdlib.h>

#define MAX_THREADS 16

static int thread_create(void (*function)(void));

static void empty_task(void)
{
}

int main(void)
{
    int ids[MAX_THREADS];
    int successful = 0;

    for (int i = 0; i < MAX_THREADS; i++) {
        ids[i] = thread_create(empty_task);

        if (ids[i] < 0) {
            printf("Creation failed at slot %d\n", i);
            break;
        }

        successful++;
    }

    printf("Threads created: %d\n", successful);
    return EXIT_SUCCESS;
}
