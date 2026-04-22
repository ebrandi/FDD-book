---
title: "Advanced Debugging Techniques"
description: "Sophisticated debugging methods for complex driver issues"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 34
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 135
---

# Advanced Debugging Techniques

## Introduction

In the previous chapter we learned how to measure what a driver does and how
fast it does it. We watched performance counters grow, we ran DTrace
aggregations to spot hot paths, and we used `pmcstat` to see which instructions
were actually consuming cycles. Measurement gave us a language for asking
whether the driver behaves the way we expect.

Debugging asks a different question. Instead of "how fast is this?" it asks
"why is this wrong?" A performance problem usually produces slow but running
code. A correctness problem can produce a crash, a deadlock, silent data
corruption, a driver that refuses to unload, a pointer that dereferences into
garbage, or a lock that is somehow held by nobody. These are the bugs that
make a seasoned kernel engineer take a slow breath and reach for better tools.

FreeBSD gives us those tools. They range from very small and very fast
assertions that live inside the kernel and catch the bug the instant it
happens, to full post-mortem analysis of a crash dump on a machine that is no
longer running. There are lightweight tracing rings that cost almost nothing
at runtime, heavyweight tracers that can unwind the entire call graph, and
memory allocators that can be swapped in during development to turn subtle
use-after-free bugs into immediate, diagnosable crashes. A well-equipped
driver author learns to reach for the right tool for the right bug, rather
than staring at `printf` output hoping for enlightenment.

The goal of this chapter is to teach you that toolkit. We will begin by
understanding when advanced debugging is the right response and when a simpler
approach will serve better. We will then work through the in-kernel assertion
macros, the panic path, and how to read and analyze a crash dump offline with
`kgdb`. We will build a debug-friendly kernel so those tools are actually
available when we need them, learn how to trace driver behavior with DTrace
and `ktrace`, and finally study how to hunt memory leaks and invalid accesses
with `memguard(9)`, `redzone`, and guard pages. We will close with the
discipline of debugging on production systems, where every action has
consequences, and with a short study of how to refactor a driver after a hard
failure so that it is more resilient to the next one.

Throughout the chapter we will use a small companion driver called `bugdemo`.
It is a pseudo-device with deliberate, controlled bugs that we can trigger
through simple `ioctl(2)` calls and then hunt with each of the techniques the
chapter teaches. Nothing we do touches real hardware, so the lab environment
stays safe even when we deliberately crash the kernel.

By the end of the chapter you will be able to add defensive assertions to a
driver, build a debug kernel, capture a crash dump, open it in `kgdb`, trace
live behavior with DTrace and `ktrace`, trap memory misuse with `memguard(9)`
and friends, and apply all of this discipline safely on systems where other
people are relying on the machine.

## Reader Guidance: How to Use This Chapter

This chapter sits in Part 7 of the book, alongside performance tuning,
asynchronous I/O, and other mastery topics. It assumes you have already
written at least a simple character device driver, understand the load and
unload lifecycle, and have worked with `sysctl`, `counter(9)`, and DTrace at
the level introduced in Chapter 33. If any of that feels uncertain, a quick
revisit of Chapters 8 through 14 and Chapter 33 will pay for itself several
times over in this chapter.

### Reading This Chapter Alongside Chapter 23

This chapter deliberately picks up where Chapter 23 left off. Chapter
23, "Debugging and Tracing," introduced the fundamentals: how to think
about bugs, how to reach for `printf`, how to use `dmesg` and the
kernel log, how to read a simple panic, how to turn on DTrace probes,
and how to make a driver easier to observe from the start. It stays
close to the everyday debugging habits a new driver author needs.

Chapter 23 also ends with an explicit hand-off. It flags that deep
`kgdb` scripting on top of a crash dump and the live-kernel
breakpoint workflows are reserved for a later, more advanced
chapter. That later chapter is this one. You are reading the second
half of a pair. If Chapter 23 is the first aid kit, Chapter 34 is
the full clinical toolbox.

In practice, this means two things. First, we will not re-explain the
fundamentals Chapter 23 already covered; we will assume you are
comfortable with `printf`, basic panic reading, and introductory
DTrace. If any of those feel shaky, reread the relevant section of
Chapter 23 first, because the advanced material builds directly on
those habits. Second, when a technique here has a simpler counterpart
in Chapter 23 (for example, a basic `bt` in `kgdb` is simpler than
walking `struct thread` fields), we will point to the Chapter 23
version and then show why the advanced version earns its extra
complexity.

Think of the two chapters as a single arc. Chapter 23 teaches you
how to notice that something is wrong and take a first look. Chapter
34 teaches you how to reconstruct, in detail, what the kernel was
doing at the moment the bug occurred, even on a machine that is no
longer running.

The material is cumulative. Each section adds another layer onto the
`bugdemo` driver, so the labs read most naturally in order. You can skim
ahead for reference, but if this is your first encounter with kernel
debugging tools, walking through the labs in sequence will build the
mental model we want.

You do not need any special hardware. A modest FreeBSD 14.3 virtual machine
is enough for every lab in the chapter. For Lab 3 and Lab 4 you will want to
have configured a crash dump device, which the chapter walks through, and
for Lab 5 you will need DTrace available in your kernel. Both are standard
on an ordinary FreeBSD installation.

Some of the techniques in this chapter deliberately crash the kernel. This is
safe on a development machine and expected as part of the learning process.
It is not safe on a production machine where other people depend on
uninterrupted service. The final section of the chapter is dedicated to that
distinction, because the discipline of knowing when not to use a tool is as
important as knowing how to use it.

## How to Get the Most Out of This Chapter

The chapter is organized around a pattern you will see repeated throughout.
First we explain what a technique is, then we explain why it exists and what
kind of bug it is meant to catch, then we ground it in real FreeBSD source
code so you can see where the idea lives in the kernel, and finally we apply
it to the `bugdemo` driver through a small lab. Reading and experimenting
together is the most effective approach. The labs are deliberately small
enough to run in a few minutes each.

A few habits will make the work smoother. Keep a terminal open to
`/usr/src/` so you can look at the real code whenever the chapter references
it. The book teaches through observation of real FreeBSD practice, not
through invented pseudo-code, and you will build stronger intuition by
confirming with your own eyes that `KASSERT` really is defined where the
chapter says it is, or that `memguard(9)` really has the API we describe.

Keep a second terminal open to your test VM, where you will load the
`bugdemo` driver, trigger bugs, and watch the output. If you can attach a
serial console to the VM, do so. A serial console is the most reliable way
to capture the tail end of a panic message before the machine reboots, and
we will use it in several labs.

Finally, keep your expectations calibrated. Kernel bugs are often not what
they appear to be at first. A use-after-free might first present itself as
a random data corruption in an unrelated subsystem. A deadlock might first
look like a slow system call. One of the most valuable skills this chapter
teaches is patience: gathering evidence before forming a theory, and
confirming the theory before committing to a fix. The tools help, but the
discipline is what separates a quick bug hunt from a long one.

With those expectations in place, let us begin by discussing when advanced
debugging is actually the right response to a problem.

## 1. When and Why You Need Advanced Debugging

Most bugs in a driver can be solved without reaching for a crash dump or a
tracing framework. A careful reading of the code, an extra `printf`, a
second look at the return value of a function, a glance at `dmesg`: these
together resolve the majority of defects a driver author encounters. If you
can see the problem, reproduce it cheaply, and hold the relevant code in
your head, the simplest tool is the right tool.

Advanced debugging exists for the bugs that do not yield to that approach.
It is the toolkit we reach for when the problem is rare, when it appears
far from its cause, when it only shows up under specific timing, when the
driver hangs rather than crashes, or when the symptom is corruption rather
than failure. Those bugs share a common property: they require evidence
you cannot easily collect by reading code, and they require control over
the kernel's execution that a normal user process does not have.

### Bugs That Need More Than a printf

The first class of bug that demands advanced tooling is the bug that
destroys the evidence of its own cause. A use-after-free is the canonical
example. The driver frees an object, then some later code, possibly in a
different function or a different thread, reads or writes that memory.
By the time the crash happens, the free has long since occurred, the
memory has been reused for something unrelated, and the backtrace at the
point of the crash points to the victim, not the culprit. A `printf`
added at the crash site will faithfully print the nonsense it sees. It
will not tell you who freed the memory or when.

A second class is the bug that only appears under concurrency. Two threads
race for a lock. One of them takes the lock in the wrong order, deadlocking
against another thread that took the same locks in the reverse order. The
system goes quiet, and the bug leaves no message on the console. Adding
`printf` calls to the locking path often perturbs the timing just enough to
make the bug disappear, a frustrating property that Heisenbug enthusiasts
are familiar with. Static lock order checking, which FreeBSD provides
through `WITNESS`, exists precisely because this class of bug is hard to
find any other way.

A third class is the bug that cannot be observed at all in user space. The
driver corrupts a kernel data structure on one code path, and the
consequence shows up many minutes later in an unrelated subsystem. The
process that triggers the corruption is long gone by the time anything
goes wrong. The only way to correlate cause and effect is to capture the
full kernel state at the moment of the panic and walk it offline with
`kgdb`, or to trace the kernel continuously with DTrace so that the
suspicious event leaves a trail.

A fourth class is the bug that only appears on hardware you cannot attach a
debugger to, or in production configurations you cannot directly instrument.
The driver runs on a customer's machine, it crashes once a week, and nobody
wants your development workstation physically plugged into it. The tool for
this situation is the crash dump: a snapshot of kernel memory written to
disk at the moment of panic, carried to a safe environment, and analyzed
there. `dumpon(8)` configures where the dump goes, `savecore(8)` retrieves
it after the reboot, and `kgdb` reads it offline.

Each of these bug classes has its own tool in the FreeBSD debugging
toolkit. The rest of the chapter introduces them in turn. The point of this
opening section is simply to set expectations: we are not about to learn a
single technique that replaces `printf`. We are learning a family of
techniques, each suited to a particular kind of difficulty.

### The Cost of Advanced Tooling

Advanced debugging is not free. Each of the techniques we will study
carries some combination of build-time cost, run-time cost, and
disciplinary cost.

Build-time cost is the easiest to describe. `INVARIANTS` and `WITNESS`
make the kernel slower because they add checks that a production kernel
skips. `DEBUG_MEMGUARD` makes certain allocations dramatically slower
because it replaces them with full-page mappings that are unmapped on
free. A debug kernel built with `makeoptions DEBUG=-g` is several times
larger than a release kernel because every function carries full debug
information. None of these costs matter on a development machine, where
correctness is worth orders of magnitude more than speed. All of them
matter in production.

Run-time cost applies to the tools you enable in a running kernel. DTrace
probes that are disabled cost essentially nothing, but an enabled probe
still runs on every hit of the instrumented function. `ktr(9)` entries
are very cheap but not free. A verbose tracing session can generate
enough log output to fill a disk. A `kdb` session pauses the entire
kernel, which on a machine people are using is a disaster. Each tool has
a run-time budget, and part of the discipline of this chapter is knowing
what that budget is.

Disciplinary cost is the hardest to quantify but the easiest to
underestimate. Advanced debugging requires patience, careful note taking,
and a willingness to sit with incomplete information. It requires
resisting the urge to patch a visible symptom before you understand the
underlying defect. A crash that happens in module X almost never means
the bug is in module X. The reader who learns to collect evidence before
forming a theory will have an easier time with this chapter than the
reader who wants to commit a fix as quickly as possible.

### A Decision Framework

With those costs in mind, here is a simple decision framework for
choosing your tool. If the bug is easy to reproduce and the cause is
likely visible in the nearby code, start with reading and with strategic
`printf` or `log(9)` statements. If the bug only appears under load or
concurrency, enable `INVARIANTS` and `WITNESS` and rebuild. If the bug
produces a panic, capture the dump and open it in `kgdb`. If the bug
involves memory corruption, enable `DEBUG_MEMGUARD` on the suspect
allocation type. If the bug involves a silent misbehavior rather than a
crash, add SDT probes and watch them with DTrace. If you need to
understand timing between events in an interrupt handler, use `ktr(9)`.
And if the bug is on a production machine, see Section 7 before doing
anything at all.

We will spend the rest of the chapter teaching each of these techniques
in depth. The `bugdemo` driver we are about to meet gives us a safe place
to apply every one of them, with known bugs to hunt and known answers to
find.

### Meeting the bugdemo Driver

The `bugdemo` driver is a small pseudo-device that we will use as our lab
subject throughout the chapter. It has no hardware to drive. It exposes a
device node at `/dev/bugdemo` and accepts a handful of `ioctl(2)` commands
that deliberately trigger different classes of bug: a null pointer
dereference, an unlocked access that `WITNESS` can catch, a
use-after-free, a memory leak, an infinite loop inside a spinlock, and so
on. Each ioctl is gated by a sysctl switch so that the driver can be
loaded safely on a development system without accidentally triggering
anything.

We will introduce the driver properly in Lab 1, when we have the assertion
macros in hand. For now, keep in mind that every technique we study can be
demonstrated on `bugdemo` with a known starting point and a known answer.
That discipline, of reproducing bugs in a controlled setting, is itself
one of the most important skills this chapter aims to teach.

Now we are ready to begin the toolkit proper, starting with the assertion
macros that catch bugs at the instant they happen.

## 2. Using KASSERT, panic, and Related Macros

Defensive programming in user space often revolves around runtime checks
and careful error handling. Defensive programming in the kernel adds one
more tool: the assertion macro. An assertion states a condition that must
be true at a given point. If the condition is false, something is very
wrong, and the safest response is to stop the kernel immediately, before
the wrong state has a chance to spread. Assertions are the cheapest and
most effective debugging tool FreeBSD gives us, and they belong in every
serious driver.

We will start with the two most important macros, `KASSERT(9)` and
`panic(9)`, look at a handful of useful companions, and then discuss when
each is appropriate.

> **A note on line numbers.** When the chapter quotes code from
> `kassert.h`, `kern_shutdown.c`, or `cdefs.h`, the landmark is always
> the macro or function name. `KASSERT`, `kassert_panic`, `panic`, and
> `__dead2` will remain findable by those names in every FreeBSD 14.x
> tree even as the lines around them move. The sample backtrace you will
> see later, which quotes `file:line` pairs such as
> `kern_shutdown.c:400`, reflects a 14.3 tree at the time of writing and
> will not match line-for-line on a freshly updated system. Grep for the
> symbol rather than scroll to the number.

### KASSERT: A Check That Vanishes in Production

`KASSERT` is the in-kernel equivalent of the user-space `assert()` macro,
but smarter. It takes a condition and a message. If the condition is
false, the kernel panics with the message. If the kernel was compiled
without the `INVARIANTS` option, the entire check disappears at compile
time and costs nothing at runtime.

The macro lives in `/usr/src/sys/sys/kassert.h`. In a FreeBSD 14.3 source
tree it looks like this:

```c
#if (defined(_KERNEL) && defined(INVARIANTS)) || defined(_STANDALONE)
#define KASSERT(exp,msg) do {                                           \
        if (__predict_false(!(exp)))                                    \
                kassert_panic msg;                                      \
} while (0)
#else /* !(KERNEL && INVARIANTS) && !STANDALONE */
#define KASSERT(exp,msg) do { \
} while (0)
#endif /* KERNEL && INVARIANTS */
```

Four details of this definition are worth pausing on.

First, the macro is defined differently depending on whether `INVARIANTS`
is set. If it is not, `KASSERT` expands to an empty `do { } while (0)`
block, which the compiler optimizes away entirely. A release kernel built
without `INVARIANTS` pays no runtime cost for `KASSERT` calls, no matter
how many the driver contains. This is the property that lets us write
generous assertions in development without worrying about production
performance. The `_STANDALONE` branch lets the same macro work in the
bootloader, where `INVARIANTS` may be absent but the check is still
desired.

Second, the `__predict_false` hint tells the compiler that the condition
is almost always true. This improves code generation for the common path,
because the compiler will arrange the branch so that the hot path does
not take a jump. Defining `__predict_false` is one of the small
performance disciplines that keeps a debug kernel usable.

Third, the body of a failing assertion calls `kassert_panic`, not `panic`
directly. This is an implementation detail for making assertion messages
easier to parse, but it matters when you see a panic message in practice:
`KASSERT` failures produce a distinctive prefix that we will recognize
later.

Fourth, notice that the `msg` argument is passed in double parentheses.
That is because the macro passes it directly to `kassert_panic`, which
has a `printf`-style signature. In practice you write:

```c
KASSERT(ptr != NULL, ("ptr must not be NULL in %s", __func__));
```

The outer parentheses belong to the macro. The inner parentheses are the
argument list for `kassert_panic`. A beginner's mistake is to write
`KASSERT(ptr != NULL, "ptr is NULL")` with only one set of parentheses,
which will not compile. The double parentheses are the discipline that
reminds us a failing assertion will be formatted like a `printf`.

### INVARIANTS and INVARIANT_SUPPORT

`INVARIANTS` is the kernel build option that controls whether `KASSERT`
is active. A debug kernel enables it. The `GENERIC-DEBUG` configuration
shipped with FreeBSD 14.3 enables it by including `std.debug`, which you
can see in `/usr/src/sys/conf/std.debug`. A release `GENERIC` kernel does
not enable it.

There is also a related option called `INVARIANT_SUPPORT`. `INVARIANT_SUPPORT`
compiles in the functions that assertions might call, without making them
mandatory. This allows loadable kernel modules that were built with
`INVARIANTS` to load into a kernel that was not built with `INVARIANTS`, as
long as `INVARIANT_SUPPORT` is present. For a driver author, the practical
upshot is this: if you build your module with `INVARIANTS`, make sure the
kernel you are loading it into has at least `INVARIANT_SUPPORT`. The
`GENERIC-DEBUG` kernel has both, which is one of the reasons we recommend
using it throughout development.

### MPASS: KASSERT With a Default Message

Writing a message for every assertion can be tedious, especially for
simple invariants. FreeBSD provides `MPASS` as a shorthand for
`KASSERT(expr, ("Assertion expr failed at file:line"))`:

```c
#define MPASS(ex)               MPASS4(ex, #ex, __FILE__, __LINE__)
#define MPASS2(ex, what)        MPASS4(ex, what, __FILE__, __LINE__)
#define MPASS3(ex, file, line)  MPASS4(ex, #ex, file, line)
#define MPASS4(ex, what, file, line)                                    \
        KASSERT((ex), ("Assertion %s failed at %s:%d", what, file, line))
```

The four forms let you customize the message, the file, or both. The
simplest form, `MPASS(ptr != NULL)`, stringifies the expression itself
and embeds the location automatically. When the message can afford to be
terse, `MPASS` produces less visual clutter in the source. When the
message needs context a future reader will appreciate, prefer
`KASSERT` with a written message.

A sensible rule of thumb is that `MPASS` is for internal invariants that
should never happen and where the identity of the expression is
self-explanatory. `KASSERT` is for conditions where the failure mode
deserves a descriptive message.

### CTASSERT: Compile-Time Assertions

Sometimes the condition you want to check can be decided at compile time.
`sizeof(struct foo) == 64`, for example, or `MY_CONST >= 8`. For those,
FreeBSD provides `CTASSERT`, also in `/usr/src/sys/sys/kassert.h`:

```c
#define CTASSERT(x)     _Static_assert(x, "compile-time assertion failed")
```

`CTASSERT` uses C11's `_Static_assert`. It produces a compile-time error
if the condition is false and has zero runtime cost because there is no
runtime involved. This is the ideal tool for structure layout checks that
must hold for the driver to be correct.

A typical use in the kernel is to guard a structure against accidental
size changes:

```c
struct bugdemo_command {
        uint32_t        op;
        uint32_t        flags;
        uint64_t        arg;
};

CTASSERT(sizeof(struct bugdemo_command) == 16);
```

If someone later adds a field without adjusting the size comment or
reordering thoughtfully, the build breaks immediately. This is far better
than finding out at runtime that the structure has grown and the ioctl
no longer matches user-space expectations.

### panic: The Unconditional Stop

Where `KASSERT` is a conditional check, `panic` is the unconditional
version. You call it when you have decided that continuing execution
would be worse than stopping:

```c
void panic(const char *, ...) __dead2 __printflike(1, 2);
```

The declaration lives in `/usr/src/sys/sys/kassert.h` and the implementation
in `/usr/src/sys/kern/kern_shutdown.c`. The `__dead2` attribute tells the
compiler that `panic` does not return, which lets it produce better code
downstream. The `__printflike(1, 2)` attribute tells it that the first
argument is a `printf`-style format string, so the compiler can type-check
the format against its arguments.

When would you use `panic` directly rather than `KASSERT`? Three common
situations. First, when the condition is so catastrophic that there is no
safe continuation even in a release kernel. Failing to allocate a soft
context during `attach`, for example, might be a `panic` rather than a
graceful cleanup if the driver has already been partially registered.
Second, when you want the message to appear even in non-debug builds,
because the event indicates a hardware or configuration failure that the
user must be told about. Third, as a placeholder during early development,
to make sure unreachable paths actually are unreachable, before you
replace the `panic` with a `KASSERT(0, ...)` in mature code.

Some drivers in `/usr/src/sys/dev/` use `panic` sparingly. Reading a few
examples will give you a feel for the tone: a `panic` message says
something like "the controller returned an impossible status" or "we
reached a case the state machine claims cannot happen." It is not the
normal response to an I/O error. It is the response to an invariant that
has been broken so thoroughly that the driver cannot be trusted to
continue.

### __predict_false and __predict_true

We saw `__predict_false` in the `KASSERT` definition. These two macros,
defined in `/usr/src/sys/sys/cdefs.h`, are compile-time hints to the
branch predictor:

```c
#if __GNUC_PREREQ__(3, 0)
#define __predict_true(exp)     __builtin_expect((exp), 1)
#define __predict_false(exp)    __builtin_expect((exp), 0)
#else
#define __predict_true(exp)     (exp)
#define __predict_false(exp)    (exp)
#endif
```

They do not change the semantics of the expression. They only tell the
compiler which outcome is more likely, which influences how the compiler
lays out the code. In a hot path, wrapping a likely-true condition in
`__predict_true` can improve cache behavior; wrapping a likely-false one
in `__predict_false` keeps the error-handling code off the fast path.

The first rule of using these macros is to be correct. If you predict
wrong, you slow the code down rather than speeding it up. The second rule
is to only use them in genuinely hot paths where the difference matters.
For most driver code, the compiler's default heuristics are fine, and
cluttering the code with predictions is more trouble than it is worth.

### Where Assertions Belong in a Driver

With these macros in hand, where do you actually put assertions? A few
patterns have proven useful in FreeBSD drivers.

The first is at function entry, for non-trivial preconditions. A driver
function that expects to be called with a particular lock held is a
perfect candidate:

```c
static void
bugdemo_process(struct bugdemo_softc *sc, struct bugdemo_command *cmd)
{
        BUGDEMO_LOCK_ASSERT(sc);
        KASSERT(cmd != NULL, ("cmd must not be NULL"));
        KASSERT(cmd->op < BUGDEMO_OP_MAX,
            ("cmd->op %u out of range", cmd->op));
        /* ... */
}
```

The `BUGDEMO_LOCK_ASSERT` is a macro convention many drivers adopt that
wraps a `mtx_assert(9)` or `sx_assert(9)` call. This pattern, where each
subsystem has its own `_ASSERT` macro that checks its own lock, scales
well across a large driver.

The second pattern is at state transitions. If a driver state machine
has four valid states and an `attach` path that should only ever run in
state `INIT`, an assertion at the top of `attach` will catch any future
refactor that breaks that invariant:

```c
KASSERT(sc->state == BUGDEMO_STATE_INIT,
    ("attach called in state %d", sc->state));
```

The third pattern is after subtle arithmetic. If a calculation should
produce a value in a known range, check:

```c
idx = (offset / PAGE_SIZE) & (SC_NRING - 1);
KASSERT(idx < SC_NRING, ("idx %u out of range", idx));
```

This is especially valuable in ring buffer code, where an off-by-one
between producer and consumer can cause silent data corruption.

The fourth pattern is for pointers that could be NULL but should not be.
If a function receives a pointer argument that is only valid when nonzero,
a single `KASSERT(ptr != NULL, ...)` at the top of the function catches
years of future misuse.

### When Not to Use Assertions

Assertions are not a replacement for error handling. The rule is:
`KASSERT` checks things that the programmer guarantees, not things the
environment guarantees. If a memory allocation with `M_NOWAIT` can fail
under memory pressure, you do not assert it succeeded. You check the
return value and handle the failure. If a user-space program passes a
structure larger than you expect, you return `EINVAL`, not `KASSERT(0)`.
The assertion is for internal consistency, not for external input.

Another anti-pattern is using assertions for conditions that only hold in
certain configurations. `KASSERT(some_sysctl == default)` is wrong if
some_sysctl is user-tunable, because the assertion will fail on any
system that has tuned it. Check the configuration explicitly and handle
it, or assert only within the branch where the assumption actually holds.

A more subtle anti-pattern is using assertions as documentation. "This is
how it works, and it had better stay that way" is a tempting use of
`KASSERT`, but if the assertion only holds today and could reasonably
change tomorrow, you have created a future bug for someone who does not
remember your promise. Better to leave a comment that states the
assumption, and let the code evolve. Assertions should capture permanent
invariants, not temporary implementation choices.

### A Small Real-World Example

Let us see these ideas applied in real FreeBSD code. Open
`/usr/src/sys/dev/null/null.c` and look at a typical check near the read
handler. The driver is extremely simple, so there are few assertions, but
many drivers in `/usr/src/sys/dev/` use `KASSERT` liberally. For a richer
example, scan `/usr/src/sys/dev/uart/uart_bus_pci.c` or
`/usr/src/sys/dev/mii/mii.c`, where assertions at function entry catch
callers that do not hold the expected locks.

The consistency of this pattern across the tree is not accidental. It
reflects a cultural expectation that drivers will express their
invariants in code, not only in comments. When you adopt the same habit
in your own drivers, you join that culture. Your drivers will be easier
to port, easier to review, and much easier to debug when something
eventually goes wrong.

### A Quick Example: Adding Assertions to bugdemo

Let us add a small set of assertions to an imagined `bugdemo` driver.
Assume we have a softc structure with a mutex, a state field, and a
counter, and an `ioctl` handler that takes a `struct bugdemo_command`.

```c
static int
bugdemo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
        struct bugdemo_softc *sc = dev->si_drv1;
        struct bugdemo_command *bcmd = (struct bugdemo_command *)data;

        KASSERT(sc != NULL, ("bugdemo: softc missing"));
        KASSERT(sc->state == BUGDEMO_STATE_READY,
            ("bugdemo: ioctl in state %d", sc->state));

        switch (cmd) {
        case BUGDEMO_TRIGGER:
                KASSERT(bcmd->op < BUGDEMO_OP_MAX,
                    ("bugdemo: op %u out of range", bcmd->op));
                BUGDEMO_LOCK(sc);
                bugdemo_process(sc, bcmd);
                BUGDEMO_UNLOCK(sc);
                return (0);
        default:
                return (ENOTTY);
        }
}
```

Four assertions, each catching a different class of future bug. The
first checks that the driver's private pointer is actually set, which is
easy to forget when `make_dev(9)` is called with `NULL` by mistake. The
second checks the driver state, which will fire if someone adds a code
path that can reach `ioctl` before `attach` has finished. The third
checks user-provided input is in range, though in a production context
this particular check would also be done as a real input validation that
returns an error, because `ioctl` is a public interface. The fourth, not
shown here but implied by `bugdemo_process`, asserts the lock is held.

These few lines express a lot of invariants. In a debug kernel, they
will catch real bugs the moment they happen. In a release kernel, they
disappear entirely. That is the bargain `KASSERT` offers, and taking it
is one of the best habits a driver author can cultivate.

With this foundation in place, we can move on to what happens when an
assertion actually fires, which brings us to the panic path and the
crash dump.

## 3. Analyzing Panics and Crash Dumps

When a `KASSERT` fails or `panic` is called, the kernel takes a series of
well-defined steps. Understanding those steps is the first part of making
sense of a crash. The second part is knowing what traces the kernel
leaves behind, and how to read them after the fact. This section walks
through both.

### What Happens During a Panic

A panic is the kernel's controlled shutdown in response to an
unrecoverable error. The exact sequence depends on the build options,
but a typical panic in a FreeBSD 14.3 kernel proceeds as follows.

First, `panic()` or `kassert_panic()` is called with a message. The
message is formatted and written to the system log. If a serial console
is attached, it appears there immediately. If only a graphical console
is available, it appears on screen, though there is often too little time
to read a long trace before the machine reboots, which is one reason we
recommend a serial or virtual console during this chapter.

Second, the kernel captures a backtrace of the panicking thread. You
will see this on the console as a list of function names with offsets.
The backtrace is the most valuable piece of information a panic
produces, because it tells you the chain of calls that led to the
failure. Reading it top-down shows you the function that called `panic`,
the function that called that one, and so on, back to the entry point.

Third, if the kernel has been built with `KDB` enabled and a backend
such as `DDB`, the kernel enters the debugger. `DDB` is the in-kernel
debugger. It accepts commands directly on the console: `bt` to show a
backtrace, `show registers` to dump register state, `show proc` to show
process information, and so on. We will use `DDB` briefly in Section 4.
If `KDB` is not enabled, or if the kernel is configured to skip the
debugger on panic, the kernel moves on.

Fourth, if a dump device is configured, the kernel writes a dump to it.
The dump is the entire contents of kernel memory, or at least the
portions marked as dumpable, serialized onto the dump device. This is
the crash dump that `savecore(8)` will retrieve after the reboot.

Fifth, the kernel reboots the machine, unless it has been asked to stop
at the debugger. After the reboot, when the system comes up, `savecore(8)`
runs and writes the dump to `/var/crash/vmcore.N` along with a textual
summary. You now have everything you need to analyze the crash offline.

The whole sequence takes anywhere from a fraction of a second to a few
minutes, depending on how large the kernel is, how fast the dump device
is, and how configured the system is. On a development VM, dumping a
kernel of a few hundred megabytes to a virtual disk is typically a
matter of seconds.

### Reading a Panic Message

A panic message in FreeBSD 14.3 looks something like this:

```text
panic: bugdemo: softc missing
cpuid = 0
time = 1745188102
KDB: stack backtrace:
db_trace_self_wrapper() at db_trace_self_wrapper+0x2b
vpanic() at vpanic+0x182
panic() at panic+0x43
bugdemo_ioctl() at bugdemo_ioctl+0x24
devfs_ioctl() at devfs_ioctl+0xc2
VOP_IOCTL_APV() at VOP_IOCTL_APV+0x3f
vn_ioctl() at vn_ioctl+0xdc
devfs_ioctl_f() at devfs_ioctl_f+0x1a
kern_ioctl() at kern_ioctl+0x284
sys_ioctl() at sys_ioctl+0x12f
amd64_syscall() at amd64_syscall+0x111
fast_syscall_common() at fast_syscall_common+0xf8
--- syscall (54, FreeBSD ELF64, sys_ioctl), rip = ..., rsp = ...
```

Read this top to bottom. The first line is the panic message itself. The
`cpuid` and `time` lines are metadata; they are rarely useful for
debugging but occasionally help when reconciling multiple logs. The
`KDB: stack backtrace:` line marks the start of the trace.

The first few frames are the panic infrastructure itself:
`db_trace_self_wrapper`, `vpanic`, `panic`. These are always there on a
panic and can be skipped. The first interesting frame is `bugdemo_ioctl`,
which is where our driver called `panic`. The frames below that are the
path that got us to `bugdemo_ioctl`: `devfs_ioctl`, `vn_ioctl`,
`kern_ioctl`, `sys_ioctl`, `amd64_syscall`. This tells us that the
panic happened during an ioctl system call, which is already a useful
clue. The final line shows the syscall number (54, which is `ioctl`)
and the instruction pointer at entry.

The offsets (`+0x24`, `+0xc2`) are byte offsets into each function. On
their own they are not human-readable, but they let `kgdb` resolve the
exact line of source code when the debug kernel is available.

Writing this kind of message down, or capturing the serial console log,
is the first thing you should do when a panic happens. If the machine
reboots too quickly to read, configure a serial console or a text-mode
virtual console where the history is retained.

### Configuring the Dump Device

For `savecore(8)` to have anything to retrieve, the kernel must know
where to write the dump. FreeBSD calls this the dump device, and
`dumpon(8)` is the utility that configures it.

There are two common ways to set this up. The simplest is a swap
partition. During install, `bsdinstall` typically creates a swap
partition large enough for kernel memory, and FreeBSD 14.3 automatically
configures it as the dump device if you enabled the relevant options.
You can check with:

```console
# dumpon -l
/dev/da0p3
```

If that command lists your swap device, you are good. If it says no
dump device is configured, you can set one manually:

```console
# dumpon /dev/da0p3
```

To make this persistent across reboots, put it in `/etc/rc.conf`:

```sh
dumpdev="/dev/da0p3"
dumpon_flags=""
```

You can see the defaults for these variables in
`/usr/src/libexec/rc/rc.conf`, which is the authoritative source for
all default rc.conf values in the base system. Grep for `dumpdev=`
and `dumpon_flags=` to find the relevant block.

An alternative, introduced in modern FreeBSD, is to use a file-backed
dump. This avoids the need to dedicate a disk partition to dumps. See
`dumpon(8)` for the exact syntax; the short version is that you can
point `dumpon` at a file on a filesystem, and the kernel will dump into
it at panic time. File-backed dumps are convenient for development VMs
where you do not want to re-partition the disk.

A second rc.conf variable controls where `savecore(8)` puts retrieved
dumps:

```sh
dumpdir="/var/crash"
savecore_enable="YES"
savecore_flags="-m 10"
```

The `-m 10` argument keeps only the most recent ten dumps, which is a
reasonable default. If you are chasing a rare bug, raise the number; if
disk space is tight, lower it. `savecore(8)` runs from `/etc/rc.d/savecore`
during boot, before most services are up, so your dump is preserved
before anything else touches `/var`.

### Enabling Dumps in the Kernel

For the kernel to be willing to write a dump, it must be built with the
right options. In FreeBSD 14.3, the `GENERIC` kernel is already
configured for the framework pieces. If you look at
`/usr/src/sys/amd64/conf/GENERIC` around the top of the file, you will
see something close to:

```text
options         KDB
options         KDB_TRACE
options         EKCD
options         DDB_CTF
```

`KDB` is the kernel debugger framework. `KDB_TRACE` enables automatic
stack traces on panic. `EKCD` enables encrypted kernel crash dumps,
which is useful when dumps contain sensitive data. `DDB_CTF` tells the
build to include CTF type information for the debugger. Together these
options give a fully capable dumping kernel.

Notice what is *not* in `GENERIC`: `options DDB` and `options GDB`
themselves. The `KDB` framework is there, but the in-kernel debugger
backend (`DDB`) and the remote GDB stub (`GDB`) are added by
`std.debug`, which `GENERIC-DEBUG` includes. A plain `GENERIC` kernel
will still write a dump on panic, but if you drop to the console during
a live system, there is no `DDB` prompt to greet you.

If you are building your own kernel, either add the backends
explicitly or, simpler, start from `GENERIC-DEBUG`, which enables them
plus the debugging options we will need for the rest of the chapter.
`GENERIC-DEBUG` lives at `/usr/src/sys/amd64/conf/GENERIC-DEBUG` and is
just two lines:

```text
include GENERIC
include "std.debug"
```

The `std.debug` file in `/usr/src/sys/conf/std.debug` adds `DDB`, `GDB`,
`INVARIANTS`, `INVARIANT_SUPPORT`, `WITNESS`, `WITNESS_SKIPSPIN`,
`MALLOC_DEBUG_MAXZONES=8`, `ALT_BREAK_TO_DEBUGGER`, `DEADLKRES`,
`BUF_TRACKING`, `FULL_BUF_TRACKING`, `QUEUE_MACRO_DEBUG_TRASH`, and a
few subsystem-specific debug flags. Note that `DDB` and `GDB` themselves
come from `std.debug`, not from `GENERIC`; the release kernel enables
`KDB` and `KDB_TRACE` but leaves the backends out unless you opt in.
This is the recommended debug kernel for driver development and the
kernel we will assume in the rest of the chapter unless we say
otherwise.

### Retrieving the Dump with savecore

After a panic and reboot, `savecore(8)` runs early in the boot
sequence. By the time you have a shell prompt, the dump is already in
`/var/crash/`:

```console
# ls -l /var/crash/
total 524288
-rw-------  1 root  wheel         1 Apr 20 14:23 bounds
-rw-r--r--  1 root  wheel         5 Apr 20 14:23 minfree
-rw-------  1 root  wheel  11534336 Apr 20 14:23 info.0
-rw-------  1 root  wheel  11534336 Apr 20 14:23 info.last
-rw-------  1 root  wheel  524288000 Apr 20 14:23 vmcore.0
-rw-------  1 root  wheel  524288000 Apr 20 14:23 vmcore.last
```

The `vmcore.N` file is the dump itself. The `info.N` file is a textual
summary of the panic, including the panic message, the backtrace, and
the kernel version. Always read `info.N` first. If the message and
backtrace are enough to identify the bug, you may not need to go
further.

A few common problems to watch for. If `ls` shows only `bounds` and
`minfree`, no dump has been captured yet. This usually means the dump
device is not configured or the kernel did not manage to write to it
before rebooting. Check `dumpon -l` and re-panic. If `savecore` logs
messages about a checksum mismatch, the dump was truncated, which
typically indicates the dump device was too small. If the machine
never panicked cleanly but simply rebooted, the kernel likely did not
have `KDB` enabled, so there was no dump machinery to invoke.

The `info.N` file is short enough to read in full. It includes the
kernel version, the panic string, and the backtrace the kernel captured
at panic time. On FreeBSD 14.3 it looks something like this:

```text
Dump header from device: /dev/da0p3
  Architecture: amd64
  Architecture Version: 2
  Dump Length: 524288000
  Blocksize: 512
  Compression: none
  Dumptime: 2026-04-20 14:22:34 -0300
  Hostname: devbox
  Magic: FreeBSD Kernel Dump
  Version String: FreeBSD 14.3-RELEASE #0: ...
  Panic String: panic: bugdemo: softc missing
  Dump Parity: 3142...
  Bounds: 0
  Dump Status: good
```

If the `Dump Status` is `good`, the dump is usable. If it says `bad`,
the dump was truncated or the checksum failed.

### Opening a Dump with kgdb

Once you have a dump, the next step is to open it with `kgdb`. `kgdb` is
FreeBSD's version of `gdb` specialized for kernel images. It needs three
things: the kernel image that produced the dump, the debug kernel image
that contains symbols, and the dump file itself. On most systems all
three are in predictable places:

- The running kernel: `/boot/kernel/kernel`
- The debug kernel with full symbols: `/usr/lib/debug/boot/kernel/kernel.debug`
- The dump: `/var/crash/vmcore.N`

The simplest invocation is:

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.0
```

or equivalently:

```console
# kgdb /usr/lib/debug/boot/kernel/kernel.debug /var/crash/vmcore.0
```

`kgdb` is a normal GDB session with kernel-specific tweaks. If your
kernel was built with `makeoptions DEBUG=-g` (which `GENERIC-DEBUG`
does), the debug symbols are included, and `kgdb` will be able to
resolve every frame to source code.

When `kgdb` starts, it automatically runs a few commands:

```console
(kgdb) bt
#0  __curthread () at /usr/src/sys/amd64/include/pcpu_aux.h:57
#1  doadump (textdump=...) at /usr/src/sys/kern/kern_shutdown.c:400
#2  0xffffffff80b6cf77 in kern_reboot (howto=260)
    at /usr/src/sys/kern/kern_shutdown.c:487
#3  0xffffffff80b6d472 in vpanic (fmt=..., ap=...)
    at /usr/src/sys/kern/kern_shutdown.c:920
#4  0xffffffff80b6d2c3 in panic (fmt=...)
    at /usr/src/sys/kern/kern_shutdown.c:844
#5  0xffffffff83e01234 in bugdemo_ioctl (dev=..., cmd=..., data=..., fflag=..., td=...)
    at /usr/src/sys/modules/bugdemo/bugdemo.c:142
...
```

The top frame is the panic infrastructure. The interesting frame is
frame 5, `bugdemo_ioctl` at `bugdemo.c:142`. To jump to that frame:

```console
(kgdb) frame 5
#5  0xffffffff83e01234 in bugdemo_ioctl (dev=..., cmd=...)
    at /usr/src/sys/modules/bugdemo/bugdemo.c:142
142         KASSERT(sc != NULL, ("bugdemo: softc missing"));
```

`kgdb` prints the source line. From here you can inspect local
variables with `info locals`, look at `sc` directly with `print sc`, or
list the surrounding source with `list`:

```console
(kgdb) print sc
$1 = (struct bugdemo_softc *) 0x0
```

That tells us `sc` really is NULL, confirming the panic message. Now we
can work out why, which usually means walking up the stack to find
where `sc` should have been set:

```console
(kgdb) frame 6
```

and so on. The sequence of `frame N`, `print VAR`, `list` is the
bread-and-butter of `kgdb` analysis. It is the same conversation a
gdb user has with any crashing program, adapted to the kernel.

### Useful kgdb Commands

Beyond `bt` and `frame`, a handful of commands cover most debugging
sessions.

- `info threads` lists all threads in the dumped system. In a modern
  kernel this can be hundreds of entries. Each has a number and a
  state.
- `thread N` switches to a specific thread, as if that thread had been
  the one that panicked. This is essential when a deadlock has
  happened and the panicking thread is not the one holding the
  problematic lock.
- `bt full` prints a backtrace with local variables at each frame.
  This is often the fastest way to see the state of a function
  involved in the panic.
- `info locals` shows local variables in the current frame.
- `print *SOMETHING` dereferences a pointer and prints the contents of
  the structure it points to.
- `list` shows the source around the current line; `list FUNC` shows
  the source of a function by name.

There are many more, documented in `gdb(1)`, but these are the ones a
driver author reaches for most often.

### Walking struct thread in a Dump

A panic backtrace answers "where did the crash happen?" but it rarely
answers "who was doing what?" The kernel keeps a dense record of every
live thread in `struct thread`, and once a dump is open in `kgdb` we
can read that record directly. For a driver author the value is
concrete: the fields of `struct thread` tell you what job this thread
was performing when the kernel crashed, which lock it was waiting on,
which process it belonged to, and whether it was still inside your
code when the panic occurred.

`struct thread` is defined in `/usr/src/sys/sys/proc.h`. It is a large
structure, so rather than read every field, we focus on a small set
that matters most for driver debugging. In `kgdb` the quickest way to
see those fields is to take the current thread and dereference it:

```console
(kgdb) print *(struct thread *)curthread
```

On the panicking CPU `curthread` is already correct, but you can also
reach a specific thread from the `info threads` listing. `kgdb` numbers
every thread sequentially. Once you know the number, `thread N`
switches context, and from there `print *$td` (or `print *(struct
thread *)0xADDR` if you have the raw address) prints the structure.

The fields to know are these. `td_name` is a short, readable name for
the thread, often set by `kthread_add(9)` or by the userspace program
that spawned it. When a driver creates its own kernel thread, this is
the name that shows up. `td_tid` is the numeric thread identifier the
kernel assigns; `ps -H` in userspace shows the same number. `td_proc`
is a pointer to the owning process, which gives us access to the
larger `struct proc` for more context. `td_flags` carries the `TDF_*`
bit field that records scheduler and debugger state; the definitions
live next to the structure in `/usr/src/sys/sys/proc.h`, and many
panics can be partially explained by reading those bits. `td_lock` is
the spin mutex that currently protects this thread's scheduler state.
It is almost always a CPU-local lock in a running kernel; in a dump,
a `td_lock` that points to an unexpected address is a strong hint
that something corrupted the scheduler view of this thread.

Two more fields are decisive when the panic involves sleeping or
waiting. `td_wchan` is the "wait channel," the kernel address the
thread is sleeping on. `td_wmesg` is a short human-readable string
describing why (for example, `"biord"` for a thread waiting on a buf
read, or `"select"` for a thread inside `select(2)`). If the panic
happened while threads were sleeping, these two fields tell you what
each thread was waiting for. `td_state` is the TDS_* state value
(defined just below `struct thread`); it tells you whether the thread
was running, runnable, or inhibited at the moment of the crash.

For lock bugs specifically, `td_locks` counts the non-spin locks
currently held by the thread, and `td_lockname` records the name of
the lock the thread is currently blocked on, if any. If a thread
panics with a non-zero `td_locks`, that thread was holding one or
more sleep locks at the time of the crash: useful context when the
panic is a `mutex not owned` or `Lock (sleep mutex) ... is not
sleepable` message.

A short `kgdb` session that pulls these fields out might look like
this:

```console
(kgdb) thread 42
[Switching to thread 42 ...]
(kgdb) set $td = curthread
(kgdb) print $td->td_name
$2 = "bugdemo_worker"
(kgdb) print $td->td_tid
$3 = 100472
(kgdb) print $td->td_state
$4 = TDS_RUNNING
(kgdb) print $td->td_wmesg
$5 = 0x0
(kgdb) print $td->td_locks
$6 = 1
(kgdb) print $td->td_proc->p_pid
$7 = 0
(kgdb) print $td->td_proc->p_comm
$8 = "kernel"
```

Reading this: thread 42 was a kernel thread called `bugdemo_worker`,
running when the panic hit, not sleeping on anything (`td_wmesg` is
NULL), and it was still holding exactly one sleep lock. The owning
process is the kernel proc with pid 0 and command name `kernel`,
which is the expected owner of kernel-only threads. The interesting
fact is `td_locks == 1`, because it tells us the thread held a lock
at panic time; a follow-up with `show alllocks` in DDB, or with
`show lockedvnods` if file locks are relevant, would pinpoint which.

### Walking struct proc in a Dump

Each thread belongs to a `struct proc`, defined next to `struct
thread` in `/usr/src/sys/sys/proc.h`. `struct proc` carries the
process-wide context: identity, credentials, address space, open
files, parent relationship. For driver bugs, a handful of these
fields are particularly useful.

`p_pid` is the process identifier, the same number userspace sees in
`ps`. `p_comm` is the process command name, truncated to `MAXCOMLEN`
bytes. Together they tell you which userspace process triggered the
kernel path that panicked. `p_state` is the PRS_* process state,
letting you distinguish a newly-forked process, a running one, and a
zombie. `p_numthreads` tells you how many threads this process has;
for a multithreaded userland program that called into your driver,
the count can be surprising. `p_flag` holds the P_* flag bits, which
encode properties like tracing, accounting, and single-threading;
`/usr/src/sys/sys/proc.h` documents each bit near the flag block.

Three pointers give you the larger context. `p_ucred` references the
process credentials, useful when the panic might be tied to a
privilege check your driver performed. `p_vmspace` points to the
address space, which matters when the panic involves a user pointer
that turned out to belong to an unexpected process. `p_pptr` points
to the parent process; walking this chain with `p_pptr->p_pptr`
eventually leads to `initproc`, the ancestor of every userspace
process.

A short walk from a thread to its process looks like this in `kgdb`:

```console
(kgdb) set $p = curthread->td_proc
(kgdb) print $p->p_pid
$9 = 3418
(kgdb) print $p->p_comm
$10 = "devctl"
(kgdb) print $p->p_state
$11 = PRS_NORMAL
(kgdb) print $p->p_numthreads
$12 = 4
(kgdb) print $p->p_flag
$13 = 536871424
```

Now we know the panic happened while a userspace `devctl` process
with pid 3418 was running, that the process had four threads, and
that its flag bits decoded through the P_* constants in
`/usr/src/sys/sys/proc.h` will tell us whether it was being traced,
accounted for, or in the middle of an exec. The flag integer on its
own looks opaque, but in `kgdb` you can let the enum-like P_* macros
do the decoding by casting or by using `info macro P_TRACED`.

For drivers that expose a character device, `p_fd` is also worth
knowing. It points to the file descriptor table of the process that
called into your driver, and in an advanced session you can walk it
to find which descriptor the call came in on. That is usually more
than a first-pass crash analysis needs, but the mechanism is worth
remembering for the rare bug that depends on how userspace had the
device open.

Between `struct thread` and `struct proc`, you can reconstruct an
astonishing amount of context from a dump that at first glance only
shows a panic message and a backtrace. The cost is reading
`/usr/src/sys/sys/proc.h` once carefully; after that, the same
vocabulary is available to you in every debugging session for the
rest of your career.

### Using kgdb Against a Live Kernel

So far we have treated `kgdb` as a post-mortem tool: open a dump,
explore it offline, think at your own pace. `kgdb` also has a second
mode, where it attaches to a running kernel through `/dev/mem` rather
than to a saved dump. This mode is capable, but it is also the most
easily misused tool in the whole debugging toolbox, so we will discuss
it with explicit warnings.

The invocation looks almost identical to the post-mortem form, except
that the "core" is `/dev/mem`:

```console
# kgdb /boot/kernel/kernel /dev/mem
```

What actually happens is that `kgdb` uses the libkvm library to read
kernel memory through `/dev/mem`. The interface is documented in
`/usr/src/lib/libkvm/kvm_open.3`, which makes the distinction clear:
the "core" argument can be either a file produced by `savecore(8)`
or `/dev/mem`, and in the latter case the currently running kernel
is the target.

This is genuinely useful. You can inspect global variables, walk lock
graphs, look at in-flight I/O, and confirm whether a sysctl you just
set has taken effect. You can do this without rebooting, without
interrupting service, and without having to reproduce a crash. On a
development system that is hosting a long-running test, it is often
the fastest way to answer "what is the driver actually doing right
now?"

The risks are real. First, the kernel is running while you are
reading. Data structures change under you. A linked list you start
walking may have an entry removed halfway through; a counter you
print may have been incremented between the moment you asked for it
and the moment `kgdb` printed it; a pointer you follow may be
reassigned before the dereference completes. Unlike a dump, you are
reading a moving target, and you will sometimes see states that are
transiently inconsistent.

Second, `kgdb` on a live kernel is strictly read-only in practical
use. You can read memory, print structures, and walk data, but you
must not write to kernel memory through this path. The libkvm
interface does not provide locking or barriers, and an uncoordinated
write would race with the kernel itself. Treat every operation
through `/dev/mem` as inspection, never modification. If you want to
change running kernel state, use `sysctl(8)` or `sysctl(3)`, or
load a module, or use DDB from the console. Those mechanisms are
designed to coordinate with the rest of the kernel; raw writes
through `/dev/mem` are not.

Third, the perturbation is not zero. Reading through `/dev/mem` can
cause TLB traffic, and on large structures the cost is measurable.
If you are also profiling, attribute the noise accordingly.

Finally, access to `/dev/mem` requires root privilege, for obvious
reasons: anything that can read `/dev/mem` can read any secret the
kernel has ever held. On production systems, restricting this is a
security concern, and the policy around who may run `kgdb` against
a live kernel should reflect that.

Given those cautions, the guidance is straightforward. Prefer a crash
dump for any session where you want to take your time, where you
want to share the state with a colleague, or where consistency
matters. Prefer a live `kgdb` session for quick, read-only glances
at a running system where the question is small and the cost of
reboot would be high. When in doubt, take a dump with `sysctl
debug.kdb.panic=1` (if the system is expendable) or `dumpon` and a
deliberate triggering event, and do your analysis on the frozen
snapshot. The snapshot will still be there tomorrow; the running
kernel will not.

### A Note on Symbols and Modules

When the panicking driver is a loadable module, `kgdb` also needs the
debug information for the module. If the module is in
`/boot/modules/bugdemo.ko` and was built with `DEBUG_FLAGS=-g`, the
debug symbols are embedded. `kgdb` will load them automatically when it
resolves frames in that module.

If the module lives somewhere nonstandard, you may need to tell `kgdb`
where to find its debug info:

```console
(kgdb) add-symbol-file /path/to/bugdemo.ko.debug ADDRESS
```

where `ADDRESS` is the module's load address, which you can find in
`kldstat(8)` output. In practice this is rarely needed on a modern
FreeBSD system, because `kgdb` looks in the right places by default.

What you do need to avoid is mixing kernels. If the running kernel and
the debug kernel came from different builds, symbols will not match
and `kgdb` will show confusing or wrong information. Rebuild both from
the same source tree, or keep matched pairs. On a development system
this is usually not a problem because you build and install both
together.

### Closing Thoughts on Dumps

The crash dump is valuable because it preserves the kernel's
state at the moment of the panic. Unlike a running system, where
every read perturbs the state, a dump is a frozen snapshot. You can
examine it as long as you like, come back to it tomorrow, share it
with a colleague, or diff the state against the source. Even once you
have moved on to other bugs, a dump from an interesting failure is
worth keeping, because it is often the only record of that exact
sequence of events.

With panic mechanics and dump analysis behind us, we can move on to
the kernel configuration choices that make debugging actually
comfortable. That is the topic of Section 4.

## 4. Building a Debug-Friendly Kernel Environment

Everything we have learned so far depends on having the right kernel
options enabled. A stock `GENERIC` kernel is a production configuration.
It is optimized for speed, ships no debug information, and does not
include the checks that catch many driver bugs. For the work of this
chapter, we want the opposite: a kernel that is slow but thorough, that
carries full debug symbols, and that actively looks for bugs rather than
trusting the driver to behave. FreeBSD calls this `GENERIC-DEBUG`, and
setting it up is the topic of this section.

We will walk through building and installing a debug kernel, then look
at each of the interesting options in detail, including the debugger
backends (`DDB`, `GDB`), the invariant checks (`INVARIANTS`, `WITNESS`),
the memory debuggers (`DEBUG_MEMGUARD`, `DEBUG_REDZONE`), and the
console controls that let you enter the debugger from the keyboard.

### Building GENERIC-DEBUG

On a FreeBSD 14.3 system with `/usr/src/` populated, building a debug
kernel is a three-command operation. From `/usr/src/`:

```console
# make buildkernel KERNCONF=GENERIC-DEBUG
# make installkernel KERNCONF=GENERIC-DEBUG
# reboot
```

The `buildkernel` step takes longer than a release build because debug
information is generated and many more checks are compiled in. On a
modest four-core VM it typically takes twenty to thirty minutes.
`installkernel` places the result in `/boot/kernel/` and keeps the
previous kernel in `/boot/kernel.old/`, which is a safety net if the
new kernel fails to boot.

After reboot you can confirm the running kernel with `uname -v`:

```console
# uname -v
FreeBSD 14.3-RELEASE-p2 #0: ...
```

The `#0` indicates a locally built kernel. You can also check that the
debug options are active by reading `sysctl debug` entries, which we
will return to shortly.

### What GENERIC-DEBUG Enables

As we saw in Section 3, `GENERIC-DEBUG` is a thin configuration that
simply includes `GENERIC` and `std.debug`. The interesting content is
in `std.debug`, which is worth reading in full because it documents the
kernel's opinion on what good debug options look like. In a FreeBSD
14.3 tree the file is at `/usr/src/sys/conf/std.debug`, and the core
options are:

```text
options         BUF_TRACKING
options         DDB
options         FULL_BUF_TRACKING
options         GDB
options         DEADLKRES
options         INVARIANTS
options         INVARIANT_SUPPORT
options         QUEUE_MACRO_DEBUG_TRASH
options         WITNESS
options         WITNESS_SKIPSPIN
options         MALLOC_DEBUG_MAXZONES=8
options         VERBOSE_SYSINIT=0
options         ALT_BREAK_TO_DEBUGGER
```

Plus a handful of subsystem-specific debug flags for networking, USB,
HID, and CAM that we do not need to dwell on. Let us look at each of
the driver-relevant options in turn.

Note one thing that `std.debug` does *not* contain: `makeoptions DEBUG=-g`.
That line lives in `GENERIC` itself, near the top of
`/usr/src/sys/amd64/conf/GENERIC`. A release `GENERIC` kernel is
already built with `-g`, because the release engineering process wants
the debug information available even when `INVARIANTS` and `WITNESS`
are off. `GENERIC-DEBUG` inherits this through its `include "GENERIC"`.

### makeoptions DEBUG=-g

This passes `-g` to the compiler for every kernel file, producing a
kernel with full DWARF debug information. `kgdb` uses this debug
information to map from addresses back to source lines. Without `-g`,
`kgdb` can still show function names, but it cannot show the source
line where a crash happened, and `print someVariable` becomes
`print *(char *)0xffffffff...` with no symbolic names.

The cost is that the kernel binary is larger. On amd64 a debug
`GENERIC-DEBUG` kernel is several times the size of a non-debug
`GENERIC` kernel. For a development VM this does not matter. For a
production system it is often the reason to keep debug information
in a separate file (`/usr/lib/debug/boot/kernel/kernel.debug`) while
the running kernel is stripped.

### INVARIANTS and INVARIANT_SUPPORT

We met these in Section 2. `INVARIANTS` activates `KASSERT` and a
number of other runtime checks scattered through the kernel. Functions
throughout `/usr/src/sys/` have `#ifdef INVARIANTS` blocks that check
things like "this list is well-formed," "this pointer points into a
valid zone," or "this reference count is nonzero." With `INVARIANTS`
enabled, these checks fire at runtime. Without it, they are compiled
out.

The checks cost CPU cycles. As a rough order-of-magnitude figure on
typical FreeBSD 14.3-amd64 hardware, a busy `INVARIANTS` kernel runs
roughly five to twenty percent slower than a release kernel, and
sometimes more on allocation-heavy workloads. This is why `INVARIANTS`
is not enabled in `GENERIC`. For driver development, this overhead is
worth accepting in exchange for the bugs it catches. See Appendix F
for a reproducible workload that measures this ratio on your own
hardware.

`INVARIANT_SUPPORT` compiles in the helper routines that assertions
call, without activating the assertions in base kernel code. As noted
earlier, it allows modules built with `INVARIANTS` to load against
kernels without `INVARIANTS`. You almost always want both.

### WITNESS: Lock Order Verifier

`WITNESS` is one of the most effective debugging tools in the FreeBSD
arsenal. It tracks every lock operation and every lock dependency in
the kernel, and it fires a warning if it ever sees a lock order that
could deadlock. Because deadlocks are a class of bug that is extremely
hard to catch any other way, `WITNESS` is indispensable for any driver
that takes more than one lock.

The way `WITNESS` works is worth understanding. Every time a thread
acquires a lock, `WITNESS` notes which other locks the thread already
holds. From these observations it builds a lock order graph: "lock A
has been seen held before lock B," and so on. If the graph ever
contains a cycle, that is a potential deadlock, and `WITNESS` prints
a warning on the console with backtraces for the offending
acquisitions.

The output looks something like this:

```text
lock order reversal:
 1st 0xfffff80003abc000 bugdemo_sc_mutex (bugdemo_sc_mutex) @ /usr/src/sys/modules/bugdemo/bugdemo.c:203
 2nd 0xfffff80003def000 sysctl_lock (sysctl_lock) @ /usr/src/sys/kern/kern_sysctl.c:1842
stack backtrace:
 #0 kdb_backtrace+0x71
 #1 witness_checkorder+0xc95
 #2 __mtx_lock_flags+0x8f
 ...
```

Read this as: your driver's `bugdemo_sc_mutex` was acquired first, and
then later another thread was observed taking `sysctl_lock` first and
`bugdemo_sc_mutex` second. This is a potential deadlock, because
enough concurrent activity could make the two threads wait for each
other. The fix is always the same: establish a consistent lock order
across all paths that take both locks, and stick to it.

`WITNESS` is not cheap. It adds bookkeeping to every lock acquisition
and release. In our lab environment, on a busy kernel running a
lock-heavy workload, the overhead can approach twenty percent; the
exact figure varies with the amount of locking the workload does. But
the bugs it finds are the kind that destroy production uptime when
they slip through, so the investment is worth it in development. See
Appendix F for a reproducible workload that isolates this overhead
against a baseline kernel.

`WITNESS_SKIPSPIN` turns off `WITNESS` on spin mutexes. Spin locks are
typically short-lived and performance-critical, so checking them adds
overhead where it matters most. The default is to check them anyway,
but `std.debug` disables that check to keep the kernel usable. You can
re-enable it if you are specifically hunting spin-lock bugs.

### A Worked Race-Condition Walkthrough: Lock-Order Bug in bugdemo

Reading about `WITNESS` in the abstract is one thing; catching a real
lock-order reversal in a driver you wrote is another. This subsection
walks through a complete cycle: we introduce a deliberate ordering
bug into `bugdemo`, run it on a `GENERIC-DEBUG` kernel, read the
`WITNESS` report, and fix the bug. The walkthrough is short, but the
pattern repeats in every deadlock you will ever debug.

Assume our `bugdemo` driver has grown two locks as it gained features.
`sc_mtx` protects the per-unit state, and `cfg_mtx` protects a
configuration blob shared across units. Most of the driver already
takes them in the order "state first, config second," which is a
reasonable choice and which the author has followed in `bugdemo_ioctl`
and in the read/write entry points. But a recent sysctl handler,
written in a hurry, acquired the config lock first to validate a
value and then reached for the state lock to apply it. In source,
the two relevant excerpts look like this:

```c
/* bugdemo_ioctl: established ordering, state then config */
mtx_lock(&sc->sc_mtx);
/* inspect per-unit state */
mtx_lock(&cfg_mtx);
/* adjust shared config */
mtx_unlock(&cfg_mtx);
mtx_unlock(&sc->sc_mtx);
```

```c
/* bugdemo_sysctl_set: new path, config then state */
mtx_lock(&cfg_mtx);
/* validate new value */
mtx_lock(&sc->sc_mtx);
/* propagate into per-unit state */
mtx_unlock(&sc->sc_mtx);
mtx_unlock(&cfg_mtx);
```

Both paths individually are fine. The problem is that taken together
they form a cycle. If thread A enters `bugdemo_ioctl` and acquires
`sc_mtx`, and thread B concurrently enters `bugdemo_sysctl_set` and
acquires `cfg_mtx`, each thread now waits on the lock the other holds.
That is a classic AB-BA deadlock. It may not trigger on every run; it
depends on timing. `WITNESS` is the tool that refuses to wait for a
rare production failure to discover it.

On a `GENERIC-DEBUG` kernel the reversal is caught the first time both
orderings are observed, even if no actual deadlock has occurred yet.
The console message has a specific shape. Using the format emitted by
`witness_output` in `/usr/src/sys/kern/subr_witness.c`, which prints
pointer, lock name, witness name, lock class, and source location for
each lock involved, the real report looks like this:

```text
lock order reversal:
 1st 0xfffff80012345000 bugdemo sc_mtx (bugdemo sc_mtx, sleep mutex) @ /usr/src/sys/modules/bugdemo/bugdemo.c:412
 2nd 0xfffff80012346000 bugdemo cfg_mtx (bugdemo cfg_mtx, sleep mutex) @ /usr/src/sys/modules/bugdemo/bugdemo.c:417
lock order bugdemo cfg_mtx -> bugdemo sc_mtx established at:
 #0 witness_checkorder+0xc95
 #1 __mtx_lock_flags+0x8f
 #2 bugdemo_sysctl_set+0x7a
 #3 sysctl_root_handler_locked+0x9c
 ...
stack backtrace:
 #0 kdb_backtrace+0x71
 #1 witness_checkorder+0xc95
 #2 __mtx_lock_flags+0x8f
 #3 bugdemo_ioctl+0xd4
 ...
```

Each `1st` and `2nd` line carries four pieces of information. The
pointer (`0xfffff80012345000`) is the address of the lock object in
kernel memory. The first string is the instance name, set when the
lock was initialized. The two strings in parentheses are the
`WITNESS` name for the lock class and the lock class itself, in this
case `sleep mutex`. The path and line are where the lock was last
acquired along this reversed ordering. The block after `lock order
... established at:` shows the earlier backtrace that first taught
`WITNESS` the now-violated ordering, and the final `stack backtrace`
shows the current call path that violates it.

Reading all of that, the diagnosis is immediate. The driver has
established `sc_mtx -> cfg_mtx` in its normal paths, and
`bugdemo_sysctl_set` has just taken `cfg_mtx -> sc_mtx`. Both paths
are ours. The fix is to pick one ordering (here, the established one)
and rewrite the offending path to match:

```c
/* bugdemo_sysctl_set: corrected to follow house ordering */
mtx_lock(&sc->sc_mtx);
mtx_lock(&cfg_mtx);
/* validate new value and propagate in one atomic window */
mtx_unlock(&cfg_mtx);
mtx_unlock(&sc->sc_mtx);
```

If the locked region needs to be narrower, a common pattern is to
read state under `sc_mtx`, drop it, validate without holding any
lock, and then reacquire in the house order to apply. Either way,
the order is fixed at the driver level, not at the call site. A
useful habit is to document the order in a comment near the lock
declarations so future contributors do not have to rediscover it.

After the fix, rebuilding `bugdemo` on the same debug kernel and
rerunning the triggering test produces no further `WITNESS` output.
If a reversal reappears, `WITNESS` also supports querying the graph
interactively with `show all_locks` in DDB, which can show the
current state even without a full reversal report; for deeper
introspection, the source of `/usr/src/sys/kern/subr_witness.c` is
the authoritative explanation of both the bookkeeping and the
report format.

### The Same Bug Seen Through lockstat(1)

`WITNESS` tells you that an ordering is wrong. It does not tell you
how often each lock is actually contended, how long each acquisition
waits, or which callers are pushing on a given lock hardest. Those
are questions about contention, not correctness, and `lockstat(1)` is
the tool for them.

`lockstat(1)` is a DTrace-backed profiler for kernel locks. It works
by instrumenting the entry and exit points of lock primitives and
reporting summaries, including spin time on adaptive mutexes, sleep
time on sx locks, and hold time when asked. The classical invocation
is `lockstat sleep N`, which gathers data for N seconds and then
prints a summary.

If we run the buggy `bugdemo` under a workload that stresses both
paths (a small userspace program that opens several unit nodes and
simultaneously twiddles the sysctl in a tight loop), and profile with
`lockstat` for five seconds, the output on a FreeBSD system looks
roughly like this:

```console
# lockstat sleep 5

Adaptive mutex spin: 7314 events in 5.018 seconds (1458 events/sec)

Count indv cuml rcnt     nsec Lock                   Caller
-------------------------------------------------------------------------------
3612  49%  49% 0.00     4172 bugdemo sc_mtx         bugdemo_ioctl+0xd4
2894  40%  89% 0.00     3908 bugdemo cfg_mtx        bugdemo_sysctl_set+0x7a
 412   6%  95% 0.00     1205 bugdemo sc_mtx         bugdemo_read+0x2f
 220   3%  98% 0.00      902 bugdemo cfg_mtx        bugdemo_ioctl+0xe6
 176   2% 100% 0.00      511 Giant                  sysctl_root_handler_locked+0x4d
-------------------------------------------------------------------------------

Adaptive mutex block: 22 events in 5.018 seconds (4 events/sec)

Count indv cuml rcnt     nsec Lock                   Caller
-------------------------------------------------------------------------------
  14  63%  63% 0.00   184012 bugdemo sc_mtx         bugdemo_sysctl_set+0x8b
   8  36% 100% 0.00    41877 bugdemo cfg_mtx        bugdemo_ioctl+0xe6
-------------------------------------------------------------------------------
```

Each table follows the same column convention: `Count` is how many
events of this type were seen, `indv` is the percentage of events in
this class, `cuml` is the running total, `rcnt` is the average
reference count (always 1 for mutexes), `nsec` is the average
duration in nanoseconds, and the last two columns identify the lock
instance and the caller. The header line `Adaptive mutex spin`
indicates contention that was resolved by a short spin; `Adaptive
mutex block` indicates contention that actually forced a thread to
sleep on the mutex. Those headers, and the column layout, are
standard `lockstat` output; the format is documented in
`/usr/src/cddl/contrib/opensolaris/cmd/lockstat/lockstat.1` along
with worked examples at the end of that man page.

Two things are worth noting. First, both `bugdemo sc_mtx` and
`bugdemo cfg_mtx` appear in both directions of the table: the sysctl
path blocked on `sc_mtx` (line 1 of the block table), and the ioctl
path blocked on `cfg_mtx` (line 2). That is the contention signature
of the same ordering bug `WITNESS` reported, seen from the other
side. `WITNESS` told us the ordering was unsafe; `lockstat` tells us
that under this workload the unsafe ordering is also costing real
time.

Second, after we apply the fix from the previous subsection,
`lockstat` becomes a validation tool: rerun with the same workload
and the `Adaptive mutex block` table should shrink dramatically,
because the mutual wait between the two paths is gone. If it does
not shrink, we have fixed the ordering but created a pure contention
problem, and the next step is to narrow the critical section rather
than to change the order.

Useful `lockstat` options beyond the default include `-H` to watch
hold events (how long locks are held, not just contended), `-D N` to
show only the top N rows per table, `-s 8` to include eight-frame
stack traces with each row, and `-f FUNC` to filter on a single
function. For driver work, `lockstat -H -s 8 sleep 10` while a
targeted test runs is a remarkably productive default.

### Reading WITNESS and lockstat Together

`WITNESS` and `lockstat` are complementary. `WITNESS` is a
correctness tool: it detects bugs that will eventually produce a
deadlock, regardless of whether the current workload happens to hit
them. `lockstat` is a performance tool: it quantifies how much
current traffic is touching each lock and how long the traffic waits.
The same driver path often shows up in both, and the two views
together are frequently decisive.

A useful discipline when a driver grows past its first lock is to
make both tools part of the routine. Run `GENERIC-DEBUG` during
development so `WITNESS` sees every new code path the moment it
executes. Periodically run `lockstat` on a realistic workload to see
whether any of your locks are becoming bottlenecks even when their
ordering is sound. A lock that passes `WITNESS` and shows low
`lockstat` contention is a lock you can mostly stop thinking about.
A lock that passes `WITNESS` but dominates `lockstat` output is a
performance problem waiting for a refactor, not a correctness bug.
A lock that fails `WITNESS` is a bug regardless of what `lockstat`
says.

With that framework in mind, we can continue looking at the other
debug-kernel options that surface different classes of bug.

### MALLOC_DEBUG_MAXZONES

FreeBSD's kernel memory allocator (`malloc(9)`) groups similar
allocations into zones for speed. `MALLOC_DEBUG_MAXZONES=8` increases
the number of zones used by `malloc`, which spreads allocations across
more distinct memory regions. The practical effect is that
use-after-free and invalid free bugs are more likely to land in a
different zone than the original allocation, making them more
detectable.

This is a low-cost option. It is always on in debug kernels.

### ALT_BREAK_TO_DEBUGGER and BREAK_TO_DEBUGGER

These two options control how the user enters the kernel debugger from
the console. `BREAK_TO_DEBUGGER` enables the traditional
`Ctrl-Alt-Esc` or serial BREAK sequence. `ALT_BREAK_TO_DEBUGGER`
enables an alternative sequence, typed as `CR ~ Ctrl-B`, which is
useful over network consoles (ssh, virtio_console, and so on) where
sending a real BREAK is awkward.

`GENERIC` ships with `BREAK_TO_DEBUGGER` enabled.
`GENERIC-DEBUG` adds `ALT_BREAK_TO_DEBUGGER`. If you are on a serial
console, either sequence will drop you into `DDB`. From `DDB` you can
inspect kernel state, set breakpoints, and optionally continue
execution or panic.

This is an important convenience during development. A driver that
hangs the system without panicking can be investigated by dropping
into the debugger on command.

### DEADLKRES: The Deadlock Detector

`DEADLKRES` enables the deadlock resolver, which is a periodic thread
that watches for threads stuck in uninterruptible wait for too long.
If it finds any, it prints a warning and optionally panics. This
complements `WITNESS` by catching deadlocks that `WITNESS` did not
predict, which happens when the lock graph is not traversable
statically (for example, when locks are acquired by address through a
generic locking API).

`DEADLKRES` has some false positives in practice, particularly for
long-lived operations like filesystem I/O under heavy load. Reading
the warning and deciding whether it is a real deadlock is part of the
debugging skill this chapter is teaching.

### BUF_TRACKING

`BUF_TRACKING` records a short history of operations on each buffer
in the buffer cache. When a corruption is found, the buffer's history
can be printed, showing which code paths touched it and in what order.
This is useful for storage driver bugs but less commonly needed in
other drivers.

### QUEUE_MACRO_DEBUG_TRASH

The `queue(3)` macros (`LIST_`, `TAILQ_`, `STAILQ_`, and so on) are
used pervasively in the kernel for linked lists. When an element is
removed from a list, the usual behavior is to leave the element's
pointers alone. `QUEUE_MACRO_DEBUG_TRASH` overwrites them with
recognizable garbage values. Any later attempt to dereference those
pointers will crash in a recognizable way, rather than silently
corrupting the list.

This is a cheap option and catches a very common class of bug:
forgetting to remove an element from a list before freeing it, then
finding the list corrupted later.

### Memory Debuggers: DEBUG_MEMGUARD and DEBUG_REDZONE

Two more options that deserve attention are `DEBUG_MEMGUARD` and
`DEBUG_REDZONE`. They are not part of `std.debug` but are commonly
added for memory debugging sessions.

`DEBUG_MEMGUARD` is a specialized allocator that can be dropped in for
particular `malloc(9)` types. It backs each allocation with a separate
page or set of pages, marks the pages around the allocation as
inaccessible, and unmaps the allocation on free. Any access beyond
the bounds of the allocation, or any access after free, causes a
page fault that is trivial to diagnose. The trade-off is that every
allocation costs a full page of virtual memory plus kernel
bookkeeping, so you normally turn `DEBUG_MEMGUARD` on for one
specific malloc type at a time.

The relevant header is `/usr/src/sys/vm/memguard.h`, and the
configuration appears in `/usr/src/sys/conf/NOTES` on the
`options DEBUG_MEMGUARD` line. We will use `memguard(9)` in detail
in Section 6.

`DEBUG_REDZONE` is a lighter-weight memory debugger that places
guard bytes before and after each allocation. When the allocation is
freed, the guard bytes are checked, and any corruption is reported.
It does not catch use-after-free but is very good at catching buffer
overruns and underruns. See the `options DEBUG_REDZONE` line in
`/usr/src/sys/conf/NOTES` for the configuration.

Both `DEBUG_MEMGUARD` and `DEBUG_REDZONE` cost memory. For a debug
kernel on a development VM, both are often enabled. For a large
production server, neither is.

### KDB, DDB, and GDB Together

We have referenced these three options throughout this chapter. Let us
pin down the distinction, because it confuses many beginners.

`KDB` is the kernel debugger framework. It is the plumbing. It defines
entry points that the rest of the kernel calls when a panic or a
break-to-debugger event happens. It also defines an interface for
backends.

`DDB` and `GDB` are two such backends. `DDB` is the in-kernel
interactive debugger. When you hit `KDB_ENTER` and `DDB` is the
selected backend, you are dropped into an interactive prompt on the
console. `DDB` has a small set of commands: `bt`, `show`, `print`,
`break`, `step`, `continue`, and a few others. It is primitive but
self-contained: no other machine is needed.

`GDB` is the remote backend. When you hit `KDB_ENTER` and `GDB` is the
selected backend, the kernel waits for a remote GDB client to attach
over a serial line or a network connection. The client runs on a
different machine and sends commands over a protocol called the GDB
remote serial protocol. This is much more flexible because you have
full `gdb` on the client side, but it requires a second machine (or
another VM) and a connection between the two.

In practice, you enable both backends and switch between them at run
time. `sysctl debug.kdb.current_backend` names the active backend.
`sysctl debug.kdb.supported_backends` lists all compiled-in backends.
You can set `debug.kdb.current_backend` to `ddb` or `gdb` depending on
what kind of session you want. This is a useful convenience, because
the overhead of having both compiled in is negligible compared to the
benefit of flexibility.

The KDB support in `GENERIC` is enough for most panics. We will use
`GDB` in Section 7 when we talk about remote debugging.

### KDB_UNATTENDED

One more option worth mentioning is `KDB_UNATTENDED`. This causes the
kernel to skip entry into the debugger on panic and go straight to the
dump and reboot. In production systems without anyone watching the
console, this is a sensible default; there is no point waiting
indefinitely for a debugger interaction that will never come. In
development, you usually want the opposite: to stay in `DDB` after a
panic so you can investigate before the state is lost to reboot. Set
this option via the `debug.debugger_on_panic` sysctl at runtime, or
`options KDB_UNATTENDED` in a kernel config.

### CTF and Debug Info Paths

One last piece of the debug environment is CTF, the Compact C Type
Format. CTF is a compressed representation of type information that
DTrace uses to understand kernel structures. `GENERIC` includes
`options DDB_CTF`, which tells the build to generate CTF information
for the kernel. On a debug kernel, CTF information lets DTrace print
structure fields by name instead of hex offsets, which makes its
output dramatically more useful.

You can confirm CTF is present with `ctfdump`:

```console
# ctfdump -t /boot/kernel/kernel | head
```

If this produces output, the kernel has CTF. If not, either the build
did not include `DDB_CTF` or the CTF generation tool (`ctfconvert`)
was not installed. In FreeBSD 14.3 both are standard.

For modules, you need to build with `WITH_CTF=1` in your environment
(or passed to `make`) to get CTF information for the module. This is
what lets DTrace understand the structures your driver defines.

### Confirming Your Debug Kernel

When you first boot into a debug kernel, spend a minute verifying that
the options you care about are actually active. Useful sysctls:

```console
# sysctl debug.kdb.current_backend
debug.kdb.current_backend: ddb
# sysctl debug.kdb.supported_backends
debug.kdb.supported_backends: ddb gdb
# sysctl debug.debugger_on_panic
debug.debugger_on_panic: 1
# sysctl debug.ddb.
debug.ddb.capture.inprogress: 0
debug.ddb.capture.bufsize: 0
...
```

If these print sensible values, your debug kernel is wired up. If
`debug.kdb.supported_backends` lists only `ddb` but you expected
`gdb`, something in your configuration is off. Go back and check that
`options GDB` is in your kernel config or in `std.debug`.

### Running on Top of the Debug Kernel

With the debug kernel running, the rest of the chapter's techniques
become available. `KASSERT` actually fires. `WITNESS` actually
complains about lock orders. `DDB` is there when you press the
break-to-debugger sequence. Crash dumps include full debug information
that `kgdb` can use to show source lines. You have moved from a
kernel that trusts the driver to a kernel that actively helps you
prove your driver is correct.

A small but meaningful consequence of running a debug kernel for
driver development is that you will see bugs in your driver much
earlier, before they reach the field, and you will have an easier
time fixing them when they do appear. The discipline of always
developing on a debug kernel, even when you are only writing simple
code, is one of the habits that separates casual hobbyist drivers
from drivers that are trustworthy enough for serious use.

With the environment set up, we can move on to the next class of
tool: tracing. Unlike assertions, which catch failures, tracing
records what happens so you can understand the shape of a bug even
when it is not crashing. That is the topic of Section 5.

## 5. Tracing Driver Behavior: DTrace, ktrace, and ktr(9)

Assertions catch what is wrong. Tracing shows what is happening. When a
driver misbehaves without crashing, or when you need to understand the
precise order of events across multiple threads, tracing is usually the
right tool. FreeBSD offers three complementary tracing facilities for
kernel code: DTrace, `ktrace(1)`, and `ktr(9)`. Each has a different
sweet spot, and a driver author should know when to reach for which.

Chapter 33 introduced DTrace as a performance measurement tool. Here we
come back to it as a correctness debugging tool, because the same
framework that can aggregate hot functions can also follow a bug
through the kernel. We will also meet `ktr(9)`, the lightweight
in-kernel tracing ring, and `ktrace(1)`, which traces system calls from
user space.

### DTrace for Correctness Debugging

DTrace is FreeBSD's production-grade dynamic tracing framework. It
works by letting you attach small scripts to probe points throughout
the kernel. A probe is a named point in the code that can be
instrumented. When a probe fires, the script runs. If the script has
useful things to record, it records them; if not, the probe is
effectively free.

Chapter 33 used DTrace with the `profile` provider for CPU sampling. In
this chapter we will use different providers for different purposes:
`fbt` (function boundary tracing) to follow entry and exit from
functions, `sdt` (statically defined tracing) to fire at explicitly
placed probe points in our driver, and `syscall` to watch user-kernel
transitions.

Let us take these in turn.

### The fbt Provider

The `fbt` provider gives you a probe at every function entry and exit
in the kernel. To list all fbt probes in our driver:

```console
# dtrace -l -P fbt -m bugdemo
```

Each function produces two probes, an `entry` and a `return`. You can
attach actions to either. A common first step in debugging a new bug
is simply to see what functions are being called:

```console
dtrace -n 'fbt::bugdemo_*:entry { printf("%s\n", probefunc); }'
```

This prints every entry into any function in the `bugdemo` module,
showing the order in which they are called. If you suspect a
particular function is or is not being reached, this one-liner will
tell you immediately.

For a deeper view, you can also record arguments. `fbt` probe arguments
are the function's parameters, accessible as `arg0`, `arg1`, etc.:

```console
dtrace -n 'fbt::bugdemo_ioctl:entry { printf("cmd=0x%lx\n", arg1); }'
```

Here `arg1` is the second parameter to `bugdemo_ioctl`, which is the
`ioctl` command number. You can watch the stream of ioctl calls in
real time.

Exit probes let you see return values:

```console
dtrace -n 'fbt::bugdemo_ioctl:return { printf("rv=%d\n", arg1); }'
```

On a return probe, `arg1` is the return value. A stream of `rv=0`
entries confirms success. A sudden `rv=22` (which is `EINVAL`) tells
you the driver rejected a call. By combining entry and return probes
you can match each call with its result.

### SDT Probes: Statically Defined Tracing

`fbt` is flexible but gives you function boundaries, not semantic
events. If you want a probe that fires at a specific point inside a
function, representing a specific event, you use SDT. SDT probes are
placed explicitly in the code. They cost essentially nothing when
disabled and produce exactly the information you want when enabled.

In FreeBSD 14.3, SDT probes are defined using macros from
`/usr/src/sys/sys/sdt.h`. The key macros are:

```c
SDT_PROVIDER_DEFINE(bugdemo);

SDT_PROBE_DEFINE2(bugdemo, , , cmd__start,
    "struct bugdemo_softc *", "int");

SDT_PROBE_DEFINE3(bugdemo, , , cmd__done,
    "struct bugdemo_softc *", "int", "int");
```

The naming convention is `provider:module:function:name`. The leading
`bugdemo` is the provider. The two empty strings are the module and
function, which we leave empty for a driver-level probe. The trailing
name identifies the probe. The underscore-underscore convention in
probe names is a DTrace idiom that becomes a dash in the user-facing
name.

The numeric suffix on `SDT_PROBE_DEFINE` indicates how many arguments
the probe takes. The string arguments are the C type names of those
arguments, which DTrace uses for display.

To fire a probe in the driver:

```c
static void
bugdemo_process(struct bugdemo_softc *sc, struct bugdemo_command *cmd)
{
        SDT_PROBE2(bugdemo, , , cmd__start, sc, cmd->op);

        /* ... actual work ... */

        SDT_PROBE3(bugdemo, , , cmd__done, sc, cmd->op, error);
}
```

`SDT_PROBE2` and `SDT_PROBE3` fire the corresponding probe with the
given arguments.

Now in DTrace you can watch these probes:

```console
dtrace -n 'sdt:bugdemo::cmd-start { printf("op=%d\n", arg1); }'
```

Notice the dash in `cmd-start`: DTrace converts the double
underscore in the name to a dash in the probe spec. `arg0` is the
softc, `arg1` is the op.

SDT probes are particularly useful for state transitions. If your
driver has three states and you want to follow the sequence, define
probes at each transition and aggregate on them:

```console
dtrace -n 'sdt:bugdemo::state-change { @[arg1, arg2] = count(); }'
```

This counts how often each (from_state, to_state) pair occurs,
giving a distribution of the state machine's behavior during your
workload.

### Following a Bug With DTrace

Consider a scenario. The `bugdemo` driver sometimes returns `EIO`
to user space, but you cannot tell from user space which code path
produced that error. With DTrace, you can walk backwards from the
return to the origin:

```console
dtrace -n '
fbt::bugdemo_ioctl:return
/arg1 == 5/
{
        stack();
}
'
```

`arg1 == 5` checks for the return value 5, which is `EIO`. When the
return matches, `stack()` prints the kernel stack trace at the point
of the return. This tells you exactly which code path returned the
error.

A more sophisticated version records the start time and duration:

```console
dtrace -n '
fbt::bugdemo_ioctl:entry
{
        self->start = timestamp;
}

fbt::bugdemo_ioctl:return
/self->start != 0/
{
        @latency["bugdemo_ioctl", probefunc] = quantize(timestamp - self->start);
        self->start = 0;
}
'
```

This produces a latency distribution for the ioctl, which is useful
when a bug manifests as unusual latency. The `self->` notation is
DTrace's thread-local storage, scoped to the current thread.

These scripts are not full programs; they are small observations
that you iterate on. The cycle of "add a probe, run the workload,
read the output, refine the probe" is one of DTrace's strengths. A
full debugging session might go through a dozen variations of a
script before the shape of the bug becomes clear.

### Understanding ktrace(1)

`ktrace(1)` is a different beast. It traces system calls made by a
user-space process, along with their arguments and return values. It
is not about the kernel's internal behavior; it is about the
interface between user space and the kernel. When a user-space tool
is using a driver and something odd happens, `ktrace(1)` is often
the first tool to reach for, because it shows exactly what the
process asked of the kernel.

To trace a program:

```console
# ktrace -t cnsi ./test_bugdemo
# kdump
```

`ktrace` writes a binary trace file (`ktrace.out` by default), and
`kdump` renders it as human-readable text. The `-t` flags select
what to trace: `c` for system calls, `n` for namei (pathname
lookups), `s` for signals, `i` for ioctls. For driver debugging, `i`
is the most directly useful.

Sample output:

```text
  5890 test_bugdemo CALL  ioctl(0x3,BUGDEMO_TRIGGER,0x7fffffffe0c0)
  5890 test_bugdemo RET   ioctl 0
  5890 test_bugdemo CALL  read(0x3,0x7fffffffe0d0,0x100)
  5890 test_bugdemo RET   read 32/0x20
```

The process made two syscalls. An ioctl on file descriptor 3 with
command `BUGDEMO_TRIGGER` succeeded. A read on the same fd returned
32 bytes. If the test fails, the trace tells you exactly what the
kernel was asked for and what it returned.

Notice that `ktrace(1)` does not show internal kernel behavior. For
that you need DTrace or `ktr(9)`. But `ktrace(1)` is the canonical
way to see user-space interactions, and combined with DTrace it
gives a complete picture.

`ktrace(1)` can also be attached to a running process:

```console
# ktrace -p PID
```

and detached:

```console
# ktrace -C
```

For a driver that is being used by a long-running daemon, this is
more practical than restarting the daemon under `ktrace`.

### ktr(9): Lightweight In-Kernel Tracing

`ktr(9)` is FreeBSD's in-kernel trace ring. It is a ring buffer of
trace entries that code can write to cheaply. Each entry includes a
timestamp, the CPU number, the thread pointer, a format string, and
up to six arguments. The ring is sized by the `KTR_ENTRIES` kernel
config option, and its contents can be dumped from `DDB` or from user
space.

`ktr(9)` is the right tool when you need very fine-grained
information about timing or ordering, especially in an interrupt
context where `printf` is too slow. Because each entry is small and
writes are lock-free, `ktr(9)` can be used in hot paths without
distorting the behavior you are trying to observe.

The macros are in `/usr/src/sys/sys/ktr.h`. The common ones are
`CTR0` through `CTR6`, varying by how many arguments follow the
format string. Each macro takes a class mask as its first argument,
then the format string, then the values:

```c
#include <sys/ktr.h>

static void
bugdemo_process(struct bugdemo_softc *sc, struct bugdemo_command *cmd)
{
        int error;

        CTR2(KTR_DEV, "bugdemo_process: sc=%p op=%d", sc, cmd->op);
        /* ... */
        CTR1(KTR_DEV, "bugdemo_process: done rv=%d", error);
}
```

`CTR2` writes a two-argument entry to the trace ring. `KTR_DEV` is
the class mask: the kernel decides at runtime whether entries for a
given class are recorded, based on `debug.ktr.mask`. At compile time,
`KTR_COMPILE` (the set of classes actually compiled in) controls
which calls are emitted at all. Classes that are not in `KTR_COMPILE`
disappear entirely, so you can leave calls in the source permanently
without paying for them when the class is disabled.

The classes are defined in `/usr/src/sys/sys/ktr_class.h`. Common
ones include `KTR_GEN` (general-purpose), `KTR_DEV` (device
drivers), `KTR_NET` (networking), and many more. For a driver, you
would typically pick `KTR_DEV` or, in larger subsystems, define a new
bit alongside the existing ones.

To enable and view the trace ring:

```console
# sysctl debug.ktr.mask=0x4          # enable KTR_DEV (bit 0x04)
# sysctl debug.ktr.entries
```

and dump it with:

```console
# ktrdump
```

`ktrdump(8)` reads the kernel's trace buffer through `/dev/kmem`
and formats it. The output is a time-ordered list of entries with
timestamps, CPUs, threads, and messages.

The beauty of `ktr(9)` is its low overhead. A trace entry is
essentially a handful of memory writes. You can leave them in the
code, compile them in to a debug kernel, and enable them at runtime
when you need them. They are especially valuable for interrupt
handler debugging, where `printf` would add milliseconds of delay
and actually change the behavior being measured.

### When to Use Which

With three tracing tools, the question is which to reach for first.

Use DTrace when the bug is about what the kernel is doing, when you
need to aggregate across many events, when you need filtering, or
when the probes can be placed dynamically. DTrace is the most
capable of the three, but it requires a running kernel and a
reasonable rate of probe firings.

Use `ktrace(1)` when the bug is about what user space is asking of
the kernel, when the symptom is a wrong return value or a sequence
of syscalls that does not match expectations. `ktrace(1)` is simple,
fast, and immediately shows the kernel-user boundary.

Use `ktr(9)` when you need the lowest possible overhead, when the
code you are tracing is in an interrupt handler, or when you want
persistent trace points that can be turned on in production with
minimal risk. `ktr(9)` is the most primitive of the three but also
the most durable.

In practice, a debugging session often uses two or three. You might
start with `ktrace(1)` to see the syscall sequence, then add DTrace
probes to narrow down which driver function is misbehaving, then
add `ktr(9)` entries to nail down timing in the interrupt path.
Each tool answers a different question, and a full picture often
requires all three.

### Tracing and Production

A quick word on production. DTrace is production-safe in most
configurations; its design specifically includes safeguards against
infinite loops and against crashing the kernel from a bad probe.
You can run DTrace on a busy production server without bringing it
down. `ktr(9)` is also production-safe, with the caveat that
enabling verbose classes costs CPU. `ktrace(1)` writes to a file
and can grow unbounded if left unchecked; use with size limits.

Contrast these with crash dumps, `DDB`, and `memguard(9)`, which
are development-only tools. The distinction matters because
Section 7 will return to the question of what is safe to do on a
production machine. For now, remember that tracing is among the
lightest-touch techniques we have, and that is why it is often the
right first step when diagnosing a live problem.

With tracing in hand, we can turn to the bugs that tracing and
assertions tend to miss: memory bugs that corrupt state without
producing clear symptoms until much later. That is the domain of
Section 6.

## 6. Hunting Memory Leaks and Invalid Memory Accesses

Memory bugs are the most treacherous bugs a driver author faces. They
are rarely visible at the moment they happen. They corrupt state
quietly, accumulate across many runs, and manifest much later in
ways that seem completely unrelated to the original defect. A
use-after-free might surface as a corrupted structure in a different
subsystem. A buffer overrun might overwrite the next allocation and
show up as a bogus field value several minutes later. A small leak
might drain memory over days until the kernel finally refuses an
allocation and the system locks up.

FreeBSD provides a family of tools for these bugs: `memguard(9)` for
use-after-free and modify-after-free detection, `redzone` for buffer
overruns, guard pages in the VM layer, and sysctls that expose the
state of the kernel memory allocator. Used together, they can
transform a class of bug that was nearly impossible to find into a
class that crashes immediately at the moment of misuse.

### Understanding the Kernel Memory Allocator

To use these tools effectively we need a rough mental model of how
the kernel allocates memory. FreeBSD has two primary allocators,
both in `/usr/src/sys/kern/`:

`kern_malloc.c` implements `malloc(9)`, the general-purpose allocator.
It is a thin wrapper over UMA, the Universal Memory Allocator, with
accounting by malloc type. Every allocation is charged to a
`struct malloc_type` (commonly declared with `MALLOC_DEFINE(9)`),
which lets the kernel track how much memory each subsystem has
used.

`subr_vmem.c` and `uma_core.c` implement the lower layers. UMA is a
slab allocator: it maintains per-CPU caches and central slabs, so
most allocations are very fast and contention-free. When a driver
calls `malloc(9)` or `uma_zalloc(9)`, what actually happens depends
on size, zone configuration, and cache state.

For debugging, the practical consequence is that a corrupted
allocation might look different depending on where it landed. The
same bug can produce different symptoms on different kernels or
under different loads, simply because the underlying memory layout
differs.

### sysctl vm and kern.malloc: Observing Allocation State

Before reaching for memory debuggers, a useful first step is to
look at the live state of the allocator. Two sysctls are
particularly useful:

```console
# sysctl vm.uma
# sysctl kern.malloc
```

The first dumps per-zone statistics for UMA: how many items are
allocated, how many are free, how many failures have occurred, and
how many pages each zone is using. The output is long, but it is
text-searchable. If you suspect a leak in a particular driver type,
find its zone in the output and watch it grow.

The second dumps per-type statistics for `malloc(9)`. Each entry
shows the type name, the number of requests, the amount allocated,
and the high-water mark. Running the driver under a workload and
comparing before and after is a simple leak-detection technique
that requires no special tooling:

```console
# sysctl kern.malloc | grep bugdemo
bugdemo:
        inuse = 0
        memuse = 0K
```

Run a workload, re-query, and compare. If `inuse` rises and does
not fall after the workload finishes, something is leaking.

The related `vmstat(8)` command has a `-m` flag that presents the
same `malloc(9)` state in a more compact form:

```console
# vmstat -m | head
         Type InUse MemUse HighUse Requests  Size(s)
          acl     0     0K       -        0  16,32,64,128,256,1024
         amd6     4    64K       -        4  16384
        bpf_i     0     0K       -        2
        ...
```

For ongoing monitoring during a workload:

```console
# vmstat -m | grep -E 'bugdemo|Type'
```

gives you a periodic snapshot of a single type's footprint.

### memguard(9): Hunting Use-After-Free

`memguard(9)` is a special allocator that can replace `malloc(9)`
for a specific type. The idea is simple: instead of returning a
piece of memory from a slab, it returns memory backed by dedicated
pages. When the memory is freed, the pages are not returned to the
pool; they are unmapped, so any subsequent access faults. And the
pages around the allocation are left inaccessible, so any read or
write past the end of the allocation also faults. This turns
use-after-free, buffer overrun, and buffer underrun bugs from
silent corruptors into immediate panics with a backtrace that
points directly at the misuse.

The cost is that each allocation now costs at least one full page
of virtual memory (plus management overhead), and every free
burns an unmapped page. For that reason, `memguard(9)` is typically
turned on for a single malloc type at a time, not for everything.

The configuration involves two steps. First, the kernel must be
built with `options DEBUG_MEMGUARD`, which `std.debug` does not
enable by default. You add it to your kernel config:

```text
include "std.debug"
options DEBUG_MEMGUARD
```

and rebuild.

Second, at runtime you tell `memguard` which malloc type to guard:

```console
# sysctl vm.memguard.desc=bugdemo
```

From that moment on, every allocation of type `bugdemo` goes
through `memguard`. Note the type string matches the name passed to
`MALLOC_DEFINE(9)` in the driver source. Typos here silently do
nothing.

You can also use `vm.memguard.desc=*` to guard everything, but as
noted, this is expensive. For a targeted bug hunt, guard only the
type you suspect.

### A memguard Session in Action

Imagine `bugdemo` has a use-after-free bug: the driver frees a
buffer when its ioctl completes, but then an interrupt handler
reads from the same buffer a moment later. Without `memguard`, the
read usually succeeds because the slab allocator has not yet
reused the memory, or it returns some unrelated data that happens
to have replaced the buffer. The driver gets plausible-but-wrong
output, which corrupts some later state, which manifests much
later as a subtle bug.

With `memguard` enabled for the driver's malloc type, the same
sequence of events fires a page fault the instant the interrupt
handler dereferences the freed pointer. The fault produces a
panic with a backtrace through the interrupt handler. The panic
message identifies the faulting address as inside a `memguard`
region, and `kgdb` on the dump shows you exactly which function
dereferenced the freed memory.

Compare that to the days of detective work the bug would have
demanded without `memguard`, and you understand why this tool is
so valuable.

### redzone: Buffer Overrun Detection

`memguard` is heavyweight. For the narrower case of buffer
overruns and underruns, FreeBSD offers `DEBUG_REDZONE`, a lighter
debugger that adds a few guard bytes before and after each
allocation. When the allocation is freed, the guard bytes are
checked. If they have been modified, `redzone` reports the
corruption, including the stack at the time of allocation.

`DEBUG_REDZONE` is added to the kernel config:

```text
options DEBUG_REDZONE
```

Unlike `memguard`, it is always active once compiled in, and it
applies to all allocations. Its overhead is memory, not time:
each allocation grows by a few bytes.

`redzone` does not catch use-after-free, because the memory it
guards is still within the original allocation. It does catch
writes that step past the intended buffer, which is a common class
of bug in drivers that compute offsets from user-provided sizes.

### Guard Pages in the VM Layer

A third mechanism, available independently of `memguard` and
`redzone`, is the use of guard pages around critical kernel
allocations. The VM system supports allocating a memory region
with inaccessible pages placed before and after it. Kernel thread
stacks use this mechanism: the page below each stack is
unmapped, so a runaway recursion faults instead of overwriting
the adjacent allocation.

Drivers that allocate stack-like objects can use `kmem_malloc(9)`
with the right flags, or can set up guard pages manually through
`vm_map_find(9)`. In practice, driver code rarely does this
directly; the mechanism is more commonly used by subsystems that
manage their own memory regions. But it is useful to know that
the capability is there, because you may see it in kernel
messages and want to understand what it means.

### Leak Detection in Practice

Leaks are the quietest class of memory bug. They produce no
crashes, no faults, no assertions. The only symptom is that
memory usage grows over time. FreeBSD gives you a few tools for
finding them.

The first, as we saw, is `kern.malloc`. Snapshot before, run the
workload, snapshot after, and look for types whose `inuse` grew
and did not shrink. This is crude but effective for driver leaks.

The second is to add counters to your driver. If every allocation
bumps a `counter(9)` and every free decrements it, a lingering
positive value at unload time tells you the driver leaked
something. A companion sysctl exposes the counter for inspection:

```c
static counter_u64_t bugdemo_inflight;

/* in attach: */
bugdemo_inflight = counter_u64_alloc(M_WAITOK);

/* in allocation path: */
counter_u64_add(bugdemo_inflight, 1);

/* in free path: */
counter_u64_add(bugdemo_inflight, -1);

/* in unload: */
KASSERT(counter_u64_fetch(bugdemo_inflight) == 0,
    ("bugdemo: %ld buffers leaked at unload",
     (long)counter_u64_fetch(bugdemo_inflight)));
```

This idiom, counting inflight allocations explicitly, is useful
in any subsystem that owns a pool of objects. The assertion at
unload time fires if anything leaked, giving you an immediate
report at the moment you notice the leak, not hours later.

The third tool is DTrace. If you know which malloc type is
leaking but not why, a DTrace script can track every allocation
and every free, accumulating the difference per stack trace:

```console
dtrace -n '
fbt::malloc:entry
/arg1 == (uint64_t)&M_BUGDEMO/
{
        @allocs[stack()] = count();
}

fbt::free:entry
/arg1 == (uint64_t)&M_BUGDEMO/
{
        @frees[stack()] = count();
}
'
```

After a workload, comparing the two aggregations often reveals a
code path that allocates but never frees. The stack traces point
you directly to the offending call sites.

### When Memory Bugs Hide

Sometimes memory bugs do not match any of these patterns. The
symptom is a panic in an unrelated subsystem, with a backtrace
that seems impossible. The driver looks fine on review; its
allocations and frees appear balanced. Yet the kernel keeps
crashing with messages about corrupt lists or invalid pointers.

The usual cause in these cases is that the driver writes past
the end of a buffer into the next allocation. The next
allocation belongs to someone else; your overrun silently
corrupts that other subsystem's data. The crash happens when
the other subsystem next touches its corrupted data, which
might be soon or might be much later.

For this class of bug, the diagnostic is to enable
`DEBUG_REDZONE` and watch for warnings. When `redzone` reports
that guard bytes have been modified, the stack trace it prints
for the allocation is the allocation in question, and the code
that overran it is the code that was writing into that
allocation. `redzone`'s report tells you both ends of the bug.

Another trick is to enable `MALLOC_DEBUG_MAXZONES=N` with a
large N. This spreads allocations across more zones, so a
driver's allocations are less likely to share a zone with
unrelated subsystems. If the symptom disappears or changes with
more zones, it is a strong hint that the bug involves cross-
zone corruption.

### Working With DDB on Memory Bugs

When the kernel panics on a memory bug, entering `DDB` can help
narrow down the cause. Useful `DDB` commands include:

- `show malloc` dumps `malloc(9)` state.
- `show uma` dumps UMA zone state.
- `show vmochk` runs a consistency check on the VM object tree.
- `show allpcpu` shows per-CPU state.

These commands produce output that helps correlate a crash with
the state of the allocators at the moment of the crash. They do
not replace `kgdb` analysis, but they can be faster to consult
when you are already in `DDB`.

### Reality Check on Memory Debuggers

`memguard`, `redzone`, and their relatives are effective. They are
also disruptive. They change allocator behavior, they slow the
kernel down, and some of them consume memory aggressively.
Leaving them on in production is not a good idea.

The right use is targeted. When a bug presents, enable the
appropriate debugger, reproduce the bug, capture the evidence,
and then turn the debugger off. Most of your driver development
happens on a kernel with `INVARIANTS` and `WITNESS` but without
`DEBUG_MEMGUARD`. `DEBUG_MEMGUARD` comes out when you are
actively chasing a memory bug and goes away when you are done.

One last consideration. Some memory debuggers, notably
`memguard`, change the observable behavior of the allocator in
ways that can mask bugs. If a driver depends on two allocations
being adjacent in memory (which it never should, but sometimes
does as an accidental invariant), `memguard` will break that
dependency and make the bug go away. This does not mean the bug
is fixed; it means the bug is now latent. Always re-test
without `memguard` after a fix, to make sure the fix is real and
not an artifact of the debugger's presence.

### Wrapping Up the Memory Section

Memory bugs are the quiet killers of driver code. The patience
to find them is built on a small, focused set of tools.
`memguard(9)` catches use-after-free and buffer overrun
directly. `redzone` catches overruns with less overhead. The
`kern.malloc` and UMA sysctls expose the allocator state that
normal code cannot see. And the discipline of counting inflight
allocations in your own driver catches leaks at unload time.
Put these together, and a class of bug that used to take days
to find can be made to announce itself in minutes.

With the major technical tools now covered, we turn to the
discipline of using them safely, particularly on systems where
other people are watching. That is the content of Section 7.

## 7. Safe Debugging Practices

Every tool we have learned in this chapter has a cost, and every cost
has a context in which it is acceptable. A debug kernel on a
development VM is a small price for catching bugs early. The same
debug kernel on a production server is a disaster in slow motion.
Knowing which tools to reach for in which context is part of what
separates a competent driver author from a dangerous one.

This section collects the practices that keep you out of trouble:
the conventions for using each tool safely, the signs that you are
about to make a mistake, and the mindset that helps you work with
discipline when the stakes are high.

### The Development vs Production Divide

The most important distinction in safe debugging is between a
development system, where you can crash the kernel freely, and a
production system, where you cannot.

On a development system, everything in this chapter is fair game.
Deliberately trigger panics. Enable `DEBUG_MEMGUARD`. Load and
unload the driver repeatedly. Attach `kgdb` to the live kernel.
Run DTrace scripts that collect megabytes of data. The worst that
can happen is that you reboot the VM, which is measured in
seconds.

On a production system, the opposite posture applies. You do not
enable debug options unless you have a specific, targeted reason.
You do not load experimental drivers. You do not run DTrace
scripts that might destabilize the probe framework. You do not
break into `DDB` on a live system. Every intervention is
preceded by a clear answer to "what do I do if this goes wrong?"

The discipline of keeping these two environments separate is the
single most effective way to avoid accidentally breaking
production. Have a development VM, keep the production kernel on
a different partition, and never confuse the two.

### What Is Safe in Production

A surprising amount of the debugging toolkit is actually safe in
production, if used carefully. Here is a partial list.

DTrace scripts are generally safe in production. The DTrace
framework is specifically designed with safety guarantees: probe
actions cannot loop indefinitely, cannot allocate arbitrary
memory, and cannot dereference arbitrary pointers without
falling into a well-defined recovery path. You can run DTrace
aggregations on a busy server without bringing it down. The
caveats are that very high-frequency probes can consume
significant CPU (a probe on every packet in a network driver is
unlikely to be free), and that DTrace output can fill filesystem
space if not rate-limited.

`ktrace(1)` on a specific process is safe, though it writes to a
file that grows unbounded. Set a size limit or watch the file.

`ktr(9)` is safe if the relevant classes are already compiled
in. Enabling a class through `sysctl debug.ktr.mask=` is safe.
Compiling in a new class requires a kernel rebuild, which is
a development activity.

Reading sysctls is always safe. `kern.malloc`, `vm.uma`,
`hw.ncpu`, `debug.kdb.*`, and all the others expose state
without changing anything. A production system with a sick
driver can be interrogated extensively through sysctl alone.

### What Is Unsafe in Production

A shorter list, but an important one.

Panics are unsafe. Deliberately crashing a production server is
only acceptable as a last resort when the server is already
irrecoverably damaged and a dump is the best path to
understanding the cause. `sysctl debug.kdb.panic=1` triggers an
immediate panic and dump. Do not do this lightly.

Breaking into `DDB` on a production console is unsafe. The
entire kernel stops while you are in `DDB`. User processes
freeze. Network connections time out. Real-time work halts.
Unless the alternative is worse (often the case during a
catastrophic crash), stay out of `DDB` on production.

`DEBUG_MEMGUARD` on all allocation types is unsafe. Memory use
balloons. Performance drops sharply. Memory-intensive workloads
may fail outright. If you absolutely must use `memguard` in
production, scope it to one malloc type at a time and monitor
memory use.

Loadable kernel modules are a risk. Loading or unloading a
module touches kernel state. A buggy module can crash the
kernel at load time, unload time, or any time in between. On
production, load only modules that have been tested in the
development environment against the same kernel.

Overly aggressive DTrace scripts can destabilize the system.
Aggregations that record stack traces produce memory pressure.
Probes with side effects can interact with the workload in
unexpected ways. Run DTrace scripts with explicit time limits
and review the aggregations carefully before leaving them
running.

### Capturing Evidence on Production Systems

When something goes wrong in production and the bug is rare or
difficult to reproduce in development, the challenge is to
capture enough evidence to diagnose the problem, without
destabilizing the running service. Several strategies help.

First, start with passive observation. `sysctl`, `vmstat -m`,
`netstat -m`, `dmesg`, and the various `-s` system statistics
commands can be run while the system is live and cost almost
nothing. If the bug produces symptoms visible in these reports,
capture the reports periodically.

Second, use DTrace with strict bounds. A script that runs for
sixty seconds and exits produces a snapshot without leaving a
standing risk. Aggregations are particularly well suited to
this style: they collect statistics over a window, print them,
and stop.

Third, if a crash dump is needed and the system has not yet
crashed, the safest approach is to wait for the crash. Modern
dump mechanisms are designed to capture the kernel state at the
moment of panic; a dump triggered manually is useful only when
you know the system is already irrecoverable.

Fourth, when a crash does happen, work on the dump, not the
live system. A reboot into a fresh kernel restores service,
while the dump remains available for offline analysis at
leisure. The discipline of "reboot fast, analyze later" is
often the right trade-off on production hardware.

### Using log(9) Over printf for Diagnostics

Throughout the chapter we have used `printf` as a shorthand for
kernel-side logging, which is how it is commonly presented in
textbooks. On a production system you should prefer `log(9)`,
which writes through the `syslogd(8)` facility rather than
directly to the console. The reasons are practical: console
output is unbuffered and slow, `log(9)` is rate-limited and
buffered, and `log(9)` ends up in `/var/log/messages` where it
is available to log-analysis tools.

The API is in `/usr/src/sys/sys/syslog.h` and
`/usr/src/sys/kern/subr_prf.c`. Usage:

```c
#include <sys/syslog.h>

log(LOG_WARNING, "bugdemo: unexpected state %d\n", sc->state);
```

The priority level (`LOG_DEBUG`, `LOG_INFO`, `LOG_NOTICE`,
`LOG_WARNING`, `LOG_ERR`, `LOG_CRIT`, `LOG_ALERT`, `LOG_EMERG`)
lets `syslogd` route messages differently.

A common extension is rate-limited logging, so a sick driver
does not flood `/var/log/messages` with millions of entries per
second. FreeBSD provides the `ratecheck(9)` primitive that you
can wrap around your own `log` calls:

```c
#include <sys/time.h>

static struct timeval lastlog;
static struct timeval interval = { 5, 0 };   /* 5 seconds */

if (ratecheck(&lastlog, &interval))
        log(LOG_WARNING, "bugdemo: error (rate-limited)\n");
```

`ratecheck(9)` returns nonzero once per interval, suppressing
repeated logs in between. The technique is essential for any
driver that might observe the same error repeatedly.

### Do Not Mix Debug and Release Kernels in One Fleet

A subtle trap is to run a mix of debug and release kernels in a
production fleet. The intuition is that debug kernels give you
better diagnostics if a bug appears. The reality is that a debug
kernel performs noticeably worse than a release kernel, has
different memory usage, and can exhibit different timing. If a
bug is sensitive to those factors (and many concurrency bugs
are), running mixed kernels guarantees that your reproduction
environment does not match your production environment.

The right approach is uniform: either the whole fleet runs
release kernels (and you debug on development hardware), or the
whole fleet runs debug kernels (and you accept the overhead).
Mixed deployments are a third option only for very controlled
experiments.

### Working With a Recovery Plan

Before running any risky debugging action, know your recovery
plan. If the system hangs, how will you recover? Is there an IPMI
interface that can issue a hardware reset? Is there a second
administrator who can cycle the power if needed? How much data
loss is acceptable?

A good recovery plan is two steps. First, get the system back up
quickly. Second, capture the evidence (dump, logs) for offline
analysis. These two steps often involve different people or
different timescales, and thinking through both in advance
prevents panic in the moment.

### Keep a Debug Journal

When debugging a hard bug, a written record is invaluable. Each
entry should include:

- What hypothesis you were testing.
- What action you took.
- What result you observed.
- What the result rules in or out.

This sounds pedantic but is genuinely useful. A long debugging
session involves dozens of micro-hypotheses, and losing track of
which ones you have tested wastes enormous time. The written
record also helps when you come back to the bug after a weekend,
or when you hand it off to a colleague.

For a driver bug that goes across multiple systems, a shared
record (bug tracker, wiki, or an internal ticket) is even more
valuable. Each person who touches the bug can see what the
others have already tried, and no one reruns the same experiment
twice.

### Practicing on Your Own Drivers

One habit that pays off over time is to keep a deliberately
buggy version of your driver around for practice. Every time
you find an interesting bug in real work, add a variant of it to
your practice driver. Then, periodically, run through the
practice driver with fresh eyes and make sure you can still
find the bugs using the tools of this chapter. This builds
muscle memory that is invaluable when a real bug appears under
time pressure.

The `bugdemo` driver we have been using throughout this chapter
is one starting point for such a practice driver. Fork it, add
your own bugs, and use it to stay sharp.

### Knowing When to Stop

A final piece of safe debugging wisdom is knowing when to stop.
Not every bug needs to be hunted to the last instruction. If a
bug is rare, has a workaround, and the cost of finding its root
cause is measured in days, there is sometimes a case for
documenting the workaround and moving on. This is a judgment
call, not a rule, but the ability to make the call is part of
professional maturity.

The opposite mistake (declaring victory too early, accepting a
surface fix that does not address the underlying defect) is
also common. The symptom is a bug that keeps coming back in
new forms. When a "fix" does not produce a stable result,
something deeper is wrong, and more investigation is called for.

Between these extremes is the healthy zone where you invest
time proportional to the importance of the bug. Kernel
development rewards patience, but it also rewards pragmatism.
The tools of this chapter exist to make the investment
efficient, not to make every bug a multi-day research project.

With safe practices established, we can turn to the last major
topic of the chapter: what to do after a debugging session
that found something serious, and how to make the driver more
resilient against the next time something similar goes wrong.

## 8. Refactoring After a Debugging Session: Recovery and Resilience

A hard-won debugging victory is not the end of the work. Finding
the bug is finding evidence. The real question is: what does
the evidence tell us about the driver, and how should the driver
change in response?

A common failure mode is to patch the immediate symptom and move
on. The patch makes the test pass, the crash stop, the corruption
vanish. But the underlying weakness that allowed the bug in the
first place is still there, lurking. The next subtle change in
surrounding code, or the next new environment, finds the same
weakness and produces the next bug.

This section is about resisting that failure mode. We will walk
through a small set of techniques for using a debugging result to
strengthen the driver, not just to fix the particular bug.

### Reading the Bug as a Message

Every bug carries a message about the design. A use-after-free
says "the driver's ownership model for this buffer is unclear."
A deadlock says "the driver's lock order is not explicitly
documented or enforced." A memory leak says "the driver's
lifecycle for this object is incomplete." A panic in `attach`
says "the driver's error recovery during initialization is
weak." A race condition says "the driver's assumptions about
thread context are not strict enough."

When you find a bug, spend a few minutes asking what the bug is
telling you about the design. The specific defect is usually a
symptom of a broader pattern, and understanding the pattern
makes future bugs easier to prevent.

### Strengthening Invariants

One concrete response to a bug is to add assertions that would
have caught it sooner. If a use-after-free was the bug, add a
`KASSERT` somewhere in the path that confirms the buffer is
still valid when used. If a lock order was violated, add a
`mtx_assert(9)` at the point where the violation occurred. If
a structure field was corrupted, add a `CTASSERT` on its
alignment or a runtime check on its value.

The goal is not to duplicate every check with an assertion, but
to convert each bug into one or two new invariants that make the
same class of bug impossible in the future. Over time, the
driver accumulates a set of defensive checks that reflect its
actual behavior, documented in code rather than in your head.

### Documenting the Ownership Model

Another common response is to clarify documentation. Many bugs
arise because ownership of a resource (who allocated it, who is
responsible for freeing it, when is it safe to access) is
implicit. Writing a few lines of comment that explicitly state
the ownership rules makes the rules visible to the next reader,
and often forces you to confront cases where the rules were not
actually consistent.

For example, a comment like:

```c
/*
 * bugdemo_buffer is owned by the softc from attach until detach.
 * It may be accessed from any thread that holds sc->sc_lock.
 * It must not be accessed in interrupt context because the lock
 * is a regular mutex, not a spin mutex.
 */
struct bugdemo_buffer *sc_buffer;
```

This comment is not decorative. It is a statement of the
invariants that the driver will enforce. If a future bug
violates the invariants, the comment is a reference point for
understanding what went wrong.

### Narrowing the API Surface

A third response is to narrow the API. If the bug was caused
because a function was called in a context where it should not
have been, can the function be made private, so that it is
only called from contexts where it is safe? If a state was
reached through a path that should not have existed, can the
state be made unreachable?

The principle is that every external entry point into a driver
is a surface for bugs. Reducing the surface, by making
functions internal, by hiding state behind accessors, by
combining related operations into atomic transactions, makes the
driver harder to misuse.

This is not about ideological minimalism. It is about recognizing
that surface area is proportional to bug risk, and that many bugs
can be prevented by the simple expedient of not exposing the
thing that was misused.

### Hardening the Unload Path

Unload is a frequently underhardened path in drivers. The
attach path is usually well tested; the unload path is usually
not. This is a major source of bugs: a driver that works
perfectly in long-running use might crash on `kldunload`.

A good unload path satisfies several invariants. Every object
allocated in attach is freed. Every thread spawned by the
driver has exited. Every timer is cancelled. Every callout is
drained. Every taskqueue has finished its pending work. Every
device node is destroyed before the memory that supports it is
freed.

After a bug in an unload path, audit the entire unload
function against this checklist. Each item is an invariant
that the driver should maintain, and violations are common.

### The Shape of a Resilient Driver

Putting these habits together, what does a resilient driver
look like? A few traits stand out.

Its locking is explicit. Every shared data structure is
protected by a named lock, and every function that accesses
the structure has either an assertion that the lock is held or
a documented reason why the lock is not needed. Lock orders
are documented in comments at each site of multi-lock
acquisition. `WITNESS` produces no warnings during normal
operation.

Its error handling is complete. Every allocation has a
corresponding free. Every `attach` has a complete `detach`. Every
path through the code cleans up after itself on failure.
Partial states do not linger. The driver does not get stuck
in half-initialized or half-torn-down states.

Its invariants are expressed in code. Preconditions are
checked with `KASSERT` at function entry. Structural
invariants are checked with `CTASSERT` at compile time.
State transitions are verified with explicit checks.

Its observability is built in. Counters expose allocation
and error rates. SDT probes fire at key events. Sysctls
expose enough state that an operator can inspect the driver
without a debugger. The driver tells you what it is doing.

Its error messages are useful. `log(9)` messages include
the subsystem name, the specific error, and enough context
to locate the problem. They are rate-limited. They do not
log spurious warnings that train the operator to ignore
them.

These traits are not free. They take time to implement and
discipline to maintain. But once a driver has them, the cost
of future bugs drops dramatically, because bugs are caught
earlier, diagnosed more easily, and fixed more definitively.

### Revisiting the bugdemo Driver

By the end of the chapter's labs, we will have applied many
of these ideas to the `bugdemo` driver. What starts out as a
handful of deliberately broken code paths grows, through
iteration, into a driver with assertions at every key point,
counters on every operation, SDT probes on every interesting
event, and an unload path that passes scrutiny. The
trajectory is deliberately designed to mirror the trajectory
of a real driver as it matures.

### Closing the Refactoring Loop

A last thought on refactoring. Every time you modify a driver
in response to a bug, you are taking a small risk that the
modification introduces a new bug. This risk is unavoidable but
manageable. A few practices help.

First, isolate the change. Make the smallest modification that
addresses the root cause, and commit it separately from
cosmetic changes. If something regresses, the blame is easy to
assign.

Second, add a test. If the bug was triggered by a specific
sequence of ioctls, add a small test program that runs that
sequence and verifies the correct result. Keep the test in your
repository. A test suite that grows with each bug becomes an
asset over years.

Third, run the existing tests. If the driver has any automated
tests at all, run them after a fix. Surprisingly many
regressions are caught this way, even with a small test suite.

Fourth, note the lesson. In your debug journal or commit
message, write a brief note about what the bug revealed about
the driver's design. The note is a gift to your future self,
who will encounter similar patterns.

With these habits in place, debugging becomes a cycle of
discovery rather than a string of firefights. Each bug teaches
something, each lesson strengthens the driver, and each
strengthened driver becomes easier to work with. The tools of
this chapter are the means by which that cycle turns.

With the conceptual material behind us, we can move into the
hands-on lab section, where we will apply every technique in
the chapter to the `bugdemo` driver and see each one produce
a concrete result.

## Hands-on Labs

Each lab in this section is self-contained but builds on the
previous ones. They use the `bugdemo` driver, whose companion
source is under `examples/part-07/ch34-advanced-debugging/`.

Before starting, make sure you have a development FreeBSD 14.3
VM where you can crash the kernel safely, a copy of the
FreeBSD source tree at `/usr/src/`, and the ability to attach
a serial or virtual console that preserves output across a
reboot.

### Lab 1: Adding Assertions to bugdemo

In this lab we build the first version of `bugdemo` and add
assertions that catch internal inconsistencies. The goal is to
see `KASSERT`, `MPASS`, and `CTASSERT` working in practice.

**Step 1: Build and load the baseline driver.**

The baseline driver lives at
`examples/part-07/ch34-advanced-debugging/lab01-kassert/`.
It is a minimal pseudo-device with a single ioctl that triggers
a bug when instructed to. From the lab directory:

```console
$ make
$ sudo kldload ./bugdemo.ko
$ ls -l /dev/bugdemo
```

If the device node appears, the driver loaded correctly.

**Step 2: Run the test tool to confirm the driver works.**

The lab also contains a small user-space program,
`bugdemo_test`, that opens the device and issues ioctls:

```console
$ ./bugdemo_test hello
$ ./bugdemo_test noop
```

Both should return success. Without any bugs triggered, the
driver behaves correctly.

**Step 3: Inspect the assertions in the source.**

Open `bugdemo.c` and find the function `bugdemo_process`. You
will see something like this:

```c
static void
bugdemo_process(struct bugdemo_softc *sc, struct bugdemo_command *cmd)
{
        KASSERT(sc != NULL, ("bugdemo: softc missing"));
        KASSERT(cmd != NULL, ("bugdemo: cmd missing"));
        KASSERT(cmd->op < BUGDEMO_OP_MAX,
            ("bugdemo: op %u out of range", cmd->op));
        MPASS(sc->state == BUGDEMO_STATE_READY);
        /* ... */
}
```

Each assertion documents an invariant. If any of them fires,
the kernel panics with a message identifying the broken
invariant.

**Step 4: Trigger an assertion.**

The driver has an ioctl called `BUGDEMO_FORCE_BAD_OP` that
intentionally sets `cmd->op` to an out-of-range value before
calling `bugdemo_process`:

```console
$ ./bugdemo_test force-bad-op
```

With a debug kernel, this produces an immediate panic:

```text
panic: bugdemo: op 255 out of range
```

and the system reboots. On a release kernel (no `INVARIANTS`),
the `KASSERT` is compiled out and the driver continues on with
an out-of-range value. The difference is exactly the value of
having a debug kernel during development.

**Step 5: Confirm the assertion fires on the right line.**

After the reboot, if a dump was captured, open it with `kgdb`:

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.last
(kgdb) bt
```

The backtrace will show `bugdemo_process`, and `frame N` to
that entry will show the assertion line. This is the end-to-end
chain: assertion fires, kernel panics, dump captures state,
kgdb identifies the code.

**Step 6: Add your own assertion.**

Modify the driver to add an assertion that a counter is
nonzero in a specific code path. Rebuild, reload, and trigger
a case that makes the counter zero. Observe that your
assertion fires as expected.

**What this lab teaches.** The `KASSERT` macro is a live check,
not a theoretical one. It fires, it panics, it identifies the
code. The discipline of adding assertions is backed by a
discipline of testing that they fire when they should.

### Lab 2: Capturing and Analyzing a Panic With kgdb

In this lab we focus on the post-mortem workflow. Starting from
a clean debug kernel, we trigger a panic, capture the dump, and
walk through it with `kgdb`.

**Step 1: Confirm the dump device is configured.**

On the VM, run:

```console
# dumpon -l
```

If the output shows a device path (typically the swap
partition), you are ready. If not, configure one:

```console
# dumpon /dev/ada0p3        # replace with your swap partition
# echo 'dumpdev="/dev/ada0p3"' >> /etc/rc.conf
```

**Step 2: Confirm the debug kernel is running.**

```console
# uname -v
# sysctl debug.debugger_on_panic
```

`debug.debugger_on_panic` should be `0` or `1` depending on
whether you want to pause at the debugger before the dump.
For automated lab work, `0` is easier; for interactive
exploration, `1` is educational.

```console
# sysctl debug.debugger_on_panic=0
```

**Step 3: Load bugdemo and trigger a panic.**

```console
# kldload ./bugdemo.ko
# ./bugdemo_test null-softc
panic: bugdemo: softc missing
Dumping ...
Rebooting ...
```

The panic message, the dump notice, and the reboot all appear
on the console. Dump writing takes a few seconds on a VM with
virtual disk.

**Step 4: After the reboot, inspect the saved dump.**

```console
# ls /var/crash/
bounds  info.0  info.last  minfree  vmcore.0  vmcore.last
# cat /var/crash/info.0
```

The `info.0` file summarizes the panic: the kernel version, the
message, and the initial backtrace captured before dumping.

**Step 5: Open the dump in kgdb.**

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.0
```

`kgdb` automatically runs a backtrace. Identify the frame that
is inside `bugdemo_ioctl` or `bugdemo_process`. Switch to it:

```console
(kgdb) frame 5
(kgdb) list
(kgdb) info locals
(kgdb) print sc
```

Observe that `sc` is NULL, confirming the panic message.

**Step 6: Explore adjacent state.**

From `kgdb`, examine the process that triggered the panic:

```console
(kgdb) info threads
(kgdb) thread N       # where N is the panicking thread
(kgdb) proc          # driver-specific helper for process state
```

`proc` is a kernel-specific command that prints the current
process. Between these commands and `bt`, you can build a full
picture of the panic's context.

**Step 7: Exit kgdb.**

```console
(kgdb) quit
```

The dump remains on disk; you can reopen it anytime.

**What this lab teaches.** The full cycle of panic, dump, and
offline analysis is routine, not mysterious. A development VM
should be able to complete this cycle in under a minute. The
discipline is to practice it before the first real bug, so
that when you need it you are not learning the tool in a
hurry.

### Lab 3: Building GENERIC-DEBUG and Confirming Options Active

This lab is about kernel configuration rather than code. The
aim is to walk through the full process of building, installing,
and validating a debug kernel.

**Step 1: Start from a fresh `/usr/src/`.**

If you have a source tree, update it. If not, install one with:

```console
# git clone --depth 1 -b releng/14.3 https://git.freebsd.org/src.git /usr/src
```

**Step 2: Review the existing GENERIC-DEBUG config.**

```console
$ ls /usr/src/sys/amd64/conf/GENERIC*
$ cat /usr/src/sys/amd64/conf/GENERIC-DEBUG
```

Observe that it is only two lines: `include GENERIC` and
`include "std.debug"`. Review `std.debug` next:

```console
$ cat /usr/src/sys/conf/std.debug
```

Confirm the options we discussed: `INVARIANTS`,
`INVARIANT_SUPPORT`, `WITNESS`, and friends.

**Step 3: Build the kernel.**

```console
# cd /usr/src
# make buildkernel KERNCONF=GENERIC-DEBUG
```

On a modest VM this takes twenty to forty minutes. The build
produces detailed output; if it stops with an error, investigate
and retry.

**Step 4: Install the kernel.**

```console
# make installkernel KERNCONF=GENERIC-DEBUG
# ls -l /boot/kernel/kernel /boot/kernel.old/kernel
```

The previous kernel is preserved in `/boot/kernel.old/` as a
recovery option.

**Step 5: Reboot into the new kernel.**

```console
# shutdown -r now
```

After reboot, confirm:

```console
$ uname -v
$ sysctl debug.kdb.current_backend
$ sysctl debug.kdb.supported_backends
```

The backend should list both `ddb` and `gdb`.

**Step 6: Confirm INVARIANTS is active.**

Build and load the lab01 `bugdemo.ko`, then trigger the out-of-
range op as in Lab 1. On a debug kernel, the panic fires. On a
release kernel, it does not. This round-trip confirms that
`INVARIANTS` is genuinely compiled in.

**Step 7: Confirm WITNESS is active.**

The lab03 variant of `bugdemo` has a deliberate lock order
inversion, triggered by a specific ioctl. Load it, run the
triggering test, and watch for a `WITNESS` warning on the
console:

```text
lock order reversal:
 ...
```

No panic is produced, just a warning. This is the expected
behavior: `WITNESS` detects potential deadlocks and reports
them, without forcing a system failure.

**Step 8: Recover if the new kernel does not boot.**

If your new kernel fails to boot for any reason, the FreeBSD
boot loader offers a recovery option. From the loader menu,
select "Boot Kernel" and then "kernel.old". Your previous
kernel boots, and you can investigate the debug kernel's
failure at leisure.

**What this lab teaches.** Building a debug kernel is not a
mysterious operation. It is a rebuild with different options
and a reboot. The hazards are predictable: long build times,
large binaries, and the need to keep the previous kernel
available as a fallback.

### Lab 4: Tracing bugdemo With DTrace and ktrace

This lab exercises the three tracing tools we studied: DTrace
`fbt` probes, DTrace SDT probes, and `ktrace(1)`.

**Step 1: Load a bugdemo variant with SDT probes.**

The `lab04-tracing` variant of bugdemo defines SDT probes at
key points:

```c
SDT_PROVIDER_DEFINE(bugdemo);
SDT_PROBE_DEFINE2(bugdemo, , , cmd__start, "struct bugdemo_softc *", "int");
SDT_PROBE_DEFINE3(bugdemo, , , cmd__done, "struct bugdemo_softc *", "int", "int");
```

Load it:

```console
# kldload ./bugdemo.ko
```

**Step 2: List the probes.**

```console
# dtrace -l -P sdt -n 'bugdemo:::*'
```

You should see the `cmd-start` and `cmd-done` probes listed.

**Step 3: Watch the probes fire.**

In one terminal:

```console
# dtrace -n 'sdt:bugdemo::cmd-start { printf("op=%d\n", arg1); }'
```

In another terminal:

```console
$ ./bugdemo_test noop
$ ./bugdemo_test hello
```

The first terminal shows each probe firing with its op value.

**Step 4: Measure latency per op.**

```console
# dtrace -n '
sdt:bugdemo::cmd-start
{
        self->start = timestamp;
}

sdt:bugdemo::cmd-done
/self->start != 0/
{
        @by_op[arg1] = quantize(timestamp - self->start);
        self->start = 0;
}
'
```

Run a workload of many ioctls, then Ctrl-C the DTrace. An
aggregation prints, showing a latency histogram per op.

**Step 5: Use fbt to trace entries.**

```console
# dtrace -n 'fbt::bugdemo_*:entry { printf("%s\n", probefunc); }'
```

Trigger some ioctls from user space. The DTrace terminal shows
each entry, giving you a live view of the driver's flow.

**Step 6: Use ktrace to trace the user-space side.**

```console
$ ktrace -t ci ./bugdemo_test hello
$ kdump
```

Observe the ioctl calls visible in the kdump output.

**Step 7: Combine ktrace and DTrace.**

Run DTrace in one terminal, watching SDT probes, while running
ktrace in another on the user-space test. The two outputs,
read together, give a complete picture of the interaction from
user space to kernel and back.

**What this lab teaches.** Tracing is not a single tool; it is a
family. DTrace is the richest, `ktrace(1)` is the simplest way
to see the user-kernel boundary, and combining them gives the
most complete view.

### Lab 5: Catching a Use-After-Free With memguard

This lab walks through a real memory debugging scenario. The
`lab05-memguard` variant of `bugdemo` contains a deliberate
use-after-free bug: under certain ioctl sequences, the driver
frees a buffer and then reads it from a callout.

**Step 1: Build a kernel with DEBUG_MEMGUARD.**

Add `options DEBUG_MEMGUARD` to your `GENERIC-DEBUG` config, or
create a new config:

```text
include GENERIC
include "std.debug"
options DEBUG_MEMGUARD
```

Rebuild and install as in Lab 3.

**Step 2: Load the lab05 bugdemo and enable memguard.**

```console
# kldload ./bugdemo.ko
# sysctl vm.memguard.desc=bugdemo
```

The second command tells `memguard(9)` to guard all allocations
made with malloc type `bugdemo`. The exact type name comes from
the driver's `MALLOC_DEFINE` call.

**Step 3: Trigger the use-after-free.**

```console
$ ./bugdemo_test use-after-free
```

The user-space call returns quickly. A moment later (when the
callout fires), the kernel panics with a page fault inside the
callout routine:

```text
Fatal trap 12: page fault while in kernel mode
fault virtual address = 0xfffff80002abcdef
...
KDB: stack backtrace:
db_trace_self_wrapper()
...
bugdemo_callout()
...
```

`memguard(9)` has converted a silent use-after-free into an
immediate page fault. The backtrace points directly at
`bugdemo_callout`.

**Step 4: Analyze the dump with kgdb.**

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.last
(kgdb) bt
(kgdb) frame N      # into bugdemo_callout
(kgdb) list
(kgdb) print buffer
```

The source line shows the read from `buffer`, and `buffer` is
a freed `memguard`-protected address. `kgdb` prints it as an
address that is no longer mapped.

**Step 5: Fix the bug and verify.**

The fix is to cancel the callout before freeing the buffer.
Modify the driver source accordingly, rebuild, reload, and run
the same test. The panic no longer fires. Keep `memguard`
enabled during the verification, then disable it and re-test:

```console
# sysctl vm.memguard.desc=
```

Both runs should succeed. If the release-mode run (without
`memguard`) still fails, the bug is not fully fixed.

**Step 6: Count inflight allocations.**

The lab also shows an alternative technique: counting inflight
allocations. Add a `counter(9)` to the driver, bump it on
allocate, decrement on free. At unload, assert that the counter
is zero:

```c
KASSERT(counter_u64_fetch(bugdemo_inflight) == 0,
    ("bugdemo: leaked %ld buffers",
     (long)counter_u64_fetch(bugdemo_inflight)));
```

Unload without first freeing all buffers, observe the assertion
fire.

**What this lab teaches.** `memguard(9)` is a specific tool for a
specific class of bug. When it applies, it turns hard bugs into
easy ones. Knowing when to reach for it is the practical skill.

### Lab 6: Remote Debugging With the GDB Stub

This lab demonstrates remote debugging over a virtual serial
port. It assumes you are using bhyve or QEMU with a serial
console exposed to the host.

**Step 1: Configure KDB and GDB in the kernel.**

Both should already be present in `GENERIC-DEBUG`. Confirm with:

```console
# sysctl debug.kdb.supported_backends
```

**Step 2: Configure the serial console in the VM.**

In bhyve, add `-l com1,stdio` to the launch command, or
equivalent. In QEMU, use `-serial stdio` or `-serial pty`. The
goal is to have a virtual serial port accessible from the host.

**Step 3: In the VM, switch to the GDB backend.**

```console
# sysctl debug.kdb.current_backend=gdb
```

**Step 4: In the VM, drop into the debugger.**

Send the break-to-debugger sequence on the serial console, or
trigger a panic:

```console
# sysctl debug.kdb.enter=1
```

The kernel halts. The serial console shows:

```text
KDB: enter: sysctl debug.kdb.enter
[ thread pid 500 tid 100012 ]
Stopped at     kdb_enter+0x37: movq  $0,kdb_why
gdb>
```

**Step 5: On the host, attach kgdb.**

```console
$ kgdb /boot/kernel/kernel
(kgdb) target remote /dev/ttyXX    # the host-side serial device
```

The host's `kgdb` connects to the kernel over the serial line.
You can now run full `kgdb` commands on the live kernel:
`bt`, `info threads`, `print`, `set variable`, and so on.

**Step 6: Set a breakpoint.**

```console
(kgdb) break bugdemo_ioctl
(kgdb) continue
```

The VM resumes. In the VM, run `./bugdemo_test hello`. The
breakpoint fires, and `kgdb` on the host shows the state.

**Step 7: Detach cleanly.**

```console
(kgdb) detach
(kgdb) quit
```

In the VM, the kernel resumes running.

**What this lab teaches.** Remote debugging is a specialized but
valuable tool. It is most useful when you need live inspection
of a running kernel, particularly for intermittent bugs that
are hard to capture as dumps.

## Challenge Exercises

The following challenges build on the labs. They are open-ended
by design: there are multiple valid approaches, and the point is
to practice choosing the right tool for each bug.

### Challenge 1: Find the Silent Bug

The `lab-challenges/silent-bug` variant of `bugdemo` contains a
bug that produces no crash and no error. Instead, a counter
sometimes reports the wrong value after a specific ioctl
sequence. Your task:

1. Write a test program that reproduces the bug.
2. Use DTrace to narrow down which function produces the wrong
   counter value.
3. Fix the bug and verify that the DTrace signature disappears.

Hint: the bug is a missing memory barrier, not a missing lock.
The symptom is cache coherence, not contention.

### Challenge 2: Hunt the Leak

The `lab-challenges/leaky-driver` variant leaks an object each
time a specific ioctl path is exercised. Your task:

1. Confirm the leak using `vmstat -m` before and after a
   workload.
2. Use DTrace to record every allocation and free of the leaked
   object type, aggregated by stack.
3. Identify the code path that allocates without freeing.
4. Add a `counter(9)`-based inflight check to the driver and
   verify that it fires when the buggy path is taken.

### Challenge 3: Diagnose the Deadlock

The `lab-challenges/deadlock` variant sometimes hangs when two
ioctls are run concurrently. Your task:

1. Reproduce the hang.
2. Attach to the hung kernel with `kgdb` (or drop into `DDB`).
3. Use `info threads` and `bt` on each stuck thread to
   identify the lock ordering.
4. Determine the fix (reorder locks, or eliminate one of them).

### Challenge 4: Read a Real Panic

Load a kernel module you did not write (for example, one of the
USB class drivers or a filesystem module). Deliberately trigger
a bad interaction by sending malformed input from user space.
When it panics (or fails to), write up:

1. The exact sequence that caused the symptom.
2. The backtrace or error observed.
3. Whether the module had assertions that would have caught the
   problem earlier.
4. One suggestion for strengthening the module's invariants.

### Challenge 5: Build Your Own bugdemo Variant

Create a new variant of `bugdemo` that contains a bug you have
encountered in real-world code. Write a test program that
triggers the bug deterministically. Then, using any subset of
the techniques in this chapter, diagnose the bug from scratch.
Write up what you learned. The point is to practice the
conversion of "I recognize this pattern" into reproducible
teaching material.

## Troubleshooting Common Issues

Even the best tools run into problems in practice. This section
collects the issues you are most likely to hit and how to resolve
them.

### The Dump Is Not Being Captured

After a panic, `/var/crash/` shows only `bounds` and `minfree`,
no `vmcore.N`. Possible causes:

- **No dump device configured.** Run `dumpon -l` after a normal
  boot. If it reports "no dump device configured," set one
  with `dumpon /dev/DEVICE` and persist it in `/etc/rc.conf`
  with `dumpdev=`.
- **Dump device too small.** A dump needs space equal to kernel
  memory plus some overhead. A 1GB swap partition will not hold
  a dump of an 8GB-memory machine. Enlarge the dump device or
  use a compressed dump (`dumpon -z`).
- **savecore disabled.** Check `/etc/rc.conf` for
  `savecore_enable="NO"`. Switch to `YES` and reboot.
- **Crash too severe.** If the panic itself prevents the dump
  machinery from running, you may see no output at all. In that
  case a serial console is essential to capture at least the
  panic message.

### kgdb Says "No Symbols"

When opening a dump, `kgdb` prints "no debugging symbols found"
or similar. Possible causes:

- **Kernel built without `-g`.** Debug kernels include `-g`
  automatically via `makeoptions DEBUG=-g`. Release kernels do
  not. Either build a debug kernel or install the debug
  symbols package if available.
- **Mismatch between kernel and dump.** If the dump came from a
  different kernel than the one `kgdb` is loading, symbols will
  not match. Use the exact kernel binary that was running at
  the time of the panic.
- **Module symbols missing.** If the panic is inside a module
  that was built without `-g`, `kgdb` shows addresses without
  source lines for that module. Rebuild the module with
  `DEBUG_FLAGS=-g`.

### DDB Freezes the System

Entering `DDB` intentionally halts the kernel. This is by
design, but on a production-like system it can look like a
hang. If you are in `DDB` and want to resume:

- `continue` exits `DDB` and returns to the kernel.
- `reset` reboots immediately.
- `call doadump` forces a dump and then reboot.

If you entered `DDB` accidentally, `continue` is almost always
the right action.

### A Module Refuses to Unload

`kldunload bugdemo` returns `Device busy`. Causes:

- **Open file descriptors.** Something still has `/dev/bugdemo`
  open. Use `fstat | grep bugdemo` to find processes and close
  them.
- **Reference counts.** Another module references this one.
  Unload that module first.
- **Pending work.** A callout or taskqueue is still scheduled.
  Wait for it to drain, or make the driver explicitly cancel
  and drain in its unload path.
- **Stuck thread.** A kernel thread spawned by the driver has
  not exited. Terminate it from within the driver on unload.

### memguard Does Nothing

`vm.memguard.desc=bugdemo` is set, but memguard does not seem
to be catching any bugs. Causes:

- **Wrong type name.** `vm.memguard.desc` must match a type
  passed to `MALLOC_DEFINE(9)` exactly. If you set
  `vm.memguard.desc=BugDemo` but the driver uses
  `MALLOC_DEFINE(..., "bugdemo", ...)`, the names do not match.
- **Kernel not built with `DEBUG_MEMGUARD`.** The sysctl node
  exists only if the option is compiled in. Check
  `sysctl vm.memguard.waste` or similar; if it returns "unknown
  oid," the feature is not compiled in.
- **Allocation path not traversed.** If the code path where
  the bug lives does not actually use the guarded type,
  `memguard` cannot catch it. Confirm the allocation type with
  `vmstat -m`.

### DTrace Says "Probe Does Not Exist"

```text
dtrace: invalid probe specifier sdt:bugdemo::cmd-start: probe does not exist
```

Causes:

- **Module not loaded.** SDT probes are defined by the module
  that provides them. If the module is not loaded, the probes
  do not exist.
- **Probe name mismatch.** The name in the source has double
  underscores (`cmd__start`), but DTrace uses a single dash
  (`cmd-start`). This is the conversion rule; the underscore
  form appears in C, the dash form in DTrace.
- **Provider not defined.** If `SDT_PROVIDER_DEFINE(bugdemo)`
  is missing or in a different file than `SDT_PROBE_DEFINE`,
  the probes will not exist.

### Kernel Builds Fail With Symbol Conflicts

When building a kernel with unusual option combinations, you
may see link errors like "multiple definition of X." Causes:

- **Conflicting options.** Some options are mutually exclusive.
  Review the option documentation in `/usr/src/sys/conf/NOTES`.
- **Stale objects.** Old build artifacts can interfere with
  new builds. Try `make cleandir && make cleandir` in the
  kernel build directory.
- **Tree inconsistency.** A partial update of `/usr/src/` can
  leave headers and sources out of sync. Run a full `svnlite
  update` or `git pull` and retry.

### The System Boots Into the Old Kernel

After `installkernel`, you reboot and `uname -v` shows the old
kernel. Causes:

- **Boot entry not updated.** The default is `kernel`, which
  points to the current kernel. If you installed with
  `KERNCONF=GENERIC-DEBUG` but did not run `make
  installkernel` cleanly, the old binary may still be in
  place. Check `/boot/kernel/kernel` timestamp.
- **Wrong kernel selected in loader.** The FreeBSD loader
  menu has a "Boot Kernel" option that can select between
  available kernels. Pick the right one, or set
  `kernel="kernel"` in `/boot/loader.conf`.
- **Boot partition unchanged.** On some systems the boot
  partition is separate and needs a manual copy. Check that
  you installed to the correct partition.

### WITNESS Reports False Positives

Sometimes `WITNESS` warns about a lock order that you know is
safe. Possible reasons and responses:

- **The order really is unsafe but harmless in practice.**
  `WITNESS` reports potential deadlocks, not actual ones. A
  cycle in the lock graph that is never exercised concurrently
  is still a bug waiting to happen. Refactor the locking.
- **Locks are acquired by address.** Generic code that locks by
  pointer can produce orders that depend on runtime data, not
  static structure. See `witness(4)` for how to suppress
  specific orders with `witness_skipspin` or manual overrides.
- **Multiple locks of the same type.** Acquiring two instances
  of the same lock class is always a potential issue. Use
  `mtx_init(9)` with a distinct type name per instance if you
  need them treated as separate classes.

## Putting It All Together: A Debugging Session Walkthrough

Before we wrap up, let us walk through a complete debugging
session that uses several of the techniques we have studied. The
scenario is fictional but realistic: a driver that sometimes
fails with a misleading error, and we track it down from first
symptom to root cause.

### The Symptom

A user reports that their program sometimes gets `EBUSY` from an
ioctl on `/dev/bugdemo`. The program always calls the ioctl in
the same way, and most of the time it works. Only under heavy
load does `EBUSY` appear, and then inconsistently.

### Step 1: Collect Evidence

The first step is to observe the phenomenon without perturbing
it. We run the user's program under `ktrace(1)` to confirm the
symptom:

```console
$ ktrace -t ci ./user_program
$ kdump | grep ioctl
```

The output confirms that a specific ioctl returns `EBUSY` now
and then. No other user-space call misbehaves. This tells us the
bug is in the kernel's handling of that ioctl, not in the user
program's logic.

### Step 2: Form a Hypothesis

The error code `EBUSY` usually indicates a resource conflict.
Reading the driver source, we find that `EBUSY` is returned when
an internal flag indicates a previous operation is still in
progress. The flag is cleared by a callout that completes the
operation.

Our hypothesis: under heavy load, the callout is delayed long
enough that a new ioctl arrives before the previous one has
completed. The driver was not designed to serialize such
requests, so it rejects the newcomer.

### Step 3: Test the Hypothesis With DTrace

We write a DTrace script that records the delay between
consecutive ioctls and the state of the busy flag at each entry:

```console
dtrace -n '
fbt::bugdemo_ioctl:entry
{
        self->ts = timestamp;
}

fbt::bugdemo_ioctl:return
/self->ts != 0/
{
        @[pid, self->result] = lquantize(timestamp - self->ts, 0, 1000000, 10000);
        self->ts = 0;
}
'
```

Running the user program under load, we observe that `EBUSY`
returns happen almost exclusively when the previous ioctl
completed more than 50 microseconds ago and the callout has not
fired yet. This corroborates the hypothesis.

### Step 4: Confirm With SDT Probes

We add SDT probes around the busy flag manipulation and watch
them:

```console
dtrace -n '
sdt:bugdemo::set-busy
{
        printf("%lld set busy\n", timestamp);
}

sdt:bugdemo::clear-busy
{
        printf("%lld clear busy\n", timestamp);
}

sdt:bugdemo::reject-busy
{
        printf("%lld reject busy\n", timestamp);
}
'
```

The trace shows a clear pattern: set, reject, reject, reject,
clear, set, clear. The clear is coming late because the callout
is competing for a shared taskqueue with other work.

### Step 5: Identify the Fix

With the evidence gathered, the fix is clear. Either the driver
needs to serialize incoming ioctls instead of rejecting them (a
queue or a wait), or it needs to complete the previous operation
synchronously instead of via callout.

We pick the queuing approach because it preserves the callout's
benefits. The driver accumulates pending requests and dispatches
them as the callout fires. Under light load, nothing changes.
Under heavy load, requests wait rather than failing.

### Step 6: Implement and Verify

We modify the driver. We run the user program under the original
workload. `EBUSY` no longer appears. The DTrace latency
distribution now shows a tail that reflects the queueing delay,
which is acceptable for this driver's use case.

We also enable `DEBUG_MEMGUARD` on the driver's malloc type and
run the workload for a while, to make sure the queuing code does
not introduce memory bugs. No faults fire.

Finally, we run the full test suite. Everything passes. The fix
is committed with a descriptive message that explains the root
cause, not just the symptom.

### Lessons From This Session

Two things are worth noting.

First, the tools we used were relatively lightweight. No crash
dump was needed. No `DDB` was entered. The bug was diagnosed
through passive observation, DTrace, and careful reading. For
many driver bugs, this is the shape of a session: not a
dramatic panic but a systematic narrowing of hypotheses.

Second, the fix addressed the root cause, not the symptom. A
surface fix might have been to increase the priority of the
callout's taskqueue. That would have reduced the frequency of
the bug without eliminating it. A more principled fix changes
the driver's contract from "reject if busy" to "queue and
serve." This is the refactoring mindset we discussed in Section
8: every bug is a message about the design.

## Additional Techniques Worth Knowing

The chapter has covered the core of FreeBSD's debugging
toolkit. A handful of additional techniques did not fit into the
main narrative but deserve mention, because you will eventually
encounter them.

### witness_checkorder With Manual Lists

`WITNESS` can be tuned. In `/usr/src/sys/kern/subr_witness.c`
there is a table of known-good lock orders that the kernel
recognizes. When building a driver that uses a subsystem locked
by an existing lock, adding the driver's own lock to this table
lets `WITNESS` verify the combined order across the driver and
the subsystem.

This is rarely needed in small drivers but becomes useful for
drivers that interact deeply with multiple subsystems.

### sysctl debug.ktr

Beyond just enabling and disabling `ktr(9)` classes, there are
additional controls:

- `debug.ktr.clear=1` clears the buffer.
- `debug.ktr.verbose=1` sends trace entries to the console in
  real time, in addition to the ring.
- `debug.ktr.stamp=1` adds timestamps to each entry.

The combination of these is especially useful when you want to
watch a live trace without running `ktrdump(8)` repeatedly.

### DDB Commands Beyond bt

`DDB` has a rich set of commands that are sparsely documented.
A few are particularly useful for driver authors:

- `show all procs` lists every process.
- `show lockedvnods` shows currently locked vnodes (useful for
  storage driver bugs).
- `show mount` shows mounted filesystems.
- `show registers` dumps CPU registers.
- `break FUNC` sets a breakpoint.
- `step` and `next` advance one instruction or one line.
- `watch` sets a watchpoint on an address.

The `help` command in `DDB` lists all available commands.
Reading the list once is a useful way to discover features you
did not know about.

### Kernel Option KDB_TRACE

`KDB_TRACE` causes the kernel to print a stack trace on every
panic, even if the operator does not interact with the
debugger. This is useful in automated testing where nobody is
watching the console. It is already in `GENERIC`.

### EKCD: Encrypted Kernel Crash Dumps

If kernel dumps contain sensitive data (process memory,
credentials, keys), the kernel can encrypt them at dump time.
The `EKCD` option enables this feature. A public key is loaded
at runtime with `dumpon -k`; the matching private key is used
at `savecore` time to decrypt.

This matters on production systems where dumps might be
transported over untrusted channels. It does not matter on a
development VM.

### Lightweight Debug Output: bootverbose

Another low-overhead option is `bootverbose`. Setting
`boot_verbose` in the loader or `bootverbose=1` in sysctl
causes many kernel subsystems to print additional diagnostic
information at boot. If your driver has not yet reached the
point where DTrace applies, `bootverbose` can help you see
what the driver is doing during `attach`.

The way to make your own driver honor `bootverbose` is to
check `bootverbose` in your probe or attach code:

```c
if (bootverbose)
        device_printf(dev, "detailed attach info: ...\n");
```

This is a well-established pattern in `/usr/src/sys/dev/`
drivers.

## A Closer Look at DDB

The in-kernel debugger, `DDB`, deserves more attention than we
have given it so far. Many driver authors use `DDB` only
reactively, when a panic lands them in it unexpectedly. With a
little practice, `DDB` is also a useful tool to enter
deliberately, for interactive inspection of a live kernel.

### Entering DDB

There are several ways to enter `DDB`. We have seen some of
them already:

- By panic, if `debug.debugger_on_panic` is nonzero.
- By serial BREAK (or `Ctrl-Alt-Esc` on a keyboard console),
  when `BREAK_TO_DEBUGGER` is compiled in.
- By the alternative sequence `CR ~ Ctrl-B`, when
  `ALT_BREAK_TO_DEBUGGER` is compiled in.
- Programmatically, with `sysctl debug.kdb.enter=1`.
- From code, by calling `kdb_enter(9)`.

In development, the programmatic entry is the most
convenient. You can drop into `DDB` at a specific point in a
script without waiting for a panic.

### DDB Prompts and Commands

Once inside, `DDB` presents a prompt. The standard prompt is
simply `db>`. Commands are typed, followed by Enter. `DDB` has
a command history (press up-arrow on the serial console) and
tab-completion for many command names.

A useful first command is `help`, which lists categories of
commands. `help show` lists the many `show` subcommands. Most
exploration happens through `show`.

### Walking a Thread

The most common diagnostic task in `DDB` is to walk a specific
thread. Start with `ps`, which lists all processes:

```console
db> ps
  pid  ppid  pgrp  uid  state  wmesg   wchan    cmd
    0     0     0    0  RL     (swapper) [...] swapper
    1     0     1    0  SLs    wait     [...] init
  ...
  500   499   500    0  SL     nanslp   [...] user_program
```

Pick the thread of interest. In `DDB`, switching to a thread
is done by the `show thread` command:

```console
db> show thread 100012
  Thread 100012 at 0xfffffe00...
  ...
db> bt
```

This walks the stack of that specific thread. A kernel deadlock
investigation typically involves walking each stuck thread to
see where it is waiting.

### Inspecting Structures

`DDB` can dereference pointers and print structure fields if
the kernel was built with `DDB_CTF`. Example:

```console
db> show proc 500
db> show malloc
db> show uma
```

Each of these prints a formatted view of the relevant kernel
state. `show malloc` gives a table of malloc types and their
current allocations. `show uma` does the same for UMA zones.
`show proc` shows a specific process in detail.

### Setting Breakpoints

`DDB` supports breakpoints. `break FUNC` sets a breakpoint at
the entry to a function. `continue` resumes execution. When the
breakpoint fires, the kernel returns to `DDB` and you can
inspect state at that point.

This is the machinery that makes `DDB` a real debugger, not
just a crash inspector. With breakpoints you can pause the
kernel at a specific code location, examine arguments, and
decide whether to continue.

The catch is that a kernel paused in `DDB` really is paused.
While you are in `DDB`, no other thread runs. On a network
server, every client times out. On a desktop, the GUI freezes.
For local, development-VM debugging, this is fine. For any
remote or shared use, it is not.

### Scripting DDB

`DDB` supports a simple scripting facility. You can define
named scripts that execute a sequence of `DDB` commands.
`script kdb.enter.panic=bt; show registers; show proc` makes
those three commands run automatically every time the debugger
enters due to a panic. This is useful for unattended dumps:
the scripted output appears on the console and in the dump,
giving you information without needing an interactive session.

The scripts are stored in kernel memory and can be configured
at boot via `/boot/loader.conf` or at runtime with `sysctl`
calls. See `ddb(4)` for the exact syntax.

### Exiting DDB

When you are done, `continue` exits `DDB` and the kernel
resumes. `reset` reboots. `call doadump` forces a dump and
reboot. `call panic` triggers a panic intentionally (useful
when you want a dump from the current state but did not reach
`DDB` via a panic).

For the developer practicing on a VM, `continue` is the one
command to remember. It brings the kernel back to life and
lets you keep working.

### DDB vs kgdb: When to Use Each

`DDB` and `kgdb` overlap but are not interchangeable.

Use `DDB` when the kernel is running (or paused on a specific
event) and you want to poke around. `DDB` runs inside the
kernel and has direct access to kernel memory and threads. It
is the right tool for quick state checks, for setting
breakpoints, and for stopping on specific events.

Use `kgdb` on a crash dump, after the machine has rebooted.
`kgdb` has no access to a running system's threads, but it has
full gdb features for offline analysis: command history,
source browsing, scripting with Python, and so on.

For a live kernel that you cannot reboot, the GDB stub backend
of `KDB` bridges the gap: the kernel pauses, and `kgdb` on
another machine attaches over a serial line for full gdb
features on the live state. This is the most capable
combination but requires two machines (or VMs).

## Practical Worked Example: Following a Null Pointer

To pull the tools together, let us walk through one more
worked example. The symptom: `bugdemo` occasionally panics
with `page fault: supervisor read instruction` and a
backtrace through `bugdemo_read`. The panic address is low,
suggesting a null pointer dereference.

### Step 1: Capture the Dump

After the panic, we confirm a dump was saved:

```console
# ls -l /var/crash/
```

and open it:

```console
# kgdb /boot/kernel/kernel /var/crash/vmcore.last
```

### Step 2: Read the Backtrace

```console
(kgdb) bt
#0  __curthread ()
#1  doadump (textdump=0) at /usr/src/sys/kern/kern_shutdown.c
#2  db_fncall_generic at /usr/src/sys/ddb/db_command.c
...
#8  bugdemo_read (dev=..., uio=..., ioflag=0)
    at /usr/src/sys/modules/bugdemo/bugdemo.c:185
```

The interesting frame is 8, `bugdemo_read`. The code at line
185 is:

```c
sc = dev->si_drv1;
amt = MIN(uio->uio_resid, sc->buflen);
```

### Step 3: Inspect the Variables

```console
(kgdb) frame 8
(kgdb) print sc
$1 = (struct bugdemo_softc *) 0x0
(kgdb) print dev->si_drv1
$2 = (void *) 0x0
```

`si_drv1` is NULL on the dev. This is the private pointer that
`make_dev(9)` sets; it should have been set during attach.

### Step 4: Walk Back

```console
(kgdb) print *dev
```

We see the device structure. The name field says "bugdemo",
the flags look reasonable, but `si_drv1` is NULL. Something
cleared it.

### Step 5: Form a Hypothesis

In the source, `si_drv1` is set once, in `attach`, and it is
read in every `read`, `write`, and `ioctl` handler. It is
never explicitly cleared. However, in the unload path, the
device is destroyed with `destroy_dev(9)`, which returns
before pending handlers finish. If a `read` is in progress
when unload starts, the dev might be partially destroyed.

### Step 6: Add an Assertion

A `KASSERT` at the top of `bugdemo_read` catches the case:

```c
KASSERT(sc != NULL, ("bugdemo_read: no softc"));
```

With this assertion in place, the next panic gives us the same
information without requiring a dump walk. We also know
immediately that the condition is real, not a random
corruption.

### Step 7: Fix the Bug

The real fix is to make the unload path wait for pending
handlers before destroying the device. FreeBSD provides
`destroy_dev_drain(9)` for exactly this purpose. Using it:

```c
destroy_dev_drain(sc->dev);
```

ensures that no read or write is in flight when the softc is
freed.

### Step 8: Verify

Load the fixed driver. Run concurrent reads and unloads. The
panic does not reproduce. The `KASSERT` stays in the code as
a safety net for future refactoring.

### Takeaway

This workflow (capture, read, inspect, hypothesize, verify)
is the shape of most productive debugging sessions. Each tool
plays a small, specific role. The discipline is to gather
evidence before acting, and to leave assertions behind as
witnesses for the future.

## Making Drivers Observable From Day One

A theme running through this chapter is that the best time to
add debugging infrastructure is before you need it. A driver
that was designed with observability in mind is easier to
debug than a driver that was designed for speed alone.

A few concrete habits support this.

### Name Every Allocator Type

`MALLOC_DEFINE(9)` requires a short and a long name. The short
name is what appears in `vmstat -m` output and in `memguard(9)`
targeting. Picking a descriptive name, unique to the driver,
makes later diagnosis easier. Never share a malloc type
between unrelated subsystems; the tooling cannot distinguish
them.

### Count Important Events

Every major event in a driver (open, close, read, write,
interrupt, error, state transition) is a candidate for a
`counter(9)`. Counters are cheap, they accumulate over time,
and they are exposed through sysctl. A driver with good
counters answers most "what is this thing doing" questions
without any additional tooling.

### Declare SDT Probes

Every state transition is a candidate for an SDT probe.
Unlike assertions or counters, probes cost nothing when
disabled. Leaving them in the source for the lifetime of the
driver is a net win: when a bug appears, DTrace can see the
event flow without requiring a rebuild.

### Use Consistent Log Messages

`log(9)` messages should follow a consistent format. A prefix
that identifies the driver, a specific error code or state,
and enough context to locate the problem are the essentials.
Avoid cleverness in log messages; a reader under time pressure
wants to know what happened, not to admire your prose.

### Provide Useful Sysctls

Every internal flag, every counter, every configuration value
should be exposed through sysctl unless there is a specific
reason not to. The reader who needs to debug your driver will
thank you; the reader who never needs to debug your driver
pays nothing for the exposure.

### Write Assertions As You Go

The best time to add a `KASSERT` is when the invariant is
fresh in your mind, which is while you are writing the code.
Going back later to sprinkle assertions is less effective
because you have forgotten some invariants and have
rationalized others as "obvious."

### Expose the State of the State Machine

Every nontrivial driver has a state machine. Exposing the
current state through a sysctl, an SDT probe at each
transition, and a counter per state makes the state machine
visible to both humans and tools. This is particularly
important for asynchronous drivers, which is the subject of
the next chapter.

### Test the Unload Path

An underhardened unload path is a classic source of crashes.
In development, write a test that loads the driver, exercises
it briefly, and unloads it, repeatedly and under various
conditions. If the driver cannot sustain a hundred
load/unload cycles, it has bugs.

These habits cost a little time in development and pay for
themselves many times over in debugging. A disciplined driver
author applies them all, even to drivers that look too simple
to need them.

## Real-World Reading List

Every tool in this chapter is documented more completely in its own
man page or source file. That is good news: you do not have to carry
the whole toolbox in your head. When a bug points you at a specific
subsystem, opening the right man page or source file will almost
always take you further than any chapter can. The following list
gathers the references that matter most for this material, in the
order you are likely to need them.

The `witness(4)` man page is the first thing to read when
`GENERIC-DEBUG` starts printing lock order reversals and you want to
understand exactly what the output means, which `sysctl` controls
change behavior, and which counters you can inspect. It documents
the `debug.witness.*` sysctls, the `show all_locks` DDB command, and
the general approach `WITNESS` takes to bookkeeping. For the actual
implementation, `/usr/src/sys/kern/subr_witness.c` is the
authoritative source. Reading the structures it maintains and the
shape of its output functions (the ones that produce the "1st ...
2nd ..." lines you saw earlier in this chapter) removes most of the
mystery from a `WITNESS` report. The file is long, but the top
comment and the output-producing functions together cover most of
what a driver author needs to know.

For lock profiling, `lockstat(1)` is documented in
`/usr/src/cddl/contrib/opensolaris/cmd/lockstat/lockstat.1`. The man
page ends with several worked examples whose output format matches
what you will see on your own systems, which makes it a useful
reference to keep open the first time you try `-H -s 8` on a real
workload. Because `lockstat(1)` is DTrace-backed, the `dtrace(1)`
man page is its natural companion; you can express the same queries
in raw D if you need flexibility the `lockstat` command-line flags
do not offer.

For kernel debugger work, `ddb(4)` documents the in-kernel debugger
in full, including every builtin command, every script hook, and
every way to enter the debugger. When in doubt, read this page
before using a DDB command you have not tried before. For offline
post-mortem analysis, `kgdb(1)` on your installed FreeBSD system
documents the kernel-specific extensions over stock `gdb`. The
underlying access layer is in libkvm, described in
`/usr/src/lib/libkvm/kvm_open.3`, which explains both the dump and
the live-kernel modes you met in Section 3.

Two smaller pointers are worth keeping in your reading queue. The
first is `/usr/src/share/examples/witness/lockgraphs.sh`, a tiny
shell script shipped in the base system that demonstrates how to
turn `WITNESS`'s accumulated lock-order graph into a visual diagram.
On a real driver, running it once gives you a picture of where your
locks sit relative to the rest of the kernel's lock hierarchy, and
it can be surprising. The second is the FreeBSD kernel source tree
itself: reading `/usr/src/sys/kern/kern_shutdown.c` (the panic and
dump path) and `/usr/src/sys/kern/kern_mutex.c` (the mutex
implementation that `WITNESS` instruments) grounds the whole
debugging workflow in the code that actually implements it.

Beyond the source tree, the FreeBSD Developers' Handbook and the
FreeBSD Architecture Handbook both contain longer-form articles on
kernel debugging. Both are shipped with the documentation set on
any FreeBSD system and kept up to date alongside the source. They
are worth browsing once, even if you do not read them end to end,
because they give names to patterns you will later recognize in
your own debugging sessions.

A final note on choosing references. Man pages age more slowly than
blog posts, and source comments age more slowly than man pages.
When two references disagree, trust the source, then the man page,
then the handbook, then everything else. That hierarchy has served
FreeBSD developers well for decades, and the habit will serve you
through the rest of this book and the rest of your career.

## Wrapping Up

Advanced debugging is a patient craft. Each tool in this chapter
exists because someone, somewhere, faced a bug that could not be
found any other way. `KASSERT` exists because invariants that
live only in the programmer's head are not invariants. `kgdb` and
crash dumps exist because some bugs destroy the machine that
produced them. `DDB` exists because a frozen kernel cannot
explain itself through any other channel. `WITNESS` exists
because deadlocks are catastrophic in production and impossible
to debug after the fact. `memguard(9)` exists because silent
memory corruption was the hardest class of bug until someone
built a tool that made it loud.

None of these tools replace understanding. A debugger cannot
tell you what your driver should do. A crash dump cannot tell
you the right locking discipline. DTrace cannot infer your
design. The tools are instruments; you are the player. The
music is the shape of the driver you are building.

The habits that make this craft successful are small and
undramatic. Develop on a debug kernel. Add assertions for every
invariant you can articulate. Capture dumps routinely so you
can open them without ceremony. Keep a journal when you are
chasing something hard. Read the FreeBSD source when a
mechanism mystifies you. Reach for the lightest tool that will
answer your question, and graduate to heavier tools only when
the lighter ones come up short.

Debugging is also a social craft. A bug that takes you a day
to find, written up clearly, can save another author a week.
Good commit messages, detailed test cases, and honest accounts
of what worked and what did not are contributions to the
common practice. The FreeBSD project's historical patience with
bug reports, its habit of capturing root causes in commit logs,
and its consistent use of `KASSERT` and `WITNESS` across
decades of drivers all stem from this collective habit of
treating bug hunting as a shared responsibility.

You now have the toolkit to participate. Load a debug kernel,
pick a driver in `/usr/src/sys/dev/` that interests you, and
read it with a debugger's eye. Where are the invariants? Where
are the assertions? Where is the locking discipline? Where
might a bug hide, and what tool would catch it? The exercise
sharpens the instincts the rest of this book has been building.

In the next chapter, we will leave correctness behind and look
at how drivers handle asynchronous I/O and event-driven work:
the patterns by which a driver serves many users at once
without blocking, and the kernel facilities that make such
designs possible. The debugging skills you have gained here will
serve you well in that territory, because asynchronous code is
where subtle concurrency bugs tend to live. A driver with solid
assertions, a clean lock ordering verified by `WITNESS`, and a
set of SDT probes to trace its event flow is also a driver that
is much easier to reason about when its work is spread across
callbacks, timers, and kernel threads.

## Bridge to Chapter 35: Asynchronous I/O and Event Handling

Chapter 35 picks up where this one leaves off. Synchronous code
is straightforward to reason about: a call arrives, the driver
does its work, the call returns. Asynchronous code is not:
callbacks fire at unpredictable times, events arrive out of
order, and the driver must manage state that persists across
many thread contexts.

The complexity of asynchronous drivers is exactly the kind of
complexity that benefits from the tools of this chapter. A
synchronous driver with a bug might crash at a predictable
place. An asynchronous driver with a bug might crash hours
later, in a callback that has no obvious connection to the
original misbehavior. `KASSERT` on the state at each callback
entry catches such bugs early. DTrace probes on each event
transition make the sequence visible. `WITNESS` detects the
deadlocks that arise naturally when multiple asynchronous paths
need to coordinate.

In the next chapter we will meet the building blocks of
asynchronous work in FreeBSD: `callout(9)` for deferred timers,
`taskqueue(9)` for background work, `kqueue(9)` for event
notification, and the patterns for using them correctly. We
will build a driver that serves many concurrent users without
blocking, and we will exercise the debugging techniques of this
chapter to keep that complexity under control.

By the time you finish Chapter 35, you will have the full
synchronous and asynchronous toolkit: drivers that handle
traffic efficiently, scale to many users, maintain correctness
under concurrency, and can be debugged when something
nevertheless goes wrong. That combination is what it takes to
write drivers that survive in production.

See you in Chapter 35.
