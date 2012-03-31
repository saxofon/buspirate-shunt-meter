#ifndef __TS_H__
#define __TS_H__

#define NSEC_PER_SEC   1000000000UL
#define NSEC_PER_MSEC     1000000UL
#define NSEC_PER_USEC        1000UL
#define USEC_PER_SEC      1000000UL
#define USEC_PER_MSEC        1000UL
#define MSEC_PER_SEC         1000UL

extern void ts_normalize(struct timespec *t);
extern struct timespec ts_add(struct timespec t1, struct timespec t2);

#endif

