#ifndef KERN_IPC_H
#define KERN_IPC_H

/**
 * @brief IPC port number.
 */
typedef unsigned ipc_portnum_t;

/**
 * @brief An IPC connection id.
 */
typedef unsigned ipc_connnum_t;

typedef struct ipc_port {
	ipc_portnum_t num;
	int fd;
} ipc_port_t;

/**
 * @brief IPC message header.
 */
typedef struct ipc_msg {
	ipc_portnum_t source;
	ipc_portnum_t dest;

	/**
	 * @brief The size of the complete message.
	 */
	uint16_t size;

#define IPC_MF_ACK	(1 << 0)
	uint8_t flags;

	/**
	 * @brief The type of an IPC message.
	 * Some IPC messages may be interpreted by the kernel.
	 */
#define IPC_MT_RAW	0 /* raw data */
#define IPC_MT_SHARE	1 /* share a memory buffer */
#define IPC_MT_ACK 	2 /* acknowledge message */
#define IPC_MT_OPEN	3 /* open a new connection */
#define IPC_MT_CLOSE	4 /* the other port of the connection was closed */
	uint16_t type;
} ipc_msg_t;

/**
 * A server may get the pid of an client process.
 */
pid_t ipc_get_pid(ipc_portnum_t port);

/**
 * @brief Open a new IPC port.
 */
int sys_ipc_port_open(const char *name, int flags, ipc_port_t *port);

/**
 * @brief Close an IPC port.
 */
int sys_ipc_port_close(int port);

int sys_ipc_connect(int self, ipc_portnum_t port, const char *name);

#endif