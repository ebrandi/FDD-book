/*
 * echo_safe.c - Safe user-kernel data exchange for Chapter 5, Lab 2.
 *
 * This module creates /dev/echolab, which accepts writes from
 * userspace into a small kernel buffer and echoes them back on read.
 * The point is to practise crossing the user-kernel boundary safely
 * with uiomove(9).
 *
 * Compatible with FreeBSD 14.3.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/uio.h>

#define BUFFER_SIZE	256

MALLOC_DEFINE(M_ECHOLAB, "echo_lab", "Echo Lab Allocations");

static struct cdev	*echo_device;
static char		*kernel_buffer;

static int
echo_write(struct cdev *dev __unused, struct uio *uio, int flag __unused)
{
	size_t bytes_to_copy;
	int error;

	printf("Echo Lab: Write request for %zd bytes\n", uio->uio_resid);

	if (kernel_buffer == NULL)
		return (ENXIO);

	/* Leave room for the trailing NUL. */
	bytes_to_copy = MIN(uio->uio_resid, BUFFER_SIZE - 1);

	memset(kernel_buffer, 0, BUFFER_SIZE);

	error = uiomove(kernel_buffer, bytes_to_copy, uio);
	if (error != 0) {
		printf("Echo Lab: uiomove from user failed: %d\n", error);
		return (error);
	}

	kernel_buffer[bytes_to_copy] = '\0';
	printf("Echo Lab: Received from user: '%s' (%zu bytes)\n",
	    kernel_buffer, bytes_to_copy);

	return (0);
}

static int
echo_read(struct cdev *dev __unused, struct uio *uio, int flag __unused)
{
	char response[BUFFER_SIZE + 64];
	size_t response_len;
	int error;

	if (kernel_buffer == NULL)
		return (ENXIO);

	snprintf(response, sizeof(response),
	    "Echo: '%s' (received %zu bytes at ticks %d)\n",
	    kernel_buffer, strnlen(kernel_buffer, BUFFER_SIZE), ticks);

	response_len = strlen(response);

	if (uio->uio_offset >= (off_t)response_len)
		return (0);	/* EOF */

	if (uio->uio_offset + uio->uio_resid > (off_t)response_len)
		response_len -= uio->uio_offset;
	else
		response_len = uio->uio_resid;

	printf("Echo Lab: Read request, sending %zu bytes\n", response_len);

	error = uiomove(response + uio->uio_offset, response_len, uio);
	if (error != 0)
		printf("Echo Lab: uiomove to user failed: %d\n", error);

	return (error);
}

static struct cdevsw echo_cdevsw = {
	.d_version =	D_VERSION,
	.d_read =	echo_read,
	.d_write =	echo_write,
	.d_name =	"echolab",
};

static int
echo_safe_handler(module_t mod __unused, int what, void *arg __unused)
{

	switch (what) {
	case MOD_LOAD:
		printf("Echo Lab: Module loading\n");

		kernel_buffer = malloc(BUFFER_SIZE, M_ECHOLAB,
		    M_WAITOK | M_ZERO);
		if (kernel_buffer == NULL)
			return (ENOMEM);

		echo_device = make_dev(&echo_cdevsw, 0, UID_ROOT, GID_WHEEL,
		    0666, "echolab");
		if (echo_device == NULL) {
			printf("Echo Lab: Failed to create character device\n");
			free(kernel_buffer, M_ECHOLAB);
			kernel_buffer = NULL;
			return (ENXIO);
		}

		printf("Echo Lab: Device /dev/echolab created\n");
		printf("Echo Lab: Test with: echo 'Hello' > /dev/echolab\n");
		printf("Echo Lab: Read with:  cat /dev/echolab\n");
		return (0);

	case MOD_UNLOAD:
		printf("Echo Lab: Module unloading\n");
		if (echo_device != NULL) {
			destroy_dev(echo_device);
			echo_device = NULL;
		}
		if (kernel_buffer != NULL) {
			free(kernel_buffer, M_ECHOLAB);
			kernel_buffer = NULL;
		}
		printf("Echo Lab: Module unloaded successfully\n");
		return (0);

	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t echo_safe_mod = {
	"echo_safe",
	echo_safe_handler,
	NULL
};

DECLARE_MODULE(echo_safe, echo_safe_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(echo_safe, 1);
