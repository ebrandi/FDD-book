#ifndef _MYDRIVER_H_
#define _MYDRIVER_H_

#include <sys/types.h>
#include <sys/bus.h>

// Public entry points: declared here so bus/framework code can call them
// These are the entry points the kernel will call during the device's lifecycle
// Function prototypes (declarations)
int  mydriver_probe(device_t dev);
int  mydriver_attach(device_t dev);
int  mydriver_detach(device_t dev);

#endif /* _MYDRIVER_H_ */
