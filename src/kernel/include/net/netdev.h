#ifndef NET_NIC_H
#define NET_NIC_H

#include <lib/list.h>
#include <net/ipv4.h>

struct netdev;
struct packet;

typedef struct netdev_drv {
	int (*send) (struct netdev *, struct packet *);
} netdev_drv_t;

typedef struct netdev {
	list_node_t node;
	netdev_drv_t *drv;
	void *priv;

	struct eth_addr *hw_addr;
	ipv4_addr_t ipv4_addr;
} netdev_t;

struct eth_addr;

int netdev_recv(netdev_t *netdev, void *buffer, size_t size);
netdev_t *netdev_eth_register(netdev_drv_t *drv, void *priv, struct eth_addr *addr);

#endif