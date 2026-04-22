---
title: "Creating Drivers Without Documentation (Reverse Engineering)"
description: "Techniques for developing drivers when documentation is unavailable"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 36
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 180
---

# Creating Drivers Without Documentation (Reverse Engineering)

## Introduction

Up to this point in the book we have always written drivers for
devices whose behaviour was at least partly documented. Sometimes the
documentation was generous, with a full programmer's reference manual
that named every register, defined every bit, and described every
command. Sometimes it was sparse, with only a header file from the
vendor and a short list of opcodes. Even in the sparse cases, however,
we always had a starting point: a hint, a partial datasheet, an
example from a related family of devices, or a manual page that told
us what the device was supposed to do.

This chapter changes that assumption. Here we will learn how to write
a driver for a device whose documentation is missing, lost, or
deliberately withheld. The hardware exists. It works under some other
operating system, or it once did, or someone has captured a few
seconds of its behaviour with a logic analyser. But there is no
register reference, no command list, no description of the data
formats. Every fact we need we will have to discover ourselves.

If that sounds intimidating, take a slow breath. Reverse engineering
a hardware device is a serious craft, but it is not magic. It is the
same engineering discipline you have already practised throughout the
book, applied in a slightly different direction. Instead of reading a
datasheet and writing code that implements it, we will observe the
device, form hypotheses about what it does, test those hypotheses
with small experiments, and write the driver one verified fact at a
time. The end result is the same kind of driver we have written in
earlier chapters, with the same `cdevsw`, the same `device_method_t`
table, the same `bus_space(9)` calls, and the same module build. The
only difference is how we arrived at the contents.

Reverse engineering deserves a chapter for several reasons. The first
is that the situation is more common than newcomers realise. Older
hardware whose vendors have gone out of business; consumer devices
that were never properly documented; embedded peripherals on a custom
board where the manufacturer simply ships a binary blob and a
datasheet expires when the contract does; specialised industrial,
scientific, or medical equipment where the documentation lives on a
CD that nobody can find. All of these are real situations that real
FreeBSD developers run into, and several important FreeBSD drivers
exist today only because someone was patient enough to do the work.

The second reason is that reverse engineering is a particularly
disciplined kind of work, and the discipline is worth learning even
if you never write a fully reverse-engineered driver. The habit of
separating observation from hypothesis, the habit of writing down
every assumption before testing it, and the habit of refusing to
guess where a guess could damage hardware: these habits make ordinary
driver work better, not just reverse-engineering work.

The third reason is that this work happens at a frontier where the
distinction between writing software and doing experimental science
becomes very thin. A reverse-engineering session looks more like a
laboratory notebook than like a programming session. You will run an
experiment, observe a result, write down what you saw, propose an
explanation, design the next experiment, and slowly build up a
picture of an unknown system. That kind of work has its own rhythm,
its own pacing, and its own small set of professional habits, and
this chapter is where we will learn them.

We will begin by asking why reverse engineering is necessary at all,
and where the legal and ethical lines lie. We will then build a
small, safe, controlled lab where the work can happen without risk to
production systems or expensive hardware. We will study the standard
FreeBSD-friendly tools for observing devices, from `usbconfig(8)` and
`pciconf(8)` to `usbdump(8)`, `dtrace(1)`, `ktr(9)`, and the
`bus_space(9)` API itself. We will see how to capture a device's
initialization sequence, how to compare runs to isolate single bits
of meaning, and how to assemble a register map by experiment.

We will look at how to recognise the recurring shapes that hardware
likes to use: ring buffers, command queues, descriptor rings,
status-and-control register pairs, fixed-format command packets. We
will write a minimal driver from scratch, one verified function at a
time, starting with reset and slowly adding more behaviour. We will
validate every assumption we can in a simulator before risking it on
real hardware. We will study how the FreeBSD community already
approaches this work, where to find prior art, and how to publish
your findings in a form that helps others.

And finally, because this is the part that newcomers most often
underestimate, we will spend a serious amount of time on safety. Some
guesses can damage hardware. Some patterns of poking at unknown
registers can erase non-volatile memory, brick a board, or leave a
device in a state that nothing short of a vendor-only repair tool can
recover from. The chapter will show you how to think about that
risk, how to design wrappers that limit it, and how to recognise the
operations that should never be performed without strong evidence
that they are safe.

The companion code for this chapter, in
`examples/part-07/ch36-reverse-engineering/`, includes a small
collection of tools that you can build and run on a normal FreeBSD
14.3 development VM: a script that identifies and dumps device
information for a target USB or PCI device, a kernel module that
performs safe register probing on a memory region you point it at, a
mock device that you can use to validate driver code before you have
real hardware in front of you, and a Markdown template for the kind
of pseudo-datasheet that should be the final product of a
reverse-engineering session. Nothing in the labs touches hardware in
a way that could damage it, and every example is safe to run inside a
virtual machine.

By the end of this chapter, you will have a clear, repeatable method
for approaching an undocumented device. You will know how to build
the lab, how to observe, how to hypothesize, how to test, and how to
document. You will know the legal lines that frame the work in the
United States and the European Union, and you will know the
professional habits that protect both you and the hardware. You will
not finish this chapter as an expert reverse engineer, because that
expertise is built up over years of practice, but you will know
enough to start, and you will know how to keep yourself and the
hardware safe while you learn.

## Reader Guidance: How to Use This Chapter

This chapter sits in Part 7 of the book, in the Mastery Topics part,
directly after Asynchronous I/O. It assumes you have read the
asynchronous-I/O chapter, the advanced-debugging chapter, and the
performance chapter, because the tools and habits from those chapters
are the same tools and habits you will use here. If those chapters
feel uncertain, a quick revisit will pay for itself several times
over in this one.

You do not need any special hardware to follow the chapter. The
worked examples use either a small kernel module that pokes at a
memory region the operator gives it, or a mock device that simulates
unknown hardware in software. Both run on a normal FreeBSD 14.3
virtual machine. If you have a real piece of unknown hardware that
you would like to investigate, the chapter will give you the
techniques and the safety habits to start, but the labs themselves
will not require it.

The chapter is intentionally long because reverse engineering is a
field where partial knowledge is dangerous. A reader who learns the
glamorous parts of the craft and skips the safety parts is likely to
brick something expensive. The labs and the safety sections deserve
the same care as the technique sections.

A reasonable reading schedule looks like this. Read the first three
sections in one sitting. They are the conceptual foundation: why
reverse engineering matters, what the legal landscape looks like, and
how to set up the lab. Take a break. Read Sections 4 through 6 in a
second sitting. They cover the central techniques of the craft:
building a register map, identifying buffers, and writing a minimal
driver. Take another break. Read Sections 7 through 11 in a third
sitting. They cover the disciplined work of expanding, validating,
collaborating, staying safe, and documenting. Save the labs for a
weekend or for several short evenings, because the labs will sink in
much better if you have time to sit with the captured data and look
at it carefully.

Some of the techniques in this chapter are slow on purpose. Capturing
a device's initialisation sequence, for example, may require running
the same boot ten times and diffing the captures to isolate the bits
that change. The diffing is part of the lesson. If you find yourself
wanting to skip to the part where the driver works, remember that the
driver will only work if you have done the slow, careful observation
work first. Reverse engineering rewards patience the way few other
parts of systems programming do.

Several of the small kernel modules in the chapter are deliberately
written as exploration scaffolds rather than as production drivers.
They are commented as such. Do not load them on a production system.
A development virtual machine where a kernel panic costs nothing more
than a reboot is the right environment for this kind of work.

If you have hardware you would like to investigate after reading the
chapter, take it slowly. Start with the safest observation tools.
Resist the urge to write to anything until you have a clear,
written-down hypothesis about what the write should do and what the
worst case looks like. If a guess could trigger a flash erase, do not
make the guess. The chapter will spell out which operations deserve
special caution and why.

## How to Get the Most Out of This Chapter

The chapter follows a pattern that mirrors the workflow of a real
reverse-engineering session. Each section either teaches a technique
that fits one phase of that workflow or shows you how the technique
ties back to the underlying discipline. If you learn the workflow as
a whole, the individual techniques will fall into place naturally.

Several habits will help you absorb the material.

Keep a notebook open beside the keyboard. A real notebook, not a text
file, if you can manage it. Reverse engineering is a notetaking
discipline, and the best practitioners keep written records of what
they observe, what they hypothesize, what they tested, and what they
learned. The act of writing slows your thinking down enough to catch
mistakes, and a paper notebook resists the temptation to rearrange
your record after the fact. If a paper notebook is not practical, use
a Markdown file in a Git repository so that you can see how your
understanding has changed over time.

Keep a terminal open to your FreeBSD development machine and another
to `/usr/src/`. The chapter references several real FreeBSD source
files, including drivers and utilities under `/usr/src/usr.sbin/` and
`/usr/src/sys/dev/`. Reading those files is part of the lesson. The
FreeBSD source is itself a body of reverse-engineering work, because
many drivers in the tree exist because someone observed an
undocumented device carefully enough to write code for it.

Type the example modules and scripts yourself the first time you see
them. The companion files in `examples/part-07/ch36-reverse-engineering/`
are there as a safety net and as a reference for when you want to
compare your code against a known-good version, but typing the code
the first time is the part that builds intuition. The whole chapter
is about building intuition for unknown systems, and there is no
shortcut for that.

Pay close attention to the language we use to describe what we know
and what we suspect. Reverse-engineering writing draws a sharp line
between an observation, a hypothesis, and a verified fact. An
observation is what you saw. A hypothesis is what you think the
observation means. A verified fact is a hypothesis that has survived
deliberate attempts to falsify it. Different kinds of statement
deserve different levels of confidence, and the chapter will model
the language carefully so that you can adopt the same precision in
your own notes.

Take seriously the safety advice. The most painful kind of mistake in
this kind of work is the one that destroys the very piece of hardware
you needed to study. Several of the techniques described here can,
if applied carelessly, write to flash memory, change a device ID
permanently, or leave a board in a state that the vendor cannot
recover. The chapter will tell you which patterns deserve the most
caution. Treat that advice the way you would treat a warning sign on
a chemistry lab.

Finally, allow yourself to be slow. Reverse engineering is not
a sprint. A complete register map for a serious peripheral is the
result of weeks or months of patient observation, and the published
results of community projects often hide an enormous amount of
careful work behind a tidy summary. If a particular device resists
your understanding for a long time, that is not a failure. It is what
the work usually feels like.

With those habits in mind, let us begin with the question of why this
work is necessary at all.

## 1. Why Reverse Engineering May Be Necessary

When a new driver author hears about reverse engineering for the
first time, the immediate question is usually some version of "why
should this ever be necessary?" If a hardware vendor wants their
device to be useful, why would they hide its programming interface?
And if the device is well known, why is there ever any documentation
problem?

The honest answer is that the world of hardware and operating systems
is messier than the world of well-documented standards. There are
many real situations where a working device exists, where some
operating system already supports it, but where no public,
machine-readable, redistributable documentation exists for the
programmer who wants to write a driver from scratch. The first
section of this chapter walks through the most common of these
situations so that you can recognise them when you encounter them and
so that you know which kind of investigation each one calls for.

### 1.1 Legacy Hardware With No Vendor Support

The most common reverse-engineering situation is the old device whose
manufacturer no longer exists. A small instrument board from the
1990s, a network card from a company that was bought out and shut
down, an embedded controller from a research project that ran for a
few years and then ended. The hardware works. The hardware was
documented at the time it was sold. But the documentation was a paper
manual that lived in a binder, or a PDF on a CD that was bundled with
the device, and twenty years later neither the binder nor the CD
exists.

In this situation, reverse engineering is the only way to bring the
device back into useful service. Sometimes a community has already
done some of the work and published partial notes. Sometimes a Linux
or NetBSD driver exists and can be read for clues; the two cases are
not equivalent, and the distinction matters both legally and
technically. An OpenBSD or NetBSD driver is BSD-licensed and can be
read, quoted, and, with attribution preserved, ported directly.
A Linux driver is almost always GPL-licensed, which means it can be
read for understanding but cannot be copied into a BSD-licensed
driver at all. Even setting the licence aside, a direct port from
Linux rarely works, because the Linux kernel's locking primitives,
its memory allocators, and its device model differ from FreeBSD's
in ways that reach deep into every line. We return to both the
legal framing and the technical pitfalls of cross-platform reading
in Section 12 and in Section 13's case studies. Sometimes the
device is so obscure that no prior work exists at all, and the
entire job falls on whoever wants to make it work.

FreeBSD has a long history of supporting devices in this category. A
careful reading of the source under `/usr/src/sys/dev/` will turn up
many drivers whose comments mention "no datasheet", "based on
observation", or similar language. The driver authors did the work
and the community benefits. This is not a marginal activity; it is
part of how FreeBSD has always supported a long tail of devices that
their original vendors abandoned long ago.

The challenges in the legacy-hardware case tend to be technical
rather than legal. The hardware is old enough that the original
vendor either no longer exists or no longer cares. The patents have
expired. The trade secrets, if there were any, are no longer being
defended. The risk is mostly that the documentation is genuinely
gone, and no amount of polite asking will recover it.

### 1.2 Devices With Binary-Only Drivers on Other Operating Systems

A second common situation is the device whose vendor publishes a
binary-only driver for one or more operating systems and refuses to
publish documentation that would allow other operating systems to
support the device. This is the case for many proprietary
peripherals: graphics cards from certain vendors, specialised audio
or video capture devices, scientific instruments with a Windows-only
or Linux-only driver, fingerprint readers in laptops, certain
wireless chipsets, and so on.

In this situation the device is in active production. Its
documentation exists, but the vendor either treats it as a trade
secret, or restricts it to companies that sign a non-disclosure
agreement, or simply has not seen any business case for publishing
it. The vendor's official position may be that FreeBSD users should
not expect support, even if the underlying hardware would be
perfectly capable of working under a properly written FreeBSD driver.

Reverse engineering in this situation is delicate, because the legal
and ethical landscape is more complicated than it is for legacy
hardware. The vendor may hold copyright on the driver binary. The
firmware that runs on the device may itself be copyrighted. The
distribution and use of the driver may be governed by an end-user
license agreement that constrains certain kinds of analysis. We will
return to the legal questions at the end of this section. For now,
note simply that the situation exists and that it is one of the
recurring reasons why reverse engineering matters.

The technical challenges in this case are usually richer than in the
legacy case, because there is more material to work with. You have a
running driver that you can observe. You have a working device whose
behaviour you can capture. You may have a firmware image you can
analyse statically. The investigation can be very productive, but it
also requires more careful attention to the legal context of the
work.

### 1.3 Custom Embedded Systems With Little or No Documentation

A third situation, increasingly common in industrial and embedded
work, is the custom board with a custom chip. A small company
designs an instrument or a controller for a specific application.
They commission a custom integrated circuit, or they program a
standard microcontroller with proprietary firmware, or they assemble
a board from off-the-shelf parts in a configuration that has never
been used elsewhere. They support the device only on their own
operating environment, often a customised Linux distribution or a
small real-time operating system.

When that company asks a contractor to integrate the device into a
larger system that runs FreeBSD, or when an end user buys the
hardware and wants to use it from FreeBSD, the only available
information may be a one-page mechanical drawing, a short README
file, and the binary firmware. No register map, no command set, no
description of how the device starts up.

This case is in some ways the hardest, because the device is
specific to one company and one customer. There is no community,
because nobody else owns one. There is no prior driver to read,
because nobody else has written one. The investigator is genuinely
alone with the hardware, the captured traffic, and whatever can be
deduced from observing the existing firmware. We will see in the
labs how to approach this kind of investigation systematically, and
we will see in Section 9 how to keep your own findings well enough
documented that someone else can build on them later.

### 1.4 The Common Thread

Every one of these situations shares a single underlying property:
the device exists and works, but the description that would let a
new driver be written from a specification is missing. The mechanical
craft of writing the driver is the same craft we have practised in
the rest of the book. What is different is the shape of the work
that comes before the writing. We have to discover what we would
ordinarily look up. That is the part this chapter teaches.

It is worth noticing what reverse engineering is not. It is not
guessing. It is not poking randomly at registers and hoping
something interesting happens. It is not trying to bypass copy
protection or break encryption or do anything else that would shade
into a different ethical universe. It is the patient, structured,
written-down process of inferring how a piece of hardware works by
observing what it does and what it produces, and then writing
software that interacts with it correctly.

A reverse-engineering project, done well, looks far more like a
laboratory science than like a hacker stereotype. There is a
hypothesis, an experiment, a measurement, and a conclusion, repeated
hundreds of times until enough conclusions have accumulated to write
a driver that works. The romance of the scene where someone types
furiously and a screen of text reveals "the secret" exists only in
films. The reality is closer to a long, slow, methodical
construction of understanding, one register and one bit at a time.

### 1.5 Legal and Ethical Considerations

Before we touch any tools, we have to talk about the legal landscape
that surrounds this work. The picture is not complicated, but it is
real, and a beginner who ignores it can stumble into trouble that
nothing in the technique sections of this chapter will help with.

Reverse engineering for the purpose of interoperability, which is
the purpose this chapter cares about, is broadly accepted in both
United States and European Union law. The point of interoperability
work is to allow a device, format, or interface to be used with a
piece of software that the original vendor did not provide. Writing
a FreeBSD driver for a USB Wi-Fi adapter that ships with a Windows
driver is a textbook interoperability case. The driver lets FreeBSD
talk to a piece of hardware that the user already owns. It does not
copy the vendor's driver. It does not redistribute the vendor's
firmware in a form that violates a license. It does not bypass any
security measure that protects copyrighted content. It produces an
independent piece of software that performs the same function the
vendor's driver performs, written from a clean understanding of the
underlying interface.

In the United States, the relevant legal framework is the doctrine
of fair use under copyright law, with a long line of court cases
recognising reverse engineering for interoperability as a legitimate
fair use. The Sega versus Accolade case from 1992, the Sony versus
Connectix case from 2000, and several similar precedents have
established that disassembling code to understand its interface is
fair use, provided the purpose is legitimate interoperability and
the resulting product does not contain the original copyrighted
code. The relevant statute that occasionally complicates matters is
the Digital Millennium Copyright Act, which forbids circumventing
"technological measures" that protect copyrighted works. The DMCA
includes specific exemptions for interoperability research, but
those exemptions are narrower than the underlying fair use right.
For ordinary driver work the DMCA is rarely a problem, but if you
ever need to defeat encryption to read firmware, the legal picture
becomes more complex and a real lawyer becomes a worthwhile
consultation.

In the European Union, the relevant framework is the Software
Directive, originally Directive 91/250 and updated in 2009 as
Directive 2009/24. Article 6 of the Software Directive explicitly
permits decompilation for the purpose of achieving interoperability
with an independently created program, provided several conditions
are met: the person doing the decompilation has a right to use the
program, the information needed for interoperability has not been
readily available, and the decompilation is limited to the parts of
the program necessary for interoperability. This is one of the most
explicit legal endorsements of interoperability reverse engineering
in any major jurisdiction.

Outside those two systems, the picture varies. Many countries follow
similar principles in practice. A few do not. If you are working in
a jurisdiction where the law is unclear, or where enforcement is
unpredictable, talk to a lawyer who actually knows your local
software-copyright law. The cost of a single hour of legal advice is
small compared to the cost of finding out the hard way.

There is a clear ethical line between interoperability work and work
that would harm the vendor or the user. Interoperability work
produces a new program that lets a piece of hardware do its
intended job under a new operating system. It does not redistribute
copyrighted code. It does not strip license restrictions from a
purchased product. It does not bypass security measures that protect
something other than the vendor's commercial interest in the
interface itself. If you find yourself wanting to do any of those
things, you are no longer doing interoperability work, and the rest
of the chapter does not apply to your situation.

A second ethical line is between observation and tampering. Watching
how a device behaves is observation. Capturing the traffic between
the device and the vendor's driver is observation. Reading the
firmware that the vendor distributed in a form intended to be
distributable is observation. Writing your own driver based on what
you observed is the legitimate end product. Writing modified
firmware that replaces the vendor's firmware on the device, or
distributing such modified firmware, is a different category of
work that brings in different legal and ethical considerations. We
will not cover that activity in this chapter. The chapter is about
writing a clean, original driver that talks to the original hardware
in its original configuration.

A third ethical consideration is honesty about your work. Document
where your information came from. If a particular register
definition came from reading the vendor's open-source driver, say
so. If a packet format came from a published specification, cite
it. If a behaviour was deduced from your own captures, describe the
captures. This honesty is partly a legal matter, because it lets you
prove that you did clean-room work, and partly a community matter,
because it lets others build on your work without having to redo
the parts you have already done.

### 1.6 Wrapping Up Section 1

This section has set the stage for the rest of the chapter. We have
seen the three most common situations where reverse engineering is
necessary: legacy hardware, devices with vendor-only support on
other operating systems, and custom embedded systems with no
documentation. We have seen that the underlying property is the
same in every case: a working device whose programming interface is
not described. And we have seen the legal framework that surrounds
this work in the United States and the European Union, with a clear
distinction between legitimate interoperability work and other,
more legally fraught activities that this chapter will not pursue.

The next section turns from why to how. We will set up the small
lab where the reverse-engineering work will happen, and we will
introduce the tools you will need. The lab is the foundation for
everything that follows, and a few hours spent setting it up
properly will save many hours of confusion later.

## 2. Preparing for the Reverse Engineering Process

A reverse-engineering session is, at its core, an experimental
activity. You will run experiments on a piece of hardware, capture
the results, analyse them, and design follow-up experiments. Like
any experimental activity, it benefits from a properly equipped
laboratory. The lab does not need to be expensive. Most of what we
need is software that is already in the FreeBSD base system, and
the rest can be assembled from a short list of free or low-cost
tools. The investment is mostly in setting up the equipment
correctly so that your captures are reliable and your experiments
are repeatable.

This section walks through the complete kit. It starts with the
mental model of what the lab is doing, then enumerates the
software tools, then discusses optional hardware tools, then
describes how to bring up a vendor driver under another operating
system on the same machine so that you can observe its behaviour,
and finally suggests a workflow for keeping the lab organised.

### 2.1 The Mental Model: What the Lab Is For

Before listing tools, it helps to picture what the finished lab
will look like. The lab is the place where you will:

1. **Identify** the device, recording every public fact about it.
2. **Observe** the device in known operational states.
3. **Capture** the traffic between the device and an existing driver.
4. **Experiment** with reads and writes to discover register
   behaviour.
5. **Document** every observation as it is made.
6. **Validate** every hypothesis by experiment, ideally with a
   simulator before risking the real device.

The lab is therefore a small system designed to support the
observe-hypothesize-test-document loop that drives the entire
craft. Every tool you add to it should serve that loop in some
identifiable way. Tools that look impressive but do not feed into
the loop are clutter, and clutter slows you down.

A first-time reader sometimes assumes that reverse engineering
requires very expensive professional equipment. It does not. A
modest FreeBSD development machine, a target system that can run
the vendor's driver, and a small budget for cables and adapters
will get you most of the way through most projects. The expensive
tools are nice to have, and we will mention them, but the core
work is done with software you already have access to.

### 2.2 The Software Tool Kit From the FreeBSD Base System

FreeBSD's base system includes a remarkable collection of tools
that together cover most of what a driver author needs for the
software side of the lab. Let us go through them in the order in
which a reverse-engineering session typically uses them.

**`pciconf(8)`** is the starting point for any PCI or PCI Express
device. It is an interface to the `pci(4)` ioctl that the kernel
exposes through `/dev/pci`. Run as root, it lists every PCI device
the kernel has enumerated, including devices for which no driver
attached. The most important invocations for reverse engineering
are:

```sh
$ sudo pciconf -lv
```

This produces a one-line summary of every PCI device on the system,
followed by the human-readable vendor and device strings if a
known database recognises them. Devices without a driver appear
as `noneN@pci0:...`. For each unknown device, this line tells you
the PCI bus address, the vendor identifier, the device identifier,
the subsystem identifiers, the class code, and the revision. Those
identifiers are the first piece of public information you will
record about the device.

```sh
$ sudo pciconf -lvc
```

The `-c` flag adds a list of the device's PCI capabilities, such
as MSI, MSI-X, power management, vendor-specific capabilities,
and PCI Express link-training data. Repeated `-c -c` increases the
verbosity for some capability types. For PCI Express devices, this
is also where you will see link-state information that tells you
whether the device negotiated the link width and speed it should
have negotiated. A surprising number of "this device does not
work" problems turn out to be link-training problems that this one
command would have revealed.

```sh
$ sudo pciconf -r device addr:addr2
```

The `-r` form reads PCI configuration register values directly,
returning the raw bytes at an offset in configuration space. This
is the safest way to inspect a device, because configuration space
is designed to be read without side effects. The complementary
`-w` form writes configuration registers; use it with extreme care,
because some configuration registers do change device behaviour
permanently.

**`devinfo(8)`** prints the FreeBSD device tree as the kernel sees
it. Where `pciconf` shows you the bus level, `devinfo` shows you
the full hierarchy: which bus the device hangs off, which parent
controller, what resources it has been assigned, and what name the
kernel has given it. The verbose form `devinfo -rv` is particularly
useful early on, because it shows the exact ranges of memory and
I/O ports allocated to the device, and those ranges are the
playground in which all the bus-space experiments will happen.

**`devctl(8)`** is the device-control utility that lets you detach
a driver from a device, attach a different driver, list events,
and disable specific devices at the kernel level. During reverse
engineering, the most useful invocations are `devctl detach
deviceN` to remove the in-tree driver from a device and
`devctl attach deviceN` to put it back. Detaching a driver is
sometimes necessary so that your experimental driver can claim the
device, and being able to put the in-tree driver back without
rebooting saves a lot of time.

**`usbconfig(8)`** is the USB equivalent of `pciconf`. It enumerates
USB devices, dumps their descriptors, and changes their state. Its
most important invocations for reverse engineering are:

```sh
$ sudo usbconfig
$ sudo usbconfig -d ugen0.3 dump_device_desc
$ sudo usbconfig -d ugen0.3 dump_curr_config_desc
$ sudo usbconfig -d ugen0.3 dump_all_config_desc
```

The first form lists every USB device the system sees, with its
unit number and address. The `dump_device_desc` form prints the
USB device descriptor: bDeviceClass, bDeviceSubClass,
bDeviceProtocol, bMaxPacketSize0, idVendor, idProduct, bcdDevice,
the manufacturer and product strings, and the number of
configurations. The `dump_curr_config_desc` and
`dump_all_config_desc` forms walk the configuration descriptors
and print the interfaces, alternate settings, and endpoints
contained within them. Together, those three commands give you a
nearly complete static picture of what the USB device claims to
be, and that picture is the starting point of every USB
investigation.

**`usbdump(8)`** is the FreeBSD equivalent of Linux's `usbmon`. It
captures USB packets by opening `/dev/bpf` and binding to a cloned
`usbusN` interface created by the `usbpf` packet-filter module, and
it writes the captured packets to a file in a format compatible with
libpcap. Captures can be saved with `-w file`, replayed with `-r
file`, and filtered with the standard BPF expression language. For
reverse engineering, the workflow is usually:

```sh
$ sudo usbdump -i usbus0 -w session1.pcap
```

This captures everything on the named USB bus to a file. After the
capture, the file can be read back with `usbdump -r session1.pcap`,
opened in Wireshark, or processed with custom scripts. The
captured file format records every USB transfer, including SETUP
packets, IN and OUT data, status responses, and timing
information. Multiple sessions captured during different operations
can be diffed against each other to isolate the packets responsible
for one specific behaviour, and that diffing is one of the most
effective techniques in the kit.

**`dtrace(1)`** is the dynamic tracing facility that we have used
in earlier chapters. For reverse engineering, DTrace is particularly
useful for tracing the points where the kernel interacts with a
driver: which `device_probe` is being called for which device,
which interrupt handlers fire when, which `bus_space` operations
the in-tree driver performs. A few well-chosen DTrace probes can
tell you in detail what the existing driver is doing, even when no
other documentation exists.

**`ktr(9)`** is the in-kernel trace facility used for fine-grained
event tracking. It is more intrusive than DTrace, because it
requires kernel options at build time, but it gives you a
high-resolution log of every traced event. For reverse engineering,
`ktr` is most useful when added to your own experimental driver so
that the timing of register accesses can be reconstructed exactly.

**`vmstat -i`** and **`procstat`**: smaller utilities that let you
watch the rate of interrupts a device is generating and the
processes that are interacting with the device. Both are part of
the base system. During an experiment, it can be useful to watch
the interrupt count change as you exercise the device, because a
sudden change in interrupt rate is itself a significant
observation.

**`hexdump(1)`**, **`xxd(1)`**, **`od(1)`**, **`bsdiff(1)`**, and
**`sdiff(1)`**: ordinary userland utilities for examining binary
data and comparing files. You will use them constantly. A capture
file viewed in `xxd` is often where the patterns first become
visible, because the eye can pick out repeating structures in a
hex dump that no automated tool would notice without being told
what to look for. `sdiff` of two `xxd` outputs is one of the
oldest and most reliable ways to find what is different between
two captures.

This list is not exhaustive, but it covers the tools you will reach
for in the first several weeks of any project. Every one of them is
in the FreeBSD base system, with manual pages under `man pciconf`,
`man usbconfig`, and so on.

Beyond the base system, a small family of third-party disassemblers
and decompilers becomes important when the only artefact you have is
a vendor binary, typically a Windows driver binary, a firmware image
extracted from a device, or an option ROM pulled from a PCI card.
**Ghidra**, the open-source reverse-engineering suite released by the
United States National Security Agency, is the tool most FreeBSD
developers reach for first because it is free, cross-platform, and
comfortably decompiles x86, ARM, and many embedded architectures into
readable C-like pseudocode. **radare2** and its graphical companion
**Cutter** are a lighter open-source alternative, well suited to
quick inspection of small firmware blobs. **IDA Pro** is the
long-established commercial product; its decompiler is still the
reference implementation in the industry, but its cost places it
outside the budget of most individual developers. You do not need any
of these tools to do excellent work on devices whose behaviour can be
reconstructed from captures alone. When you do need them, the goal is
always limited and documented: identify the names of registers,
identify the structure of command buffers, understand the order in
which the vendor's code programmes the hardware. You do not copy the
vendor's code. You write down what the binary implied about the
hardware's interface and then you discard the disassembly. This is
the clean-room discipline outlined in section 1.5 translated into
practice: the binary is a source of hardware facts, not a source of
code to be copied. Use the disassembler briefly, write down what you
learned, and build your driver from the notes.

### 2.3 Optional Hardware Tools

Hardware tools become valuable when the device's interaction with
the host is not visible to software-only capture. A USB device's
traffic passes through the host controller and can be captured by
`usbdump`. A PCI Express device's transactions go through the root
complex and are not captured by anything in the base system. An
SPI peripheral on an embedded board may communicate with its host
processor on wires that no operating-system tool can see. For those
cases, hardware tools enter the picture.

A **logic analyser** is the most generally useful hardware tool. It
attaches to wires and records the voltage on each wire over time,
producing a digital trace that can be decoded into the protocol
that the wires were carrying. For SPI, I2C, UART, and similar
low-speed busses, a basic eight or sixteen channel logic analyser
is sufficient. The Saleae Logic family is widely used in
professional work and is well supported by `sigrok`, the open-source
suite for analysing logic-analyser captures. Sigrok's `pulseview`
GUI lets you import a capture, decode it as SPI or I2C, and step
through the bus traffic byte by byte.

A **USB protocol analyser** is a specialised piece of hardware that
sits on the USB bus and captures every packet, including bus-state
events that are not visible from the host. The Beagle and Total
Phase analysers are the high-end tools in this category. They are
expensive, but for serious USB reverse engineering they reveal
behaviour that software-side capture simply cannot see. Most
hobby-level work, however, does fine with `usbdump` and a careful
methodology.

A **PCI Express protocol analyser** is even more specialised, and
practically nothing in the open-source world covers this niche. For
PCI Express work, the usual fall-back is to capture the device's
behaviour from the kernel side using DTrace, `ktr(9)`, and your own
instrumented driver, and to use `pciconf -lvc` to inspect the
configuration-space state. Real PCIe analysers from companies like
Teledyne and Keysight exist, but their cost places them outside the
reach of most individual developers.

For embedded work, an **oscilloscope** is sometimes useful for
diagnosing electrical problems that confuse the higher-level tools.
A driver that times out for unknown reasons may be timing out
because the device's clock signal is degraded; an oscilloscope will
show this when no software tool will. A modest USB-connected
oscilloscope is a reasonable investment for anyone doing serious
embedded work.

You can do excellent reverse-engineering work with none of these
hardware tools. Most consumer USB and PCI devices are accessible
purely through software-side capture and the kernel's own
introspection. The hardware tools become important when the
investigation crosses into truly low-level territory: signal
integrity, bus timing, embedded protocols, devices designed for
industries that never expected to be opened up.

### 2.4 The Observation Rig

Once you have the tools, the next decision is the shape of the
observation rig. The rig is the combination of machines and
operating systems on which you will observe the existing driver
running. There are several common configurations, each with its
own advantages.

The simplest configuration is **one host, two operating systems**.
The same physical machine boots either FreeBSD, where you will be
writing the new driver, or another operating system whose driver
already supports the device, where you will be observing it. Boot
into the other OS to observe; boot into FreeBSD to develop. The
configuration is straightforward and works well when the device is
permanently attached to the host. The disadvantage is that you
cannot observe and experiment in the same session, so iteration is
slower.

A more flexible configuration is **two hosts**: one running the
operating system whose driver supports the device, the other
running FreeBSD as your development environment. The device can be
attached to one host, observed, and then moved to the other.
Captures, notes, and code travel between the two over the network.
This configuration works well when both machines fit on a desk and
when the device can be moved without harm.

For USB devices, a **single FreeBSD host running another OS in a
virtual machine** is often the most efficient configuration. The
device is attached to the FreeBSD host, where `usbdump` can capture
its traffic. The virtual machine is configured to receive the
device through USB passthrough, so that the vendor's driver inside
the VM sees the device. As the vendor's driver operates the device,
`usbdump` on the host records every packet. This setup gives you
both observation and rapid iteration in a single session, because
you do not have to reboot anything to switch between observing and
experimenting.

For PCI devices, the equivalent is **bhyve passthrough using the
`ppt(4)` driver**. The FreeBSD host detaches the device from the
in-tree driver, attaches it to `ppt(4)`, and exposes it to a bhyve
guest where the vendor's driver can run. The vendor's driver in the
guest operates the device, while the host can use DTrace and other
tools to observe what passes between them. Bhyve passthrough is a
valuable technique for PCI work and has the great advantage of
keeping all your observation tools in a single FreeBSD host.

For very specialised hardware, **dedicated capture hardware** is
the only option. A logic analyser permanently attached to a board's
SPI bus, or a USB protocol analyser inserted between a USB device
and its host, gives you observation that no software-side tool can
provide. The trade-off is that the rig is more complex to set up
and the captured data is in a format that requires its own tools to
analyse.

### 2.5 Bringing Up the Vendor Driver Under Another OS

If you are going to observe the vendor's driver in operation, you
need a working installation of an operating system that the vendor
supports. The choice depends on what the vendor ships. For Linux
drivers, a recent stable Linux distribution is usually the right
choice. For Windows drivers, the standard procedure is a Windows
installation in a virtual machine; this works well as long as the
device can be passed through to the guest. For specialised
embedded operating systems, the situation is more variable.

Whichever OS you choose, set it up with as little extra software as
possible. The lab should be quiet. Background activity from other
drivers, automatic updates, telemetry, or unrelated processes adds
noise to your captures. A lean install lets the device's traffic
stand out clearly.

For a USB device whose vendor supplies a Linux driver, the
recommended configuration is:

1. Install a recent stable Linux distribution in a virtual machine
   on your FreeBSD host.
2. Configure bhyve to pass through the USB device to the guest.
3. Inside the guest, install the vendor's driver and verify that
   the device works.
4. On the host, attach `usbdump` to the USB bus through which the
   device communicates.
5. Repeat the device's operations in the guest while capturing on
   the host.

For a USB device whose vendor supplies a Windows driver, the
configuration is similar, with Windows in the guest instead of
Linux. Windows USB passthrough through bhyve has been improving
steadily and is workable for most consumer devices.

For a PCI device, the analogous configuration uses bhyve PCI
passthrough through the `ppt(4)` driver. Detach the device from
its host driver with `devctl detach`, attach it to `ppt(4)` with
the appropriate kernel configuration, and expose it to the bhyve
guest with `bhyve -s slot,passthru,bus/slot/function`. The vendor's
driver in the guest then operates the device. Software-side
observation from the host is harder for PCI than for USB, because
configuration-space and memory-space accesses are not visible to
the host once the device has been passed through. The fall-back
is to instrument your own experimental driver heavily and to learn
from the differences between what your driver does and what the
vendor's driver appears to be doing.

### 2.6 The Lab Notebook

Equally important to the tools is the discipline of recording what
you do. A lab notebook, paper or digital, is not optional in
reverse engineering. Without one, you will lose track of what you
have tested, what you have observed, and what you have concluded.
With one, you build up an artifact that, by itself, is part of the
output of the project.

The notebook should record:

- The date and time of every observation session.
- The exact configuration of the lab when the observation was
  taken: kernel version, tool versions, the state of the device
  before the observation began.
- The exact commands you ran.
- The captured data, or a clear pointer to where the captured data
  is stored.
- Your immediate interpretation of what you observed, with the
  word "observation" or "hypothesis" clearly attached.
- Any decision you made about what to test next, and why.

A good notebook entry reads like a scientific protocol. It should
be reproducible by someone else who has access to the same lab,
and it should tell a future reader what you knew at the time and
how you knew it. When the project is finished and you write the
pseudo-datasheet that summarises everything you learned, the
notebook is where the citations come from. When something later
turns out to be wrong, the notebook is where you go to figure out
when the wrong belief entered the picture and what other
conclusions might be infected by it.

The discipline of writing down hypotheses before testing them is
particularly important. Without that discipline, it is very easy
to convince yourself, after the fact, that you predicted a result
you did not actually predict. With it, you can tell precisely
which experiments confirmed your understanding and which ones
surprised you. The surprises are the most valuable observations,
because they are the places where your model is wrong, but they
only show up clearly when the prediction was written down before
the result was known.

### 2.7 A Sample Lab Layout

By way of concrete example, here is a configuration that has worked
well for many reverse-engineering projects on FreeBSD.

The host is a small FreeBSD 14.3 desktop with at least 16 GB of
memory. It runs the development version of the driver, the
observation tools, and the bhyve hypervisor. Its `/usr/src/` is
populated with the FreeBSD source so that source-level browsing is
fast.

A virtual machine inside bhyve runs a recent Linux distribution.
The VM has the vendor's driver installed and is configured for USB
or PCI passthrough as appropriate.

A separate Git repository, on the host, holds the project
notebook, the captured pcap files, the experimental driver code,
and the pseudo-datasheet as it grows. Each commit is dated and
described, so that the history of the project's understanding is
preserved.

A second terminal on the host always has `tail -F /var/log/messages`
running, so that any kernel message produced by the experimental
driver is immediately visible.

This layout is not the only one that works, but it is a reasonable
starting point. The key features are: a clean FreeBSD development
environment, a way to run the vendor's driver, a way to observe
the device, and a Git-tracked notebook that grows with the
project.

### 2.8 Wrapping Up Section 2

We now have a tool kit and a lab. The base system gives us
`pciconf`, `usbconfig`, `usbdump`, `devinfo`, `devctl`, `dtrace`,
and `ktr`. Optional hardware tools give us deeper visibility when
software-side capture is not enough. Bhyve and `ppt(4)` give us a
way to run the vendor's driver inside a virtual machine while
observing from the FreeBSD host. And a written lab notebook gives
us the discipline that turns ad-hoc experimentation into
repeatable engineering.

The next section puts the lab to work. We will see how to capture
a device's behaviour systematically, how to recognise the patterns
that most hardware uses to communicate with its driver, and how to
turn raw captures into the first signs of an emerging mental
model. We will be doing experimental work in earnest, and the
discipline that this section established becomes essential as we
start producing the data that everything else will be built on.

## 3. Observing Device Behavior in a Controlled Environment

With the lab in place, we now turn to observation. This is the
phase where you collect the raw data that everything else will be
built on. The goal is not to understand the device yet. The goal
is to capture, with as much fidelity as possible, what the device
does in a small set of well-defined situations. Understanding will
emerge later, from analysis. The first job is to get clean, labelled
captures.

This section walks through the standard observation techniques in
the order in which a project usually applies them. We start with
static descriptors, which give you a snapshot of the device's
identity. We move to initialisation captures, which show you what
the device does when it is first powered up or attached. We then
look at functional captures, which show what the device does when
it performs each of its useful operations. Throughout, we emphasise
the discipline of structured capture: each capture is named, dated,
labelled with the operation it represents, and stored alongside a
short note describing what the user did during the capture and what
behaviour was expected.

### 3.1 Static Descriptors and Identity Information

The first thing to capture about any device is its identity. For
a PCI or PCI Express device, this means recording the output of:

```sh
$ sudo pciconf -lv
```

For the specific device, the relevant lines look like this in
practice. Suppose the device is the third unattached PCI device
the kernel sees. After running `pciconf -lv`, you might see:

```text
none2@pci0:0:18:0:    class=0x028000 card=0x12341234 chip=0xabcd5678 \
    rev=0x01 hdr=0x00
    vendor     = 'ExampleCorp'
    device     = 'XYZ Wireless Adapter'
    class      = network
    subclass   = network
```

This single line records six facts that any future analysis will
need: the bus location (`0:18:0`), the class code
(`0x028000`, which the FreeBSD class-code table identifies as a
wireless network controller), the subsystem identifier
(`0x12341234`), the chip identifier (`0xabcd5678`, with vendor
ID `0xabcd` and device ID `0x5678`), the revision (`0x01`), and
the header type (`0x00`, a standard endpoint). Every one of those
facts may matter later. The vendor and device IDs are how the
kernel will find your driver. The subsystem ID is sometimes the
only way to distinguish two devices that share a chip but use
different layouts. The class code tells you what category the
device belongs to. The revision distinguishes silicon revisions
that may behave differently. Record all of them.

Add the capability list:

```sh
$ sudo pciconf -lvc none2@pci0:0:18:0
```

This will append a list of PCI capabilities, each a single line
with a name, an ID, and a position in configuration space. The
typical list for a modern PCI Express device includes power
management, MSI or MSI-X, PCI Express, vendor-specific capabilities,
and one or more extended capabilities. The vendor-specific
capabilities are particularly interesting, because they are the
place where vendors hide non-standard functionality and they are
often the entry point for chip-specific configuration.

For a USB device, the equivalent capture is:

```sh
$ sudo usbconfig
$ sudo usbconfig -d ugen0.5 dump_device_desc
$ sudo usbconfig -d ugen0.5 dump_curr_config_desc
```

These three commands together produce the device descriptor, the
current configuration descriptor, and a list of all configurations.
A typical output for a simple USB device looks like:

```text
ugen0.5: <ExampleCorp Foo Device> at usbus0
  bLength = 0x0012
  bDescriptorType = 0x0001
  bcdUSB = 0x0210
  bDeviceClass = 0x00
  bDeviceSubClass = 0x00
  bDeviceProtocol = 0x00
  bMaxPacketSize0 = 0x0040
  idVendor = 0x1234
  idProduct = 0x5678
  bcdDevice = 0x0102
  iManufacturer = 0x0001  <ExampleCorp>
  iProduct = 0x0002  <Foo Device>
  iSerialNumber = 0x0003  <ABC123>
  bNumConfigurations = 0x0001
```

Each field is an answer to a question. The `bcdUSB` value tells
you the USB protocol version the device claims to implement.
`bDeviceClass`, `bDeviceSubClass`, and `bDeviceProtocol` are the
USB class system, which sometimes identifies the device as a
member of a standard class (HID, mass storage, audio, video, and
so on) and sometimes leaves all three at zero, meaning that the
class is determined per-interface inside the configuration
descriptor. `idVendor` and `idProduct` are the unique numerical
identifiers; combined, they are how a USB driver is bound to the
device. `iManufacturer`, `iProduct`, and `iSerialNumber` are
indices into the device's string table; `usbconfig` resolves them
for you and prints the strings.

Dump the configuration descriptor too:

```sh
$ sudo usbconfig -d ugen0.5 dump_curr_config_desc
```

This shows the interface descriptors and endpoint descriptors. For
each interface, you will see its number, its alternate setting,
its class, subclass, and protocol, and the list of endpoints. For
each endpoint, you will see its address (which encodes both the
endpoint number and the direction), its attributes (which encode
the transfer type: control, isochronous, bulk, or interrupt), its
maximum packet size, and its polling interval if it is an interrupt
endpoint.

This static information is the entire programming-visible identity
of the USB device. It tells you exactly what kinds of pipes the
device exposes, in which directions, of what type, and at what
size. From this alone you can already make some educated guesses.
A device that exposes a single bulk-IN and a single bulk-OUT
endpoint is probably a transport for some application-specific
protocol. A device that exposes two interrupt-IN endpoints is
probably an event source of some kind. A device with isochronous
endpoints is almost certainly handling time-sensitive data such as
audio or video.

Save these dumps in your notebook. They are the device's static
identity, and they will not change between captures. They are the
header of every report you will write about the device.

### 3.2 The First Capture: Initialization

Once you have the static identity, the next capture is the
initialisation sequence. This is the sequence of operations that
the vendor's driver performs between the moment the device is
attached and the moment the device is ready for use.

The initialisation sequence is one of the most informative things
you can capture, because it usually exercises every register the
device has. The driver writes initial values, sets configuration
options, allocates buffers, sets up interrupts, enables data flow,
and reports success. Almost every register the device exposes will
be touched at least once during this sequence, and many of the
registers will reveal their general purpose just by being touched
in a way that fits the standard initialisation pattern.

For a USB device, the initialisation capture is:

```sh
$ sudo usbdump -i usbus0 -w init.pcap
```

Start `usbdump`, then attach the device. After the device is fully
initialised, stop the capture. The resulting pcap file contains
every USB transfer that passed across the bus between the device
and its driver, beginning with the USB enumeration sequence (which
should match the static descriptor dumps), continuing with the
class-specific or vendor-specific control transfers that the
driver uses to configure the device, and ending when the driver
has put the device into a ready state.

Open the file in Wireshark to view it interactively, or process it
with `usbdump -r init.pcap` to print it textually. Wireshark has
particularly good USB dissectors that will decode many standard
class-specific transfers automatically. For dissection of
vendor-specific transfers, you will be reading the raw bytes
yourself.

For a PCI device, software-side capture of initialisation is
harder, because configuration-space and memory-space writes are
not visible from outside the device once you are not using
passthrough. The usual technique is to instrument your own
experimental driver, or to add tracing to a copy of the in-tree
driver if one exists. We will return to this subject in Section 4
when we talk about register maps. For now, the equivalent of a
"first capture" for a PCI device is the output of:

```sh
$ sudo devinfo -rv
```

restricted to your device. This shows you the resources that the
kernel allocated for the device: the memory ranges, the I/O port
ranges, the interrupt assignment. Those resources tell you the
extent of the playground you will be exploring. They do not tell
you what is happening inside the playground, but they are the
boundary conditions for everything that follows.

### 3.3 Functional Captures

Once you have the initialisation, the next captures are functional.
For each thing the device can do, you take a separate capture.
Each capture should isolate one operation as cleanly as possible.

For a network device, you might take separate captures for "send
one ping", "receive one packet of unsolicited traffic", "set the
MAC address", "change the channel". For a sensor, you might take
separate captures for "read once", "set the sample rate", "enable
continuous mode", "calibrate". For a printer, you might capture
"print one page of plain text", "print one image", "query status".

The discipline of isolating operations is not optional. If your
capture file contains a hundred different operations, sorting out
which packets correspond to which operation will be impossible. If
your capture file contains exactly one operation, the packets in
it are exactly the packets for that operation, and your job is
much easier.

Every capture should include:

1. The exact operation being captured, in plain English.
2. The exact user actions that triggered the operation.
3. The exact moment the user started the action, recorded as a
   timestamp in the file name or in a sidecar note.
4. The expected behaviour of the device.
5. The actual behaviour observed.

The sidecar note is essential. Six months from now, you will not
remember which capture corresponded to which operation, and the
file name alone will not always be enough.

A sample naming scheme that has worked well for many projects is:

```text
init-001-attach-cold.pcap
init-002-attach-hot.pcap
op-001-set-mac-address-aa-bb-cc-dd-ee-ff.pcap
op-002-send-icmp-echo-request.pcap
op-003-receive-broadcast-arp.pcap
err-001-attach-with-no-power.pcap
```

The `init-`, `op-`, and `err-` prefixes group captures by purpose.
The numbered suffix is unique. The English description in the file
name is enough that you can find a specific capture without
opening it. The sidecar note lives next to the file as a `.txt` or
`.md` file with the same base name.

### 3.4 The Diff: Isolating Bits of Meaning

The single most important analysis technique in reverse engineering
is the diff. Two captures of similar operations are likely to be
mostly identical, with a few differences that correspond to the
differences in what was done. Those differences are the parts that
matter, because they are the parts whose meaning is most likely to
be visible.

Suppose you have a capture of "set channel to 1" and a capture of
"set channel to 6". Visually, the two captures will look almost
identical. They will start with the same setup, perform the same
preliminary operations, and end with the same wrap-up. Somewhere
in the middle, however, there will be a small number of bytes that
differ between the two captures. Those bytes are almost certainly
related to the channel number. By comparing them carefully, you
can deduce: which transfer carries the channel value, where in
that transfer the value lives, what numerical encoding is used
(direct numerical value, an index into a table, a bitfield), and
whether the value is sent as little-endian or big-endian.

The diff technique works best when the two captures differ in
exactly one variable. If you compare "set channel to 1" against
"set channel to 6", and the channel-to-channel difference is the
only difference between the captures, the diff is clean. If you
compare "set channel to 1, mode A" against "set channel to 6,
mode B", you have two variables changing and the analysis is
harder. Make captures that vary one thing at a time.

For text-format captures, `sdiff` is the simplest tool. For binary
captures, `bsdiff` produces compact patches that can be inspected
to see exactly which bytes changed. For pcap files, the
combination of `tshark -r file.pcap -T fields -e ...` to extract
specific fields, followed by `diff`, gives you a programmable way
to compare specific aspects of the captures.

A more sophisticated technique is to capture multiple instances of
the same operation, and compare them. Differences between
captures of nominally identical operations reveal which bytes are
actually constant (the parts of the protocol) and which bytes
change between runs even though the operation does not (sequence
numbers, timestamps, random values). The constant bytes are the
ones whose meaning you want to deduce; the variable bytes are the
ones whose meaning you can ignore for the moment.

Save every capture. Storage is cheap and the diffing technique
becomes more useful the more captures you have. A project with
fifty captures of an operation can answer many more questions than
a project with one capture, even though both projects "captured
the operation".

### 3.5 Wireshark and the USB Dissector

For USB work specifically, Wireshark is an indispensable tool.
Wireshark dissects the USB packet stream into a structured tree
view that is much easier to read than raw bytes, and it has
display filters that let you isolate one device, one endpoint, or
one direction of traffic.

The most useful filters are:

- `usb.device_address == 5` to limit the view to one specific
  device on the bus.
- `usb.endpoint_address == 0x81` to limit the view to one specific
  endpoint and direction (here, IN endpoint 1).
- `usb.transfer_type == 2` to limit the view to bulk transfers
  (1 = isochronous, 2 = bulk, 3 = interrupt).
- `usb.bRequest == 0xa0` to limit the view to a specific control
  request, useful when reverse engineering vendor-specific control
  transfers.

Combinations of these filters let you isolate exactly the part of
the capture you care about. The "Statistics" menu in Wireshark also
provides useful aggregate views, such as a list of every endpoint
seen and the number of packets on each. For USB, the "Endpoints"
view in particular is often the first thing you check after
opening a new capture.

If you have a capture that Wireshark dissects into something
class-specific (for example, a USB Mass Storage capture decoded
into SCSI commands), the dissector has effectively done the
hardest work for you. If you have a capture that Wireshark
dissects only as raw bulk transfers, you will need to read the
bytes yourself.

### 3.6 Observation Patterns to Recognise

Even before you understand a specific device, certain patterns
recur in almost every device's protocol, and learning to recognise
them speeds up every project. Watch for these as you look at
captures.

**Repeated writes followed by a single read.** This is the classic
"write a command, then read the result" pattern. The repeated
writes are usually setting up a request: command code, parameters,
length. The read is fetching the response. Many devices use this
shape for any operation that returns data.

**Status flags that change before and after events.** A bit
somewhere in a status register that becomes set when the device
finishes work, or becomes clear when work begins, is one of the
most common ways for a device to communicate progress. Look for
bits that change reliably in time with operations the user
triggered.

**A sequence of writes to escalating addresses, all in
multiples of four.** This is often a register block being written
in sequence. The operation is reset followed by configuration:
clear all the registers, then set them to their new values, then
trigger the operation by writing to a "go" register at the end.

**Identical reads from the same address until a value changes.**
This is polling. The driver is waiting for the device to do
something, and is checking a status register repeatedly. The
address being polled is almost certainly a status register. The
value that ends the polling tells you which bit in that register
is the "ready" indicator.

**Bulk-OUT followed by bulk-IN of the same length.** A common
pattern for command-response on USB bulk endpoints is to send a
fixed-size command on the OUT endpoint and then read a fixed-size
response from the IN endpoint. The two endpoints work together as
a request-response channel.

**Periodic interrupt-IN packets with timestamps that increase
linearly.** This is a heartbeat or periodic-status pattern. The
device is reporting its state at a fixed rate, regardless of what
the host does. The packets often contain a small fixed structure
with status bits and counters.

**Long sequences of writes followed by no observable response.**
This is often firmware download. The device has a writable code
region, and the driver is loading new instructions into it. Such
captures are typically very large compared to other operations,
and they often start with a fixed-size header that identifies the
firmware image.

These patterns are not exhaustive, but they appear so often that
recognising them on first sight saves enormous time. The first
hour with a new capture file is almost always spent identifying
which of these patterns the capture exhibits.

### 3.7 Wrapping Up Section 3

We have built a body of captures. Each capture is named, dated,
labelled with the operation it represents, and accompanied by a
short note describing the user actions and the expected device
behaviour. We have used `pciconf` and `usbconfig` to record the
static identity of the device. We have used `usbdump` to capture
its initialisation and its functional operations. We have learned
that the diff between two captures of similar operations is the
best tool we have for extracting bit-level meaning. And
we have learned the recurring patterns that show up in almost
every device's protocol.

The next section starts the active phase of the work. Instead of
just observing the existing driver, we will start poking at the
device ourselves, in a controlled way, to discover the meaning of
its registers. The captures we made in this section are the data
that the next section's experiments will be measured against. With
captures in hand, we know what "normal behaviour" looks like, and
we can compare what happens when we issue our own writes against
what happens when the vendor's driver issues the equivalent
writes. That comparison is the heart of register-map construction.

## 4. Creating a Hardware Register Map by Experimentation

The register map is the document that, once finished, would have
made all the previous work unnecessary. It lists every address the
device exposes, says what is at that address, defines the meaning
of each bit, and describes any side effects that reads or writes
have. With a complete register map in hand, writing the driver is
a straightforward translation exercise. Without one, the driver
cannot be written at all. The register map is the artifact that
the rest of this chapter is, in many ways, designed to produce.

In the absence of documentation, the register map has to be built
by experiment. You will write to addresses, see what happens, read
addresses back, see what they return, change one bit at a time,
look for changes in behaviour, and slowly accumulate a body of
verified facts about each address. The work is patient and
incremental, and many of the techniques are simple enough to fit
in a paragraph, but the discipline of doing them safely and
recording the results carefully is what separates a successful
project from a session that destroys the device.

This section covers the techniques. Section 10 will return to the
safety side and spell out what you must not do. Read the two
together; the techniques in this section are only safe in the
hands of someone who has internalised the warnings in Section 10.

### 4.1 Mapping the Address Space

Before you can probe addresses, you have to know which addresses
exist. For a PCI device, the device's BARs (Base Address Registers)
declare the memory regions and I/O port ranges that the device
responds to. The kernel has already enumerated these and made them
available through `bus_alloc_resource_any(9)` calls in your
driver's `attach` routine. The simplest way to see them in
operation is to read them back from the kernel:

```sh
$ sudo devinfo -rv
```

Restricted to your device, this command lists the resources the
kernel has assigned. A typical PCI device might have output like:

```text
none2@pci0:0:18:0:
  pnpinfo vendor=0xabcd device=0x5678 ...
  Memory range:
    0xf7c00000-0xf7c0ffff (BAR 0, 64K, prefetch=no)
    0xf7800000-0xf7bfffff (BAR 2, 4M, prefetch=yes)
  Interrupt:
    irq 19
```

Two memory regions and one interrupt. The 64K region is most
likely a register block, because register blocks are usually
small. The 4M region is large enough to be a frame buffer, a
descriptor ring, or a memory-mapped data area, but unlikely to be
register space. These are educated guesses based on size, not yet
verified facts. Note them as hypotheses.

The same kind of size-based intuition works in many cases. A
register region is rarely larger than a megabyte. A data buffer
or queue is rarely smaller than a few kilobytes. A region that is
exactly 16 KB or 64 KB on power-of-two boundaries is suspicious in
the right way: it looks like register space. A region that is
several megabytes and prefetchable is much more likely to be a
data region.

For a USB device, the equivalent of address-space mapping is the
endpoint enumeration. Each endpoint is a "channel" that you can
read from or write to. The endpoint addresses, sizes, and types
were captured by `usbconfig dump_curr_config_desc` in the previous
section. From the endpoint list you already know how many channels
the device exposes, of which types, and in which directions. There
is no equivalent of memory-mapped register space on USB; everything
is done through the endpoints, including any "register" reads and
writes (which appear as control transfers, vendor-specific
requests, or data inside bulk transfers).

### 4.2 The Read-First Principle

The single most important rule for safe register exploration is to
read before you write. A read from an unknown register is, almost
always, harmless. The hardware returns whatever it considers to be
the value at that address, with at most a side effect of clearing
some specific status flags in some specific kinds of registers. A
write to an unknown register, on the other hand, can do anything:
trigger a reset, start an operation, change a configuration bit,
write to flash memory, or in the worst case put the device into a
state from which it cannot easily recover.

The principle is simple: assume nothing about a write until you
have evidence that the write is safe. The evidence can come from
the captures you made in Section 3 (a write that the vendor driver
performs is presumably one of the writes the device expects), from
analogous registers on similar devices, from a published header
file or a related driver, or from your own analysis of the
register's behaviour under reads.

Read the entire register region exhaustively before doing any
writing. Save the values. Read it again ten minutes later. Compare.
Registers whose values change between reads are interesting: they
are either status registers reflecting some live state, or
counters incrementing on their own, or registers connected to
external inputs. Registers whose values are stable are either
configuration registers (whose values are stable until something
writes them) or data registers that happen not to have changed
during the observation window.

The simplest tool for this kind of exploration is a small kernel
module that takes a memory range and dumps it, and the companion
code in `examples/part-07/ch36-reverse-engineering/lab03-register-map/`
contains exactly such a module. The module attaches as a child of
the bus you point it at, allocates the memory range as a resource,
and exposes a sysctl that, when read, dumps every word in the
range. Multiple reads at different times produce a picture of
which words are stable and which are changing.

### 4.3 The Probing Module's Skeleton

For concreteness, here is the shape of a safe probing module. The
full version is in the companion files; what follows is the
essential structure that you should recognise from earlier
chapters.

```c
struct regprobe_softc {
    device_t          sc_dev;
    struct mtx        sc_mtx;
    struct resource  *sc_mem;
    int               sc_rid;
    bus_size_t        sc_size;
};

static int
regprobe_probe(device_t dev)
{
    /* Match nothing automatically; only attach when explicitly
     * told to. The user adds the device by hand with devctl(8) so
     * that the wrong device cannot be probed by accident. */
    return (BUS_PROBE_NOWILDCARD);
}

static int
regprobe_attach(device_t dev)
{
    struct regprobe_softc *sc = device_get_softc(dev);

    sc->sc_dev = dev;
    sc->sc_rid = 0;
    mtx_init(&sc->sc_mtx, "regprobe", NULL, MTX_DEF);

    sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
        &sc->sc_rid, RF_ACTIVE);
    if (sc->sc_mem == NULL) {
        device_printf(dev, "could not allocate memory resource\n");
        return (ENXIO);
    }
    sc->sc_size = rman_get_size(sc->sc_mem);
    device_printf(dev, "mapped %ju bytes\n",
        (uintmax_t)sc->sc_size);
    return (0);
}
```

The `BUS_PROBE_NOWILDCARD` return from `probe` is the protective
gesture: this driver will not attach to any device unless the
operator points it at one explicitly. This prevents the dangerous
case where a probe module accidentally attaches to a device it was
not meant to investigate.

The reading function, called from a sysctl handler, looks like
this:

```c
static int
regprobe_dump_sysctl(SYSCTL_HANDLER_ARGS)
{
    struct regprobe_softc *sc = arg1;
    char buf[16 * 1024];
    char *p = buf;
    bus_size_t off;
    uint32_t val;
    int error;

    if (sc->sc_size > sizeof(buf) - 64)
        return (E2BIG);

    mtx_lock(&sc->sc_mtx);
    for (off = 0; off < sc->sc_size; off += 4) {
        val = bus_read_4(sc->sc_mem, off);
        p += snprintf(p, sizeof(buf) - (p - buf),
            "%04jx: 0x%08x\n", (uintmax_t)off, val);
    }
    mtx_unlock(&sc->sc_mtx);

    error = sysctl_handle_string(oidp, buf, p - buf, req);
    return (error);
}
```

`bus_read_4` is a wrapper around `bus_space_read_4` that uses the
resource as both tag and handle, and it is the simplest way to read
four bytes from a memory-mapped region. Notice that we read in
units of four bytes, on four-byte aligned offsets. Most modern
devices expect register accesses to be aligned and of natural size.
Unaligned accesses, or accesses of the wrong size, can produce
garbage values or, on some hardware, cause bus errors that
manifest as kernel panics. When the device might use 16-bit or
8-bit registers, use `bus_read_2` or `bus_read_1` respectively;
when in doubt, start with 4-byte reads, because that is the most
commonly correct choice for modern peripherals.

This module does no writes at all. It is purely an observation
tool. Loading it on an unknown device gives you a snapshot of the
device's memory region without changing anything. Multiple loads,
or multiple invocations of the dump sysctl, give you a sequence of
snapshots whose differences reveal which addresses are dynamic.

### 4.4 Inferring Register Purpose From Behavior

After you have read the address space repeatedly, you start to
notice patterns. Each pattern suggests a purpose for the register.

**Stable values** are usually configuration registers, identifier
registers, or default values that have not yet been changed. A
register at offset 0 that always reads the same constant value is
often a chip-identifier register, sometimes called a "version
register" or "magic number register". Many devices use such a
register specifically so that drivers can confirm the chip is what
they expect.

**Slowly changing values** are usually counters: bytes received,
packets sent, errors detected, time elapsed. The slowly increasing
register is a hallmark of a counter, and the difference between
two readings tells you the count rate. If a counter increments by
exactly one every second, you have probably found a heartbeat or
a timestamp register. If it increments by a few thousand per
second when traffic flows and stays flat when traffic does not,
you have found a packet or byte counter for the live data path.

**Quickly changing values that wrap around** are usually
production-side ring-buffer write pointers. A device that produces
data into a ring buffer typically exposes a register holding the
current write position. The write pointer increments rapidly while
data is flowing, and wraps when it reaches the buffer's size. The
matching read pointer, written by the driver, tells the device
where the consumer is.

**Bits that toggle in time with operations** are status bits. A
bit that becomes 1 when an operation begins and 0 when it
completes is a "busy" bit. A bit that becomes 1 when an event
occurs and stays 1 until cleared is a "pending interrupt" bit. A
bit that becomes 0 when the device is healthy and 1 when an error
has occurred is an error indicator.

**Registers that read back differently from what was written** are
either masked (some bits are not implemented and read as zero
regardless of writes), one-shot (writes have side effects but the
register itself does not store the written value), or
self-clearing (writes set bits that the device clears
automatically when the operation completes). All three variants
are common, and the readback pattern is what tells them apart.

These observations build up a picture of the register map. Each
verified observation goes into the notebook, with the address, the
observed pattern, and the proposed purpose. Over time, the
notebook entries solidify into the pseudo-datasheet that Section
11 will discuss.

### 4.5 Comparing Captured Writes Against Hypotheses

The captures from Section 3 are the bridge between observation and
hypothesis. Once you have proposed that a particular register is,
for instance, the channel-selection register, you can confirm or
refute the hypothesis by comparing what you have seen the vendor
driver write to that register against what you know about the
operations the user performed.

Suppose your hypothesis is that the register at offset `0x40` is
the channel-selection register, and that the value at that
register encodes the channel number directly as a small integer.
You have a capture in which the user selected channel 6, and a
capture in which the user selected channel 11. If your hypothesis
is correct, the capture-6 sequence should contain a write of
`0x06` to the equivalent of `0x40`, and the capture-11 sequence
should contain a write of `0x0b` to the same place. If you find
those writes, the hypothesis is supported. If the captures show
writes of different values, or no writes at all to that offset,
the hypothesis is wrong.

This is the falsification step that makes the work scientific. A
hypothesis that survives one falsification attempt is worth more
than ten hypotheses that have only been informally proposed.

For a USB device, the same kind of comparison happens between the
captured control transfers, bulk packets, or interrupt packets and
the hypothesised meaning. If you believe a vendor-specific control
transfer with `bRequest = 0xa0` and `wValue` in the low byte is
the "set channel" command, you can verify it by examining the
captures of channel-1, channel-6, and channel-11 selections and
confirming that exactly that transfer appears in each, with the
expected `wValue`.

### 4.6 The Discipline of One Hypothesis at a Time

One discipline that is easy to forget under pressure is the
discipline of testing one hypothesis at a time. When several
hypotheses are in the air, the temptation is to design an
experiment that will test all of them at once. The experiment
runs, the device produces some output, and the output is
consistent with one of the hypotheses but not with another. Now
you have learned something about hypothesis A. But because you
also changed the conditions for hypothesis B, you have also lost
the ability to interpret what happened from B's perspective. The
same experiment will need to be run again, with B varied
independently.

The right discipline is: vary one thing, observe the result, draw
the conclusion that follows for that one variable, then proceed to
the next experiment. It is slower per experiment but faster
overall, because every experiment leaves behind a clear, isolated
fact rather than a tangle of partial information.

Newton's laboratory notebooks are full of single-variable
experiments. So are the notebooks of every successful
reverse-engineering project. Multivariable experiments belong to a
later phase, when you understand each variable well enough that
you can predict their interactions; in the discovery phase, they
mostly produce confusion.

### 4.7 Common Register-Layout Patterns

Across many devices, a few register-layout patterns recur often
enough to be worth knowing in advance. Recognising them can save
weeks of work.

**Status / Control / Data**: a small group of registers in which
one register holds the current state of an operation (Status), one
register accepts commands or configuration changes (Control), and
one or more registers hold the data being moved into or out of the
device (Data). Many simple peripherals use exactly this shape, and
many more use it as the shape of one of several functional units
within the device.

**Interrupt Enable / Interrupt Status / Interrupt Acknowledge**:
three registers that together implement the interrupt mechanism.
The Enable register controls which conditions cause an interrupt.
The Status register reports which conditions are currently raising
an interrupt. The Acknowledge register clears interrupt conditions
once they have been handled. The pattern is so common that, when
exploring an unknown device, you should look for it explicitly:
three adjacent registers near the top of the register map, with
identifying names like `INT_ENABLE`, `INT_STATUS`, `INT_CLEAR` if
you can find any documentation.

**Producer Pointer / Consumer Pointer**: two registers that
together implement a ring buffer. The Producer Pointer is updated
by whoever is producing data (sometimes the device, sometimes the
driver) and indicates the next free slot. The Consumer Pointer is
updated by whoever is consuming the data and indicates the next
slot to be processed. The difference between them tells you how
many slots are currently full. This pattern is the basis of nearly
every high-speed I/O interface in modern hardware, including most
network controllers, most USB controllers, and most disk
controllers.

**Window / Index / Data**: an indirect-access pattern where the
device exposes a small number of registers but uses them to access
a much larger internal address space. A Window register selects
which "page" is currently visible, an Index register selects which
register within the page, and a Data register reads or writes the
selected register. The pattern is common in older devices and in
devices with very large internal register spaces.

**Capability Pointer**: a register or small group of registers
that points to the start of a chain of capability descriptors.
PCI Express defines a standard form of this pattern; many other
devices use ad-hoc variants. The capability list lets the driver
discover what optional features the chip supports without having
to consult external documentation.

When you are exploring an unknown device, scanning the register
space for any of these recurring patterns is one of the first
things to do. A device that uses a Status / Control / Data layout
is much easier to drive than one that does not, and recognising
the layout early saves substantial time.

### 4.8 What Not to Do Yet

Before moving on, a quick reminder of restraint. The temptation,
when you have built a mostly-working register map, is to try
writing things and see what happens. Resist this temptation in
register exploration. Write only when you have a specific
hypothesis to test, when the hypothesis predicts a specific
outcome, when you have a way to verify that the outcome occurred,
and when you have considered what the worst plausible outcome of
an unintended interpretation would be. The discipline that Section
10 will spell out is built on top of this restraint, and the
register-mapping phase is exactly where it must be applied first.

### 4.9 Wrapping Up Section 4

We have learned how to map the address space of a device, how to
read it safely, how to interpret the patterns of stable, slowly
changing, and quickly changing values, and how to compare
captured writes against hypotheses to confirm or refute proposed
register meanings. We have learned the recurring layouts that
hardware likes to use. And we have set the stage for Section 10's
treatment of safety, which is the precondition for all of this
work being done responsibly.

The next section moves up one level of abstraction. Once we have
identified individual registers, we look at how they group into
larger structures: data buffers, command queues, descriptor
rings. These higher-level structures are how devices actually move
data, and identifying them is the next step in turning register
knowledge into the basis of a working driver.

## 5. Identifying Data Buffers and Command Interfaces

Registers are the way you talk to a device's control surface. Data
buffers are how the device moves real information. The register
map you built in the previous section is necessary but not
sufficient. To complete the picture you also need to understand
how the device handles data: where its buffers live, how their
boundaries are defined, what shape the data takes inside them, and
how the producer and consumer coordinate.

This section walks through the common buffer and command
structures that hardware uses, with FreeBSD-specific notes on how
to identify them through observation and how to set up the
matching `bus_dma(9)` mappings on the driver side.

### 5.1 The Big Three: Linear, Ring, and Descriptor

Almost every data buffer in modern hardware falls into one of
three shapes.

A **linear buffer** is a simple contiguous block of memory that
the driver hands to the device for one operation. The driver fills
it with data, the device consumes it, the driver hands it back
when needed. Linear buffers are the simplest shape, and they show
up in command-response patterns where each operation has its own
dedicated buffer. They are easy to identify because they appear in
the captures as a single block of bytes whose size matches the
expected operation size.

A **ring buffer** is a circular buffer with a producer pointer and
a consumer pointer. Producer writes go into the slot indicated by
the producer pointer, then the producer pointer advances. Consumer
reads come from the slot indicated by the consumer pointer, then
the consumer pointer advances. Both pointers wrap when they
reach the end of the buffer. The buffer is full when the producer
catches up to the consumer; it is empty when the two pointers are
equal in the other sense. Ring buffers are everywhere in
high-speed networking and storage, because they let producer and
consumer run at different rates without coordinated handshakes.

A **descriptor ring** is a special kind of ring buffer in which
each slot does not hold the data itself but a small fixed-size
descriptor that points to the data. The descriptor typically
contains a memory address (where the data lives), a length, a
status field (filled in by the device after processing), and some
control flags. Descriptor rings let the device do scatter-gather
DMA, accept variable-size buffers, and report per-buffer status
back to the driver. They are slightly more complex than ring
buffers but vastly more flexible, and they are the dominant
pattern in high-performance network and storage controllers.

Recognising which shape a particular device uses is the first job.
The signs are usually quite distinctive. If captures show
fixed-size blocks moved one at a time with no metadata, it is
probably a linear-buffer arrangement. If captures show data
flowing continuously with no obvious "next operation" boundary,
and there is a register that looks like a producer pointer, it is
probably a ring buffer. If the device documentation, prior driver
sources, or related-device datasheets hint at "descriptors", it is
almost certainly a descriptor ring.

### 5.2 Telling Buffers From Each Other

When you have identified that a buffer exists, the next question
is: how big is it, and how is it organised internally? The
captures from Section 3 contain the data; the question is what
shape that data has.

Several observations can help.

**Size of the underlying memory region.** If a PCI BAR is exactly
4 KB and a sysctl reveals that the device has a "ring size"
register set to 64, the natural conclusion is that the ring has
64 entries of 64 bytes each. Many devices use power-of-two sizes
for both the ring and the entry size, and the product of the two
should equal the BAR size.

**Periodic structure in the data.** A capture viewed in `xxd` or
Wireshark sometimes shows obvious periodic structure: every 32
bytes there is a small fixed pattern, every 64 bytes there is a
counter, every 16 bytes there is a status byte that varies in a
predictable way. Periodic structure of size N strongly suggests
that the buffer is organised as a sequence of records of size N.

**Alignment of pointer values.** If you see the device or the
driver using "addresses" whose low bits are always zero, the
addresses are aligned to a power-of-two boundary. If the alignment
is 16 bytes, the entries are probably 16 bytes or larger. If the
alignment is a megabyte, the entries are very large or the device
imposes a coarse alignment requirement for some other reason.

**Headers and trailers.** Many record formats include a header
that identifies the record type, a payload, and a trailer
containing a checksum or a length. The header is often a magic
value that can be recognised on sight.

**Cross-platform clues.** If the device has a Linux driver in the
open-source tree, that driver's code may already define the
record structure. Reading another driver to learn record formats
is one of the most efficient research techniques available, even
when you intend to write the FreeBSD driver from scratch in a
clean-room style. Two cautions are worth stating inline so that
the rhythm of the chapter does not let them slip past. The first
is legal: Linux drivers are almost always GPL-licensed, so
reading them is fine, but pasting or near-transcribing their
expression into a BSD-licensed FreeBSD driver is not. The second
is technical: record-layout information ports cleanly between
kernels because it describes the device, not the host, but
surrounding code does not. Linux's buffer-allocation paths, its
locking primitives, and its descriptor-ring helpers are shaped by
its own kernel, and a FreeBSD driver that tries to mirror them
line-for-line will fight the host kernel at every step. Read to
understand the format, then write FreeBSD code from your own
notes. Section 12 covers the legal framing in detail; Section 13
shows how real drivers have done this well.

### 5.3 Command Queues and Their Sequencing

For devices with command interfaces, the typical shape is a queue
of command structures, each of which the device processes in
order, with status reported back through a separate register or
through a status field in the command structure itself. Command
queues are usually a special form of descriptor ring.

The sequencing matters. Some devices process commands strictly in
order; others process them out of order and identify completion
by a tag in the command structure. Some devices process commands
one at a time; others process many in parallel. Some devices
require the driver to wait for a command to complete before
posting the next one; others accept many concurrent commands.
Identifying which mode the device is in is part of the
investigation, and the captures from Section 3 are usually rich
enough to make the answer visible.

A useful technique is to deliberately exercise the device with
operations that will overlap in time, and to observe how the
driver responds. If the driver always waits for a command to
complete before posting the next one, the device probably uses
sequential processing. If the driver posts multiple commands in
rapid succession and then waits for several completions, the
device probably uses concurrent processing. If completions arrive
in a different order from postings, the device is doing
out-of-order processing.

### 5.4 DMA and Bus Mastering

Most modern devices that move significant amounts of data use
DMA. The device, acting as a bus master, reads and writes the
host's memory directly, rather than requiring the CPU to copy
every byte. From the driver's side, DMA introduces several
constraints that you have to handle correctly.

The buffers must be physically contiguous, or at least scattered
in a way that the device can express. They must be aligned to a
boundary that the device requires. They must be visible to the
device, which on FreeBSD means they have to be set up through the
`bus_dma(9)` framework. And the driver and the device have to
synchronise their views of the buffer using `bus_dmamap_sync(9)`
calls, because modern processors maintain caches that may not be
coherent with device DMA.

The full mechanics of `bus_dma(9)` are covered in Chapter 21 and
need not be repeated here. For reverse engineering, the
implications are:

1. The device almost certainly imposes alignment and size
   constraints on its buffers. Captures that show buffers always
   starting at addresses with the same low bits are revealing the
   alignment.
2. The device may impose a maximum buffer size. Captures that
   show large operations split across multiple smaller transfers
   are revealing the maximum.
3. The device may use specific DMA address bits to encode
   metadata. A 32-bit pointer in a 64-bit address field, for
   example, sometimes leaves the upper 32 bits available for
   other uses, and the device may interpret those bits as flags or
   tags.

Identifying these constraints is part of the reverse-engineering
work for any DMA-capable device. The captures and the existing
driver source, if available, are the primary sources of evidence.

### 5.5 Reversing Packet Formats

When a device's data path uses structured packets, reversing the
packet format is one of the most rewarding parts of the work. The
recurring techniques are:

**Look for fixed bytes that never change.** These are usually
magic numbers, version bytes, or category codes. Mark them and
explore what they mean.

**Look for fields whose values correlate with operations.** A byte
that takes a small set of distinct values, with each value
appearing in exactly one kind of operation, is almost certainly an
opcode or a command type.

**Look for length fields.** A two-byte or four-byte field whose
value matches the size of the rest of the packet, in some
predictable encoding, is a length field. Length fields are often
near the start of the packet and are often the field that lets
you parse variable-length packets correctly.

**Look for sequence numbers or session identifiers.** Fields that
increment monotonically across a session, or that change once per
operation, are usually sequence numbers. Their value is rarely
interesting, but their presence and position help you ignore them
while looking for the real content.

**Look for checksums.** A field whose value depends on the rest
of the packet, in a way that you cannot predict from any single
byte, is probably a checksum. Standard checksums (CRC-16, CRC-32,
sums) can sometimes be identified by computing them over candidate
ranges and seeing which range matches. If a checksum exists, you
will need to compute it correctly when the driver constructs its
own packets, so identifying the algorithm is necessary.

Each of these observations builds the packet-format part of the
pseudo-datasheet. Combined with the register map from Section 4,
they form the documentation that Section 11 will assemble into a
proper datasheet substitute.

### 5.6 Identifying Which Operations Are Read-Like and Which Are
       Write-Like

Many devices have an asymmetric data path: some operations cause
the device to read from host memory, others cause it to write to
host memory. Identifying which is which is essential for getting
the DMA direction right.

The clearest evidence is in the captures. A USB capture, for
example, distinguishes IN endpoints (device to host) from OUT
endpoints (host to device) explicitly. PCI captures, when you can
get them, distinguish memory reads from memory writes by the
transaction type. Bulk DMA from a network controller into the
host is recorded as the device writing into host memory. Bulk DMA
from the host into the network controller is recorded as the
device reading from host memory.

If captures are not available, a useful technique is to set the
buffer to a recognisable pattern (all `0xCC` bytes, for example)
before triggering the operation and to inspect the buffer
afterwards. If the buffer's pattern is overwritten with new data,
the operation was a device-to-host transfer (the device wrote into
the buffer). If the buffer's pattern is unchanged, the operation
was a host-to-device transfer (the device only read from the
buffer). This kind of distinguishing experiment is one of the
clearest examples of an experiment whose result will tell you a
fact you cannot easily learn any other way.

### 5.7 Wrapping Up Section 5

We have learned to recognise the three big shapes of buffer
organisation: linear buffers, ring buffers, and descriptor rings.
We have seen how to deduce buffer sizes and entry sizes from
periodic structures, alignments, and the underlying memory
regions. We have learned to identify command queue shapes and
sequencing models. We have noted the constraints that DMA imposes
and the kinds of evidence that reveal them. We have gone through
the recurring techniques for reversing packet formats. And we have
seen how to identify the direction of a transfer when the
captures themselves do not say so directly.

The next section turns this growing understanding into code. We
will write the first version of an actual driver, one that
implements only the smallest useful piece of functionality: usually
reset, sometimes reset plus a single status read, sometimes the
absolute minimum needed to put the device into a state where it
can be driven further. The principle is that a small driver that
does something verifiably correct is a better starting point than
a large driver that does many things uncertainly.

## 6. Re-Implementing Minimal Device Functionality

After observation comes reconstruction. With captures in hand,
register hypotheses confirmed, and at least the rough shape of
buffer structure understood, you can start writing a driver. The
key word is "start". The first version of a reverse-engineered
driver does not have to do everything the device can do. It does
not even have to do most of what the device can do. It has to do
the smallest piece that is useful, and it has to do that piece
correctly.

This section walks through the principles of starting small and
the practical steps of writing the first minimal driver. The work
here builds on every chapter from earlier in the book, because the
mechanics of "write a driver" are exactly the same mechanics we
have been practising. What changes is the discipline around which
features to implement first and how to gain confidence that each
feature is correct.

### 6.1 Start With Reset, Always

The first feature you implement should almost always be reset. A
reset operation is the foundation of every other operation, for
several reasons.

Reset is usually the simplest operation a device has. It often
takes a single write to a single register, or a sequence of three
or four writes that put the device into a known state. The
hypothesis is small and easy to test.

Reset is the operation whose result is most easily verified. Before
the reset, the device's registers may hold arbitrary values. After
a successful reset, they hold predictable defaults: zero
configuration, no pending interrupts, no active operations,
identifier registers reading their constant magic values. If you
do a reset and then read the registers, you should see the
"freshly powered up" state.

Reset is the operation whose failure modes are the most benign.
If your reset attempt does not actually reset the device, the
device will be in an unknown state, and you can recover by
unloading your driver and letting the kernel re-probe the device.
A failed reset rarely damages anything; it just leaves you no
worse off than before.

Reset is also the precondition for almost every other operation.
A driver that cannot put the device into a known state cannot
rely on any subsequent operation. By implementing reset first, you
build the foundation that every later test of every later feature
will rely on.

### 6.2 The Skeleton of a Minimal Driver

For a PCI device, the skeleton driver looks very similar to every
other PCI driver you have seen in the book. Here is the essential
shape:

```c
struct mydev_softc {
    device_t          sc_dev;
    struct mtx        sc_mtx;
    struct resource  *sc_mem;
    int               sc_rid;
    struct resource  *sc_irq;
    int               sc_irid;
    void             *sc_ih;
    bus_size_t        sc_size;
    /* Driver-specific state goes here as it accumulates. */
};

static int
mydev_probe(device_t dev)
{
    if (pci_get_vendor(dev) == 0xabcd &&
        pci_get_device(dev) == 0x5678) {
        device_set_desc(dev, "ExampleCorp XYZ Reverse-Engineered");
        return (BUS_PROBE_DEFAULT);
    }
    return (ENXIO);
}

static int
mydev_attach(device_t dev)
{
    struct mydev_softc *sc = device_get_softc(dev);
    int error;

    sc->sc_dev = dev;
    mtx_init(&sc->sc_mtx, "mydev", NULL, MTX_DEF);

    sc->sc_rid = PCIR_BAR(0);
    sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
        &sc->sc_rid, RF_ACTIVE);
    if (sc->sc_mem == NULL)
        goto fail;
    sc->sc_size = rman_get_size(sc->sc_mem);

    sc->sc_irid = 0;
    sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
        &sc->sc_irid, RF_SHAREABLE | RF_ACTIVE);
    if (sc->sc_irq == NULL)
        goto fail;

    error = mydev_reset(sc);
    if (error != 0)
        goto fail;

    error = mydev_verify_id(sc);
    if (error != 0)
        goto fail;

    device_printf(dev, "attached, mapped %ju bytes\n",
        (uintmax_t)sc->sc_size);
    return (0);

fail:
    mydev_detach(dev);
    return (ENXIO);
}
```

The structure should look familiar. The driver allocates a memory
resource for its register region and an IRQ resource for its
interrupt. It then performs a reset and an identifier check. If
either fails, the driver bails out cleanly. The reset and the
identifier check are the first two functions worth writing.

### 6.3 Implementing Reset

The reset function is, in the simplest case, three lines of code:

```c
static int
mydev_reset(struct mydev_softc *sc)
{
    bus_write_4(sc->sc_mem, MYDEV_REG_CONTROL, MYDEV_CTRL_RESET);
    pause("mydev_reset", hz / 100);
    bus_write_4(sc->sc_mem, MYDEV_REG_CONTROL, 0);
    return (0);
}
```

We write the reset bit to the control register, wait a short time
for the device to act on it, and then clear the control register
to leave the device in a quiescent state. The constants
`MYDEV_REG_CONTROL` and `MYDEV_CTRL_RESET` come from the register
map you built in Section 4.

In practice, reset is rarely this simple. Many devices have a
specific reset protocol that involves several register writes in a
specific order, or that involves polling a status register until
the reset is complete, or that involves waiting for a specific
duration to pass before proceeding. The captures from Section 3
show you what the vendor driver does on attach. Replicating that
sequence is the safest reset implementation.

A more realistic reset might look like:

```c
static int
mydev_reset(struct mydev_softc *sc)
{
    int i;
    uint32_t status;

    /* Disable any pending interrupts. */
    bus_write_4(sc->sc_mem, MYDEV_REG_INT_ENABLE, 0);

    /* Trigger reset. */
    bus_write_4(sc->sc_mem, MYDEV_REG_CONTROL, MYDEV_CTRL_RESET);

    /* Wait for reset complete, with timeout. */
    for (i = 0; i < 100; i++) {
        status = bus_read_4(sc->sc_mem, MYDEV_REG_STATUS);
        if ((status & MYDEV_STATUS_RESETTING) == 0)
            break;
        pause("mydev_reset", hz / 100);
    }
    if (i == 100) {
        device_printf(sc->sc_dev, "reset timed out\n");
        return (ETIMEDOUT);
    }

    /* Clear any pending interrupt status. */
    bus_write_4(sc->sc_mem, MYDEV_REG_INT_STATUS, 0xffffffff);

    return (0);
}
```

This version handles the case where the reset takes some time to
complete and where the device exposes a status bit reporting
"reset in progress". Adding a timeout, with an explicit error path
when the timeout expires, is a habit worth adopting from the
start. A driver that cannot tell the difference between "the
device is slow today" and "the device has failed" will eventually
hang the kernel, and the cost of adding the timeout is trivial.

### 6.4 Verifying the Identifier

Every reverse-engineered driver should verify that the device
attached to is the device the driver expects. The verification is
typically one or two register reads, comparing the values against
known constants:

```c
static int
mydev_verify_id(struct mydev_softc *sc)
{
    uint32_t id, version;

    id = bus_read_4(sc->sc_mem, MYDEV_REG_ID);
    if (id != MYDEV_ID_MAGIC) {
        device_printf(sc->sc_dev,
            "unexpected ID 0x%08x (expected 0x%08x)\n",
            id, MYDEV_ID_MAGIC);
        return (ENODEV);
    }

    version = bus_read_4(sc->sc_mem, MYDEV_REG_VERSION);
    if ((version >> 16) != MYDEV_VERSION_MAJOR) {
        device_printf(sc->sc_dev,
            "unsupported version 0x%08x\n", version);
        return (ENODEV);
    }

    device_printf(sc->sc_dev, "ID 0x%08x version 0x%08x\n",
        id, version);
    return (0);
}
```

The check serves two purposes. First, it confirms that the
driver's understanding of the register layout is correct: if the
identifier register holds the expected value at the expected
offset, the layout is at least consistent with what we believe.
Second, it gives you a fast-fail path if the driver is somehow
attached to the wrong device, perhaps because two devices share a
PCI ID prefix or because the device has multiple silicon
revisions.

### 6.5 Adding Logging

Logging is your eyes during reverse engineering. Every register
write, every register read whose value matters, every milestone in
the driver's startup sequence: log it. The kernel's `device_printf`
goes to `dmesg`, where you can see it in real time as the driver
loads.

Logging is not a debug aid you remove later. In a
reverse-engineering project, the logging is part of the driver,
and it stays in the driver until the driver is well understood.
The cost is small: a handful of `device_printf` calls take
negligible CPU time and produce a few lines of output. The benefit
is large: when something goes wrong, you have a complete record of
what the driver tried to do.

A common pattern is to wrap register access in a small inline
helper that logs the access:

```c
static inline uint32_t
mydev_read(struct mydev_softc *sc, bus_size_t off)
{
    uint32_t val = bus_read_4(sc->sc_mem, off);
    if (mydev_log_reads)
        device_printf(sc->sc_dev,
            "read  off=0x%04jx val=0x%08x\n",
            (uintmax_t)off, val);
    return (val);
}

static inline void
mydev_write(struct mydev_softc *sc, bus_size_t off, uint32_t val)
{
    if (mydev_log_writes)
        device_printf(sc->sc_dev,
            "write off=0x%04jx val=0x%08x\n",
            (uintmax_t)off, val);
    bus_write_4(sc->sc_mem, off, val);
}
```

The `mydev_log_reads` and `mydev_log_writes` flags can be tunable
sysctls, so you can enable or disable logging without rebuilding
the driver. The companion code in
`examples/part-07/ch36-reverse-engineering/lab03-register-map/`
uses exactly this pattern.

### 6.6 Comparing Logged Output Against Captures

With logging in place, you can compare what your driver does
against what the vendor's driver did. Run the vendor's driver in
the observation rig and capture the result. Run your driver in
your development environment and capture its log output. Compare.

If the two sequences match, your driver is doing what the vendor's
driver does, at least for the operations you have implemented. If
they differ, the differences are telling you something. Maybe your
driver is missing a write that the vendor's driver makes. Maybe
your driver is writing a value that differs from what the
vendor's driver wrote. Maybe your driver is reading a register
that the vendor's driver does not read. Each difference is a
hypothesis to investigate.

This comparison technique is one of the most effective you have.
It turns the captures from passive evidence into an active
specification: the driver should produce the same sequence of
operations as the captures show. Where the driver deviates, the
deviation is a bug or a missing piece, and the captures tell you
what to add.

### 6.7 Writing the Module Boilerplate

The minimal driver also needs the standard module boilerplate. By
this stage in the book the boilerplate should be familiar:

```c
static device_method_t mydev_methods[] = {
    DEVMETHOD(device_probe,   mydev_probe),
    DEVMETHOD(device_attach,  mydev_attach),
    DEVMETHOD(device_detach,  mydev_detach),
    DEVMETHOD_END
};

static driver_t mydev_driver = {
    "mydev",
    mydev_methods,
    sizeof(struct mydev_softc),
};

DRIVER_MODULE(mydev, pci, mydev_driver, NULL, NULL);
MODULE_VERSION(mydev, 1);
```

The `pci` parent in `DRIVER_MODULE` makes the kernel offer this
driver every PCI device during probe, and the driver's `probe`
method decides whether to claim each one. The `MODULE_VERSION`
macro is a courtesy to anyone using `kldstat -v` to inspect loaded
modules; it is also required if other modules want to declare a
dependency on yours.

The Makefile is the standard FreeBSD kernel-module Makefile:

```makefile
KMOD=   mydev
SRCS=   mydev.c device_if.h bus_if.h pci_if.h

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

The `device_if.h`, `bus_if.h`, and `pci_if.h` listed in `SRCS` are
generated header files. The build system creates them on demand,
and listing them tells `make` to do so before compiling
`mydev.c`. If the driver becomes more complex and is split into
multiple source files, list each source file separately.

### 6.8 The First Successful Load

When you load the minimal driver for the first time, the success
criterion is modest. The driver should:

1. Be successfully loaded by `kldload`.
2. Recognise the device through `probe`.
3. Allocate its memory and IRQ resources through `bus_alloc_resource_any`.
4. Reset the device.
5. Verify the identifier.
6. Report success in `dmesg` and stay loaded without errors.
7. Be successfully unloaded by `kldunload` without panicking.

A driver that meets these criteria has done useful work. It has
proven that the register layout is at least partially correct, that
the reset sequence works, and that the identifier register is
where you thought it was. Compared to where you started, this is
substantial progress.

If any of the steps fail, the failure is informative. A
`bus_alloc_resource_any` failure usually means the BAR layout is
not what you expected. An ID-verification failure usually means
the register offset is wrong, or the device's identifier value is
not what you assumed. A panic on unload usually means a resource
was not properly released; this is a bug in the detach path that
needs to be fixed before any further work proceeds.

### 6.9 Unloading Cleanly

The detach path is at least as important as the attach path. A
driver that can be loaded but not unloaded cleanly is a driver
that requires a reboot every time you want to test a change, and
the reboot will quickly become the bottleneck of your work.
Spending time on a clean detach path early pays large dividends
in development speed.

```c
static int
mydev_detach(device_t dev)
{
    struct mydev_softc *sc = device_get_softc(dev);

    if (sc->sc_ih != NULL)
        bus_teardown_intr(dev, sc->sc_irq, sc->sc_ih);
    if (sc->sc_irq != NULL)
        bus_release_resource(dev, SYS_RES_IRQ,
            sc->sc_irid, sc->sc_irq);
    if (sc->sc_mem != NULL)
        bus_release_resource(dev, SYS_RES_MEMORY,
            sc->sc_rid, sc->sc_mem);
    mtx_destroy(&sc->sc_mtx);
    return (0);
}
```

Each resource that was allocated must be released. Each registered
interrupt handler must be torn down. Each mutex must be destroyed.
The order is the reverse of the allocation order, which is a
useful general rule.

### 6.10 Wrapping Up Section 6

The minimal driver is a foundation. It does very little, but it
does that little correctly. Reset works, identifier verification
works, the resource allocations are right, the detach path is
clean. From this base, every further feature can be added
incrementally, with confidence that the underlying scaffolding is
sound.

The next section takes the minimal driver and shows how to grow it
one feature at a time, always confirming that each new feature is
correct before adding the next. The technique is the discipline of
incremental development applied to a domain where there is no
specification to refer to: instead of validating against a spec,
we validate against captures and against measured behaviour. The
result is the same: a driver that does what we believe it does,
backed by evidence rather than by hope.

## 7. Iteratively Expanding the Driver and Confirming Behavior

The minimal driver in Section 6 is the seed of the real driver. To
grow it into something useful, you add features one at a time, and
for each feature you confirm that what the feature does in the
running driver matches what you have observed the device doing
under the vendor's driver. The process is straightforward but
demanding: every step has to be verified, every verification has
to be recorded, and every recording becomes part of the
pseudo-datasheet that Section 11 will assemble.

This section walks through the methodology of incremental
expansion. It does not introduce new low-level techniques, because
the low-level techniques are the same as in Sections 4 and 6. What
it adds is the discipline of growing a driver responsibly under
uncertainty.

### 7.1 The One-Feature-at-a-Time Rule

The rule is in the name. Add one feature, verify it works, commit
the result to your repository, then add the next feature. Do not
add three features at once and then try to debug the combination.
Do not add a feature and proceed to the next without verification.
Do not skip the commit, because the commit is what lets you roll
back when a later change breaks something that worked before.

The discipline is the same as test-driven development, with one
significant adaptation: the test is often a comparison against the
captures from Section 3, not a unit test in the traditional sense.
The "test" is "the driver, when invoked to perform operation X,
produces the same sequence of register accesses as the vendor's
driver does". The comparison is mechanical, but it is a real test:
either the sequences match or they do not, and a failure tells
you that something has changed.

A typical sequence of features for a network controller might be:

1. Read MAC address.
2. Set MAC address.
3. Set up receive descriptor ring.
4. Receive one packet.
5. Set up transmit descriptor ring.
6. Send one packet.
7. Enable interrupts.
8. Implement interrupt handler.
9. Bring the link up.
10. Bring the link down.
11. Reset and recover from error.

Each item is a discrete feature. Each item builds on the previous
items. Each item has a clear success criterion (the operation
either works or it does not). And each item has a corresponding
capture from Section 3 against which the driver's behaviour can be
compared.

### 7.2 The Verification Loop

For each new feature, the verification loop is:

1. Form the hypothesis: this feature works by doing X.
2. Implement the feature in the driver, with logging that records
   each register access.
3. Trigger the feature using a small userspace program, a sysctl,
   or whatever interface you have wired up.
4. Capture the kernel log output.
5. Compare against the relevant capture from Section 3.
6. If they match, declare the feature implemented.
7. If they do not match, identify the difference and decide
   whether your implementation is wrong or your hypothesis was
   wrong.

The loop is slow. A single feature might require several
iterations through the loop, with hypothesis revisions in between.
But the slowness is the point. Each iteration is a deliberate
test, with a deliberate prediction and a deliberate observation.
Over many iterations, the body of verified facts grows steadily
and the chance of an incorrect belief surviving into the final
driver shrinks rapidly.

### 7.3 The Compare-Against-Vendor Pattern

The comparison against the vendor's driver is so central to this
work that it deserves its own discussion. The pattern looks like
this.

You have a capture from Section 3 of the vendor's driver
performing operation X. The capture contains a list of register
accesses (for memory-mapped devices) or USB transfers (for USB
devices), each labelled with timestamp, address or endpoint,
direction, and value.

You have your driver, with logging enabled, performing the same
operation X. The kernel log contains a list of register accesses
or USB transfers, each labelled with timestamp, address or
endpoint, direction, and value.

You compare the two lists. The accesses should be in the same
order, to the same addresses or endpoints, in the same direction,
with values that either match exactly or differ in ways that you
can explain (because, for example, the values include sequence
numbers or timestamps that naturally differ between runs).

Where the two lists agree, your driver is doing what the vendor's
driver does. Where they disagree, you have a discrepancy to
investigate. Some discrepancies are benign: your driver might omit
a redundant register read that the vendor's driver does for some
internal reason, and the omission has no effect on the device's
behaviour. Other discrepancies are real bugs: a value you wrote
incorrectly, a sequence step you missed, a register you did not
realise the operation needed.

The compare-against-vendor pattern is essentially a
specification-by-example. The specification of operation X is "the
sequence of accesses the vendor's driver makes when performing
operation X". Your implementation conforms to the specification
when its sequence of accesses matches. The technique is not
perfect: it cannot detect bugs that affect the device's internal
state without affecting the access sequence, and it cannot detect
bugs in the values you write if those values change between runs
in the vendor's capture too. But it catches the great majority of
implementation errors, and it catches them at exactly the level
of detail where they happen.

### 7.4 Documenting Each Discovered Register

As you add features, you discover registers. Every register that
you write to or read from in any feature is part of the register
map, and every one of them needs to be added to the
pseudo-datasheet you are building.

A useful technique is to maintain the pseudo-datasheet as a
separate Markdown file alongside the driver source, and to update
it every time you discover a new fact. Section 11 will discuss
the pseudo-datasheet's structure in detail; for now, the rule is
simply that no fact you have learned should remain only in the
driver code. The driver code is the implementation; the
pseudo-datasheet is the documentation. They serve different
purposes, and someone reading either should be able to understand
the device.

A common mistake is to defer documentation until the project is
"finished". Reverse-engineering projects are rarely finished in
that sense. They reach increasing levels of completeness over
time, and the documentation should grow alongside the
implementation. A driver with twenty features and a one-paragraph
pseudo-datasheet is a maintenance disaster waiting to happen,
because nobody can extend it without rediscovering everything you
already knew.

### 7.5 When the Vendor's Driver Disagrees With Itself

A subtle complication arises when the vendor's driver performs the
same operation differently on different occasions. Sometimes this
happens because the driver has multiple code paths for the same
operation, depending on the device's state or the user's
configuration. Sometimes it happens because the driver has
optional behaviour that activates under specific conditions.

When you encounter such variation, the right response is to
capture more, not to give up. Take captures of the operation under
several different starting conditions and compare them. If you can
identify what condition selects each variant, you have learned
something about the driver's logic. If the variants are functionally
equivalent (they all achieve the same end state), you can choose
the simplest one as the canonical implementation. If they are not
functionally equivalent, the device may have multiple modes of
operation that you need to support.

Sometimes the variation is just non-determinism: the vendor's
driver chose a sequence number from a counter, or used a
timestamp, and the resulting capture differs slightly from one run
to another. Non-determinism is benign, and learning to recognise
it (so you can ignore it) is part of the skill of comparing
captures.

### 7.6 Branching the Driver as Hypotheses Diverge

When you have two competing hypotheses and an experiment that
will distinguish them, branch the driver. Make a Git branch for
the experiment, implement the change, run the experiment, observe
the result, and then either merge the branch or discard it. Branches
are cheap; you can have several hypothesis-testing branches in
flight at once, and you can come back to any of them whenever a
new experiment seems worthwhile.

The branching discipline is particularly valuable when an
experiment requires significant code changes. Without branches, you
might be reluctant to spend an hour writing experimental code that
you will throw away. With branches, the experimental code lives
in its own branch, and your main branch stays clean. If the
experiment succeeds, you merge. If it fails, you discard. Either
way, you have a clean history.

### 7.7 Wrapping Up Section 7

We have learned how to grow a minimal driver into a more capable
one through a disciplined cycle of one-feature-at-a-time
implementation, comparison against captures, and documentation of
every discovered fact. The mechanics are simple. The discipline is
demanding. The reward is a driver whose every feature is backed by
evidence, whose register map is recorded as it grows, and whose
behaviour can be defended against any future challenge.

The next section turns to a slightly different topic: how to build
confidence in your driver before you risk running it against real
hardware. Simulation, mock devices, and validation loops are the
techniques that let you find bugs in safety, where a kernel panic
or a hang costs only a VM reboot, rather than in production, where
the cost can be much higher.

## 8. Validating Assumptions With Simulation Tools

Reverse-engineering work moves between two kinds of risk. The
first is the risk of being wrong: of writing code that does
something other than what you believe it does. The second is the
risk of being right too late: of finding out about a bug only when
it has already damaged something, when the bug has driven the
device into an unrecoverable state, or when the bug has crashed a
production machine. The first risk is unavoidable; bugs are
intrinsic to the work. The second risk is largely avoidable, and
the way to avoid it is simulation.

A simulator is anything that lets you run your driver code without
the real device being attached. Simulators range from simple mock
implementations of a few register reads, through full-featured
software emulations of an entire device, to virtualised hardware
environments that pretend to be the real device for the
operating-system stack above them. FreeBSD provides several
simulator-like tools that fit naturally into the
reverse-engineering workflow.

This section covers three kinds of simulation: mock devices that
you write inside the kernel, the USB template framework for
emulating USB devices, and bhyve passthrough for running an
unmodified real device under a controlled hypervisor. We will see
how each kind helps you validate assumptions, where each is most
useful, and how to combine them with the captures and the
register-map work from earlier sections.

### 8.1 The Kernel-Space Mock Device

The simplest form of simulation is a small kernel module that
exposes the same interfaces as the real device but implements
them in software. The mock module attaches as a child of an
existing bus, allocates a software-backed "register region", and
implements the register reads and writes in C code that runs in
the kernel.

For a memory-mapped device, the mock works like this. Where the
real driver would allocate `SYS_RES_MEMORY` from the bus and use
`bus_read_4` and `bus_write_4` to talk to the device, the mock
hands the driver a pointer to a software-allocated array of
words. Reads return the current value of the word at the
requested offset. Writes update the word and trigger any
software-implemented side effects. Side effects are how the mock
implements the device's behaviour: when the driver writes the
"go" bit to the control register, the mock might internally start
a callout that, after a simulated processing time, updates the
status register to indicate completion and asserts a simulated
interrupt.

The mock is small, often a few hundred lines for a device whose
real driver might be several thousand lines. The point is not to
implement everything; the point is to implement enough that the
driver's behaviour can be tested in isolation. A driver that
correctly handles "command issued, command complete, results
returned" against a mock has at least proved that its top-level
sequencing is correct, even if the mock cannot test what happens
when the real device produces values the driver did not anticipate.

The companion files in
`examples/part-07/ch36-reverse-engineering/lab04-mock-device/`
contain a small mock device that demonstrates the pattern. The
mock implements a tiny "command and status" interface: there is a
`CMD` register the driver writes to, a `STATUS` register the
mock updates after a simulated delay, and a `DATA` register the
driver reads after the status indicates completion. The mock is
intentionally simple, but it shows the structure: define the
register addresses, allocate backing storage for them, implement
read and write callbacks, and arrange for callbacks to do
whatever side-effect logic the simulated device requires.

### 8.2 Trapping Driver Accesses

A more sophisticated mock can be built by interposing on the bus
space layer itself. FreeBSD's `bus_space(9)` API uses opaque
handles, and a custom bus-space implementation can intercept
every read and write that the driver performs. The interception
can log the access, modify the value, return a pre-arranged
response, or simulate any other behaviour.

This technique is useful when you want to test a driver that has
already been written for a real device, against a simulator, with
minimal changes to the driver itself. The driver continues to use
`bus_read_4` and `bus_write_4` as it always does. The intercept
layer takes the calls and either dispatches them to the real
device or fakes them as needed.

The technique is also useful when you want to record exactly what
the driver does during a test, in a form that can be compared
against captures from Section 3. An intercepting bus-space layer
can produce a log file that has the same structure as a capture
file, ready for direct comparison.

Implementing this kind of intercept is more involved than the
simple mock, because you have to provide a complete bus-space
implementation. The pattern is well established in the FreeBSD
source. Readers interested in the deep version of this technique
can study the architecture-specific bus-space backends in
`/usr/src/sys/x86/include/bus.h` and the related files; the
function-pointer table approach used there is the same approach
you would use for an interception layer.

### 8.3 USB Template Drivers

For USB devices, FreeBSD includes an unusually capable simulation
mechanism: the USB template framework. The template framework lets
the kernel pretend to be a USB device, with a programmable set of
descriptors, by exposing the USB device endpoint via the host
controller in device mode. The relevant source files are under
`/usr/src/sys/dev/usb/template/`, and the framework includes
ready-made templates for several common device classes:

- `usb_template_audio.c` for USB audio devices.
- `usb_template_kbd.c` for USB keyboards.
- `usb_template_msc.c` for USB Mass Storage devices.
- `usb_template_mouse.c` for USB mice.
- `usb_template_modem.c` for USB modems.
- `usb_template_serialnet.c` for USB serial-network composite
  devices.

Several additional templates under the same directory cover other
classes, including CDC Ethernet (`usb_template_cdce.c`), CDC EEM
(`usb_template_cdceem.c`), MIDI (`usb_template_midi.c`), MTP
(`usb_template_mtp.c`), and multi-function and phone composite
devices (`usb_template_multi.c`, `usb_template_phone.c`). Browse the
directory on your own system to see the full list; the set is
expanded over time as contributors add new device classes.

Each template is a programmable description of a USB device:
which class it claims to be, which endpoints it exposes, what its
descriptors look like. Loaded into a USB host controller running
in device mode (the controller pretends to be the device side of
a USB cable), the template lets the host see a USB device that
matches the descriptors.

For reverse-engineering work, the templates have two uses. The
first is as a probe-test substrate: you can write a USB device
descriptor that mimics the static identity of an unknown device
and use the template framework to expose it to a development
machine. The development machine's USB stack will probe the
descriptors, attach drivers (yours, or the in-tree drivers if
your descriptors collide), and you can observe what the host
expects from a device with this identity. The exercise is
particularly valuable when you have captured the descriptor of an
unknown device but cannot get the device itself onto your
development machine; the template lets you simulate what the host
would see.

The second use is as a foundation for a software simulator that
implements the full protocol of an unknown USB device. Building
such a simulator is a substantial undertaking, but for very
important reverse-engineering targets it can be the only way to
test driver code without risking the real hardware. The templates
provide the descriptor side of the equation; you have to add the
data-handling logic yourself.

### 8.4 bhyve and Passthrough

bhyve is FreeBSD's native hypervisor, and its PCI passthrough
support is a valuable tool for reverse engineering. The
relevant code is in `/usr/src/usr.sbin/bhyve/pci_passthru.c`,
and the kernel side uses the `ppt(4)` driver
(`/usr/src/sys/amd64/vmm/io/ppt.c`) to claim the device and
expose it to the guest.

The passthrough workflow is:

1. On the host, detach the device from any in-tree driver:
   ```sh
   $ sudo devctl detach pci0:0:18:0
   ```
2. Configure `ppt(4)` to claim the device, by adding an entry to
   `/boot/loader.conf`:
   ```text
   pptdevs="0/18/0"
   ```
   This requires a reboot to take effect, after which the device
   will be claimed by `ppt(4)` instead of any other driver.
3. Start a bhyve guest with the device passed through:
   ```sh
   $ sudo bhyve -c 2 -m 2G \
       -s 0,hostbridge \
       -s 1,virtio-blk,disk.img \
       -s 5,passthru,0/18/0 \
       -s 31,lpc \
       -l com1,stdio \
       myguest
   ```
   The `-s 5,passthru,0/18/0` argument tells bhyve to expose the
   host's PCI device at 0/18/0 to the guest, where it will appear
   in the virtual PCI bus.

Inside the guest, the device behaves like a normal PCI device.
The vendor's driver, running in the guest, can access it just as
it would on bare metal. From the host, you cannot directly see
the device's register accesses, because the hypervisor handles
them for the guest, but you can use bhyve's logging facilities to
see configuration-space accesses, interrupt routing, and other
operations that pass through the hypervisor.

For reverse engineering, the value of bhyve passthrough is that
it lets you run the vendor's driver in a controlled environment
where you can capture some kinds of activity that would otherwise
be invisible. It also lets you snapshot and restore the host
between experiments, so that you can recover quickly if an
experiment goes wrong.

A particularly useful pattern is to develop your own driver on the
host, with the device detached from `ppt(4)`, and to switch to
guest-passthrough only when you want to compare your driver's
behaviour against the vendor's. The switch requires changing the
`pptdevs` configuration and rebooting, but the inconvenience is
small compared to the value of being able to run both
implementations on the same hardware.

### 8.5 Loop With Known Return States

A simple but effective form of simulation is the validation loop
with known return states. The idea is to instrument your driver
so that, in test mode, certain register reads return values that
you specify rather than values from the device. You then exercise
the driver and observe what it does in response to each
hand-chosen value.

For example, suppose you want to know what your driver does when
the device returns an unexpected status value. In production, the
device might never produce such a value, and there is no obvious
way to test the path. With a validation loop, you replace the
read of the status register with a function that returns a
sequence of values you specify: 0x00, then 0x01, then 0xff, then
back to 0x00. The driver behaves as if the device returned each
of those values in turn, and you can verify that the driver's
response is correct in each case.

The validation loop technique is particularly valuable for error
paths. Most error conditions are rare in real operation, and
testing them by waiting for them to occur naturally would take
forever. By forcing them to occur on demand, you can test the
error paths systematically. The technique is, in spirit, the same
as the fault-injection techniques used in mature systems software,
adapted to the reverse-engineering context where the driver
itself is the artifact under test.

### 8.6 Limits of Simulation

Simulation is enormously valuable, but it has limits. A simulator
implements the simulator's understanding of the device, not the
device itself. When the simulator and the real device differ, the
simulator's tests pass and the real device misbehaves, and you
have learned that your understanding of the device was incomplete
in some specific way.

The right attitude is to treat simulation as a way to find
certain classes of bugs (driver logic errors, sequence errors,
error-handling errors) and not as a guarantee that the driver
will work on real hardware. Bugs in the driver's understanding of
the device's contract will not be caught by a simulator that
shares that misunderstanding. Bugs in the device's actual
behaviour, that differ from what the simulator implements, will
not be caught by simulator testing at all.

The combination is the answer. Simulator testing catches the bugs
it can. Real-device testing, performed carefully and incrementally
with the safety techniques from Section 10, catches the rest. The
two together are far better than either alone.

### 8.7 Wrapping Up Section 8

We have seen the simulation tools that complement the
reverse-engineering workflow: small in-kernel mock devices, the
USB template framework, bhyve passthrough, and validation loops
with known return states. Each tool addresses a different aspect
of the problem, and together they let you find a great many bugs
before any of them reach the real device.

The next section steps back from the technical work and looks at
the social context: how reverse-engineering projects collaborate,
where to find prior work, and how to publish your own findings.
The technical part of reverse engineering is the visible part, but
the community part is what makes the work durable. A driver that
is well documented and well shared lasts. A driver that is
written quickly and never published often disappears with its
author.

## 9. Community Collaboration in Reverse Engineering

Reverse engineering is rarely a solitary activity, even when it
appears to be. Almost every successful reverse-engineering
project either builds on prior work or eventually contributes to
it. The community of people who care about a particular device,
or about a particular family of devices, is small but persistent,
and learning to find and contribute to that community is one of
the most efficient things a reverse engineer can do.

This section discusses how to find prior work, how to evaluate
its trustworthiness, how to build on it without legal or ethical
problems, and how to share your own findings in a form that
helps others. The work is not glamorous, but it is what makes
reverse engineering a productive long-term activity rather than a
series of disconnected efforts.

### 9.1 Finding Prior Work

When you start investigating a device, the first thing to do is
search for prior work. Even devices that seem to be obscure are
sometimes the subject of significant prior research, and finding
that research saves enormous time.

The most valuable sources, in approximate order of usefulness:

**FreeBSD source tree.** Search `/usr/src/sys/dev/` for the
device's vendor ID, device ID, USB descriptor, or vendor name. A
driver in the FreeBSD tree is the gold standard of prior work,
because it is already validated, builds against the current
kernel, and follows FreeBSD conventions. Even when no driver
exists, related drivers for the same vendor often share patterns
that apply.

**OpenBSD and NetBSD source trees.** The other BSDs have their
own driver collections, and they often have drivers for devices
that FreeBSD does not yet support. The code is generally
straightforward to read and to port. Licensing is usually
compatible with FreeBSD (most BSD-licensed code is).

**Linux source tree.** The Linux kernel has the broadest
collection of device drivers in any open-source project. Most are
GPL-licensed, which means you cannot copy code into a FreeBSD
driver, but you can read them for understanding and write your
own implementation from a clean slate. The clean-room approach of
reading the Linux driver to understand the device, then writing
original code from that understanding, is well established and
legally accepted in interoperability cases; the full framework
for doing this safely, including the paper trail that a
clean-room defence actually depends on, appears later in this
chapter in Section 12. Even when the legal question is settled,
a direct port from Linux to FreeBSD is rarely workable for
purely technical reasons. Linux drivers lean on kernel APIs that
have no direct FreeBSD equivalent: its spinlock and mutex
primitives have different wake-up semantics, its memory
allocators distinguish contexts that FreeBSD expresses
differently, and its device model (with `struct device`, sysfs,
and the driver-core probe flow) does not map onto Newbus. The
right way to use a Linux driver is to mine it for device-level
facts (register layouts, magic values, initialisation order, quirk
tables) and then rebuild the control flow against FreeBSD's own
primitives. Section 13 shows two FreeBSD drivers that did
exactly this.

**Hardware-specific community sites.** For many devices,
enthusiasts have built dedicated sites that collect everything
known about a particular family. The Wireshark wiki has extensive
documentation on USB protocols. The OpenWrt project has
documentation on many embedded devices. Many specific hardware
families have their own community wikis or forums.

**Vendor mailing lists, technical support archives, application
notes.** Some vendors publish more in technical support contexts
than in their public documentation. A search through their
support archives or developer mailing lists can sometimes turn up
information that is not in the official documentation.

**Patents.** Patent applications often contain detailed
descriptions of how a device works, because the legal requirement
to describe the invention forces some level of disclosure. Patent
databases are searchable by company and by year, and a search for
the right vendor's patents can sometimes uncover a wealth of
detail.

**Academic papers.** When the device is in a specialised field
(scientific instrumentation, industrial control, certain
classes of network equipment), academic papers in that field may
have already documented the device's interface for research
purposes.

The first hour of any project should be spent searching these
sources. Even an unsuccessful search is informative: knowing that
no prior work exists changes the project's scope significantly.

### 9.2 Evaluating Trustworthiness

Not all prior work is equally trustworthy. A working open-source
driver is highly trustworthy, because the author's claims are
backed by code that demonstrably works. A community wiki page
written by an enthusiast may or may not be trustworthy, depending
on the source's track record. A vendor white paper is generally
trustworthy on points the vendor wants to make and possibly
silent or misleading on points the vendor would rather not
discuss.

The skill of evaluating sources is the same skill that historians
and journalists develop. You ask: who wrote this, when, on the
basis of what evidence, with what motivations? You compare claims
across multiple sources. You give the most weight to claims that
are supported by code or by direct measurement. You give less
weight to claims that depend on the author's reputation alone.

When you adopt a fact from prior work, record where it came from.
The notebook entry should say "from the Linux ath5k driver,
register XYZ at offset N is the channel selector". A year later,
when the fact turns out to be wrong, you will know where the
error came from and what other facts from the same source might
also be wrong.

### 9.3 The Clean-Room Discipline

The clean-room discipline is the standard methodology for using
prior work without infringing on its copyright. The discipline is
straightforward but requires care.

In the strict clean-room form, two people are involved. One
reads the prior work (a vendor driver, a leaked specification, a
disassembled binary) and produces a description of what the
device does. The description is in natural language and contains
no copyrighted material. The second person reads only the
description and writes the new driver. The second person never
sees the original work. Because the new driver was written
without reference to the original, it cannot be a derivative of
it under copyright law.

In the relaxed form, often used in solo projects, the same person
performs both roles, but is careful to keep them separated in
time and to document the separation. Read the prior work. Take
notes. Put the prior work away. Wait. Then, working only from
your notes, write the driver. The notes are the bridge; the prior
code is never in front of you while you are writing the new code.

The relaxed form is less legally bulletproof than the strict
form, but it is what most individual reverse engineers actually
do, and the case law on interoperability work generally supports
it. The key is the discipline of separating reading from writing,
so that you can prove (and prove to yourself) that the new code
is independent.

In all cases, the new driver must not contain copied code,
copied register-name macros, copied data structures, or copied
comments from the original. It can contain ideas, structural
patterns, and the device's interface itself, because those are
not copyrightable. Anything that is copyrightable must be
re-expressed in your own words and your own structure.

### 9.4 Sharing Your Findings

Reverse-engineering work, when complete, should be published. The
publication serves several purposes: it lets others use the
driver, it lets others extend the driver, it preserves the
knowledge against the case where you stop working on it, and it
contributes to the body of public knowledge about the device.

The minimum useful publication is a Git repository that contains:

- The driver source code, with a clear license.
- A README explaining what the driver does and how to build it.
- The pseudo-datasheet (Section 11) that documents the device.
- The test programs and any other tools needed to use the driver.
- The notebook, or a cleaned-up version of it, so that others can
  trace the reasoning behind any claim.

A more polished publication adds:

- A document describing the methodology, so that others can
  reproduce the work.
- A list of open questions and unresolved issues.
- A clear distinction between verified facts and remaining
  hypotheses.
- Attribution to all sources of prior work, with links.

The license matters. For a driver intended for FreeBSD inclusion,
the BSD two-clause license is the standard choice. The license
must make the work compatible with the FreeBSD source tree's
license requirements. If you have used material from a
GPL-licensed project, even cleanroom-style, document the
provenance carefully so that any concerns about derivation can be
resolved by examining your notes.

Once you have published, announce the work in places where
interested people will see it: the FreeBSD-arm mailing list for
embedded work, the FreeBSD-net mailing list for network drivers,
relevant project-specific communities, and your personal channels.
The announcement should be brief but informative: what the
driver does, what state of completeness it is in, where the
repository is, and who is welcome to contribute.

### 9.5 Maintaining a Pseudo-Datasheet in Markdown or Git

The pseudo-datasheet itself deserves special attention. It is the
single most valuable artifact you produce, often more valuable
than the driver code. The driver code is one implementation of
your understanding of the device; the pseudo-datasheet is the
understanding itself, and from it any number of drivers (FreeBSD,
Linux, NetBSD, custom embedded systems) can be written.

The format that has proven most useful in community projects is a
Git-tracked Markdown file (or a small set of files), structured
around the device's logical components: identity, register map,
buffer layouts, command set, error codes, initialisation
sequence, programming examples. We will look at the structure in
detail in Section 11.

The Git tracking is essential. The pseudo-datasheet is a living
document; it will be updated as new facts are discovered, as old
facts are corrected, as additional reverse-engineering work
extends its coverage. The Git history is the record of how the
understanding has evolved. When a fact is corrected, the Git log
shows when it was corrected and by whom, so that other readers
can know when their old printout has gone out of date.

The companion code in
`examples/part-07/ch36-reverse-engineering/lab06-pseudo-datasheet/`
contains a Markdown template for a pseudo-datasheet, suitable as
a starting point for your own projects. The template is
opinionated about structure, because consistent structure makes
the document easier to read, easier to compare against other
pseudo-datasheets, and easier to extract automatically into other
formats.

### 9.6 Wrapping Up Section 9

The community-collaboration side of reverse engineering is the
side that gives the work its longevity. A driver written in
isolation and never published may serve its author well, but it
disappears with that author. A driver written in dialogue with
prior work and published clearly serves a much wider audience
and lasts much longer.

The next section turns to the safety side of the work, which we
have referenced repeatedly. Safety deserves its own dedicated
section, because the consequences of getting it wrong can include
permanent damage to expensive hardware, and the techniques for
avoiding such damage are concrete enough to be taught explicitly.

## 10. Avoiding Bricking or Hardware Damage

This is the section that this chapter has been deferring all
along. Reverse engineering, done carelessly, can damage hardware.
Some kinds of damage are recoverable with effort. Other kinds
turn a working device into an inert brick that nothing short of a
specialised vendor recovery tool, if it exists at all, can fix.
A reverse engineer who works without understanding which patterns
are dangerous will eventually destroy something. The goal of this
section is to tell you, concretely, which patterns to avoid and
which patterns are safe.

The advice in this section applies to every other section in the
chapter. The probing techniques in Section 4 are safe only when
combined with these warnings. The minimal driver in Section 6 is
safe only when its writes are constrained as described here. The
iterative expansion in Section 7 is safe only when each new
feature is evaluated against these criteria before it is allowed
to write to the device. Read this section in full; do not skip
to the technique sections without it.

### 10.1 The General Principle

The general principle is simple to state and, with practice,
straightforward to apply.

> **Never write to an unknown register or send an unknown
> command without strong evidence that the operation is safe.**

The strong evidence can come from several sources, and each is
worth its own short discussion.

**The vendor's driver performed the same operation.** If you have
captured the vendor's driver writing a specific value to a
specific register, and the resulting device behaviour was healthy,
then performing the same write in your driver is, almost
certainly, safe. The vendor's driver is presumably well-tested
and would not casually do something that bricks the device.

**The operation is a documented standard operation.** Some
operations are so universal that they can be assumed to be safe.
PCI configuration-space reads are safe by design. PCI standard
control register writes (clear pending interrupts, enable bus
mastering, set memory access enable) are safe in the standard
range. USB control transfers using standard requests on standard
endpoints are safe. The standardised parts of a protocol can be
trusted; the vendor-specific parts cannot be trusted without
evidence.

**The operation is reversible.** A write that you can immediately
undo is much safer than one you cannot. Setting a configuration
bit, observing the effect, and then restoring the original value
is far safer than performing an operation that has permanent
consequences.

**The operation has been tested in simulation.** A write that you
have first tested against a mock or a simulator, where the
worst-case outcome is a kernel panic in a test VM, is safer than
a write you have only modeled in your head.

In the absence of any of these forms of evidence, the operation
is unknown, and the rule is: do not perform it.

### 10.2 Operations That Deserve Particular Caution

Some categories of operations are dangerous enough to deserve
explicit attention. These are the operations where a careless
write can permanently damage a device, and they are the
operations that should never be performed without strong
evidence.

**Flash writes.** Many devices contain flash memory used for
firmware, configuration, calibration, or device identifiers.
Writes to flash are irreversible (or at best reversible by
restoring a backup). A driver that accidentally writes to flash
can corrupt the firmware (rendering the device unbootable),
overwrite calibration data (rendering the device inaccurate or
unusable), or change the device's identity (rendering it
unrecognisable to its own firmware). The patterns that trigger
flash writes vary by device, but they often involve specific
"unlock" sequences followed by writes to addresses that are
explicitly the flash region. If your captures show such a
sequence in a context where the user explicitly performed a
"firmware update" operation, do not replicate the sequence in
your driver unless you are explicitly implementing firmware
update.

**Hard resets that affect non-volatile state.** Most resets are
soft resets that return the device to its initial in-memory
state. Some resets, however, also clear non-volatile state:
calibration data, configuration, identifier programming. These
"factory reset" operations are sometimes triggered by the same
register that triggers a soft reset, with a different bit
pattern. Use only the soft-reset pattern that the vendor's
driver uses on attach, and do not experiment with bit patterns
that you have not seen the vendor's driver use.

**EEPROM writes.** Similar to flash, EEPROM contains long-term
configuration. EEPROM writes are usually orchestrated through a
specific protocol of register writes, and the wrong protocol can
corrupt EEPROM state. Avoid EEPROM writes during exploration; if
you must perform them, do so only with a clear understanding of
what value should end up in EEPROM and a way to verify the
result.

**Modifying device identifiers.** Some devices store their
vendor and device IDs in flash or EEPROM, and a driver bug can
overwrite them. A device whose ID has been changed will not be
recognised by any driver that matches on the original ID, and
recovering it can require physical reprogramming with a hardware
programmer. Do not write to any register that is plausibly
related to device identification.

**Power management state changes.** Some devices have power
states whose transitions are managed by complex sequences of
register writes. The wrong sequence can leave the device in a
state from which it cannot easily recover, sometimes requiring
power cycling the host or even removing the device physically.
Power management is one of the most fragile areas of any
device's interface.

**PHY or PLL configuration.** Devices that include their own
clock generation (PCI Express devices in particular) have PLL
configuration that, if set wrong, can leave the device unable to
communicate at all. PHY configuration on networking devices has
similar pitfalls. These are subsystems where the vendor's driver
must be followed exactly, because there is no good way to recover
from an incorrect setting through any further configuration.

**Bus master enable on a misconfigured DMA address.** If you
enable bus mastering on a device that has a DMA address pointing
to a wild memory location, the device may read or write that
location and corrupt the host's memory. This is one of the few
ways a driver bug can crash the host through hardware action.
Always set up valid DMA mappings before enabling bus mastering,
and never enable bus mastering if you are not yet ready to manage
DMA.

These categories are not exhaustive, but they cover the most
common ways to damage a device. The general lesson is: respect
the operations whose consequences you do not fully understand,
and do not experiment with them.

### 10.3 Soft Resets and Watchdog Timers

The constructive side of the safety discussion is what to do when
something does go wrong. The first technique is the soft reset.

A soft reset is the device's "I have got into a bad state, please
restart" mechanism. The pattern is the same as the reset you
implemented in Section 6: a write to the control register that
puts the device back into its known initial state. Used
conservatively, a soft reset can recover from many kinds of
problems without requiring intervention.

The pattern in code is straightforward:

```c
static void
mydev_recover(struct mydev_softc *sc)
{
    device_printf(sc->sc_dev, "recovering device\n");
    mydev_reset(sc);
    /* Reapply any necessary configuration. */
    mydev_init_after_reset(sc);
}
```

The recovery routine should be called from any code path that
detects something has gone wrong: a timeout, an error status, an
unexpected register value. The cost of an unnecessary reset is
small; the cost of an unrecovered error can be much larger.

A watchdog timer is the next layer of defence. The driver
periodically checks that the device is making progress, and if
progress has stalled, it triggers a recovery. The pattern is:

```c
static void
mydev_watchdog(void *arg)
{
    struct mydev_softc *sc = arg;
    uint32_t counter;

    mtx_lock(&sc->sc_mtx);
    counter = bus_read_4(sc->sc_mem, MYDEV_REG_COUNTER);
    if (counter == sc->sc_last_counter) {
        sc->sc_stall_ticks++;
        if (sc->sc_stall_ticks >= MYDEV_STALL_LIMIT) {
            device_printf(sc->sc_dev, "device stalled, resetting\n");
            mydev_recover(sc);
            sc->sc_stall_ticks = 0;
        }
    } else {
        sc->sc_stall_ticks = 0;
    }
    sc->sc_last_counter = counter;
    callout_reset(&sc->sc_watchdog, hz, mydev_watchdog, sc);
    mtx_unlock(&sc->sc_mtx);
}
```

The watchdog reads a counter that the device should be
incrementing during normal operation. If the counter does not
change for several iterations, the watchdog assumes the device
has stalled and triggers recovery. The pattern is robust against
transient slowdowns (a single missed increment is tolerated) but
catches genuine stalls within a few seconds.

### 10.4 Backing Up Firmware Before Doing Anything Risky

For devices with firmware (which is most modern devices), the
firmware itself should be backed up before any risky operation.
If your captures show that the firmware is loaded by the host
during initialisation, then the firmware image is presumably
already available somewhere as a file, and you can keep a copy.
If the firmware lives in flash on the device and is not loaded
from the host, you have to read it back through whatever
mechanism the device exposes for reading flash, and store the
result.

The backup is your insurance against the case where some
experiment overwrites the firmware. With the backup, you can
restore the original. Without the backup, you may have a brick.

The discipline of backup-before-risky-operation is the same
discipline as backup-before-risky-system-administration. The
cost is small, the value when it matters is enormous, and the
people who skip it are the people who, sooner or later, regret
having skipped it.

### 10.5 Read-Only Probes Where Possible

Whenever an experiment can be performed read-only, perform it
read-only. The information gained is usually worth far more than
the time saved by writing instead of reading. Section 4's
register-mapping work was almost entirely read-only for a reason:
reading is safe, writing is not.

A useful pattern when you must investigate the effect of writes
is to read the value, compute what you expect the new value to
be, perform the write, read the value again, and verify that the
result matches your expectation. The verification step lets you
catch the case where the write did not have the effect you
expected, before you base further work on a wrong assumption.

```c
static int
mydev_set_field(struct mydev_softc *sc, bus_size_t off,
    uint32_t mask, uint32_t value)
{
    uint32_t old, new, readback;

    old = bus_read_4(sc->sc_mem, off);
    new = (old & ~mask) | (value & mask);
    bus_write_4(sc->sc_mem, off, new);
    readback = bus_read_4(sc->sc_mem, off);
    if ((readback & mask) != (value & mask)) {
        device_printf(sc->sc_dev,
            "set_field off=0x%04jx mask=0x%08x value=0x%08x "
            "readback=0x%08x mismatch\n",
            (uintmax_t)off, mask, value, readback);
        return (EIO);
    }
    return (0);
}
```

The helper performs the read-modify-write-verify cycle in one
place, with logging of mismatches. Used consistently, it catches
the cases where a write did not stick (because the register is
read-only, because the write encoded the value differently than
you expected, or because the device's state did not allow the
change at this moment) before they cause downstream confusion.

### 10.6 Safe Probing Wrappers

The companion code in
`examples/part-07/ch36-reverse-engineering/lab05-safe-wrapper/`
contains a small library of safe probing wrappers that combine
several of the techniques discussed here: timeouts on every
operation, automatic recovery on detected stalls, read-only
modes, and per-operation logging. The wrappers add a few hundred
microseconds to each operation, which is irrelevant during
exploration, and they catch the great majority of safety
problems before the problems become harmful.

The pattern is recommended for all exploratory drivers. The
production driver, when it eventually exists, can drop the
wrappers in favour of more efficient direct accesses, but during
exploration the wrappers are worth their cost in safety.

### 10.7 Recognising When to Stop

The hardest safety decision is sometimes the decision to stop.
When an experiment is not behaving as expected, when the device
is producing values you cannot explain, when your hypotheses are
all wrong and you do not yet have new ones, the temptation is to
keep poking until something becomes clear. Resist this temptation.
Stop, take a break, look at the captures again, talk to someone
else about what you have observed, and let the situation
clarify before you resume.

The reason is that the moments of confusion are exactly the
moments when accidental damage is most likely. A clear mind is
careful about which writes are safe; a frustrated mind tries
things that look promising and discovers, too late, that one of
them was destructive.

A short break also lets your subconscious work. Many of the most
useful reverse-engineering insights arrive while you are doing
something else: walking, sleeping, working on an unrelated
problem. The brain has a remarkable ability to integrate
observations into coherent understanding when given a chance.
Forcing yourself to stay at the keyboard when the work has
stalled often produces nothing useful and sometimes produces
disasters.

### 10.8 Wrapping Up Section 10

Safety in reverse engineering is built on a small set of
disciplines: never write without strong evidence, recognise the
operations that deserve particular caution, build in soft-reset
recovery and watchdog timers, back up firmware before risky
operations, prefer read-only experiments, use safe probing
wrappers, and know when to stop. None of these disciplines is
complicated. All of them are necessary. A reverse engineer who
follows them is unlikely to damage hardware. A reverse engineer
who ignores them is, sooner or later, certain to.

The next section closes the technical part of the chapter by
discussing how to turn the body of work the previous sections
have built up into a maintainable driver and a usable
pseudo-datasheet. The reverse-engineering process that started
with observations and hypotheses ends with a driver and a
document, and how those final artifacts are constructed determines
how useful the project will be to its future readers.

## 11. Refactoring and Documenting the Reversed Device

The end of a reverse-engineering project is not the moment when
the driver works. It is the moment when the driver and the
pseudo-datasheet together can be handed to another engineer who
can read them and understand both the device and the
implementation. Until that moment, the project is incomplete,
even if the driver appears to function. This section walks through
the work of consolidating the project's findings into a
maintainable form.

The work has two parts. The driver itself needs to be cleaned up,
restructured to follow normal driver-writing conventions, and
documented in the way any FreeBSD driver should be documented.
The pseudo-datasheet, which has been growing throughout the
project as a notebook of facts, needs to be reorganised into a
standalone document that someone who has never seen the project
can read and learn from.

### 11.1 The Driver Cleanup

A reverse-engineered driver, in its working-but-not-cleaned-up
state, typically carries traces of its history: comments
referring to "the suspected control register", code blocks that
test multiple hypotheses with conditional compilation, debugging
sysctls that were added during a specific investigation, log
messages that were useful at the time but no longer needed.
Cleaning up means removing the historical noise while preserving
the substantive content.

The cleanup checklist:

1. Remove every conditional compilation block that was used to
   test alternatives. The chosen alternative should remain; the
   alternatives should be deleted.
2. Replace speculative names with confirmed names. If a register
   was originally named `MYDEV_REG_UNKNOWN_40` and is now known
   to be the channel-selection register, rename it to
   `MYDEV_REG_CHANNEL`.
3. Replace investigation comments with explanatory comments. A
   comment that says "I think this might be the polling delay" is
   a hypothesis comment from the investigation phase; replace it
   with a comment that says "Wait for the device to complete the
   reset" once the hypothesis is confirmed.
4. Remove debugging sysctls that are no longer useful, and keep
   the ones that operators or maintainers will want.
5. Remove `device_printf` calls that were valuable during
   investigation but are now noise in production.
6. Verify that the detach path is clean. A driver that loads
   cleanly but fails to unload cleanly is a maintenance problem.
7. Verify that all error paths free their resources correctly. A
   driver that leaks resources on error is a problem that will
   eventually surface.

The companion code in
`examples/part-07/ch36-reverse-engineering/` includes both the
investigation-phase scaffolding and the cleaned-up form, so that
you can see the difference. The cleaned-up form is what would be
suitable for inclusion in the FreeBSD source tree (we will
discuss inclusion in Chapter 37).

### 11.2 Driver Documentation

A FreeBSD driver should have at least a manual page in section 4,
and ideally a section in the kernel-developer's documentation as
well. The manual page is for users who want to know what the
driver does and how to configure it. The developer documentation
is for kernel hackers who want to understand the driver
internally.

The manual page follows the standard FreeBSD style. The relevant
manual page for `style(4)` documents the conventions, and the
manual pages for similar drivers are good examples to follow.
A typical driver manual page covers:

- The name of the driver and a one-line description.
- The synopsis showing how to load the driver in `loader.conf`.
- The description, explaining what hardware the driver supports
  and what features it provides.
- The hardware support section, listing the specific devices the
  driver works with.
- The configuration section, describing any sysctls or loader
  variables the driver exposes.
- The diagnostics section, listing the messages the driver may
  produce and what they mean.
- The see-also section, with cross-references.
- The history section, explaining when the driver appeared.
- The author section.

For a reverse-engineered driver, the description should be
honest about the driver's provenance: it was developed by reverse
engineering, the supported feature set is what was achievable
through that process, and certain corners of the device may
behave differently than the driver expects. Operators appreciate
honest documentation; vague claims of full support set up users
for confusion when they encounter unimplemented behaviour.

### 11.3 The Pseudo-Datasheet Structure

The pseudo-datasheet is the standalone document that captures
everything you have learned about the device, in a form that
someone who has never seen your driver can read and understand.
A well-structured pseudo-datasheet often becomes the most
referenced document in any project that uses the device, because
it answers questions that the driver source code cannot answer
without close reading.

A practical pseudo-datasheet structure looks like this:

```text
1. Identity
   1.1 Vendor and device IDs
   1.2 USB descriptors (if applicable)
   1.3 Class codes (if applicable)
   1.4 Subsystem identifiers (if applicable)
   1.5 Hardware revisions covered

2. Provenance
   2.1 Sources consulted
   2.2 Methodology used
   2.3 Verification status of each fact (high / medium / low)

3. Resources
   3.1 Memory regions and their sizes
   3.2 I/O ports (if applicable)
   3.3 Interrupt assignment

4. Register Map
   4.1 Register list with offsets and short descriptions
   4.2 Per-register details: size, access type, reset value, fields

5. Buffer Layouts
   5.1 Ring buffer layouts
   5.2 Descriptor formats
   5.3 Packet formats

6. Command Interface
   6.1 Command sequencing
   6.2 Command list with formats and responses
   6.3 Error reporting

7. Initialization
   7.1 Cold attach sequence
   7.2 Warm reset sequence
   7.3 Required register settings before operation

8. Operating Patterns
   8.1 Data flow
   8.2 Interrupt handling
   8.3 Status polling

9. Quirks and Errata
   9.1 Known bugs in the device
   9.2 Workarounds in the driver
   9.3 Edge cases that have not been fully characterised

10. Open Questions
    10.1 Behaviours observed but not understood
    10.2 Registers whose purpose is not yet known
    10.3 Operations not yet investigated

11. References
    11.1 Prior work consulted
    11.2 Related drivers in other operating systems
    11.3 Public documentation, if any
```

The structure is comprehensive, and not every project will fill
every section. Sections that are not relevant can be omitted; the
template is a checklist of "what would be worth documenting if
the relevant information existed", not a requirement to invent
information that does not exist.

The most important sections are the Provenance, the Register Map,
and the Open Questions. Provenance lets readers evaluate the
trustworthiness of the document. The Register Map is the central
reference. Open Questions tells future contributors where the
work needs more attention.

### 11.4 The Provenance Section in Detail

The Provenance section deserves special attention because it is
how a pseudo-datasheet establishes its credibility. The section
should answer:

- What sources of information were used? (Captures, prior code,
  experiments, public documentation.)
- What methodology was applied to each source? (Direct reading,
  diffing, statistical analysis.)
- Which facts come from which sources?
- For each fact, what is the verification status?

A useful convention is to label each substantive fact in the
register map and elsewhere with a short verification tag:

- **HIGH**: confirmed by multiple independent observations and
  by experiment.
- **MEDIUM**: confirmed by a single source or a single
  experiment.
- **LOW**: hypothesis based on inference, not yet directly
  tested.
- **UNKNOWN**: stated for completeness, but with no evidence.

Readers can then weight each fact by its verification status, and
contributors can prioritise where to invest more investigation.
The convention costs little to maintain and gives the document a
much more honest character than a flat statement of facts that
treats all claims as equal.

### 11.5 Registers as Tables

The register map itself is best presented as a table. For each
register, the table should give:

- Offset within the register block.
- Symbolic name (the macro name in the driver).
- Size (8, 16, 32, or 64 bits, usually).
- Access type (read-only, read-write, write-only, write-1-to-clear,
  and so on).
- Reset value.
- One-line description.

A separate, more detailed entry for each register lists the
fields within the register. For example:

```text
MYDEV_REG_CONTROL (offset 0x10, RW, 32 bits, reset 0x00000000)

  Bits  Name         Description
  --------------------------------------------------
  0     RESET        Write 1 to trigger reset.
  1     ENABLE       Set to enable normal operation.
  2-3   MODE         Operating mode (0=idle, 1=rx, 2=tx, 3=both).
  4     INT_ENABLE   Enable interrupts globally.
  31:5  reserved     Read as zero, write as zero.
```

This table format is the FreeBSD convention. It scales well, it
is easy to maintain in Markdown, and it is what readers expect.

### 11.6 Pseudo-Datasheet as a Living Document

The pseudo-datasheet is rarely complete. As the driver evolves,
new behaviour is discovered, old facts are refined, edge cases
are characterised. The Git-tracked Markdown form lets the
document evolve naturally, with each commit explaining what was
learned and when.

The discipline that supports this is to update the
pseudo-datasheet first, then update the driver to match. The
pseudo-datasheet is the specification; the driver is the
implementation. When you discover that a register's bit-3 has a
purpose you did not previously know, write the new bit-3
description into the pseudo-datasheet first, with provenance,
and then update the driver to use it. The order matters because
it forces you to think about the change as a fact about the
device, separately from a change to your code.

Over time, the pseudo-datasheet becomes the trustworthy
artifact, and the driver becomes one of several possible
implementations of the contract the document specifies. New
implementations (in NetBSD, in Linux, in custom embedded code)
can be written from the pseudo-datasheet alone, without
re-deriving everything from scratch.

### 11.7 Versioning the Driver

The book's worked examples use version strings such as `v2.5-async`
to mark major iterations of a driver. For reverse-engineered
drivers, a useful convention is to use a `-rev` suffix to
indicate the reverse-engineered nature of the work, with version
numbers tracking the maturity of the implementation:

- `v0.1-rev`: minimal driver, reset and identifier only.
- `v0.2-rev`: read path implemented and verified.
- `v0.5-rev`: most operations implemented, some quirks
  understood.
- `v1.0-rev`: full functionality, all known quirks handled.
- `v2.1-rev`: a stable, mature, refactored driver suitable for
  general use.

The version string can appear in the manual page, in a
`MODULE_VERSION` macro, and in the pseudo-datasheet. It tells
operators what level of support to expect from any given
build.

### 11.8 Wrapping Up Section 11

We have seen how to consolidate the work of a
reverse-engineering project into a maintainable driver and a
standalone pseudo-datasheet. The driver is cleaned up, documented
in the manual page, and versioned to indicate its maturity. The
pseudo-datasheet captures what was learned about the device, with
provenance, in a structured form that future contributors can
extend. Together, the two artifacts are what justifies the
considerable investment that the reverse-engineering project
required.

Before turning to hands-on practice, two shorter sections round
out the theoretical material. Section 12 revisits the legal and
ethical framework from Section 1 with a practical eye, giving you
a compact set of rules for licence compatibility, contractual
restrictions, clean-room discipline, and safe-harbour activities.
Section 13 then walks through two worked case studies from the
FreeBSD tree itself, showing how the techniques of the chapter
appear in drivers that are in production today. After those two
sections, the remaining material of the chapter gives you labs to
apply what you have learned, challenge exercises to stretch your
understanding, troubleshooting notes to help when things go
wrong, and a forward-looking transition to Chapter 37, where we
will see how to take a driver like the one you have just built
and submit it for inclusion in the FreeBSD source tree.

## 12. Legal and Ethical Guardrails in Practice

Section 1 opened this chapter by sketching the legal and ethical
landscape in which reverse engineering for FreeBSD happens. That
sketch was deliberately broad, because it had to introduce
concepts like fair use, interoperability research, and clean-room
method before the reader had seen any of the technical work the
rest of the chapter covers. Now, at the end of that technical
work, it is worth taking a second, more practical pass. The
goal of this section is not to turn you into a lawyer. It is to
give you a small set of habits that protect you, the project, and
the reader of your code from the predictable ways a
reverse-engineering effort can go wrong. Each habit is concrete,
each one can be documented inside your pseudo-datasheet from
Section 11, and each one is directly informed by how the FreeBSD
tree has handled similar questions in the past.

### 12.1 Why a Second Legal Section

Section 1 answered the question "is this allowed?". Section 12
answers the question "how do I do it in a way that stays
allowed?". The distinction matters, because many of the
activities that are legal in principle become risky in practice
if they are done without structure. Reading another driver to
understand a chip is legal. Reading another driver and then
writing your own from memory, without any intermediate document
to show where your understanding came from, looks the same from
the outside but is much harder to defend if a question is ever
raised. Treating a proprietary datasheet as a reference is legal.
Quoting long passages from that datasheet into your driver's
source comments is not. The difference in both cases is process,
not intent.

The rest of this section works through four concrete areas:
licence compatibility (what you are allowed to copy), contractual
restrictions (what you are allowed to disclose), clean-room
practice (how to keep interoperability work defensible), and
safe-harbour activities (what is always permitted). None of the
content below is a substitute for legal advice on a specific
situation; it is instead the common discipline that experienced
FreeBSD developers already follow, written down in one place so
that a new author can adopt it without having to reconstruct it.

### 12.2 Licence Compatibility

The FreeBSD kernel uses a permissive BSD-style licence. When you
pull knowledge from another codebase into your driver, the
licence of that other codebase constrains what you are allowed to
copy, even if it does not constrain what you are allowed to
learn. The categories that come up in practice are BSD, GPL,
CDDL, and proprietary, and each has a different rule.

**BSD-licensed sources.** Drivers that already live in the
OpenBSD or NetBSD trees, or in older FreeBSD ports of the same
device, use a permissive licence that is compatible with the
FreeBSD kernel. You can copy code directly, with appropriate
attribution in the copyright block. The `zyd` wireless driver at
`/usr/src/sys/dev/usb/wlan/if_zyd.c` is a concrete example: the
file header preserves the original `$OpenBSD$` and `$NetBSD$`
tags at the top of the source, indicating that the code was
ported from those trees, and Damien Bergamini's BSD-style
copyright is intact alongside the copyrights of later FreeBSD
contributors. When the licence is compatible, moving code between
trees is a legitimate way to amortise work across the BSD
ecosystem, and it is already part of FreeBSD's routine practice.

**GPL-licensed sources.** Linux drivers are almost always
licensed under the GPL. You are allowed to read GPL code to
understand how a device works, because reading is not copying.
You are not allowed to paste GPL code into a BSD-licensed driver,
even in small amounts, even temporarily, even with attribution.
The FreeBSD project's position is clear: the kernel does not
accept code of incompatible licence. A driver that was reviewed
and found to contain copied GPL code would be rejected, and the
maintainer would lose standing in future reviews. The rule is
strict because the cost of relaxing it would be the licence
integrity of the tree.

**CDDL-licensed sources.** Some device drivers in the
OpenSolaris and Illumos trees are released under the CDDL. The
CDDL is file-level copyleft, which means that you cannot freely
intermix CDDL source and BSD source within the same file. The ZFS
code in FreeBSD is a well-known accommodation of this rule: CDDL
files are kept in their own directory under
`/usr/src/sys/contrib/openzfs/`, their licence header is
preserved, and BSD-licensed glue code around them sits in
separate files. For a device driver that structure is rarely a
reasonable workflow, so the safer rule is that CDDL code is look
but do not copy in the same way as GPL code.

**Proprietary sources.** Vendor SDKs, reference drivers, and
sample code are usually distributed under a proprietary licence
that forbids redistribution. Even when the vendor encourages you
to use the code as a reference, that permission is not
transferable to the FreeBSD source tree, because the vendor
cannot speak for every downstream user of FreeBSD. Reading a
proprietary driver to understand a device is generally acceptable
to a limited extent, but you cannot paste from it, and in many
jurisdictions you cannot quote from it at length either.

The practical rule is straightforward: treat anything that is not
plain BSD as read-only. If you want bytes in your driver, those
bytes must either come from your own typing or from a source
whose licence the FreeBSD project already accepts. When you make
this rule explicit in your pseudo-datasheet, by writing next to
each register description where the information was observed, you
create a record that will still be useful years later when a new
maintainer needs to know how the file came to look the way it
does.

### 12.3 Contractual Restrictions

Contracts can bind you in situations where copyright law does
not. The three kinds of contract that come up in
reverse-engineering work are non-disclosure agreements,
end-user licence agreements, and firmware licences. Each of them
constrains a different part of the workflow.

**Non-disclosure agreements.** Some vendors will share detailed
documentation in exchange for a signed NDA. The documentation is
often excellent, but the NDA typically forbids publication of the
information, sometimes indefinitely, and sometimes with
liquidated damages attached. Signing an NDA about a device and
then writing an open-source driver for that same device is a
legal minefield: every register you document in a pseudo-datasheet,
every field name, every value, could be challenged later as a
disclosure. The safer choice is almost always to refuse the NDA
and work from publicly observable information. If you have
already signed one and then want to contribute an open driver,
the clean option is to recuse yourself from the observation work
and let a second author build the pseudo-datasheet from scratch,
using only sources you never touched under the agreement.

**End-user licence agreements.** Many vendor-supplied drivers
and SDKs include an EULA that explicitly forbids reverse
engineering. The enforceability of such clauses varies across
jurisdictions, but the safer path is to avoid clicking "I agree"
in the first place. If you only ever touched the vendor driver as
a binary shipped on an installation image, without accepting an
online agreement, your position is stronger than if you signed up
for a developer programme and downloaded the SDK under an
agreement that forbade disassembly. Record which vendor materials
you consulted and under what terms. That record becomes part of
the provenance field in your pseudo-datasheet.

**Firmware licences.** Many modern devices need a binary firmware
blob that the driver loads at attach time. The vendor usually
distributes that blob under a redistribution licence that is more
permissive than the surrounding driver code but still not fully
free. FreeBSD's `ports` tree has a well-established pattern of
packaging firmware blobs in dedicated `-firmware-kmod` ports so
that the kernel code stays BSD while the blob keeps its vendor
licence. You do not need to reverse-engineer the firmware itself;
you need to understand how the driver hands it to the device. The
legal framework for firmware is separate from the legal framework
for the driver, and both have to be satisfied independently.

### 12.4 Clean-Room Practice

Reading a GPL driver to learn what a device does, and then
writing BSD code that does the same thing, is a recognised
interoperability technique. United States courts have upheld it
in cases such as Sega v. Accolade and Sony v. Connectix, and
Article 6 of the European Union's Software Directive
(2009/24/EC) carves out reverse engineering specifically for
interoperability. But the legal protection depends on how you do
the work, not just on what you do. A driver that looks
line-for-line like the Linux original will not be protected by
the clean-room defence, because the record will show that no
clean room existed.

Good clean-room practice has two elements: structural separation
and structural documentation.

Structural separation means that the person who reads the
other-platform driver, and the person who writes the FreeBSD
driver, are different activities. On a team of two, they are
literally different people. On a solo project, they are
different work sessions with different artifacts. The reader's
output is a pseudo-datasheet: a document in plain English that
describes registers, bit fields, command sequences, and quirks,
with no reference to the specific identifiers the other driver
used. The writer's input is that pseudo-datasheet, plus the
FreeBSD header files and driver examples. The writer never opens
the other-platform driver while the FreeBSD code is being
written.

Structural documentation means that the pseudo-datasheet records
where each fact came from. A register description might be
annotated "observed on wire with `usbdump(8)` on 2026-02-14" or
"inferred from vendor datasheet section 3.4" or "deduced from
Linux driver, reread into prose". That annotation is the record
that would, if ever needed, support the claim that your driver
was written from understanding rather than from copying. Patent
and copyright cases have sometimes turned on the quality of
exactly this kind of record, and maintaining it is not paranoia;
it is the same discipline that keeps the rest of your engineering
honest.

In practice, the time cost of clean-room discipline is small once
the pseudo-datasheet habit from Section 11 is already part of
your workflow. The legal value is large, because it moves you
from "I hope this is not a problem" to "I can show how it was
done".

### 12.5 Safe Harbour

It helps to know what is always safe, so that you can push the
uncertain cases through the structure of this section without
second-guessing every keystroke. The following activities are
defensible in every jurisdiction the FreeBSD project cares about,
and you can perform them without hesitation.

**Reading code is always safe.** Whatever licence the
other-platform driver carries, reading it to learn how a device
works is not a copyright violation. It is sometimes called fair
use, sometimes called interoperability research; the label
depends on the jurisdiction, but the principle is stable in every
legal system that the FreeBSD project is likely to encounter.

**Observing your own hardware is always safe.** Running a device
you own on a machine you own and recording what happens on the
bus is not actionable under copyright, contract, or trade-secret
law. The recordings you produce from `usbdump(8)`, from
`pciconf(8)`, from JTAG probes, and from logic analysers are your
own work, and you may describe and publish them freely.

**Writing from understanding is always safe.** If you can
describe the device in your own words, in a pseudo-datasheet, you
can then write a FreeBSD driver from that document without
restriction. The writing is yours, derived from facts about the
device rather than from the expression of another author.

**Publishing interoperability information is generally safe.**
Documenting a register layout, a command sequence, or a wire
protocol is publishing facts about a device, not publishing
someone else's code. Even when a vendor dislikes the publication,
they rarely have a legal basis to prevent it, and courts in both
the United States and the European Union have consistently
supported interoperability research.

Activities outside this list are not automatically unsafe, but
they deserve deliberate thought. When in doubt, widen your clean
room, strengthen your documentation, and ask for a second opinion
on the `freebsd-hackers` mailing list or in a project chat
channel before committing. The maintainers have seen these
questions many times, and the cost of asking is low.

### 12.6 Wrapping Up Section 12

We have taken a practical second pass over the legal and ethical
framework introduced in Section 1. Licence compatibility dictates
what you can copy; contractual restrictions dictate what you can
disclose; clean-room practice keeps interoperability research
defensible; and a short list of always-safe activities gives you
room to work without continuous worry. The habits are simple to
adopt once the pseudo-datasheet from Section 11 is already part
of your workflow, because the pseudo-datasheet is itself the
audit trail of the clean room.

With this foundation in place, we are ready to look at how the
FreeBSD tree itself documents reverse-engineering work. The next
section presents two worked case studies, both drawn directly
from the current source, where the driver authors recorded their
reasoning in comments that are still readable today.

## 13. Case Studies From the FreeBSD Tree

We have now covered the techniques, the documentation discipline,
and the legal framework. It is time to look at two real drivers
that were written under exactly these constraints and see how
their authors handled the work. Both drivers are in the FreeBSD
14.3 tree right now. Both have header comments and inline notes
that record, in the author's own words, what the datasheet failed
to say and what the driver does about it. Reading those comments
gives you a direct view of the reverse-engineering discipline as
it appears in production code, with no retouching and no
retrospective polish.

### 13.1 How to Read These Case Studies

For each driver we will look at four things. First, a short
description of the device and its history, so that you know what
kind of hardware is involved and roughly when the work was done.
Second, the approach the author used to work around missing
documentation, including what sources were consulted and what
observations were made. Third, the specific code that encodes the
finding, with exact file paths, function names, and the register
or constant identifiers that the driver uses today. Fourth, the
ethical and legal context in which the work was done, including
how the licence of each source shaped what the author was allowed
to write. A closing paragraph then translates the historical
method into its modern form, so that you can see how you would
approach the same problem today with the techniques from earlier
in this chapter.

None of the history below is speculative. Every claim is anchored
in a file you can open on your own FreeBSD system, and every fact
about the code is directly observable in the current source. If
anything here drifts out of date in a future release, the files
themselves will still be the source of truth, and the method of
reading them will still apply.

### 13.2 Case Study 1: The `umcs` Driver and Its Undocumented GPIO

**Device and history.** The MosChip MCS7820 and MCS7840 are
USB-to-serial bridge chips that appear in inexpensive multi-port
RS-232 adapters. The MCS7820 is a two-port part, the MCS7840 is a
four-port part, and the USB interface is electrically the same
in both cases. The FreeBSD driver, `umcs`, was written by Lev
Serebryakov in 2010 and now lives at
`/usr/src/sys/dev/usb/serial/umcs.c`. The companion header,
`/usr/src/sys/dev/usb/serial/umcs.h`, spells out the register
layout.

**Approach.** The header comment at the top of `umcs.c` is
unusually candid about the documentation situation. The author
writes that the driver supports the mos7820 and mos7840 parts,
and notes directly that the public datasheet does not contain
full programming information for the chip. A full datasheet,
distributed under restriction by MosChip technical support,
filled in some of the gaps, and a vendor-supplied reference
driver filled in the rest. The author's task was to write an
original BSD driver that behaved the same way the confirmed
information said it should, with the vendor driver used as an
observational check rather than as a source to copy.

The cleanest place to see this discipline at work is the
port-count detection inside the attach routine. A program that
drives a USB-to-serial chip must know whether the chip has two
ports or four, because the user-visible device nodes and the
internal data structures depend on that count. The datasheet
prescribes one method, the vendor driver uses another, and
experiments on real hardware show that the datasheet's method is
unreliable.

**Code.** In `/usr/src/sys/dev/usb/serial/umcs.c`, the function
`umcs7840_attach` performs the detection. The inline comment
records the problem in plain text, and the code records the
chosen workaround. The relevant fragment reads:

```c
/*
 * Documentation (full datasheet) says, that number of ports
 * is set as MCS7840_DEV_MODE_SELECT24S bit in MODE R/Only
 * register. But vendor driver uses these undocumented
 * register & bit. Experiments show, that MODE register can
 * have `0' (4 ports) bit on 2-port device, so use vendor
 * driver's way.
 */
umcs7840_get_reg_sync(sc, MCS7840_DEV_REG_GPIO, &data);
if (data & MCS7840_DEV_GPIO_4PORTS) {
```

The register involved is `MCS7840_DEV_REG_GPIO`, defined at
offset 0x07 in `/usr/src/sys/dev/usb/serial/umcs.h`. The header
is equally candid about its status. The register block as a whole
is annotated with a note explaining that the registers are
documented only in the full datasheet, which can be requested
from MosChip technical support, and the GPIO register
specifically is commented as holding the GPIO_0 and GPIO_1 bits
that are undocumented in the public datasheet. A longer note
further down the header explains that `GPIO_0` must be grounded
on two-port boards and pulled up on four-port boards, and that
the convention is enforced by board designers rather than by the
chip itself. The single-bit flag `MCS7840_DEV_GPIO_4PORTS`,
defined as `0x01`, is what the attach routine tests against the
value returned by `umcs7840_get_reg_sync`.

What makes this a good study is that the code records the story.
A reader who finds the driver fifteen years after it was written
can reconstruct the reasoning: the datasheet said one thing, the
vendor driver did another, the real hardware was tested, and the
chosen method is explained in the comment. No one has to
rediscover the same dead end.

**Ethical and legal context.** The author had two classes of
source available: the restricted full datasheet, which MosChip
distributes on request, and the vendor reference driver, which
ships with the chip evaluation kit. Both are proprietary in the
sense that they are not freely redistributable, and neither can
be copied into an open-source driver. What you can do is learn
from them, and then write your own code against the information
they contain. The `umcs` comment does exactly that. It names the
behaviour the vendor driver exhibits, it names the experiment
that confirmed the behaviour on real hardware, and the code it
produces is original BSD-licensed prose. The copyright block at
the top of the source file identifies Lev Serebryakov as the sole
author. This is a textbook clean-room outcome: the driver
benefits from vendor information without importing vendor code.

**Modern replication.** If you were writing `umcs` today, the
workflow would be the same, with better tooling. You would
capture a `usbdump(8)` trace of the vendor driver's attach
sequence to see the exact GPIO read, you would run the same read
on your own hardware across known two-port and four-port boards,
and you would record the discrepancy between the datasheet and
the observed behaviour in your pseudo-datasheet with a clear
provenance line for each source. The driver would then implement
the observed method, with a comment identical in spirit to the
one Lev Serebryakov wrote. The technique has not changed; the
tools around it have improved.

### 13.3 Case Study 2: The `axe` Driver and the Missing IPG Initialisation

**Device and history.** The ASIX AX88172 is a USB 1.1 Ethernet
adapter chip, and the AX88178 and AX88772 are its USB 2.0
descendants. Cheap Ethernet dongles based on these parts have
been common since the early 2000s, and the FreeBSD driver,
`axe`, lives at `/usr/src/sys/dev/usb/net/if_axe.c`. The original
code was written by Bill Paul between 1997 and 2003, and the
AX88178 and AX88772 support was backported from OpenBSD by
J. R. Oldroyd in 2007. The register header lives alongside the
driver at `/usr/src/sys/dev/usb/net/if_axereg.h`.

**Approach.** The top-of-file comment block in
`/usr/src/sys/dev/usb/net/if_axe.c` reads, in part, that there
is information missing from the chip manual which the driver must
know in order for the chip to function at all. The author lists
two specific facts. A bit must be set in the RX control register
or the chip will receive no packets at all. The three
inter-packet-gap registers must all be initialised, or the chip
will send no packets at all. Neither of these requirements
appears in the public datasheet, and both were established by
reading the vendor's Linux driver and observing the chip on real
hardware.

The story of the IPG registers is especially clear. Inter-packet
gap is a standard Ethernet concept: the transmitter must wait a
minimum number of bit times between frames, and the exact number
depends on the link speed. A silicon designer has several ways
to expose IPG to software, and the AX88172 chose to expose three
distinct registers that the driver must write during
initialisation. The datasheet names the registers but says
nothing about the need to program them, so a naïve driver
following only the datasheet would leave them at their reset
values and find that the chip, mysteriously, refuses to transmit.

**Code.** The initialisation sequence appears inside the function
`axe_init` in `/usr/src/sys/dev/usb/net/if_axe.c`. A short helper
called `axe_cmd` provides the standard way to issue a
vendor-specific USB control request, and `axe_init` calls it
once for each IPG write. The relevant branch reads:

```c
if (AXE_IS_178_FAMILY(sc)) {
    axe_cmd(sc, AXE_178_CMD_WRITE_NODEID, 0, 0, if_getlladdr(ifp));
    axe_cmd(sc, AXE_178_CMD_WRITE_IPG012, sc->sc_ipgs[2],
        (sc->sc_ipgs[1] << 8) | (sc->sc_ipgs[0]), NULL);
} else {
    axe_cmd(sc, AXE_172_CMD_WRITE_NODEID, 0, 0, if_getlladdr(ifp));
    axe_cmd(sc, AXE_172_CMD_WRITE_IPG0, 0, sc->sc_ipgs[0], NULL);
    axe_cmd(sc, AXE_172_CMD_WRITE_IPG1, 0, sc->sc_ipgs[1], NULL);
    axe_cmd(sc, AXE_172_CMD_WRITE_IPG2, 0, sc->sc_ipgs[2], NULL);
}
```

The two branches expose a second piece of reverse-engineering
information that is invisible in the datasheet. On the older
AX88172, each of the three IPG values is programmed as an
independent command: `AXE_172_CMD_WRITE_IPG0`,
`AXE_172_CMD_WRITE_IPG1`, and `AXE_172_CMD_WRITE_IPG2`, defined
in `/usr/src/sys/dev/usb/net/if_axereg.h`. On the newer AX88178
and AX88772, a single command writes all three at once by packing
them into the same control request:
`AXE_178_CMD_WRITE_IPG012`. The two opcodes have the same
numeric value; the AX88178 reused the opcode slot that the
AX88172 used for writing a single IPG register, and expanded its
semantics to cover all three. A driver that treated the two chip
families as interchangeable would corrupt the IPG programming on
one of them. The only way to distinguish the families is the
macro `AXE_IS_178_FAMILY(sc)`, and the need for that macro is
itself a finding of the reverse-engineering work.

**Ethical and legal context.** The axe driver has a long history,
and its provenance is recorded in the copyright block. Bill Paul
wrote the original driver against the AX88172, and the comments
show that his information came from a mix of the public datasheet
and observational work. J. R. Oldroyd's AX88178 support was
ported from OpenBSD's sibling driver, which is BSD-licensed and
directly compatible with FreeBSD. That port was legally
straightforward: the code came from a permissive source tree, and
the copyright block was preserved across the move. The
reverse-engineering notes about IPG and RX-control were carried
along with the code, so the finding itself is documented forever
in the driver's header.

What did not happen is as informative as what did. The axe driver
does not import code from the Linux `asix_usb` driver, even
though that driver was available when Bill Paul was writing the
FreeBSD equivalent. Linux uses the GPL, and pasting from it would
have made axe unshippable under the kernel's licence rules.
Reading it for understanding is exactly what Bill Paul did, and
the FreeBSD driver is his own writing throughout.

**Modern replication.** If you were writing axe today, you would
start by loading the Linux driver on a Linux test machine and
running `usbdump(8)` on a FreeBSD machine that shared the same
USB hub, so that you could capture the vendor driver's attach
sequence without needing to read a single line of GPL code. The
`usbdump(8)` output would show the three IPG writes directly,
because they travel on the wire as visible USB control transfers.
You would record the observation in your pseudo-datasheet, cite
the packet-capture file for provenance, and implement the writes
in your own driver. The comment above `axe_init` would read very
much like the one Bill Paul originally wrote.

### 13.4 Shared Lessons

Reading the two drivers together surfaces a handful of lessons
that are worth naming explicitly.

**The comment is the audit trail.** In both `umcs` and `axe`, the
header and inline comments are the place where the
reverse-engineering story is preserved. Without those comments, a
future maintainer staring at `MCS7840_DEV_REG_GPIO` or at
`AXE_178_CMD_WRITE_IPG012` would have no way to know where the
information came from or why the driver does what it does. In
both cases the author took the extra minute to write down the
reasoning, and the result is a driver that remains maintainable
fifteen or twenty years after it was written. The author's
pseudo-datasheet, in each case, was partly absorbed into the
comments and partly preserved in the structure of the header
file. That absorption is what makes the code readable today.

**Observation wins over documentation.** Both drivers trust
observation over the datasheet when the two disagree. `umcs`
trusts the experimentally verified GPIO read over the datasheet's
MODE-register method, and `axe` trusts the observation that the
chip refuses to transmit without IPG programming over the
datasheet's silence on the subject. This is not a rejection of
documentation; both authors read the datasheets carefully. It is
a recognition that a datasheet is a description of the intended
behaviour, and the chip's actual behaviour is what the driver has
to match.

**Licence discipline is not an afterthought.** Both drivers
respect the licence of the sources they drew on. `umcs` uses a
proprietary reference driver as an observational check without
copying from it. `axe` imports AX88178 and AX88772 support from
OpenBSD, which is licence-compatible, and stays clear of the
Linux `asix_usb` driver, which is not. Neither driver includes
code that the FreeBSD project would have had to strip out during
review, and neither needed a lawyer's review before it was
committed. The habits from Section 12 were already part of how
the authors worked.

**The work is reproducible.** Everything both drivers did can be
reproduced today with modern tools. `usbdump(8)`, `usbconfig(8)`,
and the bus-space tracing techniques from earlier in the chapter
would give you the same information that the original authors
had to gather by hand, and the pseudo-datasheet habit from
Section 11 would give you a cleaner audit trail than a set of
inline comments. The drivers you write for obscure hardware today
will be better documented than the drivers from 2007 and 2010,
because the techniques have matured. The spirit of the work,
however, is the same.

### 13.5 Wrapping Up Section 13

Two case studies have shown what a disciplined
reverse-engineering effort looks like when it has been absorbed
into the FreeBSD tree. In both cases the author identified a
specific gap in the public documentation, confirmed the correct
behaviour through observation, recorded the finding in a comment
that is still readable today, and wrote original code under a
licence compatible with FreeBSD. The drivers work, the code is
maintainable, and the legal record is clean.

With the legal framework from Section 12 and the worked examples
from Section 13 in place, you now have both the principles and a
set of precedents to draw on. The remaining sections of the
chapter give you hands-on practice with the techniques that the
case studies illustrate, challenge exercises to push your
understanding further, troubleshooting notes for the common ways
the work can go wrong, and a closing bridge to Chapter 37.

## Hands-On Labs

The labs in this chapter give you safe, repeatable practice with
the techniques the chapter has covered. None of them touches real
unknown hardware in a way that could damage anything; the
"unknown device" in the labs is either a software mock or a
benign region of memory that you control. Treat each lab as a
chance to internalise one specific technique before adding it to
your kit.

The companion code lives in
`examples/part-07/ch36-reverse-engineering/`. Each lab subfolder
has its own `README.md` with step-by-step instructions, and the
code is organised so that you can build and run each lab
independently of the others. As with earlier chapters, type the
code yourself the first time you work through a lab; the
companion files are there as a reference and as a known-good
version to compare against.

### Lab 1: Identifying a Device and Dumping Descriptors

This lab is the simplest possible reverse-engineering exercise.
You will use `pciconf(8)` and `usbconfig(8)` to enumerate every
device on your FreeBSD system and to dump the static descriptors
of one device of your choice. The output of the lab is a small
text file that records, for one specific device, every public
fact that the kernel can tell you about it.

Steps:

1. Run `sudo pciconf -lvc` and save the output. Note any device
   that appears as `noneN@...`, indicating that no in-tree driver
   has claimed it.
2. Run `sudo usbconfig` and save the output. Pick one USB device
   that you understand (a USB stick, for example) and that you
   own physically.
3. Run `sudo usbconfig -d ugen0.X dump_device_desc` and
   `sudo usbconfig -d ugen0.X dump_curr_config_desc` for the
   chosen device, where `ugen0.X` is the device's `usbconfig`
   identifier.
4. Open the captured output and identify each field: bDeviceClass,
   idVendor, idProduct, bMaxPacketSize0, bNumConfigurations, the
   endpoint descriptors, and so on.
5. Write a one-page summary identifying the device's identity, its
   class, its endpoints, and any notable features.

The lab is intentionally easy. The point is to internalise the
shape of the static-identity capture before applying it to a
device whose identity is genuinely unknown.

### Lab 2: Capturing a USB Initialization Sequence

This lab moves from static identity to dynamic behaviour. You
will use `usbdump(8)` to capture the initialisation sequence of a
USB device, save the capture, and explore it in Wireshark.

Steps:

1. Identify a USB device that you can attach and detach freely (a
   USB stick is a good choice, because you can unplug and replug
   it without consequence).
2. Run `sudo usbdump -i usbus0 -w stick-init.pcap` to start
   capturing on the USB bus the device is attached to. Use the
   correct bus number for your system; `usbconfig` will tell you
   which bus the device is on.
3. With `usbdump` running, plug in the device. Wait for it to be
   recognised by the kernel. Then unplug it.
4. Stop `usbdump` with Control-C.
5. Open the resulting pcap file in Wireshark. You should see a
   series of USB transfers corresponding to enumeration: the
   GET_DESCRIPTOR requests for the device, configuration, and
   string descriptors, the SET_ADDRESS request, the
   SET_CONFIGURATION request, and so on.
6. Identify each transfer and write down what it does. Compare
   the captured device descriptor against the output of
   `usbconfig dump_device_desc` from Lab 1; they should match
   field for field.

This lab is the foundation of all USB reverse engineering. Every
USB device you ever investigate will go through an enumeration
sequence at attach time, and being able to read the enumeration
sequence is the entry point to understanding what the device is
doing.

### Lab 3: Building a Safe Register Probing Module

This lab introduces the kernel-side tool that you will use for
register-map work in real projects. You will build a small kernel
module called `regprobe` that allocates a memory resource on a
device of your choosing and exposes a sysctl that dumps the
contents of the resource. No writes are performed.

Steps:

1. Build the `regprobe` module from
   `examples/part-07/ch36-reverse-engineering/lab03-register-map/`.
2. Identify a PCI device on your system that you do not need for
   anything else. A spare network card or any unused PCI device
   is ideal. (Do not use the device that backs your console or
   your storage.)
3. Detach the in-tree driver from the device with
   `sudo devctl detach <device>`.
4. Use the operating procedures in the lab's README to attach
   `regprobe` to the device.
5. Read the dump sysctl multiple times, with a few seconds
   between each read. Compare the dumps and identify which words
   are stable and which words are changing.
6. Reattach the in-tree driver with `sudo devctl attach <device>`.

The lab demonstrates two things: that a read-only probe is safe,
and that even a read-only probe can reveal interesting structure
in a device's register space. The dynamic words are most likely
counters or status registers; the stable words are most likely
configuration registers or identifiers.

### Lab 4: Writing and Driving a Mock Device

This lab introduces the simulation side of the workflow. You will
build a small kernel module that simulates a tiny "command and
status" device entirely in software, and a small user-space test
program that drives the simulated device through its sysctls.

Steps:

1. Build the `mockdev` module from
   `examples/part-07/ch36-reverse-engineering/lab04-mock-device/`.
2. Load the module with `sudo kldload ./mockdev.ko`.
3. Use the test program (also in the lab folder) to send a few
   commands to the mock device.
4. Observe the kernel log to see the commands being processed and
   the simulated status updates.
5. Modify the mock device to introduce a deliberate error: have
   it report failure for one specific command code. Verify that
   the test program detects the failure correctly.
6. Modify the test program to use a watchdog: if the mock device
   does not complete a command within a timeout, the program
   should report a failure rather than hanging.

The lab teaches the structure of mock-based testing in a setting
where the mock is small enough to understand fully. In real
projects, mocks become more complex, but the structure is the
same.

### Lab 5: Building a Safe Probing Wrapper

This lab consolidates the safety techniques from Section 10. You
will build a small library of safe probing wrappers (read-modify-
write-verify, timeout-protected operations, automatic recovery on
failure) and use them to perform an experiment on the mock device
from Lab 4.

Steps:

1. Open the `safeprobe` lab folder.
2. Read the wrapper library carefully. Notice how each operation
   is protected by a timeout, how each register write is followed
   by a readback, and how failures are reported clearly.
3. Build the wrapper library and the example driver that uses it.
4. Load the mock device from Lab 4.
5. Run the example driver against the mock device. Observe how
   the wrappers report the operations they perform.
6. Modify the mock device to inject a failure (an unexpected
   readback value, for example). Verify that the wrappers detect
   the failure and report it clearly, rather than letting the
   driver continue with corrupt state.

This lab teaches the safety wrappers as a tool you can apply to
your own drivers. The cost of using them is small; the
information they produce when something goes wrong is large.

### Lab 6: Writing a Pseudo-Datasheet

This lab is the documentation-side equivalent of the technique
labs. You will take the mock device from Lab 4 and write a
pseudo-datasheet for it, following the structure from Section 11.

Steps:

1. Open the pseudo-datasheet template in
   `examples/part-07/ch36-reverse-engineering/lab06-pseudo-datasheet/`.
2. Read the template carefully and understand the structure.
3. Examine the mock device's source code to learn its register
   layout, command set, and behaviour.
4. Fill in the template with the mock device's information,
   following the structure: identity, provenance, resources,
   register map, buffer layouts, command interface, initialisation,
   operating patterns, quirks, open questions, references.
5. Save the result as a Markdown file alongside the mock device's
   source.

The mock device is small enough that the pseudo-datasheet can be
complete in an hour or two. The exercise teaches you the structure
of the document and the level of detail that each section
deserves. When you later write a pseudo-datasheet for a real
device, you will already know how to organise the information,
even though the information itself will be much more extensive.

## Challenge Exercises

The challenge exercises stretch the techniques from the labs into
larger, more open-ended investigations. None of them requires
exotic hardware; they all use either real common devices or the
mock devices from the labs. Take your time with them. The labs
have given you the techniques; the challenges give you the
practice of applying them in less guided settings.

### Challenge 1: Triage an Unknown Capture

The companion files include a small set of pcap captures of an
unknown device's USB traffic. Open the captures in Wireshark. By
analysis alone, identify:

- The device's vendor and product identifiers.
- The device class (HID, mass storage, vendor-specific, and so
  on).
- The device's endpoint layout (which endpoints, of which types,
  in which directions).
- The general shape of the device's data flow (does it appear to
  use bulk transfers, control transfers, interrupt transfers).
- Any obvious patterns in the data (periodic transfers, command
  and response, continuous streaming).

Write a one-paragraph summary of what kind of device the captures
came from and what protocol it appears to use. The captures are
labelled with the answer in a file you should not look at until
you have written your own answer; compare and reflect.

### Challenge 2: Extend the Mock Device

Take the mock device from Lab 4 and extend it to support a
multi-byte data transfer through a small ring buffer. The ring
buffer should have a producer pointer that the mock advances when
data is "produced" (you can simulate this with a callout that
generates synthetic data at a fixed rate), a consumer pointer
that the driver advances when data is consumed, and a "queue
size" register that the driver can read to learn the ring's
capacity.

Write a small driver that:

1. Identifies the mock device.
2. Reads the queue-size register to learn the ring capacity.
3. Periodically reads the producer pointer to learn how many new
   entries are available.
4. Reads each new entry and prints it to the kernel log.
5. Updates the consumer pointer after processing each entry.

The exercise practises the ring-buffer-recognition skill from
Section 5 in a setting where you control both sides. The
challenge is to write the driver so that it correctly handles
both the empty-ring case (no new entries) and the full-ring case
(more entries than the driver has consumed).

### Challenge 3: Detect Device Identity From Behaviour Alone

The mock device from Lab 4 has, in its companion code, a "mystery
mode" that disables the identifier register and changes some of
its behaviour to disguise its identity. Without reading the mock
device's source code, write a small driver that:

1. Probes the mystery-mode mock device.
2. Performs a series of safe register reads.
3. Observes the device's response to a small set of test
   operations.
4. Identifies which of three known device identities the mock
   matches, based purely on the observed behaviour.

The challenge teaches the higher-level skill of fingerprinting a
device from behaviour rather than from explicit identifiers.
Real devices sometimes hide their identity for compatibility
reasons (presenting themselves as a more common chip), and
identifying them by behaviour is the only way to support them
correctly.

### Challenge 4: Document a Device You Own

Pick a piece of USB hardware that you own and that does not have
particularly sensitive functionality. A USB game controller, a
webcam, a USB-to-serial adapter, a USB Wi-Fi adapter. Write a
pseudo-datasheet for it, using only what you can learn through
`usbconfig`, `usbdump`, and Wireshark.

The challenge requires you to apply, in sequence, every technique
from the chapter: identification, capture, observation, hypothesis,
documentation. The result is a real artifact: a pseudo-datasheet
for a real device. Many such pseudo-datasheets have grown into
community projects that produced production drivers; yours might
be the next one.

## Hands-On Exercise: Your Own Observation

The labs have given you controlled practice against software mocks
and known-good devices, and the challenges have asked you to apply
those techniques to slightly less structured targets. The case
studies in Section 13 walked you through reverse engineering work
that has long since been absorbed into the FreeBSD tree. What none
of those has asked you to do is to sit down with a device of your
own choosing, observe it cold, map what you see onto interface and
endpoint structures, and sketch the beginning of a driver from
scratch. That is what this exercise does, and it is the closest
this chapter can bring you to the work the `umcs` and `axe` authors
did at the start of their projects.

Treat the exercise as a capstone rather than another lab. It does
not introduce a new technique; it asks you to combine the
techniques you have already practised into a single self-directed
session, using a device you physically own and tools from the
FreeBSD base system. The goal is not a shippable driver. The goal
is a faithful short record of what you observed, a skeleton file
that compiles and attaches cleanly, and a list of the questions
your observation did not answer. If you finish with those three
artefacts, the exercise has worked as intended.

### Before You Start: The Ethical Gate

Because this exercise touches a real device that you own, the
legal and ethical framework from Sections 1 and 12 applies to it
directly, and it applies before the first command runs. Work
through the short checklist below, in order. Do not proceed until
every item has a clear answer.

1. **Pick a device you own outright.** The exercise is not a
   licence to probe equipment you have borrowed, rented, or been
   granted limited access to. The device must be yours, and it
   must be a device you are willing to see briefly misbehave under
   observation. A working rule is that if you would not feel
   comfortable unplugging the device in the middle of normal
   operation, it is not a good candidate for this exercise.

2. **Pick a device whose firmware and protocol are not
   vendor-protected.** Good targets are simple class-compliant USB
   devices that implement an open specification. A USB HID
   keyboard or mouse, a USB-to-serial adapter built around a
   commodity chip, a class-compliant USB audio interface, or a
   small USB-attached LED enclosure with a straightforward
   vendor-specific protocol are all reasonable choices. Rule out
   anything whose firmware is known to be DRM-encumbered, anything
   whose protocol is covered by a non-disclosure agreement that you
   have signed, and anything whose vendor driver ships under an
   end-user licence that specifically forbids the kind of
   observation described here. When a device is ambiguous, set it
   aside and pick something else. The exercise is about technique,
   not about probing any specific piece of hardware.

3. **Observe only, do not import.** Everything the walkthrough
   asks you to do is passive observation of traffic on a bus you
   control, followed by writing your own code against your own
   notes. That stays firmly inside the clean-room framework from
   Section 12. The moment you move from observation to copying
   vendor firmware, pasting vendor driver source, or redistributing
   a binary blob you did not author, you leave that framework and
   enter a space where the answer depends on the licence of the
   specific material. Do not cross that line inside the exercise.
   If you later want to carry the work toward a real driver,
   revisit Sections 9 and 12 and work through the licensing
   questions carefully before you do.

4. **Defer to the chapter's existing guardrails.** The discipline
   described in Sections 1 and 12 is the authoritative framework
   for this project. If any step below appears to conflict with
   those sections, treat the sections as correct and narrow the
   step, not the other way around. The walkthrough here is a
   concrete application of that framework, not an exception to it.

With those four items settled, the observation can begin. The rest
of the exercise assumes that all four are cleared; if any of them
is uncertain, close this section and choose a different device.

### Step 1: Initial Identification With `usbconfig(8)`

Attach the target device to your FreeBSD host and confirm that the
kernel has enumerated it:

```console
$ sudo usbconfig list
ugen0.1: <0x1022 XHCI root HUB> at usbus0, cfg=0 md=HOST spd=SUPER (5.0Gbps) pwr=SAVE (0mA)
ugen0.2: <Example device Example vendor> at usbus0, cfg=0 md=HOST spd=FULL (12Mbps) pwr=ON (100mA)
```

Note the `ugenB.D` coordinate of your device. That coordinate is
how every later `usbconfig` command will address it.

Next, check which FreeBSD driver, if any, has claimed an interface
on the device:

```console
$ sudo usbconfig -d ugen0.2 show_ifdrv
```

If a kernel driver already owns the interface, the output names
it. A device whose interface is claimed by an existing driver will
still respond to read-only descriptor queries, but you should not
run your own experimental code against it until you have detached
the existing driver with `devctl detach`. For a first pass, pick a
device whose interface is either unclaimed or whose claiming
driver you are willing to detach.

Finally, dump the full descriptor tree and save it for later:

```console
$ sudo usbconfig -d ugen0.2 dump_all_desc > device-descriptors.txt
```

The output lists the device descriptor, each configuration
descriptor, each interface descriptor, and each endpoint
descriptor. You will compare this file against your packet capture
in Step 3, and you will use it as the raw material for the short
pseudo-datasheet fragment at the end of the exercise.

### Step 2: Packet-Level Observation With `usbdump(8)`

Descriptors tell you what the device advertises about itself. To
see what the device actually does, capture the USB bus while the
device is used:

```console
$ sudo usbdump -i usbus0 -s 2048 -w trace.pcap
```

The `-i usbus0` argument selects the USB bus to listen on; use the
bus number your device is attached to, as reported by `usbconfig
list`. The `-s 2048` argument caps each captured payload at 2048
bytes, which is comfortably above the maximum packet size of any
full-speed or high-speed endpoint you are likely to encounter in
this exercise. The `-w trace.pcap` argument writes a binary pcap
file that Wireshark can open and dissect with the built-in USB
dissector.

With `usbdump` running, exercise the device. For an HID keyboard,
press a few keys. For a USB-to-serial adapter, open the port from
another tool and send a handful of bytes in each direction. For a
USB LED enclosure, cycle the LED through its supported states.
Each action produces USB transfers on the bus, and every transfer
ends up in the capture.

Stop `usbdump` with Control-C. A human-readable text record is
also useful while you are annotating transfers by hand, so re-run
the capture once with output redirected to a text file:

```console
$ sudo usbdump -i usbus0 -s 2048 > trace.txt
```

Keep both versions. The text form is easier to read line by line
and to annotate in a plain editor; the pcap form is easier to
navigate in Wireshark when you want to follow a specific flow.

### Step 3: Map Descriptors to Interface and Endpoint Structures

Open `device-descriptors.txt` alongside `trace.txt`. Work through
the descriptor dump and, for each interface descriptor, write
down:

- The device or interface class (`bInterfaceClass`), subclass
  (`bInterfaceSubClass`), and protocol (`bInterfaceProtocol`).
  Standard values are tabulated in the USB class specifications:
  HID is class `0x03`, mass storage is class `0x08`, communications
  device class is `0x02`, audio is class `0x01`, and
  vendor-specific is class `0xFF`, among others.
- Each endpoint's address (`bEndpointAddress`), direction (the
  high bit of the address: set means IN, clear means OUT), and
  transfer type (the low two bits of `bmAttributes`: `0` control,
  `1` isochronous, `2` bulk, `3` interrupt).
- Each endpoint's maximum packet size (`wMaxPacketSize`).

Simple devices typically expose one interface with a small number
of endpoints. A USB HID keyboard commonly has a single interrupt
IN endpoint that the host polls for key-state reports. A
USB-to-serial adapter commonly has a bulk IN endpoint for data
from the device, a bulk OUT endpoint for data to the device, and
uses the control endpoint for line-state commands.

Cross-check what you wrote down against the capture. Each endpoint
address you recorded in the descriptor dump should appear in
`trace.txt` as the source or destination of at least one transfer.
Each transfer type you recorded (bulk, interrupt, control) should
appear in the capture in the direction you expect. If the two
disagree, look again before you sketch any driver code; one of
the two observations is wrong and you want to find out which.

### Step 4: Sketch a Newbus-Style Driver Skeleton

With the device's identity, interfaces, and endpoints in hand, you
can sketch the outline of a driver that would attach to it. The
sketch is not a runnable driver. It is the scaffold into which you
would later drop real transfer setup, real data handling, and real
error handling. The point of producing it now is to confirm that
your observations are consistent enough to support a driver shape
at all.

Choose a short, lowercase Newbus identifier. The convention in the
FreeBSD USB tree is a brief lowercase string that echoes either
the chip family or the device function: `umcs` for the MosChip
serial bridge, `axe` for the ASIX Ethernet chip, `uftdi` for FTDI
adapters, `ukbd` for USB HID keyboards. Pick something that does
not collide with any name already in `/usr/src/sys/dev/usb/`. Your
local sketch can use a placeholder such as `myusb` until you
commit to a real name.

The skeleton below is the minimum a USB driver needs, and it is
also what you will find in the companion file
`skeleton-template.c`:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

/* TODO: replace with the VID/PID you observed in Step 1. */
static const STRUCT_USB_HOST_ID myusb_devs[] = {
    { USB_VP(0x0000 /* VID */, 0x0000 /* PID */) },
};

static device_probe_t  myusb_probe;
static device_attach_t myusb_attach;
static device_detach_t myusb_detach;

static int
myusb_probe(device_t dev)
{
    struct usb_attach_arg *uaa = device_get_ivars(dev);

    if (uaa->usb_mode != USB_MODE_HOST)
        return (ENXIO);

    return (usbd_lookup_id_by_uaa(myusb_devs,
        sizeof(myusb_devs), uaa));
}

static int
myusb_attach(device_t dev)
{
    /* TODO: allocate the endpoints you catalogued in Step 3. */
    /* TODO: set up the usb_config entries that match them.   */
    device_printf(dev, "attached\n");
    return (0);
}

static int
myusb_detach(device_t dev)
{
    /* TODO: unwind whatever attach allocated, in reverse order. */
    device_printf(dev, "detached\n");
    return (0);
}

static device_method_t myusb_methods[] = {
    DEVMETHOD(device_probe,  myusb_probe),
    DEVMETHOD(device_attach, myusb_attach),
    DEVMETHOD(device_detach, myusb_detach),
    DEVMETHOD_END
};

static driver_t myusb_driver = {
    .name    = "myusb",
    .methods = myusb_methods,
    .size    = 0,
};

DRIVER_MODULE(myusb, uhub, myusb_driver, NULL, NULL);
MODULE_DEPEND(myusb, usb, 1, 1, 1);
MODULE_VERSION(myusb, 1);
```

The `TODO` markers show where the observations from Steps 1
through 3 feed into the driver. The vendor and product identifiers
come straight from the device descriptor you saved. The endpoint
allocation and transfer setup inside `myusb_attach` mirror the
endpoints you catalogued. In the full form of the driver, each
endpoint becomes an entry in a `struct usb_config` array whose
`.type` and `.direction` fields record what you observed: a bulk
IN endpoint uses `.type = UE_BULK` and `.direction = UE_DIR_IN`,
an interrupt IN endpoint uses `.type = UE_INTERRUPT` and
`.direction = UE_DIR_IN`, and so on. Existing drivers in
`/usr/src/sys/dev/usb/` are full of examples of the pattern;
`/usr/src/sys/dev/usb/serial/umcs.c` and
`/usr/src/sys/dev/usb/serial/uftdi.c` are both reasonable
references.

Do not try to fill in the transfer bodies yet. The goal at this
stage is a skeleton that compiles, loads, and attaches to your
device, and that prints a short message when it attaches and
another when it detaches. If the skeleton does those three things
cleanly, your observations were consistent. If it panics, refuses
to attach, or prints nonsense, the fault is almost always in the
descriptor mapping rather than in the skeleton itself; go back to
Step 3 and check the IN/OUT directions and the transfer types
before touching the code.

### Step 5: Write Down What You Learned

Close the exercise by producing two short written artefacts to
keep alongside the descriptor dump, the trace file, and the driver
skeleton:

1. A **pseudo-datasheet fragment**, following the structure from
   Section 11. Identify the device, list its class and subclass,
   list its endpoints with their transfer types, directions, and
   maximum packet sizes, and note any behaviour you observed in
   the capture that surprised you. Keep the fragment short. The
   goal is to have something you could hand to a collaborator if
   the two of you wanted to pick the work up later.

2. A **list of open questions** you could not answer from
   observation alone. Every honest reverse engineering session
   ends with open questions, and writing them down is how you
   avoid pretending to know things you do not. Typical entries
   include a vendor-specific control request whose purpose is
   unclear, a block of bytes inside a bulk transfer whose layout
   is not obvious, or a descriptor field whose meaning depends on
   a device state you have not yet seen.

These two artefacts, together with `device-descriptors.txt`,
`trace.pcap`, `trace.txt`, and `skeleton-template.c`, make up the
output of the exercise. They are, at a smaller scale, the same
kind of output the `umcs` and `axe` authors would have produced
at the start of their own projects.

### A Closing Reminder on Scope

Everything this exercise has asked you to do is passive
observation of traffic on your own bus, followed by writing your
own code against your own notes. That sits squarely inside the
clean-room tradition the chapter has described. If, after working
through the steps, you want to carry the work further toward a
shippable driver, stop and re-read Sections 9, 11, and 12 before
the next session. The techniques scale, but the legal and ethical
framework scales with them, and it is easier to apply early than
to unwind a commit later. The companion folder for this exercise,
at `examples/part-07/ch36-reverse-engineering/exercise-your-own/`,
collects a template walkthrough script, a skeleton source file
with the `TODO` markers shown above, and a short `README` that
gathers the safety notes in one place.

## Common Mistakes and Troubleshooting

Reverse engineering has a small set of mistakes that nearly
everyone makes early on, and an equally small set of mistakes
that, even with experience, are easy to slip into. Recognising
them in advance shortens the painful learning curve significantly.

### Mistake 1: Believing the First Hypothesis

The most common mistake is to form a hypothesis early, then to
interpret every subsequent observation as supporting the
hypothesis, even when a more careful reading would suggest a
different explanation. Confirmation bias is human, and it is
particularly dangerous in reverse engineering because the
observations are noisy and the hypothesis space is large.

The defence is the discipline of writing down each hypothesis
explicitly, designing a test that could falsify it, running the
test, and accepting the result honestly. If the test does not
falsify the hypothesis, the hypothesis survives, but it is not
yet "proven"; it is only "not yet refuted". The next test might
still kill it.

### Mistake 2: Skipping the Notebook

The second most common mistake is to skip the notebook because
the keyboard is faster. The notebook feels like overhead. The
results, in the moment, seem clear. The patterns are obvious.
There is no need to write them down, because they will be
remembered.

A week later, the patterns are not obvious. Another week later,
they are forgotten. A month later, the project has stalled
because nobody, including the author, can reconstruct what was
known. The notebook is what would have prevented this. Skip it
at your peril.

### Mistake 3: Writing to Unknown Registers

The third common mistake is to write to a register without
sufficient evidence that the write is safe. The temptation is
strong: you have a hypothesis, you want to test it, the test
involves a write, and what is the worst that could happen?
Section 10 has spelled out what can happen, but it is worth
repeating here. Devices can be permanently damaged by careless
writes, and the people who learn this from experience usually
learn it expensively.

The defence is the discipline of evidence-before-write. Before
performing a write, name the source of evidence that the write
is safe. If you cannot name a source, do not perform the write.

### Mistake 4: Treating the Vendor Driver as Complete

A fourth common mistake is to assume that the vendor's driver
implements the full functionality of the device, and that
matching the vendor's driver is sufficient to support the device
fully. This is often wrong. Vendor drivers sometimes implement
only the subset of device functionality that the vendor's
products use; the full device may have features that the vendor's
driver never exercises. Conversely, the vendor's driver
sometimes contains workarounds for hardware bugs that you would
not need if you implemented the operations differently.

The defence is to read the vendor's driver as one source of
information among many, with the understanding that it represents
the vendor's choices, not the complete description of the
device.

### Mistake 5: Not Testing the Detach Path

A fifth common mistake is to focus on the attach path, where
visible progress is made, and to neglect the detach path. The
result is a driver that loads and works, but cannot be unloaded
cleanly. Each test cycle then requires a reboot, which slows the
work down by an order of magnitude.

The defence is to write the detach path early, test it in every
build, and treat unload-time panics as serious bugs that block
further work. A driver that cannot be unloaded is, for practical
purposes, a driver that requires a reboot to test changes, and
that is not a productive way to work.

### Mistake 6: Not Saving Captures

A sixth mistake is to throw away captures because they "did not
show anything new". They might not show anything new today. They
might show something important six weeks from now, when you have
learned enough to interpret them. Save every capture. Storage is
cheap. The captures themselves are part of the project's history,
and they are sometimes the only way to reconstruct what was
known at a given point.

### Mistake 7: Working Alone

A seventh mistake, particularly common in beginners, is to work
alone. Reverse engineering is much faster when there is at least
one other person to talk to about the work, even if that person
is not also a reverse engineer. The act of explaining what you
have observed, what you believe, and what you are about to test
forces clarity on the explanation, and clarity is what makes the
work go forward.

If you do not have a colleague who is interested, find a community
that is. The mailing lists, IRC channels, and forums for the
device's family are full of people who understand the issues and
can sometimes provide the missing piece. The community-collaboration
side of Section 9 is not just about consuming prior work; it is
also about contributing to a discussion that helps everyone
involved.

### Troubleshooting: When the Driver Silently Does Nothing

Sometimes the driver loads, the device appears to attach, the
log says everything is fine, but no expected behaviour occurs.
The most common causes:

- The driver thinks it has set up an interrupt handler, but the
  interrupt is not actually being delivered. Check `vmstat -i` to
  see whether interrupts are arriving. If not, the interrupt
  setup is wrong.
- The driver is reading the wrong register and seeing what looks
  like a legitimate value. Compare against the `pciconf -r` or
  `regprobe` output to verify that the values you are reading
  match the values present in the device.
- The driver is using the wrong endianness for multi-byte values.
  This is particularly common when reading values that look like
  integers but are actually byte strings.
- The driver has a bug in its sequence: it performed step B
  before step A, or it skipped a required step entirely.

The defence in each case is comparison against a known reference:
the captures from Section 3, the `regprobe` dump, the working
vendor driver. When the driver does not behave as expected, the
question is: where does its behaviour first diverge from the
reference?

### Troubleshooting: When the Driver Panics on Unload

A panic on unload usually indicates that the driver freed
something prematurely or kept a reference to something the kernel
has destroyed.

The most common causes:

- A callout was not drained before the softc was freed. Check
  that every `callout_init` is matched by `callout_drain` in the
  detach path.
- A taskqueue task was still pending when the taskqueue was
  destroyed. Check that every `taskqueue_enqueue` is matched by
  `taskqueue_drain` in the detach path.
- An interrupt handler was not torn down before the IRQ resource
  was released. The order matters: tear down the handler with
  `bus_teardown_intr`, then release the resource with
  `bus_release_resource`.
- A character device node was not destroyed before the softc was
  freed. Use `destroy_dev_drain` if there is any chance the
  device might still be open.

These are the same patterns we discussed in Chapter 35 for
asynchronous drivers; reverse-engineered drivers face the same
issues, with the additional complication that you may not yet
fully understand which resources are in use at unload time.

### Troubleshooting: When Behaviour Differs Between Runs

Sometimes the device behaves differently from one run to the
next, even though nothing has changed in the driver. The causes
are usually one of:

- The device contains state that is not reset between runs.
  Investigate what the device's reset actually clears, and what
  it leaves alone.
- The driver has a race condition that produces different
  outcomes depending on timing.
- The device is sensitive to environmental conditions
  (temperature, voltage) that vary slightly between runs.
- A second driver, or a userland program, is also accessing the
  device and interfering with your investigation.

Distinguishing these causes requires careful, repeated runs with
each variable controlled. Reverse engineering against a device
whose behaviour is not deterministic is significantly harder than
the deterministic case, and identifying the source of the
non-determinism is itself part of the work.

## Wrapping Up

Reverse engineering is a craft built on patience, discipline, and
careful documentation. The chapter has walked through the entire
process: why this work is sometimes necessary, where the legal
lines fall, how to set up the lab, how to observe systematically,
how to build a register map, how to identify buffer structures,
how to write a minimal driver and grow it incrementally, how to
validate assumptions in simulation, how to collaborate with the
community, how to avoid damaging hardware, and how to consolidate
the result into a maintainable driver and a usable
pseudo-datasheet.

The techniques are concrete. The discipline is what holds them
together. A reverse engineer who follows the discipline will, in
time, be able to take an undocumented device and produce a
working driver for it. A reverse engineer who skips the
discipline will produce code that works some of the time, that
fails for reasons that are hard to diagnose, and that cannot be
maintained or extended without rediscovering everything that was
forgotten the first time around.

Several themes have run through the chapter and deserve a final
explicit summary.

The first theme is the separation of observation from hypothesis
from verified fact. An observation is what you saw. A hypothesis
is what you think it means. A verified fact is a hypothesis that
has survived deliberate attempts to falsify it. Mixing the three
together produces confusion; keeping them separate produces
clarity. The notebook discipline that the chapter has emphasised
is, at root, the discipline of keeping these three categories
distinct.

The second theme is the value of starting small. A minimal
driver that does one thing correctly is a better foundation than
a large driver that does many things uncertainly. Each new
feature added incrementally, with verification, is far cheaper to
get right than a large multi-feature change. The pace of work in
reverse engineering should always feel slower than the pace of
work in normal driver development; the slowness is what catches
the bugs that would otherwise survive into the final code.

The third theme is the centrality of safety. Reverse engineering
is one of the few software activities where careless mistakes can
permanently damage real hardware. The safety techniques of
Section 10, the simulation techniques of Section 8, and the
read-first principle of Section 4 are all manifestations of the
same underlying principle: respect the unknown, and earn the
right to perform an operation by accumulating evidence that the
operation is safe.

The fourth theme is the importance of documentation. The
pseudo-datasheet, the notebook, the manual page, the comments in
the code: these are not afterthoughts, they are part of the
output of the work. A driver without documentation is a driver
that nobody can maintain, including its author six months from
now. A pseudo-datasheet that records what was learned is the
artifact that gives the work durability beyond the author's
involvement.

The fifth theme is community. Reverse engineering is rarely truly
solitary work. Prior work exists for almost every device worth
investigating; new work, when done well, contributes to the
ongoing community knowledge. Searching, evaluating, and
contributing are part of the craft. A reverse engineer who works
in isolation reinvents wheels; a reverse engineer who engages
with the community builds on others and is built upon in turn.

You have now seen the entire shape of the work. The techniques
are within reach. The discipline takes practice. The first
serious project will be slow and full of mistakes; the second
will be faster; by the third or fourth, the rhythm will start to
feel natural. The labs in this chapter are the beginning of that
practice, and the challenge exercises are the next step. Real
projects on real devices are where the skills consolidate, and
the FreeBSD community has plenty of devices that would benefit
from someone willing to do the work.

Take a moment to appreciate what has changed in your toolkit.
Before this chapter, an undocumented device was a stop sign. Now
it is a project. The methods you have learned are the same
methods that the authors of many drivers in `/usr/src/sys/dev/`
used to bring those drivers into existence. Some of them worked
alone, others in small groups, but all of them followed
recognisably the same workflow: observe, hypothesize, test,
document. You now know how to do that work.

## Bridge to Chapter 37: Submitting Your Driver to the FreeBSD Project

The driver you have just learned to build, whether it
implements a fully reverse-engineered device or some more
conventional piece of hardware, is most useful when other people
can find it, build it, and rely on it. So far we have treated the
driver as a private project, something you load on your own
systems and document for your own future reference. The next
chapter changes that: we will see how to take a finished driver
and offer it for inclusion in the FreeBSD source tree itself.

The shift is significant. A driver in your private repository
serves you and anyone who happens to find your repository. A
driver in the FreeBSD source tree is built by every FreeBSD
release, exposed to every FreeBSD user, maintained by the
FreeBSD project, and tested by every commit that touches the
surrounding code. The amplification of value is enormous, and so
are the responsibilities that come with it. The submission
process is FreeBSD's mechanism for ensuring that the driver
meets the standards of the source tree before it is allowed in.

Chapter 37 walks through that process. We will look at the
FreeBSD development model: the difference between a contributor
and a committer, the role of the source-tree organisation, the
review process, and the conventions that the FreeBSD source tree
enforces. We will learn the style guidelines (`style(9)` for code
and `style(4)` for manual pages, with a few related conventions
for makefiles, headers, and naming), how to structure your
driver's files for inclusion in `/usr/src/sys/dev/`, how to
write the manual page that every driver should ship with, and
how to write commit messages in the form FreeBSD expects.

We will also look at the social dynamics of contribution: how to
engage with reviewers, how to respond to feedback, how to
iterate on a patch series, and how to participate in the project
in a way that builds long-term reputation. The technical side of
the submission process is straightforward; the social side is
where most first-time contributors find the surprises.

Several themes from this chapter will carry forward. The clean
detach path is essential, because reviewers will check it. The
safety wrappers and conservative behaviour from Section 10 are
the same disciplines that production-grade drivers exhibit, and
they will be appreciated by reviewers. The pseudo-datasheet is
not part of the submission, but the understanding it represents
is what justifies the driver's claims and what lets reviewers
trust the implementation. The community engagement from Section
9 is the foundation of the longer-term relationship the
contribution is intended to build.

The reverse-engineered driver is a particularly interesting case
for FreeBSD inclusion, because the project has long-standing
experience with such drivers and well-developed processes for
handling them. The provenance discipline from Section 11 is
exactly what the project needs to evaluate whether a driver is
clean of copyright concerns. The documented limitations and
open questions from the pseudo-datasheet help the project set
appropriate expectations for users. The safety wrappers and the
conservative behaviour help reviewers feel confident that the
driver will not damage user hardware.

If you have followed the labs and the challenges in this chapter,
you have produced at least a few small pieces of code that could,
with some polish, be candidates for inclusion. Chapter 37 will
show you how to take such a piece of code and walk it through the
submission process. The mock-device driver from Lab 4 might not
be a candidate (it does not drive real hardware), but the
patterns it demonstrates are exactly the patterns a real driver
should follow, and the safety wrappers from Lab 5 are patterns
that reviewers will recognise and approve.

The book is approaching its concluding chapters. You started in
Part 1 with no kernel knowledge at all. You learned UNIX and C
in Parts 1 and 2. You learned the structure and lifecycle of a
FreeBSD driver in Parts 3 and 4. You learned the bus, network,
storage, and pseudo-device patterns in Parts 4 and 5. You
learned the Newbus framework in detail in Part 6. You have now
worked through Part 7's mastery topics: portability,
virtualisation, security, FDT, performance, advanced debugging,
asynchronous I/O, and reverse engineering. The skill set is
comprehensive. The remaining chapters tie it back to the
project, the community, and the practice of contributing to a
real-world operating system.

Take a moment, before moving on to Chapter 37, to look back at
what you can now do. You can write a simple character driver
from scratch. You can write a network driver that participates in
the kernel's networking stack. You can write a driver for a
device discovered through Newbus. You can write an asynchronous
driver that supports `poll`, `kqueue`, and `SIGIO`. You can
debug a driver with `dtrace`, `KASSERT`, and `kgdb`. You can
work with a device for which no documentation exists. Each of
these capabilities is a real one, built on the previous ones,
and together they constitute a working knowledge of FreeBSD
device-driver development.

The next chapter will help you take that knowledge and turn it
into a contribution to the FreeBSD project. Let us see how that
process works.

