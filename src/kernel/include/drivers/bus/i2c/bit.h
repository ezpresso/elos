#ifndef DRIVERS_BUS_I2C_BIT_H
#define DRIVERS_BUS_I2C_BIT_H

typedef struct i2c_bit_arg {
#if 0
	void (*setsda) (device_t *device, device_t *i2c, int state);
	void (*setscl) (device_t *device, device_t *i2c, int state);
	int (*getsda) (device_t *device, device_t *i2c);
	int (*getscl) (device_t *device, device_t *i2c);
#endif

	int udelay;
	int timeout;
} i2c_bit_arg_t;

#endif