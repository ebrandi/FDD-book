#include "ops.h"

/* Console variant: pretend to manage a simple console device. */
static int inited;
static const char *state = "stopped";

static int
c_init(void)
{
	inited = 1;
	return (0);
}

static void
c_start(void)
{
	if (inited)
		state = "console-running";
}

static void
c_stop(void)
{
	state = "stopped";
}

static const char *
c_status(void)
{
	return (state);
}

const dev_ops_t console_ops = {
	.name = "console",
	.init = c_init,
	.start = c_start,
	.stop = c_stop,
	.status = c_status
};
