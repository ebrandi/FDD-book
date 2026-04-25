# FreeBSD Source-Tree Navigator

A short guide that turns "it must be somewhere under `/usr/src/sys`"
into "open this file first". Use it when you know the name of a
function, a macro, a structure, or a subsystem, but not yet the file
it lives in.

## Top-Level Map of `/usr/src/sys/`

| Directory                  | What lives here                                                 |
| :------------------------- | :-------------------------------------------------------------- |
| `/usr/src/sys/sys/`        | Cross-kernel headers: proc, mbuf, vnode, bus, queue, callout    |
| `/usr/src/sys/kern/`       | Machine-independent kernel core: scheduler, VFS, Newbus, syscalls |
| `/usr/src/sys/vm/`         | Virtual-memory subsystem: map, object, page, pagers, UMA        |
| `/usr/src/sys/net/`        | Network-stack core: ifnet, route, vnet                          |
| `/usr/src/sys/netinet/`    | IPv4 stack                                                      |
| `/usr/src/sys/netinet6/`   | IPv6 stack                                                      |
| `/usr/src/sys/geom/`       | GEOM framework and its classes                                  |
| `/usr/src/sys/fs/`         | Filesystems: devfs, cd9660, tarfs, tmpfs, fusefs, and so on     |
| `/usr/src/sys/dev/`        | Device drivers, grouped by bus or device family                 |
| `/usr/src/sys/amd64/`      | Machine-dependent code for amd64 (pmap, boot, interrupts)       |
| `/usr/src/sys/arm64/`      | Machine-dependent code for arm64                                |
| `/usr/src/sys/riscv/`      | Machine-dependent code for riscv                                |
| `/usr/src/sys/modules/`    | Per-module build glue (`Makefile`s that drive `bsd.kmod.mk`)    |
| `/usr/src/sys/conf/`       | Kernel build configuration (`GENERIC`, `MINIMAL`, tunables)     |
| `/usr/src/stand/`          | The boot loader (in FreeBSD 14.x)                               |

## Turning a Question Into a File

### By function name prefix

| Prefix                 | Look first in                                              |
| :--------------------- | :--------------------------------------------------------- |
| `vm_`, `uma_`, `pmap_` | `/usr/src/sys/vm/`                                         |
| `bus_`, `device_`, `driver_`, `devclass_`, `resource_` | `/usr/src/sys/kern/subr_bus.c`, `/usr/src/sys/kern/subr_rman.c` |
| `VOP_`, `vfs_`, `vn_`  | `/usr/src/sys/kern/vfs_*.c` or `/usr/src/sys/fs/`          |
| `g_`                   | `/usr/src/sys/geom/` (GEOM)                                |
| `if_`, `ether_`        | `/usr/src/sys/net/`                                        |
| `in_`, `ip_`, `tcp_`   | `/usr/src/sys/netinet/`                                    |
| `in6_`, `ip6_`         | `/usr/src/sys/netinet6/`                                   |
| `kthread_`, `kproc_`, `sched_`, `proc_` | `/usr/src/sys/kern/`                       |
| `mtx_`, `sx_`, `rm_`, `lockmgr_` | `/usr/src/sys/kern/kern_mutex.c`, `kern_sx.c`, `kern_rmlock.c`, `kern_lock.c` |
| `callout_`             | `/usr/src/sys/kern/kern_timeout.c`                         |
| `taskqueue_`, `gtaskqueue_` | `/usr/src/sys/kern/subr_taskqueue.c`, `subr_gtaskqueue.c` |
| `eventhandler_`        | `/usr/src/sys/kern/subr_eventhandler.c`                    |
| `kld_`, `linker_`      | `/usr/src/sys/kern/kern_linker.c`                          |
| `make_dev_`, `destroy_dev_` | `/usr/src/sys/kern/kern_conf.c`                       |
| `m_` (mbuf)            | `/usr/src/sys/kern/uipc_mbuf.c`, `/usr/src/sys/kern/kern_mbuf.c` |
| `bio_`, `biodone`, `bwrite` | `/usr/src/sys/kern/vfs_bio.c`                         |

When nothing matches, the exhaustive one-liner is:

```sh
grep -rn '\bFUNC_NAME\s*(' /usr/src/sys/ --include='*.c' --include='*.h'
```

### By macro name

| Macro family                                         | Declared in                               |
| :--------------------------------------------------- | :---------------------------------------- |
| `SYSINIT`, `SYSUNINIT`, `SI_SUB_*`, `SI_ORDER_*`     | `/usr/src/sys/sys/kernel.h`               |
| `DRIVER_MODULE`, `DEVMETHOD`, `DEVMETHOD_END`, `EARLY_DRIVER_MODULE` | `/usr/src/sys/sys/bus.h` |
| `MODULE_VERSION`, `MODULE_DEPEND`, `MODULE_METADATA` | `/usr/src/sys/sys/module.h`               |
| `EVENTHANDLER_DECLARE`, `_REGISTER`, `_INVOKE`       | `/usr/src/sys/sys/eventhandler.h`         |
| `VNET_DEFINE`, `VNET_DECLARE`, `CURVNET_SET`         | `/usr/src/sys/net/vnet.h`                 |
| `TAILQ_*`, `LIST_*`, `STAILQ_*`, `SLIST_*`           | `/usr/src/sys/sys/queue.h`                |
| `RB_*`, `SPLAY_*`                                    | `/usr/src/sys/sys/tree.h`                 |
| `MALLOC_DEFINE`, `MALLOC_DECLARE`                    | `/usr/src/sys/sys/malloc.h`               |
| `KASSERT`, `MPASS`, `INVARIANTS`                     | `/usr/src/sys/sys/systm.h`                |
| `VOP_*` wrappers                                     | generated in `sys/vnode_if.h` from `/usr/src/sys/kern/vnode_if.src` |

### By structure name

See the structure-to-subsystem-index cheatsheet. The short rule is:

1. Core kernel types start under `/usr/src/sys/sys/`.
2. VM types live under `/usr/src/sys/vm/`.
3. Network types start in `/usr/src/sys/net/` and branch into
   `/usr/src/sys/netinet/` and `/usr/src/sys/netinet6/`.
4. Bus types (`device_t`, `resource`, `rman`) live in
   `/usr/src/sys/sys/bus.h` and `/usr/src/sys/sys/rman.h`.

### By question

| Question                                          | Start here                                    |
| :------------------------------------------------ | :-------------------------------------------- |
| How does the kernel boot?                         | `/usr/src/sys/kern/init_main.c`               |
| How is each subsystem initialised?                | Walk the `SYSINIT` list in `init_main.c`      |
| How is a new syscall dispatched?                  | `/usr/src/sys/kern/syscalls.master` and `init_sysent.c` |
| What drivers does FreeBSD ship for PCI?           | `ls /usr/src/sys/dev/` (a lot of them)        |
| What does `open(2)` on `/dev/null` do?            | `/usr/src/sys/dev/null/null.c` and `/usr/src/sys/fs/devfs/` |
| How are interrupts registered?                    | `/usr/src/sys/kern/subr_intr.c`, `/usr/src/sys/kern/kern_intr.c` |
| Where is the scheduler?                           | `/usr/src/sys/kern/sched_ule.c`               |
| Where does UMA live?                              | `/usr/src/sys/vm/uma_core.c`                  |
| Where is the buffer cache?                        | `/usr/src/sys/kern/vfs_bio.c`                 |
| Where is the KLD loader?                          | `/usr/src/sys/kern/kern_linker.c`             |

## Small Readable Drivers

When the tree feels overwhelming, anchor yourself in one of these
small, self-contained drivers. Each is short enough to read in a
single sitting and representative of its subsystem.

| Driver                                                  | What to notice                                        |
| :------------------------------------------------------ | :---------------------------------------------------- |
| `/usr/src/sys/dev/null/null.c`                          | The minimal character driver (devfs, cdevsw)          |
| `/usr/src/sys/dev/led/led.c`                            | A small bus-attached driver with sysctl exposure      |
| `/usr/src/sys/dev/random/random_harvestq.c`             | Real use of kproc, eventhandler, taskqueue            |
| `/usr/src/sys/net/if_tuntap.c`                          | A readable network driver with both tun and tap       |
| `/usr/src/sys/dev/uart/uart_bus_pci.c`                  | How a PCI-attached driver binds to a parent bus       |
| `/usr/src/sys/kern/tty_info.c`                          | The `^T` SIGINFO handler; a short kernel-code read    |
| `/usr/src/sys/kern/kern_sig.c`                          | The real `coredump()` path and signal handling        |

## One-Minute Rule

If you have been grepping for more than a minute, stop and write down
what you actually know so far. Open the relevant header under
`/usr/src/sys/sys/` or the subsystem directory and re-read the
comment at the top of the file. That comment usually points to the
canonical implementation.
