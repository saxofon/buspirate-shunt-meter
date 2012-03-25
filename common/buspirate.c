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

static int optimize_read_length(struct s_bp *bp, int len)
{
	int status;
	struct termios stios;

	status = tcgetattr(bp->dev, &stios);
	if (status) {
		return errno;
	}
	stios.c_cc[VMIN] = len;
	stios.c_cc[VTIME] = 0;
	status = tcsetattr(bp->dev, TCSANOW, &stios);
	if (status) {
		return errno;
	}

	return 0;
}

int bp_reset(struct s_bp *bp)
{
	char cmd[16];
	int i;

	// enter HiZ> state
	sprintf(cmd, "\n");
	for (i=0; i<BP_ATTEMPTS; i++) {
		if (bp->cmd_state == BP_CMD_STATE_UNKNOWN) {
			write(bp->dev, cmd, strlen(cmd));
			sleep(1);
		} else {
			break;
		}
	}

	if (i == BP_ATTEMPTS) {
		return -1;
	}

	// reset
	sprintf(cmd, "#\n");
	write(bp->dev, cmd, strlen(cmd));
	sleep(1);

	return 0;
}

int bp_enter_bin(struct s_bp *bp)
{
	char cmd;
	int i;

	cmd = 0;
	for (i=0; i<BP_ATTEMPTS; i++) {
		if (bp->cmd_state != BP_CMD_STATE_BINARY) {
			write(bp->dev, &cmd, 1);
			sleep(1);
//			usleep(1000);
		} else {
			return 0;
		}
	}

	return -1;
}

void bp_install_adc_read(struct s_bp *bp, void (*adc_read)(unsigned short))
{
	if (adc_read) {
		bp->adc_read = adc_read;
	}
}

void bp_read_adc_singleshot(struct s_bp *bp)
{
	optimize_read_length(bp, 2);
	bp->cmd = BP_CMD_ADC_READ_SINGLESHOT;
	write(bp->dev, &(bp->cmd), 1);
}

void bp_read_adc_continous(struct s_bp *bp)
{
	optimize_read_length(bp, 2);
	bp->read_length = 2;
	bp->cmd = BP_CMD_ADC_READ_CONTINOUS;
	write(bp->dev, &(bp->cmd), 1);
}


// bp_reader -
static pthread_t bp_reader_tid;
static int bp_reader_status;
static void *bp_reader(void *arg)
{
	int status;
	int i;
	struct sched_param threadsched;
	unsigned char cmd;
	struct termios stios;
	unsigned short adc_sample;
	struct s_bp *bp = (struct s_bp *)arg;

	printf ("%s: begin\n", __FUNCTION__);

#if 0
	// setup thread priority
	threadsched.sched_priority = 50;

	status = sched_setscheduler(0, SCHED_FIFO, &threadsched);
	if (status) {
		printf("%s: sched_setscheduler %d\n", __FUNCTION__, status);
		pthread_exit(&bp_reader_status);
	}

	// lock thread code into memory (don't swap this code segment)
	mlockall(MCL_CURRENT | MCL_FUTURE);
#endif

	for (;;) {
		if (bp->read_length) {
			memset(bp->buf, 0, bp->read_length);
			status = read(bp->dev, bp->buf, bp->read_length);
		} else {
			memset(bp->buf, 0, sizeof(bp->buf));
			status = read(bp->dev, bp->buf, sizeof(bp->buf));
		}
		if (status == -1) {
			printf("%s: ERROR %d\n", __FUNCTION__, errno);
		} else if ((status == 2) && (bp->cmd == BP_CMD_ADC_READ_SINGLESHOT)) {
			if (bp->adc_read) {
				memcpy(&adc_sample, bp->buf, 2);
				swab(&adc_sample, &adc_sample, 2);
				bp->adc_read(adc_sample);
			}
			bp->cmd == BP_CMD_IDLE;
		} else if ((status == 2) && (bp->cmd == BP_CMD_ADC_READ_CONTINOUS)) {
			if (bp->adc_read) {
				memcpy(&adc_sample, bp->buf, 2);
				swab(&adc_sample, &adc_sample, 2);
				bp->adc_read(adc_sample);
			}
		} else {
			if (!strncmp(bp->buf, "HiZ>", 4)) {
				// default mode, need to enter binary mode
				bp->cmd_state = BP_CMD_STATE_ASCII;
				printf("%s: entered cmd state ascii\n", __FUNCTION__);
				close(bp->dev);
				bp->dev = -1;
			} else if (!strncmp(bp->buf, "BBIO1", 5)) {
				bp->cmd_state = BP_CMD_STATE_BINARY;
				printf("%s: entered cmd state binary\n", __FUNCTION__);
			} else if (!strncmp(bp->buf, "SPI1", 4)) {
				bp->cmd_state = BP_CMD_STATE_BINARY_SPI;
				printf("%s: entered cmd state SPI\n", __FUNCTION__);
			} else if (!strncmp(bp->buf, "I2C1", 4)) {
				bp->cmd_state = BP_CMD_STATE_BINARY_I2C;
				printf("%s: entered cmd state i2c\n", __FUNCTION__);
			} else if (!strncmp(bp->buf, "ART1", 4)) {
				bp->cmd_state = BP_CMD_STATE_BINARY_UART;
				printf("%s: entered cmd state uart\n", __FUNCTION__);
			} else if (!strncmp(bp->buf, "1W01", 4)) {
				bp->cmd_state = BP_CMD_STATE_BINARY_1WIRE;
				printf("%s: entered cmd state 1-wire\n", __FUNCTION__);
			} else if (!strncmp(bp->buf, "RAW1", 4)) {
				bp->cmd_state = BP_CMD_STATE_BINARY_RAW;
				printf("%s: entered cmd state raw\n", __FUNCTION__);
			} else {
				// unknown response
				printf("%s: status %d, buf %s\n", __FUNCTION__, status, bp->buf);
			}
		}
	}
}

int bp_init(struct s_bp *bp, char *dev)
{
	int i;
	int status;
	struct termios stios;
	pthread_t tid;

	memset(bp, 0, sizeof(struct s_bp));

	// open device
	bp->dev = open(dev, O_RDWR | O_NOCTTY | O_SYNC);
	if (bp->dev == -1) {
		printf("%s: couldn't open %s\n", __FUNCTION__, dev);
		return -1;
	}

	// setup 115200n81 and 1 byte buf length
	status = tcgetattr(bp->dev, &stios);
	stios.c_iflag = IGNBRK | IGNCR | IXANY;
	stios.c_oflag = 0;
	stios.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
	stios.c_lflag = NOFLSH;
	tcflush(bp->dev, TCIOFLUSH);
	status = tcsetattr(bp->dev, TCSANOW, &stios);

	optimize_read_length(bp, 1);

	// create thread for buspirate feedback
	status = pthread_create(&tid, NULL, bp_reader, bp);

	// we don't know which state buspirate was left in
	bp->cmd_state = BP_CMD_STATE_UNKNOWN;

#if 0
	// reset
	bp_reset(bp);
#endif

	// use binary protocol
	if (bp_enter_bin(bp)) {
		printf("%s: couldn't set buspirate to binary protocol!\n",
		__FUNCTION__);
		return -1;
	}

	return 0;
}
