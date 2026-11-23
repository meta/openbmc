/*
 * Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
 *
 * This program file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program in a file named COPYING; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

/*
 * mii-read: read a PHY register on a network interface's MDIO bus with
 * SIOCGMIIREG, so the access goes through the kernel's MDIO bus lock
 * instead of racing with phylib like raw MAC PHYCR (devmem) access does.
 *
 * Usage: mii-read <ifname> <phy> <reg>
 * Prints the register value as 0x%x. Exits 1 on error.
 */

#include <linux/mii.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static int parse(const char *s, unsigned long max, unsigned long *val)
{
	char *end;

	*val = strtoul(s, &end, 0);
	return (*s == '\0' || *end != '\0' || *val > max) ? -1 : 0;
}

int main(int argc, char **argv)
{
	struct ifreq ifr;
	struct mii_ioctl_data *mii = (struct mii_ioctl_data *)&ifr.ifr_data;
	unsigned long phy, reg;
	int fd;

	if (argc != 4 || parse(argv[2], 0x1f, &phy) || parse(argv[3], 0x1f, &reg)) {
		fprintf(stderr, "Usage: %s <ifname> <phy 0-0x1f> <reg 0-0x1f>\n",
			argv[0]);
		return 1;
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, argv[1], IFNAMSIZ - 1);
	mii->phy_id = phy;
	mii->reg_num = reg;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		return 1;
	}
	if (ioctl(fd, SIOCGMIIREG, &ifr) < 0) {
		perror("SIOCGMIIREG");
		close(fd);
		return 1;
	}
	close(fd);

	printf("0x%x\n", mii->val_out);
	return 0;
}
