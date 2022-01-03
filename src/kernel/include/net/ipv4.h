#ifndef NET_IPV4_H
#define NET_IPV4_H

struct packet;
struct netdev;

typedef union ipv4_addr {
	uint8_t n[4];
	uint32_t addr;
} __packed ipv4_addr_t;

typedef struct ipv4_hdr {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	uint8_t ihl :4;	/* IP header length in 4 bytes */
	uint8_t version :4;
#else
	uint8_t version :4;
	uint8_t ihl :4;
#endif

	uint8_t tos; /* Type of Service */
	uint16_t length;
	uint16_t id;

#define IPV4_RF 0x8000		/* reserved fragment flag */
#define IPV4_DF 0x4000		/* dont fragment flag */
#define IPV4_MF 0x2000		/* more fragments flag */
#define IPV4_OFFMASK 0x1fff	/* mask for fragmenting bits */
	uint16_t frag_off;
	uint8_t ttl; /* Time to live */

/* https://de.wikipedia.org/wiki/Protokoll_(IP) */
#define IPV4_PROT_ICMP	1
#define IPV4_PROT_TCP	6
#define IPV4_PROT_UDP	17
	uint8_t protocol;
	uint16_t checksum;
	ipv4_addr_t src;
	ipv4_addr_t dest;
} __packed ipv4_hdr_t;

void ipv4_print_addr(ipv4_addr_t * addr);

int ipv4_recv(struct netdev *dev, struct packet *packet);
int ipv4_send(struct netdev *dev, struct packet *packet, uint8_t proto, ipv4_addr_t *dest);

#endif