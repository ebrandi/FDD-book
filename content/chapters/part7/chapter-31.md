---
title: "Security Best Practices"
description: "Implementing security measures in FreeBSD device drivers"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 31
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 240
---

# Security Best Practices

## Introduction

You reach Chapter 31 with an understanding of environment that few authors have even asked you to build. Chapter 29 taught you to write drivers that survive changes of bus, architecture, and word size. Chapter 30 taught you to write drivers that behave correctly when the machine under them is not a machine at all, but a virtual one, and when the process using them is not on the host but in a jail. Both chapters have been about boundaries: the boundary between hardware and kernel, the boundary between host and guest, the boundary between host and container. Chapter 31 asks you to look at a different boundary, one that sits closer to home than any of those, and one that is easier to forget precisely because it runs through the middle of your own code.

The boundary this chapter is about is the boundary between the kernel and everyone who talks to it. Userland programs, the hardware itself, other parts of the kernel, the bootloader passing you a parameter, a firmware blob that arrived from the vendor's support site last week, a device that started behaving strangely after an upgrade. Every one of them is on the other side of a trust edge from the driver you wrote. Every one of them can, deliberately or accidentally, pass the driver something that does not match its expectations. A driver that respects this boundary is a driver that can be trusted to defend the kernel. A driver that does not respect it is a driver that, the day something hostile arrives, will let that hostility reach code it should never have reached.

This chapter is about building that habit of respect. It is not a security textbook, and it will not try to turn you into a vulnerability researcher. What it will do is teach you to see your driver the way an attacker or a careless program might see it, to recognise the specific classes of mistake that turn a small bug into a full system compromise, and to reach for the right FreeBSD primitives when you want to prevent those mistakes from happening.

A few words about what this means in practice. A kernel security bug is not just a worse version of a user-space security bug. A buffer overflow in a user-space program can corrupt that program's memory; a buffer overflow in a driver can corrupt the kernel, and the kernel serves every program on the system. An off-by-one error in a user-space parser may crash the parser; the same off-by-one in a driver may hand an attacker a way to read kernel memory that holds another user's secrets, or to write arbitrary bytes into the kernel's function tables. The consequences do not scale linearly with the size of the bug. They scale with the privilege of the code where the bug lives, and the kernel sits at the top of that hierarchy.

This is why driver security is not a separate subject, added on top of the programming skills you have been building. It is those programming skills, applied with a particular discipline in mind. The `copyin()` and `copyout()` calls you saw in earlier chapters become tools for enforcing a trust boundary. The `malloc()` flags you learned become ways to control memory lifetime. The locking discipline you practised becomes a way to prevent races that an attacker could otherwise steer. The privilege checks you saw briefly in Chapter 30 become a first line of defence against unprivileged callers reaching into places they should not be able to reach. Security is, in a real sense, the discipline of writing kernel code well, held to a higher standard and examined through the eyes of someone who wishes it to fail.

The chapter builds that view in ten steps. Section 1 motivates the subject and explains why a driver's security model is different from an application's. Section 2 works through the mechanics of buffer overflows and memory corruption in kernel code, a topic that is easy to misunderstand because the ordinary C you learned in Chapter 4 has pitfalls that grow teeth inside the kernel. Section 3 covers user input, the single largest source of exploitable bugs in drivers, and walks through safe use of `copyin(9)`, `copyout(9)`, and `copyinstr(9)`. Section 4 turns to memory allocation and lifetime, including the FreeBSD-specific flags on `malloc(9)` that matter for safety. Section 5 examines race conditions and time-of-check-to-time-of-use bugs, a class of issue that sits at the intersection of concurrency and security. Section 6 covers access control: how a driver should check whether a caller is allowed to do what they are asking for, using `priv_check(9)`, `ucred(9)`, `jailed(9)`, and the securelevel machinery. Section 7 addresses information leaks, the subtle class of bug in which a driver gives away data it did not intend to. Section 8 looks at logging and debugging, which can themselves become security problems if they are casual about what they print. Section 9 distils a set of design principles around secure defaults and fail-safe behaviour. Section 10 rounds out the chapter with testing, hardening, and a practical introduction to the tools FreeBSD gives you for finding these bugs before someone else does: `INVARIANTS`, `WITNESS`, the kernel sanitizers, `Capsicum(4)`, and fuzzing with syzkaller.

Alongside those ten sections, the chapter will touch on the `mac(4)` framework, Capsicum's role in constraining ioctl callers, string-safety idioms such as `copyinstr(9)`, and the build pitfalls that come with modern kernel hardening features like ASLR, SSP, and PIE for loadable modules. Each of these arrives where it is most relevant, never at the cost of the core thread.

One last thing before we begin. Writing secure kernel code is not a matter of reading a checklist and ticking items off. It is a way of reading your own code, and a set of reflexes that become automatic over time. The first time you see a driver that calls `copyin` into a fixed-size stack buffer without checking the length, you may wonder what is wrong with it; the hundredth time you see that pattern, you will feel the hair on the back of your neck stand up. The goal of this chapter is not to teach you every variant of every vulnerability; that would take a shelf of books. The goal is to help you build the reflexes. Once you have them, you will write drivers that are safer by default, and you will spot the dangerous patterns in other people's code before they cause trouble.

Let us begin.

## Reader Guidance: How to Use This Chapter

Chapter 31 is conceptual in a way that a few of the earlier chapters were not. The code samples are short and focused; the value is in the thinking they teach. You can work through the whole chapter by reading carefully, and you will come out of it a better driver author even if you never type a single line. The labs at the end turn the thinking into muscle memory, and the challenges push the thinking into uncomfortable corners where real bugs live, but the text itself is the main teaching surface of the chapter.

If you choose the **reading-only path**, plan for roughly three to four focused hours. At the end you will be able to recognise the major classes of driver security bug, to explain why kernel-level bugs change the whole system's trust model, to describe the FreeBSD primitives that defend against each class, and to sketch what a safe version of a given unsafe pattern should look like. That is a substantial body of knowledge, and for many readers it is where the chapter should end on the first pass.

If you choose the **reading-plus-labs path**, plan for eight to twelve hours spread across two or three sessions. The labs build on a tiny pedagogical driver called `secdev` that you will write over the course of the chapter. Each lab is a short, focused exercise: in one, you will fix a deliberately unsafe `ioctl` handler; in another, you will add `priv_check(9)` and observe what happens when unprivileged and jailed processes try to use a restricted entry point; in another, you will introduce a race condition, watch it manifest under `WITNESS`, and then fix it; and in a final lab, you will run a simple fuzzer against the driver's `ioctl` surface and read the resulting crash reports. Each lab leaves you with a working system and an entry in your lab logbook; none of them are long enough to exhaust an evening.

If you choose the **reading-plus-labs-plus-challenges path**, plan for a long weekend or two. The challenges push `secdev` into real territory: you will add MAC policy hooks so that a site-local policy can override the driver's defaults, you will tag the driver's ioctls with capability rights so that a Capsicum-confined process can still use the safe subset, you will write a short syzkaller description file for the driver's entry points, and you will run the sanitized kernel variants (`KASAN`, `KMSAN`) to see what they catch that the normal `INVARIANTS` build misses. Each challenge is self-contained; none require reading additional chapters to complete.

A note on the lab environment. You will continue to use the throwaway FreeBSD 14.3 machine from earlier chapters. The labs in this chapter do not need a second machine, do not need `bhyve(8)`, and do not need to modify the host in ways that would persist after a reboot. You will load and unload kernel modules, you will write to a test character device, you will read `dmesg` carefully, and you will edit a small tree of source files. If something goes wrong, a reboot will recover the host. A snapshot or boot environment is still a good idea, and it is cheap to create.

One piece of special advice for this chapter: **read slowly**. Security prose is sometimes deceptively gentle. The ideas look obvious on the page, but the reason a given bug is a bug can take a minute of thinking through before it clicks. Resist the temptation to skim. If a paragraph describes a race condition that you do not fully understand, stop and reread it. If a code snippet demonstrates an information leak, trace the path of the leaked bytes in your head until you can name which byte came from where. The reward for careful reading here is a set of reflexes that will outlive this book.

### Prerequisites

You should be comfortable with everything from earlier chapters. In particular, this chapter assumes you already understand how to write a loadable kernel module, how the driver's `open`, `read`, `write`, and `ioctl` entry points connect to `/dev/` nodes, how softc is allocated and attached to a `device_t`, how mutexes and reference counts work at the level Chapter 14 and Chapter 21 taught, and how interrupts and callouts interact with sleeping paths. If any of that is shaky, a brief revisit before starting will make the examples land more firmly.

You should also be comfortable with ordinary FreeBSD system administration: reading `dmesg`, loading and unloading modules, running commands as an unprivileged user, creating a simple jail, and using `sysctl(8)` to observe and tune the system. The chapter will refer to these tools without walking you through each one from scratch.

No prior background in security research is needed. The chapter builds its vocabulary from the ground up.

### What This Chapter Does Not Cover

A responsible chapter tells you what it leaves out. This chapter does not teach exploit development. It does not teach you how to write shellcode, how to build a ROP chain, or how to turn a crash into code execution. Those are legitimate subjects, but they belong to a different kind of book, and the skills they require are not the skills that help you write safer drivers.

This chapter does not turn you into a security auditor. Auditing a large codebase for every class of vulnerability is a distinct discipline with its own tools and rhythms. What the chapter does give you is the ability to audit your own driver competently, and to recognise the patterns worth flagging in someone else's.

The chapter does not replace the FreeBSD Security Advisories, the CERT C coding standard, the SEI's guidance on secure kernel programming, or the manual pages of the APIs it discusses. It points you toward those sources and expects you to consult them when a specific question goes deeper than the chapter can follow. Each major section of the chapter ends with a short pointer to the relevant manual pages so that your first port of call after the chapter is the FreeBSD documentation itself.

Finally, the chapter does not attempt to cover every possible bug class. It focuses on the classes that matter most for drivers and that can be addressed with FreeBSD primitives the reader already knows or can learn in a few pages. Some exotic classes of bug, such as Spectre-style speculative execution side channels, are mentioned only in passing; they belong to specialised hardening work that most driver authors do not and should not write from scratch.

### Structure and Pacing

Section 1 builds the mental model: what is at risk when a driver goes wrong, and how the driver's security model differs from an application's. Section 2 addresses buffer overflows and memory corruption in kernel code, including the subtle ways they differ from their user-space cousins. Section 3 teaches safe handling of user-provided input through `copyin(9)`, `copyout(9)`, `copyinstr(9)`, and related primitives. Section 4 covers memory allocation and lifetime: the flags on `malloc(9)`, the difference between `free(9)` and `zfree(9)`, and the use-after-free patterns that kernel modules fall into. Section 5 turns to races and TOCTOU bugs, including the security-relevant ways they manifest. Section 6 covers access control and privilege enforcement, from `priv_check(9)` through `ucred(9)` and jails to the securelevel machinery. Section 7 addresses information leaks and the surprisingly subtle ways data escapes. Section 8 discusses logging and debugging, which can themselves become security problems. Section 9 collects the principles of secure defaults and fail-safe design. Section 10 covers testing and hardening: `INVARIANTS`, `WITNESS`, `KASAN`, `KMSAN`, `KCOV`, `Capsicum(4)`, the `mac(4)` framework, and a walkthrough of running syzkaller against a driver's ioctl surface. The labs and challenges follow, along with a closing bridge into Chapter 32.

Read the sections in order. Each one assumes the previous one, and the final two sections (Sections 9 and 10) synthesise what came before into practical advice and a workflow.

### Work Section by Section

Each section in this chapter covers one core idea. Do not try to hold two sections in your head at the same time. If a section ends and you feel uncertain about one of its points, pause before starting the next one, re-read the closing paragraphs, and look up the cited manual pages. A five-minute pause to consolidate is almost always faster than discovering two sections later that the foundation was not quite firm.

### Keep the Reference Driver Close

The chapter builds a small pedagogical driver called `secdev` across its labs. You will find it, along with starter code, intentionally broken versions, and fixed variants, under `examples/part-07/ch31-security/`. Each lab directory contains the state of the driver at that step, along with its `Makefile`, a brief `README.md`, and any supporting scripts. Clone the directory, type along, and load each version after every change. Running unsafe code on your lab machine and watching what happens is part of the lesson; do not skip the live tests.

### Open the FreeBSD Source Tree

Several sections point to real FreeBSD files. The ones that repay careful reading in this chapter are `/usr/src/sys/sys/systm.h` (for the exact signatures of `copyin`, `copyout`, `copyinstr`, `bzero`, and `explicit_bzero`), `/usr/src/sys/sys/priv.h` (for the priv constants and the `priv_check` prototypes), `/usr/src/sys/sys/ucred.h` (for the credential structure), `/usr/src/sys/sys/jail.h` (for the `jailed()` macro and `prison` structure), `/usr/src/sys/sys/malloc.h` (for the allocation flags), `/usr/src/sys/sys/sbuf.h` (for the safe string builder), `/usr/src/sys/sys/capsicum.h` (for capability rights), `/usr/src/sys/sys/sysctl.h` (for the `CTLFLAG_SECURE`, `CTLFLAG_PRISON`, `CTLFLAG_CAPRD`, and `CTLFLAG_CAPWR` flags), and `/usr/src/sys/kern/kern_priv.c` (for the priv-check implementation). Open them when the chapter points you at them. The source is the authority; the book is a guide into it.

### Keep a Lab Logbook

Continue the lab logbook from earlier chapters. For this chapter, log a short note for each lab: which commands you ran, which modules loaded, what `dmesg` said, what surprised you. Security work, more than most, benefits from a paper trail, because the bugs it teaches you to see are often invisible until you look for them in the right way, and a logbook entry from last week may save you an hour of re-discovery this week.

### Pace Yourself

Several ideas in this chapter land more firmly the second time you meet them than the first. Feature bits in virtio made more sense in Chapter 30 after a day's rest; the same thing happens here with, say, the distinction between `copyin` error handling and TOCTOU-safe re-copying. If a subsection blurs the first time through, mark it, move on, and come back to it. Security reading rewards patience.

## How to Get the Most Out of This Chapter

Chapter 31 rewards a particular kind of engagement. The specific primitives it introduces, `priv_check(9)`, `copyin(9)`, `sbuf(9)`, `zfree(9)`, `ppsratecheck(9)`, are not decorative; they are the bricks of secure driver code. The most valuable habit you can build while reading this chapter is the habit of asking two questions at every single call site: where did this data come from, and who is allowed to cause it to be here?

### Read With a Hostile Mind

Security reading asks for a shift in how you look at code. When the chapter shows you a driver that copies `len` bytes from user space into a buffer, do not read the snippet as if the value of `len` is reasonable. Read it as if `len` is 0xFFFFFFFF. Read it as if `len` is a carefully chosen value that passes one obvious check and fails a subtler one. Read the code the way a bored, clever, unfriendly person might read it before going to bed. That is the reading that finds bugs.

### Run What You Read

When the chapter introduces a primitive, run a small example of it. When it shows a pattern for `priv_check`, write a two-line kernel module that calls `priv_check` with a specific constant and observe what happens when you invoke its ioctl from a non-root process. When it describes the effect of `CTLFLAG_SECURE` on a sysctl, set a dummy sysctl in your lab module, raise and lower the securelevel, and watch the behaviour change. The running system teaches what prose alone cannot.

### Type the Labs

Every line of code in the labs is there to teach something. Typing it yourself slows you down enough to notice the structure. Copy-pasting the code often feels productive and usually is not; the finger-memory of typing kernel code is part of how you learn it. Even when a lab asks you to fix a deliberately unsafe file, type the fix yourself rather than pasting in the suggested answer.

### Treat `dmesg` as Part of the Manuscript

Several of the bugs in this chapter surface only in kernel log output. A `KASSERT` firing, a `WITNESS` complaint about an out-of-order lock acquisition, a rate-limited warning from your own `log(9)` call, all of these appear in `dmesg` and nowhere else. Watch `dmesg` during the labs. Tail it in a second terminal. Copy relevant lines into your logbook when they teach something non-obvious.

### Break Things Deliberately

At several points in the chapter, and explicitly in some of the labs, you will be asked to run unsafe code to see what happens. Do it. A kernel panic on your lab machine is a cheap educational experience. Unload the module after each experiment, note the symptom in your logbook, and move on. A panic in a production system is expensive; the whole point of a lab environment is to give you the freedom to learn these lessons where they are cheap.

### Work in Pairs When You Can

If you have a study partner, this chapter is a good one to pair on. Security work benefits enormously from a second pair of eyes. One of you can read the code looking for bugs while the other reads the prose; then you can swap and compare notes. The two modes of reading find different things, and the conversation itself is educational.

### Trust the Iteration

You will not remember every flag, every constant, every priv identifier on the first pass. That is fine. What matters is that you remember the shape of the subject, the names of the primitives, and where to look when a concrete question comes up. The specific identifiers become reflex after you have written two or three security-conscious drivers; they are not a memorisation exercise.

### Take Breaks

Security reading is cognitively intense in a different way from performance work or bus-plumbing work. It asks you to hold a model of an adversary in your head while you read code designed to serve a friend. Two hours of focused reading followed by a real break is almost always more productive than four hours of grinding.

With those habits in place, let us begin with the question that frames everything else: why does driver security matter?

## Section 1: Why Driver Security Matters

It is tempting to think of driver security as a subset of software security in general, with the same techniques and the same consequences, just applied to a different codebase. That framing is not wrong, exactly, but it misses what is distinctive about drivers. The reason drivers deserve their own chapter on security is that the consequences of a security bug in a driver are different from the consequences of the same bug in a user-space program, and the defenses look different too. This section builds the mental model that the rest of the chapter rests on.

### What the Kernel Trusts

The kernel is the only part of the system that is trusted to do certain things. It is the only piece of software that can read or write any physical memory address. It is the only piece of software that can talk directly to hardware. It is the only piece of software that can grant or revoke privileges for user-space processes. It is the piece of software that holds the secrets of every user and the credentials of every running program. When it decides whether a given request should succeed, nothing above it can overrule that decision.

That privilege is the whole point of having a kernel. Without it, the kernel would not be able to enforce the boundaries that make a multi-user system possible. With it, the kernel carries a responsibility that no user-space program carries: every line of kernel code runs with the authority of the whole system, and every bug in kernel code can, in principle, be escalated into the authority of the whole system.

A driver is part of the kernel. Once loaded, a driver's code runs with the same privilege as the rest of the kernel. There is no finer-grained boundary inside the kernel that says "this code is only a driver, so it cannot touch the scheduler." A pointer dereference in your driver, if it lands on the wrong address, can corrupt any data structure the kernel uses. A buffer overflow in your driver, if it is large enough, can overwrite any function pointer the kernel uses. An uninitialised value in your driver, if it flows into the right place, can leak a neighbour's secrets. The kernel trusts the driver completely, because it has no mechanism to distrust it.

That asymmetry is the first thing to internalise. User-space programs run under the kernel, and the kernel can enforce rules on them. Drivers run inside the kernel, and no one enforces rules on them except the driver authors themselves.

### A Kernel Bug Changes the Trust Model

A bug in a user-space program is a bug. A bug in the kernel, and particularly in a driver that an unprivileged process can reach, is often something worse: it is a change to the trust model of the whole system. This is the single most important idea in this chapter, and it is worth dwelling on.

Consider a tiny bug in a user-space text editor: an off-by-one error that writes one extra byte to a buffer. In the worst case, the editor crashes. Maybe the user loses a few minutes of work. Maybe the editor's sandbox catches the crash and the impact is even smaller. The consequences are bounded by what that user could already do; the editor was running with the user's privileges, so the damage cannot escape those privileges.

Now consider the same off-by-one error in a driver's `ioctl` handler. If the driver is reachable from unprivileged processes, an unprivileged user can trigger the off-by-one. The one extra byte lands in kernel memory. Depending on where it lands, it could flip a bit in a structure the kernel uses to decide who is allowed to do what. A clever attacker can arrange for that bit flip to change which process has root privileges. Now the off-by-one is not a crash; it is a privilege escalation. The unprivileged user becomes root. The system's trust model, which assumed that only authorised users were root, no longer holds.

This is not a hypothetical scaling. It is the standard way kernel bugs are turned into exploits. The kernel's data structures sit near each other in memory. An attacker who can write a single byte somewhere in the kernel can often, with enough cleverness, steer that byte into a structure that matters. A few bytes out of place in the right data structure becomes a working exploit. A few bytes in the right place can make the difference between "my editor crashed" and "the attacker now owns the machine."

This is why the mental framing of security must shift when you move from user-space to the kernel. You are not asking "what is the worst that can happen if this code goes wrong?" You are asking "what is the worst thing that someone could do to the system if they could steer this code to go wrong in exactly the way they wanted?" Those are different questions, and the second one is always the right one to ask inside the kernel.

### A Partial Catalogue of What Is at Risk

It helps to make the abstract concrete. If a driver has a bug that can be triggered from user space, what specifically is at risk? The list is long. Here are the major categories, as a way of fixing the stakes in your mind before the chapter turns to specific classes of bug.

**Privilege escalation.** An unprivileged user obtains root privileges, or a jailed user obtains host-level privileges, or a user inside a capability-mode sandbox obtains privileges outside that sandbox.

**Arbitrary kernel memory read.** An attacker reads kernel memory they should not see. This includes cryptographic keys, password hashes, other users' file contents that happen to be in the page cache, and the kernel's own data structures that reveal where other interesting memory lives.

**Arbitrary kernel memory write.** An attacker writes to kernel memory they should not write. This is often the foundation for privilege escalation, because it can be used to modify credential structures, function pointers, or other security-critical state.

**Denial of service.** An attacker causes the kernel to panic, to hang, or to consume so many resources that the system is no longer useful. Drivers that can be made to loop indefinitely, to allocate unbounded memory, or to hit a `KASSERT` from user input are all sources of DoS.

**Information leak.** An attacker learns something they should not know: a kernel pointer (which defeats KASLR), the contents of an uninitialised buffer (which may contain previous callers' data), or metadata about other processes or devices on the system.

**Persistence.** An attacker installs code that survives reboots, typically by writing to a file the kernel will re-load at boot, or by corrupting a configuration structure.

**Sandbox escape.** An attacker who is confined to a jail, to a VM guest, or to a Capsicum capability-mode sandbox escapes their confinement by way of a driver bug.

Each of these is a plausible consequence of a single, plausible mistake in a driver. The mistake is often something that looked harmless to the author: a forgotten length check, a structure that was not zeroed before being copied out, a race between two paths that looked to be mutually exclusive. This chapter's goal is to help you see those mistakes before they become any of the items on the list.

### Real-World Incidents

Every major kernel has a history of driver-based security incidents. FreeBSD is no exception. Without turning this into a vulnerability archaeology exercise, it is worth naming a few types of incident that are particularly instructive.

There is the classical **ioctl without privilege check**, in which a driver exposes an ioctl that does something a non-root user should not be able to do, but forgets to call `priv_check(9)` or equivalent before doing it. The fix is a one-line addition; the bug can enable arbitrary-code execution as root. This pattern has appeared in multiple kernels over the decades.

There is the **information leak through uninitialised memory**, in which a driver allocates a structure, fills in some fields, and copies the structure to user space. The fields the driver did not fill in contain whatever the allocator happened to return, which may include the last caller's data. Over time, attackers have been able to extract kernel pointers, file contents, and cryptographic keys from this class of bug.

There is the **buffer overflow in a seemingly innocent path**, in which a driver parses a structure from a firmware blob or a USB descriptor without checking the length fields the data claims. An attacker who can control the firmware (for example, by connecting a malicious USB device) can trigger the overflow. This class of bug is particularly pernicious because the attacker can be physical: plug in a USB stick, walk away.

There is the **race between `open` and `read`**, in which two threads simultaneously open and read a device, and the driver's state machine has a gap in its synchronisation. The second thread observes a half-initialised state and triggers a crash, or worse, is allowed to proceed and sees data that should have been cleared.

There is the **TOCTOU bug**, in which a driver validates a value in a user-space structure, then later trusts that the value is still the same. Between the check and the use, the user-space program has changed the value, and the driver now operates on data it never validated.

Each of these is preventable. Each has a well-known FreeBSD primitive that prevents it. The chapter is structured around teaching those primitives in the right order.

### The Security Mindset

A recurring theme in this chapter is that secure code comes from a particular way of thinking, not from a particular set of techniques. The techniques matter; you need to know them. But the techniques without the mindset produce code that is secure against the specific bugs the author thought of, and unsecure against every bug the author did not. The mindset, applied consistently, keeps producing safe code even when the techniques are imperfect.

The mindset has three habits. First, **assume the worst about every input**. Every byte you read from user space, from a device, from firmware, from a bus, from a sysctl, from a loader tunable, might be the worst byte an attacker could have chosen. Not because most inputs are adversarial, but because secure code must work correctly even when they are. Second, **assume the least about the environment**. Do not assume the caller is root just because the test setup made them root; check. Do not assume a field in a structure was zeroed just because the last writer said it was; zero it yourself. Do not assume the system is at securelevel 0; test it. Third, **fail closed rather than open**. When something is wrong, return an error. When something is missing, refuse to proceed. When a check fails, stop. A driver that errs on the side of not working when the rules are unclear is a driver that is hard to abuse; a driver that errs on the side of working anyway is a driver waiting to be exploited.

Those three habits do not need memorisation. They need internalisation. This chapter is a workshop in internalising them.

### Even Root Is Not Trusted

A specific point that beginners sometimes miss: even when the caller is root, the driver must still validate the caller's input. This seems counterintuitive. If the caller is root, they can already do anything; what is the point of validating their input?

The point is that "the caller is root" is a statement about authorisation, not about correctness. A root user can ask your driver to do something, and the kernel will say yes. But a root user can also be a buggy program that is passing the wrong length by accident. A root user can be a compromised program that an attacker has taken over. A root user can be running a clumsy script that treats a pointer as a length. In every one of these cases, your driver must still behave sanely.

Concretely, if root passes you a `len` of 0xFFFFFFFF in an `ioctl` argument, the correct behaviour is to return `EINVAL`, not to happily `copyin` four gigabytes of user memory into a kernel buffer. Root did not really want that; root was running a program that had a bug. Your driver's job is to prevent the bug from becoming a kernel bug.

This is why input validation is universal. It is not about distrust of the caller; it is about the driver protecting itself and the rest of the kernel from mistakes, whether deliberate or accidental, coming from above.

### Where the Boundaries Are

A driver lives between several boundaries. It is worth naming them explicitly, because different classes of bug live at different boundaries, and the defenses differ.

The **user-kernel boundary** separates userland from the kernel. Data crossing from user space into the kernel must be validated; data crossing from kernel to user space must be sanitised. `copyin(9)` and `copyout(9)` are the primary mechanism for crossing this boundary safely. Sections 3, 4, and 7 of this chapter deal with this boundary.

The **driver-bus boundary** separates the driver from the hardware. Data read from a device is not always trustworthy; a malicious device or a firmware bug can present values the driver did not expect. Length fields in descriptors, for example, must be bounded by the driver's own expectations rather than by the values the device claims. Section 2 touches on this.

The **privilege boundary** separates different levels of authority: root from non-root, the host from a jail, the kernel from a capability-mode sandbox. Privilege checks enforce this boundary. Section 6 deals with it in depth.

The **module-module boundary** separates your driver from other kernel modules. It is the least defended of the boundaries, because the kernel trusts its own modules completely by default. This is one reason the next section talks about the blast radius of a driver bug: it is almost always larger than the driver.

### Where This Chapter Fits

Chapters 29 and 30 taught environment in two senses: architectural and operational. Chapter 29 taught you to make the driver portable across buses and architectures. Chapter 30 taught you to make it behave correctly in virtualised and containerised environments. Chapter 31 teaches a third kind of environment, which is policy: the security-relevant choices the administrator makes and the adversary attempts to violate. Taken together, the three chapters describe the environment around a FreeBSD driver at runtime, and what the driver author must do to be a responsible citizen of that environment.

The thread continues. Chapter 32 will turn to Device Tree and embedded development, which may feel like a change of subject but is in fact the same thread carried into new hardware. The security habits you learn here travel with you to every ARM board, every RISC-V system, every embedded target where a driver's privilege and resource discipline matter just as much as they do on a desktop. Later chapters will deepen the debugging story, including some of the techniques this chapter introduces at a high level. The security habits you build now will serve you for the rest of the book, and for the rest of your career as a driver author.

### Wrapping Up Section 1

A driver is part of the kernel. Every bug in a driver is a potential kernel bug, and every kernel bug is a potential change to the system's trust model. Because the blast radius is so large, the bar for correctness in a driver is higher than the bar in a user-space program. The chapter's remaining sections walk through the specific classes of bug that matter most for drivers, and the FreeBSD primitives that defend against them.

The single sentence to remember from this section is this: **in a driver, bugs are not just mistakes; they are changes to who can do what on the system**. Keep that framing in mind as you read the rest of the chapter, and every other sentence will be easier to follow.

With the stakes in view, we turn to the first concrete class of bug: buffer overflows and memory corruption inside kernel code.

## Section 2: Avoiding Buffer Overflows and Memory Corruption

Buffer overflows and their cousins, out-of-bounds reads and writes, are the oldest and still among the most common classes of security bug. They appear in user-space code, in kernel code, and in every language that does not enforce bounds at the language level. C is such a language, and kernel C is such a language with sharper edges, so drivers are fertile ground for buffer bugs.

This section explains how these bugs appear in kernel code, why they are often worse than their user-space equivalents, and how to write driver code that avoids them by construction. It assumes that the reader remembers the C material from Chapter 4 and the kernel-C material from Chapter 5 and Chapter 14. If any of that is shaky, a short revisit before reading this section will repay the investment.

### A Short Refresher on Buffers

A buffer in C is a region of memory with a specific size. In a driver, buffers come from several places. Stack-allocated buffers are declared as local variables inside functions; they live for the duration of the function call and are cheap to allocate and free. Heap-allocated buffers come from `malloc(9)` or `uma_zalloc(9)`; they live as long as the driver keeps a pointer to them. Statically allocated buffers are declared at file scope; they live for the whole lifetime of the module. Each of these has different properties and different pitfalls.

The one thing all buffers share is a size. Writing past the end of the buffer, or reading before its beginning, or indexing into it with a value that does not fit, is a **buffer overflow** (or underflow). The overflow itself is the mechanism; what the overflow writes and where the writing lands determine the severity.

A stack overflow in a driver is the most dangerous kind, because the stack holds return addresses, saved registers, and local variables for the whole call chain. A write past the end of a stack buffer can reach into the caller's return address, and from there into arbitrary code execution. A heap overflow is less directly exploitable, but heap buffers are often adjacent to other kernel data structures, and a heap overflow that lands on the right structure is a clear path to compromise. A static-buffer overflow is the least common but can still lead to compromise if the static buffer is next to other writeable module data.

The vocabulary of "stack" and "heap" overflow should feel familiar from user-space work. The mechanism is the same. The consequences are worse, because the kernel's code and data share an address space with everything else it does.

### How Overflows Happen in Kernel Code

Overflows do not happen because authors write to memory they did not intend to write to. They happen because the author writes to memory they did intend to write to, but the length or offset is wrong. The most common shapes of the mistake are:

**Trusting a length from user space.** The driver's `ioctl` argument contains a length field, and the driver uses that length to decide how much to `copyin` or how large a buffer to allocate. If the length is not bounded, the user can pick a length that makes the copy misbehave.

**Off-by-one in a loop.** A loop that iterates over an array uses `<=` where `<` was intended, or `<` where `<=` was intended. The extra iteration touches memory just past the end of the array.

**Incorrect buffer size in a call.** A call to `copyin`, `strlcpy`, `snprintf`, or similar takes a size argument. The author passes `sizeof(buf)` for a pointer-typed buf, which yields the pointer's size (four or eight bytes) rather than the buffer's size. The call writes far too many bytes.

**Arithmetic overflow in a length calculation.** The author multiplies or adds lengths to compute a buffer size, and the multiplication overflows a 32-bit integer. The resulting "size" is small, the allocation succeeds, and the subsequent copy writes far more than was allocated.

**Truncating a string without terminating it.** The author uses `strncpy` or similar, but `strncpy` does not guarantee a null terminator; a later string operation reads past the end of the buffer.

**Skipping a length check because the code "obviously" cannot reach a bad state.** The author convinces themselves that a given path cannot produce a length greater than some bound, so no check is needed. The path can produce such a length, because the author missed a case.

Each of these is a class of bug with countermeasures. The rest of this section walks through the countermeasures.

### Bound Everything

The simplest and most effective countermeasure is to bound every length. Before you use a length from an untrusted source, compare it against a known maximum. Before you allocate a buffer whose size comes from an untrusted source, compare the size against a known maximum. Before you copy into a buffer, confirm that the copy size fits.

Concretely, if your `ioctl` handler takes a structure with a `u_int32_t len` field, add a check like this at the very top of the handler:

```c
#define SECDEV_MAX_LEN    4096

static int
secdev_ioctl_set_name(struct secdev_softc *sc, struct secdev_ioctl_args *args)
{
    char *kbuf;
    int error;

    if (args->len > SECDEV_MAX_LEN)
        return (EINVAL);

    kbuf = malloc(args->len + 1, M_SECDEV, M_WAITOK | M_ZERO);
    error = copyin(args->data, kbuf, args->len);
    if (error != 0) {
        free(kbuf, M_SECDEV);
        return (error);
    }
    kbuf[args->len] = '\0';

    /* use kbuf */

    free(kbuf, M_SECDEV);
    return (0);
}
```

The first line of the function is the bound. No matter what the caller passes, `args->len` is now at most `SECDEV_MAX_LEN`. The allocation is bounded, the copy is bounded, and the null-termination is inside the buffer. This pattern is the workhorse of safe driver code.

What should the bound be? It depends on the semantics of the argument. A name of a device might reasonably be bounded to a few hundred bytes. A configuration blob might be bounded to a few kilobytes. A firmware blob might be bounded to a few megabytes. Pick a number that is generous enough to accommodate legitimate use and small enough that its consequences, if reached, are bearable. If the bound is too small, users will complain about legitimate failures; if it is too large, an attacker can use the bound itself as an amplifier for denial of service. A generous bound is almost always the right choice.

Some drivers derive the bound from the structure of the hardware. A driver for a fixed-size register bank might bound reads and writes to the bank's size. A driver for a ring with 256 entries might bound the index to 255. Bounds derived from hardware structure are particularly robust, because they correspond to a physical constraint rather than an arbitrary choice.

### The `sizeof(buf)` Trap

One of the most common buffer-size bugs in C code is the confusion between `sizeof(buf)` and `sizeof(*buf)`, or between `sizeof(buf)` and the length of the memory `buf` points to. The trap appears most often when a buffer is passed into a function.

Consider this unsafe function:

```c
static void
bad_copy(char *dst, const char *src)
{
    strlcpy(dst, src, sizeof(dst));    /* WRONG */
}
```

Here, `dst` is a `char *`, so `sizeof(dst)` is the size of a pointer: 4 on 32-bit systems, 8 on 64-bit systems. The call to `strlcpy` tells it that the destination is 8 bytes long, regardless of how big the real buffer is. On a 64-bit system, the function writes up to 8 bytes and terminates, and the caller's 4096-byte buffer now contains a short string, which is probably not what anyone wanted. On any system, if the caller's buffer was less than 8 bytes, the call overflows it.

The fix is to pass the buffer size explicitly:

```c
static void
good_copy(char *dst, size_t dstlen, const char *src)
{
    strlcpy(dst, src, dstlen);
}
```

The callers then use `sizeof(their_buf)` at the call site, where `their_buf` is known to be the array:

```c
char name[64];
good_copy(name, sizeof(name), user_input);
```

This pattern is so common in FreeBSD that many internal functions follow it: they take a `(buf, bufsize)` pair rather than a bare `buf`. When you write functions that write into a buffer, do the same. Your future self, reading the code six months later, will thank you.

### Bounded String Functions

C's traditional string functions, `strcpy`, `strcat`, `sprintf`, were designed in an era when nobody took buffer overflows seriously. They do not take a size argument; they write until they see a null terminator. In kernel code, they are trouble, because the null terminator can be far away or absent entirely.

FreeBSD provides bounded alternatives:

- `strlcpy(dst, src, dstsize)`: copy at most `dstsize - 1` bytes plus a null terminator. Returns the length of the source string. Safe to use when you know `dstsize` correctly.
- `strlcat(dst, src, dstsize)`: append `src` to `dst`, ensuring the result is at most `dstsize - 1` bytes plus a null terminator. Like `strlcpy`, this is safe when `dstsize` is correct.
- `snprintf(dst, dstsize, fmt, ...)`: format into `dst`, writing at most `dstsize` bytes including the terminator. Returns the number of bytes that would have been written, which may be larger than `dstsize`. Check the return value if you need to know about truncation.

`strncpy` and `strncat` also exist, but they have surprising semantics. `strncpy` pads with nulls if the source is shorter than the destination size, and, more dangerously, it does not null-terminate if the source is longer. `strncat` is confusing in a different way. Prefer `strlcpy` and `strlcat` in new code.

For longer formatted output, the `sbuf(9)` API is safer still. It manages an auto-growing buffer with a clean interface for appending strings, printing formatted output, and bounding the final size. It is overkill for small fixed-size copies but excellent for anything that builds up a longer message. Section 8 returns to `sbuf` in the context of logging.

### Arithmetic and Overflow

A subtler class of buffer bug comes from arithmetic on sizes. The classic example is:

```c
uint32_t total = count * elem_size;          /* may overflow */
buf = malloc(total, M_SECDEV, M_WAITOK);
copyin(user_buf, buf, total);
```

If `count * elem_size` overflows a 32-bit `uint32_t`, `total` wraps around to a small number. The `malloc` succeeds with that small number. The `copyin` is asked for the same small number of bytes, which makes the allocation-and-copy pair itself safe. But a later piece of the driver may treat `count * elem_size` as if it produced the full amount, and write past the end of the buffer.

The fix is to check for overflow explicitly:

```c
#include <sys/limits.h>

if (count == 0 || elem_size == 0)
    return (EINVAL);
if (count > SIZE_MAX / elem_size)
    return (EINVAL);
size_t total = count * elem_size;
```

The division is exact (no rounding) for integer types, and the test `count > SIZE_MAX / elem_size` is equivalent to "would the multiplication overflow `size_t`?" This pattern is well worth memorising. It is one of those idioms that appears unnecessary in the common case and essential in the exceptional case.

On modern compilers, FreeBSD also has `__builtin_mul_overflow` and its siblings, which perform the arithmetic and report overflow in a single operation. They are a little more convenient when you have them, but the explicit division check works everywhere.

### Integer Types Matter

Closely related is the choice of integer types for lengths and offsets. If a length is stored as `int`, it can be negative, and a negative value sneaking into a call that expects an unsigned length can cause spectacular misbehaviour. If a length is stored as `short`, it can only represent values up to 32767, and a caller passing a value near that limit can cause truncation.

The safe types for lengths in FreeBSD are `size_t` (unsigned, at least 32 bits, often 64 on 64-bit platforms) and `ssize_t` (signed `size_t`, usually for return values that may be negative to indicate error). Use them consistently. When you take a length as input, convert it to `size_t` at the earliest opportunity. When you store a length, store it as `size_t`. When you pass a length to a FreeBSD primitive, pass a `size_t`.

If the length comes from user space and the user-facing structure uses a `uint32_t`, the conversion on a 64-bit kernel is safe (no truncation), and you should still validate the value before using it. If the user-facing structure uses `int64_t` and the kernel needs a `size_t`, check for negatives and for overflow before the conversion.

### Stack Buffers Are Cheap but Limited

A stack buffer is a local array:

```c
static int
secdev_read_name(struct secdev_softc *sc, struct uio *uio)
{
    char name[64];
    int error;

    mtx_lock(&sc->sc_mtx);
    strlcpy(name, sc->sc_name, sizeof(name));
    mtx_unlock(&sc->sc_mtx);

    error = uiomove(name, strlen(name), uio);
    return (error);
}
```

Stack buffers are allocated automatically, freed automatically when the function returns, and are essentially free to use. They are ideal for small, short-lived data that does not need to outlive the function call.

The limit on stack buffers is the size of the kernel stack itself. FreeBSD's kernel stack is small, typically 16 KiB or 32 KiB depending on the architecture, and that stack must accommodate the whole call chain, including nested calls into the VFS, the scheduler, interrupt handlers, and so on. A driver function that declares a 4 KiB local buffer is already using a quarter of the stack. A driver function that declares a 32 KiB local buffer has almost certainly blown the stack, and the kernel will panic or corrupt memory when it happens.

A safe rule of thumb: keep local buffers under 512 bytes, and preferably under 256 bytes. For anything larger, allocate on the heap. The compiler will not warn you when you declare a stack buffer that is too large; it is the author's responsibility to keep stack usage bounded.

### Heap Buffers and Their Lifetimes

A heap buffer is allocated dynamically:

```c
char *buf;

buf = malloc(size, M_SECDEV, M_WAITOK | M_ZERO);
/* use buf */
free(buf, M_SECDEV);
```

Heap buffers can be arbitrarily large (up to the available memory), can outlive the function that allocates them, and give the author explicit control over when they are freed. They come at the cost of requiring deliberate attention: every allocation must be paired with a free, every free must happen after the last use, and every free must happen only once.

The rules for heap buffers are:

1. Always check the allocation if you used `M_NOWAIT`. With `M_WAITOK`, the allocation cannot fail; with `M_NOWAIT`, it can return `NULL` and your code must handle that.
2. Pair every `malloc` with exactly one `free`. Not zero, not two.
3. After calling `free`, do not access the buffer. If the pointer may be reused, set it to `NULL` immediately after the free, so that accidental use triggers a null-pointer panic rather than a subtle corruption.
4. If the buffer held sensitive data, zero it with `explicit_bzero` or use `zfree` before freeing.

Section 4 covers these rules in more depth, including the FreeBSD-specific flags on `malloc(9)`.

### A Worked Example: Safe and Unsafe Copy Routines

To make the patterns concrete, here is an unsafe copy routine that you might find in a first-draft driver, followed by a safe rewrite. Read the unsafe version carefully and see if you can spot all the bugs before looking at the commentary.

```c
/* UNSAFE: do not use */
static int
secdev_bad_copy(struct secdev_softc *sc, struct secdev_ioctl_args *args)
{
    char buf[256];

    copyin(args->data, buf, args->len);
    buf[args->len] = '\0';
    strlcpy(sc->sc_name, buf, sizeof(sc->sc_name));
    return (0);
}
```

There are at least four bugs in those four lines.

First, `copyin`'s return value is ignored. If the user supplied a bad pointer, `copyin` returns `EFAULT`, but the function continues as if the copy succeeded. The subsequent operations on `buf` operate on whatever garbage the stack happened to hold.

Second, `args->len` is not bounded. If the user supplies a `len` of 1000, `copyin` writes 1000 bytes into a 256-byte stack buffer. The stack is corrupted. The driver has just become a vehicle for privilege escalation.

Third, `buf[args->len] = '\0'` writes past the end of the buffer even in the non-malicious case. If `args->len == sizeof(buf)`, the assignment is to `buf[256]`, which is one past the end of the 256-byte array.

Fourth, the function returns 0 regardless of whether anything went wrong. A caller receives a success code and has no way to know that the driver silently dropped their input.

Here is a safe rewrite:

```c
/* SAFE */
static int
secdev_copy_name(struct secdev_softc *sc, struct secdev_ioctl_args *args)
{
    char buf[256];
    int error;

    if (args->len == 0 || args->len >= sizeof(buf))
        return (EINVAL);

    error = copyin(args->data, buf, args->len);
    if (error != 0)
        return (error);

    buf[args->len] = '\0';

    mtx_lock(&sc->sc_mtx);
    strlcpy(sc->sc_name, buf, sizeof(sc->sc_name));
    mtx_unlock(&sc->sc_mtx);

    return (0);
}
```

The bound is now `args->len >= sizeof(buf)`, which ensures that the terminator at `buf[args->len]` fits. The `copyin` return value is checked and propagated. The write to `sc->sc_name` happens under the mutex that protects it, ensuring that another thread reading the field at the same time sees a consistent value. The function returns the error code the caller needs to understand what happened.

The unsafe version is eight lines; the safe version is thirteen. The five extra lines are the difference between a working driver and a security incident.

### A Second Worked Example: The Descriptor Length

Here is a different class of bug that shows up in drivers for devices that present descriptor-like data (USB, virtio, PCIe configuration):

```c
/* UNSAFE */
static void
parse_descriptor(struct secdev_softc *sc, const uint8_t *buf, size_t buflen)
{
    size_t len = buf[0];
    const uint8_t *payload = &buf[1];

    /* copy the payload */
    memcpy(sc->sc_descriptor, payload, len);
}
```

The length is taken from the first byte of the buffer, which is a value the device (or an attacker impersonating it) can set arbitrarily. If `buf[0]` is 200, the `memcpy` copies 200 bytes, regardless of whether `buf` actually contains 200 bytes of valid data or whether `sc->sc_descriptor` is that large. If `buflen` is less than `buf[0] + 1`, the `memcpy` reads past the end of the caller's buffer. If `sizeof(sc->sc_descriptor)` is less than `buf[0]`, the `memcpy` writes past the end of the destination.

The safe version validates both sides of the copy:

```c
/* SAFE */
static int
parse_descriptor(struct secdev_softc *sc, const uint8_t *buf, size_t buflen)
{
    if (buflen < 1)
        return (EINVAL);

    size_t len = buf[0];

    if (len + 1 > buflen)
        return (EINVAL);
    if (len > sizeof(sc->sc_descriptor))
        return (EINVAL);

    memcpy(sc->sc_descriptor, &buf[1], len);
    return (0);
}
```

Three checks, each guarding a different invariant: the buffer has at least one byte, the stated length fits in the buffer, and the stated length fits in the destination. Each check protects against a different adversarial or accidental input.

A careful reader may notice that `len + 1 > buflen` can itself overflow if `len` is `SIZE_MAX`. For a `size_t` taken from a byte, `len` is at most 255, so the overflow cannot happen here; but if you write the same code for a 32-bit length field, the check should be rearranged to `len > buflen - 1` with an explicit `buflen >= 1` check. The habit of watching for arithmetic overflow is the same habit, applied at different scales.

### Buffer Overflow as a Class of Bug

Stepping back from the specific examples: buffer overflows are not a single bug. They are a family of bugs whose members share a structure: the code writes to or reads from a buffer with an incorrect size or offset. The concrete examples above show several members of the family, but the underlying pattern is the same: a length came from somewhere less trustworthy than the code believed it was, and the code was not prepared.

The countermeasures also share a structure. They all amount to: do not trust the length; check it against a known bound before you use it; keep the bound tight; propagate errors when the check fails; use bounded primitives (`strlcpy`, `snprintf`, `sbuf(9)`) when you have a choice; watch for arithmetic overflow in length calculations; and keep stack buffers small. That short list, consistently applied, eliminates most buffer overflow bugs before they are written.

### Memory Corruption Beyond Overflows

Not every memory-corruption bug is a buffer overflow. Drivers can corrupt memory in several other ways, and a complete treatment of safety must mention them.

**Use-after-free** is writing to, or reading from, a buffer after it has been freed. The allocator has almost certainly handed that memory to some other part of the kernel by now, so the write corrupts whatever that part of the kernel is doing. Section 4 covers use-after-free in depth.

**Double-free** is calling `free` twice on the same pointer. Depending on the allocator, this can corrupt the allocator's own data structures, leading to hard-to-diagnose panics minutes or hours later. Section 4 covers prevention.

**Out-of-bounds read** is the read-only cousin of buffer overflow. It does not corrupt memory directly, but it can leak information (see Section 7) and can cause the kernel to read from an unmapped page, which is a panic. It deserves the same countermeasures as overflow.

**Type confusion** is treating a block of memory as if it had a different type from what it actually has. For example, casting a pointer to the wrong structure type and accessing fields. In kernel C, type confusion is usually caught by the compiler, but it can still happen when a driver deals with void pointers or with structures shared across versions.

**Uninitialised memory use** is reading from a variable before assigning it a value. The value read is whatever happened to be in memory at that location, which may be previous callers' data. Section 7 covers this from the information-leak perspective.

Each of these has its own countermeasures, but the single most effective tool across all of them is the set of kernel sanitizers FreeBSD provides: `INVARIANTS`, `WITNESS`, `KASAN`, `KMSAN`, and `KCOV`. Section 10 covers these tools in depth. The short version: build your driver against a kernel with `INVARIANTS` and `WITNESS` always. Build it against a `KASAN`-enabled kernel during development. Run your tests under the sanitized kernel. The sanitizers will find bugs you would otherwise not find until a customer did.

### How Compiler Protections Help, and Where They Stop

FreeBSD kernels are usually compiled with several exploit-mitigation features enabled in the compiler. Understanding what they do is part of understanding why certain defensive habits matter more than others.

**Stack-smashing protection (SSP)** inserts a canary value on the stack between local variables and the saved return address. When the function returns, the canary is checked against a reference value; if it has been modified (because a stack-buffer overflow clobbered it), the kernel panics. SSP does not prevent the overflow from happening, but it prevents many overflows from gaining control of execution. Without SSP, overwriting the return address would redirect execution to attacker-controlled code on return. With SSP, the overwrite is detected and the kernel stops.

SSP is heuristic. Not every function gets a canary: functions without stack-allocated buffers, for example, do not need protection. The compiler applies SSP to functions that look risky. A driver author should not assume SSP will catch any particular bug; SSP catches some stack overflows, not all, and catches them only at function return, not at the moment of the overflow.

**kASLR** is orthogonal to SSP. It randomizes the base address of the kernel, loadable modules, and the stack. An attacker who wants to jump to a specific kernel function (say, to bypass a check) must first learn where that function is. kASLR makes this difficult. An information leak that exposes any kernel pointer can undo kASLR for the whole kernel: once you know one function's address, you know the offsets to all the others, and you can compute every other address.

**W^X enforcement** ensures that memory is either writable or executable, never both at once. Historically, attackers would overflow a buffer, write shellcode into the overflowed region, and jump to it. W^X breaks this by refusing to execute from writable memory. Modern attacks therefore use return-oriented programming (ROP), which chains together small snippets of existing code rather than introducing new code. ROP is still possible under W^X, but it is harder, and it is defeated by kASLR (ROP needs to know where the snippets are).

**Guard pages** surround kernel stacks with unmapped pages. A write past the end of the stack hits an unmapped page, causing a page fault that the kernel catches and turns into a panic. This prevents certain stack-smashing attacks from silently corrupting memory adjacent to the stack. The cost is one unusable page per kernel stack, which is cheap.

**Shadow stacks and CFI (control-flow integrity)** are under discussion and partial deployment in modern kernels. They aim to prevent attackers from redirecting execution by verifying that every indirect jump lands at a legitimate target. They are not yet standard in FreeBSD, but the direction of the industry is clear: more compiler-enforced restrictions on what exploit writers can do.

The lesson for driver authors: these protections are real, and they raise the cost of exploitation. But they do not prevent bugs. A buffer overflow is still a bug, even if SSP catches it. An information leak is still a bug, even if kASLR makes it less useful. The compiler protections are a last line of defense; the first line is still your careful code.

When the first line fails, the protections buy time: time for the bug to be found and fixed before an attacker chains it into a complete exploit. An information leak that, combined with a buffer overflow, would have been trivially exploitable in 1995 now requires both bugs to exist in the same driver and several more mitigations to fall. The effect is that bug reports that once meant "this is a root exploit" now often mean "this is a pre-condition for a root exploit". That is progress. But it is progress bought by the compiler, not by the code.

### Wrapping Up Section 2

Buffer overflows and memory corruption are the oldest security bugs in C code, and they remain the most common way driver code goes wrong. The countermeasures are well understood: bound every length, use bounded primitives when possible, watch for arithmetic overflow, keep stack buffers small, and run under the kernel's sanitizers during development. None of these are expensive. All of them are non-negotiable for code that lives in the kernel.

The bugs in this section all came from data of the wrong size reaching the wrong buffer. The next section turns to a closely related problem: data of the wrong shape reaching the wrong kernel function. That is the problem of user input, and it is the single largest source of driver bugs in the real world.

## Section 3: Handling User Input Safely

Every driver that exports an `ioctl`, a `read`, a `write`, or a `mmap` entry point is a driver that receives user input. The shape of the input varies, but the principle does not: data from user space must cross the user-kernel boundary, and the crossing is where most driver security bugs happen.

FreeBSD gives drivers a small and well-designed set of primitives for crossing the boundary safely. The primitives are `copyin(9)`, `copyout(9)`, `copyinstr(9)`, and `uiomove(9)`. Used correctly, they make user input almost impossible to mishandle. Used incorrectly, they turn the boundary into a gaping hole. This section teaches correct use.

### The User-Kernel Boundary

Before the primitives, it helps to make the boundary itself vivid.

A user-space program has its own address space. The program's pointers refer to addresses that are meaningful only in that address space. A pointer that points to byte `0x7fff_1234_5678` in the program's memory has no meaning inside the kernel; the kernel's view of user memory is indirect, mediated by the virtual-memory subsystem.

When the program makes an `ioctl` call that includes a pointer (for example, a pointer to a structure the driver should fill in), the kernel does not receive kernel-space access to that memory. The kernel receives a user-space address. Dereferencing it directly from kernel code is not safe: the address may be invalid (the user sent a garbage pointer), it may point to memory that is not currently resident (paged out), it may not be mapped in the current address space at all, or it may be in a region the kernel has no business reading.

Early UNIX kernels were sometimes careless here and dereferenced user pointers directly. The result was a class of bug known as "ptrace-style" attacks, in which a user program could induce the kernel to read or write arbitrary addresses by passing cleverly crafted pointers. Modern kernels, including FreeBSD, never dereference a user pointer directly from kernel code. They always go through a dedicated primitive that validates and handles the access safely.

The primitives themselves are straightforward. Before we look at them, a note on vocabulary: when the manual pages and the kernel say "kernel address," they mean an address that is meaningful in the kernel's address space. When they say "user address," they mean an address supplied by a user-space caller, which is meaningful only in that caller's address space. The primitives translate between the two, with appropriate safety checks.

### `copyin(9)` and `copyout(9)`

The two primitives at the heart of the user-kernel boundary are `copyin(9)` and `copyout(9)`:

```c
int copyin(const void *udaddr, void *kaddr, size_t len);
int copyout(const void *kaddr, void *udaddr, size_t len);
```

`copyin` copies `len` bytes from user address `udaddr` to kernel address `kaddr`. `copyout` copies `len` bytes from kernel address `kaddr` to user address `udaddr`. Both return 0 on success and `EFAULT` if any part of the copy failed, typically because the user address was invalid, the memory was not resident, or the access crossed into memory the caller had no rights to.

The signatures are declared in `/usr/src/sys/sys/systm.h`. Like most kernel primitives, they are short on names and do one thing. The one thing they do, however, is essential. If a driver reads from or writes to user memory by any other means, the driver is almost certainly wrong.

**Always check the return value.** This is the single most common source of copyin/copyout bugs: a driver calls `copyin` and proceeds as if it succeeded, when in fact it might have returned `EFAULT`. If the copy failed, the destination buffer contains whatever was there before (possibly uninitialised), and operating on it is a recipe for either a crash or an information disclosure. Every call to `copyin` or `copyout` must check the return value and either proceed with success or propagate the error:

```c
error = copyin(args->data, kbuf, args->len);
if (error != 0) {
    free(kbuf, M_SECDEV);
    return (error);
}
```

That pattern appears hundreds of times in the FreeBSD kernel. Learn it and use it at every call site.

**Never reuse a pointer after a failed copy.** If `copyin` returned `EFAULT`, the buffer may have been partially written. Do not try to "rescue" a partial result; do not assume that the first few bytes are valid. Discard the buffer, zero it if the remains may be sensitive, and return the error.

**Always validate lengths before calling.** We have seen this in Section 2, but it bears repeating here. The `len` you pass to `copyin` comes from somewhere; if it comes from the caller's structure, it must be bounded before the call. An unbounded `len` in a `copyin` is one of the most dangerous patterns in a driver.

**`copyin` and `copyout` can sleep.** These primitives may cause the calling thread to sleep while waiting for a user page to be resident. This means they cannot be called from contexts where sleeping is forbidden: interrupt handlers, spin-mutex critical sections, and the like. If you need to transfer data to or from user space from such a context, defer the work to a different context (typically a taskqueue or a regular process context) and have that context do the copy.

### `copyinstr(9)` for Strings

A string from user space is a special case. You do not know how long it is, only that it is null-terminated. You want to copy it, but you do not want to copy beyond the buffer you have prepared, and you need to handle the case where the user-provided string has no terminator within the expected range.

The primitive for this is `copyinstr(9)`:

```c
int copyinstr(const void *udaddr, void *kaddr, size_t len, size_t *lencopied);
```

`copyinstr` copies bytes from `udaddr` to `kaddr` until it encounters a null byte or until `len` bytes have been copied, whichever comes first. If `lencopied` is not NULL, `*lencopied` is set to the number of bytes copied (including the terminator, if one was found). The return value is 0 on success, `EFAULT` on a fault, and `ENAMETOOLONG` if no terminator was found within `len` bytes.

The key safety rule is: **always pass a bounded `len`**. `copyinstr` without a bound (or with a huge bound) can cause large amounts of kernel memory to be written, and in older kernels could cause the kernel to scan huge amounts of user memory before giving up. In modern FreeBSD the scan itself is bounded by `len`, but you should still pass a tight bound appropriate to the string's expected size. A path name might reasonably be bounded to `MAXPATHLEN` (which is `PATH_MAX`, currently 1024 on FreeBSD). A device name might be bounded to 64. A command name might be bounded to 32. Pick a bound that fits the use and pass it.

A second safety rule is: **always check the return value**, and treat `ENAMETOOLONG` as a distinct condition from `EFAULT`. The former means the user tried to pass a longer string than you were willing to accept, which is plausibly a legitimate mistake. The latter means the user's pointer was invalid, which may or may not be a legitimate mistake. Your driver may want to return a different error to user space depending on which condition occurred.

A third safety rule is: **check the copied length if you care**. The `lencopied` parameter tells you how many bytes were actually written, including the terminator. If your code depends on knowing the exact length, check it. If your buffer is exactly `len` bytes long and `copyinstr` returned 0, the terminator is at `kbuf[lencopied - 1]`, and the string is `lencopied - 1` bytes long.

A safe use of `copyinstr`:

```c
static int
secdev_ioctl_set_name(struct secdev_softc *sc,
    struct secdev_ioctl_name *args)
{
    char name[SECDEV_NAME_MAX];
    size_t namelen;
    int error;

    error = copyinstr(args->name, name, sizeof(name), &namelen);
    if (error == ENAMETOOLONG)
        return (EINVAL);
    if (error != 0)
        return (error);

    /* namelen includes the terminator; the string is namelen - 1 bytes */
    KASSERT(namelen > 0, ("copyinstr returned zero-length success"));
    KASSERT(name[namelen - 1] == '\0', ("copyinstr missed terminator"));

    mtx_lock(&sc->sc_mtx);
    strlcpy(sc->sc_name, name, sizeof(sc->sc_name));
    mtx_unlock(&sc->sc_mtx);

    return (0);
}
```

The function takes a fixed-size stack buffer, calls `copyinstr` with a tight bound, handles the two error cases distinctly, asserts the invariants that `copyinstr` promises (`namelen > 0`, terminator at `name[namelen - 1]`), and copies into the softc under the lock. This is the canonical pattern.

### `uiomove(9)` for Streams

`read` and `write` entry points do not use `copyin`/`copyout` directly; they use `uiomove(9)`, which is a wrapper that handles the iteration over a `struct uio` descriptor. A `uio` describes an I/O operation with potentially multiple buffers (scatter-gather) and tracks how much has been transferred so far.

```c
int uiomove(void *cp, int n, struct uio *uio);
```

`uiomove` copies up to `n` bytes between the kernel buffer `cp` and whatever is described by `uio`. If `uio->uio_rw == UIO_READ`, the copy is kernel-to-user; if `UIO_WRITE`, user-to-kernel. The function updates `uio->uio_offset`, `uio->uio_resid`, and `uio->uio_iov` to reflect the bytes transferred.

Like `copyin`, `uiomove` returns 0 on success and an error code on failure. Like `copyin`, it can sleep. Like `copyin`, the caller must check the return value.

A typical `read` implementation:

```c
static int
secdev_read(struct cdev *dev, struct uio *uio, int flag)
{
    struct secdev_softc *sc = dev->si_drv1;
    char buf[128];
    size_t len;
    int error;

    mtx_lock(&sc->sc_mtx);
    len = strlcpy(buf, sc->sc_name, sizeof(buf));
    mtx_unlock(&sc->sc_mtx);

    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;

    if (uio->uio_offset >= len)
        return (0);   /* EOF */

    error = uiomove(buf + uio->uio_offset, len - uio->uio_offset, uio);
    return (error);
}
```

This handles the case where the user reads past the end of the data (returning 0 to indicate EOF), bounds the copy to the size of the kernel buffer, and propagates any error from `uiomove`. It is a safe pattern for short, fixed data; longer data typically uses `sbuf(9)` internally and copies out with `sbuf_finish`/`sbuf_len`/`uiomove` at the end.

### Validate Every Field of Every Structure

When an `ioctl` takes a structure, the driver must validate every field before trusting any of them. A common error is to validate the fields the driver uses immediately and ignore the ones it uses later. The structure lives for the duration of the `ioctl` call, and the driver may end up using fields it did not check.

Concretely, if your `ioctl` takes this structure:

```c
struct secdev_config {
    uint32_t version;       /* protocol version */
    uint32_t flags;         /* configuration flags */
    uint32_t len;           /* length of data */
    uint64_t data;          /* user pointer to data blob */
    char name[64];          /* human-readable name */
};
```

validate every field at the top of the handler:

```c
static int
secdev_ioctl_config(struct secdev_softc *sc, struct secdev_config *cfg)
{
    if (cfg->version != SECDEV_CONFIG_VERSION_1)
        return (ENOTSUP);

    if ((cfg->flags & ~SECDEV_CONFIG_FLAGS_MASK) != 0)
        return (EINVAL);

    if (cfg->len > SECDEV_CONFIG_MAX_LEN)
        return (EINVAL);

    /* Name must be null-terminated within the field. */
    if (memchr(cfg->name, '\0', sizeof(cfg->name)) == NULL)
        return (EINVAL);

    /* ... proceed to use the structure ... */
    return (0);
}
```

Four invariants, each checked and enforced. The driver now knows that `version`, `flags`, `len`, and `name` are all in the expected range. It can use them without further validation. Without these checks, each use later in the function becomes another potential source of bugs.

An important subtlety: when a structure includes reserved fields or padding, the driver must decide what to do when those fields are non-zero. The safe choice is usually to require them to be zero:

```c
if (cfg->reserved1 != 0 || cfg->reserved2 != 0)
    return (EINVAL);
```

This preserves the possibility of using those fields in a future version of the protocol without breaking compatibility: if every current caller passes zero, any future non-zero value is necessarily from a caller that knows about the new version. Without the check, the driver cannot later distinguish old callers (who happened to leave garbage in the reserved fields) from new callers (who are using the field for a new purpose).

### Validate Structures That Come In Multiple Parts

Some `ioctl`s take a structure that contains a pointer to another block of data. The outer structure is copied in first; the pointer inside it then needs to be followed with a second `copyin`. Every field of both structures must be validated.

```c
struct secdev_ioctl_args {
    uint32_t version;
    uint32_t len;
    uint64_t data;    /* user pointer to a blob of `len` bytes */
};

static int
secdev_ioctl_something(struct secdev_softc *sc,
    struct secdev_ioctl_args *args)
{
    char *blob;
    int error;

    /* Validate the outer structure. */
    if (args->version != SECDEV_IOCTL_VERSION_1)
        return (ENOTSUP);
    if (args->len > SECDEV_MAX_BLOB)
        return (EINVAL);
    if (args->len == 0)
        return (EINVAL);

    blob = malloc(args->len, M_SECDEV, M_WAITOK | M_ZERO);

    /* Copy the inner blob. */
    error = copyin((const void *)(uintptr_t)args->data, blob, args->len);
    if (error != 0) {
        free(blob, M_SECDEV);
        return (error);
    }

    /* ... now validate the inner blob, whose shape depends on the version ... */

    free(blob, M_SECDEV);
    return (0);
}
```

The `uintptr_t` cast is worth commenting on. The user pointer arrives as a `uint64_t` in the structure, to avoid portability issues between 32-bit and 64-bit userlands. The cast to `uintptr_t` and then to `const void *` converts the integer representation back into a pointer. On a 64-bit kernel, this is a no-op; on a 32-bit kernel, the high bits of the `uint64_t` must be validated or dropped. FreeBSD runs on both, and 32-bit userland on a 64-bit kernel (via `COMPAT_FREEBSD32`) is a real case. Be explicit about the cast, and document the assumption.

### The "freezed" Problem

Some drivers have fields in user-space structures that are pointers, and the driver's convention is that the user-space memory stays valid until a particular operation completes. This pattern is common in drivers that do DMA directly from user memory.

The pattern is tricky because the user can, in principle, change the memory between the driver's validation and the driver's use. Pointer-based DMA is also the wrong idea in modern drivers; safer alternatives include:

- `mmap`, in which the driver maps kernel memory into user space for direct access, with the kernel retaining ownership of the memory and its validity.
- A copy-through-kernel approach, in which the driver always copies in, validates, and operates on the kernel copy.
- The `busdma(9)` framework, which handles user-space buffers correctly when they need to be DMA'd to hardware.

If you find yourself writing code that keeps a user-space pointer around and uses it at a later moment, stop and think. It is almost always the wrong design. Section 5 returns to this issue as a TOCTOU problem.

### Kernel Addresses Do Not Leak Into User Pointers

A recurring class of bug is when a driver, trying to communicate a pointer to user space, copies out a kernel address. The user receives a pointer to kernel memory, which is a spectacular information leak (it reveals the kernel's layout, defeating KASLR) and, if the user can somehow convince the kernel to treat the copied pointer as a user pointer, can become an arbitrary kernel-memory access.

The mistake is usually inadvertent. A common case is a structure that is shared between kernel and user space, and one of its fields is a pointer. If the driver fills in the field with a kernel pointer and then copies the structure to user space, the leak has happened.

The fix is structural: do not share structures between kernel and user that contain pointer fields intended to be used in either space. If a pointer field exists, make it `uint64_t` and treat it as an opaque integer. When the kernel fills in a user-visible pointer-like field, it must pick a value meaningful to user space, not reveal its own internal pointer.

A second class of leak is when a driver copies out a structure that contains uninitialised fields, and one of those fields happens to contain a kernel pointer (for example, because the allocator returned memory that was previously used for something that held a kernel pointer). Section 7 covers this in depth.

### `compat32` and Structure Sizes

FreeBSD supports running 32-bit user-space programs on a 64-bit kernel through the `COMPAT_FREEBSD32` machinery. For a driver, this means that the structure the caller passes may be a 32-bit structure, with different layout and size from the 64-bit version. If the driver expects the 64-bit structure and the caller passed the 32-bit one, the fields the driver reads will be at the wrong offsets, and the driver will read garbage.

Handling this is outside the scope of a typical driver; the framework helps by offering `ioctl32` entry points and automatic translation for many common cases. If your driver is used from 32-bit user-space and uses custom structures, consult the `freebsd32(9)` manual page and the `sys/compat/freebsd32` subsystem for guidance. Be aware of the issue, and test your driver from a 32-bit userland in the lab environment.

### A Larger Example: A Complete `ioctl` Handler

Combining the patterns in this section, here is what a complete, safe `ioctl` handler looks like for a hypothetical operation:

```c
static int
secdev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;
    struct secdev_ioctl_args *args;
    char *blob;
    int error;

    switch (cmd) {
    case SECDEV_IOCTL_DO_THING:
        args = (struct secdev_ioctl_args *)data;

        /* 1. Validate every field of the outer structure. */
        if (args->version != SECDEV_IOCTL_VERSION_1)
            return (ENOTSUP);
        if ((args->flags & ~SECDEV_FLAGS_MASK) != 0)
            return (EINVAL);
        if (args->len == 0 || args->len > SECDEV_MAX_BLOB)
            return (EINVAL);

        /* 2. Check that the caller has permission, if required. */
        if ((args->flags & SECDEV_FLAG_PRIVILEGED) != 0) {
            error = priv_check(td, PRIV_DRIVER);
            if (error != 0)
                return (error);
        }

        /* 3. Allocate the kernel-side buffer. */
        blob = malloc(args->len, M_SECDEV, M_WAITOK | M_ZERO);

        /* 4. Copy in the user-space blob. */
        error = copyin((const void *)(uintptr_t)args->data, blob,
            args->len);
        if (error != 0) {
            free(blob, M_SECDEV);
            return (error);
        }

        /* 5. Do the work under the softc lock. */
        mtx_lock(&sc->sc_mtx);
        error = secdev_do_thing(sc, blob, args->len);
        mtx_unlock(&sc->sc_mtx);

        /* 6. Zero and free the kernel buffer (it held user data
         * that might be sensitive). */
        explicit_bzero(blob, args->len);
        free(blob, M_SECDEV);

        return (error);

    default:
        return (ENOTTY);
    }
}
```

Each numbered step is a distinct concern. Each step handles errors locally and propagates them. The allocation is bounded by the validated length; the copy is bounded by the same length; the permission check is explicit; the cleanup is symmetric with the allocation; the final return code communicates success or the specific failure. This is what a safe ioctl handler looks like. It is not short, but every line is there for a reason.

### Common Mistakes in User-Input Handling

A short checklist of the patterns to watch for, as a reference you can return to while reviewing your own code:

- `copyin` with a length from the user, without a prior bound check.
- `copyinstr` without an explicit bound.
- Return value of `copyin`, `copyout`, or `copyinstr` ignored.
- Structure fields used before they are validated.
- Pointer field cast from `uint64_t` to `void *` without thinking about 32-bit-userland compatibility.
- String field assumed null-terminated without a `memchr` check.
- Length used in arithmetic before being bounded.
- User-space pointer kept around and used later (TOCTOU territory).
- Kernel data structure (with pointer fields) directly copied out.
- Uninitialised fields copied out to user space.

If a code review turns up any of these, pause the review, fix the pattern, and then continue.

### A Detailed Walkthrough: Designing a Safe Ioctl from Scratch

The accumulated techniques of this section can feel like a long checklist. To show how they come together in practice, let us design a single ioctl carefully, from the user-space interface down to the kernel implementation.

**The problem.** Our driver needs an ioctl that lets a user set a configuration parameter consisting of a name string (bounded length), a mode (enum), and an opaque data blob (variable length). It should also return the driver's interpretation of the configuration (for example, the canonicalized form of the name).

**Defining the interface.** The user-visible structure, defined in a header that will ship with the driver, looks like:

```c
#define SECDEV_NAME_MAX   64
#define SECDEV_BLOB_MAX   (16 * 1024)

enum secdev_mode {
    SECDEV_MODE_OFF = 0,
    SECDEV_MODE_ON = 1,
    SECDEV_MODE_AUTO = 2,
};

struct secdev_config {
    char              sc_name[SECDEV_NAME_MAX];
    uint32_t          sc_mode;
    uint32_t          sc_bloblen;
    void             *sc_blob;
    /* output */
    char              sc_canonical[SECDEV_NAME_MAX];
};
```

Notes on the design:

The name is a fixed-size inline buffer, not a pointer. This is deliberate: it avoids a separate `copyin` for the name and makes the interface simpler. The trade-off is that the buffer is always copied even if the name is short, but for 64 bytes that is negligible.

The mode is `uint32_t` rather than `enum secdev_mode` directly, because struct members that cross the user/kernel boundary should have explicit widths. The kernel validates that the value is one of the known enum values.

The blob uses a separate pointer (`sc_blob`) and a length (`sc_bloblen`). The user sets both, and the kernel uses a second `copyin` to pull the data. The length is bounded by `SECDEV_BLOB_MAX`, a value we (the driver authors) choose based on what the driver is actually going to do with the data.

The canonical output is another fixed inline buffer. The user-space caller may or may not care about this output, but the kernel always fills it.

**The kernel handler.** Let us walk through the implementation step by step. The ioctl framework will copy the structure into the kernel for us, so by the time our handler runs, `cfg` points to kernel memory. The `sc_blob` field, however, is still a user-space pointer that we must handle ourselves.

```c
static int
secdev_ioctl_config(struct secdev_softc *sc, struct secdev_config *cfg,
    struct thread *td)
{
    char kname[SECDEV_NAME_MAX];
    char canonical[SECDEV_NAME_MAX];
    void *kblob = NULL;
    size_t bloblen;
    uint32_t mode;
    int error;

    /* Step 1: Privilege check. */
    error = priv_check(td, PRIV_DRIVER);
    if (error != 0)
        return (error);

    /* Step 2: Jail check. */
    if (jailed(td->td_ucred))
        return (EPERM);

    /* Step 3: Copy and validate the name. */
    bcopy(cfg->sc_name, kname, sizeof(kname));
    kname[sizeof(kname) - 1] = '\0';  /* ensure NUL termination */
    if (strnlen(kname, sizeof(kname)) == 0)
        return (EINVAL);
    if (!secdev_is_valid_name(kname))
        return (EINVAL);

    /* Step 4: Validate the mode. */
    mode = cfg->sc_mode;
    if (mode != SECDEV_MODE_OFF && mode != SECDEV_MODE_ON &&
        mode != SECDEV_MODE_AUTO)
        return (EINVAL);

    /* Step 5: Validate the blob length. */
    bloblen = cfg->sc_bloblen;
    if (bloblen > SECDEV_BLOB_MAX)
        return (EINVAL);

    /* Step 6: Copy in the blob. */
    if (bloblen > 0) {
        kblob = malloc(bloblen, M_SECDEV, M_WAITOK | M_ZERO);
        error = copyin(cfg->sc_blob, kblob, bloblen);
        if (error != 0)
            goto out;
    }

    /* Step 7: Apply the configuration under the lock. */
    mtx_lock(&sc->sc_mtx);
    if (sc->sc_blob != NULL) {
        explicit_bzero(sc->sc_blob, sc->sc_bloblen);
        free(sc->sc_blob, M_SECDEV);
    }
    sc->sc_blob = kblob;
    sc->sc_bloblen = bloblen;
    kblob = NULL;  /* ownership transferred */

    strlcpy(sc->sc_name, kname, sizeof(sc->sc_name));
    sc->sc_mode = mode;

    /* Produce the canonical form while still under the lock. */
    secdev_canonicalize(sc->sc_name, canonical, sizeof(canonical));
    mtx_unlock(&sc->sc_mtx);

    /* Step 8: Fill the output fields. */
    bzero(cfg->sc_canonical, sizeof(cfg->sc_canonical));
    strlcpy(cfg->sc_canonical, canonical, sizeof(cfg->sc_canonical));
    /* (The ioctl framework handles copyout of cfg itself.) */

out:
    if (kblob != NULL) {
        explicit_bzero(kblob, bloblen);
        free(kblob, M_SECDEV);
    }
    return (error);
}
```

Now review this against the patterns we have discussed.

Privilege check. `priv_check(PRIV_DRIVER)` is the first line of business. No unprivileged caller ever reaches the rest.

Jail check. `jailed()` before any host-affecting work.

Name validation. The name is read from the already-copied-in `cfg`, forced NUL-terminated (defensive, in case the user did not terminate it), and whitelisted through `secdev_is_valid_name` (which presumably refuses non-alphanumeric characters).

Mode validation. An explicit whitelist of known mode values. An unknown value returns `EINVAL` immediately.

Length validation. The blob length is checked against a defined maximum before being used for allocation. Without this check, a user could request a multi-gigabyte allocation.

Allocation with `M_ZERO`. The blob buffer is zeroed so that even if `copyin` fails partway, the contents are deterministic.

Error path cleanup. The `out:` label frees `kblob` if we did not transfer ownership. The `kblob = NULL` after transfer prevents a double-free. Every path through the function reaches `out:` with `kblob` in a consistent state.

Explicit zeroing before free. The old blob (if any) is zeroed before being replaced, on the assumption that it may have contained sensitive data. The new blob on error path is also zeroed for the same reason.

Locking. The softc is updated under `sc_mtx`. The canonical form is computed under the lock so the name and canonical match.

Output zeroing. `cfg->sc_canonical` is zeroed before being filled, so padding and any fields the canonicalizer did not set are guaranteed zero.

This function has about forty lines of actual code and roughly a dozen security-relevant decisions. Each decision individually is small; the compound effect is a function that is defensible against nearly every pattern discussed in this chapter. This is what secure driver code looks like in practice: not flashy, not tricky, just careful.

The key insight is that the careful code is the easiest to review, the easiest to maintain, and the one that tends to keep working as the driver evolves. Clever tricks, by contrast, are where bugs hide.

### Wrapping Up Section 3

User input is the single largest source of driver security bugs in practice. The primitives FreeBSD provides (copyin, copyout, copyinstr, uiomove) are well designed and safe, but they must be used correctly: bounded lengths, checked return values, validated fields, zeroed buffers, and properly sized destinations. A driver that consistently applies these rules at every user-kernel boundary crossing is a driver that is hard to attack from user space.

The next section turns to a closely related subject: memory allocation. The patterns in Sections 2 and 3 assumed that `malloc` and `free` are used safely. Section 4 makes that assumption explicit and shows what "safely" means for the FreeBSD allocator in particular.

## Section 4: Secure Use of Memory Allocation

A driver that validates its inputs carefully but allocates memory carelessly has only done half the job. Memory allocation and deallocation are where a driver's behaviour in bad weather (denial of service, exhaustion, hostile inputs) is most visible, and where a handful of subtle bugs, use-after-free, double-free, leaks, can turn into full system compromise. This section covers the FreeBSD allocator's safety model and the idioms that keep a driver from becoming an allocator bug farm.

### `malloc(9)` in the Kernel

The primary kernel allocator for general-purpose work is `malloc(9)`. Its declaration in `/usr/src/sys/sys/malloc.h`:

```c
void *malloc(size_t size, struct malloc_type *type, int flags);
void free(void *addr, struct malloc_type *type);
void zfree(void *addr, struct malloc_type *type);
```

Unlike the user-space `malloc`, the kernel version takes two extra arguments. The first, `type`, is a `struct malloc_type` tag that identifies which part of the kernel is using the memory. This allows `vmstat -m` to report, per-subsystem, how much memory each part of the kernel is using. Every driver should declare its own `malloc_type` with `MALLOC_DECLARE` and `MALLOC_DEFINE`, so that its allocations are visible in the accounting.

```c
#include <sys/malloc.h>

MALLOC_DECLARE(M_SECDEV);
MALLOC_DEFINE(M_SECDEV, "secdev", "Secure example driver");
```

The first argument, `M_SECDEV`, is the identifier; the second, `"secdev"`, is the short name that appears in `vmstat -m`; the third is a longer description. Use a naming scheme that makes it easy to find the driver's allocations in system output, especially when you are diagnosing a leak.

The `flags` argument controls the allocation's behaviour. Three flags are essential:

- `M_WAITOK`: the allocator may sleep to satisfy the allocation. The call cannot fail; it either returns a valid pointer or the kernel panics (which it does only under very unusual circumstances).
- `M_NOWAIT`: the allocator must not sleep. If memory is not immediately available, the call returns `NULL`. The caller must check and handle the `NULL` case.
- `M_ZERO`: the returned memory is zeroed before being returned. Use this whenever the caller will fill in some but not all of the memory, to avoid leaking garbage.

There are others (`M_USE_RESERVE`, `M_NODUMP`, `M_NOWAIT`, `M_EXEC`), but these three are the ones a driver uses daily.

### When to Use `M_WAITOK` and When to Use `M_NOWAIT`

The choice between `M_WAITOK` and `M_NOWAIT` is dictated by context, not preference.

Use `M_WAITOK` when the driver is in a context that can sleep. This is the case in most driver entry points: `open`, `close`, `read`, `write`, `ioctl`, `attach`, `detach`. In these contexts, sleeping is allowed, and the allocator's ability to sleep until memory is available is a significant simplification.

Use `M_NOWAIT` when the driver is in a context that cannot sleep. This is the case in interrupt handlers, inside spin-mutex critical sections, and inside certain callback paths that the kernel specifies as non-sleeping. In these contexts, `M_WAITOK` would trigger a `WITNESS` assertion and a panic. Even if `WITNESS` is not enabled, sleeping in a non-sleeping context can deadlock the system.

The rule of thumb: if you can use `M_WAITOK`, use it. It removes a whole class of error handling (the NULL check), and it makes the driver's behaviour more predictable under memory pressure. Only fall back to `M_NOWAIT` when the context forces it.

With `M_NOWAIT`, you must check the return value:

```c
buf = malloc(size, M_SECDEV, M_NOWAIT);
if (buf == NULL)
    return (ENOMEM);
```

Failure to check is a null-pointer panic waiting to happen. The compiler will not warn you about it.

### `M_ZERO` Is Your Friend

One of the subtlest classes of driver bug is the one where the driver allocates memory, fills in some fields, and then uses or exposes the rest. The "rest" is whatever the allocator happened to return, which in FreeBSD is whatever the allocator's free list last had there. If that memory held another subsystem's data before being freed, a driver that fails to clear it may accidentally expose that data (an information leak) or may behave incorrectly because a field it did not set has a non-zero value.

`M_ZERO` prevents both problems:

```c
struct secdev_state *st;

st = malloc(sizeof(*st), M_SECDEV, M_WAITOK | M_ZERO);
```

After this call, every byte of `*st` is zero. The driver can then fill in specific fields and trust that everything else is either zero or set explicitly. This is so important for safety that many FreeBSD driver authors treat `M_ZERO` as the default, adding it unless there is a specific reason not to.

The exception is large allocations where you are certain you will overwrite every byte before use (for example, a buffer that is immediately filled by `copyin`). In that case, `M_ZERO` is a small waste, and you can omit it. In all other cases, prefer `M_ZERO` and accept the small cost.

A particularly important case: **any structure that will be copied to user space must either have been `M_ZERO`'d at allocation time or have had every byte explicitly set before the copy**. Otherwise the structure may include kernel data that was there before. Section 7 returns to this.

### `uma_zone` for High-Frequency Allocations

For allocations that happen many times per second with a fixed size, FreeBSD offers the UMA zone allocator:

```c
uma_zone_t uma_zcreate(const char *name, size_t size, ...);
void *uma_zalloc(uma_zone_t zone, int flags);
void uma_zfree(uma_zone_t zone, void *item);
```

UMA zones are significantly faster than `malloc` for repeated small allocations, because they maintain per-CPU caches and avoid the global allocator lock for most operations. Drivers that handle network packets, I/O requests, or other high-frequency events typically use UMA zones instead of `malloc`.

The security properties of UMA zones are similar to those of `malloc`. You still pass `M_WAITOK` or `M_NOWAIT`. You still may pass `M_ZERO` (or you may use `uma_zcreate_arg`'s `uminit`/`ctor`/`dtor` arguments to manage initial state). You still must check NULL on `M_NOWAIT`.

UMA has one additional security consideration worth knowing: **items returned to a zone are not zeroed by default**. An item freed with `uma_zfree` may retain its previous contents and be handed out to a subsequent `uma_zalloc` with that same content. If the item held sensitive data, the driver must zero it before freeing, or must pass `M_ZERO` on every allocation, or must use the `uminit` constructor machinery to zero on allocation. The safest default is to use `explicit_bzero` on the item before calling `uma_zfree`.

### Use-After-Free: What It Is and Why It Matters

A use-after-free bug occurs when a driver frees a pointer and then uses it. The allocator has, by now, almost certainly handed that memory to some other part of the kernel. Writes to the freed pointer corrupt that other part of the kernel; reads from it return whatever is now stored there.

The classic pattern:

```c
/* UNSAFE */
static void
secdev_cleanup(struct secdev_softc *sc)
{
    free(sc->sc_buf, M_SECDEV);
    /* sc->sc_buf is now dangling */

    /* later, elsewhere, something calls: */
    secdev_use_buf(sc);   /* crash or silent corruption */
}
```

The fix has two parts. First, set the pointer to NULL immediately after freeing it, so that any subsequent use is a null-pointer dereference (an immediate, diagnosable crash) rather than a dangling-pointer access (silent corruption):

```c
free(sc->sc_buf, M_SECDEV);
sc->sc_buf = NULL;
```

Second, audit the code paths that might still hold references to the freed memory. The NULL-assignment prevents crashes at `sc->sc_buf` accesses, but a local variable or a caller's parameter that still holds the old pointer is not protected. The discipline is to free memory only when you are sure no one else holds a pointer to it. Reference counts (`refcount(9)`) are the FreeBSD primitive for this.

A variant of the bug is the **use-after-detach** pattern, in which a driver frees its softc during `detach` but an interrupt handler or a callback still runs and accesses the freed softc. The fix is to drain all asynchronous activity before freeing in `detach`: cancel outstanding callouts with `callout_drain`, drain taskqueues with `taskqueue_drain`, teardown interrupt handlers with `bus_teardown_intr`, and so on. Once all async paths are quiesced, the free is safe.

### Double-Free: What It Is and Why It Matters

A double-free occurs when a driver calls `free` twice on the same pointer. The first `free` hands the memory back to the allocator. The second `free` corrupts the allocator's internal bookkeeping, because it tries to insert the same memory into the free list twice.

FreeBSD's allocator detects many double-frees and panics immediately (especially with `INVARIANTS` enabled). But some double-frees slip past the detection, and the consequences are subtle: a later allocation may return memory that is claimed to be available but is actually still in use somewhere.

The prevention is the same NULL-assignment pattern:

```c
free(sc->sc_buf, M_SECDEV);
sc->sc_buf = NULL;
```

`free(NULL, ...)` is defined to be a no-op in FreeBSD (as in most allocators), so a second call with `sc->sc_buf == NULL` does nothing. The NULL-assignment turns double-free into a safe no-op.

A related pattern is the **error-path double-free**, in which a function's cleanup logic frees a pointer, and then an outer function also frees the same pointer. The defence is to decide, explicitly, which function owns each allocation, and to have ownership transferred at clear moments. "Who frees this?" is a question that should have a clear answer at every line of the code.

### Memory Leaks Are a Security Problem

A memory leak is a piece of memory that is allocated and never freed. In a long-running driver, leaks accumulate. Eventually the kernel runs out of memory, either for the driver's subsystem or for the system as a whole, and bad things happen.

Why is a leak a security problem? Two reasons. First, a leak is a denial-of-service vector: an attacker who can trigger an allocation without a corresponding free can exhaust memory. If the attacker is unprivileged but the driver's `ioctl` allocates memory on each call, the attacker can loop on `ioctl` until the kernel OOM-kills something important. Second, a leak often hides other bugs: the leak's accumulation pressure changes the behaviour of subsequent allocations (more frequent `M_NOWAIT` failures, more unpredictable page cache), which can make racy or allocation-dependent bugs surface in production.

The prevention is discipline in allocation ownership: for every `malloc`, there must be exactly one `free`, reachable on every code path. The FreeBSD `vmstat -m` tool makes leak tracking easier in practice: `vmstat -m | grep secdev` shows, per type, how many allocations are outstanding. A driver with a leak will show a steadily rising number under load; a driver without will show a stable number.

For new drivers, it is worth stress-testing the driver in the lab for leaks: open and close the device a million times in a loop, run the full `ioctl` matrix repeatedly, watch `vmstat -m` for the driver's type, and look for growth. Any sustained growth is a leak. Leaks found in the lab are a thousand times cheaper to fix than leaks found in production.

### `explicit_bzero` and `zfree` for Sensitive Data

Some data should not be allowed to linger in memory after the driver is done with it. Cryptographic keys, user passwords, device secrets, anything whose exposure in a memory snapshot would be harmful, must be erased before the memory is freed.

The naive approach is to use `bzero` or `memset(buf, 0, len)` before the free. This works, but it has a subtle flaw: the optimiser may remove the `bzero` if it can prove that the memory is not read after. The optimiser's logic is correct as far as language semantics go, but it defeats the security intent.

The correct primitive is `explicit_bzero(9)`:

```c
void explicit_bzero(void *buf, size_t len);
```

`explicit_bzero` is declared in `/usr/src/sys/sys/systm.h`. It performs the zeroing and is guaranteed by the compiler not to be optimised away, even if the memory is not read after. Use it for any buffer that holds sensitive data:

```c
explicit_bzero(key_buf, sizeof(key_buf));
free(key_buf, M_SECDEV);
```

FreeBSD also provides `zfree(9)`, which zeros the memory before freeing:

```c
void zfree(void *addr, struct malloc_type *type);
```

`zfree` is convenient: it combines the zero and the free into one call. It first zeros the memory using `explicit_bzero`, then frees it. Use `zfree` when you are about to free a buffer that held sensitive data. Use `explicit_bzero` followed by `free` if you need to zero the buffer without freeing it, or if you are working with memory from a source other than `malloc`.

A common question: what is "sensitive data"? The conservative answer is that any data that came from user space should be treated as sensitive, because you cannot know what it represents to the user. A more pragmatic answer is that data that is obviously a secret (a key, a password hash, a nonce, authentication material) must be zeroed, and data that might reveal information about the user's activities (file contents, network payloads, command text) should be zeroed when the driver is finished with it. When in doubt, zero. The cost is small.

### `malloc_type` Tags and Accountability

The `malloc_type` tag on every allocation serves several purposes. It makes allocations visible in `vmstat -m`. It helps with panic debugging, because the tag is recorded in the allocator's metadata. It helps the allocator's own accounting, and in some configurations it enables per-type memory limits.

A driver that uses a single `malloc_type` for all its allocations is easier to audit than a driver that uses many. Create one tag per logical subsystem within the driver, not one per allocation site. For small drivers, a single tag is usually enough.

The declaration pattern:

```c
/* At the top of the driver source file: */
MALLOC_DECLARE(M_SECDEV);
MALLOC_DEFINE(M_SECDEV, "secdev", "Secure example driver");

/* Allocations throughout the driver use M_SECDEV: */
buf = malloc(size, M_SECDEV, M_WAITOK | M_ZERO);
```

The `MALLOC_DECLARE` declares the tag for external visibility; the `MALLOC_DEFINE` actually allocates it (and registers it with the accounting system). Both are needed. Do not put `MALLOC_DEFINE` in a header, because the kernel linker will complain about duplicate definitions if multiple object files include the header.

### The Lifetime of Softc

The softc is the driver's per-instance state. It is typically allocated during `attach` and freed during `detach`. The softc's lifetime is one of the most important things to get right in a driver.

The allocation usually happens via `device_get_softc`, which returns a pointer to a structure whose size was declared at driver-registration time. This means the softc memory is owned by the bus, not by the driver; the driver does not call `malloc` for it, and the driver does not call `free`. The bus allocates the softc when the driver is bound to the device and frees it when the driver is detached.

But the softc often contains pointers to other memory that the driver did allocate. Those pointers must be freed in `detach`, in the reverse order of their allocation. A typical pattern:

```c
static int
secdev_detach(device_t dev)
{
    struct secdev_softc *sc = device_get_softc(dev);

    /* Reverse order of allocation. */

    /* 1. Stop taking new work. */
    destroy_dev(sc->sc_cdev);

    /* 2. Drain async activity. */
    callout_drain(&sc->sc_callout);
    taskqueue_drain(sc->sc_taskqueue, &sc->sc_task);

    /* 3. Free allocated resources. */
    if (sc->sc_blob != NULL) {
        explicit_bzero(sc->sc_blob, sc->sc_bloblen);
        free(sc->sc_blob, M_SECDEV);
        sc->sc_blob = NULL;
    }

    /* 4. Destroy synchronization primitives. */
    mtx_destroy(&sc->sc_mtx);

    /* 5. Release bus resources. */
    bus_release_resources(dev, secdev_spec, sc->sc_res);

    return (0);
}
```

Each step handles a specific concern. The order matters: destroy the device node before freeing resources the device callbacks depend on; drain async activity before freeing data the async paths might touch; destroy synchronization primitives last.

A slip in any of these orderings is a bug. The wrong order can produce use-after-free or double-free patterns. The lab later in the chapter walks through a detach function that has subtle ordering bugs and asks you to fix them.

### A Complete Allocation/Deallocation Pattern

Pulling the patterns together, here is a safe allocation and use sequence:

```c
static int
secdev_load_blob(struct secdev_softc *sc, struct secdev_blob_args *args)
{
    char *blob = NULL;
    int error;

    if (args->len == 0 || args->len > SECDEV_MAX_BLOB)
        return (EINVAL);

    blob = malloc(args->len, M_SECDEV, M_WAITOK | M_ZERO);

    error = copyin((const void *)(uintptr_t)args->data, blob, args->len);
    if (error != 0)
        goto done;

    error = secdev_validate_blob(blob, args->len);
    if (error != 0)
        goto done;

    mtx_lock(&sc->sc_mtx);
    if (sc->sc_blob != NULL) {
        /* replace existing */
        explicit_bzero(sc->sc_blob, sc->sc_bloblen);
        free(sc->sc_blob, M_SECDEV);
    }
    sc->sc_blob = blob;
    sc->sc_bloblen = args->len;
    blob = NULL;  /* ownership transferred */
    mtx_unlock(&sc->sc_mtx);

done:
    if (blob != NULL) {
        explicit_bzero(blob, args->len);
        free(blob, M_SECDEV);
    }
    return (error);
}
```

The function has a single exit point via the `done` label. The `blob = NULL` after ownership transfer ensures that the cleanup at `done` sees the transfer and does not re-free. The `explicit_bzero` before every `free` zeroes the buffer in case it held sensitive data. The existing `sc->sc_blob`, if present, is zeroed and freed before being replaced, to avoid leaking the old blob's contents.

This pattern (single exit point, ownership transfer, explicit zeroing, checked allocation, checked copyin) appears in variations throughout the FreeBSD kernel. Learn it well.

### A Closer Look at UMA Zones

`malloc(9)` is a general-purpose allocator suited to varying sizes. For fixed-size objects that are allocated and freed frequently, the UMA zone allocator is often the better choice. UMA stands for Universal Memory Allocator, and it is declared in `/usr/src/sys/vm/uma.h`.

A UMA zone is created once, at module load, and holds a pool of objects of a fixed size. `uma_zalloc(9)` returns an object from the pool (allocating a fresh one if necessary). `uma_zfree(9)` returns an object to the pool (or frees it back to the kernel if the pool is full). Because allocations come from a pre-configured pool, they are faster than general `malloc` and have better cache locality.

Creating a zone:

```c
static uma_zone_t secdev_packet_zone;

static int
secdev_modevent(module_t mod, int event, void *arg)
{
    switch (event) {
    case MOD_LOAD:
        secdev_packet_zone = uma_zcreate("secdev_packet",
            sizeof(struct secdev_packet),
            NULL,   /* ctor */
            NULL,   /* dtor */
            NULL,   /* init */
            NULL,   /* fini */
            UMA_ALIGN_PTR, 0);
        return (0);

    case MOD_UNLOAD:
        uma_zdestroy(secdev_packet_zone);
        return (0);
    }
    return (EOPNOTSUPP);
}
```

Using a zone:

```c
struct secdev_packet *pkt;

pkt = uma_zalloc(secdev_packet_zone, M_WAITOK | M_ZERO);
/* ... use pkt ... */
uma_zfree(secdev_packet_zone, pkt);
```

The security advantages of a UMA zone over `malloc`:

A zone can have a constructor and destructor that initialize or finalize objects. This can guarantee that every object returned to the caller is in a known state.

A zone is named, so `vmstat -z` attributes allocations to it. This helps detect leaks and unusual memory patterns in specific subsystems.

The pool of objects can be drained under memory pressure. A malloc allocation is held for its lifetime; a UMA zone object can be returned to the kernel when freed if the pool is above its high-water mark.

The security pitfalls:

An object returned to the zone is not automatically zeroed. If the zone holds objects that may contain sensitive data, either add a destructor that zeros, or zero explicitly before freeing:

```c
explicit_bzero(pkt, sizeof(*pkt));
uma_zfree(secdev_packet_zone, pkt);
```

Because UMA reuses objects quickly, an object you just freed may be handed to another caller almost immediately. If the other caller is a different thread in another subsystem, residual data could flow between them. The fix, again, is explicit zeroing.

A destructor function passed to `uma_zcreate` is called when an object is about to be freed back to the kernel (not when it returns to the pool). For zeroing on every free, use `M_ZERO` on `uma_zalloc` (which zeros on allocation, equivalent to `bzero` immediately after) or zero explicitly.

UMA zones are not appropriate for every driver allocation. For one-off or irregular allocations, `malloc(9)` is simpler. For high-frequency fixed-size objects, UMA wins on performance and makes memory accounting easier. Choose based on access pattern.

### Reference Counting for Shared Objects

When an object in your driver can be held by multiple contexts (a softc that is referenced by both a callout and user-space file descriptors, for example), reference counting is the canonical tool for lifetime management. The `refcount(9)` family in `/usr/src/sys/sys/refcount.h` provides simple atomic helpers:

```c
refcount_init(&obj->refcnt, 1);  /* initial reference */
refcount_acquire(&obj->refcnt);  /* acquire an additional reference */
if (refcount_release(&obj->refcnt)) {
    /* last reference dropped; caller frees */
    free(obj, M_SECDEV);
}
```

The invariant is simple: each context that holds a pointer to the object also holds a reference. When it finishes, it releases. Whichever context is last to release is responsible for freeing.

Used correctly, refcounts prevent the classic "who frees it" ambiguity. Used incorrectly (unbalanced acquires and releases), they produce leaks or use-after-frees. The discipline is:

Every path that obtains a pointer to the object acquires a reference.

Every path that releases the pointer calls `refcount_release` and checks the return value.

A single "owning" reference is held by whoever created the object; the owner is the default releaser.

Even simple refcount usage catches a large class of lifetime bugs. For complex drivers with multiple concurrent contexts, refcounts are indispensable.

### Wrapping Up Section 4

The FreeBSD allocator is safe if used correctly. The rules are simple: check `M_NOWAIT` returns, prefer `M_ZERO`, zero sensitive data before freeing, pair every `malloc` with exactly one `free` on every code path, set pointers to NULL after freeing, drain async activity before freeing structures those activities touch, and use a per-driver `malloc_type` for accountability. A driver that follows these rules will not have leaks, use-after-frees, or double-frees.

The next section turns to a class of bug that is related but different: races and TOCTOU bugs. These are where two threads or two moments in time interact badly, and where security consequences often hide.

## Section 5: Preventing Race Conditions and TOCTOU Bugs

A race condition happens when the correctness of a driver depends on the relative timing of events it does not control. A TOCTOU bug (Time of Check to Time of Use) is a special kind of race where the driver checks a condition at one moment, then acts on the same data at a later moment, assuming the condition is still true. In between, something changes. The check is valid. The action is valid. The combination is a bug. From a security standpoint, races and TOCTOU bugs are some of the most dangerous faults a driver can have, because they often allow an attacker to bypass checks that look correct when read in isolation.

Chapter 19 already covered concurrency, locks, and synchronization primitives. The goal there was correctness. This section revisits the same tools through a security lens. We are not asking "will my driver crash". We are asking "can an attacker arrange timing so that a check I wrote is useless".

### How Races Arise in Drivers

A FreeBSD driver operates in a multi-threaded environment. Several things can be happening at once:

Two different user processes can call `read(2)`, `write(2)`, or `ioctl(2)` on the same device file. If the driver has a single `softc`, both calls run against the same state.

One thread can be running your ioctl handler while an interrupt handler for the same device fires on another CPU.

A user thread can be in the middle of your driver while a callout or taskqueue entry scheduled earlier also runs.

The device can be unplugged, causing `detach` to run while any of the above is still in progress.

Any shared data touched by more than one of these contexts, without proper synchronization, is a potential race. The race becomes a security problem when the shared data gates access, validates input, tracks buffer sizes, or holds lifetime information.

### The TOCTOU Pattern

The simplest TOCTOU pattern in a driver looks like this:

```c
if (sc->sc_initialized) {
    use(sc->sc_buffer);
}
```

Read it carefully. Nothing about it is obviously wrong. The driver checks that the buffer is initialized, then uses it. But if another thread can set `sc->sc_initialized` to `false` and free `sc->sc_buffer` between the check and the use, the use touches freed memory. The attacker does not need to corrupt the flag or the pointer. They only need to arrange timing.

A more subtle TOCTOU happens with user memory:

```c
if (args->len > MAX_LEN)
    return (EINVAL);
error = copyin(args->data, kbuf, args->len);
```

Look at `args`. If it was already copied in, this is safe. But if `args` still points into user space, a second user thread can change `args->len` between the check and the `copyin`. The check validates the old length. The copy uses the new length. If the new length exceeds `MAX_LEN`, the `copyin` overruns `kbuf`.

The fix is copy-then-check, which we already covered in Section 3. The reason this technique exists is precisely because TOCTOU on user memory is a real attack vector. Always copy user data into kernel space first, then validate, then use.

### Real-World Example: Ioctl with a Path

Imagine an ioctl that takes a path and does something with the file:

```c
/* UNSAFE */
static int
secdev_open_path(struct secdev_softc *sc, struct secdev_path_arg *args)
{
    struct nameidata nd;
    int error;

    /* Check path length */
    if (strnlen(args->path, sizeof(args->path)) >= sizeof(args->path))
        return (ENAMETOOLONG);

    NDINIT(&nd, LOOKUP, 0, UIO_USERSPACE, args->path);
    error = namei(&nd);
    /* ... */
}
```

This has two races. First, `args->path` is still in user space if `args` was not copied in; a user thread can change it between the `strnlen` check and `namei`. Second, even if `args` was copied, using `UIO_USERSPACE` tells the VFS layer to read the path from user space, at which point the process can modify it again before VFS reads it. The fix is to copy the path into kernel space with `copyinstr(9)`, validate it as a kernel string, then pass it to VFS with `UIO_SYSSPACE`.

```c
/* SAFE */
static int
secdev_open_path(struct secdev_softc *sc, struct secdev_path_arg *args)
{
    struct nameidata nd;
    char kpath[MAXPATHLEN];
    size_t done;
    int error;

    error = copyinstr(args->path, kpath, sizeof(kpath), &done);
    if (error != 0)
        return (error);

    NDINIT(&nd, LOOKUP, 0, UIO_SYSSPACE, kpath);
    error = namei(&nd);
    /* ... */
}
```

The corrected version copies the path into the kernel exactly once, validates it (by virtue of `copyinstr` bounding the length and guaranteeing a NUL terminator), then hands a stable kernel string to the VFS layer. The user process can change `args->path` as often as it likes; we are no longer reading from there.

### Shared State and Locking

For races between concurrent in-kernel contexts, the tool is a lock. FreeBSD offers several. The most common in drivers are:

`mtx_t`, a mutex, created with `mtx_init(9)`. Mutexes are fast, short, and must not be held across sleeps. Use them to protect a small critical section.

`sx_t`, a shared-exclusive lock, created with `sx_init(9)`. Shared-exclusive locks can be held across sleeps. Use them when the critical section includes something like `malloc(M_WAITOK)` or a VFS call.

`struct rwlock`, a read-write lock, for the read-mostly case. Multiple readers can hold the lock in shared mode; an exclusive writer excludes all readers.

`struct mtx` paired with condition variables (`cv_init(9)`, `cv_wait(9)`, `cv_signal(9)`) for producer-consumer patterns.

The rules for safe locking are simple and absolute:

Define exactly what data each lock protects. Write it in a comment next to the softc field.

Acquire the lock before reading or writing the protected data. Release it afterwards.

Do not hold locks longer than necessary. Long critical sections hurt performance and increase deadlock risk.

Acquire multiple locks in a consistent order across all code paths. Inconsistent ordering leads to deadlock.

Do not sleep while holding a mutex. Convert to an sx lock or drop the mutex first.

Do not call into user space (`copyin`, `copyout`) while holding a mutex. Copy first, then lock. Release, then copy back.

### A Closer Look: Fixing a Racy Driver

Consider the following minimal handler:

```c
/* UNSAFE: races on sc_open */
static int
secdev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;

    if (sc->sc_open)
        return (EBUSY);
    sc->sc_open = true;
    return (0);
}

static int
secdev_close(struct cdev *dev, int fflags, int devtype, struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;

    sc->sc_open = false;
    return (0);
}
```

The intent is that only one process can have the device open at a time. The bug is that `sc_open` is checked and set without a lock. Two concurrent `open(2)` calls can both read `sc_open == false`, both decide they are the first, and both set it to `true`. Both succeed. Now two processes share a device that was meant to be exclusive. This is a real-world bug class that has affected real drivers. Fix:

```c
/* SAFE */
static int
secdev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;
    int error = 0;

    mtx_lock(&sc->sc_mtx);
    if (sc->sc_open)
        error = EBUSY;
    else
        sc->sc_open = true;
    mtx_unlock(&sc->sc_mtx);
    return (error);
}

static int
secdev_close(struct cdev *dev, int fflags, int devtype, struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;

    mtx_lock(&sc->sc_mtx);
    sc->sc_open = false;
    mtx_unlock(&sc->sc_mtx);
    return (0);
}
```

Now the read and the write happen inside a single critical section. Only one caller at a time can be inside that section, so the check-then-set sequence is atomic from the perspective of any other caller.

### Lifetime Races at Detach

The hardest races in drivers are lifetime races around `detach`. The device goes away, but a user thread is still inside your ioctl handler, or an interrupt is in flight, or a callout is pending. If `detach` frees the softc while one of these references it, you have a use-after-free.

FreeBSD gives you tools to handle this:

`callout_drain(9)` waits for any scheduled callout to finish before returning. Call it in `detach` before freeing anything the callout touches.

`taskqueue_drain(9)` and `taskqueue_drain_all(9)` wait for pending tasks to complete.

`destroy_dev(9)` marks a character device as gone and waits for all in-flight threads to leave the device's d_* methods before returning. After `destroy_dev`, no new threads can enter and no old threads remain.

`bus_teardown_intr(9)` removes an interrupt handler and waits for any in-flight instance of that handler to complete.

A correct `detach` function in a driver that has all of these resources looks roughly like:

```c
static int
secdev_detach(device_t dev)
{
    struct secdev_softc *sc = device_get_softc(dev);

    /* 1. Prevent new user-space entries. */
    if (sc->sc_cdev != NULL)
        destroy_dev(sc->sc_cdev);

    /* 2. Drain asynchronous activity. */
    callout_drain(&sc->sc_callout);
    taskqueue_drain_all(sc->sc_taskqueue);

    /* 3. Tear down interrupts (if any). */
    if (sc->sc_intr_cookie != NULL)
        bus_teardown_intr(dev, sc->sc_irq, sc->sc_intr_cookie);

    /* 4. Free resources. */
    /* ... */

    /* 5. Destroy lock last. */
    mtx_destroy(&sc->sc_mtx);
    return (0);
}
```

The order matters. We first stop accepting new work, then drain all in-flight work, then free the resources that the in-flight work was using. If we freed resources first and drained second, a callout still running could touch freed memory. That is a classic detach-time use-after-free, and it is a security bug, not just a crash.

### Atomics and Lock-Free Code

FreeBSD provides atomic operations (`atomic_add_int`, `atomic_cmpset_int`, and so on) in `/usr/src/sys/sys/atomic_common.h` and architecture-specific headers. Atomics are useful for counters, reference counts, and simple flags. They are not a substitute for locks when multiple related fields must change together.

A common beginner mistake is to say "I will use an atomic to avoid a lock". Sometimes this is correct. Often it leads to a subtly broken data structure because the atomic operation only makes one field safe, while the code really needed two fields updated together.

The safe rule is: if you can express the invariant with a single atomic read or write, an atomic may be appropriate. If the invariant spans multiple fields or a compound condition, use a lock.

### Refcounts as a Lifetime Tool

When an object can be referenced from multiple contexts, a refcount helps manage lifetime. `refcount_init`, `refcount_acquire`, and `refcount_release` (declared in `/usr/src/sys/sys/refcount.h`) give you a simple atomic refcount. The last release returns true, at which point the caller is responsible for freeing the object.

Refcounts solve the classic problem where context A and context B both hold a pointer to an object. Either can finish with it first. The one that finishes last frees it. Neither needs to know whether the other is done, because the refcount tracks that for them.

A driver that uses a refcount on its softc, or on per-open state, can release that state safely even under concurrent access. The cost is some care at every entry and exit point to balance acquires and releases.

### Ordering and Memory Barriers

Modern CPUs reorder memory accesses. A write in your code may become visible to other CPUs in a different order than it was issued. This is usually invisible because locks on FreeBSD include the necessary barriers. When writing lock-free code, you may need explicit barriers (`atomic_thread_fence_acq`, `atomic_thread_fence_rel`, and variants). For almost all driver code, using a lock removes the need to think about barriers. That is another reason to prefer locks over hand-rolled lock-free constructs when you are still learning.

### Signal and Sleep Safety

If your driver sleeps waiting for an event, using `msleep(9)`, `cv_wait(9)`, or `sx_sleep(9)`, use the interruptible variant (`msleep(..., PCATCH)`) when the wait is initiated by user space. Otherwise a stuck device can hold a process in an uninterruptible state forever, and a sufficiently patient attacker can use that to exhaust process slots. The interruptible wait lets the process be signalled.

Always check the return value of a sleep. If it returns a non-zero value, the sleep was interrupted (either by a signal or by another condition), and the driver should typically unwind and return to user space. Don't assume the condition is true just because the sleep returned.

### Rate Limiting and Resource Exhaustion

A final race-related security concern is resource exhaustion. If an attacker can call your ioctl a million times per second, and each call allocates a kilobyte of kernel memory that is not freed until close, they can drive the system out of memory. This is a denial of service attack, and a careful driver defends against it.

The defenses are: cap per-open resource use, cap global resource use, rate-limit expensive operations. FreeBSD provides `eventratecheck(9)` and `ppsratecheck(9)` in `/usr/src/sys/sys/time.h` for rate limiting, and you can build your own counters where needed. The principle is that the cost to call your driver should not be wildly asymmetric: if a single call allocates megabytes of state, either the caller needs a privilege check or the driver needs a hard cap.

### Epoch-Based Reclamation: A Lock-Free Reader Idiom

For read-mostly data structures where readers must never block and writers are rare, FreeBSD provides an epoch-based reclamation framework in `/usr/src/sys/sys/epoch.h`. Readers enter an epoch, access the shared data without taking a lock, and exit the epoch. Writers update the data (usually by replacing a pointer) and then wait for all readers currently in an epoch to exit before freeing the old data.

The idiom is useful for driver code that has frequent reads on a hot path and wants to avoid locking overhead there. For example, a network driver that looks up a rule from a routing-table-like structure on every packet may want readers to run lock-free.

```c
epoch_enter(secdev_epoch);
rule = atomic_load_ptr(&sc->sc_rules);
/* use rule; must not outlive the epoch */
do_stuff(rule);
epoch_exit(secdev_epoch);
```

A writer replacing the rule set:

```c
new_rules = build_new_rules();
old_rules = atomic_load_ptr(&sc->sc_rules);
atomic_store_ptr(&sc->sc_rules, new_rules);
epoch_wait(secdev_epoch);
free(old_rules, M_SECDEV);
```

`epoch_wait` blocks until all readers that entered before the store have exited. After it returns, no reader can still be using `old_rules`, so it is safe to free.

The security considerations with epochs are subtle:

A reader inside an epoch may hold a pointer to something that is about to be replaced. The reader must finish using the pointer before exiting the epoch; any use after the exit is a use-after-free.

A reader inside an epoch cannot sleep. The epoch is an asymmetric lock: writers wait on readers, so a reader that sleeps can starve writers indefinitely.

The writer must ensure that the replacement operation is atomic from a reader's perspective. For a single pointer, an atomic store does the job. For more complex updates, two epochs or a read-copy-update sequence may be needed.

Used correctly, epochs give very high performance on read-heavy workloads. Used incorrectly (a reader that sleeps, or a writer that fails to wait), they produce races that are hard to reproduce and hard to diagnose. Beginners should prefer locks until the performance profile justifies the complexity of epoch-based code.

### Wrapping Up Section 5

Races and TOCTOU bugs are timing-based bugs. They happen when two contexts touch shared data without coordination, or when a driver checks a condition and acts on it at two different times. The tools to prevent them are straightforward: copy user data into the kernel once and work from the copy; use a lock around every access to shared mutable state; define what each lock protects and hold it across the full check-and-act sequence; drain asynchronous work before freeing the structures it touches; use refcounts for multi-context lifetime management.

None of this is new to concurrency programming. What is new is the mindset: a race in a driver is not merely a correctness problem. It is a security problem, because an attacker can often arrange the timing they need to exploit it. The next section steps back from timing and looks at a different kind of defense: privilege checks, credentials, and access control.

## Section 6: Access Control and Privilege Enforcement

Not every operation a driver exposes should be available to every user. Reading a temperature sensor may be fine for everyone. Reprogramming the device's firmware should require privilege. Writing raw bytes to a storage controller should probably require more than that. This section is about how a FreeBSD driver decides whether the caller is allowed to do what they are asking, using the kernel's credential and privilege machinery.

The tools are `struct ucred`, `priv_check(9)` and `priv_check_cred(9)`, jail-aware checks, securelevel checks, and the broader MAC and Capsicum frameworks.

### The Caller's Credential: struct ucred

Every thread running in the FreeBSD kernel carries a credential, a pointer to a `struct ucred`. The credential records who the thread is running as, which jail they are confined to, which groups they belong to, and other security attributes. From inside a driver, the credential is almost always reached via `td->td_ucred`, where `td` is the `struct thread *` passed to your entry point.

The structure is declared in `/usr/src/sys/sys/ucred.h`. The fields most relevant to drivers are:

`cr_uid`, the effective user ID. Usually what you check to answer "is this root".

`cr_ruid`, the real user ID.

`cr_gid`, the effective group ID.

`cr_prison`, a pointer to the jail the process is in. All processes have one. Unjailed processes belong to `prison0`.

`cr_flags`, a small set of flags including `CRED_FLAG_CAPMODE`, which indicates capability mode (Capsicum).

Do not check `cr_uid == 0` as your privilege gate. That is a common mistake and it is almost always wrong. The correct gate is `priv_check(9)`, which handles jails, securelevel, and MAC policies correctly. Checking `cr_uid` manually bypasses all of that and gives root inside a jail the same power as root on the host, which is not what jails are for.

### priv_check and priv_check_cred

The canonical primitive for "may the caller do this privileged thing" is `priv_check(9)`. Its prototype, from `/usr/src/sys/sys/priv.h`:

```c
int priv_check(struct thread *td, int priv);
int priv_check_cred(struct ucred *cred, int priv);
```

`priv_check` operates on the current thread. `priv_check_cred` operates on an arbitrary credential; you use it when the credential to check is not the running thread's, for example when validating an operation on behalf of a file that was opened earlier.

Both return 0 if the privilege is granted and an errno (typically `EPERM`) if not. The driver's pattern is almost always:

```c
error = priv_check(td, PRIV_DRIVER);
if (error != 0)
    return (error);
```

The `priv` argument selects one of several dozen named privileges. The full list lives in `/usr/src/sys/sys/priv.h` and covers areas like filesystem, networking, virtualization, and drivers. For most device drivers, the relevant names are:

`PRIV_DRIVER`, the generic driver privilege. Grants access to operations restricted to administrators.

`PRIV_IO`, raw I/O to hardware. More restrictive than `PRIV_DRIVER`, appropriate for operations that bypass the driver's usual abstractions and talk directly to hardware.

`PRIV_KLD_LOAD`, used by the module loader. You will not typically use this from a driver.

`PRIV_NET_*`, used by network-related operations.

Several dozen more. Read the list in `priv.h` and pick the most specific match for the operation being gated. `PRIV_DRIVER` is a reasonable default when nothing more specific fits.

A real-world example from the kernel: in `/usr/src/sys/dev/mmc/mmcsd.c`, the driver checks `priv_check(td, PRIV_DRIVER)` before allowing certain ioctls that would let a user reprogram the storage controller. In `/usr/src/sys/dev/syscons/syscons.c`, the console driver checks `priv_check(td, PRIV_IO)` before allowing operations that manipulate the hardware directly, since those bypass the normal tty abstraction.

### Jail Awareness

FreeBSD jails (jail(8) and jail(9)) partition the system into compartments. Processes inside a jail share the host's kernel but have a restricted view of the system: their own hostname, their own network visibility, their own filesystem root, and reduced privileges. Inside a jail, `priv_check` refuses many privileges that would otherwise be granted to root. This is one of the main reasons to use `priv_check` instead of checking `cr_uid == 0`.

Some operations, however, make no sense inside a jail at all. Reprogramming device firmware, for example, is a host operation. A jailed root user should never be able to do it. For these, add an explicit jail check:

```c
if (jailed(td->td_ucred))
    return (EPERM);
error = priv_check(td, PRIV_DRIVER);
if (error != 0)
    return (error);
```

The `jailed()` macro, defined in `/usr/src/sys/sys/jail.h`, returns true if the credential's prison is anything other than `prison0`. For operations that should never be performed from within a jail, check this first.

For operations that should be allowed inside a jail but with restrictions, use the jail's own fields. `cred->cr_prison->pr_flags` carries per-jail flags; the jail framework also has helpers for checking whether certain capabilities are allowed in the jail. In most driver work you will not go beyond the simple `jailed()` check.

### Securelevel

FreeBSD supports a systemwide setting called securelevel. At securelevel 0, the system behaves normally. At higher securelevels, certain operations are restricted even for root: raw disk writes may be disabled, the system time cannot be set backwards, kernel modules cannot be unloaded, and so on. The rationale is that on a well-secured server, raising the securelevel at boot means an attacker who later gains root cannot disable logging, install a rootkit module, or rewrite core system files.

For drivers, the relevant helpers are declared in `/usr/src/sys/sys/priv.h`:

```c
int securelevel_gt(struct ucred *cr, int level);
int securelevel_ge(struct ucred *cr, int level);
```

Their return values are counterintuitive and worth studying carefully. They return 0 when the securelevel is **not** above or at the threshold (that is, the operation is allowed), and `EPERM` when the securelevel **is** above or at the threshold (the operation should be denied). In other words, the return value is ready to be used directly as an error code.

The usage pattern for a driver that should refuse to modify hardware at securelevel 1 or higher is:

```c
error = securelevel_gt(td->td_ucred, 0);
if (error != 0)
    return (error);
```

Read carefully: this says "return an error if the securelevel is greater than 0". When securelevel is 0 (normal), `securelevel_gt(cred, 0)` returns 0 and the check passes. When securelevel is 1 or higher, it returns `EPERM` and the operation is refused.

Most drivers do not need securelevel checks. They make sense for operations that are potentially system-destabilizing: reprogramming firmware, writing to raw disk sectors, lowering the system clock, and so on.

### Layering Checks

A driver that wants to be defense-in-depth can layer these checks:

```c
static int
secdev_reset_hardware(struct secdev_softc *sc, struct thread *td)
{
    int error;

    /* Not inside a jail. */
    if (jailed(td->td_ucred))
        return (EPERM);

    /* Not at elevated securelevel. */
    error = securelevel_gt(td->td_ucred, 0);
    if (error != 0)
        return (error);

    /* Must have driver privilege. */
    error = priv_check(td, PRIV_DRIVER);
    if (error != 0)
        return (error);

    /* Okay, do the dangerous thing. */
    return (secdev_do_reset(sc));
}
```

Each check answers a different question. `jailed()` asks whether we are in the right security domain. `securelevel_gt` asks whether the system administrator has told the kernel to refuse this kind of operation. `priv_check` asks whether this particular thread has the appropriate privilege.

In many drivers, only the `priv_check` is strictly necessary, because it handles jails and securelevel through the MAC framework and the privilege definitions themselves. The explicit `jailed()` and `securelevel_gt` calls are appropriate for operations with known host-wide consequences. When in doubt, start with `priv_check(td, PRIV_DRIVER)` and add more layers only when you can explain what each additional check buys.

### The Credential on Open, Ioctl, and Other Paths

When designing privilege checks, think about where in the driver's lifecycle they live. There are two main places:

At open time. If only privileged users should be able to open the device, check privileges in `d_open`. This is simplest and gives per-open enforcement: once a user has opened the device, they are free to do what that device allows. This is the model used, for example, by `/dev/mem`, which is openable only with appropriate privilege.

At operation time. If the device supports multiple operations with different privilege requirements, check each operation independently. A storage controller might allow reading device status to any user, reading SMART data to the owner of the device file, and triggering firmware update only to users with `PRIV_DRIVER`. Each operation has its own gate.

A driver can combine both: a privilege check on open to keep unprivileged users out entirely, and additional checks on specific ioctls for operations that need more.

An open-time check is easy to implement:

```c
static int
secdev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    int error;

    error = priv_check(td, PRIV_DRIVER);
    if (error != 0)
        return (error);

    /* ... rest of open logic ... */
    return (0);
}
```

An ioctl-time check follows the same pattern; the `struct thread *td` argument is available in every entry point.

### Device File Permissions

Independent of in-driver privilege checks, FreeBSD also applies the usual UNIX permission model to device files themselves. When your driver calls `make_dev_s` or `make_dev_credf` to create a device node, you choose an owner, group, and mode. Those apply at the filesystem level: a user who fails the permission check on the device node never reaches your `d_open`.

The `make_dev_args` structure, declared in `/usr/src/sys/sys/conf.h`, includes `mda_uid`, `mda_gid`, and `mda_mode` fields. The pattern is:

```c
struct make_dev_args args;

make_dev_args_init(&args);
args.mda_devsw = &secdev_cdevsw;
args.mda_uid = UID_ROOT;
args.mda_gid = GID_OPERATOR;
args.mda_mode = 0640;
args.mda_si_drv1 = sc;
error = make_dev_s(&args, &sc->sc_cdev, "secdev");
```

`UID_ROOT` and `GID_OPERATOR` are conventional symbolic names. The mode `0640` means owner can read and write, group can read, others have no access. Choose these thoughtfully. A device that could expose sensitive data or cause hardware damage should not be world-readable or world-writable.

The usual pattern for a privileged device is mode `0600` (root-only) or `0660` (root and a specific group, often `operator` or `wheel`). Mode `0640` is common for devices readable by a trusted group for monitoring purposes. Modes like `0666` (world-writable) are almost never appropriate, even for simple pseudo-devices, unless the device really does nothing that should be restricted.

### Devfs Rules

Even if your driver creates the device node with a conservative mode, the system administrator can change that through devfs rules. A devfs rule can relax or restrict permissions based on device name, jail, and other criteria. Your driver should not assume the mode it set at creation is the mode the device will have at runtime; it should continue to apply its in-kernel checks regardless. The filesystem mode and the in-kernel `priv_check` defend different attackers; keep both.

### The MAC Framework

The FreeBSD Mandatory Access Control framework, declared in `/usr/src/sys/security/mac/`, lets policy modules hook into the kernel and make access decisions based on richer labels than UNIX permissions. A MAC policy can, for example, restrict which users can access which devices even if UNIX permissions allow it, or log every use of a sensitive operation.

For driver authors, the point is this: `priv_check` already consults the MAC framework. When you use `priv_check`, you are opting into whatever MAC policies the administrator has configured. If you bypass `priv_check` and roll your own privilege check using `cr_uid`, you bypass MAC as well. That is one more reason to always use `priv_check`.

Writing your own MAC policy module is beyond the scope of this chapter; the MAC framework is a substantial subject and has its own documentation. The key takeaway is simply that MAC exists, `priv_check` honors it, and you should not fight it.

**A brief note on MAC policies shipped with FreeBSD.** The base system includes several MAC policies as loadable modules: `mac_bsdextended(4)` for file-system rule lists, `mac_portacl(4)` for network-port access control, `mac_biba(4)` for Biba integrity policy, `mac_mls(4)` for Multi-Level Security labels, and `mac_partition(4)` for partitioning processes into isolated groups. None of these need to be understood in detail by a driver author; the key point is that your driver, by using `priv_check`, gets their policy decisions for free. An administrator who enables `mac_bsdextended` gets additional filesystem-level restrictions; your driver does not need to know.

**MAC and the device node.** When you create a device with `make_dev_s`, the MAC framework may assign a label to the device node. Policies consult that label when access is attempted. A driver does not interact with labels directly; the framework handles it. But understanding that a label exists explains why, on a MAC-enabled system, access to your device may be refused even when UNIX permissions allow it. That is not a bug; it is MAC doing its job.

### Capsicum and Capability Mode

Capsicum, declared in `/usr/src/sys/sys/capsicum.h`, is a capability system bolted onto FreeBSD. A process in capability mode has lost access to most global namespaces (no new file opens, no network with side effects, no arbitrary ioctl, and so on). It can only operate on file descriptors it already holds, and those file descriptors may themselves have limited rights (read only, write only, certain ioctls only, and so on).

Capsicum was introduced to FreeBSD through the work of Robert Watson and collaborators. It sits alongside the traditional UNIX permission model and adds a second, more granular layer. Where UNIX permissions ask "can this user access this resource by name", Capsicum asks "does this process have a capability for this specific object". The two layers work together: the user must have UNIX permission to open the file in the first place, but once the file descriptor exists, Capsicum can further restrict what the holder of the descriptor can do with it.

For a driver, the main Capsicum concern is: some of your ioctls may be inappropriate for a process in capability mode. The helper `IN_CAPABILITY_MODE(td)`, defined in `capsicum.h`, tells you whether the calling thread is in capability mode. A driver can check it and refuse operations that are unsafe:

```c
if (IN_CAPABILITY_MODE(td))
    return (ECAPMODE);
```

This is appropriate for operations with global side effects that a capability-mode process should not have access to. Examples might be an ioctl that reconfigures the global driver state, an ioctl that affects other processes or other file descriptors, or an ioctl that performs an operation that requires querying the global filesystem namespace. If your driver's ioctl needs to touch something that is not already named by the file descriptor it was called on, a capability-mode check is appropriate.

For most driver operations, however, the Capsicum story is simpler: the process that holds the file descriptor was granted the rights it needed when the descriptor was given to it. The driver does not need to re-check those rights; the file-descriptor layer already did. Just make sure your driver supports the normal cap-rights flow (it almost certainly does by default) and consider which individual ioctls should be marked with `CAP_IOCTL_*` rights at the VFS layer.

**Cap rights at ioctl granularity.** FreeBSD allows a file descriptor to be restricted to a specific subset of ioctls via `cap_ioctls_limit(2)`. For example, a process can hold a file descriptor that allows `FIOASYNC` and `FIONBIO` but no other ioctls. The restriction is enforced by the VFS layer, not by your driver, but the set of ioctls you expose is what defines what can be selected for restriction. A driver that implements only meaningful, well-documented ioctls makes it easier for consumers to apply sensible cap-ioctl restrictions.

**Examining Capsicum usage in the tree.** For real-world examples of Capsicum-aware code, look at `/usr/src/sys/net/if_tuntap.c` alongside the core capability files under `/usr/src/sys/kern/sys_capability.c`. Most individual drivers rely on the VFS layer to enforce `caprights`, and only add an explicit `IN_CAPABILITY_MODE(td)` check on the handful of operations with global side effects. The pattern is consistent: preserve the normal behavior, add an `IN_CAPABILITY_MODE` check where operations would be unsafe, and document which ioctls are sandbox-safe.

### Sysctls With Security Flags

Many drivers expose tunables and statistics through sysctls. A sysctl that exposes sensitive information, or that can be set to change driver behaviour, should use appropriate flags. From `/usr/src/sys/sys/sysctl.h`:

`CTLFLAG_SECURE` (value `0x08000000`) asks the sysctl framework to consult `priv_check(PRIV_SYSCTL_SECURE)` before allowing the operation. It is useful for sysctls that should not be changed at elevated securelevel.

`CTLFLAG_PRISON` allows the sysctl to be visible and writable from inside a jail (rarely wanted for drivers).

`CTLFLAG_CAPRD` and `CTLFLAG_CAPWR` allow the sysctl to be read or written from capability mode. By default, sysctls are inaccessible in capability mode.

`CTLFLAG_TUN` makes the sysctl settable as a loader tunable (from `/boot/loader.conf`).

`CTLFLAG_RD` vs `CTLFLAG_RW` determines read-only vs read-write access; prefer `CTLFLAG_RD` for anything that exposes state, and be deliberate about what you make writable.

A sysctl that exposes a driver-internal buffer for debugging should typically be `CTLFLAG_RD | CTLFLAG_SECURE` at minimum, and possibly not exist at all in production builds.

### A Complete Privilege-Gated Ioctl

Putting the pieces together, here is what a privilege-gated ioctl looks like, end to end:

```c
static int
secdev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
    struct secdev_softc *sc = dev->si_drv1;
    int error;

    switch (cmd) {
    case SECDEV_GET_STATUS:
        /* Anyone with the device open can do this. */
        error = secdev_get_status(sc, (struct secdev_status *)data);
        break;

    case SECDEV_RESET:
        /* Resetting is privileged, jail-restricted, and securelevel-sensitive. */
        if (jailed(td->td_ucred)) {
            error = EPERM;
            break;
        }
        error = securelevel_gt(td->td_ucred, 0);
        if (error != 0)
            break;
        error = priv_check(td, PRIV_DRIVER);
        if (error != 0)
            break;
        error = secdev_do_reset(sc);
        break;

    default:
        error = ENOTTY;
        break;
    }

    return (error);
}
```

Different commands get different gates. The status command is unprivileged, since it just reads state. The reset command is the danger case, and it goes through the full layered check.

### Wrapping Up Section 6

Access control in a FreeBSD driver is a collaboration between several layers. Filesystem permissions on the device node decide who can open it. The `priv_check(9)` family of functions decides whether a thread may perform a given privileged operation. Jail checks decide whether the operation makes sense in the caller's security domain. Securelevel checks decide whether the system administrator has allowed this class of operation at all. The MAC framework lets policy modules add their own opinions on top. Capsicum rights limit what a capability-confined process can do.

The correct use of these tools comes down to a short list of rules: check the caller's credentials at the right points, prefer `priv_check` over ad-hoc UID checks, add `jailed()` and `securelevel_gt` when the operation has host-wide consequences, pick the most specific `PRIV_*` constant that fits the operation, and set conservative device-file modes in `make_dev_s`.

The next section looks at a different kind of leak: not a privilege escape, but an information escape. Even operations that are properly gated can inadvertently reveal kernel memory contents if they are not written carefully.

## Section 7: Protecting Against Information Leaks

An information leak happens when kernel memory that should not be visible to user space is nonetheless copied out to user space. The classic form is returning the contents of a structure to the user without first initializing the structure. Any padding bytes between fields, or trailing bytes after the last field, contain whatever was on the kernel stack or in a freshly allocated page the last time that memory was used. That could be a password, a pointer that would help defeat ASLR, an encryption key, or anything else.

Information leaks are sometimes dismissed as "not that bad". They are. In modern attack chains, an information leak is often the first step: it defeats the kernel's address-space-layout-randomization (kASLR) and makes other exploits reliable. A bug class that starts with "just leaks a few bytes" often ends with "attacker gains kernel code execution".

### How Information Leaks Happen

There are three main ways a driver leaks information:

**Uninitialized structure fields copied to user space.** A structure has N defined fields plus padding and alignment slots. The code fills in the N fields and calls `copyout`. The padding goes along for the ride, carrying whatever uninitialized stack memory happened to be there.

**Partially initialized buffers.** The driver allocates a buffer, fills in some of it, and copies the whole buffer to user space. The uninitialized tail carries heap contents.

**Oversized replies.** The driver is asked for `N` bytes, but returns a buffer of size `M > N`. The extra `M - N` bytes contain whatever was in the tail of the source buffer.

**Reading beyond a NUL.** For string data, the driver copies a buffer up to its allocated size instead of up to the NUL terminator. The bytes after the NUL can carry any data that happened to be in that buffer earlier.

Each of these is easy to create by accident and easy to prevent once you know the pattern.

### The Padding Problem

Consider this structure:

```c
struct secdev_info {
    uint32_t version;
    uint64_t flags;
    uint16_t id;
    char name[32];
};
```

On a 64-bit system, the compiler inserts padding to align `flags` to 8 bytes. Between `version` (4 bytes) and `flags` (8 bytes), there are 4 bytes of padding. After `id` (2 bytes) and before `name` (1-byte alignment), there are 6 more bytes of padding at the end if the structure is sized up to a multiple of 8.

If your code does:

```c
struct secdev_info info;

info.version = 1;
info.flags = 0x12345678;
info.id = 42;
strncpy(info.name, "secdev0", sizeof(info.name));

error = copyout(&info, args->buf, sizeof(info));
```

then the padding bytes, which you never set, go out to user space. They contain whatever stack memory happened to be at those positions when the function was entered. That is an information leak.

The fix is universal and cheap: zero the structure first.

```c
struct secdev_info info;

bzero(&info, sizeof(info));      /* or memset(&info, 0, sizeof(info)) */
info.version = 1;
info.flags = 0x12345678;
info.id = 42;
strncpy(info.name, "secdev0", sizeof(info.name));

error = copyout(&info, args->buf, sizeof(info));
```

Now the padding is zero, as is any field you forgot to set. The cost is one call to `bzero`; the benefit is that your driver cannot leak kernel memory through this structure, no matter what fields are added later. Always zero structures before copyout.

An equivalent pattern using designated initializers works when you are declaring and initializing in one step:

```c
struct secdev_info info = { 0 };  /* or { } in some standards */
info.version = 1;
/* ... */
```

The `= { 0 }` zeros all bytes including padding. Combine this with setting the specific fields afterwards, and you have a clean pattern.

### The Heap Allocation Case

When you allocate a buffer with `malloc(9)` and fill it before returning to user space, you have the same issue. Always use `M_ZERO` to zero-initialize, or explicitly zero the buffer before writing to it:

```c
buf = malloc(size, M_SECDEV, M_WAITOK | M_ZERO);
```

Even if you intend to fill every byte, using `M_ZERO` is cheap insurance: if a bug causes a partial fill, the unfilled bytes are zero rather than stale heap contents.

### Oversized Replies

A subtle form of leak happens when the driver returns more data than the user asked for. Imagine an ioctl that returns a list of items:

```c
/* User asks for up to user_len bytes of list data. */
if (user_len > sc->sc_list_bytes)
    user_len = sc->sc_list_bytes;

error = copyout(sc->sc_list, args->buf, sc->sc_list_bytes);  /* BUG: wrong length */
```

The driver copies `sc_list_bytes` bytes regardless of what the user asked for. If `sc_list_bytes > user_len`, the driver writes past `args->buf`, which is a different bug (buffer overflow in user space). If the driver is writing to a local buffer first and then copying out, a similar error would write past the local buffer.

The correct pattern is to clamp the length and use the clamped length for the copy:

```c
size_t to_copy = MIN(user_len, sc->sc_list_bytes);
error = copyout(sc->sc_list, args->buf, to_copy);
```

Information leaks through oversized replies are common when driver code evolves: the original author wrote a paired check-and-copy; a later change altered one side but not the other. Every copyout should use the already-validated kernel-side length, and that length should be bounded by the user's buffer size.

### Strings and the NUL Terminator

Strings are a particularly rich source of information leaks because they have two different natural lengths: the length of the string (up to the NUL) and the size of the buffer it lives in. Suppose:

```c
char name[32];
strncpy(name, "secdev0", sizeof(name));  /* copies 8 bytes, NUL-padded */

/* ... later, maybe in a different function ... */
strncpy(name, "xdev", sizeof(name));     /* copies 5 bytes, NUL-padded */

copyout(name, args->buf, sizeof(name));  /* copies all 32 bytes */
```

The second `strncpy` overwrites the first five bytes with "xdev\0" and then pads the rest of the buffer with NULs. That happens to be safe because `strncpy` pads with NULs when the source is shorter than the destination. But if the buffer came from `malloc(9)` without `M_ZERO`, or from a stack buffer that was written to by earlier code, bytes after the NUL may contain stale data. Copying the full buffer then leaks it.

The safe pattern is to copy only up to the NUL, or to zero the buffer before writing:

```c
bzero(name, sizeof(name));
snprintf(name, sizeof(name), "%s", "secdev0");
copyout(name, args->buf, strlen(name) + 1);
```

`snprintf` guarantees NUL termination. Zeroing first ensures the bytes after the NUL are zero. The `+ 1` in the copy length includes the NUL itself.

Alternatively, copy only the string and let user space deal with its own padding:

```c
copyout(name, args->buf, strlen(name) + 1);
```

The cleanest pattern is to zero first and copy exactly the valid length.

### Sensitive Data: Explicit Zeroing Before Freeing

When a driver allocates memory to hold sensitive data (cryptographic keys, user credentials, proprietary secrets), the memory should be zeroed explicitly before being freed. Otherwise the freed memory returns to the kernel allocator's free pool with the data still visible, and subsequent allocations from that pool may expose it.

FreeBSD provides `explicit_bzero(9)`, declared in `/usr/src/sys/sys/systm.h`, which zeroes memory in a way that the compiler cannot optimize away:

```c
explicit_bzero(sc->sc_secret, sc->sc_secret_len);
free(sc->sc_secret, M_SECDEV);
sc->sc_secret = NULL;
```

Ordinary `bzero` can be eliminated by the compiler if the data is not read after being zeroed, which is exactly the situation before a free. `explicit_bzero` is guaranteed to perform the zeroing. Use it whenever sensitive data is about to be freed or go out of scope.

There is also `zfree(9)`, declared in `/usr/src/sys/sys/malloc.h`, which zeroes and frees in one call:

```c
zfree(sc->sc_secret, M_SECDEV);
sc->sc_secret = NULL;
```

`zfree` knows the allocation size from the allocator metadata and zeroes that many bytes before freeing. This is usually the cleanest pattern for cryptographic material.

For UMA zones, the equivalent is that the zone itself can be asked to zero on free, or you can `explicit_bzero` the object before calling `uma_zfree`. For stack buffers with sensitive content, `explicit_bzero` at the end of the function is the right tool.

### Never Leak Kernel Pointers

One specific form of information leak is returning a kernel pointer to user space. The kernel address of a softc, or of an internal buffer, is useful information to an attacker trying to exploit another bug. `printf("%p")` in log messages can also leak addresses. The general rule: do not put kernel addresses in user-visible output.

For sysctls and ioctls, the simplest rule is that no field in a user-facing structure should be a raw kernel pointer. If the driver wants to expose an identifier for a kernel object, use a small integer ID (an index into a table, for example), not the address of the object. Convert from one to the other inside the driver, never expose the raw pointer.

FreeBSD's `printf(9)` supports the `%p` format, which does print a pointer, but log messages in production drivers should avoid `%p` for anything where the pointer could aid exploitation. For debugging, `%p` is fine during development; before shipping the driver, audit `printf` and `log` calls to ensure no `%p` remains in paths accessible from user space.

### Sysctl Output

Sysctls that expose structures have the same rules as ioctls. Zero the structure before filling it, clamp the output length to the caller's buffer, and avoid pointer leaks. The `sysctl_handle_opaque` helper is often used for raw structures; make sure the structure is fully initialized before the handle returns.

A safer pattern is to expose each field as its own sysctl, using `sysctl_handle_int`, `sysctl_handle_string`, and so on. This avoids the padding problem entirely because each value is copied out as a primitive. It is also more ergonomic for users: `sysctl secdev.stats.packets` is more useful than an opaque blob they have to decode.

### copyout Errors

`copyout` can fail. If the user buffer becomes unmapped between the validation and the copy, `copyout` returns `EFAULT`. Your driver must handle this cleanly: typically, return the error to the user, and make sure any partial success is rolled back.

A sequence like "allocate state, fill output buffer, copyout, commit state" is safer than "commit state, copyout". If the copyout fails in the second pattern, the state is committed but the user never learned what happened. If it fails in the first pattern, nothing was committed, and the user gets a clean error.

### Deliberate Disclosure

Some sysctls and ioctls are explicitly designed to reveal information that would otherwise be private. These need an especially careful threat model. Ask: who is allowed to call this? What do they learn? Could a less-trusted attacker who obtains that information use it for something worse? A dmesg-style sysctl that exposes recent kernel messages is fine, but only because it has been scoped and filtered; exposing raw kernel log buffers without scoping is very different.

When in doubt, a sysctl that reveals sensitive data should be gated with `CTLFLAG_SECURE`, restricted to privileged users, and exposed only through paths that users must explicitly opt into. Default to less disclosure rather than more.

### Kernel Pointer Hashing

Sometimes a driver legitimately needs to expose something that identifies a kernel object, for debugging or for correlating events. The raw pointer address is the wrong answer for the reasons discussed. A better answer is a hashed or masked representation that identifies the object without revealing its address.

FreeBSD provides `%p` in `printf(9)`, which prints a pointer. It also provides a related mechanism where pointers can be "obfuscated" in user-visible output using a per-boot secret, so that two pointers in the same output are consistently distinguishable but their absolute values are not leaked. The support for this varies across subsystems; when designing your own output, consider whether a dense integer ID (an index into a table) is sufficient. Often it is.

For logs, `%p` is fine during development when logs are private. Before shipping, replace any `%p` in paths reachable from user space with either a debug-only guard (so the format is present only in debug builds) or with a non-pointer identifier.

### Wrapping Up Section 7

Information leaks are the quieter cousin of buffer overflows: they do not crash, they do not corrupt, they merely send data to user space that should have stayed in the kernel. The tools to prevent them are simple and cheap. Zero structures before filling them. Use `M_ZERO` on heap allocations that will be copied to user space. Clamp copy lengths to the smaller of the caller's buffer and the kernel's source buffer. Use `explicit_bzero` or `zfree` for sensitive data before freeing. Keep kernel pointers out of user-visible output. Bound strings to their actual length, not their buffer size.

A driver that applies these habits consistently will not leak information through its interfaces. The next section moves to the debugging and diagnostics side: how to log without leaking, how to debug without leaving production-hostile code behind, and how to keep the operator informed without handing an attacker a map.

## Section 8: Safe Logging and Debugging

Every driver logs. `printf(9)` and `log(9)` are among the first tools a driver author reaches for, and for good reason: a well-placed log message turns a mysterious failure into a readable narrative. But logs are not free. They consume disk, they can be flooded, and they can leak sensitive data. A security-aware driver treats logging as a first-class design concern, not a debug afterthought.

This section is about writing log messages that help operators without hurting security.

### The Logging Primitives

FreeBSD drivers have two main ways to emit messages.

`printf(9)`, the same name as the C library function but with kernel-specific semantics, writes to the kernel message buffer and, if the console is active, to the console. It is unconditional: every `printf` call results in a message.

`log(9)`, declared in `/usr/src/sys/sys/syslog.h`, writes to the kernel log ring with a syslog-compatible priority. Messages go to the in-kernel log buffer (readable by `dmesg(8)`) and, via `syslogd(8)`, to the configured log destinations. The priority is the familiar syslog scale: `LOG_EMERG`, `LOG_ALERT`, `LOG_CRIT`, `LOG_ERR`, `LOG_WARNING`, `LOG_NOTICE`, `LOG_INFO`, `LOG_DEBUG`.

Use `log(9)` when you want the message to be filtered or routed by syslog. Use `printf(9)` when you want unconditional emission, typically for very important events or for output that should always appear on the console.

`device_printf(9)` is a small wrapper over `printf` that prefixes the message with the device name (`secdev0: ...`). Prefer it inside driver code so messages are easy to attribute.

### What to Log and What Not to Log

A security-aware driver logs:

**State transitions that matter.** Attach, detach, reset, firmware update, link up, link down. These let an operator correlate driver behaviour with system events.

**Errors from the hardware or from user requests.** A bad ioctl argument, a DMA error, a timeout, a CRC mismatch. These let the operator diagnose problems.

**Rate-limited summaries of anomalous events.** If a malformed ioctl is received a million times per second, log the first, summarize the rest.

A security-aware driver does not log:

**User data.** The contents of buffers the user passed in. You never know what is in them.

**Cryptographic material.** Keys, IVs, plaintext, ciphertext. Ever.

**Sensitive hardware state.** On a security device, some register contents are themselves secrets.

**Kernel addresses.** `%p` is fine in early development; it has no place in production logs.

**Details of authentication failures.** A log message that says "user jane failed check X because register was 0x5d" tells an attacker what check to defeat. A log that says "authentication failed" tells the operator there was a failure without tutoring the attacker.

Think about who reads the logs. On a multi-tenant server, other users may have log-reading privileges. On a shipped appliance, the log may be exported for remote support. Treat log messages as information that could end up on any surface the system touches.

### Rate Limiting

A noisy driver is a security problem. If an attacker can trigger a log message, they can trigger a million of them. Log flooding consumes disk space, slows the system, and buries legitimate messages. FreeBSD provides `eventratecheck(9)` and `ppsratecheck(9)` in `/usr/src/sys/sys/time.h`:

```c
int eventratecheck(struct timeval *lasttime, int *cur_pps, int max_pps);
int ppsratecheck(struct timeval *lasttime, int *cur_pps, int max_pps);
```

Both return 1 if the event is allowed through and 0 if it has been rate-limited. `lasttime` and `cur_pps` are per-call state you keep in your softc. `max_pps` is the limit in events per second.

Pattern:

```c
static struct timeval secdev_last_log;
static int secdev_cur_pps;

if (ppsratecheck(&secdev_last_log, &secdev_cur_pps, 5)) {
    device_printf(dev, "malformed ioctl from uid %u\n",
        td->td_ucred->cr_uid);
}
```

Now, no matter how many malformed ioctls the attacker sends, the driver emits at most 5 log messages per second. That is enough for the operator to notice something is happening without drowning the system.

Per-event rate limiting (one `lasttime`/`cur_pps` pair per event type) is better than a single global limit, because it prevents a flood of one event type from masking other events.

### Log Levels in Practice

A good rule of thumb is this:

`LOG_ERR` for unexpected driver failures that require operator attention. "DMA mapping failed", "device returned CRC error", "firmware update aborted".

`LOG_WARNING` for unusual but not necessarily critical situations. "Received oversized buffer, truncating", "falling back to polled mode".

`LOG_NOTICE` for events that are normal but worth recording. "Firmware version 2.1 loaded", "device attached".

`LOG_INFO` for high-volume status information that operators may filter.

`LOG_DEBUG` for debugging output. A production driver usually does not emit `LOG_DEBUG` unless the operator has enabled debug logging via a sysctl.

`LOG_EMERG` and `LOG_ALERT` are reserved for system-threatening conditions and are not typically emitted by device drivers.

Choosing the right level matters because operators configure syslog to filter by level. A driver that logs every received packet at `LOG_ERR` makes the logs useless.

### Debug Logging and Production

During development, you will want verbose logging: every state transition, every entry and exit, every buffer allocation. That is fine. The question is how to turn it off in production without losing the ability to re-enable it when there is a bug to diagnose.

Two patterns are common:

**A sysctl-controlled debug level.** The driver reads a sysctl at the top of each log-worthy event and emits or suppresses the message based on the level. This allows runtime control without recompiling.

```c
static int secdev_debug = 0;
SYSCTL_INT(_hw_secdev, OID_AUTO, debug, CTLFLAG_RW,
    &secdev_debug, 0, "debug level");

#define SECDEV_DBG(level, fmt, ...) do {                    \
    if (secdev_debug >= (level))                            \
        device_printf(sc->sc_dev, fmt, ##__VA_ARGS__);      \
} while (0)
```

**Compile-time control.** A driver can use `#ifdef SECDEV_DEBUG` to include or exclude debug blocks. This is faster (no runtime check) but requires a rebuild to change. Often the two are combined: `#ifdef SECDEV_DEBUG` wraps the infrastructure, and the sysctl controls verbosity within that.

Either way, avoid `printf` calls in hot paths that are not guarded by some kind of conditional. An uncommented `printf` in an interrupt handler or a per-packet path is a performance disaster waiting to be enabled.

### Leaving Nothing Behind

Before committing driver changes, grep the driver for:

Raw `printf` calls without `device_printf` prefixes. These make log attribution harder.

`%p` format specifiers. If they appear in paths reachable from user space, replace with less sensitive formats (a sequence number, a hash, nothing).

`LOG_ERR` on user-triggerable events without rate limiting. Attackers can weaponize these.

`TODO`, `XXX`, `FIXME`, `HACK` near security-related code. Leaving these for reviewers is fine; shipping them is not.

Test-only fprintf-equivalents that were supposed to be removed.

### dmesg and the Kernel Message Buffer

The kernel message buffer is a fixed-size ring buffer shared by every driver and the kernel itself. On a busy system, old messages scroll out as new ones arrive. A driver that floods the buffer pushes out useful messages from other drivers.

`dmesg(8)` shows the current contents of the buffer. Operators rely on it. Being a good citizen in the buffer means: log important things, do not log in hot paths, rate-limit everything triggerable by users, and do not flood.

The buffer size is tunable (`kern.msgbufsize` sysctl), but you cannot count on a particular size. Write as if every message is valuable and must compete with others for space.

### KTR and Tracing

For detailed tracing without the cost of `printf`, FreeBSD provides KTR (Kernel Tracing), declared in `/usr/src/sys/sys/ktr.h`. KTR macros, when enabled, record events in a compact in-kernel ring that is separate from the message buffer. A kernel compiled with `options KTR` can be queried with `sysctl debug.ktr.buf` and with `ktrdump(8)`.

KTR events are best for per-operation tracing where a `printf` would be too heavy. They are almost free at runtime when disabled. For a security-sensitive driver, KTR gives you a way to leave tracing infrastructure in the code without paying for it in production.

Other tracing frameworks (dtrace(1) via SDT probes) are worth learning for deep inspection. They are out of scope for this chapter, but know that they exist.

### Logging Privileged Operations

A specific case worth calling out: when your driver successfully performs a privileged operation, log it. This creates an audit trail. If a firmware update happens, log who triggered it. If a hardware reset is issued, log it. If a device is reconfigured, log the change.

```c
log(LOG_NOTICE, "secdev: firmware update initiated by uid %u (euid %u)\n",
    td->td_ucred->cr_ruid, td->td_ucred->cr_uid);
```

The operator can later see who did what. If there is ever a security incident, this log is the first evidence. Make it accurate and make it hard to forge.

Do not over-log legitimate privileged use; a firmware update triggered by `freebsd-update` once a month is one message, not a thousand. But the single message should carry enough detail to reconstruct what happened: who, when, what, with what arguments.

### The audit(4) Framework

For deeper audit trails than `log(9)` provides, FreeBSD includes an audit subsystem (`audit(4)`) based on the BSM (Basic Security Module) audit format originally from Solaris. When enabled via `auditd(8)`, the kernel emits structured audit records for many security-relevant events: logins, privilege changes, file access, and, increasingly, driver-specific events when drivers instrument themselves.

A driver that handles highly sensitive operations can emit custom audit records using `AUDIT_KERNEL_*` macros declared in `/usr/src/sys/security/audit/audit.h`. This is more involved than a `log(9)` call, but it produces records that fit into the operator's existing audit workflow, are structured (machine-readable), and can be forwarded to remote audit collectors for compliance.

For most drivers, `log(9)` with `LOG_NOTICE` and a clear message is enough. For drivers that must meet specific compliance requirements (government, financial, medical), consider investing in audit integration. The infrastructure is already in the kernel; you just need to call into it.

### Using dtrace with Your Driver

Alongside logging, `dtrace(1)` lets an operator observe driver behavior without recompiling. A driver that declares Statically Defined Trace (SDT) probes through `sys/sdt.h` exposes well-defined hook points that dtrace scripts can latch onto.

```c
#include <sys/sdt.h>

SDT_PROVIDER_DECLARE(secdev);
SDT_PROBE_DEFINE2(secdev, , , ioctl_called, "u_long", "int");

static int
secdev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
    SDT_PROBE2(secdev, , , ioctl_called, cmd, td->td_ucred->cr_uid);
    /* ... */
}
```

An operator can then write a dtrace script that fires on `secdev:::ioctl_called` and counts or logs each event. The advantage over `log(9)` is that dtrace probes have essentially no cost when disabled, and they let the operator decide what to observe rather than forcing the driver author to anticipate every useful question.

For a security-focused driver, SDT probes on entry and exit of privileged operations let security monitoring tools observe usage patterns without the driver having to log every call. This is useful for anomaly detection: a sudden spike in ioctl calls from an unexpected UID, for example, can be flagged by a dtrace-based monitor.

### Wrapping Up Section 8

Logging is how a driver talks to its operator. Like any communication, it can be clear or confused, honest or misleading, helpful or harmful. A security-aware driver logs important events with appropriate levels, avoids logging sensitive data, rate-limits anything an attacker can trigger, and uses debug infrastructure that can be turned on and off without recompilation. It prefers `device_printf(9)` for attribution, uses `log(9)` with thoughtful priorities, and never leaves `%p` or unguarded `printf` statements in production paths.

The next section takes a broader view. Beyond specific techniques (bounds-checking, privilege checks, safe logging), there is a design-level question: what should a driver do by default when something goes wrong? What fail-safe behavior should it exhibit? That is the subject of secure defaults.

## Section 9: Secure Defaults and Fail-Safe Design

A driver's design decisions shape its security long before any individual line of code is written. Two drivers can use the same APIs, the same allocator, the same locking primitives, and end up with very different security postures, because one was designed to be open and the other was designed to be closed. This section is about the design choices that make a driver safe by default.

The central idea is summarized in a single principle: when in doubt, refuse. A driver that fails open has to be correct in every branch to be safe. A driver that fails closed only has to be correct in the narrow paths where it decides to allow something.

### Fail Closed

The first and most important design decision is what happens when your code reaches a state it did not expect. Consider a switch statement:

```c
switch (op) {
case OP_FOO:
    return (do_foo());
case OP_BAR:
    return (do_bar());
}
return (0);   /* fall-through: everything else succeeds! */
```

This is a fail-open design. Any operation code that is not `OP_FOO` or `OP_BAR` succeeds silently, returning 0. That is almost never what you want. A new operation code added to the API but not handled in the driver becomes a silent no-op. An attacker who discovers this can use it to bypass checks.

The fail-closed version:

```c
switch (op) {
case OP_FOO:
    return (do_foo());
case OP_BAR:
    return (do_bar());
default:
    return (EINVAL);
}
```

Unknown operations explicitly return an error. If a new operation is added to the API, the compiler or the tests will tell you the moment you handle it and forget to update the switch, because the new case is needed to silence the `EINVAL`.

The same principle applies at every decision point. When a function checks a precondition:

```c
/* Fail open: if the check is inconclusive, allow the operation. */
if (bad_condition == true)
    return (EPERM);
return (0);

/* Fail closed: if the check is inconclusive, refuse. */
if (good_condition != true)
    return (EPERM);
return (0);
```

The second form fails closed: if the precondition cannot be proven good, the operation is refused. This is safer when `good_condition` has any chance of being false due to an error in setup, a race, or a bug.

### Whitelist, Don't Blacklist

Closely related: when deciding what is allowed, whitelist the known-good rather than blacklisting the known-bad. Blacklists are always incomplete, because you cannot enumerate every bad input. Whitelists are finite by construction.

```c
/* Bad: blacklist */
if (c == '\n' || c == '\r' || c == '\0')
    return (EINVAL);

/* Good: whitelist */
if (!isalnum(c) && c != '-' && c != '_')
    return (EINVAL);
```

The blacklist missed `\t`, `\x7f`, every high-bit character, and so on. The whitelist made the allowed set explicit and refused everything else.

This applies to input validation generally. A driver that accepts a set of configuration names should explicitly list them. A driver that accepts a set of operation codes should enumerate them. If a user sends something that is not on the list, refuse.

### Smallest Useful Interface

A driver exposes functionality to user space through device nodes, ioctls, sysctls, and sometimes network protocols. Every exposed entry is a potential attack surface. A secure driver exposes only what users actually need.

Before shipping an ioctl, ask: does anyone actually use this? If a debugging ioctl was useful during development but has no production role, remove it or compile it out behind a debug flag. If a sysctl exposes internal state that only matters for engineering, hide it behind `CTLFLAG_SECURE` and consider removing it.

The cost of removing an interface now is small: a few lines of code. The cost later, when the interface has shipped and has users, is much larger. Smaller interfaces are easier to review, easier to test, and have fewer opportunities for bugs.

### Least Privilege on Open

A device node can be created with restrictive or permissive modes. Start restrictive. A mode of `0600` or `0640` is almost always a better default than `0666`. If users complain that they cannot access the device, that is a conversation you want to have; you can always relax the mode, and the operator can use devfs rules to do so per-site. If users silently gain access they should not have, you will not have that conversation until something breaks.

Similarly, a driver that supports jails should default to not being accessible in jails unless there is a specific reason. The reasoning is the same: it is easier to open up later than to retrofit a closed policy onto an open one.

### Conservative Default Values

Every configurable parameter has a default. Choose conservative ones.

A driver that has a configurable "allow user X to do Y" tunable should default to X = none. If an operator wants to grant access, they can change the tunable. If the default granted access, every deployment that missed the tunable would be open.

A driver that has a timeout should default to a short timeout. If the operation usually finishes quickly, a short default is fine. If it sometimes takes longer, the operator can bump the timeout. A long default is a denial-of-service opportunity.

A driver that has a buffer size limit should default to a small limit. Again, operators can raise it; attackers cannot.

### Defense in Depth

No single security mechanism is perfect. A defense-in-depth driver assumes any one layer can fail and builds multiple layers.

Example: suppose a driver accepts an ioctl that requires privilege. The layers of defense are:

The device node mode blocks unprivileged users from opening the device at all.

A `priv_check` at open time blocks unprivileged users even if the mode is misconfigured.

A `priv_check` on the specific ioctl catches the case where an unprivileged user somehow reached the ioctl handler.

A `jailed()` check on the ioctl blocks jailed users.

Input validation on the ioctl arguments refuses malformed requests.

A rate-limit log records repeated malformed requests.

If all five are present, a failure in any one is contained by the others. If only one is present and it fails, the driver is compromised. Defense in depth costs a little more code and a little more CPU; it buys real resilience.

### Timeouts and Watchdogs

A driver that waits on external events should have timeouts. Hardware can fail to respond. User space can stop reading. Networks can stall. Without a timeout, a waiting driver can hold resources forever, and an attacker who controls the external event can deny service by simply not responding.

`msleep(9)` accepts a timeout argument in ticks. Use it. A sleep with no timeout is rarely the right answer in driver code.

For longer-lived operations, a watchdog timer can detect that an operation has stalled and take recovery action: abort, retry, or reset. The `callout(9)` framework is the usual mechanism.

### Bounded Resource Use

Every resource a driver can allocate on behalf of a caller should have a cap. Buffer sizes have maximum values. Per-open resource counts have maximum values. Global resource counts have maximum values. When a cap is hit, the driver returns an error, not an attempt at "best effort".

Without caps, a misbehaving or hostile process can exhaust resources. The exhaustion might be memory, file-descriptor-like state, interrupt-worthy events, or simply CPU time. Caps ensure that no single caller can dominate.

A reasonable default structure:

```c
#define SECDEV_MAX_BUFLEN     (1 << 20)   /* per buffer */
#define SECDEV_MAX_OPEN_BUFS  16          /* per open */
#define SECDEV_MAX_GLOBAL     256         /* driver-wide */
```

Check each cap explicitly before allocating. Return `EINVAL`, `ENOMEM`, or `ENOBUFS` as appropriate when the cap is hit.

### Safe Module Load and Unload

A driver that supports being unloaded must handle cleanup correctly. An unsafe unload is a security bug. If unload leaves a callback registered, or a mapping in place, or a DMA in flight, then re-loading the module (or unloading and resuming) can touch memory that is no longer owned by the driver. That is a use-after-free waiting to happen.

The rule: if any part of `detach` or `unload` fails, either propagate the error (and keep the driver loaded) or drive the cleanup to completion. Partial teardown is worse than no teardown.

A reasonable strategy: make the unload path paranoid. It checks every resource and tears down every one that was allocated, in reverse order of allocation. It uses the `callout_drain` and `taskqueue_drain` helpers to wait for async work. Only after every such resource is quiet does it free the softc.

If any step fails, return `EBUSY` from `detach` and document that the driver cannot currently be unloaded. That is better than half-freeing and crashing later.

### Safe Concurrent Entries

A driver's entry points (open, close, read, write, ioctl) can be called concurrently. The driver should be written as if every entry point could be called from any context at any time. Anything else is a race waiting to fire.

The practical implication: every entry point that touches shared state acquires the softc lock first. Every operation that uses resources from the softc does so under the lock. If the operation has to sleep or do user-space work, the code drops the lock, does the work, and re-acquires carefully, checking that the state it had not changed under its feet.

Concurrency is not an afterthought. It is part of the interface.

### Error Paths Are Normal Paths

A subtle aspect of secure design is that error paths get the same care as success paths. In a driver, error paths often free resources, release locks, and restore state. A bug on an error path is just as exploitable as a bug on a success path; often more so, because error paths are less tested.

Write every error path as if it were the happy path for a user who is trying to find bugs. Every `goto cleanup` or `out:` label is a candidate for a double-free, a missed unlock, or a left-behind mapping. Walk each error path mentally and confirm that:

Every resource allocated on the success path is freed on the error path.

No resource is freed twice.

Every lock held is released exactly once.

No error path leaves partially initialized state visible to other contexts.

A systematic pattern helps. The "single cleanup path" idiom (one label, cleanup proceeds in reverse order of allocation) catches most such bugs by construction:

```c
static int
secdev_do_something(struct secdev_softc *sc, struct secdev_arg *arg)
{
    void *kbuf = NULL;
    struct secdev_item *item = NULL;
    int error;

    kbuf = malloc(arg->len, M_SECDEV, M_WAITOK | M_ZERO);

    error = copyin(arg->data, kbuf, arg->len);
    if (error != 0)
        goto done;

    item = uma_zalloc(sc->sc_zone, M_WAITOK | M_ZERO);

    error = secdev_process(sc, kbuf, arg->len, item);
    if (error != 0)
        goto done;

    mtx_lock(&sc->sc_mtx);
    LIST_INSERT_HEAD(&sc->sc_items, item, link);
    mtx_unlock(&sc->sc_mtx);
    item = NULL;  /* ownership transferred */

done:
    if (item != NULL)
        uma_zfree(sc->sc_zone, item);
    free(kbuf, M_SECDEV);
    return (error);
}
```

Each allocation is paired with a cleanup at `done`. The cleanup uses `NULL` checks so that resources freed earlier (or never allocated) do not cause double-frees. Ownership transfers set the pointer to `NULL`, which suppresses the cleanup.

Consistent use of this pattern eliminates most cleanup-path bugs. The code is longer than an early-return style, but it is dramatically safer.

### Don't Trust Yourself Either

A final aspect of fail-safe design is to assume that even your own code has bugs. Include `KASSERT(9)` checks for invariants. `KASSERT` does nothing when `INVARIANTS` is not configured (typical in release builds), but in developer kernels it checks every assertion and panics on failure. That turns a subtle corruption bug into a loud, debuggable panic.

```c
KASSERT(sc != NULL, ("secdev: NULL softc"));
KASSERT(len <= SECDEV_MAX_BUFLEN, ("secdev: len %zu too large", len));
```

Invariants documented as `KASSERT` help readers (future you, future colleagues) understand what the code expects. They also catch regressions that would otherwise silently corrupt state.

### Graceful Degradation vs Full Refusal

A design choice that often arises in fail-safe work: when a non-critical part of an operation fails, should the driver continue with a degraded result, or should it refuse the operation entirely?

There is no universal answer. Each case depends on what the caller is likely to do with partial success. A driver that returns a packet with some fields uninitialized (because a subsystem call failed) is inviting the caller to trust the zero bytes as meaningful. A driver that fails the whole operation is more disruptive but less surprising.

For security-relevant operations, prefer full refusal. A privilege check that fails should not result in "most of the operation ran, but we did not do the privileged step"; it should result in the whole thing refused. A partial success that depended on the skipped step is a bug waiting to be found.

For non-security operations, graceful degradation is often the right call. If an optional statistics update fails, the main operation should still succeed. Document what the degradation looks like so callers can anticipate it.

### Case Study: Real-World Secure Defaults in /dev/null

The FreeBSD `null` driver, at `/usr/src/sys/dev/null/null.c`, is worth studying as an example of secure-by-default design. It is one of the simplest drivers in the tree, yet its construction embodies most of the principles in this chapter.

It creates two device nodes, `/dev/null` and `/dev/zero`, both with world-accessible permissions (`0666`). This is intentional: they are meant to be used by every process, privileged or not, and neither can leak information or corrupt kernel state. The permission decision is deliberate and documented.

The read, write, and ioctl handlers are minimal. `null_read` returns 0 (end of file). `null_write` consumes input without touching kernel state. `zero_read` fills the user buffer with zeros using `uiomove_frombuf` against a static zero-filled buffer.

The ioctl handler returns `ENOIOCTL` for unknown commands, so the upper layers can translate to the proper error. A small set of specific `FIO*` commands for non-blocking and async behavior are handled, each doing only the minimal bookkeeping that makes sense for a null or zero stream.

The driver has no locking because it has no mutable state worth protecting: the zero buffer is constant, and the read/write operations do not modify any shared data. The absence of locking is not carelessness; it is a consequence of the design minimizing what is shared in the first place.

The driver's `detach` is straightforward, destroying the device nodes. Because there is no async state, no callouts, no interrupts, no taskqueues, the cleanup is correspondingly simple.

What makes this a good example of secure defaults is the discipline of not doing more than is needed. The driver does not speculatively add features, does not expose internal state, does not support ioctls that were not demanded by specific users. Its interface is minimal, which keeps its attack surface minimal. Its behaviour is predictable and has been exactly the same for decades.

Real drivers cannot always be this simple; most have state to manage, hardware to talk to, and operations to perform. But the design principle generalizes: the simpler the driver, the fewer the failure modes. When faced with a choice between adding functionality and leaving it out, the more secure choice is usually to leave it out.

### Wrapping Up Section 9

Secure defaults come down to a disposition toward refusal. Default to `EINVAL` for unknown inputs. Default to restrictive modes on device nodes. Default to conservative limits on resources. Default to short timeouts. Default to strict privilege requirements. Whitelist, do not blacklist. Fail closed, not open.

None of these are exotic. They are design habits that add up. A driver built on them is not merely a driver that can be made secure; it is a driver that is secure by default, and has to be actively broken before it becomes insecure.

The next section brings the chapter to a close by looking at the other end of the development cycle: testing. How do you know your driver is as safe as you think it is? How do you hunt for the bugs that review missed?

## Section 10: Testing and Hardening Your Driver

A driver is not secure because you wrote secure code. It is secure because you tested it thoroughly, including under conditions you did not design for. This section is about the tools FreeBSD gives you for finding bugs before attackers do, and the habits that make a driver stay secure as it evolves.

### A Walkthrough: Finding a Bug with KASAN

Before the general guidance, consider a specific scenario. You have a driver that passes all your functional tests but that you suspect has a memory-safety bug. You build a kernel with `options KASAN`, boot it, load your driver, and run a stress test. The test crashes the kernel with output that looks something like:

```text
==================================================================
ERROR: KASan: use-after-free on address 0xfffffe003c180008
Read of size 8 at 0xfffffe003c180008 by thread 100123

Call stack:
 kasan_report
 secdev_callout_fn
 softclock_call_cc
 ...

Buffer of size 4096 at 0xfffffe003c180000 was allocated by thread 100089:
 kasan_alloc_mark
 malloc
 secdev_attach
 ...

The buffer was freed by thread 100089:
 kasan_free_mark
 free
 secdev_detach
 ...
==================================================================
```

Read the output carefully. KASAN tells you the exact instruction that accessed freed memory (`secdev_callout_fn`), the exact allocation that was freed (in `secdev_attach`), and the exact free (in `secdev_detach`). Now the bug is obvious: the callout was scheduled at attach, but detach freed the buffer before draining the callout. When the callout fires after the free, it accesses the freed buffer.

The fix: add `callout_drain` to detach before the `free`. KASAN helped you find, in thirty seconds, a bug that might have taken hours or weeks to find by inspection, and that might never have been found in production until a customer reported a random crash.

KASAN is not free. The runtime overhead is substantial, both in CPU (perhaps 2 to 3 times slower) and in memory (each byte of allocated memory has an accompanying shadow byte). You would not run production with it. But for developer testing, and especially for driver authors, it is one of the most effective tools available.

KMSAN works analogously for uninitialized memory reads, and KCOV powers coverage-guided fuzzing. Together they address the main classes of memory-safety bugs: use-after-free (KASAN), uninitialized memory (KMSAN), and bugs not reached by your tests (KCOV plus a fuzzer).

### Build With Kernel Sanitizers

A stock FreeBSD kernel is optimized for production. A development kernel for driver testing should be optimized for finding bugs. The options you add to the kernel config file turn on extra checking.

**`options INVARIANTS`** enables `KASSERT(9)`. Every assertion is checked at runtime. A failed assertion panics the kernel with a stack trace pointing to the assertion. This catches invariant violations that would otherwise corrupt data silently.

**`options INVARIANT_SUPPORT`** is implied by `INVARIANTS` but is sometimes needed as a separate option for modules built against an `INVARIANTS` kernel.

**`options WITNESS`** turns on the WITNESS lock-order checker. Every lock acquisition is recorded, and the kernel panics if a cycle is detected (A held, then B acquired; later, B held, then A acquired). This catches deadlock bugs before they deadlock.

**`options WITNESS_SKIPSPIN`** disables WITNESS for spin mutexes, which can reduce overhead at the cost of missing some checks.

**`options DIAGNOSTIC`** enables additional runtime checks in various subsystems. It is looser than `INVARIANTS` and catches some additional cases.

**`options KASAN`** enables the Kernel Address Sanitizer, which detects use-after-free, out-of-bounds access, and some uninitialized memory uses. It requires compiler support and a substantial memory overhead but is excellent for finding memory-safety bugs.

**`options KMSAN`** enables the Kernel Memory Sanitizer, which detects uses of uninitialized memory. This directly catches the information-leak bugs described in Section 7.

**`options KCOV`** enables kernel coverage tracking, which is what makes coverage-guided fuzzing work.

A driver-development kernel might add:

```text
options INVARIANTS
options INVARIANT_SUPPORT
options WITNESS
options DIAGNOSTIC
```

and, for deeper memory-safety testing, `KASAN` or `KMSAN` on supported architectures. Build that kernel, boot it, and run your driver against it. Many bugs surface immediately.

Production builds do not typically include these options (they slow the kernel significantly). Use them as a development safety net.

### Stress Testing

A driver that passes functional tests can still fail under stress. Stress testing exercises the driver's concurrency, its allocation patterns, and its error paths at volumes that amplify race conditions.

A simple stress harness for a character device might:

Open the device from N processes concurrently.

Each process issues M ioctls with valid and invalid arguments in a random order.

A separate process periodically detaches and re-attaches the device (or kldunload/kldload).

This quickly exposes races between user-space operations and detach, which are among the hardest race categories to catch by inspection.

FreeBSD's `stress2` test framework at `https://github.com/pho/stress2` has a long history of finding kernel bugs. It includes scenarios for VFS, networking, and various subsystems. A driver author can learn a lot by reading those scenarios and adapting them to the driver's interface.

### Fuzzing

Fuzzing is the technique of generating large numbers of random or semi-random inputs and observing whether the program crashes, asserts, or misbehaves. Modern fuzzers are coverage-guided: they watch which code paths are exercised and evolve inputs that explore new paths. This is far more effective than purely random input.

For driver testing, the key fuzzer is **syzkaller**, an external project that understands syscall semantics and produces structured inputs. Syzkaller is not part of the FreeBSD base system; it is an external tool that runs on top of a FreeBSD kernel built with `KCOV` coverage instrumentation. Syzkaller has found many bugs in the FreeBSD kernel over the years, and a driver that wants to be exercised thoroughly benefits from being described in a syzkaller syscall description (`.txt` file under syzkaller's `sys/freebsd/`).

If your driver exposes a substantial ioctl or sysctl interface, consider writing a syzkaller description for it. The format is straightforward, and the investment pays off the first time syzkaller finds a bug no human reviewer would have spotted.

Simpler fuzzing approaches also work. A shell script that issues random ioctls with random arguments and watches `dmesg` for panics is better than no fuzzing at all. The goal is to generate inputs your design did not anticipate.

### ASLR, PIE, and Stack Protection

Modern FreeBSD kernels use several exploit-mitigation techniques. Understanding them is part of understanding why the bugs we have discussed matter.

**kASLR**, kernel Address Space Layout Randomization, places the kernel's code, data, and stacks at randomized addresses at boot. An attacker who wants to jump to kernel code, or to overwrite a specific function pointer, does not know where that code or pointer is. kASLR is foundational for making many memory-safety bugs unexploitable in practice.

Information leaks (Section 7) are particularly dangerous because they can defeat kASLR. A single leaked kernel pointer gives the attacker the base address and unlocks everything else.

**SSP**, the Stack-Smashing Protector, places a canary value on the stack between local variables and the return address. When a function returns, the canary is checked; if it has been overwritten (because a buffer overflow clobbered it on the way to the return address), the kernel panics. SSP does not prevent overflows but it prevents many of them from gaining control of execution.

Not every function is protected. The compiler applies SSP based on heuristics: functions with local buffers, functions that take addresses of locals, and so on. Understanding this means understanding why certain buffer-overflow patterns are more exploitable than others.

**PIE**, Position-Independent Executables, allows the kernel (and modules) to be relocated to random addresses. Combined with kASLR, this is what makes the randomization effective.

**Stack guards and guard pages** surround kernel stacks with unmapped pages. An attempt to write past the stack hits an unmapped page and panics rather than silently corrupting adjacent memory.

**W^X**, write-xor-execute, keeps kernel memory either writable or executable, never both. This prevents many classic exploits that relied on writing shellcode into memory and then jumping to it.

A driver author does not implement any of these; they are kernel-wide protections. But a driver's bugs can undermine them. An information leak defeats kASLR. A buffer overflow that reliably hits a function pointer or vtable defeats SSP. A use-after-free that races a fresh allocation gives an attacker controlled memory at a kernel address.

In short: the point of careful driver code is not just to avoid crashes. It is to keep the kernel's defenses intact. When your driver leaks a pointer, you did not merely expose information; you downgraded the entire system's exploit-mitigation posture.

### Reading Your Diffs

Every time you modify the driver, read the diff carefully. Look for:

New `copyin` or `copyout` calls: are the lengths clamped? Are the buffers zeroed first?

New privilege-sensitive operations: do they have `priv_check` or equivalent?

New locking: is the lock order consistent with other code?

New allocations: are they paired with frees on every path, including error paths?

New log messages: are they rate-limited? Do they leak sensitive data?

New user-visible fields in structures: are they initialized? Is the structure zeroed before the copyout?

A diff-review habit catches many regressions. If your project uses code review (it should), make these questions part of the checklist.

### Static Analysis

FreeBSD kernel code can be analyzed by several static-analysis tools, including `cppcheck`, `clang-analyzer` (scan-build), and, increasingly, Coverity and GitHub CodeQL-style tools. These tools often report warnings that a human reviewer would miss: a conditional that can never be true, a pointer used after a path where it was freed, a missing null check.

Treat static-analysis warnings seriously. Most are false positives; some are real bugs. Silencing a warning should be a decision, not a reflex. When the tool is wrong, add a comment explaining why. When the tool is right, fix the code.

`syntax check` with `bmake` on the kernel source tree is a fast first pass. Running `clang --analyze` or `scan-build` against your driver is a deeper pass. Neither replaces review or testing, but both catch bugs at low cost.

### Code Review

No tool replaces another pair of eyes. Review is especially important for security-relevant code. When proposing a change to a security-sensitive path, find someone else to look at it. Describe what the change is, what invariants it preserves, and what you checked. Be grateful when they find a problem you missed.

For open-source projects, the FreeBSD review system (`reviews.freebsd.org`) provides a convenient way to get external review. Use it. The community has a long tradition of thoughtful, security-aware review, and reviewers often catch things you would not.

### Testing After a Change

When a bug is found and fixed, add a test that would have caught it. This matters because:

The same bug class often recurs in other places. A test that catches the specific instance may catch future similar bugs.

Without a test, you have no way to know that the fix worked.

Without a test, a future refactoring may re-introduce the bug.

Tests can be unit tests (in user space, exercising individual functions), integration tests (loading the driver in a VM and exercising it), or fuzz cases (inputs that used to crash and should not now). All have their place.

### Continuous Integration

Automated testing on every change catches regressions early. A CI setup that builds the driver against a development kernel with `INVARIANTS`, `WITNESS`, and possibly `KASAN` runs the stress harness, and checks the result, is the backbone of a driver that stays safe.

For a driver in the FreeBSD tree, this is already provided by the project's CI. For out-of-tree drivers, setting up CI takes some effort but pays back quickly.

### Treat Bug Reports Seriously

When someone reports a crash or a suspected vulnerability in your driver, treat it as real until you have evidence otherwise. Even a "harmless" bug may be exploitable in ways the reporter did not see. "I can crash the kernel with this ioctl" is not a minor issue; it is at minimum a denial-of-service bug, and very often a memory-safety bug that could become arbitrary code execution.

The FreeBSD security team (`secteam@freebsd.org`) is the right audience for vulnerability reports in the base system. For out-of-tree drivers, have a similar channel. Respond quickly, fix carefully, and credit the reporter when appropriate.

### Hardening Over Time

A driver's security posture is not static. New classes of bugs emerge. New mitigations become available. New attack techniques make old bugs more exploitable. Budget time every release cycle to:

Re-read the security-relevant paths of the driver.

Check for newly discovered compiler warnings or static-analysis findings.

Try the latest tools (KASAN, KMSAN, syzkaller) against the driver.

Update the privilege model if FreeBSD has added new `PRIV_*` codes or more specific checks.

Remove interfaces that no user actually needs.

The discipline of regular re-examination is what distinguishes a driver that is secure on the day it ships from one that stays secure through its lifetime.

### Post-Incident: What To Do When a Bug Becomes a CVE

A realistic chapter on security must cover the possibility that, despite all the precautions, a bug in your driver gets reported externally as a vulnerability. The pathway is typically:

A researcher or user discovers unexpected behavior in your driver.

They investigate and determine that the behavior is a security bug: information leak, privilege escalation, crash-on-untrusted-input, or similar.

They report the bug, ideally via a responsible-disclosure channel (for FreeBSD base-system drivers, this is the `secteam@freebsd.org` address).

You receive the report.

The first response matters. Even if the bug turns out to be less serious than it looks, treat the reporter as a collaborator, not an adversary. Acknowledge receipt promptly. Ask clarifying questions if needed. Do not dismiss without investigation. Do not attempt to gag the reporter. Most vulnerability researchers want the bug fixed; if you cooperate, you get a fix faster and usually get public credit that reflects well on the project.

Triage the report technically. Can you reproduce the bug? Is it a crash, an information leak, a privilege escalation, or something else? What is the attacker model: who has to have access, and what do they gain? Is it exploitable in combination with other known bugs?

If confirmed, coordinate a fix. Keep in mind that for FreeBSD base-system drivers, the fix must flow through the project's normal review process and, where appropriate, through the security advisory process. For out-of-tree drivers, you have more flexibility but still should write the fix carefully and test it thoroughly.

Prepare the disclosure. Typical disclosure practice gives the project time to fix the bug before details become public. Industry norms are usually 90 days. Within that window, the advisory is prepared, a patched version is released, and public disclosure happens simultaneously with the release. Do not leak details early; do not delay past the agreed date.

Write the commit message carefully. Security fix commits should mention the vulnerability without giving attackers a roadmap. "Fix incorrect bounds check in secdev_write that could allow kernel memory disclosure" is better than either "tweak write" (too vague, reviewers miss it) or "Fix CVE-2026-12345, where an attacker can read arbitrary kernel memory by issuing a write of X bytes followed by a read, bypassing Y check" (too specific, attackers read your commit history before users can upgrade).

After the release, if details become public, be prepared to answer questions. Users want to know: am I vulnerable, how do I upgrade, and how can I tell if I was attacked? Have clear, calm answers ready.

Post-mortem the bug. Not to blame, but to learn. Why did the bug exist? Was there a pattern the review missed? Could a tool have caught it? Should the team's process change? Write the conclusions down; apply them in future work.

Security is a continuing practice, and post-incident learning is one of its most important parts. A project that fixes the bug and moves on has learned nothing; a project that reflects on why the bug occurred makes the next bug less likely.

### Wrapping Up Section 10

Testing and hardening are how a careful design becomes a secure one. Build your development kernel with `INVARIANTS`, `WITNESS`, and, where possible, `KASAN` or `KMSAN`. Stress-test under concurrent load. Fuzz with syzkaller or, at minimum, with a random-input harness. Use static analysis. Review diffs. Respond seriously to bug reports. Re-test after every fix. Harden over time.

A driver does not become secure by accident. It becomes secure because the author assumed bugs existed, looked for them with every tool available, and fixed them one at a time.

## Hands-On Labs

The labs in this chapter build a small character device called `secdev` and guide you through making it progressively more secure. Each lab starts from a provided starter file, asks you to make specific changes, and provides a "fixed" reference to compare against. Work through them in order.

The labs are designed to be run in a FreeBSD 14.3 virtual machine or test host where kernel panics are acceptable. Do not run them on a machine with important running services; an error in the unsafe driver can crash the kernel.

If you are running these labs inside a VM, make sure the VM is configured to write crash dumps to a location you can recover after reboot. Enable `dumpon(8)` and set `/etc/fstab` appropriately so that core dumps land in `/var/crash` after a panic. See `/usr/src/sbin/savecore/savecore.8` for details. This infrastructure is how you will diagnose any panics the labs provoke.

The companion files for these labs are under `examples/part-07/ch31-security/`. Each lab has its own subfolder containing a `secdev.c` source file, a `Makefile`, a `README.md` describing the lab, and, where appropriate, a `test/` subfolder with small user-space test programs.

As you work through the labs, keep a running log in your lab logbook: which files you modified, what you observed when you loaded the broken version, what you observed with the fix in place, and any unexpected behavior. The logbook is a learning tool; it forces you to articulate what you see, which is how learning consolidates.

### Lab 1: The Unsafe secdev

**Goal.** Build, load, and test the intentionally unsafe version of `secdev`, confirm that it works, and then identify at least three security issues by reading the code with a security mindset.

**Prerequisites.**

This lab assumes you have a FreeBSD 14.3 virtual machine or test system where you can load and unload kernel modules. You should have already completed the module-building chapters (Part 2 and onward) so that `make`, `kldload`, `kldunload`, and device node access are familiar. If you have not, pause and revisit those chapters; the rest of Chapter 31 assumes you are comfortable with module compilation.

**Steps.**

1. Copy `examples/part-07/ch31-security/lab01-unsafe/` to a working directory on your FreeBSD test machine. You can either clone the book's companion repository or copy the files manually if you extracted them locally.

2. Read `secdev.c` carefully. Note what it does: it provides a `/dev/secdev` character device with `read`, `write`, and `ioctl` operations. `read` returns the contents of an internal buffer. `write` copies user data into the buffer. An ioctl (`SECDEV_GET_INFO`) returns a structure describing the device.

3. Read `Makefile`. It should be a standard FreeBSD kernel module makefile using `bsd.kmod.mk`.

4. Build the module with `make`. Address any build errors by consulting earlier chapters on module-building. A successful build produces `secdev.ko`.

5. Load the module with `kldload ./secdev.ko`. Verify with:
   ```
   kldstat | grep secdev
   ls -l /dev/secdev
   ```
   You should see the module listed and the device node present with whatever permissions the unsafe driver created.

6. Exercise the device as a normal functional test:
   ```
   echo "hello" > /dev/secdev
   cat /dev/secdev
   ```
   You should see `hello` printed back. If you do not, check `dmesg` for error messages.

7. Now, review the code with the security mindset from this chapter. For each of the following categories, find at least one issue in the unsafe code:
   - Buffer overflow opportunity.
   - Information leak opportunity.
   - Missing privilege check.
   - Unchecked user input.
   Write down each finding in your lab logbook, including the line number and the specific concern.

8. Unload the module with `kldunload secdev` when you are done. Verify with `kldstat` that it is gone.

**Observations.**

The unsafe `secdev` has several issues by design. In `secdev_write`, the code calls `uiomove(sc->sc_buf, uio->uio_resid, uio)`, which copies `uio_resid` bytes regardless of `sizeof(sc->sc_buf)`. A write of 8192 bytes to a 4096-byte buffer overflows the internal buffer. Depending on what lies next to `sc_buf` in memory, this may or may not crash immediately, but it always corrupts adjacent kernel memory.

`SECDEV_GET_INFO` returns a `struct secdev_info` that is filled in field-by-field without being zeroed first. Any padding bytes between fields carry stack contents to user space. The structure likely has padding around the `uint64_t` members for alignment.

The device is created with `args.mda_mode = 0666` (or equivalent), allowing any user on the system to read and write. A user with no special privilege can corrupt the kernel buffer or leak information through the ioctl.

The ioctl handler does not check `priv_check` or similar. Any user who can open the device can issue any ioctl.

`secdev_read` copies `sc->sc_buflen` bytes regardless of the caller's buffer size, potentially reading beyond valid data if `sc_buflen` was ever larger than the currently valid content.

**Additional exploration.**

As a non-root user, try the operations that should be privileged and confirm that they succeed when they should not. Write a short C program that issues `SECDEV_GET_INFO` and prints the returned structure as a hex dump. Look for non-zero bytes in fields that were not explicitly set; those are leaked kernel data.

**Wrapping up.**

The goal of this lab is pattern recognition. A real driver would have subtler versions of all these bugs, buried inside hundreds of lines of code. Training yourself to see them in a simple driver makes them easier to see everywhere else. Keep `lab01-unsafe/secdev.c` as a reference for what not to do.

### Lab 2: Bounds-Check the Buffer

**Goal.** Fix the buffer overflow in `write` and add a corresponding length check in `read`. Observe the difference in how the driver behaves when stress-tested.

**Steps.**

1. Start from `lab02-bounds/secdev.c`. This is `lab01`'s code plus some `TODO` comments marking where you will add checks.

2. In `secdev_write`, calculate how much data can safely be written to the internal buffer. Remember that `uiomove` writes at most the length you pass. Clamp `uio->uio_resid` to the remaining space before calling `uiomove`.

3. In `secdev_read`, make sure you only copy out as much data as is actually valid in the buffer, not its full allocated size.

4. Rebuild and re-test. With the fixes in place, a write of 10KB to a 4KB buffer should simply fill the buffer, not overflow it.

5. Stress-test the fixed driver:
   ```
   dd if=/dev/zero of=/dev/secdev bs=8192 count=100
   dd if=/dev/secdev of=/dev/null bs=8192 count=100
   ```
   Neither command should crash the kernel or produce warnings in `dmesg`. If they do, your bounds check is incomplete.

6. Compare your fix with `lab02-fixed/secdev.c`. If your fix is different but correct, that is fine; multiple solutions can be valid. If yours is incorrect, study the reference fix and understand where you went wrong.

**Building confidence.**

Write a small C program that issues writes of various sizes (0 bytes, 1 byte, buffer size, buffer size + 1, much larger than buffer size) and verifies that each returns the expected number of bytes written or a sensible error. This kind of boundary testing is what real driver tests look like.

**Wrapping up.**

Bounds checking is the simplest security fix and it catches a large fraction of real-world driver bugs. Internalize the pattern: every `uiomove`, `copyin`, `copyout`, and memcpy bounds the length against both source and destination sizes. The compiler cannot catch this for you; it is entirely the author's responsibility.

### Lab 3: Zero Before copyout

**Goal.** Fix the information leak in the `SECDEV_GET_INFO` ioctl. Observe, via a user-space test program, the difference between the broken and fixed versions.

**Steps.**

1. Start from `lab03-info-leak/secdev.c`. This contains the ioctl as in the original unsafe code.

2. Observe the structure definition. Note the padding between fields:
   ```c
   struct secdev_info {
       uint32_t version;
       /* 4 bytes of padding here on 64-bit systems */
       uint64_t flags;
       uint16_t id;
       /* 6 bytes of padding to align name to 8 bytes */
       char name[32];
   };
   ```
   Check the size with `pahole` or a small C program that prints `sizeof(struct secdev_info)`.

3. Before fixing, build and load the broken version. Run the test program provided in `lab03-info-leak/test/leak_check.c`. It issues the ioctl repeatedly and dumps the returned structure as a hex dump. Look at the padding bytes. You should see non-zero values that differ between runs; those are leaked kernel stack bytes.

4. In `secdev_ioctl`, before filling in the `struct secdev_info`, zero the structure with `bzero` (or use `= { 0 }` initialization).

5. Also fix the name field: use `snprintf` instead of `strncpy` to guarantee a NUL terminator, and copy only up to the NUL rather than the full buffer size.

6. Rebuild and re-test with the same `leak_check` program. The padding bytes should now be zero on every run. The visible behavior from user space is unchanged; the internal change is that padding bytes no longer carry stack contents.

7. Compare with `lab03-fixed/secdev.c`.

**A deeper exploration.**

If you have `KMSAN` built into your kernel, load the broken version of the driver and run `leak_check`. KMSAN should report an uninitialized read when the structure is copied out. This demonstrates why KMSAN is valuable: it catches information leaks that are invisible without it.

**Wrapping up.**

This fix costs a single `bzero` call. The benefit is that the ioctl cannot leak information, ever, regardless of what future changes add or remove fields. Make `bzero` (or zero-initializing declaration) part of your reflex for any structure that will touch `copyout`, `sysctl`, or similar boundaries.

### Lab 4: Add Privilege Checks

**Goal.** Restrict the device to privileged users, and verify that unprivileged access is refused.

**Steps.**

1. Start from `lab04-privilege/secdev.c`.

2. Modify the device-node creation code in `secdev_modevent` (or `secdev_attach`, depending on structure) to use a restrictive mode (`0600`) and the root user and group:
   ```c
   args.mda_uid = UID_ROOT;
   args.mda_gid = GID_WHEEL;
   args.mda_mode = 0600;
   ```

3. In `secdev_open`, add a `priv_check(td, PRIV_DRIVER)` call at the top:
   ```c
   error = priv_check(td, PRIV_DRIVER);
   if (error != 0)
       return (error);
   ```
   Return the error if the check fails.

4. Rebuild and reload the module.

5. Test from a non-root shell:
   ```
   % cat /dev/secdev
   cat: /dev/secdev: Permission denied
   ```
   Open should fail with `EPERM` (reported as "Permission denied"). The filesystem mode blocks access before `d_open` is even reached.

6. Temporarily change the device-node mode (as root) with `chmod 0666 /dev/secdev`. Try again as non-root. This time the filesystem allows the open, but `priv_check` in `d_open` refuses:
   ```
   % cat /dev/secdev
   cat: /dev/secdev: Operation not permitted
   ```
   This demonstrates the in-kernel layer of the defense.

7. Reset the permissions with `chmod 0600 /dev/secdev` or reload the module to restore the default.

8. As root, the device should continue to work normally. Verify:
   ```
   # echo "hello" > /dev/secdev
   # cat /dev/secdev
   hello
   ```

9. Compare with `lab04-fixed/secdev.c`.

**Digging deeper.**

Try creating a jailed environment and running a shell as root inside the jail:
```console
# jail -c path=/ name=testjail persist
# jexec testjail sh
# cat /dev/secdev
```
Depending on whether your driver has added a `jailed()` check, the behavior differs. If the driver does not check `jailed`, jailed-root can still access the device. Add `if (jailed(td->td_ucred)) return (EPERM);` at the top of `secdev_open` and verify that the jailed access is now refused.

**Wrapping up.**

Restricting device-node permissions is a two-layer defense: the filesystem layer, and the in-kernel `priv_check`. Both together make the driver robust against configuration mistakes. Adding `jailed()` on top blocks even root-in-jail from sensitive operations. Each layer defends against a different failure mode; do not rely on any single one.

### Lab 5: Rate-Limited Logging

**Goal.** Add a rate-limited log message for malformed ioctls and verify that a flood of malformed requests does not overwhelm the log.

**Steps.**

1. Start from `lab05-ratelimit/secdev.c`.

2. Add a static `struct timeval` and a static `int` to hold rate-limit state. These are global per-driver, not per-softc, unless you specifically want per-device limits:
   ```c
   static struct timeval secdev_log_last;
   static int secdev_log_pps;
   ```

3. In `secdev_ioctl`, in the `default` branch (the case that handles unknown ioctls), use `ppsratecheck` to decide whether to log:
   ```c
   default:
       if (ppsratecheck(&secdev_log_last, &secdev_log_pps, 5)) {
           device_printf(sc->sc_dev,
               "unknown ioctl 0x%lx from uid %u\n",
               cmd, td->td_ucred->cr_uid);
       }
       return (ENOTTY);
   ```
   The third argument, `5`, is the maximum messages per second.

4. Rebuild and reload.

5. Write a tiny test program that issues a million bad ioctls in a tight loop:
   ```c
   #include <sys/ioctl.h>
   #include <fcntl.h>
   int main(void) {
       int fd = open("/dev/secdev", O_RDWR);
       for (int i = 0; i < 1000000; i++)
           ioctl(fd, 0xdeadbeef, NULL);
       return (0);
   }
   ```

6. While it runs (as root), monitor `dmesg -f`. You should see messages arriving, but at no more than about 5 per second. Without rate limiting, you would have a million messages.

7. Count the messages with something like `dmesg | grep "unknown ioctl" | wc -l`. Compare to one million (the number of attempts).

8. Compare with `lab05-fixed/secdev.c`.

**Variations to try.**

Replace `ppsratecheck` with `eventratecheck` and note the difference (event-based vs per-second). Experiment with different maximum rates. Add a suppressed-count summary that emits periodically ("suppressed N messages in last M seconds") for operator visibility.

**Wrapping up.**

Rate-limited logging gives you visibility into suspicious activity without making the driver itself a denial-of-service vector. Apply the pattern to any log message that can be triggered by user actions. The cost is a few extra lines per log statement; the benefit is that your driver is no longer a tool attackers can use to flood the system.

### Lab 6: Safe Detach

**Goal.** Make `secdev_detach` safe under concurrent activity. Observe, by deliberately racing unload with active use, how the fix prevents use-after-free panics.

**Steps.**

1. Start from `lab06-detach/secdev.c`. This version introduces a small callout that periodically updates an internal counter, and an ioctl that sleeps briefly to simulate long-running work.

2. Review the current `detach` function. Note what it frees and in what order. The starter file intentionally has a flawed detach that frees the softc without draining.

3. Test the flawed version first (build with `INVARIANTS` and `WITNESS` in the kernel):
   - Start a test program that holds `/dev/secdev` open and issues the slow ioctl in a loop.
   - While it runs, issue `kldunload secdev`.
   - Observe the result. You may see a kernel panic, a stuck kldunload, or, if you are lucky, nothing visible (the race may not fire on every run). `WITNESS` may complain about lock state.
   - Rebuild and try again until you see the problem. Concurrent races can be flaky.

4. Now fix the detach:
   - Use `destroy_dev` on the cdev before any other cleanup, so that no new user-space thread can enter the driver, and any in-flight thread finishes before `destroy_dev` returns.
   - Add a `callout_drain` call before freeing the softc. This ensures that any in-flight callout has finished.
   - If the driver uses a taskqueue, add `taskqueue_drain_all`.
   - Only after all draining, free resources.

5. Rebuild and re-test the same race:
   - The user program continues running uninterrupted during `kldunload`.
   - After `kldunload`, the user program's next ioctl receives an error (typically `ENXIO`) because the cdev was destroyed.
   - The kernel remains stable. No panic, no WITNESS complaint.

6. Compare with `lab06-fixed/secdev.c`. Confirm that the fixed version handles in-flight activity safely.

**Understanding what happened.**

The flawed version races because:
- `destroy_dev` is called, or is not called early enough. In-flight d_* calls continue.
- The callout is scheduled in the future and has not fired yet.
- The softc is freed while something still references it.
- The freed softc is reused by the allocator for some other purpose.
- The callout fires, touches what it thinks is its softc, and corrupts whatever memory is now there.

The fix sequences the cleanup: stop accepting new entries first (`destroy_dev`), stop in-flight entries by waiting for them to leave (part of `destroy_dev`'s contract), stop scheduled work (`callout_drain`), and only then free state. Each step closes a door; nothing beyond a closed door can reach the memory being freed.

**Wrapping up.**

Detach-time races are among the hardest driver bugs to catch by inspection, because the bug only occurs when timing aligns. Using `destroy_dev`, `callout_drain`, and `taskqueue_drain_all` defensively in every `detach` function is one of the highest-value habits you can adopt. Do it mechanically, even if you do not think your driver has asynchronous activity. The next author to add a callout may forget; your defensive detach catches them.

### Lab 7: Secure Defaults Everywhere

**Goal.** Apply every lesson so far to a single driver: the "secure secdev". Build it from a skeleton, then review the finished result as if you were performing a security audit.

**Steps.**

1. Start from `lab07-secure/secdev.c`. This is a skeleton with `TODO` markers in many places.

2. Fill in each `TODO`, applying the lessons from Labs 1 to 6 plus any additional defenses you think appropriate. Suggested additions:
   - A `MALLOC_DEFINE` for the driver's memory.
   - A softc mutex protecting all shared fields.
   - `priv_check(td, PRIV_DRIVER)` in `d_open` and in each privileged ioctl.
   - `jailed()` checks for operations that should not be available to jailed users.
   - `securelevel_gt` for operations that should be refused at elevated securelevel.
   - `bzero` on every structure before filling it for `copyout`.
   - `M_ZERO` on every allocation that will be copied to user space.
   - `explicit_bzero` on sensitive buffers before `free`.
   - Rate-limited `device_printf` on every log message triggerable from user space.
   - `destroy_dev`, `callout_drain`, and other drains in `detach` before any free.
   - A sysctl-controlled `secdev_debug` flag that gates verbose logging.
   - Input validation that whitelists allowed operation codes.
   - Bounded copies in both directions.
   - `KASSERT` statements documenting internal invariants.

3. Rebuild and load the module.

4. Run a comprehensive functional test to confirm everything still works:
   - As root, open the device, read, write, ioctl.
   - As non-root, confirm `/dev/secdev` is inaccessible.
   - Inside a jail, confirm sensitive operations are refused.

5. Run a security stress test:
   - Boundary cases (0-byte reads, exactly buffer-size writes, one-byte-over writes).
   - Malformed ioctls.
   - Concurrent open/read/write/close from multiple processes.
   - `kldunload` during active use.

6. Compare your work with `lab07-fixed/secdev.c`. Note differences. Where your version is more defensive, ask whether the extra defense is worth the complexity. Where the reference is more defensive, ask whether you missed a defense.

**A self-review.**

Once your lab 7 driver builds and passes tests, put on the reviewer hat. Go through the Security Checklist section of this chapter and confirm each item. Any items you cannot confirm are gaps in your driver. Fix them now, while the code is fresh; later, finding and fixing such gaps is slower and more error-prone.

**Wrapping up.**

This lab is the consolidation of the chapter. Your finished driver is still a simple character device, but it is one you would not be embarrassed to see in a real FreeBSD tree. The practices you applied here are the same practices that separate amateur drivers from professional ones. Keep your lab 7 driver as a reference: when you write your first real driver, this is the skeleton you will start from.

## Challenge Exercises

These challenges go beyond the labs. They are longer, more open-ended, and assume you have finished Lab 7. Take your time. None of them require new FreeBSD APIs; they require deeper application of what you have learned.

These challenges are meant to be attempted over days or weeks, not in a single sitting. They exercise judgment as much as coding: the question "is this secure" is often "secure against what threat model". Being explicit about the threat model is part of the exercise.

### Challenge 1: Add a Multi-Step ioctl

Design and implement an ioctl that performs a multi-step operation on `secdev`: first, the user uploads a blob; second, the user requests processing; third, the user downloads the result. Each step is a separate ioctl call.

The challenge is to manage per-open state correctly: the blob uploaded in step 1 must be associated with the calling file descriptor, not globally. Two concurrent users must not see each other's blobs. State must be cleaned up when the file descriptor is closed, even if the user never reached step 3.

Security considerations: bound the blob size, validate each step of the state machine (cannot request processing without a blob; cannot download without completing processing), make sure partial state on error paths is cleaned up, and make sure a user-visible identifier (if you expose one) is not a kernel pointer.

### Challenge 2: Write a syzkaller Description

Write a syzkaller syscall description for `secdev`'s ioctl interface. The format is documented in the syzkaller repository. Install syzkaller and feed it your driver; run it for at least an hour (ideally longer) and see what it finds.

If it finds bugs, fix them. Write a note about what each bug was and how the fix works. If it does not find bugs in several hours, consider whether your syzkaller description really exercises the driver. Often a description that does not find bugs is not exploring the interface thoroughly.

### Challenge 3: Detect Double Free in Your Own Code

Intentionally introduce a double-free bug into a copy of your secure `secdev`. Build the module against a kernel with `INVARIANTS` and `WITNESS`. Load and exercise the module in a way that triggers the double-free. Observe what happens.

Now rebuild the kernel with `KASAN`. Load and exercise the same broken module. Observe the difference in how the bug is detected.

Write down what each sanitizer caught and how readable the output was. This exercise builds intuition for which sanitizer to reach for first in which situation.

### Challenge 4: Threat-Model an Existing Driver

Pick a driver in the FreeBSD tree that you have not previously examined (something small, ideally under 2000 lines). Read it carefully. Write a threat model: who are the callers, what privileges do they need, what could go wrong, what mitigations are in place, what could be added?

The goal is not to find specific bugs. It is to practice the security mindset on real code. A good threat model is a few pages of prose that would let another engineer review the same driver efficiently.

### Challenge 5: Compare `/dev/null` and `/dev/mem`

Open `/usr/src/sys/dev/null/null.c` and `/usr/src/sys/dev/mem/memdev.c` (or the per-architecture equivalents). Read both.

Write a short essay (a page or two) on the security differences. `/dev/null` is one of the simplest drivers in FreeBSD; what does it do, and why is it safe? `/dev/mem` is one of the most dangerous; what does it do, and how does FreeBSD keep it safe? What can you learn about the shape of secure driver code from the contrast?

## Troubleshooting and Common Mistakes

A short catalogue of mistakes I have seen repeatedly in real driver code, with the symptom, the cause, and the fix.

### "Sometimes it works, sometimes it doesn't"

**Symptom.** A test passes most of the time but occasionally fails. Running under load amplifies the failure rate.

**Cause.** Almost always a race condition. Something is being read and written concurrently without a lock.

**Fix.** Identify the shared state. Add a lock. Acquire the lock for the whole check-and-act sequence. Do not trust `atomic_*` operations to solve a multi-field invariant problem.

### "The driver crashes on unload"

**Symptom.** `kldunload` triggers a panic or a stuck kernel.

**Cause.** A callout, taskqueue task, or kernel thread is still running when `detach` frees the structure it uses. Or an in-flight cdev operation is still in the driver's code when `destroy_dev` is skipped.

**Fix.** In `detach`, call `destroy_dev` before anything else. Then `callout_drain` every callout, `taskqueue_drain_all` every taskqueue, and wait for every kernel thread to exit. Only then free state. Structure the detach as a strict reverse of attach.

### "The ioctl works from root but not from my service account"

**Symptom.** User reports that root can use the device, but a non-root account cannot.

**Cause.** Device node permissions are too restrictive, or a `priv_check` call refuses the operation.

**Fix.** If the operation truly should be privileged, this is working as intended; document it. If not, reconsider: was the privilege check added in error? Is the device-node mode too tight? The correct answer depends on the operation; most real answers are "yes, it should be privileged, update the docs".

### "dmesg is flooding"

**Symptom.** `dmesg` shows thousands of identical messages from the driver. Legitimate messages are being pushed out.

**Cause.** A log statement in a path triggerable from user space, without rate limiting.

**Fix.** Wrap the log in `ppsratecheck` or `eventratecheck`. Limit to a few per second. If the message is about an error, include a count of suppressed messages when the rate returns to normal (the rate helpers support this).

### "The structure comes back with garbage bytes"

**Symptom.** A user-space tool reports seeing apparently random data in a field it did not expect to be set.

**Cause.** The driver did not zero the structure before filling and copying out. The "random" data is actually stack or heap content from before.

**Fix.** Add a `bzero` at the top of the function, or initialize the structure with `= { 0 }` at declaration. Never `copyout` an uninitialized structure.

### "We leak memory but I don't see where"

**Symptom.** `vmstat -m` shows the driver's malloc type growing over time. Eventually the system runs out of memory.

**Cause.** An allocation path that does not pair with a free path, or an error path that returns without freeing.

**Fix.** Use a named malloc type (`MALLOC_DEFINE`). Audit every allocation. Walk every error path. Consider the single-cleanup-label pattern. Build with `INVARIANTS` and watch for allocator warnings on unload.

### "kldload succeeds but my device doesn't show up in /dev"

**Symptom.** `kldstat` shows the module loaded, but there is no `/dev/secdev` entry.

**Cause.** Usually an error in the `attach` sequence before `make_dev_s` is called, or `make_dev_s` itself failed silently.

**Fix.** Check the return value of `make_dev_s`. Add a `device_printf` reporting any error. Verify `attach` is being reached by adding a `device_printf` at the top.

### "A simple C test passes, but a shell script that does the same thing in a loop fails"

**Symptom.** Single-shot testing works. Rapid repeated testing fails.

**Cause.** Likely a race between repeated operations, or a resource that is not being cleaned up between calls. Sometimes a TOCTOU bug that is timing-sensitive.

**Fix.** Stress-test harder. Use `dtrace` or `ktrace` to see what is happening. Look for state that persists across calls and should not.

### "KASAN says use-after-free but my malloc/free are balanced"

**Symptom.** `KASAN` reports access to freed memory, but visual inspection of the driver shows each allocation freed exactly once.

**Cause.** A common subtle case: a callout or taskqueue task still holds a pointer to the freed object. The callout fires after free.

**Fix.** Trace the callout lifecycle. Ensure `callout_drain` (or equivalent) runs before any free. A related case is an asynchronous completion callback; ensure the operation is either completed or cancelled before the owning structure is freed.

### "WITNESS complains about lock order"

**Symptom.** `WITNESS` reports "lock order reversal" and identifies two locks that were acquired in inconsistent order.

**Cause.** At one point the code acquired lock A then lock B; at another point it acquired lock B then lock A. This can deadlock.

**Fix.** Decide on a canonical order for your locks. Document it. Acquire them in that order everywhere. If a code path legitimately needs the reverse order, use `mtx_trylock` with a backoff-and-retry pattern.

### "vmstat -m shows a negative free count"

**Symptom.** `vmstat -m` lists the driver's malloc type with a negative number of allocations, or with an inuse count that increases over time without bound.

**Cause.** A mismatched `malloc`/`free` type, or a leak where allocations happen without corresponding frees.

**Fix.** A negative free count almost always means a `free` call passed the wrong type tag. Audit every `free(ptr, M_TYPE)` and confirm the type matches the `malloc`. A continuously rising inuse count is a leak; audit every path that allocates and confirm it has a matching free on every exit.

### "The driver works on amd64 but panics on arm64"

**Symptom.** Functional testing on amd64 passes; the same driver panics on arm64.

**Cause.** Often a mismatch in structure padding or alignment. arm64 has different padding rules from amd64 for some structures. An access that is aligned on amd64 may be misaligned on arm64 and panic.

**Fix.** Use `__packed` carefully (it changes alignment), use `__aligned(N)` where alignment matters, and avoid assuming the size or layout of a structure matches between architectures. For fields crossing the user/kernel boundary, use explicit widths (`uint32_t` rather than `int`, `uint64_t` rather than `long`).

### "The driver compiles without errors but dmesg shows kernel build warnings"

**Symptom.** The module builds, but loading it produces warnings about unresolved symbols or ABI mismatches.

**Cause.** The module was built against a different kernel than the one it is being loaded into. The kernel ABI is not guaranteed stable across versions, so a module built against 14.2 may not load cleanly on 14.3.

**Fix.** Rebuild the module against the running kernel's source tree. `uname -r` shows the running kernel version; verify that `/usr/src` matches. If they do not, install the matching source (via `freebsd-update`, `svn`, or `git`, depending on your source distribution).

### "The driver is intermittently slower than expected"

**Symptom.** Benchmarks show occasional large latency spikes even under moderate load.

**Cause.** Often a lock-contention issue: multiple threads queue on a single mutex. Sometimes an allocator stall: `malloc(M_WAITOK)` in a hot path waits for memory to become available.

**Fix.** Use `dtrace` to profile lock contention (`lockstat` provider) and identify which lock is hot. Restructure to reduce the critical section, split the lock, or use a lock-free approach. For allocator stalls, preallocate or use a UMA zone with a high-water mark.

## Security Checklist for Driver Code Review

This section is a reference checklist you can keep next to your code as you review a driver, yours or someone else's. It is not exhaustive, but if every item on the list has been consciously considered, the driver is in much better shape than the average.

### Structural Checks

The driver's module-load and module-unload paths are symmetric. Every resource allocated on load is freed on unload, and the order of freeing is the reverse of the order of allocation.

The driver uses `make_dev_s` or `make_dev_credf` (not the legacy `make_dev` alone) so that errors during device-node creation are reported and handled.

The device node is created with conservative permissions. Mode `0600` or `0640` is the default; anything more permissive has an explicit reason recorded in comments or commit messages.

The driver declares a named `malloc_type` via `MALLOC_DECLARE` and `MALLOC_DEFINE`. All allocations use this type.

Every lock in the driver has a comment next to its declaration saying what it protects. The comment is accurate.

### Input and Boundary Checks

Every `copyin` call is paired with a size argument that cannot exceed the destination buffer size.

Every `copyout` call uses a length that is the minimum of the caller's buffer size and the kernel source's size.

`copyinstr` is used for strings that should be NUL-terminated. The return value (including `done`) is checked.

Every ioctl argument structure is copied into kernel space before any of its fields are read.

`uiomove` calls pass a length that is clamped to the buffer being read from or written to, not `uio->uio_resid` alone.

Every user-provided length field is validated: non-zero when required, bounded below the appropriate maximum, checked against remaining buffer space.

### Memory Management

Every `malloc` call checks the return value if `M_NOWAIT` is used. `M_WAITOK` without `M_NULLOK` is never null-checked uselessly; the code relies on the allocator's guarantee.

Every `malloc` is paired with exactly one `free` on every code path. Success paths and error paths are both audited.

Sensitive data (keys, passwords, credentials, proprietary secrets) is zeroed with `explicit_bzero` or `zfree` before the memory is released.

Structures that will be copied to user space are zeroed before being filled.

Buffers allocated for user output use `M_ZERO` at allocation time to prevent stale-data leaks through the tail.

After a pointer is freed, it is either set to NULL or the scope immediately ends.

### Privilege and Access Control

Operations that require administrative privilege call `priv_check(td, PRIV_DRIVER)` or a more specific `PRIV_*` constant.

Operations that should not be allowed inside a jail explicitly check `jailed(td->td_ucred)` and return `EPERM` if jailed.

Operations that depend on the system's securelevel call `securelevel_gt` or `securelevel_ge` and handle the return value correctly (note the inverted semantics: nonzero means refuse).

No operation uses `cr_uid == 0` as a privilege gate. `priv_check` is used instead.

Sysctls that expose sensitive data use `CTLFLAG_SECURE` or restrict themselves to privileged users via permission checks.

### Concurrency

Every field of the softc that is accessed by more than one context is protected by a lock.

The full check-and-act sequence (including lookups that decide whether an operation is legal) is held under the appropriate lock.

No `copyin`, `copyout`, or `uiomove` call is made while holding a mutex. If user-space I/O is needed, the code drops the lock, does the I/O, and re-acquires, checking invariants.

`detach` calls `destroy_dev` (or equivalent) first, then drains callouts, taskqueues, and interrupts, then frees state.

Callouts, taskqueues, and kernel threads are tracked so that every one of them can be drained during unload.

### Information Hygiene

No kernel pointer (`%p` or equivalent) is returned to user space through an ioctl, sysctl, or log message in a user-triggerable path.

No user-triggerable log message is uncapped; `ppsratecheck` or similar wraps every such message.

Logs do not include user-supplied data that could contain control characters or sensitive information.

Debug logging is wrapped in a conditional (sysctl or compile-time) so that production builds do not emit it by default.

### Failure Modes

Every switch statement has a `default:` branch that returns a sensible error.

Every parser or validator whitelists what is allowed, rather than blacklisting what is not.

Every operation with resource use has a cap. The cap is documented.

Every sleep has a finite timeout unless a genuine reason requires unbounded waiting (and even then, `PCATCH` is used to allow signals).

Every error path frees the resources its success path would have kept.

The driver's response to unexpected input is to refuse the operation, not to guess.

### Testing

The driver has been loaded and tested against a kernel built with `INVARIANTS` and `WITNESS`. No assertions fire and no lock-order violations are reported.

The driver has been tested under concurrent load (multiple processes, multiple open file descriptors, interleaved operations).

The driver has been tested under detach-time concurrency (a user is inside the driver while unload is attempted).

Some form of fuzzing (ideally syzkaller, at minimum a randomized shell test) has been run against the driver.

The driver has been reviewed by someone other than its author. The review was specifically for security considerations, not only functionality.

### Evolution

The driver's security posture is re-examined at regular intervals. New compiler warnings and new sanitizer findings are triaged seriously. New FreeBSD privilege codes are considered. Unused interfaces are removed.

Bug reports against the driver are treated as possibly exploitable until proven otherwise.

Commit history shows that security-relevant changes receive careful commit messages that explain what was wrong and what the fix does.

## A Closer Look at Real-World Vulnerability Patterns

The principles in this chapter are abstractions over real bugs that happened in real kernels. This section studies a few patterns that have appeared across the FreeBSD, Linux, and other open-source operating systems over the years. The goal is not to catalogue CVEs (there are whole databases for that) but to train pattern recognition.

### The Incomplete Copy

A classic pattern: a driver receives a variable-length user buffer. It copies a fixed header, extracts a length field from the header, then copies the variable portion according to that length.

```c
error = copyin(uaddr, &hdr, sizeof(hdr));
if (error != 0)
    return (error);

if (hdr.body_len > MAX_BODY)
    return (EINVAL);

error = copyin(uaddr + sizeof(hdr), body, hdr.body_len);
```

The bug is that the length check compares `body_len` against `MAX_BODY`, but `body` may be a fixed-size buffer sized differently. If `MAX_BODY` is defined carelessly, or if it was once the size of `body` but `body` has since shrunk, the copy overflows `body`.

Every time you see a pattern of "validate header, then copy body based on header", check that the length bound actually matches the destination buffer size. Use `sizeof(body)` directly if you can, rather than a macro that might drift.

### The Sign Confusion

A length is stored as `int` but should be non-negative. A caller passes `-1`. Your code:

```c
if (len > MAX_LEN)
    return (EINVAL);

buf = malloc(len, M_FOO, M_WAITOK);
copyin(uaddr, buf, len);
```

Does the first check pass? Yes, because `-1` is less than `MAX_LEN` when compared as a signed `int`. What happens in `malloc(len, ...)` with `len = -1`? On many platforms, `-1` silently becomes a very large positive `size_t`. The allocation fails (or worse, succeeds at a huge size), or `copyin` tries to copy a huge buffer.

The fix is to use unsigned types for sizes (preferably `size_t`), or to check for negative values explicitly:

```c
if (len < 0 || len > MAX_LEN)
    return (EINVAL);
```

Or, better, change the type so that negative values cannot exist:

```c
size_t len = arg->len;     /* copied from user, already size_t */
if (len > MAX_LEN)
    return (EINVAL);
```

Sign confusion is one of the most common root causes of buffer overflows in kernel code. Use `size_t` for sizes. Use `ssize_t` only when negative values are meaningful. Never mix signed and unsigned in a size check.

### The Incomplete Validation

A driver accepts a complex structure with many fields. The validation function checks some fields but forgets others:

```c
if (args->type > TYPE_MAX)
    return (EINVAL);
if (args->count > COUNT_MAX)
    return (EINVAL);
/* forgot to validate args->offset */

use(args->offset);  /* attacker-controlled */
```

The bug is that `args->offset` is used as an index into an array without being bounds-checked. An attacker supplies a huge offset and reads or writes kernel memory.

The fix is to treat validation as a checklist. For every field in the input structure, ask: what values are legal? Enforce them all. A helper function `is_valid_arg` that centralizes the validation and is called early is better than scattered checks.

### The Skipped Check on the Error Path

A driver carefully validates input on the success path, but the error path cleans up based on a field that was never validated:

```c
if (args->count > COUNT_MAX)
    return (EINVAL);
buf = malloc(args->count * sizeof(*buf), M_FOO, M_WAITOK);
error = copyin(args->data, buf, args->count * sizeof(*buf));
if (error != 0) {
    /* error cleanup */
    if (args->free_flag)          /* untrusted field */
        some_free(args->ptr);     /* attacker-controlled */
    free(buf, M_FOO);
    return (error);
}
```

The error path uses `args->free_flag` and `args->ptr`, neither of which were validated. If the attacker arranges for `copyin` to fail (say, by unmapping the memory), the error path frees an attacker-controlled pointer, corrupting the kernel heap.

The lesson: validation must cover every field that any code path reads. It is tempting to think "the error path is unusual; it is fine". Attackers specifically aim for error paths because they are less tested.

### The Double-Lookup

A driver looks up an object in a table by name or ID, then performs an operation. Between the lookup and the operation, the object is removed by another thread. The operation then acts on freed memory.

```c
obj = lookup(id);
if (obj == NULL)
    return (ENOENT);
do_operation(obj);   /* obj may have been freed in between */
```

The fix is to take a reference on the object (using a refcount) inside the lookup, hold the reference across the operation, and release it at the end. The lookup function takes the lock, increments the refcount, and releases the lock. The operation then works with a refcount-held pointer that cannot be freed out from under it. The release decrements the refcount; when it drops to zero, the last holder frees the object.

Reference counts are the FreeBSD-canonical answer to the double-lookup problem. See `/usr/src/sys/sys/refcount.h`.

### The Buffer That Grew

A buffer was once 256 bytes. A constant `BUF_SIZE = 256` was defined. The code checked `len <= BUF_SIZE` and copied `len` bytes into the buffer. Later, someone increased the buffer to 1024 bytes but forgot to update the constant. Or the constant was updated but an `sizeof(buf)` in one call was not, because it was not using the constant.

This class of bug is prevented by always using `sizeof` on the destination buffer directly, rather than a constant that may drift:

```c
char buf[BUF_SIZE];
if (len > sizeof(buf))     /* always matches the actual buf size */
    return (EINVAL);
```

Constants are useful when multiple places need the same bound. If you use a constant, keep the definition and the array adjacent in the source code, and consider adding a `_Static_assert(sizeof(buf) == BUF_SIZE, ...)` to catch drift.

### The Unchecked Pointer from a Structure

A driver receives a structure from user space that contains pointers. The driver uses the pointers directly:

```c
error = copyin(uaddr, &cmd, sizeof(cmd));
/* cmd.data_ptr is user-space pointer */
use(cmd.data_ptr);   /* treating user pointer as kernel pointer */
```

This is a catastrophic bug: the pointer is a user-space address, but the code dereferences it as if it were kernel memory. On some architectures this may access whatever memory happens to be at that address in kernel space, which is usually garbage or invalid. On others, it faults. In some specific pathological cases, it accesses sensitive kernel data.

The fix: never dereference a pointer obtained from user space. Pointers in user-supplied structures must be passed to `copyin` or `copyout`, which correctly translate user addresses. Never treat them as kernel addresses.

### The Forgotten copyout

A driver reads a structure from user space, modifies it, but forgets to copy the modified version back:

```c
error = copyin(uaddr, &cmd, sizeof(cmd));
if (error != 0)
    return (error);

cmd.status = STATUS_OK;
/* forgot to copyout */
return (0);
```

This is a functional bug, not strictly a security bug, but its mirror image is: forgetting `copyin` and assuming a field was already set. "I set `cmd.status` in `copyin`, then I read it later" is wrong if the field was actually set by user space; the user's value is what the code reads.

Every structure that flows user-to-kernel and back needs a clear convention about when `copyin` and `copyout` happen, and what fields are authoritative in which direction. Document it and follow it.

### The Accidental Race

A driver takes a lock, reads a field, releases the lock, and then uses the value:

```c
mtx_lock(&sc->sc_mtx);
val = sc->sc_val;
mtx_unlock(&sc->sc_mtx);

/* ... some unrelated work ... */

mtx_lock(&sc->sc_mtx);
if (val == sc->sc_val) {
    /* act on val */
}
mtx_unlock(&sc->sc_mtx);
```

The driver assumes `val` is still current because it re-checks. But "act on val" uses the stale copy, not the current field. If `sc_val` is a pointer, the act may operate on a freed object. If `sc_val` is an index, the act may use a stale index.

The lesson: once you release a lock, any value you read under that lock is stale. If you need to re-act under the lock, re-read the state inside the re-acquisition. The `if (val == sc->sc_val)` check protects against changes; the act needs to use the current value, not the stored one.

### The Silent Truncation

A driver receives a string of up to 256 bytes, stores it in a 128-byte buffer. The code uses `strncpy`:

```c
strncpy(sc->sc_name, user_name, sizeof(sc->sc_name));
```

`strncpy` stops at the destination size. But `strncpy` does not guarantee a NUL terminator if the source was longer. Later code does:

```c
printf("name: %s\n", sc->sc_name);
```

`printf("%s", ...)` reads until a NUL. If `sc_name` is not NUL-terminated, printf reads past the end of the array into adjacent memory, potentially leaking that memory in the log or crashing.

Safer options: `strlcpy` (guarantees NUL termination, truncates if needed), or `snprintf` (same guarantee with formatting). `strncpy` is a landmine; it is in the standard library only for historical reasons.

### The Over-Logged Event

A driver logs every time an event fires. The event is user-triggerable. A user sends a million events in a loop. The kernel message buffer fills and overflows; legitimate messages are lost. The user has accomplished a denial-of-service on the logging subsystem itself.

The fix, as discussed in Section 8, is rate limiting. Every user-triggerable log message should be wrapped in a rate-limit check. A suppressed-count summary ("[secdev] 1234 suppressed messages in last 5 seconds") can be emitted periodically to inform the operator of ongoing flooding.

### The Invisible Bug

A driver works fine for years. Then a compiler update changes how it handles a specific idiom, or a kernel API changes semantics in a new FreeBSD release, and the driver's behaviour changes. A check that used to work silently stops working. Users do not notice until an exploit appears.

Invisible bugs are the strongest argument for `KASSERT`, sanitizers, and tests. A `KASSERT(p != NULL)` at the top of every function documents what that function expects. An `INVARIANTS` kernel catches the moment an invariant breaks. A good test suite notices when behavior changes.

The simpler the function and the clearer its contract, the fewer places invisible bugs can hide. This is one reason the FreeBSD kernel coding style described in `style(9)` values short functions with clear responsibilities: they are easier to reason about, which makes invisible bugs easier to avoid in the first place.

### Wrapping Up the Pattern Catalogue

Each of the patterns above has been seen in real kernel code. Many have been CVEs. The defenses are:

- Use `size_t` for sizes; avoid sign confusion.
- Whitelist validation; do not forget fields.
- Treat error paths with the same rigor as success paths.
- Use refcounts to manage object lifetime under concurrency.
- Use `sizeof` directly on the buffer rather than a drift-prone constant.
- Never dereference user pointers.
- Keep the `copyin` / `copyout` story explicit per field.
- Remember that a value read under a lock is stale after the lock is released.
- Use `strlcpy` or `snprintf`, never `strncpy`.
- Rate-limit every user-triggerable log.
- Write invariants as `KASSERT` so regressions are caught.

Memorize these patterns. Apply them as a mental checklist on every function you write or review.

## Appendix: Headers and APIs Used in This Chapter

A short reference to the FreeBSD headers referenced throughout this chapter, grouped by topic. Each header is in `/usr/src/sys/` followed by the path listed.

### Memory and Copy Operations

- `sys/systm.h`: declarations for `copyin`, `copyout`, `copyinstr`, `bzero`, `explicit_bzero`, `printf`, `log`, and many kernel core primitives.
- `sys/malloc.h`: `malloc(9)`, `free(9)`, `zfree(9)`, `MALLOC_DECLARE`, `MALLOC_DEFINE`, M_* flags.
- `sys/uio.h`: `struct uio`, `uiomove(9)`, UIO_READ / UIO_WRITE constants.
- `vm/uma.h`: UMA zone allocator (`uma_zcreate`, `uma_zalloc`, `uma_zfree`, `uma_zdestroy`).
- `sys/refcount.h`: reference-count primitives (`refcount_init`, `refcount_acquire`, `refcount_release`).

### Privilege and Access Control

- `sys/priv.h`: `priv_check(9)`, `priv_check_cred(9)`, `PRIV_*` constants, `securelevel_gt`, `securelevel_ge`.
- `sys/ucred.h`: `struct ucred` and its fields.
- `sys/jail.h`: `struct prison`, `jailed(9)` macro, prison-related helpers.
- `sys/capsicum.h`: Capsicum capabilities, `cap_rights_t`, `IN_CAPABILITY_MODE(td)`.
- `security/mac/mac_framework.h`: MAC framework hooks (mostly for policy writers, but reference).

### Locking and Concurrency

- `sys/mutex.h`: `struct mtx`, `mtx_init`, `mtx_lock`, `mtx_unlock`, `mtx_destroy`.
- `sys/sx.h`: shared/exclusive locks.
- `sys/rwlock.h`: read/write locks.
- `sys/condvar.h`: condition variables (`cv_init`, `cv_wait`, `cv_signal`).
- `sys/lock.h`: common lock infrastructure.
- `sys/atomic_common.h`: atomic operations (and architecture-specific headers).

### Device Files and Dev Infrastructure

- `sys/conf.h`: `struct cdev`, `struct cdevsw`, `struct make_dev_args`, `make_dev_s`, `make_dev_credf`, `destroy_dev`.
- `sys/module.h`: `DRIVER_MODULE`, `MODULE_VERSION`, kernel module declarations.
- `sys/kernel.h`: SYSINIT, SYSUNINIT, and related kernel hook macros.
- `sys/bus.h`: `device_t`, device methods, `bus_alloc_resource`, `bus_teardown_intr`.

### Timing, Rate Limiting, Callouts

- `sys/time.h`: `eventratecheck(9)`, `ppsratecheck(9)`, `struct timeval`.
- `sys/callout.h`: `struct callout`, `callout_init_mtx`, `callout_reset`, `callout_drain`.
- `sys/taskqueue.h`: task queue primitives (`taskqueue_create`, `taskqueue_enqueue`, `taskqueue_drain`).

### Logging and Diagnostics

- `sys/syslog.h`: `LOG_*` priority constants for `log(9)`.
- `sys/kassert.h`: `KASSERT`, `MPASS`, assertion macros.
- `sys/ktr.h`: KTR tracing macros.
- `sys/sdt.h`: Statically Defined Tracing probes for dtrace(1).

### Sysctls

- `sys/sysctl.h`: `SYSCTL_*` macros, `CTLFLAG_*` flags including `CTLFLAG_SECURE`, `CTLFLAG_PRISON`, `CTLFLAG_CAPRD`, `CTLFLAG_CAPWR`.

### Network (when applicable)

- `sys/mbuf.h`: `struct mbuf`, mbuf allocation and manipulation.
- `net/if.h`: `struct ifnet`, network interface primitives.

### Epoch and Lock-Free

- `sys/epoch.h`: epoch-based reclamation primitives (`epoch_enter`, `epoch_exit`, `epoch_wait`).
- `sys/atomic_common.h` and architecture-specific atomic headers: memory barriers, atomic reads and writes.

### Tracing and Observability

- `security/audit/audit.h`: kernel audit framework (when compiled in).
- `sys/sdt.h`: Statically Defined Tracing for dtrace integration.
- `sys/ktr.h`: KTR in-kernel tracing.

This appendix is not exhaustive; the full set of headers a driver may need is far larger. It covers the ones referenced in this chapter. When writing your own driver, `grep` through `/usr/src/sys/sys/` for the primitive you need, and read the header to understand what is available. Many of these headers are well commented and repay careful reading.

Reading the headers is itself a security practice. Every primitive has a contract: what arguments it accepts, what constraints it imposes, what it guarantees on success, what it returns on failure. A driver that uses a primitive without reading its contract is relying on assumptions that may not hold. A driver that reads the contract, and holds itself to it, is a driver that benefits from the kernel's own discipline.

Many of the headers listed above are themselves worth studying as examples of good kernel design. `sys/refcount.h` is small, carefully commented, and demonstrates how a simple primitive is built from atomic operations. `sys/kassert.h` shows how conditional compilation is used to build a feature that costs nothing in production but catches bugs in developer kernels. `sys/priv.h` shows how a long list of named constants can be organized by subsystem and used as the grammar of a policy. When you run out of ideas for how to structure your own driver's internals, these headers are a good place to find inspiration.

## Appendix: Further Reading

A short list of resources that go deeper into FreeBSD security than this chapter can:

**FreeBSD Architecture Handbook**, in particular the chapters on the jail subsystem, Capsicum, and MAC framework. Available online at `https://docs.freebsd.org/en/books/arch-handbook/`.

**FreeBSD Handbook security chapter**, which is oriented toward administrators but includes useful context on how system-level features (jails, securelevel, MAC) interact.

**Capsicum: Practical Capabilities for UNIX**, the original paper by Robert Watson, Jonathan Anderson, Ben Laurie, and Kris Kennaway. Explains the design rationale behind Capsicum, which helps when deciding how your driver should behave in capability mode.

**"The Design and Implementation of the FreeBSD Operating System"**, by Marshall Kirk McKusick, George V. Neville-Neil, and Robert N. M. Watson. The second edition covers FreeBSD 11; many security-relevant chapters remain applicable in later versions.

**style(9)**, the FreeBSD kernel coding style guide, available as a manual page: `man 9 style`. Readable kernel code is safer kernel code; the conventions in `style(9)` are part of how the tree stays reviewable at scale.

**KASAN, KMSAN, and KCOV documentation** in `/usr/src/share/man/` and related sections. Reading these helps you configure and interpret sanitizer output.

**syzkaller documentation**, at `https://github.com/google/syzkaller`. The `sys/freebsd/` directory contains syscall descriptions that illustrate how to describe your own driver's interface.

**CVE databases** such as `https://nvd.nist.gov/vuln/search` or `https://cve.mitre.org/`. Searching for "FreeBSD" or specific driver names shows real bugs that have been found and fixed. Reading a few CVE reports per month teaches a great deal about what kinds of bugs occur in practice.

**FreeBSD security advisories**, at `https://www.freebsd.org/security/advisories/`. These are official reports on fixed vulnerabilities. Many are kernel-side and relevant to driver authors.

**The FreeBSD source tree itself** is the largest and most authoritative reference. Spend time reading drivers similar to yours. Look at how they validate input, check privilege, manage locking, and handle detach. Imitating the patterns you see in well-reviewed code is one of the fastest ways to learn.

**Security mailing lists**, such as `freebsd-security@` and the broader `oss-security` list, carry daily traffic on kernel and driver issues across open-source projects. Subscribing passively and skimming a few posts a week builds awareness of threat trends without demanding much effort.

**Formal verification literature**, although specialist, has begun to touch kernel code. Projects like seL4 demonstrate what a fully verified microkernel looks like. FreeBSD is not that, but reading about formal verification shapes how you think about invariants and contracts in your own code.

**Books on secure coding practices in C** such as `Secure Coding in C and C++` by Robert Seacord translate well to kernel work, since kernel C is a dialect of the same language and has the same pitfalls, plus more. Chapter-by-chapter, they provide the mental catalogue of bugs that this chapter could only sketch.

**FreeBSD-specific books**, notably the McKusick, Neville-Neil, and Watson book mentioned above, but also older volumes that cover the evolution of specific subsystems. Reading about how jails evolved, how Capsicum was designed, or how MAC came to be helps you understand the rationale behind the primitives rather than just their mechanics.

**Conference talks** from BSDCan, EuroBSDCon, and AsiaBSDCon often touch security topics. Video archives let you watch years of past talks at your own pace. Many talks are given by active FreeBSD developers and reflect current thinking.

**Academic papers on operating system security** from venues such as USENIX Security, IEEE S&P, and CCS provide a longer-term view. Not every paper is relevant to drivers, but the ones that are deepen your understanding of threat models, attacker capabilities, and the theoretical basis for mitigations.

**The CVE feed**, particularly when filtered for kernel issues, is a continuous drip of real-world examples. Reading a few each week builds intuition for what bugs look like in practice and which classes recur most often.

**Your own code, six months later**. Rereading your earlier work with the benefit of distance is a valuable learning tool. The bugs you will notice are the bugs you have learned to see since you wrote it. Make a habit of this; schedule time for it.

The resources above, even a small subset of them, will keep you growing for years. Security is a field of continuous learning. This chapter is one step in that learning; the next step is yours.

Every security-minded driver author should have read at least a few of these. The field moves, and staying current is part of the craft.

## Wrapping Up

Security in device drivers is not a single technique. It is a way of working. Every line of code carries a little responsibility for the kernel's safety. The chapter has covered the main pillars:

**The kernel trusts every driver fully.** Once code runs in the kernel, there is no sandbox, no isolation, no second chance. The driver author's discipline is the system's last line of defense.

**Buffer overflows and memory corruption** are the classical kernel vulnerability. They are prevented by bounding every copy, preferring bounded string functions, and treating pointer arithmetic with suspicion.

**User input crosses a trust boundary.** Every byte from user space must be copied into the kernel with `copyin(9)`, `copyinstr(9)`, or `uiomove(9)` before it is used. Every byte going back must be copied out with `copyout(9)` or `uiomove(9)`. The user-space memory is not trustworthy; kernel memory is. Keep them cleanly separated.

**Memory allocation** must be checked, balanced, and accounted for. Always check `M_NOWAIT` returns. Use `M_ZERO` by default. Pair every `malloc` with exactly one `free`. Use a per-driver `malloc_type` for accountability. Use `explicit_bzero` or `zfree` for sensitive data.

**Races and TOCTOU bugs** are caused by inconsistent locking or by treating user-space data as stable. Fix them with locks around shared state and by copying user data before validating.

**Privilege checks** use `priv_check(9)` as the canonical primitive. Layer with jail awareness and securelevel where appropriate. Set conservative device-node permissions. Let the MAC and Capsicum frameworks work alongside.

**Information leaks** are prevented by zeroing structures before filling them, bounding copy lengths on both ends, and keeping kernel pointers out of user-visible output.

**Logging** is part of the driver's interface. Use it to help the operator without helping the attacker. Rate-limit anything triggerable from user space. Do not log sensitive data.

**Secure defaults** mean failing closed, whitelisting rather than blacklisting, setting conservative default values, and treating error paths with the same care as success paths.

**Testing and hardening** turn careful code into trustworthy code. Build with `INVARIANTS`, `WITNESS`, and the kernel sanitizers. Stress-test. Fuzz. Review. Re-test.

None of this is a one-time effort. A driver stays secure because its author keeps applying these habits, every commit, every release, for the life of the code.

The discipline is not glamorous. It is boring work: zero the structure, check the length, acquire the lock, use `priv_check`. But this boring work is exactly what keeps systems secure. An exploited kernel is a catastrophic event for users. An exploited driver is a foothold into the kernel. The person at the keyboard of that driver, deciding whether to add the bounds check or to skip it, is making a security decision that may be invisible for years and then suddenly matter very much.

Be the author who adds the bounds check.

### One More Reflection: Security as Professional Identity

Something worth saying explicitly: the habits in this chapter are not merely techniques. They are what distinguishes a journeyman kernel author from an apprentice. Every mature kernel engineer carries this mental checklist not because they memorized it but because they have, over years, internalized a skepticism toward their own code. The skepticism is not anxiety. It is discipline.

Write code, and then read it back as if a stranger had written it. Ask what happens if the caller is hostile. Ask what happens if the value is zero, or negative, or impossibly large. Ask what happens if the other thread arrives between these two statements. Ask what happens on the error path you did not plan to test. Write the check. Write the assertion. Move on.

This is what professional kernel engineers do. It is not glamorous, it is rarely applauded, and it is what keeps the operating system we all rely on from falling apart. The kernel is not magic; it is millions of lines of carefully checked code, written and rewritten by people who treat every line as a small responsibility. Joining that profession means joining that discipline.

You have now been given the tools. The rest is practice.

## Looking Ahead: Device Tree and Embedded Development

This chapter trained you to look at your driver from the outside, through the eyes of whoever might try to misuse it. The boundaries you learned to watch were invisible to the compiler but very real to the kernel: user space on one side, kernel memory on the other; one thread with privilege, another without; a length field the caller claimed, a length the driver had to verify. Chapter 31 was about *who is allowed to ask the driver to do something*, and *what the driver should check before it agrees*.

Chapter 32 shifts the perspective entirely. The question stops being *who wants this driver to run* and becomes *how does this driver find its hardware at all*. On the PC-like machines we have leaned on so far, that question had a comfortable answer. PCI devices announced themselves through standard configuration registers. ACPI-described peripherals appeared in a table the firmware handed to the kernel. The bus did the looking, the kernel probed each candidate, and your driver's `probe()` function only had to look at an identifier and say yes or no. Discovery was mostly someone else's problem.

On embedded platforms that assumption breaks. A small ARM board does not speak PCI, does not carry an ACPI BIOS, and does not hand the kernel a neat table of devices. The SoC has an I2C controller at a fixed physical address, three UARTs at three other fixed addresses, a GPIO bank at a fourth, a timer, a watchdog, a clock tree, and a dozen other peripherals soldered onto the board in a particular arrangement. Nothing in the silicon announces itself. If the kernel is going to attach drivers to these peripherals, something has to tell the kernel where they are, what they are, and how they relate.

That something is the **Device Tree**, and Chapter 32 is where you learn to work with it. You will see how `.dts` source files describe the hardware, how the Device Tree Compiler (`dtc`) turns them into the `.dtb` blobs the bootloader hands to the kernel, and how FreeBSD's FDT support walks those blobs to decide which drivers to attach. You will meet the `ofw_bus` interfaces, the `simplebus` enumerator, and the Open Firmware helpers (`ofw_bus_search_compatible`, `ofw_bus_get_node`, the property-reading calls) that turn a Device Tree node into a working driver attachment. You will compile a small overlay, load it, and watch a pedagogical driver attach in `dmesg`.

The security habits you have built in this chapter travel with you into that territory. A driver for an embedded board is still a driver: it still runs in kernel space, still copies data across user-space boundaries, still needs bounds checks, still takes locks, still cleans up in detach. An ARM board does not loosen any of those requirements. If anything, embedded systems raise the stakes, because the same board image may ship to thousands of devices in the field, each one harder to patch than a server in a data center. The disposition you have just learned, skeptical of inputs, careful with memory, conservative about privilege, is exactly the disposition an embedded driver author needs.

What changes in Chapter 32 is the set of helpers you call to discover your hardware and the files you read to know where to point them. The probe-attach-detach shape stays. The softc stays. The lifecycle stays. A handful of new calls and a new way of thinking about hardware description are what you add. The chapter builds them up gently, from the shape of a `.dts` file to a working driver that blinks an LED on a real or emulated board.

See you there.

## A Final Note on Habits

This chapter has been longer than some. The length is deliberate. Security is not a topic that can be summarized into a single punchy rule; it is a way of thinking that requires examples, practice, and repetition. A reader who finishes this chapter once will have been exposed to the patterns. A reader who returns to this chapter when starting a new driver will find new meaning in passages that seemed merely informative on the first read.

Here are the most important habits, condensed into a single list for you to carry forward. They are the reflexes that matter most in daily driver work:

Every user-space value is hostile until copied in, bounded, and validated.

Every length has a maximum. The maximum is enforced before anything uses the length.

Every structure copied to user space is zeroed first.

Every allocation is paired with a free on every code path.

Every critical section is held across the full check-and-act sequence it protects.

Every privilege-sensitive operation checks `priv_check` before acting.

Every detach path drains async work before freeing state.

Every log message triggerable from user space is rate-limited.

Every unknown input returns an error, never a silent success.

Every assumption worth making is worth writing as a `KASSERT`.

Nine lines. If these become automatic, you have the core of what this chapter teaches.

The craft grows from here. There are more patterns, more subtleties, more tools; you will encounter them as you read more FreeBSD source, as you review more code, as you write more drivers. What stays the same is the disposition: skeptical of hostile inputs, careful with memory, clear about lock boundaries, conservative about what to expose. That disposition is the one kernel engineers share across decades. You have it now. Use it well.

## A Note on Evolving Threats

One further thought before the closing words. The threats we defend against today are not the threats we will defend against in ten years. Attackers evolve. Mitigations evolve. New classes of bugs are discovered, old classes are retired. A driver that was state-of-the-art in its defenses in 2020 may need updating to be considered safe in 2030.

This is not a reason for despair. It is a reason for continuous learning. Every year, a responsible driver author should read a few new security papers, try a few new sanitizers, and look at the recent CVEs affecting kernels similar to their own. Not to memorize specific vulnerabilities, but to keep a sense of where the bugs are being found today.

The patterns this chapter teaches are stable. Buffer overflows have been bugs since before UNIX. Use-after-free has been a bug since C had malloc. Race conditions have been bugs since kernels had multiple threads. The specific incarnations change, but the underlying defenses endure. A driver written with the disposition this chapter encourages will be mostly right in any decade; when the details shift, the author who built the disposition will adapt faster than one who merely memorized a checklist.

## Closing Words

A driver is small. A driver's influence is large. The code you write runs in the most privileged part of the system, touches memory that every other process depends on, and is trusted with the secrets of users who will never see your name. That trust is not automatic; it is earned, one careful line at a time, by authors who assumed the attacker was watching and built accordingly.

The authors of FreeBSD have been writing that kind of code for decades. The FreeBSD kernel is not perfect; no kernel of its scale can be. But it has a culture of care, a set of primitives that reward diligence, and a community that treats security bugs as learning opportunities rather than embarrassments. When you write a FreeBSD driver, you are writing into that culture. Your code will be read by people who know the difference between a buffer overflow and a buffer that happens to be large enough; who know the difference between a privilege check that catches root-outside-jail and one that catches root-inside-jail; who know that a race condition is not a rare timing fluke but a vulnerability waiting for the right attacker.

Write for those readers. Write for the user whose laptop runs your code without knowing it is there. Write for the maintainer who will inherit your work in ten years. Write for the reviewer who will spot the defensive check you added and feel quietly glad that someone thought of it.

That is what chapter 31 has been about. That is what the rest of your career as a kernel author will be about. Thank you for taking the time to work through it carefully. The chapter ends here; the practice begins tomorrow.
