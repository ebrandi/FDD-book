# Manpage and Source Reading Plan

An eight-step self-study itinerary across the subsystems covered in
Appendix E. Each step names one or two manual pages and one readable
source file. Take one step per session; do not rush.

The plan assumes a FreeBSD 14.3 system with a checked-out source tree
under `/usr/src/sys/`. Every manual page is accessible with
`man 9 NAME`, every source file with any text editor.

---

## Step 0: Warm-Up

- `man 9 intro`
- `ls /usr/src/sys/` and `ls /usr/src/sys/sys/`

Read `intro(9)` once, then scan the directory listings. You are not
trying to memorise anything; you are trying to *notice* which
subdirectories exist and roughly what they are named after. This is
the map your brain will index against for the rest of the plan.

## Step 1: Process and Thread

- `man 9 kthread`, `man 9 kproc`, `man 9 curthread`
- Open `/usr/src/sys/sys/proc.h`; read the comment above `struct proc`
  and `struct thread`, then skim their fields.
- Read `/usr/src/sys/dev/random/random_harvestq.c`, focusing on where
  it creates and joins its kproc.

Goal: know what a `struct thread` is, know the difference between
`kthread_add(9)` and `kproc_create(9)`, and recognise the signature
pattern for a driver that spawns a background worker.

## Step 2: Memory (VM)

- `man 9 malloc`, `man 9 uma`, `man 9 contigmalloc`, `man 9 bus_dma`
- Open `/usr/src/sys/vm/uma.h` and read the header comment.
- Skim the first page or two of `/usr/src/sys/vm/uma_core.c` to see
  what an allocator's init path looks like.

Goal: know the three allocators drivers reach for (`malloc(9)`,
`uma(9)`, `contigmalloc(9)`), know that `bus_dma(9)` is the portable
DMA wrapper, and know that `pmap(9)` is the architecture-specific
page-table layer you do not normally call directly.

## Step 3: File and VFS (Character Path)

- `man 9 cdev`, `man 9 make_dev`, `man 5 devfs`
- Read `/usr/src/sys/dev/null/null.c` end to end.
- Open `/usr/src/sys/fs/devfs/devfs_devs.c` and notice how character
  device entries are wired into devfs.

Goal: understand how a `cdevsw` and a `make_dev_s(9)` call combine
into a visible `/dev` entry, and recognise the structure shape of a
character driver.

## Step 4: File and VFS (Storage Path)

- `man 9 g_attach`, `man 9 bio`, `man 4 geom`
- Open `/usr/src/sys/sys/bio.h` and read `struct bio`.
- Open `/usr/src/sys/geom/geom.h` and notice the provider and consumer
  typedefs.

Goal: understand that a storage driver receives `struct bio`
requests on a start routine and calls `g_io_deliver()` when it
finishes them, and recognise the GEOM provider/consumer vocabulary.

## Step 5: Network

- `man 9 mbuf`, `man 9 ifnet`, `man 9 ether_ifattach`, `man 4 vnet`
- Open `/usr/src/sys/sys/mbuf.h` and read the `struct mbuf` comment.
- Read `/usr/src/sys/net/if_tuntap.c` end to end if you can.

Goal: recognise an mbuf chain, understand that a driver publishes
itself as an `ifnet` rather than as a `/dev` entry, and notice how
the transmit and receive paths fit together.

## Step 6: Driver Infrastructure (Newbus)

- `man 9 device`, `man 9 driver`, `man 9 DRIVER_MODULE`,
  `man 9 bus_alloc_resource`, `man 9 bus_setup_intr`
- Open `/usr/src/sys/sys/bus.h` and skim the macro definitions for
  `DEVMETHOD`, `DEVMETHOD_END`, and `DRIVER_MODULE`.
- Read `/usr/src/sys/dev/led/led.c`; note how it declares methods,
  registers with `DRIVER_MODULE`, and keeps a softc.

Goal: recognise the Newbus registration pattern in any driver,
know where `device_t` and softc come from, and know the basic
resource-allocation calls.

## Step 7: Boot and Module System

- `man 9 SYSINIT`, `man 9 kld`, `man 9 module`,
  `man 9 MODULE_VERSION`, `man 9 MODULE_DEPEND`
- Open `/usr/src/sys/sys/kernel.h` and read the `SYSINIT` comment plus
  the `SI_SUB_*` list.
- Skim the beginning of `/usr/src/sys/kern/init_main.c` to see how
  `mi_startup()` walks the SYSINIT list.

Goal: understand how kernel initialisation is ordered, why a driver
may need to declare dependencies with `MODULE_DEPEND`, and how
`DRIVER_MODULE` ties into the module event handler.

## Step 8: Kernel Services

- `man 9 eventhandler`, `man 9 taskqueue`, `man 9 callout`
- Open `/usr/src/sys/sys/eventhandler.h` and list the standard event
  tags defined near the bottom of the file.
- Re-read the kproc and taskqueue sections of
  `/usr/src/sys/dev/random/random_harvestq.c` in that light.

Goal: know when to reach for an eventhandler, a taskqueue, or a
callout, and know where each one is declared.

---

## Suggested Pacing

| Step                              | Approximate time         |
| :-------------------------------- | :----------------------- |
| 0. Warm-up                        | 15 minutes               |
| 1. Process and Thread             | 30 minutes               |
| 2. Memory (VM)                    | 45 minutes               |
| 3. File and VFS (character)       | 45 minutes               |
| 4. File and VFS (storage)         | 45 minutes               |
| 5. Network                        | 60 minutes               |
| 6. Driver Infrastructure          | 45 minutes               |
| 7. Boot and Module System         | 30 minutes               |
| 8. Kernel Services                | 30 minutes               |

Total: around five and a half hours of focused reading, spread across
eight sessions.

## What to Do After the Plan

The plan is a skeleton. After step 8 you can take any real driver in
`/usr/src/sys/dev/` and read it with roughly the same questions in
mind: which subsystems does it touch, which structures does it
manipulate, which kernel services does it use, and which parts of the
boot-and-module system does it depend on. Appendix E will keep making
more sense each time, because the same seven subsystems keep showing
up.
