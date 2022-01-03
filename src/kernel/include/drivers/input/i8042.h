#ifndef DEVICE_INPUT_I8042_H
#define DEVICE_INPUT_I8042_H

/**
 * @brief Registers which the controller emulates for the child devices.
 */
enum {
	I8042_DATA, /* rw */
	I8042_DATA_PRESENT,  /* r */

	I8082_REGSZ,

#if 0
	/* Not implemented */
	I8042_CMD, /* send command to device */
	I8042_CMD_END, /* finished with command */
#endif
};

#endif