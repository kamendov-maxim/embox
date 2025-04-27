/**
 * @file
 * @brief
 *
 * @author  Anton Kozlov
 * @date    21.07.2014
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <drivers/char_dev.h>
#include <embox/unit.h>
#include <kernel/sched/sched_lock.h>
#include <kernel/sched/waitq.h>
#include <kernel/task/resource/idesc.h>
#include <kernel/thread.h>
#include <kernel/thread/sync/mutex.h>
#include <kernel/thread/thread_sched_wait.h>
#include <net/inetdevice.h>
#include <net/l0/net_entry.h>
#include <net/l2/ethernet.h>
#include <net/l3/arp.h>
#include <net/netdevice.h>
#include <util/err.h>
#include <util/math.h>

struct tap_dev {
	struct char_dev cdev;
	struct net_device *netdev;
	struct mutex mtx_use;
	struct sk_buff_head rx_q;
	struct waitq wq;
};

EMBOX_UNIT_INIT(tap_dev_init);

static inline void tap_krnl_lock(struct tap_dev *tap) {
	sched_lock();
}

static inline void tap_krnl_unlock(struct tap_dev *tap) {
	sched_unlock();
}

static inline void tap_user_lock(struct tap_dev *tap) {
	mutex_lock(&tap->mtx_use);
}

static inline void tap_user_unlock(struct tap_dev *tap) {
	mutex_unlock(&tap->mtx_use);
}

static int tap_open(struct net_device *dev) {
	return 0;
}

static int tap_set_mac(struct net_device *dev, const void *addr) {
	memcpy(dev->dev_addr, addr, ETH_ALEN);
	return ENOERR;
}

static int tap_xmit(struct net_device *dev, struct sk_buff *skb) {
	struct tap_dev *tap = netdev_priv(dev);
	struct ethhdr *ethh;

	/* we don't build headers for dev with NOARP flag */
	ethh = eth_hdr(skb);
	ethh->h_proto = htons(ETH_P_IP);
	memcpy(ethh->h_source, skb->dev->dev_addr, ETH_ALEN);
	memset(ethh->h_dest, 0, ETH_ALEN);

	skb_queue_push(&tap->rx_q, skb);
	waitq_wakeup(&tap->wq, 1);
	return 0;
}

static const struct net_driver tap_netdrv_ops = {
    .xmit = tap_xmit,
    .start = tap_open,
    .set_macaddr = tap_set_mac,
};

static int tap_setup(struct net_device *dev) {
	dev->mtu = (16 * 1024) + 20 + 20 + 12;
	dev->hdr_len = ETH_HEADER_SIZE;
	dev->addr_len = ETH_ALEN;
	/* dev->type = ARP_HRD_LOOPBACK; */
	dev->type = ARP_HRD_ETHERNET;
	/* dev->flags = IFF_NOARP | IFF_RUNNING; */
	dev->flags = IFF_RUNNING;
	dev->drv_ops = &tap_netdrv_ops;
	dev->ops = &ethernet_ops;
	return 0;
}

static int tap_dev_open(struct char_dev *cdev, struct idesc *idesc) {
	assert(cdev);

	tap_user_lock((struct tap_dev *)cdev);

	return 0;
}

static void tap_dev_close(struct char_dev *cdev) {
	assert(cdev);

	tap_user_unlock((struct tap_dev *)cdev);
}

static ssize_t tap_dev_read(struct char_dev *cdev, void *buf, size_t nbyte) {
	struct waitq_link *wql = &thread_self()->schedee.waitq_link;
	struct tap_dev *tap;
	struct sk_buff *skb;
	int ret = 0;
	int min_len;

	assert(cdev);

	tap = (struct tap_dev *)cdev;

	waitq_link_init(wql);
	do {
		tap_krnl_lock(tap);
		{
			waitq_wait_prepare(&tap->wq, wql);
			skb = skb_queue_pop(&tap->rx_q);
		}
		tap_krnl_unlock(tap);

		if (skb) {
			min_len = min(skb->len, nbyte);
			if (min_len > 0) {
				memcpy(buf, skb->mac.raw, min_len);
				ret = min_len;
			}
			break;
		}
		else {
			ret = sched_wait_timeout(SCHED_TIMEOUT_INFINITE, NULL);
		}
	} while (ret == 0);
	waitq_wait_cleanup(&tap->wq, wql);

	return ret;
}

static ssize_t tap_dev_write(struct char_dev *cdev, const void *buf,
    size_t nbyte) {
	struct net_device *netdev;
	struct tap_dev *tap;
	struct sk_buff *skb;
	/* unsigned char *raw; */
	struct ethhdr *ethh;

	assert(cdev);

	tap = (struct tap_dev *)cdev;
	netdev = tap->netdev;

    if (nbyte < ETH_HLEN)
    {
        return -EINVAL;
    }

	skb = skb_alloc(nbyte
                 /* + ETH_HLEN */
                 );
	if (!skb) {
		return -ENOMEM;
	}

	ethh = eth_hdr(skb);

	ethh->h_proto = htons(ETH_P_IP);
	memcpy(ethh->h_dest, netdev->dev_addr, ETH_ALEN);
	memset(ethh->h_source, 0, ETH_ALEN);

	/* raw = skb->mac.raw + ETH_HLEN; */
	/* memcpy(raw, buf, nbyte); */
	memcpy(skb->mac.raw, buf, nbyte);

	skb->dev = netdev;
	netif_rx(skb);

	return nbyte;
}

static const struct char_dev_ops tap_dev_ops = {
    .read = tap_dev_read,
    .write = tap_dev_write,
    .open = tap_dev_open,
    .close = tap_dev_close,
};

static struct tap_dev tap_dev = {
    .cdev = CHAR_DEV_INIT(tap_dev.cdev, "tap0", &tap_dev_ops),
};

CHAR_DEV_REGISTER((struct char_dev *)&tap_dev);

static int tap_dev_init(void) {
	struct net_device *netdev;
	int err;

	assert(strlen(tap_dev.cdev.name) < IFNAMSIZ);

	netdev = netdev_alloc(tap_dev.cdev.name, &tap_setup, 0);
	if (netdev == NULL) {
		return -ENOMEM;
	}

	if ((err = inetdev_register_dev(netdev))) {
		netdev_free(netdev);
		return err;
	}

	mutex_init(&tap_dev.mtx_use);
	waitq_init(&tap_dev.wq);
	skb_queue_init(&tap_dev.rx_q);

	tap_dev.netdev = netdev;
	netdev->priv = &tap_dev;

	return 0;
}
