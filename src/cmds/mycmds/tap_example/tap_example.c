#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <net/if.h>

int create_tap_device(char *dev_name, int flag) {
	struct ifreq ifr;
	int fd, err_code;

	if ((fd = open("/dev/tap0", O_RDWR)) < 0) {
		printf("Tap device fd creation error. (%d)\n", fd);
		return fd;
	}

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, dev_name);
	ifr.ifr_flags |= flag;

	if ((err_code = ioctl(fd, FIONBIO, &ifr)) < 0) {
		printf("Tap device ioctl error. (%d)\n", err_code);
		close(fd);
		return err_code;
	}

	return fd;
}

int main(int argc, char *argv[]) {
	int tap_fd = create_tap_device("tap0", 0x0002);
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
		printf("ICMP receive : %d.%d.%d.%d -> %d.%d.%d.%d (%d)\n", dst_ip[0],
		    dst_ip[1], dst_ip[2], dst_ip[3], src_ip[0], src_ip[1], src_ip[2],
		    src_ip[3], ret_length);

		memcpy(&buf[26], dst_ip, 4);
		memcpy(&buf[30], src_ip, 4);
		buf[24] = 0;
		ret_length = write(tap_fd, buf, ret_length);
		printf("ICMP send : %d.%d.%d.%d -> %d.%d.%d.%d (%d)\n", src_ip[0],
		    src_ip[1], src_ip[2], src_ip[3], dst_ip[0], dst_ip[1], dst_ip[2],
		    dst_ip[3], ret_length);
	}

	return close(tap_fd);
}
