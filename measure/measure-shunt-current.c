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

#include <ts.h>
#include <buspirate.h>

#define BP_DEV "/dev/buspirate"

#define DEBUG
#undef RT_PRIO
#define CONTINOUS

// define the resistance of the shunt we have in serie with the power feed.
#define SHUNT_RESISTANCE 1.0


// define our sample period
// for continues mode, this depends on baudrate. We avarage to 10 bits and 2 bytes per read...
#ifdef CONTINOUS
#define SAMPLE_PERIOD_US      (1000000.0/(BP_DEV_BAUDRATE/10.0/2.0))
#else
#define SAMPLE_PERIOD_US      25000.0
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
	int average; // amount of samples to avarage over
} adc;

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
//	printf("%s: %f V\n", __FUNCTION__, adc.buf[adc.head]/1024.0*6.6);
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

#ifdef RT_PRIO
	// setup thread priority
	threadsched.sched_priority = 10;

	status = sched_setscheduler(0, SCHED_FIFO, &threadsched);
	if (status) {
		printf("%s: sched_setscheduler %d\n", __FUNCTION__, status);
		pthread_exit(&adc_trig_sample_status);
	}
#endif

	// never let us be swapped out
	mlockall(MCL_CURRENT | MCL_FUTURE);

	// setup our periodic clock
	period.tv_sec = adc->period / USEC_PER_SEC;
	period.tv_nsec = adc->period * NSEC_PER_USEC - 
			period.tv_sec * NSEC_PER_USEC;
	ts_normalize(&period);

#ifdef DEBUG
	printf("%s: period.tv_sec %d period.tv_nsec %d\n",
		__FUNCTION__, period.tv_sec, period.tv_nsec);
#endif

	printf ("%s: ready to loop\n", __FUNCTION__);
	clock_gettime(CLOCK_MONOTONIC, &mark);
	for (;;) {
		mark = ts_add(mark, period);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &mark, NULL);

		bp_read_adc_singleshot(adc->bp);
	}
}
#endif

static pthread_t adc_sample_avarage_tid;
static int adc_sample_avarage_status;
static void *adc_sample_avarage(void * arg)
{
	int status;
	struct timespec mark, period;
	struct sched_param threadsched;
	struct s_adc *adc = (struct s_adc *)arg;
	int start;
	int average_nr;
	float average;
	int i;

	printf ("%s: begin\n", __FUNCTION__);

#ifdef RT_PRIO
	// setup thread priority
	threadsched.sched_priority = 9;

	status = sched_setscheduler(0, SCHED_FIFO, &threadsched);
	if (status) {
		printf("%s: sched_setscheduler %d\n", __FUNCTION__, status);
		pthread_exit(&adc_sample_avarage_status);
	}
#endif

	// never let us be swapped out
	mlockall(MCL_CURRENT | MCL_FUTURE);

	// setup our periodic clock
	period.tv_nsec = (adc->period*adc->average) * NSEC_PER_USEC;
	ts_normalize(&period);

#ifdef DEBUG
	printf("%s: period.tv_sec %d period.tv_nsec %d\n",
		__FUNCTION__, period.tv_sec, period.tv_nsec);
#endif

	clock_gettime(CLOCK_MONOTONIC, &mark);
	for (;;) {
		mark = ts_add(mark, period);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &mark, NULL);

		// calulate nr of sample to average over
		average_nr = adc->average;
		if (average_nr > adc_head_n_tail_diff(adc))
			average_nr = adc_head_n_tail_diff(adc);

		if (average_nr == 0)
			average = 1;

		// calculate average
		start = adc->head - average_nr;
		if (start < 0)
			start = BUFSZ + start;
		average = 0.0;
		for (i=0; i<average_nr; i++) {
			average += adc->buf[start++]/1024.0*6.6;
		}
		average = average/average_nr;


		printf("%s: averaging over %d samples gives %f A\n",
			__FUNCTION__, average_nr, average / SHUNT_RESISTANCE);

		
	}
}

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
	adc.average = 8192;
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
	// setup buspirate to just shuffle readings
	bp_read_adc_continous(adc.bp);
#else
	// create thread for triggering adc sampling
	status = pthread_create(&tid, NULL, adc_trig_sample, &adc);
#endif

	// create thread for calculating and displaying avarage current
	status = pthread_create(&tid, NULL, adc_sample_avarage, &adc);

	for (;;) {
		sleep(1);
	}

	exit(0);
}
