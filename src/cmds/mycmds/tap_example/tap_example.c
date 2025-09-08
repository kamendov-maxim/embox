#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <net/if.h>
#include <net/if_tun.h>
#include <net/util/checksum.h>
#include "kernel/task/resource/idesc.h"

#define DEV_NAME           "tap0"
#define IFACE_INET_ADDRESS "10.0.4.1"
#define NETMASK            "255.255.255.0"
#define HW_ADDR            "02:ab:cd:ef:12:34"
#define SUBNET             "10.0.4.0/24"

uint8_t hw_addr[6] = {0x02, 0x12, 0x34, 0x56, 0x78, 0xab};

void setup(void) {
	char system_str[128];
	memset(system_str, 0, sizeof(system_str));
	strcpy(system_str, "ifconfig ");
	strcat(system_str, DEV_NAME);
	strcat(system_str, " ");
	strcat(system_str, IFACE_INET_ADDRESS);
	strcat(system_str, " netmask ");
	strcat(system_str, NETMASK);
	printf("%s\n", system_str);
	system(system_str);

	memset(system_str, 0, sizeof(system_str));
	strcpy(system_str, "ifconfig ");
	strcat(system_str, DEV_NAME);
	strcat(system_str, " hw ether ");
	strcat(system_str, HW_ADDR);
	printf("%s\n", system_str);
	system(system_str);

	memset(system_str, 0, sizeof(system_str));
	strcpy(system_str, "route add ");
	strcat(system_str, SUBNET);
	strcat(system_str, " dev ");
	strcat(system_str, DEV_NAME);
	printf("%s\n", system_str);
	system(system_str);
}

#include <kernel/task/resource/index_descriptor.h>
int create_tap_device(char *dev_name, int flag) {
	struct ifreq ifr;
	int fd, err_code;
    int fd2;
    struct idesc* idesc1, *idesc2;

	if ((fd = open("/dev/tun0", O_RDWR)) < 0) {
		printf("Tap device fd creation error. (%d)\n", fd);
		return fd;
	}
    printf("tap fd %d\n", fd);
	if ((fd2 = open("/dev/tun0", O_RDWR)) < 0) {
		printf("Tap device fd creation error. (%d)\n", fd);
		return fd;
	}
    printf("tap fd2 %d\n", fd2);

	idesc1 = index_descriptor_get(fd);
	idesc2 = index_descriptor_get(fd);
    printf("%d %d\n", idesc1->idesc_usage_count, idesc2->idesc_usage_count);
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, dev_name);
	ifr.ifr_flags |= flag;
	if ((err_code = ioctl(fd2, TUNSETIFF, &ifr)) < 0) {
		printf("Tap device ioctl error. (%d)\n", err_code);
		close(fd);
		return err_code;
	}

	return fd2;
}

void arp_request(unsigned char *ethernet_hdr) {
	unsigned char *arp_hdr = ethernet_hdr + 14;
	unsigned char ip_addr[4];

	/* Swapping adresses */
	memcpy(ethernet_hdr, ethernet_hdr + ETH_ALEN, ETH_ALEN);
	memcpy(ethernet_hdr + ETH_ALEN, hw_addr, ETH_ALEN);

	/* Hw address length */
	arp_hdr[4] = 0x06;

	/* This is response */
	arp_hdr[6] = 0x00;
	arp_hdr[7] = 0x02;

	/* Filling arp response header */
	/* Copy IP addresses */
	memcpy(ip_addr, arp_hdr + 14, 4);
	memcpy(arp_hdr + 14, arp_hdr + 24, 4);
	memcpy(arp_hdr + 24, ip_addr, 4);

	/* Fill in hw addresses */
	memcpy(arp_hdr + 8, hw_addr, ETH_ALEN);
	memcpy(arp_hdr + 18, ethernet_hdr, ETH_ALEN);
}

int main(int argc, char *argv[]) {
	int tap_fd = create_tap_device(DEV_NAME, IFF_TAP);
	if (tap_fd < 0) {
		printf("tap_fd < 0\n");
	}
	printf("Tap device created successfully\n");

	setup();

	int ret_length = 0;
	unsigned char buf[1024];
	while (1) {
		ret_length = read(tap_fd, buf, sizeof(buf));
		if (ret_length < 0) {
			break;
		}

		if (buf[12] == 0x08
		    && buf[13] == 0x06 /* Check if this is arp request */
		    && buf[20] == 0x00 && buf[21] == 0x01) {
			arp_request(buf);
			ret_length = write(tap_fd, buf, 42);
			continue;
		}

		memcpy(buf, buf + ETH_ALEN, ETH_ALEN);
		memcpy(buf + ETH_ALEN, hw_addr, ETH_ALEN);

		unsigned char src_ip[4];
		unsigned char dst_ip[4];
		memcpy(src_ip, &buf[26], 4);
		memcpy(dst_ip, &buf[30], 4);
		printf("ICMP receive : %hhu.%hhu.%hhu.%hhu -> %hhu.%hhu.%hhu.%hhu "
		       "(%d)\n",
		    dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3], src_ip[0], src_ip[1],
		    src_ip[2], src_ip[3], ret_length);

		memcpy(&buf[26], dst_ip, 4);
		memcpy(&buf[30], src_ip, 4);

		uint8_t *ip_header = buf + 14;
		size_t iph_len = (ip_header[0] & 0xF) * 4;
		uint8_t *icmp_header =
		    ip_header
		    + iph_len; /* Count offset by multiplying value in ihl ip field by 4 */
		icmp_header[0] = 0; // Type = 0 (Echo Reply)

		/* Recalculate ICMP checksum */
		icmp_header[2] = 0;
		icmp_header[3] = 0;
		size_t icmp_len = ret_length - iph_len;
		uint16_t icmp_checksum = ptclbsum((uint16_t *)icmp_header, icmp_len);
		icmp_header[3] = (uint8_t)(icmp_checksum >> 8);
		icmp_header[2] = (uint8_t)icmp_checksum;

		/* Recalculate IP checksum */
		ip_header[10] = 0;
		ip_header[11] = 0;
		uint16_t ip_checksum = ptclbsum(ip_header, iph_len);
		ip_header[11] = (uint8_t)(ip_checksum >> 8);
		ip_header[10] = (uint8_t)ip_checksum;

		ret_length = write(tap_fd, buf, ret_length);
		printf("ICMP send : %hhu.%hhu.%hhu.%hhu -> %hhu.%hhu.%hhu.%hhu (%d)\n",
		    src_ip[0], src_ip[1], src_ip[2], src_ip[3], dst_ip[0], dst_ip[1],
		    dst_ip[2], dst_ip[3], ret_length);
	}

	return close(tap_fd);
}
