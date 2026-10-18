
#include <stdio.h>

int main(void)
{
    setbuf(stdout, NULL);

    printf("Task A started\n");

    while (1) {}

    printf("Task B finished\n");
    return 0;
}
