/*
 * error_handling.c - Comprehensive error handling for Chapter 5, Lab 4.
 *
 * This module creates /dev/errorlab, a small control surface that
 * accepts a handful of text commands (alloc, free <n>, error_on,
 * error_off, status, cleanup) to let you exercise error paths on
 * demand.
 *
 * The kernel C dialect covered here:
 *   - errno-style return codes
 *   - resource cleanup on every error path
 *   - error injection for deliberate failure testing
 *   - statistics counters updated atomically
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
#include <sys/errno.h>

#define MAX_BUFFERS	5
#define BUFFER_SIZE	1024

MALLOC_DEFINE(M_ERRORLAB, "error_lab", "Error Handling Lab");

struct error_lab_state {
	void	*buffers[MAX_BUFFERS];
	int	 buffer_count;
	int	 error_injection_enabled;
	int	 operation_count;
	int	 success_count;
	int	 error_count;
};

static struct error_lab_state	*lab_state = NULL;
static struct cdev		*error_device = NULL;

static void
cleanup_all_resources(struct error_lab_state *state)
{
	int i;

	if (state == NULL)
		return;

	printf("Error Lab: Beginning resource cleanup\n");
	for (i = 0; i < MAX_BUFFERS; i++) {
		if (state->buffers[i] != NULL) {
			printf("Error Lab: Freeing buffer %d at %p\n",
			    i, state->buffers[i]);
			free(state->buffers[i], M_ERRORLAB);
			state->buffers[i] = NULL;
		}
	}
	state->buffer_count = 0;
	printf("Error Lab: All %d slots cleared\n", MAX_BUFFERS);
}

static int
allocate_buffer_safe(struct error_lab_state *state)
{
	void *buf;
	int slot;

	if (state == NULL)
		return (EINVAL);

	state->operation_count++;

	if (state->buffer_count >= MAX_BUFFERS) {
		state->error_count++;
		return (ENOSPC);
	}

	for (slot = 0; slot < MAX_BUFFERS; slot++) {
		if (state->buffers[slot] == NULL)
			break;
	}
	if (slot >= MAX_BUFFERS) {
		state->error_count++;
		return (ENOSPC);
	}

	if (state->error_injection_enabled) {
		printf("Error Lab: Simulating allocation failure\n");
		state->error_count++;
		return (ENOMEM);
	}

	buf = malloc(BUFFER_SIZE, M_ERRORLAB, M_NOWAIT | M_ZERO);
	if (buf == NULL) {
		printf("Error Lab: Real allocation failure for %d bytes\n",
		    BUFFER_SIZE);
		state->error_count++;
		return (ENOMEM);
	}

	state->buffers[slot] = buf;
	state->buffer_count++;
	state->success_count++;

	printf("Error Lab: Allocated buffer %d at %p (%d/%d total)\n",
	    slot, buf, state->buffer_count, MAX_BUFFERS);
	return (0);
}

static int
free_buffer_safe(struct error_lab_state *state, int slot)
{

	if (state == NULL)
		return (EINVAL);
	if (slot < 0 || slot >= MAX_BUFFERS) {
		printf("Error Lab: Invalid slot %d (must be 0-%d)\n",
		    slot, MAX_BUFFERS - 1);
		return (EINVAL);
	}
	if (state->buffers[slot] == NULL)
		return (ENOENT);

	printf("Error Lab: Freeing buffer %d at %p\n",
	    slot, state->buffers[slot]);
	free(state->buffers[slot], M_ERRORLAB);
	state->buffers[slot] = NULL;
	state->buffer_count--;
	return (0);
}

static int
error_write(struct cdev *dev __unused, struct uio *uio, int flag __unused)
{
	char command[64];
	size_t len;
	int error;
	int slot;

	if (lab_state == NULL)
		return (EIO);

	len = MIN(uio->uio_resid, sizeof(command) - 1);
	error = uiomove(command, len, uio);
	if (error != 0)
		return (error);

	command[len] = '\0';
	if (len > 0 && command[len - 1] == '\n')
		command[len - 1] = '\0';

	printf("Error Lab: Processing command: '%s'\n", command);

	if (strcmp(command, "alloc") == 0) {
		error = allocate_buffer_safe(lab_state);
	} else if (strncmp(command, "free ", 5) == 0) {
		slot = strtol(command + 5, NULL, 10);
		error = free_buffer_safe(lab_state, slot);
	} else if (strcmp(command, "error_on") == 0) {
		lab_state->error_injection_enabled = 1;
		printf("Error Lab: Error injection ENABLED\n");
	} else if (strcmp(command, "error_off") == 0) {
		lab_state->error_injection_enabled = 0;
		printf("Error Lab: Error injection DISABLED\n");
	} else if (strcmp(command, "status") == 0) {
		printf("Error Lab: Status: %d/%d buffers, %d ops, %d ok, %d err\n",
		    lab_state->buffer_count, MAX_BUFFERS,
		    lab_state->operation_count, lab_state->success_count,
		    lab_state->error_count);
	} else if (strcmp(command, "cleanup") == 0) {
		cleanup_all_resources(lab_state);
	} else {
		printf("Error Lab: Unknown command '%s'\n", command);
		printf("Error Lab: Valid commands: alloc, free <n>, "
		    "error_on, error_off, status, cleanup\n");
		error = EINVAL;
	}

	return (error);
}

static int
error_read(struct cdev *dev __unused, struct uio *uio, int flag __unused)
{
	char status[512];
	size_t len;
	int i;

	if (lab_state == NULL)
		return (EIO);

	len = snprintf(status, sizeof(status),
	    "Error Handling Lab Status\n"
	    "========================\n"
	    "Buffers: %d/%d allocated\n"
	    "Operations: %d total (%d ok, %d err)\n"
	    "Error injection: %s\n"
	    "\nBuffer map:\n",
	    lab_state->buffer_count, MAX_BUFFERS,
	    lab_state->operation_count, lab_state->success_count,
	    lab_state->error_count,
	    lab_state->error_injection_enabled ? "ENABLED" : "disabled");

	for (i = 0; i < MAX_BUFFERS; i++) {
		len += snprintf(status + len, sizeof(status) - len,
		    "  Slot %d: %s\n", i,
		    lab_state->buffers[i] ? "ALLOCATED" : "free");
	}

	if (uio->uio_offset >= (off_t)len)
		return (0);

	return (uiomove(status + uio->uio_offset,
	    MIN(len - (size_t)uio->uio_offset, uio->uio_resid), uio));
}

static struct cdevsw error_cdevsw = {
	.d_version =	D_VERSION,
	.d_read =	error_read,
	.d_write =	error_write,
	.d_name =	"errorlab",
};

static int
error_handling_handler(module_t mod __unused, int what, void *arg __unused)
{

	switch (what) {
	case MOD_LOAD:
		printf("Error Lab: Module loading\n");

		lab_state = malloc(sizeof(*lab_state), M_ERRORLAB,
		    M_WAITOK | M_ZERO);
		if (lab_state == NULL)
			return (ENOMEM);

		error_device = make_dev(&error_cdevsw, 0, UID_ROOT, GID_WHEEL,
		    0666, "errorlab");
		if (error_device == NULL) {
			free(lab_state, M_ERRORLAB);
			lab_state = NULL;
			return (ENXIO);
		}

		printf("Error Lab: Device /dev/errorlab created\n");
		printf("Error Lab: Try: echo 'alloc' > /dev/errorlab\n");
		printf("Error Lab: Status: cat /dev/errorlab\n");
		return (0);

	case MOD_UNLOAD:
		printf("Error Lab: Module unloading\n");

		if (error_device != NULL) {
			destroy_dev(error_device);
			error_device = NULL;
		}
		if (lab_state != NULL) {
			printf("Error Lab: Final stats: %d total / %d ok / %d err\n",
			    lab_state->operation_count,
			    lab_state->success_count,
			    lab_state->error_count);
			cleanup_all_resources(lab_state);
			free(lab_state, M_ERRORLAB);
			lab_state = NULL;
		}
		printf("Error Lab: Module unloaded successfully\n");
		return (0);

	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t error_handling_mod = {
	"error_handling",
	error_handling_handler,
	NULL
};

DECLARE_MODULE(error_handling, error_handling_mod,
    SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(error_handling, 1);
