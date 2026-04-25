# Structure-to-Subsystem Index

A fast lookup table: when you meet an unfamiliar type in FreeBSD
kernel or driver code, which subsystem owns it, and where is it
declared. Use it as a translator while reading `/usr/src/sys/`.

The groups below follow Appendix E. Within each group, entries are
listed in the order you are likely to meet them in the book.

## Process and Thread

| Type or name              | Declared in                                | What it represents                                     |
| :------------------------ | :----------------------------------------- | :----------------------------------------------------- |
| `struct proc`             | `/usr/src/sys/sys/proc.h`                  | One running process: PID, credentials, address space   |
| `struct thread`           | `/usr/src/sys/sys/proc.h`                  | One runnable thread of execution                       |
| `curthread`               | `/usr/src/sys/sys/proc.h` (macro)          | Pointer to the thread currently executing on this CPU  |
| `kthread_add`             | `/usr/src/sys/sys/kthread.h`               | Add a thread to an existing kernel process             |
| `kproc_create`            | `/usr/src/sys/sys/kthread.h`               | Create a new kernel process with one initial thread    |

## Memory (VM)

| Type or name              | Declared in                                | What it represents                                     |
| :------------------------ | :----------------------------------------- | :----------------------------------------------------- |
| `vm_map_t`                | `/usr/src/sys/vm/vm.h`, `/usr/src/sys/vm/vm_map.h` | An address space's mapping list                |
| `vm_object_t`             | `/usr/src/sys/vm/vm.h`, `/usr/src/sys/vm/vm_object.h` | A backing store of pages                     |
| `vm_page_t`               | `/usr/src/sys/vm/vm.h`, `/usr/src/sys/vm/vm_page.h` | One physical page of RAM                       |
| `uma_zone_t`              | `/usr/src/sys/vm/uma.h`                    | A slab-backed allocator for a fixed-size object        |
| `struct malloc_type`      | `/usr/src/sys/sys/malloc.h`                | A named accounting bucket for `malloc(9)`              |
| `pmap_t`                  | `/usr/src/sys/vm/pmap.h`                   | A machine-dependent page-table manager                 |

## File and VFS

| Type or name              | Declared in                                | What it represents                                     |
| :------------------------ | :----------------------------------------- | :----------------------------------------------------- |
| `struct vnode`            | `/usr/src/sys/sys/vnode.h`                 | A VFS-level handle to a file, directory, or device     |
| `struct vop_vector`       | generated from `/usr/src/sys/kern/vnode_if.src` | A per-filesystem vnode-operation dispatch table   |
| `struct mount`            | `/usr/src/sys/sys/mount.h`                 | A mounted filesystem                                   |
| `struct cdev`             | `/usr/src/sys/sys/conf.h`                  | A character device node managed by devfs               |
| `struct cdevsw`           | `/usr/src/sys/sys/conf.h`                  | A character driver's entry-point table                 |
| `struct buf`              | `/usr/src/sys/sys/buf.h`                   | A buffer-cache descriptor used by the old block path   |
| `struct bio`              | `/usr/src/sys/sys/bio.h`                   | A per-operation block I/O request on the GEOM path     |
| `struct g_provider`       | `/usr/src/sys/geom/geom.h`                 | A storage surface a GEOM consumer can attach to        |
| `struct g_consumer`       | `/usr/src/sys/geom/geom.h`                 | A GEOM client attached to a provider                   |

## Network

| Type or name              | Declared in                                | What it represents                                     |
| :------------------------ | :----------------------------------------- | :----------------------------------------------------- |
| `struct mbuf`             | `/usr/src/sys/sys/mbuf.h`                  | A packet fragment; packets are chains of these         |
| `struct m_tag`            | `/usr/src/sys/sys/mbuf.h`                  | Extensible metadata attached to an mbuf                |
| `if_t`                    | `/usr/src/sys/net/if.h`                    | Modern per-interface descriptor handle                 |
| `struct ifnet`            | `/usr/src/sys/net/if_private.h`            | Full per-interface state (legacy view of `if_t`)       |
| `struct socket`           | `/usr/src/sys/sys/socketvar.h`             | Kernel state behind a `socket(2)` file descriptor      |
| `ifnet_arrival_event`     | `/usr/src/sys/net/if_var.h`                | Eventhandler fired when an interface appears           |
| `ifnet_departure_event`   | `/usr/src/sys/net/if_var.h`                | Eventhandler fired when an interface goes away         |
| `CURVNET_SET`, `VNET_*`   | `/usr/src/sys/net/vnet.h`                  | Macros for per-VNET state                              |

## Driver Infrastructure (Newbus)

| Type or name              | Declared in                                | What it represents                                     |
| :------------------------ | :----------------------------------------- | :----------------------------------------------------- |
| `device_t`                | `/usr/src/sys/sys/bus.h`                   | Opaque handle to a Newbus device-tree node             |
| `driver_t`                | `/usr/src/sys/sys/bus.h`                   | Descriptor for a driver: name, methods, softc size     |
| `devclass_t`              | `/usr/src/sys/sys/bus.h`                   | Registry of device instances for a driver class        |
| `device_method_t`         | `/usr/src/sys/sys/bus.h` (kobj)            | One entry in a driver's method table                   |
| `DEVMETHOD`, `DEVMETHOD_END` | `/usr/src/sys/sys/bus.h`                | Macros to populate a `device_method_t[]`               |
| `DRIVER_MODULE`           | `/usr/src/sys/sys/bus.h`                   | The top-level registration macro for a driver          |
| `struct resource`         | `/usr/src/sys/sys/rman.h`                  | An allocated memory, I/O, or IRQ window                |
| softc                     | convention, declared in the driver         | Your per-instance driver state                         |

## Boot and Module System

| Type or name              | Declared in                                | What it represents                                     |
| :------------------------ | :----------------------------------------- | :----------------------------------------------------- |
| `SYSINIT`, `SYSUNINIT`    | `/usr/src/sys/sys/kernel.h`                | Register a startup/teardown hook at a specific phase   |
| `SI_SUB_*`                | `/usr/src/sys/sys/kernel.h`                | Coarse phase IDs in the init order                     |
| `SI_ORDER_*`              | `/usr/src/sys/sys/kernel.h`                | Fine ordering within a phase                           |
| `MODULE_VERSION`          | `/usr/src/sys/sys/module.h`                | Declare this module's ABI version                      |
| `MODULE_DEPEND`           | `/usr/src/sys/sys/module.h`                | Declare a required module and its version range        |
| `kld_file_t`              | `/usr/src/sys/sys/linker.h`                | A loaded kernel linker file                            |

## Kernel Services

| Type or name              | Declared in                                | What it represents                                     |
| :------------------------ | :----------------------------------------- | :----------------------------------------------------- |
| `eventhandler_tag`        | `/usr/src/sys/sys/eventhandler.h`          | A subscription token for an event handler             |
| `EVENTHANDLER_REGISTER`   | `/usr/src/sys/sys/eventhandler.h`          | Subscribe to an event                                  |
| `EVENTHANDLER_DEREGISTER` | `/usr/src/sys/sys/eventhandler.h`          | Unsubscribe from an event                              |
| `struct taskqueue`        | `/usr/src/sys/sys/taskqueue.h`             | A queue of deferred work items                         |
| `struct task`             | `/usr/src/sys/sys/taskqueue.h`             | One work item enqueued on a taskqueue                  |
| `struct gtaskqueue`       | `/usr/src/sys/sys/gtaskqueue.h`            | A CPU-affinity-aware grouped taskqueue                 |
| `struct callout`          | `/usr/src/sys/sys/callout.h`               | A one-shot or periodic kernel timer                    |

## When the Type Is Not Here

Search the tree with the patterns from the source-tree navigator
cheatsheet. The fastest manual command is:

```sh
grep -n 'struct NAME {' /usr/src/sys/sys/ /usr/src/sys/kern/ \
                        /usr/src/sys/vm/ /usr/src/sys/net/ \
                        /usr/src/sys/dev/
```

Nine times out of ten the right header is in one of those
directories.
