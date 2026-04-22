---
title: "Asynchronous I/O and Event Handling"
description: "Implementing asynchronous operations and event-driven architectures"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 35
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 135
---

# Asynchronous I/O and Event Handling

## Introduction

Up to this point, almost every driver we have written has operated on a
simple schedule. A user process calls `read(2)` and waits. Our driver
produces data, the kernel copies it out, and the call returns. A user
process calls `write(2)` and waits. Our driver consumes the data, stores
it, and the call returns. The user thread sleeps while the driver works,
and it wakes up when the work is done. This is the synchronous model, and
it is the right starting point for teaching drivers because it matches
the shape of an ordinary function call: you ask for something, you wait,
you get an answer.

Synchronous I/O works well for many devices, but it fails for others. A
keyboard does not decide to produce a keypress because a program called
`read()`. A serial port does not time its incoming bytes to match the
reader's schedule. A sensor might produce data at irregular intervals, or
only when something interesting happens in the physical world. If we
insist that every user of such a device must block in `read()` until the
next event arrives, we force the userland program into a terrible choice.
It can either dedicate a thread to blocking on each device, which makes
the program hard to write and slow to respond to anything else, or it can
loop in userland calling `read()` with a short timeout over and over,
which wastes CPU cycles and still misses events that happen between
polls.

FreeBSD solves this problem by giving drivers a set of asynchronous
notification mechanisms, each built on the same underlying idea: a
process does not need to block in `read()` to learn that data is ready.
It can instead register interest in a device, go do other useful work,
and let the kernel wake it up when the device has something to say. The
mechanisms differ in their details, their performance profiles, and their
intended use cases, but they share a common shape. A waiter declares what
it is waiting for, the driver records that interest, the driver later
discovers that the condition is satisfied, and the driver delivers a
notification that causes the waiter to be woken, scheduled, or signalled.

Four of these mechanisms matter to driver authors. The classic `poll(2)`
and `select(2)` system calls let a userland program ask the kernel which
of a set of file descriptors are ready. The newer `kqueue(2)` framework
offers a more efficient, more expressive event interface and is the
preferred choice for modern high-performance applications. The
`SIGIO` signal mechanism, invoked through `FIOASYNC` and `fsetown()`,
delivers signals to a registered process whenever the device state
changes. And drivers that need to track their own internal events
typically build a small event queue inside the softc so that readers see
a consistent sequence of readable records rather than raw hardware state.

In this chapter we will learn how each of these mechanisms works, how to
implement it correctly in a character driver, how to combine them so that
a single driver can serve callers from `poll(2)`, `kqueue(2)`, and
`SIGIO` simultaneously, and how to audit the resulting code for the
subtle races and missed wakeups that are the bane of asynchronous
programming. We will ground every piece in real FreeBSD 14.3 source,
looking at how `if_tuntap.c`, `sys_pipe.c`, and `evdev/cdev.c` solve the
same problems in production.

By the end of the chapter you will be able to take a blocking driver and
give it full asynchronous support without breaking its synchronous
semantics. You will know how to implement `d_poll()`, `d_kqfilter()`, and
`FIOASYNC` handlers correctly. You will understand why `selrecord()` and
`selwakeup()` must be called in a specific order with specific locking.
You will know what a `knlist` is, how `knote` attaches to it, and why
`KNOTE_LOCKED()` is the call you want in almost every driver. You will
see how `fsetown()` and `pgsigio()` combine to deliver signals to exactly
the right process. And you will know how to build an internal event
queue that ties the whole mechanism together so that each asynchronous
notification leads the reader to a single, consistent, well-defined
record in the driver.

Throughout the chapter we will develop a companion driver called
`evdemo`. It is a pseudo-device that simulates an event source:
timestamps, state transitions, and occasional "interesting" events that
a userland program wants to observe in real time. Each section of the
chapter builds another layer onto `evdemo`, so by the end you will have
a small but complete asynchronous driver that you can load, inspect, and
extend. Like `bugdemo` in the previous chapter, `evdemo` touches no real
hardware, so every experiment is safe to run on a development FreeBSD
virtual machine.

## Reader Guidance: How to Use This Chapter

This chapter sits in Part 7 of the book, in the Mastery Topics part,
directly after Advanced Debugging. It assumes you have already written at
least a simple character driver, know how to load and unload a module
safely, and have worked with the synchronous `read()`, `write()`, and
`ioctl()` handlers. If any of those feel uncertain, a quick revisit of
Chapters 8 through 12 will pay for itself several times in this one.

You do not need to have finished every earlier mastery chapter to follow
this one. A reader who has mastered the basic character driver pattern
and has seen `callout(9)` or `taskqueue(9)` in passing will be able to
keep up. Where a previous chapter's material is essential, we will give
you a short reminder in the relevant section.

The material is cumulative within the chapter. Each section adds a new
asynchronous mechanism to the `evdemo` driver, and the final refactor
ties them together. You can skim forward to learn about one specific
mechanism, but the labs read most naturally in sequence, because later
labs assume the code from the earlier ones.

You do not need any special hardware. A modest FreeBSD 14.3 virtual
machine is enough for every lab in the chapter. A serial console is
useful but not required. You will want a second terminal open so you can
watch `dmesg`, run the user-space test programs, and monitor wait
channels in `top(1)` while the driver is loaded.

A reasonable reading schedule looks like this. Read the first three
sections in one sitting to build the mental model for poll and select.
Take a break. Read Sections 4 and 5 on a separate day, because `kqueue`
and signals each introduce a new set of ideas. Work through the labs at
your own pace. The chapter is long on purpose: asynchronous I/O is where
a lot of driver complexity lives, and rushing through the material is
the surest way to write a driver that works most of the time but misses
wakeups in rare cases.

Some of the code in this chapter deliberately does the wrong thing so
that we can see the symptoms of common mistakes. Those examples are
clearly labelled. The finished labs do the right thing, and the final
refactored driver is safe to load.

## How to Get the Most Out of This Chapter

The chapter follows a pattern you will see repeated in every section.
First we explain what a mechanism is and what problem it solves. Then we
show how userland expects it to behave, so that you understand the
contract your driver must honour. Then we look at the real FreeBSD
kernel source to see how existing drivers implement the mechanism. And
finally we apply it to the `evdemo` driver in a lab.

Several habits will help you absorb the material.

Keep a terminal open to `/usr/src/` so that you can look up any FreeBSD
source file the chapter references. Asynchronous I/O is one of the areas
where reading real drivers pays off the most, because the patterns are
short enough to see in one pass and the variations between drivers
teach you what is essential and what is merely stylistic. When the
chapter mentions `if_tuntap.c` or `sys_pipe.c`, open the file and look.
A minute spent with the real source builds more intuition than any
secondhand description.

Keep a second terminal open to your FreeBSD virtual machine so that you
can load and unload `evdemo` as the chapter progresses. Type the code
yourself the first time you see it. The companion files under
`examples/part-07/ch35-async-io/` contain the finished sources, but
typing the code builds muscle memory that reading does not. When a
section introduces a new callback, add it to the driver, rebuild, reload,
and test before moving on.

Pay close attention to locking. Asynchronous I/O is the area where a
careless lock acquisition can turn a clean driver into a deadlock or a
silent data corruption. When the chapter shows a mutex being acquired
before a call to `selrecord()` or `KNOTE_LOCKED()`, notice the order and
ask yourself why it must be that way. When a lab instruction says to
take the softc mutex before modifying an event queue, take it.
Discipline about locking is the single habit that most reliably
separates working asynchronous drivers from ones that mostly work.

Finally, remember that asynchronous code tends to reveal its bugs only
under pressure. A driver that passes a single-threaded test may still
have missed wakeups or races that manifest when two or three threads
contend for the same device. Several labs in this chapter include
multi-reader stress tests for exactly that reason. Do not skip them.
Running your code under contention is the best way to prove that it
works for real.

With those habits in mind, let us begin with the difference between
synchronous and asynchronous I/O, and the question of when each is the
right choice.

## 1. Synchronous vs. Asynchronous I/O in Device Drivers

Synchronous I/O is the model we have used in almost every driver so far.
A user process calls `read(2)`. The kernel dispatches into our `d_read`
callback. We either hand back the data that is already available, or we
put the calling thread to sleep on a condition variable until data
arrives. When the data is ready, we wake the thread, it copies the data
out, and `read(2)` returns. The user program blocks for the duration of
the call, then resumes.

This pattern is easy to reason about. It matches the way ordinary
functions work: you call, you wait, you get a result. It is also a very
good fit for devices where the caller's demand drives the device's work.
A disk reader asks for data, and the disk controller is told to fetch
it. A sensor with a `read_current_value` operation naturally fits a
synchronous call. For these devices, the user process always knows when
to ask, and the cost of the wait is the cost of the actual I/O.

But for many real devices, the driver's work is not driven by the
caller's demand. It is driven by the world.

### The World Does Not Wait for read()

Consider a keyboard. The device has no opinion about who is calling
`read(2)` when a key is pressed. The user hits the key, an interrupt
fires, the driver pulls a scan code out of the hardware, and the data is
now available. If a userland program is blocked in `read()`, it wakes
up and gets the key. If no program is reading, the key sits in a
buffer. If several programs share interest in the keyboard, only one of
them receives the key under classic blocking semantics, which is almost
never what the programmer wants.

Consider a serial port. Bytes arrive at the speed of the wire,
independent of any program's readiness to receive them. If the driver
blocks every incoming byte behind a reader, it effectively forces the
reader to keep one thread always asleep in `read()`, just in case
something happens. That thread cannot do anything else. A single
well-designed process might want to react to several serial ports, a
network socket, a timer, and a keyboard, all at once. The synchronous
model cannot express that.

Consider a USB sensor that reports a value only when the measured
quantity crosses a threshold. A temperature sensor might raise an event
only when the temperature changes by more than half a degree. A motion
sensor might fire only when motion is detected. The device's own
schedule, not the userland's schedule, decides when data is ready. A
reader who blocks in `read()` might wait milliseconds, or seconds, or
minutes, or forever.

Each of these situations shares a property: the event is external to the
program's request. The driver knows when data is ready. The userland
does not. If the userland has to block in `read()` every time to learn
what the driver knows, the program is held hostage to the driver's
rhythm.

### Why Busy Waiting Is a Bad Answer

One naive solution is for the userland program to poll the driver.
Instead of calling `read()` once and blocking, it calls `read()` in
non-blocking mode over and over. `open(/dev/...)` with `O_NONBLOCK`
returns immediately if no data is available. The program can spin in a
loop, calling `read()`, doing other work, calling `read()` again, and so
on.

This pattern is called busy waiting, and it is almost always wrong. It
burns CPU even when nothing is happening, because the program keeps
asking the driver whether it has work. It misses events that happen
between polls. It adds latency to every event: a key pressed a hundred
microseconds after the last poll waits until the next poll to be seen.
And it scales poorly: a program that watches ten such devices has to
poll ten of them in every iteration, making all the problems worse.

Busy waiting is appropriate in exactly one situation: when the polling
frequency is known, the device latency is measured in microseconds, and
the program has no other work. Even in that case, the right answer is
usually to use the CPU's high-precision timing facilities and `usleep()`
between polls rather than spin. For any other case, busy waiting is the
wrong tool.

The synchronous blocking model and the busy-waiting model are the two
endpoints of a spectrum. Both waste resources. What we want is a third
option: the userland asks the kernel to tell it when the device is
ready, then does other work until the kernel raises its hand. That
third option is what asynchronous I/O provides.

### Asynchronous I/O Is Not Just Non-blocking Read

A common beginner mistake is to think that asynchronous I/O means
calling `read()` with `O_NONBLOCK`. It does not. Non-blocking `read()`
returns immediately if data is not available; that is a useful property,
but it is not asynchronous I/O on its own. Non-blocking `read()` without
a notification mechanism is just busy waiting dressed up a little.

Asynchronous I/O, in the sense this chapter uses the term, is a
notification protocol between the driver and the userland. The userland
does not have to be reading to learn that the driver has data. The
driver does not have to guess who is interested. When the driver's
state changes in a relevant way, it notifies its waiters through a
well-defined mechanism: `poll`/`select`, `kqueue`, `SIGIO`, or some
combination of those. The waiter wakes up, reads the data, and goes
back to waiting.

This distinction matters because it separates three independent
concerns in a driver:

The first concern is wait registration. A userland program declares
interest in a device by calling `poll()`, `kevent()`, or by enabling
`FIOASYNC`. The driver remembers that registration so that it can find
the waiter later.

The second concern is wakeup delivery. When the driver's state changes,
it calls `selwakeup()`, `KNOTE_LOCKED()`, or `pgsigio()` to deliver the
notification. This is a separate operation from producing the data. A
driver can produce data without delivering a notification (for example,
during an initial fill that happens before anyone has registered). A
driver can deliver a notification without producing data (for example,
when a device hangs up). And a driver can deliver several
notifications for one unit of data, if several mechanisms are
registered.

The third concern is event ownership. A `SIGIO` signal is delivered to a
specific process or process group. A `knote` belongs to a specific
`kqueue`. A `select()` waiter belongs to a specific thread. If the
driver cannot match the wakeup to the right owner, notifications get
lost or delivered to the wrong party. Each mechanism has its own rules
for matching notifications to owners, and we must get those rules right
for each one separately.

Keeping these three concerns distinct is one of the main themes of this
chapter. A lot of the subtle bugs in asynchronous drivers come from
mixing them up. If you find yourself wondering why a specific wakeup
call exists or why a specific lock is held, nine times out of ten the
answer lies in keeping registration, delivery, and ownership separate.

### Real-World Patterns: Event Sources That Want Asynchronous I/O

It helps to name the patterns where asynchronous I/O is the right
choice, because once you recognise them you will see them everywhere.

Character input devices are the classic case. A keyboard, a mouse, a
touchscreen, a joystick: each produces events when the user interacts
with it, at a rate nobody can predict in advance. The user might press a
key now, or in five minutes. The driver knows when the event arrives.
Userland needs a way to learn.

Serial and network interfaces are another case. Bytes arrive from the
wire at the wire's pace. A terminal emulator does not want to block
waiting for the next byte, because it also has to redraw its screen,
respond to keyboard input, and update its cursor. A network program
does not want to block waiting for the next packet, because it usually
has to watch several sockets at once.

Sensors that report on condition are a third case. A button that reports
"pressed" or "released." A temperature sensor that fires when the
measured value crosses a threshold. A motion detector. A door contact.
All of these are event-driven in the strict sense: nothing happens until
something interesting happens in the world.

Control lines and modem signals are a fourth case. The `CARRIER`,
`DSR`, and `RTS` lines on a serial port change state independently of
the data flow. A program that cares about them wants to be told when
they change, not to poll them continuously.

Any device that combines several kinds of events into one stream is a
fifth case. Consider an `evdev` input device that aggregates keystrokes,
mouse motion, and touchscreen events into a unified event stream. The
driver builds an internal queue of events, one record per interesting
thing, and readers draw events from the queue. We will build a small
version of exactly this pattern later in the chapter, because it
illustrates how an event queue, asynchronous notification, and
synchronous `read()` semantics combine into one well-structured driver.

### When Not to Use Asynchronous I/O

For the sake of balance, let us name a few cases where asynchronous I/O
is not the right answer.

A driver whose only operation is a bulk transfer at the caller's
request has no reason to expose `poll()` or `kqueue()`. If every
interaction is a round trip that the user initiates, the synchronous
blocking model is both simpler and correct. Adding asynchronous
notification to such a driver only increases complexity.

A driver whose data rate is so high that any notification overhead
matters might need a different approach entirely. `netmap(4)` and
similar kernel-bypass frameworks exist for precisely this case, and they
are well beyond the scope of this chapter. An ordinary `kqueue()`-based
design is fine up to millions of events per second, but at some point
the cost of any notification mechanism becomes a bottleneck.

A driver whose consumer is another kernel subsystem rather than a
userland program usually does not need userland-facing asynchronous
notification at all. It needs in-kernel synchronization: mutexes,
condition variables, `callout(9)`, `taskqueue(9)`. Those are the
patterns we studied in earlier chapters, and they remain the right
answer when both sides of the event live inside the kernel.

For everything in between, asynchronous I/O is the right tool, and
learning it properly is one of the most durable skills a driver author
can acquire. The next three sections build up the mental model and the
code: first `poll()` and `select()`, then `selrecord()` and
`selwakeup()`, then `kqueue()`. The later sections add signals, event
queues, and the combined design.

### A Mental Model for the Rest of the Chapter

Before we move on, let us fix a mental model that will guide the rest
of the chapter. Every asynchronous driver has three kinds of code paths.

The first is the producer path. This is where the driver learns that
something has happened. For hardware, it is the interrupt handler. For
a pseudo-device like `evdemo`, it is whatever code simulates the event.
The producer's job is to update the driver's internal state so that a
reader who looked now would see the new event.

The second is the waiter path. This is where a userland caller
registers interest. The caller's thread enters the kernel through a
system call (`poll`, `select`, `kevent`, or `ioctl(FIOASYNC)`), the
kernel dispatches to our `d_poll` or `d_kqfilter` callback, and we
record the caller's interest in a way that the producer can find later.

The third is the delivery path. This is where the producer notifies the
waiters. The producer has just updated state. It calls `selwakeup()`,
`KNOTE_LOCKED()`, `pgsigio()`, or some combination of those, and those
calls wake up the waiting threads, which then typically call `read()`
to pick up the actual data.

This three-path model is the frame through which we will approach every
mechanism. When we study `poll()`, we will ask: what is the producer
doing, what does the waiter register, and what does the delivery look
like? When we study `kqueue()`, we will ask the same three questions.
When we study `SIGIO`, same three questions. The mechanisms differ in
their details, but they all fit the same shape, and knowing the shape
makes each one easier to learn.

With the mental model established, let us look at `poll(2)` and
`select(2)`, the oldest and most portable of the three mechanisms.

## 2. Introducing poll() and select()

The `poll(2)` and `select(2)` system calls are the original UNIX answer
to the question, "how do I wait on multiple file descriptors at once?"
They have been in UNIX for decades, they work across every platform
that matters, and they are still the most portable way for a userland
program to watch several devices, sockets, or pipes in one loop.

They share the same underlying abstraction. A program passes a set of
file descriptors and a mask of events it cares about: readable,
writable, or exceptional. The kernel examines each descriptor, asks its
driver or subsystem whether the event is ready, and if none are, puts
the calling thread to sleep until one becomes ready or a timeout
expires. When it wakes, the kernel returns which descriptors are now
active, and the program can service them.

From the driver's point of view, `poll` and `select` both funnel into
the same `d_poll` callback on a `cdev`. Whether the userland program
used `poll(2)` or `select(2)` is invisible to the driver. We answer one
question: given this set of events the caller is interested in, which
of them are ready right now? If none are ready, we also register the
caller so that we can wake it when something becomes ready.

That double role (answer now, register for later) is the heart of the
`d_poll` contract. The driver must answer the current state immediately
and must not forget the waiter if the answer was "nothing." Getting
either half wrong produces the two classic poll bugs. If the driver
reports "not ready" when data actually is ready, the caller goes to
sleep and never wakes, because no further event will occur to trigger a
wakeup. If the driver fails to register the waiter when nothing is
ready, the caller also never wakes, because the driver never knows who
to wake when data finally does arrive. Both bugs produce the same
symptom (a hung process) and both are a consequence of failing to
implement exactly the right pattern.

### What Userland Expects From poll() and select()

Before we implement `d_poll`, it helps to know exactly what the userland
caller is doing. The user code typically looks something like this:

```c
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

struct pollfd pfd[1];
int fd = open("/dev/evdemo", O_RDONLY);

pfd[0].fd = fd;
pfd[0].events = POLLIN;
pfd[0].revents = 0;

int r = poll(pfd, 1, 5000);   /* wait up to 5 seconds */
if (r > 0 && (pfd[0].revents & POLLIN)) {
    /* data is ready; do a read() now */
    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf));
    /* ... */
}
```

The user passes an array of `struct pollfd`, each with an `events` mask
indicating what it cares about. The kernel returns by writing the
`revents` field with the events that are actually ready. The third
argument is a timeout in milliseconds, with `-1` meaning "wait forever"
and `0` meaning "do not block at all, just poll the state."

`select(2)` does the same thing with a slightly different API: three
`fd_set` bitmaps for readable, writable, and exceptional descriptors,
and a timeout as a `struct timeval`. Inside the kernel, both calls
normalize into the same operation on each involved descriptor, which
ends in our `d_poll` callback.

The caller expects these semantics:

If any of the requested events is currently ready, the call must
return promptly with the ready events set.

If none of the requested events is ready and the timeout has not
expired, the call must block until one of the events becomes ready or
the timeout fires.

If the descriptor is closed or becomes invalid during the call, the
kernel returns `POLLNVAL`, `POLLHUP`, or `POLLERR` as appropriate.

The event mask bits that a driver typically deals with are:

`POLLIN` and `POLLRDNORM`, both meaning "data is available to read."
FreeBSD defines `POLLRDNORM` as distinct from `POLLIN`, but in most
driver code we treat them together because programs commonly ask for
one or the other and expect either to work.

`POLLOUT` and `POLLWRNORM`, both meaning "the device has buffer space
to accept a write." FreeBSD defines `POLLWRNORM` as identical to
`POLLOUT`, so in practice they are the same bit.

`POLLPRI`, meaning "out-of-band or priority data is available." Most
character drivers do not have a priority notion and leave this alone.

`POLLERR`, meaning "an error has occurred on the device." The driver
typically sets this when something has gone wrong and the device
cannot recover.

`POLLHUP`, meaning "the peer has hung up." A pty master sees this when
the slave closes. A pipe reader sees it when the writer closes. A
device driver usually sets this during the detach path, or when a
layered service has disconnected.

`POLLNVAL`, meaning "the request is not valid." The driver usually
leaves this bit to the kernel framework, which sets it when the
descriptor is invalid or when the driver has no `d_poll`.

The combination of `POLLHUP` and `POLLIN` is worth noting: when a
device closes and it had buffered data, readers should see `POLLHUP`
along with `POLLIN`, because the buffered data can still be read even
though no more is coming. Well-written userland programs handle this
case explicitly.

### The d_poll Callback

Now we can look at the `d_poll` callback itself. Its signature, defined
in `/usr/src/sys/sys/conf.h`, is:

```c
typedef int d_poll_t(struct cdev *dev, int events, struct thread *td);
```

The `dev` argument is our `cdev`, from which we retrieve the softc
through `dev->si_drv1`. The `events` argument is the mask of events the
caller is interested in. The `td` argument is the calling thread, which
we need to pass to `selrecord()` so that the kernel can match future
wakeups to the right waiter. The return value is the subset of `events`
that are ready right now.

A skeletal implementation looks like this:

```c
static int
evdemo_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int revents = 0;

    mtx_lock(&sc->sc_mtx);

    if (events & (POLLIN | POLLRDNORM)) {
        if (evdemo_event_ready(sc))
            revents |= events & (POLLIN | POLLRDNORM);
        else
            selrecord(td, &sc->sc_rsel);
    }

    if (events & (POLLOUT | POLLWRNORM))
        revents |= events & (POLLOUT | POLLWRNORM);

    mtx_unlock(&sc->sc_mtx);
    return (revents);
}
```

This is the classic pattern. Let us walk through it one line at a time.

We take the softc mutex because we are about to look at the driver's
internal state, and no other thread should be modifying it while we
decide whether an event is ready. Holding the lock while we call
`selrecord()` is also what closes the race between the answer and the
registration, as we will see in a moment.

We look at each event type the caller cares about. For the readable
events, we ask the driver whether any data is ready. If it is, we add
the matching bits to `revents`. If it is not, we call `selrecord()` to
register this thread as a waiter on the `sc_rsel` selinfo. That
selinfo lives in the softc, is shared across all potential waiters, and
is what we will later pass to `selwakeup()` when data arrives.

For the writable events, we do not have an internal buffer that can
fill up in this example, so we always report the device as writable.
Many drivers are in this category: writes always fit. Drivers with
bounded buffers should check the buffer state the same way they check
the read state, and only report `POLLOUT` when there is space.

We release the lock and return the mask of ready events.

Three things about this pattern deserve emphasis.

First, we return immediately with `revents` in every case. The `d_poll`
callback does not sleep. If nothing is ready, we register a waiter and
return zero. The kernel's generic poll framework takes care of the
actual blocking: after `d_poll` returns, the kernel atomically sleeps
the thread if no file descriptor returned any events. The driver
author does not see this sleep; it is handled entirely by the poll
dispatch logic in the kernel.

Second, we must call `selrecord()` only for event types that are not
currently ready. If an event is ready and we also call `selrecord()`,
we do not break anything (the framework handles this), but it is
wasteful: the thread is not going to sleep, so registering it is
pointless. The pattern "check, and if not ready then register" keeps
the work proportional.

Third, the lock we hold during the check and the `selrecord()` call is
the same lock we will take during the producer path when we call
`selwakeup()`. This is what prevents the missed wakeup race: if the
producer fires after we check the state but before we register the
waiter, the producer cannot deliver the wakeup until our `selrecord()`
has completed, so the wakeup will find us. We will look at this in
detail in Section 3.

### Registering the d_poll Method on the cdevsw

To make our driver answer `poll()` calls, we fill in the `d_poll` field
of the `struct cdevsw` we pass to `make_dev()` or `make_dev_s()`:

```c
static struct cdevsw evdemo_cdevsw = {
    .d_version = D_VERSION,
    .d_name    = "evdemo",
    .d_open    = evdemo_open,
    .d_close   = evdemo_close,
    .d_read    = evdemo_read,
    .d_write   = evdemo_write,
    .d_ioctl   = evdemo_ioctl,
    .d_poll    = evdemo_poll,
};
```

If we do not set `d_poll`, the kernel provides a default. In
`/usr/src/sys/kern/kern_conf.c`, the default is `no_poll`, which calls
`poll_no_poll()`. That default returns the standard readable and
writable bits unless the caller asked for anything exotic, in which
case it returns `POLLNVAL`. The behaviour makes sense for devices that
are always ready, like `/dev/null` and `/dev/zero`, but it is almost
never what you want for an event-driven device. For any driver that
has real asynchronous semantics, you want to implement `d_poll`
yourself.

### What Real Drivers Look Like

Let us look at two real implementations, because the pattern will
become clearer when you see it in production code.

Open `/usr/src/sys/net/if_tuntap.c` and find the function `tunpoll`.
It is short enough to quote:

```c
static int
tunpoll(struct cdev *dev, int events, struct thread *td)
{
    struct tuntap_softc *tp = dev->si_drv1;
    struct ifnet    *ifp = TUN2IFP(tp);
    int     revents = 0;

    if (events & (POLLIN | POLLRDNORM)) {
        IFQ_LOCK(&ifp->if_snd);
        if (!IFQ_IS_EMPTY(&ifp->if_snd)) {
            revents |= events & (POLLIN | POLLRDNORM);
        } else {
            selrecord(td, &tp->tun_rsel);
        }
        IFQ_UNLOCK(&ifp->if_snd);
    }
    revents |= events & (POLLOUT | POLLWRNORM);
    return (revents);
}
```

This is our skeleton almost verbatim, with the `tun` driver's outgoing
packet queue as the data source and the `tun_rsel` selinfo as the
wait point. The lock here is `IFQ_LOCK`, the queue lock, which the
producer also takes before modifying the queue and calling
`selwakeuppri()`. That matched locking is what makes the design
correct.

Now open `/usr/src/sys/dev/evdev/cdev.c` and find `evdev_poll`. This
is a slightly longer and more instructive example because it handles
a revoked device explicitly:

```c
static int
evdev_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdev_client *client;
    int ret;
    int revents = 0;

    ret = devfs_get_cdevpriv((void **)&client);
    if (ret != 0)
        return (POLLNVAL);

    if (client->ec_revoked)
        return (POLLHUP);

    if (events & (POLLIN | POLLRDNORM)) {
        EVDEV_CLIENT_LOCKQ(client);
        if (!EVDEV_CLIENT_EMPTYQ(client))
            revents = events & (POLLIN | POLLRDNORM);
        else {
            client->ec_selected = true;
            selrecord(td, &client->ec_selp);
        }
        EVDEV_CLIENT_UNLOCKQ(client);
    }
    return (revents);
}
```

Note two extra pieces of behaviour that we did not have in the skeleton.

When the client has been revoked (which happens when the device is
being detached while the client still has the file descriptor open),
the function returns `POLLHUP` so that the userland program knows to
give up. This is the right handling of the detach case. Our
skeleton does not do this yet, but the final refactored `evdemo` will.

The driver sets a flag, `ec_selected`, to remember that a waiter has
been registered. This lets the producer avoid calling `selwakeup()`
for clients that have never polled, which is a small optimization.
Most drivers skip this optimization and just call `selwakeup()` every
time, which is simpler and still correct.

### What the User Sees

On the userland side, the caller does not care which implementation
we picked. It calls `poll()` with a timeout and sees the result. The
first call returns zero if nothing is ready and the timeout expires,
or a positive number of ready descriptors otherwise. The second call
sees the `revents` bitmask and dispatches to the right handling.

This is the clean separation asynchronous I/O achieves. The user
program does not know or care about `selinfo` or `knlist`. It knows
only that it asked the kernel "is this ready yet?" and got an answer.
The driver's job is to make that answer truthful and to ensure that
the next relevant event will wake the waiter.

### Wrapping Up Section 2

We now have the userland view of poll and select, the kernel signature
of `d_poll`, and a first skeletal implementation that registers waiters
and reports readable events. But the skeleton is still incomplete. We
have used `selrecord()` without explaining what it really does with the
`struct selinfo`, and we have not yet seen the matching `selwakeup()`
call that produces the notification. That is the subject of the next
section, and it is where the subtle correctness issues of poll-based
asynchronous I/O live.

## 3. Using selwakeup() and selrecord()

`selrecord()` and `selwakeup()` are the two halves of the classic
poll-wait protocol. They have been in BSD kernels since the original
introduction of `select(2)` in 4.2BSD, and they are still the canonical
way to implement wait/wakeup for `poll(2)` and `select(2)` in FreeBSD
drivers. The pair is simple in outline but subtle in detail, and most
of the interesting bugs in poll-based drivers come from getting the
subtlety wrong.

This section takes you through the selinfo machinery step by step.
First we look at what `struct selinfo` actually contains. Then we look
at exactly what `selrecord()` does and what it does not do. Then we
look at `selwakeup()` and its companions. Finally we examine the
classic missed-wakeup race, the locking discipline that prevents it,
and the diagnostic techniques you can use to confirm that your driver
is doing the right thing.

### struct selinfo

Open `/usr/src/sys/sys/selinfo.h` and look at the definition:

```c
struct selinfo {
    struct selfdlist    si_tdlist;  /* List of sleeping threads. */
    struct knlist       si_note;    /* kernel note list */
    struct mtx          *si_mtx;    /* Lock for tdlist. */
};

#define SEL_WAITING(si)    (!TAILQ_EMPTY(&(si)->si_tdlist))
```

Three fields only. `si_tdlist` is a list of threads currently sleeping
on this selinfo because they called `selrecord()` and their `poll()`
or `select()` call decided to block. `si_note` is a `knlist`, which we
will meet in Section 4 when we implement `kqueue` support; it allows
the same selinfo to serve both `poll()` and `kqueue()` waiters.
`si_mtx` is the lock that protects the list.

The `SEL_WAITING()` macro tells you whether any thread is currently
parked on this selinfo. Drivers occasionally use it to decide whether
to bother calling `selwakeup()`, though the wakeup routine itself is
cheap enough that the test is usually unnecessary.

Two important habits for `struct selinfo`:

First, the driver must zero-initialize the selinfo before first use.
The usual way is to embed it in a softc that is zeroed by
`malloc(..., M_ZERO)`, but if you allocate a selinfo separately you
must zero it with `bzero()` or equivalent. An uninitialized selinfo
will crash the kernel the first time `selrecord()` is called on it.

Second, the driver must drain waiters from the selinfo before
destroying it. The canonical sequence at detach time is
`seldrain(&sc->sc_rsel)` followed by `knlist_destroy(&sc->sc_rsel.si_note)`.
The `seldrain()` call wakes up any currently parked waiters so that
they see the descriptor has become invalid rather than blocking
forever. The `knlist_destroy()` call cleans up the knote list for
kqueue waiters, which we will implement in the next section.

### What selrecord() Does

`selrecord()` is called from `d_poll` when the driver decides the
current event is not ready and the thread will need to wait. Its
signature:

```c
void selrecord(struct thread *selector, struct selinfo *sip);
```

The implementation lives in `/usr/src/sys/kern/sys_generic.c`. The
essence of it is short enough to summarize:

1. The function checks that the thread is in a valid poll context.
2. It takes one of the per-thread preallocated `selfd` descriptors
   attached to the thread's `seltd` structure.
3. It links that descriptor into the thread's list of active waits
   and into the `selinfo`'s `si_tdlist`.
4. It remembers the selinfo's mutex on the descriptor, so that the
   wakeup path knows which lock to take.

The key thing to understand is what `selrecord()` does not do. It
does not sleep the thread. It does not block. It does not
transition the thread to any blocked state. It merely records the
fact that this thread has an interest in this selinfo, so that
later, when the kernel's poll dispatch code decides to block the
thread (if no descriptor returned any events), it knows which
selinfos the thread is parked on.

After all of a thread's `d_poll` callbacks have returned, the poll
dispatch code looks at the results. If any file descriptor returned
events, the call returns immediately without blocking. If none did,
the thread goes to sleep. The sleep is on a per-thread condition
variable inside `struct seltd`, and the wakeup is delivered through
that condition variable. The selinfo's role is to link the thread's
`seltd` to all the relevant drivers so that each driver can find the
thread later.

This separation between "record" and "sleep" is what lets a single
`poll()` call monitor many file descriptors. The thread is
registered with every selinfo of every driver it cares about. When
any one of them fires, the wakeup finds the thread through its
`seltd` and walks back out to the poll dispatch, which then looks at
all the registered file descriptors to see which ones are ready.

### What selwakeup() Does

`selwakeup()` is called from the producer path when the driver's
state changes in a way that might satisfy a waiter. Its signature:

```c
void selwakeup(struct selinfo *sip);
```

There is also a variant called `selwakeuppri()` that takes a priority
argument, useful when the driver wants to control the priority at
which the woken thread resumes. In practice, `selwakeup()` is fine
for almost every driver; `selwakeuppri()` is used in a few subsystems
that want to emphasize latency at the cost of fairness.

The implementation walks the selinfo's `si_tdlist` and signals each
parked thread's condition variable. It also walks the selinfo's
`si_note` list and delivers kqueue-style notifications to any knotes
attached there, so a single `selwakeup()` call serves both poll
waiters and kqueue waiters.

Critically, `selwakeup()` must be called after the driver's internal
state has been updated to reflect the new event. If you call
`selwakeup()` before the data is visible, the woken thread runs
through `d_poll` again, sees that nothing is ready (because the
producer has not yet made it visible), re-registers, and sleeps.
When the producer finally updates state, nobody gets woken, because
the re-register happened after the wakeup. The driver then has to
wait for the next event to unstick the waiter, which may never come.

The correct order is always: update state, then wake up. Never the
reverse.

### The Missed Wakeup Race

The most famous bug in poll-based drivers is the missed wakeup. It
looks like this:

```c
/* Producer thread */
append_event(sc, ev);              /* update state */
selwakeup(&sc->sc_rsel);           /* wake waiters */

/* Consumer thread, in d_poll */
if (events & POLLIN) {
    if (event_ready(sc))
        revents |= POLLIN;
    else
        selrecord(td, &sc->sc_rsel);
}
return (revents);
```

If the producer runs between the consumer's `event_ready()` check
and the consumer's `selrecord()` call, the wakeup is lost. The
consumer saw no event, the producer posted an event and called
`selwakeup()` on an empty waiter list, and the consumer then
registered. Nobody is going to call `selwakeup()` again until the
next event arrives. The consumer now sleeps until that next event,
even though an event is already ready.

This is the classic TOCTOU race between the check and the register.
The standard fix is to use a single mutex to serialize the check,
the register, and the wakeup:

```c
/* Producer thread */
mtx_lock(&sc->sc_mtx);
append_event(sc, ev);
mtx_unlock(&sc->sc_mtx);
selwakeup(&sc->sc_rsel);

/* Consumer thread, in d_poll */
mtx_lock(&sc->sc_mtx);
if (events & POLLIN) {
    if (event_ready(sc))
        revents |= POLLIN;
    else
        selrecord(td, &sc->sc_rsel);
}
mtx_unlock(&sc->sc_mtx);
return (revents);
```

Now the check and the register are atomic with respect to the
producer. If the producer updates state before the consumer checks,
the consumer sees the event and returns `POLLIN` without registering.
If the producer is about to update state while the consumer is in the
critical section, the producer has to wait for the consumer to
finish. In both cases, the wakeup reaches the consumer.

The important subtlety is that `selwakeup()` is called outside the
softc mutex. This is the standard pattern in the FreeBSD kernel:
update the state under the lock, drop the lock, deliver the
notification. `selwakeup()` itself is safe to call from many
contexts, but it takes the selinfo's internal mutex, and we do not
want to nest that lock inside an arbitrary driver lock. In practice
the rule is, hold the softc lock across the state update, drop it,
then call `selwakeup()`.

You will see this pattern across FreeBSD drivers. In `if_tuntap.c`
the producer path calls `selwakeuppri()` from outside any driver
lock. In `evdev/cdev.c` the same. The producer updates state under
its internal lock, releases the lock, and then issues the wakeup.
The consumer, in `d_poll`, takes the same lock across the check and
the `selrecord()`. That discipline eliminates the missed wakeup race.

### Thinking About the Lock

Why does this work? Because the lock serializes two specific
operations: the producer's state update and the consumer's check
plus register. The `selwakeup()` call and the thread's subsequent
sleep are outside the lock, but that is fine, because the condition
variable semantics of the underlying mechanism handle that race
separately.

Here is the argument in more detail. Suppose the consumer acquires
the lock first. It checks the state, sees nothing, calls
`selrecord()` to register, and releases the lock. Some time later
the producer acquires the lock, updates the state, releases the
lock, and calls `selwakeup()`. The consumer is already registered,
so the wakeup finds it. Good.

Now suppose the producer acquires the lock first. It updates the
state, releases the lock, and calls `selwakeup()`. The consumer was
not yet registered, so the wakeup finds no waiters. That is fine
because the consumer has not yet reached the point where it would
have slept; the consumer is still about to acquire the lock. When
the consumer does acquire the lock, it checks the state, sees the
event (because the producer has already updated it), and returns
`POLLIN` without calling `selrecord()`. The consumer is correctly
notified.

The third case is the tricky one. The consumer has just checked
the state (under the lock) and is about to call `selrecord()`, but
in fact, because the lock is held the whole time, this case cannot
arise. The producer cannot update the state until the consumer
releases the lock, at which point the consumer has already
registered.

So the lock discipline is: always hold the lock across the consumer's
check and register, and always hold the lock across the producer's
state update. The `selwakeup()` call itself happens outside the
lock, because it has its own internal synchronization.

### Common Mistakes

A few mistakes are worth calling out explicitly.

Calling `selwakeup()` inside the state-update lock is wrong in most
cases because `selwakeup()` itself may need to take other locks (the
selinfo mutex, the thread's selinfo queue lock). Doing this from
within the softc mutex creates a lock-ordering opportunity that is
easy to get wrong. The rule of thumb is, update under the lock,
drop, then `selwakeup()`.

Forgetting to wake up all interested selinfos is the other common
mistake. If the driver has separate read and write selinfos (say,
one for `POLLIN` waiters and one for `POLLOUT` waiters), it must
wake the right one when state changes. Waking the wrong one means
the actual waiter sleeps forever.

Calling `selrecord()` without holding any lock produces a time window
in which the event can arrive without delivering the wakeup. This is
the race we just analyzed, and the fix is always the same: hold the
lock.

Calling `selrecord()` every time, even when data is ready, is not a
correctness bug but it is a pointless load on the per-thread
`selfd` pool. If data is ready, the thread is not going to sleep,
so registering it is wasted work. The pattern "check; if ready,
return; if not, register" is the correct one.

Calling `selwakeup()` on a destroyed selinfo is a crash waiting to
happen. The detach path must call `seldrain()` before freeing the
selinfo or the surrounding softc.

### Diagnostic Techniques

When a driver's poll support is not working, a few tools help you
isolate the problem.

The first tool is `top(1)`. Load the driver, open a descriptor in a
userland program, and have the program call `poll()` with a long
timeout. Look at the program in `top -H` and check the WCHAN
column. If the poll is working correctly, the thread's wait channel
will be `select` or something similar. If the thread is in some
other state (running, runnable, short sleep), the poll call may
have returned prematurely, or the program may be spinning.

The second tool is counters on the driver. Add a counter for each
call to `selrecord()`, one for each call to `selwakeup()`, and one
for each time `d_poll` returns a ready mask. After a test, print
these counters through `sysctl`. If `selrecord()` fires but
`selwakeup()` never does, the producer path is never triggering.
If `selwakeup()` fires but the program stays asleep, you probably
have a missed wakeup because the state-update and register happen
outside the lock.

The third tool is `ktrace(1)` and `kdump(1)`. Run the test program
under `ktrace`, and the dump will show every system call and its
timing. A program that calls `poll()` and blocks will show up as a
`RET poll` entry after the wakeup, and the timestamp will tell you
when the wakeup actually arrived. If the producer event happened at
time T and the wakeup arrived seconds later, you have a bug.

The fourth tool is DTrace, which can instrument `selwakeup` itself.
A script that probes `fbt:kernel:selwakeup:entry` and prints the
calling driver's softc pointer shows every wakeup in the system. If
your driver's wakeup never fires, DTrace will tell you so in
milliseconds.

### Closing the Loop: evdemo With Poll Support

Putting the pieces together, here is the minimum additional code our
`evdemo` driver needs to support `poll()` correctly:

```c
/* In the softc */
struct evdemo_softc {
    /* ... existing fields ... */
    struct selinfo sc_rsel;  /* read selectors */
};

/* At attach */
knlist_init_mtx(&sc->sc_rsel.si_note, &sc->sc_mtx);

/* In d_poll */
static int
evdemo_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int revents = 0;

    mtx_lock(&sc->sc_mtx);
    if (events & (POLLIN | POLLRDNORM)) {
        if (sc->sc_nevents > 0)
            revents |= events & (POLLIN | POLLRDNORM);
        else
            selrecord(td, &sc->sc_rsel);
    }
    if (events & (POLLOUT | POLLWRNORM))
        revents |= events & (POLLOUT | POLLWRNORM);
    mtx_unlock(&sc->sc_mtx);

    return (revents);
}

/* In the producer path (for evdemo, this is the event injection
 * routine triggered from a callout or ioctl) */
static void
evdemo_post_event(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    mtx_lock(&sc->sc_mtx);
    evdemo_enqueue(sc, ev);
    mtx_unlock(&sc->sc_mtx);
    selwakeup(&sc->sc_rsel);
}

/* At detach */
seldrain(&sc->sc_rsel);
knlist_destroy(&sc->sc_rsel.si_note);
```

Notice that we called `knlist_init_mtx()` on the selinfo's embedded
`si_note` knlist even though we are not yet implementing kqueue.
This costs us almost nothing and makes the selinfo compatible with
kqueue support we will add in Section 4. If you do not pre-initialize
`si_note`, the first `selwakeup()` call that tries to walk the knlist
will crash. Many drivers initialize the knlist during attach as a
matter of habit.

Also notice that the `evdemo_post_event` helper holds the softc
mutex while it updates the event count, drops the mutex, and then
calls `selwakeup()`. That is the standard producer pattern, and it
is the one we will reuse throughout the rest of the chapter.

### Wrapping Up Section 3

At this point you have all the conceptual and practical pieces of
poll-based asynchronous I/O. You know the contract, the kernel
structures, the correct locking discipline, and the common failure
modes. You can take an existing blocking driver, add `d_poll`
support, and have it behave correctly under `poll(2)` and
`select(2)`.

The problem is that `poll(2)` and `select(2)` have well-known
scalability limitations. Every call re-declares the full set of
descriptors the caller is interested in, which is O(N) per call.
For programs watching thousands of descriptors, this overhead
dominates. FreeBSD has offered a better mechanism since the late
1990s, namely `kqueue(2)`, and that is the subject of the next
section.

## 4. Supporting kqueue and EVFILT_READ/EVFILT_WRITE

`kqueue(2)` is FreeBSD's scalable event-notification facility. Unlike
`poll(2)` and `select(2)`, which require the userland program to
re-declare its interests on every call, `kqueue(2)` lets the program
register interests once and then ask only for events that have
actually fired. For a program watching ten thousand file descriptors
where only a few are active, this is the difference between a fast,
interactive program and a slow, loaded one.

`kqueue` is also more expressive than `poll`. Beyond the basic
readable and writable filters, it offers filters for signals, timers,
file-system events, process lifecycle events, user-defined events, and
several other categories. A driver that wants to participate only in
the classic readable and writable notifications still fits cleanly
into the framework; the broader features are available if needed.

From the driver's point of view, kqueue support adds one callback to
the `cdevsw`, `d_kqfilter`, and one set of filter operations, a
`struct filterops`, that provides the lifecycle and event-delivery
functions for each filter type. The whole mechanism reuses the
`struct selinfo` we met in Section 3, so drivers that already support
`poll()` can add `kqueue` support by writing about a hundred lines of
extra code and calling a handful of new APIs.

### What kqueue Looks Like to Userland

Before we implement the driver side, let us see what the user program
looks like. A caller opens a `kqueue`, registers interest in a file
descriptor, and then reaps events:

```c
#include <sys/event.h>

int kq = kqueue();
int fd = open("/dev/evdemo", O_RDONLY);

struct kevent change;
EV_SET(&change, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
kevent(kq, &change, 1, NULL, 0, NULL);

for (;;) {
    struct kevent ev;
    int n = kevent(kq, NULL, 0, &ev, 1, NULL);
    if (n > 0 && ev.filter == EVFILT_READ) {
        char buf[256];
        ssize_t r = read(fd, buf, sizeof(buf));
        /* ... */
    }
}
```

The `EV_SET` macro constructs a `struct kevent` describing the
interest: "watch file descriptor `fd` for `EVFILT_READ` events, using
edge-triggered (`EV_CLEAR`) semantics, and keep it active
(`EV_ADD`)." The first `kevent()` call registers that interest. The
loop then calls `kevent()` in blocking mode, asking for the next
event, and services it when it arrives.

The driver never sees the `kqueue` file descriptor or the `kevent`
structure directly. It sees only the per-interest `struct knote` and
its attached `struct filterops`. The registration flows through the
framework to our `d_kqfilter` callback, which chooses the right
filter operations and attaches the knote to our softc. The delivery
flows through `KNOTE_LOCKED()` calls in the producer path, which
walks our knote list and notifies each attached kqueue of the
ready event.

### The Data Structures

Two structures matter on the driver side: `struct filterops` and
`struct knlist`.

`struct filterops`, defined in `/usr/src/sys/sys/event.h`, holds the
per-filter lifecycle functions:

```c
struct filterops {
    int     f_isfd;
    int     (*f_attach)(struct knote *kn);
    void    (*f_detach)(struct knote *kn);
    int     (*f_event)(struct knote *kn, long hint);
    void    (*f_touch)(struct knote *kn, struct kevent *kev, u_long type);
    int     (*f_userdump)(struct proc *p, struct knote *kn,
                          struct kinfo_knote *kin);
};
```

The fields we care about for a driver are:

`f_isfd` is 1 if the filter is attached to a file descriptor. Almost
all driver filters have this set to 1. A filter that watches
something not tied to an fd (like `EVFILT_TIMER`) would set it to 0.

`f_attach` is called when a knote is being attached to a newly
registered interest. Many drivers leave this as `NULL` because all
the attach work happens in `d_kqfilter` itself.

`f_detach` is called when a knote is being removed. The driver
uses this to unregister the knote from its internal knote list.

`f_event` is called to evaluate whether the filter's condition is
currently satisfied. It returns non-zero if yes, zero if no. It is
the kqueue equivalent of the state check in `d_poll`.

`f_touch` is used when the filter supports `EV_ADD`/`EV_DELETE`
updates that should not be treated as full re-registration. Most
drivers leave this as `NULL` and accept the default behaviour.

`f_userdump` is used for kernel introspection and can be left `NULL`
in driver code.

`struct knlist`, defined in the same header, holds a list of knotes
attached to a particular object. It carries pointers to the object's
lock operations so that the kqueue framework can grab and release
the right lock when delivering events:

```c
struct knlist {
    struct  klist   kl_list;
    void    (*kl_lock)(void *);
    void    (*kl_unlock)(void *);
    void    (*kl_assert_lock)(void *, int);
    void    *kl_lockarg;
    int     kl_autodestroy;
};
```

Drivers rarely touch this structure directly. The framework provides
helper functions, starting with `knlist_init_mtx()` for the common
case of a knlist protected by a single mutex.

### Initializing a knlist

The simplest way to initialize a knlist is:

```c
knlist_init_mtx(&sc->sc_rsel.si_note, &sc->sc_mtx);
```

The first argument is the knlist to initialize. The second is the
driver's mutex. The framework stores the mutex and will take it when
needed to protect the knote list. The knote list is usually embedded
in a `struct selinfo`, as we saw in the previous section; reusing
the same selinfo for both poll and kqueue waiters lets a single
`selwakeup()` call cover both mechanisms.

For a driver that is already zeroing the softc through
`M_ZERO`, the initialization is just this one call during attach.

### The d_kqfilter Callback

The `d_kqfilter` callback is the entry point for kqueue registration.
Its signature, in `/usr/src/sys/sys/conf.h`, is:

```c
typedef int d_kqfilter_t(struct cdev *dev, struct knote *kn);
```

The `dev` argument is our `cdev`. The `kn` argument is the knote
being registered. The callback decides which filter operations
apply, attaches the knote to our knote list, and returns zero on
success.

A minimal implementation for a driver that supports `EVFILT_READ`:

```c
static int
evdemo_kqfilter(struct cdev *dev, struct knote *kn)
{
    struct evdemo_softc *sc = dev->si_drv1;

    switch (kn->kn_filter) {
    case EVFILT_READ:
        kn->kn_fop = &evdemo_read_filterops;
        kn->kn_hook = sc;
        knlist_add(&sc->sc_rsel.si_note, kn, 0);
        return (0);
    default:
        return (EINVAL);
    }
}
```

Let us walk through this.

The `switch` on `kn->kn_filter` decides which filter type we are
dealing with. A driver that supports only `EVFILT_READ` returns
`EINVAL` for anything else. A driver that also supports `EVFILT_WRITE`
has a second case that points to a different filter operations
structure.

We set `kn->kn_fop` to the filter operations for this filter type.
The kqueue framework calls these ops as the knote's lifecycle
progresses.

We set `kn->kn_hook` to the softc. The knote has this generic
pointer for per-driver use. Our filter functions will pull the softc
back out of `kn->kn_hook` when they are called.

We call `knlist_add()` to link the knote into our knote list. The
third argument, `islocked`, is zero here because we are not holding
the knlist lock at this point. If we were, we would pass 1.

Return zero to indicate success.

### The filterops Implementation

The filter operations are where the per-filter behaviour lives. For
`EVFILT_READ` on `evdemo`, they look like this:

```c
static int
evdemo_kqread(struct knote *kn, long hint)
{
    struct evdemo_softc *sc = kn->kn_hook;
    int ready;

    mtx_assert(&sc->sc_mtx, MA_OWNED);

    kn->kn_data = sc->sc_nevents;
    ready = (sc->sc_nevents > 0);

    if (sc->sc_detaching) {
        kn->kn_flags |= EV_EOF;
        ready = 1;
    }

    return (ready);
}

static void
evdemo_kqdetach(struct knote *kn)
{
    struct evdemo_softc *sc = kn->kn_hook;

    knlist_remove(&sc->sc_rsel.si_note, kn, 0);
}

static const struct filterops evdemo_read_filterops = {
    .f_isfd   = 1,
    .f_attach = NULL,
    .f_detach = evdemo_kqdetach,
    .f_event  = evdemo_kqread,
};
```

The `f_event` function, `evdemo_kqread`, is called every time the
framework wants to know whether the filter is ready. It looks at the
softc, reports the number of available events in `kn->kn_data` (a
convention that kqueue users rely on to learn how much data is
available), and returns non-zero if at least one event is waiting.
It also flips the `EV_EOF` flag when the device is detaching, which
lets the userland see that no more events are coming.

Note the assertion that the softc mutex is held. The framework takes
our knlist's lock, which we told it is the softc mutex via
`knlist_init_mtx`. Because the f_event callback is invoked inside
that lock, we can look at `sc_nevents` and `sc_detaching` safely.

The `f_detach` function removes the knote from our knlist when the
userland no longer cares about this registration.

The constant `evdemo_read_filterops` is what `d_kqfilter` pointed at
in the previous subsection. `f_isfd = 1` tells the framework that
this filter is tied to a file descriptor, which is the correct value
for any driver-level filter.

### Delivering Events Through KNOTE_LOCKED

On the producer side, we need to notify registered knotes when the
driver's state changes. The macro is `KNOTE_LOCKED()`, defined in
`/usr/src/sys/sys/event.h`:

```c
#define KNOTE_LOCKED(list, hint)    knote(list, hint, KNF_LISTLOCKED)
```

It takes a knlist pointer and a hint. The hint is passed through to
each knote's `f_event` callback, giving the producer a way to pass
context (for example, a specific event type) to the filter. Most
drivers pass zero.

The `KNOTE_LOCKED` variant is what you want when you are already
holding the knlist's lock. The `KNOTE_UNLOCKED` variant is used
when you are not. Since the knlist's lock is usually your softc
mutex, and since the rest of the producer path is holding that
lock anyway, `KNOTE_LOCKED` is the usual choice.

Adding it to our producer path:

```c
static void
evdemo_post_event(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    mtx_lock(&sc->sc_mtx);
    evdemo_enqueue(sc, ev);
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);
    selwakeup(&sc->sc_rsel);
}
```

We now notify both kqueue and poll waiters from the same producer.
`KNOTE_LOCKED` inside the softc mutex walks the knote list and
evaluates each knote's `f_event`, queuing notifications to any
kqueues that have active waiters. `selwakeup` outside the lock
wakes up `poll()` and `select()` waiters. The two mechanisms are
independent and neither interferes with the other.

### Detach: Cleaning Up knlist

At detach time, the driver must drain the knlist before destroying
it. The clean sequence is:

```c
knlist_clear(&sc->sc_rsel.si_note, 0);
seldrain(&sc->sc_rsel);
knlist_destroy(&sc->sc_rsel.si_note);
```

`knlist_clear()` removes every knote that is still attached. After
this call, any userland program that still has a kqueue registration
will see the knote go away on its next reap. `seldrain()` wakes up
any parked `poll()` waiters so that they return. `knlist_destroy()`
checks that the list is empty and frees the internal resources.

The order matters. If you destroy the knlist without clearing it
first, the destroy will panic on the assertion that the list is
empty. If you clear the knlist but leave poll waiters parked, they
will sleep until something wakes them, which is wasteful. Follow
the sequence above and the detach path is clean.

### A Fuller Example: Pipes

Open `/usr/src/sys/kern/sys_pipe.c` and look at the pipe kqfilter
implementation. It is one of the most extensive examples in the
kernel, and it is worth reading in full because pipes support both
read and write filters with proper EOF handling. The key pieces are
the two filterops structures:

```c
static const struct filterops pipe_rfiltops = {
    .f_isfd   = 1,
    .f_detach = filt_pipedetach,
    .f_event  = filt_piperead,
    .f_userdump = filt_pipedump,
};

static const struct filterops pipe_wfiltops = {
    .f_isfd   = 1,
    .f_detach = filt_pipedetach,
    .f_event  = filt_pipewrite,
    .f_userdump = filt_pipedump,
};
```

And the read filter's event function:

```c
static int
filt_piperead(struct knote *kn, long hint)
{
    struct file *fp = kn->kn_fp;
    struct pipe *rpipe = kn->kn_hook;

    PIPE_LOCK_ASSERT(rpipe, MA_OWNED);
    kn->kn_data = rpipe->pipe_buffer.cnt;
    if (kn->kn_data == 0)
        kn->kn_data = rpipe->pipe_pages.cnt;

    if ((rpipe->pipe_state & PIPE_EOF) != 0 &&
        ((rpipe->pipe_type & PIPE_TYPE_NAMED) == 0 ||
        fp->f_pipegen != rpipe->pipe_wgen)) {
        kn->kn_flags |= EV_EOF;
        return (1);
    }
    kn->kn_flags &= ~EV_EOF;
    return (kn->kn_data > 0);
}
```

Note the handling of EOF, the explicit clearing of `EV_EOF` when the
pipe is no longer at EOF (which matters if the named pipe has a new
writer), and the use of `kn->kn_data` to report the amount of
available data. These are the details a finished driver gets right.

### The Anatomy of struct knote

We have been passing a `struct knote` pointer around without looking
closely at it, but the driver's life is easier once we know what it
contains. `struct knote`, defined in `/usr/src/sys/sys/event.h`, is
the kernel's per-registration record. Every call to `kevent(2)` that
registers an interest creates exactly one knote, and that knote
persists until the registration is removed. For a driver, the knote
is the unit of currency: every knlist operation takes a knote, every
filter callback receives a knote, and every delivery walks a list of
them. Knowing what lives inside the structure turns the callback
contracts we have been following into something we can reason about
instead of memorise.

The fields the driver cares about are a small subset of the whole
structure, but each one repays attention.

`kn_filter` identifies which filter type the userland asked for.
Inside `d_kqfilter`, this is what we switch on: `EVFILT_READ`,
`EVFILT_WRITE`, `EVFILT_EXCEPT`, and so on. The value comes from the
`filter` field of the `struct kevent` that userland submitted. A
driver that supports only one filter type checks this field and
rejects any mismatch with `EINVAL`.

`kn_fop` is the pointer to the `struct filterops` table that will
service this knote for the rest of its life. The driver sets this
inside `d_kqfilter`. After that point, the framework calls through
this pointer to reach our attach, detach, event, and touch callbacks.
The filterops table is always `static const` in the drivers we
examine, because the framework does not take a reference on it and
the driver is expected to keep the pointer valid for the knote's
lifetime.

`kn_hook` is a generic per-driver pointer. The driver typically sets
it to the softc, to a per-client state record, or to whichever
object the filter should react to. The framework never reads or
writes it. When the filter callbacks run, they pull the driver state
out of `kn_hook` rather than going through a global lookup, which
both avoids the lookup cost and avoids a class of lock-ordering
problems that global lookups can introduce.

`kn_hookid` is an integer companion to `kn_hook`, available for
per-driver tagging. Most drivers leave it alone.

`kn_data` is how the filter's `f_event` callback communicates "how
much is ready" back to the userland. For readable filters, drivers
conventionally store the number of bytes or records available. For
writable filters, they store the amount of space available. Userland
reads this through the `data` field of the returned `struct kevent`,
and tools like `libevent` rely on that convention. The
`/dev/klog` driver stores a raw byte count here, while the evdev
driver stores the queue depth scaled into bytes by multiplying the
record count by `sizeof(struct input_event)`, because evdev clients
read `struct input_event` records rather than raw bytes.

`kn_sfflags` and `kn_sdata` hold the per-registration flags and data
that userland requested through the `fflags` and `data` fields of
`struct kevent`. Filters that support fine-grained control, like
`EVFILT_TIMER` with its period or `EVFILT_VNODE` with its note
mask, look at these to decide how to behave. Simple driver filters
usually ignore them.

`kn_flags` holds the delivery-time flags that the framework passes
through to the userland on the next reap. The one every driver uses
is `EV_EOF`, which signals "no more data will ever arrive from this
source." Drivers set `EV_EOF` in `f_event` when the device is being
detached, when a pseudo-terminal's peer has closed, when a pipe has
lost its writer, or whenever the readiness signal has become
permanent.

`kn_status` is internal state owned by the framework: `KN_ACTIVE`,
`KN_QUEUED`, `KN_DISABLED`, `KN_DETACHED`, and a handful of others.
Drivers must not modify it. The driver's job is simply to report
readiness through `f_event`; the framework updates `kn_status`
accordingly.

`kn_link`, `kn_selnext`, and `kn_tqe` are the linked-list linkage
fields used by the various kqueue framework lists. The knlist
helpers manipulate them on our behalf. Drivers should never touch
them directly.

Put together, these fields tell a simple story. The driver creates
a knote's association with its filter operations inside
`d_kqfilter`, sets `kn_hook` and optionally `kn_hookid` so the
filter callbacks can recover their context, and then lets the
framework manage linkage and status. The driver owns readiness
reporting through `f_event` and nothing else. The handoff between
driver and framework is clean, and most driver bugs in this area
come from trying to reach across that boundary, either by modifying
framework-owned status flags or by retaining stale pointers into
the knote after `f_detach` has fired.

One point worth emphasising: the knote outlives any single
`f_event` call, but it does not outlive `f_detach`. Once the
framework invokes `f_detach`, the knote is being torn down; the
driver must unhook it from any internal structure it is attached
to and must not keep the pointer. The `kn_hook` pointer, which is
driver-owned, must be treated the same way. If the driver was
keeping a back-pointer from a softc field to the knote for any
reason (uncommon, but sometimes useful for driver-initiated
detach), it must clear that back-pointer during `f_detach` before
the framework frees the knote.

### Inside struct knlist: How the Driver's Waiting Room Works

`struct knlist`, declared in `/usr/src/sys/sys/event.h`, is where a
driver accumulates the knotes that are currently interested in one
of its notification sources. Every driver object that can wake
kqueue waiters owns at least one knlist. The pipe object owns two,
one for readers and one for writers. The tty owns two as well,
`t_inpoll` and `t_outpoll`, each with its own knlist. The evdev
client object owns one per client. In our `evdemo` driver, we
piggy-back on the `struct selinfo.si_note` we already have for
poll, so the same knlist is the one waking both poll and kqueue
consumers.

The structure itself is small:

```c
struct knlist {
    struct  klist   kl_list;
    void    (*kl_lock)(void *);
    void    (*kl_unlock)(void *);
    void    (*kl_assert_lock)(void *, int);
    void    *kl_lockarg;
    int     kl_autodestroy;
};
```

`kl_list` is the singly-linked list head of `struct knote` entries,
with linkage through each knote's `kn_selnext` field. The list head
is manipulated by the framework, never by the driver directly.

`kl_lock`, `kl_unlock`, and `kl_assert_lock` are function pointers
that the framework uses when it needs to take the object's lock. The
knlist does not own a lock of its own; it borrows the driver's
locking regime. This is why a `struct selinfo` can carry a knlist
without creating a separate lock: the lock is whatever the driver
has already declared.

`kl_lockarg` is the argument passed to those lock functions. When we
initialise a knlist with `knlist_init_mtx(&knl, &sc->sc_mtx)`, the
framework stores `&sc->sc_mtx` in `kl_lockarg` and arranges for the
lock callbacks to wrap `mtx_lock` and `mtx_unlock`. The driver never
sees this wiring and never needs to.

`kl_autodestroy` is a flag used by a few specific subsystems, most
notably AIO, where the knlist lives inside the `struct kaiocb` and
must be torn down automatically when the request completes. Driver
code almost never sets this. The `aio_filtops` path in
`/usr/src/sys/kern/vfs_aio.c` is the canonical use, and it is worth
remembering that the flag exists so that reading that file later
does not surprise you.

The lock contract deserves emphasis because it is the single most
common source of kqueue driver bugs. When the framework calls our
`f_event`, it holds the knlist lock, which is our softc mutex. Our
`f_event` may read softc state but must not take the softc mutex
again (it is already ours), must not sleep, and must not block on
any other lock that could be held across an `f_event` invocation.
When we invoke `KNOTE_LOCKED`, we are asserting that we already hold
the lock, so the framework skips locking on its way through the
list. When we invoke `KNOTE_UNLOCKED`, the framework takes and
releases the lock on our behalf. Mixing the two styles inside one
producer path is a classic source of subtle double-lock panics
under load.

The unification with `struct selinfo` is worth noticing. Back in
Section 3 we treated `struct selinfo` as a poll-only concept, but it
actually embeds a `struct knlist` in its `si_note` member. This is
why a driver that already supports `poll()` has the infrastructure
for `kqueue()` sitting in its softc: adding kqueue is largely a
matter of initialising the knlist with `knlist_init_mtx` and wiring
up the filter operations. The producer path already calls into
`selwakeup()`, which itself walks `si_note` under the appropriate
lock and notifies any attached knotes. Doing the notification
explicitly with `KNOTE_LOCKED(&sc->sc_rsel.si_note, 0)` is clearer
and lets us choose exactly when the kqueue fan-out happens relative
to any other producer work. In the drivers we will read below, both
styles appear; either one is correct as long as the locking is
consistent.

### The knlist Lifecycle in Detail

The lifecycle of a knlist follows the lifecycle of the driver object
that owns it. A knlist comes into existence during attach (either
the driver's attach entry point for a real hardware driver or the
SYSINIT for a pseudo-device), lives through the open-read-close
cycles of userland consumers, and is torn down at detach. The
functions we need, all declared in `/usr/src/sys/sys/event.h` and
implemented in `/usr/src/sys/kern/kern_event.c`, are `knlist_init`,
`knlist_init_mtx`, `knlist_add`, `knlist_remove`, `knlist_clear`,
and `knlist_destroy`.

`knlist_init_mtx` is the one almost every driver calls. It
initialises the list head, configures the knlist to use
`mtx_lock`/`mtx_unlock` with the driver's mutex as the argument, and
marks the knlist as live. The caller passes a pointer to the knlist
(usually `&sc->sc_rsel.si_note` or, for drivers with per-direction
notification, `&sc->sc_wsel.si_note` as well) and a pointer to a
mutex that already exists in the driver.

`knlist_init` is the general form, used when the driver's lock
regime is not a simple mutex. It accepts three function pointers
(lock, unlock, assert), an argument pointer passed to those
functions, and the underlying list head. Pipes use the `_mtx` form
with their pipe-pair mutex; socket buffers use a customised
`knlist_init` because they have their own locking discipline. Most
drivers do not need the general form.

`knlist_add` is called from `d_kqfilter` to link a newly registered
knote into the list. Its prototype is
`void knlist_add(struct knlist *knl, struct knote *kn, int islocked)`.
The `islocked` argument tells the function whether the caller
already holds the knlist lock. If it is zero, the function takes the
lock for us. If it is one, we are asserting that we already hold
it. Drivers that do no extra locking inside `d_kqfilter` pass zero;
drivers like `/dev/klog` that took the msgbuf lock on entry pass
one. Either pattern is correct; the choice depends on what the
driver wants to protect around the `knlist_add` call.

`knlist_remove` is the reverse operation, normally called from the
`f_detach` callback. Its prototype is
`void knlist_remove(struct knlist *knl, struct knote *kn, int islocked)`.
The framework invokes `f_detach` with the knlist lock already held,
so `islocked` is one in that context. If for any reason the driver
needs to remove a specific knote from outside `f_detach` (which is
unusual and rarely correct), it must arrange its own locking.

`knlist_clear` is the bulk-removal function used at driver detach
time. It walks the list, removes every knote, and marks each of
them with `EV_EOF | EV_ONESHOT` so that the userland sees a final
event and the registration is discarded. The signature
`void knlist_clear(struct knlist *knl, int islocked)` is actually a
wrapper around `knlist_cleardel` in `/usr/src/sys/kern/kern_event.c`
with a NULL `struct thread *` and the kill flag set, meaning
"remove everything." Drivers call this from `detach` right before
tearing down the knlist.

`knlist_destroy` releases the internal machinery of the knlist.
Before calling it, the knlist must be empty. If you destroy a knlist
with live knotes, the kernel asserts and panics. This is why the
detach sequence we saw earlier is rigid:

```c
knlist_clear(&sc->sc_rsel.si_note, 0);
seldrain(&sc->sc_rsel);
knlist_destroy(&sc->sc_rsel.si_note);
```

`knlist_clear` empties the list. `seldrain` wakes any `poll()`
waiters that are still parked on the same selinfo, so their waiting
threads return from the kernel. `knlist_destroy` tears down the
internals and validates that the list is empty. If any of these
steps is skipped, the detach becomes unsafe: live knotes trying to
call an unloaded driver's `f_event` would crash the kernel; a poll
waiter whose selinfo has been freed would wake to a dangling
pointer.

Two further points are worth noticing in the implementation of
`knlist_remove` in `/usr/src/sys/kern/kern_event.c`. It walks into
the internal `knlist_remove_kq` helper which also acquires the kq
lock so that the removal is coherent with any in-progress event
dispatch. And it sets `KN_DETACHED` in `kn_status` to signal to the
rest of the framework that this knote is gone. Drivers never
observe `KN_DETACHED` directly, but understanding that it exists
explains why concurrent detach and event delivery can race safely:
the framework's internal state machine keeps them consistent.

### The kqfilter Callback Contract

`d_kqfilter` is called from the kqueue registration path in
`/usr/src/sys/kern/kern_event.c`, specifically from
`kqueue_register` via the file descriptor's `fo_kqfilter` method.
By the time the callback runs, the framework has already validated
the file descriptor, allocated the `struct knote`, and filled in
the userland's request. Our job is narrow: pick the right
filterops, attach to the right knlist, and return zero.

What `d_kqfilter` must do. It must inspect `kn->kn_filter` to decide
which filter type the userland is asking for. It must set
`kn->kn_fop` to a valid `struct filterops` for that type. It must
attach the knote to a knlist that belongs to our driver, typically
by calling `knlist_add`. And it must return zero on success or a
sensible errno on failure. If the driver cannot service the
requested filter, `EINVAL` is the right answer.

What `d_kqfilter` must not do. It must not sleep, because the
kqueue registration path holds locks that are not safe to sleep
under. It must not allocate memory with `M_WAITOK`, for the same
reason. It must not call any function that can block on another
process. If the driver needs more than a fast lookup and a knlist
insertion, it is doing something wrong. The callback is essentially
a fast-path wiring operation.

Lock state on entry is worth understanding. The framework does not
hold the knlist lock when it calls `d_kqfilter`. We can therefore
pass `islocked = 0` to `knlist_add` if we have not taken the
knlist's lock ourselves. If our driver needs to look at softc state
as part of the filter-selection logic, for example to report
`ENODEV` on a revoked cdev as the evdev driver does, we can take
the softc mutex ourselves, check the state, do the `knlist_add`
with `islocked = 1`, and release the mutex before returning. The
evdev example below shows exactly that pattern.

Returning a non-zero value from `d_kqfilter` means "the userland
will get this errno back from `kevent(2)`." It does not mean "try
again." A driver that returns `EAGAIN` will confuse userland
because `kevent` does not interpret that value the way `read` does.
Stick to `EINVAL` for unsupported filters and `ENODEV` for revoked
or torn-down devices, and avoid clever error returns.

One subtlety about when `d_kqfilter` is invoked: a single
`kevent(2)` call that registers a new interest with `EV_ADD` enters
the framework, finds that no knote exists yet for this (file, filter)
pair, allocates one, and then calls `fo_kqfilter` on the file
descriptor's fileops. That is where our `d_kqfilter` is reached,
through the cdev fileops table. If the caller is instead updating
an existing registration (for example, toggling between enabled and
disabled with `EV_ENABLE`/`EV_DISABLE`), our callback is not
involved; the framework handles that internally through
`f_touch` or direct status manipulation.

### Worked Example: The /dev/klog Driver

The simplest real driver-side `kqfilter` implementation in the tree
is the kernel log device, `/dev/klog`, in
`/usr/src/sys/kern/subr_log.c`. Its entire kqueue support fits in
about forty lines and uses exactly the pattern we have been
discussing. Let us read it.

The filterops table is a minimal one, with only detach and event
callbacks:

```c
static const struct filterops log_read_filterops = {
    .f_isfd   = 1,
    .f_attach = NULL,
    .f_detach = logkqdetach,
    .f_event  = logkqread,
};
```

The attach hook is NULL because all the driver-side work happens in
`logkqfilter` itself. There is no need for a separate `f_attach`
callback; the `d_kqfilter` entry point does everything it needs to
do. Drivers that need to perform per-knote setup beyond what
`d_kqfilter` does can use `f_attach`, but that is uncommon.

`logkqfilter` is the `d_kqfilter` callback:

```c
static int
logkqfilter(struct cdev *dev __unused, struct knote *kn)
{

    if (kn->kn_filter != EVFILT_READ)
        return (EINVAL);

    kn->kn_fop = &log_read_filterops;
    knlist_add(&logsoftc.sc_selp.si_note, kn, 1);

    return (0);
}
```

The `/dev/klog` driver supports only readable events; a request for
any other filter type gets `EINVAL`. The callback sets `kn_fop` to
the static filterops table and then attaches the knote to the
softc's selinfo knlist. The third argument to `knlist_add` is `1`
here, which means "the caller already holds the knlist lock." The
driver takes the message-buffer lock before entering the callback
for its own reasons, so passing `1` is correct.

The event function is just as short:

```c
static int
logkqread(struct knote *kn, long hint __unused)
{

    mtx_assert(&msgbuf_lock, MA_OWNED);

    kn->kn_data = msgbuf_getcount(msgbufp);
    return (kn->kn_data != 0);
}
```

It asserts the message-buffer lock (which is what the knlist uses),
reads the number of queued bytes, and returns non-zero if anything
is available. The userland sees the byte count in `kn->kn_data` on
the next reap.

The detach function is one line:

```c
static void
logkqdetach(struct knote *kn)
{

    knlist_remove(&logsoftc.sc_selp.si_note, kn, 1);
}
```

It removes the knote from the knlist, again passing `1` because the
framework has taken the lock before entering `f_detach`.

The last piece is the producer. When the log timeout fires and
there is new data to notify waiters about, `/dev/klog` calls
`KNOTE_LOCKED(&logsoftc.sc_selp.si_note, 0)` under the message
buffer lock. That walks the knlist, calls each registered knote's
`f_event`, and queues notifications for any kqueues that have
waiters. The hint of zero is ignored by `logkqread`, which is the
common case.

The whole kqueue integration is initialised once at subsystem start
via `knlist_init_mtx(&logsoftc.sc_selp.si_note, &msgbuf_lock)`.
`/dev/klog` is never unloaded in practice, so there is no teardown
sequence to study here. That comes later, in the evdev example.

The takeaway is how small this code is. A complete, working,
production-grade `kqfilter` integration for a real driver in
FreeBSD 14.3 is under forty lines. The complexity of kqueue is in
the framework, not in the driver's contribution.

### Worked Example: TTY Read and Write Filters

The terminal subsystem in `/usr/src/sys/kern/tty.c` gives us the
next step up: a driver that supports both readable and writable
filters, and that uses `EV_EOF` to signal that the device is gone.
The pattern is the one we use in any driver that wants to expose
two independent sides of the same device.

The two filterops tables in `/usr/src/sys/kern/tty.c` are:

```c
static const struct filterops tty_kqops_read = {
    .f_isfd   = 1,
    .f_detach = tty_kqops_read_detach,
    .f_event  = tty_kqops_read_event,
};

static const struct filterops tty_kqops_write = {
    .f_isfd   = 1,
    .f_detach = tty_kqops_write_detach,
    .f_event  = tty_kqops_write_event,
};
```

The `d_kqfilter` entry point, `ttydev_kqfilter`, switches on the
requested filter and attaches to one of two knlists:

```c
static int
ttydev_kqfilter(struct cdev *dev, struct knote *kn)
{
    struct tty *tp = dev->si_drv1;
    int error;

    error = ttydev_enter(tp);
    if (error != 0)
        return (error);

    switch (kn->kn_filter) {
    case EVFILT_READ:
        kn->kn_hook = tp;
        kn->kn_fop = &tty_kqops_read;
        knlist_add(&tp->t_inpoll.si_note, kn, 1);
        break;
    case EVFILT_WRITE:
        kn->kn_hook = tp;
        kn->kn_fop = &tty_kqops_write;
        knlist_add(&tp->t_outpoll.si_note, kn, 1);
        break;
    default:
        error = EINVAL;
        break;
    }

    tty_unlock(tp);
    return (error);
}
```

Three things are worth noticing here.

First, each direction has its own selinfo (`t_inpoll`, `t_outpoll`)
and therefore its own knlist. A readable knote goes on one list and
a writable knote goes on the other. This lets the producer notify
only the side that changed: when incoming characters arrive, only
readable waiters wake; when the output buffer drains, only writable
waiters wake. Drivers that unify both sides onto one knlist would
have to waste cycles waking everybody for every state change.

Second, the third argument to `knlist_add` is `1`, because
`ttydev_enter` has already taken the tty lock before the switch
runs. The tty subsystem keeps that lock held from entry to exit
across most entry points, so every knlist operation inside is
already-locked.

Third, the read event callback demonstrates the `EV_EOF` discipline
we described earlier:

```c
static int
tty_kqops_read_event(struct knote *kn, long hint __unused)
{
    struct tty *tp = kn->kn_hook;

    tty_lock_assert(tp, MA_OWNED);

    if (tty_gone(tp) || (tp->t_flags & TF_ZOMBIE) != 0) {
        kn->kn_flags |= EV_EOF;
        return (1);
    } else {
        kn->kn_data = ttydisc_read_poll(tp);
        return (kn->kn_data > 0);
    }
}
```

If the tty is gone or is a zombie, `EV_EOF` is set and the filter
reports ready so that userland wakes up, reads, gets nothing, and
learns from the EOF flag that the device is finished. Otherwise
the filter reports the number of readable bytes and whether that
count is positive. The write-side callback `tty_kqops_write_event`
mirrors this pattern, reporting `ttydisc_write_poll` for the output
buffer's free space. The detach callbacks simply remove the knote
from whichever list it was on, with `islocked = 1` again.

What the tty example teaches is that a driver with two directions
needs two knlists, two filterops tables, two event functions, and a
`d_kqfilter` that steers the registration to the right one. The
producer side is symmetric: incoming characters trigger
`KNOTE_LOCKED` on `t_inpoll.si_note`; outgoing buffer space
triggers the same on `t_outpoll.si_note`. The separation is clean
and predictable, and it matches the way userland programs think
about terminal I/O.

### Worked Example: evdev Detach Discipline

For the last worked example we turn to the input-event subsystem in
`/usr/src/sys/dev/evdev/cdev.c`. Its kqfilter is structurally
similar to `/dev/klog`, but the evdev driver demonstrates something
the previous two examples glossed over: a complete detach sequence
that tears down the knlist safely even when live userland processes
may still have kqueue registrations outstanding.

The filterops and attach paths look familiar. The evdev filterops
table is:

```c
static const struct filterops evdev_cdev_filterops = {
    .f_isfd   = 1,
    .f_detach = evdev_kqdetach,
    .f_event  = evdev_kqread,
};
```

The `d_kqfilter` implementation adds an important extra check on
revocation, which makes evdev a little richer than `/dev/klog`:

```c
static int
evdev_kqfilter(struct cdev *dev, struct knote *kn)
{
    struct evdev_client *client;
    int ret;

    ret = devfs_get_cdevpriv((void **)&client);
    if (ret != 0)
        return (ret);

    switch (kn->kn_filter) {
    case EVFILT_READ:
        kn->kn_fop = &evdev_cdev_filterops;
        kn->kn_hook = client;
        EVDEV_CLIENT_LOCKQ(client);
        if (client->ec_revoked)
            ret = ENODEV;
        else
            knlist_add(&client->ec_selp.si_note, kn, 1);
        EVDEV_CLIENT_UNLOCKQ(client);
        break;
    default:
        ret = EINVAL;
    }

    return (ret);
}
```

If the client has been revoked, because the device is going away or
because a controlling process has explicitly revoked access, the
driver returns `ENODEV` rather than attaching a knote. Notice that
the driver takes its own per-client lock around both the
`ec_revoked` check and the `knlist_add`, so the two operations are
atomic with respect to revocation. This is the contract we
described earlier, applied cleanly: cheap lookups, a brief lock
hold, no sleeping, no memory allocation in the hot path.

The event function reports readiness from the per-client event
queue:

```c
static int
evdev_kqread(struct knote *kn, long hint __unused)
{
    struct evdev_client *client = kn->kn_hook;

    EVDEV_CLIENT_LOCKQ_ASSERT(client);

    kn->kn_data = EVDEV_CLIENT_SIZEQ(client) *
                  sizeof(struct input_event);
    if (client->ec_revoked) {
        kn->kn_flags |= EV_EOF;
        return (1);
    }
    return (kn->kn_data != 0);
}
```

Notice the `kn->kn_data` convention: not just "number of items" but
"number of items in bytes," because userland reads raw
`struct input_event` values and expects byte counts in the way that
`read()` returns them. This kind of detail matters for userland
libraries that use `kn->kn_data` to size buffers.

The producer path in `evdev_notify_event` combines every
asynchronous notification mechanism the subsystem supports:

```c
if (client->ec_blocked) {
    client->ec_blocked = false;
    wakeup(client);
}
if (client->ec_selected) {
    client->ec_selected = false;
    selwakeup(&client->ec_selp);
}
KNOTE_LOCKED(&client->ec_selp.si_note, 0);

if (client->ec_sigio != NULL)
    pgsigio(&client->ec_sigio, SIGIO, 0);
```

This is a complete asynchronous producer: blocking `read()` waiters
are signaled via `wakeup()`, `poll()` and `select()` waiters are
signaled via `selwakeup()`, kqueue waiters are signaled via
`KNOTE_LOCKED`, and registered SIGIO consumers are signaled via
`pgsigio`. Any given consumer sees exactly one of these, but the
producer does not need to know which one; it calls all of them
unconditionally and lets each mechanism filter itself. Our
`evdemo` driver will adopt the same layered producer as we finish
the chapter.

The detach sequence is the piece that is uniquely instructive. When
an evdev client goes away, the driver runs:

```c
knlist_clear(&client->ec_selp.si_note, 0);
seldrain(&client->ec_selp);
knlist_destroy(&client->ec_selp.si_note);
```

This is exactly the three-step discipline we described. The result
is that any userland process still holding a kqueue registration
for this client reaps a final `EV_EOF` event and then sees the
registration disappear; any `poll()` waiter still parked on the
selinfo wakes and returns; any in-flight kqueue delivery that was
about to call back into our filterops completes safely before the
knlist memory is released.

Getting the order wrong turns this from a clean teardown into a
panic. `knlist_destroy` before `knlist_clear` asserts on a
non-empty list. `knlist_clear` without `seldrain` leaves poll
waiters hanging. `seldrain` without a preceding `knlist_clear` will
work but will leave kqueue registrations pointing at a driver that
is about to disappear, and the first event delivery attempt will
crash. Follow the sequence.

The evdev example brings together everything we have covered in
this section: a revocation-aware attach, a byte-count-correct event
report, a combined producer path, and a teardown that respects the
lifetime rules. A driver that imitates this pattern will behave
well in production.

### The hint Parameter: What It Is and Why It Exists

Every `f_event` callback receives a `long hint` argument that we
have been quietly setting to zero. It is worth understanding what
that parameter is for, because it is not zero everywhere in the
kernel.

The hint is a cookie passed through from the producer to the
filter. When a producer calls `KNOTE_LOCKED(list, hint)`, the
framework passes that same `hint` value to every filter's
`f_event`. It is entirely up to the producer and the filter to
agree on what the value means. The framework does not interpret it.

For simple drivers that have one meaning of "ready," zero is the
natural choice and the filter ignores the argument. For drivers
with more than one producer path, the hint can distinguish them.
The vnode filter uses non-zero hints to encode `NOTE_DELETE`,
`NOTE_RENAME`, and related vnode-level events, and the
`f_event` function tests the hint bits to decide which
`kn->kn_fflags` bits to set in the delivered event. That is beyond
what an ordinary character driver needs, but it explains the
signature's generality.

The producer side is where the hint value originates. A driver can
call `KNOTE_LOCKED(&sc->sc_rsel.si_note, MY_HINT_NEW_DATA)` and the
filter can switch on the value to take different paths. In
practice, ordinary drivers pass zero and keep the filter simple.

### Delivering Events: KNOTE_LOCKED vs KNOTE_UNLOCKED, in Depth

The two delivery macros in `/usr/src/sys/sys/event.h` are:

```c
#define KNOTE_LOCKED(list, hint)    knote(list, hint, KNF_LISTLOCKED)
#define KNOTE_UNLOCKED(list, hint)  knote(list, hint, 0)
```

Both call the same underlying `knote()` function in
`/usr/src/sys/kern/kern_event.c`, which walks the knlist and
invokes `f_event` on each knote. The difference is the third
argument: `KNF_LISTLOCKED` says "the caller already holds the
knlist lock," while zero says "take it for me."

Choosing between them is a matter of matching the producer's
locking path. If the producer is called with the driver's mutex
already taken (because it is invoked from a locked ISR handler, or
from inside a producer function that needed the lock for its own
work), `KNOTE_LOCKED` is correct. If the producer is called
unlocked (because it is running in thread context and the lock
would be taken specifically for the notification), `KNOTE_UNLOCKED`
is correct. The mistake to avoid is calling `KNOTE_LOCKED` without
actually holding the lock, which races horribly under load, or
calling `KNOTE_UNLOCKED` while holding the lock, which recurses and
panics.

An ISR-context example helps: if a device interrupt handler calls
a bottom-half function that acquires the softc mutex, does some
work, and needs to notify kqueue waiters, the cleanest pattern is
to do the work and the `KNOTE_LOCKED` call inside the held mutex
and drop the lock afterwards. The mutex is the knlist lock, so
`KNOTE_LOCKED` is what to use. If the notification instead comes
from a thread that has not yet taken the lock, the thread takes
the lock, does the work, calls `KNOTE_LOCKED`, and then drops the
lock; or it uses `KNOTE_UNLOCKED` and lets the framework briefly
take the lock on its way through the list.

A second subtlety is the behaviour of `knote` when the list is
empty. Walking an empty list is cheap but not free; it still takes
the lock. Drivers that deliver very high-rate notifications can
test `KNLIST_EMPTY(list)` first and skip the `KNOTE_LOCKED` call
if there are no waiters. The macro `KNLIST_EMPTY`, defined in
`/usr/src/sys/sys/event.h` as `SLIST_EMPTY(&(list)->kl_list)`, is
safe to read without the lock for the purpose of a hint, because
the worst case of a stale read is a missed wakeup on a knote that
was added a microsecond ago, and that knote will notice the next
delivery. In practice this optimisation is rarely worth the
complexity, but it is worth knowing about.

### Common Pitfalls in Driver kqfilter Implementations

Over the course of reading kqueue-aware drivers in the tree, a
handful of recurring bug patterns show up. Knowing the pitfalls in
advance helps avoid them.

Forgetting to destroy the knlist. A driver that calls
`knlist_init_mtx` in attach but does not call `knlist_destroy` in
detach leaks the knlist's internal state and, worse, may leave live
knotes dangling. The fix is to include the clear-drain-destroy
sequence in every detach path.

Calling `knlist_destroy` before `knlist_clear`. `knlist_destroy`
asserts that the list is empty. If there are any knotes still
attached, the assertion fails and the kernel panics. Always clear
first.

Using `KNOTE_LOCKED` without the lock. This is subtle because it
works most of the time. Under load, two producers can race in the
knote walk, and the framework's assumption that the list is stable
during traversal breaks. The symptom is usually a knote pointer
corruption or a use-after-free in `f_event`.

Sleeping in `f_event`. The framework is holding the knlist lock,
which is our softc mutex, when it calls us. Sleeping under a mutex
is a kernel bug. If `f_event` needs state that is not already
accessible under the softc mutex, the design is wrong; move the
state into the softc or pre-compute it before the notification.

Returning stale `kn_data`. The `kn->kn_data` field should reflect
the state at the moment the filter was evaluated. A driver that
computes `kn_data` once in `d_kqfilter` and forgets to update it in
`f_event` will deliver stale byte counts to the userland. Always
recompute it in `f_event`.

Keeping `kn_hook` pointing at freed memory. If `kn_hook` is set to
a softc, and the softc is freed before the knote is detached, the
next `f_event` call will dereference freed memory. This is what
`knlist_clear` and `seldrain` are supposed to prevent, but only if
they are called in the correct order and before the softc is
freed. The detach order in the driver's detach entry point matters.

Setting `EV_EOF` only once. `EV_EOF` is sticky in the sense that
once it is set, the userland will see it, but `f_event` is called
multiple times over a knote's life. If the condition that caused
`EV_EOF` can become false again (for example, a named pipe that
gains a new writer), the filter must clear `EV_EOF` explicitly.
The pipe filter in `/usr/src/sys/kern/sys_pipe.c` demonstrates
this: `filt_piperead` both sets and clears `EV_EOF` depending on
the pipe's state.

Confusing `f_isfd` with `f_attach`. `f_isfd = 1` means the filter
is tied to a file descriptor; almost all driver filters want this.
`f_attach = NULL` means "the registration path does not need a
per-knote attach callback beyond what `d_kqfilter` already did."
They are independent. A driver can set `f_isfd = 1` and
`f_attach = NULL` at the same time, which is the common case.

Returning errors from `f_event`. `f_event` returns an int, but it
is a boolean: zero means "not ready," non-zero means "ready." It
is not an errno. Returning `EINVAL` from `f_event` means "ready,"
which is almost certainly not what the driver intended.

### A Mental Model for the kqueue Framework

It is worth pausing to assemble a mental model of the kqueue
framework that fits what we have learned. Different readers will
find different models helpful; one that works well for driver
authors is this.

Imagine each driver object (a cdev, a per-client state record, a
pipe, a tty) as a small office. The office has in-boxes and
out-boxes, which are the knlists. When a visitor (a userland
program) wants to be told when there is new mail in the in-box,
they register a sticky note with the office: their kqueue file
descriptor, plus the filter type they care about. The office
clerks (our `d_kqfilter` callback) take the sticky note, check
which in-box it belongs on (`EVFILT_READ` in-box or `EVFILT_WRITE`
out-box), and pin it there. The sticky note records who to notify
(the kqueue) and how (the `struct filterops` callbacks).

When mail actually arrives (the producer path inserts a record
and wants to notify), the office clerks walk the in-box sticky
notes and, for each one, check whether the condition is currently
satisfied (the `f_event` callback). If it is, the clerk picks up
the phone and rings the visitor's kqueue, delivering a
notification. The visitor reads the notification on their next
`kevent(2)` reap.

When the visitor changes their mind and no longer wants mail
notifications (removes the registration), the office clerks pull
the sticky note down (the `f_detach` callback). When the office
closes permanently (the driver detaches), the clerks pull down
every sticky note at once (`knlist_clear`), wake any visitors who
are physically sitting in the waiting room (`seldrain`), and then
dismantle the sticky-note corkboard (`knlist_destroy`).

The lock on the corkboard is the driver's softc mutex. The clerks
hold it while walking the notes, pinning a note, or pulling a note
down. This is why `f_event` must not sleep: the clerks cannot let
go of the lock while they are working through the list, because
other clerks might arrive with updates. It is also why
`KNOTE_LOCKED` is the right call when the producer already holds
the lock: the clerk saying "I am already holding it" lets the
framework skip an unnecessary re-acquisition.

The model is simplified, and it leaves out complications like
`EV_CLEAR` edge semantics and `f_touch` registration updates, but
it captures the essential architecture. The driver owns the
corkboard; the framework owns the sticky notes. The driver
reports readiness; the framework handles delivery. The driver
tears down the corkboard at detach; the framework's sticky-note
structures are freed as part of that teardown.

Keep this picture in mind as you read the code of other kqueue-using
subsystems, and the unfamiliar names will map back onto familiar
roles. `kqueue_register` is the visitor walking in to submit a
sticky note. `knote` is the clerk walking the corkboard. `f_event`
is each note's individual readiness-check. `selwakeup` is the
general fire alarm that also reaches the corkboard. The names are
different; the shapes are the same.

### Reading kern_event.c: A Guide for the Curious

For readers who want to go further than the callbacks, the kqueue
framework itself is worth a tour. `/usr/src/sys/kern/kern_event.c`
is about three thousand lines, which looks intimidating, but the
structure of the file is predictable once we know what to look for.

Near the top of the file, the static filterops tables for the
built-in filters are declared. `file_filtops` handles the generic
read and write filters for file descriptors that do not provide
their own kqfilter; `timer_filtops` handles `EVFILT_TIMER`;
`user_filtops` handles `EVFILT_USER`; and several more follow.
These are the filterops the framework installs at boot, and reading
them gives a good sense of how a filterops table is meant to look
in production code.

After the static declarations come the system call entry points:
`kqueue`, `kevent`, and the legacy variants. These do argument
validation and dispatch to the core machinery. A reader tracing a
userland call through the kernel starts here.

The core machinery is a set of functions with names beginning
`kqueue_`. `kqueue_register` handles `EV_ADD`, `EV_DELETE`,
`EV_ENABLE`, `EV_DISABLE`, and `EV_RECEIPT`; it is where the
framework calls our `d_kqfilter` through `fo_kqfilter`.
`kqueue_scan` reaps ready events back to userland. `kqueue_acquire`
and `kqueue_release` reference-count the kqueue for safe concurrent
access. `kqueue_close` tears the kqueue down when the last file
descriptor referencing it is closed. Tracing from the top of
`kqueue_register` through `kqueue_expand`, `knote_attach`, and the
`fo_kqfilter` call reveals the full registration path.

The `knote` function itself, about two-thirds of the way through
the file, is the one we reach through `KNOTE_LOCKED` and
`KNOTE_UNLOCKED`. It walks the knlist, invokes `f_event` on each
knote, and queues notifications for any that report ready. Reading
it shows exactly why the lock assertions on our `f_event` are
necessary and how the framework interleaves list traversal with
kqueue notification. The walk uses `SLIST_FOREACH_SAFE` with a
temporary pointer, so an `f_detach` during the walk does not
corrupt the iteration. That subtle detail is what makes
concurrent detach and delivery safe.

Further down comes the knlist machinery: `knlist_init`,
`knlist_init_mtx`, `knlist_add`, `knlist_remove`,
`knlist_cleardel`, `knlist_destroy`, and the various helpers. These
are the functions we have been calling. Reading them confirms the
lock semantics we have been relying on and shows how the
`islocked` arguments are consumed inside the helpers.

Near the end of the file come the filter implementations for the
built-in filters, with names like `filt_timerattach`, `filt_user`,
and `filt_fileattach`. These are worth reading because they are
the closest thing to a reference implementation for how a filter
should be structured. The pipe filter in
`/usr/src/sys/kern/sys_pipe.c` is another good reference; socket
kqueue support in `/usr/src/sys/kern/uipc_socket.c` is a third.

A reader who works through `kqueue_register`, `knote`, and
`knlist_remove` in that order will understand most of the framework
by the end of an afternoon. The remaining machinery (auto-destroy,
timer implementation, proc-and-signal filters, vnode-note masks) is
specialised enough that a driver author can skip it unless a
specific need arises. The rest of this chapter does not require any
of it.

### Driver Patterns We Have Not Yet Used

Two patterns appear in the tree that we have not used in `evdemo`
because they are not needed, but are worth recognising so that
readers who see them elsewhere know what they are.

The first is the use of `f_attach` for per-knote setup beyond what
`d_kqfilter` does. The `EVFILT_TIMER` filter uses `f_attach` to
start a one-shot or repeating timer when the knote is first
registered, and `f_detach` to stop it. The `EVFILT_USER` filter in
`/usr/src/sys/kern/kern_event.c` uses `filt_userattach` as a no-op
because the knote is not attached to anything in the kernel; the
user-triggered `NOTE_TRIGGER` mechanism handles delivery entirely
through `f_touch`. A driver that needs its own per-knote state
could allocate it in `f_attach` and free it in `f_detach`, using
`kn_hook` or `kn_hookid` to remember the pointer. Almost no driver
actually needs this, because the per-registration state usually
fits naturally in the softc.

The second is `f_touch`, which intercepts `EV_ADD`, `EV_DELETE`,
and `EV_ENABLE`/`EV_DISABLE` operations. The `filt_usertouch`
function in `/usr/src/sys/kern/kern_event.c` is a good reference
for how `f_touch` is structured: it inspects the `type` argument
(one of `EVENT_REGISTER`, `EVENT_PROCESS`, or `EVENT_CLEAR`) to
decide what the userland is asking for and updates the knote's
fields accordingly. Most driver filters leave `f_touch` as NULL
and accept the framework's default behaviour, which is to store
`sfflags`, `sdata`, and the event flags in the knote directly
during `EV_ADD`. The default is correct for filters that do not
need extra behaviour on registration updates.

A third pattern the tree uses but our driver does not is the
"kill" variant of knlist teardown. `knlist_cleardel` in
`/usr/src/sys/kern/kern_event.c` accepts a `killkn` flag that,
when set, forces every knote off the list whether or not it is
still in use. `knlist_clear` is the common wrapper with this flag
set. A driver that wants to preserve knotes across an event (for
example, to re-attach them to a new object) could call
`knlist_cleardel` with `killkn` false and the knotes would be
unhooked but left alive. This is almost never what a driver wants.
The common case is `knlist_clear`, which kills and frees.

### A Note on EV_CLEAR, EV_ONESHOT, and Edge-Triggered Behaviour

The kqueue framework supports several delivery modes through flags
on the `struct kevent`:

`EV_CLEAR` makes the filter edge-triggered: once a knote fires, it
will not fire again until the underlying condition transitions from
false to true again. This is the common choice for readable and
writable filters on high-throughput descriptors, because it avoids
spamming the userland with repeated notifications for the same
data.

`EV_ONESHOT` makes the filter fire exactly once and then auto-delete
itself. It is useful for one-time events.

`EV_DISPATCH` makes the filter fire at most once per `kevent()`
reap, auto-disabling itself after each fire. The userland re-enables
it by re-registering with `EV_ENABLE`. This is the mode preferred by
multi-threaded userland programs that want to ensure only one thread
reacts to each event.

The driver's filter functions do not need to know about these flags;
the framework handles them. The driver just reports whether the
underlying condition is satisfied, and the framework decides what
to do with the resulting knote.

### Wrapping Up Section 4

We now have `kqueue` support in our driver. The total code we added
is not enormous: a `d_kqfilter` callback, a `struct filterops`, two
short filter functions, and a `KNOTE_LOCKED()` call in the
producer. The complexity is more about understanding the framework
than about writing a lot of code.

But we have only covered the two most common filters, `EVFILT_READ`
and `EVFILT_WRITE`. The chapter's scope deliberately excludes deeper
kqueue topics such as user-defined filters (`EVFILT_USER`), custom
`f_touch` implementations, and interactions with the AIO subsystem.
Those are specialized enough that they rarely appear in ordinary
drivers, and they would crowd out material that most readers need.
If you do need them, the material in this section prepares you to
read the corresponding parts of `/usr/src/sys/kern/kern_event.c`
and understand what you find.

Looking back at what this section has covered, the reader should
now be comfortable with several layers that tend to blur together
in casual discussions of kqueue. The outermost layer is the
userland API: `kqueue(2)`, `kevent(2)`, and the `struct kevent`
values that programs submit and reap. The middle layer is the
framework: `kqueue_register`, `knote`, `kqueue_scan`, and the
machinery that matches registrations to deliveries. The inner
layer is the driver contract: `d_kqfilter`, `struct filterops`,
`struct knote`, `struct knlist`, and the small set of helper
functions like `knlist_init_mtx`, `knlist_add`, `knlist_remove`,
`knlist_clear`, and `knlist_destroy`. The three layers
communicate through well-defined boundaries, and understanding
which is which is the difference between guessing at kqueue and
understanding it.

We have also walked through three real driver implementations:
`/dev/klog`, the tty subsystem, and the evdev input stack. Each
illustrates a different aspect of the kqfilter contract. The klog
driver shows the minimum that a kqueue-aware driver needs. The
tty subsystem shows how to handle two directions with two
separate knlists. The evdev driver shows the revocation-aware
attach, the byte-count-correct event report, the combined
producer path that fans out to multiple asynchronous mechanisms,
and the strict clear-drain-destroy detach sequence. A driver that
combines the appropriate pieces of these three patterns will
behave well in production, and a reader who has followed the
discussion should be able to recognise the patterns when they
appear in other subsystems in the tree.

In the next section we turn to the third asynchronous mechanism,
`SIGIO`. Unlike `poll()` and `kqueue()`, which are pull-style
notifications (the userland asks, the kernel answers), `SIGIO` is
push-style: the kernel sends a signal to the registered process
whenever the device state changes. It is older, simpler, and has
some subtle problems in multi-threaded programs, but it is still
useful in specific situations and is part of the standard driver
toolkit.

## 5. Asynchronous Signals With SIGIO and FIOASYNC

The third classic asynchronous mechanism is signal-driven I/O, also
called `SIGIO` notification after the signal it typically uses. The
user enables it through the `FIOASYNC` ioctl on an open file
descriptor, sets an owner with `FIOSETOWN`, and installs a handler for
`SIGIO`. The driver, whenever it has a relevant state change, sends
`SIGIO` to the registered owner. That signal can interrupt almost any
system call in the owner, which then typically services the device
and returns to its normal work.

Signal-driven I/O is older than `kqueue`, less scalable than `poll`,
and has some subtle issues in multi-threaded programs. It is still
the right mechanism in a small but real set of cases: single-threaded
programs that want the simplest possible asynchronous notification,
shell scripts using `trap`, and legacy code that has used `SIGIO` for
decades and will not change. FreeBSD continues to support it in full,
and most ordinary character drivers are expected to honour the
mechanism.

### How Signal-Driven I/O Works From Userland

A user program using `SIGIO` does three things. It installs a signal
handler for `SIGIO`. It tells the kernel which process should own the
signal for this descriptor. It enables asynchronous notification.

The code looks roughly like this:

```c
#include <signal.h>
#include <sys/filio.h>
#include <fcntl.h>
#include <unistd.h>

static volatile sig_atomic_t got_sigio;

static void
on_sigio(int sig)
{
    got_sigio = 1;
}

int
main(void)
{
    int fd = open("/dev/evdemo", O_RDONLY | O_NONBLOCK);

    struct sigaction sa;
    sa.sa_handler = on_sigio;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGIO, &sa, NULL);

    int pid = getpid();
    ioctl(fd, FIOSETOWN, &pid);

    int one = 1;
    ioctl(fd, FIOASYNC, &one);

    for (;;) {
        pause();
        if (got_sigio) {
            got_sigio = 0;
            char buf[256];
            ssize_t n;
            while ((n = read(fd, buf, sizeof(buf))) > 0) {
                /* process data */
            }
        }
    }
}
```

The sequence of ioctls is important. The program first installs the
signal handler so that `SIGIO` will not be ignored when it arrives.
It then calls `FIOSETOWN` with its own PID (positive values mean
process, negative values mean process group) so that the driver
knows where to deliver the signal. Finally it calls `FIOASYNC` with a
non-zero value to enable notification.

Once asynchronous notification is enabled, every state change in the
driver that would have satisfied a `POLLIN` causes a `SIGIO` signal
to the owner. The program's handler runs asynchronously, sets a
flag, and returns; the main loop then services the device. Drain
the device to empty with non-blocking reads, because between the
time the signal was sent and the time the handler ran, multiple
events may have accumulated.

### The FIOASYNC, FIOSETOWN, and FIOGETOWN Ioctls

Open `/usr/src/sys/sys/filio.h` to see the ioctl definitions:

```c
#define FIOASYNC    _IOW('f', 125, int)   /* set/clear async i/o */
#define FIOSETOWN   _IOW('f', 124, int)   /* set owner */
#define FIOGETOWN   _IOR('f', 123, int)   /* get owner */
```

These are standard ioctls that most of the file descriptor-handling
layer already understands. For an ordinary file descriptor (a
socket, a pipe, a pty), the kernel handles them without involving
the driver. For a `cdev`, however, the driver is responsible for
implementing them, because the driver owns the state that the ioctls
manipulate.

The conventional approach in a FreeBSD character driver is:

`FIOASYNC` takes an `int *` argument. Non-zero enables asynchronous
notification. Zero disables it. The driver stores the flag in the
softc and uses it to decide whether to generate signals.

`FIOSETOWN` takes an `int *` argument. A positive value is a PID, a
negative value is a process group ID, and zero clears the owner.
The driver uses `fsetown()` to record the owner.

`FIOGETOWN` takes an `int *` argument to be filled in. The driver
uses `fgetown()` to retrieve the current owner.

### fsetown, fgetown, and funsetown

The owner-tracking mechanism uses a `struct sigio` in the kernel. We
do not have to allocate or manage that structure directly; the
`fsetown()` and `funsetown()` helpers do it for us. The public API,
in `/usr/src/sys/sys/sigio.h` and `/usr/src/sys/kern/kern_descrip.c`,
consists of four functions:

```c
int   fsetown(pid_t pgid, struct sigio **sigiop);
void  funsetown(struct sigio **sigiop);
pid_t fgetown(struct sigio **sigiop);
void  pgsigio(struct sigio **sigiop, int sig, int checkctty);
```

The driver stores a single `struct sigio *` in the softc. All four
helpers take a pointer to this pointer, because they may replace the
whole structure as part of their work. The helpers take care of
reference counting, locking, and safe removal during process exit
through `eventhandler(9)`.

`fsetown()` installs a new owner. It expects to be called with the
interrupted caller's credentials available (which is always the case
inside an ioctl handler). If the target PID is zero, it clears the
owner. If the target is a positive number, it looks up the process.
If it is a negative number, it looks up the process group. It
returns zero on success or an errno on failure.

`funsetown()` clears the owner and frees the associated structure.
Drivers call it during close and during detach to make sure no
stale references are left behind.

`fgetown()` returns the current owner as a PID (positive) or a
process group ID (negative), or zero if no owner is set.

`pgsigio()` delivers a signal to the owner. The third argument,
`checkctty`, should be zero for a driver that is not a controlling
terminal. This is what the driver calls from the producer path
whenever asynchronous notification is enabled.

### Implementing SIGIO in evdemo

Putting the pieces together, here is what we add to our driver to
support `SIGIO`:

In the softc:

```c
struct evdemo_softc {
    /* ... existing fields ... */
    struct sigio    *sc_sigio;
    bool             sc_async;
};
```

In `d_ioctl`:

```c
static int
evdemo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int error = 0;

    switch (cmd) {
    case FIOASYNC:
        mtx_lock(&sc->sc_mtx);
        sc->sc_async = (*(int *)data != 0);
        mtx_unlock(&sc->sc_mtx);
        break;

    case FIOSETOWN:
        error = fsetown(*(int *)data, &sc->sc_sigio);
        break;

    case FIOGETOWN:
        *(int *)data = fgetown(&sc->sc_sigio);
        break;

    default:
        error = ENOTTY;
        break;
    }
    return (error);
}
```

In the producer path:

```c
static void
evdemo_post_event(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    bool async;

    mtx_lock(&sc->sc_mtx);
    evdemo_enqueue(sc, ev);
    async = sc->sc_async;
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);

    selwakeup(&sc->sc_rsel);
    if (async)
        pgsigio(&sc->sc_sigio, SIGIO, 0);
}
```

In `d_close` or during detach:

```c
static int
evdemo_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;

    funsetown(&sc->sc_sigio);
    /* ... other close handling ... */
    return (0);
}
```

Let us walk through the pieces.

The softc gains two new fields: `sc_sigio`, the pointer we pass to
`fsetown()` and friends, and `sc_async`, a flag telling the producer
whether signals are enabled. The flag is redundant with "sc_sigio
is non-NULL" in a sense, but keeping it explicit makes the producer
code clearer and faster.

The `d_ioctl` handler implements the three ioctls. We take the softc
mutex for `FIOASYNC` because we update `sc_async`. We do not take
the mutex for `FIOSETOWN` and `FIOGETOWN` because the `fsetown()`
and `fgetown()` helpers have their own internal locking and should
not be called with a driver lock held.

In the producer, we copy `sc_async` into a local variable under the
lock so that the value we use outside the lock is consistent. If we
had simply read `sc->sc_async` after the lock was dropped, another
thread could have changed it in between, which is a race. Taking a
snapshot under the lock avoids the race.

We call `pgsigio()` outside the softc lock because `pgsigio()` takes
its own locks and could create ordering problems if nested. The
pattern is the same as `selwakeup()`: update under the lock, drop,
then deliver the notification.

In `d_close`, we call `funsetown()` to clear the owner. This also
handles the case where the process that set the owner has since
exited, so the driver does not leak `struct sigio` allocations. If
the process has already exited, `funsetown()` is essentially a
no-op; if it has not, the call cleans up the registration.

### Caveats: Signal Semantics in Multi-Threaded Programs

Signal-driven I/O has well-known weaknesses in multi-threaded
programs. The primary issue is that signals in POSIX are sent to a
process, not to a specific thread. When the kernel delivers `SIGIO`,
any of the process's threads whose mask allows the signal may be
the one to receive it. For a program that wants a specific thread
to service the notification, this is inconvenient.

There are workarounds. `pthread_sigmask()` can be used to block
`SIGIO` in all threads except the one that should service it. If
you want to convert signals into readable events on a file
descriptor, FreeBSD provides `EVFILT_SIGNAL` through `kqueue(2)`,
which lets a kqueue report that a given signal has been delivered
to the process. FreeBSD does not provide the Linux-specific
`signalfd(2)` system call. The simplest workaround, and often the
right one, is to use `kqueue` directly for the underlying driver
events: threads can each own a separate kqueue and each can wait
for exactly the events they care about, without fighting signal
delivery rules at all.

A second weakness is that signals interrupt system calls. Under
the default SA flags, an interrupted system call returns with
`EINTR`, and the program must check for this and retry. This is
unusual enough that it often produces bugs in programs that were
written without `SIGIO` in mind. The workaround is to set
`SA_RESTART` in `sa_flags`, which makes the kernel restart
interrupted system calls automatically.

A third weakness is that signal delivery is asynchronous with
respect to the program's execution. A signal that arrives while
the program is in the middle of a data structure update can lead
to inconsistent state if the signal handler touches the same
structure. The fix is to keep signal handlers very simple (set a
flag, return) and do the actual work in the main loop.

For modern programs, `kqueue` avoids all three of these issues.
For legacy programs and simple single-threaded applications,
`SIGIO` is fine, and implementing it in a driver is a small amount
of code.

### What Real Drivers Look Like

The `if_tuntap.c` driver provides a representative example of SIGIO
handling. In the softc:

```c
struct tuntap_softc {
    /* ... */
    struct sigio        *tun_sigio;
    /* ... */
};
```

In the ioctl handler, the driver calls `fsetown()` and `fgetown()`
for `FIOSETOWN` and `FIOGETOWN` respectively, and stores the
`FIOASYNC` flag. In the producer path (when a packet is ready to
be read from the interface), the driver calls `pgsigio()`:

```c
if (tp->tun_flags & TUN_ASYNC && tp->tun_sigio)
    pgsigio(&tp->tun_sigio, SIGIO, 0);
```

In the close path, it calls `funsetown()`.

The `evdev/cdev.c` driver has a similar structure. These are the
patterns you will reuse in your own drivers.

### Testing SIGIO From Shell

A nice property of `SIGIO` is that you can demonstrate it from the
shell without writing any code. The Bourne-style shells (sh, bash)
have a built-in `trap` command that runs an action when a signal
arrives. Combined with the `FIOASYNC` ioctl, we can set up a test
in a few lines:

```sh
trap 'echo signal received' SIGIO
exec 3< /dev/evdemo
# (mechanism to enable FIOASYNC on fd 3 goes here)
# Trigger events in another terminal and watch for "signal received"
```

The catch is that there is no direct shell-level way to issue an
`ioctl`. You need either a small C helper, or a tool like the
`ioctl(1)` command that some BSDs ship, or `truss` in a traced
child. For the lab in this chapter we provide a small `evdemo_sigio`
program that calls the right ioctls and then simply pauses,
leaving the shell's `trap` handler to show the signal deliveries.

### A Note on POSIX AIO

FreeBSD also supports the POSIX `aio_read(2)` and `aio_write(2)`
APIs. These are beyond the normal scope of a character driver, and
ordinary `cdev` drivers almost never need to implement anything
special to participate in AIO. The remaining subsections of this
section explain why that is, how AIO actually dispatches requests
inside the kernel, and when (if ever) a driver should think about
AIO at all. The intent is to head off a common source of confusion:
when readers see "asynchronous I/O on files" in FreeBSD
documentation, they are reading about POSIX AIO, and it is easy to
assume that a driver needs its own AIO machinery to be a first-class
citizen. It does not.

### How AIO Dispatches: fo_aio_queue and aio_queue_file

When a userland program calls `aio_read(2)` or `aio_write(2)`, the
request enters the kernel, is validated, and becomes a
`struct kaiocb` (kernel AIO control block). The code path from
there is worth tracing because it explains why a character driver
almost never needs to do anything about POSIX AIO at all.

In `/usr/src/sys/kern/vfs_aio.c`, the dispatch is done at the file
operations layer. The relevant decision, inside `aio_aqueue`, looks
like this:

```c
if (fp->f_ops->fo_aio_queue == NULL)
    error = aio_queue_file(fp, job);
else
    error = fo_aio_queue(fp, job);
```

The decision is made at the file-ops layer, not the cdev layer. If
the file's `struct fileops` has its own `fo_aio_queue` function
pointer, AIO delegates to it. Vnode file operations set
`fo_aio_queue = vn_aio_queue_vnops`, which routes regular-file
requests through a path that knows how to talk to the underlying
file system. A cdev file's fileops, by contrast, leaves
`fo_aio_queue` as NULL, so AIO falls into the generic
`aio_queue_file` path.

`aio_queue_file` in `/usr/src/sys/kern/vfs_aio.c` tries two things.
First, it attempts `aio_qbio` (the bio-based path, described in the
next subsection) if the underlying object looks like a block device.
Second, if the bio path is not applicable, it schedules
`aio_process_rw` on one of the AIO worker threads. `aio_process_rw`
is a daemon-based path that simply calls `fo_read` or `fo_write`
synchronously from the AIO worker thread. In other words, for a
generic cdev, "asynchronous I/O" is implemented by asking a kernel
thread to perform a synchronous `read()` or `write()` on the
application's behalf.

This is why ordinary character drivers do not need to implement AIO
hooks of their own. The AIO subsystem does not call into the driver
through a new entry point; it calls `fo_read` and `fo_write`, which
in turn call the driver's existing `d_read` and `d_write`. If our
driver already supports blocking and non-blocking reads correctly,
it already supports AIO, just through a worker thread. No additional
code is required on the driver side.

### The Block Device Path: aio_qbio and Bio Callbacks

For block devices (disk, cd, and friends), the worker-thread path
is inefficient because the block I/O layer already has its own
asynchronous completion mechanism. FreeBSD takes advantage of this
through `aio_qbio` in `/usr/src/sys/kern/vfs_aio.c`, which submits
the request as a `struct bio` directly to the underlying device's
strategy routine and arranges for `aio_biowakeup` to be called on
completion. The bio carries a back-pointer to the `struct kaiocb`
so that completion can find its way back to the AIO framework.

`aio_biowakeup` in `/usr/src/sys/kern/vfs_aio.c` retrieves the
`struct kaiocb` that the bio is carrying, computes the residual
byte count, and calls `aio_complete` with the result.
`aio_complete` sets the status and error fields on the kaiocb,
marks it as finished, and then calls `aio_bio_done_notify`, which
fans out to any kqueue registration on the kaiocb, any blocking
waiter in `aio_suspend`, and any signal registration that the
userland requested through the `aiocb.aio_sigevent` field.

`aio_biocleanup` is the companion helper that frees the bio's
buffer mappings and returns the bio itself to its pool. Every bio
used on the AIO path passes through it, either on the wakeup path
or in the cleanup loop when submission fails partway through a
multi-bio request.

This path is entirely internal to the block I/O layer. A character
driver that is not a block device will never see it. A block device
driver sees exactly the same bios it would see from any other
source: the driver cannot tell that this particular bio came from
`aio_read` rather than from `read` on a buffer cache page. That is
the point. AIO fits into the block layer by reusing the existing
strategy contract, not by adding a parallel path. A block driver
that gets its strategy routine right gets AIO for free.

### The Worker Thread Path: aio_process_rw

When `aio_qbio` is not applicable, as is the case for almost every
character driver, `aio_queue_file` falls through to
`aio_schedule(job, aio_process_rw)`. That puts the job on the AIO
work queue. One of the pre-spawned AIO daemon threads (the pool
size is tunable through the `vfs.aio.max_aio_procs` sysctl) picks
it up, runs `aio_process_rw`, and does the actual I/O.

`aio_process_rw` in `/usr/src/sys/kern/vfs_aio.c` is the heart of
the worker path. It prepares a `struct uio` from the kaiocb's
fields, calls `fo_read` or `fo_write` on the file, and passes the
return value on to `aio_complete`. From the driver's point of view,
the I/O comes in through a perfectly ordinary read or write call,
with one subtle difference: the calling thread is an AIO daemon, not
the process that submitted the request. The user credentials are
correct because the AIO framework preserved them, but the process
context is the AIO daemon's. Drivers that rely on `curthread` or
`curproc` for their own bookkeeping might see surprising values;
drivers that do not, which is almost all of them, behave
identically whether the caller is the user's own thread or an AIO
daemon.

The worker-thread path is not "async" in the hardware sense. It is
"async" in the API sense: the userland did not block. The
substitution happens at the thread boundary, not at the I/O
boundary, so a slow device still ties up an AIO worker while it
serves the request. For most cdev drivers, this is exactly the
right trade-off. The userland gets the programming model it wants;
the kernel uses a worker thread to get the work done; the driver
does nothing special. If the driver already honours `O_NONBLOCK`
correctly, the worker thread can even submit non-blocking requests
to it and return `EAGAIN` back to the userland through the normal
path.

### Completion: aio_complete, aio_cancel, and aio_return

Once `aio_complete` has been called, the kaiocb enters its finished
state. The userland program will eventually call `aio_return(2)` to
retrieve the byte count, or `aio_error(2)` to check the error code,
or wait on a kqueue or signal to be told that the job has finished.
Those calls look up the kaiocb by its userland pointer and return
the fields that `aio_complete` set.

From the driver's point of view, there is nothing to do on the
return path. The driver does not own the kaiocb, does not free it,
and does not signal completion directly. Completion is announced by
`aio_complete`; `aio_return` is a userland concern handled entirely
by the kernel's AIO layer. The driver's job ended when it satisfied
the `fo_read` or `fo_write` call, or when the strategy routine
called `biodone` on the bio.

For cancellation, `aio_cancel` in `/usr/src/sys/kern/vfs_aio.c`
ultimately calls `aio_complete(job, -1, ECANCELED)`. That is it.
The job is marked complete with an error, and the usual wakeup
paths fire. The driver does not need to know about cancellation at
all unless it implements its own long-running request-holding
queue, which is exceptional.

One distinction is worth making explicit. `aio_cancel` is the
kernel-side cancellation function used internally by AIO; it is
not the userland syscall. The userland-facing `aio_cancel(2)` takes
a file descriptor and a pointer to an `aiocb` and asks the kernel
to cancel one or all pending requests. Internally that winds up
calling the kernel `aio_cancel` on each matching kaiocb. The naming
is a little unfortunate; reading the source makes it obvious which
one is which.

### EVFILT_AIO: How AIO Uses kqueue

It is worth knowing, though not acting on, that `EVFILT_AIO` exists.
Declared in `/usr/src/sys/sys/event.h` and implemented in
`/usr/src/sys/kern/vfs_aio.c` as the `aio_filtops` table, it lets
userland programs wait for AIO completions on a kqueue. The
filterops are registered once at AIO module load by
`kqueue_add_filteropts(EVFILT_AIO, &aio_filtops)`. The per-kaiocb
callbacks are:

```c
static const struct filterops aio_filtops = {
    .f_isfd   = 0,
    .f_attach = filt_aioattach,
    .f_detach = filt_aiodetach,
    .f_event  = filt_aio,
};
```

`f_isfd` is zero here because an AIO registration is keyed on the
kaiocb, not on a file descriptor. `filt_aioattach` links the knote
into the kaiocb's own knlist. `filt_aio` reports the completion
status by checking whether the kaiocb has been marked finished. The
`kl_autodestroy` field of the kaiocb's knlist is set, so the knlist
can be torn down automatically when the kaiocb is freed. This is
one of the few places in the tree where `kl_autodestroy` is
actually exercised, which makes `vfs_aio.c` useful reading if you
ever need to understand how that flag is used.

None of this is driver business. The AIO module registers
`EVFILT_AIO` once at boot, and from then on the userland can wait
for completions through kqueue without any further driver
involvement. A driver that wants userland to be able to wait for
driver-originated events through kqueue does so through
`EVFILT_READ` or `EVFILT_WRITE`, not through `EVFILT_AIO`.

### Why kqueue Is the Right Answer for Most Drivers

Bringing this together, the guidance for driver authors is clear.

If the driver is a block device, the kernel already wires AIO into
the bio path through `aio_qbio`. No additional work is needed. A
block driver that serves its strategy routine correctly also serves
AIO correctly.

If the driver is a character device that emits events and wants
userland to wait for them without blocking a thread, the right
mechanism is `kqueue`. Userland registers `EVFILT_READ` or
`EVFILT_WRITE` on the driver's file descriptor and the driver
notifies waiters through `KNOTE_LOCKED`. This is what we have been
building throughout this chapter, and it is what the drivers we
have read all do.

If the driver is a character device that userland programmers would
like to call with `aio_read(2)` for portability reasons, no work is
required on the driver side. AIO will serve the request through a
worker thread that calls the driver's existing `d_read`. The
userland gets the portability it wants; the driver gets to stay
simple.

The only time a driver might consider implementing `d_aio_read` or
`d_aio_write` is when it has a high-performance, genuinely
asynchronous hardware path that can complete work without blocking
a worker thread, and when the cost of the worker-thread fallback
would be prohibitive. This is exceptionally rare in ordinary
drivers, and the drivers that do have such a path (storage drivers,
mostly) usually expose it through the block layer rather than as a
cdev.

In short: for cdev drivers, "implement AIO" almost always means
"implement kqueue." The remaining AIO machinery belongs to the
kernel, not to us. And that is the note we wanted to end this part
of the chapter on, because it closes the loop: of the four
asynchronous mechanisms (poll, kqueue, SIGIO, AIO), the one that
needs the most driver code is kqueue, and the one that needs the
least is AIO. The chapter has therefore spent its time on the
mechanism that matters.

### Reading vfs_aio.c: A Guide

For readers who want to trace the AIO path through the kernel,
`/usr/src/sys/kern/vfs_aio.c` is organised as follows.

Near the top of the file, the `struct kaiocb` and `struct kaioinfo`
are discussed (through comments in the surrounding code, since the
structures themselves are declared in `/usr/src/sys/sys/aio.h`).
The `filt_aioattach`/`filt_aiodetach`/`filt_aio` set of static
functions and the `aio_filtops` table appear next. These are the
kqueue integration for `EVFILT_AIO`.

The SYSINIT and module registration come after that, with
`aio_onceonly` doing the one-time setup that includes
`kqueue_add_filteropts(EVFILT_AIO, &aio_filtops)`. This is where
the system-wide `EVFILT_AIO` filter is installed. No driver
participates; the AIO module does it alone.

The middle portion of the file is the heart of AIO: `aio_aqueue`
(the syscall-layer entry point), `aio_queue_file` (the generic
dispatcher), `aio_qbio` (the bio-based path), `aio_process_rw`
(the worker-thread path), `aio_complete` (the completion
announcement), and `aio_bio_done_notify` (the wake-up fan-out).
Tracing from `aio_aqueue` through each of these in turn maps the
life of an AIO request from submission to completion.

The completion-signalling functions include `aio_bio_done_notify`,
which walks the kaiocb's knlist and fires `KNOTE_UNLOCKED` on any
registered `EVFILT_AIO` knote, wakes any thread blocked in
`aio_suspend`, and delivers any registered signal through
`pgsigio`. This is the AIO analogue of the combined producer path
we saw in the evdev driver.

Cancellation lives in `aio_cancel` and the syscall-layer
`kern_aio_cancel`. `aio_cancel` on a kaiocb simply calls
`aio_complete(job, -1, ECANCELED)`, which pushes the job through
the same completion path as a successful one. The userland sees
`ECANCELED` instead of a byte count.

The file ends with the syscall implementations for
`aio_read`, `aio_write`, `aio_suspend`, `aio_cancel`,
`aio_return`, `aio_error`, and friends, plus the `lio_listio`
batch submission. These all eventually call into the core
dispatchers in the middle of the file.

A reader who traces a userland `aio_read` through `aio_aqueue` to
`aio_queue_file`, through either `aio_qbio` or `aio_process_rw`,
and then through `aio_complete` back to `aio_bio_done_notify`, has
seen the whole AIO path end to end. The file is long but the
structure is regular, and the parts that concern drivers are a
small fraction of the whole.

### A Driver-Side Checklist

Now that we have discussed what AIO does and does not ask of
drivers, here is a short checklist that driver authors can use as a
quick reference.

For a cdev driver that only needs basic readable-event
notification, there is nothing to do for AIO. Implement `d_read`,
implement `d_poll` or `d_kqfilter` for non-blocking notification,
and the userland can use `aio_read(2)` through the AIO worker
thread with no additional driver code.

For a cdev driver that wants to be friendly to userland programs
using AIO for portability reasons, the same answer applies: nothing
extra is needed. The AIO worker thread handles it.

For a block device driver, the bio layer handles AIO through
`aio_qbio` and `aio_biowakeup`. A block driver that services its
strategy routine correctly also services AIO correctly. Again,
nothing extra is needed.

For a driver that has a genuinely asynchronous hardware path and
wants to expose it through AIO without going through a worker
thread, the `d_aio_read` and `d_aio_write` hooks on `cdevsw` exist
but are rare enough that implementing them is outside the scope of
this chapter. Such a driver should study the file-ops `fo_aio_queue`
mechanism in `/usr/src/sys/kern/vfs_aio.c` and the handful of
subsystems that use it.

For every other driver, the answer is simpler still: implement
kqueue, let userland wait for events the efficient way, and treat
AIO as a userland convenience that the kernel handles without the
driver's involvement.

### Wrapping Up Section 5

We now have three independent asynchronous notification mechanisms
in our driver: `poll()`, `kqueue()`, and `SIGIO`. Each is relatively
small on its own, and each can be implemented without interfering
with the others. The pattern, in every case, is the same: register
interest in the waiter path, deliver notification in the producer
path, and be careful about locking and cleanup.

But these three mechanisms assume the driver has a well-defined
notion of "an event is ready." So far our discussion has been
abstract about what an event actually is. In the next section we
look at how a driver organizes its events internally, so that a
single `read()` call can produce a clean, well-typed record rather
than raw hardware state. The internal event queue is the piece that
ties the whole asynchronous design together.

## 6. Internal Event Queues and Message Passing

Up to this point we have treated "an event is ready" as a fuzzy
condition. In real drivers, the condition is usually concrete: there
is a record in an internal queue. The producer inserts records, the
consumer reads them, and the asynchronous notification mechanisms
tell the consumer when the queue has gained or lost records. Getting
the queue right is what makes the rest of the driver simple.

An event queue has several attributes that distinguish it from a raw
byte buffer. Each entry is a structured record, not a stream of
bytes: a typed event with a payload. Entries are delivered whole,
not partially: a reader either gets a complete record or gets no
record. The queue has a bounded size, so producers must have a
policy for what happens when the queue fills up: drop the oldest,
drop the newest, report an error, or wait for room. And the queue
is consumed in order: events are delivered in the order they were
inserted, unless the design explicitly allows otherwise.

Designing the queue carefully pays off throughout the driver. A
reader who sees a stream of well-typed records can write simple,
robust userland code. A producer who knows the queue's policy on
overflow can make sensible decisions when events arrive faster than
they can be consumed. The asynchronous notification mechanisms
(`poll`, `kqueue`, `SIGIO`) all become cleaner, because they can
each express their condition in terms of queue emptiness rather than
in terms of arbitrary per-device state.

### Designing the Event Record

The first decision is what a single event looks like. A minimal
record for our `evdemo` driver:

```c
struct evdemo_event {
    struct timespec ev_time;    /* timestamp */
    uint32_t        ev_type;    /* event type */
    uint32_t        ev_code;    /* event code */
    int64_t         ev_value;   /* event value */
};
```

This mirrors the layout of real event interfaces like `evdev`, which
is no accident: a timestamp plus a (type, code, value) triple is
enough to describe most event streams, from keyboard keypresses to
sensor readings to button events on a game controller. The
timestamp lets userland reconstruct when the event happened
regardless of when it was consumed, which matters for latency-
sensitive applications.

A driver that needs more structure can add fields, but the
discipline of keeping the record fixed-size is worth defending. A
fixed-size record makes the queue's memory management easy, makes
the read path a simple copy, and avoids ABI issues that arise when
records have variable length.

### The Ring Buffer

The queue itself can be a simple ring buffer of fixed capacity:

```c
#define EVDEMO_QUEUE_SIZE 64

struct evdemo_softc {
    /* ... */
    struct evdemo_event sc_queue[EVDEMO_QUEUE_SIZE];
    u_int               sc_qhead;  /* next read position */
    u_int               sc_qtail;  /* next write position */
    u_int               sc_nevents;/* count of queued events */
    u_int               sc_dropped;/* overflow count */
    /* ... */
};

static inline bool
evdemo_queue_empty(const struct evdemo_softc *sc)
{
    return (sc->sc_nevents == 0);
}

static inline bool
evdemo_queue_full(const struct evdemo_softc *sc)
{
    return (sc->sc_nevents == EVDEMO_QUEUE_SIZE);
}

static void
evdemo_enqueue(struct evdemo_softc *sc, const struct evdemo_event *ev)
{
    mtx_assert(&sc->sc_mtx, MA_OWNED);

    if (evdemo_queue_full(sc)) {
        /* Overflow policy: drop oldest. */
        sc->sc_qhead = (sc->sc_qhead + 1) % EVDEMO_QUEUE_SIZE;
        sc->sc_nevents--;
        sc->sc_dropped++;
    }

    sc->sc_queue[sc->sc_qtail] = *ev;
    sc->sc_qtail = (sc->sc_qtail + 1) % EVDEMO_QUEUE_SIZE;
    sc->sc_nevents++;
}

static int
evdemo_dequeue(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    mtx_assert(&sc->sc_mtx, MA_OWNED);

    if (evdemo_queue_empty(sc))
        return (-1);

    *ev = sc->sc_queue[sc->sc_qhead];
    sc->sc_qhead = (sc->sc_qhead + 1) % EVDEMO_QUEUE_SIZE;
    sc->sc_nevents--;
    return (0);
}
```

Several things about this code are worth calling out.

We use a simple modular arithmetic ring rather than a linked list.
This keeps the memory footprint fixed, avoids allocations at event
time, and makes the queue lock-free from a cache-line point of
view (two reads and a write per operation). Most drivers with this
pattern use a ring.

We track `sc_nevents` separately from the head and tail pointers.
Using head and tail alone, without a count, leads to the classic
ambiguity between "empty" and "full": when head equals tail, the
queue could be either state. The count field resolves the
ambiguity and makes the fast paths cheap.

We have an overflow policy baked into `evdemo_enqueue`. When the
queue is full, we drop the oldest event. This is the right policy
for an event stream where recent events are more valuable than
stale ones; a security log or a metrics stream might prefer the
opposite. We also increment `sc_dropped` so that userland can tell
how many events were lost.

Both `evdemo_enqueue` and `evdemo_dequeue` assert that the softc
mutex is held. This is a structural safety net: if the caller
forgets to take the lock, the assertion fires on a debug kernel and
points at exactly the wrong call site. Without the assertion, the
bug might manifest only under rare timing as silent queue
corruption.

### The Read Path

With the queue in place, the synchronous `read()` handler becomes
short:

```c
static int
evdemo_read(struct cdev *dev, struct uio *uio, int flag)
{
    struct evdemo_softc *sc = dev->si_drv1;
    struct evdemo_event ev;
    int error = 0;

    while (uio->uio_resid >= sizeof(ev)) {
        mtx_lock(&sc->sc_mtx);
        while (evdemo_queue_empty(sc) && !sc->sc_detaching) {
            if (flag & O_NONBLOCK) {
                mtx_unlock(&sc->sc_mtx);
                return (error ? error : EAGAIN);
            }
            error = cv_wait_sig(&sc->sc_cv, &sc->sc_mtx);
            if (error != 0) {
                mtx_unlock(&sc->sc_mtx);
                return (error);
            }
        }
        if (sc->sc_detaching) {
            mtx_unlock(&sc->sc_mtx);
            return (0);
        }
        evdemo_dequeue(sc, &ev);
        mtx_unlock(&sc->sc_mtx);

        error = uiomove(&ev, sizeof(ev), uio);
        if (error != 0)
            return (error);
    }
    return (0);
}
```

The pattern is standard: loop while the caller has buffer space left,
wait for a record if the queue is empty, dequeue one under the lock,
release the lock, copy out through `uiomove(9)`. We handle
`O_NONBLOCK` by returning `EAGAIN` when the queue is empty, and we
handle detach by returning zero (end-of-file) so that readers can
cleanly terminate.

The `cv_wait_sig()` call is a condition variable wait that also
returns on signal delivery, so that a reader blocked in `read()`
can be interrupted by `SIGINT` or other signals. This is the
interruptible-wait pattern you may remember from earlier chapters
on synchronization. The condition variable is signalled from the
producer path, which we look at next.

### Integrating the Producer Path

The producer now has three things to do: enqueue the event, signal
any blocked readers through the condition variable, and deliver
asynchronous notifications through the three mechanisms we have
studied:

```c
static void
evdemo_post_event(struct evdemo_softc *sc, struct evdemo_event *ev)
{
    bool async;

    mtx_lock(&sc->sc_mtx);
    evdemo_enqueue(sc, ev);
    async = sc->sc_async;
    cv_broadcast(&sc->sc_cv);
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);

    selwakeup(&sc->sc_rsel);
    if (async)
        pgsigio(&sc->sc_sigio, SIGIO, 0);
}
```

This is the producer's canonical shape. All the state updates and
all the in-lock notifications happen inside the softc mutex; the
out-of-lock notifications happen outside. The order matters: the
in-lock `cv_broadcast` and `KNOTE_LOCKED` happen before we drop the
lock, and the out-of-lock `selwakeup` and `pgsigio` happen after.

One detail is the use of `cv_broadcast()` rather than
`cv_signal()`. If multiple readers are blocked in `read()`, we
usually want to wake all of them so that each can try to claim a
record. With `cv_signal()` we wake only one, and the others stay
asleep until another event arrives. In a single-reader design
`cv_signal()` would be fine; in the general case `cv_broadcast()`
is safer.

### The Poll and Kqueue Integration

The beauty of the internal event queue is that `d_poll` and
`d_kqfilter` become one-liners in terms of queue state:

```c
static int
evdemo_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int revents = 0;

    mtx_lock(&sc->sc_mtx);
    if (events & (POLLIN | POLLRDNORM)) {
        if (!evdemo_queue_empty(sc))
            revents |= events & (POLLIN | POLLRDNORM);
        else
            selrecord(td, &sc->sc_rsel);
    }
    if (events & (POLLOUT | POLLWRNORM))
        revents |= events & (POLLOUT | POLLWRNORM);
    mtx_unlock(&sc->sc_mtx);

    return (revents);
}

static int
evdemo_kqread(struct knote *kn, long hint)
{
    struct evdemo_softc *sc = kn->kn_hook;

    mtx_assert(&sc->sc_mtx, MA_OWNED);

    kn->kn_data = sc->sc_nevents;
    if (sc->sc_detaching) {
        kn->kn_flags |= EV_EOF;
        return (1);
    }
    return (!evdemo_queue_empty(sc));
}
```

The readable filter reports `kn->kn_data` as the number of queued
events, and returns true whenever the queue is non-empty. The
userland program sees `kn_data` and can tell how many events are
available without having to call `read()` yet. This is a small but
useful feature of the kqueue API, and it costs us nothing to
support.

### Exposing Queue Metrics Through sysctl

A diagnostic-friendly driver exposes its queue state through
`sysctl(9)`. For `evdemo` we add a few counters:

```c
SYSCTL_NODE(_dev, OID_AUTO, evdemo, CTLFLAG_RW, 0, "evdemo driver");

SYSCTL_UINT(_dev_evdemo, OID_AUTO, qsize, CTLFLAG_RD,
    &evdemo_qsize, 0, "queue capacity");
SYSCTL_UINT(_dev_evdemo, OID_AUTO, qlen, CTLFLAG_RD,
    &evdemo_qlen, 0, "current queue length");
SYSCTL_UINT(_dev_evdemo, OID_AUTO, dropped, CTLFLAG_RD,
    &evdemo_dropped, 0, "events dropped due to overflow");
SYSCTL_UINT(_dev_evdemo, OID_AUTO, posted, CTLFLAG_RD,
    &evdemo_posted, 0, "events posted since attach");
SYSCTL_UINT(_dev_evdemo, OID_AUTO, consumed, CTLFLAG_RD,
    &evdemo_consumed, 0, "events consumed by read(2)");
```

These can be turned into `counter(9)` counters for cache-friendliness
on multi-core systems, but a simple `uint32_t` is fine for
instructional purposes. With these counters, a `sysctl dev.evdemo`
invocation shows the queue's runtime state at a glance, which is
invaluable when debugging a driver that seems to be missing events
or dropping them.

### Overflow Policies: A Design Discussion

Our code drops the oldest event when the queue fills. Let us think
about when that is the right choice and when it is not.

Dropping the oldest is right when recent events are more valuable
than old ones. A user-interface event queue is a good example: a
program that woke up to find a hundred keystrokes queued usually
cares about the most recent ones, not the ones from five minutes
ago. A telemetry stream where each record is timestamped is similar:
the old records are stale.

Dropping the newest is right when the queue represents a ledger that
must not have gaps. A security log should never lose an event to
overflow; it should rather refuse to log the newest event (and
increment a "dropped" counter) than silently rewrite history.

Blocking the producer is right when the producer can actually wait.
A driver whose producer is an interrupt handler cannot block; a
driver whose producer is a user-space write call can. If the
producer can wait, then a full queue becomes back-pressure that
slows the producer down to match the consumer, which is often
exactly what you want.

Returning an error is right for a request-response protocol where
the caller needs to know immediately whether the command succeeded.
This is more common in ioctl paths than in event queues, but it is
a valid policy.

The common mistake is to pick a policy without thinking about which
one fits the device. A driver that drops old events when a security
log is the underlying data will lose evidence. A driver that drops
new events when a UI needs responsiveness will feel laggy. Picking
the right policy is a design decision, and it is worth documenting
it in the driver's comments so that future maintainers understand
why you chose what you chose.

### Avoiding Partial Reads

One small but important detail: the read path must either deliver a
complete event or no event at all. It must not copy out half an
event and return a short read count, because the userland caller
would then have to reconstruct the event across multiple calls,
which is fragile and error-prone.

The simplest way to enforce this is the guard at the top of the
loop:

```c
while (uio->uio_resid >= sizeof(ev)) {
    /* ... */
}
```

If the user's buffer has fewer bytes left than one event, we simply
stop. The caller gets exactly as many complete events as fit. If
the caller passed a zero-length buffer, we return immediately with
zero bytes, which is the convention for an empty read.

### Handling Event Coalescing

Some drivers have legitimate reasons to coalesce events. If a
keyboard produces "key pressed" followed immediately by "key
released" for the same key, the driver might be tempted to collapse
these into a single "key tapped" event to save queue space. Our
advice is to resist this temptation in most cases. Coalescing
changes the event semantics and can confuse userland programs that
were written to expect raw events.

Where coalescing is justified (for example, coalescing mouse
movements in a way that preserves the final position), implement it
carefully and document it. The coalescing logic should live in the
enqueue path, not in the consumer path, so that all consumers see
consistent behaviour.

### Wrapping Up Section 6

The internal event queue is what ties the asynchronous mechanisms
together. Every notification, every readable check, every kqueue
filter, every SIGIO delivery: all of them reduce to "is the queue
non-empty, or not?" Once the queue is in place, the rest of the
driver becomes a matter of wiring, not of design.

In the next section we look at the design patterns for combining
`poll`, `kqueue`, and `SIGIO` in a single driver, and at the
locking audit that ensures the combination is correct. Adding each
mechanism individually was the easy part. Making them all work
together, with one producer and many simultaneous waiters of
different kinds, is where real driver engineering happens.

## 7. Combining Asynchronous Techniques

So far we have looked at `poll`, `kqueue`, and `SIGIO` one at a
time, each in its own section, each with its own lock discipline
and wakeup pattern. In a real driver, all three mechanisms coexist.
A single producer path has to wake condition-variable sleepers,
poll waiters, kqueue knotes, and signal owners, in a specific
order, under specific locks, without dropping any wakeup and
without deadlocking.

This section is about getting that combination right. It is largely
a review and a consolidation: we have seen each mechanism
individually, and now we see them together. The review is worth
doing because the interactions between mechanisms are exactly the
place where driver bugs like to hide. Small differences in lock
ordering or notification timing that would not cause a visible
problem with one mechanism alone can lead to missed wakeups or
deadlocks when several mechanisms are layered.

### When to Use Each Mechanism

A driver that supports all three mechanisms lets its userland
clients choose the right tool for the job. The three mechanisms
have different strengths:

`poll` and `select` are the most portable. A userland program that
needs to run unchanged on a wide range of UNIX systems will use
`poll`. Drivers should support `poll` because it is the lowest
common denominator, and implementing it is cheap.

`kqueue` is the most efficient and most flexible. Userland programs
that watch thousands of descriptors should use `kqueue`. Drivers
should support `kqueue` because it is the preferred mechanism for
new FreeBSD code and because most applications that care about
performance will choose it.

`SIGIO` is the simplest for a specific class of programs: shell
scripts using `trap`, small single-threaded programs that want the
simplest possible notification, and legacy code. Drivers should
support `SIGIO` because the work is minimal and the supported use
cases are real.

In practice, almost every character driver for an event-driven
device should implement all three. The code is small, the
maintenance is low, and the userland flexibility is high.

### The Producer Path Template

The canonical producer path for a driver supporting all three
mechanisms is:

```c
static void
driver_post_event(struct driver_softc *sc, struct event *ev)
{
    bool async;

    mtx_lock(&sc->sc_mtx);
    enqueue_event(sc, ev);
    async = sc->sc_async;
    cv_broadcast(&sc->sc_cv);
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);

    selwakeup(&sc->sc_rsel);
    if (async)
        pgsigio(&sc->sc_sigio, SIGIO, 0);
}
```

Every piece of this template has a reason for its placement.

The `mtx_lock` acquires the softc mutex. This is the single lock
that serializes all state transitions in the driver, and all
readers and writers respect it.

`enqueue_event` is inside the lock. The queue is the shared state,
and any update to it must be atomic relative to other updates and
to state reads.

`async = sc->sc_async` is inside the lock. This captures a
consistent snapshot of the async flag so that we can use it
outside the lock without racing.

`cv_broadcast` is inside the lock. Condition variables require that
the associated mutex be held when signalling. The signal is
delivered immediately, but the actual wakeup of a blocked thread
happens when the mutex is released.

`KNOTE_LOCKED` is inside the lock. It walks the knote list and
delivers kqueue notifications, and it expects the knlist's lock
(which is our softc mutex) to be held.

`mtx_unlock` releases the softc mutex. After this point we are
outside the critical section.

`selwakeup` is outside the lock. This is the canonical ordering for
`selwakeup`: it must not be called inside arbitrary driver locks
because it takes its own internal locks.

`pgsigio` is outside the lock for the same reason.

This order is the least-error-prone arrangement. Many variations
are possible, but departures from this pattern need to be
justified by a specific reason.

### Lock Ordering

With four distinct notification calls and one state update, lock
ordering matters. Let us work through what locks are in play.

The softc mutex is acquired first and held across the state update
and the in-lock notifications.

`cv_broadcast` does not acquire any additional locks beyond the one
we already hold.

`KNOTE_LOCKED` evaluates each knote's `f_event` callback. The
callbacks execute with the knlist's lock (our softc mutex) held.
Those callbacks must not try to acquire any additional locks,
because doing so would create a nested acquisition that other
paths (say, the consumer in `d_poll`) might take in the opposite
order. In practice, `f_event` callbacks only read state, which is
exactly what we designed for.

`selwakeup` acquires the selinfo's internal mutex and walks the
list of parked threads, waking them. This is done outside the
softc mutex. Internally, `selwakeup` also walks the selinfo's
knote list, but that was already handled by our earlier
`KNOTE_LOCKED` call; doing it twice is harmless but wasteful, so
we do the `KNOTE_LOCKED` while we have the lock and let
`selwakeup` just handle the thread list.

`pgsigio` acquires the signal-related locks and delivers the
signal to the owning process or process group. This is outside
the softc mutex.

The lock ordering rule is: softc mutex first, never nested inside
the selinfo or signal locks. As long as we follow this order, we
cannot deadlock.

### The Consumer Paths

Each of the three consumer paths uses the softc mutex in a
consistent way:

```c
/* Condition-variable consumer: d_read */
mtx_lock(&sc->sc_mtx);
while (queue_empty(sc))
    cv_wait_sig(&sc->sc_cv, &sc->sc_mtx);
dequeue(sc, ev);
mtx_unlock(&sc->sc_mtx);

/* Poll consumer: d_poll */
mtx_lock(&sc->sc_mtx);
if (queue_empty(sc))
    selrecord(td, &sc->sc_rsel);
else
    revents |= POLLIN;
mtx_unlock(&sc->sc_mtx);

/* Kqueue consumer: f_event */
/* Called with softc mutex already held by the kqueue framework */
return (!queue_empty(sc));

/* SIGIO consumer: handled entirely in userland; the driver
 * only sends the signal, never consumes it */
```

All three consumers check the queue under the softc mutex. This is
what closes the race between the producer's state update and the
consumer's check: if the producer has the lock, the consumer waits
and sees the post-update state; if the consumer has the lock, the
producer waits and posts after the consumer has registered.

### Common Pitfalls

A few specific bugs come up often enough to name them explicitly.

**Forgetting one of the notification calls in the producer.** The
canonical ordering looks like a boilerplate sequence, and it is
easy to leave out one of the four calls. Tests that exercise only
one mechanism will pass, but the other mechanisms will be broken.
Code review and automated tests help here.

**Holding the lock during `selwakeup` or `pgsigio`.** The chapter's
advice is to drop the lock before these calls. Some drivers
accidentally hold the lock (for example, because the producer is
deep in an unlocked-lock-unlocked pattern that is hard to refactor).
The result is a latent deadlock that manifests only when a specific
lock is held by a different path.

**Calling `cv_signal` instead of `cv_broadcast`.** A single-reader
driver can use `cv_signal`. A driver that allows multiple readers
must use `cv_broadcast`, because only one of the signalled waiters
will succeed in dequeuing an event and the others must see the
updated state to re-sleep. If you pick `cv_signal` and then allow
multiple readers later, you have introduced a latent missed wakeup
that only appears under contention.

**Forgetting `knlist_init_mtx` at attach.** A driver that never
initializes its knlist will crash on the first `KNOTE_LOCKED`
call, because the knlist's lock function pointers are null. The
symptom is a null-pointer dereference inside `knote()`, and it can
be confusing if you forgot the init call in a refactor.

**Forgetting `funsetown` at close.** A process that enabled
`FIOASYNC` and then exited without closing the fd leaves a stale
`struct sigio` behind. The kernel handles process exit through an
`eventhandler(9)` that calls `funsetown` for us, so this is usually
safe, but leaking the structure during close is still a bug.

**Forgetting `seldrain` and `knlist_destroy` at detach.** Waiters
parked on the selinfo must be woken when the device goes away.
Forgetting this leaves waiters asleep forever and can panic the
kernel when the selinfo is freed.

### Testing the Combined Design

The best way to test a driver that supports all three mechanisms is
to run three userland programs in parallel:

A `poll`-based reader that watches for events and prints them.

A `kqueue`-based reader that does the same with `EVFILT_READ`.

A `SIGIO`-based reader that enables `FIOASYNC` and prints on each
signal.

Trigger events in the driver at a known rate and verify that all
three readers see them all. If any reader lags or misses events,
there is a bug in that mechanism's wiring. Counters on the driver
side help here: if the driver reports 1000 events posted but a
reader reports 900 events seen, one in ten notifications is being
dropped.

Running all three readers at once against the same device stresses
the producer in a way single-mechanism tests do not. Any lock
ordering bug that only manifests when all three are active will
show up under this workload.

### Application Compatibility

A well-behaved driver can expect to work with legacy and modern
userland code, with single-threaded and multi-threaded programs,
with code that picks one mechanism and code that picks another.
The way to achieve this is to support all three mechanisms and to
honour each one's documented contract.

Legacy `select`-based code should work through our `poll`
implementation, because `select` is translated into `poll` in the
kernel.

Modern `kqueue`-based code should work through our `d_kqfilter`,
because `kqueue` is the native mechanism for event-driven userland
on FreeBSD.

Single-threaded programs using `SIGIO` should work through our
`FIOASYNC`/`FIOSETOWN` handling.

Programs that mix mechanisms (for instance, watching some
descriptors with `kqueue` and using `SIGIO` for urgent events)
should also work, because the driver's producer path notifies all
mechanisms on every event.

This is what "application compatibility" means for a driver.
Honour the contracts, notify all waiters, handle cleanup
correctly, and userland code of any vintage will work.

### Wrapping Up Section 7

We have a complete picture now. Three asynchronous mechanisms, one
producer, one queue, one set of locks, one detach sequence. The
combined design is not much more code than any single mechanism
alone; the art is in getting the locks and the ordering right, and
in testing the combination so that latent bugs are found before
they ship.

The next section takes this combined design and applies it as a
refactor of our evolving `evdemo` driver. We will audit the final
code, look at what changed, and release the driver as version
v2.5-async. The refactor is where the abstract advice turns into
concrete, working source.

## 8. Final Refactor for Asynchronous Support

The previous sections built up `evdemo` one mechanism at a time, so
the code we have now is a working but slightly haphazard
accumulation. In this section we refactor the driver as a single
coherent whole, with a consistent locking discipline, a complete
detach path, and a set of exposed counters that let us observe its
behaviour. The result is the companion driver at
`examples/part-07/ch35-async-io/lab06-v25-async/`, which serves as
the reference implementation for the exercises in this chapter.

Calling this the "final" refactor is slightly aspirational: a real
driver is never truly finished. But refactoring after a feature is
built is a useful habit, because it is when the structure of the
code becomes visible as a whole rather than as a series of additions.
Bugs that hid during incremental development often become obvious
once the code is laid out as a single flow.

### Thread-Safety Review

Our review starts with the locking. Every state element in the softc
is now protected by `sc_mtx`, with the following exceptions:

`sc_sigio` is protected internally by the `SIGIO_LOCK` global, not
by our softc mutex. This is correct, because the `fsetown`,
`fgetown`, `funsetown`, and `pgsigio` APIs take the global lock
themselves. We must not take `sc_mtx` before calling those APIs,
or we would invert lock order with the rest of the kernel's signal
code.

`sc_rsel` is protected internally by its own selinfo mutex. We do
not touch the internal list directly; we only call `selrecord` and
`selwakeup`. Those functions take the internal lock themselves.

Everything else (the queue, the counters, the async flag, the
detaching flag, the condition variable wait queue) is protected by
`sc_mtx`.

The audit is: every code path that reads or writes one of these
fields takes `sc_mtx` before the access and drops it after. Let us
walk through each path.

Attach: `sc_mtx` is initialized before any access. Everything else
is zeroed. No concurrent access is possible at attach time because
no handle to the driver exists yet.

Detach: `sc_mtx` is taken to set `sc_detaching = true`,
`cv_broadcast` and `KNOTE_LOCKED` are issued, the lock is dropped,
`selwakeup` is called, and `destroy_dev_drain` is invoked. After
`destroy_dev_drain` returns, no more calls to our callbacks can
start. We can then `seldrain`, `knlist_destroy`, `funsetown`,
`mtx_destroy`, `cv_destroy`, and free the softc.

Open: `sc_mtx` is not strictly needed because the open is
serialized by the kernel, but taking it for internal state updates
is cheap and clarifies the code.

Close: `funsetown` is called outside `sc_mtx`.

Read: `sc_mtx` is held around the queue check, the `cv_wait_sig`
call, and the `dequeue`. The `uiomove` is done outside the lock,
because `uiomove` might page-fault and we do not want to hold
driver locks across faults.

Write: not applicable in `evdemo`, but in a driver that accepts
writes the pattern is symmetric.

Ioctl: `FIOASYNC` takes `sc_mtx`; `FIOSETOWN` and `FIOGETOWN` do
not, because they use `fsetown/fgetown` which have their own
locking.

Poll: `sc_mtx` is held across the check and `selrecord` call.

Kqfilter: `sc_mtx` is taken by the kqueue framework before calling
our `f_event` callback. Our `d_kqfilter` takes it for the
`knlist_add` call.

Producer (`evdemo_post_event` from the callout): `sc_mtx` is held
across the enqueue, the `cv_broadcast`, and the `KNOTE_LOCKED`
call; dropped before `selwakeup` and `pgsigio`.

Every read and write of every softc field is accounted for under
`sc_mtx` or under the appropriate external lock. This is the audit
you want to perform on every asynchronous driver, because it is the
audit that finds latent concurrency bugs before they ship.

### The Complete Attach Sequence

Putting the attach path together, in the order the calls must
happen:

```c
static int
evdemo_modevent(module_t mod, int event, void *arg)
{
    struct evdemo_softc *sc;
    int error = 0;

    switch (event) {
    case MOD_LOAD:
        sc = malloc(sizeof(*sc), M_EVDEMO, M_WAITOK | M_ZERO);
        mtx_init(&sc->sc_mtx, "evdemo", NULL, MTX_DEF);
        cv_init(&sc->sc_cv, "evdemo");
        knlist_init_mtx(&sc->sc_rsel.si_note, &sc->sc_mtx);
        callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);

        sc->sc_dev = make_dev(&evdemo_cdevsw, 0, UID_ROOT, GID_WHEEL,
            0600, "evdemo");
        sc->sc_dev->si_drv1 = sc;
        evdemo_sc_global = sc;
        break;
    /* ... */
    }
    return (error);
}
```

The order is deliberate: first initialize all the synchronization
primitives, then register the callbacks (which can start arriving
at any time after the `make_dev` call), then publish the softc via
`si_drv1` and the global pointer.

One subtlety is `M_WAITOK`. We want a blocking allocation at
attach time because we are in a module-load context, which is
always allowed to sleep. `M_ZERO` is essential because an
uninitialized selinfo, knlist, or condition variable will crash
the kernel. With these flags, the allocation either succeeds with
a zeroed structure or the module load fails cleanly.

### The Complete Detach Sequence

The detach path is more delicate, because we have to coordinate
with in-flight callers and active waiters:

```c
case MOD_UNLOAD:
    sc = evdemo_sc_global;
    if (sc == NULL)
        break;

    mtx_lock(&sc->sc_mtx);
    sc->sc_detaching = true;
    cv_broadcast(&sc->sc_cv);
    KNOTE_LOCKED(&sc->sc_rsel.si_note, 0);
    mtx_unlock(&sc->sc_mtx);
    selwakeup(&sc->sc_rsel);

    callout_drain(&sc->sc_callout);
    destroy_dev_drain(sc->sc_dev);

    seldrain(&sc->sc_rsel);
    knlist_destroy(&sc->sc_rsel.si_note);
    funsetown(&sc->sc_sigio);

    cv_destroy(&sc->sc_cv);
    mtx_destroy(&sc->sc_mtx);

    free(sc, M_EVDEMO);
    evdemo_sc_global = NULL;
    break;
```

The sequence here is worth studying because it contains several
order-sensitive steps.

Setting `sc_detaching` under the lock and broadcasting is what
lets blocked readers wake up and see the flag. Without this, a
reader stuck in `cv_wait_sig` would sleep forever because we are
about to destroy the condition variable.

The `KNOTE_LOCKED` call (with the `EV_EOF` path in `f_event`) lets
any kqueue waiters see the end-of-file.

The `selwakeup` outside the lock wakes poll waiters. They return
to userland and see their file descriptors becoming invalid.

The `callout_drain` stops the simulated event source. Any callout
that is about to fire completes first; no new ones start.

The `destroy_dev_drain` waits for any in-flight callbacks to
return. After this, `d_open`, `d_close`, `d_read`, `d_write`,
`d_ioctl`, `d_poll`, and `d_kqfilter` are all guaranteed to have
returned.

The `seldrain` cleans up any lingering selinfo state.

The `knlist_destroy` verifies the knote list is empty (it should
be, because every knote's `f_detach` was called when the file
descriptor closed) and frees the internal lock state.

The `funsetown` clears the signal owner.

Finally we destroy the condition variable and the mutex, free the
softc, and clear the global pointer.

This careful ordering is the difference between a driver that
unloads cleanly and a driver that panics on the second load. The
test regimen for any serious driver includes a "load and unload a
hundred times in a loop" exercise, because the race windows in a
detach path are often too narrow to hit on a single try.

### Exposing Event Metrics

The finished driver exposes its event metrics through `sysctl`:

```c
SYSCTL_NODE(_dev, OID_AUTO, evdemo, CTLFLAG_RW, 0, "evdemo driver");

static SYSCTL_NODE(_dev_evdemo, OID_AUTO, stats,
    CTLFLAG_RW, 0, "Runtime statistics");

SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, posted, CTLFLAG_RD,
    &evdemo_posted, 0, "Events posted since attach");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, consumed, CTLFLAG_RD,
    &evdemo_consumed, 0, "Events consumed by read(2)");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, dropped, CTLFLAG_RD,
    &evdemo_dropped, 0, "Events dropped due to overflow");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, qlen, CTLFLAG_RD,
    &evdemo_qlen, 0, "Current queue length");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, selwakeups, CTLFLAG_RD,
    &evdemo_selwakeups, 0, "selwakeup calls");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, knotes_delivered, CTLFLAG_RD,
    &evdemo_knotes_delivered, 0, "knote deliveries");
SYSCTL_UINT(_dev_evdemo_stats, OID_AUTO, sigio_sent, CTLFLAG_RD,
    &evdemo_sigio_sent, 0, "SIGIO signals sent");
```

Each counter is incremented under the softc lock from the
producer. The counters are not required for correct operation, but
they are required for the driver to be observable. A driver that
reports zero events consumed while the queue is full tells us the
reader is not draining. A driver that reports more selwakeups than
knotes delivered tells us something about the mix of waiters. A
driver that reports many `sigio_sent` but no visible effect in
userland tells us to check the owner's signal handler.

Observability costs almost nothing to add and pays for itself many
times over in production debugging. Adding it to the final refactor
is part of what makes the driver ready for real use.

### Versioning the Driver

We tag this version as `v2.5-async` in the code and in the companion
example directory. The convention is a simple `MODULE_VERSION`
declaration:

```c
MODULE_VERSION(evdemo, 25);
```

The number is the integer form of the version: 25 for 2.5.
FreeBSD's module-loading infrastructure uses this number to enforce
dependency constraints between modules. A module that depends on
`evdemo` at a specific version can declare that with
`MODULE_DEPEND(9)`. For our standalone driver the version is
mostly informative, but bumping it with every feature release is
a good habit.

### Wrapping Up Section 8

The final `evdemo` driver supports blocking and non-blocking
`read()`, `poll()` with `selrecord`/`selwakeup`, `kqueue()` with
`EVFILT_READ`, and `SIGIO` through `FIOASYNC`/`FIOSETOWN`. It has a
bounded internal event queue with a drop-oldest overflow policy. It
exposes counters through `sysctl` for observability. Its attach
and detach sequences are audited for thread safety. It is a small
driver, about four hundred lines of C, but it demonstrates every
pattern this chapter has taught.

More importantly, it is a template. The patterns you have seen here
generalize to any driver that needs asynchronous I/O. A USB input
device replaces the simulated callout with a real URB callback. A
GPIO driver replaces the callout with a real interrupt handler. A
network pseudo-device replaces the event queue with an mbuf chain.
The asynchronous notification framework (poll, kqueue, SIGIO) stays
the same across all of these. Once you know the pattern, adding
asynchronous support to a new driver is a matter of wiring, not
design.

We have now covered the chapter's core material. The next part of
the chapter is hands-on: a sequence of labs that walks you through
building `evdemo` yourself, adding each mechanism in turn, and
verifying the behaviour with real userland programs. If you have
been reading without running code, now is the time to open a
terminal on your FreeBSD virtual machine and start typing.

## Hands-On Labs

The labs in this section build `evdemo` incrementally. Each lab
corresponds to a folder under `examples/part-07/ch35-async-io/` in
this book's companion source. You can either type each lab from
scratch (which is slower but builds stronger intuition), or start
from the provided sources and focus on the code the lab is
teaching. Either approach is fine; pick whichever matches your
learning style.

A few general notes before we begin.

Every lab uses the same `Makefile` pattern. A `KMOD` line names the
module, a `SRCS` line lists the sources, and `bsd.kmod.mk` does the
rest. Run `make` in the lab directory to produce `evdemo.ko`, and
`sudo kldload ./evdemo.ko` to load it. `make test` builds the
user-space test programs in the same directory.

Every lab exposes a device node at `/dev/evdemo`. If you forget to
unload a previous version of the driver before building a new one,
the load will fail with "device already exists." Run
`sudo kldunload evdemo` to clean up, then reload.

Every lab includes a small test program that exercises the
mechanism the lab is teaching. Running the test program alongside
the driver verifies that the mechanism works end to end. If a test
program hangs or reports an error, something in the driver is
broken, and the lab's troubleshooting notes will usually help you
find it.

### Lab 1: Synchronous Baseline

The first lab establishes a synchronous baseline that the later
labs build on. Our goal here is a minimal `evdemo` driver that
supports blocking `read()` on an internal event queue. No
asynchronous mechanisms yet. This lab teaches the queue data
structures and the condition-variable pattern that everything else
will layer on top of.

**Files:**

- `evdemo.c` - driver source
- `evdemo.h` - shared header with event record definition
- `evdemo_test.c` - user-space reader
- `Makefile` - module build plus test target

**Steps:**

1. Read the contents of the lab directory. Familiarize yourself
   with the structure of `evdemo_softc`, especially the queue
   fields and the condition variable.

2. Build the driver: `make`.

3. Build the test program: `make test`.

4. Load the driver: `sudo kldload ./evdemo.ko`.

5. In one terminal, run the test program:
   `sudo ./evdemo_test`. The program opens `/dev/evdemo` and calls
   `read()`, which will block because no events have been posted.

6. In a second terminal, trigger events:
   `sudo sysctl dev.evdemo.trigger=1`. The sysctl is wired up in
   the driver to call `evdemo_post_event` with a synthetic event.
   The test program should unblock, print the event, and call
   `read()` again.

7. Trigger a few more events. Watch the test program print each
   one as it arrives.

8. Unload the driver: `sudo kldunload evdemo`.

**What to observe:** The `read()` call in the test program blocks
while the queue is empty and returns exactly one event at a time.
The test program does not spin on the CPU while waiting; you can
confirm this by watching `top -H` in a third terminal and noting
that the test process is in the `S` (sleeping) state on a wait
channel named something like `evdemo` or the generic `cv`.

**Common mistakes to check for:** If the test program returns
immediately with zero bytes, the queue may be reporting itself as
empty but the `read()` path is not waiting on the condition
variable. Check that the while loop in `evdemo_read` is actually
calling `cv_wait_sig`. If the test program hangs and never
unblocks even after triggering an event, check that the producer
is actually calling `cv_broadcast` inside the mutex.

**Takeaway:** Blocking `read()` with a condition variable is the
synchronous baseline. It works, but it is not enough for programs
that need to watch multiple descriptors or react to events without
having a thread blocked in `read()` all the time. The next labs add
asynchronous support.

### Lab 2: Adding poll() Support

The second lab adds `d_poll` to the driver so that userland programs
can wait on multiple descriptors or integrate `evdemo` into an event
loop. This lab teaches the `selrecord`/`selwakeup` pattern.

**Files:**

- `evdemo.c` - driver source (extended from Lab 1)
- `evdemo.h` - shared header
- `evdemo_test_poll.c` - poll-based test program
- `Makefile` - module build plus test target

**Changes in the driver from Lab 1:**

Add a `struct selinfo sc_rsel` to the softc.

Initialize it with `knlist_init_mtx(&sc->sc_rsel.si_note, &sc->sc_mtx)`
during attach. Even though we are not yet using kqueue, pre-initializing
the `si_note` knlist is cheap and makes the selinfo compatible with
kqueue support later.

Add a `d_poll` callback:

```c
static int
evdemo_poll(struct cdev *dev, int events, struct thread *td)
{
    struct evdemo_softc *sc = dev->si_drv1;
    int revents = 0;

    mtx_lock(&sc->sc_mtx);
    if (events & (POLLIN | POLLRDNORM)) {
        if (!evdemo_queue_empty(sc))
            revents |= events & (POLLIN | POLLRDNORM);
        else
            selrecord(td, &sc->sc_rsel);
    }
    if (events & (POLLOUT | POLLWRNORM))
        revents |= events & (POLLOUT | POLLWRNORM);
    mtx_unlock(&sc->sc_mtx);

    return (revents);
}
```

Wire it into the `cdevsw`:

```c
.d_poll = evdemo_poll,
```

Call `selwakeup(&sc->sc_rsel)` from `evdemo_post_event` after the
mutex is dropped.

Call `seldrain(&sc->sc_rsel)` and
`knlist_destroy(&sc->sc_rsel.si_note)` during detach.

**Steps:**

1. Copy the Lab 1 source to start from.
2. Apply the changes above.
3. Build: `make`.
4. Build the test program: `make test`.
5. Load: `sudo kldload ./evdemo.ko`.
6. Run the poll-based test: `sudo ./evdemo_test_poll`. It should
   call `poll()` with a 5-second timeout and print the result. With
   no events posted, `poll()` returns zero after the timeout.
7. Trigger an event while the test is running:
   `sudo sysctl dev.evdemo.trigger=1`. The `poll()` call should
   return immediately with `POLLIN` set, and the program should
   read the event.
8. Try `poll()` with several descriptors: the test program's
   extended mode opens `/dev/evdemo` twice and polls both
   descriptors. Trigger events and watch which one fires.

**What to observe:** `poll()` blocks until an event arrives, not
until a timeout elapses, when an event is indeed triggered. The
program does not spin on the CPU; it is genuinely asleep in the
kernel. You can verify this with `top -H` and looking at WCHAN,
which should show `select` or a similar wait channel.

**Common mistakes to check for:** If the poll returns immediately
with `POLLIN` even when the queue is empty, check that your
queue-emptiness check is correct. If the poll returns with the
timeout even after you trigger events, the producer is not calling
`selwakeup`, or it is calling `selwakeup` before updating the
queue. If the kernel panics when you trigger an event, the
selinfo was not properly initialized; check that `M_ZERO` was used
in the softc allocation and that `knlist_init_mtx` was called.

**Takeaway:** `poll()` support is a hundred lines of extra code
and gives every poll-based userland program the ability to
integrate `evdemo`. The key is the lock discipline: the softc
mutex serializes the check and the register in `d_poll` against
the queue update in the producer. Without the lock, the race we
analysed in Section 3 would cause occasional missed wakeups.

### Lab 3: Adding kqueue Support

The third lab adds `d_kqfilter` so that programs using `kqueue(2)`
can integrate `evdemo`. This lab teaches the filter operations
structure and the `KNOTE_LOCKED` delivery pattern.

**Files:**

- `evdemo.c` - driver source (extended from Lab 2)
- `evdemo.h` - shared header
- `evdemo_test_kqueue.c` - kqueue-based test program
- `Makefile`

**Changes in the driver from Lab 2:**

Add the filter operations:

```c
static int evdemo_kqread(struct knote *, long);
static void evdemo_kqdetach(struct knote *);

static const struct filterops evdemo_read_filterops = {
    .f_isfd = 1,
    .f_attach = NULL,
    .f_detach = evdemo_kqdetach,
    .f_event = evdemo_kqread,
};

static int
evdemo_kqread(struct knote *kn, long hint)
{
    struct evdemo_softc *sc = kn->kn_hook;

    mtx_assert(&sc->sc_mtx, MA_OWNED);
    kn->kn_data = sc->sc_nevents;
    if (sc->sc_detaching) {
        kn->kn_flags |= EV_EOF;
        return (1);
    }
    return (sc->sc_nevents > 0);
}

static void
evdemo_kqdetach(struct knote *kn)
{
    struct evdemo_softc *sc = kn->kn_hook;

    knlist_remove(&sc->sc_rsel.si_note, kn, 0);
}
```

Add the `d_kqfilter` callback:

```c
static int
evdemo_kqfilter(struct cdev *dev, struct knote *kn)
{
    struct evdemo_softc *sc = dev->si_drv1;

    switch (kn->kn_filter) {
    case EVFILT_READ:
        kn->kn_fop = &evdemo_read_filterops;
        kn->kn_hook = sc;
        knlist_add(&sc->sc_rsel.si_note, kn, 0);
        return (0);
    default:
        return (EINVAL);
    }
}
```

Wire it into the `cdevsw`:

```c
.d_kqfilter = evdemo_kqfilter,
```

Add a `KNOTE_LOCKED(&sc->sc_rsel.si_note, 0)` call inside the
producer's critical section. Between the `cv_broadcast` and the
`mtx_unlock`.

Add `knlist_clear(&sc->sc_rsel.si_note, 0)` at the top of detach,
before `seldrain`, to remove any still-attached knotes that did
not get their `f_detach` called (for instance, because a kqueue
was closed with the device's knote still attached).

**Steps:**

1. Copy the Lab 2 source.
2. Apply the changes above.
3. Build and load.
4. Run the kqueue-based test:
   `sudo ./evdemo_test_kqueue`. The program opens
   `/dev/evdemo`, creates a kqueue, registers
   `EVFILT_READ` for the device, and calls `kevent()` in blocking
   mode.
5. Trigger events and watch the kqueue reader print them.

**What to observe:** The kqueue reader reports events through the
`kevent()` API rather than through `poll()`. It gets the
`kn_data` value in `ev.data`, which tells it how many events are
queued.

**Common mistakes to check for:** If the kqueue reader returns
immediately with an error, `d_kqfilter` may be returning
`EINVAL` because of a wrong case. Check the switch statement.
If the kqueue reader hangs even after events are triggered,
`KNOTE_LOCKED` is probably not being called, or is being called
outside the lock. If the kernel panics on module unload with
complaints about a non-empty knote list, `knlist_clear` is
missing.

**Takeaway:** `kqueue` support is another hundred lines of code.
The structure is similar to `poll`: a check in the event
callback, a delivery in the producer, and a detach step. The
framework handles the heavy lifting.

### Lab 4: Adding SIGIO Support

The fourth lab adds asynchronous signal delivery. This lab teaches
`FIOASYNC`, `fsetown`, and `pgsigio`.

**Files:**

- `evdemo.c` - driver source (extended from Lab 3)
- `evdemo.h`
- `evdemo_test_sigio.c` - SIGIO-based test program
- `Makefile`

**Changes in the driver from Lab 3:**

Add the async support to the softc:

```c
bool              sc_async;
struct sigio     *sc_sigio;
```

Add the three ioctls to the ioctl handler:

```c
case FIOASYNC:
    mtx_lock(&sc->sc_mtx);
    sc->sc_async = (*(int *)data != 0);
    mtx_unlock(&sc->sc_mtx);
    break;

case FIOSETOWN:
    error = fsetown(*(int *)data, &sc->sc_sigio);
    break;

case FIOGETOWN:
    *(int *)data = fgetown(&sc->sc_sigio);
    break;
```

Add `pgsigio` delivery to the producer, outside the lock:

```c
if (async)
    pgsigio(&sc->sc_sigio, SIGIO, 0);
```

Add `funsetown(&sc->sc_sigio)` to the close path and the detach
path.

**Steps:**

1. Copy Lab 3.
2. Apply the changes above.
3. Build and load.
4. Run the SIGIO-based test:
   `sudo ./evdemo_test_sigio`. The program installs a SIGIO
   handler, calls `FIOSETOWN` with its PID, calls `FIOASYNC` to
   enable, and then pauses in a loop, draining the driver with
   non-blocking reads whenever the handler sets the flag.
5. Trigger events and watch the program print each one.

**What to observe:** Each event arrives via a signal, not through
a blocking `read()` or a `poll()`. The signal handler itself does
not read from the device; it sets a flag, and the main loop
reads. This is the standard pattern for SIGIO handlers.

**Common mistakes to check for:** If the test program does not
see any signals, `FIOASYNC` may not be enabling `sc_async`, or
the producer is not checking `sc_async`. Also check that
`fsetown` was called before the producer fires.

If the test program aborts with an error about SIGIO, the signal
handler may not be installed, or the signal may be masked. Use
`sigprocmask` or `sigaction` with `SA_RESTART` if you want system
calls to be automatically restarted across signal delivery.

**Takeaway:** SIGIO is simpler than poll or kqueue from the
driver's point of view: one ioctl handler, one call to `fsetown`,
one call to `pgsigio`. The userland side is more complex because
signals have inherently tricky semantics.

### Lab 5: The Event Queue

The fifth lab focuses on the internal event queue itself. We
reorganize the driver so that the queue is the single source of
truth for all the asynchronous mechanisms, and we add sysctl-based
introspection so we can watch queue behaviour at runtime.

**Files:**

- `evdemo.c` - driver source with polished queue implementation
- `evdemo.h` - shared header with the event record
- `evdemo_watch.c` - a diagnostic tool that prints queue metrics
- `Makefile`

**What changes:**

The queue functions become standalone and well-documented. Every
operation takes the softc mutex, asserts it with `mtx_assert`, and
uses a consistent naming convention.

A `sysctl` subtree under `dev.evdemo.stats` exposes queue length,
total events posted, total events consumed, and total events
dropped due to overflow.

A `trigger` sysctl allows the userland to post a synthetic event
of a given type, which simplifies testing without having to write
and load a custom test program.

A `burst` sysctl posts a batch of events all at once, which
exercises the queue's overflow behaviour.

**Steps:**

1. Copy Lab 4.
2. Apply the queue polish: extract the enqueue/dequeue operations
   into clearly named helpers, add the counters, add the sysctl
   entries.
3. Build and load.
4. Run `sysctl dev.evdemo.stats` in a loop to watch queue state:
   `while :; do sysctl dev.evdemo.stats; sleep 1; done`.
5. Trigger bursts:
   `sudo sysctl dev.evdemo.burst=100`. Watch the queue fill up,
   then drop overflow events when the queue is full.
6. Run any of the reader test programs (poll, kqueue, or SIGIO)
   while triggering bursts. Watch the reader drain the queue.

**What to observe:** The queue length reported in sysctl tracks the
number of events that have been posted but not yet consumed. The
dropped counter grows when events are posted while the queue is
full. The posted and consumed counters diverge when the reader is
slower than the producer, and converge when the reader catches up.

**Common mistakes to check for:** If the dropped counter grows
without the overflow policy firing, the queue's fullness check is
wrong. If the posted counter grows but the consumed counter does
not, the producer is enqueuing but the reader is not dequeuing
(which might be correct if no reader is running, but usually
means a bug in the read path).

**Takeaway:** The event queue is what the three asynchronous
mechanisms pivot around. With sysctl observability, we can watch
the queue's behaviour directly and verify that it is doing what
we expect under various loads.

### Lab 6: The Combined v2.5-async Driver

The final lab is the consolidated `evdemo` driver, with all three
asynchronous mechanisms, the audited locking discipline, the
exposed metrics, and the clean detach path. This is the
reference implementation that future drivers can be modeled on.

**Files:**

- `evdemo.c` - full reference driver
- `evdemo.h` - shared header
- `evdemo_test_poll.c` - poll-based test
- `evdemo_test_kqueue.c` - kqueue-based test
- `evdemo_test_sigio.c` - SIGIO-based test
- `evdemo_test_combined.c` - test that runs all three at once
- `Makefile`

**What this lab demonstrates:**

The combined test program forks three children. One uses `poll`,
one uses `kqueue`, one uses `SIGIO`. Each child opens its own file
descriptor to `/dev/evdemo` and watches for events. The parent
triggers events at a known rate and reports after a fixed
duration.

**Steps:**

1. Build and load.
2. Run the combined test: `sudo ./evdemo_test_combined`. It
   forks the three children, triggers 1000 events at a few
   hundred per second, and prints a summary at the end.
3. Observe that all three readers see all events.

**What to observe:** The posted counter in sysctl equals the sum of
events seen across all three readers. None of the mechanisms drops
events. The readers finish within a few milliseconds of each
other, demonstrating that the driver is responsive to all three
simultaneously.

**Common mistakes to check for:** If one reader is consistently
behind, check that its mechanism's notification is being issued
on every event. If the three readers produce different event
counts, one mechanism is dropping notifications, which suggests a
missed wakeup in the producer.

**Takeaway:** A driver that implements all three asynchronous
mechanisms correctly serves any userland caller. This is the
target to aim for when you build a production driver for an
event-driven device. Once you know the pattern, the work is
mechanical.

### Lab 7: Unload Stress Test

The final lab is a stress test of the detach path, because detach
is where the subtle bugs in asynchronous drivers tend to hide.

**Files:**

- `evdemo.c` from Lab 6
- `evdemo_stress.sh` - shell script that loads, exercises, and
  unloads the driver in a loop

**Steps:**

1. Load the driver.
2. In one terminal, run the combined test continuously in a
   loop.
3. In another terminal, run the stress script:
   `sudo ./evdemo_stress.sh 100`. This loads, exercises,
   unloads, and reloads the driver one hundred times in a row,
   exercising the attach and detach sequences under concurrent
   readers.
4. Observe that no panics occur, that all readers terminate
   cleanly across each unload-reload cycle, and that sysctl
   counters reset to zero on each attach.

**What to observe:** A driver with correct detach logic can
sustain a hundred or a thousand load/unload cycles without
panicking, leaking memory, or hanging. A driver with an
incorrect detach will typically panic within ten or twenty
cycles.

**Common mistakes to check for:** The most common detach bug is
forgetting to drain in-flight callers before freeing the softc.
`destroy_dev_drain` is the canonical tool for this; without it,
an in-flight `read()` or `ioctl()` can touch a freed softc.

The second most common bug is a mismatch between the attach and
detach initialization order. `knlist_init_mtx` must happen before
the device is published, because a `kqfilter` call can arrive
immediately after. Symmetrically, `knlist_destroy` must happen
after the device is drained.

**Takeaway:** Stress-testing the unload path is the single most
effective test for an asynchronous driver. If your driver
survives 100 load/unload cycles under concurrent load, it is
probably solid.

## Challenge Exercises

These exercises are optional. They build on the labs to sharpen
your skills in specific areas. Take your time; there is no rush.

### Challenge 1: Dual Mechanism Shootout

Modify `evdemo_test_combined` to measure the per-event latency of
each mechanism: the time between the producer's `evdemo_post_event`
call and the userland reader's `read()` return. Use the
`CLOCK_MONOTONIC` clock and record the timing on the event record
itself.

Report a small table showing the mean, median, and 99th percentile
latency for each of `poll`, `kqueue`, and `SIGIO`. Try it with no
contention (one reader per mechanism) and with contention (three
readers per mechanism). Which mechanism has the lowest latency
under no contention? Under contention?

The expected answer is that `kqueue` is lowest, `poll` second, and
`SIGIO` variable (because signal delivery latency depends on the
reader's current execution state). But the details depend on your
hardware, and the exercise is to measure rather than to predict.

### Challenge 2: Multi-Reader Stress

Open twenty file descriptors to `/dev/evdemo` and poll all of them
at once from a single thread using `kqueue`. Trigger 10000 events
and verify that each event is delivered to all twenty descriptors
exactly once.

This tests that the driver's knote list handles multiple knotes
correctly and that `KNOTE_LOCKED` walks the list completely on
every event.

### Challenge 3: Observe the Missed Wakeup Race

The third challenge asks you to deliberately break the driver so
that you can observe a missed wakeup. Modify `evdemo_post_event`
so that it updates the queue and calls the notifications outside
the softc mutex instead of inside:

```c
/* BROKEN: race with d_poll */
mtx_lock(&sc->sc_mtx);
evdemo_enqueue(sc, ev);
mtx_unlock(&sc->sc_mtx);
selwakeup(&sc->sc_rsel);
/* ... */
```

This unlocks the producer's enqueue from the consumer's
check-and-register. With a high enough event rate and a busy
consumer, you should occasionally see `poll()` calls that return
after a long delay despite events having been posted.

Try to reproduce the race. Time the `poll()` calls. Report how
often the race fires as a function of event rate. Then restore
the correct locking and verify the race disappears.

The point of this exercise is not to write broken code. It is to
see, with your own eyes, that the lock discipline we described in
Section 3 is not a theoretical nicety but a real correctness
property. Experiencing the race once is worth reading a hundred
descriptions of it.

### Challenge 4: Event Coalescing

Add an event coalescing feature to `evdemo`. When the producer
posts an event of a type that matches the most recent event in
the queue, merge them into a single event with an incremented
counter instead of appending a new entry. This is similar to how
some drivers coalesce interrupt events.

Test it with a burst of a hundred events of the same type. The
queue length should stay at one. Now test it with a hundred
events of alternating types: the queue should fill with alternating
entries.

The challenge is as much about designing the userland contract as
about implementing the feature. What does the reader see when
coalescing happens? How does it know an event was coalesced? What
does the kqueue `kn_data` field report when the queue has one
entry but it represents many events?

There is no single right answer. Document your design choices in
the source and be prepared to defend them.

### Challenge 5: POLLHUP and POLLERR

Add graceful handling of `POLLHUP` and `POLLERR` to the driver.
When the device is detached while a userland program still has it
open, that program should see `POLLHUP` in its next `poll()`
call (along with `POLLIN` if there are still queued events). When
the driver has an internal error that prevents future operations,
it should set an error flag and report `POLLERR` on subsequent
`poll()` calls.

Test it by arranging for the driver to be detached while a reader
is polling. The reader should wake up with `POLLHUP` and exit
cleanly.

This teaches the full `poll()` contract and the subtleties of the
`revents` bitmask. It also overlaps with the detach logic, which
is the right place to set the HUP condition.

### Challenge 6: evdev-Style Compatibility

Add a compatibility layer to `evdemo` that implements the evdev
ioctl set, so that your driver becomes visible to existing
evdev-aware userland programs. The key ioctls are
`EVIOCGVERSION`, `EVIOCGID`, `EVIOCGNAME`, and a few others
documented in `/usr/src/sys/dev/evdev/input.h`.

This is a larger exercise and genuinely useful for understanding
how real input devices expose themselves to userland. It requires
reading the evdev source carefully and choosing a sensible
subset to implement.

### Challenge 7: Trace a kqueue Registration End to End

Using `dtrace(1)` or `ktrace(1)`, trace a single `kevent(2)` call
that registers an `EVFILT_READ` on an `evdemo` file descriptor.
Your trace should cover:

- The entry into the `kevent` syscall.
- The call into `kqueue_register` in the kqueue framework.
- The invocation of `fo_kqfilter` on the cdev fileops.
- The entry into `evdemo_kqfilter` (our driver's `d_kqfilter`).
- The `knlist_add` call.
- The return back through the framework to userland.

Capture the stack trace at each point. Then trigger a producer
event in the driver and trace the delivery path:

- The `KNOTE_LOCKED` call in the producer.
- The entry into `knote` in the framework.
- The call to `evdemo_kqread` (our `f_event`).
- The queuing of the notification onto the kqueue.

Finally, the userland reaps the event with another `kevent` call.
Trace that path as well:

- The second entry into the `kevent` syscall.
- The call into `kqueue_scan`.
- The walk of the queued knotes.
- The delivery to userland.

Submit your traces, annotated with a few sentences on what each
part is doing. This exercise forces a direct confrontation with
the kqueue framework source and is the surest way to go from
"understands the callbacks" to "understands the framework." A
reader who completes this challenge will have the confidence to
read any kqueue-using subsystem in the tree.

Tip: `dtrace -n 'fbt::kqueue_register:entry { stack(); }'` is a
reasonable starting point. Build outwards from there, adding probes
on `knote`, `knlist_add`, `knlist_remove`, and your driver's
entry points as you identify them in the source.

### Challenge 8: Observe the knlist Lock Discipline

Write a small test program that opens an `evdemo` device twice
from two different processes, registers an `EVFILT_READ` on each,
and then triggers a producer event. Use `dtrace` to measure how
many times the knlist lock is acquired and released during the
single delivery. Predict the number in advance based on what the
chapter has taught about `KNOTE_LOCKED` and the knlist walk; then
verify against the trace.

Next, modify `evdemo` so that the producer uses `KNOTE_UNLOCKED`
instead of `KNOTE_LOCKED` (while adjusting the surrounding
locking so the call is safe). Repeat the measurement. The number
of acquisitions should change, and the change should match what
the framework does differently in the two code paths.

Tip: `dtrace -n 'mutex_enter:entry /arg0 == (uintptr_t)&sc->sc_mtx/ { @ = count(); }'`
will count mutex acquisitions on a specific mutex if you know its
address. You can find the address through `kldstat -v` plus a bit
of symbolic inspection.

## Troubleshooting Common Mistakes

Asynchronous I/O bugs tend to fall into recognizable categories.
This section collects the most common failure modes, their
symptoms, and their usual causes, so that when you encounter one
you can diagnose it quickly.

### Symptom: poll() never returns

A poll() call blocks forever even though events are being
triggered.

**Cause 1:** The producer is not calling `selwakeup`. Add a
counter to `evdemo_post_event` and verify that it is actually
incrementing when events are triggered.

**Cause 2:** The producer is calling `selwakeup` before the
queue state is updated. Verify that `selwakeup` is called after
`mtx_unlock`, not before.

**Cause 3:** The consumer's `d_poll` is not calling `selrecord`
correctly. Check that the call is made under the softc mutex and
that the selinfo passed is the same one the producer wakes.

**Cause 4:** The consumer is checking the wrong state. Verify
that the queue-emptiness check in `d_poll` is looking at the
same field that the producer updates.

### Symptom: kqueue event fires but read() returns no data

A kqueue reader receives an `EVFILT_READ` event but a subsequent
`read()` returns `EAGAIN` or zero bytes.

**Cause 1:** The queue was drained by another reader between the
kqueue event delivery and the read. This is a benign symptom of
multi-reader contention, not a bug. The reader should loop on
`EAGAIN` and wait for the next event.

**Cause 2:** The `f_event` callback is returning true when the
queue is actually empty. Check the `evdemo_kqread` logic.

**Cause 3:** The event was coalesced or re-filed after the kqueue
delivery. Check for any queue manipulation that could remove the
event after `KNOTE_LOCKED` was called.

### Symptom: SIGIO is delivered but the handler is not called

The driver is calling `pgsigio`, but the userland program never
sees the signal.

**Cause 1:** The program has not installed a handler for `SIGIO`.
By default, `SIGIO` is ignored, not delivered.

**Cause 2:** The program has blocked `SIGIO` with
`pthread_sigmask` or `sigprocmask`. Check the signal mask.

**Cause 3:** The program called `FIOSETOWN` with a wrong PID, so
the signal is going to another process. Verify the argument is
the current process's PID.

**Cause 4:** The driver is calling `pgsigio` only when `sc_async`
is true, but the userland never enabled `FIOASYNC`. Check that
the ioctl handler is updating `sc_async` correctly.

### Symptom: Kernel panic on module unload

The kernel panics during `kldunload evdemo`.

**Cause 1:** `knlist_destroy` is being called on a knlist that
still has knotes attached. Add `knlist_clear` before
`knlist_destroy` to force-remove any remaining knotes.

**Cause 2:** `seldrain` is being called before in-flight callers
have returned. Call `destroy_dev_drain` first, then `seldrain`.

**Cause 3:** The condition variable is being destroyed while a
thread is still waiting on it. Set `sc_detaching = true` and
`cv_broadcast` before `cv_destroy`.

**Cause 4:** The softc is being freed while another thread still
holds a pointer to it. Ensure that the global softc pointer is
cleared after `destroy_dev_drain` returns, not before.

### Symptom: Memory leak on repeated load/unload

After many load/unload cycles, `vmstat -m` shows growing
allocations for the driver's `MALLOC_DEFINE` type.

**Cause 1:** The softc is not being freed on detach. Check that
`free(sc, M_EVDEMO)` is called.

**Cause 2:** `funsetown` is not being called. Each
`fsetown` call allocates a `struct sigio` that must be freed.

**Cause 3:** Some internal allocation (for example, a per-reader
structure) is not being freed at close time. Audit every
allocation path and confirm every `malloc` has a matching
`free`.

### Symptom: Slow-to-wake poll() under load

A poll-based reader usually wakes quickly but occasionally takes
significant time to see an event.

**Cause:** The scheduler's wakeup delivery latency on a busy
system is in the millisecond range. This is not a driver bug;
it is a general property of the kernel's scheduler.

If this latency is unacceptable for your use case, consider
`kqueue` with `EV_CLEAR`, which has slightly lower wakeup
overhead, or use a dedicated kernel thread for the consumer
rather than a userland process.

### Symptom: Events are dropped under load

The driver's `dropped` sysctl counter grows during a burst of
events.

**Cause:** The queue is smaller than the burst size, and the
overflow policy (drop-oldest) is kicking in.

This is working as designed for the default policy. If your
application cannot tolerate drops, increase the queue size or
change the overflow policy to block the producer.

### Symptom: Only one reader wakes up even when multiple are waiting

Several readers are blocked in `read()` or `poll()`, but when an
event is posted only one of them wakes.

**Cause:** The producer is calling `cv_signal` instead of
`cv_broadcast`. `cv_signal` wakes exactly one sleeper;
`cv_broadcast` wakes all of them.

For a driver with multiple concurrent readers,
`cv_broadcast` is the correct choice, because each reader may
race for the event and all of them need to see the wakeup to
decide whether to re-sleep.

### Symptom: The device hangs during detach

`kldunload` does not return, and the kernel shows the thread
blocked somewhere in our detach code.

**Cause 1:** A call is blocked in `d_read` and we did not wake
it before waiting for `destroy_dev_drain`. Set `sc_detaching`,
broadcast, and wake selinfo before calling `destroy_dev_drain`.

**Cause 2:** A callout is in flight and we did not drain it.
Call `callout_drain` before `destroy_dev_drain`, or the callout
may re-enter the driver after we think we are finished.

**Cause 3:** A thread is parked in `cv_wait_sig` on a condition
that will never be broadcast again. Ensure every wait loop
checks `sc_detaching` as a separate exit condition.

### Symptom: Reader wakes but finds nothing to do

A reader is woken by `poll`, `kqueue`, or a blocked `read`, but on
returning to check the queue it finds the queue empty and has to
go back to sleep. This happens occasionally even in a correct
driver.

**Cause:** Spurious wakeups are a normal part of kernel life. The
scheduler may deliver a wakeup that was meant for another waiter,
a different event source sharing the same `selinfo` may have
fired, or a race between the producer and another consumer may
have drained the queue before this reader got a chance to look.
None of these situations indicates a bug.

The correct response in the driver and in the reader is the same:
always re-check the condition after waking, and treat a wakeup as
a hint that something may have happened, not a guarantee that the
specific event you expected is available. Every wait loop in the
driver should look like the pattern we established in Section 3,
with `cv_wait_sig` inside a `while` that checks the real
condition. Every userland reader should expect to see `EAGAIN` or
a zero-length read after a wakeup and loop back to poll again.

If wakeups without work are happening frequently enough to waste
significant CPU, consider whether the producer is calling
`selwakeup` more often than necessary, for example on every
intermediate state change rather than only when a reader-visible
event is ready. Coalescing wakeups at the producer is the fix;
disabling the re-check loop in the consumer is not.

### Symptom: Panic on module unload with "knlist not empty"

The module unload path panics with an assertion failure in
`knlist_destroy` that reads something like "knlist not empty" or
prints a non-zero count on the knlist's list head.

**Cause 1:** `knlist_destroy` was called without a preceding
`knlist_clear`. `knlist_destroy` asserts that the list is empty;
live knotes on the list trigger the panic. Inspect the detach path
and confirm that `knlist_clear(&sc->sc_rsel.si_note, 0)` runs
before `knlist_destroy`.

**Cause 2:** A userland process still has a kqueue registration
open and the driver tried to tear down without forcing the knotes
off. The `knlist_clear` call is designed to handle exactly this
case: it marks every remaining knote with `EV_EOF | EV_ONESHOT`
so that the userland sees a final event and the registration
dissolves. If the driver is skipping `knlist_clear` to "let the
userland detach naturally," the assertion fires. The fix is to
call `knlist_clear` unconditionally in detach.

**Cause 3:** The detach path is being called while an event
delivery is in progress. The kqueue framework uses its own
internal locking to keep delivery and detach coherent, but a
driver that tears down its softc while `f_event` is still running
on another thread will corrupt the lifecycle. Ensure that all
producer paths have stopped (for example, by setting a
`sc_detaching` flag and draining any work queues) before entering
the clear-drain-destroy sequence.

### Symptom: Panic in f_event with a stale kn_hook

The kernel panics inside the driver's `f_event` function with a
backtrace that shows a dereference of freed or garbage memory
through `kn->kn_hook`.

**Cause 1:** The softc was freed before the knlist was torn down.
The driver's detach path must clear and destroy the knlist before
freeing the softc, in that order. Reversing the order leaves live
knotes pointing at freed memory.

**Cause 2:** A per-client state object (for example, an
`evdev_client`) was freed while a knote still references it. The
cleanup logic for per-client state must run the
`knlist_clear`/`seldrain`/`knlist_destroy` sequence on the client's
selinfo before freeing the client struct, not after.

**Cause 3:** A different code path accidentally called `free()` on
the softc or client state. Memory debuggers (`KASAN` on platforms
that support it, or manually instrumented poison patterns on those
that do not) will confirm that the memory is freed when `f_event`
reads it. This is a general memory-corruption debugging exercise;
the knote is the victim, not the cause.

### Symptom: KNOTE_LOCKED panics with a lock-not-held assertion

A producer path that calls `KNOTE_LOCKED` panics with an
assertion like "mutex not owned" inside the knlist lock check.

**Cause:** The producer is calling `KNOTE_LOCKED` without actually
holding the knlist's lock. `KNOTE_LOCKED` is the variant that
tells the framework "skip locking, the caller has it"; if the
caller does not, the framework's assertions catch it. The fix is
either to take the lock (usually the softc mutex) around the
`KNOTE_LOCKED` call, or to use `KNOTE_UNLOCKED` instead and let
the framework take the lock itself.

Read the producer path carefully. A common mistake is to drop the
softc lock partway through a producer function for some other
reason (for example, to call a function that cannot be called
under the lock), and then to forget to re-acquire it before the
`KNOTE_LOCKED` call. The fix is to re-take the lock or to call
`KNOTE_UNLOCKED` instead.

### Symptom: kqueue events arrive but kn_data is always zero

A kqueue waiter wakes up and reads a `struct kevent` whose `data`
field is zero, even though the driver has events pending.

**Cause 1:** The `f_event` function sets `kn->kn_data` only under
certain conditions and leaves it untouched otherwise. The
framework preserves whatever value was last written, so a stale
zero from a previous invocation persists into the next delivery.
The fix is to compute and assign `kn->kn_data` unconditionally at
the top of `f_event`.

**Cause 2:** The `f_event` function is returning non-zero based on
a condition other than the queue depth, and the `kn_data` field
was not updated to reflect the actual count. Check that `kn_data`
is assigned the real depth, not a boolean, and that the comparison
that drives the return value is consistent with it.

### Symptom: poll() works but kqueue never fires

A poll-based waiter sees events correctly, but a kqueue waiter on
the same file descriptor never wakes up.

**Cause 1:** The driver's `d_kqfilter` entry point is not in the
cdevsw. Check the `cdevsw` initializer and confirm that
`.d_kqfilter = evdemo_kqfilter` is present. Without it, the kqueue
framework has no way to register a knote on the descriptor.

**Cause 2:** The producer is calling `selwakeup` but not
`KNOTE_LOCKED`. `selwakeup` does walk the knlist attached to the
selinfo, but only under specific conditions; drivers that want to
reliably wake kqueue waiters should call `KNOTE_LOCKED` (or
`KNOTE_UNLOCKED`) explicitly in the producer path.

**Cause 3:** The `f_event` function always returns zero. Check
whether the ready condition is being evaluated correctly. Add a
`printf` to confirm that `f_event` is being called; if it is but
returns zero, the bug is in the readiness check, not in the
framework.

### General Advice

When debugging an asynchronous driver, add counters liberally.
Every `selrecord`, every `selwakeup`, every `KNOTE_LOCKED`,
every `pgsigio` should have a counter. When behaviour looks
wrong, printing the counters is the fastest way to tell you
which mechanism is misbehaving.

Use `ktrace` on the userland side to see exactly when system
calls return. If the driver thinks it delivered a wakeup at
time T and the userland thinks it returned at time T+5
seconds, the wakeup was queued but not delivered, which often
means a lock was held too long somewhere.

Use DTrace probes in the driver and on `selwakeup` itself.
The `fbt:kernel:selwakeup:entry` probe shows every selwakeup
system-wide. The `fbt:kernel:pgsigio:entry` probe does the
same for signal deliveries. A missing call shows up as a gap in
the probe output.

Do not suspect the framework. The kernel's async-I/O
infrastructure is battle-tested and almost never has bugs at
this level. Suspect your own driver first, particularly the
lock ordering and the attach/detach sequence.

## Wrapping Up

Asynchronous I/O is one of the places where a driver's
correctness is most severely tested. A synchronous driver can hide
many small locking mistakes behind a single-threaded flow that
happens to run serially. An asynchronous driver exposes every
corner of its locking discipline, every race between the producer
and the consumer, and every subtle ordering constraint in the
detach path. Getting an asynchronous driver right is harder than
writing the synchronous version, but the rewards are
significant: the driver serves many users at once, integrates
cleanly with userland event loops, plays well with modern
frameworks, and avoids the performance pathologies of blocking
and busy waiting.

The mechanisms we have studied in this chapter are the classical
ones. `poll()` and `select()` are portable across every UNIX
system, and implementing them in a driver is a matter of one
callback and a `selinfo`. `kqueue()` is the preferred mechanism
for modern FreeBSD applications, and it adds one more callback and
a set of filter operations. `SIGIO` is the oldest mechanism and
has some sharp edges in multi-threaded code, but it remains useful
for shell scripts and legacy programs.

Each mechanism has the same underlying shape: a waiter registers
interest, a producer detects a condition, and the kernel delivers
a notification to the waiter. The details differ, but the shape
does not. Understanding the shape makes each specific mechanism
easier to learn. The internal event queue we built in Section 6
is what ties the shape together: every mechanism expresses its
condition in terms of queue state, and every producer updates
the queue before notifying.

The locking discipline is the single habit that most consistently
distinguishes a working asynchronous driver from a broken one.
Take the softc mutex before checking state. Take it before
updating state. Take it before registering a waiter. Take it before
calling the in-lock notifications (`cv_broadcast`,
`KNOTE_LOCKED`). Drop it before calling the out-of-lock
notifications (`selwakeup`, `pgsigio`). This pattern is not an
aesthetic choice; it is the pattern that prevents missed wakeups
and deadlocks. When you see the pattern violated in a driver, ask
why, because nine times out of ten the deviation is a bug.

The detach sequence is the second habit that deserves discipline.
Set the detaching flag under the lock. Broadcast to wake every
waiter. Deliver an `EV_EOF` to kqueue waiters. Call `selwakeup` to
free poll waiters. Call `callout_drain` to stop the producer.
Call `destroy_dev_drain` to wait for in-flight callers. Only
after all of these can you safely `seldrain`, `knlist_destroy`,
`funsetown`, `cv_destroy`, `mtx_destroy`, and free the softc.
Skipping any step is a recipe for a panic at unload time, and
those panics are especially painful to diagnose because they
happen after the code you were testing.

The observability habit is the third. Every counter you add at
development time saves hours of diagnosis when the driver is in
production. Every sysctl entry you expose gives operators and
debuggers a window into the driver's state without rebuilding the
kernel. Every DTrace probe you declare lets a distant engineer
with a production incident see into your code without shipping new
software. Observability is not a luxury; it is a feature, and
writing a driver without it is writing a driver you cannot debug.

You now have every piece of the async-I/O toolkit that a FreeBSD
driver author needs for ordinary work. You can take a blocking
character driver, audit its state transitions, identify the
producer and the consumer paths, add `poll`, `kqueue`, and
`SIGIO` support, and verify the whole thing under stress. The
patterns generalize beyond character drivers: the same mechanisms
apply to pseudo-devices, network devices with control channels,
filesystems with file events, and any other subsystem that
exposes an event stream to userland.

Two final notes before we move on.

First, asynchronous I/O is not a one-time lesson. You will find,
as you read more of the FreeBSD source, that variations of these
patterns appear everywhere: in network drivers using
`grouptaskqueue`, in filesystems using `kqueue` for file events,
in the audit subsystem using a ring buffer shared with userland.
Each variation is an instance of the same underlying ideas.
Being able to recognize the pattern when you see it is more
valuable than memorizing any particular API.

Second, when you write your own driver, resist the temptation to
invent your own asynchronous mechanism. The kernel's provided
mechanisms cover virtually every use case, and userland programs
know how to use them. A custom mechanism is work for you, work
for your users, and work for whoever maintains the driver next.
Reuse the standard patterns. They exist for a reason.

## Bridge to Chapter 36: Creating Drivers Without Documentation

The next chapter changes the kind of challenge we face. Up to now,
every chapter has assumed that the device we are writing for is
documented. We knew its registers, its command set, its error
codes, its timing requirements. The book has shown how to turn
that documentation into working kernel code, and how to test,
debug, and optimize the result.

But not every device is documented. A driver author sometimes
encounters hardware for which no datasheet is available, either
because the vendor refuses to publish one, because the hardware is
so old that the documentation has been lost, or because the device
is a derivative of something documented but with undocumented
changes. In those cases, the craft of driver writing shifts
towards reverse engineering: observing the device's behaviour,
deducing its interface, and producing a working driver from
indirect evidence rather than from specifications.

Chapter 36 is about that craft. We will look at how experienced
authors approach an undocumented device. We will study the tools
for observing device behaviour, from bus analysers and protocol
sniffers to the kernel's own built-in tracing facilities. We will
learn how to build a register map by experiment, how to recognize
common command patterns across vendors, and how to write a driver
that is correct despite incomplete information about the hardware.

The asynchronous mechanisms from this chapter will reappear there,
because event-driven hardware is exactly the kind of hardware that
most rewards careful reverse engineering. A device whose
documentation is missing still talks to the world in events, and
making those events visible through `poll`, `kqueue`, and `SIGIO`
is often the first step in figuring out what the device is
actually doing.

The debugging skills from Chapter 34 will also matter, because an
undocumented device produces many more surprising behaviours than
a documented one, and `KASSERT`, `WITNESS`, and `DTrace` are the
tools for catching those surprises early. The foundation we have
built in Parts 2 through 7 is exactly what the reverse-engineering
chapter needs.

If you have been reading this book from the beginning, take a
moment to appreciate how far you have come. You started with an
empty source tree and no knowledge of the kernel. You now know how
to write a driver that supports synchronous and asynchronous I/O,
handles concurrency correctly, observes its own behaviour through
counters and DTrace probes, and can be debugged on a live system.
You have written enough drivers by this point that the kernel is
no longer an alien environment. It is a place you know how to work
in.

The next chapter takes that knowledge and asks, what if the
documentation for the device is missing? What does the same craft
look like when you are working from evidence rather than from
specifications? The answer, as it turns out, is that the craft
changes less than you might think. The tools are the same, the
disciplines are the same, and the habits you have built carry
you most of the way.

Let us look at how that works.


