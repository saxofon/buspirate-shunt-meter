#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <asm/fcntl.h>
#include <linux/mman.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sched.h>
#include <termios.h>
#include <errno.h>
#include <asm/ioctls.h>

#include "ts.h"

void ts_normalize(struct timespec *t)
{
        while (t->tv_nsec >= NSEC_PER_SEC) {
                t->tv_sec++;
                t->tv_nsec -= NSEC_PER_SEC;
        }
	if ((t->tv_sec >= 1) && (t->tv_nsec < 0)) {
                t->tv_sec--;
                t->tv_nsec += NSEC_PER_SEC;
	}
}

struct timespec ts_add(struct timespec t1, struct timespec t2)
{
        struct timespec result;

        result.tv_sec = t1.tv_sec + t2.tv_sec;
        result.tv_nsec = t1.tv_nsec + t2.tv_nsec;

	ts_normalize(&result);

        return (result);
}
