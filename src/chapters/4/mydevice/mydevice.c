#include <stdio.h>
#include "mydevice.h"

// Function definitions
void mydevice_probe(void)
{
    printf("[mydevice] Probing hardware... done.\n");
}

void mydevice_attach(void)
{
    printf("[mydevice] Attaching device and initializing resources...\n");
}

void mydevice_detach(void)
{
    printf("[mydevice] Detaching device and cleaning up.\n");
}
