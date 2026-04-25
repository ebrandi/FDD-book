/*
 * strings_demo.c - Safe string and memory operations in the kernel.
 *
 * Companion for the "Hands-On Lab: Safe String Handling" section of
 * Chapter 5.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/libkern.h>

MALLOC_DEFINE(M_STRDEMO, "strdemo", "String demo buffers");

static int
strings_demo_modevent(module_t mod __unused, int type, void *arg __unused)
{
	char *buffer1, *buffer2;
	const char test_string[] = "FreeBSD Kernel Programming";
	size_t len, copied;

	switch (type) {
	case MOD_LOAD:
		printf("=== Kernel String Handling Demo ===\n");

		buffer1 = malloc(64, M_STRDEMO, M_WAITOK | M_ZERO);
		buffer2 = malloc(32, M_STRDEMO, M_WAITOK | M_ZERO);

		copied = strlcpy(buffer1, test_string, 64);
		printf("strlcpy: copied=%zu result='%s'\n", copied, buffer1);

		copied = strlcpy(buffer2, test_string, 32);
		printf("strlcpy small buf: copied=%zu result='%s'\n",
		    copied, buffer2);
		if (copied >= 32)
			printf("warning: source truncated\n");

		len = strnlen(buffer1, 64);
		printf("strnlen: length=%zu\n", len);

		strlcat(buffer2, " rocks!", 32);
		printf("strlcat: '%s'\n", buffer2);

		memset(buffer1, 'X', 10);
		buffer1[10] = '\0';
		printf("memset: '%s'\n", buffer1);

		snprintf(buffer1, 64, "module loaded at ticks=%d", ticks);
		printf("snprintf: '%s'\n", buffer1);

		free(buffer1, M_STRDEMO);
		free(buffer2, M_STRDEMO);

		printf("strings_demo: module loaded\n");
		break;

	case MOD_UNLOAD:
		printf("strings_demo: module unloaded\n");
		break;

	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t strings_demo_mod = {
	"strings_demo",
	strings_demo_modevent,
	NULL
};

DECLARE_MODULE(strings_demo, strings_demo_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(strings_demo, 1);
