# Driver Touchpoint Checklist

Before you start a new driver, walk this checklist and mark the
subsystems the driver will actually touch. Unchecked rows are the
sections of Appendix E you can skip for now. Checked rows are the
sections worth rereading *before* you write the first line of code.

The goal is not completeness. It is orientation: knowing, on paper,
which subsystems will push back on your design and which ones will
be happy to ignore you.

## Project Metadata

```
Driver name:        _______________________________
Parent bus:         pci / usbus / iicbus / spibus / acpi / simplebus / nexus
Target arch(s):     _______________________________
FreeBSD version:    _______________________________
Primary purpose:    _______________________________
```

## Subsystem Checklist

### 1. Process and Thread

```
[ ] Driver spawns a kernel thread or kproc
    (watchdog, poller, deferred work host)
[ ] Driver looks at the calling thread's process credentials
    (in d_ioctl, in a sysctl handler, in a privilege check)
[ ] Driver uses curthread for anything other than sleep/wakeup
[ ] Driver runs at a non-default priority
```

If any box is checked, re-read Appendix E "Process and Thread
Subsystem" and Chapter 14 (taskqueues) or Chapter 24 (kernel
integration, kprocs).

### 2. Memory (VM)

```
[ ] Driver allocates memory with malloc(9)
[ ] Driver uses a UMA zone for fixed-size objects
[ ] Driver needs contiguous physical memory (contigmalloc)
[ ] Driver does DMA and uses bus_dma(9)
[ ] Driver exposes memory to userland via d_mmap or d_mmap_single
[ ] Driver wires pages of a user buffer across a long I/O
```

If any box is checked, re-read Appendix E "Memory Subsystem" and the
corresponding chapter (5 for allocators, 21 for DMA, 16/17 for
bus_space).

### 3. File and VFS

```
[ ] Driver publishes a /dev entry with make_dev_s(9) + cdevsw
[ ] Driver implements d_open, d_close, d_read, d_write, d_ioctl
[ ] Driver publishes a GEOM provider (storage driver)
[ ] Driver receives struct bio on a start routine
[ ] Driver interacts with struct buf (legacy block path)
[ ] Driver needs to respect file-system locking rules
```

If any box is checked, re-read Appendix E "File and VFS Subsystem"
and Chapters 7-9 (character driver) or Chapter 27 (storage and
GEOM).

### 4. Network

```
[ ] Driver registers as an ifnet (network driver)
[ ] Driver transmits or receives mbufs
[ ] Driver must be VNET-aware (jails with VIMAGE)
[ ] Driver needs routing or socket knowledge beyond ifnet
[ ] Driver subscribes to ifnet_arrival_event / ifnet_departure_event
[ ] Driver offloads checksums, TSO, LRO, or RSS
```

If any box is checked, re-read Appendix E "Network Subsystem" and
Chapter 28 (writing a network driver).

### 5. Driver Infrastructure (Newbus)

```
[ ] Driver declares a device_method_t array
[ ] Driver uses DRIVER_MODULE to bind under a parent bus
[ ] Driver allocates resources with bus_alloc_resource_any
[ ] Driver sets up interrupts with bus_setup_intr
[ ] Driver supports multiple parent buses
    (e.g. DRIVER_MODULE twice under pci and under acpi)
[ ] Driver uses kobj directly beyond the standard DEVMETHOD macros
```

(Every driver will have at least the first four boxes checked.)
Re-read Appendix E "Driver Infrastructure" and Chapter 6 (anatomy),
Chapter 7 (first driver), Chapter 18 (PCI).

### 6. Boot and Module System

```
[ ] Driver requires another module (MODULE_DEPEND)
[ ] Driver must run early in kernel startup (SYSINIT with low SI_SUB)
[ ] Driver must run late in kernel startup (high SI_SUB, high SI_ORDER)
[ ] Driver must publish its own ABI version for others to depend on
[ ] Driver is a console or early-boot device
[ ] Driver must attach before root mount
```

If any box is checked, re-read Appendix E "Boot and Module System"
and Chapter 24 (kernel integration), Chapter 32 (embedded boot if
applicable).

### 7. Kernel Services

```
[ ] Driver registers a shutdown_pre_sync / shutdown_final handler
[ ] Driver reacts to vm_lowmem
[ ] Driver needs to defer work out of an interrupt filter
    (use taskqueue)
[ ] Driver needs a timer or a watchdog (use callout)
[ ] Driver uses a grouped taskqueue for per-CPU work
[ ] Driver exposes sysctl counters or tunables
```

If any box is checked, re-read Appendix E "Kernel Services" and
Chapter 13 (timers), Chapter 14 (taskqueues), Chapter 24 (kernel
integration).

## After the Checklist

Once the checklist is filled in, the shape of the driver is already
visible on the page. A minimal character driver for a small
peripheral might check only Driver Infrastructure and File and VFS
(character path). A PCI network driver with DMA might check
Memory, Network, Driver Infrastructure, and Kernel Services. An
early-boot console driver might check Driver Infrastructure, Boot
and Module System, and a single box under Kernel Services for the
shutdown hook.

The checked subsystems are where you should concentrate your
reading *before* you start coding. The unchecked ones do not
disappear, but they move to the background, and you can ignore them
until the design tells you otherwise.

## A Final Habit

Keep the filled-in checklist in the driver's design notes or at the
top of a `README.md` in your driver tree. Revisit it when the design
changes. A subsystem that sneaks in after the initial design (for
example, DMA that appears late in development) is usually a signal
that you need to revisit the corresponding section of Appendix E
before you write more code.
