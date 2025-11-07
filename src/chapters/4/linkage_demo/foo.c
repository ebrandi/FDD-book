#include <stdio.h>

/* 
 * Global variable with external linkage:
 * - Visible to other files (translation units) in the program.
 * - No 'static' keyword means it has external linkage by default.
 */
int shared_counter = 0;

/* 
 * File-private variable with internal linkage:
 * - The 'static' keyword at file scope means this name
 *   is only visible inside foo.c.
 */
static int internal_flag = 1;

/*
 * Function with external linkage by default:
 * - Can be called from other files if they declare its prototype.
 */
void increment(void) {
    /* 
     * Local variable with no linkage:
     * - Exists only during this function call.
     * - Cannot be accessed from anywhere else.
     */
    int step = 1;

    if (internal_flag)         // Only code in foo.c can see internal_flag
        shared_counter += step; // Modifies the global shared_counter

    printf("Counter: %d\n", shared_counter);
}
