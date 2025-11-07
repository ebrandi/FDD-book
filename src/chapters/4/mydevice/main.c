#include "mydevice.h"

int main(void)
{
    mydevice_probe();
    mydevice_attach();
    mydevice_detach();
    return 0;
}
