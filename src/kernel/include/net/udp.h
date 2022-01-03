#ifndef NET_UDP_H
#define NET_UDP_H

#define PORT_DNS		53
#define PORT_BOOTP_SERVER	67
#define PORT_BOOTP_CLIENT	68
#define PORT_DHCPV6_CLIENT	546

union ipv4_addr;
struct netdev;
struct packet;

typedef struct {
	uint16_t src_port;
	uint16_t dst_port;
	uint16_t len;
	uint16_t checksum;
} __packed udp_hdr_t;

int udp_recv(struct netdev *dev, struct packet *packet);
int udp_send(struct netdev *dev, struct packet *packet, uint16_t src, uint16_t dest, union ipv4_addr *addr);

#endif