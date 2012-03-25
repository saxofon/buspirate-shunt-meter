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

#include <buspirate.h>

#define BP_DEV "/dev/buspirate"

#define CONTINOUS

#define NSEC_PER_SEC   1000000000UL
#define NSEC_PER_MSEC     1000000UL
#define NSEC_PER_USEC        1000UL
#define USEC_PER_SEC      1000000UL
#define USEC_PER_MSEC        1000UL
#define MSEC_PER_SEC         1000UL


// define our sample period
// for continues mode, this depends on baudrate. We avarage to 10 bits and 2 bytes per read...
#ifdef CONTINOUS
#define SAMPLE_PERIOD_US      (1000000.0/(BP_DEV_BAUDRATE/10.0/2.0))
#else
#define SAMPLE_PERIOD_US      500000.0
#endif

// Circular buffer storing ADC data
// One buf-short represent one ADC reading, MSB in high byte.
// Storage size set to one hour
#define BUFSZ_HOURS    1
#define BUFSZ_MINUTES  0
#define BUFSZ_SECOUNDS 0
#define BUFSZ (int)((1000000.0/SAMPLE_PERIOD_US) * (BUFSZ_HOURS*3600+BUFSZ_MINUTES*60+BUFSZ_SECOUNDS))

static struct s_adc {
	struct s_bp *bp;
	int head;
	int tail;
	float period;
	unsigned short *buf;
} adc;

static struct timespec add_ts(struct timespec time1, struct timespec time2)
{
        struct timespec result;

        result.tv_sec = time1.tv_sec + time2.tv_sec;
        result.tv_nsec = time1.tv_nsec + time2.tv_nsec;
        if (result.tv_nsec >= 1000000000UL) {
                result.tv_sec++;
                result.tv_nsec -= 1000000000UL;
        }

        return (result);
}

// adc_head_n_tail_diff return the active amount of sample data
// retval > 0: active amount of data
// retval == 0: no data collected
static int adc_head_n_tail_diff(struct s_adc *adc)
{
	int diff;

	if (adc->head >= adc->tail) {
		diff = adc->head - adc->tail;
	} else {
		diff= adc->head + (BUFSZ - adc->tail) + 1;
	}

//	printf("%s: diff %d\n", __FUNCTION__, diff);

	return diff;
}

// adc_head_n_tail_update - advances data head pointer and possibly the
// tail pointer in case we need to circulate over old data (which happens
// when recording is longer than buffer size admit)
static int adc_head_n_tail_update(struct s_adc *adc)
{
	// update tail pointer - only if we need to circulate over old data
	if (adc_head_n_tail_diff(adc) == BUFSZ) {
		if (adc->tail == BUFSZ) {
			adc->tail = 0;
		} else {
			adc->tail++;
		}
	}

	// update head pointer
	if (adc->head == BUFSZ) {
		adc->head = 0;
	} else {
		adc->head++;
	}

//	printf("%s: head set to %d and tail set to %d\n", __FUNCTION__,
//		adc->head, adc->tail);
}

// adc_sampler - this is a bp callback function
static void adc_sampler(unsigned short reading)
{
	int status;

	// advance buffer pointer
	status = adc_head_n_tail_update(&adc);

	adc.buf[adc.head] = reading;
	printf("%s: %f V\n", __FUNCTION__, adc.buf[adc.head]/1024.0*6.6);
}

#ifndef CONTINOUS
static pthread_t adc_trig_sample_tid;
static int adc_trig_sample_status;
static void *adc_trig_sample(void *arg)
{
	int status;
	struct timespec mark, period;
	struct sched_param threadsched;
	struct s_adc *adc = (struct s_adc *)arg;

	printf ("%s: begin\n", __FUNCTION__);

#if 0
	// setup thread priority
	threadsched.sched_priority = 10;

	status = sched_setscheduler(0, SCHED_FIFO, &threadsched);
	if (status) {
		printf("%s: sched_setscheduler %d\n", __FUNCTION__, status);
		pthread_exit(&adc_trig_sample_status);
	}

	// never let us be swapped out
	mlockall(MCL_CURRENT | MCL_FUTURE);
#endif

	// setup our periodic clock
	period.tv_sec = adc->period / USEC_PER_SEC;
	period.tv_nsec = adc->period * NSEC_PER_USEC - 
			period.tv_sec * NSEC_PER_USEC;

	printf ("%s: ready to loop\n", __FUNCTION__);
	clock_gettime(CLOCK_MONOTONIC, &mark);
	for (;;) {
		mark = add_ts(mark, period);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &mark, NULL);

		bp_read_adc_singleshot(adc->bp);
	}
}

static pthread_t adc_sample_avarage_tid;
static int adc_sample_avarage_status;
static void *adc_sample_avarage(void * arg)
{
	int status;
	struct timespec mark, period;
	struct sched_param threadsched;
	struct s_adc *adc = (struct s_adc *)arg;

	printf ("%s: begin\n", __FUNCTION__);

#if 0
	// setup thread priority
	threadsched.sched_priority = 9;

	status = sched_setscheduler(0, SCHED_FIFO, &threadsched);
	if (status) {
		printf("%s: sched_setscheduler %d\n", __FUNCTION__, status);
		pthread_exit(&adc_sample_avarage_status);
	}

	// never let us be swapped out
	mlockall(MCL_CURRENT | MCL_FUTURE);
#endif

	// setup our periodic clock
	period.tv_sec = adc->period / USEC_PER_SEC;
	period.tv_nsec = adc->period * NSEC_PER_USEC - 
			period.tv_sec * NSEC_PER_USEC;

	printf ("%s: ready to loop\n", __FUNCTION__);
	clock_gettime(CLOCK_MONOTONIC, &mark);
	for (;;) {
		mark = add_ts(mark, period);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &mark, NULL);

		// avarage over X samples
		printf ("%s: in the loop\n", __FUNCTION__);
	}
}
#endif


int main(int argc, char *argv[])
{
	int status;
	pthread_t tid;

	// init circular buffer
	memset(&adc, 0, sizeof(adc));

	// initialize buspirate
	status = bp_init(&bp, BP_DEV);
	if (status) {
		exit(-1);
	}

	// install our ADC reading callback
	bp_install_adc_read(&bp, adc_sampler);

	// initialize adc structure
	adc.bp = &bp;
	adc.period = SAMPLE_PERIOD_US;
	adc.buf = malloc(BUFSZ*2);
	if (!adc.buf) {
		printf("%s: out of mem\n", __FUNCTION__);
	} else {
		printf("%s: allocated %d bytes for circular buffer\n",
			__FUNCTION__, BUFSZ*2);
	}

	printf("Buspirate ADC sample-n-avarage\n");
	printf("  Current config:\n");
	printf("    ADC sample period    : %f us\n", adc.period); 

#ifdef CONTINOUS
	bp_read_adc_continous(adc.bp);
#else
	// create thread for triggering adc sampling
	status = pthread_create(&tid, NULL, adc_trig_sample, &adc);

	// create thread for calculating and displaying avarage current
//	status = pthread_create(&tid, NULL, adc_sample_avarage, &adc);
#endif

	for (;;) {
		sleep(1);
	}

	exit(0);
}
