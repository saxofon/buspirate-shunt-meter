#ifndef __BUSPIRATE_H__
#define __BUSPIRATE_H__

#define DEBUG

#define BP_DEV_BAUDRATE             115200
#define BP_MAX_READ_LEN             128

#define BP_CMD_STATE_BINARY         0x00
#define BP_CMD_STATE_BINARY_SPI     0x01
#define BP_CMD_STATE_BINARY_I2C     0x02
#define BP_CMD_STATE_BINARY_UART    0x03
#define BP_CMD_STATE_BINARY_1WIRE   0x04
#define BP_CMD_STATE_BINARY_RAW     0x05
#define BP_CMD_STATE_ASCII          0x0F
#define BP_CMD_STATE_UNKNOWN        0xFF

#define BP_CMD_IDLE                 0x100
#define BP_CMD_RESET                0x00
#define BP_CMD_TO_SPI               0x01
#define BP_CMD_TO_I2C               0x02
#define BP_CMD_TO_UART              0x03
#define BP_CMD_TO_1WIRE             0x04
#define BP_CMD_TO_RAW               0x05
#define BP_CMD_TO_RESET_INTO_ASCII  0x0F
#define BP_CMD_SELFTEST             0x10
#define BP_CMD_LONG_TEST            0x11
#define BP_CMD_SETUP_PWM            0x12
#define BP_CMD_DISABLE_PWM          0x13
#define BP_CMD_ADC_READ_SINGLESHOT  0x14
#define BP_CMD_ADC_READ_CONTINOUS   0x15
#define BP_CMD_GPIO_DIR(x)         (0x40 + x)
#define BP_CMD_GPIO_VAL(x)         (0x60 + x)

#define BP_ATTEMPTS                 40

static struct s_bp {
	int dev;
	int cmd_state;
	int cmd;
	int read_length;
	unsigned char buf[BP_MAX_READ_LEN];
	void (*adc_read)(unsigned short);
} bp;

// main routines
extern int bp_init(struct s_bp *bp, char *dev);


// ADC routines
extern void bp_install_adc_read(struct s_bp *bp, void (*adc_read)(unsigned short));
extern void bp_read_adc_singleshot(struct s_bp *bp);
extern void bp_read_adc_continous(struct s_bp *bp);

#endif
