---
title: "Timers and Delayed Work"
description: "How FreeBSD drivers express time: scheduling work for the future with callout(9), running it safely under a documented lock, and tearing it down without races at unload."
partNumber: 3
partName: "Concurrency and Synchronization"
chapter: 13
lastUpdated: "2026-04-18"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 195
---

# Timers and Delayed Work

## Reader Guidance & Outcomes

So far, every line of driver code we have written has been *reactive*: the user calls `read(2)`, the kernel calls our handler, we do work, we return. Chapter 12's blocking primitives extended that model with the ability to *wait* for something we did not initiate. But the driver itself never reached out to the world on its own. It had no way to say "in 100 milliseconds from now, please do this". It had no way to count time at all, except as something it observed passing while it was already inside a syscall.

That changes here. Chapter 13 introduces *time* as a first-class concept in your driver. The kernel has an entire subsystem dedicated to running your code at a specific moment in the future, repeatedly if you ask, with precise lock-handling rules and clean teardown semantics. It is called `callout(9)`, and it is small, regular, and deeply useful. By the end of this chapter, your `myfirst` driver will have learned to schedule its own work, to act on the world without being prodded, and to give back its scheduled work safely when the device is unloaded.

The shape of the chapter mirrors Chapter 12's. Each new primitive comes with a motivation, a precise API tour, a worked refactor of the running driver, a `WITNESS`-verified design, and a documented locking story in `LOCKING.md`. The chapter does not stop at "you can call `callout_reset`"; it walks you through every contract the callout subsystem cares about, so that the timers you write keep working when the rest of the driver evolves.

### Why This Chapter Earns Its Own Place

You could try to fake timers. A kernel thread that sleeps in a loop, calling `cv_timedwait_sig` and doing work each time it wakes, is technically a timer. So is a user-space process that opens the device once a second and pokes a sysctl. Neither is wrong, but both are clumsy compared to what the kernel offers, and both create new resources (the kernel thread, the user-space process) that have their own lifetime to manage.

`callout(9)` is the right answer in nearly every case where you want a function to run "later". It is built on the kernel's hardware-clock infrastructure, costs essentially nothing at rest, scales to thousands of pending callouts per system, integrates with `WITNESS` and `INVARIANTS`, and provides clear rules for how to interact with locks and how to drain pending work at teardown. Most drivers in `/usr/src/sys/dev/` use it. Once you know it, the pattern transfers across every kind of driver you will meet: USB, network, storage, watchdog, sensor, anything whose physical world has a clock in it.

The cost of *not* knowing `callout(9)` is high. A driver that re-invents timing creates a private subsystem that nobody else knows how to debug. A driver that uses `callout(9)` correctly slots into the kernel's existing observability tools (`procstat`, `dtrace`, `lockstat`, `ddb`) and behaves predictably at unload time. The chapter pays for itself the first time you have to extend a driver someone else wrote.

### Where Chapter 12 Left the Driver

A brief recap of where you should stand, because Chapter 13 builds directly on the Chapter 12 deliverables. If any of the following is missing or feels uncertain, return to Chapter 12 before starting this chapter.

- Your `myfirst` driver compiles cleanly and is at version `0.6-sync`.
- It uses `MYFIRST_LOCK(sc)` / `MYFIRST_UNLOCK(sc)` macros around `sc->mtx` (the data-path mutex).
- It uses `MYFIRST_CFG_SLOCK(sc)` / `MYFIRST_CFG_XLOCK(sc)` around `sc->cfg_sx` (the configuration sx).
- It uses two named condition variables (`sc->data_cv`, `sc->room_cv`) for blocking reads and writes.
- It supports timed reads via `cv_timedwait_sig` and the `read_timeout_ms` sysctl.
- The lock order `sc->mtx -> sc->cfg_sx` is documented in `LOCKING.md` and enforced by `WITNESS`.
- `INVARIANTS` and `WITNESS` are enabled in your test kernel; you have built and booted it.
- The Chapter 12 stress kit (Chapter 11 testers plus `timeout_tester` and `config_writer`) builds and runs cleanly.

That driver is what we extend in Chapter 13. We will add a periodic callout, then a watchdog callout, then a configurable tick-source, and finally consolidate them with a refactor pass and a documentation update. The driver's data path stays as it was; the new code lives next to the existing primitives.

### What You Will Learn

By the time you move on to the next chapter, you will be able to:

- Explain when a callout is the right primitive for the job and when a kernel thread, a `cv_timedwait`, or a user-space helper would serve better.
- Initialize a callout with the appropriate lock-awareness using `callout_init`, `callout_init_mtx`, `callout_init_rw`, or `callout_init_rm`, and choose between the lock-managed and `mpsafe` variants for your driver's context.
- Schedule a one-shot timer with `callout_reset` (tick-based) or `callout_reset_sbt` (sub-tick precision), using the `tick_sbt`, `SBT_1S`, `SBT_1MS`, `SBT_1US` time constants where appropriate.
- Schedule a periodic timer by having the callback re-arm itself, with the right pattern that survives `callout_drain`.
- Choose between `callout_reset` and `callout_schedule` and understand when each is the right tool.
- Describe the lock contract `callout(9)` enforces when you initialize it with a lock pointer: the kernel acquires that lock before your function runs, releases it after (unless you set `CALLOUT_RETURNUNLOCKED`), and serialises the callout with respect to other lock holders.
- Read and interpret the `c_iflags` and `c_flags` fields of a callout, and use `callout_pending`, `callout_active`, and `callout_deactivate` correctly.
- Use `callout_stop` to cancel a pending callout in normal driver code, and `callout_drain` at teardown to wait for an in-flight callback to finish.
- Recognise the unload race (a callout fires after `kldunload` and crashes the kernel) and describe the standard cure: drain at detach, refuse detach until the device is quiet.
- Apply the `is_attached` pattern (which we built for cv waiters in Chapter 12) to callout callbacks, so a callback that fires during teardown returns cleanly without rescheduling.
- Build a watchdog timer that detects a stuck condition and acts on it.
- Build a debounce timer that ignores rapid repeated events.
- Build a periodic tick-source that injects synthetic data into the cbuf for testing.
- Verify the callout-enabled driver against `WITNESS`, `lockstat(1)`, and a long-running stress test that includes timer activity.
- Extend `LOCKING.md` with a "Callouts" section that names every callout, its callback, its lock, and its lifetime.
- Refactor the driver into a shape where timer code is grouped, named, and obviously safe to maintain.

That is a substantial list. None of it is optional for a driver that uses time at all. All of it transfers directly to drivers that will appear in Part 4 and beyond, where real hardware brings its own clocks and demands its own watchdogs.

### What This Chapter Does Not Cover

Several adjacent topics are deliberately deferred:

- **Taskqueues (`taskqueue(9)`).** Chapter 16 introduces the kernel's general deferred-work framework. Taskqueues and callouts are complementary: callouts run a function at a specific time; taskqueues run a function as soon as a worker thread can pick it up. Many drivers use both: a callout fires at the right moment, the callout enqueues a task, the task runs the actual work in process context where sleeping is permitted. Chapter 13 stays inside the callout's own callback for simplicity; the deferred-work pattern belongs to Chapter 16.
- **Hardware interrupt handlers.** Chapter 14 introduces interrupts. A real driver may install an interrupt handler that runs without process context. The `callout(9)` rules around lock classes are similar to the rules for interrupt handlers (you may not sleep), but the framing is different. We will revisit timer-and-interrupt interaction in Chapter 14.
- **`epoch(9)`.** A read-mostly synchronization framework used by network drivers. Out of scope for Chapter 13.
- **High-resolution event scheduling.** The kernel exposes `sbintime_t` and the `_sbt` variants for sub-tick precision; we touch on the sbintime-based variants of the callout API briefly, but the full story of event timer drivers (`/usr/src/sys/kern/kern_clocksource.c`) belongs in a kernel-internals book, not a driver book.
- **Real-time and deadline scheduling.** Out of scope. We rely on the general scheduler.
- **Periodic workloads via the scheduler tick (`hardclock`).** The kernel itself uses `hardclock(9)` for system-wide periodic work; drivers do not interact with `hardclock` directly. We mention it for context.

Staying inside those lines keeps the chapter focused. The reader of Chapter 13 should finish with confident control of `callout(9)` and a working sense of when to reach for `taskqueue(9)` instead. Chapter 14 and Chapter 16 fill in the rest.

### Estimated Time Investment

- **Reading only**: about three hours. The API surface is small but the lock and lifetime rules deserve careful attention.
- **Reading plus typing the worked examples**: six to eight hours over two sessions. The driver evolves in four small stages; each stage adds one timer pattern.
- **Reading plus all labs and challenges**: ten to fourteen hours over three or four sessions, including time for stress tests with timers active and `lockstat` measurements.

If you find yourself confused midway through Section 5 (the lock-context rules), that is normal. The interaction between callouts and locks is the most surprising part of the API, and even experienced kernel programmers occasionally get it wrong. Stop, re-read Section 5's worked example, and continue when the model has settled.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source matches Chapter 12 Stage 4 (`stage4-final`). The starting point assumes the cv channels, bounded reads, sx-protected configuration, and reset sysctl are all in place.
- Your lab machine is running FreeBSD 14.3 with `/usr/src` on disk and matching the running kernel.
- A debug kernel with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` is built, installed, and booting cleanly.
- You read Chapter 12 carefully. The lock-order discipline, the cv pattern, and the snapshot-and-apply pattern are assumed knowledge here.
- You have run the Chapter 12 composite stress kit at least once and seen it pass cleanly.

If any of the above is shaky, fixing it now is a much better investment than pushing through Chapter 13 and trying to debug from a moving foundation.

### How to Get the Most Out of This Chapter

Three habits will pay off quickly.

First, keep `/usr/src/sys/kern/kern_timeout.c` and `/usr/src/sys/sys/callout.h` bookmarked. The header is short and contains the API the chapter teaches. The implementation file is long but well-commented; we will point at specific functions a few times during the chapter. Two minutes spent at each pointer pays off.

> **A note on line numbers.** When this chapter names a specific function in `kern_timeout.c` or a macro in `callout.h`, treat the name as the fixed address and any line we happen to mention as scenery around it. `callout_init_mtx` and `callout_reset_sbt_on` will still carry those names in future FreeBSD 14.x revisions; the lines where they sit will have moved by the time your editor opens the file. Jump to the symbol and let your editor report the rest.

Second, run every code change you make under `WITNESS`. The callout subsystem has its own rules that `WITNESS` checks at runtime. The most common Chapter 13 mistake is to schedule a callout whose function tries to acquire a sleepable lock from a context where sleeping is illegal; `WITNESS` catches it immediately on a debug kernel and silently corrupts on a production kernel. Do not run Chapter 13 code on the production kernel until it passes the debug kernel.

Third, type the changes by hand. The companion source under `examples/part-03/ch13-timers-and-delayed-work/` is the canonical version, but the muscle memory of typing `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)` once is worth more than reading it ten times. The chapter shows the changes incrementally; mirror that incremental pace in your own copy of the driver.

### Roadmap Through the Chapter

The sections in order are:

1. Why use timers in a driver. Where time enters the picture in real driver work, and which patterns map onto `callout(9)` rather than something else.
2. Introduction to FreeBSD's `callout(9)` API. The structure, the lifecycle, the four initialization variants, and what each one buys you.
3. Scheduling one-shot and repeating events. `callout_reset`, `callout_reset_sbt`, `callout_schedule`, and the periodic-callback re-arm pattern.
4. Integrating timers into your driver. The first refactor: a heartbeat callout that periodically logs stats or fires a synthetic event.
5. Handling locking and context in timers. The lock-aware initialization, the `CALLOUT_RETURNUNLOCKED` and `CALLOUT_SHAREDLOCK` flags, the rules around what a callout function may and may not do.
6. Timer cleanup and resource management. The unload race, `callout_stop` versus `callout_drain`, the standard detach pattern with timers.
7. Use cases and extensions for timed work. Watchdogs, debouncing, periodic polling, delayed retries, statistics rollovers, all framed as small recipes you can lift into other drivers.
8. Refactoring and versioning. A clean `LOCKING.md` extension, a bumped version string, an updated changelog, and a regression run that includes timer-related testing.

Hands-on labs and challenge exercises follow, then a troubleshooting reference, a wrapping-up section, and a bridge to Chapter 14.

If this is your first pass, read linearly and do the labs in order. If you are revisiting, the cleanup section (Section 6) and the refactoring section (Section 8) stand alone and make good single-sitting reads.



## Section 1: Why Use Timers in a Driver?

Most of what a driver does is reactive. A user opens the device, the open handler runs. A user issues `read(2)`, the read handler runs. A signal arrives, a sleeper wakes. The kernel hands control to the driver in response to something the world did. Each invocation has a clear cause, and once the work is done, the driver returns and waits for the next cause.

Real hardware does not always cooperate with that model. A network card may need a heartbeat sent every few seconds even when nothing else is happening, just to convince the switch on the other end that the link is alive. A storage controller may need a watchdog reset every five hundred milliseconds, or it will assume the host has gone away and reset the channel. A USB hub poll has to happen on a timer because the USB bus does not interrupt for the kind of state change the driver wants to see. A button on a development board needs debouncing because the spring contacts produce many events in quick succession when the user only meant one. A driver that retries a transient failure should back off, not loop tightly.

All of those are reasons to schedule code for the future. The kernel has a single primitive for the job, `callout(9)`, and Chapter 13 teaches it from the ground up. Before we get to the API, this section sets the conceptual stage. We look at what "later" means in a driver, what shapes a "later" callback typically takes, and where `callout(9)` fits relative to the other ways a driver could express the idea of time.

### The Three Shapes of "Later"

Driver code that wants to do something in the future falls into one of three shapes. Knowing which shape you are in is half of choosing the right primitive.

**One-shot.** "Do this once, X milliseconds from now, then forget about it." Examples: schedule a watchdog timeout that fires only if no activity has been observed in the next second; debounce a button by ignoring all subsequent presses for fifty milliseconds; defer a teardown step until the current operation completes. The callback runs once and the driver does not re-arm it.

**Periodic.** "Do this every X milliseconds, until I tell you to stop." Examples: poll a hardware register that does not interrupt; emit a heartbeat to a peer; refresh a cached value; sample a sensor; rotate a statistics window. The callback runs once, then re-arms itself for the next interval, and continues until the driver stops it.

**Bounded wait.** "Do this when condition Y becomes true, but give up if Y has not happened in X milliseconds." Examples: wait for a hardware response with a timeout; wait for the buffer to drain or the deadline to fire; allow Ctrl-C to interrupt a wait. We met this shape in Chapter 12 with `cv_timedwait_sig`. The driver thread is the one that waits, not a callback.

`callout(9)` is the primitive for the first two shapes. The third uses `cv_timedwait_sig` (Chapter 12), `mtx_sleep` with a non-zero `timo`, or one of the `_sbt` variants for sub-tick precision. The two are complementary, not alternatives: many drivers use both. A bounded wait suspends the calling thread; a callout runs in a separate context after a delay.

### Real-World Patterns

A short tour of patterns that recur across drivers in `/usr/src/sys/dev/`. Recognising them early gives you a vocabulary for the rest of the chapter.

**The heartbeat.** A periodic callout that fires every N milliseconds and emits some trivial state (a counter increment, a log line, a packet on the wire). Useful for debugging and for protocols that need a liveness signal.

**The watchdog.** A one-shot callout scheduled at the start of an operation. If the operation completes normally, the driver cancels the callout. If the operation hangs, the callout fires and the driver takes corrective action (reset the hardware, log a warning, kill a stuck request). Almost every storage and network driver has at least one watchdog.

**The debounce.** A one-shot callout scheduled when an event arrives. Subsequent identical events within the timeout are ignored. When the callout fires, the driver acts on the most recent event. Used for hardware events that bounce (mechanical switches, optical sensors).

**The poll.** A periodic callout that reads a hardware register and acts on the value. Used when the hardware does not produce interrupts for the events the driver cares about, or when interrupts are too noisy to be useful.

**The retry-with-backoff.** A one-shot callout scheduled with an increasing delay after each failed attempt. The first failure schedules a 10 ms retry; the second schedules a 20 ms retry; and so on. Bounds the rate at which the driver pesters the hardware after a failure.

**The statistics rollover.** A periodic callout that takes a snapshot of internal counters at regular intervals, computes a per-interval rate, and stores it in a circular buffer for later inspection.

**The deferred reaper.** A one-shot callout that completes a teardown after some grace period. Used when an object cannot be freed immediately because some other code path may still hold a reference; the callout waits long enough for those references to drain, then frees the object.

We will implement the first three (heartbeat, watchdog, deferred tick-source) in `myfirst` over the course of this chapter. The others are the same shape; once you know the pattern, the variations are mechanical.

### Why Not Just Use a Kernel Thread?

A reasonable question for a beginner: why is `callout(9)` a separate API at all? Could the same effects be achieved by a kernel thread that loops, sleeps, and acts?

In principle, yes. In practice, no driver should reach for a kernel thread when a callout will do.

A kernel thread is a heavyweight resource. It has its own stack (typically 16 KB on amd64), its own scheduler entry, its own priority, its own state. Spinning one up for a periodic action that takes 10 microseconds every second is wasteful: 16 KB of memory plus scheduler overhead just to wake up, do trivial work, and sleep again. Multiplied across many drivers, the kernel ends up with hundreds of mostly-idle threads.

A callout is essentially free at rest. The data structure is a few pointers and integers (see `struct callout` in `/usr/src/sys/sys/_callout.h`). There is no thread, no stack, no scheduler entry. The kernel's hardware-clock interrupt walks a callout wheel and runs every callout that has come due, then returns. Thousands of pending callouts cost essentially nothing until they fire.

A callout also slots into the kernel's existing observability tools. `dtrace`, `lockstat`, and `procstat` all understand callouts. A custom kernel thread does not have any of that for free; you would have to instrument it yourself.

The exception, of course, is when the work the timer needs to do is genuinely long and would benefit from being in a sleepable thread context. A callout function may not sleep; if your work requires sleeping, the callout's job is to *enqueue* the work onto a taskqueue or wake a kernel thread that can do it safely. Chapter 16 covers that pattern. For Chapter 13, the work the callout does is short, non-sleeping, and lock-aware.

### Why Not Just Use `cv_timedwait` In a Loop?

Another reasonable alternative: a kernel thread that loops on `cv_timedwait_sig` would also produce periodic behaviour. So would a user-space helper that polls a sysctl. Why callout?

The kernel-thread answer is the resource argument from the previous subsection: callouts are vastly cheaper than threads.

The user-space-helper answer is correctness: a driver whose timing depends on a user-space process is a driver that fails when that process crashes, gets paged out, or is denied CPU by an unrelated workload. A driver should be self-sufficient for its own correctness, even if user-space tooling provides additional features on top.

There is one situation where `cv_timedwait_sig` is the right answer: when the *calling thread itself* needs to wait. Chapter 12's `read_timeout_ms` sysctl uses `cv_timedwait_sig` because the reader is the one waiting; it has work to do as soon as data arrives or the deadline fires. A callout would be wrong because the reader's syscall thread cannot be the one running the callback (the callback runs in a different context).

Use `cv_timedwait_sig` when the syscall thread waits. Use `callout(9)` when something independent of any syscall thread must happen at a specific time. The two coexist comfortably in the same driver; Chapter 13 will end with a driver that uses both.

### A Brief Note on Time Itself

The kernel exposes time through several units, each with its own conventions. We met them in Chapter 12; a recap helps before we wade into the API.

- **`int` ticks.** The legacy unit. `hz` ticks equal one second. The default `hz` on FreeBSD 14.3 is 1000, so one tick is one millisecond. `callout_reset` takes its delay in ticks.
- **`sbintime_t`.** A 64-bit signed binary fixed-point representation: upper 32 bits seconds, lower 32 bits a fraction of a second. The unit constants are in `/usr/src/sys/sys/time.h`: `SBT_1S`, `SBT_1MS`, `SBT_1US`, `SBT_1NS`. `callout_reset_sbt` takes its delay in sbintime.
- **`tick_sbt`.** A global variable holding `1 / hz` as an sbintime. Useful when you have a tick count and want the equivalent sbintime: `tick_sbt * timo_in_ticks`.
- **The precision argument.** `callout_reset_sbt` takes an additional `precision` argument. It tells the kernel how much wiggle is acceptable when scheduling, which lets the callout subsystem coalesce nearby timers for power efficiency. A precision of zero means "fire as close to the deadline as possible". A precision of `SBT_1MS` means "anywhere within a millisecond of the deadline is fine".

For most driver work the tick-based API is the right level of precision. We use `callout_reset` (ticks) throughout the early sections of the chapter and reach for `callout_reset_sbt` only when sub-millisecond precision matters or when we want to tell the kernel about acceptable slop.

### When a Callout Is the Wrong Tool

For completeness, three situations where `callout(9)` is *not* the right answer.

- **The work needs to sleep.** Callout functions run in a context that may not sleep. If the work involves `uiomove`, `copyin`, `malloc(M_WAITOK)`, or any other potentially-blocking call, the callout must enqueue a task on a taskqueue or wake a kernel thread that can do the work in process context. Chapter 16.
- **The work needs to run on a specific CPU for cache reasons.** `callout_reset_on` lets you bind a callout to a specific CPU, which is useful, but if the requirement is "run on the same CPU that submitted the request" the answer might be a per-CPU primitive instead. We touch on `callout_reset_on` briefly and defer the deeper CPU-affinity discussion.
- **The work is event-driven, not time-driven.** If the trigger is "data arrived" rather than "100 ms have elapsed", you want a cv or a wakeup, not a callout. Mixing the two often leads to unnecessary complexity.

### A Mental Model: The Callout Wheel

To make the cost argument from the previous subsections concrete, here is what the kernel actually does to manage callouts. You do not need this to use `callout(9)` correctly, but knowing it makes several later sections easier to follow.

The kernel maintains a *callout wheel* per CPU. Conceptually, the wheel is a circular array of buckets. Each bucket corresponds to a small range of time. When you call `callout_reset(co, ticks_in_future, fn, arg)`, the kernel computes which bucket "now plus ticks_in_future" lands in and adds the callout to that bucket's list. The arithmetic is `(current_tick + ticks_in_future) modulo wheel_size`.

A periodic timer interrupt (the hardware clock) fires at every tick. The interrupt handler increments a global tick counter, looks at the current bucket of every CPU's wheel, and walks the list. For each callout that has reached its deadline, the kernel pulls it off the wheel and either runs the callback inline (for `C_DIRECT_EXEC` callouts) or hands it to a callout-processing thread.

Three properties of this mechanism matter for the chapter.

First, scheduling a callout is cheap: it is essentially "compute a bucket index and link the structure into a list". A few atomic operations. No allocation. No context switch.

Second, an unscheduled callout costs nothing: it is just a `struct callout` somewhere in your softc. The kernel does not know about it until you call `callout_reset`. There is no per-callout overhead at rest.

Third, the granularity of the wheel is one tick. A 1.7-tick delay rounds up to 2 ticks. The `precision` argument to `callout_reset_sbt` lets you trade exactness for the kernel's freedom to coalesce nearby firings, which is a power-saving optimization on systems with many concurrent timers. For driver work, the default precision is almost always fine.

There is a great deal more in the actual implementation: per-CPU wheels for cache locality, deferred migration when a callout is rescheduled to a different CPU, special handling for `C_DIRECT_EXEC` callouts that run in the timer interrupt itself, and so on. The implementation is in `/usr/src/sys/kern/kern_timeout.c` if you are curious. Reading it once is worthwhile; you do not need to memorize it.

### What "Now" Means in the Kernel

A small but recurring confusion: there are several time bases in the kernel, and they measure different things.

`ticks` is a global variable that counts hardware-clock interrupts since boot. It increments by one every clock tick. It is fast to read (one memory load), wraps around every few weeks at typical `hz=1000`, and is the time base `callout_reset` uses. Always express callout deadlines as "now plus N ticks", which is what `callout_reset(co, N, ...)` does.

`time_uptime` and `time_second` are `time_t` values that count seconds since boot or since the epoch (respectively). Less precise; useful for log timestamps and human-readable elapsed times.

`sbinuptime()` returns an `sbintime_t` representing seconds-and-fractions since boot. This is the time base `callout_reset_sbt` works in. It does not wrap (well, it would in some hundreds of years).

`getmicrouptime()` and `getnanouptime()` are coarse but fast accessors for "now"; they may be a tick or two old. `microuptime()` and `nanouptime()` are precise but more expensive (they read the hardware timer directly).

For a driver doing typical timer work, `ticks` (for tick-based callout work) and `getsbinuptime()` (for sbintime-based work) are the two that come up. We use them in the labs without commentary; if you wonder where they come from, this is the answer.

### Wrapping Up Section 1

Time enters driver work in three shapes: one-shot, periodic, and bounded wait. The first two are exactly what `callout(9)` is for; the third is what Chapter 12's `cv_timedwait_sig` is for. Real-world patterns (heartbeat, watchdog, debounce, poll, retry, rollover, deferred reaper) are all instances of one-shot or periodic callouts; recognising them lets you reuse the same primitive across many situations.

Underneath the API, the kernel maintains a per-CPU callout wheel that costs essentially nothing at rest and very little to schedule. The granularity is one tick (one millisecond on a typical FreeBSD 14.3 system). The implementation handles thousands of pending callouts per CPU without breaking a sweat.

Section 2 introduces the API: the callout structure, the four initialization variants, and the lifecycle every callout follows.



## Section 2: Introduction to FreeBSD's `callout(9)` API

`callout(9)` is, like most of the synchronization primitives, a small API on top of a careful implementation. The data structure is short, the lifecycle is regular (init, schedule, fire, stop or drain, destroy), and the rules are explicit enough that you can verify your usage by reading the source. This section walks through the structure, names the variants of init, and lays out the lifecycle stages so the rest of the chapter has a vocabulary to reuse.

### The Callout Structure

The data structure lives in `/usr/src/sys/sys/_callout.h`:

```c
struct callout {
        union {
                LIST_ENTRY(callout) le;
                SLIST_ENTRY(callout) sle;
                TAILQ_ENTRY(callout) tqe;
        } c_links;
        sbintime_t c_time;       /* ticks to the event */
        sbintime_t c_precision;  /* delta allowed wrt opt */
        void    *c_arg;          /* function argument */
        callout_func_t *c_func;  /* function to call */
        struct lock_object *c_lock;   /* lock to handle */
        short    c_flags;        /* User State */
        short    c_iflags;       /* Internal State */
        volatile int c_cpu;      /* CPU we're scheduled on */
};
```

It is one structure per callout, embedded in the softc or wherever else you need it. The fields you touch directly are: none. Every interaction goes through API calls. The fields you can read for diagnostic purposes are `c_flags` (via `callout_active` / `callout_pending`) and `c_arg` (rarely useful from outside).

The two flag fields deserve a sentence each.

`c_iflags` is internal. The kernel sets and clears bits in it under the callout subsystem's own lock. The bits encode whether the callout is on a wheel or processing list, whether it is pending, and a handful of internal book-keeping states. Driver code uses `callout_pending(c)` to read it; nothing else.

`c_flags` is external. The caller (your driver) is supposed to manage two bits in it: `CALLOUT_ACTIVE` and `CALLOUT_RETURNUNLOCKED`. The active bit is meant to track "I have asked this callout to be scheduled and have not cancelled it yet". The returnunlocked bit changes the lock-handling contract; we will get to that in Section 5. Driver code reads the active bit via `callout_active(c)` and clears it via `callout_deactivate(c)`.

The `c_lock` field deserves its own paragraph. When you initialize the callout with `callout_init_mtx`, `callout_init_rw`, or `callout_init_rm`, the kernel records the lock pointer here. Later, when the callout fires, the kernel acquires that lock before calling your callback function and releases it after the callback returns (unless you specifically asked otherwise). This means your callback runs as if the caller had acquired the lock for it. The lock-managed callout is almost always what you want for driver code; we will say more in Section 5.

### The Callback Function Signature

A callout's callback function has a single argument: a `void *`. The kernel passes whatever you registered with `callout_reset` (or its variants). The function returns `void`. Its full signature, from `/usr/src/sys/sys/_callout.h`:

```c
typedef void callout_func_t(void *);
```

Convention: pass a pointer to your softc (or to whatever per-instance state the callback needs). The first line of the callback casts the void pointer back to the struct pointer:

```c
static void
myfirst_heartbeat(void *arg)
{
        struct myfirst_softc *sc = arg;
        /* ... do timer work ... */
}
```

The argument is fixed at registration time and does not change between firings. If you need to pass changing context to the callback, store it somewhere the callback can find via the softc.

### The Four Initialization Variants

`callout(9)` offers four ways to initialize a callout, distinguished by what kind of lock (if any) the kernel will acquire for you before the callback runs.

```c
void  callout_init(struct callout *c, int mpsafe);

#define callout_init_mtx(c, mtx, flags) \
    _callout_init_lock((c), &(mtx)->lock_object, (flags))
#define callout_init_rw(c, rw, flags) \
    _callout_init_lock((c), &(rw)->lock_object, (flags))
#define callout_init_rm(c, rm, flags) \
    _callout_init_lock((c), &(rm)->lock_object, (flags))
```

`callout_init(c, mpsafe)` is the legacy, lock-naive variant. The `mpsafe` argument is now badly named; it really means "may run without acquiring Giant for me". Pass `1` for any modern driver code; pass `0` only if you genuinely want the kernel to acquire Giant before your callback (almost never, and only in very old code paths). New drivers should not use this variant. The chapter mentions it for completeness because you will see it in old code.

`callout_init_mtx(c, mtx, flags)` registers a sleep mutex (`MTX_DEF`) as the callout's lock. Before each firing, the kernel acquires the mutex and releases it after the callback returns. This is the variant you will use for almost all driver code. It pairs naturally with the `MTX_DEF` mutex you already have on the data path.

`callout_init_rw(c, rw, flags)` registers an `rw(9)` reader/writer lock. The kernel takes the write lock unless you set `CALLOUT_SHAREDLOCK`, in which case it takes the read lock. Less common in driver code; useful when the callback needs to read a piece of read-mostly state and several callouts share the same lock.

`callout_init_rm(c, rm, flags)` registers an `rmlock(9)`. Specialized; used in network drivers with hot read paths that should not contend.

For the `myfirst` driver, every callout we add will use `callout_init_mtx(&sc->some_co, &sc->mtx, 0)`. The kernel acquires `sc->mtx` before the callback runs, the callback can manipulate the cbuf and other mutex-protected state without taking the lock itself, and the kernel releases `sc->mtx` after. The pattern is clean, the rules are explicit, and `WITNESS` will yell if you violate them.

### The flags Argument

The flags argument to `_callout_init_lock` is one of two values for driver code:

- `0`: the callout's lock is acquired before the callback and released after. This is the default and the right answer almost every time.
- `CALLOUT_RETURNUNLOCKED`: the callout's lock is acquired before the callback. The callback is responsible for releasing it (or it may have already been released by something the callback called). This is occasionally useful when the callback's last action is to drop the lock and do something the lock cannot cover.
- `CALLOUT_SHAREDLOCK`: only valid for `callout_init_rw` and `callout_init_rm`. The lock is acquired in shared mode rather than exclusive.

For Chapter 13 we use `0` everywhere. `CALLOUT_RETURNUNLOCKED` is mentioned in Section 5 for completeness; the chapter does not need it.

### The Five Lifecycle Stages

Every callout follows the same five-stage lifecycle. Knowing the stages by name will make the rest of the chapter much easier to read.

**Stage 1: initialized.** The `struct callout` has been initialized with one of the init variants. It has a lock association (or `mpsafe`). It has not been scheduled. Nothing will fire until you tell it to.

**Stage 2: pending.** You have called `callout_reset` or `callout_reset_sbt`. The kernel has placed the callout on its internal wheel and noted the time at which it should fire. `callout_pending(c)` returns true. The callback has not yet run. You can cancel by calling `callout_stop(c)`, which removes it from the wheel.

**Stage 3: firing.** The deadline has arrived and the kernel is now running the callback. If the callout has a registered lock, the kernel has acquired it. Your callback function is executing. During this stage `callout_active(c)` is true, `callout_pending(c)` may be false (it has been removed from the wheel). The callback is free to call `callout_reset` to re-arm itself (this is the periodic pattern).

**Stage 4: completed.** The callback has returned. If the callback re-armed via `callout_reset`, the callout is back in stage 2. Otherwise it is now idle: `callout_pending(c)` is false. If the kernel acquired a lock for the callback, it has released it.

**Stage 5: destroyed.** The callout's underlying memory is no longer needed. There is no `callout_destroy` function; instead, you must ensure that the callout is not pending and not firing, then free the containing structure. The standard tool for the "wait for the callout to be safely idle" job is `callout_drain`. Section 6 covers this in detail.

The cycle is: init once, alternate between pending and (firing + completed) any number of times, drain, free.

### A First Look at the API

We have not yet scheduled anything. Let us read the four most important calls, with one-line summaries each:

```c
int  callout_reset(struct callout *c, int to_ticks,
                   void (*fn)(void *), void *arg);
int  callout_reset_sbt(struct callout *c, sbintime_t sbt,
                   sbintime_t prec, void (*fn)(void *), void *arg, int flags);
int  callout_stop(struct callout *c);
int  callout_drain(struct callout *c);
```

`callout_reset` schedules the callout. The first argument is the callout to schedule. The second is the delay in ticks (multiply seconds by `hz` to convert; on FreeBSD 14.3, `hz=1000` typically, so a tick is a millisecond). The third is the callback function. The fourth is the argument to pass to the callback. Returns nonzero if the callout was previously pending and got cancelled (so the new schedule is replacing the old).

`callout_reset_sbt` is the same but takes the delay as `sbintime_t` and accepts a precision and flags. Used for sub-tick precision or when you want to tell the kernel about acceptable slop. Most drivers use `callout_reset` and reach for `_sbt` only when needed.

`callout_stop` cancels a pending callout. If the callout was pending, it is removed from the wheel and never fires. Returns nonzero if a pending callout was cancelled. If the callout was not pending (already fired, or never scheduled), the call is a no-op and returns zero. Critically: `callout_stop` does *not* wait for an in-flight callback to finish. If the callback is currently executing on another CPU, `callout_stop` returns before the callback returns.

`callout_drain` is the safe-for-teardown variant. It cancels the callout if pending, *and* waits for any currently-executing callback to return before itself returning. After `callout_drain` returns, the callout is guaranteed to be idle and not running anywhere. This is the function you call at detach time. Section 6 walks through why this matters.

### Reading the Source

If you have ten minutes, open `/usr/src/sys/sys/callout.h` and `/usr/src/sys/kern/kern_timeout.c` and skim. Three things to look for:

The header defines the public API in less than 130 lines. Every function the chapter mentions is declared there. The macros that wrap `_callout_init_lock` are clearly visible.

The implementation file is long (around 1550 lines in FreeBSD 14.3), but the function names match the API. `callout_reset_sbt_on` is the core scheduling function; everything else is a wrapper. `_callout_stop_safe` is the unified stop-and-maybe-drain function; `callout_stop` and `callout_drain` are macros that call it with different flags. `callout_init` and `_callout_init_lock` sit near the bottom of the file.

The chapter cites FreeBSD functions and tables by name rather than line number, because line numbers drift between releases while the function and symbol names survive. If you need approximate line numbers for `kern_timeout.c` in FreeBSD 14.3: `callout_reset_sbt_on` near 936, `_callout_stop_safe` near 1085, `callout_init` near 1347. Open the file and jump to the symbol; the line is whatever your editor reports.

The KASSERTs scattered through the source are the rules in code form. For example, the assertion in `_callout_init_lock` that "you may not give me a sleepable lock" enforces the rule that callouts may not block on a lock that could sleep. Reading those assertions builds confidence that the API guarantees what it says.

### A Worked Lifecycle Walkthrough

Putting the lifecycle stages on a timeline makes them concrete. Imagine a heartbeat callout that is initialized at attach, enabled at t=0, and disabled at t=2.5 seconds.

- **t=-1s (attach time)**: The driver calls `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)`. The callout is now in stage 1 (initialized). `callout_pending(c)` returns false. The kernel knows about the callout's lock association.
- **t=0s**: The user enables the heartbeat by writing the sysctl. The handler acquires `sc->mtx`, sets `interval_ms = 1000`, and calls `callout_reset(&sc->heartbeat_co, hz, myfirst_heartbeat, sc)`. The callout transitions to stage 2 (pending). `callout_pending(c)` returns true. The kernel has placed it on a wheel bucket corresponding to t+1 second.
- **t=1s**: The deadline arrives. The kernel pulls the callout off the wheel (`callout_pending(c)` becomes false). The kernel acquires `sc->mtx`. The kernel calls `myfirst_heartbeat(sc)`. The callout is now in stage 3 (firing). The callback runs, emits a log line, calls `callout_reset` to re-arm. The re-arm puts the callout back on a wheel bucket for t+2 seconds. `callout_pending(c)` is true again. The callback returns. The kernel releases `sc->mtx`. The callout is now in stage 2 (pending) again, waiting for its next firing.
- **t=2s**: Same sequence. The callback fires, re-arms, the callout is pending for t+3 seconds.
- **t=2.5s**: The user disables the heartbeat by writing the sysctl. The handler acquires `sc->mtx`, sets `interval_ms = 0`, and calls `callout_stop(&sc->heartbeat_co)`. The kernel removes the callout from the wheel. `callout_stop` returns 1 (it cancelled a pending callout). `callout_pending(c)` becomes false. The callout is now back in stage 1 (initialized but idle).
- **t=∞ (later, at detach time)**: The detach path calls `callout_drain(&sc->heartbeat_co)`. The callout is already idle; `callout_drain` returns immediately. The driver may now safely free the surrounding state.

Notice three things about the timeline.

The cycle of pending → firing → pending repeats indefinitely as long as the callback re-arms. There is no hard limit on iterations.

A `callout_stop` can intercept the pending-firing-pending cycle at any point. If the callout is in stage 2 (pending), `callout_stop` cancels it. If the callout is in stage 3 (firing) on another CPU, `callout_stop` does *not* cancel it (the callback will run to completion); the next iteration of the cycle will not happen because the callback's re-arm condition (`interval_ms > 0`) is now false.

The `is_attached` check in the callback (which we will introduce in Section 4) provides a similar interception point during teardown. If the callback fires after detach has cleared `is_attached`, the callback exits without re-arming, and the next iteration does not happen.

This timeline is the entire shape of `callout(9)` use in driver code. Variations involve adding a one-shot pattern (no re-arm), a watchdog pattern (cancel on success), or a debounce pattern (only schedule if not already pending). The lifecycle stages are the same.

### A Word About "active" Versus "pending"

Two related concepts that beginners sometimes conflate.

`callout_pending(c)` is set by the kernel when the callout is on the wheel waiting to fire. It is cleared by the kernel when the callout fires (the callback is about to run) or when `callout_stop` cancels it.

`callout_active(c)` is set by the kernel when `callout_reset` succeeds. It is cleared by `callout_deactivate` (a function you call) or by `callout_stop`. Crucially, the kernel does *not* clear `callout_active` when the callback fires. The active bit is a flag that says "I scheduled this callout and have not actively cancelled it"; whether the callback has fired since then is a separate question.

A callout can be in any of four states:

- not active and not pending: never scheduled, or cancelled via `callout_stop`, or deactivated via `callout_deactivate` after firing.
- active and pending: scheduled, on the wheel, waiting to fire.
- active and not pending: scheduled, fired (or about to fire), and the callback has not yet called `callout_deactivate`.
- not active and pending: rare, but possible if the driver calls `callout_deactivate` while the callout is still scheduled. Most drivers never reach this state because they call `callout_deactivate` only inside the callback, after the pending bit has already been cleared.

For most drivers you only need `callout_pending` (used in patterns like the debounce). The `active` flag matters more in code that wants to know "did we schedule a callout, even if it has already run?". For Chapter 13, we use `pending` once and never use `active`.

### Wrapping Up Section 2

Callouts are small structures with a small API and a regular lifecycle. The four initialization variants pick the kind of lock the kernel will acquire for you (or none). The four functions you will use most are `callout_reset`, `callout_reset_sbt`, `callout_stop`, and `callout_drain`. Section 3 puts them to work, scheduling one-shot and periodic timers and showing how the periodic re-arm pattern actually works.



## Section 3: Scheduling One-shot and Repeating Events

A timer in `callout(9)` is always conceptually one-shot. There is no `callout_reset_periodic` function. Periodic behaviour is built by having the callback re-arm itself at the end of each firing. Both the one-shot and the periodic patterns use the same API call (`callout_reset`); the difference is whether the callback decides to schedule the next firing.

This section walks through both patterns with worked examples that compile and run. We will not yet integrate them into `myfirst`; that is Section 4. Here we focus on the timing primitives and the patterns you will use.

### The One-Shot Pattern

The simplest possible callout: schedule a callback to fire once, in the future.

```c
static void
my_oneshot(void *arg)
{
        device_printf((device_t)arg, "one-shot fired\n");
}

void
schedule_a_one_shot(device_t dev, struct callout *co)
{
        callout_reset(co, hz / 10, my_oneshot, dev);
}
```

`hz / 10` means "100 milliseconds from now" on a system with `hz=1000`. The callback receives the device pointer we registered. It runs once, prints, and returns. The callout is now idle. To run it again, you would call `callout_reset` again.

Three things to notice. First, the callback's argument is whatever we passed to `callout_reset`, untyped, recovered with a cast. Second, the callback emits one log line and returns; it does not re-schedule. This is the one-shot pattern. Third, we used `hz / 10` rather than a hard-coded value. Always express callout delays in terms of `hz` so the code is portable across systems with different clock rates.

If you wanted a 250 ms delay, you would write `hz / 4` (or `hz * 250 / 1000` for clarity). For a 5 second delay, `hz * 5`. The arithmetic is integer; for fractional values, multiply before dividing to preserve precision.

### The Periodic Pattern

For periodic behaviour, the callback re-arms itself at the end:

```c
static void
my_periodic(void *arg)
{
        struct myfirst_softc *sc = arg;
        device_printf(sc->dev, "tick\n");
        callout_reset(&sc->heartbeat_co, hz, my_periodic, sc);
}

void
start_periodic(struct myfirst_softc *sc)
{
        callout_reset(&sc->heartbeat_co, hz, my_periodic, sc);
}
```

The first call to `callout_reset` (in `start_periodic`) arms the callout for one second from now. When it fires, `my_periodic` runs, emits a log line, and re-arms for one second after the present moment. The next firing happens, the cycle continues. To stop the periodic firing, call `callout_stop(&sc->heartbeat_co)` (or `callout_drain` at teardown). Once the callout has been stopped, `my_periodic` will not fire again until `start_periodic` is called again.

Three subtleties.

First, the re-arm happens at the *end* of the callback. If the callback's work takes a long time, the next firing is delayed by that work. The actual interval between firings is roughly `hz` ticks plus the time the callback took. For most driver use cases this is fine. If you need an exact period, use `callout_schedule` or `callout_reset_sbt` with a calculated absolute deadline.

Second, the callback is called with the callout's lock acquired (we will see why in Section 5). When the callback calls `callout_reset` to re-arm, the callout subsystem handles the re-arm correctly even though it is being called from inside the firing of the same callout. The kernel's internal bookkeeping is designed for exactly this pattern.

Third, if the driver is being torn down at the same moment the callback re-arms, you have a race: the re-arm puts the callout back on the wheel after the cancel/drain has run. Section 6 explains how to handle this. The short answer is: at detach time, set a "shutting down" flag in the softc under the mutex, then `callout_drain` the callout. The callback checks the flag at entry and returns without re-arming if it sees the flag set. The drain waits for the in-flight callback to return.

### `callout_schedule` for Re-Arming Without Repeating Arguments

For periodic callouts, the callback re-arms with the same function and argument every time. `callout_reset` requires you to pass them again. `callout_schedule` is a convenience that uses the function and argument from the last `callout_reset`:

```c
int  callout_schedule(struct callout *c, int to_ticks);
```

Inside the periodic callback:

```c
static void
my_periodic(void *arg)
{
        struct myfirst_softc *sc = arg;
        device_printf(sc->dev, "tick\n");
        callout_schedule(&sc->heartbeat_co, hz);
}
```

The kernel uses the function pointer and argument it remembers from the last `callout_reset` call. Less typing, and the code reads slightly cleaner. Both `callout_reset` and `callout_schedule` work for the periodic pattern; pick whichever you prefer.

### Sub-Tick Precision With `callout_reset_sbt`

When you need precision finer than a tick, or you want to tell the kernel about acceptable slop, use the sbintime variant:

```c
int  callout_reset_sbt(struct callout *c, sbintime_t sbt,
                       sbintime_t prec,
                       void (*fn)(void *), void *arg, int flags);
```

Example: schedule a 250-microsecond timer:

```c
sbintime_t sbt = 250 * SBT_1US;
callout_reset_sbt(&sc->fast_co, sbt, SBT_1US,
    my_callback, sc, C_HARDCLOCK);
```

The `prec` argument is the precision the caller is willing to accept. `SBT_1US` says "anywhere within one microsecond of the deadline is fine"; the kernel may coalesce this timer with other timers up to one microsecond apart. `0` means "fire as close to the deadline as possible". The flags include `C_HARDCLOCK` (align to the system clock interrupt, default for most cases), `C_DIRECT_EXEC` (run in the timer interrupt context, only useful with a spin lock), `C_ABSOLUTE` (interpret `sbt` as absolute time rather than a relative delay), and `C_PRECALC` (used internally; don't set it).

For the chapter, we use `callout_reset` (tick-based) almost everywhere. `callout_reset_sbt` is mentioned for completeness; the lab section has one exercise using it.

### Cancellation: `callout_stop`

To cancel a pending callout, call `callout_stop`:

```c
int  callout_stop(struct callout *c);
```

If the callout is pending, the kernel removes it from the wheel and returns 1. If the callout is not pending (already fired, or never scheduled), the call is a no-op and returns 0.

Critically: `callout_stop` does *not* wait. If the callback is currently executing on another CPU when `callout_stop` is called, the call returns immediately. The callback continues to run on the other CPU and finishes when it finishes. If the callback re-arms itself, the callout is back on the wheel after `callout_stop` returns.

This means `callout_stop` is the right tool for normal operation (cancel a pending callout because the condition that motivated it has been resolved) but the *wrong* tool for teardown (where you must wait for any in-flight callback to finish before freeing the surrounding state). For teardown, use `callout_drain`. Section 6 covers this distinction in depth.

The standard pattern in normal operation:

```c
/* Decided we don't need this watchdog any more */
if (callout_stop(&sc->watchdog_co)) {
        /* The callout was pending; we just cancelled it. */
        device_printf(sc->dev, "watchdog cancelled\n");
}
/* If callout_stop returned 0, the callout had already fired
   or was never scheduled; nothing to do. */
```

A small point of caution: between `callout_stop` returning 1 and the next statement running, no other thread can re-arm the callout because we hold the lock that protects the surrounding state. Without the lock, `callout_stop` would still cancel correctly, but the meaning of the return value would become racy.

### Cancellation: `callout_drain`

`callout_drain` is the teardown-safe variant:

```c
int  callout_drain(struct callout *c);
```

Like `callout_stop`, it cancels a pending callout. *Unlike* `callout_stop`, if the callback is currently executing on another CPU, `callout_drain` waits for it to return before itself returning. After `callout_drain` returns, the callout is guaranteed to be idle: not pending, not firing, and (if the callback has not re-armed) it will not fire again.

Two important rules.

First, the caller of `callout_drain` *must not* hold the callout's lock. If the callout is currently executing (it has acquired the lock and is running the callback), `callout_drain` needs to wait for the callback to return, which means the callback needs to release the lock, which means the caller of `callout_drain` cannot be holding it. Holding the lock would deadlock.

Second, `callout_drain` may sleep. The thread waits on a sleep queue for the callback to finish. Therefore `callout_drain` is only legal in contexts where sleeping is allowed (process context or kernel thread; not interrupt or spin-lock context).

The standard teardown pattern:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* mark "going away" so a re-arming callback will not re-schedule */
        MYFIRST_LOCK(sc);
        sc->is_attached = 0;
        MYFIRST_UNLOCK(sc);

        /* drain the callout: cancel pending, wait for in-flight */
        callout_drain(&sc->heartbeat_co);
        callout_drain(&sc->watchdog_co);

        /* now safe to destroy other primitives and free state */
        /* ... */
}
```

Section 6 expands this pattern in detail.

### `callout_pending` and `callout_active`

Two diagnostic accessors that are useful when you want to know what state a callout is in:

```c
int  callout_pending(const struct callout *c);
int  callout_active(const struct callout *c);
void callout_deactivate(struct callout *c);
```

`callout_pending(c)` returns nonzero if the callout is currently scheduled and waiting to fire. False if the callout has already fired (or never been scheduled, or was cancelled).

`callout_active(c)` returns nonzero if `callout_reset` has been called on this callout since the last `callout_deactivate`. The "active" bit is something *you* manage. The kernel never sets or clears it on its own (with one small exception: a successful `callout_stop` clears it). The convention is that the callback clears the bit at the start, the rest of the driver sets it when scheduling, and code that wonders "do I have a pending or just-fired callout?" can check `callout_active`.

For most driver work you do not need either accessor. We mention them because real driver source uses them and you should recognize the pattern. The `myfirst` driver in Chapter 13 uses `callout_pending` once, in the watchdog cancellation path; the rest of the chapter does not need them.

### A Worked Example: A Two-Stage Schedule

Putting the pieces together: a small worked example that schedules a callback to fire 100 ms from now, then re-schedules it for 500 ms after that, then runs it once and stops.

```c
static int g_count = 0;
static struct callout g_co;
static struct mtx g_mtx;

static void
my_callback(void *arg)
{
        printf("callback fired (count=%d)\n", ++g_count);
        if (g_count == 1) {
                /* Reschedule for 500 ms later. */
                callout_reset(&g_co, hz / 2, my_callback, NULL);
        } else if (g_count == 2) {
                /* Done; do nothing, callout becomes idle. */
        }
}

void
start_test(void)
{
        mtx_init(&g_mtx, "test_co", NULL, MTX_DEF);
        callout_init_mtx(&g_co, &g_mtx, 0);
        callout_reset(&g_co, hz / 10, my_callback, NULL);
}

void
stop_test(void)
{
        callout_drain(&g_co);
        mtx_destroy(&g_mtx);
}
```

Ten lines of substance. The callback decides whether to re-arm based on the count. After two firings, it stops re-arming and the callout becomes idle. `stop_test` drains the callout (waiting if necessary for any in-flight firing), then destroys the mutex.

This pattern, with variations, is the entire shape of `callout(9)` use in driver code. Section 4 puts it inside `myfirst` and gives it real work to do.

### Wrapping Up Section 3

Callouts are scheduled with `callout_reset` (tick-based) or `callout_reset_sbt` (sbintime-based). One-shot behaviour comes from a callback that does not re-arm; periodic behaviour comes from a callback that re-arms itself at the end. Cancellation is `callout_stop` for normal operation and `callout_drain` for teardown. The accessors `callout_pending`, `callout_active`, and `callout_deactivate` are for diagnostic inspection.

Section 4 takes the patterns of this section and integrates a real callout into the `myfirst` driver: a heartbeat that periodically logs a stat line.



## Section 4: Integrating Timers into Your Driver

Theory is comfortable; integration is where the rough edges show up. This section walks through adding a heartbeat callout to `myfirst`. The heartbeat fires once per second, logs a short statistics line, and re-arms itself. We will see how the callout integrates with the existing mutex, how the lock-aware initialization removes a class of race, how the `is_attached` flag from Chapter 12 protects the callback during teardown, and how `WITNESS` confirms the design is correct.

Consider this Stage 1 of the chapter's driver evolution. By the end of this section, the `myfirst` driver has its first timer.

### Adding a Heartbeat Callout

Add two fields to `struct myfirst_softc`:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct callout          heartbeat_co;
        int                     heartbeat_interval_ms;  /* 0 = disabled */
        /* ... rest ... */
};
```

`heartbeat_co` is the callout itself. `heartbeat_interval_ms` is a sysctl-tunable that lets the user enable, disable, and adjust the heartbeat at runtime. A value of zero disables the heartbeat. A positive value is the interval in milliseconds.

Initialize the callout in `myfirst_attach`. Place the call after the mutex is initialized and before the cdev is created (so the callout is ready to schedule but no user can yet trigger anything):

```c
static int
myfirst_attach(device_t dev)
{
        /* ... existing setup ... */

        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);
        cv_init(&sc->data_cv, "myfirst data");
        cv_init(&sc->room_cv, "myfirst room");
        sx_init(&sc->cfg_sx, "myfirst cfg");
        callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0);

        /* ... rest of attach ... */
}
```

`callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)` registers `sc->mtx` as the callout's lock. From this point on, any time the heartbeat callout fires, the kernel will acquire `sc->mtx` before calling our callback and release it after the callback returns. This is exactly the contract we want: the callback may freely manipulate cbuf state and per-softc fields without taking the lock itself.

Drain the callout in `myfirst_detach`, before destroying the primitives:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* ... refuse detach while active_fhs > 0 ... */
        /* ... clear is_attached and broadcast cvs under sc->mtx ... */

        seldrain(&sc->rsel);
        seldrain(&sc->wsel);

        callout_drain(&sc->heartbeat_co);

        if (sc->cdev_alias != NULL) { destroy_dev(sc->cdev_alias); /* ... */ }
        /* ... rest of detach as before ... */
}
```

The `callout_drain` call must come *after* `is_attached` is cleared and the cvs are broadcast (so a callback that fires during the drain sees the cleared flag), and *before* any primitive the callback might touch is destroyed. The cleared `is_attached` flag prevents the callback from rescheduling; the drain waits for any in-flight callback to finish. After `callout_drain` returns, no callback can be running and none is pending; the rest of detach can safely free state.

### The Heartbeat Callback

Now the callback itself:

```c
static void
myfirst_heartbeat(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t used;
        uint64_t br, bw;
        int interval;

        MYFIRST_ASSERT(sc);

        if (!sc->is_attached)
                return;  /* device going away; do not re-arm */

        used = cbuf_used(&sc->cb);
        br = counter_u64_fetch(sc->bytes_read);
        bw = counter_u64_fetch(sc->bytes_written);
        device_printf(sc->dev,
            "heartbeat: cb_used=%zu, bytes_read=%ju, bytes_written=%ju\n",
            used, (uintmax_t)br, (uintmax_t)bw);

        interval = sc->heartbeat_interval_ms;
        if (interval > 0)
                callout_reset(&sc->heartbeat_co,
                    (interval * hz + 999) / 1000,
                    myfirst_heartbeat, sc);
}
```

Ten lines that capture the entire periodic-heartbeat pattern. Let us walk through them.

`MYFIRST_ASSERT(sc)` confirms that `sc->mtx` is held. The callout was initialized with `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)`, so the kernel acquired `sc->mtx` before calling us; the assertion is a sanity check that catches the case where someone (perhaps a future maintainer) accidentally changes the init to `callout_init` without paying attention.

`if (!sc->is_attached) return;` is the teardown guard. If the detach path has cleared `is_attached`, we exit immediately without doing any work and without re-arming. The drain in `myfirst_detach` will see the callout idle and complete cleanly.

The cbuf-used and counter reads happen under the lock. We call `cbuf_used` (which expects `sc->mtx` held) and `counter_u64_fetch` (which is lockless and safe everywhere). The `device_printf` call is potentially expensive but is conventional for log lines; we tolerate the cost because it happens at most once per second.

The re-arm at the end uses the current value of `heartbeat_interval_ms`. If the user has set it to zero (disabled the heartbeat), we do not re-arm, and the callout becomes idle until something else schedules it. If the user has changed the interval, the next firing will use the new value. This is a small but significant feature: the heartbeat's frequency is dynamically configurable without restarting the driver.

The `(interval * hz + 999) / 1000` arithmetic converts milliseconds to ticks, rounding up. Same formula as Chapter 12's bounded waits, for the same reason: never round below the requested duration.

### Starting the Heartbeat From a Sysctl

The user enables the heartbeat by writing a non-zero value to `dev.myfirst.<unit>.heartbeat_interval_ms`. We need a sysctl handler that schedules the first firing:

```c
static int
myfirst_sysctl_heartbeat_interval_ms(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error;

        old = sc->heartbeat_interval_ms;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        sc->heartbeat_interval_ms = new;
        if (new > 0 && old == 0) {
                /* Enabling: schedule the first firing. */
                callout_reset(&sc->heartbeat_co,
                    (new * hz + 999) / 1000,
                    myfirst_heartbeat, sc);
        } else if (new == 0 && old > 0) {
                /* Disabling: cancel any pending heartbeat. */
                callout_stop(&sc->heartbeat_co);
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

The handler:

1. Reads the current value (so a read-only query returns the current interval).
2. Lets `sysctl_handle_int` validate and update the local `new` variable.
3. Validates that the new value is non-negative.
4. Acquires `sc->mtx` to commit the change atomically against any racing callout activity.
5. If the heartbeat was disabled and is now enabled, schedules the first firing.
6. If the heartbeat was enabled and is now disabled, cancels the pending callout.
7. Drops the lock and returns.

Note the symmetric handling. If the user toggles the heartbeat off and on rapidly, the handler does the right thing each time. A re-arm in the callback would not fire a new heartbeat after the user disables it (the callback checks `heartbeat_interval_ms` before re-arming). A schedule from the sysctl would not double-schedule (the callback re-arms only if `interval_ms > 0`, and the sysctl only schedules if `old == 0`).

A subtle point: the callout is initialized with `sc->mtx` as its lock, and the sysctl handler acquires `sc->mtx` before calling `callout_reset`. The kernel acquires `sc->mtx` for callbacks too. This means the sysctl handler and any in-flight callback are serialized: the sysctl waits if a callback is currently running, and the callback cannot run while the sysctl holds the lock. The race "user disables the heartbeat just as the callback re-arms" is closed by the lock.

Register the sysctl in attach:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
    OID_AUTO, "heartbeat_interval_ms",
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    sc, 0, myfirst_sysctl_heartbeat_interval_ms, "I",
    "Heartbeat interval in milliseconds (0 = disabled)");
```

And initialize `heartbeat_interval_ms = 0` in attach so the heartbeat is disabled by default. A user opts in by setting the sysctl; the driver is silent until then.

### Verifying the Refactor

Build the new driver and load it on a `WITNESS` kernel. Three tests:

**Test 1: heartbeat off by default.**

```sh
$ kldload ./myfirst.ko
$ dmesg | tail -3   # attach line shown; no heartbeat logs
$ sleep 5
$ dmesg | tail -3   # still no heartbeat logs
```

Expected: the attach line, then nothing. The heartbeat is disabled by default.

**Test 2: heartbeat on.**

```sh
$ sysctl -w dev.myfirst.0.heartbeat_interval_ms=1000
$ sleep 5
$ dmesg | tail -10
```

Expected: about five heartbeat lines, one per second:

```text
myfirst0: heartbeat: cb_used=0, bytes_read=0, bytes_written=0
myfirst0: heartbeat: cb_used=0, bytes_read=0, bytes_written=0
myfirst0: heartbeat: cb_used=0, bytes_read=0, bytes_written=0
```

**Test 3: heartbeat under load.**

In one terminal:

```sh
$ ../../part-02/ch10-handling-io-efficiently/userland/producer_consumer
```

While that runs, observe `dmesg` in another terminal. The heartbeat lines should now show non-zero byte counts:

```text
myfirst0: heartbeat: cb_used=0, bytes_read=1048576, bytes_written=1048576
```

**Test 4: disable cleanly.**

```sh
$ sysctl -w dev.myfirst.0.heartbeat_interval_ms=0
$ sleep 5
$ dmesg | tail -3   # nothing new
```

The heartbeat stops; no further lines are emitted.

**Test 5: detach with heartbeat active.**

```sh
$ sysctl -w dev.myfirst.0.heartbeat_interval_ms=1000
$ kldunload myfirst
```

Expected: detach succeeds. The drain in `myfirst_detach` cancels the pending callout and waits for any in-flight firing to complete. No `WITNESS` warnings, no panics.

If any of these tests fails, the most likely cause is either (a) the `is_attached` check is missing in the callback (so the callback re-arms during teardown and the `callout_drain` never returns) or (b) the lock initialization is wrong (so the callback runs without the expected mutex held and `MYFIRST_ASSERT` fires).

### A Note on the Heartbeat's Overhead

A 1-second heartbeat is essentially free: one callout per second, three counter reads, one log line, one re-arm. Total CPU: microseconds per firing. Memory: zero beyond the `struct callout` already in the softc.

A 1-millisecond heartbeat is a different story. A thousand log lines per second will saturate the `dmesg` buffer in seconds and dominate the driver's CPU usage. Use short intervals only when the work is genuinely fast and the log is gated by a debug level.

For demonstration purposes, `1000` (one per second) is reasonable. For a real-world heartbeat the reasonable range is probably 100 ms to 10 s. The chapter does not enforce a minimum; the user's choice is their own.

### A Mental Model: How a Heartbeat Plays Out

A step-by-step picture of what the kernel and the driver do during a single heartbeat firing. Useful for cementing the lifecycle vocabulary in concrete terms.

- **t=0**: User runs `sysctl -w dev.myfirst.0.heartbeat_interval_ms=1000`.
- **t=0+δ**: The sysctl handler runs. It reads the current value (0), validates the new value (1000), and acquires `sc->mtx`. Inside the lock: it sets `sc->heartbeat_interval_ms = 1000`. Detecting the 0 → non-zero transition, it calls `callout_reset(&sc->heartbeat_co, hz, myfirst_heartbeat, sc)`. The kernel computes the wheel bucket for "now plus 1000 ticks" and links the callout into that bucket. The handler releases `sc->mtx` and returns 0.
- **t=0 to t=1s**: The kernel does other work. The callout sits on the wheel.
- **t=1s**: The hardware-clock interrupt fires. The callout subsystem walks the current wheel bucket and finds `sc->heartbeat_co` waiting. The kernel removes it from the wheel and dispatches it.
- **t=1s+δ**: A callout-processing thread wakes (or, if the system is idle, the timer interrupt itself runs the callback). The kernel acquires `sc->mtx` (this may briefly block if another thread holds it; for our typical workload, the lock is free). Once `sc->mtx` is held, the kernel calls `myfirst_heartbeat(sc)`.
- **Inside the callback**: `MYFIRST_ASSERT(sc)` confirms the lock is held. The `is_attached` check passes. The callback reads `cbuf_used` (lock held; safe), reads the per-CPU counters (lockless; always safe), emits one `device_printf` line. It checks `sc->heartbeat_interval_ms` (1000); since it is positive, it calls `callout_reset(&sc->heartbeat_co, hz, myfirst_heartbeat, sc)` to schedule the next firing. The kernel re-links the callout into the wheel bucket for "now plus 1000 ticks". The callback returns.
- **t=1s+ε**: The kernel releases `sc->mtx`. The callout is now back on the wheel, waiting for t=2s.
- **t=2s**: The cycle repeats.

Three observations.

First, the kernel manages the lock on your behalf. Your callback runs as if some unseen caller had acquired `sc->mtx` for it. There is no `mtx_lock`/`mtx_unlock` in the callback because the kernel handles them.

Second, the re-arm is just another `callout_reset` call. It is allowed because the callout's lock is held; the kernel's internal bookkeeping handles the case "this callout is currently firing, and is being re-armed inside its own callback".

Third, the time between firings is approximately `hz` ticks, but is slightly more than that: the callback's work time plus any scheduling latency adds to the interval. For a 1-second heartbeat the drift is microseconds; for a 1-millisecond heartbeat it could be measurable. If exact period matters, use `callout_reset_sbt` and compute the next deadline as "previous deadline + interval", not "now + interval".

### Visualizing the Timer With dtrace

A useful sanity check: confirm the heartbeat fires at the rate you configured.

```sh
# dtrace -n 'fbt::myfirst_heartbeat:entry { @ = count(); } tick-1sec { printa(@); trunc(@); }'
```

This dtrace one-liner counts how many times `myfirst_heartbeat` is entered each second. With `heartbeat_interval_ms=1000`, the count should be 1 per second. With `heartbeat_interval_ms=100`, the count should be 10. With `heartbeat_interval_ms=10`, the count should be 100.

If the count is wildly off from the expected value, the configuration did not take effect. Common causes: the sysctl handler did not commit the change (bug in the handler), the callback is exiting early due to `is_attached == 0` (bug in the teardown flow), or the system is so loaded that the callout is firing late and pile-up is occurring. In normal operation the count should be stable to within one count per second.

A more elaborate dtrace recipe: histogram of the time spent in the callback.

```sh
# dtrace -n '
fbt::myfirst_heartbeat:entry { self->ts = timestamp; }
fbt::myfirst_heartbeat:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-30sec { exit(0); }'
```

Each callback typically takes a few microseconds (the time to read counters and emit a log line). If the histogram shows callbacks taking milliseconds or more, something is wrong; investigate.

### Wrapping Up Section 4

The driver has its first timer. The heartbeat callout fires periodically, logs a stat line, and re-arms. The `is_attached` flag (introduced in Chapter 12 for cv waiters) plays exactly the same role here: it lets the callback exit cleanly when the device is being torn down. The lock-aware initialization (`callout_init_mtx` with `sc->mtx`) means the callback runs with the data-path mutex held, and the kernel handles the lock acquisition for us.

Section 5 examines the lock contract more carefully. The contract is the most important rule in the callout API; getting it right makes the rest easy, and getting it wrong creates bugs that are hard to find.



## Section 5: Handling Locking and Context in Timers

Section 4 used `callout_init_mtx` and trusted the kernel to acquire `sc->mtx` before each firing. This section opens that black box. We look at exactly what the kernel does with the lock pointer you registered, what guarantees you can assume inside the callback, and what you may and may not do during a firing.

The lock contract is the most important rule in `callout(9)`. A driver that respects it is correct by construction. A driver that violates it produces races that are hard to reproduce and even harder to diagnose. Spend the time on this section now; the rest of the chapter is easier when the model is solid.

### What the Kernel Does Before Your Callback Runs

When a callout's deadline arrives, the kernel's callout-processing code (in `/usr/src/sys/kern/kern_timeout.c`) finds the callout on the wheel and prepares to fire it. The preparation depends on what lock you registered:

- **No lock (`callout_init` with `mpsafe=1`).** The kernel sets `c_iflags`, marks the callout as no longer pending, and calls your function directly. Your function must do all its own locking.

- **A mutex (`callout_init_mtx` with an `MTX_DEF` mutex).** The kernel acquires the mutex with `mtx_lock`. If the mutex is contended, the firing thread blocks until it can acquire. Once the mutex is held, the kernel calls your function. After your function returns, the kernel releases the mutex with `mtx_unlock` (unless you set `CALLOUT_RETURNUNLOCKED`).

- **An rw lock (`callout_init_rw`).** Same as the mutex case, but with `rw_wlock` (or `rw_rlock` if you set `CALLOUT_SHAREDLOCK`).

- **An rmlock (`callout_init_rm`).** Same shape with rmlock primitives.

- **Giant (the default of `callout_init` with `mpsafe=0`).** The kernel acquires Giant. Avoid this for new code.

The lock is acquired in the firing thread's context. From your callback's point of view, the lock is just held: the same invariants apply as if any other thread had called `mtx_lock` and then called your function.

### Why the Lock Is Acquired By the Kernel

A natural question: why doesn't the callback acquire the lock itself? The kernel-acquires-the-lock model has three subtle benefits.

**It cooperates correctly with `callout_drain`.** When `callout_drain` waits for an in-flight callback to finish, it must know whether a callback is currently running. The kernel's callout subsystem tracks exactly that, but only because it is the code that acquired the lock and began the callback. If the callback acquired its own lock, the subsystem would not know the difference between "callback is currently blocked trying to acquire the lock" and "callback has returned", and a clean drain would be impossible to implement without exposing kernel-private state. The kernel-acquires model keeps the subsystem firmly in control of the firing timeline.

**It enforces the lock-class rule.** The kernel checks at registration time that the lock you supply is not sleepable beyond what callouts can tolerate. A sleepable sx lock or lockmgr lock would let the callback call `cv_wait`, which in callout context is illegal. The init function (`_callout_init_lock` in `kern_timeout.c`) has the assertion: `KASSERT(lock == NULL || !(LOCK_CLASS(lock)->lc_flags & LC_SLEEPABLE), ...)` to catch this.

**It serializes the callback against `callout_reset` and `callout_stop`.** When the callback is firing, the lock is held. When you call `callout_reset` or `callout_stop` from your driver code, you must hold the same lock (the kernel checks). Therefore the cancel/reschedule and the firing are mutually exclusive: at any moment, either the callback is firing (lock held by the kernel-acquired path) or the driver code is reacting to a state change (lock held by the driver code). They never run concurrently.

This third property is what makes the heartbeat sysctl handler in Section 4 race-free. The handler acquires `sc->mtx`, decides to cancel or schedule, and the cancel/schedule completes atomically against any in-flight callback. No special precautions needed; the lock does the work.

### What You May Do Inside a Callback

The callback runs with the registered lock held. The lock determines what is legal.

For an `MTX_DEF` mutex (our case), the rules are the same as any other code that holds a sleep mutex:

- You may read and write any state protected by the mutex.
- You may call `cbuf_*` helpers and other mutex-internal operations.
- You may call `cv_signal` and `cv_broadcast` (the cv API does not require the interlock to be dropped first).
- You may call `callout_reset`, `callout_stop`, or `callout_pending` on the same callout (re-arm, cancel, or check).
- You may call `callout_reset` on a *different* callout if you hold its lock too (or if it's mpsafe).

### What You May Not Do Inside a Callback

The same rules: no sleeping while holding the mutex.

- You may **not** call `cv_wait`, `cv_wait_sig`, `mtx_sleep`, or any other sleeping primitive directly. (The mutex is held; sleeping with it held would be a sleep-with-mutex violation that `WITNESS` catches.)
- You may **not** call `uiomove`, `copyin`, or `copyout` (each may sleep).
- You may **not** call `malloc(..., M_WAITOK)`. Use `M_NOWAIT` instead, with proper error handling for the allocation failure case.
- You may **not** call `selwakeup` (it takes its own locks that may produce ordering violations).
- You may **not** call any function that may sleep.

The callback should be short. A few microseconds of work is typical. If you need to do something long-running, the callback should *enqueue* the work to a taskqueue or wake a kernel thread that can do it in process context. Chapter 16 covers the taskqueue pattern.

### What If You Do Need To Sleep?

The standard answer: do not sleep in the callback. Defer the work. Two patterns are common.

**Pattern 1: Set a flag, signal a kernel thread.** The callback sets a flag in the softc and signals a cv. A kernel thread (created by `kproc_create` or `kthread_add`, both topics for later chapters) is waiting on the cv; it wakes, does the long-running work in process context, and goes back to waiting. The callback is short; the work is unconstrained.

**Pattern 2: Enqueue a task on a taskqueue.** The callback calls `taskqueue_enqueue` to defer the work to a taskqueue worker thread. The worker runs in process context and may sleep. Again, the callback is short; the work is unconstrained. Chapter 16 introduces this in depth.

For Chapter 13 we keep all our timer work short and lock-friendly; we do not yet need to defer. The pattern is mentioned so you know the option exists.

### The CALLOUT_RETURNUNLOCKED Flag

`CALLOUT_RETURNUNLOCKED` changes the lock contract. Without it, the kernel acquires the lock before calling the callback and releases it after the callback returns. With it, the kernel acquires the lock before calling the callback and the *callback* is responsible for releasing it (or the callback may call something that drops the lock).

Why would you want this? Two reasons.

**The callback drops the lock to do something that cannot be done under it.** For example, the callback finishes its locked work, drops the lock, then enqueues a task on a taskqueue. The enqueue does not need the lock and could even violate ordering if held. Setting `CALLOUT_RETURNUNLOCKED` lets you write the drop in the natural place.

**The callback hands the lock off to another function.** If the callback calls a helper that takes ownership of the lock and is responsible for releasing it, `CALLOUT_RETURNUNLOCKED` documents the handoff to `WITNESS` so the assertion check passes.

Without `CALLOUT_RETURNUNLOCKED`, the kernel will assert that the lock is still held by the firing thread when the callback returns. The flag tells the assertion that the callback is allowed to leave the function with the lock dropped.

For Chapter 13 we do not need `CALLOUT_RETURNUNLOCKED`. All our callbacks acquire no additional locks, drop no locks, and return with the same lock state as on entry. The flag is mentioned so you recognize it in real-driver source.

### The CALLOUT_SHAREDLOCK Flag

`CALLOUT_SHAREDLOCK` is only valid for `callout_init_rw` and `callout_init_rm`. It tells the kernel to acquire the lock in shared (read) mode rather than exclusive (write) mode before calling the callback.

Used when the callback only reads state and there are many callouts that share the same lock. With `CALLOUT_SHAREDLOCK`, multiple callbacks can run concurrently as long as no writer holds the lock.

For Chapter 13 we use `callout_init_mtx` with `MTX_DEF`, where shared mode does not exist. The flag is mentioned for completeness.

### The "Direct Execution" Mode

The kernel offers a "direct" mode where the callout function runs in the timer interrupt context itself, rather than being deferred to a thread. The flag is `C_DIRECT_EXEC`, passed to `callout_reset_sbt`. It is documented in `/usr/src/sys/sys/callout.h` and only valid for callouts whose lock is a spin mutex (or no lock at all).

Direct execution is fast (no context switch, no thread wakeup) but the rules are stricter than ordinary callout context: no sleeping (already true), no acquiring sleep mutexes, no calling functions that might. The function runs in interrupt context, with all the constraints that implies (Chapter 14).

For Chapter 13 we never use `C_DIRECT_EXEC`. Our callouts are not time-critical to that degree. We mention it because you will see it in some hardware drivers (especially network drivers with hot RX paths).

### A Worked Example: Lock Contract in the Heartbeat

Recall the heartbeat callback from Section 4:

```c
static void
myfirst_heartbeat(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t used;
        uint64_t br, bw;
        int interval;

        MYFIRST_ASSERT(sc);

        if (!sc->is_attached)
                return;

        used = cbuf_used(&sc->cb);
        br = counter_u64_fetch(sc->bytes_read);
        bw = counter_u64_fetch(sc->bytes_written);
        device_printf(sc->dev,
            "heartbeat: cb_used=%zu, bytes_read=%ju, bytes_written=%ju\n",
            used, (uintmax_t)br, (uintmax_t)bw);

        interval = sc->heartbeat_interval_ms;
        if (interval > 0)
                callout_reset(&sc->heartbeat_co,
                    (interval * hz + 999) / 1000,
                    myfirst_heartbeat, sc);
}
```

Walk through the lock contract:

- The callout was initialized with `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)`. The kernel holds `sc->mtx` before calling us.
- The `MYFIRST_ASSERT(sc)` confirms `sc->mtx` is held. Sanity check.
- `sc->is_attached` is read under the lock. Safe.
- `cbuf_used(&sc->cb)` is called. The cbuf helper expects `sc->mtx` held; we have it.
- `counter_u64_fetch(sc->bytes_read)` is called. `counter(9)` is lockless and safe everywhere.
- `device_printf` is called. `device_printf` does not take any of our locks; it's safe under our mutex.
- `sc->heartbeat_interval_ms` is read under the lock. Safe.
- `callout_reset` is called to re-arm. The callout API requires the callout's lock to be held when calling `callout_reset`; we have it.

Every operation in the callback respects the lock contract. The kernel will release `sc->mtx` after the callback returns.

A specific check: the callback does *not* call anything that may sleep. `device_printf` does not sleep. `cbuf_used` does not sleep. `counter_u64_fetch` does not sleep. `callout_reset` does not sleep. The callback respects the mutex's no-sleep convention.

If we accidentally added a sleep, `WITNESS` would catch it on a debug kernel: "sleeping thread (pid X) owns a non-sleepable lock" or similar. The lesson: trust the kernel to enforce the rules; just keep the callback short.

### What Happens If Two Callouts Share a Lock

A single lock can be the interlock for many callouts. Consider:

```c
callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0);
callout_init_mtx(&sc->watchdog_co, &sc->mtx, 0);
callout_init_mtx(&sc->tick_source_co, &sc->mtx, 0);
```

Three callouts, all using `sc->mtx`. When any one of them fires, the kernel acquires `sc->mtx` and runs the callback. While that callback is running, the lock is held; no other callback (or other thread acquiring `sc->mtx`) can proceed.

This is the right pattern: the data-path mutex protects all per-softc state, and any callout that needs to read or modify that state shares the same lock. The serialization is automatic and free.

The downside: if the heartbeat callback is slow, it delays the watchdog callback. Keep callbacks short.

### What If the Callback Is Currently Firing When You Call `callout_reset`?

A subtle but important question: what happens if the callback is in the middle of executing on one CPU, and you call `callout_reset` on another CPU to reschedule it?

The kernel handles this case correctly. Let us walk through it.

The callback is firing on CPU 0. It holds `sc->mtx` (the kernel acquired it before calling). On CPU 1, you call `callout_reset(&sc->heartbeat_co, hz, fn, arg)` (perhaps because the user changed the interval). The callout API requires the caller to hold the same lock the callout uses; you do, on CPU 1.

But CPU 0 is already inside the callback, holding `sc->mtx`. Therefore CPU 1 cannot have just acquired it. Either CPU 1 acquired the lock long before CPU 0 got it (in which case CPU 0 is currently blocked waiting for the lock and is not in the callback), or CPU 1 is somehow about to acquire the lock and CPU 0 is about to release it.

The kernel handles the case correctly via the same mechanism it uses for ordinary `mtx_lock` synchronization. There is exactly one holder of `sc->mtx` at any given instant. If CPU 0 is firing, CPU 1's `callout_reset` is blocked waiting for the lock. When CPU 0's callback finishes and the kernel releases the lock, CPU 1 acquires the lock and proceeds with the reschedule. The callout is now scheduled for the new deadline.

If the callback re-armed itself before CPU 0 released the lock (the periodic pattern), the callout is currently pending. CPU 1's `callout_reset` cancels the pending and replaces with the new schedule. The return value is 1 (cancelled).

If the callback did not re-arm (one-shot, or interval was 0), the callout is idle. CPU 1's `callout_reset` schedules it. The return value is 0 (no previous schedule cancelled).

Either way, the result is correct: after `callout_reset` returns, the callout is scheduled for the new deadline, with the new function and argument.

### What If the Callback Is Currently Firing When You Call `callout_stop`?

Similar question: callback firing on CPU 0, caller on CPU 1 wants to cancel.

CPU 1 calls `callout_stop`. It needs to hold the callout's lock; it does. CPU 0 is firing the callback while holding the same lock; CPU 1's lock acquisition blocks. When CPU 0's callback returns and releases the lock, CPU 1 acquires it.

At this point, the callback may have re-armed (if it was a periodic). `callout_stop` cancels the pending schedule. Return value is 1.

If the callback did not re-arm, the callout is idle. `callout_stop` is a no-op. Return value is 0.

After `callout_stop` returns, the callout will not fire again unless something else schedules it. Importantly, the callback that was running on CPU 0 has *already finished* by the time `callout_stop` returns; the lock was held through the duration. So `callout_stop` does effectively wait for the in-flight callback, but only because of the lock-acquisition wait, not because of any explicit waiting in the callout subsystem.

This is why `callout_stop` is safe to use in normal driver operation when you hold the lock, and why `callout_drain` is needed only when you are about to free the surrounding state (where you cannot hold the lock during the wait).

### `callout_stop` From a Context Without the Lock

What if you call `callout_stop` without holding the callout's lock? The kernel's `_callout_stop_safe` function will detect the missing lock and assert (under `INVARIANTS`). On a non-`INVARIANTS` kernel, the call may produce incorrect results or race conditions.

The rule: when calling `callout_stop` or `callout_reset`, you must hold the same lock the callout was initialized with. The kernel enforces this; a violation is a `WITNESS` warning or an `INVARIANTS` panic.

For Chapter 13 we always hold `sc->mtx` when calling `callout_reset` or `callout_stop` from sysctl handlers. The detach path is the exception: it drops the lock before calling `callout_drain`. `callout_drain` does not require the lock to be held; in fact it requires it to *not* be held.

### A Pattern: Conditional Re-Arm

A useful pattern for periodic callouts: only re-arm if some condition is true. In our heartbeat:

```c
interval = sc->heartbeat_interval_ms;
if (interval > 0)
        callout_reset(&sc->heartbeat_co, ..., myfirst_heartbeat, sc);
```

The conditional re-arm gives the user fine control over the periodic firing. A user who sets `interval_ms = 0` disables the heartbeat at the next firing. The callback exits without re-arming; the callout becomes idle.

A more elaborate version: re-arm at variable intervals based on activity. A heartbeat that fires more often when the buffer is busy and less often when it is idle:

```c
if (cbuf_used(&sc->cb) > 0)
        interval = sc->heartbeat_busy_interval_ms;  /* short */
else
        interval = sc->heartbeat_idle_interval_ms;  /* long */

if (interval > 0)
        callout_reset(&sc->heartbeat_co, ..., myfirst_heartbeat, sc);
```

The variable interval lets the heartbeat sample the device adaptively. When activity is high, it fires often (catching state changes quickly); when activity is low, it fires rarely (saving CPU and log space).

### Wrapping Up Section 5

The lock contract is the heart of `callout(9)`. The kernel acquires the registered lock before each firing, runs your callback, and releases the lock after. This serializes the callback against other holders of the lock and eliminates a class of race that would otherwise require explicit handling. The rules inside the callback are the same as the lock's normal rules: for an `MTX_DEF` mutex, no sleeping, no `uiomove`, no `malloc(M_WAITOK)`. The callback should be short; if it needs to do long work, defer to a taskqueue (Chapter 16) or a kernel thread.

Reschedule and stop work correctly even when the callback is firing on another CPU; the lock-acquisition mechanism ensures atomicity. The conditional re-arm pattern (re-arm only if some condition is true) is the natural way to give a periodic callout a graceful disable path.

Section 6 deals with the corollary of all this: at unload time, you must not free the surrounding state while a callback is in progress or pending. `callout_drain` is the tool, and the unload race is the problem it solves.



## Section 6: Timer Cleanup and Resource Management

Every callout has a destruction problem. Between the moment you decide to remove the driver and the moment the surrounding memory is freed, you have to make sure no callback is running and no callback is scheduled to run. If a callback fires after the memory is freed, the kernel crashes. If a callback is running when you free the memory, the kernel crashes. The crash is reliable, immediate, and fatal; it is the kind of bug that hangs the test machine and is hard to debug because the backtrace points at code that has already been freed.

`callout(9)` provides the tools to solve this cleanly: `callout_stop` for normal cancellation, `callout_drain` for teardown, and `callout_async_drain` for the rare cases where you want to schedule cleanup without blocking. This section walks through each, names the unload race precisely, and presents the standard pattern for safe driver detach.

### The Unload Race

Imagine the driver as Stage 1 of Chapter 13 (heartbeat enabled, `kldunload` called). Without `callout_drain`, the sequence might be:

1. User runs `kldunload myfirst`.
2. The kernel calls `myfirst_detach`.
3. `myfirst_detach` clears `is_attached`, broadcasts cvs, drops the mutex, and calls `mtx_destroy(&sc->mtx)`.
4. The driver module is unloaded; the memory containing `sc->mtx`, `sc->heartbeat_co`, and `myfirst_heartbeat`'s code is freed.
5. The hardware-clock interrupt fires, the callout subsystem walks the wheel, finds `sc->heartbeat_co` (still on the wheel because we never cancelled it), and calls `myfirst_heartbeat` with `sc` as the argument.
6. `myfirst_heartbeat` is no longer in memory. The kernel jumps to a now-invalid address. Panic.

The race is not theoretical. Even if step 5 happens microseconds after step 4, the kernel still crashes. The window is small but non-zero.

The cure is to ensure that by step 4, no callout is pending and no callback is in flight. Two actions:

- **Cancel pending callouts.** If the callout is on the wheel, remove it. `callout_stop` does this.
- **Wait for in-flight callbacks.** If a callback is currently running on another CPU, wait for it to return. `callout_drain` does this.

`callout_drain` does both: it cancels pending and waits for in-flight. It is what you call at detach time.

### `callout_stop` vs `callout_drain`

The distinction is whether the call waits.

`callout_stop`: cancels pending, returns immediately. Does not wait for an in-flight callback. Returns 1 if the callout was pending and got cancelled; 0 otherwise.

`callout_drain`: cancels pending, *and* waits for any in-flight callback to return before itself returning. Returns 1 if the callout was pending and got cancelled; 0 otherwise. After `callout_drain` returns, the callout is guaranteed to be idle.

Use `callout_stop` in normal driver operation when you want to cancel the timer because the condition that motivated it has been resolved. The watchdog use case: schedule a watchdog at the start of an operation; cancel it (with `callout_stop`) when the operation completes successfully. If the watchdog is already firing on another CPU, `callout_stop` returns and the watchdog will run to completion; that is fine because the watchdog handler will see the operation has completed and do nothing (or take some recovery action that is now unnecessary but harmless).

Use `callout_drain` at detach time, where waiting is required to prevent the unload race. Do not use `callout_stop` at detach time; the callback might be running on another CPU and the surrounding memory could be freed before it returns.

### Two Critical Rules for `callout_drain`

`callout_drain` has two rules that are easy to violate.

**Rule 1: do not hold the callout's lock when calling `callout_drain`.** If the callout is currently executing, the callback is holding the lock (the kernel acquired it for the callback). `callout_drain` waits for the callback to return; the callback returns when its work is done; the work includes the lock being released. If the caller of `callout_drain` is *also* holding the lock, the caller would block waiting for itself to release it. Deadlock.

**Rule 2: `callout_drain` may sleep.** It waits on a sleep queue for the in-flight callback to finish. Therefore `callout_drain` is only legal in contexts where sleeping is allowed: process context (typical detach path) or kernel-thread context. Not interrupt context. Not while holding a spin lock. Not while holding any other non-sleepable lock.

These rules together imply that the standard detach path drops `sc->mtx` (and any other non-sleepable lock) before calling `callout_drain`. The chapter's detach pattern follows this:

```c
MYFIRST_LOCK(sc);
sc->is_attached = 0;
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
MYFIRST_UNLOCK(sc);    /* drop the mutex before draining */

seldrain(&sc->rsel);
seldrain(&sc->wsel);

callout_drain(&sc->heartbeat_co);   /* now safe to call */
```

The mutex is dropped after clearing `is_attached`. The `callout_drain` runs without the mutex held; it is free to wait on a sleep queue. Any callback that fires during the drain sees `is_attached == 0` and exits without re-arming. After the drain, the callout is idle.

### The `is_attached` Pattern, Revisited

In Chapter 12 we used `is_attached` as a signal to cv waiters: "the device is going away; return ENXIO". In Chapter 13 we use it for the same purpose with callouts: "the device is going away; do not re-arm".

The pattern is identical:

```c
static void
myfirst_some_callback(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);

        if (!sc->is_attached)
                return;  /* device going away; do not re-arm */

        /* ... do the work ... */

        /* re-arm if periodic */
        if (some_condition)
                callout_reset(&sc->some_co, ticks, myfirst_some_callback, sc);
}
```

The check is at the top, before any work. If `is_attached == 0`, the callback exits immediately without doing work and without re-arming. The drain in detach will see the callout idle (no pending firing) and complete cleanly.

A subtle point: the check happens *under the lock* (the kernel acquired it for us). The detach path clears `is_attached` *under the lock*. So the callback always sees the current value of `is_attached`; there is no race. This is the same property we relied on in Chapter 12 for cv waiters.

### Why Not `callout_stop` Instead?

A natural question: instead of `callout_drain`, why not `callout_stop` followed by some manual wait?

The implementation of `callout_drain` (in `_callout_stop_safe` in `/usr/src/sys/kern/kern_timeout.c`) does exactly that, but inside the kernel where it can use internal sleep queues without exposing them. Trying to do the same thing in driver code is fragile: you would need to know whether the callback is currently running, which you cannot tell from outside without inspecting kernel-private fields.

Just call `callout_drain`. It is what the API is for.

### `callout_async_drain`

For the rare case where you want to drain without blocking, the kernel offers `callout_async_drain`:

```c
#define callout_async_drain(c, d) _callout_stop_safe(c, 0, d)
```

It cancels pending and arranges for a "drain done" callback (the `d` function pointer) to be called when the in-flight callback finishes. The caller does not block; control returns immediately. Useful in contexts where you cannot sleep but need to know when the drain has completed.

For the chapter's purposes, `callout_async_drain` is overkill. We do detach in process context where blocking is fine. We mention it because you will see it in some real-driver source.

### The Standard Detach Pattern with Timers

Putting it all together, the canonical detach pattern for a driver with one or more callouts:

> **Reading this example.** The listing below is a composite view of the canonical `callout(9)` teardown sequence, distilled from real drivers such as `/usr/src/sys/dev/re/if_re.c` (where `callout_drain(&sc->rl_stat_callout)` runs at detach time) and `/usr/src/sys/dev/watchdog/watchdog.c` (where two callouts are drained in sequence). We have kept the phase ordering, the mandatory `callout_drain()` calls, and the lock discipline intact; a production driver adds per-device bookkeeping that the real detach function interleaves with each step. Every symbol the listing names, from `callout_drain` to `seldrain` to `mtx_destroy`, is a real FreeBSD API; the `myfirst_softc` fields are this chapter's evolving driver.

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* 1. Refuse detach if the device is in use. */
        MYFIRST_LOCK(sc);
        if (sc->active_fhs > 0) {
                MYFIRST_UNLOCK(sc);
                return (EBUSY);
        }

        /* 2. Mark the device as going away. */
        sc->is_attached = 0;
        cv_broadcast(&sc->data_cv);
        cv_broadcast(&sc->room_cv);
        MYFIRST_UNLOCK(sc);

        /* 3. Drain the selinfo readiness machinery. */
        seldrain(&sc->rsel);
        seldrain(&sc->wsel);

        /* 4. Drain every callout. Each takes its own line. */
        callout_drain(&sc->heartbeat_co);
        callout_drain(&sc->watchdog_co);
        callout_drain(&sc->tick_source_co);

        /* 5. Destroy cdevs (no new opens after this). */
        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }

        /* 6. Free other resources. */
        sysctl_ctx_free(&sc->sysctl_ctx);
        cbuf_destroy(&sc->cb);
        counter_u64_free(sc->bytes_read);
        counter_u64_free(sc->bytes_written);

        /* 7. Destroy primitives in reverse acquisition order:
         *    cvs first, then sx, then mutex. */
        cv_destroy(&sc->data_cv);
        cv_destroy(&sc->room_cv);
        sx_destroy(&sc->cfg_sx);
        mtx_destroy(&sc->mtx);

        return (0);
}
```

Seven phases. Each one is a hard requirement. Let us walk through them.

**Phase 1**: refuse detach while the device is in use (`active_fhs > 0`). Without this, a user with the device open could close their descriptor in the middle of detach, hitting code paths that no longer have valid state.

**Phase 2**: mark the device as going away. The `is_attached` flag is the signal to every blocked or future code path that the device is being removed. The cv broadcasts wake any cv waiters; they re-check `is_attached` and exit with `ENXIO`. The lock is held during this phase to make the change atomic against any thread that just entered a handler.

**Phase 3**: drain `selinfo`. This ensures that `selrecord(9)` and `selwakeup(9)` callers no longer reference the device's selinfo structures.

**Phase 4**: drain every callout. Each `callout_drain` cancels pending and waits for in-flight. The mutex is dropped before the first drain (it was dropped at the end of phase 2). After phase 4, no callout can be running.

**Phase 5**: destroy cdevs. After this, no new `open(2)` can reach the driver. (The ones that snuck in just before would have already been refused in phase 1, but that's the safety net.)

**Phase 6**: free auxiliary resources (sysctl context, cbuf, counters).

**Phase 7**: destroy primitives in reverse order. The order matters for the same reason discussed in Chapter 12: cvs use the mutex as their interlock; if we destroy the mutex first, a callback in the middle of releasing the mutex would crash.

This is a lot. It is also what every driver has to do if it has callouts and primitives. The companion source for Chapter 13 (`stage4-final/myfirst.c`) follows this pattern exactly.

### A Note on Kernel-Module Unload

`kldunload myfirst` triggers the detach path through the kernel's module-event handling. The `MOD_UNLOAD` event causes the kernel to call the driver's detach function. If the detach function returns an error (typically `EBUSY`), the unload fails and the module remains loaded.

The standard pattern we just walked through returns `EBUSY` if `active_fhs > 0`. A user who wants to unload the driver must first close every open descriptor. From a shell:

```sh
# List processes holding the device open.
$ fstat | grep myfirst
USER     CMD          PID    FD     ... NAME
root     cat        12345     3     ... /dev/myfirst
$ kill 12345
$ kldunload myfirst
```

This is conventional UNIX behavior; the user is expected to close descriptors before unloading. The driver enforces it.

### Initializing After Drain

A subtle point: after `callout_drain`, the callout is idle but is *not* in the same state as a freshly-initialized callout. The `c_func` and `c_arg` fields still point at the last callback and argument, in case a later `callout_schedule` wants to reuse them. The internal flags are cleared.

If you wanted to reuse the same `struct callout` for a different purpose (different lock, different callback signature), you would need to call `callout_init_mtx` (or one of the variants) again to re-initialize. In the detach path, we never re-init; the surrounding memory is about to be freed. The state at drain time is sufficient.

### A Worked Walkthrough: Catching the Unload Race in DDB

To make the unload race visceral, walk through what happens when a careless driver omits `callout_drain` and the next callout firing crashes the kernel.

Imagine a buggy driver that disables the heartbeat sysctl in detach but does not call `callout_drain`. The detach path looks like this:

```c
static int
buggy_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        MYFIRST_LOCK(sc);
        sc->is_attached = 0;
        sc->heartbeat_interval_ms = 0;  /* hope the callback won't re-arm */
        MYFIRST_UNLOCK(sc);

        /* No callout_drain here! */

        destroy_dev(sc->cdev);
        mtx_destroy(&sc->mtx);
        return (0);
}
```

The `is_attached = 0` and `heartbeat_interval_ms = 0` are intended to make the callback exit without re-arming. But:

- The callback might be already in the middle of executing when detach starts. The lock is held by the kernel-acquired path. The detach path's `MYFIRST_LOCK(sc)` blocks until the callback releases the lock. Once the lock is acquired by detach, `is_attached` and `heartbeat_interval_ms` are set. The detach drops the lock. So far so good.

- *But*: the callback that was just running has already entered the re-arm path before checking `interval_ms`. It calls `callout_reset` to schedule the next firing, with the just-cleared `interval_ms` value of 0... no, wait, the callback re-reads `sc->heartbeat_interval_ms`, sees 0, and does not re-arm. OK, that case is safe.

- *Or*: the callback completed cleanly with no re-arm. The callout is now idle. The detach path proceeds. It destroys `sc->mtx` and the surrounding state. Everything seems fine.

- *Then*: a different invocation of the callback starts firing. The callout was not on the wheel (no re-arm), so this should not happen, right?

It can happen if there was a concurrent firing on a different CPU. Imagine: the callout fires on CPU 0 and CPU 1 in close succession. CPU 0 starts the callback (acquires the lock). CPU 1 enters the firing path, tries to acquire the lock, blocks. CPU 0 finishes the callback and re-arms (puts the callout back on the wheel for the next firing). CPU 0 releases the lock. CPU 1 acquires the lock and runs the callback. The callback re-arms. CPU 1 releases the lock.

Now suppose the detach path runs between the CPU-0 release and the CPU-1 acquisition. The detach takes the lock (which is now free), clears the flags, drops the lock. CPU 1 acquires the lock and calls the callback. The callback re-reads the flags, sees the cleared values, and exits without re-arming. OK, still safe.

But now consider: the detach path has destroyed the mutex. CPU 1's callback execution is done. The kernel releases the now-destroyed mutex. The release operation operates on freed memory. Panic.

This is the unload race. The fix is straightforward but absolutely required: call `callout_drain(&sc->heartbeat_co)` after dropping the mutex and before destroying primitives. The drain waits for all in-flight callbacks (on any CPU) to return before it itself returns.

Walk through with the drain in place:

- Detach acquires the lock, clears flags, drops the lock.
- Detach calls `callout_drain(&sc->heartbeat_co)`. The drain notices any in-flight callback and waits.
- All callbacks that were firing return cleanly (they re-read the flags, exit without re-arming).
- The drain returns.
- Detach destroys the cdev, then destroys the mutex.
- No callback can be running at this point. No callback can fire later because the wheel does not have the callout.

The drain is the safety net. Skipping it produces a panic that may not happen on every unload, but will happen eventually under load. The drain is mandatory.

### What Happens If You Forget Drain on a Production Kernel

A production kernel without `INVARIANTS` or `WITNESS` does not catch the unload race in advance. The first time a callout fires after the freed module's memory has been reused, the kernel reads garbage instructions, jumps to a random location, and crashes with whatever pattern the random bytes happen to produce. The crash backtrace points at code that was never the bug; the actual bug is several seconds in the past, in the detach path that did not drain.

This is exactly why the standard advice is "test on a debug kernel before promoting to production". `WITNESS` catches some forms of the race (it warns about callbacks being called with non-sleepable locks held in unexpected ways); `INVARIANTS` catches some others (the `mtx_destroy` of a destroyed mutex). The production kernel sees only the panic and the wrong backtrace.

### What `callout_drain` Returns

`callout_drain` returns the same value as `callout_stop`: 1 if a pending callout was cancelled, 0 if not. Callers usually do not look at the return value; the function is called for its side effect (waiting for in-flight callbacks to finish).

If you want to be sure that the callout is fully idle after a particular code path completed, the discipline is: call `callout_drain` and ignore the return value. Whether the callout was pending or not, after the drain it is idle.

### Detach Order with Multiple Callouts

If your driver has three callouts (heartbeat, watchdog, tick source) and you `callout_drain` each in turn, the total wait time is at most the longest of any single in-flight callback (not the sum). The drains are independent: each waits for its own callback. They can effectively run in parallel because each only blocks on its specific callout.

For the chapter's pseudo-device, callbacks are short (microseconds). The drain time is dominated by the cost of the wakeup on the sleep queue, not by the callback work. In total, all three drains complete in much less than a millisecond, even under load.

For drivers with longer callback work, the wait time can be longer. A watchdog callback that takes 10 ms means the worst-case drain is 10 ms (if you happened to call `callout_drain` while it was firing). Most of the time the callout is idle and the drain is instant. Either way, the drain is bounded; it does not loop indefinitely.

### The Same Bug, Different Primitive: A Taskqueue Sketch

The "callback ran after detach" bug is not unique to callouts. Every kernel deferred-work primitive has the same pitfall, and the standard answer is always a drain routine that waits for in-flight callbacks to finish. A brief sibling walkthrough drives the point home without pulling Chapter 13 off-topic.

Suppose a driver enqueues work on a taskqueue instead of using a callout. The softc holds a `struct task` and a `struct taskqueue *`, and something in the driver calls `taskqueue_enqueue(sc->tq, &sc->work)` when work is needed. Now imagine a buggy detach that clears `is_attached` and tears down the softc but forgets to drain the task:

```c
static int
buggy_tq_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        MYFIRST_LOCK(sc);
        sc->is_attached = 0;
        MYFIRST_UNLOCK(sc);

        /* No taskqueue_drain here! */

        destroy_dev(sc->cdev);
        free(sc->buf, M_MYFIRST);
        mtx_destroy(&sc->mtx);
        return (0);
}
```

The outcome is the same shape as the callout case. If the task was pending when detach ran, the worker thread pops it after detach has freed `sc->buf` and destroyed `sc->mtx`. The task handler dereferences `sc`, finds stale memory, and either reads garbage or panics on the first locked operation. If the task was already running on another CPU, the worker thread is still inside the handler when detach frees the memory underneath it, with the same ending.

The fix is structurally identical to `callout_drain`:

```c
taskqueue_drain(sc->tq, &sc->work);
```

`taskqueue_drain(9)` waits until the specified task is neither pending nor currently executing on any worker thread. After it returns, that task cannot fire again unless something re-enqueues it, which is exactly what detach is trying to prevent by clearing `is_attached` first. For drivers that use many tasks on the same queue, `taskqueue_drain_all(9)` waits for every task currently queued or running on that taskqueue, which is the usual call in a module-unload path where nothing on the queue will be re-enqueued.

The takeaway is not a new rule but a wider one: any deferred-work primitive in the kernel, whether `callout(9)`, `taskqueue(9)`, or the network-stack epoch callbacks you will meet in Part 6, needs a corresponding drain before the memory it reads is freed. Chapter 16 walks through `taskqueue(9)` in depth, including how drain interacts with task enqueue ordering; for now, remember that the mental model is identical. Clear the flag, drop the lock, drain the primitive, destroy the storage. The word changes with the primitive, but the shape of the pattern does not.

### Wrapping Up Section 6

The unload race is real. `callout_drain` is the cure. The standard detach pattern is: refuse if busy, clear `is_attached` under the lock, broadcast cvs, drop the lock, drain selinfo, drain every callout, destroy cdevs, free auxiliary resources, destroy primitives in reverse order. Every phase is necessary; skipping any of them creates a race that crashes the kernel under load.

Section 7 puts the framework to work on real timer use cases: watchdogs, debouncing, periodic tick sources.



## Section 7: Use Cases and Extensions for Timed Work

Sections 4 through 6 introduced the heartbeat callout: periodic, lock-aware, drained at teardown. The same pattern handles a wide range of real driver problems with small variations. This section walks through three more callouts that we add to `myfirst`: a watchdog that detects buffer staleness, a tick source that injects synthetic events, and (briefly) a debounce shape used in many hardware drivers. Together with the heartbeat, the four cover the bulk of what driver timers are used for in practice.

Treat this section as a recipe collection. Each subsection is a self-contained pattern that you can lift into other drivers.

### Pattern 1: A Watchdog Timer

A watchdog detects a stuck condition and acts on it. The classic shape: schedule a callout at the start of an operation; if the operation completes successfully, cancel the callout; if the callout fires, the operation is presumed stuck and the driver takes recovery action.

For `myfirst`, a useful watchdog is "the buffer has not made progress for too long". If `cb_used > 0` and the value has not changed for N seconds, no reader is draining the buffer. This is unusual; we will log a warning.

Add fields to the softc:

```c
struct callout          watchdog_co;
int                     watchdog_interval_ms;   /* 0 = disabled */
size_t                  watchdog_last_used;
```

`watchdog_interval_ms` is a sysctl tunable. `watchdog_last_used` records the value of `cbuf_used` from the previous tick; the next tick compares.

Initialize in attach:

```c
callout_init_mtx(&sc->watchdog_co, &sc->mtx, 0);
sc->watchdog_interval_ms = 0;
sc->watchdog_last_used = 0;
```

Drain in detach:

```c
callout_drain(&sc->watchdog_co);
```

The callback:

```c
static void
myfirst_watchdog(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t used;
        int interval;

        MYFIRST_ASSERT(sc);

        if (!sc->is_attached)
                return;

        used = cbuf_used(&sc->cb);
        if (used > 0 && used == sc->watchdog_last_used) {
                device_printf(sc->dev,
                    "watchdog: buffer has %zu bytes, no progress in last "
                    "interval; reader stuck?\n", used);
        }
        sc->watchdog_last_used = used;

        interval = sc->watchdog_interval_ms;
        if (interval > 0)
                callout_reset(&sc->watchdog_co,
                    (interval * hz + 999) / 1000,
                    myfirst_watchdog, sc);
}
```

The structure mirrors the heartbeat: assert, check `is_attached`, do work, re-arm if interval is non-zero. The work this time is the staleness check: compare current `cbuf_used` to last recorded; if they match and are non-zero, no progress has been made.

The sysctl handler is symmetric to the heartbeat's:

```c
static int
myfirst_sysctl_watchdog_interval_ms(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error;

        old = sc->watchdog_interval_ms;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (new < 0)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        sc->watchdog_interval_ms = new;
        if (new > 0 && old == 0) {
                sc->watchdog_last_used = cbuf_used(&sc->cb);
                callout_reset(&sc->watchdog_co,
                    (new * hz + 999) / 1000,
                    myfirst_watchdog, sc);
        } else if (new == 0 && old > 0) {
                callout_stop(&sc->watchdog_co);
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

The only addition: when enabling, we initialize `watchdog_last_used` to the current `cbuf_used`, so the first comparison has a sensible baseline.

To test: enable the watchdog with a 2-second interval, write some bytes into the buffer, and do not read them. After two seconds, `dmesg` should show the watchdog warning.

```sh
$ sysctl -w dev.myfirst.0.watchdog_interval_ms=2000
$ printf 'hello' > /dev/myfirst
$ sleep 5
$ dmesg | tail
myfirst0: watchdog: buffer has 5 bytes, no progress in last interval; reader stuck?
myfirst0: watchdog: buffer has 5 bytes, no progress in last interval; reader stuck?
```

Now drain the buffer:

```sh
$ cat /dev/myfirst
hello
```

The watchdog stops warning because `cbuf_used` is now zero (the comparison `used > 0` fails).

This is a contrived watchdog. Real watchdogs do more: reset a hardware engine, kill a stuck request, log to the kernel ringbuffer with a specific format that monitoring tools can grep. The shape is the same: detect, act, re-arm.

### Pattern 2: A Tick Source for Synthetic Events

A tick source is a callout that periodically generates events as if the hardware did. Useful for drivers that simulate something or that want a stable test workload independent of user-space activity.

For `myfirst`, a tick source can periodically write a single byte into the cbuf. With the heartbeat enabled, the byte counts will rise visibly without any external producer.

Add fields:

```c
struct callout          tick_source_co;
int                     tick_source_interval_ms;  /* 0 = disabled */
char                    tick_source_byte;          /* the byte to write */
```

Initialize in attach:

```c
callout_init_mtx(&sc->tick_source_co, &sc->mtx, 0);
sc->tick_source_interval_ms = 0;
sc->tick_source_byte = 't';
```

Drain in detach:

```c
callout_drain(&sc->tick_source_co);
```

The callback:

```c
static void
myfirst_tick_source(void *arg)
{
        struct myfirst_softc *sc = arg;
        size_t put;
        int interval;

        MYFIRST_ASSERT(sc);

        if (!sc->is_attached)
                return;

        if (cbuf_free(&sc->cb) > 0) {
                put = cbuf_write(&sc->cb, &sc->tick_source_byte, 1);
                if (put > 0) {
                        counter_u64_add(sc->bytes_written, put);
                        cv_signal(&sc->data_cv);
                        /* selwakeup omitted on purpose: it may sleep
                         * and we are inside a callout context with the
                         * mutex held. Defer to a taskqueue if real-time
                         * poll(2) wakeups are needed. */
                }
        }

        interval = sc->tick_source_interval_ms;
        if (interval > 0)
                callout_reset(&sc->tick_source_co,
                    (interval * hz + 999) / 1000,
                    myfirst_tick_source, sc);
}
```

The structure is the same as the heartbeat. The work is different: write one byte to the cbuf, increment the counter, signal `data_cv` so any reader wakes.

Note the deliberate omission of `selwakeup` from the callback. `selwakeup` may sleep and may take other locks, which is illegal under our mutex. Calling it from a callout context with the mutex held would be a `WITNESS` violation. The cv_signal is enough to wake blocking readers; `poll(2)` waiters will not be woken in real time, but they will pick up the next state change at their normal poll interval. For a real driver that needs immediate `poll(2)` wakeups from a callout, the answer is to defer the `selwakeup` to a taskqueue (Chapter 16). For Chapter 13, omitting it is acceptable.

A sysctl handler enables and disables, mirroring the others:

```c
static int
myfirst_sysctl_tick_source_interval_ms(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error;

        old = sc->tick_source_interval_ms;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (new < 0)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        sc->tick_source_interval_ms = new;
        if (new > 0 && old == 0)
                callout_reset(&sc->tick_source_co,
                    (new * hz + 999) / 1000,
                    myfirst_tick_source, sc);
        else if (new == 0 && old > 0)
                callout_stop(&sc->tick_source_co);
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

To test:

```sh
$ sysctl -w dev.myfirst.0.tick_source_interval_ms=100
$ cat /dev/myfirst
ttttttttttttttttttttttttttttttt    # ten 't's per second
^C
$ sysctl -w dev.myfirst.0.tick_source_interval_ms=0
```

The tick source produces ten 't' characters per second, which `cat` reads and prints. Disable by setting the sysctl back to zero.

### Pattern 3: A Debounce Shape

A debounce ignores rapid repeated events. The shape: when an event arrives, check whether a "debounce timer" is already pending; if so, ignore the event; if not, schedule a debounce timer for N milliseconds, and act on the event when the timer fires.

For `myfirst`, we do not have a hardware event source, so we will not implement a full debounce. The shape, in pseudo-code:

```c
static void
some_event_callback(struct myfirst_softc *sc)
{
        MYFIRST_LOCK(sc);
        sc->latest_event_time = ticks;
        if (!callout_pending(&sc->debounce_co)) {
                callout_reset(&sc->debounce_co,
                    DEBOUNCE_DURATION_TICKS,
                    myfirst_debounce_handler, sc);
        }
        MYFIRST_UNLOCK(sc);
}

static void
myfirst_debounce_handler(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        /* Act on the latest event seen. */
        process_event(sc, sc->latest_event_time);
        /* Do not re-arm; one-shot. */
}
```

When the first event arrives, the debounce timer is scheduled. Subsequent events update the recorded "latest event time" but do not re-schedule the timer (because it is still pending). When the debounce timer fires, the handler processes the most recent event. After the handler returns, the timer is no longer pending; the next event will reschedule.

This is a one-shot pattern, not periodic. The callback does not re-arm. The `callout_pending` check in `some_event_callback` is the gate.

Lab 13.5 walks through implementing a similar debounce as a stretch exercise. The chapter does not add it to `myfirst` because we have no hardware event to debounce, but the shape is one to remember.

### Pattern 4: A Retry With Exponential Backoff

A retry-with-backoff shape: an operation fails; schedule a retry after N milliseconds; if the retry also fails, schedule the next retry after 2N milliseconds; and so on, capped at some maximum.

For `myfirst`, no operations fail in a way that demands retry. The shape:

```c
struct callout          retry_co;
int                     retry_attempt;          /* 0, 1, 2, ... */
int                     retry_base_ms;          /* base interval */
int                     retry_max_attempts;     /* cap */

static void
some_operation_failed(struct myfirst_softc *sc)
{
        int next_delay_ms;

        MYFIRST_LOCK(sc);
        if (sc->retry_attempt < sc->retry_max_attempts) {
                next_delay_ms = sc->retry_base_ms * (1 << sc->retry_attempt);
                callout_reset(&sc->retry_co,
                    (next_delay_ms * hz + 999) / 1000,
                    myfirst_retry, sc);
                sc->retry_attempt++;
        } else {
                /* Give up. */
                device_printf(sc->dev, "retry: exhausted attempts; failing\n");
                some_failure_action(sc);
        }
        MYFIRST_UNLOCK(sc);
}

static void
myfirst_retry(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        if (some_operation(sc)) {
                /* success */
                sc->retry_attempt = 0;
        } else {
                /* failure: schedule next retry */
                some_operation_failed(sc);
        }
}
```

The callback retries the operation. Success resets the attempt counter. Failure schedules the next retry with an exponentially-increasing delay, capped at `retry_max_attempts`.

This pattern is in many real drivers, particularly storage and network drivers handling transient hardware errors. Chapter 13 does not add it to `myfirst` because we have no failures to retry. The shape is in your toolbox.

### Pattern 5: A Deferred Reaper

A deferred reaper is a one-shot callout that frees something after a grace period. Used when an object cannot be freed immediately because some other code path may still hold a reference, but we know that after some time has passed, all references will have drained.

The shape, sketched as pseudo-code (the `some_object` type stands in for whatever deferred-free object your driver actually uses):

```c
struct some_object {
        TAILQ_ENTRY(some_object) link;
        /* ... per-object fields ... */
};

TAILQ_HEAD(some_object_list, some_object);

struct myfirst_softc {
        /* ... existing fields ... */
        struct callout           reaper_co;
        struct some_object_list  pending_free;
        /* ... */
};

static void
schedule_free(struct myfirst_softc *sc, struct some_object *obj)
{
        MYFIRST_LOCK(sc);
        TAILQ_INSERT_TAIL(&sc->pending_free, obj, link);
        if (!callout_pending(&sc->reaper_co))
                callout_reset(&sc->reaper_co, hz, myfirst_reaper, sc);
        MYFIRST_UNLOCK(sc);
}

static void
myfirst_reaper(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct some_object *obj, *tmp;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        TAILQ_FOREACH_SAFE(obj, &sc->pending_free, link, tmp) {
                TAILQ_REMOVE(&sc->pending_free, obj, link);
                free(obj, M_DEVBUF);
        }

        /* Do not re-arm; new objects scheduled later will re-arm us. */
}
```

The reaper runs once a second (or whatever interval makes sense), frees everything on the pending list, and stops. New scheduling adds to the list and re-arms only if the reaper is not currently pending.

Used in network drivers where receive buffers cannot be freed immediately because the network layer still has references; the buffer is queued for the reaper, which frees it after a grace period.

`myfirst` does not need this pattern. It is in the toolbox.

### Pattern 6: A Polling Loop Replacement

Some hardware does not interrupt for events the driver cares about. A typical example: a sensor that has a status register the driver must check every few milliseconds to learn about new readings. Without callouts, the driver would either spin (waste CPU) or run a kernel thread that sleeps and polls (waste a thread). With callouts, the polling loop is a periodic callback that reads the register, takes appropriate action, and re-arms.

```c
static void
myfirst_poll(void *arg)
{
        struct myfirst_softc *sc = arg;
        uint32_t status;
        int interval;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        status = bus_read_4(sc->res, REG_STATUS);   /* hypothetical */
        if (status & STATUS_DATA_READY) {
                /* Pull data from the device into the cbuf. */
                myfirst_drain_hardware(sc);
        }
        if (status & STATUS_ERROR) {
                /* Recover from the error. */
                myfirst_handle_error(sc);
        }

        interval = sc->poll_interval_ms;
        if (interval > 0)
                callout_reset(&sc->poll_co,
                    (interval * hz + 999) / 1000,
                    myfirst_poll, sc);
}
```

The callback reads a hardware register (not implemented in our pseudo-device, but the shape is clear), checks bits, acts, and re-arms. The interval determines how often the driver checks; shorter means more responsive but more CPU. Real polling drivers typically use 1-10 ms intervals when active; longer when idle.

The code comment about `bus_read_4` is a forward reference to Chapter 19, which introduces bus space access. For Chapter 13, treat it as pseudo-code that demonstrates the pattern; the polling logic is what matters.

### Pattern 7: A Statistics Window

A periodic callout that takes a snapshot of internal counters at regular intervals and computes per-interval rates. Useful for monitoring; the driver can answer "how many bytes per second am I currently moving?" without the user having to sample manually.

```c
struct myfirst_stats_window {
        uint64_t        last_bytes_read;
        uint64_t        last_bytes_written;
        uint64_t        rate_bytes_read;       /* bytes/sec, latest interval */
        uint64_t        rate_bytes_written;
};

struct myfirst_softc {
        /* ... existing fields ... */
        struct callout                  stats_window_co;
        int                             stats_window_interval_ms;
        struct myfirst_stats_window     stats_window;
        /* ... */
};

static void
myfirst_stats_window(void *arg)
{
        struct myfirst_softc *sc = arg;
        uint64_t cur_br, cur_bw;
        int interval;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        cur_br = counter_u64_fetch(sc->bytes_read);
        cur_bw = counter_u64_fetch(sc->bytes_written);
        interval = sc->stats_window_interval_ms;

        if (interval > 0) {
                /* bytes-per-second over this interval */
                sc->stats_window.rate_bytes_read = (cur_br -
                    sc->stats_window.last_bytes_read) * 1000 / interval;
                sc->stats_window.rate_bytes_written = (cur_bw -
                    sc->stats_window.last_bytes_written) * 1000 / interval;
        }

        sc->stats_window.last_bytes_read = cur_br;
        sc->stats_window.last_bytes_written = cur_bw;

        if (interval > 0)
                callout_reset(&sc->stats_window_co,
                    (interval * hz + 999) / 1000,
                    myfirst_stats_window, sc);
}
```

Expose the rates as sysctls. A user can `sysctl dev.myfirst.0.stats.rate_bytes_read` and see the per-interval rate, computed live without having to manually sample and difference.

This pattern is in many monitoring-friendly drivers. The granularity (the interval) is configurable; longer intervals smooth out short bursts; shorter intervals respond more quickly. Choose to match what the user wants to measure.

### Pattern 8: A Timed Status Refresh

A periodic callout that refreshes a cached value the rest of the driver reads. Useful when the underlying value is expensive to compute every time but acceptable to be slightly stale.

For our `myfirst`, we do not have an expensive computation to cache. The shape, in pseudo-code:

```c
static void
myfirst_refresh_status(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        sc->cached_status = expensive_compute(sc);
        callout_reset(&sc->refresh_co, hz, myfirst_refresh_status, sc);
}

/* Other code reads sc->cached_status freely; it may be up to 1s stale. */
```

Used in drivers where the computation is expensive (parsing a hardware status table, communicating with a remote subsystem) but the consumer can tolerate stale values. The callback runs periodically and refreshes; the consumer gets the cached value.

`myfirst` does not need this pattern. It is in your toolbox.

### Pattern 9: A Periodic Reset

Some hardware needs a periodic reset (a write to a specific register) to keep an internal watchdog from triggering. The pattern:

```c
static void
myfirst_periodic_reset(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        bus_write_4(sc->res, REG_KEEPALIVE, KEEPALIVE_VALUE);
        callout_reset(&sc->keepalive_co, hz / 2,
            myfirst_periodic_reset, sc);
}
```

The hardware expects the keepalive write at least every second; we send it every 500 ms to have margin. If we miss a few writes (system load, reschedule), the hardware does not panic.

Used in storage controllers, network controllers, and embedded systems where the device has a per-side watchdog the host driver must satisfy.

### Combining Patterns

A driver typically uses several callouts at once. `myfirst` (Stage 4 of this chapter) uses three: heartbeat, watchdog, tick source. Each has its own callout and its own sysctl tunable. They share the same lock (`sc->mtx`), which means only one fires at a time; serialization is automatic.

In a more complex driver, you might have ten or twenty callouts, each for a specific purpose. The pattern scales: each callout has its own struct callout, its own callback, its own sysctl (if user-facing), and its own line in the detach `callout_drain` block. The disciplines from this chapter (lock-aware init, `is_attached` check, drain at detach) apply to every one of them.

### Wrapping Up Section 7

Nine patterns cover most of what driver timers do: heartbeat, watchdog, debounce, retry-with-backoff, deferred reaper, statistics rollover, polling loop, statistics window, timed status refresh, and periodic reset. Each is a small variation on the periodic or one-shot shape. The disciplines from Sections 4 through 6 (lock-aware initialization, `is_attached` check, drain at detach) apply uniformly. A driver that adds new timers follows the same recipe; the surface area expands without the maintenance burden growing.

Section 8 closes the chapter with the housekeeping pass: documentation, version bump, regression test, pre-commit checklist.



## Section 8: Refactoring and Versioning Your Timer-Enhanced Driver

The driver now has three callouts (heartbeat, watchdog, tick source), four sysctls (the three intervals plus the existing config), and a detach path that drains every callout safely. The remaining work is the housekeeping pass: tidying the source for clarity, updating the documentation, bumping the version, running static analysis, and verifying the regression suite passes.

This section follows the same shape as the equivalent sections in Chapters 11 and 12. None of it is glamorous. All of it is what makes the difference between a driver delivered once and a driver that keeps working as it grows.

### Cleaning Up the Source

After this chapter's focused additions, three small reorganizations are worth doing.

**Group callout-related code.** Move all the callout callbacks (`myfirst_heartbeat`, `myfirst_watchdog`, `myfirst_tick_source`) into a single section of the source file, after the wait helpers and before the cdevsw handlers. Move the corresponding sysctl handlers next to them. The compiler does not care about ordering; the reader does.

**Standardise macro vocabulary.** Add a small set of macros to make callout operations consistent across the driver. The existing pattern with `MYFIRST_LOCK` and `MYFIRST_CFG_*` extends naturally:

```c
#define MYFIRST_CO_INIT(sc, co)  callout_init_mtx((co), &(sc)->mtx, 0)
#define MYFIRST_CO_DRAIN(co)     callout_drain((co))
```

The `MYFIRST_CO_INIT` macro takes `sc` explicitly so it works in any function, not just those where a local variable named `sc` happens to be in scope. `MYFIRST_CO_DRAIN` only needs the callout itself, because draining does not require the softc.

The macros are thin, but they document the convention: every callout in the driver uses `sc->mtx` as its lock and is drained at detach. A future maintainer who adds a callout sees the macro and knows the rule.

**Comment the detach order.** The detach function is short on its own but the order of operations is critical. Add comments at each phase:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* Phase 1: refuse if in use. */
        MYFIRST_LOCK(sc);
        if (sc->active_fhs > 0) {
                MYFIRST_UNLOCK(sc);
                return (EBUSY);
        }

        /* Phase 2: signal "going away" to all waiters and callbacks. */
        sc->is_attached = 0;
        cv_broadcast(&sc->data_cv);
        cv_broadcast(&sc->room_cv);
        MYFIRST_UNLOCK(sc);

        /* Phase 3: drain selinfo. */
        seldrain(&sc->rsel);
        seldrain(&sc->wsel);

        /* Phase 4: drain every callout (no lock held; safe to sleep). */
        MYFIRST_CO_DRAIN(&sc->heartbeat_co);
        MYFIRST_CO_DRAIN(&sc->watchdog_co);
        MYFIRST_CO_DRAIN(&sc->tick_source_co);

        /* Phase 5: destroy cdevs (no new opens after this). */
        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }

        /* Phase 6: free auxiliary resources. */
        sysctl_ctx_free(&sc->sysctl_ctx);
        cbuf_destroy(&sc->cb);
        counter_u64_free(sc->bytes_read);
        counter_u64_free(sc->bytes_written);

        /* Phase 7: destroy primitives in reverse order. */
        cv_destroy(&sc->data_cv);
        cv_destroy(&sc->room_cv);
        sx_destroy(&sc->cfg_sx);
        mtx_destroy(&sc->mtx);

        return (0);
}
```

In attach, the matching initialization uses the two-argument form so that `sc` is passed explicitly:

```c
MYFIRST_CO_INIT(sc, &sc->heartbeat_co);
MYFIRST_CO_INIT(sc, &sc->watchdog_co);
MYFIRST_CO_INIT(sc, &sc->tick_source_co);
```

The comments turn the function from a sequence of seemingly-arbitrary calls into a documented checklist.

### Updating LOCKING.md

The Chapter 12 `LOCKING.md` documented three primitives, two lock classes, and one lock order. Chapter 13 adds three callouts. The new sections to add:

```markdown
## Callouts Owned by This Driver

### sc->heartbeat_co (callout(9), MYFIRST_CO_INIT)

Lock: sc->mtx (registered via callout_init_mtx).
Callback: myfirst_heartbeat.
Behaviour: periodic; re-arms itself at the end of each firing if
  sc->heartbeat_interval_ms > 0.
Started by: the heartbeat sysctl handler (transition 0 -> non-zero).
Stopped by: the heartbeat sysctl handler (transition non-zero -> 0)
  via callout_stop, and by myfirst_detach via callout_drain.
Lifetime: initialised in attach via MYFIRST_CO_INIT; drained in detach
  via MYFIRST_CO_DRAIN.

### sc->watchdog_co (callout(9), MYFIRST_CO_INIT)

Lock: sc->mtx.
Callback: myfirst_watchdog.
Behaviour: periodic; emits a warning if cb_used has not changed and
  is non-zero between firings.
Started/stopped: via the watchdog sysctl handler and detach, parallel
  to the heartbeat.

### sc->tick_source_co (callout(9), MYFIRST_CO_INIT)

Lock: sc->mtx.
Callback: myfirst_tick_source.
Behaviour: periodic; injects a single byte into the cbuf each firing
  if there is room.
Started/stopped: via the tick_source sysctl handler and detach,
  parallel to the heartbeat.

## Callout Discipline

1. Every callout uses sc->mtx as its lock via callout_init_mtx.
2. Every callout callback asserts MYFIRST_ASSERT(sc) at entry.
3. Every callout callback checks !sc->is_attached at entry and
   returns early without re-arming.
4. The detach path clears sc->is_attached under sc->mtx, broadcasts
   both cvs, drops the mutex, and then calls callout_drain on every
   callout.
5. callout_stop is used to cancel pending callouts in normal driver
   operation (sysctl handlers); callout_drain is used at detach.
6. NEVER call selwakeup, uiomove, copyin, copyout, malloc(M_WAITOK),
   or any sleeping primitive from a callout callback. The mutex is
   held during the callback, and these calls would violate the
   sleep-with-mutex rule.

## History (extended)

- 0.7-timers (Chapter 13): added heartbeat, watchdog, and tick-source
  callouts; documented callout discipline; standardised callout
  detach pattern.
- 0.6-sync (Chapter 12, Stage 4): combined version with cv channels,
  bounded reads, sx-protected configuration, reset sysctl.
- ... (earlier history as before) ...
```

Add this to the existing `LOCKING.md` rather than replacing the existing content. The new sections sit alongside the existing "Locks Owned by This Driver", "Lock Order", "Locking Discipline", and so on.

### Bumping the Version

Update the version string:

```c
#define MYFIRST_VERSION "0.7-timers"
```

Update the changelog entry:

```markdown
## 0.7-timers (Chapter 13)

- Added struct callout heartbeat_co, watchdog_co, tick_source_co
  to the softc.
- Added sysctls dev.myfirst.<unit>.heartbeat_interval_ms,
  watchdog_interval_ms, tick_source_interval_ms.
- Added callbacks myfirst_heartbeat, myfirst_watchdog,
  myfirst_tick_source, each lock-aware via callout_init_mtx.
- Updated detach to drain every callout under the documented
  seven-phase pattern.
- Added MYFIRST_CO_INIT and MYFIRST_CO_DRAIN macros for callout
  init and teardown.
- Updated LOCKING.md with a Callouts section and callout
  discipline rules.
- Updated regression script to include callout tests.
```

### Updating the README

Two new features in the README:

```markdown
## Features (additions)

- Callout-based heartbeat that periodically logs cbuf usage and
  byte counts.
- Callout-based watchdog that detects stalled buffer drainage.
- Callout-based tick source that injects synthetic data for testing.

## Configuration (additions)

- dev.myfirst.<unit>.heartbeat_interval_ms: periodic heartbeat
  in milliseconds (0 = disabled).
- dev.myfirst.<unit>.watchdog_interval_ms: watchdog interval in
  milliseconds (0 = disabled).
- dev.myfirst.<unit>.tick_source_interval_ms: tick-source interval
  in milliseconds (0 = disabled).
```

### Running Static Analysis

Run `clang --analyze` against the new code. The exact flags depend on your kernel configuration; the same recipe the Chapter 11 regression section used still works, with the added knowledge that callout init macros expand into function calls that `clang` can now see through:

```sh
$ make WARNS=6 clean all
$ clang --analyze -D_KERNEL -DKLD_MODULE \
    -I/usr/src/sys -I/usr/src/sys/contrib/ck/include \
    -fno-builtin -nostdinc myfirst.c
```

Triage the output as before. A few false positives may appear around the callout init macros (the analyser does not always track the lock association embedded in `_callout_init_lock`); document each so the next maintainer does not re-triage them.

### Running the Regression Suite

The Chapter 12 regression script extends naturally. Two design points are worth noting before the script: each subtest clears the kernel message buffer with `dmesg -c` so that `grep -c` only counts lines produced *during* that subtest; and reads use `dd` with a fixed `count=` rather than `cat`, so an unexpectedly empty buffer cannot make the script hang.

```sh
#!/bin/sh
# regression.sh: full Chapter 13 regression.

set -eu

die() { echo "FAIL: $*" >&2; exit 1; }
ok()  { echo "PASS: $*"; }

[ $(id -u) -eq 0 ] || die "must run as root"
kldstat | grep -q myfirst && kldunload myfirst
[ -f ./myfirst.ko ] || die "myfirst.ko not built; run make first"

# Clear any stale dmesg contents so per-subtest greps are scoped.
dmesg -c >/dev/null

kldload ./myfirst.ko
trap 'kldunload myfirst 2>/dev/null || true' EXIT

sleep 1
[ -c /dev/myfirst ] || die "device node not created"
ok "load"

# Chapter 7-12 tests (abbreviated; see prior chapters' scripts).
printf 'hello' > /dev/myfirst || die "write failed"
# dd with bs and count avoids blocking if the buffer is shorter
# than expected; if the read returns short, the test still proceeds.
ROUND=$(dd if=/dev/myfirst bs=5 count=1 2>/dev/null)
[ "$ROUND" = "hello" ] || die "round-trip mismatch (got '$ROUND')"
ok "round-trip"

# Chapter 13-specific tests. Each subtest clears dmesg first so the
# subsequent grep counts only the lines produced during that test.

# Heartbeat enable/disable.
dmesg -c >/dev/null
sysctl -w dev.myfirst.0.heartbeat_interval_ms=100 >/dev/null
sleep 1
HB_LINES=$(dmesg | grep -c "heartbeat:" || true)
[ "$HB_LINES" -ge 5 ] || die "expected >=5 heartbeat lines, got $HB_LINES"
sysctl -w dev.myfirst.0.heartbeat_interval_ms=0 >/dev/null
ok "heartbeat enable/disable"

# Watchdog: enable, write, wait, expect warning, then drain via dd
# (not cat, which would block once the 7 bytes are gone).
dmesg -c >/dev/null
sysctl -w dev.myfirst.0.watchdog_interval_ms=500 >/dev/null
printf 'wd_test' > /dev/myfirst
sleep 2
WD_LINES=$(dmesg | grep -c "watchdog:" || true)
[ "$WD_LINES" -ge 1 ] || die "expected >=1 watchdog line, got $WD_LINES"
sysctl -w dev.myfirst.0.watchdog_interval_ms=0 >/dev/null
dd if=/dev/myfirst bs=7 count=1 of=/dev/null 2>/dev/null  # drain
ok "watchdog warns on stuck buffer"

# Tick source: enable, read, expect synthetic bytes.
dmesg -c >/dev/null
sysctl -w dev.myfirst.0.tick_source_interval_ms=50 >/dev/null
TS_BYTES=$(dd if=/dev/myfirst bs=1 count=10 2>/dev/null | wc -c | tr -d ' ')
[ "$TS_BYTES" -eq 10 ] || die "expected 10 tick bytes, got $TS_BYTES"
sysctl -w dev.myfirst.0.tick_source_interval_ms=0 >/dev/null
ok "tick source produces bytes"

# Detach with callouts active. The trap will not fire after the
# explicit unload because the unload succeeds.
sysctl -w dev.myfirst.0.heartbeat_interval_ms=100 >/dev/null
sysctl -w dev.myfirst.0.tick_source_interval_ms=100 >/dev/null
sleep 1  # allow each callout to fire at least a few times
dmesg -c >/dev/null
kldunload myfirst
trap - EXIT  # the driver is now unloaded
ok "detach with active callouts"

# WITNESS check. Confined to events since the unload above.
WITNESS_HITS=$(dmesg | grep -ci "witness\|lor" || true)
if [ "$WITNESS_HITS" -gt 0 ]; then
    die "WITNESS warnings detected ($WITNESS_HITS lines)"
fi
ok "witness clean"

echo "ALL TESTS PASSED"
```

A few notes on portability and robustness.

The `dmesg -c` calls flush the kernel message buffer between subtests; on FreeBSD `dmesg -c` is documented to clear the buffer after printing it. Without these, a test that runs after the heartbeat subtest could see heartbeat lines from earlier in the run and miscount.

`dd` is used in place of `cat` for the round-trip and watchdog-drain reads. `cat` blocks until EOF, which a character device never returns; `dd` exits after `count=` blocks have been read. The driver is blocking-by-default, so an over-eager `cat` on an empty buffer would simply hang and break the script.

The detach step does not call `kldload` again at the end, because the only later test (`witness clean`) does not need the driver loaded. The `trap` is cleared after the successful unload so EXIT does not try to unload an already-unloaded module.

A green run after each commit is the minimum bar. A green run on a `WITNESS` kernel after the long-duration composite stress (Chapter 12 plus the Chapter 13 callouts active) is the higher bar.

### Pre-Commit Checklist

The Chapter 12 checklist gets three new items for Chapter 13:

1. Have I updated `LOCKING.md` with any new callouts, intervals, or detach changes?
2. Have I run the full regression suite on a `WITNESS` kernel?
3. Have I run the long-duration composite stress for at least 30 minutes with all timers enabled?
4. Have I run `clang --analyze` and triaged every new warning?
5. Have I added a `MYFIRST_ASSERT(sc)` and `if (!sc->is_attached) return;` to every new callout callback?
6. Have I bumped the version string and updated `CHANGELOG.md`?
7. Have I verified that the test kit builds and runs?
8. Have I checked that every cv has both a signaller and a documented condition?
9. Have I checked that every sx_xlock has a paired sx_xunlock on every code path?
10. **(New)** Have I added a `MYFIRST_CO_DRAIN` for every new callout in the detach path?
11. **(New)** Have I confirmed no callout callback calls `selwakeup`, `uiomove`, or any sleeping primitive?
12. **(New)** Have I verified that disabling a callout via its sysctl actually stops the periodic firing?

The new items capture the most common Chapter 13 mistakes. A callout that is initialised but never drained is a kldunload crash waiting to happen. A callback that calls a sleeping function is a `WITNESS` warning waiting to happen. A sysctl that fails to stop a callout is a confusing user experience.

### A Note on Backward Compatibility

A reasonable concern: the Chapter 13 driver adds three new sysctls. Will existing scripts that interact with `myfirst` (perhaps the Chapter 12 stress kit) break?

The answer is no, for two reasons.

First, the new sysctls all default to disabled (interval = 0). The driver's behaviour is unchanged unless the user enables one of them.

Second, the Chapter 12 sysctls (`debug_level`, `soft_byte_limit`, `nickname`, `read_timeout_ms`, `write_timeout_ms`) and stats (`cb_used`, `cb_free`, `bytes_read`, `bytes_written`) are unchanged. Existing scripts read and write the same values. The Chapter 13 additions are purely additive.

This is the discipline of *non-breaking changes*: when you add a feature, do not change the meaning of existing features. The cost is small (think about the change before making it); the benefit is that existing users see no regression.

For Chapter 13, the heartbeat, watchdog, and tick source are all opt-in. A user who does not know about Chapter 13 sees the same driver as before. A user who reads the chapter and enables one of the timers gets the new behaviour. Both groups are happy.

### A Note on Sysctl Naming

The chapter uses sysctl names like `dev.myfirst.0.heartbeat_interval_ms`. The `_ms` suffix is intentional: it documents the unit. A user who sees `heartbeat_interval` could reasonably guess seconds, milliseconds, or microseconds; the suffix removes the ambiguity.

Other conventions:

- `_count` for counters (always non-negative).
- `_max`, `_min` for bounds.
- `_threshold` for switches.
- `_ratio` for percentages or fractions.

Following these conventions makes the sysctl tree self-describing. A user inspecting `sysctl dev.myfirst.0` can guess the meaning of every entry from its name and unit.

### Wrapping Up Section 8

The driver is now version `0.7-timers`. It has:

- A documented callout discipline in `LOCKING.md`.
- A standardised macro pair (`MYFIRST_CO_INIT`, `MYFIRST_CO_DRAIN`) for callout lifecycle.
- A seven-phase detach pattern documented in code comments.
- A regression script that exercises every callout.
- A pre-commit checklist that catches the Chapter 13-specific failure modes.
- Three new sysctls with self-describing names and a default-disabled posture.

That is the close of the chapter's main teaching arc. The labs and challenges follow.



## Hands-on Labs

These labs consolidate the Chapter 13 concepts through direct hands-on experience. They are ordered from least to most demanding.

### Pre-Lab Setup Checklist

Before starting any lab, confirm:

1. **Debug kernel running.** `sysctl kern.ident` reports the kernel with `INVARIANTS` and `WITNESS`.
2. **WITNESS active.** `sysctl debug.witness.watch` returns a non-zero value.
3. **Driver source matches Chapter 12 Stage 4.** The Chapter 13 examples build on top of this.
4. **A clean dmesg.** `dmesg -c >/dev/null` once before the first lab.
5. **Companion userland built.** From `examples/part-03/ch12-synchronization-mechanisms/userland/`, the timeout/config testers should be present.
6. **Backup of Stage 4.** Copy your Chapter 12 Stage 4 driver to a safe location before starting any lab that modifies the source.

### Lab 13.1: Add a Heartbeat Callout

**Goal.** Convert your Chapter 12 Stage 4 driver into a Chapter 13 Stage 1 driver by adding the heartbeat callout.

**Steps.**

1. Copy your Stage 4 driver into `examples/part-03/ch13-timers-and-delayed-work/stage1-heartbeat/`.
2. Add `struct callout heartbeat_co` and `int heartbeat_interval_ms` to `struct myfirst_softc`.
3. In `myfirst_attach`, call `callout_init_mtx(&sc->heartbeat_co, &sc->mtx, 0)` and initialize `heartbeat_interval_ms = 0`.
4. In `myfirst_detach`, drop the mutex and add `callout_drain(&sc->heartbeat_co);` before destroying primitives.
5. Implement `myfirst_heartbeat` callback as shown in Section 4.
6. Implement `myfirst_sysctl_heartbeat_interval_ms` and register it.
7. Build and load on a `WITNESS` kernel.
8. Verify by setting the sysctl: `sysctl -w dev.myfirst.0.heartbeat_interval_ms=1000` and watching `dmesg` for heartbeat lines.

**Verification.** Heartbeat lines appear once per second when enabled. They stop when the sysctl is set to 0. Detach succeeds even with the heartbeat enabled. No `WITNESS` warnings.

**Stretch goal.** Use `dtrace` to count heartbeat callbacks per second:

```sh
# dtrace -n 'fbt::myfirst_heartbeat:entry { @ = count(); } tick-1sec { printa(@); trunc(@); }'
```

The count should match the configured interval (1 per second for 1000 ms).

### Lab 13.2: Add a Watchdog Callout

**Goal.** Add the watchdog callout that detects stalled buffer drainage.

**Steps.**

1. Copy Lab 13.1 into `stage2-watchdog/`.
2. Add `struct callout watchdog_co`, `int watchdog_interval_ms`, `size_t watchdog_last_used` to the softc.
3. Initialize and drain in attach/detach as for the heartbeat.
4. Implement `myfirst_watchdog` and the corresponding sysctl handler from Section 7.
5. Build and load.
6. Test: enable a 1-second watchdog, write some bytes, do not drain, observe warning.

**Verification.** The watchdog warning appears every second while the buffer has unconsumed bytes. The warning stops when the buffer is drained.

**Stretch goal.** Make the watchdog log the time since the last change in the warning message: "no progress for X.Y seconds".

### Lab 13.3: Add a Tick Source

**Goal.** Add the tick source callout that injects synthetic bytes into the cbuf.

**Steps.**

1. Copy Lab 13.2 into `stage3-tick-source/`.
2. Add `struct callout tick_source_co`, `int tick_source_interval_ms`, `char tick_source_byte` to the softc.
3. Initialize and drain as before.
4. Implement `myfirst_tick_source` as shown in Section 7. Note the deliberate omission of `selwakeup` from the callback.
5. Implement the sysctl handler.
6. Build and load.
7. Enable a 100 ms tick source, read with `cat`, observe the synthetic bytes.

**Verification.** `cat /dev/myfirst` produces approximately 10 bytes per second of the configured tick byte (default `'t'`).

**Stretch goal.** Add a sysctl that lets the user change the tick byte at runtime. Verify that changing the byte takes effect immediately on the next firing.

### Lab 13.4: Verify Detach With Active Callouts

**Goal.** Confirm that detach works correctly even when all three callouts are firing.

**Steps.**

1. Load the Stage 3 (tick source) driver.
2. Enable all three callouts:
   ```sh
   sysctl -w dev.myfirst.0.heartbeat_interval_ms=500
   sysctl -w dev.myfirst.0.watchdog_interval_ms=500
   sysctl -w dev.myfirst.0.tick_source_interval_ms=100
   ```
3. Confirm activity in `dmesg`.
4. Run `kldunload myfirst`.
5. Verify no panic, no `WITNESS` warning, no hang.

**Verification.** The unload completes within a few hundred milliseconds. `dmesg` shows no warnings related to the unload.

**Stretch goal.** Time the unload with `time kldunload myfirst`. The drain should be the dominant contributor to the time; expect a few hundred milliseconds depending on the intervals.

### Lab 13.5: Build a Debounce Timer

**Goal.** Implement a debounce shape (not used by `myfirst`, but a useful exercise).

**Steps.**

1. Create a scratch directory for an experimental driver.
2. Implement a sysctl `dev.myfirst.0.event_count` that increments by 1 each time it is written. (The user write triggers the "event".)
3. Add a debounce callout that fires 100 ms after the most recent event and prints the total count of events seen during the window.
4. Test: write the sysctl rapidly five times. Observe that one debounce log line appears 100 ms after the last write, reporting the count.

**Verification.** Multiple rapid events produce only one log line, with a count equal to the number of events.

### Lab 13.6: Detect a Deliberate Race

**Goal.** Introduce a deliberate bug (a callout callback that calls something sleepable) and observe `WITNESS` catching it.

**Steps.**

1. In a scratch directory, modify the heartbeat callback to call something that may sleep, such as `pause("test", hz / 100)`.
2. Build and load on a `WITNESS` kernel.
3. Enable the heartbeat with a 1-second interval.
4. Observe `dmesg` for the warning: "Sleeping on \"test\" with the following non-sleepable locks held: ..." or similar.
5. Revert the change.

**Verification.** `WITNESS` produces a warning that names the sleeping operation and the held mutex. The warning includes the source line.

### Lab 13.7: Long-Running Composite Stress with Timers

**Goal.** Run the Chapter 12 composite stress kit for 30 minutes with the new Chapter 13 callouts enabled.

**Steps.**

1. Load the Stage 4 driver.
2. Enable all three callouts at 100 ms intervals.
3. Run the Chapter 12 composite stress script for 30 minutes.
4. After completion, check:
   - `dmesg | grep -ci witness` returns 0.
   - All loop iterations completed.
   - `vmstat -m | grep cbuf` shows the expected static allocation.

**Verification.** All criteria met; no warnings, no panics, no memory growth.

### Lab 13.8: Profile Callout Activity With dtrace

**Goal.** Use dtrace to observe callout firing patterns.

**Steps.**

1. Load the Stage 4 driver.
2. Enable all three callouts at 100 ms intervals.
3. Run a dtrace one-liner to count callout firings per callback per second:
   ```sh
   # dtrace -n '
   fbt::myfirst_heartbeat:entry,
   fbt::myfirst_watchdog:entry,
   fbt::myfirst_tick_source:entry { @[probefunc] = count(); }
   tick-1sec { printa(@); trunc(@); }'
   ```
4. Observe the per-second counts.

**Verification.** Each callback fires approximately 10 times per second (1000 ms / 100 ms).

**Stretch goal.** Modify the dtrace script to report the time spent inside each callback (using `quantize` and `timestamp`).

### Lab 13.9: Cancel a Watchdog Inline

**Goal.** Make the watchdog a one-shot timer that the read path cancels on success, demonstrating the cancel-on-progress pattern.

**Steps.**

1. Copy Lab 13.4 (`stage3-tick-source` plus heartbeat/watchdog) into a scratch directory.
2. Modify `myfirst_watchdog` to be one-shot: do not re-arm at the end.
3. Schedule the watchdog from `myfirst_write` after each successful write.
4. Cancel the watchdog (using `callout_stop`) from `myfirst_read` after a successful drain.
5. Test: write some bytes; do not read; observe the watchdog warning fire once.
6. Test: write some bytes; read them; observe no warning (because the read cancelled the watchdog).

**Verification.** The watchdog warning fires only when the buffer is left undrained. Successful drains cancel the pending watchdog.

**Stretch goal.** Add a counter that tracks how often the watchdog fired versus how often it was cancelled. Expose as a sysctl. The ratio is a quality metric for buffer drainage.

### Lab 13.10: Schedule From Inside a Sysctl Handler

**Goal.** Verify that scheduling a callout from a sysctl handler produces correct timing.

**Steps.**

1. Add a sysctl `dev.myfirst.0.schedule_oneshot_ms` to the Stage 4 driver. Writing N to it schedules a one-shot callback to fire N milliseconds later.
2. The callback simply logs "one-shot fired".
3. Test: write 100 to the sysctl. Observe the log line about 100 ms later.
4. Test: write 1000 to the sysctl. Observe about 1 second later.
5. Test: write 1 to the sysctl five times in quick succession. Observe how the kernel handles the rapid rescheduling.

**Verification.** Each write produces one log line at approximately the configured interval. Rapid writes either schedule new firings (cancelling the previous) or are coalesced; observe which.

**Stretch goal.** Use `dtrace` to measure the delta between the sysctl write and the actual firing. The histogram should be tight around the configured interval.



## Challenge Exercises

The challenges extend Chapter 13 beyond the baseline labs. Each is optional; each is designed to deepen your understanding.

### Challenge 1: Sub-Millisecond Tick Source

Modify the tick source callout to use `callout_reset_sbt` with a sub-millisecond interval (say, 250 microseconds). Test it. What happens to the heartbeat output (which logs counters)? What does `lockstat` show for the data mutex?

### Challenge 2: Watchdog with Adaptive Intervals

Make the watchdog reduce its interval each time it fires (signal of trouble), and increase its interval when it sees forward progress. Cap both ends at reasonable values.

### Challenge 3: Defer the Selwakeup to a Taskqueue

The tick source omits `selwakeup` because it cannot be called from the callout context. Read `taskqueue(9)` (Chapter 16 will introduce this in depth) and use a taskqueue to defer the `selwakeup` to a worker thread. Verify that `poll(2)` waiters now wake correctly.

### Challenge 4: Multi-CPU Callout Distribution

By default, callouts run on a single CPU. Use `callout_reset_on` to bind each of the three callouts to a different CPU. Use `dtrace` to verify the binding. Discuss the trade-offs.

### Challenge 5: Bound the Maximum Interval

Add validation to each interval sysctl to enforce a minimum (say, 10 ms) and a maximum (say, 60000 ms). Below the minimum, refuse with `EINVAL`. Above the maximum, also refuse. Document the choice.

### Challenge 6: Callout-Based Read Timeout

Replace the Chapter 12 `cv_timedwait_sig`-based read timeout with a callout-based mechanism: schedule a one-shot callout when the reader starts blocking; the callout fires `cv_signal` on the data cv to wake the reader. Compare the two approaches.

### Challenge 7: A Statistics Rollover

Add a callout that takes a snapshot of `bytes_read` and `bytes_written` every 5 seconds and stores the per-interval rates in a circular buffer (separate from the cbuf). Expose the most recent rates via a sysctl.

### Challenge 8: Drain Without Hold

Verify experimentally that calling `callout_drain` while holding the callout's lock deadlocks. Write a small driver variant that does this deliberately, observe the deadlock with DDB, and document the symptom.

### Challenge 9: Reuse a Callout Structure

Use the same `struct callout` for two different callbacks at different times: schedule with callback A, wait for it to fire, schedule with callback B. What happens if A is still pending when you call `callout_reset` with B's function? Write a test to verify the kernel's behaviour.

### Challenge 10: Callout-Based Hello-World Module

Write a minimal module (no `myfirst` involved) that does nothing except install a single callout that prints "tick" every second. Use this as a sanity check for the callout subsystem on your test machine.

### Challenge 11: Verify Lock Serialization

Demonstrate that two callouts sharing the same lock are serialized. Write a driver with two callouts; have each callback sleep briefly (with `DELAY()` if you must, since `DELAY()` does not sleep but spins). Confirm via `dtrace` that the callbacks never overlap.

### Challenge 12: Coalesce Latency

Use `callout_reset_sbt` with various precision values (0, `SBT_1MS`, `SBT_1S`) for a 1-second timer. Use `dtrace` to measure the actual firing times. How much does the kernel coalesce when given more slop? When does coalescing reduce CPU usage?

### Challenge 13: Callout Wheel Inspection

The kernel exposes the callout wheel state through `kern.callout_stat` and `kern.callout_*` sysctls. Read them on a busy system. Can you identify the callouts your driver has scheduled?

### Challenge 14: Callout Function Pointer Replacement

Schedule a callout with one function. Before it fires, schedule it again with a different function. What happens? Does the second function replace the first? Document the behaviour with a small experiment.

### Challenge 15: Adaptive Heartbeat

Make the heartbeat fire faster when there is recent activity (writes in the last second) and slower when idle. The interval should range from 100 ms (active) to 5 seconds (idle). Test it under a stress workload to verify it adapts as expected.



## Troubleshooting

This reference catalogues the bugs you are most likely to encounter while working through Chapter 13.

### Symptom: Callout never fires

**Cause.** Either the interval is zero (callout was disabled), or the sysctl handler did not actually call `callout_reset`.

**Fix.** Check the sysctl handler logic. Confirm that the transition from 0 to non-zero is detected. Add a `device_printf` at the call site to verify.

### Symptom: kldunload panics shortly after

**Cause.** A callout was not drained at detach. The callout fired after the module was unloaded.

**Fix.** Add `callout_drain` for every callout in the detach path. Confirm the order: drain *after* clearing `is_attached`, *before* destroying primitives.

### Symptom: WITNESS warns "sleeping thread (pid X) owns a non-sleepable lock"

**Cause.** A callout callback called something that sleeps (uiomove, copyin, malloc(M_WAITOK), pause, or any cv_wait variant) while holding the kernel-acquired mutex.

**Fix.** Remove the sleeping operation from the callback. If the work requires sleeping, defer to a taskqueue or kernel thread.

### Symptom: Heartbeat fires once and then never again

**Cause.** The callback re-arm code is missing or guarded by a condition that becomes false.

**Fix.** Check the re-arm at the end of the callback. Confirm that `interval_ms > 0` and that the call to `callout_reset` actually executes.

### Symptom: Callout fires more often than the configured interval

**Cause.** Two paths are scheduling the same callout. Either the sysctl handler and the callback are both calling `callout_reset`, or two callbacks share a callout struct.

**Fix.** Audit the call sites. The sysctl handler should `callout_reset` only on the 0 -> non-zero transition; the callback re-arms only at its own end.

### Symptom: Detach hangs

**Cause.** A callout callback re-armed itself between the `is_attached = 0` and the `callout_drain`. The drain is now waiting for the callback to finish; the callback (which checked `is_attached` before the assignment took effect) is not exiting.

**Fix.** Confirm that the `is_attached = 0` happens under the same lock as the callout's lock. Confirm that the drain happens after the assignment, not before. The check inside the callback must see the cleared flag.

### Symptom: WITNESS warns about lock-order issues with the callout's lock

**Cause.** The callout lock is being acquired in conflicting orders by different paths.

**Fix.** The callout's lock is `sc->mtx`. Confirm that every path that acquires `sc->mtx` follows the canonical order (mtx first, then any other lock). The callback runs with `sc->mtx` already held; the callback must not acquire any lock that should be acquired before `sc->mtx`.

### Symptom: Callout-Drain Sleeps Forever

**Cause.** `callout_drain` was called with the callout's lock held. Deadlock: the drain waits for the callback to release the lock, the callback is waiting because the drain is the lock holder.

**Fix.** Drop the lock before calling `callout_drain`. The standard detach pattern does this.

### Symptom: Callback runs but data is stale

**Cause.** The callback is using cached values from before the firing. Either it stored data in a local variable that became stale, or it dereferenced a structure that was modified.

**Fix.** The callback runs with the lock held. Re-read fields each time the callback fires; do not cache across firings.

### Symptom: `procstat -kk` shows no thread waiting on the callout

**Cause.** Callouts do not have associated threads. The callback runs in a kernel thread context (the callout subsystem manages a small pool), but no specific thread is "the callout's thread" the way a kernel thread might own a wait condition.

**Fix.** None needed; this is by design. To see callout activity, use `dtrace` or `lockstat` instead.

### Symptom: callout_reset returns 1 unexpectedly

**Cause.** The callout was previously pending and got cancelled by this `callout_reset`. The return value is informational, not an error.

**Fix.** None needed; this is normal. Use the return value if you care whether the previous schedule was overwritten.

### Symptom: Sysctl handler reports EINVAL for valid input

**Cause.** The handler's validation rejects the value. Common cause: the user passed a negative number that the validation correctly rejects, or the handler has a too-strict bound.

**Fix.** Inspect the validation code. Confirm the user's input meets the documented constraints.

### Symptom: Two callbacks for different callouts run concurrently and deadlock

**Cause.** Both callouts are bound to the same lock, so they cannot run concurrently. If they appear to deadlock, check whether either callback acquires another lock that the other path already holds.

**Fix.** Audit the lock acquisition order. The callback-running thread holds `sc->mtx`; if it tries to acquire `sc->cfg_sx`, the order must be mtx-then-sx (which our canonical order is).

### Symptom: tick_source produces the wrong byte

**Cause.** The callback reads `tick_source_byte` at firing time. If a sysctl just changed it, the callback may see either the old or the new value depending on timing.

**Fix.** This is correct behaviour; the byte change takes effect on the next firing. If immediate effect is required, use the snapshot-and-apply pattern from Chapter 12.

### Symptom: lockstat shows the data mutex held for unusually long during heartbeats

**Cause.** The heartbeat callback is doing too much work while the lock is held.

**Fix.** The heartbeat does only counter reads and one log line; if the hold time is long, it is probably the `device_printf` (which acquires global locks for the message buffer). For low-overhead heartbeats, gate the log line behind a debug level.

### Symptom: Heartbeat continues after sysctl is set to 0

**Cause.** The `callout_stop` did not actually cancel because the callback was already running. The callback re-armed before checking the new value.

**Fix.** The race is closed if the sysctl handler holds `sc->mtx` while updating `interval_ms` and calling `callout_stop`. The callback runs under the same lock; it cannot run between the update and the stop. Verify the lock is held in the right places.

### Symptom: WITNESS warns about acquiring the callout's lock during init

**Cause.** Some earlier path in attach has not yet established the lock order rules. Adding a callout's lock association makes WITNESS notice the inconsistency.

**Fix.** Move the `callout_init_mtx` to after the mutex is initialized. The order must be: mtx_init, then callout_init_mtx.

### Symptom: A single fast callout causes high CPU usage

**Cause.** A 1 ms callout that does even a small amount of work fires 1000 times per second. If each firing takes 100 microseconds, that is 10% of one CPU.

**Fix.** Increase the interval. Sub-second intervals should be used only when truly required.

### Symptom: dtrace cannot find the callback function

**Cause.** dtrace's `fbt` provider needs the function to be present in the kernel's symbol table. If the function was inlined or optimized away, the probe is not available.

**Fix.** Confirm the function is not declared `static inline` or wrapped in a way that prevents external linkage. The standard `static void myfirst_heartbeat(void *arg)` is fine; dtrace can probe it.

### Symptom: heartbeat_interval_ms reads back as 0 after setting it

**Cause.** The sysctl handler updates a local copy and never commits to the softc field, or the field is overwritten elsewhere.

**Fix.** Confirm the handler assigns `sc->heartbeat_interval_ms = new` after validation, before returning.

### Symptom: WITNESS warns "callout_init: lock has sleepable lock_class"

**Cause.** You called `callout_init_mtx` with an `sx` lock or other sleepable primitive instead of an `MTX_DEF` mutex. Sleepable locks are forbidden as callout interlocks because callouts run in a context where sleeping is illegal.

**Fix.** Use `callout_init_mtx` with an `MTX_DEF` mutex, or `callout_init_rw` with an `rw(9)` lock, or `callout_init_rm` with an `rmlock(9)`. Do not use `sx`, `lockmgr`, or any other sleepable lock.

### Symptom: Detach takes seconds even when callouts seem idle

**Cause.** A callout has a long interval (say, 30 seconds) and is currently pending. `callout_drain` waits for the next firing or for the explicit cancellation. If the deadline is far in the future, the wait can be long.

**Fix.** `callout_drain` actually does not wait for the deadline; it cancels the pending and returns once any in-flight callback completes. If your detach takes seconds, something else is wrong (a callback is genuinely taking that long, or a different sleep is involved). Use `dtrace` on `_callout_stop_safe` to investigate.

### Symptom: `callout_pending` returns true after `callout_stop`

**Cause.** Race: another path scheduled the callout between the `callout_stop` and your check of `callout_pending`. Or: the callout was previously firing on another CPU and has just re-armed.

**Fix.** Always hold the callout's lock when calling `callout_stop` and checking `callout_pending`. The lock makes the operations atomic.

### Symptom: A callout function appears in `dmesg` long after the driver was unloaded

**Cause.** The unload race. The callout fired after detach destroyed state. If the kernel did not panic immediately, the printed line is from the freed callback's code, executing in a kernel that has lost track of the original module.

**Fix.** This should not happen if you called `callout_drain` correctly. If it does, your detach path is broken; review every callout to confirm each is drained.

### Symptom: Multiple callouts fire all at once after a long pause

**Cause.** The system was under load (a long-running interrupt, a stuck callout-processing thread) and could not service the callout wheel. When it recovers, it processes all the deferred callouts in quick succession.

**Fix.** This is normal under unusual load. If it happens routinely, investigate why the system could not service callouts on time. `dtrace -n 'callout-end'` (using the `callout` provider, if your kernel exposes it) shows actual firing times.

### Symptom: A periodic callout drifts: each firing is slightly later than the previous

**Cause.** The re-arm is `callout_reset(&co, hz, ..., ...)`, which schedules "1 second from now". Each firing's "now" is slightly later than the previous firing's deadline, so the actual interval grows by the callback's execution time.

**Fix.** For exact periodicity, compute the next deadline as "previous deadline + interval", not "now + interval". Use `callout_reset_sbt` with `C_ABSOLUTE` and an absolute sbintime computed from the original schedule.

### Symptom: Callout never fires even though `callout_pending` returns true

**Cause.** Either the callout is stuck on a CPU that is offline (rare, but possible during CPU hot-unplug), or the system's clock interrupt is not firing on that CPU.

**Fix.** Check `kern.hz` and `kern.eventtimer` sysctls. The default hz=1000 should produce regular firings. If a CPU is offline, the callout subsystem migrates pending callouts to a working CPU, but there is a window. For most drivers, this is not a real concern.

### Symptom: Stress test causes intermittent panic in `callout_process`

**Cause.** Almost certainly the unload race or an incorrect lock association on a callout. The callout subsystem itself is well-tested; bugs at this level are usually in the calling code.

**Fix.** Audit every callout's init and drain. Check that the lock association is correct (no sleepable locks). Run with `INVARIANTS` to catch invariant violations.

### Symptom: `kern.callout.busy` counter grows under load

**Cause.** The callout subsystem detected callbacks taking too long. Each "busy" event is a callback that did not complete within an expected window.

**Fix.** Inspect the slow callbacks with `dtrace`. Long callbacks indicate either too much work (split into multiple callouts or defer to a taskqueue) or a lock-contention issue (the callback is waiting for the lock to become available).

### Symptom: Driver logs show "callout_drain detected migration" or similar

**Cause.** A callout was bound to a specific CPU (via `callout_reset_on`) and the binding migration overlapped with the drain. The kernel resolves this internally; the log message is informational.

**Fix.** None usually needed. If the message is frequent, consider whether per-CPU binding is necessary at all.

### Symptom: `callout_reset_sbt` gives unexpected timing

**Cause.** The `precision` argument is too loose: the kernel coalesced your callout with others into a window much wider than expected.

**Fix.** Set precision to a smaller value (or 0 for "fire as close to deadline as possible"). The default is `tick_sbt` (one tick of slop), which is fine for most timer work.

### Symptom: A callout that was working stops firing after a power-management event

**Cause.** The system's clock interrupt may have been reconfigured (transition between event-timer modes during sleep/wake). The callout subsystem reschedules pending callouts after such transitions, but the timing may be slightly off.

**Fix.** Verify with `dtrace` that the callout's callback is being invoked. If not, the callout has been migrated or discarded; reschedule from a known-good code path.

### Symptom: All callouts in the driver fire on the same CPU

**Cause.** This is the default. Callouts are bound to the CPU that scheduled them; if all your `callout_reset` calls run on CPU 0 (because the user's syscall was dispatched there), all the callouts fire on CPU 0.

**Fix.** This is correct for most drivers. If you want load distribution, use `callout_reset_on` to bind explicitly to different CPUs. Most drivers do not need this; the per-CPU wheels naturally balance over time as different syscalls hit different CPUs.

### Symptom: `callout_drain` returns but the next syscall sees stale state

**Cause.** The callback completed and returned, but a subsequent code path observed state that the callback had set. This is correct behaviour, not a bug.

**Fix.** None. The drain only guarantees the callback is no longer running; any state changes the callback made are still in effect. If the changes are unwanted, the callback should not have made them.

### Symptom: Re-arm in callback fails silently

**Cause.** The condition `interval > 0` is false because the user just disabled the timer. The callback exits without re-arming; the callout becomes idle.

**Fix.** This is correct behaviour. If you want to know when the callback declined to re-arm, add a counter or log line.

### Symptom: Callout fires but `device_printf` is silent

**Cause.** The driver's `dev` field is NULL or the device has been detached and the cdev destroyed. `device_printf` may suppress output in those states.

**Fix.** Add an explicit `printf("%s: ...\n", device_get_nameunit(dev), ...)` to bypass the wrapper. Or confirm that `sc->dev` is valid via `KASSERT`.



## Reference: The Driver's Stage Progression

Chapter 13 evolves the `myfirst` driver in four discrete stages, each of which is its own directory under `examples/part-03/ch13-timers-and-delayed-work/`. The progression mirrors the chapter's narrative; it lets a reader build the driver one timer at a time and see what each addition contributes.

### Stage 1: heartbeat

Adds the heartbeat callout that periodically logs cbuf usage and byte counts. The new sysctl `dev.myfirst.<unit>.heartbeat_interval_ms` enables, disables, and adjusts the heartbeat at runtime.

What changes: one new callout, one new callback, one new sysctl, and the corresponding init/drain in attach/detach.

What you can verify: setting the sysctl to a positive value produces periodic log lines; setting it to 0 stops them; detach succeeds even with the heartbeat enabled.

### Stage 2: watchdog

Adds the watchdog callout that detects stalled buffer drainage. The new sysctl `dev.myfirst.<unit>.watchdog_interval_ms` enables, disables, and adjusts the interval.

What changes: one new callout, one new callback, one new sysctl, and the corresponding init/drain.

What you can verify: enabling the watchdog and writing bytes (without reading them) produces warning lines; reading the buffer stops the warnings.

### Stage 3: tick-source

Adds the tick source callout that injects synthetic bytes into the cbuf. The new sysctl `dev.myfirst.<unit>.tick_source_interval_ms` enables, disables, and adjusts the interval.

What changes: one new callout, one new callback, one new sysctl, and the corresponding init/drain.

What you can verify: enabling the tick source and reading from `/dev/myfirst` produces bytes at the configured rate.

### Stage 4: final

The combined driver with all three callouts, plus a `LOCKING.md` extension, the version bump to `0.7-timers`, and the standardised `MYFIRST_CO_INIT` and `MYFIRST_CO_DRAIN` macros.

What changes: integration. No new primitives.

What you can verify: the regression suite passes; the long-duration stress test with all callouts active runs cleanly; `WITNESS` is silent.

This four-stage progression is the canonical Chapter 13 driver. The companion examples mirror the stages exactly so a reader can compile and load any one of them.



## Reference: Anatomy of a Real Watchdog

A real production watchdog does more than the chapter's example. A short tour of what real watchdogs typically include, useful when you write or read driver source.

### Per-Request Tracking

Real I/O watchdogs track each pending request individually. The watchdog callback walks a list of pending requests, finds those that have been outstanding too long, and acts on each.

```c
struct myfirst_request {
        TAILQ_ENTRY(myfirst_request) link;
        sbintime_t   submitted_sbt;
        int           op;
        /* ... other request state ... */
};

TAILQ_HEAD(, myfirst_request) pending_requests;
```

The watchdog walks `pending_requests`, computes the age of each, and acts on the stale ones.

### Threshold-Based Action

Different ages get different actions. Up to T1, ignore (the request is still working). T1 to T2, log a warning. T2 to T3, attempt soft recovery (send a reset to the request). Beyond T3, hard recovery (reset the channel, fail the request).

```c
age_sbt = now - req->submitted_sbt;
if (age_sbt > sc->watchdog_hard_sbt) {
        /* hard recovery */
} else if (age_sbt > sc->watchdog_soft_sbt) {
        /* soft recovery */
} else if (age_sbt > sc->watchdog_warn_sbt) {
        /* log warning */
}
```

### Statistics

A real watchdog tracks how often each threshold was hit, what percentage of requests exceeded each threshold, and so on. The statistics are exposed as sysctls for monitoring.

### Configurable Thresholds

Each threshold (T1, T2, T3) is a sysctl. Different deployments need different bounds; hard-coding is wrong.

### Recovery Logging

The recovery action logs to dmesg with a recognizable prefix that monitoring tools can grep. A detailed message with the request's identity, the action taken, and any kernel state that might help diagnose the underlying issue.

### Coordination With Other Subsystems

A hard recovery often involves cooperation with other parts of the driver: the I/O layer must know that the channel is being reset, queued requests must be re-queued or failed, and the driver's "is operational" state must be updated.

For Chapter 13, our watchdog is much simpler. It detects one specific condition (no progress on the cbuf), logs a warning, and re-arms. This captures the essential pattern. Real-world watchdogs add the pieces above incrementally.



## Reference: Periodic vs Event-Driven Driver Architecture

A small architectural digression. Some drivers are dominated by events (an interrupt arrives, the driver responds). Others are dominated by polling (the driver wakes periodically to check). Understanding which one your driver is helps choose primitives.

### Event-Driven

In an event-driven design, the driver is mostly idle. Activity is triggered by:

- User syscalls (`open`, `read`, `write`, `ioctl`).
- Hardware interrupts (Chapter 14).
- Wake-ups from other subsystems (cv signals, taskqueue runs).

Callouts in an event-driven design are typically watchdogs (track an event, fire if it does not happen) and reapers (clean up after events).

The `myfirst` driver was originally event-driven (read/write triggered everything). Chapter 13 adds some polling-flavoured behaviour (heartbeat, tick source) for demonstration, but the underlying design is still event-driven.

### Polling-Driven

In a polling-driven design, the driver wakes periodically to do work, regardless of whether anyone is asking. This is appropriate for hardware that does not interrupt for the events the driver cares about.

Callouts in a polling-driven design are the heartbeat of the driver: every firing, the callback checks the hardware and processes whatever it finds.

The polling-loop pattern (Section 7) is the basic shape. Real polling drivers extend it with adaptive intervals (poll faster when busy, slower when idle), error counting (give up after too many failed polls), and so on.

### Hybrid

Most real drivers are hybrid: events drive most activity, but a periodic callout catches what events miss (timeouts, slow polling, statistics). The patterns from this chapter apply to either side; the choice of which to use where is a design decision.

For `myfirst`, our hybrid uses:
- Event-driven syscall handlers for the main I/O.
- A heartbeat callout for periodic logging.
- A watchdog callout for stuck-state detection.
- An optional tick source callout for synthetic event generation.

A real driver would have many more callouts, but the shape is the same.



## Wrapping Up

Chapter 13 took the driver you built in Chapter 12 and gave it the ability to act on its own schedule. Three callouts now sit alongside the existing primitives: a heartbeat that periodically logs state, a watchdog that detects stalled drainage, and a tick source that injects synthetic bytes. Each is lock-aware, drained at detach, configurable through a sysctl, and documented in `LOCKING.md`. The driver's data path is unchanged; the new code is purely additive.

We learned that `callout(9)` is small, regular, and well-integrated with the rest of the kernel. The lifecycle is the same five stages every time: init, schedule, fire, complete, drain. The lock contract is the same model every time: the kernel acquires the registered lock before each firing and releases it after, serializing the callback against any other holder. The detach pattern is the same seven phases every time: refuse if busy, mark going-away under the lock, drop the lock, drain selinfo, drain every callout, destroy cdevs, free state, destroy primitives in reverse order.

We also learned a small handful of recipes that recur across drivers: heartbeat, watchdog, debounce, retry-with-backoff, deferred reaper, statistics rollover. Each is a small variation on the periodic or one-shot shape; once you know the patterns, the variations are mechanical.

Four closing reminders before moving on.

The first is to *drain every callout at detach*. The unload race is reliable, immediate, and fatal. The cure is mechanical: one `callout_drain` per callout, after `is_attached` is cleared and before primitives are destroyed. There is no excuse to skip this.

The second is to *keep callbacks short and lock-aware*. The callback runs with the registered lock held, in a context that may not sleep. Treat it like a hardware-interrupt handler: do the minimum, defer the rest. If the work needs to sleep, enqueue it on a taskqueue (Chapter 16) or wake a kernel thread.

The third is to *use sysctls to make timer behaviour configurable*. Hard-coding intervals is a maintenance burden. Letting users tune the heartbeat, the watchdog, or the tick source from `sysctl -w` makes the driver useful in environments you did not anticipate. The cost is small (one sysctl handler per knob) and the benefit is large.

The fourth is to *update `LOCKING.md` in the same commit as any code change*. A driver whose documentation drifts from the code accumulates subtle bugs because no one knows what the rules are supposed to be. The discipline is one minute per change; the benefit is years of clean maintenance.

These four disciplines together produce drivers that compose well with the rest of FreeBSD, that survive long-term maintenance, and that behave predictably under load. They are also the disciplines Chapter 14 will assume; the patterns from this chapter transfer directly to interrupt handlers.

### What You Should Now Be Able to Do

A short self-checklist before turning to Chapter 14:

- Choose between `callout(9)` and `cv_timedwait_sig` for any "wait until X happens, or until Y time has passed" requirement.
- Initialize a callout with the appropriate lock variant for your driver's needs.
- Schedule one-shot and periodic callouts using `callout_reset` (or `callout_reset_sbt` for sub-tick precision).
- Cancel callouts with `callout_stop` in normal operation; drain with `callout_drain` at detach.
- Write a callout callback that respects the lock contract and the no-sleeping rule.
- Use the `is_attached` pattern to make callbacks safe during teardown.
- Document every callout in `LOCKING.md`, including its lock, its callback, its lifecycle.
- Recognize the unload race and avoid it through the standard seven-phase detach pattern.
- Build watchdog, heartbeat, debounce, and tick-source patterns as needed.
- Use `dtrace` to verify callout firing rates, latency, and lifecycle behaviour.
- Read real-driver source (led, uart, network drivers) and recognize the patterns from this chapter at work.

If any of those feels uncertain, the labs in this chapter are the place to build the muscle memory. None requires more than an hour or two; together they cover every primitive and every pattern the chapter introduced.

### A Note On the Companion Examples

The companion source under `examples/part-03/ch13-timers-and-delayed-work/` mirrors the chapter's stages. Each stage builds on the previous one, so you can compile and load any stage to see exactly the driver state the chapter describes at that point.

If you prefer to type the changes by hand (recommended for the first read), use the chapter's worked examples as your guide and the companion source as a reference. If you prefer to read finished code, the companion source is canonical.

A note on the `LOCKING.md` document: the chapter's text explains what `LOCKING.md` should contain. The actual file is in the example tree alongside the source. Keep both in sync as you make changes; the discipline of updating `LOCKING.md` in the same commit as the code change is the most reliable way to keep the documentation accurate.



## Reference: callout(9) Quick Reference

A compact API summary for everyday lookup.

### Initialization

```c
callout_init(&co, 1)                       /* mpsafe; no lock */
callout_init_mtx(&co, &mtx, 0)             /* lock is mtx (default) */
callout_init_mtx(&co, &mtx, CALLOUT_RETURNUNLOCKED)
callout_init_rw(&co, &rw, 0)               /* lock is rw, exclusive */
callout_init_rw(&co, &rw, CALLOUT_SHAREDLOCK)
callout_init_rm(&co, &rm, 0)               /* lock is rmlock */
```

### Scheduling

```c
callout_reset(&co, ticks, fn, arg)         /* tick-based delay */
callout_reset_sbt(&co, sbt, prec, fn, arg, flags)
callout_reset_on(&co, ticks, fn, arg, cpu) /* bind to CPU */
callout_schedule(&co, ticks)               /* re-use last fn/arg */
```

### Cancellation

```c
callout_stop(&co)                          /* cancel; do not wait */
callout_drain(&co)                         /* cancel + wait for in-flight */
callout_async_drain(&co, drain_fn)         /* drain async */
```

### Inspection

```c
callout_pending(&co)                       /* is the callout scheduled? */
callout_active(&co)                        /* user-managed active flag */
callout_deactivate(&co)                    /* clear the active flag */
```

### Common Flags

```c
CALLOUT_RETURNUNLOCKED   /* callback releases the lock itself */
CALLOUT_SHAREDLOCK       /* acquire rw/rm in shared mode */
C_HARDCLOCK              /* align to hardclock() */
C_DIRECT_EXEC            /* run in timer interrupt context */
C_ABSOLUTE               /* sbt is absolute time */
```



## Reference: Standard Detach Pattern

The seven-phase detach pattern for a driver with callouts:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* Phase 1: refuse if in use. */
        MYFIRST_LOCK(sc);
        if (sc->active_fhs > 0) {
                MYFIRST_UNLOCK(sc);
                return (EBUSY);
        }

        /* Phase 2: mark going away; broadcast cvs. */
        sc->is_attached = 0;
        cv_broadcast(&sc->data_cv);
        cv_broadcast(&sc->room_cv);
        MYFIRST_UNLOCK(sc);

        /* Phase 3: drain selinfo. */
        seldrain(&sc->rsel);
        seldrain(&sc->wsel);

        /* Phase 4: drain every callout (no lock held). */
        callout_drain(&sc->heartbeat_co);
        callout_drain(&sc->watchdog_co);
        callout_drain(&sc->tick_source_co);

        /* Phase 5: destroy cdevs (no new opens). */
        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }

        /* Phase 6: free auxiliary resources. */
        sysctl_ctx_free(&sc->sysctl_ctx);
        cbuf_destroy(&sc->cb);
        counter_u64_free(sc->bytes_read);
        counter_u64_free(sc->bytes_written);

        /* Phase 7: destroy primitives in reverse order. */
        cv_destroy(&sc->data_cv);
        cv_destroy(&sc->room_cv);
        sx_destroy(&sc->cfg_sx);
        mtx_destroy(&sc->mtx);

        return (0);
}
```

Skipping any phase creates a class of bug that crashes the kernel under load.



## Reference: When to Use Each Timing Primitive

A compact decision table.

| Need | Primitive |
|---|---|
| One-shot callback at time T | `callout_reset` |
| Periodic callback every T ticks | `callout_reset` with re-arm |
| Sub-millisecond callback timing | `callout_reset_sbt` |
| Bind callback to a specific CPU | `callout_reset_on` |
| Run callback in timer-interrupt context | `callout_reset_sbt` with `C_DIRECT_EXEC` |
| Wait until a condition, with deadline | `cv_timedwait_sig` (Chapter 12) |
| Wait until a condition, no deadline | `cv_wait_sig` (Chapter 12) |
| Defer work to a worker thread | `taskqueue_enqueue` (Chapter 16) |
| Long-running periodic work | Kernel thread + `cv_timedwait` |

The first four are `callout(9)` use cases; the others use other primitives.



## Reference: Callout Mistakes to Avoid

A compact list of the most common mistakes:

- **Forgetting `callout_drain` at detach.** Causes a panic the next time the callout fires.
- **Calling `callout_drain` while holding the callout's lock.** Causes a deadlock.
- **Calling sleeping functions from the callback.** Causes a `WITNESS` warning or a panic.
- **Using `callout_init` (with `mpsafe=0`) for new code.** Acquires Giant; harms scalability.
- **Forgetting the `is_attached` check at the top of the callback.** Detach can race with re-arm and never complete.
- **Sharing a `struct callout` between two callbacks.** Confusing and rarely what you want; use two structs.
- **Hard-coding intervals in the callback.** No way for users to tune behaviour.
- **Failing to validate sysctl input.** Negative or absurd intervals create surprising behaviour.
- **Calling `selwakeup` from a callback.** Takes other locks; can produce ordering violations.
- **Using `callout_stop` at detach.** Does not wait for in-flight; causes the unload race.



## Reference: Reading kern_timeout.c

Two functions in `/usr/src/sys/kern/kern_timeout.c` are worth opening once.

`callout_reset_sbt_on` is the core scheduling function. Every other variant of `callout_reset` is a wrapper that ends up here. The function handles the cases of "callout is currently running", "callout is pending and being rescheduled", "callout needs to be migrated to a different CPU", and "callout is fresh". The complexity is real; the public-facing behaviour is simple.

`_callout_stop_safe` is the unified stop-and-maybe-drain function. Both `callout_stop` and `callout_drain` are macros that call this function with different flags. The `CS_DRAIN` flag is the one that triggers the wait-for-in-flight behaviour. Reading this function once shows exactly how the drain interacts with the firing callback.

The file is around 1550 lines in FreeBSD 14.3. You do not need to read every line. Skim the function names, find the two functions above, and read each carefully. Twenty minutes of reading gives you a working sense of the implementation.



## Reference: The c_iflags and c_flags Fields

A short look at the two flag fields, useful when you read real driver source that inspects them directly.

`c_iflags` (internal flags, set by the kernel):

- `CALLOUT_PENDING`: the callout is on the wheel waiting to fire. Read via `callout_pending(c)`.
- `CALLOUT_PROCESSED`: internal bookkeeping for which list the callout is on.
- `CALLOUT_DIRECT`: set if `C_DIRECT_EXEC` was used.
- `CALLOUT_DFRMIGRATION`: set during deferred migration to a different CPU.
- `CALLOUT_RETURNUNLOCKED`: set if the lock-handling contract was set up to expect the callback to release the lock.
- `CALLOUT_SHAREDLOCK`: set if the rw/rm lock is to be acquired in shared mode.

`c_flags` (external flags, managed by the caller):

- `CALLOUT_ACTIVE`: a user-managed bit. Set by the kernel during a successful `callout_reset`; cleared by `callout_deactivate` or by a successful `callout_stop`. Driver code reads via `callout_active(c)`.
- `CALLOUT_LOCAL_ALLOC`: deprecated; only used in legacy `timeout(9)` style.
- `CALLOUT_MPSAFE`: deprecated; use `callout_init_mtx` instead.

Driver code touches only `CALLOUT_ACTIVE` (via `callout_active` and `callout_deactivate`) and `CALLOUT_PENDING` (via `callout_pending`). Everything else is internal.



## Reference: A Chapter 13 Self-Assessment

Before turning to Chapter 14, use this rubric. The format mirrors Chapter 11 and Chapter 12: conceptual questions, code-reading questions, and hands-on questions. If any item feels uncertain, the relevant section name is in parentheses.

The questions are not exhaustive; they sample the chapter's core ideas. A reader who can answer all of them confidently is ready for the next chapter. A reader who struggles on a particular item should re-read the relevant section before continuing.

### Conceptual Questions

These questions sample the Chapter 13 vocabulary. A reader who can answer them all without re-checking the chapter has internalised the material.

1. **Why use `callout(9)` instead of a kernel thread for periodic work?** Callouts are essentially free at rest; threads have a 16 KB stack and scheduler overhead. For short periodic work, a callout is the right answer.

2. **What is the difference between `callout_stop` and `callout_drain`?** `callout_stop` cancels pending and returns immediately; `callout_drain` cancels pending and waits for any in-flight callback to finish. Use `callout_stop` in normal operation, `callout_drain` at detach.

3. **What does the `lock` argument to `callout_init_mtx` accomplish?** The kernel acquires that lock before each callback firing and releases it after. The callback runs with the lock held.

4. **What may a callout callback not do?** Anything that may sleep, including `cv_wait`, `mtx_sleep`, `uiomove`, `copyin`, `copyout`, `malloc(M_WAITOK)`, `selwakeup`.

5. **Why does the standard detach pattern call `callout_drain` after dropping the mutex?** `callout_drain` may sleep waiting for the in-flight callback. Sleeping with the mutex held is illegal. Dropping the mutex before draining is mandatory.

6. **What is the unload race?** A callout fires after `kldunload` has run, finding its function and surrounding state freed. The kernel jumps to invalid memory and panics.

7. **What is the periodic callback re-arm pattern?** The callback does its work and calls `callout_reset` at the end to schedule the next firing.

8. **Why does the callback check `is_attached` before doing work?** Detach clears `is_attached`; if the callback fires during the brief window between clearing and `callout_drain`, the check prevents the callback from doing work that depends on state being torn down.

9. **What happens if you call `callout_drain` while holding the callout's lock?** Deadlock: the drain waits for the callback to release the lock, but the callback cannot release the lock that the drainer is holding. Always drop the lock before calling `callout_drain`.

10. **What is the purpose of `MYFIRST_CO_INIT` and `MYFIRST_CO_DRAIN`?** They are macro wrappers around `callout_init_mtx` and `callout_drain` that document the convention: every callout uses `sc->mtx` and is drained at detach. Standardising via macros makes new callouts mechanical to add and easy to review.

11. **Why is `device_printf` safe to call from a callout callback while the mutex is held?** It does not acquire any of the driver's locks and it does not sleep; it writes to a global ringbuffer with its own internal locking. It is one of the few "safe to call from a callout context" output functions.

12. **What is the difference between scheduling a callout for `hz` ticks and scheduling it for `tick_sbt * hz`?** Conceptually nothing; both represent one second. The first uses `callout_reset` (tick-based API); the second uses `callout_reset_sbt` (sbintime-based API). Choose the API that matches the precision you need.

### Code Reading Questions

Open your Chapter 13 driver source and verify:

1. Every `callout_init_mtx` is paired with a `callout_drain` in detach.
2. Every callback starts with `MYFIRST_ASSERT(sc)` and `if (!sc->is_attached) return;`.
3. No callback calls `selwakeup`, `uiomove`, `copyin`, `copyout`, `malloc(M_WAITOK)`, or `cv_wait`.
4. The detach path drops the mutex before calling `callout_drain`.
5. Every callout has a sysctl that allows the user to enable, disable, or change its interval.
6. Every callout's lifecycle (init in attach, drain in detach) is documented in `LOCKING.md`.
7. Every periodic callback re-arms only when its interval is positive.
8. Every sysctl handler holds the mutex when calling `callout_reset` or `callout_stop`.

### Hands-On Questions

These should all be quick to run; if any fails, the relevant lab in this chapter walks through the setup.

1. Load the Chapter 13 driver. Enable the heartbeat with a 1-second interval. Confirm dmesg shows one log line per second.

2. Enable the watchdog with a 1-second interval. Write some bytes. Wait. Confirm the warning appears.

3. Enable the tick source with a 100 ms interval. Read with `cat`. Confirm 10 bytes per second.

4. Enable all three callouts. Run `kldunload myfirst`. Verify no panic, no warning.

5. Open `/usr/src/sys/kern/kern_timeout.c`. Find `callout_reset_sbt_on`. Read the first 50 lines. Can you describe what it does in two sentences?

6. Use `dtrace` to confirm that the heartbeat fires at the expected rate. With `heartbeat_interval_ms=200`, the rate should be 5 firings per second.

7. Modify the watchdog to log additional information (e.g., how many callbacks have fired since boot). Verify the new field appears in dmesg.

8. Open `/usr/src/sys/dev/led/led.c`. Find the calls to `callout_init_mtx`, `callout_reset`, and `callout_drain`. Compare to the patterns in this chapter. Are there differences?

If all eight hands-on questions pass and the conceptual questions are easy, your Chapter 13 work is solid. You are ready for Chapter 14.

A note on pacing: Part 3 of this book has been dense. Three chapters (11, 12, 13) on synchronization-related topics is a lot of new vocabulary to absorb. If the labs in this chapter felt easy, that is a good sign. If they felt hard, take a day or two before starting Chapter 14; the material will compound nicely after a short break, and starting Chapter 14 with fresh attention is better than pushing through tired.

A note on testing: the regression script in Section 8 covers the basic functionality. For long-term confidence, run the composite stress kit from Chapter 12 with all three Chapter 13 callouts active, on a `WITNESS` kernel, for at least 30 minutes. A clean run is the bar to clear before declaring the driver production-ready. Anything less risks the unload race or a subtle lock-order issue that the basic regression does not catch.

If the stress run finds problems, the troubleshooting reference earlier in this chapter is the first stop. Most issues fall into one of the symptom patterns there. If the symptom does not match anything in the reference, the next step is the in-kernel debugger; the DDB recipes in the troubleshooting section get you started.

When you have a clean stress run and a clean review, the chapter's work is done. The driver is now stage-4 final, version 0.7-timers, with a documented callout discipline and a regression suite that proves the discipline holds under load. Take a moment to appreciate that. Then proceed.



## Reference: Callouts in Real FreeBSD Drivers

A short tour of how `callout(9)` is used in actual FreeBSD source. The patterns you have learned in this chapter map directly onto the patterns these drivers use.

### `/usr/src/sys/dev/led/led.c`

A simple but instructive example. The `led(4)` driver lets user-space scripts blink LEDs on hardware that supports them. The driver schedules a callout every `hz / 10` (100 ms) to step through the blink pattern.

The key call inside `led_timeout` and `led_state` is:

```c
callout_reset(&led_ch, hz / 10, led_timeout, p);
```

The callout is initialized in `led_drvinit`:

```c
callout_init_mtx(&led_ch, &led_mtx, 0);
```

A periodic callout, lock-aware via the driver's mutex. Exactly the pattern this chapter teaches.

### `/usr/src/sys/dev/uart/uart_core.c`

The serial-port (UART) driver uses a callout in some configurations to poll for input on hardware that does not interrupt for character receipt. The pattern is the same: `callout_init_mtx`, periodic callback, drain at detach.

### Network Driver Watchdogs

Most network drivers (ixgbe, em, mlx5, etc.) install a watchdog callout at attach time. The watchdog fires every few seconds; it checks whether the hardware has produced an interrupt recently and resets the chip if not. The callback is short, lock-aware, and deferred to the rest of the driver if it needs to do something complex (typically by enqueueing a task on a taskqueue).

### Storage Driver I/O Timeouts

ATA, NVMe, and SCSI drivers use callouts as I/O timeouts. When a request is sent to the hardware, the driver schedules a callout for some bounded time in the future. If the request completes normally, the driver cancels the callout. If the callout fires, the driver assumes the request is stuck and takes recovery action (reset the channel, retry, fail the request to the user).

This is the watchdog pattern (Section 7) applied to per-request operations rather than to the device as a whole.

### USB Hub Polling

The USB hub driver polls hub status every few hundred milliseconds (configurable). The poll discovers attached/detached devices, port-status changes, and transfer completions that the hub does not interrupt for. The pattern is the polling-loop pattern (Section 7).

### What These Drivers Do Differently

The drivers above use additional primitives beyond what Chapter 13 covers, especially taskqueues. Many of them schedule a callout that, instead of doing the actual work, enqueues a task on a taskqueue. The task runs in process context and may sleep. Chapter 16 introduces this pattern in depth.

For Chapter 13 we keep all the work inside the callout's callback, lock-aware and non-sleeping. Real drivers extend this pattern by deferring long work; the underlying timing infrastructure (callout) is the same.



## Reference: Comparing callout to Other Time Primitives

A more detailed comparison than the decision table earlier in the chapter.

### `callout(9)` vs `cv_timedwait_sig`

The same primitive at the syscall layer is `cv_timedwait_sig` (Chapter 12): "wait until condition X becomes true, but give up after T milliseconds". The caller is the one waiting; the cv is the one that gets signalled.

Compare to `callout(9)`: a callback runs at time T, regardless of whether anyone is waiting for it. The callback is the one acting; it does its own work, possibly signalling other things.

The two differ in *who waits* and *who acts*. In `cv_timedwait_sig`, the syscall thread is both waiter and (after wakeup) actor. In `callout(9)`, the syscall thread is uninvolved; an independent context fires the callback.

Use `cv_timedwait_sig` when the syscall thread has work to do as soon as the wait completes. Use `callout(9)` when something independent must happen at a deadline.

### `callout(9)` vs `taskqueue(9)`

`taskqueue(9)` runs a function "as soon as possible" by enqueueing it on a worker thread. There is no time delay; the work runs as soon as the worker can pick it up.

Compare to `callout(9)`: the function runs at a specific time in the future.

A common pattern is to combine the two: a callout fires at time T, decides that work is needed, and enqueues a task. The task runs in process context and does the actual work (which may include sleeping). Chapter 16 will cover this combination.

### `callout(9)` vs Kernel Threads

A kernel thread can loop and call `cv_timedwait_sig` to produce periodic behaviour. The thread is heavyweight: 16 KB of stack, scheduler entry, priority assignment.

Compare to `callout(9)`: no thread; the kernel's timer interrupt mechanism handles the firing, and a small callout-processing pool runs the callbacks.

Use a kernel thread when the work is genuinely long-running (the worker thread waits, does substantial work, waits again). Use a callout when the work is short and you just need it to fire on a schedule.

### `callout(9)` vs Periodic SIGALRM in User Space

A user-space process can install a `SIGALRM` handler and use `alarm(2)` for periodic behaviour. The signal handler runs in the process; it is short and constrained.

Compare to `callout(9)`: kernel-side, lock-aware, integrated with the rest of the driver.

User-space alarms are appropriate for user-space code. They have no role in driver work; the kernel does its own thing.

### `callout(9)` vs Hardware Timers

Some hardware has its own timer registers (a "GP timer" or "watchdog timer" that the host driver programs). These hardware timers fire interrupts directly to the host. They are fast, precise, and bypass the kernel's callout subsystem.

Use the hardware timer when:
- The hardware provides one and you have an interrupt handler.
- The precision required exceeds what `callout_reset_sbt` can deliver.

Use `callout(9)` when:
- The hardware does not have a usable timer for your purpose.
- The precision the kernel can deliver (down to `tick_sbt` or sub-tick with `_sbt`) is sufficient.

For our pseudo-device, no hardware exists; `callout(9)` is the right and only choice.



## Reference: Common Callout Vocabulary

A glossary of terms used in the chapter, useful when you encounter them in driver source.

**Callout**: an instance of `struct callout`; a scheduled-or-unscheduled timer.

**Wheel**: the kernel's per-CPU array of callout buckets, organized by deadline.

**Bucket**: one element of the wheel, holding a list of callouts that should fire in a small range of time.

**Pending**: state in which the callout is on the wheel waiting to fire.

**Active**: a user-managed bit indicating "I scheduled this callout and have not actively cancelled it"; not the same as pending.

**Firing**: state in which the callout's callback is currently executing.

**Idle**: state in which the callout is initialized but not pending; either never scheduled or has fired and not re-armed.

**Drain**: the operation of waiting for an in-flight callback to complete (typically in detach); `callout_drain`.

**Stop**: the operation of cancelling a pending callout without waiting; `callout_stop`.

**Direct execution**: an optimization where the callback runs in the timer interrupt context itself, set with `C_DIRECT_EXEC`.

**Migration**: the kernel's relocation of a callout to a different CPU (typically because the originally-bound CPU is offline).

**Lock-aware**: a callout initialized with one of the lock variants (`callout_init_mtx`, `_rw`, or `_rm`); the kernel acquires the lock for each firing.

**Mpsafe**: a legacy term for "may be called without acquiring Giant"; in modern use it appears as the `mpsafe` argument to `callout_init`.

**Re-arm**: the action a callback takes to schedule the next firing of the same callout.



## Reference: Timer Anti-Patterns to Avoid

A short catalogue of patterns that look reasonable but are wrong.

**Anti-pattern 1: Polling in a tight loop.** Some drivers, especially ones written by beginners, busy-wait on a hardware register: `while (!(read_reg() & READY)) ; /* keep checking */`. This burns CPU and produces a system that is unresponsive under load. The callout-based polling pattern is the cure: schedule a callback that checks the register and re-arms.

**Anti-pattern 2: Hard-coded intervals.** A driver that hard-codes "wait 100 ms" everywhere is a driver that is hard to tune. Make the interval a sysctl or a softc field that the user can adjust.

**Anti-pattern 3: Missing drain at detach.** The most common Chapter 13 mistake. The unload race crashes the kernel. Always drain.

**Anti-pattern 4: Sleeping in the callback.** The callback runs with a non-sleepable lock held; sleeping is forbidden. If the work needs to sleep, defer to a kernel thread or taskqueue.

**Anti-pattern 5: Using `callout_init` (the legacy variant) for new code.** The lock-naive variant requires you to do all your own locking inside the callback, which is more error-prone than letting the kernel do it. Use `callout_init_mtx` for new code.

**Anti-pattern 6: Sharing a `struct callout` between multiple callbacks.** A `struct callout` is not a queue. If you need to fire two different callbacks, use two `struct callout`s.

**Anti-pattern 7: Calling `callout_drain` while holding the callout's lock.** Causes a deadlock. Drop the lock first.

**Anti-pattern 8: Setting the same lock as multiple unrelated subsystems' callout interlocks.** The serialization can produce surprising lock contention. Each subsystem should generally have its own lock; share only when the work is genuinely related.

**Anti-pattern 9: Re-using a `struct callout` after `callout_drain` without re-initializing.** After drain, the callout's internal state is reset, but the function and argument from the last `callout_reset` are still there. If you `callout_schedule` next, you reuse those. This is subtle. For clarity, call `callout_init_mtx` again before reuse.

**Anti-pattern 10: Forgetting that `callout_stop` does not wait.** In normal operation this is correct; in detach it is wrong. Use `callout_drain` for detach.

These patterns recur often enough to be worth memorizing. A driver that avoids all ten will have a much easier time.



## Reference: Tracing Callouts With dtrace

A short collection of `dtrace` recipes useful for inspecting callout behaviour. Each is one or two lines; together they cover most diagnostic needs.

### Count Firings of a Specific Callback

```sh
# dtrace -n 'fbt::myfirst_heartbeat:entry { @ = count(); } tick-1sec { printa(@); trunc(@); }'
```

Per-second count of how many times the heartbeat callback ran. Useful for confirming the configured rate.

### Histogram Time Spent In Callback

```sh
# dtrace -n '
fbt::myfirst_heartbeat:entry { self->ts = timestamp; }
fbt::myfirst_heartbeat:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-30sec { exit(0); }'
```

Distribution of callback durations, in nanoseconds. Useful for spotting unusually slow firings.

### Trace All Callout Resets

```sh
# dtrace -n 'fbt::callout_reset_sbt_on:entry { printf("co=%p, fn=%p, arg=%p", arg0, arg3, arg4); }'
```

Every `callout_reset` (and its variants) call. Useful for confirming what code paths are scheduling callouts.

### Trace Callout Drains

```sh
# dtrace -n 'fbt::_callout_stop_safe:entry /arg1 == 1/ { printf("drain co=%p", arg0); stack(); }'
```

Every call to the drain path (`flags == CS_DRAIN`). Useful for confirming detach calls drain on every callout.

### Per-CPU Callout Activity

```sh
# dtrace -n 'fbt::callout_process:entry { @[cpu] = count(); } tick-1sec { printa(@); trunc(@); }'
```

Per-second count of callout-processing invocations on each CPU. Tells you which CPUs are doing the timer work.

### Identify Slow Callouts

```sh
# dtrace -n '
fbt::callout_process:entry { self->ts = timestamp; }
fbt::callout_process:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-30sec { exit(0); }'
```

Distribution of how long the callout-processing loop takes. Long durations indicate either many callouts firing at the same time or slow individual callbacks.

### A Combined Diagnostic Script

For reading per-second:

```sh
# dtrace -n '
fbt::callout_reset_sbt_on:entry { @resets = count(); }
fbt::_callout_stop_safe:entry /arg1 == 1/ { @drains = count(); }
fbt::myfirst_heartbeat:entry { @hb = count(); }
fbt::myfirst_watchdog:entry { @wd = count(); }
fbt::myfirst_tick_source:entry { @ts = count(); }
tick-1sec {
    printa("resets=%@u drains=%@u hb=%@u wd=%@u ts=%@u\n",
        @resets, @drains, @hb, @wd, @ts);
    trunc(@resets); trunc(@drains);
    trunc(@hb); trunc(@wd); trunc(@ts);
}'
```

A condensed diagnostic line per second. Useful as a sanity check during development.



## Reference: Inspecting Callout State From DDB

When a system hangs and you need to inspect callout state from the debugger, several DDB commands help.

### `show callout <addr>`

If you know a callout's address, this shows its current state: pending or not, scheduled deadline, callback function pointer, argument. Useful when you know which callout to inspect.

### `show callout_stat`

Dumps overall callout statistics: how many are scheduled, how many fired since boot, how many are pending. Useful for system-wide overview.

### `ps`

The standard process listing. Threads inside callout-processing are typically named `clock` or similar. They are usually in `mi_switch` or in the callback being executed.

### `bt <thread>`

Backtrace of a specific thread. If the thread is inside a callout callback, the backtrace shows the call chain: the kernel's callout subsystem at the bottom, the callback at the top. This tells you which callback is running.

### `show all locks`

If a callout's callback is currently executing, the backtrace will show `mtx_lock` (the kernel acquiring the callout's lock). `show all locks` confirms which lock is held and by which thread.

### Combined: Inspecting a Hung Callout

```text
db> show all locks
... shows myfirst0 mutex held by thread 1234

db> ps
... 1234 is "myfirst_heartbeat" (or similar)

db> bt 1234
... backtrace shows _cv_wait or similar; the callback is sleeping (which it should not!)
```

If you see this, the callback is doing something illegal (sleeping with a non-sleepable lock held). The fix is to remove the sleeping operation from the callback.



## Reference: Comparing Tick-Based and SBT-Based APIs

The two callout APIs (tick-based and sbintime-based) deserve a side-by-side comparison.

### Tick-Based API

```c
callout_reset(&co, ticks, fn, arg);
callout_schedule(&co, ticks);
```

The delay is in ticks: integer count of clock interrupts. On a 1000 Hz kernel, one tick is one millisecond. Multiply seconds by `hz` to convert; e.g., `5 * hz` for five seconds, `hz / 10` for 100 ms.

Pros: simple, well-known, fast (no sbintime arithmetic).
Cons: precision limited to one tick (1 ms typical); cannot express sub-tick delays.

Use for: most callout work. Watchdogs at second-level intervals, heartbeats at hundred-millisecond intervals, periodic polling at tens-of-milliseconds intervals.

### SBT-Based API

```c
callout_reset_sbt(&co, sbt, prec, fn, arg, flags);
callout_schedule_sbt(&co, sbt, prec, flags);
```

The delay is an `sbintime_t`: high-precision binary fixed-point time. Use `SBT_1S`, `SBT_1MS`, `SBT_1US`, `SBT_1NS` constants to construct values.

Pros: sub-tick precision; explicit precision/coalescing argument; explicit flags for absolute vs relative time.
Cons: more arithmetic; must understand `sbintime_t`.

Use for: callouts that need sub-millisecond precision (network protocols, hardware controllers with tight timing requirements). Most driver work does not need this.

### Conversion Helpers

```c
sbintime_t  ticks_to_sbt = tick_sbt * timo_in_ticks;  /* tick_sbt is global */
sbintime_t  ms_to_sbt = ms_value * SBT_1MS;
sbintime_t  us_to_sbt = us_value * SBT_1US;
```

The `tick_sbt` global gives you the sbintime equivalent of one tick; multiply by your tick count to convert.



## Reference: Pre-Production Callout Audit

A short audit to perform before promoting a callout-using driver from development to production. Each item is a question; each item should be answerable with confidence.

### Callout Inventory

- [ ] Have I listed every callout the driver owns in `LOCKING.md`?
- [ ] For each callout, have I named its callback function?
- [ ] For each callout, have I named the lock it uses (if any)?
- [ ] For each callout, have I documented its lifetime (init in attach, drain in detach)?
- [ ] For each callout, have I documented its trigger (what causes it to be scheduled)?
- [ ] For each callout, have I documented whether it re-arms (periodic) or fires once (one-shot)?

### Initialization

- [ ] Does every callout init use `callout_init_mtx` (or `_rw`/`_rm`) rather than the bare `callout_init`?
- [ ] Is the init called after the lock it references is initialized?
- [ ] Is the lock kind correct (sleep mutex for sleepable contexts, etc.)?

### Scheduling

- [ ] Does every `callout_reset` happen with the appropriate lock held?
- [ ] Is the interval reasonable for the work the callback does?
- [ ] Is the conversion from milliseconds to ticks correct (`(ms * hz + 999) / 1000` for ceil)?
- [ ] If the callout is periodic, does the callback re-arm only under a documented condition?

### Callback Hygiene

- [ ] Does every callback start with `MYFIRST_ASSERT(sc)` (or equivalent)?
- [ ] Does every callback check `is_attached` before doing work?
- [ ] Does every callback exit early if `is_attached == 0`?
- [ ] Does the callback avoid sleeping operations (`uiomove`, `cv_wait`, `mtx_sleep`, `malloc(M_WAITOK)`, `selwakeup`)?
- [ ] Is the callback's total work time bounded?

### Cancellation

- [ ] Does the sysctl handler use `callout_stop` to disable the timer?
- [ ] Does the sysctl handler hold the lock when calling `callout_stop` and `callout_reset`?
- [ ] Are there any code paths that can race with the sysctl handler?

### Detach

- [ ] Does the detach path drop the mutex before calling `callout_drain`?
- [ ] Does the detach path drain every callout?
- [ ] Are the callouts drained in the correct phase (after `is_attached` is cleared)?

### Documentation

- [ ] Is every callout documented in `LOCKING.md`?
- [ ] Are the discipline rules (lock-aware, no-sleep, drain-at-detach) documented?
- [ ] Is the callout subsystem mentioned in the README?
- [ ] Are there sysctls exposed that let users tune the behaviour?

### Testing

- [ ] Have I run the regression suite with `WITNESS` enabled?
- [ ] Have I tested detach with all callouts active?
- [ ] Have I run a long-duration stress test?
- [ ] Have I used `dtrace` to verify the firing rates match the configured intervals?

A driver that passes this audit is a driver you can trust under load.



## Reference: Standardising Timers Across a Driver

For a driver with several callouts, consistency matters more than cleverness. A short discipline.

### One Naming Convention

Pick a convention and follow it. The chapter's convention:

- The callout struct is named `<purpose>_co` (e.g., `heartbeat_co`, `watchdog_co`, `tick_source_co`).
- The callback is named `myfirst_<purpose>` (e.g., `myfirst_heartbeat`, `myfirst_watchdog`, `myfirst_tick_source`).
- The interval sysctl is named `<purpose>_interval_ms` (e.g., `heartbeat_interval_ms`, `watchdog_interval_ms`, `tick_source_interval_ms`).
- The sysctl handler is named `myfirst_sysctl_<purpose>_interval_ms`.

A new maintainer can add a new callout following the convention without thinking about names. Conversely, a code review immediately catches deviations.

### One Init/Drain Pattern

Every callout uses the same initialization and drain:

```c
/* In attach: */
callout_init_mtx(&sc-><purpose>_co, &sc->mtx, 0);

/* In detach (after dropping the mutex): */
callout_drain(&sc-><purpose>_co);
```

Or, with the macros:

```c
MYFIRST_CO_INIT(sc, &sc-><purpose>_co);
MYFIRST_CO_DRAIN(&sc-><purpose>_co);
```

The macros document the pattern in their definition; the call sites are short and uniform.

### One Sysctl Handler Pattern

Every interval sysctl handler follows the same structure:

```c
static int
myfirst_sysctl_<purpose>_interval_ms(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error;

        old = sc-><purpose>_interval_ms;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (new < 0)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        sc-><purpose>_interval_ms = new;
        if (new > 0 && old == 0) {
                /* enabling */
                callout_reset(&sc-><purpose>_co,
                    (new * hz + 999) / 1000,
                    myfirst_<purpose>, sc);
        } else if (new == 0 && old > 0) {
                /* disabling */
                callout_stop(&sc-><purpose>_co);
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

The handler's shape is the same for every interval sysctl. A new sysctl is mechanical to add.

### One Callback Pattern

Every periodic callback follows the same structure:

```c
static void
myfirst_<purpose>(void *arg)
{
        struct myfirst_softc *sc = arg;
        int interval;

        MYFIRST_ASSERT(sc);
        if (!sc->is_attached)
                return;

        /* ... do the per-firing work ... */

        interval = sc-><purpose>_interval_ms;
        if (interval > 0)
                callout_reset(&sc-><purpose>_co,
                    (interval * hz + 999) / 1000,
                    myfirst_<purpose>, sc);
}
```

Assert, check `is_attached`, do work, conditionally re-arm. Every callback in the driver has this shape; deviations stand out.

### One Documentation Pattern

Every callout is documented in `LOCKING.md` with the same fields:

- Lock used.
- Callback function.
- Behaviour (periodic or one-shot).
- Started by (which code path schedules it).
- Stopped by (which code path stops it).
- Lifetime (init in attach, drain in detach).

A new callout's documentation is mechanical. A code review can verify the documentation against the code.

### Why Standardise

Standardisation has costs: a new contributor must learn the conventions; deviations require a special reason. The benefits are larger:

- Reduced cognitive load. A reader who knows the pattern instantly understands every callout.
- Fewer mistakes. The standard pattern handles the common cases (lock acquisition, `is_attached` check, drain) correctly; a deviation is more likely to be wrong.
- Easier review. Reviewers can scan for shape rather than reading every line.
- Easier handoff. A maintainer who has not seen the driver can add a new callout following the existing template.

The cost of standardisation is paid once at design time. The benefits accrue forever. Always worth it.



## Reference: Further Reading on Timers

For readers who want to go deeper:

### Manual Pages

- `callout(9)`: the canonical API reference.
- `timeout(9)`: the legacy interface (deprecated; mentioned for historical reading).
- `microtime(9)`, `getmicrouptime(9)`, `getsbinuptime(9)`: time-reading primitives that callouts often use.
- `eventtimers(4)`: the event-timer subsystem that drives callouts.
- `kern.eventtimer`: the sysctl tree that exposes event-timer state.

### Source Files

- `/usr/src/sys/kern/kern_timeout.c`: the callout implementation.
- `/usr/src/sys/kern/kern_clocksource.c`: the event-timer driver layer.
- `/usr/src/sys/sys/callout.h`, `/usr/src/sys/sys/_callout.h`: the public API and structure.
- `/usr/src/sys/sys/time.h`: the sbintime constants and conversion macros.
- `/usr/src/sys/dev/led/led.c`: a small driver that exemplifies the callout pattern.
- `/usr/src/sys/dev/uart/uart_core.c`: a more elaborate use, including a polling fallback for hardware that does not interrupt for input.

### Manual Pages To Read in Order

For a reader new to FreeBSD's time subsystem, a sensible reading order:

1. `callout(9)`: the canonical API reference.
2. `time(9)`: units and primitives.
3. `eventtimers(4)`: the event-timer subsystem that drives callouts.
4. `kern.eventtimer` and `kern.hz` sysctls: runtime controls.
5. `microuptime(9)`, `getmicrouptime(9)`: time-reading primitives.
6. `kproc(9)`, `kthread(9)`: for when you genuinely need a kernel thread.

Each builds on the previous; reading in order takes a couple of hours and gives you a solid mental model of the kernel's time infrastructure.

### External Material

The chapter on timers in *The Design and Implementation of the FreeBSD Operating System* (McKusick et al.) covers the historical evolution of timer subsystems and the reasoning behind the current design. Useful as context; not required.

The FreeBSD developers' mailing list (`freebsd-hackers@`) occasionally discusses callout improvements and edge cases. Searching the archive for "callout" returns relevant historical context for how the API has evolved.

For deeper understanding of how the kernel schedules events at the lowest level, the eventtimers(4) manual page and the source under `/usr/src/sys/kern/kern_clocksource.c` are worth a careful read. They are below the level of this chapter (we do not interact with event timers directly), but they explain why the callout subsystem can deliver the precision it does.

Finally, real driver source. Pick any driver in `/usr/src/sys/dev/` that uses callouts (most do), read its callout-related code, and compare it to the patterns in this chapter. The translation is direct; you will recognize the shapes immediately. That kind of reading turns the chapter's abstractions into working knowledge.



## Reference: Callout Cost Analysis

A short discussion of what callouts actually cost, useful when deciding intervals or designing a high-frequency timer.

### Cost At Rest

A `struct callout` that has not been scheduled costs nothing beyond the sizeof the structure (about 80 bytes on amd64). The kernel does not know about it. It sits in your softc, doing nothing.

A `struct callout` that has been scheduled but not yet fired costs slightly more: the kernel has linked it into a wheel bucket. The link entries cost a few bytes. The kernel does not poll the structure; it only looks at it when the relevant bucket comes due.

The hardware-clock interrupt (which drives the wheel) fires `hz` times per second (typically 1000). It is essentially zero-cost in the empty case (no callouts due) and proportional to the number of due callouts in the busy case.

### Cost Per Firing

When a callout fires, the kernel does roughly:

1. Walk the wheel bucket; find the callout. Constant time per callout in the bucket.
2. Acquire the callout's lock (if any). Cost depends on contention; typically nanoseconds.
3. Call the callback function. Cost depends on the callback.
4. Release the lock. Microseconds.

For a typical short callback (a few microseconds of work), the per-firing cost is dominated by the callback itself plus the lock acquisition. The kernel's overhead is negligible.

### Cost At Cancel/Drain

`callout_stop` is fast: linked-list removal plus an atomic flag update. Microseconds.

`callout_drain` is fast if the callout is idle (just like `callout_stop`). If the callback is currently firing, the drain waits via the sleep-queue mechanism; the wait time depends on how long the callback takes.

### Practical Implications

Hundreds of pending callouts: no problem. The wheel handles them efficiently.

Thousands of pending callouts: still no problem in normal operation. Walking a wheel bucket of dozens of callouts is fast.

A single callout that fires at 1 Hz: essentially free. One hardware interrupt out of a thousand walks the bucket and finds the callout.

A single callout that fires at 1 kHz: starts to be measurable. One thousand callbacks per second adds up. If the callback takes 10 microseconds, that is 1% of one CPU. If the callback is heavier, more.

A callout at 10 kHz or faster: probably the wrong design. Reach for a busy-poll or a hardware timer or a specialised mechanism.

### Comparison To Other Approaches

A kernel thread that loops on `cv_timedwait` and does work each wake costs:

- Memory: ~16 KB stack.
- Per wake: scheduler entry, context switch, callback, context switch back.

For a 1 Hz workload, the kernel-thread cost (one wake per second) is about the same as the callout cost. For a 1 kHz workload, both are similar. For a 10 kHz workload, both are getting expensive; consider whether you really need that frequency.

A user-space loop polling a sysctl:

- Memory: a whole user process (megabytes).
- Per poll: syscall round-trip, sysctl handler invocation, return to user space.

Always more expensive than a kernel callout. Only appropriate when the polling logic genuinely belongs in user space (a monitoring tool, an external probe).

### When To Worry About Cost

Most drivers do not have to. Callouts are cheap; the kernel is well-tuned. Worry about cost only when:

- Profiling shows callouts dominate CPU usage. (Use `dtrace` to confirm.)
- You are writing a high-frequency driver (network or storage with tight latency requirements).
- The system has thousands of callouts active and you want to understand the load.

In all other cases, write the callout naturally and trust the kernel to handle the load.



## Looking Ahead: Bridge to Chapter 14

Chapter 14 is titled *Taskqueues and Deferred Work*. Its scope is the kernel's deferred-work framework as seen from a driver: how to move work out of a context that cannot safely run it (a callout callback, an interrupt handler, an epoch section) and into a context that can.

Chapter 13 prepared the ground in three specific ways.

First, you already know that callout callbacks run under a strict context contract: no sleeping, no sleepable-lock acquisition, no `uiomove`, no `copyin`, no `copyout`, and no `selwakeup` with a driver mutex held. You saw that contract enforced at the line in `myfirst_tick_source` where `selwakeup` was deliberately omitted because the callout context could not legally make the call. Chapter 14 introduces `taskqueue(9)`, which is the primitive the kernel offers for exactly this kind of hand-off: the callout enqueues a task, and the task runs in thread context where the omitted call is legal.

Second, you already know the drain-at-detach discipline. `callout_drain` ensures no callback is running when detach proceeds. Tasks have a matching primitive: `taskqueue_drain` waits until a specific task is neither pending nor running. The mental model is the same; the ordering grows by one step (callouts first, tasks second, then everything they affect).

Third, you already know the shape of `LOCKING.md` as a living document. Chapter 14 extends it with a Tasks section that names every task, its callback, its lifetime, and its place in the detach order. The discipline is the same; the vocabulary is a little wider.

Specific topics Chapter 14 will cover:

- The `taskqueue(9)` API: `struct task`, `TASK_INIT`, `taskqueue_create`, `taskqueue_start_threads`, `taskqueue_enqueue`, `taskqueue_drain`, `taskqueue_free`.
- The predefined system taskqueues (`taskqueue_thread`, `taskqueue_swi`, `taskqueue_fast`, `taskqueue_bus`) and when a private taskqueue is preferable.
- The coalescing rule: what happens when a task is enqueued while it is already pending.
- `struct timeout_task` and `taskqueue_enqueue_timeout` for deferred-and-scheduled work.
- Patterns that recur in real FreeBSD drivers, and the debugging story for when they go wrong.

You do not need to read ahead. Chapter 13 is sufficient preparation. Bring your `myfirst` driver (Stage 4 of Chapter 13), your test kit, and your `WITNESS`-enabled kernel. Chapter 14 starts where Chapter 13 ended.

A small closing reflection. You started this chapter with a driver that could not act on its own: every line of work was triggered by something the user did. You leave with a driver that has internal time, that periodically logs its state, that detects stalled drainage, that injects synthetic events for testing, and that tears down all of that infrastructure cleanly when the module is unloaded. That is a real qualitative leap, and the patterns transfer directly to every kind of driver Part 4 will introduce.

Take a moment. The driver you started Part 3 with knew how to handle one thread at a time. The driver you have now coordinates many threads, supports configurable timed work, and tears down without a race. From here, Chapter 14 adds the *task*, which is the missing piece for any driver whose timer callbacks need to trigger work that callouts cannot safely perform. Then turn the page.

### A Final Aside on Time

One last thought before Chapter 14. You have spent two chapters on synchronization (Chapter 12) and one chapter on time (Chapter 13). The two are deeply related: synchronization is, at its core, about *when* events happen relative to each other, and time is the explicit measure of that. Locks serialize accesses; cvs coordinate waits; callouts fire at deadlines. All three are different ways of slicing the same underlying question: how do independent execution streams agree on order?

Chapter 14 adds a fourth piece: *context*. A callout fires at a precise moment, but the context in which it fires (no sleeping, no sleepable locks, no user-space copy) is narrower than what most real work needs. Deferred work via `taskqueue(9)` is the bridge from that narrow context into a thread context where the full set of kernel operations is legal.

The patterns transfer. The lock-aware initialization that callouts use for their callbacks is the same shape you will apply when deciding which lock a task callback acquires. The drain pattern that callouts use at detach is the same shape that tasks use at teardown. The "do little here, defer the rest" discipline that callouts demand is the discipline that Chapter 14 gives you a concrete tool to follow.

So when you reach Chapter 14, the framework will already be familiar. You will be adding one more primitive to your driver's toolkit. Its rules compose cleanly with the callout rules you now know. The tools you have built (LOCKING.md, the seven-phase detach, the assert-and-check-attached pattern) will absorb the new primitive without growing brittle.

That is what makes Part 3 of this book work as a unit. Each chapter adds one more dimension to the driver's awareness of the world (concurrency, synchronization, time, deferred work), and each builds on the previous chapter's infrastructure. By the end of Part 3, your driver will be ready for Part 4 and the real hardware that lies beyond.
