/*-
 * Chapter 22 Stage 1: header additions.
 *
 * Append these fields to struct myfirst_softc in myfirst.h.
 * Stage 1 only needs counters; Stage 2 and 3 add more fields.
 */

/*
 * Power-management counters. Expose through sysctls under
 * dev.myfirst.N.
 */
/*
	uint64_t power_suspend_count;
	uint64_t power_resume_count;
	uint64_t power_shutdown_count;
 */
