---
title: "FreeBSD Kernel API Reference"
description: "A practical, lookup-oriented reference for the FreeBSD kernel APIs, macros, data structures, and manpage families used throughout the book's driver-development chapters."
appendix: "A"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 45
---

# Appendix A: FreeBSD Kernel API Reference

## How to Use This Appendix

This appendix is the companion lookup table for everything the book has taught you to use inside a FreeBSD driver. The main chapters build each API carefully, show it in a working driver, and explain the mental model behind it. This appendix is the short, scannable counterpart you keep open while coding, debugging, or reading someone else's driver.

It is deliberately written to be a reference and not a tutorial. It does not try to teach any subsystem from scratch. Each entry assumes you have already met the API somewhere in the book, or that you are willing to read the manual page before you use it. What the entry gives you is the vocabulary to navigate: what the API is for, the small set of names that actually matter, the mistakes you are likely to make, where it typically fits in the driver lifecycle, and which chapter teaches it in full. If the entry is doing its job, you can answer four questions in under a minute:

1. Which API family do I need for the problem in front of me?
2. What is the exact name of the function, macro, or type I want?
3. What caveat should I check before I trust it?
4. Which manual page or chapter should I open next?

Nothing more is promised. Every detail below was verified against the FreeBSD 14.3 source tree and the corresponding manual pages in `man 9`. Where a distinction is important but deferred to another part of the book, the entry points forward rather than pretending to resolve it here.

### How the Entries Are Organised

The appendix groups APIs by the problem they solve, not alphabetically. A driver rarely reaches for a name in isolation. It reaches for a whole family: memory with its flags, a lock with its sleep rules, a callout with its cancellation story. Keeping those families together makes the appendix more useful for real lookup tasks.

Within each family, every entry follows the same short pattern:

- **Purpose.** What the API is for, in one or two sentences.
- **Typical use in drivers.** When a driver reaches for it.
- **Key names.** The functions, macros, flags, or types you actually call or declare.
- **Header(s).** Where the declarations live.
- **Caveats.** The handful of mistakes that cause real bugs.
- **Lifecycle phase.** Where in probe, attach, normal operation, or detach the API usually appears.
- **Manual pages.** The `man 9` entries to read next.
- **Where the book teaches this.** Chapter references for full context.

Keep this pattern in mind when you scan. If you only need the name of a flag, look at **Key names**. If you only need the manual page, look at **Manual pages**. If you have forgotten why the API exists at all, read **Purpose** and stop there.

### What This Appendix Is Not

This appendix is not a replacement for `man 9`, not a replacement for the book's teaching chapters, and not a replacement for reading real drivers under `/usr/src/sys/dev/`. It is short on purpose. The canonical reference remains the manual page; the canonical mental model remains the chapter that introduced the API; the canonical truth remains the source tree. This appendix helps you find all three quickly.

It also does not cover every kernel interface. The kernel is large, and a full reference would repeat material that belongs in Appendix E (FreeBSD Internals and Kernel Reference) or in focused chapters such as Chapter 16 (Accessing Hardware), Chapter 19 (Handling Interrupts), and Chapter 20 (Advanced Interrupt Handling). The goal here is coverage of the APIs that a driver author actually uses in day-to-day work, at the level of detail a driver author actually needs.

## Reader Guidance

You can use this appendix in three different ways, and they each call for a different reading strategy.

If you are **writing new code**, treat it as a checklist. Pick the family that matches your problem, skim the entries, note the key names, and jump to the manual page or the chapter for the details. Time investment: one or two minutes per lookup.

If you are **debugging**, treat it as a map of assumptions. When a driver misbehaves, the bug is almost always in a caveat that the author overlooked: a mutex held across a sleepable copy, a callout stopped but not drained, a resource freed before the interrupt was torn down. The **Caveats** line of each entry is where those assumptions live. Read them in order and ask whether your driver honours each one.

If you are **reading an unfamiliar driver**, treat it as a translator. When you see a function or macro you do not recognise, find its family in this appendix, read the **Purpose**, and move on. The full understanding can come later from the chapter or the manual page. The goal during exploration is to keep moving and to form an initial mental model of what the driver is doing.

A few conventions used throughout:

- All source paths are shown in book-facing form, `/usr/src/sys/...`, matching the layout on a standard FreeBSD system.
- Manual pages are cited in the usual FreeBSD style: `mtx(9)` means section 9 of the manual. You can read any of them with, for example, `man 9 mtx`.
- When a family has no dedicated manual page, the entry says so and points to the closest available documentation.
- When the book defers a topic to a later chapter or to Appendix E, the entry points forward rather than fabricating detail here.

With that in mind, we can open the appendix proper. The first family is memory: where drivers get the bytes they need, how they give them back, and which flags control the behaviour along the way.

## Memory Allocation

Every driver allocates memory, and every allocation carries rules about when you may block, where the memory is physically located, and how it is returned. The kernel provides three main allocators: `malloc(9)` for general-purpose allocation, `uma(9)` for high-frequency fixed-size objects, and `contigmalloc(9)` for physically contiguous ranges that hardware can address. Below you will find each one at a glance, plus the small vocabulary of flags they share.

### `malloc(9)` / `free(9)` / `realloc(9)`

**Purpose.** General-purpose kernel memory allocator. Gives you a byte buffer of any size, tagged by a `malloc_type` so you can account for it later with `vmstat -m`.

**Typical use in drivers.** Softc allocation, small variable-size buffers, temporary scratch space, anything where a fixed-size zone would be overkill.

**Key names.**

- `void *malloc(size_t size, struct malloc_type *type, int flags);`
- `void free(void *addr, struct malloc_type *type);`
- `void *realloc(void *addr, size_t size, struct malloc_type *type, int flags);`
- `MALLOC_DEFINE(M_FOO, "foo", "description for vmstat -m");`
- `MALLOC_DECLARE(M_FOO);` for use in headers.

**Header.** `/usr/src/sys/sys/malloc.h`.

**Flags that matter.**

- `M_WAITOK`: the caller may block until memory is available. The allocation will succeed or the kernel will panic.
- `M_NOWAIT`: the caller must not block. The allocation may return `NULL`. Always check for `NULL` with `M_NOWAIT`.
- `M_ZERO`: zero the returned memory before handing it back. Combine with either wait flag.
- `M_NODUMP`: exclude the allocation from crash dumps.

**Caveats.**

- `M_WAITOK` must not be used while holding a spin mutex, in an interrupt filter, or in any context that cannot sleep.
- `M_NOWAIT` callers must check the return value. Failing to handle `NULL` is one of the most common driver crashes in review.
- Never mix allocator families. Memory returned by `malloc(9)` must be freed by `free(9)`; `uma_zfree(9)` and `contigfree(9)` are not interchangeable.
- The `struct malloc_type` pointer must match between `malloc` and the corresponding `free`.

**Lifecycle phase.** Most commonly in `attach` (softc, buffers) and `detach` (release). Smaller allocations can appear in normal I/O paths as long as the context permits the chosen flag.

**Manual page.** `malloc(9)`.

**Where the book teaches this.** Introduced in Chapter 5 alongside kernel-specific C idioms; used in Chapter 7 when your first driver allocates its softc; revisited in Chapter 10 when I/O buffers become real, and again in Chapter 11 when allocation flags must obey locking rules.

### `uma(9)` Zones

**Purpose.** Fixed-size object cache, highly optimised for allocations that are frequent, uniform, and performance-sensitive. Reuses objects instead of repeatedly hitting the general allocator.

**Typical use in drivers.** Network mbuf-like structures, per-packet state, per-request descriptors, anything where you allocate and free millions of identical small objects per second.

**Key names.**

- `uma_zone_t uma_zcreate(const char *name, size_t size, uma_ctor, uma_dtor, uma_init, uma_fini, int align, uint32_t flags);`
- `void uma_zdestroy(uma_zone_t zone);`
- `void *uma_zalloc(uma_zone_t zone, int flags);`
- `void uma_zfree(uma_zone_t zone, void *item);`

**Header.** `/usr/src/sys/vm/uma.h`.

**Flags.**

- `M_WAITOK`, `M_NOWAIT`, `M_ZERO` on allocation, identical in meaning to `malloc(9)`.
- Creation-time flags such as `UMA_ZONE_ZINIT`, `UMA_ZONE_NOFREE`, `UMA_ZONE_CONTIG`, and alignment hints (`UMA_ALIGN_CACHE`, `UMA_ALIGN_PTR`, and so on) tune behaviour for specific workloads.

**Caveats.**

- Zones must be created before they can be used and must be destroyed before the module unloads. Forgetting `uma_zdestroy` in `detach` leaks the entire zone.
- Constructors and destructors run on allocation and free respectively, not on zone creation and destruction; use the `init` and `fini` callbacks for once-per-slab work.
- A zone is expensive to create. Create one per object type per module, not per instance.
- There is no dedicated `uma(9)` manual page. The authoritative reference is the header and the existing users under `/usr/src/sys/`.

**Lifecycle phase.** `uma_zcreate` in module load or early attach; `uma_zalloc` and `uma_zfree` in I/O paths; `uma_zdestroy` in module unload.

**Manual page.** None dedicated. Read `/usr/src/sys/vm/uma.h` and look at `/usr/src/sys/kern/kern_mbuf.c` and `/usr/src/sys/net/netisr.c` for realistic use.

**Where the book teaches this.** Mentioned briefly in Chapter 7 as an alternative to `malloc(9)`; revisited when high-rate drivers need it in Chapter 28 (networking) and Chapter 33 (performance tuning).

### `contigmalloc(9)` / `contigfree(9)`

**Purpose.** Allocate a physically contiguous range of memory within a specified address window. Required when hardware must DMA into memory without an IOMMU and therefore needs contiguous physical pages.

**Typical use in drivers.** DMA buffers for devices that cannot scatter-gather, and only after confirming that `bus_dma(9)` is not a better fit.

**Key names.**

- `void *contigmalloc(unsigned long size, struct malloc_type *type, int flags, vm_paddr_t low, vm_paddr_t high, unsigned long alignment, vm_paddr_t boundary);`
- `void contigfree(void *addr, unsigned long size, struct malloc_type *type);`

**Header.** `/usr/src/sys/sys/malloc.h`.

**Caveats.**

- Fragmentation after boot makes large contiguous allocations fail. Do not assume success.
- For almost all modern hardware, prefer the `bus_dma(9)` framework. It handles tags, maps, bouncing, and alignment in a portable way.
- `contigmalloc` allocations are a scarce system resource; free them as soon as possible.

**Lifecycle phase.** Typically in `attach`; freed in `detach`.

**Manual page.** `contigmalloc(9)`.

**Where the book teaches this.** Mentioned alongside `bus_dma(9)` in Chapter 21 when DMA first becomes a real concern.

### Allocation Flag Cheat Sheet

| Flag         | Meaning                                                |
| :----------- | :----------------------------------------------------- |
| `M_WAITOK`   | Caller may block until memory is available.           |
| `M_NOWAIT`   | Caller must not block; returns `NULL` on failure.     |
| `M_ZERO`     | Zero the allocation before returning.                  |
| `M_NODUMP`   | Exclude the allocation from crash dumps.              |

Use `M_WAITOK` only where sleeping is permitted. When in doubt, the safe answer is `M_NOWAIT` plus a `NULL` check.

## Synchronisation Primitives

If memory is the raw material of a driver, synchronisation is the discipline that prevents two execution contexts from corrupting it at the same time. FreeBSD gives you a small, well-designed toolkit. The names below are the ones you will meet most often. Full teaching lives in Chapters 11, 12, and 15, with interrupt-context nuances in Chapters 19 and 20; this appendix collects the vocabulary.

### `mtx(9)`: Mutexes

**Purpose.** The default kernel mutual-exclusion primitive. A thread acquires the lock, enters the critical section, and releases the lock.

**Typical use in drivers.** Protecting softc fields, circular buffers, reference counts, and any shared state whose critical section is short and does not sleep.

**Key names.**

- `void mtx_init(struct mtx *m, const char *name, const char *type, int opts);`
- `void mtx_destroy(struct mtx *m);`
- `mtx_lock(m)`, `mtx_unlock(m)`, `mtx_trylock(m)`.
- `mtx_assert(m, MA_OWNED | MA_NOTOWNED | MA_RECURSED | MA_NOTRECURSED);` for invariants.
- Sleep-on-mutex helpers: `msleep(9)` and `mtx_sleep(9)`.

**Header.** `/usr/src/sys/sys/mutex.h`.

**Options.**

- `MTX_DEF`: the default sleepable-on-contention mutex. Use this for almost everything.
- `MTX_SPIN`: pure spinlock. Required for interrupt-filter context and other places where blocking is impossible. Rules are stricter.
- `MTX_RECURSE`: allow the same thread to acquire the lock multiple times. Use with caution; it often hides design mistakes.
- `MTX_NEW`: force `mtx_init` to treat the lock as newly created. Useful with `WITNESS`.

**Caveats.**

- Never sleep while holding an `MTX_DEF` or `MTX_SPIN` mutex. `uiomove(9)`, `copyin(9)`, `copyout(9)`, `malloc(9, M_WAITOK)`, and most bus primitives can sleep. Audit carefully.
- Always pair `mtx_init` with `mtx_destroy`. Forgetting the destroy leaks internal state and annoys `WITNESS`.
- Lock order matters. Once the kernel has seen you acquire lock A before lock B, it will warn if you ever reverse the pair. Plan your lock hierarchy in advance.
- `MTX_SPIN` disables preemption; hold it for as short a time as possible.

**Lifecycle phase.** `mtx_init` in `attach`; `mtx_destroy` in `detach`. Lock and unlock operations everywhere in between.

**Manual pages.** `mutex(9)`, `mtx_pool(9)`, `msleep(9)`.

**Where the book teaches this.** First treatment in Chapter 11, deepened in Chapter 12 with lock-order and `WITNESS` discipline, and revisited in Chapter 19 for interrupt-safe variants (`MTX_SPIN`).

### `sx(9)`: Sleepable Shared-Exclusive Locks

**Purpose.** A reader-writer lock where either the reader or the writer may block. Use when multiple readers are common, writers are rare, and the critical section may sleep.

**Typical use in drivers.** Configuration state read by many paths and modified infrequently. Not for fast-path data.

**Key names.**

- `void sx_init(struct sx *sx, const char *desc);`
- `void sx_destroy(struct sx *sx);`
- `sx_slock(sx)`, `sx_sunlock(sx)` for shared access.
- `sx_xlock(sx)`, `sx_xunlock(sx)` for exclusive access.
- `sx_try_slock`, `sx_try_xlock`, `sx_upgrade`, `sx_downgrade`.
- `sx_assert(sx, SA_SLOCKED | SA_XLOCKED | SA_LOCKED | SA_UNLOCKED);`

**Header.** `/usr/src/sys/sys/sx.h`.

**Caveats.**

- `sx` allows sleeping inside the critical section, unlike `mtx`. That flexibility is the whole point; make sure you actually need it.
- `sx` locks are more expensive than mutexes. Do not use them as a default.
- Avoid mixing `sx` and `mtx` in the same lock order without thinking through the implications.

**Lifecycle phase.** `sx_init` in `attach`; `sx_destroy` in `detach`.

**Manual page.** `sx(9)`.

**Where the book teaches this.** Chapter 12.

### `rmlock(9)`: Read-Mostly Locks

**Purpose.** Extremely fast reader path, slower writer path. Readers do not contend with each other. Designed for data that is read on every operation but written only rarely.

**Typical use in drivers.** Routing-like tables, configuration state used in fast paths, structures where writer overhead is acceptable because writes are rare.

**Key names.**

- `void rm_init(struct rmlock *rm, const char *name);`
- `void rm_destroy(struct rmlock *rm);`
- `rm_rlock(rm, tracker)`, `rm_runlock(rm, tracker)`.
- `rm_wlock(rm)`, `rm_wunlock(rm)`.

**Header.** `/usr/src/sys/sys/rmlock.h`.

**Caveats.**

- Each reader needs its own `struct rm_priotracker`, typically on the stack. Do not share one.
- Readers may not sleep unless the lock was initialised with `RM_SLEEPABLE`.
- The writer path is heavy; if writes are frequent, `sx` or `mtx` is a better choice.

**Lifecycle phase.** `rm_init` in `attach`; `rm_destroy` in `detach`.

**Manual page.** `rmlock(9)`.

**Where the book teaches this.** Introduced briefly in Chapter 12 and used in later chapters where read-mostly patterns arise.

### `cv(9)` / `condvar(9)`: Condition Variables

**Purpose.** A named wait channel. One or more threads sleep until another thread signals that the condition they are waiting for has become true.

**Typical use in drivers.** Waiting for a buffer to drain, for hardware to finish a command, or for a specific state transition. Use instead of bare `wakeup(9)` channels when you want the wait reason to be explicit.

**Key names.**

- `void cv_init(struct cv *cv, const char *desc);`
- `void cv_destroy(struct cv *cv);`
- `cv_wait(cv, mtx)`, `cv_wait_sig(cv, mtx)`, `cv_wait_unlock(cv, mtx)`.
- `cv_timedwait(cv, mtx, timo)`, `cv_timedwait_sig(cv, mtx, timo)`.
- `cv_signal(cv)`, `cv_broadcast(cv)`, `cv_broadcastpri(cv, pri)`.

**Header.** `/usr/src/sys/sys/condvar.h`.

**Caveats.**

- The mutex passed to `cv_wait` must be held by the caller; `cv_wait` drops it while sleeping and reacquires it on return.
- Always recheck the predicate after `cv_wait` returns. Spurious wakeups and signals are possible.
- `cv_signal` wakes one waiter; `cv_broadcast` wakes all. Pick based on the design, not on instinct.

**Lifecycle phase.** `cv_init` in `attach`; `cv_destroy` in `detach`.

**Manual page.** `condvar(9)`.

**Where the book teaches this.** Chapter 12, with interruptible and timed waits revisited in Chapter 15.

### `sema(9)`: Counting Semaphores

**Purpose.** A counting semaphore with `wait` and `post` operations. Less common than mutexes or condition variables.

**Typical use in drivers.** Producer-consumer patterns where a counted resource must be tracked, such as a fixed pool of command slots.

**Key names.**

- `void sema_init(struct sema *sema, int value, const char *desc);`
- `void sema_destroy(struct sema *sema);`
- `sema_wait(sema)`, `sema_trywait(sema)`, `sema_timedwait(sema, timo)`.
- `sema_post(sema)`.

**Header.** `/usr/src/sys/sys/sema.h`.

**Caveats.**

- Semaphores are appropriate for counting. For one-thread-in-critical-section patterns, use `mtx` instead.
- `sema_wait` may return early on signal; check return values.

**Manual page.** `sema(9)`.

**Where the book teaches this.** Chapter 15, as part of the advanced synchronisation toolkit.

### `atomic(9)`: Atomic Operations

**Purpose.** Single-word, interruption-free read-modify-write operations. Faster than any lock, and strictly limited in expressiveness.

**Typical use in drivers.** Counters, flags, and compare-and-swap patterns where the critical section fits in one integer.

**Key names.**

- `atomic_add_int`, `atomic_subtract_int`, `atomic_set_int`, `atomic_clear_int`.
- `atomic_load_int`, `atomic_store_int`, with acquire and release variants.
- `atomic_cmpset_int`, `atomic_fcmpset_int` for compare-and-swap.
- Width variants: `_8`, `_16`, `_32`, `_64`, and pointer-sized `_ptr`.
- Barrier helpers: `atomic_thread_fence_acq()`, `atomic_thread_fence_rel()`, `atomic_thread_fence_acq_rel()`.

**Header.** `/usr/src/sys/sys/atomic_common.h` plus `machine/atomic.h` for architecture-specific pieces.

**Caveats.**

- Atomic operations give you one word of mutual exclusion. Any invariant that spans two fields still needs a lock.
- Memory ordering matters. The plain operations are relaxed; use the `_acq`, `_rel`, and `_acq_rel` variants when one access must become visible before or after another.
- For per-CPU counters that are read rarely, `counter(9)` scales better.

**Lifecycle phase.** Any. Cheap enough to use in interrupt filters.

**Manual page.** `atomic(9)`.

**Where the book teaches this.** Chapter 11, with `counter(9)` introduced alongside for per-CPU patterns.

### `epoch(9)`: Read-Mostly Lock-Free Sections

**Purpose.** Lightweight reader protection for data structures where readers vastly outnumber writers and latency must be minimal. Writers wait for all current readers to leave before freeing memory.

**Typical use in drivers.** Network stack fast paths, read-mostly lookup tables in high-performance drivers. Not a general-purpose primitive.

**Key names.**

- `epoch_t epoch_alloc(const char *name, int flags);`
- `void epoch_free(epoch_t epoch);`
- `epoch_enter(epoch)`, `epoch_exit(epoch)`.
- `epoch_wait(epoch)` for writers to block until readers drain.
- `NET_EPOCH_ENTER(et)` and `NET_EPOCH_EXIT(et)` wrappers for the network stack.

**Header.** `/usr/src/sys/sys/epoch.h`.

**Caveats.**

- Readers must not block, sleep, or call any function that does, while inside an epoch section.
- Freeing protected memory must be deferred until `epoch_wait` returns.
- Epoch sections are a tool of last resort, not a default primitive. Choose locks first.

**Manual page.** `epoch(9)`.

**Where the book teaches this.** Introduced briefly in Chapter 12; used in depth only where real drivers in later chapters require it.

### Lock Decision Cheat Sheet

| You want to...                                        | Reach for                  |
| :---------------------------------------------------- | :------------------------- |
| Protect a short, non-sleeping critical section        | `mtx(9)` with `MTX_DEF`    |
| Protect state in an interrupt filter                  | `mtx(9)` with `MTX_SPIN`   |
| Allow many readers, rare writers, may sleep          | `sx(9)`                    |
| Allow many readers, rare writers, no sleep in readers | `rmlock(9)`                |
| Sleep until a named condition holds                   | `cv(9)` with a mutex       |
| Increment or compare-and-swap a single word          | `atomic(9)`                |
| Lock-free reader path for read-mostly data           | `epoch(9)`                 |

When a row does not obviously match the problem, the full discussion in Chapters 11, 12, and 15 is the place to resolve it.

## Deferred Execution and Timers

Drivers often need to run work later, periodically, or from a context that can sleep. The kernel gives you three tools for that: `callout(9)` for one-shot and periodic timers, `taskqueue(9)` for deferred work that may sleep, and `kthread(9)` or `kproc(9)` for long-running background threads. They overlap in some situations; the rule of thumb is that callouts run from the timer interrupt context (fast, no sleeping), taskqueues run in a worker thread (can sleep, can grab sleepable locks), and kthreads are whole threads you own.

### `callout(9)`: Kernel Timers

**Purpose.** Schedule a function to run after a time delay. The callback runs in soft-interrupt context by default and must not sleep.

**Typical use in drivers.** Watchdog timers, polling intervals, retry delays, idle timeouts.

**Key names.**

- `void callout_init(struct callout *c, int mpsafe);` plus `callout_init_mtx` and `callout_init_rm`.
- `int callout_reset(struct callout *c, int ticks, void (*func)(void *), void *arg);`
- `int callout_stop(struct callout *c);`
- `int callout_drain(struct callout *c);`
- `int callout_pending(struct callout *c);`, `callout_active(struct callout *c);`

**Header.** `/usr/src/sys/sys/callout.h`.

**Caveats.**

- `callout_stop` does not wait for a running callback. Use `callout_drain` before freeing the softc in `detach`.
- A callout can fire even after you think you cancelled it if the timer was already dispatched. Guard the callback with a flag or use the `_mtx` and `_rm` variants to integrate cancellation with your lock.
- Running tickless kernels means ticks are abstract. Convert real time with `hz` or use `callout_reset_sbt` for subsecond precision.

**Lifecycle phase.** `callout_init` in `attach`; `callout_drain` in `detach`; `callout_reset` whenever the next firing time needs to be set.

**Manual page.** `callout(9)`.

**Where the book teaches this.** Chapter 13.

### `taskqueue(9)`: Deferred Work in a Worker Thread

**Purpose.** Hand off work from a context that cannot sleep, or that should not hold a lock for long, to a worker thread. Tasks queued on the same taskqueue run in order.

**Typical use in drivers.** Interrupt post-processing, hardware command completion handlers, reset and recovery paths that may need to allocate memory or grab sleepable locks.

**Key names.**

- `struct taskqueue *taskqueue_create(const char *name, int mflags, taskqueue_enqueue_fn, void *context);`
- `void taskqueue_free(struct taskqueue *queue);`
- `TASK_INIT(struct task *t, int priority, task_fn_t *func, void *context);`
- `int taskqueue_enqueue(struct taskqueue *queue, struct task *task);`
- `void taskqueue_drain(struct taskqueue *queue, struct task *task);`
- `void taskqueue_drain_all(struct taskqueue *queue);`
- Global queues such as `taskqueue_thread`, `taskqueue_swi`, `taskqueue_fast`.

**Header.** `/usr/src/sys/sys/taskqueue.h`.

**Caveats.**

- Enqueuing the same task twice before it runs is a no-op by design. If you need a new request every time, that is fine; if you expect two runs, use distinct tasks.
- `taskqueue_drain` waits for the task to finish; call it before freeing anything the task uses.
- Private taskqueues are cheap but not free. Reuse global taskqueues (`taskqueue_thread`, `taskqueue_fast`) unless you have a reason to own one.

**Lifecycle phase.** `taskqueue_create` (if private) and `TASK_INIT` in `attach`; `taskqueue_drain` and `taskqueue_free` in `detach`.

**Manual page.** `taskqueue(9)`.

**Where the book teaches this.** Chapter 14.

### `kthread(9)` and `kproc(9)`: Kernel Threads and Processes

**Purpose.** Create a dedicated kernel thread or process that runs your function. Useful when the workload is long-lived, needs its own scheduling policy, or needs to be explicitly addressable.

**Typical use in drivers.** Rare. Most driver work is served better by a taskqueue or a callout. Kernel threads appear in subsystems with genuine long-running loops, such as housekeeping daemons.

**Key names.**

- `int kthread_add(void (*func)(void *), void *arg, struct proc *p, struct thread **td, int flags, int pages, const char *fmt, ...);`
- `int kproc_create(void (*func)(void *), void *arg, struct proc **procp, int flags, int pages, const char *fmt, ...);`
- `void kthread_exit(void);`
- `kproc_exit`, `kproc_suspend_check`.

**Header.** `/usr/src/sys/sys/kthread.h`.

**Caveats.**

- Creating a thread is heavier than queuing a task. Prefer `taskqueue(9)` unless the workload is genuinely long-running.
- Cleanly shutting down a kthread requires cooperation: set a stop flag, wake the thread, and wait for it to exit. Forgetting any step leaks the thread across module unloads.
- A kthread must exit by calling `kthread_exit`, not by returning.

**Manual pages.** `kthread(9)`, `kproc(9)`.

**Where the book teaches this.** Mentioned in Chapter 14 as the heavier alternative to taskqueues.

### Deferred Work Decision Cheat Sheet

| You need to...                                                   | Reach for         |
| :--------------------------------------------------------------- | :---------------- |
| Fire a function after a delay, briefly, no sleeping              | `callout(9)`      |
| Defer work that may sleep or grab sleepable locks               | `taskqueue(9)`    |
| Run a persistent background loop                                 | `kthread(9)`      |
| Convert short periodic polling into real interrupts             | See Chapter 19    |

## Bus and Resource Management

The bus layer is where a driver meets hardware. Newbus introduces the driver to the kernel; `rman(9)` hands out the resources that represent MMIO regions, I/O ports, and interrupts; `bus_space(9)` accesses them portably; `bus_dma(9)` lets devices DMA safely.

### Newbus: `DRIVER_MODULE`, `DEVMETHOD`, and Related Macros

**Purpose.** Register a driver with the kernel, bind it to a device class, declare the entry points the kernel should call, and publish version and dependency information.

**Typical use in drivers.** Every kernel module that owns a device. This is the scaffolding that turns a pile of C code into something `kldload` can attach to hardware.

**Key names.**

- `DRIVER_MODULE(name, bus, driver, devclass, evh, evharg);`
- `MODULE_VERSION(name, version);`
- `MODULE_DEPEND(name, busname, vmin, vpref, vmax);`
- `DEVMETHOD(method, function)` and `DEVMETHOD_END` for the method table.
- `device_method_t` entries such as `device_probe`, `device_attach`, `device_detach`, `device_shutdown`, `device_suspend`, `device_resume`.
- Types: `device_t`, `devclass_t`, `driver_t`.

**Header.** `/usr/src/sys/sys/module.h` and `/usr/src/sys/sys/bus.h`.

**Caveats.**

- `DRIVER_MODULE` expands into a module event handler; do not declare your own `module_event_t` table by hand unless you know exactly why.
- `MODULE_DEPEND` is how you make the loader bring in your prerequisites. Forgetting it produces ugly symbol-resolution failures at load time.
- `DEVMETHOD_END` terminates the method table. Without it the kernel will walk past the end.
- `device_t` is opaque; use accessors such as `device_get_softc`, `device_get_parent`, `device_get_name`, and `device_printf`.

**Lifecycle phase.** Declaration only. The macros expand into module-init and module-fini glue that is run on `kldload` and `kldunload`.

**Manual pages.** `DRIVER_MODULE(9)`, `MODULE_VERSION(9)`, `MODULE_DEPEND(9)`, `module(9)`, `DEVICE_PROBE(9)`, `DEVICE_ATTACH(9)`, `DEVICE_DETACH(9)`.

**Where the book teaches this.** Full treatment in Chapter 7, with the anatomy first sketched in Chapter 6.

### `devclass(9)` and Device Accessors

**Purpose.** A `devclass_t` groups instances of the same driver so the kernel can find them, number them, and iterate over them. In drivers, you mostly use the accessors, not the devclass directly.

**Key names.**

- `device_t device_get_parent(device_t dev);`
- `void *device_get_softc(device_t dev);`
- `int device_get_unit(device_t dev);`
- `const char *device_get_nameunit(device_t dev);`
- `int device_printf(device_t dev, const char *fmt, ...);`
- `devclass_find`, `devclass_get_device`, `devclass_get_devices`, `devclass_get_count` when you truly need to walk a class.

**Header.** `/usr/src/sys/sys/bus.h`.

**Caveats.**

- `device_get_softc` assumes the softc was registered through the driver structure. Rolling your own `device_t`-to-state mapping is almost always wrong.
- Direct devclass manipulation is rare in drivers. If you find yourself reaching for it, check whether the question belongs in a bus-level interface instead.

**Manual pages.** `devclass(9)`, `device(9)`, `device_get_softc(9)`, `device_printf(9)`.

**Where the book teaches this.** Chapter 6 and Chapter 7.

### `rman(9)`: Resource Manager

**Purpose.** A uniform view over MMIO regions, I/O ports, interrupt numbers, and DMA channels. Your driver asks for resources by type and RID and gets back a `struct resource *` with useful accessors.

**Key names.**

- `struct resource *bus_alloc_resource(device_t dev, int type, int *rid, rman_res_t start, rman_res_t end, rman_res_t count, u_int flags);`
- `struct resource *bus_alloc_resource_any(device_t dev, int type, int *rid, u_int flags);`
- `int bus_release_resource(device_t dev, int type, int rid, struct resource *r);`
- `int bus_activate_resource(device_t dev, int type, int rid, struct resource *r);`
- `int bus_deactivate_resource(device_t dev, int type, int rid, struct resource *r);`
- `rman_res_t rman_get_start(struct resource *r);`, `rman_get_end`, `rman_get_size`.
- `bus_space_tag_t rman_get_bustag(struct resource *r);`, `rman_get_bushandle`.
- Resource types: `SYS_RES_MEMORY`, `SYS_RES_IOPORT`, `SYS_RES_IRQ`, `SYS_RES_DRQ`.
- Flags: `RF_ACTIVE`, `RF_SHAREABLE`.

**Header.** `/usr/src/sys/sys/rman.h`.

**Caveats.**

- The `rid` parameter is a pointer and may be rewritten by the allocator. Pass the address of a real variable.
- Release every allocated resource in `detach` in reverse order of allocation. Leaking a resource almost always corrupts the next attach.
- `RF_ACTIVE` is the common case. Do not forget it, or you will get a handle that cannot be used with `bus_space(9)`.
- Always check the return value. A failed allocation is common on hardware with quirks.

**Lifecycle phase.** Allocation in `attach`; release in `detach`. If the driver has exotic needs, `bus_activate_resource` and `bus_deactivate_resource` can manage activation separately.

**Manual pages.** `rman(9)`, `bus_alloc_resource(9)`, `bus_release_resource(9)`, `bus_activate_resource(9)`.

**Where the book teaches this.** Chapter 16.

### `bus_space(9)`: Portable Register Access

**Purpose.** Read and write device registers through a `(tag, handle, offset)` triple that hides whether the underlying access is memory-mapped, port-based, big-endian, little-endian, or indexed.

**Typical use in drivers.** Every MMIO or I/O port access. Do not dereference `rman_get_virtual` yourself; use `bus_space`.

**Key names.**

- Types: `bus_space_tag_t`, `bus_space_handle_t`.
- Reads: `bus_space_read_1(tag, handle, offset)`, `_2`, `_4`, `_8`.
- Writes: `bus_space_write_1(tag, handle, offset, value)`, `_2`, `_4`, `_8`.
- Multi-register helpers: `bus_space_read_multi_N`, `bus_space_write_multi_N`, `bus_space_read_region_N`, `bus_space_write_region_N`.
- Barriers: `bus_space_barrier(tag, handle, offset, length, flags)` with `BUS_SPACE_BARRIER_READ` and `BUS_SPACE_BARRIER_WRITE`.

**Header.** `/usr/src/sys/sys/bus.h`, with machine-specific details in `machine/bus.h`.

**Caveats.**

- Never touch device registers through a raw pointer. Portability and debugging both depend on `bus_space`.
- Barriers are not automatic. When two writes must occur in order, insert `bus_space_barrier` between them.
- The width used in `bus_space_read_N` or `bus_space_write_N` must match the register's natural size. Mismatches cause silent corruption on some architectures.

**Lifecycle phase.** Any time the driver talks to the device.

**Manual page.** `bus_space(9)`.

**Where the book teaches this.** Chapter 16.

### `bus_dma(9)`: Portable DMA

**Purpose.** Describe DMA constraints with a tag, load a buffer through a map, and let the framework handle alignment, bouncing, and coherency. Required for any serious device that moves data.

**Key names.**

- `int bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment, bus_addr_t boundary, bus_addr_t lowaddr, bus_addr_t highaddr, bus_dma_filter_t *filtfunc, void *filtfuncarg, bus_size_t maxsize, int nsegments, bus_size_t maxsegsz, int flags, bus_dma_lock_t *lockfunc, void *lockfuncarg, bus_dma_tag_t *dmat);`
- `int bus_dma_tag_destroy(bus_dma_tag_t dmat);`
- `int bus_dmamap_create(bus_dma_tag_t dmat, int flags, bus_dmamap_t *mapp);`
- `int bus_dmamap_destroy(bus_dma_tag_t dmat, bus_dmamap_t map);`
- `int bus_dmamap_load(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf, bus_size_t buflen, bus_dmamap_callback_t *callback, void *arg, int flags);`
- `void bus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t map);`
- `void bus_dmamap_sync(bus_dma_tag_t dmat, bus_dmamap_t map, bus_dmasync_op_t op);`
- `int bus_dmamem_alloc(bus_dma_tag_t dmat, void **vaddr, int flags, bus_dmamap_t *mapp);`
- `void bus_dmamem_free(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map);`
- Flags: `BUS_DMA_WAITOK`, `BUS_DMA_NOWAIT`, `BUS_DMA_ALLOCNOW`, `BUS_DMA_COHERENT`, `BUS_DMA_ZERO`.
- Sync operations: `BUS_DMASYNC_PREREAD`, `BUS_DMASYNC_POSTREAD`, `BUS_DMASYNC_PREWRITE`, `BUS_DMASYNC_POSTWRITE`.

**Header.** `/usr/src/sys/sys/bus_dma.h`.

**Caveats.**

- Tags form a tree. Child tags inherit parent constraints; create them in the right order.
- `bus_dmamap_load` may complete asynchronously. Always use the callback, even for synchronous buffers.
- `bus_dmamap_sync` is not decoration. Without the right sync direction, caches and device memory will disagree.
- On platforms with IOMMUs the framework does the right thing. Do not skip it just because your development hardware is coherent.

**Lifecycle phase.** Tag creation and mapping setup in `attach`; load and sync in I/O paths; unload and destroy in `detach`.

**Manual page.** `bus_dma(9)`.

**Where the book teaches this.** Chapter 21.

### Interrupt Setup

**Purpose.** Attach a filter or handler to an IRQ resource so the kernel can deliver interrupts to the driver.

**Key names.**

- `int bus_setup_intr(device_t dev, struct resource *r, int flags, driver_filter_t *filter, driver_intr_t *handler, void *arg, void **cookiep);`
- `int bus_teardown_intr(device_t dev, struct resource *r, void *cookie);`
- Flags: `INTR_TYPE_NET`, `INTR_TYPE_BIO`, `INTR_TYPE_TTY`, `INTR_TYPE_MISC`, `INTR_MPSAFE`, `INTR_EXCL`.

**Caveats.**

- Provide a filter when the fast-path decision is cheap and the driver can honour filter-context restrictions (no sleeping, no sleepable locks). Provide a handler when the work needs a thread.
- `INTR_MPSAFE` is mandatory for new drivers. Without it, the kernel serialises the handler on the Giant lock, which is almost always wrong.
- Tear down before freeing the resource. The order is: `bus_teardown_intr`, then `bus_release_resource`.

**Lifecycle phase.** `bus_setup_intr` at the end of `attach`, after the rest of the softc is ready; `bus_teardown_intr` at the start of `detach`, before any resource is released.

**Manual page.** `BUS_SETUP_INTR(9)`, `bus_alloc_resource(9)` for the resource side.

**Where the book teaches this.** Chapter 19, with advanced patterns in Chapter 20.

## Device Nodes and Character-Device I/O

Once hardware is bound, drivers most often expose themselves to userland through a device node in `/dev/`. The APIs below build and tear down those nodes, and the associated switch table declares how the kernel should dispatch `read`, `write`, `ioctl`, and `poll`.

### `make_dev_s(9)` and `destroy_dev(9)`

**Purpose.** Create a new device node under `/dev/`, connected to a `cdevsw` that holds the function pointers the kernel will call.

**Key names.**

- `int make_dev_s(struct make_dev_args *args, struct cdev **cdev, const char *fmt, ...);`
- `void destroy_dev(struct cdev *cdev);`
- Fields in `struct make_dev_args`: `mda_si_drv1`, `mda_devsw`, `mda_uid`, `mda_gid`, `mda_mode`, `mda_flags`, `mda_unit`.
- Legacy helper `make_dev(struct cdevsw *, int unit, uid_t, gid_t, int mode, const char *fmt, ...)` still exists but `make_dev_s` is preferred in new code.

**Header.** `/usr/src/sys/sys/conf.h`.

**Caveats.**

- Always use `make_dev_s`. The older `make_dev` swallows errors and does not let you set all the arguments.
- Set `mda_si_drv1` to the softc so the cdev carries a pointer back to driver state without a separate lookup.
- `destroy_dev` waits for all active threads to leave the cdev before returning, making it safe to free the softc afterwards.

**Lifecycle phase.** `make_dev_s` at the end of `attach`; `destroy_dev` at the start of `detach`, before any backing state is torn down.

**Manual page.** `make_dev(9)`.

**Where the book teaches this.** Chapter 8.

### `cdevsw`: Character-Device Switch Table

**Purpose.** Declare the entry points the kernel should call when a process opens, reads, writes, or otherwise interacts with the device node.

**Key names.**

- `struct cdevsw` fields: `d_version`, `d_flags`, `d_name`, `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`, `d_poll`, `d_kqfilter`, `d_mmap`, `d_mmap_single`.
- `d_version` must be `D_VERSION`.
- Common flags: `D_NEEDGIANT` (legacy), `D_TRACKCLOSE`, `D_MEM`.

**Header.** `/usr/src/sys/sys/conf.h`.

**Caveats.**

- Always set `d_version = D_VERSION`. The kernel refuses to attach a switch table with a missing or stale version.
- `d_flags` default of zero is fine for modern MPSAFE drivers. Do not add `D_NEEDGIANT` unless you genuinely need it.
- Unused entries can be left as `NULL`; the kernel substitutes defaults. Do not point them at stubs that do nothing.

**Lifecycle phase.** Declared statically at module scope. Referenced by `struct make_dev_args`.

**Manual page.** `make_dev(9)` covers the structure in the context of `make_dev_s`.

**Where the book teaches this.** Chapter 8.

### `ioctl(9)` Dispatch

**Purpose.** Provide out-of-band commands to a device, addressed by a numeric command and an argument buffer.

**Key names.**

- Entry point: `d_ioctl_t` with signature `int (*)(struct cdev *, u_long cmd, caddr_t data, int fflag, struct thread *td);`
- Command encoding macros: `_IO`, `_IOR`, `_IOW`, `_IOWR`.
- Copy helpers: `copyin(9)` and `copyout(9)` for pointer-carrying ioctls.

**Caveats.**

- Use `_IOR`, `_IOW`, or `_IOWR` to declare commands. They encode size and direction, which matters for cross-architecture compatibility.
- Validate command arguments before acting on them. An ioctl is a trust boundary.
- Never dereference a user pointer directly. Use `copyin(9)` and `copyout(9)`.

**Lifecycle phase.** Normal operation.

**Manual page.** `ioctl(9)` (conceptual); the entry point is documented alongside `cdevsw` in `make_dev(9)`.

**Where the book teaches this.** Chapter 24 (Section 3), with further patterns in Chapter 25.

### `devfs_set_cdevpriv(9)`: Per-Open State

**Purpose.** Attach a driver-private pointer to an open file descriptor. The pointer is freed by a callback when the last close occurs.

**Key names.**

- `int devfs_set_cdevpriv(void *priv, d_priv_dtor_t *dtor);`
- `int devfs_get_cdevpriv(void **datap);`
- `void devfs_clear_cdevpriv(void);`

**Header.** `/usr/src/sys/sys/conf.h`.

**Caveats.**

- Per-open state is the right tool for per-descriptor settings, cursors, or pending transactions. Do not store per-open state in the softc.
- The destructor runs in the context of the last close. Keep it short and non-blocking.

**Manual page.** `devfs_set_cdevpriv(9)`.

**Where the book teaches this.** Chapter 8.

## Process and User-Space Interaction

Userland cannot be trusted with kernel addresses, and the kernel cannot be trusted to follow userland pointers without care. The APIs below cross the trust boundary safely.

### `copyin(9)`, `copyout(9)`, `copyinstr(9)`

**Purpose.** Move bytes between kernel and user address spaces with address validation. These are the only safe way to touch a user pointer from kernel code.

**Key names.**

- `int copyin(const void *uaddr, void *kaddr, size_t len);`
- `int copyout(const void *kaddr, void *uaddr, size_t len);`
- `int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done);`
- Related: `fueword`, `fuword`, `subyte`, `suword`, documented under `fetch(9)` and `store(9)`.

**Header.** `/usr/src/sys/sys/systm.h`.

**Caveats.**

- All three may sleep. Do not call them while holding a non-sleepable mutex.
- They return `EFAULT` on a bad address, not zero. Always check the return value.
- `copyinstr` distinguishes truncation from success via its `done` argument; do not ignore it.

**Lifecycle phase.** `d_ioctl`, `d_read`, `d_write`, and anywhere else userland is the source or destination.

**Manual pages.** `copy(9)`, `fetch(9)`, `store(9)`.

**Where the book teaches this.** Chapter 9.

### `uio(9)`: I/O Descriptor for Read and Write

**Purpose.** The kernel's own description of an I/O request. Hides the difference between user and kernel buffers, between scatter-gather and contiguous transfers, and between read and write directions.

**Key names.**

- `int uiomove(void *cp, int n, struct uio *uio);`
- `int uiomove_nofault(void *cp, int n, struct uio *uio);`
- Fields in `struct uio`: `uio_iov`, `uio_iovcnt`, `uio_offset`, `uio_resid`, `uio_segflg`, `uio_rw`, `uio_td`.
- Segment flags: `UIO_USERSPACE`, `UIO_SYSSPACE`, `UIO_NOCOPY`.
- Direction: `UIO_READ`, `UIO_WRITE`.

**Header.** `/usr/src/sys/sys/uio.h`.

**Caveats.**

- Use `uiomove` in `d_read` and `d_write` entry points. It is the right tool even when the userland buffer is a simple contiguous region.
- `uiomove` may sleep. Drop non-sleepable mutexes before calling it.
- After `uiomove` returns, `uio_resid` has been updated. Do not maintain your own byte count in parallel; read it from `uio_resid`.

**Lifecycle phase.** Normal I/O.

**Manual page.** `uio(9)`.

**Where the book teaches this.** Chapter 9.

### `proc(9)` and Thread Context for Drivers

**Purpose.** Access to the calling thread and its process, primarily for credential checks, signal state, and diagnostic printing.

**Key names.**

- `curthread`, `curproc`, `curthread->td_proc`.
- `struct ucred *cred = curthread->td_ucred;`
- `int priv_check(struct thread *td, int priv);`
- `pid_t pid = curproc->p_pid;`

**Header.** `/usr/src/sys/sys/proc.h`.

**Caveats.**

- Direct use of process internals is rare. When you need it, it is usually for a credential check, which should go through `priv_check(9)`.
- Do not store `curthread` across sleeps. The thread that re-enters the driver may be a different one.

**Manual page.** No single page; see `priv(9)` and `proc(9)`.

**Where the book teaches this.** Referenced in Chapter 9 and Chapter 24 when ioctl handlers need credentials.

## Observability and Notification

A driver that cannot be observed is a driver that cannot be trusted. The kernel provides several ways for userland to peek at driver state, subscribe to events, and wait for readiness. The APIs below are the most common.

### `sysctl(9)`: Read-and-Write Configuration Nodes

**Purpose.** Publish driver state and tunables under a hierarchical name so tools like `sysctl(8)` and monitoring scripts can read or modify them.

**Key names.**

- Static declarations: `SYSCTL_NODE`, `SYSCTL_INT`, `SYSCTL_LONG`, `SYSCTL_STRING`, `SYSCTL_PROC`, `SYSCTL_OPAQUE`.
- Dynamic context API: `sysctl_ctx_init`, `sysctl_ctx_free`, `SYSCTL_ADD_NODE`, `SYSCTL_ADD_INT`, `SYSCTL_ADD_PROC`.
- Handler helpers: `sysctl_handle_int`, `sysctl_handle_long`, `sysctl_handle_string`.
- Access flags: `CTLFLAG_RD`, `CTLFLAG_RW`, `CTLFLAG_TUN`, `CTLFLAG_STATS`, `CTLTYPE_INT`, `CTLTYPE_STRING`.

**Header.** `/usr/src/sys/sys/sysctl.h`.

**Caveats.**

- For anything tied to a specific device instance, use the dynamic API. `device_get_sysctl_ctx` and `device_get_sysctl_tree` give you the right context.
- Handlers run in user context. They may sleep and may fail.
- Publish tunables sparingly. Every knob is a contract with future users.

**Lifecycle phase.** Static declarations are module-wide. Dynamic declarations are created in `attach` and destroyed automatically in `detach` through the context.

**Manual pages.** `sysctl(9)`, `sysctl_add_oid(9)`, `sysctl_ctx_init(9)`.

**Where the book teaches this.** Introduced in Chapter 7, with deeper treatment in Chapter 24 (Section 4) when the driver starts exposing metrics to userland.

### `eventhandler(9)`: In-Kernel Publish-Subscribe

**Purpose.** Register for kernel-wide events such as mount, unmount, low memory, and shutdown. The kernel invokes registered callbacks in response.

**Key names.**

- `EVENTHANDLER_DECLARE(name, type_t);`
- `eventhandler_tag EVENTHANDLER_REGISTER(name, func, arg, priority);`
- `void EVENTHANDLER_DEREGISTER(name, tag);`
- `void EVENTHANDLER_INVOKE(name, ...);`
- Priority constants: `EVENTHANDLER_PRI_FIRST`, `EVENTHANDLER_PRI_ANY`, `EVENTHANDLER_PRI_LAST`.

**Header.** `/usr/src/sys/sys/eventhandler.h`.

**Caveats.**

- Handlers run synchronously. Keep them short.
- Always deregister before module unload. A dangling handler will panic when the event fires.

**Manual page.** `EVENTHANDLER(9)`.

**Where the book teaches this.** Referenced in Chapter 24 when drivers integrate with kernel-wide notifications such as shutdown and low-memory events.

### `poll(2)` and `kqueue(2)`: Readiness Notification

**Purpose.** Let userland wait for driver-owned readiness events. `poll(2)` is the older interface; `kqueue(2)` is the modern one with richer filters.

**Key names.**

- Entry point for `poll`: `int (*d_poll)(struct cdev *, int events, struct thread *);`
- Entry point for `kqueue`: `int (*d_kqfilter)(struct cdev *, struct knote *);`
- Wait-list management: `struct selinfo`, `selrecord(struct thread *td, struct selinfo *sip)`, `selwakeup(struct selinfo *sip)`.
- kqueue support: `struct knote`, `knote_enqueue`, `knlist_init_mtx`, `knlist_add`, `knlist_remove`.
- Event bits: `POLLIN`, `POLLOUT`, `POLLERR`, `POLLHUP` for `poll`; `EVFILT_READ`, `EVFILT_WRITE` for `kqueue`.

**Headers.** `/usr/src/sys/sys/selinfo.h`, `/usr/src/sys/sys/event.h`, `/usr/src/sys/sys/poll.h`.

**Caveats.**

- `d_poll` must call `selrecord` when no events are ready and report current readiness when they are.
- `selwakeup` must be called with no mutex held that might invert against the scheduler. This is a common lock-order bug.
- `kqueue` support is richer but also more code. When the driver already has a clean `poll` path, extending it to `kqueue` is often the right next step rather than a rewrite.

**Lifecycle phase.** Setup in `attach`; tear down in `detach`; actual dispatch in `d_poll` or `d_kqfilter`.

**Manual pages.** `selrecord(9)`, `kqueue(9)`, and the userland pages `poll(2)` and `kqueue(2)`.

**Where the book teaches this.** Chapter 10 introduces `poll(2)` integration in full; `kqueue(2)` is referenced there and explored in depth in Chapter 35.

## Diagnostics, Logging, and Tracing

Driver correctness does not just live in the code. It lives in the ability to observe, assert, and trace. The APIs below are how you make a driver tell the truth about itself.

### `log(9)` and `printf(9)`

**Purpose.** Emit messages to the kernel log so they appear in `dmesg` and in `/var/log/messages`.

**Key names.**

- `void log(int level, const char *fmt, ...);`
- Standard kernel `printf` family: `printf`, `vprintf`, `uprintf`, `tprintf`.
- Per-device helper: `device_printf(device_t dev, const char *fmt, ...);`
- Priority constants from `syslog.h`: `LOG_EMERG`, `LOG_ALERT`, `LOG_CRIT`, `LOG_ERR`, `LOG_WARNING`, `LOG_NOTICE`, `LOG_INFO`, `LOG_DEBUG`.

**Headers.** `/usr/src/sys/sys/systm.h`, `/usr/src/sys/sys/syslog.h`.

**Caveats.**

- Do not log at `LOG_INFO` on the I/O fast path. It floods the console and masks real issues.
- `device_printf` automatically prepends the device name, which makes logs easy to filter. Prefer it over bare `printf`.
- Log once for each distinct event class, not once per packet.

**Lifecycle phase.** Any.

**Manual pages.** `printf(9)`.

**Where the book teaches this.** Chapter 23.

### `KASSERT(9)`: Kernel Assertions

**Purpose.** Declare invariants that must be true. When the kernel is built with `INVARIANTS`, a violated assertion panics with a descriptive message. Without `INVARIANTS`, the assertion compiles away.

**Key names.**

- `KASSERT(expression, (format, args...));`
- `MPASS(expression);` for simpler message-free assertions.
- `CTASSERT(expression);` for compile-time assertions on constants.

**Header.** `/usr/src/sys/sys/kassert.h`, included transitively by `/usr/src/sys/sys/systm.h`.

**Caveats.**

- The expression must be cheap and free of side effects. The compiler does not optimise it into place; you write the invariant.
- The message is a parenthesised `printf` argument list. Include enough context to diagnose a failure from the panic alone.
- Use `KASSERT` for conditions that indicate a programmer error, not for normal runtime conditions.

**Lifecycle phase.** Wherever an invariant must be documented and enforced.

**Manual page.** `KASSERT(9)`.

**Where the book teaches this.** Chapter 23 introduces `INVARIANTS` and assertion use; Chapter 34 Section 2 treats `KASSERT` and diagnostic macros in depth.

### `WITNESS`: Lock-Order Verifier

**Purpose.** A kernel option that tracks the order in which each thread acquires locks and warns when a later thread reverses a previously observed order.

**Key names.**

- Built into `mtx(9)`, `sx(9)`, `rm(9)`, and the locking macros. No separate API call is needed.
- Kernel options: `WITNESS`, `WITNESS_SKIPSPIN`, `WITNESS_COUNT`.
- Assertions that cooperate with `WITNESS`: `mtx_assert`, `sx_assert`, `rm_assert`.

**Caveats.**

- `WITNESS` is a debugging option. Build a debug kernel to enable it; it is too expensive for production.
- Warnings are not noise. If `WITNESS` complains, there is a bug.
- Lock-order warnings reference the lock names passed to `mtx_init`, `sx_init`, and so on. Give each lock a meaningful name.

**Manual page.** No single page. See `lock(9)` and `locking(9)`.

**Where the book teaches this.** Chapter 12 (Section 6), with reinforcement in Chapter 23.

### `ktr(9)`: Kernel Trace Facility

**Purpose.** A low-overhead ring buffer for event tracing inside the kernel. `ktr` records are emitted by macros and can be dumped with `ktrdump(8)`.

**Key names.**

- `CTR0(class, fmt)`, `CTR1(class, fmt, a1)`, up to `CTR6` with increasing argument counts.
- Trace classes: `KTR_GEN`, `KTR_NET`, `KTR_DEV`, and many others in `sys/ktr_class.h`.
- Kernel option: `KTR` with per-class masks.

**Header.** `/usr/src/sys/sys/ktr.h`.

**Caveats.**

- `ktr` must be enabled at kernel build time; check for `KTR` in the configuration.
- Each record is small. Do not try to log entire structures.
- For user-facing diagnostics, `dtrace(1)` is often a better answer.

**Manual page.** `ktr(9)`.

**Where the book teaches this.** Chapter 23.

### DTrace Static Probes and Major Providers

**Purpose.** Static and dynamic tracing infrastructure that lets userland attach to probe points in the running kernel without a recompile.

**Key names.**

- Statically defined tracing: `SDT_PROVIDER_DECLARE`, `SDT_PROVIDER_DEFINE`, `SDT_PROBE_DECLARE`, `SDT_PROBE_DEFINE`, `SDT_PROBE`.
- Common providers on FreeBSD: `sched`, `proc`, `io`, `vfs`, `fbt` (function boundary tracing), `sdt`.
- Headers: `/usr/src/sys/sys/sdt.h`, `/usr/src/sys/cddl/dev/dtrace/...`.

**Caveats.**

- `fbt` requires no change to the driver, but `sdt` probes give you named, stable points that survive future refactoring.
- A disabled probe has negligible cost. Do not worry about adding several of them.
- DTrace scripts themselves are userland code; the driver only defines the probe points that the scripts can attach to.

**Manual pages.** `SDT(9)`, `dtrace(1)`, `dtrace(8)`.

**Where the book teaches this.** Chapter 23.

## Cross-Reference by Driver Lifecycle Phase

The same APIs show up in different phases of a driver's life. The table below is a quick reverse index: when you are writing a particular phase, here are the families that usually belong there.

### Module Load

- `MODULE_VERSION`, `MODULE_DEPEND`, `DEV_MODULE` (if the module is a pure cdev).
- Static `MALLOC_DEFINE`, `SYSCTL_NODE`, `SDT_PROVIDER_DEFINE` declarations.
- Event-handler registration that must survive before any device attaches.

### Probe

- `device_get_parent`, `device_get_nameunit`, `device_printf`.
- Return values: `BUS_PROBE_DEFAULT`, `BUS_PROBE_GENERIC`, `BUS_PROBE_SPECIFIC`, `BUS_PROBE_LOW_PRIORITY`, `ENXIO` on no match.

### Attach

- `device_get_softc`, `malloc(9)` for softc fields, `MALLOC_DEFINE` tags.
- Locking initialisation: `mtx_init`, `sx_init`, `rm_init`, `cv_init`, `sema_init`.
- Resource allocation: `bus_alloc_resource` or `bus_alloc_resource_any`.
- `bus_space` setup through `rman_get_bustag` and `rman_get_bushandle`.
- DMA scaffolding: `bus_dma_tag_create`, `bus_dmamap_create`.
- `callout_init`, `TASK_INIT`, taskqueue creation when needed.
- Interrupt setup: `bus_setup_intr`.
- Device node creation: `make_dev_s`.
- sysctl tree: `device_get_sysctl_ctx`, `SYSCTL_ADD_*`.
- `uma_zcreate` for high-frequency objects.
- Eventhandler registration tied to this driver.

### Normal Operation

- `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`, `d_poll`, `d_kqfilter`.
- `uiomove`, `copyin`, `copyout`, `copyinstr`.
- `bus_space_read_*`, `bus_space_write_*`, `bus_space_barrier`.
- `bus_dmamap_load`, `bus_dmamap_sync`, `bus_dmamap_unload`.
- Locking: `mtx_lock`, `mtx_unlock`, `sx_slock`, `sx_xlock`, `cv_wait`, `cv_signal`, `atomic_*`.
- Deferred work: `callout_reset`, `taskqueue_enqueue`.
- Diagnostics: `device_printf`, `log`, `KASSERT`, `SDT_PROBE`.

### Detach

- Tear-down in reverse attach order.
- `bus_teardown_intr` before any resource is released.
- `destroy_dev` before the softc fields it references are torn down.
- `callout_drain` before freeing the callout structure.
- `taskqueue_drain_all` and `taskqueue_free` for private taskqueues.
- `bus_dmamap_unload`, `bus_dmamap_destroy`, `bus_dma_tag_destroy`.
- `bus_release_resource` for every resource allocated in attach.
- `cv_destroy`, `sx_destroy`, `mtx_destroy`, `rm_destroy`, `sema_destroy`.
- `uma_zdestroy` for every zone the driver owns.
- Eventhandler deregistration.
- Final `free` or `contigfree` of anything allocated.

### Module Unload

- Verify that no device instance is still attached. Newbus usually handles this, but defensive `DRIVER_MODULE` event handlers should refuse unload if state remains.

## Quick-Reference Checklists

These checklists are meant to be read in five minutes or less. They do not replace the teaching in the chapters; they remind you of the things that experienced driver authors no longer forget.

### Locking Discipline Checklist

- Every shared field in the softc has exactly one lock that protects it, documented in a comment near the field.
- No mutex is held across `uiomove`, `copyin`, `copyout`, `malloc(9, M_WAITOK)`, or `bus_alloc_resource`.
- Lock order is declared in a comment at the top of the file and respected everywhere.
- `mtx_assert` or `sx_assert` appears on functions that require a particular lock to be held on entry.
- `WITNESS` is enabled in the development kernel and its warnings are treated as bugs.
- Every `mtx_init` has a matching `mtx_destroy`, and so on for every lock type.

### Resource Lifetime Checklist

- `bus_setup_intr` is the last thing in `attach`; `bus_teardown_intr` is the first thing in `detach`.
- Every allocated resource has a matching release, in reverse order, in `detach`.
- `callout_drain` is called before the structure it points at is freed.
- `taskqueue_drain_all` or `taskqueue_drain` is called before the task structures or their arguments are freed.
- `destroy_dev` is called before the softc fields referenced by `mda_si_drv1` are torn down.

### User-Space Safety Checklist

- No user pointer is dereferenced directly. Every cross-boundary access goes through `copyin`, `copyout`, `copyinstr`, or `uiomove`.
- All return values from the copy helpers are checked. `EFAULT` is propagated rather than ignored.
- `_IOR`, `_IOW`, and `_IOWR` are used for ioctl command numbers.
- Ioctl handlers validate arguments before acting on them.
- Credentials are checked with `priv_check(9)` when the operation is privileged.

### Diagnostic Coverage Checklist

- Every major branch that should never be taken carries a `KASSERT`.
- Logs use `device_printf` for instance context.
- At least one DTrace SDT probe marks entry to the main I/O paths.
- `sysctl` exposes the driver's counters in a stable, documented tree.
- The driver has been built and exercised under `INVARIANTS` and `WITNESS` before it is considered done.

## Wrapping Up

This appendix is a reference, not a chapter. It becomes more useful the more you use it. Keep it within reach while you are writing, debugging, or reading driver code, and turn to it whenever you want a fast reminder about a flag, a manual page, or a caveat you almost remember.

Three suggestions for getting the most out of it over time.

First, treat the **Manual pages** line as the canonical next step for any API you only half remember. The manual pages in section 9 are maintained with the tree; they age well. Opening one of them costs nothing and pays back every time.

Second, treat the **Caveats** line as a debugging companion. Most driver bugs are not unknown unknowns. They are documented caveats that the author skipped under time pressure. When you are stuck, read the caveats for every API the problem area touches. It is unglamorous but effective.

Third, when you find a missing entry or a correction to make, write it down. This appendix improves as drivers improve. The FreeBSD kernel is alive and so is the reference. If a new primitive appears, or an old one is retired, the appendix that matches reality is the one you will actually trust.

From here you can jump in several directions. Appendix E covers FreeBSD internals and subsystem behaviour at a depth this reference deliberately avoids. Appendix B collects the algorithms and systems-programming patterns that recur throughout the kernel. Appendix C grounds the hardware concepts that the bus and DMA families depend on. And every chapter in the main book still has source code you can read, labs you can run, and questions you can answer by opening `/usr/src/` and looking at the real thing.

Good reference material is quiet. It stays out of the way while you work, and it is there when you need it. That is the role this appendix is meant to play for the rest of your driver-writing life with FreeBSD.
