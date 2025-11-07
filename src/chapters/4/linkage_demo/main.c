#include <stdio.h>

/*
 * 'extern' tells the compiler:
 * - This variable exists in another file (foo.c).
 * - Do not allocate storage for it here.
 */
extern int shared_counter;

/*
 * Forward declaration for the function defined in foo.c:
 * - Lets us call increment() from this file.
 */
void increment(void);

int main(void) {
    increment();            // Calls increment() from foo.c
    shared_counter += 10;   // Legal: shared_counter has external linkage
    increment();

    // internal_flag = 0;   // ERROR: not visible here (internal linkage in foo.c)
    return 0;
}
