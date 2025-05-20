#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <net/if.h>
#include <net/if_tun.h>
#include <net/util/checksum.h>

int create_tap_device(char *dev_name, int flag) {
	struct ifreq ifr;
	int fd, err_code;

	if ((fd = open("/dev/tun0", O_RDWR)) < 0) {
		printf("Tap device fd creation error. (%d)\n", fd);
		return fd;
	}

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, dev_name);
	ifr.ifr_flags |= flag;

	if ((err_code = ioctl(fd, TUNSETIFF, &ifr)) < 0) {
		printf("Tap device ioctl error. (%d)\n", err_code);
		close(fd);
		return err_code;
	}

	return fd;
}

int main(int argc, char *argv[]) {
	int tap_fd = create_tap_device("tun0", IFF_TAP);
	if (tap_fd < 0) {
		printf("tap_fd < 0\n");
	}
	printf("Tap device created successfully\n");

	int ret_length = 0;
	unsigned char buf[1024];
	while (1) {
		ret_length = read(tap_fd, buf, sizeof(buf));
		if (ret_length < 0) {
			break;
		}
		unsigned char src_ip[4];
		unsigned char dst_ip[4];
		memcpy(src_ip, &buf[26], 4);
		memcpy(dst_ip, &buf[30], 4);
		printf("ICMP receive : %hhu.%hhu.%hhu.%hhu -> %hhu.%hhu.%hhu.%hhu (%d)\n", dst_ip[0],
		    dst_ip[1], dst_ip[2], dst_ip[3], src_ip[0], src_ip[1], src_ip[2],
		    src_ip[3], ret_length);

		memcpy(&buf[26], dst_ip, 4);
		memcpy(&buf[30], src_ip, 4);

        uint8_t *icmp_header = buf + (buf[14] & 0xF) * 4; // Count offset by multiplying value in ihl ip field by 4
        icmp_header[0] = 0;  // Type = 0 (Echo Reply)

        // Recalculate ICMP checksum
        icmp_header[2] = 0;
        icmp_header[3] = 0;
        size_t icmp_len = ret_length - 20;
        uint16_t icmp_checksum = partial_sum((uint16_t*)icmp_header, icmp_len);
        icmp_header[2] = (uint8_t)(icmp_checksum >> 8);
        icmp_header[3] = (uint8_t)icmp_checksum;

        // Recalculate IP checksum
        uint8_t *ip_header = buf;
        ip_header[10] = 0;
        ip_header[11] = 0;
        uint16_t ip_checksum = partial_sum((uint16_t*)ip_header, 20);
        ip_header[10] = (uint8_t)(ip_checksum >> 8);
        ip_header[11] = (uint8_t)ip_checksum;

		buf[24] = 0;
		ret_length = write(tap_fd, buf, ret_length);
		printf("ICMP send : %hhu.%hhu.%hhu.%hhu -> %hhu.%hhu.%hhu.%hhu (%d)\n", src_ip[0],
		    src_ip[1], src_ip[2], src_ip[3], dst_ip[0], dst_ip[1], dst_ip[2],
		    dst_ip[3], ret_length);
	}

	return close(tap_fd);
}
