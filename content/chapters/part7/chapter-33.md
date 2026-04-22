---
title: "Performance Tuning and Profiling"
description: "Optimizing driver performance through profiling and tuning"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 33
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 225
---

# Performance Tuning and Profiling

## Introduction

Chapter 32 ended with a driver that attaches on an embedded board, reads its pin assignments from a Device Tree, toggles an LED, and detaches cleanly. That chapter answered one question: *does the driver work?* The present chapter answers a different one: *does it work well?* The two questions sound similar and are profoundly distinct.

A driver that works is one whose code path produces the expected result for each valid input. A driver that works well is one whose code path produces that result with the right throughput, the right latency, the right CPU cost, and the right memory footprint for the machine it runs on. You can write a driver that passes every functional test and still makes the system sluggish, miss deadlines, drop packets, or overheat a small board. In kernel work, correctness is the floor. Performance is the view above it.

We are going to spend this chapter building the habits, tools, and mental model that turn an observation like *the system feels slow* into a diagnosis like *the driver's write path is acquiring a contended mutex twice per byte, which on this workload adds forty microseconds per call*. The diagnosis, not the fix, is the hard part. The fix is usually small. What takes experience is the discipline to resist guessing and the skill to measure the right thing.

There is a joke among performance engineers that every beginner optimisation starts with the words *I thought it would be faster if...*. That sentence is the single most reliable source of regressions in kernel code. A cache-friendly layout in the abstract can become a cache-hostile layout on a particular processor. A lock-free queue can be slower than a mutex-guarded one at low contention. A carefully tuned interrupt coalescing window can ruin latency for a small class of workloads you did not have in mind. The only antidote to *I thought it would be faster* is *I measured that it is faster*, and the whole point of this chapter is to give you a toolkit with which *I measured that* becomes a habit.

FreeBSD gives you an unusually rich set of measurement tools. DTrace, originally brought into the tree from Solaris, lets you probe almost any function in the kernel without recompiling anything. The `hwpmc(4)` subsystem and the `pmcstat(8)` userland tool expose the CPU's hardware performance counters: cycles, instructions, cache misses, branch mispredictions, stalls. The `sysctl(9)` interface and the `counter(9)` framework give you cheap, safe, always-on counters you can leave in production. The `log(9)` kernel logging facility gives you a rate-limited channel for informational messages. The `ktrace(1)` subsystem traces system-call boundaries. The `top(1)` and `systat(1)` tools let you see where CPU time is going in aggregate. Each of these tools has a particular strength, and the chapter will spend careful time showing you which tool to reach for when.

We are also going to spend time on the *habits* of measurement rather than only the tools. Measurement is easy to do badly. A driver that is instrumented heavily may not behave like the uninstrumented version; the probes change timing. A counter that is written from every CPU to a single cache line can become the bottleneck the driver is trying to measure. A DTrace script that prints on every probe turns into a denial-of-service attack on `dmesg`. A sysctl that reads unaligned memory may appear fast because the compiler silently inlines it on x86-64 and slow on arm64. The tools you use to measure *affect* what you measure, and you need the habit of remembering this while you work.

The chapter has a clear arc. We start in Section 1 with what performance is, why it matters, when it is worth pursuing, and how to set measurable goals. Section 2 introduces the kernel's timing and measurement primitives: the different `nano*` and `bintime` calls, the precision-versus-cost trade-off between them, and the habits for counting operations and timing them without wrecking the code path you are trying to observe. Section 3 is the DTrace chapter-within-a-chapter: we look at the providers most useful to driver authors, the form of a useful one-liner, the shape of a longer script, and the `lockstat` provider that specialises in lock contention. Section 4 introduces hardware PMCs, explains what cycles, cache misses, and branches actually measure, and walks through a `pmcstat(8)` session from sample collection to a flame-graph-style view of a driver's hot functions. Section 5 turns inward, toward the driver: cache-line alignment, buffer pre-allocation, UMA zones, per-CPU counters via `DPCPU_DEFINE(9)` and `counter(9)`. Section 6 looks at interrupt handling and taskqueue use, because interrupts are often where the first performance cliff sits. Section 7 covers the production-grade runtime metrics you leave in the driver after tuning ends: sysctl trees, rate-limited logging, and the kinds of debug modes that belong in a shipped driver. Section 8 is the discipline section: how to remove your scaffolding, document what you measured, and ship the optimised driver as a version the rest of the system can rely on.

After Section 8 you will find hands-on labs, challenge exercises, a troubleshooting reference, a Wrapping Up section, and a bridge into Chapter 34.

One practical note before we begin. Performance work in a kernel is easier than it was in the 1990s and harder than it appears today. The 1990s problem was that the tools were few and the documentation was thin. The modern problem is the opposite: the tools are many, each is capable, and it is easy to use the wrong one or to chase a metric that does not match the workload. Pick the simplest tool that answers your question. A DTrace one-liner that shows you a function runs twice as often as you expected is worth more than a six-hour `pmcstat` session that produces a flame graph of noise. The most valuable skill is taste in picking the question, not virtuosity with any single tool.

Let us begin.

## Reader Guidance: How to Use This Chapter

Chapter 33 sits at a particular place in the book's arc. Earlier chapters taught you how to build drivers that do their job, survive bad input, integrate with the kernel's frameworks, and run on diverse platforms. This chapter teaches you how to look at those drivers with measuring instruments and decide whether they are doing their job *well*. The orientation is inward, toward code you have already written, and outward, toward the tools FreeBSD gives you to look at that code while it runs.

There are two reading paths. The reading-only path takes about three to four focused hours. You finish with a clear mental model of what the FreeBSD performance tools do, when to use each, and how a disciplined tuning cycle looks from goal to fix. You will not have produced a tuned driver, but you will be able to read a `pmcstat` output, a DTrace aggregation, a lockstat summary, or a sysctl metric from a colleague and understand what it says.

The reading-plus-labs path takes seven to ten hours spread across two or three sittings. The labs are built around a small pedagogical driver called `perfdemo`, a character device that synthesises reads from an in-kernel generator at a configurable rate. Across the chapter you will instrument `perfdemo` with counters and sysctl nodes, trace it with DTrace, sample it with `pmcstat`, tighten its memory and interrupt paths, expose runtime metrics, and finally strip the scaffolding and ship a v2.3-optimized version. Every lab ends in something you can observe on a running FreeBSD system. You do not need embedded hardware; an ordinary amd64 or arm64 FreeBSD machine, physical or virtual, is enough.

If you happen to have a machine with a hardware PMU and privileges to load the `hwpmc(4)` kernel module, you will gain the most from Section 4. Most consumer x86 and arm64 systems do have useable counters. Virtual machines expose a subset; some cloud environments expose none. When a lab requires PMC support, we will flag it and offer an alternative path using `profile` DTrace sampling that works everywhere.

### Prerequisites

You should be comfortable with the driver material from Parts 1 through 6 and the embedded-focused Chapter 32. In particular you should know:

- What `probe()`, `attach()`, and `detach()` do and when each runs.
- How to declare a softc, how `device_t` and the kobj method table fit together, and how `DRIVER_MODULE(9)` registers a driver with newbus.
- How to allocate and release a mutex via `mtx_init(9)`, `mtx_lock(9)`, `mtx_unlock(9)`, and `mtx_destroy(9)`.
- How `bus_setup_intr(9)` registers an interrupt filter or handler, and the difference between filter and ithread handlers.
- How to allocate memory with `malloc(9)`, release it with `free(9)`, and when `M_WAITOK` versus `M_NOWAIT` is appropriate.
- How to declare a `sysctl` node with the static macros (`SYSCTL_DECL`, `SYSCTL_NODE`, `SYSCTL_INT`, and siblings) and the dynamic context (`sysctl_ctx_init(9)`).
- How to run `kldload(8)`, `kldunload(8)`, and `sysctl(8)` on a FreeBSD system.

If any of these feel shaky, the relevant earlier chapter is a short revisit away. Chapter 7 covers module and lifecycle basics, Chapters 11 through 13 cover locking and interrupt handling, Chapter 15 covers sysctl, Chapter 17 covers taskqueues, Chapter 21 covers DMA and buffering, and Chapter 32 the newbus resource-acquisition patterns you will build on in Section 6.

You do not need prior experience with DTrace, `pmcstat`, or flame graphs. The chapter introduces each from scratch at the level a driver author needs, and points you at the manual pages when you want to go deeper.

### Structure and Pacing

Sections 1 and 2 are foundational. They introduce the vocabulary of performance and the kernel's measurement primitives. They are short by the standards of this chapter, because the book's earlier material already assumes the reader knows how to read a `sysctl` and how to reason about mutexes.

Sections 3 and 4 are the tool-heavy sections. DTrace and `pmcstat` deserve thorough treatment because they are the two tools you will reach for most often. Expect to spend an hour on each if you are reading carefully.

Sections 5 and 6 are the tuning sections. They are the ones that tell you what to *change* once you have measured. They are also the sections most likely to tempt a reader into over-engineering; read them with the mindset that you will apply their techniques selectively, only after evidence demands it.

Section 7 is about the metrics that stay in the driver after tuning. This section is the one that turns a sprinter into a marathon runner; the drivers that age well are the ones that expose the right numbers, not the ones that went fastest on the benchmark day.

Section 8 closes the loop. It teaches you how to remove the temporary instrumentation that helped during tuning, document the benchmarks you ran, update the manual page with the tuning knobs the driver now exposes, and publish the driver at its new version.

Read the sections in order on a first pass. Each is written to stand alone enough for later reference, but the book's progressive teaching model depends on the order.

### Work Section by Section

Each section covers one clear piece of the subject. Read one, let it sit for a while, then move on. If the end of a section feels blurry, pause and re-read the final paragraphs; they are designed to consolidate the material before the next section builds on it.

### Keep the Lab Driver Close

The lab driver lives under `examples/part-07/ch33-performance/` in the book's repository. Each lab directory holds a self-contained stage of the driver with its `Makefile`, `README.md`, and any DTrace scripts or helper shell scripts. Clone the directory, work in place, build it with `make`, load it with `kldload`, and run the relevant measurements. The feedback loop between a kernel module and a sysctl or DTrace reading is the most educational feature of this chapter; use it.

### Open the FreeBSD Source Tree

Several sections refer to real FreeBSD files. The most useful ones to keep open for this chapter are `/usr/src/sys/sys/counter.h` and `/usr/src/sys/sys/pcpu.h`, which define the counter and per-CPU primitives; `/usr/src/sys/sys/time.h`, which declares the kernel's time functions; `/usr/src/sys/sys/lockstat.h`, which defines the lockstat DTrace probes; `/usr/src/sys/vm/uma.h`, which declares the UMA zone allocator; `/usr/src/sys/dev/hwpmc/`, the PMC driver tree; and `/usr/src/sys/kern/subr_taskqueue.c`, where `taskqueue_start_threads_cpuset()` lives. Open them when the chapter points you at them. The manual pages (`counter(9)`, `sysctl(9)`, `mutex(9)`, `dtrace(1)`, `pmc(3)`, `pmcstat(8)`, `hwpmc(4)`, `uma(9)`, `taskqueue(9)`) are the second-best references after the source.

> **A note on line numbers.** Each of those files will still define the symbols the chapter references: `sbinuptime` in `time.h`, the `lockstat` probe macros in `lockstat.h`, `taskqueue_start_threads_cpuset` in `subr_taskqueue.c`. Those names persist across FreeBSD 14.x point releases. The line each one sits on in your tree may have moved since this chapter was written, so search for the symbol rather than scrolling to a specific number.

### Keep a Lab Logbook

Continue the lab logbook from earlier chapters. For this chapter, note the *numbers* in particular. A log entry like *perfdemo v1.0, read() at 1000 Hz, median 14.2 us, P99 85 us* is the kind of evidence that lets you compare a later version honestly. Without the logbook, you will find yourself guessing a week from now whether a change made things faster; with it, you can check.

### Pace Yourself

Performance work is cognitively demanding. A reader who does two focused hours, takes a proper break, and does two more hours will almost always go further than a reader who grinds through five hours in one sitting. The tools and the data benefit from a rested mind.

## How to Get the Most Out of This Chapter

A few habits compound throughout the chapter. They are the same habits that experienced performance engineers use; the only trick is to start early.

### Measure Before You Change

This is the rule. Every optimisation should begin with a measurement that shows the current behaviour, end with a measurement that shows the new behaviour, and compare the two against a goal stated in advance. An optimisation without a before-and-after number is a guess that happened to pass review.

If you remember nothing else from this chapter, remember this.

### State the Goal in Numbers

A goal like *I want the read path to be faster* is not a goal. A goal like *I want median `read()` latency below 20 microseconds and P99 below 80 microseconds, on this workload* is a goal. You can measure it, you can know when you have achieved it, and you can stop optimising once you have. Most performance work runs long because the goal was vague.

### Prefer the Simplest Tool That Answers the Question

The temptation to reach for `pmcstat` with a callgraph and a flame graph is strong. Usually a DTrace one-liner is enough. The simplest tool that tells you which function dominates, which counter is climbing, or which lock is contended is the right tool. Escalate only when the simple tool runs out.

### Never Instrument the Hot Path at Production Cost

The act of measuring adds latency. A counter increment costs a few nanoseconds on modern hardware. A DTrace probe, when enabled, costs tens to hundreds of nanoseconds depending on the probe. A `printf` in the hot path costs tens of microseconds. Know these rough numbers before you sprinkle measurement code around. If the driver's budget is fifty microseconds, a single `printf` will eat the budget.

### Read the Manual Pages You Use

`dtrace(1)`, `pmcstat(8)`, `hwpmc(4)`, `sysctl(8)`, `sysctl(9)`, `counter(9)`, `uma(9)`, `taskqueue(9)`, `mutex(9)`, `timer(9)`, and `logger(1)` are the manual pages that matter for this chapter. Each is short, precise, and written by people who know the tool deeply. The book cannot replace them, only orient you.

### Type the Lab Code

The `perfdemo` driver in the labs is small for the same reason `edled` was small in Chapter 32: so that you can type it. Type it. The finger-memory of writing a sysctl tree, a DTrace probe definition, a counter increment, and a lock acquisition path is worth more than reading the same code ten times.

### Tag Every Measurement with Its Context

A number without context is useless. Every measurement should be recorded with the workload that produced it, the machine it ran on, the kernel version, the driver version, and what the system was also doing at the time. A driver that does 1.2 million operations per second on an idle server may do 400,000 on the same server while a backup is running. If you do not record the context, you will be mystified when the number changes and will blame the driver.

### Do Not Tune What Is Not Slow

Every section of this chapter will describe a technique you could apply. Apply them only when measurement demands. Cache-line alignment matters when false sharing is a real cost; on a low-concurrency driver it is clutter. Per-CPU counters matter when counter contention is real; on a low-throughput driver they are premature. Resist the temptation to sprinkle all the techniques because you have just learned them; the book has taught you the options, and the measurements tell you which to choose.

### Follow the Numbers Between Sections

As you work through the chapter, keep the `perfdemo` driver's numbers in your logbook. Each section should produce a small improvement that moves the numbers in the right direction. If a section's technique does not move your numbers, write that down too; it is equally valuable to know what *did not* help.

With those habits in hand, let us look at the reason any of this matters in the first place: the performance behaviour of the driver itself.

## Section 1: Why Performance Matters in Device Drivers

Every driver is a performance contract whether its author writes one or not. The contract says: *when you ask me to do X, I will do X in this amount of time, using this much CPU, with this peak memory, and with this variability.* A driver that keeps its contract is a well-behaved part of the system. A driver that breaks it produces the symptoms everyone on the system feels: audio skipping, video tearing, packets dropped, storage queues piling up, shells hanging on a write, interrupt-driven sensors missing events, a UI stuttering even though the machine has spare CPU. Most user-visible performance problems on any non-trivial system have a driver somewhere in their call path.

The contract has four axes. Learning to see them as separate is the first real step of this chapter.

### The Four Axes of Driver Performance

**Throughput** is the amount of work the driver can do per unit time. For a network driver it is packets per second, or bytes per second. For a storage driver it is I/O operations per second (IOPS) or megabytes per second. For a character device it is reads or writes per second. For a GPIO driver it is pin toggles per second. Throughput answers the question *how much?*

**Latency** is the time between a request and its completion. For a network driver it is the microseconds between a packet arriving and the stack handing it to the protocol layer. For a storage driver it is the milliseconds (or now microseconds) between a `read()` call and the data being available. For an input driver it is the time between a key press and the userland process seeing the event. Latency answers the question *how fast?*, and unlike throughput it is usually measured as a distribution. A median (P50) of 10 microseconds with a P99 of 500 microseconds is not the same driver as a median of 50 microseconds with a P99 of 60 microseconds.

**Responsiveness** is a related but distinct axis: how quickly the driver wakes up and does its work after an event, from the perspective of other threads. A driver may have fine latency on its hot path but still be unresponsive if it holds a lock for ten milliseconds, if it busy-waits on a register poll, or if it queues work on a taskqueue that shares threads with some other slow subsystem. Responsiveness is what users and schedulers experience; latency is what the driver itself would report.

**CPU cost** is how much CPU time each unit of work consumes. A driver doing 100,000 operations per second at 1% CPU is cheaper than one doing the same work at 10% CPU. On a small embedded board this is the axis that decides whether a sensor loop is feasible at all; on a larger server it is the axis that decides how much else can share the machine.

These four axes are not independent. A driver can trade throughput for latency: batch more work per interrupt and you raise throughput but also raise the latency each packet sees. It can trade latency for CPU: poll more often and you lower latency but raise CPU cost. It can trade responsiveness for throughput: hold the lock longer per call and each call is cheaper but contention rises. You cannot minimise all four at once; performance work is about choosing the trade-off that matches the workload.

That, in a sentence, is why measurement matters. Without numbers, you cannot know which axis is actually the problem, and you cannot know whether your change improved the right one.

### A Worked Example: Where Do These Axes Show Up?

Consider a network driver handling a small flow of 100 packet-per-second control traffic and a large flow of 1 gigabit-per-second bulk transfer at the same time. Each flow needs a different performance profile.

The small flow wants low latency. A 1 ms spike on a control packet may miss a deadline that the higher layer cares about. For this flow, you would rather handle each packet immediately than batch. The driver's RX path should wake up the protocol layer on every packet, the interrupt should be serviced with low delay, and any locks the packet passes through should be held briefly.

The large flow wants high throughput and is relatively insensitive to latency. Each packet in the bulk transfer can sit in a receive queue for tens of microseconds without anyone noticing, and the system benefits if the driver batches interrupt processing to reduce per-packet overhead. For this flow, interrupt coalescing is your friend and a dedicated taskqueue thread makes sense.

A driver serving only one of these flows can tune for it. A driver serving both has to pick a compromise, or maintain multiple queues with different policies, or provide a tuning knob that lets the operator choose. In all three cases, the driver author needs to know which axis matters for the workload the driver will actually face.

Real FreeBSD drivers do make this kind of distinction. The `iflib(9)` framework that many modern network drivers use provides both a fast-path receive for low-latency delivery and a rxeof batching loop for throughput. The `cam(4)` storage subsystem distinguishes between synchronous I/O that blocks a thread and asynchronous I/O that does not. The `hwpmc(4)` module distinguishes between counting mode (low-overhead, always on) and sampling mode (higher overhead, diagnostic). Every mature driver tree in FreeBSD has these distinctions built in somewhere; noticing them is part of learning to read kernel code as a performance engineer.

### When Optimisation Is Worth Doing

Not every driver needs tuning. A GPIO driver that toggles a pin a few times a second is doing a fine job even if every operation takes a millisecond. A debug pseudo-device that logs one line per hour is as fast as it needs to be. An ACPI-hotplug-notification driver that fires once per week spends almost no time in any state. For drivers like these, spending a day profiling would be a day wasted.

The drivers that usually merit tuning share some common features. They sit in a *hot path*, a sequence of calls that a high-rate or latency-sensitive workload passes through many times per second. They are in the ladder from the userland syscall to the hardware, with enough layers above and below them to make the driver a measurable fraction of total cost. They are on devices whose hardware is fast enough that the driver, not the device, becomes the bottleneck. And they are visible to users or to tests, so that a regression will be noticed and a speedup will be valued.

The classic examples are high-speed network drivers (10G, 25G, 40G, 100G), NVMe storage drivers, virtualisation I/O drivers (virtio-net, virtio-blk), audio drivers with tight latency budgets, and on embedded systems any driver in the main control loop of the product. If a driver is on this list, measuring it and tuning it pays back many times over. If it is not, tune only when a measurement (or a complaint from a user) points at the driver as the problem.

### When Optimisation Is Premature

Knuth's remark that *premature optimisation is the root of all evil* was about the middle layer of a program, not its outermost design decisions. The saying applies to drivers in a particular way: it is almost always wrong to spend effort optimising code before it works correctly, before it is measured, or before a goal has been set. The reason is not that optimisation is never warranted; it is that early optimisation tends to make code harder to reason about, harder to debug, and harder to change when the real bottleneck turns out to be somewhere else.

Concretely, the following are red flags for premature optimisation in a driver:

- Hand-written SIMD code in a driver whose throughput is measured in thousands, not billions, of operations per second.
- A carefully cache-line-aligned softc in a driver that has at most one outstanding operation per CPU at a time.
- A lock-free ring buffer in a driver whose real bottleneck is a `malloc(9)` call per operation.
- A per-CPU counter farm in a driver that runs one operation per second.
- An elaborate taskqueue scheme in a driver whose interrupt handler runs in 2 microseconds.

In each case the technique is real and legitimate, but it has been chosen without evidence. The effort costs something (complexity, reviewability, risk of bugs), and it produces a benefit only when evidence shows it is needed. The discipline is not *never optimise*; it is *measure, then decide*.

### Setting Measurable Goals

A performance goal has four parts: the **metric**, the **target**, the **workload**, and the **environment**.

The metric names what you are measuring. *Median `read()` latency*, *packets per second forwarded*, *interrupts per second serviced*, *CPU percentage at full load*, *peak kernel memory used*, or *P99 worst-case latency of the probe-to-attach path* are all valid metrics. Each is a single number produced by a single procedure.

The target names the value you want the metric to have. *Below 20 microseconds*, *above 1 million packets per second*, *under 5% CPU*, *under 4 MB of kernel memory at peak*. The target should be concrete and measurable.

The workload names the conditions under which the metric is produced. *`read()` calls issued at 10,000 Hz with `M_WAITOK` allocation*, *one TCP stream at the line rate on a 10 Gbps NIC*, *a 4K random-read test against a 16 GB file*. The workload should be reproducible; if no one can rerun it, the measurement is not useful.

The environment names the machine, the kernel, and the surrounding processes. *amd64, 8-core Xeon at 3.0 GHz, FreeBSD 14.3 GENERIC kernel, otherwise idle*. Two different machines will give different numbers for the same driver, and a measurement that is valid on one environment may be invalid on another.

A goal of the form *the `perfdemo` driver's median `read()` latency, measured across 100,000 calls on an idle amd64 Xeon E5-2680 at 3.0 GHz running FreeBSD 14.3 GENERIC, with one userland reader thread pinned to CPU 1, should be below 20 microseconds* is a complete performance goal. It has a metric, a target, a workload, and an environment. You can run it, record the result, compare a tuning change, and decide whether you are done.

A goal of the form *make the driver faster* is useless. It has no metric, no target, no workload, no environment. You can work on it for a year and still not know when to stop.

The discipline of writing the goal down before you start is where most of the quality in a performance project comes from. If you cannot write the goal, you are not ready to optimise. The rest of the chapter will assume you have one, and will teach you the tools that let you check it.

### A Note on "Fast" and "Correct"

Two failures of performance work deserve to be named early.

The first is that a driver that is fast in one dimension can be bad overall. A driver that achieves blistering throughput by dropping occasional packets silently is not faster; it is broken. A driver that halves its latency by skipping a safety check in the fast path is not faster; it is risky. A driver that reduces CPU by deferring free until unload is not faster; it is leaking. Every optimisation must preserve correctness, including the correctness of error paths and the behaviour under pathological input. An optimisation that trades correctness for speed is a regression, full stop.

The second failure is that a driver that is fast today can be a debugging nightmare tomorrow. Performance work often introduces complexity: pre-allocation pools, multiple locks, batched operations, inline assembly for atomics, careful memory layouts. Each of these is a future debugging cost. Before you commit to a complex optimisation, ask whether a simpler one is good enough. The driver that lives in the tree for years is not usually the one that was fastest in the benchmark; it is the one a maintainer can still read and reason about after the original author has moved on.

We will return to both points in Section 8 when we talk about shipping a tuned driver.

### Exercise 33.1: Define Performance Goals for a Driver You Have

Pick a driver you have written in earlier chapters. `perfdemo` will appear in Section 2, so for now choose an earlier one: `nullchar` from Chapter 7, the ring-buffer character driver from Chapter 12, or the `edled` from Chapter 32. For that driver, write performance goals for at least two of the four axes (throughput, latency, responsiveness, CPU cost). For each goal, include a metric, a target, a workload, and an environment. Keep the goals in your logbook.

You do not have to have measured the driver yet. The exercise is the writing of the goal. If you cannot decide on a target, write *unknown, to be measured in Section 2* and come back.

The act of writing a specific goal sharpens what the rest of the chapter is trying to teach. Readers who skip this exercise often find Sections 2 through 7 feel abstract; readers who do it find that each new tool has an obvious use.

### Wrapping Up Section 1

We opened with the claim that every driver is a performance contract whether the author writes one or not. The four axes of throughput, latency, responsiveness, and CPU cost give you a vocabulary for writing that contract down. The discipline of measurable goals, pinned to a metric, target, workload, and environment, gives you a way to know whether the contract is being kept. The reminders about premature optimisation and about the relationship between speed and correctness keep the work honest.

The rest of the chapter is the toolkit. In the next section we look at the kernel's measurement primitives: timing functions, counters, sysctl nodes, and the FreeBSD tools (`sysctl`, `dtrace`, `pmcstat`, `ktrace`, `top`, `systat`) that expose the metrics your goal depends on.

## Section 2: Measuring Performance in the Kernel

Measurement in userland is the world most programmers grow up in. You wrap a function call with a timer, you run it a million times, you print the average. Measurement in the kernel is not so different in shape, but it is more sensitive in practice. Kernel code runs at the boundary of the system; a careless measurement can slow down the very thing it is trying to measure, or worse, change the behaviour of the system in ways that make the measurement meaningless. The goal of this section is to teach you how to measure inside the kernel without contaminating the measurement.

We will move through four things: the kernel's time functions, the counter primitives that let you accumulate events cheaply, the tools that read those counters from userland, and the habits of instrumentation that keep the measurement honest.

### Kernel Time Functions

The kernel exposes several functions that return the current time. They differ in precision, cost, and whether they advance monotonically. The declarations are in `/usr/src/sys/sys/time.h`.

There are two orthogonal choices when you pick one of these. The first choice is **format**:

- `*time` variants return a `struct timespec` (seconds and nanoseconds) or `struct timeval` (seconds and microseconds).
- `*uptime` variants return the same format, but measured from the system's boot rather than from the wall clock. Uptime does not jump when the administrator adjusts the wall clock; wall time does.
- `*bintime` variants return a `struct bintime` (a fixed-point binary fraction of a second) and `sbinuptime()` returns a `sbintime_t`, a signed 64-bit fixed-point value. These are the kernel's internal, highest-resolution formats.

The second choice is **precision versus cost**. FreeBSD distinguishes between a *fast but imprecise* path and a *precise but more expensive* path:

- Functions with the **`get`** prefix (`getnanotime()`, `getnanouptime()`, `getbinuptime()`, `getmicrotime()`, `getmicrouptime()`, `getsbinuptime()`) return a value cached at the last timer tick. They are very fast, typically just a few loads from a cache-resident global. Their precision is on the order of `1/hz`, which on a default FreeBSD system is about 1 millisecond.
- Functions without the `get` prefix (`nanotime()`, `nanouptime()`, `binuptime()`, `microtime()`, `microuptime()`, `sbinuptime()`) call into the selected timecounter hardware and return a value accurate to the timecounter's resolution, often tens of nanoseconds. They cost more, typically tens to hundreds of nanoseconds, and on some hardware may serialise the CPU pipeline.

The golden rule: use the `get*` variants whenever 1 millisecond of precision is enough, and the non-`get` variants when you need real precision. A driver measuring its own hot-path latency generally needs the non-`get` variants. A driver timestamping a rare event, or recording the time of a state change, can almost always use the `get*` variants.

Here is the short version of the decision tree:

- Need wall-clock time, millisecond precision: `getnanotime()`.
- Need uptime (monotonic), millisecond precision: `getnanouptime()`.
- Need uptime, nanosecond precision: `nanouptime()`.
- Need uptime, highest resolution, minimal cost per call: `sbinuptime()` (or `getsbinuptime()` when millisecond precision is fine).
- Need to compute a duration: subtract two `sbinuptime()` values and convert to microseconds or nanoseconds at the end.

A representative timing in a driver read path might look like this:

```c
#include <sys/time.h>
#include <sys/sysctl.h>

static uint64_t perfdemo_read_ns_total;
static uint64_t perfdemo_read_count;

static int
perfdemo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    sbintime_t t0, t1;
    int error;

    t0 = sbinuptime();

    /* ... the real work of reading ... */
    error = do_the_read(uio);

    t1 = sbinuptime();

    /* Convert sbintime_t difference to nanoseconds.
     * sbt2ns() is defined in /usr/src/sys/sys/time.h. */
    atomic_add_64(&perfdemo_read_ns_total, sbttons(t1 - t0));
    atomic_add_64(&perfdemo_read_count, 1);

    return (error);
}
```

A few things are worth noticing. The timestamps are captured with `sbinuptime()`, not `getsbinuptime()`, because we are measuring microsecond-scale durations. The accumulator and counter are 64-bit and updated with `atomic_add_64()`, so concurrent readers do not lose updates. The conversion from `sbintime_t` to nanoseconds uses `sbttons()`, a macro declared in `/usr/src/sys/sys/time.h`; we do not do the division on the hot path. And neither the timestamps nor the accumulation print anything; the data goes into counters that a sysctl reads on demand.

The pattern generalises. Timestamp at the boundaries of the operation you care about, accumulate the difference, expose the accumulator through sysctl, and compute derived metrics (average, throughput) in userland.

### Time Stamp Counters and Other High-Precision Sources

Below the `nanotime` and `sbinuptime` family sits the hardware that actually feeds them: the processor's Time Stamp Counter (TSC) on x86, the Generic Timer on arm64, and their equivalents on other architectures. FreeBSD abstracts these sources behind a *timecounter* interface; the kernel selects one at boot and the `sbinuptime` path reads it. You can see which timecounter your system is using with `sysctl kern.timecounter`:

```console
# sysctl kern.timecounter
kern.timecounter.tick: 1
kern.timecounter.choice: ACPI-fast(900) HPET(950) i8254(0) TSC-low(1000) dummy(-1000000)
kern.timecounter.hardware: TSC-low
```

On a modern amd64 machine the TSC is almost always the chosen source. It advances at a constant rate regardless of CPU frequency (on processors that support invariant TSC, which is essentially everything since the mid-2000s), it is cheap to read (a single instruction), and its resolution is on the order of the CPU clock period, roughly 0.3 nanoseconds at 3 GHz.

A driver rarely needs to read the TSC directly. `sbinuptime()` already wraps it. But when you are debugging the kernel's own timing code, or when you need a timestamp that is *only* a load from a register with no further arithmetic, the kernel provides `rdtsc()` as a `static __inline` function in `/usr/src/sys/amd64/include/cpufunc.h`. Its use is almost always a mistake in driver code: you lose the kernel's unit conversions, you lose portability across architectures, and you gain a few nanoseconds. Reach for `sbinuptime()` instead; the portability and abstraction pay for themselves.

On arm64, the equivalent of the TSC is the Generic Timer's counter register, read via the ARM-specific `READ_SPECIALREG` macros in `/usr/src/sys/arm64/include/cpu.h`. The kernel exposes it through the same `sbinuptime()` abstraction, so a driver written to `sbinuptime()` is portable across the two architectures without change. This is one of the small but meaningful benefits of staying on the abstractions FreeBSD provides.

A subtle point: the different timecounter choices have different costs and different precisions. On the Intel and AMD generations currently in service, HPET is slow (on the order of hundreds of nanoseconds to read) but high-precision, ACPI-fast is fast but lower-precision, and TSC is fast and precise, which is why the kernel prefers it when available. If `kern.timecounter.hardware` shows anything other than TSC on an amd64 machine, something on the system disabled it, and `sbinuptime()` calls will be more expensive than you expect. Check `dmesg | grep timecounter` early in any performance investigation. See Appendix F for a reproducible benchmark that rotates through each timecounter source.

### Composition: Timing, Counting, and Aggregating Together

The examples above showed timing (a pair of `sbinuptime()` calls) and counting (a `counter_u64_add()` increment) separately. Real driver measurement almost always composes the two: you want to know *how many* of each kind of operation happened *and* what the *latency distribution* of each kind looked like. The composition is simple once the primitives are in hand.

A pattern that appears in many FreeBSD drivers is the counter-and-histogram pair. For every operation, you increment a counter and add its latency to a histogram bucket. The histogram is represented as an array of `counter_u64_t` values, one per bucket, with bucket boundaries chosen for the expected range. On the hot path, you do one counter increment for total count and one counter increment for the bucket the latency fell in:

```c
#define PD_HIST_BUCKETS 8

static const uint64_t perfdemo_hist_bounds_ns[PD_HIST_BUCKETS] = {
    1000,       /* <1us    */
    10000,      /* <10us   */
    100000,     /* <100us  */
    1000000,    /* <1ms    */
    10000000,   /* <10ms   */
    100000000,  /* <100ms  */
    1000000000, /* <1s     */
    UINT64_MAX, /* >=1s    */
};

static counter_u64_t perfdemo_read_hist[PD_HIST_BUCKETS];
static counter_u64_t perfdemo_read_count;

static int
perfdemo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    sbintime_t t0, t1;
    uint64_t ns;
    int error, i;

    t0 = sbinuptime();
    error = do_the_read(uio);
    t1 = sbinuptime();

    ns = sbttons(t1 - t0);
    counter_u64_add(perfdemo_read_count, 1);
    for (i = 0; i < PD_HIST_BUCKETS; i++) {
        if (ns < perfdemo_hist_bounds_ns[i]) {
            counter_u64_add(perfdemo_read_hist[i], 1);
            break;
        }
    }

    return (error);
}
```

The hot path does one `sbinuptime` pair (on the order of a few tens of nanoseconds each on typical FreeBSD 14.3-amd64 hardware), one multiplication to convert to nanoseconds, a linear scan of eight bucket bounds (branch-predicted well after warm-up), and two counter increments. As a rough order-of-magnitude figure on the same class of machine, the total overhead stays comfortably under a microsecond, which is acceptable for most read paths and small enough to leave on in production. See Appendix F for a reproducible benchmark of the underlying `sbinuptime()` cost on your own hardware.

Exposing the histogram via sysctl is a small extension of the procedural-sysctl pattern from Section 7. You fetch each bucket's counter, copy them into a local array, and hand the array to `SYSCTL_OUT()`. Userland reads the array and plots it.

If the expected latency range is known, the linear bucket search can be replaced by a constant-time mapping. For power-of-ten buckets, `log10` of the latency lands you in the right bucket; for power-of-two buckets, a single `fls` instruction suffices. The constant-time variants are only worth the complexity when the linear search appears as a real cost in a profile, which in practice happens only for very hot paths with many buckets.

### A Complete Instrumented Read Path

Putting the above together, a fully instrumented `perfdemo_read()` that captures call count, error count, total bytes, total latency, and a latency histogram looks like this:

```c
#include <sys/counter.h>
#include <sys/time.h>

struct perfdemo_stats {
    counter_u64_t reads;
    counter_u64_t errors;
    counter_u64_t bytes;
    counter_u64_t lat_ns_total;
    counter_u64_t hist[PD_HIST_BUCKETS];
};

static struct perfdemo_stats perfdemo_stats;

static int
perfdemo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    sbintime_t t0, t1;
    uint64_t ns, bytes_read;
    int error, i;

    t0 = sbinuptime();
    bytes_read = uio->uio_resid;
    error = do_the_read(uio);
    bytes_read -= uio->uio_resid;
    t1 = sbinuptime();

    ns = sbttons(t1 - t0);
    counter_u64_add(perfdemo_stats.reads, 1);
    counter_u64_add(perfdemo_stats.lat_ns_total, ns);
    counter_u64_add(perfdemo_stats.bytes, bytes_read);
    if (error)
        counter_u64_add(perfdemo_stats.errors, 1);
    for (i = 0; i < PD_HIST_BUCKETS; i++) {
        if (ns < perfdemo_hist_bounds_ns[i]) {
            counter_u64_add(perfdemo_stats.hist[i], 1);
            break;
        }
    }

    return (error);
}
```

This is the shape of a production-grade measurement. It counts, it times, it records distributions, and it costs a handful of nanoseconds per call on the hot path. The derived metrics (mean latency, P50, P95, P99 estimated from the histogram) are computed in userland on demand. The `counter(9)` values scale to tens of cores with no contention; the linear bucket search is cache-friendly; the conditional `errors` counter adds no cost on the success path.

In the sections that follow, we will tune other parts of the driver, but the measurement scaffolding above is the baseline we measure *against*. Every change we make will be judged against the numbers the scaffold produces.

### Counters: Simple, Atomic, Per-CPU

Kernel code often needs to count things: operations completed, errors, retries, interrupt storms. FreeBSD offers three grades of counter primitive, in order of increasing complexity and increasing scalability.

**A plain `uint64_t` updated with `atomic_add_64()`**. This is the simplest counter. It is correct under concurrency, it is cheap on modern hardware, and it works everywhere. At low concurrency (tens of thousands of updates per second from a handful of CPUs), it is a fine default. At high concurrency (hundreds of thousands of updates per second from many CPUs), the cache line holding the counter becomes a contention point and the atomic operation starts to show up in profiles. For most driver counters, the plain atomic is the right choice.

**A `counter(9)` value (`counter_u64_t`)**. This is a per-CPU counter with a simple fetch interface. You allocate it with `counter_u64_alloc()`, update it with `counter_u64_add()`, and read it with `counter_u64_fetch()` (which sums across all CPUs). Because each CPU updates its own memory, there is no contention on the update path. The trade-off is that the fetch iterates over all CPUs, so reading the counter is slightly more expensive. For drivers whose counters are updated on fast paths from many CPUs, `counter(9)` is a better fit than a plain atomic. The declarations are in `/usr/src/sys/sys/counter.h`.

**A `DPCPU_DEFINE(9)` variable**. DPCPU stands for *dynamic per-CPU*. It lets you define any type of variable, not just a counter, with per-CPU storage. You declare the variable with `DPCPU_DEFINE(type, name)`, access the current CPU's copy with `DPCPU_GET(name)` and `DPCPU_SET(name, v)`, and sum across all CPUs with `DPCPU_SUM(name)`. It is the most flexible of the three primitives, and it is what `counter(9)` is implemented on top of. Reach for it when you need per-CPU state that is not just a counter, or when you need a counter and have a specific reason to bypass the `counter(9)` abstraction. The declarations are in `/usr/src/sys/sys/pcpu.h`.

Here is a practical example. Suppose `perfdemo` has three counters: total reads, total errors, and total bytes delivered. The `counter(9)` form is:

```c
#include <sys/counter.h>

static counter_u64_t perfdemo_reads;
static counter_u64_t perfdemo_errors;
static counter_u64_t perfdemo_bytes;

/* In module init: */
perfdemo_reads  = counter_u64_alloc(M_WAITOK);
perfdemo_errors = counter_u64_alloc(M_WAITOK);
perfdemo_bytes  = counter_u64_alloc(M_WAITOK);

/* On the fast path: */
counter_u64_add(perfdemo_reads, 1);
counter_u64_add(perfdemo_bytes, bytes_delivered);
if (error)
    counter_u64_add(perfdemo_errors, 1);

/* In module fini: */
counter_u64_free(perfdemo_reads);
counter_u64_free(perfdemo_errors);
counter_u64_free(perfdemo_bytes);

/* In a sysctl handler that reports the values: */
uint64_t v = counter_u64_fetch(perfdemo_reads);
```

The `counter(9)` API keeps the hot-path cost small and constant regardless of how many CPUs your driver serves. On a 64-core server it is a transformational improvement over a single atomic. On a 4-core laptop it is a small but real improvement. On an embedded single-core board it is no better than an atomic. Use it when there will be contention, or when you want to be ready for the day a driver graduates from a 2-core test machine to a 64-core production one.

### Exposing Metrics via sysctl

A counter that nobody can read is a counter that does not exist. FreeBSD's `sysctl(9)` subsystem is the standard way to expose driver metrics to userland, and the chapter covers the production patterns in detail in Section 7. For now, the simplest pattern is:

```c
SYSCTL_NODE(_hw, OID_AUTO, perfdemo, CTLFLAG_RD, 0,
    "perfdemo driver");

SYSCTL_U64(_hw_perfdemo, OID_AUTO, reads, CTLFLAG_RD,
    &perfdemo_reads_value, 0,
    "Total read() calls");
```

For a `counter(9)` variable, the idiomatic way is to use a procedural sysctl that calls `counter_u64_fetch()` on each read. We will write that pattern out in Section 7. For now, the `SYSCTL_U64` macro above is enough for a plain `uint64_t` or for the `counter_u64_fetch()` result cached into a regular variable during a periodic callout.

From userland, `sysctl hw.perfdemo.reads` returns the value, and scripts can poll at intervals, compute rates, and produce plots.

### The Measurement Tools FreeBSD Gives You

On top of the primitives above, FreeBSD ships several userland tools that read the kernel's exposed metrics and present them in useful ways. A short tour follows; we will return to the important ones in later sections.

**`sysctl(8)`**. Reads any sysctl node. The most basic measurement interface. It is the tool for polling a counter every second to compute a rate, for checking a one-shot metric, and for dumping a subtree for later comparison. A script like `while sleep 1; do sysctl hw.perfdemo; done | awk ...` is a surprisingly common first step.

**`top(1)`**. Shows processes, threads, CPU use, memory use. With the `-H` flag it shows kernel threads, including interrupt and taskqueue threads. Useful when a driver has its own thread whose CPU use you want to watch, or when you want to see an ithread spinning on interrupts.

**`systat(1)`**. An older but still-useful family of monitoring views. `systat -vmstat` shows CPU, disk, memory, and interrupt rates. `systat -iostat` focuses on storage. `systat -netstat` focuses on the network. When you want a live monitor without writing a script, `systat` is often faster to reach for than anything more elaborate.

**`vmstat(8)`**. A per-second view of system-wide statistics. `vmstat -i` lists interrupt rates by vector. `vmstat -z` lists UMA zone activity, which is invaluable when tracking down kernel memory pressure.

**`ktrace(1)`**. Traces system-call entries and exits for specific processes. It is lower-level than DTrace and predates it, but remains useful when the interaction you want to see crosses the userland-kernel boundary and does not require kernel-side custom probes.

**`dtrace(1)`**. The Swiss army knife of FreeBSD kernel observation. Covered in depth in Section 3.

**`pmcstat(8)`**. The hardware-performance-counter tool. Covered in depth in Section 4.

The important insight is that these tools are not competitors. Each one is best at a particular kind of measurement. You learn performance work by learning which tool answers which question, and reaching for that one first.

### The Heisenberg Effect, Practically

The joking reference in the chapter's introduction was to a real problem: the act of measuring changes what is measured. In kernel code the problem has teeth, and you need to know its shape.

The first form of the problem is **direct overhead**. Every measurement has a cost. If you add a `printf` inside a read path that runs at 100,000 reads per second, you have added tens of microseconds per read, and the driver's effective rate drops to a fraction of what it would be without the print. If you time every operation with `sbinuptime()` and increment an atomic counter, you have added perhaps 50 nanoseconds per call; for most drivers this is acceptable, but in a nanosecond-budget hot path even this is too much. The rule of thumb: estimate the cost of each measurement probe and decide whether the driver can afford it on the path you are probing.

The second form is **cache and lock contention**. A single atomic counter updated by all CPUs is a cache line bouncing between them. On low-contention paths this costs a few nanoseconds. On high-contention paths it can cost hundreds of nanoseconds per update and dominate the operation. A counter that is supposed to be cheap can become the bottleneck it is trying to measure. The fix, as we saw above, is `counter(9)` or a DPCPU variable, both of which update per-CPU memory.

The third form is **observer-induced behaviour change**. This is the most insidious. Adding instrumentation can change what the system does beyond the direct cost. A `printf` may cause a timer that was about to fire to miss its deadline. A DTrace probe that turns out to use a slower code path can disable a fast-path optimisation. A breakpoint set in a debugger can hide a race that the unprobed code exhibits. Good measurement design tries to minimise observer effects, but the awareness that they exist is what separates a disciplined measurement practice from a superstitious one.

The practical consequence is that you should always *disable* measurement code when measuring the baseline, unless the measurement is itself what you are evaluating. If you want to know how fast a driver can go, run it with the minimum instrumentation needed to report the result. If the minimum instrumentation is expensive, reduce it. This is also why `counter(9)` is preferred over atomics for hot-path counting: not just because it is faster, but because it perturbs the system less.

### Instrumenting Without Contamination

The practical advice on adding measurement code distils into a small checklist. Before you add a probe, ask:

1. **What metric is this probe producing?** Every probe must correspond to a concrete line in the goals document. If you cannot name it, do not add it.
2. **On what path does the probe live?** A probe on the module-load path is free. A probe on a path that runs once per operation on a high-throughput driver must be cheap enough to fit the budget.
3. **What does the probe cost?** An atomic counter is a few nanoseconds. A `counter(9)` is about the same, with better scaling. A `sbinuptime()` timestamp is tens of nanoseconds. A `printf` is tens of microseconds. A DTrace probe, when enabled, is hundreds of nanoseconds.
4. **Is the probe always on, or only during an instrumentation mode?** A counter that stays in production is a feature. A detailed trace is a tool for a tuning session.
5. **Does the probe cause contention?** Writing to a single cache line from many CPUs is a contention problem; reading it is not.
6. **Does the probe change ordering?** An added memory barrier can hide a race; an added sleep can hide a contention problem.

Most of this is common sense once you have seen one or two measurements go wrong. Writing it down as a checklist early saves the pain of re-discovery.

### Exercise 33.2: Time the Read Path

Take a simple driver you have written earlier, or use the `perfdemo` scaffold we will introduce in the lab section. Add two `counter_u64_t` variables: `reads` and `read_ns_total`. In the read handler, timestamp with `sbinuptime()` at entry, do the work, timestamp again, add the nanosecond difference to `read_ns_total`, and increment `reads` by 1. Expose both via sysctl.

Build the driver. Load it. Run a userland loop that reads from the device 100,000 times. Then compute the average latency as `read_ns_total / reads` and record the number in your logbook. Next, remove the counter updates, build again, and rerun the test; if the timing tools allow, compare the wall-clock time of the 100,000-read loop with and without the counters. The difference is the probe overhead.

You will find that `counter(9)` updates are cheap enough to leave in production. That fact is one of the most useful practical takeaways of the chapter.

### Wrapping Up Section 2

Kernel measurement uses the same primitives as userland measurement but with tighter constraints. The `get*` time functions are for cheap, low-precision timestamps; the non-`get*` functions are for precise, more expensive ones. Counter primitives come in three grades, from plain atomics to `counter(9)` to DPCPU variables, with increasing scalability and flexibility. Exposure to userland is through `sysctl(9)`. The tools that read the exposed state (`sysctl(8)`, `top`, `systat`, `vmstat`, `ktrace`, `dtrace`, `pmcstat`) each specialise. And the discipline of measuring without contaminating the measurement is as important as the primitives themselves.

The next section introduces DTrace, which is the most general and capable of the kernel measurement tools FreeBSD gives you. DTrace deserves a whole section of its own because it answers the *what is happening right now* question better than anything else.

## Section 3: Using DTrace to Analyze Driver Behavior

DTrace is the single most useful tool in a kernel performance engineer's box. Its combination of expressiveness, low overhead when disabled, and ability to see almost any function in the kernel without recompiling has no peer. Section 3 is the longest section of the chapter for this reason; the time you spend learning DTrace pays back on every driver you ever measure.

The section assumes no prior DTrace experience. Readers who have used DTrace on Solaris or macOS will find FreeBSD's implementation familiar in substance, with a few differences in provider availability and in how to enable it.

### What DTrace Is

DTrace is a kernel facility that lets you attach small scripts to probes. A probe is a named point in the kernel at which you can observe what is happening: a function entry, a function return, a scheduler switch, an I/O completion, a packet arrival, a user-defined event in a driver. A script says *when this probe fires, do X*. The script is compiled into a safe bytecode that runs in the kernel's context, aggregated in memory, and read back into the userland `dtrace` process.

The design has four features that matter for driver work.

First, the probes are **pervasive**. Every non-static function in the kernel is a probe. So are scheduler events, every system call boundary, every lock operation, every I/O dispatch, and thousands of other points. You rarely need to add probes to find one you need.

Second, probes have **near-zero cost when disabled**. An unused probe is a NOP in the instruction stream; it does not slow the kernel. The overhead appears only when you enable the probe, and even then the enabled overhead is typically a few hundred nanoseconds per fire. You can leave your driver production-ready with a dozen SDT probes in it, and when DTrace is not running, the driver is exactly as fast as if the probes were not there.

Third, DTrace is **safe**. The scripts run in a constrained language that prohibits loops, arbitrary memory access, and any operation that could crash the kernel. A buggy script returns an error; it does not panic the system.

Fourth, DTrace **aggregates in the kernel**. You can keep per-thread counts, quantiles, average values, histograms, and similar summaries in the kernel and pull the summary out periodically. You do not need to emit every event to userland; most DTrace scripts emit aggregated summaries, which is why they scale to millions of events per second.

### Enabling DTrace on FreeBSD

DTrace is part of the base system and works out of the box on most FreeBSD installations, but it requires that the kernel have DTrace support compiled in and that the relevant modules be loaded. On a GENERIC kernel, the support is present. To use it, load the provider modules:

```console
# kldload dtraceall
```

The `dtraceall` module is a convenience that loads all the DTrace providers. If you only need a specific provider (for example, `fbt` for function-boundary tracing or `lockstat` for lock contention), you can load just that one:

```console
# kldload fbt
# kldload lockstat
```

After loading, `dtrace -l` lists the available probes. Expect the list to be long; a typical GENERIC kernel has tens of thousands of probes available.

### The Probe Name Format

Every DTrace probe has a four-tuple name of the form:

```text
provider:module:function:name
```

The **provider** is the source of the probe: `fbt` for function-boundary tracing, `sdt` for statically defined tracepoints, `profile` for timed sampling, `sched` for scheduler events, `io` for I/O completion, `lockstat` for lock events, and several others.

The **module** is the kernel module the probe lives in: `kernel` for core kernel code, `your_driver_name` for your own module, `geom_base` for GEOM, and so on.

The **function** is the function the probe is in.

The **name** is the name of the probe within the function: for `fbt`, always `entry` or `return`.

So a probe like `fbt:kernel:malloc:entry` means *the entry point of the `malloc` function, in the kernel module, via the `fbt` provider*. A probe like `fbt:perfdemo:perfdemo_read:return` means *the return point of `perfdemo_read`, in the `perfdemo` module*.

Wildcards are allowed. `fbt:perfdemo::entry` means *all entry points in the `perfdemo` module*. `fbt:::entry` means *all function entries in the kernel*. Be careful with the latter; it is a lot of probes.

### Your First DTrace One-Liner

The canonical DTrace introduction is to count how often a function is called. For `perfdemo`, once the driver is loaded:

```console
# dtrace -n 'fbt:perfdemo:perfdemo_read:entry { @[probefunc] = count(); }'
```

This says: on every entry to any `perfdemo` function named like `perfdemo_read`, increment an aggregation keyed by function name. When you press Ctrl-C, DTrace prints the aggregation:

```text
  perfdemo_read                                                  10000
```

Ten thousand calls were made to `perfdemo_read` during the trace window. You now know the call rate; divide by the wall-clock seconds you ran the trace for.

A slightly richer one-liner that measures the time spent in the function:

```console
# dtrace -n 'fbt:perfdemo:perfdemo_read:entry { self->t = timestamp; }
              fbt:perfdemo:perfdemo_read:return /self->t/ {
                  @t = quantize(timestamp - self->t); self->t = 0; }'
```

This says: on entry, store the timestamp in a thread-local variable `self->t`. On return, if we have a saved timestamp (the `/self->t/` predicate), compute the difference and add it to a quantised histogram. When you stop the trace, DTrace prints the histogram:

```text
           value  ------------- Distribution ------------- count
             512 |                                         0
            1024 |@@@@                                     1234
            2048 |@@@@@@@@@@@@@@@@@@                       6012
            4096 |@@@@@@@@@@@@@@                           4581
            8192 |@@@@                                     1198
           16384 |@                                        145
           32768 |                                         29
           65536 |                                         8
          131072 |                                         2
          262144 |                                         0
```

The histogram says the function mostly runs in the 1 to 8 microsecond range, with a long tail out to a quarter of a millisecond. You now have the distribution, not just the average, of the function's latency. This is already better information than most published benchmarks publish.

### Aggregations: the DTrace Superpower

The `quantize` aggregator is one of several that DTrace provides. The ones you will use most often are:

- `count()`: how many times the event fired.
- `sum(value)`: total of `value` across all fires.
- `avg(value)`: mean of `value`.
- `min(value)`, `max(value)`: extremes.
- `quantize(value)`: power-of-two histogram.
- `lquantize(value, lower, upper, step)`: linear histogram with custom bounds.
- `stddev(value)`: standard deviation.

Aggregations can be indexed. `@[probefunc] = count();` keys by function name. `@[pid] = count();` keys by process id. `@[execname] = sum(arg0);` sums a value by executable name. Indexed aggregations are how DTrace produces *top-N* style summaries without ever sending per-event data to userland.

### Useful DTrace Patterns for Driver Work

Here are a handful of patterns that keep coming back in driver performance work. Each is a fill-in-the-blank template.

**Count calls to a function by caller:**

```console
# dtrace -n 'fbt:perfdemo:perfdemo_read:entry {
    @[stack(5)] = count();
}'
```

The `stack(5)` argument captures the five-frame user stack at the time of the probe. The aggregation tells you who the typical caller is.

**Measure the time a function spends in itself versus in its callees:**

```console
# dtrace -n '
fbt:perfdemo:perfdemo_read:entry { self->s = timestamp; }
fbt:perfdemo:perfdemo_read:return / self->s / {
    @total = quantize(timestamp - self->s);
    self->s = 0;
}
fbt:perfdemo:perfdemo_do_work:entry { self->w = timestamp; }
fbt:perfdemo:perfdemo_do_work:return / self->w / {
    @worktime = quantize(timestamp - self->w);
    self->w = 0;
}'
```

Running this with a concurrent read loop tells you how much of the read time is inside `perfdemo_do_work` versus in the rest of `perfdemo_read`.

**Count errors by location:**

```console
# dtrace -n 'fbt:perfdemo:perfdemo_read:return / arg1 != 0 / {
    @errors[probefunc, arg1] = count();
}'
```

The `arg1` on a return probe is the function's return value. If the function returns an errno on failure, this aggregation shows you which errors happen how often.

**Watch memory allocation:**

```console
# dtrace -n 'fbt:kernel:malloc:entry / execname == "perfdemo" / {
    @sizes = quantize(arg0);
}'
```

This aggregates the size argument (`arg0`) passed to `malloc(9)` by contexts running in the `perfdemo` module. It answers questions like *how big are the allocations my driver does?*

These patterns are small variations on a theme. Learn them by writing them, not by reading them. The chapter's lab in Section 3 will give you a concrete one to try.

### Profile Sampling with the `profile` Provider

The `fbt` provider fires on every function entry or return, which is thorough but noisy. For *where is CPU time going* questions, the `profile` provider is often better. It fires at a regular rate (for example, 997 Hz, a prime so it does not synchronise with the timer interrupt) on every CPU, independent of what is running. A script keyed by kernel stack gives you a statistical profile of where the kernel is spending time.

```console
# dtrace -n 'profile-997 / arg0 != 0 / {
    @[stack()] = count();
}'
```

The `/arg0 != 0/` predicate filters out idle CPUs (where `arg0` is the user-space PC, which is zero for kernel-thread idling). Ctrl-C after a minute, and you get a list of kernel stacks with counts. The top stack is where the kernel spent most time. Send the output to a flame-graph renderer (the FlameGraph tools are installable as a port), and you have a visual, hierarchical view of kernel CPU use.

### Static Probes with SDT

`fbt` is broad but not always the right answer. The `fbt` provider has a probe for every non-static function in the kernel, which means you can observe any function you please, but it also means the probe names are compiler-generated and may change as the code does. For stable, named observation points, FreeBSD provides the **SDT** (Statically Defined Tracepoint) provider. An SDT probe is declared in the driver's source, has a stable name, and can be inspected by DTrace scripts just like any other probe. It fires only when enabled, and when disabled is a NOP.

A driver adds SDT probes like this:

```c
#include <sys/sdt.h>

SDT_PROVIDER_DEFINE(perfdemo);
SDT_PROBE_DEFINE2(perfdemo, , , read_start,
    "struct perfdemo_softc *", "size_t");
SDT_PROBE_DEFINE3(perfdemo, , , read_done,
    "struct perfdemo_softc *", "size_t", "int");
```

The first argument is the provider name (`perfdemo`). The next two are the module and function grouping; we leave them empty to get `perfdemo:::read_start` as the probe name. Then the probe name itself. Then the argument types, one per fired argument.

In the driver code, you fire the probe where it matters:

```c
static int
perfdemo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct perfdemo_softc *sc = dev->si_drv1;
    size_t want = uio->uio_resid;
    int error;

    SDT_PROBE2(perfdemo, , , read_start, sc, want);

    error = do_the_read(sc, uio);

    SDT_PROBE3(perfdemo, , , read_done, sc, uio->uio_resid, error);

    return (error);
}
```

From a DTrace script, the probes now have readable names:

```console
# dtrace -n 'perfdemo:::read_start { @starts = count(); }
              perfdemo:::read_done  { @done = count(); }'
```

The stable names are the point. A change to `perfdemo_read`'s internal structure does not break the script; a DTrace user can write `perfdemo:::read_done` and know exactly what it means, which they could not say of `fbt:perfdemo:perfdemo_read:return`.

For drivers that are going to be observed often, a modest set of SDT probes at the operational boundaries (read, write, ioctl, interrupt, error, overflow, underflow) is a very small code cost and a large observability gain.

### The `lockstat` Provider

One of DTrace's more specialised providers, `lockstat`, deserves a separate introduction because lock contention is one of the most common performance problems in concurrent drivers. Its probes are declared in `/usr/src/sys/sys/lockstat.h`. They fire on every acquire, release, block, and spin of every kernel mutex, rwlock, sxlock, and lockmgr lock.

The two probes you will use most often are:

- `lockstat:::adaptive-acquire` and `lockstat:::adaptive-release` for plain mutex (`mtx`) operations.
- `lockstat:::adaptive-block` when a thread had to sleep waiting for a contended mutex.
- `lockstat:::spin-acquire`, `lockstat:::spin-release`, `lockstat:::spin-spin` for spin mutexes.

The argument `arg0` to a lockstat probe is a `struct lock_object *` pointer. To get the lock's name, `((struct lock_object *) arg0)->lo_name`. A useful one-liner that shows the lock names with the most acquires over a measurement window:

```console
# dtrace -n 'lockstat:::adaptive-acquire {
    @[((struct lock_object *)arg0)->lo_name] = count();
}'
```

A more useful one that measures how long the lock was *held*:

```console
# dtrace -n '
lockstat:::adaptive-acquire {
    self->s[arg0] = timestamp;
}
lockstat:::adaptive-release / self->s[arg0] / {
    @[((struct lock_object *)arg0)->lo_name] = sum(timestamp - self->s[arg0]);
    self->s[arg0] = 0;
}'
```

Expect thread-local storage to grow if many distinct locks are acquired; the kernel caps it and DTrace will stop the script if it overflows. For most drivers, this script runs comfortably for a few minutes before it runs out.

A third useful one tracks **contention**, not just acquisition. An adaptive mutex that blocks because some other thread holds it fires `lockstat:::adaptive-block`. Counting those by lock name shows you which locks are actually contended:

```console
# dtrace -n 'lockstat:::adaptive-block {
    @[((struct lock_object *)arg0)->lo_name] = count();
}'
```

If your driver's mutex shows up at the top of this list, you have found a real contention problem. If it does not, locking is not your problem and you can focus elsewhere. The data is more valuable than most of the advice you could give yourself about locking in the abstract.

### A Longer DTrace Script

DTrace scripts can live in files (`.d`) and be invoked with `dtrace -s script.d`. Here is a longer script for measuring `perfdemo`'s read behaviour, with per-process aggregation and a predicate to ignore a specific process that is also running on the system:

```c
/*
 * perfdemo-reads.d
 *
 * Aggregate perfdemo_read() timings per userland process.
 * Requires the perfdemo module to be loaded.
 */

#pragma D option quiet

fbt:perfdemo:perfdemo_read:entry
{
    self->start = timestamp;
    self->size  = args[1]->uio_resid;
}

fbt:perfdemo:perfdemo_read:return
/ self->start && execname != "dtrace" /
{
    this->dur = timestamp - self->start;
    @durations[execname] = quantize(this->dur);
    @sizes[execname]     = quantize(self->size);
    @count[execname]     = count();
    self->start = 0;
    self->size  = 0;
}

END
{
    printa("\nRead counts per process:\n%-20s %@u\n", @count);
    printa("\nRead durations (ns) per process:\n%s\n%@d\n", @durations);
    printa("\nRead request sizes per process:\n%s\n%@d\n", @sizes);
}
```

The script uses three DTrace idioms that are worth calling out. First, `args[1]->uio_resid` accesses the second argument of `perfdemo_read` (the `struct uio *`) and reads its `uio_resid` field; DTrace understands kernel structures with their field names. Second, `self->...` is thread-local storage that carries data between an entry probe and the matching return probe. Third, the predicate `/ self->start && execname != "dtrace" /` filters out probes where we did not see the entry (for example, because the script started mid-call) and excludes DTrace's own reads of the driver (which would otherwise skew the results).

Invoke the script while a workload runs:

```console
# dtrace -s perfdemo-reads.d
```

Let it run for a minute, then Ctrl-C. The three aggregated printouts at the end give you counts, duration histograms, and size histograms per process, all from a single script with essentially no effect on the workload's performance.

Scripts like this are where DTrace really shines. They answer questions a developer could not have answered without recompiling the kernel two decades ago.

### The `sched` Provider for Scheduler Events

One of DTrace's less-advertised providers is `sched`, which fires on scheduler events: a thread being put on a run queue, a thread being taken off a run queue, a context switch, a thread being woken up, and related events. For driver performance work, `sched` answers questions about *latency in the scheduler*, which is the layer of the system that gets between your driver's wake-up call and the userland thread actually running.

The probes you will reach for most often are:

- `sched:::enqueue`: a thread is put on a run queue.
- `sched:::dequeue`: a thread is taken off a run queue.
- `sched:::wakeup`: a thread is awoken by another thread (often from an interrupt or a driver).
- `sched:::on-cpu`: a thread starts running on a CPU.
- `sched:::off-cpu`: a thread stops running on a CPU.

A common use in driver work is to measure the *wake-up-to-run latency*: how long from the moment the driver calls `wakeup(9)` on a sleep channel to the moment the user-land thread actually runs. A simple one-liner:

```console
# dtrace -n '
sched:::wakeup / curthread->td_proc->p_comm == "mydriver_reader" / {
    self->w = timestamp;
}
sched:::on-cpu / self->w / {
    @runlat = quantize(timestamp - self->w);
    self->w = 0;
}'
```

This script records the timestamp when the reader process is awoken and again when it starts running on a CPU. The aggregation is the distribution of the delay between the two. On typical FreeBSD 14.3-amd64 hardware in our lab environment, an idle system usually shows this latency in the sub-microsecond range, while a contended one pushes it into the low tens of microseconds. If your driver's median read latency is under 10 microseconds but its P99 is in the hundreds, the scheduler is often where the tail comes from, and `sched:::wakeup` and `sched:::on-cpu` are the probes that prove it. See Appendix F for notes on reproducing this measurement across kernel configurations.

Another useful `sched` pattern is counting how often a thread is preempted off a CPU, keyed by the thread's name:

```console
# dtrace -n '
sched:::off-cpu / curthread->td_flags & TDF_NEEDRESCHED / {
    @preempt[execname] = count();
}'
```

This tells you which threads are being preempted away by a higher-priority thread, which in an interrupt-heavy system points at the drivers whose handlers are forcing context switches. On a well-behaved system this count is low.

### The `io` Provider for Storage Drivers

For drivers in the storage stack, the `io` provider is invaluable. It fires on buffer-cache and bio events: `io:::start` when a bio is dispatched, `io:::done` when it completes, `io:::wait-start` when a thread begins waiting, `io:::wait-done` when the wait finishes. The combination gives you end-to-end latency for every storage operation.

A classic one-liner to measure storage latency:

```console
# dtrace -n 'io:::start { self->s = timestamp; }
             io:::done / self->s / {
                 @lat = quantize(timestamp - self->s);
                 self->s = 0; }'
```

The histogram it produces is the latency distribution of every bio the system completed during the measurement window, in nanoseconds. For a NVMe-backed file system the median is tens of microseconds; for a SATA SSD it is hundreds; for a rotating disk it is milliseconds. If a driver you are investigating shows much higher latency than its peer, you have narrowed the problem.

A richer version aggregates by the device and operation:

```console
# dtrace -n '
io:::start { self->s = timestamp; }
io:::done / self->s / {
    @lat[args[0]->bio_dev, args[0]->bio_cmd == BIO_READ ? "READ" : "WRITE"]
        = quantize(timestamp - self->s);
    self->s = 0;
}'
```

The script accesses the bio structure's fields through `args[0]`, which DTrace understands because the probe arguments are typed in the kernel. It partitions the latency distribution by device and operation direction, so you can see whether reads and writes have different distributions, or whether one device is dragging down the average.

The `io` provider's real strength is that it answers the *total* latency question: the time the application saw, not just the time the driver contributed. If your driver is fast but the system is slow, `io` helps localise the problem.

### Combining Multiple Providers in One Script

A script is not limited to a single provider. DTrace's power is most evident when you combine providers to answer a question that no single provider could.

Consider this question: *on a system where the perfdemo driver is used by two processes, which process's reads cause the most time to be spent in the kernel?*

A script:

```c
/*
 * perfdemo-by-process.d
 *
 * Measures cumulative kernel time spent handling perfdemo_read()
 * per userland process, using sched + fbt providers.
 */

#pragma D option quiet

fbt:perfdemo:perfdemo_read:entry
{
    self->start = timestamp;
    self->pid   = pid;
    self->exec  = execname;
}

fbt:perfdemo:perfdemo_read:return / self->start /
{
    @total_time[self->exec] = sum(timestamp - self->start);
    @count[self->exec]      = count();
    self->start = 0;
}

END
{
    printf("\nPer-process perfdemo_read() summary:\n");
    printf("%-20s %10s %15s\n", "PROCESS", "CALLS", "TOTAL_NS");
    printa("%-20s %@10d %@15d\n", @count, @total_time);
}
```

Run this script while the workload runs, Ctrl-C, and see a two-column summary of which process spent the most total time in your driver's read path. That is data a plain `top(1)` cannot give you; it attributes kernel time *per process*, not *per thread*, and only for your specific function.

### Putting It All Together: Driver Trace Session

A typical DTrace session for a driver you are tuning follows a predictable sequence of scripts, each building on the previous one. A concrete workflow:

1. **Who is calling the driver?** Run `dtrace -n 'fbt:perfdemo:perfdemo_read:entry { @[execname, pid] = count(); }'` for thirty seconds, Ctrl-C. This tells you which userland process and PID is exercising the driver, which is useful context for the rest of the session.

2. **How long do calls take?** Run the quantize script from the chapter. The distribution tells you the expected range and whether there is a tail.

3. **If there is a tail, what causes it?** Run a script that keys the histogram by stack trace. Different stack traces take different paths; seeing them split shows you which path is slow.

4. **Which lock, if any, is contended?** Run the `lockstat::adaptive-block` script. If your mutex is in the top entries, lock contention is real.

5. **Where is the CPU time going?** Run the `profile-997` script for a minute. The top stacks tell you which functions dominate.

6. **What is the scheduler doing?** If wake-up latency matters, run the `sched:::wakeup` / `sched:::on-cpu` one-liner. If the distribution has a long tail, the scheduler is where the user-visible delays come from.

At the end of this sequence, you have a coherent picture: who calls, how fast, where time goes, what blocks, and how the scheduler handles the results. Each script is a few lines. The sequence is the whole DTrace-for-drivers workflow.

### A Note on DTrace's Limitations

DTrace is capable but not infinite. A few limitations to keep in mind:

- The DTrace language has no loops. This is by design; it is what guarantees probes terminate. If you need something that feels like a loop, use aggregations instead.
- You cannot modify kernel state from a DTrace script. It is an observer, not a debugger. To intervene, you need different tools.
- Probes fire in a safe but constrained context. You cannot, for example, call arbitrary kernel functions from inside a probe.
- Large amounts of thread-local storage can overflow. The kernel reclaims stale TLS conservatively; a script that stashes data in `self->foo` without clearing it will eventually run out.
- Aggregations are kept in per-CPU buffers and merged lazily. If you print an aggregation and immediately exit, the last few events may not appear.
- Some providers are unavailable in VMs, in jails, or when MAC policies restrict tracing.

The last point is practically important: if your driver is under test in a bhyve guest or on a cloud VM, some providers may simply not work. The `fbt` and `sdt` providers usually work; the `profile` provider depends on kernel support; the `lockstat` provider depends on the kernel being compiled with the probes in place.

### Exercise 33.3: DTrace Your Driver

Take the `perfdemo` driver and write a DTrace script (or a one-liner) that answers each of these questions:

1. How many reads per second is the driver doing over a one-minute window?
2. What is the P50, P95, and P99 read latency, in nanoseconds?
3. Which userland process is doing the reads?
4. Does the driver ever sleep on its mutex, and if so, for how long?
5. Of the CPU time in the kernel during the measurement, how much is spent inside `perfdemo` and how much elsewhere?

Save your scripts in the lab directory. The act of writing them is where DTrace becomes second-nature.

### Wrapping Up Section 3

DTrace is the kernel performance engineer's default tool. Its probes are everywhere, its enabled cost is small, its aggregations scale, and its scripts are expressive without being dangerous. We looked at the probe name format, at one-liners and aggregations, at SDT probes for stable observation points, at the `lockstat` provider for lock contention, at profile sampling for CPU profiling, and at the shape of a longer `.d` script.

The next section turns from function-level observation to CPU-level observation. `pmcstat` and `hwpmc(4)` give you hardware-counter data that DTrace cannot: cycles, cache misses, branch mispredicts. They are the right tool when you know which function is hot and need to understand *why* the hardware is taking so long to run it.

## Section 4: Using pmcstat and CPU Counters

Section 3 gave you a way to measure time and event counts at the function level. Section 4 is about measuring what the CPU hardware is doing while it runs those functions: instructions retired, cycles consumed, cache misses, branch mispredictions, memory stalls. These hardware events are the layer below function-level timing, and they explain the *why* behind a surprising `pmcstat` result. A function may be slow because it runs too many instructions, because it waits on memory, because it mispredicts branches, or because the CPU is stalled on a dependency. `pmcstat` tells you which.

The section introduces hardware counters, explains how `pmcstat(8)` uses them, walks through a sampling session from setup to interpretation, and closes with the limitations of the tool.

### Hardware Performance Counters in One Page

Modern CPUs include a small set of hardware counters that the processor increments on specific events. The counters are programmable: you choose which event to count, start the counter, and read the value. Intel calls the subsystem *Performance Monitoring Counters* (PMCs). AMD calls it the same thing. ARM calls it the *Performance Monitor Unit* (PMU). They differ in detail but share a model.

On an x86-64 Intel or AMD CPU you typically have:

- A small number of fixed-function counters, each tied to a specific event (for instance, *instructions retired* or *unhalted core cycles*).
- A larger number of programmable counters, which can be configured to count any of hundreds of events.

Each counter is a 48-bit or 64-bit register that increments on every event. The events are vendor-specific and vary by processor generation. Common events include:

- **Cycles**: how many CPU cycles have passed. `cpu_clk_unhalted.thread` on Intel.
- **Instructions retired**: how many instructions completed. `inst_retired.any` on Intel.
- **Cache references** and **cache misses**: at various levels of the memory hierarchy. `llc-misses` is a commonly useful shorthand.
- **Branches** and **branch mispredictions**.
- **Memory stalls**: cycles where the pipeline was waiting on memory.
- **TLB misses**: cycles where virtual-to-physical translation missed the TLB.

The counters can be used in two modes. **Counting mode** just reads the counter at the end of a workload and gives you a total. **Sampling mode** configures the counter to fire an interrupt every N events; the interrupt handler captures the current PC and stack, and after the run you have a statistical sample of where in the code the events happened. Counting mode tells you the total; sampling mode tells you the distribution. Both are useful.

### `hwpmc(4)` and `pmcstat(8)`

FreeBSD exposes the CPU counters through the `hwpmc(4)` kernel module and the `pmcstat(8)` userland tool. The module drives the hardware; the tool collects and presents the data. To use them:

```console
# kldload hwpmc
# pmcstat -L
```

The first command loads the module. The second asks the tool to list the event names available on this machine. On an Intel Core i7 laptop the list is hundreds of entries long; on an arm64 board it is shorter but still substantive.

The event names are the main complication. Intel has its own naming, AMD has a different one, ARM has a third. The `pmcstat -L` command lists the names for your CPU. FreeBSD also provides a set of portable mnemonic events that work on any supported processor: `CPU_CLK_UNHALTED`, `INSTRUCTIONS_RETIRED`, `LLC_MISSES`, and a few others. Prefer the portable mnemonics when your measurement does not depend on a vendor-specific event.

### A First Sampling Session

The simplest `pmcstat` invocation runs a command under a sampled counter:

```console
# pmcstat -S instructions -O /tmp/perfdemo.pmc sleep 30 &
# dd if=/dev/perfdemo of=/dev/null bs=4096 count=1000000 &
```

The `-S instructions` flag configures the counter to sample on `instructions` (a portable mnemonic for retired instructions). `-O /tmp/perfdemo.pmc` tells it to write the raw samples to a file. `sleep 30` is the workload; for thirty seconds the sampler runs. The `dd` runs in parallel and drives load on the `perfdemo` device.

When `sleep 30` finishes, `pmcstat` stops and the file `/tmp/perfdemo.pmc` holds the raw samples. You turn them into a summary with:

```console
# pmcstat -R /tmp/perfdemo.pmc -G /tmp/perfdemo.graph
```

The `-R` flag reads the raw file; `-G` writes a callgraph summary. The callgraph is a text file in a format that a flame-graph renderer or a simple `sort | uniq -c` pipeline can consume.

You can also ask for a top-style view of the hottest functions:

```console
# pmcstat -R /tmp/perfdemo.pmc -T
```

which prints a list sorted by sample count:

```text
 %SAMP CUM IMAGE            FUNCTION
  12.5 12.5 kernel          perfdemo_read
   8.3 20.8 kernel          uiomove
   6.9 27.7 kernel          copyout
   5.1 32.8 kernel          _mtx_lock_sleep
   ...
```

The `%SAMP` column is the fraction of samples the function received. `perfdemo_read` dominated with 12.5%. `uiomove`, `copyout`, and `_mtx_lock_sleep` followed. Now you know where to focus. If `perfdemo_read` is doing most of the work and `_mtx_lock_sleep` is in the top five, your driver is likely contending on its mutex.

### Sampling Event Choice

`instructions` is the default because it is portable and usually useful, but other events change the question you are asking. A few useful variants:

- **`-S cycles`**: samples on unhalted cycles. Tells you where the CPU spends wall-clock time. Usually the best starting event.
- **`-S LLC-misses`** (Intel): samples on last-level-cache misses. Tells you which functions are suffering from main-memory accesses.
- **`-S branches`** or **`-S branch-misses`**: tells you which functions are hot in the branch predictor.
- **`-S mem-any-ops`** (Intel): memory operation rate.

A common workflow is to run a session with `-S cycles` first, identify a suspect function, then rerun with `-S LLC-misses` to see whether that function is hot because of instructions or because of memory.

### Callgraphs and Flame Graphs

A top-N function list tells you where the hot function is; a callgraph tells you *how you got there*. `pmcstat -G` writes a callgraph file, and the conventional format can be fed to Brendan Gregg's FlameGraph scripts to produce an SVG flame graph. A flame graph shows the stack traces that led to each sampled function, sized by sample count. It is the single most useful visualisation of a CPU profile I know of, and learning to read one is worth a couple of hours.

A practical invocation on FreeBSD, assuming you have installed the FlameGraph tools from the `sysutils/flamegraph` port:

```console
# pmcstat -R /tmp/perfdemo.pmc -g -k /boot/kernel > /tmp/perfdemo.stacks
# stackcollapse-pmc.pl /tmp/perfdemo.stacks > /tmp/perfdemo.folded
# flamegraph.pl /tmp/perfdemo.folded > /tmp/perfdemo.svg
```

The `-g` flag tells `pmcstat` to include kernel stacks; `-k /boot/kernel` tells it to resolve kernel symbols against the running kernel. The `stackcollapse-pmc.pl` and `flamegraph.pl` scripts come from the FlameGraph tools. Open the resulting SVG in a browser; each box is a function, its width is the fraction of time spent in it, and the stack of boxes below it shows how execution reached it.

### Interpreting a PMC Result

A `pmcstat` result is raw data, not a conclusion. You have to reason about what it means in the context of the workload and the goal. A few patterns are common enough to recognise.

**A function at the top of `%SAMP` but with low `IPC` (instructions per cycle)** is probably memory-bound. Compare the cycles count to the instructions count; if cycles are much higher than instructions, the CPU is stalling. Look at cache misses and TLB misses to confirm.

**A function at the top with high IPC** is doing a lot of work in a tight loop. That is either a legitimate hot loop you should leave alone, or an opportunity for algorithmic improvement. Run the `LLC-misses` and `branches` counters to see whether the hardware is happy.

**`_mtx_lock_sleep` or `turnstile_wait` in the top five** is a sign that a mutex is being contended on. Run `lockstat` (Section 3) to find out which one.

**Many functions below the 1% threshold, and no clear hot function** usually means the overhead is spread across the whole driver path. Look at the total CPU cost of the operation and decide whether the driver is doing too much per call, rather than doing one thing slowly.

**A function that does not appear at all but that you expected to see** may be compiled inline, may have been absorbed by a macro, or may simply not be on the hot path. Check whether the compiler optimised it away; a quick `objdump -d` on the module can confirm.

These patterns get easier with practice. The first few `pmcstat` sessions leave most readers confused. By the tenth, the patterns are familiar.

### Reading a Callgraph in Detail

When you run `pmcstat -R output.pmc -G callgraph.txt`, the resulting file is a text-format callgraph: every sample's full stack trace, one per line, in reverse order (innermost frame last). A small excerpt:

```text
Callgraph for event instructions
@ 100.0% 12345 total samples
perfdemo_read    <- devfs_read_f <- dofileread <- sys_read <- amd64_syscall <- fast_syscall_common
    at 45.2% 5581 samples
perfdemo_do_work <- perfdemo_read <- devfs_read_f <- dofileread <- sys_read <- amd64_syscall <- fast_syscall_common
    at 20.1% 2481 samples
_mtx_lock_sleep  <- perfdemo_read <- devfs_read_f <- dofileread <- sys_read <- amd64_syscall <- fast_syscall_common
    at 12.8% 1580 samples
```

Each entry shows the *leaf* function on the left, followed by the chain of callers. The percentage is the fraction of total samples in which that exact call chain was seen. The top entry shows where most of the CPU time ended up; the chain of callers shows *how* the profiler got there.

Three habits make callgraph reading productive.

First, **do not trust the percentages at face value for short sessions**. A sample count under a few hundred is noisy; percentages based on such counts can swing by ten or more between runs. Collect samples for long enough to get into the thousands before drawing conclusions.

Second, **follow the chain, not just the leaf**. A function that shows up at the top may be a common subroutine called from many places. The interesting question is often *which caller spent the most time calling this subroutine?* The callgraph answers that directly; the function list does not.

Third, **treat the root samples with caution**. The top of the stack is usually a syscall entry or an ithread wrapper. It is the caller of interest that tells you whose code is hot, not the common boilerplate at the stack's bottom.

### Cross-Referencing pmcstat and DTrace

`pmcstat` gives you where the CPU is spending time. DTrace gives you what the functions are doing. A disciplined investigation uses both. A typical workflow:

1. Run `pmcstat -S cycles` for a minute while the workload runs. Identify the top three functions.
2. For each top function, run a DTrace script on its `fbt:::entry` and `fbt:::return` probes to get a call-rate and latency histogram.
3. Multiply call rate by mean latency to estimate the total CPU time the function receives. This number should roughly match the `pmcstat` fraction; if it does not, one of the two tools is lying (usually your predicate).
4. Pick the function whose impact is largest and instrument its body with more granular DTrace probes.

The two tools complement each other. `pmcstat` has the hardware-counter granularity; DTrace has the function-body granularity and the ability to aggregate by context (process, thread, syscall, lock). Used alone, each tells half a story. Used together, they triangulate the performance picture.

### A Top-Down Performance Analysis Method

For amd64 CPUs, Intel publishes a *top-down microarchitecture analysis* (TMA) method that organises CPU performance into a tree of categories: front-end bound, back-end bound, bad speculation, retiring. Each category has sub-categories that narrow the diagnosis: memory-bound vs core-bound, bandwidth vs latency, branch mispredicts vs machine clears. The method is useful because it turns a list of hundreds of PMC events into a small hierarchy that points at the bottleneck.

FreeBSD's `pmcstat` does not produce a top-down report directly, but you can compute the relevant ratios by collecting the right events together:

- **Retiring rate**: `UOPS_RETIRED.RETIRE_SLOTS` divided by total issue slots.
- **Front-end bound**: stall cycles in the front end divided by total cycles.
- **Back-end bound**: stall cycles in the back end divided by total cycles.
- **Bad speculation**: branch mispredict cost divided by total cycles.

A `pmcstat` invocation that counts these events together:

```console
# pmcstat -s cpu_clk_unhalted.thread -s uops_retired.retire_slots \
          -s idq_uops_not_delivered.core -s int_misc.recovery_cycles \
          ./run-workload.sh 100000
```

The exact event names vary between CPU generations; `pmcstat -L` lists what your CPU supports. Compute the ratios manually from the `pmcstat` output. If retiring is under 30% of slots, your code is stalling; the other categories narrow the cause.

For most driver work this level of detail is overkill. The simpler `-S cycles` sampling session identifies the hot function, and a look at the function's source tells you whether it is dominated by memory accesses, arithmetic, branches, or locks. But when the simpler analysis runs out (you see a hot function, you cannot tell *why* it is hot), the top-down method is the systematic next step.

### Counting Mode

`pmcstat -P` (counting, per-process) and `pmcstat -s` (system-wide counting) are the counting counterparts of `-S`. Counting mode is ideal when you have a short benchmark and want a single number. A typical invocation:

```console
# pmcstat -s instructions -s cycles dd if=/dev/perfdemo of=/dev/null bs=4096 count=10000
```

When `dd` finishes, `pmcstat` prints:

```text
p/instructions p/cycles
  2.5e9          9.8e9
```

This tells you the dd ran 2.5 billion instructions in 9.8 billion cycles, an IPC of about 0.25. That is a low IPC; a modern CPU can sustain 3 or 4 instructions per cycle on friendly code. An IPC of 0.25 suggests the code is stalling heavily, usually on memory. A rerun with `-s LLC-misses` confirms:

```text
p/LLC-misses p/cycles
  1.2e7        9.8e9
```

12 million cache misses over 9.8 billion cycles. Divide 9.8 billion by 1.2 million: about 8000 cycles per miss on average, which is roughly the cost of a DRAM fetch. That is consistent with memory-bound behaviour.

The workflow is: run a counting session to get the system-wide numbers, run a sampling session to find which function is responsible, then reason about why. Each session answers a different question.

### Process-Attached vs System-Wide

`pmcstat -P` attaches to a process and reports its counters exclusively. `-s` counts system-wide. The distinction matters when you want to exclude other workloads. A process-attached measurement of `dd` gives you just `dd`'s cycles and instructions, not the whole system's. A system-wide measurement includes everything.

For driver work, system-wide sampling is often what you want, because the driver's CPU time is counted in kernel threads that are not the benchmark process. Use `pmcstat -s` with kernel-stack collection when you want to see kernel functions, and `pmcstat -P` when you want to focus on a single userland process.

### `pmcstat` and Virtualisation

Hardware PMCs in virtual machines are complicated. Some hypervisors expose the host's PMCs to guests (KVM with `vPMC` support, Xen with PMC passthrough); some expose a filtered subset; some expose nothing at all. FreeBSD's `hwpmc(4)` will report errors on events that the underlying hardware does not expose. If `pmcstat -L` produces a short list on your VM, or if `pmcstat -S cycles` fails with an error, you are probably in a PMC-restricted environment.

The fallback is DTrace's `profile` provider. It samples at a fixed rate instead of on hardware events, so it works anywhere the kernel does, including in heavily virtualised environments. Its results are coarser (you cannot sample on cache misses, for example), but it tells you where CPU time is going, which is the most common question anyway.

### Exercise 33.4: pmcstat `perfdemo`

On a FreeBSD machine with the `perfdemo` driver loaded and the `hwpmc` module available, run these three sessions and record the results in your logbook:

1. Counting mode: `pmcstat -s instructions -s cycles <your dd invocation>`. Note the IPC.
2. Sampling mode: `pmcstat -S cycles -O /tmp/perfdemo.pmc <your workload>`, then `pmcstat -R /tmp/perfdemo.pmc -T`. Note the top five functions.
3. If your system supports it, `pmcstat -S LLC-misses -O /tmp/perfdemo-miss.pmc <your workload>`, then `pmcstat -R /tmp/perfdemo-miss.pmc -T`. Compare to the cycle samples.

If `hwpmc` is not available, substitute DTrace's `profile-997` with `@[stack()] = count();` for the sampling sessions. The results will be less precise but still instructive.

### Wrapping Up Section 4

Hardware performance counters are the layer below function-level timing. `hwpmc(4)` exposes them, `pmcstat(8)` drives them, and DTrace's `profile` provider is the portable fallback. Counting mode gives totals, sampling mode gives distributions, and a flame graph turns the samples into a shape the human eye can read. The data is raw; interpretation takes practice. But once the patterns are familiar, a `pmcstat` session answers questions no other tool can.

We have now finished the measurement half of the chapter. Sections 1 through 4 taught you how to know what your driver is doing. Sections 5 through 8 turn to what to do with that knowledge: how to buffer, align, and allocate for throughput; how to keep interrupt handlers fast; how to expose runtime metrics; and how to ship the optimised result.

## Section 5: Buffering and Memory Optimization

Memory is where a surprising number of driver performance problems hide. A function that looks fine on paper can spend most of its cycles waiting for memory, thrashing the cache, fighting with another CPU for a shared line, or churning through the memory allocator because it frees and reallocates on every call. The techniques that fix these problems are well understood and well supported in FreeBSD; what takes experience is recognising when each one applies.

This section covers five memory topics, in order of increasing specificity: cache lines and false sharing, alignment for DMA and cache efficiency, pre-allocated buffers, UMA zones, and per-CPU counters.

### Cache Lines and False Sharing

A modern CPU does not read memory one byte at a time. It reads in units of a **cache line**, typically 64 bytes on x86-64 and on arm64 (some arm64 implementations use 128-byte lines; check `CACHE_LINE_SIZE` in `/usr/src/sys/arm64/include/param.h` for your target). Every time the CPU reads a byte, it reads the whole cache line into its L1 data cache. Every time it writes a byte, it modifies the whole cache line.

This is almost always a win: spatial locality means the next few bytes you read are often in the same line. But it also creates a subtle problem when two fields in a single cache line are written by different CPUs. The CPUs' caches have to coordinate; each time one CPU writes the line, the other CPU's copy is invalidated, and the line bounces between them. The cost is measurable and, at high concurrency, dominant. The phenomenon is called **false sharing**, because the two CPUs are not actually sharing data (they write different fields), but the cache coherence protocol treats them as if they were.

A driver's softc structure is a common place for false sharing to show up. If a softc has two counters, one updated by the read path and one updated by the write path, and both sit in the same cache line, every read and every write causes a cache bounce. At low concurrency this is invisible. At high concurrency it can halve your throughput.

The fix is explicit cache-line alignment. FreeBSD's `CACHE_LINE_SIZE` macro names the cache line size for the current architecture, and the `__aligned` compiler attribute places a variable at the right alignment. To isolate a field in its own cache line:

```c
struct perfdemo_softc {
    struct mtx          mtx;
    /* ... other fields ... */

    uint64_t            read_ops __aligned(CACHE_LINE_SIZE);
    char                pad1[CACHE_LINE_SIZE - sizeof(uint64_t)];

    uint64_t            write_ops __aligned(CACHE_LINE_SIZE);
    char                pad2[CACHE_LINE_SIZE - sizeof(uint64_t)];
};
```

This is one way to do it, and it makes the isolation explicit to the reader. A cleaner way, when the struct is going to be allocated individually rather than as an array, is to put each hot counter in its own aligned sub-structure, or to use `counter(9)` which handles per-CPU placement internally and avoids the issue.

You do not have to pad every field in every struct. In most cases the softc is allocated once per device and accessed by one CPU at a time, and alignment does not matter. Reach for cache-line alignment when:

- A profile shows false-sharing-like behaviour (high `LLC-misses` on a function that does not look memory-heavy, or low IPC on a function that should be CPU-bound).
- Multiple CPUs are known to write different fields of the same struct concurrently.
- You measured a difference. As always.

### Alignment for DMA

DMA engines usually require buffers aligned to a boundary larger than a cache line: 512 bytes for disk DMA, 4 KB for some network hardware, sometimes larger. If you allocate a buffer with `malloc(9)` and hand it to DMA without checking alignment, you are relying on the allocator's default alignment, which is usually large enough on amd64 but not guaranteed on all architectures.

For DMA, FreeBSD's `bus_dma(9)` API is the right interface. It handles alignment, bounce buffers, and scatter-gather for you. We covered it in Chapter 21. Here the relevant note is just that bus_dma handles alignment explicitly, and a driver should not allocate DMA-able memory with plain `malloc(9)` and hope.

A related memory concern is **alignment for SIMD or vectorised code**, where the compiler or the ISA may require 16-byte or 32-byte alignment on certain arguments. Again, `__aligned` is the tool. For buffers you allocate yourself, `contigmalloc(9)` or the UMA zone allocator can produce aligned memory.

### Pre-Allocated Buffers

Every driver has a hot path. The cardinal rule for a hot path is that it should not allocate memory. Memory allocation is expensive on every kernel allocator: `malloc(9)` takes a lock and walks lists; `uma(9)` caches things per CPU but still has a fallback path to the slab allocator; `contigmalloc(9)` may block on page-level operations. None of these belong in a function that runs thousands of times per second.

The fix is **pre-allocation**. Allocate the memory your hot path needs in `attach()`, not in the hot path. If you need a fixed number of buffers for a ring, allocate them once. If you need a pool of buffers that get reused, allocate the pool at attach and keep a free list. If you need an occasional larger buffer, allocate it in a cold path.

For `perfdemo`, pre-allocation might look like this:

```c
#define PERFDEMO_RING_SIZE      64
#define PERFDEMO_BUFFER_SIZE    4096

struct perfdemo_softc {
    struct mtx      mtx;
    char           *ring[PERFDEMO_RING_SIZE];
    int             ring_head;
    int             ring_tail;
    /* ... */
};

static int
perfdemo_attach(device_t dev)
{
    struct perfdemo_softc *sc = device_get_softc(dev);
    int i;

    for (i = 0; i < PERFDEMO_RING_SIZE; i++) {
        sc->ring[i] = malloc(PERFDEMO_BUFFER_SIZE, M_PERFDEMO, M_WAITOK);
    }
    /* ... */
    return (0);
}

static int
perfdemo_detach(device_t dev)
{
    struct perfdemo_softc *sc = device_get_softc(dev);
    int i;

    for (i = 0; i < PERFDEMO_RING_SIZE; i++) {
        if (sc->ring[i] != NULL) {
            free(sc->ring[i], M_PERFDEMO);
            sc->ring[i] = NULL;
        }
    }
    return (0);
}
```

On the hot path, the driver takes a buffer from the ring, uses it, returns it. No allocator, no lock beyond the ring's own, no risk of allocation failure under load.

This is the pattern that separates drivers that perform well under stress from drivers that fall off a cliff when memory pressure rises. A driver that allocates on the hot path will eventually fail to allocate, and its error path will be exercised in the most stressful moment. A driver that pre-allocates takes its failure at attach time, where it can be reported cleanly.

### UMA Zones

`malloc(9)` is a good general-purpose allocator, but for frequently allocated and freed fixed-size objects it is not optimal. FreeBSD provides the UMA (Universal Memory Allocator) zone framework for this case. UMA gives you per-CPU caches, so a `uma_zalloc()` followed by a `uma_zfree()` within one CPU's thread is usually a handful of pointer swaps with no locks. The declarations are in `/usr/src/sys/vm/uma.h`.

A zone is created once per driver at module load, destroyed at unload:

```c
#include <vm/uma.h>

static uma_zone_t perfdemo_buffer_zone;

static int
perfdemo_modevent(module_t mod, int what, void *arg)
{
    switch (what) {
    case MOD_LOAD:
        perfdemo_buffer_zone = uma_zcreate("perfdemo_buffer",
            PERFDEMO_BUFFER_SIZE, NULL, NULL, NULL, NULL,
            UMA_ALIGN_CACHE, 0);
        if (perfdemo_buffer_zone == NULL)
            return (ENOMEM);
        break;
    case MOD_UNLOAD:
        uma_zdestroy(perfdemo_buffer_zone);
        break;
    }
    return (0);
}
```

Note the `UMA_ALIGN_CACHE` flag. It asks UMA to align each item in the zone to a cache-line boundary, which matters when items are used from multiple CPUs. The macro is defined in `/usr/src/sys/vm/uma.h`.

On the hot path, allocation and free look like this:

```c
void *buf;

buf = uma_zalloc(perfdemo_buffer_zone, M_WAITOK);
/* use buf */
uma_zfree(perfdemo_buffer_zone, buf);
```

The `M_WAITOK` flag says "this call may sleep waiting for memory". On a non-sleeping path, use `M_NOWAIT` and handle a NULL return. For a hot path that absolutely must not block, you can also do `M_NOWAIT | M_ZERO` and keep a fallback pool.

UMA is the right tool when the driver repeatedly allocates and frees objects of the same size. For one-off, variable-size allocations, `malloc(9)` remains fine. For rings of pre-allocated buffers, UMA is overkill; a plain array is simpler.

The rule of thumb: if you are allocating the same-size thing hundreds of times per second, it deserves a UMA zone.

### A Tour of UMA Internals

UMA's behaviour under the hood is worth understanding because it explains both its performance advantages and the cases where it does *not* help. The zone allocator has three layers: per-CPU caches, zone-wide lists, and the slab allocator below everything.

The per-CPU cache is a small array of free items, one per CPU. When a CPU calls `uma_zalloc`, UMA first checks its own cache; if there is an item, the call is a quick pointer swap with no lock. The default cache size is tuned by UMA based on the item size and the machine's CPU count, and can be overridden with `uma_zone_set_maxcache()` when a particular zone needs a different ceiling.

When the per-CPU cache is empty, UMA refills it from the zone-wide free list. This path takes a lock, pulls several items at once, and returns. It is more expensive than the cache path but still much cheaper than a full allocator call.

When the zone-wide free list is empty, UMA calls into the slab allocator to carve more items from a fresh page. This is the most expensive path and involves allocating a physical page, partitioning it into slab-sized items, and populating the zone's free list. Most of a zone's hot-path calls never reach this layer; the per-CPU cache and the zone-wide list are hit first.

Three consequences follow.

First, **UMA is fastest when allocations and frees happen on the same CPU**. A pair of `uma_zalloc` / `uma_zfree` calls on one CPU is a handful of pointer swaps. A `uma_zalloc` on CPU 0 followed by a `uma_zfree` on CPU 1 returns the item to CPU 1's cache, which may or may not benefit CPU 0's future allocations. If your driver allocates on one CPU and frees on another routinely, the per-CPU advantage erodes.

Second, **zones can be tuned with `uma_zone_set_max()` and `uma_prealloc()`**. Pre-allocation reserves a fixed number of items at zone creation, so the first few hundred allocations never hit the slab path. Max-size caps the zone and fails subsequent allocations with `M_NOWAIT` instead of allowing unbounded growth. Both are useful for drivers that need predictable memory behaviour.

Third, **`vmstat -z` is your window into UMA**. It lists every zone, the per-item size, the current count, the cumulative count, and the failure count. A zone whose current count is growing without a matching free path is a leak. A zone whose failure count is non-zero is under memory pressure. Learn to read `vmstat -z` fluently; it is the single best tool for diagnosing UMA issues.

The internals are in `/usr/src/sys/vm/uma_core.c` for the main allocator and `/usr/src/sys/vm/uma.h` for the public interface. Reading the interface file is enough for most driver work; the core file is where to look when a failure mode does not match your expectation.

### Bounce Buffers and DMA Memory Mapping

DMA-capable devices need physically contiguous, DMA-safe memory. On amd64 all memory is DMA-capable by default (assuming the device is not IOMMU-constrained), but on other architectures and on IOMMU-protected systems the story is more complex. FreeBSD's `bus_dma(9)` API hides the complexity, but a driver author needs to know when bounce buffers come into play.

A **bounce buffer** is an intermediate buffer the kernel uses when a user-provided buffer is not DMA-safe. If the device's DMA engine cannot reach a given physical address (for example, the device is 32-bit and the buffer is above 4 GB), the kernel allocates a bounce buffer in reachable memory, copies the data in or out, and points the DMA engine at the bounce buffer. This is transparent to the driver but not free: every bounced operation includes a memory copy.

The performance impact shows up in two places. First, the copy doubles the memory traffic for the operation, which matters when the operation is bandwidth-sensitive. Second, the bounce-buffer pool is bounded; under pressure, allocations fail, and the driver must wait. A `bus_dma(9)` driver on a 64-bit system with plenty of memory rarely sees bounce buffers; a 32-bit driver on a system with memory above 4 GB sees them constantly.

The tuning knobs, when bounce buffers become a measurable cost:

- Ask the device if it supports 64-bit DMA. Many 32-bit-era devices actually implement 64-bit addressing and the driver can enable it with the right `bus_dma_tag_create()` flags.
- Use `BUS_SPACE_UNRESTRICTED` in the tag's `lowaddr` field if the device really is unrestricted, to tell `bus_dma` not to bounce.
- For truly 32-bit devices that must work on a 64-bit system, consider pre-allocating reachable buffers in the driver and copying there on the dispatch path, which at least moves the bounce cost to a known place.

This topic deserves its own chapter; Chapter 21 covers `bus_dma(9)` in depth. Here the point is that bounce buffers are a performance feature you may not have chosen consciously, and when they appear in a profile you now know what they mean.

### Per-CPU Counters Revisited

Section 2 introduced `counter(9)` and `DPCPU_DEFINE(9)`. This section is where those primitives matter most. On a driver with many CPUs all writing to the same counter, the counter's cache line becomes a contention point. `counter(9)` avoids it by keeping a per-CPU copy and summing on read.

A before-and-after example makes the point concrete. Suppose `perfdemo` has a single `atomic_add_64()` counter for total reads, updated on every read. On an 8-core system doing a parallel-reader benchmark, the counter cache line bounces between eight L1 caches. A `pmcstat` profile shows `atomic_add_64` surprisingly high in the sampled functions. Switching to `counter_u64_add()`:

- Each CPU updates its own copy. No contention.
- The cache line each CPU writes is in its own L1 exclusively.
- The fetch path sums all CPUs' copies, which happens only when someone reads the sysctl.

The result is that the counter's cost on the hot path drops from hundreds of nanoseconds per update (the cache miss plus the atomic) to a few nanoseconds (a direct write to a CPU-local cache line). On an 8-core system this can be a 10x reduction in counter cost.

A practical template for a driver with several per-CPU counters:

```c
#include <sys/counter.h>

struct perfdemo_stats {
    counter_u64_t reads;
    counter_u64_t writes;
    counter_u64_t bytes;
    counter_u64_t errors;
};

static struct perfdemo_stats perfdemo_stats;

static void
perfdemo_stats_init(void)
{
    perfdemo_stats.reads  = counter_u64_alloc(M_WAITOK);
    perfdemo_stats.writes = counter_u64_alloc(M_WAITOK);
    perfdemo_stats.bytes  = counter_u64_alloc(M_WAITOK);
    perfdemo_stats.errors = counter_u64_alloc(M_WAITOK);
}

static void
perfdemo_stats_free(void)
{
    counter_u64_free(perfdemo_stats.reads);
    counter_u64_free(perfdemo_stats.writes);
    counter_u64_free(perfdemo_stats.bytes);
    counter_u64_free(perfdemo_stats.errors);
}
```

Each field is a `counter_u64_t`, which is internally a pointer to per-CPU storage. Each increment is a per-CPU add. Each read is a sum across all CPUs.

For state that is not a counter, DPCPU is the tool:

```c
#include <sys/pcpu.h>

DPCPU_DEFINE_STATIC(struct perfdemo_cpu_state, perfdemo_cpu);

/* In a fast path, on the current CPU: */
struct perfdemo_cpu_state *s = DPCPU_PTR(perfdemo_cpu);
s->last_read_ns = now;
s->read_count++;
```

DPCPU is slightly less convenient than `counter(9)` because you have to define and initialise the per-CPU structure yourself, but it gives you the flexibility of per-CPU arbitrary state rather than just a counter.

### Per-CPU Data Patterns Beyond Counters

`counter(9)` handles the common case of per-CPU integer accumulation. DPCPU handles the general case of per-CPU anything. In practice there are four patterns that come up repeatedly.

**Pattern 1: Per-CPU cache of a scratch buffer**. When a driver needs a small scratch buffer on a hot path, and the buffer is always used on one CPU at a time, a DPCPU pointer to a per-CPU pre-allocated buffer avoids any allocation at all:

```c
DPCPU_DEFINE_STATIC(char *, perfdemo_scratch);

static int
perfdemo_init_scratch(void)
{
    int cpu;
    CPU_FOREACH(cpu) {
        char *p = malloc(PERFDEMO_SCRATCH_SIZE, M_PERFDEMO, M_WAITOK);
        if (p == NULL)
            return (ENOMEM);
        DPCPU_ID_SET(cpu, perfdemo_scratch, p);
    }
    return (0);
}

/* On the hot path: */
critical_enter();
char *s = DPCPU_GET(perfdemo_scratch);
/* ... use s ... */
critical_exit();
```

The `critical_enter()` / `critical_exit()` pair is necessary because the scheduler could preempt the thread between the `DPCPU_GET` and the use, and the thread could resume on a different CPU. Staying in a critical section prevents the migration. The cost is small (a few nanoseconds) but non-zero; use it whenever you access DPCPU data that must be stable across an operation.

**Pattern 2: Per-CPU state for lockless statistics**. Some drivers maintain rolling windows, last-timestamp values, or other state per CPU. DPCPU gives you the per-CPU storage without any synchronisation: each CPU reads and writes its own copy, and a global summary is computed on demand.

```c
struct perfdemo_cpu_stats {
    uint64_t last_read_ns;
    uint64_t cpu_time_ns;
    uint32_t current_queue_depth;
};

DPCPU_DEFINE_STATIC(struct perfdemo_cpu_stats, perfdemo_cpu_stats);

/* Fast path: */
critical_enter();
struct perfdemo_cpu_stats *s = DPCPU_PTR(perfdemo_cpu_stats);
s->last_read_ns = sbttons(sbinuptime());
s->cpu_time_ns += elapsed;
critical_exit();
```

The summary for reporting:

```c
static uint64_t
perfdemo_total_cpu_time(void)
{
    uint64_t total = 0;
    int cpu;
    CPU_FOREACH(cpu) {
        struct perfdemo_cpu_stats *s =
            DPCPU_ID_PTR(cpu, perfdemo_cpu_stats);
        total += s->cpu_time_ns;
    }
    return (total);
}
```

**Pattern 3: Per-CPU queues, one-way**. A driver that defers work to a taskqueue can maintain a per-CPU pending list that the producing CPU appends to and the consumer periodically drains. Because each producer is the only writer of its per-CPU list, no locks are needed on the producer side. The consumer still needs locking or atomic swaps to drain safely, but the hot path is lock-free.

**Pattern 4: Per-CPU configuration**. Some drivers parameterise their behaviour per-CPU for scaling reasons: each CPU has its own buffer size, its own retry count, its own soft limits. DPCPU makes this natural. The trade-off is that configuration values are scattered across CPUs; reporting them requires a loop that visits every CPU.

These patterns share a common theme: per-CPU state eliminates inter-CPU synchronisation on the hot path, in exchange for a small cost on the aggregation path. When the hot path is fast and the aggregation is infrequent, the trade-off is obviously favourable.

### When Not to Align, Allocate, or Cache

Every technique above has a cost. Alignment pads structures and wastes memory. Pre-allocation ties up buffers that may go unused. UMA zones consume kernel memory. Per-CPU counters use `N * CPU_COUNT` bytes of memory where the driver might have got away with `N * 1`. None of these costs is fatal, but in the aggregate they add up, especially on small systems.

The rule of thumb is the one you will hear throughout the chapter: do not apply these techniques until measurement demands. A driver that runs at 1000 operations per second on an idle desktop does not benefit from cache alignment, UMA zones, or per-CPU counters. A driver on a 64-core storage server processing 10 million operations per second may need all three.

A useful self-check before adding any of these techniques: *can I point to a specific `pmcstat` result, DTrace aggregation, or benchmark number that shows the current code is slow in the way the technique would fix?* If yes, apply it. If no, do not.

### Exercise 33.5: Cache-Line and Per-CPU Tuning

Using the `perfdemo` driver, do the following:

1. Baseline: measure the throughput of `perfdemo_read()` with the current atomic-counter implementation, using a multi-threaded reader that runs one thread per CPU. Record the throughput in ops/sec.
2. Add `__aligned(CACHE_LINE_SIZE)` to the atomic counters in the softc and pad to isolate them. Rebuild, reload, rerun. Record the new throughput.
3. Replace the atomic counters with `counter(9)` counters. Rebuild, reload, rerun. Record the new throughput.
4. Compare the three numbers.

The point is not that one approach is always best. The point is that the three approaches produce different numbers on different hardware, and the evidence tells you which is right for your target.

### Wrapping Up Section 5

Memory tuning is about understanding the memory hierarchy the CPU is working against. Cache-line alignment avoids false sharing. DMA alignment works through `bus_dma(9)`. Pre-allocation keeps the hot path out of the allocator. UMA zones specialise fixed-size frequent allocations. Per-CPU counters and per-CPU state eliminate cache-line contention on shared metrics. Each technique is a tool in the toolkit, and each deserves evidence before being applied.

The next section looks at the other major source of driver performance bottlenecks: interrupt handling and deferred work via taskqueues.

## Section 6: Interrupt and Taskqueue Optimization

Interrupts are where the driver meets the hardware, and for many drivers they are where the first performance cliff sits. An interrupt handler runs in a constrained context. It cannot sleep. It cannot acquire most sleepable locks. It competes with other threads for CPU time, but it also preempts them. A handler that takes too long holds up the whole system. A handler that fires too often consumes CPU the rest of the system needs. The techniques in this section are about keeping the interrupt handler fast and moving the rest of the work to a place where it can be done calmly.

The section covers four topics: measuring interrupt behaviour, filter versus ithread handlers (a brief recap), moving work to taskqueues, and interrupt coalescing.

### Measuring Interrupt Behaviour

Before you change anything, you need to know how often interrupts fire and how long the handler runs. FreeBSD gives you both metrics through standard tools.

**Interrupt rate** is reported by `vmstat -i`:

```console
# vmstat -i
interrupt                          total       rate
irq1: atkbd0                          10          0
irq9: acpi0                      1000000        500
irq11: em0 ehci0+                  44000         22
irq16: ohci0 uhci1+                 1000          0
cpu0:timer                       2000000       1000
cpu1:timer                       2000000       1000
cpu2:timer                       2000000       1000
cpu3:timer                       2000000       1000
Total                            8148010       4547
```

The `total` column is cumulative since boot; the `rate` column is per second over the sampling window. If your driver's interrupt is firing at thousands of hertz and you did not expect it, something is wrong. If it is firing at single digits per second and you did expect thousands, something else is wrong.

**Interrupt handler latency** is reported by DTrace. The `fbt` provider has probes on every non-static function, including the driver's interrupt handler. A one-liner that measures how long the handler runs:

```console
# dtrace -n 'fbt:perfdemo:perfdemo_intr:entry { self->t = timestamp; }
              fbt:perfdemo:perfdemo_intr:return /self->t/ {
                  @ = quantize(timestamp - self->t);
                  self->t = 0; }'
```

The resulting histogram tells you the latency distribution of your interrupt handler. A handler with a median of one or two microseconds and a P99 of under ten is well behaved. A handler with a median of 100 microseconds and a P99 in the milliseconds is doing too much in the wrong context.

**Interrupt storm detection** is built into FreeBSD. When an interrupt fires much too often (the default threshold is 100,000 per second), the kernel disables the interrupt source temporarily and logs a warning:

```text
interrupt storm detected on "irq18:"; throttling interrupt source
```

If you see this message in `dmesg`, your driver is either processing the interrupt incorrectly (not acknowledging it, letting it re-fire immediately) or the hardware is generating at a rate the driver cannot keep up with. Either is a bug; it is not a normal state.

### Filter Handlers vs Ithread Handlers

Chapter 13 covered the two shapes of interrupt handler in detail. This section relies on them; here is a short recap for context.

A **filter handler** runs in the interrupt context of the hardware interrupt itself. It has very strict constraints: it cannot sleep, it cannot acquire most locks, and it should do the minimum work needed to quiet the hardware and tell the kernel what to do next. It returns one of `FILTER_HANDLED`, `FILTER_STRAY`, or `FILTER_SCHEDULE_THREAD`. The last tells the kernel to run the associated ithread handler.

An **ithread handler** runs in a dedicated kernel thread, scheduled shortly after the filter returns `FILTER_SCHEDULE_THREAD`. It can acquire sleepable locks, do more complex work, and take as long as is reasonable without stalling the system. It is still preemptible but it is not the interrupt context.

The two-level design lets you split interrupt handling cleanly. The filter does the minimum: read the status register, figure out whether the interrupt is ours, acknowledge it so the hardware stops asserting it, and schedule the ithread if work remains. The ithread does the rest: process the data, wake up userland, deallocate buffers, update counters.

For performance, the question is: do you need both levels? The answer is almost always yes for drivers handling real hardware, and often no for simple devices. A filter-only design is fast: the handler runs once per interrupt, does its work, and returns. A filter-plus-ithread design adds the thread-scheduling cost (a few microseconds) but lets the main work run with fewer context constraints.

If your filter handler is fast and complete, keep the design filter-only. If it is large and you need sleepable locks, split into filter and ithread. If the ithread is itself fast and the split is just adding overhead, consolidate. The measurements will tell you which case you are in.

### Moving Work to Taskqueues

A common pattern for long-running driver work is to move it out of interrupt context entirely, into a taskqueue. A taskqueue is a simple, named queue of functions to run, scheduled by a dedicated kernel thread. You enqueue a task from the interrupt handler; the thread dequeues and runs it; the interrupt returns quickly.

The basic pattern:

```c
#include <sys/taskqueue.h>

struct perfdemo_softc {
    struct task perfdemo_task;
    /* ... */
};

static void
perfdemo_task_handler(void *arg, int pending)
{
    struct perfdemo_softc *sc = arg;
    /* long-running work here; can acquire sleepable locks */
}

static int
perfdemo_attach(device_t dev)
{
    struct perfdemo_softc *sc = device_get_softc(dev);

    TASK_INIT(&sc->perfdemo_task, 0, perfdemo_task_handler, sc);
    /* ... */
    return (0);
}

static void
perfdemo_intr(void *arg)
{
    struct perfdemo_softc *sc = arg;

    /* Quick filter work */

    taskqueue_enqueue(taskqueue_fast, &sc->perfdemo_task);
}
```

The filter handler does its minimal work and then schedules a task on `taskqueue_fast`, the kernel's default fast-taskqueue. The task runs on the taskqueue's thread, which is preemptible and can sleep. The interrupt handler returns as quickly as it can.

The `taskqueue_fast` queue is shared by many subsystems. On a heavily loaded system, this can mean your task waits behind others. If you need a taskqueue of your own, create one:

```c
struct taskqueue *my_tq;

/* In module init: */
my_tq = taskqueue_create("perfdemo_tq", M_WAITOK,
    taskqueue_thread_enqueue, &my_tq);
taskqueue_start_threads(&my_tq, 1, PI_NET, "perfdemo tq thread");
```

`taskqueue_start_threads` creates one thread bound to the queue. For a driver with CPU-bound work and a many-core system, you can use `taskqueue_start_threads_cpuset()` to pin threads to specific CPUs, which helps cache locality:

```c
#include <sys/cpuset.h>

cpuset_t cpus;

CPU_ZERO(&cpus);
CPU_SET(1, &cpus);   /* bind to CPU 1 */
taskqueue_start_threads_cpuset(&my_tq, 1, PI_NET, &cpus,
    "perfdemo tq thread");
```

The advantage of a dedicated taskqueue is predictable scheduling. Your task is not competing with the whole system for thread time on `taskqueue_fast`. The disadvantage is more threads and more memory, so do this only when the shared taskqueue is genuinely causing a problem.

### Interrupt Coalescing

Hardware interrupts have a fixed cost: saving CPU state, transitioning to kernel context, dispatching the handler, and returning. On modern hardware this is under a microsecond, but it is not zero. When a device fires very many interrupts per second, the cumulative cost becomes noticeable. Network NICs on a 10 Gbps link can generate hundreds of thousands of interrupts per second at peak; storage controllers on NVMe can generate more.

**Interrupt coalescing** is the technique of asking the hardware to batch multiple events into a single interrupt. Instead of interrupting once per packet, the NIC interrupts once per millisecond, and the driver handles all packets received in that millisecond. The throughput goes up because the per-event overhead drops; the latency goes up too, because events wait longer in the batch.

Not every driver supports coalescing, and it is a hardware feature. When the hardware provides it (most modern NICs and NVMe controllers do), the driver exposes knobs through sysctl or ioctl. The trade-off must be tuned to the workload: a latency-sensitive workload wants short coalescing windows; a throughput-sensitive workload wants long ones. Sometimes a driver exposes separate knobs for the two directions (RX and TX), allowing fine-grained tuning.

As a driver author, you have three options relating to coalescing:

1. **Use hardware coalescing**. If the hardware has it, expose a sysctl knob. The operator tunes the driver for their workload.
2. **Do software-side batching**. If the hardware does not, and the driver can usefully process events in groups, the driver can keep work pending until a threshold is reached and then dispatch the whole batch. `iflib(9)` does this for many network drivers.
3. **Do nothing**. If the event rate is low enough that coalescing would not help, or the latency budget is tight enough that coalescing would hurt, leave it alone.

The right choice is, again, the one the measurements point to. If `vmstat -i` shows your driver interrupting at hundreds of thousands of hertz and CPU time in the interrupt path is a noticeable fraction of total CPU, coalescing is worth trying. If the interrupt is at single digits per second, coalescing is irrelevant.

### Debouncing

A related concept is **debouncing**: filtering out redundant or too-close-together interrupts. GPIO input buttons are the classic example; a mechanical switch produces many rapid interrupts when pressed or released, and the driver must filter out the ones that are not real state changes. The technique is:

1. On interrupt, read the input and timestamp.
2. If the input matches the last-reported state and the timestamp is within the debounce window, ignore.
3. Otherwise, report the change and update the last-reported state.

A `sbinuptime()` timestamp and a 20-millisecond debounce window are sufficient for most mechanical switches. For other event sources, the threshold is domain-specific.

Debouncing is not usually called performance tuning, but it has the same effect: fewer interrupts handled, less CPU consumed, fewer spurious events reported. It is a small but real technique.

### Dedicated Threads for Long Work

The kernel allows a driver to have its own long-lived thread, distinct from both taskqueue threads and interrupt threads. Use `kproc_create(9)` or `kthread_add(9)` to create one. A dedicated thread makes sense when:

- The driver has a long-running loop (for example, a polling loop that replaces interrupts for deterministic timing).
- The work must not contend with other subsystems on a shared taskqueue.
- The work has its own cadence that does not match the interrupt cadence (for example, a periodic flush, a watchdog, or a state-machine driver).

For most drivers, a taskqueue is simpler and sufficient. A dedicated thread is a step up in complexity and should be justified by the work's nature.

### MSI and MSI-X: Per-Vector Interrupts

A classic device uses a single interrupt line: every event fires the same interrupt, the handler must distinguish them by reading a status register, and the handler runs on whichever CPU the kernel's interrupt balancer chose. A modern device supports Message-Signaled Interrupts (MSI) or the extended MSI-X variant, which lets the device send many distinct interrupts, each with its own vector and each configurable to a specific CPU.

For a driver author, MSI-X brings three performance advantages over legacy line-based interrupts.

First, **per-vector interrupts eliminate shared handlers**. With legacy interrupts, a driver's filter handler might fire for other drivers' events and must return `FILTER_STRAY` when the event is not its own. With MSI-X, each vector belongs to one driver; the filter handler only runs when its device has work.

Second, **per-vector CPU pinning reduces cache-line bouncing**. A multi-queue network driver can dedicate one RX vector to CPU 0, one to CPU 1, and so on. Each CPU processes its own queue, touching its own data structures, without sharing cache lines with the other CPUs. On a high-throughput driver, this scales linearly with core count.

Third, **interrupt routing reduces latency**. If a driver knows which CPU will consume a packet's data, it can request an interrupt on that CPU directly, so the data arrives with the cache already warm.

In FreeBSD, MSI-X is requested via `pci_alloc_msix(9)` during `attach()`. The driver asks for N vectors; the PCI subsystem allocates up to N (or fewer if the hardware supports fewer). Each vector is a standard `bus_alloc_resource()` IRQ that you set up with `bus_setup_intr(9)` as usual. The `iflib(9)` framework wraps the machinery for network drivers; for custom drivers, the raw API is only a few lines longer.

A small excerpt from a driver that allocates four MSI-X vectors:

```c
static int
mydrv_attach(device_t dev)
{
    struct mydrv_softc *sc = device_get_softc(dev);
    int n = 4;

    if (pci_alloc_msix(dev, &n) != 0 || n < 4) {
        device_printf(dev, "cannot allocate 4 MSI-X vectors\n");
        return (ENXIO);
    }

    for (int i = 0; i < 4; i++) {
        sc->irq[i] = bus_alloc_resource_any(dev, SYS_RES_IRQ,
            &(int){i + 1}, RF_ACTIVE);
        if (sc->irq[i] == NULL)
            return (ENXIO);
        if (bus_setup_intr(dev, sc->irq[i], INTR_TYPE_NET | INTR_MPSAFE,
                NULL, mydrv_intr, &sc->queue[i], &sc->ih[i]) != 0)
            return (ENXIO);
    }

    /* Optionally, pin each IRQ to a CPU: */
    for (int i = 0; i < 4; i++) {
        int cpu = i;
        bus_bind_intr(dev, sc->irq[i], cpu);
    }

    return (0);
}
```

The per-vector handler `mydrv_intr` runs only when *its* queue has work; it gets the per-queue softc pointer as its argument. The `bus_bind_intr()` call pins each vector to a specific CPU; on a system with more CPUs than vectors, you might pin every vector to a different CPU.

MSI-X is not always a win. On a single-queue device there is no benefit, just extra setup. On a low-interrupt-rate device the difference is negligible. But on any modern high-throughput device (10 Gbps NIC, NVMe drive), MSI-X is the standard interrupt model and using anything else leaves performance on the table.

### Polling as an Alternative to Interrupts

For the extreme upper end of throughput, some drivers abandon interrupts entirely in favour of *polling*. A polling driver runs a tight loop that checks the hardware for work, processes it immediately, and goes around again. The trade-off is CPU cost: a polling thread consumes a CPU continuously, even when idle. The benefit is interrupt cost elimination: at high event rates, the interrupt and context-switch overhead per event disappears.

FreeBSD's network stack supports polling via `ifconfig polling`. Storage drivers typically do not poll. For custom drivers, polling is worth considering only when:

- The event rate is extremely high (millions per second).
- Latency is critical enough that interrupt latency (under a microsecond) is too much.
- CPU is cheap (a dedicated core for the poll loop is acceptable).

Most drivers fit none of these criteria, and polling is the wrong choice. But knowing it exists lets you recognise when a profile shows interrupt overhead dominating, and gives you a last-resort alternative.

A middle ground is *adaptive polling*: switch between interrupt-driven and polling mode based on load. NAPI-style batching (named after the Linux subsystem that pioneered it, but widely used in FreeBSD's `iflib`) takes the first interrupt, disables further interrupts, polls until the queue is drained, then re-enables. This captures most of polling's efficiency at high rates while keeping idle-time cost low.

### NAPI-Style Batching in Practice

The `iflib(9)` framework implements NAPI-style batching automatically. A driver that uses `iflib` receives packets through a callback that polls the hardware queue until empty. For non-`iflib` drivers, the pattern is straightforward to implement by hand:

```c
static int
mydrv_filter(void *arg)
{
    struct mydrv_softc *sc = arg;

    /* Disable hardware interrupts for this queue. */
    mydrv_hw_disable_intr(sc);

    /* Schedule the ithread or taskqueue to drain. */
    return (FILTER_SCHEDULE_THREAD);
}

static void
mydrv_ithread(void *arg)
{
    struct mydrv_softc *sc = arg;
    int drained = 0;

    while (mydrv_hw_has_work(sc) && drained < MYDRV_POLL_BUDGET) {
        mydrv_process_one(sc);
        drained++;
    }

    if (!mydrv_hw_has_work(sc)) {
        /* Queue is empty; re-enable interrupts. */
        mydrv_hw_enable_intr(sc);
    } else {
        /* Budget hit with work remaining; reschedule. */
        taskqueue_enqueue(sc->tq, &sc->task);
    }
}
```

The filter disables the interrupt and schedules the ithread. The ithread polls up to `POLL_BUDGET` events, then checks whether the queue is empty. If yes, it re-enables the interrupt. If no, it reschedules itself to continue draining on the next pass. The budget prevents a single burst from monopolising the CPU; the empty-queue check prevents perpetually-polling when traffic stops.

NAPI-style batching is a good fit for medium-to-high-rate drivers where neither pure interrupts nor pure polling is ideal. The budget and the re-enable logic are the two places where mistakes happen; the budget must be large enough to amortise the interrupt re-enable cost but small enough not to stall other drivers, and the re-enable must happen before the hardware can forget about pending work.

### Budget Allocation Across Stages

A useful exercise for any performance-critical driver is to write down a *latency budget* across the driver's stages. The total budget is the operation's deadline (for example, 100 microseconds for a low-latency network packet). Subtract each stage's expected cost, and what remains is the headroom.

For a network driver receiving a packet:

- Interrupt dispatch: 1 us.
- Filter handler: 2 us.
- Ithread scheduling + start: 3 us.
- Packet processing (protocol layer): 20 us.
- Wakeup of userland reader: 5 us.
- Scheduler dispatch: 5 us.
- Userland copy and acknowledgment: 10 us.

Total: 46 us. On a 100 us budget, there are 54 us of headroom. If the driver starts missing its deadline, the budget tells you which stage is most likely to have slipped, and where to measure first.

The numbers above are illustrative; the actual numbers are hardware-specific and workload-specific. The habit of writing the budget before measuring is what makes measurement efficient. You start with a hypothesis about where time is going, confirm or refute it with the tools in Sections 2 through 4, and refine the budget as the driver evolves.

### Exercise 33.6: Interrupt vs Taskqueue Latency

With the `perfdemo` driver, add a second variant that simulates interrupt-like processing using a high-resolution callout. Compare two configurations:

1. All processing done directly in the callout's handler (interrupt-like, in a privileged context).
2. Callout enqueues a task on a taskqueue; the taskqueue thread does the processing.

Measure the end-to-end latency (the time from the simulated interrupt to when the processed data is visible to userland) in both cases. A DTrace script can record the timestamps; a userland reader with `nanosleep()` on an `select()` can see the delivery.

Configuration 1 will usually have lower latency and higher CPU concentration in the callout context. Configuration 2 will have slightly higher latency but smoother CPU use across threads. Which is better depends on the workload; the exercise is to see the trade-off with numbers, not to declare one universally correct.

### Wrapping Up Section 6

Interrupts are where the driver meets the hardware, and they are where performance cliffs first appear. Measuring interrupt rate and handler latency is easy with `vmstat -i` and DTrace. The filter-plus-ithread split keeps the interrupt context small; taskqueues move work to a calmer environment; dedicated threads offer isolation for special cases; coalescing and debouncing control the interrupt rate itself. Each technique has a cost, and each deserves evidence before it is applied.

The next section turns to the metrics and logging you leave in the driver after tuning ends: the `sysctl` tree that exposes your runtime state, the `log(9)` calls that report the conditions worth reporting, and the patterns that distinguish a shipped driver from a benchmark-only one.

## Section 7: Using sysctl and Logging for Runtime Metrics

The measurement techniques in Sections 2 through 6 are instruments you reach for when a specific question arises. The metrics in Section 7 are the ones that stay in the driver forever. They are the dashboard lights on a car: always visible, always current, always there to tell a future operator what the driver is doing. A driver without a good sysctl tree is a driver that an operator cannot diagnose after the original author has moved on. A driver with a good one is a driver that telegraphs its state whenever anyone asks.

This section covers sysctl tree design, the common metric shapes, rate-limited logging via `log(9)`, and the art of putting the right amount of observability into a shipped driver.

### Designing a sysctl Tree

The sysctl tree is hierarchical. Each node has a name, a type, a flag set, and usually a value. The top-level nodes (`kern`, `net`, `vm`, `hw`, `dev`, and a few others) are fixed by the kernel; a driver creates a subtree under one of them. For a hardware driver, `dev.<driver_name>.<unit>.<metric>` is the conventional placement. For a pseudo-device or non-hardware driver, `hw.<driver_name>.<metric>` is common.

The declarations use the macros in `/usr/src/sys/sys/sysctl.h`. A minimal driver subtree:

```c
SYSCTL_NODE(_dev, OID_AUTO, perfdemo, CTLFLAG_RD, 0,
    "perfdemo driver");

SYSCTL_NODE(_dev_perfdemo, OID_AUTO, stats, CTLFLAG_RD, 0,
    "Statistics");

SYSCTL_U64(_dev_perfdemo_stats, OID_AUTO, reads, CTLFLAG_RD,
    &perfdemo_reads_cached, 0, "Total read() calls");
```

The first `SYSCTL_NODE` creates `dev.perfdemo`. The second creates `dev.perfdemo.stats`. The `SYSCTL_U64` creates `dev.perfdemo.stats.reads` and points it at a `uint64_t` variable. From userland:

```console
# sysctl dev.perfdemo.stats.reads
dev.perfdemo.stats.reads: 12345
```

For a device with multiple instances, the more idiomatic path is to use `device_get_sysctl_ctx(9)` and `device_get_sysctl_tree(9)` inside the `attach()` method. FreeBSD will have already created `dev.perfdemo.0` and `dev.perfdemo.1` for two instances, and these helpers give you the handle to add children under each:

```c
static int
perfdemo_attach(device_t dev)
{
    struct perfdemo_softc *sc = device_get_softc(dev);
    struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
    struct sysctl_oid *tree = device_get_sysctl_tree(dev);

    SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "reads",
        CTLFLAG_RD, &sc->stats.reads_cached, 0,
        "Total read() calls");
    SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "errors",
        CTLFLAG_RD, &sc->stats.errors_cached, 0,
        "Total errors");
    /* ... */
    return (0);
}
```

The ctx and tree are released automatically when the device detaches, so you do not have to do cleanup by hand. This is the standard pattern for per-device metrics.

### Exposing `counter(9)` Values via sysctl

A `counter_u64_t` is a per-CPU counter, and exposing it through sysctl requires a little more work than a plain `uint64_t`. The pattern is a *procedural sysctl*: a sysctl that runs a function when read. The function fetches the counter sum and writes it to the sysctl buffer.

```c
static int
perfdemo_sysctl_counter(SYSCTL_HANDLER_ARGS)
{
    counter_u64_t *cntp = arg1;
    uint64_t val = counter_u64_fetch(*cntp);

    return (sysctl_handle_64(oidp, &val, 0, req));
}

SYSCTL_PROC(_dev_perfdemo_stats, OID_AUTO, reads,
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    &perfdemo_stats.reads, 0, perfdemo_sysctl_counter, "QU",
    "Total read() calls");
```

The `CTLFLAG_MPSAFE` flag says the handler does not need to be serialised by the sysctl subsystem's lock. `"QU"` is the type-string: `Q` for quad (64-bit) and `U` for unsigned. The handler is called on every read, so the sum is current.

Several modern drivers encapsulate this in a helper macro; see `/usr/src/sys/net/if.c` for an example of `SYSCTL_ADD_COUNTER_U64`. If you add many counter sysctls, it is worth defining a similar helper for your driver.

### What to Expose

A driver's sysctl tree is an interface. Like any interface, it should be thoughtful: expose enough to be useful, but not so much that the tree becomes cluttered and its values lose meaning. A good starting set for a data-plane driver:

- **Total operations**: reads, writes, ioctls, interrupts. Cumulative counters.
- **Total errors**: transient errors (retries), permanent errors (failures), partial-transfer errors.
- **Bytes moved**: total bytes transferred in each direction.
- **Current state**: queue depth, busy flag, mode.
- **Configuration**: interrupt coalescing threshold, buffer size, debug level.

For each, think about whether an operator would want the value to diagnose a problem. If yes, expose it. If no, keep it internal.

Avoid exposing implementation details the driver might change. The sysctl tree is an API; renaming a node breaks scripts and monitoring dashboards. Choose the names carefully and keep them stable.

### Read-Only, Read-Write, and Tunable

The `CTLFLAG_RD` flag makes a sysctl read-only from userland. The `CTLFLAG_RW` flag makes it writable, so an operator can change the driver's behaviour at runtime. `CTLFLAG_TUN` marks a sysctl as a **tunable**, which means its initial value can be set in `/boot/loader.conf` before the module loads.

Writable sysctls are flexible and dangerous. An operator can set a debug level, change a buffer size, toggle a feature flag. The driver must validate the written value carefully; an out-of-range buffer size can corrupt state. For most metrics, read-only is the right choice. For configuration, read-write with validation in a procedural handler is the pattern.

Here is a validated tunable that accepts values between 16 and 4096:

```c
static int perfdemo_buffer_size = 1024;

static int
perfdemo_sysctl_bufsize(SYSCTL_HANDLER_ARGS)
{
    int new_val = perfdemo_buffer_size;
    int error;

    error = sysctl_handle_int(oidp, &new_val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);
    if (new_val < 16 || new_val > 4096)
        return (EINVAL);
    perfdemo_buffer_size = new_val;
    return (0);
}

SYSCTL_PROC(_dev_perfdemo, OID_AUTO, buffer_size,
    CTLTYPE_INT | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_sysctl_bufsize, "I",
    "Buffer size in bytes (16..4096)");
```

The handler checks `req->newptr != NULL` to distinguish a write from a read (a read has a non-null `newptr` only if a value was supplied). It validates the range and updates the variable. The `CTLFLAG_RWTUN` flag combines `CTLFLAG_RW` and `CTLFLAG_TUN`: runtime-writable and loader-tunable.

A rule worth absorbing: *any sysctl that changes driver state must validate its input*. The alternative is a user-configurable crash.

### Rate-Limited Logging with `log(9)`

The kernel's `printf(9)` is fast but undisciplined: every call produces a line in `dmesg` regardless of rate. For informational messages that might fire often, the kernel provides `log(9)`, which tags the message with a priority level (`LOG_DEBUG`, `LOG_INFO`, `LOG_NOTICE`, `LOG_WARNING`, `LOG_ERR`, `LOG_CRIT`, `LOG_ALERT`, `LOG_EMERG`) and lets userland's `syslogd(8)` filter it. A message at `LOG_DEBUG` is only logged if syslogd is configured to accept debug messages; the default configuration drops them.

```c
#include <sys/syslog.h>

log(LOG_DEBUG, "perfdemo: read of %zu bytes returning %d\n",
    size, error);
```

The line still goes through the kernel's log buffer and costs the same as a `printf(9)` to produce, but it does not appear in `dmesg` unless syslogd is told to include debug messages. It is the right tool for diagnostic messages that an operator might sometimes want but that should not pollute the default log.

For messages that really should be rate-limited, FreeBSD has `ppsratecheck(9)` and `ratecheck(9)`. They return non-zero at most N times per second; use them to gate a print:

```c
static struct timeval perfdemo_last_err;

if (error != 0 && ratecheck(&perfdemo_last_err,
    &(struct timeval){1, 0})) {
    device_printf(dev, "transient error %d\n", error);
}
```

This rate-limits the print to once per second. The `struct timeval` in the second argument is the interval; `{1, 0}` means one second. If the error rate is a thousand per second, you get one log line per second instead of a thousand.

Rate-limited logging is the right pattern for any message that could plausibly fire on a hot path. A driver that logs every transient error at full rate can DoS itself through `dmesg` alone.

### `device_printf(9)` and Its Friends

For messages that identify the specific device, use `device_printf(9)`:

```c
device_printf(sc->dev, "attached at %p\n", sc);
```

It prepends the device name and unit number automatically: `perfdemo0: attached at 0xfffffe00c...`. This is the convention for any message that would otherwise need to include `sc->dev` in its format string. Every FreeBSD driver uses `device_printf(9)` for its attach and detach messages; emulate that pattern.

For messages that do not belong to a specific device, plain `printf(9)` is fine but should still be tagged with the module name: `printf("perfdemo: %s: ...\n", __func__, ...)`. The `__func__` identifier is a C99 built-in that expands to the current function's name; it makes logs much easier to trace to their source.

### Debug Modes

A common pattern in mature drivers is a **debug mode**: a writable sysctl that controls the verbosity of logging. At debug level 0 the driver logs only attach, detach, and real errors. At level 1 it logs transient errors. At level 2 it logs every operation. The pattern is cheap (a compare-to-zero on the hot path) and gives operators a safe way to turn on detailed logging when diagnosing a problem.

```c
static int perfdemo_debug = 0;
SYSCTL_INT(_dev_perfdemo, OID_AUTO, debug, CTLFLAG_RWTUN,
    &perfdemo_debug, 0,
    "Debug level (0=errors, 1=transient, 2=verbose)");

#define PD_DEBUG(level, sc, ...) do {                    \
    if (perfdemo_debug >= (level))                       \
        device_printf((sc)->dev, __VA_ARGS__);           \
} while (0)
```

Used as:

```c
PD_DEBUG(2, sc, "read %zu bytes\n", bytes);
PD_DEBUG(1, sc, "transient error %d\n", error);
```

At the default `perfdemo_debug = 0`, both lines are a single compare-to-zero that predicts well in the branch predictor. Neither message is produced. At `perfdemo_debug = 1`, only the transient-error line is produced. At `perfdemo_debug = 2`, both are.

The operator turns this on with:

```console
# sysctl dev.perfdemo.debug=2
```

and off again when done. This is a convention found in many FreeBSD drivers; reuse it.

### Tracking Behaviour Over Time

A counter that is polled every second gives you a rate. A sysctl that returns a histogram of recent latencies gives you a distribution. For longer-term tracking, you can keep a rolling window of values inside the driver, exposed through a sysctl that returns the array:

```c
#define PD_LAT_SAMPLES 60

static uint64_t perfdemo_recent_lat[PD_LAT_SAMPLES];
static int perfdemo_lat_idx;

static void
perfdemo_lat_record(uint64_t ns)
{
    perfdemo_recent_lat[perfdemo_lat_idx] = ns;
    perfdemo_lat_idx = (perfdemo_lat_idx + 1) % PD_LAT_SAMPLES;
}

static int
perfdemo_sysctl_recent_lat(SYSCTL_HANDLER_ARGS)
{
    return (SYSCTL_OUT(req, perfdemo_recent_lat,
        sizeof(perfdemo_recent_lat)));
}

SYSCTL_PROC(_dev_perfdemo, OID_AUTO, recent_lat,
    CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_sysctl_recent_lat, "S",
    "Last 60 samples of read latency (ns)");
```

The pattern works for any rolling window: latencies, queue depths, interrupt rates, whatever the operator might want to see in time order.

More sophisticated forms of the same idea store percentile estimates (P50, P95, P99) using streaming algorithms like exponential-weighted moving averages or the t-digest. For most drivers this is overkill; a simple rolling window, or even just a cumulative histogram stored as a bucket array, is enough.

### Exposing Histograms via sysctl

A cumulative histogram is one of the most useful long-lived metrics a driver can expose. Userland can poll it, subtract the previous snapshot from the current one, and compute the rate per bucket. Plotting the result over time gives an immediate view of the driver's latency distribution.

The pattern: declare an array of `counter_u64_t`, one per bucket. Update the correct bucket in the hot path (linear scan or constant-time bucketing). Expose the array through a single procedural sysctl.

```c
static int
perfdemo_sysctl_hist(SYSCTL_HANDLER_ARGS)
{
    uint64_t values[PD_HIST_BUCKETS];
    int i, error;

    for (i = 0; i < PD_HIST_BUCKETS; i++)
        values[i] = counter_u64_fetch(perfdemo_stats.hist[i]);

    error = SYSCTL_OUT(req, values, sizeof(values));
    return (error);
}

SYSCTL_PROC(_dev_perfdemo_stats, OID_AUTO, lat_hist,
    CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_sysctl_hist, "S",
    "Read latency histogram (buckets: <1us, <10us, <100us, "
    "<1ms, <10ms, <100ms, <1s, >=1s)");
```

From userland, a small script reads the array and prints it:

```sh
#!/bin/sh
sysctl -x dev.perfdemo.stats.lat_hist | awk -F= '{
    split($2, bytes, " ");
    for (i = 1; i <= length(bytes); i += 8) {
        val = 0;
        for (j = 7; j >= 0; j--) {
            val = val * 256 + strtonum("0x" bytes[i + j]);
        }
        printf "Bucket %d: %d\n", (i - 1) / 8, val;
    }
}'
```

The script is a few lines and gives you an immediately useful dashboard. Over time, extensions become obvious: compare two snapshots to get rates, plot the deltas over time, alert when a high-latency bucket crosses a threshold.

### Per-CPU Sysctl Presentation

For a driver with DPCPU state, there is a choice between aggregating across CPUs (the standard approach: compute a single number from `DPCPU_SUM` or a manual loop) and presenting per-CPU values separately. Per-CPU presentation is useful when the operator needs to diagnose CPU imbalance: one CPU handling most of the work, another idle, or queue-depth variance between CPUs.

A procedural sysctl that returns a per-CPU array:

```c
static int
perfdemo_sysctl_percpu(SYSCTL_HANDLER_ARGS)
{
    uint64_t values[MAXCPU];
    int cpu;

    for (cpu = 0; cpu < mp_ncpus; cpu++) {
        struct perfdemo_cpu_stats *s =
            DPCPU_ID_PTR(cpu, perfdemo_cpu_stats);
        values[cpu] = s->cpu_time_ns;
    }
    return (SYSCTL_OUT(req, values, mp_ncpus * sizeof(uint64_t)));
}
```

The handler visits each active CPU, reads its DPCPU state, and copies the values into a contiguous buffer. The userland reader sees an array of `uint64_t` sized `mp_ncpus`. For exporting to a monitoring tool, per-CPU presentation gives a detailed picture that aggregation obscures.

### Stable Interfaces for Operators

A driver's sysctl tree is an interface; once operators write scripts or dashboards against it, changing the names breaks things. A few rules help keep the tree stable over years.

First, **name things for what they are, not for how they are implemented**. A sysctl called `reads` describes a user-observable concept; a sysctl called `atomic_counter_0` describes an implementation detail. The former is stable across refactors; the latter forces a rename whenever the implementation changes.

Second, **version the interface if it has to change**. If you add a new field to an existing structure-valued sysctl, older scripts will still read the old size correctly. If you rename a node, add the new name as an alias first, then deprecate the old one over at least one release cycle.

Third, **document every node in the `descr` string**. The last argument to `SYSCTL_*` macros is a short description that appears in `sysctl -d`. Keep it accurate and useful; it is the only inline documentation an operator has when diagnosing at three in the morning.

Fourth, **avoid driver-internal concepts in names**. A counter named `uma_zone_free_count` requires the operator to know what UMA is; a counter named `free_buffers` describes what it counts in terms any operator can understand.

A driver that follows these rules produces a sysctl tree that ages well. The exercise in Section 7 is a chance to practise.

### Handling Sysctl Writes Safely

A writable sysctl is a public API for changing driver state at runtime. Every path that accepts user input must validate, synchronise, and report cleanly on failure. The pattern in production drivers is:

```c
static int
perfdemo_sysctl_mode(SYSCTL_HANDLER_ARGS)
{
    struct perfdemo_softc *sc = arg1;
    int new_val, error;

    new_val = sc->mode;
    error = sysctl_handle_int(oidp, &new_val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);

    if (new_val < 0 || new_val > 2)
        return (EINVAL);

    PD_LOCK(sc);
    if (sc->state != PD_STATE_READY) {
        PD_UNLOCK(sc);
        return (EBUSY);
    }
    sc->mode = new_val;
    perfdemo_apply_mode(sc);
    PD_UNLOCK(sc);

    return (0);
}
```

Three things are happening. First, the handler validates the value; out-of-range values return `EINVAL` rather than corrupting the driver. Second, the handler takes the driver's lock before mutating state; another CPU reading or writing the mode concurrently does not see half-updates. Third, the handler checks the driver's lifecycle state; changes are rejected if the driver is detaching or in error. Each of these is a small addition to the simplest possible handler, and each prevents a real class of bug.

A related consideration is whether the sysctl change is idempotent. If the new value equals the current value, the handler should do nothing (or at most confirm the current state). If the new value differs, the handler must change state atomically so that no one sees a partial update. The lock-then-validate-then-apply pattern above satisfies both constraints.

### Exercise 33.7: A Reporting sysctl Tree

Build out the `perfdemo` driver's sysctl tree with at least the following nodes:

- `dev.perfdemo.<unit>.stats.reads`: total reads.
- `dev.perfdemo.<unit>.stats.errors`: total errors.
- `dev.perfdemo.<unit>.stats.bytes`: total bytes.
- `dev.perfdemo.<unit>.stats.latency_avg_ns`: average latency in nanoseconds.
- `dev.perfdemo.<unit>.config.buffer_size`: buffer size (tunable, 16..4096).
- `dev.perfdemo.<unit>.config.debug`: debug level (0..2).

Use `counter(9)` for the counters; use a procedural sysctl for the derived latency average; use `CTLFLAG_RWTUN` for the tunables. Build and load the driver; verify each sysctl returns a sensible value; change a tunable at runtime and confirm the driver respects it.

The exercise produces an observable interface the rest of the chapter can rely on. It is also a nice self-contained piece of work that is worth adding to a portfolio.

### Wrapping Up Section 7

The sysctl tree is the driver's always-on observability interface. A thoughtful tree exposes totals, rates, states, and configuration. `counter(9)` values need procedural sysctls to fetch their summed totals. Rate-limited logging via `log(9)` and `ratecheck(9)` keeps informational messages from drowning the log. Debug modes give operators a safe way to enable verbose tracing when diagnosing problems. Each of these is a small investment; the cumulative effect is a driver that is auditable from the command line, diagnosed without rebooting, and maintained confidently after the author has moved on.

The last instructional section, Section 8, is the discipline section. It teaches you how to clean up after a tuning project, document the benchmarks that produced the result, update the manual page with the knobs the driver now exposes, and ship the optimised driver as a version the system can rely on.

## Section 8: Refactoring and Versioning After Tuning

Tuning adds scaffolding. Counters that measured specific hot paths, DTrace probes that answered a question once, print statements from an all-nighter, commented-out variants you want to keep handy: all of this accumulates during a performance project. The same discipline that produced the measurements produces the final cleanup. A driver that lives in the tree after tuning is the one whose author knew when to stop and how to leave the code in a state a maintainer can still read.

This section is about that cleanup. It covers what to remove, what to keep, how to document the work, and how to version the result.

### Removing Temporary Measurement Code

The most important cleanup is the removal of temporary measurement code. During tuning you may have added:

- `printf()` calls that trace specific operations.
- Counters that helped you find one bottleneck but do not belong in production.
- Timings that accumulated into global arrays.
- DTrace-style probes in the middle of hot paths that are not formally SDT-declared.
- Commented-out versions of optimisations you tried and discarded.

Each of these is a future maintenance cost. The rule is simple: if a piece of code was only useful for the tuning session, delete it. Version control keeps the history; the commit message explains what you tried. The working-tree code should read as if the measurements had never been there.

A useful discipline is to **mark temporary code with a comment when you add it**, so you know what to remove later:

```c
/* PERF: added for v2.3 tuning; remove before ship. */
printf("perfdemo: read entered at %ju\n", (uintmax_t)sbinuptime());
```

When the tuning project is done, grep for `PERF:` and remove every line. A driver with no `PERF:` markers is one that has been cleaned up; a driver with a dozen is one where the author forgot.

### What to Keep

Not every piece of measurement code should go. Some of it has lasting value and belongs in the shipped driver. The criteria:

- **Keep counters that tell operators what the driver is doing.** Total reads, writes, errors, bytes: these belong in the sysctl tree forever.
- **Keep SDT probes at operational boundaries.** They cost nothing when disabled and give any future engineer an instant way to measure the driver.
- **Keep configuration knobs that expose meaningful trade-offs.** The buffer size, the coalescing threshold, the debug level: these are interfaces to the operator.
- **Remove one-off prints and hand-written timings.** These were for one tuning session and have no place in shipped code.
- **Remove counters that are too specific to the question you were answering.** A counter named `reads_between_cache_miss_A_and_cache_miss_B` probably has no lasting value; a counter named `reads` does.

The test is: *would a future engineer benefit from this information six months from now?* If yes, keep it. If no, remove it.

### Benchmarking the Final Driver

After the scaffolding is gone, benchmark the final driver one more time. This benchmark is the one that becomes the *published* performance of the driver; record it in your logbook and in a plain-text document in the driver's source tree. A benchmark report should include:

- The driver version (for example, `perfdemo 2.3`).
- The machine: CPU model, core count, RAM size, FreeBSD version.
- The workload: what `dd` command, what file sizes, what userland threads, what coalescing window.
- The result: throughput, latency (P50, P95, P99), CPU usage.
- Context: what else was running, any relevant sysctl settings.

A reader who picks up the driver months later should be able to reproduce the benchmark without asking you. That reproducibility is the purpose of the report.

### Documenting Tuning Knobs

If the driver now exposes tunable sysctls that affect performance, document them. The right place is the driver's manual page (an `.4` page under `/usr/share/man/man4/`) or a section at the top of the driver's source file. A manual page section looks like:

```text
.Sh TUNABLES
The following tunables can be set at the
.Xr loader.conf 5
prompt or at runtime via
.Xr sysctl 8 :
.Bl -tag -width "dev.perfdemo.buffer_size"
.It Va dev.perfdemo.buffer_size
Size of the internal read buffer, in bytes.
Default 1024; valid range 16 to 4096.
Larger values increase throughput for bulk reads
but raise per-call latency.
.It Va dev.perfdemo.debug
Verbosity of debug logging.
0 logs errors only.
1 adds transient-error notes.
2 logs every operation.
Default 0; higher values should only be used
during diagnostic sessions.
.El
```

The format is arcane on a first look, but every FreeBSD manual page uses the same pattern. The existing `.4` pages under `/usr/src/share/man/man4/` are excellent templates; copy one whose style matches your driver and adapt.

For drivers that do not ship a manual page yet, this is the moment to write one. A driver without a manual page is underdocumented; one with a manual page is at a credibility threshold expected of base-system code.

### Versioning

A tuned driver is a new version of the driver. Mark it with `MODULE_VERSION()` in the driver source:

```c
MODULE_VERSION(perfdemo, 3);   /* was 2 before this tuning pass */
```

The version number is consumed by `kldstat -v` and by other modules that declare `MODULE_DEPEND()` against yours. Bumping it signals that the driver's behaviour has changed enough for consumers to care.

For a shipped driver, the convention is a major version for breaking changes, a minor version (or version suffix) for compatible additions, and a patch version for bug fixes. A pure performance tuning pass that adds no new functionality is a patch; a tuning pass that adds new sysctls is a minor; a tuning pass that changes the semantics of existing interfaces is a major. The book's exercises call a tuned driver `v2.3-optimized`; in reality, the version scheme is the one your project uses.

Updating the changelog is part of the versioning work. A `CHANGELOG.md` or a comment in the driver source is the right place. Each entry should include:

- The version and date.
- What changed at a high level (tuning, bug fix, new feature).
- The benchmark numbers if they changed.
- Any backward-incompatible changes operators need to know about.

The habit of keeping the changelog current is the habit that makes long-lived code maintainable. Drivers without changelogs accumulate lore; drivers with them accumulate history.

### Reviewing the Diff

Before shipping, read the whole diff of the tuning work as if you were a new maintainer reviewing the change. Look for:

- Code paths that have become harder to reason about.
- Comments that no longer match the code.
- Temporary code that survived the cleanup.
- Error paths that were changed but not retested.
- Locking that was added or removed.

This review catches the problems your own measurements did not. A piece of code that is correct but unreadable is a problem waiting to happen.

### The Performance Report

The final deliverable of a tuning project is a short written report. Not a blog post; not a presentation; a plain document that lives with the driver. A useful template:

```text
perfdemo v2.3-optimized - Performance Report
==============================================

Summary: v2.3 is a pure-tuning release that reduces median read
latency from 40 us to 12 us and triples throughput on a 4-core
amd64 test machine, without changing the driver's interface.

Goals (set before tuning):
  - Median read() latency under 20 us.
  - P99 read() latency under 100 us.
  - Single-thread throughput above 500K ops/sec.
  - Multi-thread throughput scaling linearly up to 4 CPUs.

Before (v2.2):
  - Median read: 40 us.
  - P99 read: 180 us.
  - Single-thread: 180K ops/sec.
  - 4-thread: 480K ops/sec (2.7x; sublinear).

After (v2.3):
  - Median read: 12 us.
  - P99 read: 65 us.
  - Single-thread: 520K ops/sec.
  - 4-thread: 1.95M ops/sec (3.75x; near-linear).

Changes applied:
  1. Replaced single atomic counter with counter(9).
  2. Cache-line aligned hot softc fields.
  3. Pre-allocated read buffers in attach().
  4. Switched to UMA zone for per-request scratch buffers.
  5. Added sysctl nodes for stats and config (non-breaking).

Measurements:
  All numbers from DTrace aggregations over 100,000-sample
  windows, 4-core amd64, 3.0 GHz Xeon, FreeBSD 14.3 GENERIC,
  otherwise idle. See benchmark/v2.3/ for raw data and scripts.

Tunables introduced:
  - dev.perfdemo.buffer_size: runtime buffer size (default 1024).
  - dev.perfdemo.debug: runtime debug verbosity (default 0).

Risks:
  Cache-line alignment increases softc memory by approximately
  200 bytes per instance. Unlikely to matter on modern systems
  but worth noting for memory-constrained embedded targets.

Remaining work:
  None for v2.3. Future tuning may investigate reducing P99
  latency further if workload analysis shows a specific cause.
```

A report like this becomes institutional knowledge. A maintainer two years from now reading this document has everything needed to understand what was done, why, and how to reproduce it.

### A/B Testing Tuning Changes

Before committing a tuning change to the tree, the responsible thing is to test it against the previous version under the same workload. An A/B test compares the two versions against a single benchmark, multiple times, under the same system state. If the new version is measurably better *and* the difference survives the noise of multiple runs, the change is worth keeping.

A simple A/B harness:

```sh
#!/bin/sh
# ab-test.sh <module-a-name> <module-b-name> <runs>

MODULE_A=$1
MODULE_B=$2
RUNS=$3

echo "Module A: $MODULE_A"
echo "Module B: $MODULE_B"
echo "Runs per module: $RUNS"
echo

for i in $(seq 1 $RUNS); do
    sudo kldload ./"$MODULE_A".ko
    time ./run-workload.sh 100000
    sudo kldunload "$MODULE_A"
    echo "A$i done"

    sudo kldload ./"$MODULE_B".ko
    time ./run-workload.sh 100000
    sudo kldunload "$MODULE_B"
    echo "B$i done"
done
```

Run this with `./ab-test.sh perfdemo-v22 perfdemo-v23 10`. The script alternates modules to prevent warm-up effects from biasing one in the other's favour. Ten runs each is usually enough to distinguish a 5% difference from noise.

A/B testing matters for two reasons. First, it forces you to state the comparison as *version A vs version B under workload X*, which is the shape a performance claim should have. Second, it catches regressions: if v2.3 is slower than v2.2 on some axis you did not measure, the A/B run shows it. A tuning change that improves one metric while hurting another is surprisingly common; only a disciplined comparison surfaces them.

### The Benchmark Harness

A sophisticated project eventually accumulates a proper benchmark harness: a reusable script that runs a known workload, collects a known set of metrics, and writes the results in a known format. A harness is worth building when:

- You run the same benchmark more than three or four times.
- Multiple people need to reproduce the benchmark.
- The results are used in performance reports and must be comparable across runs.

The harness typically includes:

- A setup script that loads the driver, configures its sysctls, and verifies the system is idle.
- A runner script that invokes the workload for a fixed duration or a fixed number of iterations.
- A collector that captures metrics before and after, computes deltas, and writes a structured report.
- A teardown script that unloads the driver and restores any system state.
- A results archive that preserves raw and processed outputs with timestamps and run identifiers.

For `perfdemo`, the harness lives in the lab directory as `final-benchmark.sh`. For a production driver, it lives in the source tree alongside the driver, so anyone can reproduce the results.

The details of the harness depend on the driver. What matters is that there is *some* harness; ad-hoc benchmarks that someone has to remember how to run are ad-hoc evidence that someone has to remember how to trust.

### Sharing Performance Numbers With the Project

For drivers that go into the FreeBSD base system or a significant port, the performance numbers are not a private deliverable. They become part of the project record: commit messages, mailing-list threads, release notes. The conventions for sharing are worth knowing.

**Commit messages** should summarise the change, the benchmark, and the result in the body. A good performance-related commit message looks like:

```text
perfdemo: switch counters to counter(9) for better scaling

Single counters in the softc were contended on multi-CPU systems.
Switching to counter(9) reduces per-call overhead and allows the
driver to scale to higher throughput under parallel load.

Measured on an 8-core amd64 Xeon at 3.0 GHz, FreeBSD 14.3-STABLE:
  Before: 480K ops/sec at 4-thread peak.
  After:  1.95M ops/sec at 4-thread peak (4x).

Benchmark data and scripts are in tools/perfdemo-bench/.
```

The first line is a short summary. The body expands on what changed, why, and what the measurement showed. A reader scanning commit history can understand the significance of the change in 60 seconds.

**Mailing-list threads** announcing the change follow the same structure but can include more context: the goal of the tuning, the alternatives considered, any caveats. Link to benchmark scripts and raw data if the scale of the change warrants discussion.

**Release notes** are terser. One or two lines: *The `perfdemo(4)` driver now uses `counter(9)` for its internal statistics. This reduces overhead on multi-CPU systems and allows higher throughput under parallel load.*

Each audience gets what it needs. The pattern is that every performance claim, at every level of detail, points at a reproducible measurement.

### Exercise 33.8: Ship perfdemo v2.3

Using the state of `perfdemo` you built across Sections 2 through 7, produce a v2.3-optimized release:

1. Remove every PERF: marker and the scaffolding it denoted.
2. Decide what stays in the sysctl tree and what was only useful during tuning.
3. Update `MODULE_VERSION()` to 3 (or your chosen new number).
4. Update the driver's manual page (or write one if it does not exist) with the TUNABLES section.
5. Run the final benchmark and record the numbers.
6. Write the performance report.

The exercise is the entire chapter's output in a concrete form. When you finish, you have a driver that would pass review in any serious FreeBSD project, with the performance measurements that justify its state.

### Wrapping Up Section 8

Tuning is only half a project. The other half is the discipline of cleaning up, documenting, and shipping. A driver with clean measurement primitives, a thoughtful sysctl tree, honest manual-page documentation, an updated version number, and a written performance report is the driver that ages well. The shortcut of skipping any of these steps is tempting, but the cost is paid by the next maintainer, which might be your future self.

The instructional portion of the chapter is complete. The next sections are the hands-on labs, the challenge exercises, the troubleshooting reference, the Wrapping Up, and the bridge into Chapter 34.

## Putting It All Together: A Complete Tuning Session

Before the labs, a worked walkthrough that ties the eight sections into a single story. This is the shape a real tuning session takes, compressed into a few pages. The session is not fictional; it is the same pattern that produces most of the performance improvements in the FreeBSD tree.

**The driver.** `perfdemo` v2.0 is a working character device that produces synthetic data on `read()`. Its hot path takes a mutex, allocates a temporary buffer via `malloc(9)`, fills it with pseudo-random data, copies it to userland via `uiomove(9)`, frees the buffer, releases the mutex, and returns. It has a single atomic counter for total reads and no sysctl tree beyond what FreeBSD's `device_t` gives it for free.

**The goal.** A user reports that `perfdemo` is slow in their production workload: they run forty concurrent reader threads, and throughput saturates at about 600,000 ops/sec across all threads on their 16-core amd64 server. They want 2 million or better. The stated goal is *median `read()` latency under 20 us and aggregate throughput above 2 million ops/sec on a 16-core amd64 Xeon running FreeBSD 14.3 GENERIC, with forty reader threads pinned across CPUs 0-15*.

**Measurement, round 1.** Before changing anything, we need numbers. We load the driver, run the user's workload, and collect baseline data.

`vmstat -i` does not show any unusual interrupt rates, so we rule out interrupt problems early. `top -H` shows the system's CPU at 65%; the driver is clearly not the only consumer, but it is a significant one. We instrument the driver minimally: one `counter(9)` for total reads, one for total nanoseconds of latency, both exposed via a procedural sysctl. We run the workload for sixty seconds and record the average latency as 52 microseconds. The goal is under 20. We are 32 microseconds off.

**DTrace, round 1.** With the baseline in hand, we reach for DTrace. A one-liner to see the function-level latency distribution:

```console
# dtrace -n '
fbt:perfdemo:perfdemo_read:entry { self->s = timestamp; }
fbt:perfdemo:perfdemo_read:return /self->s/ {
    @lat = quantize(timestamp - self->s);
    self->s = 0;
}'
```

After a minute the histogram shows a clear shape. P50 is around 30 us; P95 is 150 us; P99 is a surprising 1.5 ms. The long tail is where the user's pain comes from.

We widen the investigation. Another one-liner:

```console
# dtrace -n 'lockstat:::adaptive-block {
    @[((struct lock_object *)arg0)->lo_name] = count();
}'
```

The top entry: *perfdemo_softc_mtx* with about 30,000 blocks during the one-minute window. Our driver's mutex is contended; forty reader threads are serialising on it.

**The first fix.** The mutex protects a counter and a pool pointer; in principle the counter could be per-CPU and the pool pointer could be lockless. We replace the atomic counter with `counter_u64_add()`, which does not require the driver's lock, and we rework the pool into a per-CPU array of buffers (one per CPU). The hot path no longer needs to take the mutex for the common case; the mutex only guards the pool's administrative paths (init, fini, resize).

We rebuild, reload, rerun. Average latency drops from 52 us to 14 us. Throughput climbs from 600K to 1.8M ops/sec. We are close to the goal but not there.

**DTrace, round 2.** With the mutex out of the way, we profile again:

```console
# dtrace -n 'profile-997 /arg0 != 0/ { @[stack()] = count(); }'
```

The top stack now shows most of the time in `uiomove`, `malloc`, and `free`. The allocator is the next bottleneck; every read is allocating and freeing a temporary buffer. The counter-level cost is negligible.

**The second fix.** We switch from `malloc(9)` on the hot path to a UMA zone created at attach, with `UMA_ALIGN_CACHE` for per-CPU-aligned items. The zone is sized for the expected working set; `vmstat -z` confirms the zone population stabilises a few seconds after workload start.

Rebuild, reload, rerun. Average latency drops from 14 us to 9 us. Throughput climbs from 1.8M to 2.3M ops/sec. We have met the goal: median under 20 us and throughput above 2 million.

**pmcstat, round 1.** We are inside the goal, but before declaring victory we want to know whether the driver is now well-balanced across CPUs. We run a sampling session:

```console
# pmcstat -S cycles -O /tmp/perfdemo.pmc ./run-workload.sh
# pmcstat -R /tmp/perfdemo.pmc -T
```

The top functions are `uiomove`, `perfdemo_read`, and a couple of cache-coherence primitives from the kernel. No single bottleneck; the driver is now spending its time on the work it should be doing. Good.

A counting session:

```console
# pmcstat -s cycles -s instructions ./run-workload.sh
```

Prints an IPC of about 1.8. That is healthy for mostly-memory-moving code; we are not leaving hardware throughput on the table.

**Cleanup.** We read the diff. Four `PERF:` markers remain from the investigation; we remove them. Two commented-out variants exist from the second fix; we remove them. A debug-level sysctl that we added at one point but never used finds its way in; we remove it too.

**Sysctl tree.** The tree now has `dev.perfdemo.0.stats.reads`, `dev.perfdemo.0.stats.errors`, `dev.perfdemo.0.stats.bytes`, `dev.perfdemo.0.stats.lat_hist` (the histogram), and `dev.perfdemo.0.config.debug` (a level-0-to-2 debug verbosity). Each is documented in the `descr` string.

**Manual page.** A paragraph in the existing `.4` page describes the new tunables. The benchmark-report document describes the tuning and the numbers.

**Final numbers.** On the 16-core Xeon under the user's workload:

- Median `read()` latency: 9 us.
- P95 `read()` latency: 35 us.
- P99 `read()` latency: 95 us.
- Throughput: 2.3 million ops/sec.

**Time invested.** Roughly one engineer-day from first measurement to final report. The two fixes are small; finding them took longer than writing them. That ratio is typical.

**Lessons.** Three points stand out.

First, the investigation did not start with code changes. It started with measurement, moved through DTrace, and only then changed code. Every change was motivated by a specific observation.

Second, two fixes were enough. The mutex and the allocator were the two bottlenecks; the third-order optimisations (cache alignment on small fields, NAPI-style batching, MSI-X) were unnecessary. The chapter described them all because you have to know they exist; the session showed that you apply them selectively.

Third, the scaffolding was temporary. `PERF:` markers, commented-out variants, an unused debug level: all removed before ship. The cleaned-up driver looks simple because the tuning left little trace. That simplicity is the goal.

The labs below walk through the same shape of session, with concrete code and commands, at each stage.

## Hands-On Labs

This chapter's labs are built around a small pedagogical driver called `perfdemo`. The driver is a character device that synthesises data on `read()` at a controlled rate, and exposes a sysctl tree. It is deliberately uncomplicated; the interesting part is what the labs do *with* it. Each lab takes the driver through one stage of the performance workflow: baseline measurement, DTrace tracing, PMC sampling, memory tuning, interrupt tuning, and final cleanup.

All files for the labs live under `examples/part-07/ch33-performance/`. Each lab directory has its own `README.md` with build and run instructions. Clone the directory, follow the README for each lab, and work through them in order.

### Lab 1: Baseline `perfdemo`, Counters, and sysctl

**Objective:** Build the baseline `perfdemo` driver, load it, and exercise its sysctl-based counters. This lab is the foundation for every other lab in the chapter; take the time to get it right.

**Directory:** `examples/part-07/ch33-performance/lab01-perfcounters/`.

**Prerequisites:** A FreeBSD 14.3 system (physical, virtual, or jail with kernel build tools), the FreeBSD source tree at `/usr/src`, and root access to load kernel modules.

**Steps:**

1. Change into the lab directory: `cd examples/part-07/ch33-performance/lab01-perfcounters/`.
2. Read `perfdemo.c` to familiarise yourself with the baseline driver. Note the structure: a `module_event_handler`, a `probe`/`attach`/`detach` set, a `cdevsw` with a read method, a softc, and the sysctl tree under `hw.perfdemo`.
3. Build the module with `make`. If the build fails, check that `/usr/src` is populated and that you can build other kernel modules (the easiest test: `cd /usr/src/sys/modules/nullfs && make`).
4. Load the module: `sudo kldload ./perfdemo.ko`. Confirm it is loaded: `kldstat | grep perfdemo`.
5. Check the sysctl tree: `sysctl hw.perfdemo`. You should see:

    ```
    hw.perfdemo.reads: 0
    hw.perfdemo.writes: 0
    hw.perfdemo.errors: 0
    hw.perfdemo.bytes: 0
    ```

6. Verify the device node exists: `ls -l /dev/perfdemo`.
7. In one terminal, start a watch: `watch -n 1 'sysctl hw.perfdemo'`.
8. In another terminal, run the workload: `./run-workload.sh 100000`. The script issues 100,000 reads against `/dev/perfdemo`. It prints its wall-clock time on completion.
9. Verify the counters moved as expected. After the workload, `hw.perfdemo.reads` should be around 100,000 (a few extras from ad-hoc reads are normal).
10. Run the workload at a larger size: `./run-workload.sh 1000000`. Record the wall-clock time again. Divide `1000000 / wallclock_seconds` to get ops/sec.
11. Unload the driver: `sudo kldunload perfdemo`.

**What you should see:**

- The sysctl counters increase as the workload runs.
- The final values should match the workload's request count (within one or two operations of variance).
- The wall-clock time of the 1M-read run should be a few seconds on a modern machine.
- No kernel warnings in `dmesg`.

**What you should record in your logbook:**

- The wall-clock time and ops/sec of both workload runs.
- The machine: CPU model, core count, RAM, FreeBSD version.
- Any `dmesg` output from the module-load and module-unload events.
- The final sysctl values.

**Expected baseline numbers (for comparison):**

On an 8-core amd64 Xeon at 3.0 GHz running FreeBSD 14.3-RELEASE with the baseline driver, a 1M-read single-threaded workload completes in roughly 3 seconds, giving about 330,000 ops/sec. Your numbers will differ based on hardware, but the order of magnitude should be similar.

**Common mistakes:**

- Forgetting to load the driver before running the workload. The loader script will fail with `ENODEV` if `/dev/perfdemo` is missing.
- Running the loader script with a very small count (for example, 100) and expecting stable numbers. The noise of measurement starts to dominate below a few thousand iterations. Use 100,000 or more for baseline runs.
- Confusing `hw.perfdemo.reads` with `hw.perfdemo.bytes`. The counter for total reads is distinct from the counter for total bytes. A 100-byte read increments `reads` by one and `bytes` by 100.
- Forgetting to unload the driver before rebuilding. A stale `.ko` in the kernel can cause confusing symptoms on the next load.

**Troubleshooting:** If the module fails to load with *module version mismatch*, your kernel version does not match the one `perfdemo` was built against. Rebuild after ensuring `/usr/obj` and `/usr/src` match your running kernel.

### Lab 2: DTrace `perfdemo`

**Objective:** Use DTrace to understand what `perfdemo`'s read path is doing, measured from outside without modifying the driver.

**Directory:** `examples/part-07/ch33-performance/lab02-dtrace-scripts/`.

**Prerequisites:** Lab 1 complete; the `perfdemo` driver loaded.

**Steps:**

1. Ensure the `perfdemo` driver is loaded (from Lab 1). If not, `cd` to `lab01-perfcounters/` and `sudo kldload ./perfdemo.ko`.
2. Load the DTrace providers: `sudo kldload dtraceall`. You can verify with `sudo dtrace -l | head` (it will list a few probes).
3. In one terminal, run the workload continuously: `while :; do ./run-workload.sh 10000; done`. Let it churn.
4. In another terminal, run the simple count one-liner:

    ```
    # sudo dtrace -n 'fbt:perfdemo:perfdemo_read:entry { @ = count(); }'
    ```

    Let it run for 30 seconds, then Ctrl-C. You should see a number in the tens of thousands. Divide by 30 to get reads/sec, which should match what `hw.perfdemo.reads` delta shows.

5. Run the latency histogram script `read-latency.d` in this lab directory:

    ```
    # sudo dtrace -s read-latency.d
    ```

    Let it run for 30 seconds. The histogram will look something like:

    ```
               value  ------------- Distribution ------------- count
                 512 |                                         0
                1024 |@                                        125
                2048 |@@@@@                                    520
                4096 |@@@@@@@@@@@@@@@@@@                       1850
                8192 |@@@@@@@@@@@@@                            1320
               16384 |@@@                                      290
               32768 |                                         35
               65536 |                                         8
              131072 |                                         2
              262144 |                                         0
    ```

    Record the P50 (the bucket that contains the median) and the P99 (the bucket that crosses 99% cumulative).

6. Run the lock-contention script `lockstat-simple.d`:

    ```
    # sudo dtrace -s lockstat-simple.d
    ```

    Let it run for 30 seconds. Look for `perfdemo` or `perfdemo_softc_mtx` in the output. The baseline driver *will* show contention on its mutex if you run the workload with concurrency.

7. Run the profile-sampling script `profile-sample.d`:

    ```
    # sudo dtrace -s profile-sample.d
    ```

    Let it run for 60 seconds. The output lists the top kernel stacks by sample count. `perfdemo_read` should appear among the top entries.

8. Stop the workload (Ctrl-C in its terminal).

**What you should see:**

- `read-latency.d` produces a histogram with a bell-like peak around the median, and a tail to the right.
- `lockstat-simple.d` produces a list of locks touched during the measurement; if the workload is concurrent, `perfdemo_softc_mtx` appears.
- `profile-sample.d` produces a list of function stacks; `perfdemo_read`, `uiomove`, `copyout`, and `_mtx_lock_sleep` are typical entries.

**What you should record in your logbook:**

- P50 and P99 latency.
- Top three functions from `profile-sample.d`.
- The contended lock names from `lockstat-simple.d`.

**Expected results (for comparison):**

On the same 8-core Xeon, the baseline driver under 4-thread concurrent read load shows a median around 8 us, P99 around 60 us, heavy contention on `perfdemo_softc_mtx`, and top profile entries dominated by `perfdemo_read`, `uiomove`, and `_mtx_lock_sleep`.

**Common mistakes:**

- Running DTrace before the driver is loaded. The `fbt:perfdemo:` probes do not exist until `perfdemo.ko` is in the kernel.
- Writing scripts that print per-event. The DTrace buffer can drop events; always aggregate and print at the end.
- Forgetting to Ctrl-C the DTrace session at the right moment. Some aggregations only print on explicit stop.
- Running the workload at too low a rate to produce interesting data. DTrace latency histograms need at least tens of thousands of samples for clean percentile estimates.
- Forgetting to load `dtraceall`. A bare DTrace install without providers will complain about unknown providers.

**Troubleshooting:** If `dtrace -l | grep perfdemo` is empty after the driver is loaded, check that `dtraceall` is loaded (`kldstat | grep dtrace`). If the probes still do not appear, the driver may have been compiled without debug symbols; check that `make` used the default flags and did not strip `-g`.

### Lab 3: pmcstat `perfdemo` (optional, requires hwpmc)

**Objective:** Sample `perfdemo` with hardware performance counters and interpret the output. This lab is richer than the others because `pmcstat` has more knobs than DTrace; plan on spending extra time.

**Directory:** `examples/part-07/ch33-performance/lab03-pmcstat/`.

**Prerequisites:** A physical or fully paravirtualised machine where PMCs are available. On most cloud VMs and on shared hypervisors, PMCs are restricted; if `pmcstat -L` lists only a handful of events or the tool refuses to start, follow the alternative path with DTrace's `profile` provider.

**Steps:**

1. Ensure `hwpmc` is loaded: `sudo kldload hwpmc` (or confirm it is already loaded with `kldstat | grep hwpmc`). Check `dmesg | tail` after loading; you should see a line like `hwpmc: SOFT/16/64/0x67<REA,WRI,INV,LOG,EV1,EV2,CAS>`.

2. Verify the tool sees events: `pmcstat -L | head -30`. You should see a mix of portable event names (`cycles`, `instructions`, `cache-references`, `cache-misses`) and vendor-specific names (for Intel, entries starting with `cpu_clk_`, `inst_retired`, `uops_issued`, and so on).

3. Run a counting session during a workload. The `-s` flag (lowercase) is for system-wide counting; use this to see the driver's contribution plus everything else the kernel is doing:

    ```
    # sudo pmcstat -s instructions -s cycles -O /tmp/pd.pmc \
        ./run-workload.sh 100000
    ```

    After the workload finishes, `pmcstat` prints the totals:

    ```
    p/instructions p/cycles
        3.2e9          9.5e9
    ```

    Divide instructions by cycles to compute IPC: 3.2 billion / 9.5 billion = 0.34. An IPC below 1 on a modern CPU usually indicates stalls; likely memory or branch mispredicts.

4. Run a sampling session. The `-S` flag (uppercase) configures sampling:

    ```
    # sudo pmcstat -S cycles -O /tmp/pd-cycles.pmc \
        ./run-workload.sh 100000
    # pmcstat -R /tmp/pd-cycles.pmc -T
    ```

    The `-T` output is the top-N function list. You should see something like:

    ```
     %SAMP CUM IMAGE            FUNCTION
      13.2 13.2 kernel          perfdemo_read
       9.1 22.3 kernel          uiomove_faultflag
       8.5 30.8 kernel          copyout
       6.2 37.0 kernel          _mtx_lock_sleep
       4.9 41.9 kernel          _mtx_unlock_sleep
       ...
    ```

    Note the top five functions and their percentages.

5. Run a second sampling session, this time with `LLC-misses` to see where cache misses happen (requires Intel CPU):

    ```
    # sudo pmcstat -S LLC-misses -O /tmp/pd-llc.pmc \
        ./run-workload.sh 100000
    # pmcstat -R /tmp/pd-llc.pmc -T
    ```

    Compare the top entries to the cycle samples. If the same functions appear in both, the time is memory-bound; if different functions appear, the cycle-hot functions are CPU-bound (arithmetic, branches).

6. If you have the FlameGraph tools installed (install them from the `sysutils/flamegraph` port: `sudo pkg install flamegraph`), generate an SVG:

    ```
    # sudo pmcstat -R /tmp/pd-cycles.pmc -g -k /boot/kernel > pd.stacks
    # stackcollapse-pmc.pl pd.stacks > pd.folded
    # flamegraph.pl pd.folded > pd.svg
    ```

    Open `pd.svg` in a browser. The SVG is interactive; click any box to zoom into that call stack.

7. Inspect the flame graph. The bottom boxes (system call entry) should be wide and boring. Above them, your driver's functions appear as narrower stacks. The width of each box is its fraction of CPU time; the height shows call depth.

**Alternative if hwpmc is unavailable:** Use DTrace's `profile` provider:

```console
# sudo dtrace -n 'profile-997 /arg0 != 0/ { @[stack()] = count(); }' \
    -o /tmp/pd-prof.txt
```

Let it run for a minute while the workload runs, Ctrl-C, then examine `/tmp/pd-prof.txt`. The output is similar to pmcstat's callgraph, though coarser because the profile sample rate is fixed.

**What you should record in your logbook:**

- The IPC from the counting session.
- The top five functions from the sampling session, with their percentages.
- The top three functions in the LLC-misses session (if available) and whether they matched the cycle-samples list.
- A screenshot or description of the flame graph's dominant stacks.

**Expected baseline numbers (for comparison):**

On the baseline driver on an 8-core Xeon, IPC is typically 0.3 to 0.5 (memory-bound). The top function is usually `perfdemo_read` at 10-15% of cycles, with `uiomove`, `copyout`, and `_mtx_lock_sleep` following. After tuning (Labs 4 and 6), IPC should climb above 1.0 and `_mtx_lock_sleep` should disappear from the top entries.

**Common mistakes:**

- Forgetting to load `hwpmc` before running `pmcstat`. The tool will report *no device* or *no PMCs available*.
- Running the sampling session for too short a window. A few seconds of samples gives noisy data; a minute is usually enough.
- Misreading `pmcstat -T`'s output. The `%SAMP` column is the fraction of samples a function received across all CPUs; a 10% value means 10% of time, not 10% of instructions.
- Using the wrong flag: `-s` is counting, `-S` is sampling. They produce different outputs and the difference is easy to miss when reading someone else's scripts.
- Trying to sample a function that got inlined. `inst_retired.any` counts instructions retired; an inlined function's instructions are counted against the caller, not the inlined one. If you expect to see function X in the samples and it does not appear, check with `objdump -d perfdemo.ko | grep X` to see whether X was compiled as a real function.

**Troubleshooting:** If `pmcstat` hangs or prints *ENOENT*, the event name is wrong. `pmcstat -L` lists every event name the kernel knows about on your CPU; pick one from that list. If the tool crashes with a signal, `hwpmc` may not be fully initialised; unload and reload the module.

### Lab 4: Cache Alignment and Per-CPU Counters

**Objective:** Measure the performance impact of cache-line alignment and the `counter(9)` API compared to a plain atomic, under concurrent multi-CPU load.

**Directory:** `examples/part-07/ch33-performance/lab04-cache-aligned/`.

**Prerequisites:** Labs 1 and 2 complete, so you know the baseline driver's behaviour and how to use DTrace.

**Steps:**

1. Build three variants of the driver, each in its own subdirectory:
   - `v1-atomic`: single atomic counter, default layout.
   - `v2-aligned`: single atomic counter with `__aligned(CACHE_LINE_SIZE)` and padding around it to isolate the cache line.
   - `v3-counter9`: `counter(9)` counter instead of an atomic.

   Each subdirectory has its own `Makefile`. Run `make` in each. The only differences between the three sources are in the softc layout and the counter increment lines.

2. Identify your CPU count: `NCPU=$(sysctl -n hw.ncpu)`.

3. For each variant, run the multi-threaded reader script `./run-parallel.sh <N>` with `N` equal to the number of CPUs:

    ```
    # cd v1-atomic && sudo kldload ./perfdemo.ko && ./run-parallel.sh $NCPU
    # sudo kldunload perfdemo

    # cd ../v2-aligned && sudo kldload ./perfdemo.ko && ./run-parallel.sh $NCPU
    # sudo kldunload perfdemo

    # cd ../v3-counter9 && sudo kldload ./perfdemo.ko && ./run-parallel.sh $NCPU
    # sudo kldunload perfdemo
    ```

    The script spawns `N` reader threads, each pinned to a different CPU, each issuing reads as fast as it can for a fixed duration. It prints the aggregate throughput at the end.

4. Record the throughput (ops/sec) for each variant.

5. Now repeat with half and double the thread count:

    ```
    # ./run-parallel.sh $((NCPU / 2))
    # ./run-parallel.sh $((NCPU * 2))
    ```

    Compare the three throughputs at each thread count. Half-CPU runs usually show less contention; double-CPU runs show heavy contention and scheduler effects.

6. For the clearest demonstration, run a DTrace script on each variant while the workload runs:

    ```
    # sudo dtrace -n 'lockstat:::adaptive-block /
        ((struct lock_object *)arg0)->lo_name == "perfdemo_mtx" / {
            @ = count();
        }'
    ```

    `v1-atomic` should show significant blocking; `v3-counter9` should show almost none.

**What you should see:**

- `v1-atomic` is the baseline. Its throughput does not scale well beyond about half the CPUs; the atomic counter's cache-line bouncing becomes a bottleneck.
- `v2-aligned` improves if the counters were previously packed with other hot fields on the same cache line. If the original softc had the atomic counter on its own line already, alignment is a no-op.
- `v3-counter9` scales near-linearly to the CPU count. Each CPU updates its own per-CPU copy; no cache-line bouncing on the counter.

**Expected results (for comparison):**

On an 8-core Xeon with 8 threads running this workload:

- `v1-atomic`: around 600K ops/sec aggregate.
- `v2-aligned`: around 680K ops/sec aggregate.
- `v3-counter9`: around 2.0M ops/sec aggregate.

At 16 threads (oversubscribed):

- `v1-atomic`: around 550K (starts dropping).
- `v2-aligned`: around 650K (also drops).
- `v3-counter9`: around 1.8M (held up by scheduler overhead, not contention).

The per-CPU counter scales; the atomics do not.

**What you should record in your logbook:**

- Throughput numbers for each variant and each thread count.
- The lock-block count for each variant from the DTrace script.
- A short note on what the numbers tell you about your hardware (how contended the CPU-interconnect is, whether hyper-threading helps or hurts).

**Common mistakes:**

- Running the parallel script with only one thread. The point of the experiment is contention; a single-thread run eliminates it and hides the effect.
- Concluding from a small difference. If the three numbers differ by less than 10%, the measurement noise could explain the difference; rerun at least three times and check variance.
- Forgetting that `UMA_ALIGN_CACHE` in UMA zones does similar work for you. The `counter(9)` primitive is a clean solution; you rarely need to hand-align for counters.
- Expecting linear scaling from `v1-atomic`. Atomic counters *cannot* scale past about the memory system's coherence bandwidth; once that bandwidth is saturated, adding CPUs makes throughput worse, not better.
- Forgetting to unload between runs. Loading a new variant on top of a running one does not replace the running one; `kldunload perfdemo` then `kldload ./perfdemo.ko`.

**Troubleshooting:** If the throughput numbers are inconsistent between runs, CPU frequency scaling is probably the culprit. Pin the frequency with `sysctl dev.cpufreq.0.freq=$(sysctl -n dev.cpufreq.0.freq_levels | awk '{print $1}' | cut -d/ -f1)` (adjust for your CPU). On systems where this is not available, disable dynamic frequency scaling in the BIOS/firmware.

### Lab 5: Interrupt and Taskqueue Split

**Objective:** Compare in-interrupt-context work with taskqueue-deferred work, including behaviour under load.

**Directory:** `examples/part-07/ch33-performance/lab05-interrupt-tq/`.

**Prerequisites:** Lab 1 complete.

**Steps:**

1. Inspect the two variants in the lab directory:
   - `v1-in-callout`: a callout fires every millisecond and does its processing inline. The "interrupt handler" is the callout's function; all work happens in that context.
   - `v2-taskqueue`: the callout enqueues a task; a taskqueue thread dequeues and processes the work. The callout itself does nothing else.

2. Build each: `make` in each subdirectory.

3. Load the first variant: `cd v1-in-callout && sudo kldload ./perfdemo.ko`.

4. Run the measurement script: `./measure-latency.sh`. It spawns a userland reader that waits for the callout's processed data, then records the wall-clock time between the callout firing and the reader seeing the result. The script prints the median and P99 latency over 10,000 iterations.

5. Unload and load the second variant: `sudo kldunload perfdemo && cd ../v2-taskqueue && sudo kldload ./perfdemo.ko`.

6. Run the same measurement script.

7. Record median and P99 latency for each variant at idle.

8. Now repeat with artificial load on the system. In a second shell, start a CPU-intensive workload: `make -j$(sysctl -n hw.ncpu) buildworld` (if you have the world source checked out) or a simpler stressor: `for i in $(seq 1 $(sysctl -n hw.ncpu)); do yes > /dev/null & done`.

9. Rerun the measurement for each variant. The scheduler is now busy; the two variants should differ more sharply.

10. Stop the artificial load (`killall yes` or equivalent). Check that CPU usage returns to idle before declaring the test complete.

**What you should see:**

- `v1-in-callout` at idle has lower median latency but spikes when the callout's context is preempted (which happens at taskqueue boundary).
- `v2-taskqueue` at idle has a few microseconds of added latency (the callout-to-taskqueue dispatch) but smoother behaviour.
- Under load, `v1-in-callout` latency becomes highly variable; the callout context competes with the stressor for CPU.
- Under load, `v2-taskqueue` latency is higher but smoother; the taskqueue thread's scheduling class is more stable.

**Expected results (for comparison):**

On an 8-core Xeon:

- `v1-in-callout` at idle: median 8 us, P99 30 us.
- `v2-taskqueue` at idle: median 12 us, P99 40 us.
- `v1-in-callout` under load: median 15 us, P99 2000 us (spikes due to preemption).
- `v2-taskqueue` under load: median 20 us, P99 250 us (smoother).

The P99 improvement under load is the main reason production drivers prefer taskqueues for any significant work.

**What you should record:**

- Median and P99 latency at idle for each variant.
- Median and P99 latency under load for each variant.
- A short note on when you would use each in practice.

**Common mistakes:**

- Benchmarking only at idle. The two variants behave most differently under system load; test both states.
- Confusing callouts with real interrupts. A callout is a software timer; the lab uses it to stand in for an interrupt because real interrupt timing is hardware-specific. The conclusions transfer, but the absolute numbers depend on your scheduler.
- Forgetting to stop the stressor before declaring the test complete. Leaving `yes` running is rude to the next test.
- Drawing conclusions from a single measurement. Latency is distributional; a single run is a single point, not a distribution. Collect at least 10,000 measurements per configuration.

### Lab 6: Ship v2.3-optimized

**Objective:** Apply the full tuning work to produce a finished `perfdemo` v2.3-optimized, complete with sysctl tree, manual page, and benchmark report.

**Directory:** `examples/part-07/ch33-performance/lab06-v23-final/`.

**Prerequisites:** Labs 1 through 5 complete. You should have the baseline numbers from Lab 1 and familiarity with the per-CPU counter work from Lab 4.

**Steps:**

1. Inspect the starting state of the driver in the lab directory. This is the baseline `perfdemo` with no tuning applied; comments throughout the source mark where each tuning pass will go.

2. Apply the three changes we worked through in the chapter:

   **Change 1: `counter(9)` for all statistics.** Replace the atomic counters with `counter_u64_t`. Update the sysctl handlers to use procedural sysctls that call `counter_u64_fetch`. Mark each change with a short comment describing what was changed.

   **Change 2: Cache-line alignment on hot softc fields.** Identify the fields in the softc that are written frequently from multiple CPUs. For each, add `__aligned(CACHE_LINE_SIZE)` and appropriate padding. Note: if you use `counter(9)` for all counters, most of the cache-alignment work is already done; only non-counter hot fields (like pool pointers or state flags) need manual alignment.

   **Change 3: Pre-allocated buffers.** Create a UMA zone at `MOD_LOAD` time with `UMA_ALIGN_CACHE`. Use `uma_zalloc`/`uma_zfree` on the hot path instead of `malloc`/`free`. Destroy the zone at `MOD_UNLOAD`.

3. Build and load: `make && sudo kldload ./perfdemo.ko`. Run a quick smoke test with `./run-workload.sh 10000` to confirm the driver works.

4. Clean up any `PERF:` markers from the source. Grep for them: `grep -n 'PERF:' perfdemo.c`. Each line should either be removed or the marker replaced with a permanent comment if the measurement is staying.

5. Update the `MODULE_VERSION()` macro to `3`. Find the line and change:

    ```c
    MODULE_VERSION(perfdemo, 2);   /* before */
    MODULE_VERSION(perfdemo, 3);   /* after */
    ```

6. Update the driver's manual page. If one does not exist, copy `/usr/src/share/man/man4/null.4` as a template and adapt it. The page should have:

    - `.Nm perfdemo`
    - A one-line description in `.Nd`
    - A `.Sh DESCRIPTION` explaining what the driver does.
    - A `.Sh TUNABLES` section documenting the sysctl knobs.
    - A `.Sh SEE ALSO` referencing related pages.

    Lint the page: `mandoc -Tlint perfdemo.4`. Render it: `mandoc -Tascii perfdemo.4 | less`.

7. Run the final benchmark: `./final-benchmark.sh`. The script exercises the driver across several workloads (single-thread sequential, single-thread random, multi-thread parallel) and records median and P99 latency plus throughput for each. Copy the output to your logbook.

8. Write the performance report. Use the template from Section 8 as a starting point. Fill in:

    - The "Before" numbers from Lab 1.
    - The "After" numbers from step 7.
    - The three changes applied.
    - The machine details.
    - The tunables introduced (if any).

    Save the report as `PERFORMANCE.md` in the lab directory.

9. Unload the driver: `sudo kldunload perfdemo`. Confirm it unloaded cleanly with no hung threads or leaked memory (`vmstat -z | grep perfdemo` should show zero allocated, or the zone should not appear if it was destroyed).

**What you should produce:**

- `perfdemo.c` with the three tuning changes applied and no `PERF:` markers.
- `perfdemo.4` (manual page) with a TUNABLES section.
- `PERFORMANCE.md` (benchmark report).
- Clean build, clean load, clean unload.

**Expected results (for comparison):**

The v2.3 driver on an 8-core Xeon:

- Single-thread sequential reads: median 9 us, P99 60 us, throughput 400K ops/sec.
- Multi-thread (8-thread) parallel reads: median 11 us, P99 85 us, throughput 2.8M ops/sec.

Compare to the baseline (roughly 30 us median, 330K ops/sec, 600K under 8 threads). The v2.3 is 3x on single-thread and nearly 5x on parallel.

**Common mistakes:**

- Skipping the cleanup. A driver with `PERF:` markers is not a shipped driver.
- Reporting benchmark numbers without context. Every benchmark number needs the environment it was produced in.
- Updating the version number without the matching changelog entry. The version change signals behaviour change; the changelog documents what changed.
- Writing the manual page in vague prose. The `TUNABLES` section in particular should name exact value ranges, defaults, and trade-offs.
- Not running `mandoc -Tlint`. A broken manual page is as bad as no manual page.

### Wrapping Up the Labs

The labs work through one complete tuning cycle: baseline, DTrace, PMC, memory, interrupts, ship. By the time you finish Lab 6, you will have exercised every tool in the chapter at least once and have a concrete `perfdemo` artefact you can carry forward. If you have time, the challenge exercises below extend the driver in directions that are too specific for the main text but that teach the habits the chapter pointed at.

### A Lab Summary Table

For quick reference, the labs at a glance:

| Lab | What it teaches | Primary tool | Output |
|-----|-----------------|--------------|--------|
| 1   | Baseline measurement | `sysctl(8)` | Ops/sec number |
| 2   | Function-level profiling | `dtrace(1)` | Latency histogram, hot functions |
| 3   | Hardware-level profiling | `pmcstat(8)` | IPC, PMC-hot functions |
| 4   | Counter-contention tuning | `counter(9)` | Scaling comparison |
| 5   | Interrupt-context trade-offs | taskqueue | Latency under load |
| 6   | Ship discipline | `MODULE_VERSION`, manual page | A clean v2.3 driver |

Each lab is scoped to one hour if you are practised with the tools, two hours if you are learning them for the first time. The full set is a solid afternoon's work; the challenge exercises extend it into a weekend's work for readers who want deeper practice.

### Lab Dependencies

The labs are sequential. Lab 1 establishes the baseline numbers. Lab 2 uses those numbers as the before-and-after comparison. Lab 3 is optional (hwpmc may not be available), but its concepts flow into Lab 4. Lab 4 is where the main memory-tuning lesson is. Lab 5 is independent of the others but uses the same `perfdemo` driver. Lab 6 assumes you have absorbed Labs 1-5 and are ready to apply the full sequence.

If you are short on time, do Labs 1, 2, 4, and 6 in order. Those four are the core of the chapter.

## Challenge Exercises

These exercises go beyond the main labs. Each is optional and can be done in any order. They are designed to stretch the ideas from the chapter in ways that generalise to real driver work.

### Challenge 1: Latency Budget for a Real Driver

Pick a real FreeBSD driver that interests you. Good candidates include `/usr/src/sys/dev/e1000/` for the Intel e1000 family of Ethernet drivers (including `em` and `igb`), `/usr/src/sys/dev/nvme/` for the NVMe storage driver, `/usr/src/sys/dev/sound/pcm/` for the audio core, or `/usr/src/sys/dev/virtio/block/` for the virtio block device.

Read the driver's source and write a latency budget. For each phase of its hot path (interrupt filter, ithread, taskqueue handler, softclock callout, userland wakeup), estimate the maximum latency the driver can afford and still meet its workload's needs. A reasonable budget entry looks like:

```text
phase                  target    justification
---------------------  --------  --------------
PCIe read of status    < 500ns   register read on local bus
interrupt filter       < 2us     must not steal ithread time
ithread hand-off       < 10us    softclock expects prompt wake
taskqueue deferred     < 100us   reasonable for RX processing
wakeup to userland     < 50us    scheduler-dependent
```

Then audit the actual code: does the code on each phase justify your estimate? If the ithread handler takes a `MTX_DEF` sleep mutex that might block, is your budget honest?

The exercise is not about precision; the numbers above are illustrative. It is about looking at a real driver with performance eyes and reasoning about how its design fits a workload. Record your budget, your audit, and any discrepancies in your logbook. This habit, applied over many drivers, builds intuition faster than any other practice.

**Hint:** Look for explicit comments about timing in the source. Real drivers occasionally have comments of the form `/* this must complete within N us because ... */`. Those comments are a gift. Read them.

### Challenge 2: Compare Three Allocators

For a driver hot path that allocates fixed-size buffers (say, 64-byte buffers), benchmark three implementations:

1. `malloc(9)` with a per-call `malloc(M_TEMP, M_WAITOK)` followed by `free(M_TEMP)`.
2. A UMA zone created with `uma_zcreate("myzone", 64, ..., UMA_ALIGN_CACHE, 0)`, with `uma_zalloc()`/`uma_zfree()` on the hot path.
3. A pre-allocated array of 1024 buffers plus a freelist, allocated once at attach, reused on the hot path with a simple atomic pop/push.

Wire each into a clone of `perfdemo` and run the same read workload against all three. Measure:

- Throughput (reads/second).
- Median and P99 latency (from a counter-backed histogram).
- Cache miss rate (from `pmcstat -S LLC_MISSES` or the DTrace `profile` provider's `LLC_MISSES` alias if available).

On a modern amd64 machine, you should see a meaningful step from 1 to 2 (roughly 1.5x to 3x throughput, depending on contention), and a smaller but real step from 2 to 3 (perhaps 10% to 30%). The shape of the histogram is often more interesting than the average: `malloc(9)` has a long tail driven by VM pressure, UMA is tighter, the preallocated ring has the tightest tail.

Write a short note explaining the trade-offs. The preallocated ring is the fastest but the most rigid (the buffer count is fixed at attach). UMA is the right default for variable workloads. `malloc(9)` is fine for rare, large, or non-hot-path allocations. Do not conclude from this exercise that `malloc(9)` is bad; it is often the correct choice for control paths.

### Challenge 3: Write a Useful DTrace Script

Write a DTrace script that answers a question you actually have about a driver. Some examples:

- *How much time does my driver spend waiting on its own mutex?* Use `lockstat:::adaptive-spin` and `lockstat:::adaptive-block` filtered by the lock address.
- *What is the distribution of request sizes coming into my driver?* Capture `args[0]->uio_resid` from `fbt:myfs:myfs_read:entry` into a `quantize()` aggregation.
- *Which userland process is the heaviest consumer of my driver?* Aggregate `@[execname] = count()` on the driver's entry point.
- *What is the 99th percentile latency of the write path, broken out by request size?* Use a two-dimensional aggregation `@[bucket(size)] = quantize(latency)` where `bucket()` collapses sizes into a handful of ranges.
- *Is my driver ever called at interrupt context?* Use `self->in_intr = curthread->td_intr_nesting_level` and aggregate by that flag.

Save the script as a `.d` file in your lab directory. Annotate it with a header comment that names:

- The script's purpose (one sentence).
- The probes it uses.
- The invocation command, including any arguments.
- Sample output and how to interpret it.

A DTrace script that has the answer to a question a colleague asks is more valuable than almost anything else in an engineer's notebook. Keep a growing folder of such scripts over the years; they compound.

**Hint:** Before committing to a complex aggregation, run the probe with a `printf()` on a handful of events and confirm the variables you want to capture are actually available and contain the values you expect. Add the aggregation only once the raw probe is known to fire correctly.

### Challenge 4: Instrument a Read Path Without Contaminating It

Start with a `perfdemo`-style driver where the read path runs in a tight loop at high throughput. Add enough instrumentation to answer two questions:

1. What is the P50 and P99 latency of this read path?
2. What is the distribution of request sizes?

Measure the throughput before and after instrumentation. Aim for under 5% throughput loss from instrumentation. The exercise rewards thinking about the cost of each probe. Rough cost hierarchy on modern amd64, from cheapest to most expensive:

- Static SDT probe at compile time with DTrace disabled: effectively zero cost.
- A `counter(9)` increment on a per-CPU slot: a few cycles.
- An `atomic_add_int()` on a shared counter: tens to hundreds of cycles under contention.
- A `sbinuptime()` call: tens of cycles.
- A `log(9)` call: hundreds to thousands of cycles plus potential lock contention.
- A `printf()` call: thousands of cycles plus a possible sleep.

Design the instrumentation accordingly. A reasonable shape: per-CPU `counter(9)` for counts, `DPCPU_DEFINE` histogram buckets for latency distribution, no per-call `printf()`, no shared atomics. Measure the before-and-after throughput carefully and write up what you found.

**Hint:** Compile with `-O2` and inspect the generated assembly for the hot function with `objdump -d perfdemo.ko | sed -n '/<perfdemo_read>/,/^$/p'` to confirm the instrumentation did not defeat compiler optimisations, such as pushing variables to the stack that the non-instrumented version kept in registers.

### Challenge 5: Expose a Live Histogram

Extend the `perfdemo` sysctl tree with a latency histogram. Define a set of latency buckets (for example, 0 to 1us, 1 to 10us, 10 to 100us, 100us to 1ms, 1ms to 10ms, 10ms and above). For each read, measure the latency, find its bucket, and increment a per-CPU counter for that bucket. Expose the whole bucket array as a single sysctl that returns an opaque structure.

A reasonable handler:

```c
static int
perfdemo_sysctl_hist(SYSCTL_HANDLER_ARGS)
{
    uint64_t snapshot[PD_HIST_BUCKETS];
    for (int i = 0; i < PD_HIST_BUCKETS; i++)
        snapshot[i] = counter_u64_fetch(perfdemo_read_hist[i]);
    return (SYSCTL_OUT(req, snapshot, sizeof(snapshot)));
}
```

From userland, write a Python or shell script that polls the sysctl every second with `sysctl -b` and prints a live textual bar chart of the distribution. Compare the live distribution during a steady workload, during a bursty workload, and during an idle period.

The exercise demonstrates the full loop: in-kernel per-CPU counters, aggregation at read time, a binary sysctl, userland visualisation, and a continuous observation pipeline. This is the pattern real drivers use to ship latency telemetry.

**Hint:** `sysctl -b` reads the raw bytes; your userland code must unpack them using `struct.unpack` in Python or a small C program. Do not expose the buckets as a formatted string from the kernel; let the raw numbers flow out and let userland do the presentation.

### Challenge 6: Tune for Two Workloads

Pick two realistic workloads for the driver. For example, *small, latency-sensitive reads at 100 per second* (latency-bound) and *large, throughput-sensitive reads at 1000 per second* (throughput-bound). Identify the driver settings that optimise each workload. Candidates include:

- Buffer size (small for latency, large for throughput).
- Coalescing threshold, if any (none for latency, yes for throughput).
- Batch size for batched operations (small for latency, large for throughput).
- Polling versus interrupts (interrupts for latency, polling for throughput).
- Debug logging level (off for both; leave on only for troubleshooting).

Write a `loader.conf`-style table showing the best settings for each workload. Explain the trade-offs: why the latency setting would hurt throughput, why the throughput setting would hurt latency, and what the cost would be of picking a middle setting.

This challenge makes concrete the point from Section 1 that one driver can be fast in different ways for different workloads. Real driver tuning often means exposing these knobs with sensible defaults and documenting the workload each corresponds to.

**Hint:** Do not guess the best settings; measure them. Run each workload against each setting and fill in a table. The winning setting is often not the one you would have guessed, especially for throughput-versus-latency trade-offs where caches and scheduler interactions surprise experienced engineers.

### Challenge 7: Write the Manual Page

If `perfdemo` does not have a manual page, write one. Follow the convention of `/usr/src/share/man/man4/`: copy a simple existing page like `null(4)` or `mem(4)`, change the name, document the device, the sysctl knobs, the tunables, and any relevant errors.

A reasonable skeleton:

```text
.Dd March 1, 2026
.Dt PERFDEMO 4
.Os
.Sh NAME
.Nm perfdemo
.Nd performance demonstration pseudo-device
.Sh SYNOPSIS
To load the driver as a module at boot time, place the following line in
.Xr loader.conf 5 :
.Pp
.Dl perfdemo_load="YES"
.Sh DESCRIPTION
The
.Nm
driver is a pseudo-device used to illustrate performance measurement and
tuning techniques in FreeBSD device drivers.
...
.Sh SYSCTL VARIABLES
.Bl -tag -width "hw.perfdemo.bytes"
.It Va hw.perfdemo.reads
Total number of reads served since load.
...
.El
.Sh SEE ALSO
.Xr sysctl 8 ,
.Xr dtrace 1
.Sh HISTORY
The
.Nm
driver first appeared as a companion to
.Em FreeBSD Device Drivers: From First Steps to Kernel Mastery .
```

Verify the formatting with `mandoc -Tlint perfdemo.4`. Render it with `mandoc -Tascii perfdemo.4 | less`.

A manual page is a small but real commitment to the rest of the system. It turns a private tool into a shared one. It also forces you to name and describe every knob, which occasionally reveals knobs that do not need to exist.

**Hint:** The mdoc language is terse but strict. Read `mdoc(7)` once and use a recent simple man page as a template. Do not invent mdoc macros; use only the ones documented.

### Challenge 8: Profile an Unfamiliar Driver

Pick a FreeBSD driver you have not worked with. On a test system where the hardware is present (or in a VM with a compatible emulated device), run `pmcstat` or a DTrace profile during a realistic workload. Good VM-friendly targets:

- `virtio_blk` under a load generated by `dd if=/dev/vtbd0 of=/dev/null bs=1M count=1024`.
- `virtio_net` under a load generated by `iperf3`.
- `xhci(4)` if you have USB devices attached.
- `urtw(4)` or similar wireless drivers if you have the hardware.

Identify the top three functions by CPU time. For each, read the source and form a hypothesis about what the function is doing that takes the time. Record the hypothesis in your logbook. For example: *`virtio_blk_enqueue` spends most of its time in `bus_dmamap_load_ccb` because each request copies into a bounce buffer.*

You do not have to verify the hypothesis. The exercise is about the habit of looking at an unfamiliar driver with measurement tools and reasoning about the data they produce. The hypothesis, right or wrong, sharpens the next read of the source.

**Hint:** If the top function is in common kernel code (`copyin`, `bcopy`, `memset`, spinlocks), that is still useful information: it tells you where the driver spends its time, even if the root cause is outside the driver itself. Follow the callers back to the driver function and form the hypothesis there.

### Wrapping Up the Challenges

The challenges extend the chapter's ideas in directions the main text could not. If you have time to do one or two, they compound everything the chapter taught. If you have time for none of them, that is fine: the main labs give you the essential experience.

A useful selection strategy for limited time: pick one challenge from the *reading* family (Challenge 1 or 8), one from the *instrumentation* family (Challenge 3, 4, or 5), and one from the *shipping* family (Challenge 6 or 7). That mix exercises the three skills the chapter builds: reading existing drivers with performance eyes, instrumenting new drivers cleanly, and making finished drivers ready for others.

## Performance Patterns Reference

The chapter covered a large surface. This section consolidates the practical patterns into a compact reference you can scan when working on a real driver. Each pattern names the situation it applies to, the technique, and the FreeBSD primitive to reach for.

### Pattern: You Need a Hot-Path Counter

**Situation:** You want to count an event that happens thousands or millions of times per second, and your measurement must not dominate the cost of the event.

**Technique:** Use `counter(9)`. Declare a `counter_u64_t`, allocate it in attach with `counter_u64_alloc(M_WAITOK)`, increment on the hot path with `counter_u64_add(c, 1)`, sum on read with `counter_u64_fetch(c)`, free in detach with `counter_u64_free(c)`.

**Primitive path:** `/usr/src/sys/sys/counter.h`, `/usr/src/sys/kern/subr_counter.c`.

**Why this pattern:** `counter(9)` uses per-CPU slots updated without atomics. Read combines them. The increment is a handful of cycles with no cache-line contention.

**Anti-pattern:** An `atomic_add_64()` on a shared `uint64_t`. Each increment round-trips the cache line; at high rates, the counter becomes the bottleneck.

### Pattern: You Need a Hot-Path Allocation

**Situation:** You want to allocate a fixed-size buffer many times per second, and `malloc(9)` is showing up in profiles.

**Technique:** Create a UMA zone at attach with `uma_zcreate("mydrv_buf", sizeof(struct my_buf), NULL, NULL, NULL, NULL, UMA_ALIGN_CACHE, 0)`. Allocate on the hot path with `uma_zalloc(zone, M_NOWAIT)`. Free with `uma_zfree(zone, buf)`. Destroy in detach with `uma_zdestroy(zone)`.

**Primitive path:** `/usr/src/sys/vm/uma.h`, `/usr/src/sys/vm/uma_core.c`.

**Why this pattern:** UMA gives each CPU a cache of free buffers. Allocate and free on the same CPU is typically a pop and push from a per-CPU stack, with no cross-CPU coordination. `UMA_ALIGN_CACHE` prevents false sharing between adjacent buffers.

**Anti-pattern:** A per-request `malloc(M_DEVBUF, M_WAITOK)` in the hot path. The general-purpose allocator has more overhead and less locality than a dedicated zone.

### Pattern: You Need a Pre-Allocated Pool

**Situation:** You have a fixed known count of working buffers (for example, a ring descriptor count tied to hardware), and you want zero allocator cost on the hot path.

**Technique:** At attach, `malloc()` the array once. Keep the free list as a simple SLIST or stack. Pop and push from the free list under a single lock (or an atomic pointer if lock-free design is warranted). Free the whole array at detach.

**Why this pattern:** A pre-allocated pool has the lowest possible per-request cost and the most predictable latency. The rigidity (fixed count) is the trade.

**Anti-pattern:** Using `malloc()` or UMA when the count is known and fixed. The extra indirection costs cycles with no benefit.

### Pattern: You Need to Avoid False Sharing

**Situation:** Two frequently-updated variables live in the same cache line and are written by different CPUs. Each write invalidates the other CPU's copy, causing a ping-pong.

**Technique:** Put each hot variable on its own cache line with `__aligned(CACHE_LINE_SIZE)`. For per-CPU data, use `DPCPU_DEFINE` which automatically pads.

**Primitive path:** `/usr/src/sys/amd64/include/param.h` and its siblings under `/usr/src/sys/<arch>/include/param.h` for `CACHE_LINE_SIZE` (pulled in via `<sys/param.h>`), `/usr/src/sys/sys/pcpu.h` for DPCPU macros.

**Why this pattern:** Cache coherence protocols bounce whole cache lines; if two writers share a line, every write by one invalidates the other's cache. Separating the variables costs a small padding and eliminates the ping-pong.

**Anti-pattern:** Adding alignment without measuring. Many variables do not need alignment; adding it blindly wastes memory without improving performance.

### Pattern: You Need to Measure Latency Without Polluting It

**Situation:** You want to measure the latency of a hot path without the measurement dominating the cost.

**Technique:** For a sampled measurement, use DTrace's `profile` provider at 997 Hz. For every-call measurement, capture `sbinuptime()` at entry and exit, subtract, and bucket into a per-CPU `counter(9)` histogram. Read the histogram via sysctl.

**Why this pattern:** DTrace `profile` is near-free when disabled. `sbinuptime()` is fast (tens of cycles). A per-CPU histogram bucket is a `counter(9)` increment. The aggregate is read from userland without touching the hot path.

**Anti-pattern:** `nanotime()` per call (it is much slower than `sbinuptime()`), or a `printf()` per call (ruinous), or a shared atomic per call (cache-line bottleneck).

### Pattern: You Need to Expose Driver State

**Situation:** You want operators to be able to query and configure the driver at runtime.

**Technique:** Build a `sysctl(9)` tree under `hw.<drivername>`. Use `SYSCTL_U64` for simple counters, `SYSCTL_INT` for simple integers, and procedural sysctls (`SYSCTL_PROC`) for computed values or bulk data. Flag tunables with `CTLFLAG_TUN` so `loader.conf` can set them.

**Primitive path:** `/usr/src/sys/sys/sysctl.h`, `/usr/src/sys/kern/kern_sysctl.c`.

**Why this pattern:** sysctl is the canonical FreeBSD interface for driver observability and configuration. Operators know how to use `sysctl(8)`; log aggregators can scrape sysctl trees; monitoring scripts can poll.

**Anti-pattern:** Custom ioctls for every knob, or environment-variable reading at runtime, or `printf()` that expects the user to watch the log.

### Pattern: You Need to Split an Interrupt Handler

**Situation:** The interrupt handler does more than the small amount of work safe in interrupt context; it sometimes blocks or contends.

**Technique:** Register a filter handler (returns `FILTER_SCHEDULE_THREAD`) that does the minimum (acknowledge hardware, read status). Register an ithread handler that runs in thread context and does moderate work. For work that can wait or that takes real time, schedule on a taskqueue.

**Primitive path:** `/usr/src/sys/sys/bus.h`, `/usr/src/sys/kern/kern_intr.c`, `/usr/src/sys/kern/subr_taskqueue.c`.

**Why this pattern:** The filter runs in hard interrupt context with the strictest constraints; the ithread runs at a specific priority; the taskqueue runs at a configurable priority on a worker thread. Each layer is suited to its work.

**Anti-pattern:** Doing all the work in the filter, or blocking on locks in the ithread, or running heavy work at interrupt priority where it delays other drivers.

### Pattern: You Need to Rate-Limit a Log Message

**Situation:** An error condition can produce many log entries per second; you want one per N seconds or per M events to avoid flooding.

**Technique:** Use `ratecheck(9)`. Hold a `struct timeval` and an interval; call `ratecheck(&last, &interval)`. The call returns 1 when enough time has passed and updates the state; return 0 means *skip this message*.

**Primitive path:** `/usr/src/sys/sys/time.h` (declaration) and `/usr/src/sys/kern/kern_time.c` (implementation).

**Why this pattern:** `log(9)` is cheap per call but not free at thousands per second. Unbounded logging causes log-disk fill and can contend the log device. A rate-limiter gives visibility without flood.

**Anti-pattern:** Unconditional `printf()` in error paths, or a manual counter that drifts and leaks.

### Pattern: You Need a Stable Operator Interface

**Situation:** You want operators to rely on specific sysctl names or counter structures that will not change across versions.

**Technique:** Document the stable interface in the manual page. Version the driver with `MODULE_VERSION()` so operators can detect which interface they have. When you must change an interface, keep the old one (or a minimal shim) for at least one version and announce the deprecation in the release notes.

**Primitive path:** Manual page conventions in `/usr/src/share/man/man4/`.

**Why this pattern:** Operators write scripts against sysctl names. Breaking those names breaks their scripts. A stable published interface is a commitment you can keep.

**Anti-pattern:** Renaming a sysctl casually, or deleting a counter that scripts depend on, or reshaping a procedural sysctl's output without versioning.

### Pattern: You Need to Ship After Tuning

**Situation:** You have finished a tuning round and want to leave the driver in a clean, shippable state.

**Technique:** Remove temporary measurement code that was not meant to stay. Keep production-grade counters and SDT probes. Update `MODULE_VERSION()`. Run a full benchmark and record the numbers. Update the manual page. Write a short performance report that explains what was tuned and why. Commit with a message that references the report.

**Why this pattern:** The code change is only part of the work. The report, the numbers, and the documentation are what make the change durable. Without them, the next maintainer has no way to know what was done or why.

**Anti-pattern:** Committing tuning changes without benchmarks or documentation. The next maintainer cannot tell what changed, cannot reproduce the measurement, and may undo the work by accident.

### A Short Tool Reference Table

Below is a one-page summary of the tools introduced in this chapter, their typical use, and their source or documentation path.

| Tool | Use | Where to learn more |
|------|-----|---------------------|
| `sysctl(8)` | Read and write kernel state variables | `sysctl(8)`, `/usr/src/sbin/sysctl/` |
| `top(1)` | Process and system resource overview | `top(1)` |
| `systat(1)` | Interactive system statistics display | `systat(1)` |
| `vmstat(8)` | Virtual memory, interrupt, and zone statistics | `vmstat(8)` |
| `ktrace(1)` | Trace system calls for a process | `ktrace(1)`, `kdump(1)` |
| `dtrace(1)` | Dynamic tracing of userland and kernel | `dtrace(1)`, `dtrace(7)`, `fbt(7)`, `sdt(7)` |
| `pmcstat(8)` | Hardware performance counter reader | `pmcstat(8)`, `hwpmc(4)` |
| `flamegraph.pl` | Stack-sample visualisation | external, from Brendan Gregg |
| `gprof(1)` | Profile user programs | `gprof(1)`; not used for kernel |
| `netstat(1)` | Network statistics | `netstat(1)` |

A driver maintainer does not need all of these every day. But knowing which one answers which question saves hours of blind investigation.

### A Short Primitive Reference Table

The core in-kernel primitives used in this chapter, in one place.

| Primitive | Purpose | Header |
|-----------|---------|--------|
| `counter(9)` | Per-CPU hot counters | `sys/counter.h` |
| `DPCPU_DEFINE_STATIC` | Define per-CPU variable | `sys/pcpu.h` |
| `DPCPU_GET` / `DPCPU_PTR` | Access current CPU's slot | `sys/pcpu.h` |
| `DPCPU_SUM` | Sum across all CPUs | `sys/pcpu.h` |
| `uma_zcreate` | Create UMA zone | `vm/uma.h` |
| `uma_zalloc` / `uma_zfree` | Allocate from / free to zone | `vm/uma.h` |
| `UMA_ALIGN_CACHE` | Cache-align zone items | `vm/uma.h` |
| `bus_alloc_resource_any` | Allocate bus resource (irq, memory) | `sys/bus.h` |
| `bus_setup_intr` | Register interrupt handler | `sys/bus.h` |
| `bus_dma_tag_create` | Create DMA tag | `sys/bus_dma.h` |
| `taskqueue_enqueue` | Queue deferred work | `sys/taskqueue.h` |
| `callout(9)` | One-shot or periodic timer | `sys/callout.h` |
| `SYSCTL_U64` / `SYSCTL_INT` | Declare sysctl | `sys/sysctl.h` |
| `SYSCTL_PROC` | Declare procedural sysctl | `sys/sysctl.h` |
| `log(9)` | Log a message | `sys/syslog.h` |
| `ratecheck(9)` | Rate-limit a message | `sys/time.h` |
| `SDT_PROBE_DEFINE` | Declare static DTrace probe | `sys/sdt.h` |
| `CACHE_LINE_SIZE` | Per-architecture cache-line size | `sys/param.h` (via MD `<arch>/include/param.h`) |
| `sbinuptime()` | Fast monotonic time | `sys/time.h` |
| `getnanotime()` | Fast coarse wall time | `sys/time.h` |
| `nanotime()` | Precise wall time | `sys/time.h` |

The habit of knowing where to find these in a fresh source tree is worth as much as the knowledge of when to use them. Open the header, read the macro definitions, and see the usage sites with `grep -r SYSCTL_PROC /usr/src/sys/dev/ | head`.

### Wrapping Up the Patterns Reference

The patterns above are not commandments; they are well-supported defaults. A specific driver may have a reason to deviate from any of them. What matters is that the deviation is conscious, measured, and documented. *We do not use UMA here because...* is fine; *we do not use UMA here, I guess* is not.

When you work on an unfamiliar driver, you can use this table as a checklist: does the driver count its hot events cheaply, allocate from a suitable zone, avoid false sharing on shared variables, split its interrupt work appropriately, expose state through sysctl, rate-limit its logging, and ship with a versioned stable interface? Each negative answer is a candidate for improvement if performance is under investigation.

## Troubleshooting and Common Mistakes

Performance work has its own class of problems, distinct from functional bugs. A driver that measures wrong is as broken as a driver that crashes, but the symptoms are subtler. This section catalogs the most common failures, their symptoms, and their fixes.

### DTrace Reports Zero Samples

**Symptom:** A DTrace script compiles, runs, and on Ctrl-C reports an empty aggregation or zero counts.

**Most common causes:**

- The driver module is not loaded. `fbt:mymodule:` probes do not exist until the module is in the kernel.
- The function name is wrong. Check with `dtrace -l | grep myfunction`; DTrace lowercase the probe names in the listing, but the function name inside the kernel is the C identifier.
- The function was inlined by the compiler. An `fbt` probe only exists for a function the compiler emits as a callable symbol. Static functions may be inlined and disappear; check with `objdump -t module.ko | grep myfunction`.
- The probe is on a path that was not exercised. Run the workload that should hit the path, not just any workload.
- The predicate filters everything out. Remove the predicate and confirm the probe fires; then tighten the predicate.

**Debugging sequence:** Check `kldstat` (is the module loaded?), `dtrace -l -n probe_name` (does the probe exist?), `dtrace -n 'probe_name { printf("fired"); }'` (does it fire at all?), then add your real logic.

### DTrace Drops Events

**Symptom:** DTrace prints *dropped N events* or the aggregation counts are suspiciously low for a known-high-rate event.

**Most common causes:**

- The buffer is too small. Increase it: `dtrace -b 64m ...` for a 64 MB buffer.
- The aggregation is too chatty. Every `printf()` inside a probe is a potential drop; use aggregations for high-rate events, not per-event prints.
- The switch rate is too high. DTrace switches buffers between kernel and userland; at very high rates this can drop. Tune with `dtrace -x bufpolicy=fill` for fewer drops at the cost of missing the tail.

### pmcstat Reports *No Device* or *No PMCs*

**Symptom:** `pmcstat` prints errors like `pmc_init: no device`, `ENXIO`, or similar.

**Most common causes:**

- `hwpmc` is not loaded. Run `kldload hwpmc`.
- The underlying CPU does not expose PMCs. Many VMs and some cloud instances have PMCs disabled. Fall back to DTrace's `profile` provider.
- The event name is unsupported on this CPU. Run `pmcstat -L` to see the available events.

### pmcstat Sampling Misses Hot Functions

**Symptom:** A function you *know* is hot does not appear in `pmcstat -T` output.

**Most common causes:**

- The function is inline and has no callable symbol. The sampler attributes samples to the outer function.
- The sample rate is too low. Try `-S cycles -e 100000` for one sample per 100k cycles.
- The function is too fast for a single sample to catch it reliably. Cumulate samples over a long workload.
- The function is in a module whose symbols were stripped. Rebuild with debugging symbols.

### Counter Values Look Impossible

**Symptom:** A counter has a negative value, wraps around unexpectedly, or reports numbers inconsistent with the workload.

**Most common causes:**

- A counter was updated from multiple CPUs without atomics. Use `atomic_add_64()` or `counter(9)`.
- A counter was read mid-update. For `counter(9)`, this is not a problem because the fetch sums per-CPU values; for a plain atomic, a read is always consistent but an arithmetic combination of two reads might not be. Snapshot the counters in a consistent order.
- The counter overflowed. A 32-bit counter at a million events per second overflows in about an hour. Use `uint64_t`.
- The counter was not initialised. `counter_u64_alloc()` returns a zeroed counter, but a plain variable is only zeroed at load. A static global is fine; a local is not.

### sysctl Handler Crashes

**Symptom:** Reading or writing a sysctl causes a kernel panic.

**Most common causes:**

- The sysctl was registered against memory that has been freed (for example, a softc field after detach).
- The handler dereferences a NULL pointer. Check `req->newptr` before assuming a write.
- The handler runs without appropriate locks. Use `CTLFLAG_MPSAFE` and do the locking in the handler, or omit `CTLFLAG_MPSAFE` and accept the giant lock.
- The handler calls a function that cannot be called from sysctl context (for example, one that sleeps while holding a spin lock).

### The Driver Slows Down Under Measurement

**Symptom:** The driver's throughput drops when DTrace or `pmcstat` is running.

**Most common causes:**

- The probes are in a hot path. Move them to less-frequent paths or reduce the probe count.
- The DTrace script is too chatty. Use aggregations, not per-event prints.
- The PMC sample rate is too high. Reduce it.
- The probe predicate is expensive. Simple predicates (`arg0 == 0`) are cheap; complex ones with string compares are not.

### Taskqueue Thread Starvation

**Symptom:** Taskqueue work accumulates without being processed; the driver's user-visible response degrades.

**Most common causes:**

- The taskqueue's thread was pinned to a CPU that is now busy with something else. Use `taskqueue_start_threads_cpuset()` for predictable placement or leave the thread unpinned.
- The taskqueue is shared (`taskqueue_fast`) and other subsystems are keeping it busy. Use a dedicated taskqueue.
- A previous task is blocked waiting on something that never completes. Kill the task; add a timeout to the wait.
- The thread was killed by a MOD_UNLOAD but the tasks were not drained. Use `taskqueue_drain(9)` before releasing resources.

### Interrupt Storm Detected

**Symptom:** `dmesg` shows *interrupt storm detected* and the interrupt is throttled.

**Most common causes:**

- The driver does not acknowledge the interrupt in the handler, so the hardware keeps asserting it.
- The driver acknowledges the wrong register.
- The hardware genuinely fires at a rate the driver cannot serve. Enable coalescing if supported.
- A shared-interrupt siblings is misbehaving, and the filter handler claims the interrupt falsely by returning `FILTER_HANDLED` when it should return `FILTER_STRAY`.

### Cache-Line Alignment Has No Effect

**Symptom:** You added `__aligned(CACHE_LINE_SIZE)` to a counter and saw no change in throughput.

**Most common causes:**

- The counter is not actually contended. Single-threaded or low-concurrency workloads do not benefit from alignment.
- The counter was already aligned by the compiler's default layout. Confirm with `offsetof()`.
- The neighbouring field is no longer hot. The hot field changed, the old alignment is still there, and no contention exists.
- The CPU does not bounce cache lines hard enough for the difference to be measurable. Some architectures have cheaper cache coherence than others.

Alignment is not free (it pads the struct and wastes cache); if it has no effect, remove it. Measurement tells you.

### UMA Zone Leaks

**Symptom:** `vmstat -z` shows a UMA zone growing without bound, and the system eventually runs out of kernel memory.

**Most common causes:**

- The driver allocates with `uma_zalloc()` but does not free with `uma_zfree()` in the corresponding path. Match the allocation and free paths.
- A pointer was overwritten before being freed. Inspect with KASAN or MEMGUARD (Chapter 34 material).
- A failed operation's cleanup path is wrong. Every early return from a function that allocates must free what was allocated.

### The Benchmark Is Noisy

**Symptom:** Repeated runs of the same benchmark produce significantly different numbers.

**Most common causes:**

- System activity interferes. Run benchmarks on an otherwise idle machine.
- CPU frequency scaling is changing the clock rate between runs. Pin frequency with `sysctl dev.cpufreq.0.freq_levels` (check the options) or set performance mode.
- Cache warm-up differs between runs. Discard the first run's numbers or add a warm-up pass.
- The kernel's lazy data structures (UMA caches, buffer caches) differ between runs. Drop caches between runs or accept the variance.
- Hyper-threading pairs are contending. Disable SMT for clean numbers.

A benchmark with high variance is not necessarily wrong, but its conclusions need to survive the variance. If the tuning change improves the median by 5% but the run-to-run variance is 20%, you cannot conclude anything from a single run; run many and look at the distribution.

### You Measured the Wrong Thing

**Symptom:** You tuned the driver to meet a goal, but the user-visible performance did not change.

**Most common causes:**

- The metric you optimised is not the one that matters. Tuning median latency while the P99 is the user-visible problem is a classic miss.
- The workload you benchmarked is not the workload the user runs. Optimising for sequential reads when the user does random reads is wasted effort.
- The driver was not the bottleneck. Sometimes the bottleneck is in userland, in the scheduler, or in a different subsystem. `pmcstat -s cycles` on the whole system tells you where the time really goes.

If tuning did not help, that is not failure; it is information. The metric or workload was wrong, and now you know. Restate the goal and measure again.

### Numbers Get Worse After a Tuning Change

**Symptom:** You applied what looked like a beneficial change (added a per-CPU cache, shortened a critical section, moved work to a taskqueue), and the measured numbers are worse.

**Most common causes:**

- The change introduced a regression that is not obvious from the code. For example, moving work to a taskqueue added a context switch on every request, and at your rate the switch cost dominates.
- The change shifted the bottleneck. You removed lock contention, and now the allocator is the new bottleneck.
- The change broke a previously-invisible correctness property. A softer race now produces extra work (retries, error paths) and the driver appears slower because it is doing more.
- The measurement is noisy and the run that looked worse is within the variance band. Run it ten times and see.

**Debugging sequence:** Revert the change and confirm the baseline reproduces. Apply the change and measure again. If both reproduce, the change is the cause; if the baseline varies, the measurement is unstable. Check the diff for unrelated changes that crept in. Profile both variants and compare: the profile will show where time actually goes.

### Throughput Climbs but P99 Latency Gets Worse

**Symptom:** A change improved the average throughput but made the tail latency worse. Users complain, even though your benchmark's primary metric improved.

**Most common causes:**

- The change traded tail latency for throughput. For example, larger batching improves throughput but increases the time an unlucky request waits for its batch to complete.
- Queue depth grew. Longer queues give more opportunities for backlog, which extends the P99 under load.
- A slower path was taken more often. For example, the hot path got faster but the error path still pays the old cost, and the benchmark now exercises the error path more.

**Debugging sequence:** Plot throughput versus offered load; plot P99 latency versus offered load. The knee of the latency curve is where the driver enters overload. If the knee moved to the left (lower load produces high latency), the change made tail latency worse even if peak throughput improved. Tune back toward the latency target.

### Results Differ Between Hardware Configurations

**Symptom:** A change that improves performance on one machine makes no difference or is worse on another.

**Most common causes:**

- CPU microarchitecture differs. Cache-coherence protocols, branch predictors, and memory subsystems vary between generations; optimisations tuned for one generation may be irrelevant or harmful for another.
- Core count differs. Per-CPU caches scale with CPU count; on a small-CPU system, the overhead may exceed the benefit.
- NUMA topology differs. An optimisation that ignores NUMA may be fine on a single-socket system and harmful on a multi-socket one.
- The memory subsystem differs. DRAM speed, memory channels, and cache sizes vary widely.

**Debugging sequence:** Record the full hardware configuration with each measurement (CPU model, core count, memory configuration). Measure on at least two distinct machines. Accept that some optimisations are machine-specific; document them if so.

### KASSERT Fires Only Under Load

**Symptom:** A `KASSERT()` that has been silent during functional testing fires when the driver is under heavy load.

**Most common causes:**

- A race that is rare under low concurrency becomes frequent under high concurrency. The assertion catches a state that only occurs when two CPUs reach the critical section simultaneously.
- An ordering violation that depends on exact timing. Slower paths hid it; faster paths expose it.
- A resource exhaustion that only appears at high rates. For example, `M_NOWAIT` returning NULL because the allocator briefly runs out of memory.
- A driver-visible effect of kernel lazy evaluation (UMA cache rebalancing, callout backlog processing).

**Debugging sequence:** Capture the panic's stack trace and the values of local variables. Add `mtx_assert(&sc->mtx, MA_OWNED)` at key points to verify lock ownership. Add temporary SDT probes around the suspect region and trace the order of events with DTrace. Sometimes the fix is a tighter lock; sometimes it is a memory barrier; sometimes it is a redesign.

### Measurement Disagrees With User Perception

**Symptom:** Your benchmark says the driver is fast; users say the driver feels slow.

**Most common causes:**

- The benchmark measures an aggregate; users perceive individual worst-case events. A driver with 10us median latency and a 1s P99.99 feels awful even though the benchmark score is excellent.
- The benchmark uses synthetic load; the real workload has dependencies (file access patterns, syscall chains) that the benchmark does not reproduce.
- Users perceive end-to-end latency, not driver latency. If the driver is 5% of the end-to-end time, even a large improvement produces no user-visible change.
- The user complaint is correct and the benchmark is measuring the wrong thing.

**Debugging sequence:** Ask users exactly what they do. Reproduce it. Measure the actual experience with a stopwatch or a user-space profiler if necessary. Add per-operation histograms and look at the tail percentiles. A P99.99 is not a statistical artifact; it is someone's bad day.

### DTrace Script Hangs or Takes a Long Time to Exit

**Symptom:** After Ctrl-C, DTrace takes seconds or minutes to finish printing aggregations; sometimes it appears to hang.

**Most common causes:**

- The aggregation is very large (millions of keys). Printing takes time proportional to the aggregation size.
- The aggregation contains large strings. String allocation in DTrace is not free.
- The kernel buffer is full of queued events waiting to be delivered to userland. DTrace drains the buffer before exiting.
- A probe is still firing rapidly and DTrace is trying to keep up.

**Debugging sequence:** Cap the aggregation with `trunc(@, N)` or `printa()` with a limited list. Use `dtrace -x aggsize=M` to bound the aggregation size. Use `dtrace -x switchrate=1hz` to reduce switch overhead during the run. If you need to kill a runaway DTrace, `kill -9` is safe; DTrace releases probes cleanly.

### Sysctl Reports Different Numbers on Successive Reads

**Symptom:** Reading the same sysctl twice in quick succession returns different values, even when no workload is running.

**Most common causes:**

- The counter is still being updated by ongoing operations. This is expected and usually fine.
- A per-CPU counter is being snapshotted non-atomically; different CPUs are read at different instants.
- The handler uses a non-stable data structure (a linked list under traversal) without holding the appropriate lock.
- A bug in the handler reads beyond the allocated buffer, returning stale stack data.

**Debugging sequence:** Stop the workload completely. Read the sysctl ten times. If the numbers still vary, the handler is buggy (most likely missing a lock or a barrier). If the numbers are stable when idle, the variance is from background activity; document the counter as *approximate* if appropriate.

### A Driver Runs Fast Alone, Slow in Production

**Symptom:** Benchmarks in isolation show the driver meeting its goals; production performance is much worse.

**Most common causes:**

- Shared resources in production are contested. The filesystem buffer cache, the scheduler, the network stack, or other drivers compete for the same CPUs and memory.
- Production workload profile differs from the benchmark. The benchmark uses a uniform pattern; production uses a mix that exercises code paths the benchmark never touches.
- Production memory pressure invalidates caches. In benchmarks, your UMA zone stays warm; in production, memory pressure flushes it.
- Security features (IBRS, retpolines, kernel-page-table isolation) have different costs in production than in the benchmark's environment.

**Debugging sequence:** Instrument the driver so it is observable in production. Start DTrace or a lightweight profiler in production briefly and compare the distributions with the benchmark's. If the gap is reproducible, narrow down which subsystem differs; the answer is often memory pressure or scheduler differences rather than the driver itself.

### Wrapping Up the Troubleshooting

Every item in this section is a real failure mode that experienced performance engineers have made. The value is in reading the list once, recognising the patterns when they appear, and spending the measurement time to diagnose the specific failure rather than guessing. *Why is my driver slow* has thousands of possible answers; the troubleshooting patterns narrow the search space in a few specific steps.

A habit worth cultivating: when you hit a new failure mode not on this list, write it down. Capture the symptom, the cause, and the debugging sequence. Over a few years, your own list becomes more useful than any book's, because it reflects the drivers and hardware you actually work with.

## Wrapping Up

Chapter 33 took us from *does the driver work?* to *does it work well?*. The two questions sound similar and differ in the shape of the answer. The first is a yes/no of correctness; the second is a numerical distribution of throughput, latency, responsiveness, and CPU cost, measured under a specific workload in a specific environment.

We began in Section 1 with the four axes of driver performance and the discipline of stating measurable goals. A goal with a metric, a target, a workload, and an environment is what separates a performance project from a performance hope. The reminder that optimisation must preserve correctness, debuggability, and maintainability is the constraint that keeps performance work honest.

Section 2 introduced the kernel's measurement primitives: the `get*` and non-`get` time functions, the three grades of counter (plain atomic, `counter(9)`, DPCPU), the sysctl machinery for exposing state, and the FreeBSD tools (`sysctl`, `top`, `systat`, `vmstat`, `ktrace`, `dtrace`, `pmcstat`) that read it. The central insight: measurement has cost, and the cost of measurement must not contaminate the measurement itself.

Section 3 introduced DTrace. Its pervasive probes, near-zero disabled cost, kernel-side aggregation, and expressive scripting language make it the tool of choice for most driver observation. The `fbt` provider covers every function; the `sdt` provider gives you stable named probes in your own driver; the `profile` provider samples at a fixed rate; the `lockstat` provider specialises in lock contention.

Section 4 introduced hardware performance counters. `hwpmc(4)` exposes the CPU's built-in counters; `pmcstat(8)` drives them from userland. Counting mode gives totals; sampling mode gives distributions. The callgraph collector plus flame-graph renderer turns the samples into a visualisation that makes hotspots obvious. The data is raw and interpretation takes practice, but the combination answers questions no other tool can.

Section 5 turned to memory. Cache-line alignment avoids false sharing; `bus_dma(9)` handles DMA alignment; pre-allocation keeps the hot path out of the allocator; UMA zones with `UMA_ALIGN_CACHE` specialise for fixed-size frequent allocations; per-CPU counters with `counter(9)` and DPCPU eliminate cache-line contention on shared metrics. Each technique has a cost, and each deserves evidence before it is applied.

Section 6 turned to interrupts and taskqueues. Measuring interrupt rate and handler latency is easy with `vmstat -i` and DTrace. The filter-plus-ithread split keeps the interrupt context small; taskqueues move work to a calmer environment; dedicated threads offer isolation for special cases; coalescing and debouncing control the interrupt rate itself. The judgement of when to apply each technique comes from measurement, not from reading.

Section 7 turned inward, to the runtime metrics that belong in the shipped driver. A thoughtful sysctl tree exposes totals, rates, states, and configuration. Procedural sysctls fetch `counter(9)` sums. Rate-limited logging via `log(9)` and `ratecheck(9)` keeps informational messages from flooding the log. Debug modes give operators a safe way to raise verbosity without rebuilding. Each element is a small commitment; the cumulative result is a driver that is auditable, diagnosable, and maintainable.

Section 8 closed the loop. Tuning produces scaffolding; shipping requires cleanup. Remove temporary measurement code. Keep the counters, SDT probes, and configuration knobs that have lasting value. Benchmark the final driver, document the tuning knobs in the manual page, update `MODULE_VERSION()`, and write a plain-text performance report. These steps are the difference between a one-off improvement and a lasting one.

The labs walked through six concrete stages: baseline counters, DTrace scripts, pmcstat sampling, cache-aligned and per-CPU tuning, the interrupt-vs-taskqueue split, and the v2.3-optimized finalisation. Each lab produced a measurable output; the numbers are what the chapter is ultimately about.

The challenge exercises extended the work in directions too specific for the main text: latency budgets for real drivers, allocator comparisons, useful DTrace scripts of your own, an observability-budget study, a live histogram, workload-specific tuning, manual-page writing, and profiling an unfamiliar driver.

The troubleshooting section catalogued the common failure modes and their diagnoses. Each one is a recognisable pattern; spending the measurement time to name which pattern you are in is the half of the work that most beginners skip.

### What You Should Be Able to Do Now

If you worked through the labs and read the explanations carefully, you can now:

- Set measurable performance goals for a driver before you start tuning.
- Choose the right kernel time function for a measurement based on the precision you need.
- Count events cheaply on the hot path with `counter(9)` or `atomic_add_64()`.
- Expose driver metrics to userland through a sysctl tree.
- Write DTrace one-liners and scripts that answer specific questions about a driver.
- Measure lock contention with the `lockstat` provider.
- Sample kernel CPU time with `pmcstat` or with DTrace's `profile` provider.
- Read a flame graph.
- Apply cache-line alignment, pre-allocation, UMA zones, and per-CPU counters selectively, with evidence.
- Split a driver's interrupt work between filter, ithread, and taskqueue based on measured latency.
- Ship a tuned driver: remove scaffolding, document tunables, update the version, write a report.

### What This Chapter Leaves Implicit

There are two topics the chapter touched on but did not deep-dive. Each has its own chapter or its own literature, and trying to cover it fully here would have bogged down the main thread.

First, **the deep internals of `hwpmc(4)`**. The chapter showed you how to use it; the source tree under `/usr/src/sys/dev/hwpmc/` shows you how it works, across Intel, AMD, ARM, RISC-V, and PowerPC backends. If you want to understand how the kernel samples the PC register on an overflow interrupt, or how the userland `pmcstat` talks to the kernel, reading that tree pays off. Most readers never need to.

Second, **advanced DTrace**. The scripts we wrote are straightforward. DTrace's speculations, its USDT interfaces, its complex aggregations, and its advanced predicates are worth a book of their own. Brendan Gregg's *DTrace: Dynamic Tracing in Oracle Solaris, Mac OS X, and FreeBSD* is the canonical reference. For driver work, the patterns we covered carry you a long way.

### Key Takeaways

Distilled into a page, the chapter's core lessons are:

1. **Measure before you change.** An optimisation without a before-and-after number is a guess.
2. **State the goal in numbers.** Metric, target, workload, environment. Without these, you cannot know when you are done.
3. **Prefer the simplest tool that answers the question.** A DTrace one-liner usually beats a full `pmcstat` session.
4. **The tools that measure also perturb.** Know the cost of each probe before you use it on a hot path.
5. **Four axes, not one.** Throughput, latency, responsiveness, CPU cost. Optimising one can harm another.
6. **`counter(9)` for hot counters.** Per-CPU update, summed on read. Scales from one CPU to many.
7. **`UMA_ALIGN_CACHE` for hot allocations.** Cache-aligned per-CPU caches, cheap alloc and free.
8. **Pre-allocate, do not allocate in the hot path.** Attach-time cost is fine; per-call cost is not.
9. **Keep the interrupt handler small.** Filter-plus-ithread-plus-taskqueue is the standard split; use it.
10. **Ship with a sysctl tree.** A driver with no observability is a driver nobody can diagnose.
11. **Rate-limit your logs.** A driver that logs every error can DoS itself.
12. **Write the report.** The tuning work is not done until the numbers are written down where the next maintainer can find them.

### Before You Move On

Before you treat this chapter as completed and move to Chapter 34, take a moment to verify the following. These are the checks that separate a learner who has seen the material from a learner who owns it.

- You can write, from memory, the skeleton of a procedural sysctl that returns a `counter(9)` sum.
- You can write a DTrace one-liner that histograms a function's latency.
- You can explain, in one paragraph, the difference between `getnanotime()` and `nanotime()`.
- You can name three measurements you would capture before changing a driver's hot path, and at least one tool for each.
- Given a driver that appears to be CPU-bound, you have a plan: `vmstat -i` to check for interrupt storms, `dtrace -n 'profile-997 { @[stack()] = count(); }'` to find the hot function, `pmcstat -S cycles -T` to confirm, and `lockstat` if the profile points at locks.
- Given a driver whose throughput does not scale with CPU count, you have a plan: check for a single atomic counter being contended, check for a shared taskqueue, check for a global lock.
- You know which sections of your manual page would name the tunables, and roughly how to write them.

If any of these still feel shaky, loop back to the relevant section and re-read it. The chapter's material compounds; Chapter 34 will assume you have internalised most of what is here.

### A Note on Sustained Practice

Performance work, more than almost any other area of kernel engineering, rewards steady practice. The first few `pmcstat` sessions are confusing; the tenth is natural. The first DTrace script you write is laborious; the hundredth is a one-liner typed without thinking. The first performance report reads like a mystery; the tenth reads like a decision document.

If you have driver work in your day job or on a personal project, spend ten minutes of your week on a performance measurement. Not a tuning pass, just a measurement. Record the numbers. Compare them to last week. Over a year this compounds into the habits this chapter is trying to teach. The tools are all there; the habit is what takes time.

### Connecting Back to the Rest of the Book

Chapter 33 rests on material from earlier parts. A short tour of what you now see in a new light:

From Part 1 and Part 2, the module lifecycle and `DRIVER_MODULE()` registration. The driver you tuned in this chapter is the same shape of driver you built in those parts; tuning does not change the skeleton.

From Part 3, the Newbus framework. `simplebus`, the bus-resource APIs, `bus_alloc_resource_any()`: these are the building blocks your driver uses to find its hardware. Knowing them lets you reason about where time goes during attach and detach.

From Part 4, the driver-to-userspace interfaces. Your `read()` and `write()` paths are the ones you tuned in this chapter. The sysctl tree you built in Section 7 is a natural extension of the ioctl and sysctl material from Chapter 15.

From Part 5 and Part 6, the testing and debugging chapters. Those tools apply to tuned drivers in full. A driver that performs well but crashes under load is not a tuned driver; it is a broken one.

From Part 7, Chapter 32 on Device Tree and embedded development is the platform half. Performance on an embedded board has the same shape as performance on a server, but the constraints are tighter: less RAM, less CPU, less power. The techniques scale down in priority (cache-line alignment matters less when you have one core), but the measurement discipline matters as much or more.

The picture after Chapter 33 is that you have the full measurement-and-tuning toolkit for FreeBSD drivers. You can build drivers. You can test them. You can port them. You can measure them. You can tune them. You can ship them. The remaining chapters of Part 7 refine and finalise those skills, and the next one is about the debugging techniques that the performance work sometimes reveals are still needed.

### Looking Ahead

Chapter 34 turns from performance to **advanced debugging**. When tuning reveals a deeper bug, when a driver panics under specific load, when memory corruption shows up in a crash dump, the tools in Chapter 34 are the ones you reach for. `KASSERT(9)` for in-code assertions, `panic(9)` for unrecoverable states, `ddb(4)` for interactive live kernel debugging, `kgdb` for remote and post-mortem debugging, `ktr(9)` for lightweight ring-buffer tracing, and `memguard(9)` for memory-corruption detection. The boundary between performance work and debugging is often fuzzy: a performance problem turns out to be a correctness bug in disguise, or a correctness bug hides behind a performance symptom. Chapter 34 gives you the tools for the other half of the diagnosis.

Beyond Chapter 34, the final chapters of Part 7 complete the mastery arc: asynchronous I/O and event handling, coding style and readability, contributing to FreeBSD, and a capstone project that brings several earlier threads together. Each builds on what you have done here.

The measurement work is done. The next chapter is about what to do when the numbers point at something the tuning tools alone cannot reach.

### Glossary of Terms Introduced in This Chapter

A quick reference for the vocabulary the chapter introduced or used heavily. When a term has a dedicated manual page or a specific FreeBSD primitive, the reference is noted.

**Aggregation (DTrace):** A server-side running summary (count, sum, min, max, average, quantize) computed in kernel space and printed at session end. Use aggregations, not per-event prints, for any probe firing more than a few times per second.

**Benchmark harness:** A repeatable script that runs a workload against the driver with controlled parameters, collects measurements, and reports results in a consistent format. The harness reduces measurement-to-measurement variance by removing human variation.

**Bounce buffer:** A kernel buffer that sits between a device and user memory when direct DMA is not possible because of alignment or address-range constraints. Managed by `bus_dma(9)`. Bounce buffers cost an extra copy; eliminating them is a common tuning target.

**Cache coherence:** The hardware mechanism that keeps caches on multiple CPUs consistent. When one CPU writes a line, other CPUs that hold the line must invalidate or update their copies. The cost of coherence traffic is why false sharing is a problem.

**Cache-line alignment:** Placing a variable on a boundary that matches the hardware cache-line size (typically 64 bytes on amd64). Used to prevent false sharing between a hot variable and its neighbours. `__aligned(CACHE_LINE_SIZE)`.

**Callgraph (profiling):** A tree of caller-callee relationships weighted by sample count. Reading a callgraph tells you which call paths consume the most time, not just which leaf functions.

**Coalescing:** Combining many small events into fewer large ones. Hardware interrupt coalescing reduces interrupt rate at the cost of per-event latency.

**Counter(9):** A FreeBSD primitive for per-CPU summed counters. Cheap on update, cheap on aggregate read, scales with CPU count.

**DPCPU:** FreeBSD per-CPU variable storage with alignment and access macros. `DPCPU_DEFINE_STATIC`, `DPCPU_GET`, `DPCPU_PTR`, `DPCPU_SUM`.

**DTrace:** FreeBSD's dynamic tracing framework. Safe, pervasive, near-zero cost when disabled. Scripts compose probes, predicates, and actions.

**Event mode (PMC):** Hardware performance counter mode where the counter counts a specific event (cycles, instructions, cache misses). Compare with sampling mode.

**False sharing:** Cache-coherence ping-pong between CPUs caused by unrelated variables living in the same cache line. The effect is invisible in source code and obvious in profile results.

**`fbt` provider:** DTrace's *function boundary tracing* provider. Gives you probes on entry and return of every kernel function that is not inlined.

**Filter handler:** An interrupt handler that runs in the low-level interrupt context. Does the minimum and either claims the interrupt and signals the ithread (`FILTER_SCHEDULE_THREAD`), or declines (`FILTER_STRAY`).

**Flame graph:** A visualisation of profile samples as nested rectangles. Width represents sample count; height represents stack depth. Makes hotspot identification visual.

**hwpmc(4):** The FreeBSD kernel module that exposes hardware performance counters. Loaded with `kldload hwpmc`.

**Ithread:** Interrupt thread. A kernel thread that runs an interrupt handler in thread context, not interrupt context. Scheduled at a driver-specified priority.

**Latency:** The time from a request's arrival to its completion. Typically reported as percentiles (P50, P95, P99, P99.9) rather than an average.

**Lock contention:** The state where multiple threads compete for the same lock, causing some to wait. Detectable with the DTrace `lockstat` provider.

**MSI / MSI-X:** Message Signalled Interrupt (PCIe). Replaces legacy INTx interrupts with in-band messages. MSI-X allows many independent vectors per device, which enables per-queue or per-CPU interrupt handling.

**NAPI-style batching:** A Linux-borrowed but FreeBSD-applicable technique where an interrupt disables further interrupts from the device and a poll loop drains the queue until empty, then re-enables interrupts. Amortises interrupt overhead under load.

**Observability:** The property of a system that makes its internal state visible to external tools. In FreeBSD, sysctl trees, counters, SDT probes, and log messages contribute to observability.

**P99 / P99.9:** The 99th and 99.9th percentiles of a distribution. The value below which 99% (or 99.9%) of samples fall. Tail percentiles often reveal worse user experience than averages.

**Per-CPU data:** Data structures that have a distinct copy on each CPU, eliminating cache-line contention. FreeBSD's `DPCPU` macros manage layout and access.

**pmcstat(8):** The userland tool that drives `hwpmc(4)`. Supports event mode and sampling mode, system-wide and per-process.

**`profile` provider (DTrace):** A DTrace provider that fires at a fixed frequency (for example, 997 Hz) on all CPUs. Used for portable statistical profiling.

**Responsiveness:** The time from an external stimulus (interrupt, ioctl) to the driver's first action. Distinct from latency (request-to-completion) and from throughput.

**Sampling mode (PMC):** Hardware performance counter mode where the counter triggers an interrupt every N events and the kernel records the program counter. Used for statistical profiling.

**`sched` provider (DTrace):** A DTrace provider for scheduler events: `on-cpu`, `off-cpu`, `enqueue`, `dequeue`. Used to diagnose scheduling-related latency.

**SDT probe:** Statically-defined trace probe. Compiled into the driver with `SDT_PROBE_DEFINE` and `SDT_PROBE*()` macros. Always present, always stable, near-zero cost when disabled.

**softc:** A driver's *software context* structure. Holds per-device state. Accessed with `device_get_softc(dev)`.

**sysctl tree:** A hierarchy of kernel variables exposed to userland through `sysctl(8)`. Drivers register nodes under `hw.<drivername>` by convention.

**Tail latency:** The high percentiles of a latency distribution (P99, P99.9, P99.99). Often what users perceive as *slow*, even when averages are good.

**Taskqueue:** A FreeBSD kernel thread pool for deferred work. Drivers enqueue tasks with `taskqueue_enqueue(9)`; the thread pool dequeues and runs them.

**TMA (Top-Down Microarchitecture Analysis):** A method for classifying CPU-bound performance problems. Branches into *frontend-bound*, *bad-speculation*, *backend-bound*, and *retiring*.

**Throughput:** The rate at which the driver completes work, measured as operations per second or bytes per second.

**UMA zone:** A specialised allocator for fixed-size objects. Created with `uma_zcreate()`. Cheap per-CPU cached allocation; suitable for hot paths with fixed-size buffers.

**workload:** The specific pattern of requests used during measurement. A driver's performance depends on the workload; a measurement without a named workload is incomplete.

### A Parting Word

Performance engineering has a reputation for being arcane. The reputation is half earned and half unearned. It is earned because the tools are many, the output is numeric, and the interpretation takes experience. It is unearned because the discipline is small and learnable: measure before you change, state the goal in numbers, prefer the simplest tool, know the cost of your probes, preserve correctness. That is the whole discipline in a sentence.

The chapter's hardest lesson is the one about humility. A beginner's instinct is to look at a line of code and think *I can make this faster*. An experienced engineer's instinct is to look at a measurement and think *this is the hot function; now let me understand why*. The gap between the two mindsets is the gap between years of practice. Every hour spent with DTrace, `pmcstat`, and a notebook full of real measurements narrows it.

You have now met the measurement half of the FreeBSD performance toolkit. The tools are in the tree; the manual pages are waiting; the habit of using them is the last piece. Make it part of every serious driver you write, and the rest follows.

One last encouragement before we move on. If the depth of the chapter felt intimidating, return to it one section at a time. Performance work is not a single skill to master in one sitting; it is a cluster of related skills, each small in isolation. Run one DTrace one-liner. Expose one sysctl counter. Run one `pmcstat` session. Each small practice earns a small confidence, and the confidences compound. Within a handful of drivers, the toolkit feels routine rather than exotic, and the mindset of *measure, then decide* becomes second nature rather than a checklist.

On to Chapter 34 and the debugging techniques that complement everything we have just covered.

Take a deep breath; you have earned the next chapter.

