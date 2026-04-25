/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * register_dump.c -- Chapter 16 user-space register dump tool.
 *
 * Queries every Chapter 16 register sysctl from dev.myfirst.0 and
 * prints a formatted summary. Useful for Labs 1 and 4.
 *
 * Build: make
 * Run:   ./register_dump
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

struct reg_desc {
	const char *sysctl_name;
	const char *pretty_name;
	unsigned int offset;
};

static const struct reg_desc regs[] = {
	{ "dev.myfirst.0.reg_ctrl",         "CTRL",          0x00 },
	{ "dev.myfirst.0.reg_status",       "STATUS",        0x04 },
	{ "dev.myfirst.0.reg_data_in",      "DATA_IN",       0x08 },
	{ "dev.myfirst.0.reg_data_out",     "DATA_OUT",      0x0c },
	{ "dev.myfirst.0.reg_intr_mask",    "INTR_MASK",     0x10 },
	{ "dev.myfirst.0.reg_intr_status",  "INTR_STATUS",   0x14 },
	{ "dev.myfirst.0.reg_device_id",    "DEVICE_ID",     0x18 },
	{ "dev.myfirst.0.reg_firmware_rev", "FIRMWARE_REV",  0x1c },
	{ "dev.myfirst.0.reg_scratch_a",    "SCRATCH_A",     0x20 },
	{ "dev.myfirst.0.reg_scratch_b",    "SCRATCH_B",     0x24 },
};

static int
read_reg(const char *name, unsigned int *value)
{
	size_t len = sizeof(*value);

	if (sysctlbyname(name, value, &len, NULL, 0) != 0)
		return (-1);
	if (len != sizeof(*value))
		return (-1);
	return (0);
}

int
main(int argc, char **argv)
{
	unsigned int value;
	size_t i;

	(void)argc;
	(void)argv;

	printf("myfirst register dump\n");
	printf("---------------------\n");
	printf("%-14s %-6s %-10s %s\n", "Name", "Offset", "Value", "Decoded");

	for (i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
		if (read_reg(regs[i].sysctl_name, &value) != 0) {
			printf("%-14s 0x%02x   (unavailable: %s)\n",
			    regs[i].pretty_name, regs[i].offset,
			    regs[i].sysctl_name);
			continue;
		}
		printf("%-14s 0x%02x   0x%08x ",
		    regs[i].pretty_name, regs[i].offset, value);

		/* Small decoded view for a few well-known registers. */
		if (regs[i].offset == 0x00) {
			printf("[");
			if (value & 0x1)  printf(" ENABLE");
			if (value & 0x2)  printf(" RESET");
			if (value & 0x100) printf(" LOOPBACK");
			if ((value & 0xf0) != 0) printf(" mode=%u", (value>>4)&0xf);
			printf(" ]");
		} else if (regs[i].offset == 0x04) {
			printf("[");
			if (value & 0x1)  printf(" READY");
			if (value & 0x2)  printf(" BUSY");
			if (value & 0x4)  printf(" ERROR");
			if (value & 0x8)  printf(" DATA_AV");
			printf(" ]");
		} else if (regs[i].offset == 0x18) {
			char id[5];
			id[0] = (value >>  0) & 0xff;
			id[1] = (value >>  8) & 0xff;
			id[2] = (value >> 16) & 0xff;
			id[3] = (value >> 24) & 0xff;
			id[4] = '\0';
			printf("[ '%s' ]", id);
		} else if (regs[i].offset == 0x1c) {
			printf("[ v%u.%u ]",
			    (value >> 16) & 0xffff,
			    value & 0xffff);
		}
		printf("\n");
	}

	return (0);
}
