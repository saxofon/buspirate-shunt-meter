/*
 * Copyright (C) 2012 Per Hallsmark <per@hallsmark.se>
 *
 * (see the files README and COPYING for more details)
 */

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
#include <rb.h>

#define BP_DEV "/dev/buspirate"

#undef DEBUG
#undef RT_PRIO
#undef CONTINOUS

// define the resistance of the shunt we have in serie with the power feed.
#define SHUNT_RESISTANCE 1.0

// define our sample period
// for continues mode, this depends on baudrate. We avarage to 10 bits and 2 bytes per read...
#ifdef CONTINOUS
#define SAMPLE_PERIOD_US      (1000000.0/(BP_DEV_BAUDRATE/10.0/2.0))
#else
#define SAMPLE_PERIOD_US      100000.0
#endif
//#define SAMPLE_AVERAGES       ((int)(USEC_PER_SEC/SAMPLE_PERIOD_US))
#define SAMPLE_AVERAGES       1

// Circular buffer storing ADC data
// One buf-short represent one ADC reading, MSB in high byte.
// Storage size set to one hour
#define BUFSZ_HOURS    0
#define BUFSZ_MINUTES  0
#define BUFSZ_SECOUNDS 10
#define BUFSZ (int)((1000000.0/SAMPLE_PERIOD_US) * (BUFSZ_HOURS*3600+BUFSZ_MINUTES*60+BUFSZ_SECOUNDS))

static struct s_adc {
	struct s_bp *bp;
	struct s_rb *rb;
	float period;
	int average; // amount of samples to avarage over
} adc;

// adc_sampler - this is a bp callback function
static void adc_sampler(unsigned short reading)
{
	int status;

	// advance buffer pointer
	status = rb_head_n_tail_update(adc.rb);

	RB_NODE(adc.rb, unsigned short) = reading;
//	((unsigned short*)(adc.rb->buf))[adc.rb->head] = reading;
//	adc.buf[adc.head] = reading;
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
	unsigned short node_data;

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
		if (average_nr > rb_head_n_tail_diff(adc->rb))
			average_nr = rb_head_n_tail_diff(adc->rb);

		if (average_nr == 0)
			average = 1;

		// calculate average
		average = 0.0;
		for (i=0; i<average_nr; i++) {
#ifdef DEBUG
			printf("%s: start %d\n", __FUNCTION__, start);
#endif
			node_data = RB_NODE_IDX(adc->rb, unsigned short,
				rb_head_minus(adc->rb,BUFSZ - i));
			average += node_data/1024.0*6.6;
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

	// init super structure
	memset(&adc, 0, sizeof(adc));
	adc.bp = (struct s_bp *)malloc(sizeof(struct s_bp));
	memset(adc.bp, 0, sizeof(struct s_bp));
	adc.rb = (struct s_rb *)malloc(sizeof(struct s_rb));
	memset(adc.rb, 0, sizeof(struct s_rb));

	// initialize buspirate
	status = bp_init(adc.bp, BP_DEV);
	if (status) {
		exit(-1);
	}

	// install our ADC reading callback
	bp_install_adc_read(adc.bp, adc_sampler);

	// initialize adc structure
	adc.period = SAMPLE_PERIOD_US;
	adc.average = SAMPLE_AVERAGES;
	status = rb_init(adc.rb, BUFSZ, sizeof(unsigned short));
	if (status) {
		exit(-1);
	}

//	status = rb_test();
//	exit(0);

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
