#include <stdio.h>
#include <string.h>

int main() {
    // Fill stack with non-zero garbage
    char garbage[100];
    memset(garbage, 'Z', sizeof(garbage));
    
    // Deliberately forget the null terminator
    char broken[5] = {'B', 'S', 'D', '!', 'X'};  

    // Print as if it were a string - will likely show garbage now
    printf("Broken string: %s\n", broken);

    // Now with proper termination
    char fixed[6] = {'B', 'S', 'D', '!', 'X', '\0'};
    printf("Fixed string: %s\n", fixed);

    return 0;
}
