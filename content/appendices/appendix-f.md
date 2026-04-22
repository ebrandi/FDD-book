---
title: "Benchmark Harness and Results"
description: "A reproducible benchmark harness, with working source code and representative measurements, for the performance claims made in Chapters 15, 28, 33, and 34."
appendix: "F"
lastUpdated: "2026-04-21"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 35
---

# Appendix F: Benchmark Harness and Results

## How to Use This Appendix

Several chapters in this book make performance claims. Chapter 15 gives order-of-magnitude timings for `mtx_lock`, `sx_slock`, condition variables, and semaphores. Chapter 28 notes that driver code typically shrinks by thirty to fifty percent when a network driver is converted to `iflib(9)`. Chapter 33 discusses the cost hierarchy of timecounter sources, with TSC at one end, HPET at the other, and ACPI-fast in the middle. Chapter 34 describes the run-time cost of debug kernels with `INVARIANTS` or `WITNESS` enabled, and of kernels with active DTrace scripts attached. Every one of those claims is qualified in the text with a phrase such as "on typical FreeBSD 14.3-amd64 hardware", "in our lab environment", or "order-of-magnitude". The qualifiers exist because the absolute numbers depend on the specific machine, the specific workload, and the specific compiler that built the kernel.

This appendix exists so that those qualifiers stand on reproducible ground. For each class of claim, the companion tree under `examples/appendices/appendix-f-benchmarks/` contains a working harness that the reader can build, run, and extend. Where a harness is portable and does not require hardware access, this appendix also reports the measurement it produces on a known machine, so that readers have a concrete number to compare against. Where a harness requires a specific kernel configuration that cannot be assumed to exist on any given reader's machine, only the harness itself is provided, together with clear instructions for reproducing it.

The aim is not to replace the Chapter 15 or Chapter 34 claims with authoritative figures. It is to let readers see how the claims were arrived at, to let them verify the claims on their own hardware, and to make the results honest about what varies with environment and what does not.

### How this appendix is organised

The appendix has five benchmark sections, each with the same internal structure.

- **What is being measured.** One paragraph describing the claim from the chapter and the quantity the harness measures.
- **The harness.** The filesystem location of the companion files, the programming language used, and a short description of the approach.
- **How to reproduce.** The exact command or command sequence the reader runs.
- **Representative result.** A measured value, or "harness only, no result captured" when the harness has not been run by the author.
- **Hardware envelope.** The range of machines over which the result is expected to generalise, and the range over which it is known not to.

The five sections in order are timecounter read cost, synchronization primitive latency, iflib driver code-size reduction, and DTrace-with-INVARIANTS-and-WITNESS overhead. A final section on scheduler wakeup latency points at the existing Chapter 33 script rather than introducing a new one, since the script there is already the harness.

## Hardware and Software Setup

Before presenting the benchmarks, a word on the envelope. The numbers in this appendix and the representative results quoted inside it come from two kinds of measurement.

The first kind is **portable measurement**: anything that counts lines of source code, reads the output of a deterministic tool, or otherwise depends only on the FreeBSD source tree and a working compiler. Those measurements produce the same result on any host that has the same source checkout. The iflib code-size comparison in Section 4 is the only portable measurement in the appendix, and its result can be reproduced exactly on any machine with `/usr/src` synchronised to a FreeBSD 14.3-RELEASE source tag.

The second kind is **hardware-dependent measurement**: anything that times a kernel path, a syscall, or a hardware register read. Those measurements depend on the CPU, the memory hierarchy, and the kernel configuration. For every hardware-dependent benchmark in this appendix, the harness is provided, the reproduction steps are precise, and a representative result is quoted only when the author has actually run the harness on a known machine. Where the author has not, the appendix says so explicitly and leaves the results table blank for the reader to fill in.

The fairer phrasing is that this appendix is a **runnable field guide**. The chapters quote qualified figures; the appendix shows you how to measure the same quantities on the hardware in front of you, and what to expect if your hardware belongs to the same broad family as the hardware the chapters had in mind ("modern amd64", "Intel and AMD generations currently in service").

### Caveats that apply to every section

A few caveats apply throughout, and it is simpler to name them once than to repeat them in each section.

All the hardware-dependent harnesses measure averages over large loops. Averages hide tail behaviour. A P99 latency can be an order of magnitude higher than the mean on the same path, especially for anything that involves a scheduler wakeup. Any serious performance claim in production will need a distributional measurement, not a single number; this appendix is about the mean because that is what the chapter claims refer to.

Readers running these harnesses under virtualisation should expect significantly noisier results than on bare metal. A virtualised TSC, for example, may be synthesised by the hypervisor in a way that adds hundreds of nanoseconds to every read. The Chapter 33 cost hierarchy still holds qualitatively under virtualisation, but the absolute numbers will move.

Finally, none of these harnesses are intended for use in production kernels. The sync-primitive kmod in particular spawns kernel threads that do tight no-op loops; it is safe to load for a few seconds on a development machine and unload, but it should not be loaded on a busy server.

## Timecounter Read Cost

### What is being measured

Chapter 33 describes a three-way cost hierarchy for timecounter sources in FreeBSD: TSC is cheap to read, ACPI-fast is moderately expensive, and HPET is expensive. The claim is qualified with "on the Intel and AMD generations currently in service", and separately with "on typical FreeBSD 14.3-amd64 hardware". The harness in this section measures the average cost of one `clock_gettime(CLOCK_MONOTONIC)` call, which the kernel resolves through whichever source `kern.timecounter.hardware` currently selects, and separately the cost of a bare `rdtsc` instruction as a floor.

The quantity measured is nanoseconds per call, averaged over ten million iterations. The underlying kernel path is `sbinuptime()` in `/usr/src/sys/kern/kern_tc.c`, which reads the current timecounter's `tc_get_timecount` method, scales it, and returns the result as an `sbintime_t`.

### The harness

The harness lives under `examples/appendices/appendix-f-benchmarks/timecounter/` and is composed of three pieces.

`tc_bench.c` is a small userland program that calls `clock_gettime(CLOCK_MONOTONIC)` in a tight loop and reports the average nanoseconds per call. It reads `kern.timecounter.hardware` at startup and prints the current source name, so that each run is self-documenting.

`rdtsc_bench.c` is a companion userland program that reads the `rdtsc` instruction directly, using the inline assembly pattern that the kernel uses in its own `rdtsc()` wrapper in `/usr/src/sys/amd64/include/cpufunc.h`. Its output is the cost of the instruction itself, without any kernel overhead.

`run_tc_bench.sh` is a root-only shell wrapper that reads `kern.timecounter.choice` (the list of available sources for the current kernel), iterates over each entry, sets `kern.timecounter.hardware` to that source, runs `tc_bench`, and restores the original setting on exit. The result is a table with one row per timecounter source, ready to compare.

### How to reproduce

Build the two userland programs:

```console
$ cd examples/appendices/appendix-f-benchmarks/timecounter
$ make
```

Run the rotation (requires root to flip the sysctl):

```console
# sh run_tc_bench.sh
```

Or just the direct TSC floor:

```console
$ ./rdtsc_bench
```

### Representative result

Harness only, no result captured. The harness has been compiled and its logic reviewed, but the author has not run it on the reference machine while writing this appendix. A reader running the harness on typical FreeBSD 14.3-amd64 hardware should expect the TSC column to report values in the low tens of nanoseconds, the ACPI-fast column to report values several times higher, and the HPET column (if available and not disabled in firmware) to report values an order of magnitude higher still. The absolute numbers will vary with CPU generation, power state, and whether `clock_gettime` is served by the fast-gettime path or falls through to the full syscall.

### Hardware envelope

The ordering of the three timecounter sources by cost has been stable across amd64 generations since invariant TSC became standard in the mid-2000s. Readers on different CPU vendors or different microarchitectural generations will see different absolute numbers but the same ordering. On ARM64, there is no HPET or ACPI-fast in the usual sense, and the relevant comparison is between the Generic Timer counter register and the software paths that wrap it; the harness will still run, but only one entry will appear in the table. If `kern.timecounter.choice` on the reader's machine shows a single source, that is itself a useful data point: the system's firmware has restricted the choice, and no rotation is possible.

See also Chapter 33 for the surrounding context, particularly the section on `sbinuptime()` and the discussion of why driver code should avoid reading `rdtsc()` directly.

## Synchronization Primitive Latencies

### What is being measured

Chapter 15 presents a table of approximate per-operation costs for FreeBSD's synchronization primitives: atomics at a nanosecond or two, uncontended `mtx_lock` at tens of nanoseconds, uncontended `sx_slock` and `sx_xlock` slightly higher, and `cv_wait`/`sema_wait` at microseconds because they always involve a full scheduler wakeup. The numbers in the table are described as "order-of-magnitude estimates on typical FreeBSD 14.3 amd64 hardware", with the caveat that they "can move by a factor of two or more across CPU generations". The harness in this section measures each row of that table directly.

The quantities measured are:

- Nanoseconds per `mtx_lock` / `mtx_unlock` pair on an uncontended mutex.
- Nanoseconds per `sx_slock` / `sx_sunlock` pair on an uncontended sx.
- Nanoseconds per `sx_xlock` / `sx_xunlock` pair on an uncontended sx.
- Nanoseconds per one-shot `cv_signal` / `cv_wait` round-trip between two kernel threads.
- Nanoseconds per one-shot `sema_post` / `sema_wait` round-trip between two kernel threads.

### The harness

The harness lives under `examples/appendices/appendix-f-benchmarks/sync/` and is a single loadable kernel module, `sync_bench.ko`. The module exposes five write-only sysctls under `debug.sync_bench.` (one per benchmark) and five read-only sysctls that report the most recent result for each benchmark.

Each benchmark times a fixed number of iterations using `sbinuptime()` in `/usr/src/sys/kern/kern_tc.c` for the timestamps. The mutex, sx_slock, and sx_xlock benchmarks run entirely in the calling thread and exercise only the uncontended fast path. The cv and sema benchmarks spawn a worker kproc that signs a ping/pong protocol with the main thread; every iteration therefore includes one wakeup and one context switch in each direction, which is precisely what Chapter 15's "wakeup latency" column measures.

The module follows the kmod pattern used in other chapters of this book, declared with `DECLARE_MODULE` and initialised through `SI_SUB_KLD / SI_ORDER_ANY`. Sources are `/usr/src/sys/kern/kern_mutex.c` for `mtx_lock`, `/usr/src/sys/kern/kern_sx.c` for `sx_slock` / `sx_xlock`, `/usr/src/sys/kern/kern_condvar.c` for condition variables, and `/usr/src/sys/kern/kern_sema.c` for counting semaphores.

### How to reproduce

Build the module:

```console
$ cd examples/appendices/appendix-f-benchmarks/sync
$ make
```

Load and drive it:

```console
# kldload ./sync_bench.ko
# sh run_sync_bench.sh
# kldunload sync_bench
```

`run_sync_bench.sh` runs each benchmark in sequence and prints a small table; individual benchmarks can also be triggered directly by writing a `1` to the corresponding `debug.sync_bench.run_*` sysctl and then reading `debug.sync_bench.last_ns_*`.

### Representative result

Harness only, no result captured. The kmod has been written against FreeBSD 14.3 headers and its logic reviewed against the Chapter 15 table, but the author has not loaded and run it on the reference machine while writing this appendix. A reader running the harness on typical FreeBSD 14.3-amd64 hardware should expect the uncontended mutex and sx numbers to land in the low tens of nanoseconds, and the cv and sema round-trip numbers to land in the low microseconds because those paths cross the scheduler twice. Any reader whose numbers are more than a factor of two higher should look at scheduler affinity, CPU frequency scaling, and whether the host is under additional load.

### Hardware envelope

The Chapter 15 table's "order-of-magnitude" qualifier is deliberate. Uncontended lock costs track the cost of one or two atomic compare-and-swap operations on the current cache line, which varies with CPU generation and cache topology. Round-trip wakeup costs track scheduler latency, which varies more: a dedicated-CPU server with `kern.sched.preempt_thresh` tuned for low latency can show sub-microsecond round-trips, while a busy multi-tenant machine can see tens of microseconds. The harness reports a single mean per benchmark; readers who need a distribution should extend the module to capture quantiles, or use DTrace `lockstat` probes on the running kernel instead.

See also Chapter 15 for the conceptual framework and the guidance on when each primitive is appropriate.

## iflib Driver Code-Size Reduction

### What is being measured

Chapter 28 claims that on the drivers that have been converted to `iflib(9)` to date, driver code typically shrinks by thirty to fifty percent compared with the equivalent plain-ifnet implementation. Unlike the other benchmarks in this appendix, this one is not a hardware measurement. It is a source-code measurement: how many lines of C source a modern NIC driver needs under `iflib(9)` versus how many it needed without it.

The quantity measured is the line count of a driver's main source file, split into three figures: the raw `wc -l` total, the number of non-blank lines, and the number of non-blank non-comment lines (an approximation of "code lines" that is close enough to the truth for order-of-magnitude comparison).

### The harness

The harness lives under `examples/appendices/appendix-f-benchmarks/iflib/` and is a set of portable shell scripts.

`count_driver_lines.sh` takes one driver source file and reports all three figures. The comment stripping is a simple `awk` pass that understands `/* ... */` (including multi-line) and `// ... EOL` forms; it is not a full C parser but is accurate enough to be useful.

`compare_iflib_corpus.sh` is the main driver. It walks two curated corpora and produces a comparison table:

- iflib corpus: `/usr/src/sys/dev/e1000/if_em.c`, `/usr/src/sys/dev/ixgbe/if_ix.c`, `/usr/src/sys/dev/igc/if_igc.c`, `/usr/src/sys/dev/vmware/vmxnet3/if_vmx.c`.
- Plain-ifnet corpus: `/usr/src/sys/dev/re/if_re.c`, `/usr/src/sys/dev/bge/if_bge.c`, `/usr/src/sys/dev/fxp/if_fxp.c`.

The iflib drivers were selected by grepping for `IFDI_` method callbacks, which are the characteristic iflib interface points; the plain-ifnet drivers were selected to span a comparable range of hardware classes. Both lists are variables at the top of the script that the reader can edit.

`git_conversion_delta.sh` is a third script for readers who have a full FreeBSD Git clone with history. It finds the commit that converted a named driver to iflib (by searching commits that touch the file and mention "iflib" in the log) and reports the line-count delta at that commit. A before/after conversion diff is the only way to measure Chapter 28's claim directly; the cross-driver comparison is a proxy that depends on driver complexity being roughly comparable, which is a strong assumption.

### How to reproduce

On any FreeBSD 14.3 source checkout:

```console
$ cd examples/appendices/appendix-f-benchmarks/iflib
$ sh compare_iflib_corpus.sh /usr/src
```

For the before/after measurement, on a full FreeBSD Git clone:

```console
$ sh git_conversion_delta.sh /path/to/freebsd-src.git if_em.c
```

### Representative result

Captured against the FreeBSD 14.3-RELEASE source tree,  `compare_iflib_corpus.sh` produces the following summary:

```text
=== iflib ===
  if_em.c  raw=5694  nonblank=5044  code=4232
  if_ix.c  raw=5168  nonblank=4519  code=3573
  if_igc.c raw=3305  nonblank=2835  code=2305
  if_vmx.c raw=2544  nonblank=2145  code=1832
  corpus=iflib drivers=4 avg_code=2985

=== plain-ifnet ===
  if_re.c  raw=4151  nonblank=3693  code=3037
  if_bge.c raw=6839  nonblank=6055  code=4990
  if_fxp.c raw=3245  nonblank=2943  code=2228
  corpus=plain-ifnet drivers=3 avg_code=3418

=== summary ===
  iflib avg code lines:       2985
  plain-ifnet avg code lines: 3418
  delta:                      433
  reduction:                  12%
```

A cross-corpus reduction of roughly twelve percent is smaller than Chapter 28's thirty-to-fifty-percent claim, which is exactly what the caveat at the bottom of the script warns about. The chapter claim is a per-driver before-and-after figure: the same hardware, converted to iflib, goes from N lines to (0.5-0.7) times N lines. A cross-driver comparison is a different thing: it compares different hardware with different feature sets and different quirk counts. The cross-driver reduction sets a floor (there is *some* reduction in average size across the corpus), and the per-driver reduction quoted in Chapter 28 sets the ceiling (the individual conversion commits show the larger number). Readers with a Git clone can use `git_conversion_delta.sh` to verify the per-driver number directly.

### Pinned per-driver measurement

To anchor Chapter 28's range to a concrete figure, we ran the per-commit comparison on 2026-04-21 against the ixgbe conversion commit `4fd3548cada3` ("ixgbe(4): Convert driver to use iflib", authored 2017-12-20 by erj@FreeBSD.org). That commit is the cleanest per-driver conversion in the tree: it neither consolidated drivers nor changed features on the same revision. The four driver-specific files that existed on both sides of the commit (`if_ix.c`, `if_ixv.c`, `ix_txrx.c`, and `ixgbe.h`) went from 10,606 raw lines (7,093 non-blank non-comment) down to 7,600 raw lines (5,074 non-blank non-comment) on the conversion commit itself, a twenty-eight percent reduction in code lines. Restricting the comparison to the core PF files (`if_ix.c` and `ix_txrx.c`) tightens the result to thirty-two percent, inside Chapter 28's thirty-to-fifty-percent range. The twenty-eight-percent figure is the headline one to carry away, with the narrower thirty-two-percent figure showing how much of the residual is shared-header and VF-sibling code rather than framework-saved driver logic.

A larger but less representative data point is the earlier em/e1000 conversion commit `efab05d61248` ("Migrate e1000 to the IFLIB framework", 2017-01-10), which achieves roughly a seventy percent reduction on the e1000-class driver code: the combined driver sources drop from 13,188 down to 3,920 non-blank non-comment lines. That commit folded `if_em.c`, `if_igb.c`, and `if_lem.c` into a single iflib-based `if_em.c` plus a new `em_txrx.c`, so the measured reduction mixes framework savings with the consolidation of three related drivers into one and should not be read as a typical per-driver figure. Taken together, the ixgbe and e1000 data points bracket Chapter 28's thirty-to-fifty-percent range from below and above: a clean single-driver conversion lands at or just below the lower edge, and a driver-consolidating conversion overshoots the upper edge.

### Hardware envelope

This benchmark does not depend on hardware. The result is a function of the FreeBSD source tree at a specific revision, and should be identical on any machine with the same checkout. Readers using a different FreeBSD branch (15-CURRENT, an older release) will see different absolute numbers because the tree evolves.

See also Chapter 28 for the surrounding discussion of `ifnet(9)` and how `iflib(9)` sits inside it.

## DTrace, INVARIANTS, and WITNESS Overhead

### What is being measured

Chapter 34 makes two performance claims about debug kernels:

- A busy `INVARIANTS` kernel runs roughly five to twenty percent slower than a release kernel, sometimes more on allocation-heavy workloads, as a rough order-of-magnitude figure on typical FreeBSD 14.3-amd64 hardware.
- `WITNESS` adds bookkeeping to every lock acquisition and release; in our lab environment, on a busy kernel running a lock-heavy workload, the overhead can approach twenty percent.

It also mentions, in the context of DTrace, that active DTrace scripts add overhead proportional to how many probes they fire and how much work each probe does.

The quantity measured by the harness in this section is wall-clock time to complete a fixed workload. The harness does not attempt to compute percentages directly; it provides two well-defined workloads and a consistent output format, and it expects the reader to run the suite once per kernel condition and compute ratios between them.

### The harness

The harness lives under `examples/appendices/appendix-f-benchmarks/dtrace/` and has four parts.

`workload_syscalls.c` is a userland program that performs one million iterations of a tight loop containing four cheap system calls (`getpid`, `getuid`, `gettimeofday`, `clock_gettime`). This workload exaggerates the cost of the syscall entry and exit path, where `INVARIANTS` assertions and `WITNESS` lock-tracking fire most often.

`workload_locks.c` is a userland program that spawns four threads and performs ten million `pthread_mutex_lock` / `pthread_mutex_unlock` pairs per thread, on a small rotating set of mutexes. Userland mutexes fall through to the kernel's `umtx(2)` path when they contend, so this workload exercises the lock-heavy contention path that `WITNESS` instruments.

`dtrace_overhead.d` is a minimal DTrace script that fires on every syscall entry and return without printing anything per probe. Attaching it during a `workload_syscalls` run measures the cost of having the DTrace probe framework actively instrumenting the syscall path.

`run_overhead_suite.sh` runs both workloads once, captures `uname` and `sysctl kern.conftxt`, and writes a tagged report. The reader is expected to run the suite four times: on a base `GENERIC` kernel, on an `INVARIANTS` kernel, on a `WITNESS` kernel, and on any of those with `dtrace_overhead.d` attached in another terminal. Comparing the four reports gives the percentages Chapter 34 claims.

The surrounding kernel sources are `/usr/src/sys/kern/subr_witness.c` for WITNESS, `/usr/src/sys/sys/proc.h` and its siblings for the assertion macros enabled by `INVARIANTS`, and the DTrace provider sources under `/usr/src/sys/cddl/dev/` for the probe framework.

### How to reproduce

On each kernel condition:

1. Boot into the kernel under test.
2. Build the workloads:

   ```console
   $ cd examples/appendices/appendix-f-benchmarks/dtrace
   $ make
   ```

3. Run the suite:

   ```console
   # sh run_overhead_suite.sh > result-<label>.txt
   ```

4. For the DTrace condition, launch the script in another terminal before running the suite:

   ```console
   # dtrace -q -s dtrace_overhead.d
   ```

After all four runs, compare the `result-*.txt` files side by side. The ratios `INVARIANTS_ns / base_ns`, `WITNESS_ns / base_ns`, and `dtrace_ns / base_ns` are the overhead figures Chapter 34 refers to.

### Representative result

Harness only, no result captured. The workloads have been compiled on the reference system and their logic reviewed against the Chapter 34 claims, but the author has not built the three comparison kernels to capture end-to-end numbers while writing this appendix. Readers who run the four-kernel comparison on typical FreeBSD 14.3-amd64 hardware should expect `INVARIANTS` to land in the five-to-twenty-percent range on `workload_syscalls`, somewhat higher on `workload_locks` because lock paths are the hottest code under `INVARIANTS`. `WITNESS` should land lower than `INVARIANTS` on pure-syscall workloads (where few new lock orders are visited) and higher on `workload_locks`, approaching the twenty-percent figure the chapter names. The DTrace column should be small on `workload_locks` (no relevant probes fire in the hot path) and non-trivial on `workload_syscalls` (every iteration fires two probes).

### Hardware envelope

The ratios are more stable across hardware than the absolute numbers. Readers on different CPUs will see different wall times for the baseline workload, but the per-kernel percentage overhead should stay within a tight band because it is determined by how much extra work the debug kernel does per operation, not by how fast each operation is. The most common sources of surprise are virtualisation (where the syscall path has additional per-call hypervisor overhead that dilutes the percentage), aggressive power management (where CPU frequency varies across runs), and SMT (where per-core numbers differ from per-logical-CPU numbers). The harness does not try to control for any of these; a reader wanting production-quality numbers will need to pin the test to a single CPU, disable frequency scaling, and run the suite several times.

See also Chapter 34 for the surrounding treatment of debugging kernels and for the discussion of when each of `INVARIANTS`, `WITNESS`, and DTrace is worth enabling.

## Scheduler Wakeup Latency

Chapter 33 contains a DTrace snippet that measures scheduler wakeup latency using `sched:::wakeup` and `sched:::on-cpu`. That snippet is already the harness; this section would only duplicate it. Readers interested in the sub-microsecond idle-system figure and the low-tens-of-microseconds contended-system figure that Chapter 33 names should use the script exactly as printed there, on the reader's hardware. The DTrace provider sources are under `/usr/src/sys/cddl/dev/dtrace/`.

If the reader wants to compare wakeup latency with and without `WITNESS`, the same script applies; just boot the two kernels and run it on each.

## Wrapping Up: Using the Harness Without Being Misled

The harness in this appendix exists because every number in Chapters 15, 28, 33, and 34 is an order-of-magnitude claim, and order-of-magnitude claims deserve reproducibility. A few habits make the harness actually useful rather than a source of false confidence.

First, run each benchmark more than once. Single-run results are dominated by startup noise, warm-up effects, and interference from whatever else is running on the machine. The harness scripts report a single number; run them three to five times and take the median, or extend them to aggregate runs automatically before trusting the result.

Second, keep the conditions honest. If you want to compare `INVARIANTS` to a base kernel, build both kernels with the same compiler, the same `CFLAGS`, and the same `GENERIC` or `GENERIC-NODEBUG` baseline. If you want to compare two timecounter sources, run both comparisons on the same boot of the same kernel; a reboot between runs changes too many variables.

Third, resist the temptation to treat the numbers as universal. A measurement on one 4-core laptop in a quiet office is not a measurement on a 64-core production server in a noisy rack. The harness measures the machine in front of you; Chapter 15 through 34's qualified phrases ("on typical 14.3-amd64 hardware", "in our lab environment") are honest about the same limitation. The harness makes the qualified phrase testable without pretending it becomes universal.

Fourth, prefer ratios to absolutes. A claim that `WITNESS` costs twenty percent is much more stable than a claim that `WITNESS` costs four hundred nanoseconds per lock. The former survives CPU changes, compiler changes, and kernel-version changes that would wipe out the latter. When you run the harness on your hardware, the percentage overhead is what you should keep; the absolute number is just the arithmetic that produced it.

And finally, extend the harness rather than trusting it blindly. Every script here is small enough to read end to end; every kmod is under three hundred lines. If a chapter claim matters to you, open the harness, verify that it measures what you expect it to measure, modify it if your workload needs something different, and run the modified version. The harness is a starting point, not a finish line.

The qualified phrases in Chapters 15, 28, 33, and 34 and the runnable harness in this appendix are two halves of the same discipline. The chapters are honest about what is variable; the appendix shows you how to measure the variation yourself.
