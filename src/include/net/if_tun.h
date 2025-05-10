#ifndef _UAPI__IF_TUN_H
#define _UAPI__IF_TUN_H

#include <net/l2/ethernet.h>
#include <sys/ioctl.h>

/* Ioctl defines */
#define TUNSETIFF     _IOW('T', 202, int) 
#define TUNGETIFF      _IOR('T', 210, unsigned int)

/* TUNSETIFF ifr flags */
#define IFF_TUN		0x0001
#define IFF_TAP		0x0002
/* Used in TUNSETIFF to bring up tun/tap without carrier */
#define IFF_NO_CARRIER	0x0040
#define IFF_NO_PI	0x1000

#endif /* _UAPI__IF_TUN_H */

