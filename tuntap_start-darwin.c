/*
 * Copyright (c) 2012-2013 Tristan Le Guern <leguern AT medu DOT se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Darwin has support for tun and tap drivers through the project tuntaposx,
 * by Mattias Nissler <mattias.nissler AT gmx DOT de>.
 *
 * It can be found here: http://tuntaposx.sourceforge.net
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/param.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_types.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>

#include <fcntl.h>
#include <ifaddrs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tuntap.h"
#include "tuntap_private.h"

static int
tuntap_sys_create_dev(struct device *dev, int tun) {
	return -1;
}

int
tuntap_sys_start(struct device *dev, int mode, int tun) {
	struct ifreq ifr;
	struct ifaddrs *ifa;
	char name[MAXPATHLEN];
	int fd;
	char *type;

	fd = -1;

	/* Force creation of the driver if needed or let it resilient */
	if (mode & TUNTAP_MODE_PERSIST) {
		mode &= ~TUNTAP_MODE_PERSIST;
		/* TODO: Call tuntap_sys_create_dev() */
	}

        /* Set the mode: tun or tap */
	if (mode == TUNTAP_MODE_ETHERNET) {
		type = "tap";
		ifr.ifr_flags |= IFF_LINK0;
	}
	else if (mode == TUNTAP_MODE_TUNNEL) {
		type = "tun";
		ifr.ifr_flags &= ~IFF_LINK0;
	}
	else {
		tuntap_log(TUNTAP_LOG_ERR, "Invalid parameter 'mode'");
		return -1;
	}

	/* Try to use the given driver or loop throught the avaible ones */
	if (tun < TUNTAP_ID_MAX) {
		(void)snprintf(name, sizeof name, "/dev/%s%i", type, tun);
		fd = open(name, O_RDWR);
	} else if (tun == TUNTAP_ID_ANY) {
		for (tun = 0; tun < TUNTAP_ID_MAX; ++tun) {
			(void)memset(name, '\0', sizeof name);
			(void)snprintf(name, sizeof name, "/dev/%s%i",
			    type, tun);
			if ((fd = open(name, O_RDWR)) > 0)
				break;
		}
	} else {
		tuntap_log(TUNTAP_LOG_ERR, "Invalid parameter 'tun'");
		return -1;
	}
	switch (fd) {
	case -1:
		tuntap_log(TUNTAP_LOG_ERR, "Permission denied");
		return -1;
	case 256:
		tuntap_log(TUNTAP_LOG_ERR, "Can't find a tun entry");
		return -1;
	default:
		/* NOTREACHED */
		break;
	}

	/* Set the interface name */
	(void)memset(&ifr, '\0', sizeof ifr);
	(void)snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s%i", type, tun);
	/* And save it */
	(void)strlcpy(dev->if_name, ifr.ifr_name, sizeof dev->if_name);

	/* Get the interface default values */
	if (ioctl(dev->ctrl_sock, SIOCGIFFLAGS, &ifr) == -1) {
		tuntap_log(TUNTAP_LOG_ERR, "Can't get interface values");
		return -1;
	}

	/* Set our modifications */
	if (ioctl(dev->ctrl_sock, SIOCSIFFLAGS, &ifr) == -1) {
		tuntap_log(TUNTAP_LOG_ERR, "Can't set interface values");
		return -1;
	}

	/* Save flags for tuntap_{up, down} */
	dev->flags = ifr.ifr_flags;

	/* Save pre-existing MAC address */
	if (mode == TUNTAP_MODE_ETHERNET && getifaddrs(&ifa) == 0) {
		struct ifaddrs *pifa;

		for (pifa = ifa; pifa != NULL; pifa = pifa->ifa_next) {
			if (strcmp(pifa->ifa_name, dev->if_name) == 0) {
				struct ether_addr eth_addr;

				/*
				 * The MAC address is from 10 to 15.
				 *
				 * And yes, I know, the buffer is supposed
				 * to have a size of 14 bytes.
				 */
				(void)memcpy(dev->hwaddr,
				  pifa->ifa_addr->sa_data + 10,
				  ETHER_ADDR_LEN);

				(void)memset(&eth_addr.ether_addr_octet, 0,
				  ETHER_ADDR_LEN);
				(void)memcpy(&eth_addr.ether_addr_octet,
				  pifa->ifa_addr->sa_data + 10,
				  ETHER_ADDR_LEN);
				break;
			}
		}
		if (pifa == NULL)
			tuntap_log(TUNTAP_LOG_WARN,
			    "Can't get link-layer address");
		freeifaddrs(ifa);
	}
	return fd;
}
