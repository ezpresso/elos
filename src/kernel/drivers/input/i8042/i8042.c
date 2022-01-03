/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2017, Elias Zell
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <kern/system.h>
#include <kern/sync.h>
#include <device/device.h>
#include <drivers/input/i8042.h>

#define STA_OBUF 	(1 << 0) /* Output buffer full */
#define STA_IBUF	(1 << 1) /* input buffer full */
#define STA_SYS		(1 << 2) /* System Flag */
#define STA_CMD		(1 << 3) /* 0: Data Byte, 1: Command Byte */
#define STA_TOE		(1 << 6) /* Timeout error */
#define STA_PE		(1 << 7) /* Parity Error */

#define CMD_CFG_RD	0x20
#define CMD_CFG_WR	0x60
#define CMD_2ND_DISABLE 0xA7
#define CMD_2ND_ENABLE	0xA8
#define CMD_2ND_TEST	0xA9
#define CMD_SELF_TEST	0xAA
#define CMD_1ST_TEST	0xAB
#define CMD_1ST_DISABLE	0xAD
#define CMD_1ST_ENABLE	0xAE
#define CMD_RD_INPUT	0xC0 /* Read Input Port */
#define CMD_RD_OUTPUT	0xD0 /* Read Output Port */
#define CMD_WR_OUTPUT	0xD1 /* Write Output Port */
#define CMD_WR_SCND_IN	0xD4
#define CMD_RD_TEST	0xE0 /* Read Test Inputs */
#define CMD_RST 	0xFF
#define 	RST_OK		0xFA
#define RESULT_ACK	0xFA

#define CFG_1ST_INTR	(1 << 0)
#define CFG_2ND_INTR	(1 << 1)
#define CFG_POST	(1 << 2)
#define CFG_1ST_CLK_DIS	(1 << 4)
#define CFG_2ND_CLK_DIS	(1 << 5)
#define CFG_TRANS	(1 << 6)

#define NUM_PORT 	2

typedef struct i8042_port {
	struct i8042_cntlr *cntlr;
	bool present;
	int idx;
} i8042_port_t;

typedef struct i8042_cntlr {
	bus_res_t cmd_res;
	bus_res_t data_res;

	sync_t lock;

	i8042_port_t ports[NUM_PORT];
	size_t num_ports;
} i8042_cntlr_t;

static inline uint8_t i8042_get_intr_flag(i8042_port_t *port) {
	static uint8_t flags[2] = { CFG_1ST_INTR, CFG_2ND_INTR };
	return flags[port->idx];
}

static inline uint8_t i8042_status(i8042_cntlr_t *cntlr) {
	return bus_res_readb(&cntlr->cmd_res, 0x0);
}

static inline uint8_t i8042_get_data(i8042_cntlr_t *cntlr) {
	return bus_res_readb(&cntlr->data_res, 0x0);
}

static inline void i8042_set_data(i8042_cntlr_t *cntlr, uint8_t data) {
	bus_res_writeb(&cntlr->data_res, 0x0, data);
}

static inline void i8042_cmd(i8042_cntlr_t *cntlr, uint8_t cmd) {
	bus_res_writeb(&cntlr->cmd_res, 0x0, cmd);
}

static inline uint8_t i8042_get_result(i8042_cntlr_t *cntlr) {
	while(!(i8042_status(cntlr) & STA_OBUF)) {
		continue;
	}
	return i8042_get_data(cntlr);
}

static inline void i8042_cmd_arg(i8042_cntlr_t *cntlr, uint8_t arg) {
	while(i8042_status(cntlr) & STA_OBUF) {
		continue;
	}
	i8042_set_data(cntlr, arg);
}

static inline void i8042_send_port(i8042_cntlr_t *cntlr, int port,
	uint8_t data)
{
	if(port == 0) {
		while(i8042_status(cntlr) & STA_IBUF) {
			continue;
		}

		i8042_set_data(cntlr, data);
	} else {
		i8042_cmd(cntlr, CMD_WR_SCND_IN);
		i8042_cmd_arg(cntlr, data);
	}
}

static inline uint8_t i8042_get_cfg(i8042_cntlr_t *cntlr) {
	i8042_cmd(cntlr, CMD_CFG_RD);
	return i8042_get_result(cntlr);
}

static inline void i8042_set_cfg(i8042_cntlr_t *cntlr, uint8_t cfg) {
	i8042_cmd(cntlr, CMD_CFG_WR);
	i8042_cmd_arg(cntlr, cfg);
}

static inline void i8042_enable_port(i8042_cntlr_t *cntlr, int port) {
	static const uint8_t cmds[] = { CMD_1ST_ENABLE, CMD_2ND_ENABLE };
	i8042_cmd(cntlr, cmds[port]);
}

static inline void i8042_disable_port(i8042_cntlr_t *cntlr, int port) {
	static const uint8_t cmds[] = { CMD_1ST_DISABLE, CMD_2ND_DISABLE };
	i8042_cmd(cntlr, cmds[port]);
}

static inline uint8_t i8042_test_port(i8042_cntlr_t *cntlr, int port) {
	static const uint8_t cmds[] = { CMD_1ST_TEST, CMD_2ND_TEST };
	i8042_cmd(cntlr, cmds[port]);
	return i8042_get_result(cntlr);
}

static uint8_t i8042_res_readb(bus_res_t *res, bus_off_t off) {
	i8042_cntlr_t *cntlr = device_priv(res->map.map_dev);

	synchronized(&cntlr->lock) {
		switch(off) {
		case I8042_DATA:
			return i8042_get_data(cntlr);
		case I8042_DATA_PRESENT:
			return !!(i8042_status(cntlr) & STA_OBUF);
		default:
			kpanic("[i8042] readb: invalid offset argument");
		}
	}

	notreached();
}

static bus_res_acc_t i8042_io_acc = {
	.readb = i8042_res_readb,
};

static int i8042_alloc_res(device_t *device, bus_res_req_t *req) {
	device_t *child = bus_res_get_device(req->res);
	i8042_port_t *port = bus_child_priv(child);
	int err = 0;

	if(req->bus_id != 0 || !BUS_ADDR_ANY(req->addr, req->end, req->size) ||
		device_get_parent(child) != device)
	{
		return -EINVAL;
	}

	switch(req->type) {
	case BUS_RES_INTR: {
		bus_adjust_resource(device, port->idx == 0 ? "intr-port0" :
			"intr-port1", req);
		return bus_generic_alloc_res(device, req);
	}
	case BUS_RES_IO:
		/*
		 * Allocate fake IO register.
		 */
		bus_res_init(req->res, 0, I8082_REGSZ);
		break;
	default:
		return -EINVAL;
	}

	return err;
}

static void i8042_free_res(__unused device_t *device, bus_res_t *res) {
	if(res->type == BUS_RES_INTR) {
		bus_generic_free_res(device, res);
	} else if(res->type == BUS_RES_IO) {
		bus_res_destroy(res);
	}
}

static int i8042_setup_res(device_t *device, bus_res_t *res) {
	device_t *child = bus_res_get_device(res);
	i8042_port_t *port = bus_child_priv(child);
	i8042_cntlr_t *cntlr = port->cntlr;

	if(res->type == BUS_RES_IO) {
		bus_res_set_acc(device, res, &i8042_io_acc);
	} else if(res->type == BUS_RES_INTR) {
		uint8_t config;
		int err;

		err = bus_generic_setup_res(device, res);
		if(err) {
			return err;
		}

		synchronized(&cntlr->lock) {
			/*
			 * Enable interrupts for that port.
			 */
			config = i8042_get_cfg(port->cntlr);
			config |= i8042_get_intr_flag(port);
			i8042_set_cfg(port->cntlr, config);
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

static void i8042_teardown_res(__unused device_t *device, bus_res_t *res) {
	device_t *child = bus_res_get_device(res);
	i8042_port_t *port = bus_child_priv(child);
	i8042_cntlr_t *cntlr = port->cntlr;
	uint8_t config;

	if(res->type != BUS_RES_INTR) {
		return;
	}

	/*
	 * Disable interrupts for that port.
	 */
	synchronized(&cntlr->lock) {
		/*
		 * For some reason I have to disable kbd to get cfg
		 * byte.
		 *
		 * I don't really understand the hw.
		 *
		 */
		i8042_disable_port(port->cntlr, 0);
		i8042_disable_port(port->cntlr, 1);

		/*
		 * Just in case if there is some data left.
		 */
		while(i8042_status(port->cntlr) & STA_OBUF) {
			i8042_get_data(port->cntlr);
		}

		config = i8042_get_cfg(port->cntlr);
		config &= ~i8042_get_intr_flag(port);
		i8042_set_cfg(port->cntlr, config);

		i8042_enable_port(port->cntlr, 0);
		i8042_enable_port(port->cntlr, 1);
	}

	bus_generic_teardown_res(device, res);
}

static void i8042_on_child_free(__unused device_t *bus, device_t *child) {
	bus_set_child_priv(child, NULL);
}

static int i8042_probe(__unused device_t *device) {
	return DEVICE_PROBE_OK;
}

static void i8042_setup_port(device_t *device, i8042_port_t *port) {
	i8042_cntlr_t *cntlr = port->cntlr;
	device_t *port_dev;
	uint16_t id;

	if(i8042_test_port(cntlr, port->idx) != 0x00) {
		goto error;
	}

	/*
	 * Enable the device.
	 */
	i8042_enable_port(cntlr, port->idx);

	/*
	 * Make sure that we don't get input from the device, which
	 * could mess with the data sent back from identify command!
	 */
	i8042_send_port(cntlr, port->idx, 0xF5);
	if(i8042_get_result(cntlr) != RESULT_ACK) {
		goto error;
	}

	/*
	 * Send identify command.
	 */
	i8042_send_port(cntlr, port->idx, 0xF2);
	if(i8042_get_result(cntlr) != RESULT_ACK) {
		goto error;
	}

	/*
	 * Retrieve the result.
	 */
	id = i8042_get_result(cntlr);
	if(id == 0xAB) {
		id |= i8042_get_result(cntlr) << 8;
	}

	/*
	 * Enable input again.
	 */
	i8042_send_port(cntlr, port->idx, 0xF4);
	if(i8042_get_result(cntlr) != RESULT_ACK) {
		goto error;
	}

	port->present = true;

	port_dev = bus_add_child_at(device, NULL, port->idx);
	bus_set_child_priv(port_dev, port);
	device_add_id(port_dev, "ps2", id, device_generic_match_id);

	return;

error:
	device_printf(device, "Port %d not working\n", port->idx);
	return;
}

static int i8042_attach(device_t *device) {
	i8042_cntlr_t *cntlr = device_priv(device);
	uint8_t config, i;
	int err;

	err = bus_alloc_res_name(device, BUS_RES_IO, 0, "io-data",
			RF_MAP, &cntlr->data_res);
	if(err) {
		return err;
	}

	err = bus_alloc_res_name(device, BUS_RES_IO, 0, "io-cmd",
			RF_MAP, &cntlr->cmd_res);
	if(err) {
		goto err_cmd_res;
	}

	cntlr->num_ports = 2;
	sync_init(&cntlr->lock, SYNC_SPINLOCK);

	/*
	 * Setup the two ports.
	 */
	for(i = 0; i < cntlr->num_ports; i++) {
		cntlr->ports[i].cntlr = cntlr;
		cntlr->ports[i].idx = i;
	}

	/*
	 * Disable both ports.
	 */
	for(i = 0; i < cntlr->num_ports; i++) {
		i8042_disable_port(cntlr, i);
	}

	/*
	 * Flush output buffer.
	 */
	while(i8042_status(cntlr) & STA_OBUF) {
		i8042_get_data(cntlr);
	}

	config = i8042_get_cfg(cntlr);
	if(!(config & CFG_2ND_CLK_DIS)) {
		device_printf(device, "No second port\n");
		cntlr->num_ports = 1;
	}

	/*
	 * Leave translation enabled (for now) and disable interrupts.
	 */
	config &= ~(/* CFG_TRANS | */ CFG_1ST_INTR | CFG_2ND_INTR);
	i8042_set_cfg(cntlr, config);

	/*
	 * Self test.
	 */
	i8042_cmd(cntlr, CMD_SELF_TEST);
	if(i8042_get_result(cntlr) != 0x55) {
		device_printf(device, "Self test failed\n");
		goto err_map;
	}

	if(cntlr->num_ports == 2) {
		i8042_enable_port(cntlr, 1);
		config = i8042_get_cfg(cntlr);
		if(config & CFG_2ND_CLK_DIS) {
			cntlr->num_ports = 1;
		} else {
			i8042_disable_port(cntlr, 1);
		}
	}

	for(i = 0; i < cntlr->num_ports; i++) {
		i8042_setup_port(device, &cntlr->ports[i]);
	}

	device_set_desc(device, "PS/2 Controller");
	bus_probe_children(device);

	return 0;

err_map:
	bus_free_res(device, &cntlr->cmd_res);
err_cmd_res:
	bus_free_res(device, &cntlr->data_res);
	return 0;
}

static int i8042_detach(device_t *device) {
	i8042_cntlr_t *cntlr = device_priv(device);
	size_t i;
	int err;

	err = bus_generic_detach(device);
	if(err) {
		return err;
	}

	for(i = 0; i < cntlr->num_ports; i++) {
		i8042_disable_port(cntlr, i);
	}

	bus_free_res(device, &cntlr->cmd_res);
	bus_free_res(device, &cntlr->data_res);

	sync_destroy(&cntlr->lock);

	return 0;
}

static bus_driver_t i8042_bus_driver = {
	.bus_alloc_res = i8042_alloc_res,
	.bus_free_res = i8042_free_res,
	.bus_setup_res = i8042_setup_res,
	.bus_teardown_res = i8042_teardown_res,
	.bus_child_free = i8042_on_child_free,
};

static DRIVER(i8042_driver) {
	.name = "i8042",
	.flags = DEV_DEP_INTR,
	.type = DEVICE_INPUT,
	.private = sizeof(i8042_cntlr_t),
	.probe = i8042_probe,
	.attach = i8042_attach,
	.detach = i8042_detach,
	.new_pass = bus_generic_new_pass,
	.bus = &i8042_bus_driver,
};
