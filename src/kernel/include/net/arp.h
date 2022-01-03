#ifndef NET_ARP_H
#define NET_ARP_H

typedef struct arp_hdr {
#define ARP_HTYPE_ETH		0x01
	uint16_t htype; /* hardware type */
	uint16_t ptype; /* protocol type */
	uint8_t hlen; /* Hardware address length */
	uint8_t plen; /* Protocol address length */

#define ARP_OP_REQUEST		0x01
#define ARP_OP_REPLY		0x02
	uint16_t op; /* Operation */
} __packed arp_hdr_t;

struct packet;
struct netdev;
struct eth_addr;
union ipv4_addr;

int arp_recv(struct netdev *dev, struct packet *packet);
int arp_send(struct netdev *dev, uint16_t op, struct eth_addr *tha, union ipv4_addr *tpa);

#endif