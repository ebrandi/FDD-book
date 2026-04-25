# FreeBSD Kernel Subsystem Map

A single-page view of the seven subsystems a driver author meets most
often in FreeBSD, their responsibilities, and their most characteristic
types. Keep it next to the chapter you are reading.

## The Big Picture

```
+-----------------------------------------------------------------+
|                            USER SPACE                           |
|     applications, daemons, shells, tools, the libraries         |
+-----------------------------------------------------------------+
                               |
                  system-call trap (the boundary)
                               |
+-----------------------------------------------------------------+
|                           KERNEL SPACE                          |
|                                                                 |
|   +-----------------------+   +-----------------------------+   |
|   |   VFS / devfs / GEOM  |   |        Network stack        |   |
|   |  vnode, buf, bio,     |   |   mbuf, socket, ifnet,      |   |
|   |  vop_vector, cdevsw   |   |   route, VNET               |   |
|   +-----------------------+   +-----------------------------+   |
|                 \                     /                         |
|                  \                   /                          |
|                 Driver infrastructure (Newbus)                  |
|           device_t, driver_t, devclass_t, softc, kobj           |
|           bus_alloc_resource, bus_space, bus_dma                |
|                               |                                 |
|      Process/thread subsystem  |  Memory / VM subsystem         |
|      proc, thread              |  vm_map, vm_object, vm_page    |
|      ULE scheduler, kthreads   |  pmap, UMA, pagers             |
|                               |                                 |
|         Boot and module system (SYSINIT, KLD, modules)          |
|         Kernel services (eventhandler, taskqueue, callout)      |
+-----------------------------------------------------------------+
                               |
                      hardware I/O boundary
                               |
+-----------------------------------------------------------------+
|                             HARDWARE                            |
|     MMIO registers, interrupt controllers, DMA-capable memory   |
+-----------------------------------------------------------------+
```

## The Seven Subsystems at a Glance

| Subsystem                | Responsibility (one line)                                   | Characteristic types                       | Header entry point                    |
| :----------------------- | :---------------------------------------------------------- | :----------------------------------------- | :------------------------------------ |
| Process and Thread       | Who is running, when, and on which CPU                      | `struct proc`, `struct thread`             | `/usr/src/sys/sys/proc.h`             |
| Memory (VM)              | Virtual-to-physical mappings, pages, allocators             | `vm_map_t`, `vm_object_t`, `vm_page_t`, `uma_zone_t` | `/usr/src/sys/vm/vm.h`       |
| File and VFS             | Files, devices in `/dev`, GEOM stacks, buffer cache         | `struct vnode`, `vop_vector`, `struct bio`, `struct buf` | `/usr/src/sys/sys/vnode.h` |
| Network                  | Packet movement, interfaces, sockets, VNETs                 | `struct mbuf`, `ifnet` / `if_t`, `struct socket` | `/usr/src/sys/sys/mbuf.h`         |
| Driver Infrastructure    | Newbus: the device tree and the driver binding machinery    | `device_t`, `driver_t`, `devclass_t`, softc | `/usr/src/sys/sys/bus.h`              |
| Boot and Module System   | How the kernel starts and how modules come and go           | `SYSINIT`, `SI_SUB_*`, `MODULE_DEPEND`     | `/usr/src/sys/sys/kernel.h`, `/usr/src/sys/sys/module.h` |
| Kernel Services          | Cross-cutting services: events, deferred work, timers       | `eventhandler_tag`, `struct taskqueue`, `struct callout` | `/usr/src/sys/sys/eventhandler.h` |

## When Each Subsystem Shows Up

| You are about to...                               | You are about to touch...             |
| :------------------------------------------------ | :------------------------------------ |
| Allocate memory for a softc or a buffer           | Memory (VM)                           |
| Create or destroy a kernel worker thread          | Process and Thread                    |
| Publish a `/dev` entry                            | File and VFS (devfs, cdevsw)          |
| Accept a block I/O request from a filesystem      | File and VFS (GEOM, struct bio)       |
| Register a network interface                      | Network (ifnet)                       |
| Receive or transmit a packet                      | Network (mbuf, ifnet)                 |
| Match a driver to a PCI or USB device             | Driver Infrastructure (Newbus)        |
| Declare a dependency on another module            | Boot and Module System                |
| Run a hook at kernel shutdown or at low memory    | Kernel Services (eventhandler)        |
| Defer work out of an interrupt filter             | Kernel Services (taskqueue)           |
| Arm a watchdog timer                              | Kernel Services (callout)             |

## Pattern to Remember

Every driver begins in Driver Infrastructure (Newbus), borrows from
Memory (VM) for its state, borrows from Process and Thread for the
threads it runs on, and then *publishes itself* to one (or more) of
three subsystems on top: File and VFS for character or storage
drivers, Network for interface drivers, or a specialised framework
(input, sound, crypto) that itself sits on one of those layers.
Kernel Services and the Boot and Module System are the cross-cutting
helpers underneath all of them.

When a piece of code feels unfamiliar, ask: *which of the seven
subsystems is it in?* Once you can answer that, the rest follows.
