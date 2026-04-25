# FreeBSD Kernel API Cheatsheet

A one-page companion to Appendix A. Groups the APIs that a driver
reaches for most often, with just enough detail to scan quickly.

## Memory Allocation

| API               | When to use                               | Header                |
| :---------------- | :---------------------------------------- | :-------------------- |
| `malloc(9)`       | General-purpose byte buffers              | `sys/malloc.h`        |
| `uma(9)`          | High-frequency fixed-size objects         | `vm/uma.h`            |
| `contigmalloc(9)` | Physically contiguous DMA buffer          | `sys/malloc.h`        |

Allocation flags: `M_WAITOK` (may sleep), `M_NOWAIT` (must check NULL),
`M_ZERO` (zero on allocation), `M_NODUMP` (exclude from crash dump).

## Synchronisation

| API              | Problem it solves                                   |
| :--------------- | :-------------------------------------------------- |
| `mtx(9)` default | Short, non-sleeping critical section                |
| `mtx(9)` spin    | Interrupt filter context, no sleeping allowed       |
| `sx(9)`          | Reader-writer lock, critical section may sleep      |
| `rmlock(9)`      | Many readers, rare writers, readers do not sleep    |
| `cv(9)`          | Named wait channel; use with a mutex                |
| `sema(9)`        | Counting semaphore for producer-consumer pools      |
| `atomic(9)`      | Single-word read-modify-write                       |
| `epoch(9)`       | Lock-free reader path for read-mostly data          |

Discipline reminders:
- Never sleep while holding a non-sleepable mutex.
- Pair every `*_init` with a `*_destroy`.
- Document lock order at the top of the file.
- `WITNESS` warnings are bugs.

## Deferred Execution and Timers

| API            | Purpose                                              |
| :------------- | :--------------------------------------------------- |
| `callout(9)`   | Schedule a function to fire after a delay           |
| `taskqueue(9)` | Defer sleepable work to a worker thread             |
| `kthread(9)`   | Dedicated kernel thread (heavyweight, rarely used)  |

Rule of thumb: callout for timers, taskqueue for deferred work that may
sleep, kthread only for genuinely long-running loops.

## Bus and Resource Management

| API             | Purpose                                                 |
| :-------------- | :------------------------------------------------------ |
| Newbus macros   | Register the driver, methods, and module metadata      |
| `rman(9)`       | Allocate and release resources (MMIO, ports, IRQ)      |
| `bus_space(9)`  | Portable MMIO and port register access                 |
| `bus_dma(9)`    | Portable DMA with tags, maps, and sync operations      |
| `bus_setup_intr`| Attach an interrupt filter or handler                  |

Resource rule: free in reverse order of allocation. Tear down interrupts
before releasing the IRQ resource.

## Device Nodes and I/O

| API                     | Purpose                                         |
| :---------------------- | :---------------------------------------------- |
| `make_dev_s(9)`         | Create a `/dev/` node                           |
| `destroy_dev(9)`        | Destroy a `/dev/` node                          |
| `struct cdevsw`         | Declare character-device entry points          |
| `d_ioctl`               | Out-of-band commands from userland              |
| `devfs_set_cdevpriv(9)` | Attach per-open state to a descriptor          |

Entry-point signatures live in `sys/conf.h`. Always set `d_version =
D_VERSION`.

## User-Space Interaction

| API                 | Purpose                                            |
| :------------------ | :------------------------------------------------- |
| `copyin(9)`         | Copy a user buffer into kernel memory              |
| `copyout(9)`        | Copy a kernel buffer out to user memory            |
| `copyinstr(9)`      | Copy a null-terminated user string                 |
| `uiomove(9)`        | Move bytes through a `struct uio`                  |
| `priv_check(9)`     | Check a privilege credential                       |

All of these may sleep. Do not call them under a non-sleepable lock.

## Observability and Notification

| API              | Purpose                                               |
| :--------------- | :---------------------------------------------------- |
| `sysctl(9)`      | Publish configurable or observable state              |
| `eventhandler(9)`| Subscribe to kernel-wide events                       |
| `d_poll`         | Support `poll(2)` waiting                             |
| `d_kqfilter`     | Support `kqueue(2)` waiting                           |
| `selrecord(9)`   | Register a waiter in a `struct selinfo`               |

`poll(2)` and `kqueue(2)` both depend on correct `selwakeup` and `knote`
invocations. Keep them outside mutex-held regions that could cause
lock-order inversions.

## Diagnostics

| API                | Purpose                                            |
| :----------------- | :------------------------------------------------- |
| `device_printf(9)` | Log a message with device name prefix              |
| `log(9)`           | Write a prioritised message to the kernel log      |
| `KASSERT(9)`       | Enforce an invariant when `INVARIANTS` is set      |
| `WITNESS`          | Lock-order verifier (kernel option)                |
| `ktr(9)`           | Ring-buffer trace events (`CTR0`..`CTR6`)          |
| `SDT(9)`           | Static DTrace probe points                         |

## Quick Reminders

- Always tag memory allocations with a `malloc_type`.
- Prefer `make_dev_s` to `make_dev`.
- Prefer `taskqueue(9)` to `kthread(9)` unless you need a persistent thread.
- The manual pages in section 9 are the authoritative reference.
- `dmesg`, `vmstat -m`, `sysctl -a`, and `dtrace(1)` are your friends during
  debugging.
