#ifndef NET_DHCP_H
#define NET_DHCP_H

#include <net/ipv4.h>
#include <net/eth.h>

#define DHCP_MAGIC_COOKIE 0x63825363

#define DCHP_OPT_PAD 			0
#define DCHP_OPT_SUBNET_MASK 		1
#define DCHP_OPT_ROUTER 		3
#define DCHP_OPT_DNS 			6
#define DCHP_OPT_REQUESTED_IP_ADDR 	50
#define DCHP_OPT_LEASE_TIME 		51
#define DCHP_OPT_DHCP_MESSAGE_TYPE 	53
#define DCHP_OPT_SERVER_ID 		54
#define DCHP_OPT_PARAMETER_REQUEST	55
#define DCHP_OPT_END			255

#define DHCP_MSG_DISCOVER	1
#define DHCP_MSG_OFFER		2
#define DHCP_MSG_REQUEST	3
#define DHCP_MSG_DECLINE	4
#define DHCP_MSG_ACK		5
#define DHCP_MSG_NAK		6
#define DHCP_MSG_RELEASE	7
#define DHCP_MSG_INFORM		8

typedef struct {
#define DCHP_OP_REQUEST	1
#define DCHP_OP_REPLY	2
	uint8_t op;

#define DCHP_HTYPE_ETH	1
	uint8_t htype;
	uint8_t hlen;
	uint8_t hops;
	uint32_t xid;
	uint16_t secs;
	uint16_t flags;
	ipv4_addr_t ciaddr;	/* client ip */
	ipv4_addr_t yiaddr;	/* your ip */
	ipv4_addr_t siaddr;	/* server ip */
	ipv4_addr_t giaddr;	/* gateway ip */
	eth_addr_t client_addr;	/* client mac addr */
	uint8_t _[10];
	char server_name[64];
	char boot_file_name[128];
} __packed dhcp_hdr_t;

struct netdev;

int dhcp_recv(struct netdev *dev, struct packet *packet);
int dhcp_discover(struct netdev *dev);

#endif