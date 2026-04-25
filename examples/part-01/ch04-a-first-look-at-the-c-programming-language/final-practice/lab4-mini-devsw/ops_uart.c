#include "ops.h"

/* UART variant: separate state to show independence between backends. */
static int ready;
static const char *state = "idle";

static int
u_init(void)
{
	ready = 1;
	return (0);
}

static void
u_start(void)
{
	if (ready)
		state = "uart-txrx";
}

static void
u_stop(void)
{
	state = "idle";
}

static const char *
u_status(void)
{
	return (state);
}

const dev_ops_t uart_ops = {
	.name = "uart",
	.init = u_init,
	.start = u_start,
	.stop = u_stop,
	.status = u_status
};
