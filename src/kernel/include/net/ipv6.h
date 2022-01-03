#ifndef NET_IPV6_H
#define NET_IPV6_H

struct netdev;
struct packet;

typedef union ipv6_addr {
	uint8_t octets[16];
	/* uint32_t u32[4];
	struct {
		uint64_t prefix;
		uint64_t id;
	} __packed;
	__int128 addr; */
} __packed ipv6_addr_t;

typedef struct ipv6_hdr {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	uint32_t tc0 : 4;
	uint32_t version : 4;
	uint32_t tcfl : 24;
#else
	uint32_t version : 4;
	uint32_t tcfl : 28;
#endif

	uint16_t length;
	uint8_t next;
	uint8_t hoplim;
	
	ipv6_addr_t src;
	ipv6_addr_t dst;
} __packed ipv6_hdr_t;

int ipv6_recv(struct netdev *dev, struct packet *packet);

#endif