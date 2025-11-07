#include <stdio.h>

// Function definition
unsigned int read_register(unsigned int address)
{
    // Simulated register value based on address
    unsigned int value = address * 2; 
    printf("[driver] Reading register at address 0x%X...\n", address);
    return value;
}

int main(void)
{
    unsigned int reg_addr = 0x10; // pretend this is a hardware register address
    unsigned int data;

    data = read_register(reg_addr); // Call the function
    printf("[driver] Value read: 0x%X\n", data);

    return 0;
}
