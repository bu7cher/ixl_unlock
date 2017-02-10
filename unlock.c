/*-
 * Copyright (c) 2017 Yandex LLC
 * Copyright (c) 2017 Andrey V. Elsukov <ae@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/stdint.h>

#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/route.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <errno.h>

#define I40E_NVM_ACCESS \
     (((((((('E' << 4) + '1') << 4) + 'K') << 4) + 'G') << 4) | 5)

#define I40E_NVM_READ	0xB
#define I40E_NVM_WRITE	0xC

#define I40E_NVM_MOD_PNT_MASK 0xFF

#define I40E_NVM_TRANS_SHIFT	8
#define I40E_NVM_TRANS_MASK	(0xf << I40E_NVM_TRANS_SHIFT)
#define I40E_NVM_CON		0x0
#define I40E_NVM_SNT		0x1
#define I40E_NVM_LCB		0x2
#define I40E_NVM_SA		(I40E_NVM_SNT | I40E_NVM_LCB)
#define I40E_NVM_ERA		0x4
#define I40E_NVM_CSUM		0x8
#define I40E_NVM_EXEC		0xf

#define I40E_NVM_ADAPT_SHIFT	16
#define I40E_NVM_ADAPT_MASK	(0xffffULL << I40E_NVM_ADAPT_SHIFT)

#define I40E_NVMUPD_MAX_DATA	4096

#define	I40E_SR_EMP_SR_SETTINGS_PTR	0x48

struct i40e_nvm_access {
	uint32_t command;
	uint32_t config;
	uint32_t offset;
	uint32_t data_size;
	uint8_t data[1];
};


static void
usage(const char *name)
{

	printf("Usage: %s [args] <ifname>\n", name);
	printf("	-h	show this message\n");
	printf("	-g	show NVM content to check validness\n");
	printf("	-u	unlock the card and modify NVM\n");
	exit(0);
}

#define	PHY_CAP_SIZE	0x0d
#define	PHY_CAP_OFFSET	0x19
static int
show_info(const char *ifname, uint16_t *offp)
{
	struct ifdrv req;
	struct i40e_nvm_access *nvm;
	uint16_t *ptr, offset;
	int i, s;

	nvm = calloc(1, sizeof(*nvm) + PHY_CAP_SIZE * sizeof(uint16_t));
	if (nvm == NULL)
		err(1, "calloc: ");

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == -1)
		err(2, "socket: ");

	memset(&req, 0, sizeof(req));
	strlcpy(req.ifd_name, ifname, sizeof(req.ifd_name));

	req.ifd_cmd = I40E_NVM_ACCESS;
	req.ifd_len = sizeof(*nvm) + PHY_CAP_SIZE * sizeof(uint16_t);
	req.ifd_data = nvm;

	/* Read EMP SR settings pointer (6.3.1.69) */
	nvm->command = I40E_NVM_READ;
	nvm->config = I40E_NVM_SA << I40E_NVM_TRANS_SHIFT;
	nvm->offset =  I40E_SR_EMP_SR_SETTINGS_PTR * sizeof(uint16_t);
	nvm->data_size = sizeof(uint16_t); /* in bytes */
	if (ioctl(s, SIOCGDRVSPEC, &req) == -1)
		err(3, "ioctl: ");

	offset = *(uint16_t *)nvm->data;
	if (offp == NULL)
		printf("EMP SR: 0x%04x\n", offset);

	/* Pointer is in 4k units.*/
	if (offset & 0x8000)
		errx(1, "EMP SR pointer is in 4k units. This is untested.\n");

	/* Read PHY Capability LAN 0 Pointer (6.3.18.21) */
	offset += PHY_CAP_OFFSET;
	nvm->command = I40E_NVM_READ;
	nvm->config = I40E_NVM_SA << I40E_NVM_TRANS_SHIFT;
	nvm->offset = offset * sizeof(uint16_t);
	nvm->data_size = sizeof(uint16_t);
	if (ioctl(s, SIOCGDRVSPEC, &req) == -1)
		err(4, "ioctl: ");

	offset += *(uint16_t *)nvm->data;
	if (offp != NULL) {
		*offp = offset;
		free(nvm);
		return (s);
	}

	printf("PHY CAP DATA OFFSET: 0x%04x\n", offset);

	/* Read PHY Capability data structure 0 (6.3.22) */
	nvm->command = I40E_NVM_READ;
	nvm->config = I40E_NVM_SA << I40E_NVM_TRANS_SHIFT;
	nvm->offset = offset * sizeof(uint16_t);
	nvm->data_size = PHY_CAP_SIZE * sizeof(uint16_t);
	if (ioctl(s, SIOCGDRVSPEC, &req) == -1)
		err(5, "ioctl: ");

	printf("PHY Capability data structure 0:\n");
	ptr = (uint16_t *)nvm->data;
	for (i = 0; i < PHY_CAP_SIZE; i++) {
		printf("%08x  %02x  0x%04x", offset, i, *ptr++);
		switch(i) {
		case 0x00:
			printf(" (Section Length) should be 0x000b\n");
			break;
		case 0x08:
			printf(" (PHY Capabilities Misc0) <== will be modified\n");
			break;
		case 0x0a:
			printf(" (40 LESM Timer Values) should be 0x0a1e\n");
			break;
		default:
			printf("\n");
		};
	}
	close(s);
	free(nvm);
	return (0);
}

static void
update_nvm(const char *ifname)
{
	struct ifdrv req;
	struct i40e_nvm_access *nvm;
	uint16_t *ptr, offset, value;
	int c, i, s;

	s = show_info(ifname, &offset);
	if (s <= 0)
		errx(1, "show_info failed\n");

	nvm = calloc(1, sizeof(*nvm) + sizeof(uint16_t));
	if (nvm == NULL)
		err(1, "calloc: ");

	memset(&req, 0, sizeof(req));
	strlcpy(req.ifd_name, ifname, sizeof(req.ifd_name));

	req.ifd_cmd = I40E_NVM_ACCESS;
	req.ifd_len = sizeof(*nvm) + sizeof(uint16_t);
	req.ifd_data = nvm;

	for (i = 0, c = 0; i < 4; i++) {
		/* Read PHY Capabilities Misc[i] */
		nvm->command = I40E_NVM_READ;
		nvm->config = I40E_NVM_SA << I40E_NVM_TRANS_SHIFT;
		nvm->offset =  (offset + i * 0x0c + 0x08) * sizeof(uint16_t);
		nvm->data_size = sizeof(uint16_t); /* in bytes */
		if (ioctl(s, SIOCGDRVSPEC, &req) == -1)
			err(2, "ioctl: ");
		value = *(uint16_t *)nvm->data;
		printf("PHY Capabilities Misc%d: 0x%04x", i, value);
		if ((value & (1 << 11)) == 0) {
			printf(" skipped\n");
			continue;
		}
		value &= ~(1 << 11);
		/* Write updated value */
		nvm->command = I40E_NVM_WRITE;
		nvm->config = I40E_NVM_SA << I40E_NVM_TRANS_SHIFT;
		nvm->offset =  (offset + i * 0x0c + 0x08) * sizeof(uint16_t);
		nvm->data_size = sizeof(uint16_t); /* in bytes */
		*(uint16_t *)nvm->data = value;
		if (ioctl(s, SIOCSDRVSPEC, &req) == -1)
			err(3, "ioctl: ");
		printf(" -> 0x%04x\n", value);
		c++;
		sleep(1);
	}

	if (c != 0) {
		/* Update checksum */
		nvm->command = I40E_NVM_WRITE;
		nvm->config = (I40E_NVM_SA |
		    I40E_NVM_CSUM) << I40E_NVM_TRANS_SHIFT;
		nvm->offset = 0;
		nvm->data_size = sizeof(uint16_t);
		*(uint16_t *)nvm->data = 0;
		if (ioctl(s, SIOCSDRVSPEC, &req) == -1)
			err(4, "ioctl: ");
		printf("NVM successfully updated\n");
	}

	close(s);
	free(nvm);
}

int
main(int argc, char **argv)
{
	const char *name;
	int cmd, ch;

	name = argv[0];
	cmd = 0;
	while ((ch = getopt(argc, argv, "ugh:")) != -1) {
		switch (ch) {
		default:
			usage(name);
			break;
		case 'g':
			cmd = 1;
			break;
		case 'u':
			cmd = 2;
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (cmd == 0)
		usage(name);
	if (argc < 1)
		errx(1, "ifname is required\n");
	if (cmd == 1)
		show_info(argv[0], NULL);
	else
		update_nvm(argv[0]);

	return (0);
}

