#include "log.h"

int
main(void)
{
	LOG_INFO("Starting");
	LOG_DEBUG("Internal detail: x=%d", 42);
	LOG_ERR("Something went wrong: %s", "timeout");
	return (0);
}
