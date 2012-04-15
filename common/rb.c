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

#include <rb.h>

// rb_head_n_tail_diff return the active amount of sample data
// retval > 0: active amount of data
// retval == 0: no data collected
int rb_head_n_tail_diff(struct s_rb *rb)
{
	int diff;

	__sync_synchronize();
	if (rb->head >= rb->tail) {
		diff = rb->head - rb->tail;
	} else {
		diff= rb->head + (rb->nr_of_nodes - rb->tail) + 1;
	}

//	printf("%s: diff %d\n", __FUNCTION__, diff);

	return diff;
}

// rb_head_n_tail_update - advances data head pointer and possibly the
// tail pointer in case we need to circulate over old data (which happens
// when recording is longer than buffer size admit)
int rb_head_n_tail_update(struct s_rb *rb)
{
	// update tail pointer - only if we need to circulate over old data
	__sync_synchronize();
	if (rb_head_n_tail_diff(rb) == rb->nr_of_nodes) {
		if (rb->tail == rb->nr_of_nodes) {
			rb->tail = 0;
		} else {
			rb->tail++;
		}
	}

	// update head pointer
	__sync_synchronize();
	if (rb->head == rb->nr_of_nodes) {
		rb->head = 0;
	} else {
		rb->head++;
	}

//	printf("%s: head set to %d and tail set to %d\n", __FUNCTION__,
//		rb->head, rb->tail);
}

int rb_head_minus(struct s_rb *rb, int minus)
{
	int ofs;

	// first check if we have less than requested minus offset
	if (rb_head_n_tail_diff(rb) < minus)
		return RB_TAIL(rb);

	ofs = RB_HEAD(rb) - minus;

	if (ofs < 0)
		ofs = rb->nr_of_nodes - abs(RB_HEAD(rb) - minus);

	return ofs;
}

int rb_init(struct s_rb *rb, int nr_of_nodes, int node_sz)
{
	rb->nr_of_nodes = nr_of_nodes;
	rb->buf = malloc(nr_of_nodes*node_sz);
	if (!rb->buf) {
		printf("%s: out of mem\n", __FUNCTION__);
	} else {
		printf("%s: allocated %d bytes for ring buffer\n",
			__FUNCTION__, nr_of_nodes*node_sz);
	}
	memset(rb->buf, 0, nr_of_nodes*node_sz);
	return errno;
}

int rb_free(struct s_rb *rb)
{
	if (rb->buf)
		free(rb->buf);
	rb->buf = NULL;
	return 0;
}

int rb_info(struct s_rb *rb)
{
	printf("  buf         0x%X\n", rb->buf);
	printf("  head        %d\n", rb->head);
	printf("  tail        %d\n", rb->tail);
	printf("  nr_of_nodes %d\n", rb->nr_of_nodes);
}

int rb_test(void)
{
	int status;
	int i, j;
	struct s_rb *rb;

	printf("%s: start\n", __FUNCTION__);

	rb = (struct s_rb *)malloc(sizeof(struct s_rb));
        memset(rb, 0, sizeof(struct s_rb));

	status = rb_init(rb, 20, sizeof(int));
        if (status) {
                exit(-1);
        }

	rb_info(rb);

	for (j=0; j<5; j++) {
		// add some test data
		for (i=0; i<(rb->nr_of_nodes/2); i++) {
			printf("%s: adding node %d\n", __FUNCTION__,
				j*(rb->nr_of_nodes/2)+i);
			rb_info(rb);
			RB_NODE(rb, unsigned short) = j*(rb->nr_of_nodes/2)+i;
			status = rb_head_n_tail_update(rb);
		}

		// print out test data
		for (i=0; i<(rb->nr_of_nodes/2); i++) {
			printf("%s: node %d contains %d\n", __FUNCTION__,
				j*(rb->nr_of_nodes/2)+i,
				RB_NODE_IDX(rb, unsigned short, rb_head_minus(rb,(rb->nr_of_nodes/2)-i)));
		}
	}
}
