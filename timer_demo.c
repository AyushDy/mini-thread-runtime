
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

static volatile sig_atomic_t alarm_count = 0;

static void handle_alarm(int signal_number)
{
    (void)signal_number;
    alarm_count++;
}

int main(void)
{
    struct sigaction sa = {0};
    struct itimerval timer = {0};

    sa.sa_handler = handle_alarm;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    /* First alarm after 1 second; repeat every 1 second. */
    timer.it_value.tv_sec = 1;
    timer.it_interval.tv_sec = 1;

    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        perror("setitimer");
        return 1;
    }

    printf("Starting busy loop. Press Ctrl+C to stop.\n");

    unsigned long long work = 0;
    sig_atomic_t reported = 0;

    while (1) {
        work++;

        if (alarm_count != reported) {
            reported = alarm_count;
            printf("Timer signals received: %d, work: %llu\n",
                   (int)reported, work);
        }
    }

    return 0;
}
