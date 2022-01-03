#ifndef NET_ETH_H
#define NET_ETH_H

/* https://en.wikipedia.org/wiki/EtherType */

struct packet;
struct netdev;

typedef struct eth_addr {
	uint8_t addr[6];
} __packed eth_addr_t;

typedef struct {
	eth_addr_t dest;
	eth_addr_t src;
	uint16_t ether_type;
} __packed eth_hdr_t;
#define ETH_HDR_SZ sizeof(eth_hdr_t)

/* https://en.wikipedia.org/wiki/EtherType */
#define ETH_TYPE_IPV4	0x0800
#define ETH_TYPE_ARP	0x0806
#define ETH_TYPE_IPV6	0x86DD

void eth_print_addr(eth_addr_t *addr);

int eth_addr_cmp(eth_addr_t *a, eth_addr_t *b);

int eth_recv(struct netdev *dev, struct packet *packet);
int eth_send(struct netdev *dev, struct packet *packet, uint16_t type, eth_addr_t *dst);

#endif