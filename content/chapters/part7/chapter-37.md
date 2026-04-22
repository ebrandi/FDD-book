---
title: "Submitting Your Driver to the FreeBSD Project"
description: "Process and guidelines for contributing drivers to FreeBSD"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 37
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 150
---

# Submitting Your Driver to the FreeBSD Project

## Introduction

If you have followed this book from the beginning, you have
travelled a long way. You started without any kernel knowledge at
all, learned UNIX and C, walked through the anatomy of a FreeBSD
driver, built character and network drivers by hand, explored the
Newbus framework, and worked through a full part on mastery topics:
portability, virtualisation, security, FDT, performance, advanced
debugging, asynchronous I/O, and reverse engineering. You have now
reached the point where you can sit down in front of a lab machine,
open a text editor, and write a driver for a device that FreeBSD
does not yet support. That is a serious engineering skill, and it
did not come for free.

This chapter is where the work turns outward. Up to now, the
drivers you have built have lived on your own systems. You loaded
them with `kldload`, tested them, debugged them, and unloaded them
when you were done. They were useful to you, and perhaps to a few
friends or colleagues who copied them from your repository. That is
already worthwhile work. But a driver that lives only on your
machine serves only the people who happen to find your machine. A
driver that lives inside the FreeBSD source tree serves every
FreeBSD user, on every release, on every architecture the driver
targets, for as long as the code is maintained. The amplification
of value is enormous, and the responsibilities that come with it
are the subject of this chapter.

The FreeBSD Project has been accepting contributions from outside
developers since the early 1990s. Thousands of people have submitted
patches; hundreds have eventually become committers. The process by
which new code enters the tree is not a bureaucratic obstacle
course. It is a review workflow designed to preserve the qualities
that make FreeBSD trustworthy: code consistency, long-term
maintainability, portability across architectures, legal cleanness,
careful documentation, and continuity of care. Every one of those
qualities is something the reviewers protect on behalf of everyone
who runs FreeBSD. When you submit a driver, you are asking the
project to take on a long-term responsibility for it. The review
process is how the project confirms that the driver is worth that
responsibility, and also how the project helps you get the driver
into a shape where the answer is yes.

That framing matters. New contributors often arrive at the review
process expecting an adversarial experience, where reviewers look
for reasons to reject the work. The reality is the opposite.
Reviewers are, overwhelmingly, trying to help. They want your
driver to be merged. They want it to be merged in a form that will
still work five releases from now. They want it to not be a
maintenance burden for whoever has to touch the surrounding code
next year. The comments they leave on a first-round patch are not a
score; they are a list of things that, when addressed, will let the
patch be committed. A contributor who internalises that framing
finds the review process cooperative rather than stressful.

There is, however, a distinction that needs to be clear from the
start. A working driver is not the same thing as an upstream-ready
driver. A driver that loads on your laptop, drives your hardware,
and does not panic when you unload it has cleared only the first
few checkpoints. To be upstream-ready it also needs to pass the
project's style guidelines, carry a proper licence, come with a
manual page that explains how a user interacts with it, build on
every architecture the project supports, integrate cleanly into the
existing source-tree layout, and be accompanied by a commit message
that another reviewer can read in five years without needing to
reconstruct the context. None of these are busywork. Each of them
exists because experience has shown what happens to code bases
where they are skipped.

The chapter is organised around a natural workflow. We will begin
by looking at how the FreeBSD Project is organised from the point
of view of a contributor, and what the distinction between
contributor and committer actually means in practice. We will then
walk through the mechanical preparation of a driver for submission:
what file layout to use, what code style to follow, how to name
things, and how to write a commit message that a reviewer can read
with gratitude. We will look at licensing and legal compatibility,
because even excellent code cannot be accepted if its provenance is
unclear. We will spend a serious amount of time on manual pages,
because the manual page is the reader-facing half of the driver and
it deserves the same care as the code. We will walk through testing
expectations, from local builds through `make universe`, and we
will see how to generate a patch in a form the reviewers find
convenient. We will discuss the social side of the review process:
how to work with a mentor, how to respond to feedback, how to
iterate through review rounds without losing momentum. And we will
end with the longest-lasting commitment of all, which is
maintenance after the driver has been merged.

The companion code for this chapter, in
`examples/part-07/ch37-upstream/`, includes several practical
artifacts: a reference driver-tree layout that mirrors the shape a
small driver would take in `/usr/src/sys/dev/`; a sample manual
page you can adapt to your own driver; a submission checklist you
can use as a final review before sending a patch; a draft cover
letter for an email to a project mailing list; a helper script that
generates a patch with the conventions the project expects; and a
pre-submission validation script that runs the lint, style, and
build checks in the correct order. None of these are a substitute
for understanding the underlying material, but they will save you
from the common mistakes that cost first-time contributors a round
or two of reviews.

One more note before we begin. This chapter is not going to teach
you the political or governance history of the FreeBSD Project. We
will touch on the Core Team and the roles of the various committees
only as much as is necessary for a contributor to navigate the
project. If you are curious about FreeBSD governance after reading
this chapter, the project's own documentation is the right next
stop, and we will point you toward it. The scope of this chapter is
the practical work of turning a driver you have written into a
driver that can be merged upstream.

By the end of the chapter, you will have a clear picture of the
submission workflow, a hands-on understanding of the style and
documentation conventions, a rehearsal of the review cycle, and a
realistic sense of what happens after your driver is in the tree.
You will not be a FreeBSD committer by the end of this chapter; the
project grants commit rights only after a sustained history of
quality contributions, and that is by design. But you will know how
to make the first contribution, how to make it well, and how to
build up the reputation that could, in time, lead to commit rights
if that is a direction you choose to pursue.

## Reader Guidance: How to Use This Chapter

This chapter sits in Part 7 of the book, immediately after the
chapter on reverse engineering and immediately before the closing
chapter. Unlike many earlier chapters, the subject matter here is
more about workflow and discipline than about kernel internals. You
will not need to write any new driver code to follow the chapter,
although you will benefit greatly if you apply what you learn to a
driver you have already written.

The reading time is moderate. If you read straight through without
stopping to try anything, the prose will take around two to three
hours. If you work through the labs and challenges, budget a full
weekend or several evenings. The labs are structured as short,
focused exercises that you can do against any small driver you have
on hand, including one of the earlier chapter drivers, one of the
mock drivers from Chapter 36, or a fresh driver you write for this
chapter.

You do not need any special hardware. A FreeBSD 14.3 development
virtual machine, or a bare-metal FreeBSD system where you are
comfortable running build and test commands, is sufficient. The
labs will ask you to apply style checks to real code, build a real
driver as a loadable module, validate a manual page with
`mandoc(1)`, and rehearse the patch-generation workflow against a
throwaway git branch. Nothing will touch the real FreeBSD
Phabricator or GitHub, so there is no risk of accidentally
submitting half-finished work to the project.

A reasonable reading schedule looks like this. Read Sections 1 and
2 in one sitting; they set up the conceptual framing of how FreeBSD
development works and how your driver should be laid out. Take a
break. Read Sections 3 and 4 in a second sitting; they cover
licensing and manual pages, which together make up most of the
paperwork side of a submission. Read Sections 5 and 6 in a third
sitting; they cover testing and actual patch generation, which is
where the chapter turns from preparation to action. Read Sections 7
and 8 in a fourth sitting; they cover the human and long-term sides
of contribution. The labs are best done over a weekend, with
enough time to redo them more than once if the first pass exposes
something you would like to improve.

If you are already a confident FreeBSD user and a confident kernel
developer, the material in this chapter will feel familiar in
broad shape but may still surprise you in the specifics. The
specifics matter. A reviewer who knows the tree well will notice
within seconds whether the file layout matches the conventions,
whether the copyright header is in the current recommended form,
whether the manual page uses the modern mdoc idioms, and whether
the commit message follows the expected subject-line style. Getting
these small things right up front makes the difference between a
review that lasts one round and a review that lasts five.

If you are a beginner, do not let the specifics intimidate you.
Every committer in the project was, at one point, someone whose
first patch bounced off five rounds of review before landing. The
ability to write good code is something you have already built up
over the book. The ability to submit it well is what this chapter
adds. You will not get it perfect on the first try. That is
normal. What matters is that you understand the shape of the
process and that you approach each review with the intent to
improve the submission rather than to defend it.

Several of the guidelines in this chapter, especially around
licensing, manual pages, and the review workflow, reflect the state
of the FreeBSD Project as of FreeBSD 14.3. The project evolves, and
a few of the specific conventions may shift over time. Where we
know a convention is changing, we will flag it. Where we are citing
a specific file in the tree, we will name the file so you can open
it yourself and verify the current state. The reader who trusts but
also verifies is the reader the project benefits from most.

One last note about pace. This chapter deliberately teaches
discipline as much as it teaches process. Several of the sections
will seem almost repetitive in their insistence on small details:
trailing whitespace, correct header comments, exact manual-page
macro usage. That insistence is part of the lesson. FreeBSD is a
large code base with a long institutional memory, and the small
details are what keep it maintainable. If you find yourself tempted
to skim a style section, slow down instead. The slowness is the
craft.

## How to Get the Most Out of This Chapter

The chapter is organised to be read linearly, but every section
stands on its own well enough that you can come back to a specific
section when you need to. Several habits will help you absorb the
material.

First, read each section with the FreeBSD source tree open on your
screen. Every time the chapter mentions a reference file such as
`/usr/src/share/man/man9/style.9`, open it and skim it. The
chapter gives you the shape and the motivation; the reference files
give you the authoritative detail. A habit of cross-checking what
you read in the chapter against what the tree actually says will
serve you for the rest of your career as a FreeBSD contributor.

> **A note on line numbers.** When the chapter points you at a named
> piece of infrastructure in the tree, such as `make_dev_s`,
> `DRIVER_MODULE`, or the `style(9)` rules themselves, the pointer is
> anchored on that name. The `mydev.c:23` style-check transcripts you
> will see later refer to lines in your own in-progress driver and will
> change as you edit it. Either way, the durable reference is the
> symbol, not the number: grep for the name rather than rely on a line.

Second, keep a small note file as you read. Every time a section
mentions a convention, a required section, or a command, write it
down. By the end of the chapter you will have a personalised
submission checklist. The companion `examples/part-07/ch37-upstream/`
directory includes a checklist template you can start from, but a
checklist you have typed up yourself, in your own words, will be
more useful than any template.

Third, have a small driver of your own in mind as you read. It
could be the LED driver from earlier chapters, the mock device from
Chapter 36, or a character driver you wrote for practice. The
chapter will ask you to imagine preparing that specific driver for
submission. Working against a concrete driver makes the
instructions land far more firmly than trying to absorb them in the
abstract.

Fourth, do not skip the labs. The labs in this chapter are short
and practical. Most of them take less than an hour. They exist
because there are parts of the submission process that only become
clear once you try them against real code. A reader who works
through the labs will emerge with genuine muscle memory; a reader
who skips them will re-read the chapter six months from now and
discover that most of it did not stick.

Fifth, treat the early mistakes as part of the training. The first
time you run `tools/build/checkstyle9.pl` against your code, you
will see warnings. The first time you run `mandoc -Tlint` against
your manual page, you will see warnings. The first time you run
`make universe` against your tree, you will see errors on at least
one architecture. Every one of those warnings is teaching you
something. The reviewers in the project see these same warnings
every day; the craft of preparing a submission is, in large part,
the craft of noticing and fixing them before anyone else has to.

Finally, be patient with the rhythm of the chapter. A few of the
later sections spend time on what might seem like social or
interpersonal material: how to handle feedback, how to respond to a
reviewer who has misunderstood your patch, how to build the
relationships that lead to sponsorship. That material is not
optional. Software engineering at the level of an open-source
kernel project is a collaborative craft, and the collaboration is
the craft. Reading those sections carelessly will cost you more in
practice than reading the style sections carelessly.

You now have the map. Let us turn to the first section and look at
how the FreeBSD Project is organised from the point of view of a
contributor.

## Section 1: Understanding the FreeBSD Contribution Process

### What the FreeBSD Project Actually Is

Before we can talk about contributing to the FreeBSD Project, we
need a clear picture of what the project is. The FreeBSD Project
is a community of volunteers and paid contributors who together
develop, test, document, release, and support the FreeBSD operating
system. The project has been continuously active since 1993. It is
organised around a set of shared source trees, a code-review
culture, a release engineering discipline, and a body of accumulated
institutional knowledge about how kernels, userlands, ports, and
documentation should be put together.

The project is often summarised in three words: source, ports, and
documentation. These correspond to three main repositories or
subprojects, each with its own maintainers, reviewers, and
conventions. Source, usually written as `src`, is the base system:
the kernel, the libraries, the userland utilities, everything that
a FreeBSD installation ships with. Ports is the collection of
third-party software that can be built on FreeBSD, such as
programming languages, desktop environments, and application
servers. Documentation is the Handbook, the articles, the books
such as the Porter's Handbook and the Developer's Handbook, the
FreeBSD website, and the translation infrastructure.

Device drivers live primarily in the `src` tree, because they are
part of the base system kernel and base-system support for
hardware. When this chapter talks about submitting a driver, it
means submitting it to the `src` tree. Ports and Documentation
have their own contribution pipelines, which follow similar
principles but differ in detail. This chapter focuses exclusively
on `src`.

The `src` tree is large. You can see its top-level structure by
browsing `/usr/src/`. The manual page
`/usr/src/share/man/man7/development.7` gives a short, readable
introduction to the development process, and the file
`/usr/src/CONTRIBUTING.md` is the project's own current guidance
for contributors. If you read only two files before your first
submission, read those two. We will quote from both repeatedly in
this chapter.

### The Project's Decision-Making Structure

FreeBSD uses a relatively flat decision-making structure compared
to some other large projects. The core of the structure is the
group of committers, who are developers with write access to the
source repositories. Committers are elected by existing committers
after a sustained history of quality contributions. A nine-person
elected body called the Core Team handles certain kinds of
project-wide decisions and disputes. Smaller teams such as the
Release Engineering Team (re@), the Security Officer Team (so@),
the Ports Management Team (portmgr@), and the Documentation
Engineering Team (doceng@) look after specific areas.

For the purposes of submitting a driver, most of that structure
does not matter much in day-to-day practice. The people who will
review your driver are individual committers who happen to know the
subsystem your driver fits into. If your driver is a network
driver, the reviewers will probably be people active in the
networking subsystem. If it is a USB driver, the reviewers will be
people active in USB. The Core Team is not involved in individual
driver submissions; neither is the Release Engineering Team,
although they will be the ones who decide which release your driver
first appears in once it is merged.

The practical mental model is this. The FreeBSD Project is a large
community of engineers. Some of them can commit to the tree
directly. A much larger number contribute through review processes.
When you submit a driver, you become part of that larger number,
and the review process is how the committer community evaluates
whether the driver is ready to enter the tree under their shared
responsibility.

### Contributor Versus Committer

The distinction between contributor and committer is central to
how the project works, and it is often misunderstood by newcomers.

A contributor is anyone who submits changes to the project. You
become a contributor the first time you open a Phabricator review
or a GitHub pull request against the FreeBSD source tree. There is
no formal process for becoming a contributor. You simply submit
work. If the work is good, it gets reviewed, revised, and
eventually committed to the tree by a committer on your behalf.
The commit carries your name and email in the `Author:` field, so
that you get credit for the code even though you did not push it
yourself.

A committer is a contributor who has been granted direct write
access to one of the repositories. Commit rights are granted after
a sustained history of quality contributions, usually over several
years, and only after a nomination by an existing committer and a
vote by the relevant committer group. Commit rights come with
responsibilities: you are expected to review other people's
patches, participate in project discussions, and take ownership of
the code you have committed over the long term.

The two roles are not a hierarchy of prestige. They are a division
of labour. Contributors focus on writing and submitting good
patches. Committers focus on reviewing, integrating, and
maintaining the tree. A contributor with a single high-value patch
is more valuable to the project than a committer who does not
actively participate. The project depends on both.

For this chapter, you should think of yourself as a contributor.
Your goal is to produce a submission that a committer can review,
accept, and commit. If, years from now, you find yourself with a
long history of contributions and a sustained relationship with the
project, the question of commit rights may arise organically. But
that is a question for later. The focus here is making your first
contributions count.

### How src Work Is Organised

The `src` repository is a single git tree. The main branch,
confusingly called `main` in git but also referred to as CURRENT in
release-engineering language, is where all active development
happens. Changes are committed first to `main`. Then, if the
change is a bug fix or a small feature that fits within a stable
release, it may be cherry-picked back into one of the `stable/`
branches, which correspond to the major FreeBSD releases such as
14 and 15. Releases themselves are tagged points on the `stable/`
branches.

As a driver contributor, your default target is `main`. Your patch
should apply to current `main`, should build against current
`main`, and should be tested against current `main`. If the driver
is something users of FreeBSD 14 would also want, a committer may
choose to cherry-pick the commit back to the relevant `stable/`
branch after it has been in `main` for a bit, but that is the
committer's decision, not yours to make in the submission.

The git repository is visible at `https://cgit.freebsd.org/src/`
and is also mirrored at `https://github.com/freebsd/freebsd-src`.
You can clone from either. The authoritative push URL, for those
with commit access, is `ssh://git@gitrepo.FreeBSD.org/src.git`, but
as a contributor you will not push directly. You will generate
patches and send them through the review workflow.

### Where Device Drivers Live in the Source Tree

Most device drivers live under `/usr/src/sys/dev/`. This directory
contains hundreds of subdirectories, one per driver or device
family. If you browse it, you will see a cross-section of the
hardware FreeBSD supports: Ethernet chips, SCSI adapters, USB
devices, sound cards, I/O controllers, and a long list of other
categories.

A small selection of existing driver subdirectories worth knowing:

- `/usr/src/sys/dev/null/` for the `/dev/null` character device.
- `/usr/src/sys/dev/led/` for the generic LED framework.
- `/usr/src/sys/dev/uart/` for UART drivers.
- `/usr/src/sys/dev/virtio/` for the VirtIO family.
- `/usr/src/sys/dev/usb/` for the USB subsystem and the USB-side
  device drivers.
- `/usr/src/sys/dev/re/` for the RealTek PCI/PCIe Ethernet driver.
- `/usr/src/sys/dev/e1000/` for the Intel Gigabit Ethernet driver
  family.
- `/usr/src/sys/dev/random/` for the kernel random-number
  subsystem.

Some categories of driver live elsewhere. Network drivers whose
role is more about the networking stack than about the device
itself sometimes live under `/usr/src/sys/net/`. Filesystem-like
devices and pseudo-devices sometimes live under `/usr/src/sys/fs/`.
Architecture-specific drivers sometimes live under
`/usr/src/sys/<arch>/`. For most beginner submissions, however,
the question will be which subdirectory under
`/usr/src/sys/dev/` is the right home, and the answer is almost
always obvious. If your driver is for a new network chip, it
probably belongs in its own subdirectory under
`/usr/src/sys/dev/`, possibly inside an existing family
subdirectory if it extends an existing family. If it is for a USB
device, you may find that it lives under `/usr/src/sys/dev/usb/`
instead. If you are unsure, a search through the existing tree for
a similar driver will usually tell you where yours belongs.

### The Second Half of the Driver: Kernel Build Integration

In addition to the driver source files themselves, a driver that
is merged into FreeBSD has a second home under
`/usr/src/sys/modules/`. This directory contains the kernel-module
Makefiles that let the driver be built as a loadable kernel module.
For each driver in `/usr/src/sys/dev/<driverdir>/`, there is
typically a corresponding directory in
`/usr/src/sys/modules/<moduledir>/` containing a small Makefile
that tells the build system how to assemble the module. We will
look at that Makefile in detail in Section 2.

A few drivers have additional integration points. Drivers that
ship as part of the default kernel are listed in the architecture
configuration files under
`/usr/src/sys/<arch>/conf/GENERIC`. Drivers that come with
device-tree bindings may have entries under
`/usr/src/sys/dts/`. Drivers that expose tunable sysctls or
loader variables need entries in the relevant documentation.

As a first-time contributor, you do not need to worry about all of
these integration points at once. The minimum set for a typical
driver submission is the files under `/usr/src/sys/dev/<driver>/`,
the Makefile under `/usr/src/sys/modules/<driver>/`, and the
manual page under `/usr/src/share/man/man4/<driver>.4`. Anything
beyond that is incremental.

### The Review Platforms

FreeBSD currently accepts source-code contributions through
several channels. The `/usr/src/CONTRIBUTING.md` file lists them
explicitly:

- A GitHub pull request against
  `https://github.com/freebsd/freebsd-src`.
- A code review in Phabricator at
  `https://reviews.freebsd.org/`.
- An attachment on a Bugzilla ticket at
  `https://bugs.freebsd.org/`.
- Direct access to the git repository, for committers only.

Each of these channels has its own conventions and its own
preferred use cases.

Phabricator is the traditional code-review platform for the
project. It handles full review workflows: multi-round feedback,
revision history, inline comments, reviewer assignment, and
commit-ready patches. Most significant patches, including most
driver submissions, go through Phabricator. You will see it
referred to as "review D12345" or similar, where `D12345` is the
Phabricator differential revision identifier.

GitHub pull requests are an increasingly accepted submission
route, particularly for small, self-contained, uncontroversial
patches. The `CONTRIBUTING.md` file explicitly notes that GitHub
PRs work well when the change is limited to fewer than about ten
files and fewer than about two hundred lines, and when it passes
the GitHub CI jobs and has limited scope. A typical small driver
fits within those bounds; a larger driver with many files and
integration points may be better handled through Phabricator.

Bugzilla is the project's bug tracker. If your driver fixes a
specific reported bug, a patch attached to the corresponding
Bugzilla entry is the right home for it. If the driver is new
work rather than a bug fix, Bugzilla is usually not the right
starting point, although a reviewer may ask you to open a
Bugzilla ticket so that the work has a tracking number.

For a first driver submission, either Phabricator or a GitHub pull
request is appropriate. Many contributors start with a GitHub PR
because the workflow is familiar, and switch to Phabricator if the
review grows beyond what GitHub handles well. We will walk through
both routes in Section 6.

The review-platform landscape does shift over time, and the
specific URLs, scope limits, and preferred routes described in
this chapter can be superseded by changes to
`/usr/src/CONTRIBUTING.md` or to the project's contribution pages.
The process details above were last verified against the in-tree
`CONTRIBUTING.md` on 2026-04-20. Before you prepare your first
submission, re-read the current `CONTRIBUTING.md` and the
committer's guide linked from the FreeBSD documentation site; if
they disagree with this chapter, trust the project's files, not
the book.

### Exercise: Browse the Source Tree and Identify Similar Drivers

Before moving on to Section 2, spend half an hour browsing
`/usr/src/sys/dev/` and building an intuition for what a FreeBSD
driver looks like from the outside.

Pick three or four drivers that are roughly similar in scope to
the one you intend to submit, or to any driver you have built
during this book. For each, look at:

- The directory contents. How many source files? How many headers?
  What are the file names?
- The corresponding Makefile under `/usr/src/sys/modules/`. What
  does it list in `KMOD=` and `SRCS=`?
- The manual page under `/usr/src/share/man/man4/`. Open it and
  note the section structure.
- The copyright header in the main `.c` file. Note its format.

You are not trying to memorise anything in this exercise. You are
building a baseline intuition. By the time you have looked at
three or four real drivers, the conventions of the tree will feel
less abstract. When Section 2 talks about where files go and how
they are named, the recommendations will land on top of a mental
picture you have already built. That is the right way to absorb
this material.

### Wrapping Up Section 1

The FreeBSD Project is a long-lived community organised around
three main subprojects: src, ports, and documentation. Device
drivers live in the src tree, mostly under `/usr/src/sys/dev/`,
with corresponding module Makefiles under `/usr/src/sys/modules/`
and manual pages under `/usr/src/share/man/man4/`. Contributions
enter the tree through a review process handled by committers who
are active in the relevant subsystem. The distinction between
contributor and committer is a division of labour, not a hierarchy
of prestige. Your goal as a first-time contributor is to produce a
submission that a committer can review, accept, and commit.

With that framing in place, we can now turn to the mechanical
question of what shape your driver should be in before you submit
it. Section 2 walks through the preparation step by step.

## Section 2: Getting Your Driver Ready for Submission

### The Gap Between a Working Driver and a Submission-Ready Driver

A driver that loads, runs, and unloads cleanly on your test
machine is a working driver. A driver that a FreeBSD committer
can review, merge, and maintain is a submission-ready driver. The
gap between the two is almost always larger than first-time
contributors expect, and closing the gap is the work of this
section.

The gap has three parts. The first is layout: where the files go,
what they are named, and how they integrate with the existing
build system. The second is style: how the code is formatted,
named, and commented, and how closely it matches the project's
`style(9)` guidelines. The third is presentation: how the commit
is packaged, what the commit message says, and how the patch is
structured for review. None of these are hard once you know what
to look for, but each of them involves a dozen or two small
conventions that collectively determine whether a reviewer's first
impression is smooth or bumpy.

Before we begin, take a moment to appreciate why these conventions
exist. FreeBSD has thirty years of accumulated code. Thousands of
drivers have entered the tree over that time. The conventions that
feel arbitrary when you first encounter them are, in almost every
case, the result of an earlier painful experience that the
community decided never to repeat. A convention that prevents a
bug, or that reduces a recurring source of reviewer friction, pays
for itself many times over. When you follow the conventions, you
are benefiting from thirty years of institutional memory. When you
ignore them, you are volunteering to relearn those lessons yourself,
and to put your reviewers through them again.

### Where the Files Go

For a standalone driver in the tree, the typical layout looks like
this. Let us assume your driver is called `mydev` and that it
drives a PCI-attached sensor board.

- `/usr/src/sys/dev/mydev/mydev.c` is the main driver source
  file. For a small driver, this may be the only source file.
- `/usr/src/sys/dev/mydev/mydev.h` is the driver header. If you
  only have a single `.c` file and its internal declarations do
  not need to be exposed, you may not need this header.
- `/usr/src/sys/dev/mydev/mydevreg.h` is a common name for a
  header that defines the hardware registers and bit fields. This
  convention, using a `reg` suffix, is widespread in the tree, and
  separating register definitions from driver-internal declarations
  is good practice.
- `/usr/src/sys/modules/mydev/Makefile` is the Makefile for
  building the driver as a loadable kernel module.
- `/usr/src/share/man/man4/mydev.4` is the manual page.

You may encounter existing drivers that do not follow this exact
layout. Older drivers from before the current conventions were
established sometimes put everything in one place, or use
different file names. The conventions continue to evolve. For a
new driver, following the modern layout will save you review
friction.

### What Goes in `mydev.c`

The main source file typically contains, in order:

1. The copyright and licence header, in the format we will cover
   in Section 3.
2. The `#include` directives, typically starting with
   `<sys/cdefs.h>` and `<sys/param.h>`, followed by the other
   kernel headers your driver needs.
3. Forward declarations and static variables.
4. The driver methods: `probe`, `attach`, `detach`, and anything
   else your `device_method_t` table references.
5. Any helper functions.
6. The `device_method_t` table, the `driver_t` structure, the
   `DRIVER_MODULE` registration, and the `MODULE_VERSION`
   declaration. Modern FreeBSD drivers no longer declare a
   `static devclass_t` variable; the current `DRIVER_MODULE`
   signature takes five arguments (name, bus, driver, event
   handler, event handler argument) and the bus code manages the
   device class for you.

There is a visible rhythm to a well-organised driver file that
experienced FreeBSD readers pick up on immediately. Methods come
before the tables that reference them. Static helpers come near
the methods that use them. The registration macros come last, so
that the file reads as a linear story from dependencies through
functions to registration.

### A Minimal Driver File

For orientation, here is a minimal shape for `mydev.c`. It is not
complete, but it shows the structural elements that a reviewer
will expect to see. You have seen the mechanics of each of these
macros in earlier chapters; here we are focusing on how they
arrange themselves on the page.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Your Name <you@example.com>
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the
 * following conditions are met:
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/mydev/mydev.h>

static int	mydev_probe(device_t dev);
static int	mydev_attach(device_t dev);
static int	mydev_detach(device_t dev);

static int
mydev_probe(device_t dev)
{
	/* match your PCI vendor/device ID here */
	return (ENXIO);
}

static int
mydev_attach(device_t dev)
{
	/* allocate resources, initialise the device */
	return (0);
}

static int
mydev_detach(device_t dev)
{
	/* release resources, quiesce the device */
	return (0);
}

static device_method_t mydev_methods[] = {
	DEVMETHOD(device_probe,		mydev_probe),
	DEVMETHOD(device_attach,	mydev_attach),
	DEVMETHOD(device_detach,	mydev_detach),
	DEVMETHOD_END
};

static driver_t mydev_driver = {
	"mydev",
	mydev_methods,
	sizeof(struct mydev_softc),
};

DRIVER_MODULE(mydev, pci, mydev_driver, 0, 0);
MODULE_VERSION(mydev, 1);
MODULE_DEPEND(mydev, pci, 1, 1, 1);
```

Several things are worth noticing. The copyright header uses the
`/*-` opening marker, which the automatic licence-harvesting
script recognises. The SPDX line names the licence explicitly.
The indentation is tabs, not spaces, as mandated by `style(9)`.
The function declarations are tab-separated, also per `style(9)`.
The `DRIVER_MODULE` and related macros appear at the bottom, in
the order the build system expects. This is the shape a reviewer
will expect to see.

### The Module Makefile

The Makefile for the module is usually tiny. Here is a realistic
example, modelled on the one at `/usr/src/sys/modules/et/Makefile`:

```makefile
.PATH: ${SRCTOP}/sys/dev/mydev

KMOD=	mydev
SRCS=	mydev.c
SRCS+=	bus_if.h device_if.h pci_if.h

.include <bsd.kmod.mk>
```

Several conventions are encoded in this short file.

`SRCTOP` is a build-system variable that points to the top of the
source tree. Using it means the Makefile works regardless of where
in the tree the build is invoked from. Do not hardcode `/usr/src`.

`KMOD` names the module. This is what `kldload` uses. Match it to
the driver name.

`SRCS` lists the source files. The `.c` files are your driver
sources. The `.h` files that look like `bus_if.h` and `pci_if.h`
are not regular headers; they are auto-generated by the build
system from the method definitions in the corresponding `.m`
files. You list them so that the build system knows to generate
them before compiling your driver. Include `device_if.h` because
every driver uses `device_method_t`; include `bus_if.h` if your
driver uses `bus_*` methods; include `pci_if.h` if it is a PCI
driver; and so on.

`bsd.kmod.mk` is the standard kernel-module build infrastructure.
Including it at the end gives you all the build rules you need.

A few additional conventions apply:

- Do not add copyright headers to trivial Makefiles. The tree
  convention is that tiny Makefiles such as this one are treated
  as mechanical files and do not carry licences. Real Makefiles
  with substantial logic do carry copyright headers.
- Do not use GNU `make` features. FreeBSD's base build system
  uses the in-tree BSD make, not GNU make.
- Keep indentation with tabs, not spaces, for rule bodies.

### The Header File

If your driver has a header file for internal declarations, place
it in the same directory as the `.c` file. The convention is to
name the internal header `<driver>.h` and any hardware register
definitions `<driver>reg.h`. Keep the header's scope narrow. It
should declare structures and constants that are used across
multiple `.c` files within the driver or that are needed for
interoperation with closely related subsystems. It should not
leak driver-internal details into the broader kernel namespace.

The header begins with the same copyright header as the `.c`
file, followed by the standard include guard:

```c
#ifndef _DEV_MYDEV_MYDEV_H_
#define _DEV_MYDEV_MYDEV_H_

/* header contents */

#endif /* _DEV_MYDEV_MYDEV_H_ */
```

The include guard name follows the convention of the full path,
in uppercase, with slashes and dots replaced by underscores, and
with a leading and trailing underscore. The convention is
consistent across the tree and a reviewer will spot deviations.

### Following `style(9)`: The Short Summary

The complete FreeBSD coding style is documented in
`/usr/src/share/man/man9/style.9`. You should read that manual
page before submitting a driver, and at least skim it periodically
as your own style matures. Here we will pull out the points that
most commonly trip up first-time contributors.

#### Indentation and Line Width

Indentation uses real tabs, with a tab stop of 8 columns. Second
and subsequent levels of indentation that are not aligned to a tab
stop use 4 spaces of additional indent. Line width is 80 columns;
a few exceptions are allowed when breaking a line would make it
less readable or would break something that gets grepped for, such
as a panic message.

#### Copyright Header Form

The copyright header uses the `/*-` opening marker. This marker
is magic. An automated tool harvests licences from the tree by
looking for multi-line comments that start in column 1 with `/*-`.
Using the marker flags the block as a licence; using a regular
`/*` does not. Immediately after `/*-`, the next significant line
should be `SPDX-License-Identifier:` followed by the SPDX licence
code, such as `BSD-2-Clause`. Then come one or more `Copyright`
lines. Then the licence text.

#### Function Declarations and Definitions

Function return type and storage class go on the line above the
function name. The function name starts in column 1. Arguments
fit on the same line as the name unless that would exceed 80
columns, in which case subsequent arguments are aligned to the
opening parenthesis.

Correct:

```c
static int
mydev_attach(device_t dev)
{
	struct mydev_softc *sc;

	sc = device_get_softc(dev);
	return (0);
}
```

Incorrect, as a reviewer would immediately flag:

```c
static int mydev_attach(device_t dev) {
    struct mydev_softc *sc = device_get_softc(dev);
    return 0;
}
```

The differences look small: the position of the return type, the
position of the opening brace, the use of spaces instead of tabs,
the declaration with initialisation on a single line, and the
missing parentheses around the return value. Every one of those
differences violates `style(9)`. Collectively they make the
function look out of place in the tree. A reviewer will ask you
to fix them, and fixing them after the fact is more work than
writing them correctly the first time.

#### Variable Names and Identifier Conventions

Use lower-case identifiers with underscores rather than camelCase.
`mydev_softc`, not `MydevSoftc` or `mydevSoftc`. Functions follow
the same convention.

Constants and macros are in upper case with underscores:
`MYDEV_REG_CONTROL`, `MYDEV_FLAG_INITIALIZED`.

Global variables are rare in drivers; prefer per-device state in
the softc. When a global is unavoidable, give it a name prefixed
with the driver name to avoid collision with the rest of the
kernel.

#### Return-Value Parentheses

FreeBSD style requires that `return` expressions be parenthesised:
`return (0);` not `return 0;`. This is a convention going back to
the original BSD kernel and is enforced reasonably strictly.

#### Comments

Multi-line comments use the form:

```c
/*
 * This is the opening of a multi-line comment.  Make it real
 * sentences.  Fill the lines to the column 80 mark so the
 * comment reads like a paragraph.
 */
```

Single-line comments can use either the traditional `/* ... */`
or `// ...` form. Be consistent within a file; do not mix styles.

Comments should explain why, not what. `/* iterate over the
array */` is not useful when the reader can see the loop. `/*
the hardware requires a read-back to flush the write before we
proceed */` is useful because it explains a non-obvious
constraint.

#### Error Messages

Use `device_printf(dev, "message\n")` for device-specific log
output. Do not use `printf` directly from a driver if you have
a `device_t` handy; `device_printf` prefixes the message with the
driver and unit number, which is what every reader of kernel logs
expects to see.

Error messages that get grepped for should stay on a single line
even if they exceed 80 columns. The `style(9)` manual page is
explicit about this.

#### Magic Numbers

Do not use magic numbers in the body of the code. Hardware
register offsets, bit masks, and status codes should be named
constants in the `<driver>reg.h` header. This makes the code
readable and also makes it easy to patch the register definitions
when, inevitably, you discover that something was slightly off.

### Using `tools/build/checkstyle9.pl`

The project ships an automated style checker at
`/usr/src/tools/build/checkstyle9.pl`. It is a Perl script that
reads source files and warns about common style violations. It is
not perfect, and some warnings will be false positives or reflect
conventions that the script does not quite get right, but it
catches a large fraction of the easy mistakes.

Run it against your driver before submission:

```sh
/usr/src/tools/build/checkstyle9.pl sys/dev/mydev/mydev.c
```

You will see output like:

```text
mydev.c:23: missing blank line after variable declarations
mydev.c:57: spaces not tabs at start of line
mydev.c:91: return value not parenthesised
```

Fix each warning. Re-run. Repeat until the output is clean.

The `CONTRIBUTING.md` file is explicit about this: "Run
`tools/build/checkstyle9.pl` on your Git branch and eliminate all
errors." Reviewers do not want to be the style checker for you.
Submitting code that has not been run through `checkstyle9.pl`
wastes their time.

### Using `indent(1)` Carefully

FreeBSD also ships `indent(1)`, a C source reformatter. It can
reformat a file to conform to parts of `style(9)` automatically.
It is useful but it is not magical. `indent(1)` handles some style
rules well, such as tab-based indentation and brace placement, but
it handles other rules poorly or not at all, and in some cases it
makes things worse by reformatting comments or function signatures
in ways that violate `style(9)` even though the input was correct.

Treat `indent(1)` as a rough first pass rather than as a
canonical formatter. Run it on a file to get close to conforming,
then read the output carefully and fix anything it got wrong. Do
not run it on existing tree files as part of an unrelated patch;
mixing style changes with functional changes is a review
anti-pattern.

### Commit Messages

A good commit message does two things. First, it tells the reader
at a glance what the commit does. Second, it tells the reader in
more detail why the commit does it. The subject line is the
first; the body is the second.

The subject line conventions in the FreeBSD tree look like this:

```text
subsystem: Short description of the change
```

The `subsystem` prefix tells the reader which part of the tree is
affected. For a driver submission, the prefix is typically the
driver name:

```text
mydev: Add driver for MyDevice FC100
```

The first word after the colon is capitalised, and the description
is a fragment, not a full sentence. The subject line is capped
around 50 characters, with 72 as a hard limit. Look at recent
commits in the tree with `git log --oneline` to see the pattern:

```text
rge: add disable_aspm tunable for PCIe power management
asmc: add automatic voltage/current/power/ambient sensor detection
tcp: use RFC 6191 for connection recycling in TIME-WAIT
pf: include all elements when hashing rules
```

The body of the commit message comes after a blank line. It
explains the change in more detail: what the change does, why it
is needed, what hardware or scenario it affects, and any
considerations a future reader might need to know. Wrap the body
at 72 columns.

A good commit message for a driver submission might look like
this:

```text
mydev: Add driver for FooCorp FC100 sensor board

This driver supports the FooCorp FC100 series of PCI-attached
environmental sensor boards, which expose a simple command and
status interface over a single BAR.  The driver implements
probe/attach/detach following the Newbus conventions, exposes a
character device for userland communication, and supports
sysctl-driven sampling configuration.

The FC100 is documented in the FooCorp Programmer's Reference
Manual version 1.4, which the maintainer has on file.  Tested on
amd64 and arm64 against a hardware sample; no errata were
observed during the test period.

Reviewed by:	someone@FreeBSD.org
MFC after:	2 weeks
```

Several pieces of that message are standard. `Reviewed by:` names
the committer who signed off on the review. `MFC after:` suggests
a period before the commit can be merged from CURRENT back to
STABLE (MFC stands for Merge From Current). You do not fill in
these lines yourself as a contributor; the committer who commits
your patch will add them.

What you do write is the body: the descriptive paragraphs that
explain the change. Write them as if you are writing them for a
future reader who will see the commit in `git log` five years
from now and wonder what it was about. That reader is you,
possibly, or it is someone maintaining your driver after you have
moved on. Make the commit message kind to them.

### Signed-off-by and the Developer Certificate of Origin

For GitHub pull requests in particular, the `CONTRIBUTING.md`
file asks that commits include a `Signed-off-by:` line. This line
certifies the Developer Certificate of Origin at
`https://developercertificate.org/`, which in plain terms is a
statement that you have the right to contribute the code under
the project's licence.

Adding a `Signed-off-by:` is easy:

```sh
git commit -s
```

The `-s` flag adds a line of the form:

```text
Signed-off-by: Your Name <you@example.com>
```

to the commit message. Use the same name and email you use for
the commit author line.

### What a Complete Submission-Ready Tree Looks Like

After all of this, your driver's tree within the FreeBSD source
tree should look roughly like this:

```text
/usr/src/sys/dev/mydev/
	mydev.c
	mydev.h              (optional)
	mydevreg.h           (optional but recommended)

/usr/src/sys/modules/mydev/
	Makefile

/usr/src/share/man/man4/
	mydev.4
```

And you should be able to build the module with:

```sh
cd /usr/src/sys/modules/mydev
make obj
make depend
make
```

And validate the manual page with:

```sh
mandoc -Tlint /usr/src/share/man/man4/mydev.4
```

And run the style checker with:

```sh
/usr/src/tools/build/checkstyle9.pl /usr/src/sys/dev/mydev/mydev.c
```

If all three of these complete without errors, your driver is
mechanically ready for submission. There is still the licensing,
the manual page content, the testing, and the patch generation
to cover, which we will do in the next sections. But the basic
layout is now in place, and a reviewer opening the patch will
find that the file names, the file layouts, the style, and the
build integration match what they expect to see in the tree.

### Common Mistakes in Section-2 Preparation

Before we close this section, let us collect the most common
preparation mistakes that first-time contributors make. Treat
this as a quick self-check before moving on to Section 3.

- Files in the wrong location. The driver lives under
  `/usr/src/sys/dev/<driver>/`, not at the top of `/usr/src/sys/`.
  The module Makefile lives under `/usr/src/sys/modules/<driver>/`.
  The manual page lives under `/usr/src/share/man/man4/`.
- File names that do not match the driver. If the driver is
  `mydev`, the main source file is `mydev.c`, not `main.c` or
  `driver.c`.
- Missing or incorrect copyright header. The header uses `/*-` as
  the opening marker, the SPDX identifier comes first, and the
  licence text matches one of the project's accepted licences.
- Spaces instead of tabs. `style(9)` is explicit about tabs, and
  the style checker will flag space indentation immediately.
- Missing parentheses on `return` expressions. A recurring small
  mistake that the style checker will catch.
- Missing blank line between variable declarations and code.
  Another small convention that the style checker catches.
- Commit message that does not follow the `subsystem: Short
  description` form. The reviewer will ask you to rewrite it.
- Trailing whitespace. The `CONTRIBUTING.md` file explicitly
  flags trailing whitespace as something reviewers dislike.
- Makefile that hardcodes `/usr/src` instead of using `${SRCTOP}`.

Every one of these is an easy fix when you know to look for it.
Every one of them adds one round of back-and-forth to a review
when you do not. The goal of this section was to give you the
knowledge to catch all of them before you submit.

### Wrapping Up Section 2

Getting a driver ready for submission is less about cleverness
than about attention to detail. The file layout, the style, the
copyright header, the Makefile, the commit message: each of them
has a conventional form, and a driver whose files match those
conventions is a driver whose reviewer's first impression is
"this looks right." That first impression is worth more than any
other single factor in determining how many rounds of review the
patch will need.

We have not yet talked about the licence itself in detail, nor
about the manual page, nor about testing. Those are the subjects
of the next three sections. But the mechanical preparation of
the source tree, which is where first-time contributors most
often stumble, is now covered.

Let us turn next to licensing and the legal considerations that
frame every FreeBSD contribution.

## Section 3: Licensing and Legal Considerations

### Why Licensing Matters Up Front

The easiest way to get your driver rejected is to get the licence
wrong. Licensing is not a procedural preference in FreeBSD; it is
a foundation of how the project works. The FreeBSD operating
system ships under a combination of permissive licences that users
can rely on without surprise. A contribution that carries an
incompatible licence, or an unclear licence, or no licence at
all, cannot be accepted into the tree, no matter how excellent the
code is in every other respect.

This is not legal formalism for its own sake. It is a practical
necessity. FreeBSD is used in many environments, including
commercial products that are shipped to millions of users. Those
users rely on the project's licence to understand their
obligations. A single file in the tree that carries a surprise
licence could expose the entire project's downstream users to
obligations they did not sign up for. The project cannot accept
that risk.

For you as a contributor, the practical takeaway is this: get the
licence right up front. It is easier, by a wide margin, than
trying to fix it after the review process has flagged it. This
section walks through what the project accepts, what it does not
accept, and how to structure the copyright header so that your
submission sails through the licensing check.

### What Licences FreeBSD Accepts

The FreeBSD Project prefers, as a default, the two-clause BSD
licence, often written as BSD-2-Clause. This is the permissive
licence that most of FreeBSD itself ships under, and it is the
default recommendation for new code. BSD-2-Clause allows
redistribution in source and binary form, with or without
modification, as long as the copyright notice and the licence
text are preserved. It imposes no requirement on downstream users
to distribute their source, no requirement for compatibility
notices, and no patent grant clause that might complicate
commercial use.

The three-clause BSD licence, BSD-3-Clause, is also accepted. It
adds one clause prohibiting the use of the author's name in
endorsements. Some older FreeBSD code uses this form, and it is
equivalent for most practical purposes.

A few other permissive licences appear in the tree for specific
files contributed under them historically. The MIT-style licences
and the ISC licence both appear in places. The Beerware licence,
a whimsical permissive licence introduced by Poul-Henning Kamp,
also appears in a few files such as `/usr/src/sys/dev/led/led.c`.
These licences are compatible with FreeBSD's overall licensing
scheme and are accepted when they accompany specific code
contributed under them.

For a new driver that you are writing yourself, the right default
is BSD-2-Clause. Unless you have a specific reason to use a
different licence, use BSD-2-Clause. It is the licence your
reviewers will expect, and any deviation from it will trigger a
conversation you probably do not want to have on a first
submission.

### What Licences FreeBSD Does Not Accept

Several licences are not compatible with the FreeBSD source tree
and code under them cannot be merged. The most common ones that
first-time contributors sometimes try to use are:

- The GNU General Public Licence (GPL), in any version. GPL code
  is not compatible with FreeBSD's licensing model because it
  imposes source-distribution obligations on downstream users
  that the rest of the tree does not carry. FreeBSD does include
  some GPL-licensed components in userland, such as the GNU
  Compiler Collection, but these are specific historical cases
  and are not a template for new contributions. Driver code, in
  particular, is not accepted under GPL.
- The Lesser GPL (LGPL). Same reasoning as GPL.
- The Apache Licence, version 2 or otherwise, unless there is
  specific discussion and approval. The Apache Licence includes a
  patent grant clause that interacts in complicated ways with the
  permissive BSD licences. Some Apache-licensed code is accepted
  in specific contexts but it is not the default for new code.
- The MIT licence in its various forms, while technically
  permissive, is not a default choice for FreeBSD. If you have a
  specific reason to use MIT, discuss it with a reviewer before
  submission.
- Anything proprietary. The tree cannot accept code whose licence
  restricts redistribution or modification.
- Code with unclear licensing, including code copied from other
  projects whose licence is not known, code generated by tools
  whose licensing terms are unclear, and code contributed without
  a clear licence declaration.

If you are porting or adapting code from another open-source
project, check the licence of the source project carefully
before you start. Bringing in code from a GPL-licensed project
into your driver, even a small function, contaminates the driver
and makes it unable to enter the FreeBSD tree.

### The Copyright Header in Detail

The copyright header at the top of every source file in the tree
has a specific structure, documented in `style(9)`. Let us walk
through a complete header and examine every part.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Your Name <you@example.com>
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the
 * following conditions are met:
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
```

The opening `/*-` is not a typo. The dash after the star is
significant. An automated script in the tree harvests licence
information from files by looking for multi-line comments that
start in column 1 with the sequence `/*-`. Using `/*-` flags the
block as a licence; using plain `/*` does not. `style(9)` is
explicit: if you want the tree's licence harvester to pick up
your licence correctly, use `/*-` for the opening line.

Immediately after the opening is the SPDX-License-Identifier
line. SPDX is a standardised vocabulary for describing licences
in machine-readable form. The line tells the harvester which
licence the file is under, and it does so in a form that cannot be
misinterpreted. Use `BSD-2-Clause` for a two-clause BSD licence,
`BSD-3-Clause` for a three-clause BSD licence. For other licences,
consult the SPDX identifier list at `https://spdx.org/licenses/`.
Do not invent identifiers.

The copyright line names the year and the copyright holder. Use
your full legal name, or the name of your employer if you are
contributing work done under employment, followed by an email
address that is stable enough to contact you years from now. If
you are contributing as an individual, use your personal email
rather than a throwaway address.

Multiple copyright lines may be present if the file has had
multiple authors. When you add a copyright line, add it at the
bottom of the existing list, not at the top. Do not delete
anyone else's copyright line. The existing attributions are
legally significant.

The licence text itself follows. The text reproduced above is the
standard BSD-2-Clause text. Do not modify it. The wording is
legally specific, and changing it, even in a way that seems
clearer, may make the licence legally distinct from what the
project accepts.

Finally, there is a blank line after the closing `*/`, before the
code begins. This blank line is a convention in the tree and is
called out in `style(9)`. Its purpose is visual.

### Reading Existing Headers to Build Intuition

The best way to internalise the licence header conventions is to
look at real headers in the tree. Open
`/usr/src/sys/dev/null/null.c` and read its header. Open
`/usr/src/sys/dev/led/led.c` and read its header (which is under
the Beerware licence, an unusual but accepted case). Open a
couple of network drivers under `/usr/src/sys/dev/re/` or
`/usr/src/sys/dev/e1000/` and read theirs. Within fifteen minutes
you will have absorbed the pattern.

A few things you will notice:

- Some older files in the tree do not have SPDX identifiers.
  These predate the SPDX convention. For new contributions, use
  SPDX.
- Some older files still have a `$FreeBSD$` tag near the top.
  This was a CVS-era marker that is no longer active since the
  project moved to git. New contributions do not include
  `$FreeBSD$` tags.
- Some files have multiple copyright lines spanning multiple
  contributors across years. This is normal and correct. When you
  add a copyright line to an existing file, append it.
- A few files have non-standard licences (Beerware, MIT-style,
  ISC). These are historical and are accepted on a case-by-case
  basis. Do not use them as templates for new contributions.

### Derivative Works and External Code

If your driver is entirely your own work, the header is
straightforward. If it includes code derived from another project,
the picture is more complicated.

Any code you copy or adapt from another project carries that
project's licence along with it. If the project's licence is
BSD-compatible, you can use the code, but you must preserve the
original copyright notice and make the adaptation visible. If the
licence is not BSD-compatible, such as GPL, you cannot use the
code at all.

The convention in the tree for derivative works is to preserve
the original copyright line and add your own as a separate line:

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 1998 Original Author <original@example.com>
 * Copyright (c) 2026 Your Name <you@example.com>
 *
 * [licence text]
 */
```

If the original licence is BSD-3-Clause and you are contributing
additions under BSD-2-Clause, the file is effectively
BSD-3-Clause overall, because the three-clause requirement
flows through derivative works. Use the more restrictive of the
two licences in the SPDX identifier, or keep separate licensing
at a per-section level if the code is clearly separable. When in
doubt, ask a reviewer.

If the code is taken from a specific external source whose origin
is relevant, mention it in a comment near the relevant function:

```c
/*
 * Adapted from the NetBSD driver at
 * src/sys/dev/foo/foo.c, revision 1.23.
 */
```

This helps reviewers and future maintainers understand the
provenance. It also helps anyone tracking bugs back to their
upstream fix.

### Adapted Code from Vendor Sources

A common scenario for hardware drivers is that the vendor
provides sample code or a reference driver under some licence. If
the vendor's code is under a BSD-compatible licence, you may be
able to use it directly, possibly with adaptation, preserving the
vendor's copyright. Read the vendor's licence carefully. If the
licence is not BSD-compatible, you cannot use the vendor's code
in a driver destined for the tree. You may be able to use the
vendor's documentation as a reference for independently
implementing the driver, but you cannot lift code.

If the vendor provides documentation under a non-disclosure
agreement (NDA), the situation is more delicate still. An NDA
typically prohibits you from disclosing the documentation. It
may not prohibit you from using the documentation to write code,
but the resulting code must be your own work, not a copy of any
code the vendor provided. Be scrupulous about keeping this line
clear. If there is any doubt, do not proceed without legal
advice.

### Code You Did Not Write but Are Submitting

If you are submitting code that someone else wrote, such as a
colleague's contribution, you need their explicit permission and
their copyright line in the header. You cannot contribute code on
someone else's behalf without their knowledge. The
`Signed-off-by:` line that the project asks for is, in part, a
mechanism for tracking this; the Developer Certificate of Origin
that the line certifies includes a statement that you have the
right to contribute the code.

If you are a company employee contributing work done under
employment, your employer typically holds the copyright, not you.
The copyright line should name the employer. Many companies have
internal processes for approving open-source contributions;
follow those before submitting. Some companies prefer to have
their employees sign a contributor licence agreement (CLA) with
the FreeBSD Foundation for clarity; if yours does, coordinate
with your company before submitting.

### Adding Licence Headers to an Existing Driver

If you are retrofitting a driver you have already written but
never prepared for submission, you will need to add the proper
header to each file. The steps are:

1. Decide on the licence. For a new driver, use BSD-2-Clause.
2. Write the SPDX identifier line.
3. Write the copyright line with your name, email, and the year
   of first creation.
4. Paste in the standard BSD-2-Clause licence text.
5. Verify that the opening is `/*-` and the file starts in
   column 1.
6. Verify that there is a single blank line after the closing
   `*/`.
7. Repeat for each file: the `.c` files, the `.h` files, the
   manual page (where the licence appears as `.\" -` style
   comments rather than `/*-` style), and any other files that
   contain substantial content.

For the Makefile, as noted earlier, a licence header is
conventionally omitted for trivial files. The module Makefile
shown in Section 2 is trivial enough that no header is required.

### Validating the Header

There is no single automated tool that validates every aspect of
a FreeBSD copyright header. The `checkstyle9.pl` script catches
some kinds of formatting errors near the header. The licence
harvester in the tree works on the `/*-` marker and the SPDX
line. The most reliable validation, however, is to compare your
header directly against a known-good header from a recent commit
in the tree, such as the header in
`/usr/src/sys/dev/null/null.c` or any recently added driver.

Build a small habit: when you open a new source file, paste in a
known-good header as the first action. This prevents the easy
mistake of forgetting the header entirely and also ensures that
the shape is right from the start.

### Wrapping Up Section 3

Licensing is one of the places where getting it right up front
saves enormous amounts of time. The FreeBSD Project accepts
BSD-2-Clause, BSD-3-Clause, and a few other permissive licences
for historical files. New contributions should default to
BSD-2-Clause. The copyright header uses a specific form, opening
with `/*-`, followed by an SPDX identifier, followed by one or
more copyright lines, followed by the standard licence text. Code
derived from other projects carries its original licence
obligations forward, and derivative work must preserve the
original attributions. Code you did not write yourself requires
the author's permission and attribution.

With the legal side handled, we can turn to the manual page.
Every driver in the tree ships with a manual page, and writing a
good one is one of the places where first-time contributors most
often underestimate the effort involved. Section 4 walks through
the conventions and provides a template you can adapt.

## Section 4: Writing a Manual Page for Your Driver

### Why the Manual Page Matters

The manual page is the user-facing half of your driver. When
someone finds your driver in the tree and wants to know what it
does, they will not read the source code. They will run
`man 4 mydev`. What they see will be, for most of them, the only
documentation they ever have of your driver. If the manual page
is clear, complete, and well-organised, users will understand
what the driver supports, how to use it, and what its limitations
are. If the manual page is missing, sparse, or poorly organised,
users will be confused, they will file bug reports that are
really documentation problems, and they will, reasonably, form a
negative impression of the driver.

From the project's point of view, the manual page is a first-class
artifact of the contribution. A driver without a manual page
cannot be merged. A driver with a poor manual page will be
delayed at review until the manual page is brought up to standard.
You should think of the manual page as part of the driver, not as
an afterthought.

From a practical point of view, writing the manual page is often
a useful discipline in its own right. The act of explaining to a
user what the driver does, what hardware it supports, what
tunables it exposes, and what its known limitations are, forces
you to articulate those things clearly. It is not uncommon for a
well-written manual page to surface questions that the driver's
design had not yet resolved. Writing the manual page is therefore
part of the work of finishing the driver, not a step after the
driver is done.

### Manual Page Sections: A Quick Orientation

The manual pages in FreeBSD are organised into numbered sections.
The sections are:

- Section 1: General user commands.
- Section 2: System calls.
- Section 3: Library calls.
- Section 4: Kernel interfaces (devices, device drivers).
- Section 5: File formats.
- Section 6: Games.
- Section 7: Miscellaneous and conventions.
- Section 8: System administration and privileged commands.
- Section 9: Kernel internals (APIs and subsystems).

Your driver belongs in Section 4. The manual-page file goes under
`/usr/src/share/man/man4/` and is conventionally named
`<driver>.4`, for example `mydev.4`. The `.4` suffix is the
manual-page convention; it marks the file as a section-4 page.

The file itself is written in the mdoc macro language, not in
plain text. Mdoc is a structured macro set that produces
formatted manual pages from a source file that is more or less
human-readable. The project's style for mdoc is documented in
`/usr/src/share/man/man5/style.mdoc.5`; you should read that file
before writing your first manual page, though much of what it
says will make more sense after you have tried to write one.

### The Structure of a Section-4 Manual Page

A section-4 manual page has a well-established structure. The
following sections appear in roughly this order:

1. `NAME`: The driver name and a one-line description.
2. `SYNOPSIS`: How to include the driver in the kernel or load it
   as a module.
3. `DESCRIPTION`: What the driver does, in prose.
4. `HARDWARE`: The list of hardware the driver supports. This
   section is required for section-4 pages and is drawn verbatim
   into the Release Hardware Notes.
5. `LOADER TUNABLES`, `SYSCTL VARIABLES`: If the driver exposes
   tunables, document them here.
6. `FILES`: The device nodes and any configuration files.
7. `EXAMPLES`: Usage examples, when relevant.
8. `DIAGNOSTICS`: Explanations of the driver's log messages.
9. `SEE ALSO`: Cross-references to related manual pages and
   documents.
10. `HISTORY`: When the driver first appeared.
11. `AUTHORS`: The primary author(s) of the driver.
12. `BUGS`: Known issues and limitations.

Not every section is required for every driver. For a simple
driver, `NAME`, `DESCRIPTION`, `HARDWARE`, `SEE ALSO`, and
`HISTORY` are the minimum. For a more complex driver, add the
others as relevant.

### A Minimum Working Manual Page

Here is a complete, working section-4 manual page for a
hypothetical `mydev` driver. Save it as `mydev.4`, run
`mandoc -Tlint` on it, and you will see it pass clean. This is
the kind of manual page you can adapt to your own driver.

```text
.\"-
.\" SPDX-License-Identifier: BSD-2-Clause
.\"
.\" Copyright (c) 2026 Your Name <you@example.com>
.\"
.\" Redistribution and use in source and binary forms, with or
.\" without modification, are permitted provided that the
.\" following conditions are met:
.\" 1. Redistributions of source code must retain the above
.\"    copyright notice, this list of conditions and the following
.\"    disclaimer.
.\" 2. Redistributions in binary form must reproduce the above
.\"    copyright notice, this list of conditions and the following
.\"    disclaimer in the documentation and/or other materials
.\"    provided with the distribution.
.\"
.\" THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY
.\" EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
.\" THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
.\" PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
.\" AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
.\" SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
.\" NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
.\" LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
.\" HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
.\" CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
.\" OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
.\" EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
.\"
.Dd April 20, 2026
.Dt MYDEV 4
.Os
.Sh NAME
.Nm mydev
.Nd driver for FooCorp FC100 sensor boards
.Sh SYNOPSIS
To compile this driver into the kernel,
place the following line in your
kernel configuration file:
.Bd -ragged -offset indent
.Cd "device mydev"
.Ed
.Pp
Alternatively, to load the driver as a
module at boot time, place the following line in
.Xr loader.conf 5 :
.Bd -literal -offset indent
mydev_load="YES"
.Ed
.Sh DESCRIPTION
The
.Nm
driver provides support for FooCorp FC100 series PCI-attached
environmental sensor boards.
It exposes a character device at
.Pa /dev/mydev0
that userland programs can open, read, and write using standard
system calls.
.Pp
Each attached board is enumerated with an integer unit number
beginning at 0.
The driver supports probe, attach, and detach through the
standard Newbus framework.
.Sh HARDWARE
The
.Nm
driver supports the following hardware:
.Pp
.Bl -bullet -compact
.It
FooCorp FC100 rev 1.0
.It
FooCorp FC100 rev 1.1
.It
FooCorp FC200 (compatibility mode)
.El
.Sh FILES
.Bl -tag -width ".Pa /dev/mydev0"
.It Pa /dev/mydev0
First unit of the driver.
.El
.Sh SEE ALSO
.Xr pci 4
.Sh HISTORY
The
.Nm
driver first appeared in
.Fx 15.0 .
.Sh AUTHORS
.An -nosplit
The
.Nm
driver and this manual page were written by
.An Your Name Aq Mt you@example.com .
```

That manual page is a complete, valid section-4 page. It is short
because the hypothetical driver is simple. A more complex driver
would have larger `DESCRIPTION`, `HARDWARE`, and possibly
`LOADER TUNABLES`, `SYSCTL VARIABLES`, `DIAGNOSTICS`, and `BUGS`
sections. But the skeleton is the same.

Let us walk through the parts that are most frequently
misunderstood.

### The Header Block

The header block at the top is a set of comment lines that begin
with `.\"`. These are mdoc comments. They are not rendered in the
manual page output. They exist to carry the copyright header and
any notes to future editors.

The opening marker is `.\"-` with a dash, equivalent to the `/*-`
in C files. The licence harvester recognises it.

The `.Dd` macro sets the document date. It is formatted as month,
day, year with full month name. The project's mdoc style is to
update `.Dd` whenever the content of the manual page changes
meaningfully. Do not bump the date for trivial changes such as
whitespace fixes, but do bump it for anything semantic.

The `.Dt` macro sets the document title. The convention is the
driver name in uppercase, followed by the section number:
`MYDEV 4`.

The `.Os` macro emits the operating system identifier in the
footer. Use it bare; mdoc will fill in the right thing from
build-time macros.

### The NAME Section

The `.Sh NAME` macro opens the NAME section. The content is a
pair of macros:

```text
.Nm mydev
.Nd driver for FooCorp FC100 sensor boards
```

`.Nm` sets the name of the thing being documented. Once set, `.Nm`
with no arguments elsewhere in the page expands to the name,
which is how we avoid repeating the driver name over and over.

`.Nd` is the short description, a sentence-fragment of the form
"driver for ..." or "API for ..." or "device for ...". Do not
capitalise the first word or add a period at the end.

### The SYNOPSIS Section

For a driver, the SYNOPSIS typically shows two things: how to
compile the driver into the kernel as a built-in, and how to
load it as a module. The built-in form uses `.Cd` for a kernel
configuration line. The loadable form shows the `_load="YES"`
entry for `loader.conf`.

If the driver exposes a header file that userland programs must
include, or if it exposes a library-like API, the SYNOPSIS can
also include `.In` for the include directive and `.Ft`/`.Fn` for
function prototypes. See `/usr/src/share/man/man4/led.4` for an
example of a driver manual page whose SYNOPSIS shows function
prototypes.

### The DESCRIPTION Section

The DESCRIPTION section is where you explain, in prose, what the
driver does. Write it for a user who has installed FreeBSD, has
the hardware in front of them, and wants to know what the driver
offers.

Keep the paragraphs focused. Use `.Pp` to separate paragraphs.
Use `.Nm` to refer to the driver, not the driver name typed out.
Use `.Pa` for path names, `.Xr` for cross-references to other
manual pages, `.Ar` for argument names, and `.Va` for variable
names.

Describe the driver's behaviour, its lifecycle (probe, attach,
detach), its device-node structure, and any concepts a user needs
to understand before interacting with it. Do not document
internal implementation details here; the source code is the
right place for those.

### The HARDWARE Section

The HARDWARE section is required for section-4 pages. This is
the section that gets drawn verbatim into the Release Hardware
Notes, which is the document users consult to see whether their
hardware is supported.

Several specific rules apply to this section. These rules are
documented in `/usr/src/share/man/man5/style.mdoc.5`:

- The introductory sentence should be in the form: "The .Nm
  driver supports the following <device class>:" followed by the
  list.
- The list should be a `.Bl -bullet -compact` list with one
  hardware model per `.It` entry.
- Each model should be named with its official commercial name,
  not an internal code name or chip revision.
- The list should include all hardware that is known to work,
  including revisions.
- The list should not include hardware that is known not to
  work; those belong in `BUGS`.

For a brand-new driver, the list may be short. That is fine. For
a driver that has been in the tree for a while and has
accumulated support for many hardware variants, the list grows
over time as each new variant is tested.

### The FILES Section

The FILES section lists the device nodes and configuration files
that the driver uses. Use a `.Bl -tag` list with `.Pa` entries
for the file names. For example:

```text
.Sh FILES
.Bl -tag -width ".Pa /dev/mydev0"
.It Pa /dev/mydev0
First unit of the driver.
.It Pa /dev/mydev1
Second unit of the driver.
.El
```

Keep the `.Bl -tag -width` value wide enough to accommodate the
longest path in the list. If the widths do not match, the list
will render badly.

### The SEE ALSO Section

The SEE ALSO section cross-references related manual pages. It is
written as a list of `.Xr` cross-references, comma-separated,
with the list sorted first by section number and then
alphabetically within a section:

```text
.Sh SEE ALSO
.Xr pci 4 ,
.Xr sysctl 8 ,
.Xr style 9
```

A driver's SEE ALSO typically includes the bus it attaches to
(such as `pci(4)`, `usb(4)`, or `iicbus(4)`), any userland tool
that interacts with it, and any section-9 APIs that are central
to the driver's implementation.

### The HISTORY Section

The HISTORY section says when the driver first appeared. For a
brand-new driver that will first appear in the next release,
write the release version as a placeholder:

```text
.Sh HISTORY
The
.Nm
driver first appeared in
.Fx 15.0 .
```

The committer who commits your patch will verify the version
number against the release schedule and may adjust it. That is
fine.

### The AUTHORS Section

The AUTHORS section names the primary authors of the driver.
Use `.An -nosplit` at the top to tell mdoc not to split the
author list across lines at name boundaries. Then use `.An` for
each author with `.Aq Mt` for the email address.

```text
.Sh AUTHORS
.An -nosplit
The
.Nm
driver was written by
.An Your Name Aq Mt you@example.com .
```

For a driver with multiple authors, list them in order of
contribution, with the primary author first.

### Validating the Manual Page

Once the manual page is written, validate it with `mandoc(1)`:

```sh
mandoc -Tlint /usr/src/share/man/man4/mydev.4
```

`mandoc -Tlint` runs the page through the mandoc parser in strict
mode and reports any structural or semantic problems. Fix every
warning. A clean `mandoc -Tlint` run is a prerequisite for
submission.

You can also render the page to see what it looks like:

```sh
mandoc /usr/src/share/man/man4/mydev.4 | less -R
```

Read the rendered output as a user would. If something reads
awkwardly, fix the source. If a cross-reference renders in a way
you did not expect, check the macro usage. Read the output at
least twice.

The project additionally recommends the `igor(1)` tool, available
from the ports tree as `textproc/igor`. `igor` catches prose-level
issues that `mandoc` does not, such as double spaces, unmatched
quotes, and common prose mistakes. Install it with
`pkg install igor` and run it on your page:

```sh
igor /usr/src/share/man/man4/mydev.4
```

Fix any warnings it produces.

### The One-Sentence-Per-Line Rule

An important convention in FreeBSD mdoc pages is the
one-sentence-per-line rule. Each sentence in the manual page
source starts on a new line, regardless of line width. This is
not about display formatting; mdoc will reflow the text for
display. It is about source readability and about how `diff`
shows changes. When changes are line-oriented, a diff of a manual
page change shows which sentences changed; when sentences span
lines, the diff is harder to read.

The `CONTRIBUTING.md` file is explicit about this:

> Please be sure to observe the one-sentence-per-line rule so
> manual pages properly render. Any semantic changes to the
> manual pages should bump the date.

In practice this means you write:

```text
The driver supports the FC100 family.
It attaches through the standard PCI bus framework.
Each unit exposes a character device under /dev/mydev.
```

Not:

```text
The driver supports the FC100 family. It attaches through the
standard PCI bus framework. Each unit exposes a character device
under /dev/mydev.
```

The first form is conventional, the second is not.

### Common Manual-Page Mistakes

Several mistakes recur in first-time submissions:

- Missing HARDWARE section. Section-4 pages are required to have
  one. If your driver supports no hardware yet (because it is a
  pseudo-device), document that explicitly.
- Period at the end of the NAME description. The `.Nd`
  description should be a fragment without a trailing period.
- Capitalised section headings spelled wrongly. The headings are
  canonical. Use `DESCRIPTION`, not `DESCRIPTIONS`. Use
  `SEE ALSO`, not `See Also`. Use `HISTORY`, not `History`.
- Multi-sentence paragraphs without `.Pp` separation. Use `.Pp`
  between paragraphs in prose.
- Forgetting to bump `.Dd` when making semantic changes. If you
  change the content of the manual page, update the date.
- Using `.Cm` or `.Nm` where `.Ql` (literal quotation) or plain
  text is appropriate.
- Missing or malformed `.Bl`/`.El` pairs. Every list must be
  properly opened and closed.

Running `mandoc -Tlint` catches most of these. Running `igor`
catches a few more. Reading the rendered output catches the
remainder.

### Looking at Real Manual Pages

Before you finalise your own manual page, spend time reading
real ones. Three useful models:

- `/usr/src/share/man/man4/null.4` is a minimal page. Good for
  the basic shape.
- `/usr/src/share/man/man4/led.4` is a slightly more complex
  page that shows SYNOPSIS with function prototypes.
- `/usr/src/share/man/man4/re.4` is a full-featured network
  driver page. Good for seeing HARDWARE, LOADER TUNABLES,
  SYSCTL VARIABLES, DIAGNOSTICS, and BUGS in action.

Read each of them. Open them in `less`, read the rendered
version, then open the source in an editor. Compare the
rendered output to the source. You will see how the macros
produce the formatted text, and you will absorb the conventions
by osmosis.

### Wrapping Up Section 4

The manual page is not an afterthought. It is a first-class
artifact that ships with your driver and is the primary
user-facing documentation. A good manual page has a specific
structure (NAME, SYNOPSIS, DESCRIPTION, HARDWARE, and so on), is
written in mdoc, follows the one-sentence-per-line rule, and
passes `mandoc -Tlint` clean. The page deserves the same care
as the code. A driver with a poor manual page will be held at
review until the page is brought up to standard; a driver with a
good one will clear that part of the review easily.

With the licence and the manual page in hand, you have all the
paperwork of a driver submission covered. The next section turns
to the technical side of testing, because a driver that compiles
cleanly and passes style checks still needs to build on every
supported architecture and behave correctly in a variety of
situations. Section 5 walks through those tests.

## Section 5: Testing Your Driver Before Submission

### The Tests That Matter

Testing a driver before submission is not a single action. It is a
sequence of verifications, each of which checks a different
property. A driver that passes all of them is a driver the
reviewers can focus on in terms of design and intent, rather than
in terms of preventable mechanical problems. A driver that skips
some of them is a driver that will have avoidable problems
surfaced during review, each of which adds a round to the review
cycle.

The tests fall into several categories:

1. Code-style tests, which verify that the source conforms to
   `style(9)`.
2. Manual-page tests, which verify that the mdoc source is
   syntactically valid and renders cleanly.
3. Local build tests, which verify that the driver builds as a
   loadable kernel module against the current FreeBSD source
   tree.
4. Runtime tests, which verify that the driver loads, attaches
   to its device, handles a basic workload, and detaches
   cleanly.
5. Cross-architecture build tests, which verify that the driver
   compiles on every architecture the project supports.
6. Lint and static-analysis tests, which catch bugs that the
   compiler does not flag but that are visible to more
   aggressive tooling.

Each category has its own tools and its own workflow. This
section walks through them in order.

### Code-Style Tests

We have already seen `tools/build/checkstyle9.pl` in Section 2.
Here we will elaborate on its use.

The script lives at
`/usr/src/tools/build/checkstyle9.pl`. It is a Perl program, so
you invoke it as a script with Perl:

```sh
perl /usr/src/tools/build/checkstyle9.pl /usr/src/sys/dev/mydev/mydev.c
```

Or, if the script is executable and Perl is in its shebang line,
simply:

```sh
/usr/src/tools/build/checkstyle9.pl /usr/src/sys/dev/mydev/mydev.c
```

The output is a list of warnings with line numbers. Typical
warnings include:

- "space(s) before tab"
- "missing blank line after variable declarations"
- "unused variable"
- "return statement without parentheses"
- "function name is not followed by a newline"

Each warning maps to a specific rule in `style(9)`. Fix each one.
Re-run. Repeat until the output is clean.

If you find yourself disagreeing with a warning, check `style(9)`
first. It is possible for the script to produce false positives,
but those are rare. Most of the time, a disagreement with the
style checker is a misunderstanding of `style(9)`. Read the
manual page before arguing.

Run `checkstyle9.pl` on every `.c` and `.h` file in your driver.
The Makefile does not need to pass it since it is not C code.

### Manual-Page Tests

For the manual page, the canonical test is `mandoc -Tlint`:

```sh
mandoc -Tlint /usr/src/share/man/man4/mydev.4
```

Fix every warning. Re-run. Repeat until the output is clean.

In addition, run `igor` if you have it installed:

```sh
igor /usr/src/share/man/man4/mydev.4
```

And render the page to read it as a user would:

```sh
mandoc /usr/src/share/man/man4/mydev.4 | less -R
```

You can also install your page into the system for a realistic
test:

```sh
cp /usr/src/share/man/man4/mydev.4 /usr/share/man/man4/
makewhatis /usr/share/man
man 4 mydev
```

This last check is useful because it verifies that `man` can find
the page, that `apropos` can find it via `whatis`, and that the
page renders correctly in the standard pager.

### Local Build Tests

Before doing anything else with the driver, verify that it
builds. From the module directory:

```sh
cd /usr/src/sys/modules/mydev
make clean
make obj
make depend
make
```

The output should be a single `mydev.ko` file in the module's
object directory. No warnings, no errors. If you see warnings,
fix them. `style(9)` calls out that warnings should not be
ignored; submissions that introduce warnings are held for review.

If you are running on the same machine where you will load the
module, install it:

```sh
sudo make install
```

This copies `mydev.ko` to `/boot/modules/` so that `kldload` can
find it.

### Runtime Tests

Once the module is built and installed, test it:

```sh
sudo kldload mydev
dmesg | tail
```

The `dmesg` output should show your driver probing, attaching to
any available hardware, and completing attach without error. If
there is no matching hardware, the driver should simply not
attach, which is fine for the loading test.

Exercise the driver as a user would. Open its device nodes, read
and write to them, run the operations your driver supports, and
watch for any diagnostic messages. Run it under load. Run it with
multiple simultaneous openers. Run it with edge-case inputs.
These are the kinds of tests that catch bugs the compiler cannot
see.

Then unload:

```sh
sudo kldunload mydev
dmesg | tail
```

The unload should complete silently, with no "device busy" errors
and no panics. If the unload produces a warning about busy
resources, the driver's detach path has a leak; fix it before
submission.

Repeat the load/unload cycle several times. A driver that loads
and unloads once is not the same as a driver that loads and
unloads repeatedly. Bugs in the detach path often show up only on
the second or third unload, when the state left over from the
first unload interferes with the second load.

### Cross-Architecture Build Tests

FreeBSD supports several architectures. The active ones as of
FreeBSD 14.3 include:

- `amd64` (64-bit x86).
- `arm64` (64-bit ARM, also called aarch64).
- `i386` (32-bit x86).
- `powerpc64` and `powerpc64le` (POWER).
- `riscv64` (64-bit RISC-V).
- `armv7` (32-bit ARM).

A driver that builds on `amd64` may or may not build on all of
the others. Common cross-architecture problems include:

- Integer-size assumptions. A `long` is 64 bits on `amd64` and
  `arm64` but 32 bits on `i386` and `armv7`. If your code assumes
  `sizeof(long) == 8`, it will break on 32-bit architectures. Use
  `int64_t`, `uint64_t`, or similar fixed-size types when the size
  matters.
- Pointer-size assumptions. Similarly, pointers are 64 bits on
  `amd64` and 32 bits on `i386`. Casting between pointers and
  integers requires `intptr_t`/`uintptr_t`.
- Endianness. Some architectures are little-endian, some are
  big-endian, some are configurable. If your driver reads or
  writes network-byte-order data, use the explicit byte-swap
  macros (`htonl`, `htons`, `bswap_32`, and friends), not
  hand-rolled conversions.
- Alignment. Some architectures enforce strict alignment on
  multi-byte loads. Use `memcpy` or the `bus_space(9)` API rather
  than direct casts when accessing hardware registers.
- Bus abstractions. The `bus_space(9)` API abstracts hardware
  access correctly across architectures; using inline `volatile
  *` casts does not.

The best way to catch cross-architecture problems is to build the
driver for each architecture. Fortunately, FreeBSD has a build
target that does exactly that:

```sh
cd /usr/src
make universe
```

`make universe` builds the world and the kernel for every
supported architecture. The full build can take an hour or more
depending on the machine, so it is not something you run on every
change, but it is the canonical pre-submission test. The
`Makefile` in `/usr/src/` describes it:

> `universe` - `Really` build everything (buildworld and all
> kernels on all architectures).

If you do not want to build everything, you can build just a
single architecture:

```sh
cd /usr/src
make TARGET=arm64 buildkernel KERNCONF=GENERIC
```

This is faster and is often enough to catch the typical
cross-architecture issues.

For just your module, you can sometimes cross-build with:

```sh
cd /usr/src
make buildenv TARGET_ARCH=aarch64
cd sys/modules/mydev
make
```

But `make universe` and `make buildkernel TARGET=...` are the
canonical tests, and any serious submission should pass them.

### tinderbox: The Failure-Tracking Variant of universe

A variant of `make universe` is `make tinderbox`:

```sh
cd /usr/src
make tinderbox
```

Tinderbox is the same as universe, but at the end it reports the
list of architectures that failed and exits with an error if any
did. For a submission workflow, this is often more useful than
plain `universe`, because the failure list is a clear action
item.

### Running Kernel Lint Tools

FreeBSD's kernel build optionally runs additional checks. The
`LINT` kernel configuration is a kernel built with every driver
and option turned on, which surfaces cross-cutting problems that
single-feature kernels miss. Building the LINT kernel is not
usually required for a driver submission, but it is a useful
sanity check if you are touching anything widely used.

`clang` itself, as FreeBSD's default compiler, performs
sophisticated static analysis during normal compilation. Build
with `WARNS=6` to see the most aggressive warning set:

```sh
cd /usr/src/sys/modules/mydev
make WARNS=6
```

And fix any warnings that appear. Clang also has a scan-build
tool that runs static analysis as a separate pass:

```sh
scan-build make
```

Install it from the ports tree (`devel/llvm`) if it is not
available.

### Testing in a Virtual Machine

Much of this chapter assumes you are testing on a real machine or
a virtual machine. Virtual machines are particularly useful for
driver testing because a panic costs nothing more than a reboot.
Two common approaches:

- bhyve, FreeBSD's native hypervisor. A FreeBSD guest under
  bhyve can be a good testing environment, particularly for
  network drivers using `virtio`.
- QEMU. QEMU can emulate architectures other than the host, which
  makes it useful for testing cross-architecture builds at
  runtime without needing physical hardware in each architecture.

For cross-architecture runtime testing, QEMU with a FreeBSD image
in the target architecture is a good workflow. Build the module
for the target architecture, copy it into the QEMU VM, and run
`kldload` there. Crashes inside the VM do not affect your host.

### Testing Against HEAD

The FreeBSD source tree's `main` branch is sometimes called HEAD,
in the release-engineering sense. Your driver should build and
run against HEAD, because that is where your patch will first
land. If you have been developing against an older branch, update
to HEAD before final testing:

```sh
cd /usr/src
git pull
```

Then rebuild and retest. Kernel APIs change; a driver that built
against a six-month-old tree may need small adjustments to build
against current HEAD.

### A Shell Script for the Whole Pipeline

For a serious submission, consider putting the test sequence into
a shell script. The companion examples include one, but the
skeleton is straightforward:

```sh
#!/bin/sh
# pre-submission-test.sh
set -e

SRC=/usr/src/sys/dev/mydev
MOD=/usr/src/sys/modules/mydev
MAN=/usr/src/share/man/man4/mydev.4

echo "--- style check ---"
perl /usr/src/tools/build/checkstyle9.pl "$SRC"/*.c "$SRC"/*.h

echo "--- mandoc lint ---"
mandoc -Tlint "$MAN"

echo "--- local build ---"
(cd "$MOD" && make clean && make obj && make depend && make)

echo "--- load/unload cycle ---"
sudo kldload "$MOD"/mydev.ko
sudo kldunload mydev

echo "--- cross-architecture build (arm64) ---"
(cd /usr/src && make TARGET=arm64 buildkernel KERNCONF=GENERIC)

echo "all tests passed"
```

Run this script before every submission. If it exits clean, your
driver has cleared all the mechanical tests. Reviewers can then
focus on the design.

### What Testing Does Not Catch

Testing tells you that your driver compiles and that it works in
the situations you tested. It does not tell you that it works in
all situations. A driver that passes every test may still have
bugs that surface only under rare workloads, on rare hardware, or
in rare interleavings of kernel state.

This is normal. Software is never fully tested. The role of
pre-submission testing is not to prove the driver correct, but to
catch the mistakes that are easy to catch. Design-level bugs,
rare race conditions, and subtle protocol violations will still
find their way into the tree and will be caught, sometimes much
later, by users who encounter them. That is what post-merge
maintenance is for, and we will cover it in Section 8.

### Wrapping Up Section 5

Testing is a multi-stage verification process. Style checks,
manual-page lints, local builds, runtime load/unload cycles,
cross-architecture builds, and static analysis each test a
different property. A driver that passes all of them is a driver
in shape for review. The tools are standard: `checkstyle9.pl`,
`mandoc -Tlint`, `make`, `make universe`, `make tinderbox`, and
clang's built-in analysis. The discipline is to run them in
order, fix every warning they produce, and not submit until they
all pass clean.

With the driver tested, we can now turn to the mechanics of
actually submitting it for review. Section 6 walks through patch
generation and the submission workflow.

## Section 6: Submitting a Patch for Review

### What a Patch Is, in the FreeBSD Sense

A patch, in the FreeBSD sense, is a reviewable unit of change.
It can be a single commit or a series of commits. It represents
one logical change to the tree. For a new driver submission, the
patch is typically one or two commits that introduce the new
driver files, the new module Makefile, and the new manual page.

The mechanical form of a patch is a text representation of the
changes, usually in unified-diff format. There are several ways
to generate such a representation:

- `git diff` produces a diff between two commits or between a
  commit and the working tree.
- `git format-patch` produces a patch file per commit, with the
  full commit metadata included, in a form suitable for email or
  for attachment to a review.
- `arc diff`, from the Phabricator command-line tools, posts the
  current working state as a Phabricator revision.
- `gh pr create`, from the GitHub command-line tools, opens a
  GitHub pull request.

The right tool depends on where you are sending the patch. For
Phabricator, `arc diff` is standard. For GitHub, `gh pr create`
or the GitHub web UI is standard. For mailing lists, `git
format-patch` with `git send-email` is standard.

All of them rely on the same underlying git commit. Before you
worry about the submission tool, make sure the commit itself is
clean.

### Preparing the Commit

Start from a clean, up-to-date clone of the FreeBSD source tree:

```sh
git clone https://git.FreeBSD.org/src.git /usr/src
```

Or, if you already have a clone, update it:

```sh
cd /usr/src
git fetch origin
git checkout main
git pull
```

Create a topic branch for your work:

```sh
git checkout -b mydev-driver
```

Make your changes: add the driver files, the module Makefile,
and the manual page. Run all the tests from Section 5. Fix any
issues.

Commit your changes. The commit should be a single logical unit
of change. If you are introducing a brand-new driver, one commit
is usually appropriate: "mydev: Add driver for FooCorp FC100
sensor boards." The commit message should follow the conventions
from Section 2.

```sh
git add sys/dev/mydev/ sys/modules/mydev/ share/man/man4/mydev.4
git commit -s
```

The `-s` adds a `Signed-off-by:` line. The editor opens for the
commit message; fill in the subject line and body following the
conventions from Section 2.

Review the commit:

```sh
git show HEAD
```

Read every line. Check that no unrelated files are included. Check
that no trailing whitespace is present. Check that the commit
message reads well. If anything is off, amend:

```sh
git commit --amend
```

Repeat until the commit is exactly as you want it.

### Generating a Patch for Review

Once the commit is clean, generate the patch. For a Phabricator
review, `arc diff`:

```sh
cd /usr/src
arc diff main
```

`arc` will detect that you are on a topic branch, generate the
diff, and open a Phabricator review in your browser. Fill in the
summary, tag reviewers if you know any, and submit.

For a GitHub pull request, push your branch and use `gh`:

```sh
git push origin mydev-driver
gh pr create --base main --head mydev-driver
```

Or open the pull request through the GitHub web UI. The pull
request form asks for a title (use the commit subject line) and
a body (use the commit body). The title and body form the
description of the pull request; they should match what the
committed change will eventually carry.

For a mailing-list submission or an email to a maintainer:

```sh
git format-patch -1 HEAD
```

This produces a file like `0001-mydev-Add-driver-for-FooCorp-FC100-sensor-boards.patch`
containing the commit. You can attach it to an email or send it
inline with `git send-email`. Mailing-list submissions are less
common today than Phabricator or GitHub but are still accepted.

### Which Route to Take

The `CONTRIBUTING.md` file gives specific guidance on which route
to use when. The short version:

- GitHub pull requests are preferred when the change is small
  (fewer than about 10 files and 200 lines), self-contained,
  passes CI cleanly, and needs little developer time to land.
- Phabricator is preferred for larger changes, for work that
  needs extended review, and for subsystems where the
  maintainers prefer Phabricator.
- Bugzilla is appropriate when the patch fixes a specific
  reported bug.
- Direct email to a committer is appropriate when you know the
  maintainer of the subsystem and the change is small enough to
  handle informally.

A new driver is usually somewhere between the GitHub size limit
and the Phabricator fit. If your driver has fewer than 10 files
and fewer than 200 lines, a GitHub PR will work. If it is larger,
try Phabricator first.

Whichever you choose, make sure the driver has been through all
the pre-submission tests. The CI that runs on GitHub and the
review process on Phabricator will both catch problems, but it
saves everyone time if you have caught them first.

### Writing a Good Review Description

The patch itself is only half of what you submit. The other half
is the description: the text that accompanies the patch and
explains what it does, why it is needed, and how it was tested.

On Phabricator, the description is the Summary field. On GitHub,
it is the PR body. On a mailing list, it is the body of the
email.

A good description has three parts:

1. A one-paragraph summary of what the patch does.
2. A discussion of the design and any interesting decisions.
3. A list of what was tested.

For a driver submission, a typical description might read:

> This patch adds a driver for FooCorp FC100 environmental
> sensor boards. The boards are PCI-attached and expose a simple
> command-and-status interface over a single BAR. The driver
> implements probe/attach/detach following Newbus conventions,
> exposes a character device for userland interaction, and
> documents tunable sampling intervals via sysctl.
>
> The FC100 is documented in FooCorp's Programmer's Reference
> Manual version 1.4. The driver supports revisions 1.0 and 1.1
> of the board, and operates the FC200 in its FC100-compatibility
> mode.
>
> Tested on amd64 and arm64 with a physical FC100 rev 1.1 board.
> Passes `make universe`, `mandoc -Tlint`, and
> `checkstyle9.pl`. Load/unload cycle verified 50 times without
> leaks.
>
> Reviewer suggestions welcome on the sysctl structure.

This description does several things right. It explains the
driver in a way a reviewer who has never seen it can understand.
It establishes what has been tested. It explicitly invites
feedback on a specific design question. It reads as a
collaborative request for review, not as a "here is my code,
merge it" demand.

### The Draft Email for the Mailing List

Even if you plan to submit via Phabricator or GitHub, a draft
email to one of the FreeBSD mailing lists can be a useful
introduction. The general-purpose list for development questions
is `freebsd-hackers@FreeBSD.org`; there are also subsystem lists
such as `freebsd-net@` for network drivers and `freebsd-scsi@`
for storage. Pick the list that most closely matches the
subsystem your driver lives in, and if in doubt, start with
`freebsd-hackers@`.

A draft email might look like this:

```text
To: freebsd-hackers@FreeBSD.org
Subject: New driver: FooCorp FC100 sensor boards

Hello,

I am working on a driver for FooCorp FC100 PCI-attached
environmental sensor boards. The boards are documented, I have
two hardware samples to test against, and the driver is in a
state that passes mandoc -Tlint, checkstyle9.pl, and make
universe clean.

Before I open a review, I wanted to ask the list if anyone has:

* Experience with similar sensor boards that might inform the
  sysctl structure.
* Strong preferences about whether the driver should expose a
  character device or a sysctl tree as the primary interface.
* Comments on the draft manual page (attached).

The code is available at https://github.com/<me>/<branch> for
anyone who wants to take an early look.

Thanks,
Your Name <you@example.com>
```

This is the kind of email that tends to generate helpful
responses. It shows that the work is serious, it asks specific
questions, and it offers a way to see the code. Many successful
FreeBSD submissions start with an email like this.

For this book, you do not need to actually send such an email.
The companion examples include a draft you can use as a
template. The exercise in Section 4 of this chapter includes
writing your own draft.

### What Happens After You Submit

Once you submit, the review process begins. The exact flow
depends on the submission route, but the general pattern is
similar across routes.

For a Phabricator review:

- The revision appears in Phabricator's queue. It is
  automatically subscribed to any mailing lists relevant to the
  subsystem.
- Reviewers may pick up the review from the queue, or you may
  tag specific reviewers you think are relevant.
- Reviewers leave comments on specific lines, general comments,
  and requested changes.
- You address the comments by updating the commit and running
  `arc diff --update` to refresh the revision.
- The review cycle repeats until the reviewers are satisfied.
- A committer eventually lands the patch, credited to your name
  as the author.

For a GitHub pull request:

- The PR appears in the GitHub queue for `freebsd/freebsd-src`.
- CI jobs run automatically; they must pass.
- Reviewers comment on the PR, leave line comments, or request
  changes.
- You address the comments by committing fixups to your branch.
  Eventually, you squash the fixups into the original commit.
- When a reviewer is ready, they will either merge the PR
  themselves (if they are a committer) or shepherd it to
  Phabricator for a deeper review.
- The merged commit retains your authorship.

For a mailing-list submission:

- Mailing list readers respond with feedback.
- You iterate based on the feedback and send updated versions
  as replies to the original thread.
- When a committer is ready, they will commit the patch, credited
  to you.

In all cases, the iteration is part of the process. A patch that
enters the tree in exactly the form it was first submitted is
rare. Expect at least one round of feedback, often several. Each
round is the reviewers helping you polish the submission.

### Response Time and Patience

Reviewers are volunteers, even those who are paid by employers
to work on FreeBSD. Their time is finite. Response time for a
review can range from hours (for a small, well-prepared patch
that matches the reviewer's current interests) to weeks (for a
large, complex patch that needs careful reading).

If your patch has not received a response within a reasonable
time, it is acceptable to send a polite ping. The usual
conventions:

- Wait at least a week before pinging.
- Keep the ping short: "Just a friendly ping on this review, in
  case it slipped off anyone's radar." Nothing more.
- Do not ping more than once per week. If a patch is not
  getting attention after multiple pings, the issue is probably
  not that the reviewers have forgotten; it may be that the
  patch needs more work, or that the reviewers who know the
  subsystem are busy with other things.
- Consider asking on the relevant mailing list for review
  attention. A public ask is sometimes more effective than
  private pings.

Do not respond to review silence with anger or pressure. The
project is run by volunteers. A review that takes longer than
you hoped is not a personal insult.

### Iteration and Patch Updates

Every round of review will have comments you need to address.
Some will be small (rename a variable, add a comment, fix a
typo in the manual page). Some will be larger (rework a
function, change an interface, add a test).

Address each comment. If you disagree with a comment, reply
explaining your reasoning; do not just ignore it. Reviewers are
open to being convinced, but only if you make a case.

When you update the patch, keep the commit history clean. If
you pushed a "fixup" commit earlier, squash it into the original
commit before the final submission. The tree commits should each
be logically complete; they should not contain messy incremental
steps.

The workflow for a GitHub PR update typically looks like:

```sh
# make the fixes
git add -p
git commit --amend
git push --force-with-lease
```

For a Phabricator update:

```sh
# make the fixes
git add -p
git commit --amend
arc diff --update
```

Always use `--force-with-lease` rather than `--force` when
force-pushing. `--force-with-lease` refuses to push if the
remote has moved in a way you did not know about, which prevents
accidentally overwriting a reviewer's changes.

### Common Mistakes in Submission

A few common submission-time mistakes:

- Submitting a draft. Polish the patch first. Submitting a patch
  you know is not ready wastes the reviewers' time.
- Submitting against an outdated tree. Rebase against current
  HEAD before submission.
- Including unrelated changes. Each submission should be one
  logical change. Style cleanups, unrelated bug fixes, and
  random improvements should be separate submissions.
- Not responding to feedback. A patch that stalls in review
  because the author never replied is a patch that will die.
- Pushing back defensively. Reviewers are trying to help.
  Responding defensively to feedback is a quick way to sour the
  relationship.
- Submitting the same patch to multiple routes simultaneously.
  Pick one route. If you submit to Phabricator, do not also
  open a GitHub PR with the same content.

### Wrapping Up Section 6

Submitting a patch for review is a mechanical process once the
patch is in shape. The patch is a commit (or series of commits)
with a proper message, against a current tree. The submission
route depends on the size and nature of the change: small
self-contained changes go to GitHub PRs, larger or deeper
changes go to Phabricator, specific bug fixes can attach to
Bugzilla entries. The accompanying description explains the
patch and invites review. What follows is an iterative review
cycle that ends with a committer landing the patch.

The next section looks at the human side of that iteration: how
to work with a mentor or committer, how to handle feedback, and
how to turn a first submission into the beginning of a
longer-term relationship with the project.

## Section 7: Working With a Mentor or Committer

### Why the Human Side Matters

The submission process is ultimately a collaboration with
people, not with a platform. The patch you submit is reviewed by
engineers who have their own contexts, their own workloads, and
their own experiences with what makes a driver easy or hard to
review. The success or failure of your submission depends as
much on how you engage with these people as on the technical
quality of the code.

That framing bothers some first-time contributors, who would
prefer that the technical work stand alone. The preference is
understandable, but it does not match reality. FreeBSD is a
community project, not a code-submission service. Reviewers
offer their time because they care about the project and because
they enjoy helping other contributors succeed. When that care is
reciprocated, the experience is good for everyone. When it is
not, the experience can be frustrating even for patches that are
technically good.

This section walks through the human side of the contribution
process. Some of it will feel obvious. Much of it is rarely
discussed explicitly, which is why first-time contributors
sometimes stumble even when their code is solid.

### The Role of a Mentor

A mentor, in the FreeBSD context, is a committer who has agreed
to guide a specific new contributor through their first
submissions. Not every contribution involves a mentor; many
first submissions land through ordinary review without a formal
mentorship. But when a mentor is involved, the relationship has
a specific shape.

A mentor typically does these things:

- Reviews your patches in detail, often before they go to wider
  review.
- Helps you understand the project's conventions and the
  specific subsystem you are working in.
- Sponsors commits on your behalf, meaning they commit the patch
  to the tree crediting you as the author.
- Answers questions about project process, style, and social
  norms.
- Vouches for you in nomination discussions if, later, commit
  rights become appropriate.

A mentor is not doing your work. They are accelerating your
integration into the project. A good mentor is patient, willing
to explain, and willing to push back when you are going in a
wrong direction. A good mentee is diligent, willing to listen,
and willing to do the work of iterating.

Finding a mentor is often organic. It happens because you
engaged productively with a specific committer over several
rounds of review, and they offered to take on a more structured
mentorship role. It is rarely because you asked cold. If you are
interested in mentorship, the right move is to start
contributing visibly and productively, and to let the
relationship develop.

The FreeBSD Project also has more formal mentorship programmes
at various times, including for specific demographic groups or
specific subsystems. These programmes are the right place to
look for a mentor if you want a structured starting point.

### Sponsorship: The Commit Pathway

A sponsor is a committer who commits a patch on behalf of a
contributor. Every contribution from a non-committer goes through
a sponsor at commit time. The sponsor is not necessarily the same
person as the primary reviewer, and not necessarily a mentor,
though they can be both.

Finding a sponsor for a patch is usually straightforward. If the
patch has been through review and at least one committer has
approved it, that committer is usually willing to sponsor the
commit. You do not need to ask formally; the commit will happen
when the reviewer is ready.

If the patch has been reviewed but no one has moved to commit
it, a polite question is appropriate: "Is anyone in a position
to sponsor the commit of this patch?" Asking in the review
thread or on the relevant mailing list will usually find someone.

Do not confuse sponsorship with support in the abstract. A
sponsor is specifically the person who runs the `git push` that
lands your patch. They take on a small amount of responsibility:
their name appears in the commit metadata, and they are
implicitly certifying that the patch was ready to land.

### Receiving Feedback Gracefully

Feedback on your patch can be hard to read, especially the first
time. Reviewers write in the mode of reviewing code, which means
they name specific things that need to change. That mode reads
as negative even when the underlying assessment of the patch is
overwhelmingly positive. A review that says "this is a great
start, but here are twenty things to fix" is normal for a first
submission.

The right response to feedback is to address it. For each
comment:

- Read it carefully. Make sure you understand what the reviewer
  is asking.
- If the comment is clear and actionable, make the change. Do
  not argue just because the suggestion was not your first
  choice.
- If the comment is unclear, ask for clarification. "Can you
  say more about what you mean by X?" is a perfectly fine
  response.
- If you disagree with the comment, reply explaining your
  reasoning. Be specific: "I thought about using X but went with
  Y because Z." Reviewers are open to being persuaded.
- If the comment is outside the scope of the patch, say so and
  propose handling it separately. "Good catch, but this is
  really a separate change; I will send it as a follow-up."

Never respond with hostility. Even if you believe the reviewer
is wrong, respond calmly and with reasoning. A review thread
that descends into anger is one that the reviewer will
disengage from, and your patch will stall.

Several specific responses to avoid:

- "The code already works." The code working is not the question.
  The question is whether it matches the conventions and design
  expectations of the tree.
- "This is just style; the code is fine." Style is part of
  engineering quality. Reviewers are not wasting your time when
  they ask about style.
- "Other drivers in the tree do it this way." They may well, and
  the tree has plenty of older drivers that do not match modern
  conventions. The goal for new contributions is to match modern
  conventions, not to reproduce historical drift.
- "I will do that later." If you say you will do it later, the
  reviewer has no way to verify that you will. Do it now, or
  discuss why a later fix is appropriate.

The review process is cooperative. The reviewer is not your
adversary. Every comment, even one you disagree with, is the
reviewer investing time in your patch. Respond to that
investment with your own.

### Iteration and Patience

Patch review is iterative by design. A typical first driver
submission goes through three to five rounds of review before it
lands. Each round takes days to weeks, depending on reviewer
availability and the size of the changes requested.

The total elapsed time from first submission to merge, for a
new driver, is often several weeks. Sometimes it is months.
That is normal. FreeBSD is a careful project; careful review
takes time.

Several habits that help with iteration:

- Respond quickly. The faster you respond to feedback, the
  faster the review proceeds. Delays on your side are as
  damaging to the timeline as delays on the reviewer's side.
- Batch small fixes. If the reviewer leaves ten comments, make
  all ten fixes in a single update rather than sending ten
  individual updates. Reviewers prefer to see the work
  integrated.
- Keep the commit clean. As you iterate, amend the original
  commit rather than stacking fixup commits. The commit that
  eventually lands should be a single clean commit, not a
  messy history.
- Test before re-submitting. Each iteration should pass the
  same pre-submission tests as the first submission. Do not
  break tests between rounds.
- Summarise each iteration. When you update the patch, a short
  reply on the review saying "updated to address all comments;
  specifically: did X, did Y, clarified Z" helps reviewers
  re-orient quickly.

Above all, be patient. The review process exists because code
quality matters. Hurrying through it compromises the quality
and produces a patch that lands quickly but creates problems
later.

### Handling Disagreements

Occasionally, reviewers will leave feedback you genuinely
disagree with. The comment is not unclear; you have thought
about it, and you believe the reviewer is wrong. What do you do?

First, consider that you might be wrong. Most of the time, when
a reviewer raises a concern, there is something behind it that
you may not be seeing. The reviewer has context about the tree,
the subsystem, and the history that you may not have. Assume
the concern is legitimate until you have evidence otherwise.

Second, if after thought you still disagree, reply with
reasoning. Explain your perspective. Cite specific details from
the code, the datasheet, or the tree. Ask the reviewer to engage
with your reasoning.

Third, if the disagreement persists, escalate gently. Ask for a
second opinion from another committer. Post to the relevant
mailing list describing the question. Sometimes disagreements
reveal that there are multiple defensible answers and that the
project has not reached a clear position; that is useful
information to surface.

Fourth, if the disagreement still persists and nothing is
resolving it, you have a choice. You can make the change the
reviewer requested, even though you disagree, and land the
patch. Or you can withdraw the patch. Both are legitimate. The
project's culture is not one of forcing contributors to do
things they disagree with, but neither is it one of
rubber-stamping patches the committer community has concerns
about. If the disagreement is fundamental, withdrawing is
sometimes the right outcome.

Disagreements of this depth are rare. Most feedback is
practical and either clearly right or clearly accommodatable.
The serious disagreements, when they happen, are usually about
design choices where multiple answers are defensible.

### Building a Long-Term Relationship

A first submission is not the end of the work. If it goes well,
it can be the beginning of a long-term relationship with the
project. Many committers started as first-time contributors whose
early patches went smoothly, whose later patches built on that
trust, and whose involvement eventually grew to the point where
commit rights made sense.

Building that kind of relationship is not about performing. It
is about continuing to contribute consistently and productively.
Several habits that help:

- Respond to bug reports about your driver. If a user reports a
  bug, triage it, confirm or deny, and follow up. A driver whose
  author is responsive is a driver the project values.
- Review other people's patches. Once you are familiar with a
  subsystem, you can review new patches in that subsystem.
  Reviewing is how you become known as an expert, and how you
  internalise the subsystem's conventions.
- Participate in discussions. The mailing lists and the IRC
  channels have ongoing technical discussions. Participating,
  thoughtfully, is part of being in the community.
- Keep your driver up to date. If kernel APIs change, update
  your driver. If new hardware variants appear, add support. The
  driver is not finished at merge; it is a living artifact you
  are caretaking.

None of this is required. The project is grateful for any
contribution, including one-shot patches from contributors who
never return. But if you are interested in deeper involvement,
these are the ways in.

### Identifying Existing Maintainers

Many subsystems in FreeBSD have identifiable maintainers or
long-time contributors. Finding them is useful because they are
often the best reviewers for related work.

Several ways to identify maintainers:

- `git log --format="%an %ae" <file>` shows who has committed
  changes to a specific file. The names that appear frequently
  are the active maintainers.
- `git blame <file>` shows who wrote each line. If a specific
  function is something you are extending, the person who wrote
  it is often the right person to ask.
- The `MAINTAINERS` file, where it exists, lists formal
  maintainers. FreeBSD does not have a single tree-wide
  MAINTAINERS file, but some subsystems have informal
  equivalents.
- The `AUTHORS` section of the manual page names the primary
  author.

For a driver that extends an existing family, the author of the
existing driver is usually the first reviewer to approach. They
have the context and the authority. For a completely new driver
in a new area, find a reviewer by asking on the relevant mailing
list.

### Exercise: Identify a Similar Driver's Maintainer

Before moving on, pick a driver in the tree that is similar in
scope to the one you are working on. Use `git log` to identify
its maintainer. Note their name and email. Then read a few of
their commits and look at the reviewers they typically engage
with. This gives you a mental model of who the people in the
subsystem are, and makes the human side of the submission feel
more concrete.

You are not expected to contact them unless you have a specific
question. The exercise is about building awareness.

### Wrapping Up Section 7

The human side of the review process is as important as the
technical side. A mentor or committer who is engaged with your
patch is a resource; treating them with respect, responding to
feedback constructively, and iterating patiently are the
practical disciplines of that engagement. Disagreements happen
and are usually productive; defensiveness is the main risk to
avoid. A first submission, handled well, can be the beginning of
a long-term relationship with the project.

The last piece of the submission workflow is what happens after
the patch is in the tree. Section 8 covers the long arc of
maintenance.

## Section 8: Maintaining and Supporting Your Driver Post-Merge

### The Merge Is Not the End

When your patch lands in the FreeBSD tree, a natural feeling is
that the work is done. The driver is in. The review is over. The
commit is in the history. You can move on.

The feeling is understandable, but the picture is incomplete.
Merging the driver into the tree is the beginning of a different
kind of work, not the end of the driver's life. As long as the
driver is in the tree, it requires occasional maintenance. As
long as it is used, it will occasionally surface bugs. As long
as the kernel evolves, its APIs will drift and the driver will
need to follow.

This section walks through the post-merge maintenance picture.
The expectations are not heavy, but they are real. A driver
whose author disappears after the merge is a driver the project
has to maintain on its own, and eventually, if no one takes over,
that can be a reason to mark the driver as deprecated.

### Bugzilla Watching

The FreeBSD Bugzilla at `https://bugs.freebsd.org/` is the
project's primary bug tracker. Bugs filed against your driver
will appear there. You are not required to subscribe to
Bugzilla as a contributor, but you should at least know how to
check for open bugs against your driver.

A simple way to check:

```text
https://bugs.freebsd.org/bugzilla/buglist.cgi?component=kern&query_format=advanced&short_desc=mydev
```

Replace `mydev` with your driver's name. The query returns bugs
whose summary mentions your driver.

If a bug is filed:

- Read the report carefully.
- If you can reproduce it, do so.
- If you can fix it, prepare a patch. The patch goes through the
  same review process as any other change.
- If you cannot reproduce it, ask the reporter for more
  information: FreeBSD version, hardware details, relevant log
  output.
- If it is a real bug but you do not have the time or capacity
  to fix it, say so in the bug report. A bug with an engaged
  author who cannot currently fix it is different from a bug
  with an absent author. At minimum, the engagement means that
  someone else who looks at the bug has context to work with.

Bugzilla also hosts enhancement requests (feature requests for
new functionality). These are lower priority than bug reports,
but they are useful signals about what users need. You do not
need to implement every enhancement request, but acknowledging
them and discussing priorities is part of maintenance.

### Responding to Community Feedback

In addition to Bugzilla, community feedback can reach you
through several other channels:

- Direct email from users.
- Discussions on the mailing lists.
- Questions on IRC channels.
- Comments on review threads for related work.

The expectation for a responsive maintainer is not that you
respond to every one of these instantly. The expectation is that
you are reachable at the email address on record for the driver
(the one in the `AUTHORS` section of the manual page and in the
commit history), and that when you respond to something, you do
so productively.

A practical rhythm might look like this: once a week or once
every two weeks, check your driver-related email and Bugzilla
queries. Respond to anything that is waiting. Triage anything
new. Keep the response times reasonable, on the scale of a week
or two rather than months.

If your circumstances change and you cannot maintain the driver
any more, say so publicly. The project can and will find new
maintainers if the need is known. The worst outcome is
disappearing silently, leaving bugs unacknowledged and users
uncertain whether the driver is being maintained at all.

### Kernel API Drift

The FreeBSD kernel evolves. APIs that were stable when your
driver was written may change. When this happens, your driver
needs to be updated, and you are the first person the project
will look to for the update.

Several kinds of API drift that commonly affect drivers:

- Changes to the Newbus framework: new method signatures, new
  method categories, changes to `device_method_t` macros.
- Changes to the bus-specific attachment patterns: PCI, USB,
  iicbus, spibus, and others evolve over time.
- Changes to the `bus_space(9)` interface.
- Changes to the character-device interface (`cdevsw`,
  `make_dev`, etc.).
- Changes to the memory allocation API (`malloc(9)`, `contigmalloc`,
  `bus_dma`).
- Changes to the synchronisation primitives (`mtx`, `sx`, `rw`).
- Deprecation of old APIs in favour of new ones.

Usually, these changes are announced on the mailing lists before
they are committed, and sometimes they come with a "tree-sweep"
commit that updates all users of the old API to the new one. If
your driver is in the tree, the tree-sweep will typically update
it automatically. But not always; sometimes the sweep is
conservative and leaves drivers that it cannot mechanically
update for the maintainer to handle.

A good habit: check `freebsd-current@` at least occasionally for
API change discussions that affect your driver. If you see one,
check whether your driver still builds against current HEAD. If
it does not, send a patch to update it.

### The UPDATING File

The project maintains an `UPDATING` file at `/usr/src/UPDATING`
that lists significant changes in the source tree, including API
changes that drivers may need to respond to. Read it occasionally
(especially before updating your tree) to see whether anything
affects your driver.

A typical UPDATING entry might read:

```text
20260315:
	The bus_foo_bar() API has changed to require an explicit
	flags argument.  Drivers using bus_foo_bar() should pass
	BUS_FOO_FLAG_DEFAULT to preserve historical behaviour.
	Drivers using bus_foo_bar_old() should migrate to the new
	API as bus_foo_bar_old() will be removed in FreeBSD 16.
```

If you see an entry like this mentioning a function your driver
uses, update the driver accordingly.

### Tree-Wide Refactors

Occasionally, the project does tree-wide refactors that touch
every driver. Examples from FreeBSD's history include:

- The conversion from `$FreeBSD$` CVS tags to git-only metadata.
- The introduction of SPDX-License-Identifier lines across the
  tree.
- Large-scale renames of APIs such as the `make_dev` family or
  the `contigmalloc` family.

When a tree-wide refactor happens, the refactor commit typically
updates your driver along with everyone else's. You may not need
to do anything. But the refactor will show up in `git log`
against your driver, and future developers looking at the
history will see it. Understand what happened so that you can
explain it if asked.

### Participating in Future Releases

FreeBSD has a release cycle of roughly a year between major
releases, with point releases on a more frequent schedule. Your
driver participates in this cycle whether you do anything active
or not.

A few things are worth understanding:

- Your driver is built for every release that comes out of a
  branch where it lives. If it lives in `main`, it will be in
  the next major release. If it is also cherry-picked to a
  `stable/` branch, it will be in the next point release of
  that branch.
- Before major releases, the release-engineering team may ask
  maintainers to confirm that their drivers are in good shape.
  If you get such a request for your driver, respond. It is a
  simple action that helps the project plan the release.
- After a release, your driver is in the wild on every
  installation that uses that release. Bug reports may be more
  frequent right after a release.

Participating in the release cycle is a low-effort form of
maintenance. It mostly consists of being available for the
release engineers to contact you if they need to.

### Keeping Code Updated: A Rhythm

A reasonable rhythm for maintaining a driver in the tree:

- Monthly: check Bugzilla for open bugs against the driver.
  Respond to anything outstanding.
- Monthly: rebuild the driver against current HEAD and check for
  warnings or failures. If anything fails, investigate and fix.
- Quarterly: re-read the manual page. Update if the driver has
  changed since the last review.
- Before major releases: run through the full pre-submission
  test suite (style, mandoc, build, universe) on your driver as
  it currently stands. Fix anything that has drifted.
- Whenever you have a hardware sample and a spare afternoon:
  exercise the driver on the hardware and make sure it still
  works.

This rhythm is not mandatory. A driver can go months without
maintenance if nothing is broken. But having a rhythm in mind
keeps the driver healthy over time.

### Exercise: Create a Monthly Maintenance Checklist

Before closing this section, open a text file and write down a
monthly maintenance checklist for your driver. Include:

- The URL for the Bugzilla query that shows bugs against the
  driver.
- The commands to rebuild the driver against current HEAD.
- The commands to run the style and lint checks.
- The commands to check for API drift (e.g., `grep` for
  deprecated calls).
- A note about the email address where users might reach you.
- A reminder to update the manual page date if you make semantic
  changes.

Save this checklist with the driver's source, or in your
personal notes. The act of writing it down commits you to the
rhythm. Checklists that exist on paper get followed; checklists
that live only in memory do not.

### When You Cannot Maintain Anymore

Life changes. Jobs change. Priorities shift. At some point you
may find that you cannot maintain your driver the way you once
did. This is normal and the project has processes for handling
it.

The right move is to say so publicly. Options:

- Post to `freebsd-hackers@` or the relevant subsystem list
  saying that you are stepping back from the driver and inviting
  someone else to take it over.
- File a Bugzilla entry tagged as a maintainership transition
  question.
- Email the committers who have reviewed your patches and tell
  them directly.

The project will then find a new maintainer, or mark the driver
as orphaned, or decide on some other path. The important thing
is that the status is known. Silent abandonment is worse than
any of the alternatives.

If no one takes over the driver and it continues to be used,
the project may eventually mark it deprecated. This is not a
failure; it is a reasonable response to code that no one is
actively caring for. Drivers can be deprecated, removed, and
later re-added if someone steps up. The history of the tree is
full of such cycles.

### Wrapping Up Section 8

Post-merge maintenance is a lighter-weight activity than initial
submission, but it is real. The expectations are: watch Bugzilla
for bugs against your driver, respond to users who reach out,
keep the driver building against current HEAD as the kernel
evolves, participate in release cycles, and say so publicly if
you cannot continue to maintain. A driver whose author is
engaged over time is a driver the project values beyond the
initial merge.

With Sections 1 through 8 complete, we have walked the full
arc of a driver submission: from understanding the project to
preparing the files, through licensing, manual pages, testing,
submission, review iteration, and long-term maintenance. The
remainder of the chapter offers hands-on labs and challenge
exercises that let you practise the workflow against real code,
followed by a consolidation of the mental model and a bridge
to the closing chapter.

## Hands-On Labs

The labs in this chapter are designed to be done against a real
driver. The easiest approach is to take a driver you have already
written during the book, such as the LED driver from earlier
chapters or the mock device from Chapter 36, and walk it through
the submission-preparation workflow.

If you do not have a driver at hand, the companion examples in
`examples/part-07/ch37-upstream/` include a skeleton driver you
can use.

All the labs can be done in a FreeBSD 14.3 development virtual
machine. None of them will submit anything to the real FreeBSD
project, so you can work freely without worrying about
accidentally publishing half-finished work.

### Lab 1: Prepare the File Layout

Goal: Take an existing driver and rearrange its files into the
conventional FreeBSD layout.

Steps:

1. Identify the driver you will be working with. Call it
   `mydev`.
2. Create the directory structure:
   - `sys/dev/mydev/` for driver sources.
   - `sys/modules/mydev/` for the module Makefile.
   - `share/man/man4/` for the manual page.
3. Move or copy the `.c` and `.h` files into `sys/dev/mydev/`.
   Rename them if necessary so that the main source file is
   `mydev.c`, the internal header is `mydev.h`, and any hardware
   register definitions live in `mydevreg.h`.
4. Write a module Makefile at `sys/modules/mydev/Makefile`
   following the template from Section 2.
5. Build the module with `make`. Fix any build errors.
6. Verify that the module loads with `kldload` and unloads with
   `kldunload`.

Success criterion: the driver builds as a loadable module and
the file layout matches the conventions of the tree.

Expected time: 30 to 60 minutes for a small driver.

Common problems:

- Include paths that assumed the old layout. Fix the includes to
  use `<dev/mydev/mydev.h>` rather than `"mydev.h"`.
- Forgotten entries in `SRCS`. If you have multiple `.c` files,
  list all of them.
- Missing `.PATH`. The Makefile needs `.PATH:
  ${SRCTOP}/sys/dev/mydev` so make can find the sources.

### Lab 2: Audit the Code Style

Goal: Bring the driver source into compliance with `style(9)`.

Steps:

1. Run `/usr/src/tools/build/checkstyle9.pl` against every `.c`
   and `.h` file in the driver. Capture the output.
2. Read each warning carefully. Cross-reference against
   `style(9)` to understand what the rule is.
3. Fix each warning in the source. Rerun the style checker
   after each batch of fixes.
4. When the style checker is clean, read through the source by
   eye looking for anything the checker missed: inconsistent
   indentation inside multi-line arguments, comment styles,
   variable-declaration groupings.
5. Ensure every function that is not exported has the `static`
   keyword. Ensure every exported function has a declaration in
   a header.

Success criterion: the style checker produces no warnings against
any file in the driver.

Expected time: one to three hours for a driver that has not
previously been through a style audit.

Common surprises:

- Space-instead-of-tab warnings on lines you thought were fine.
  The checker is strict; trust it.
- Warnings about blank lines between variable declarations and
  code. `style(9)` requires a blank line.
- Warnings about return expressions without parentheses. Fix by
  adding parentheses.

### Lab 3: Add the Copyright Header

Goal: Ensure every source file has a correct FreeBSD-style
copyright header.

Steps:

1. Identify every file in the driver that needs a header: each
   `.c` file, each `.h` file, and the manual page.
2. For each file, check the existing header. If it is missing or
   malformed, replace it with a known-good template.
3. Use `/*-` as the opening of the header in `.c` and `.h` files.
   Use `.\"-` as the opening in the manual page.
4. Include the SPDX-License-Identifier line with the appropriate
   licence, typically `BSD-2-Clause`.
5. Add your name and email to the Copyright line.
6. Include the standard licence text.
7. Verify that the file starts in column 1 with the `/*-` or
   `.\"-` opening.

Success criterion: every file has a correctly-formatted header
that matches the conventions of files already in the tree.

Expected time: 30 minutes.

Verification:

- Compare your header against the header in
  `/usr/src/sys/dev/null/null.c`. They should be structurally
  identical.
- If you are using an automated licence-harvester tool, it
  should recognise your headers.

### Lab 4: Draft the Manual Page

Goal: Write a complete section-4 manual page for the driver.

Steps:

1. Create `share/man/man4/mydev.4`.
2. Start from the template in Section 4 of this chapter, or from
   the companion example.
3. Fill in each section for your driver:
   - NAME and NAME description.
   - SYNOPSIS showing how to compile in the kernel or load as a
     module.
   - DESCRIPTION in prose.
   - HARDWARE listing supported devices.
   - FILES listing device nodes.
   - SEE ALSO with relevant cross-references.
   - HISTORY noting the driver's first appearance.
   - AUTHORS with your name and email.
4. Follow the one-sentence-per-line rule throughout.
5. Run `mandoc -Tlint mydev.4` and fix every warning.
6. Render the page with `mandoc mydev.4 | less -R` and read it
   from a user's perspective. Fix anything awkward.
7. If you have `igor` installed, run it and address its
   warnings.

Success criterion: `mandoc -Tlint` is silent, and the rendered
page reads clearly.

Expected time: one to two hours for a first manual page.

Reading assignment for the lab: before you start, read
`/usr/src/share/man/man4/null.4`, `/usr/src/share/man/man4/led.4`,
and `/usr/src/share/man/man4/re.4`. These three pages span the
range of complexity that section-4 manual pages can have, and
they will give you a strong intuition for what yours should
look like.

### Lab 5: Build and Load Automation

Goal: Write a shell script that automates the pre-submission
build and load cycle.

Steps:

1. Create a script named `pre-submission-test.sh` in the
   companion examples directory.
2. The script should, in order:
   - Run the style checker on every source file.
   - Run `mandoc -Tlint` on the manual page.
   - Run `make clean && make obj && make depend && make` in the
     module directory.
   - Load the resulting module with `kldload`.
   - Unload the module with `kldunload`.
   - Report success or failure clearly.
3. Use `set -e` so the script exits on the first error.
4. Include helpful echo statements announcing each stage.
5. Test the script against your driver.

Success criterion: the script runs clean against a driver that is
ready for submission, and produces clear error output for a
driver that has problems.

Expected time: 30 minutes for a simple script; longer if you add
polish.

### Lab 6: Generate a Submission Patch

Goal: Practise the patch-generation workflow without actually
submitting.

Steps:

1. In a throwaway git clone of the tree, create a topic branch
   for your driver:

   ```sh
   git checkout -b mydev-driver
   ```

2. Add the driver files:

   ```sh
   git add sys/dev/mydev/ sys/modules/mydev/ share/man/man4/mydev.4
   ```

3. Commit with a proper message following the conventions from
   Section 2:

   ```sh
   git commit -s
   ```

4. Generate a patch:

   ```sh
   git format-patch -1 HEAD
   ```

5. Read the generated `.patch` file. Verify that it looks clean:
   no unrelated changes, no trailing whitespace, a well-formed
   commit message.
6. Apply the patch to a fresh clone to verify that it applies
   cleanly:

   ```sh
   git am < 0001-mydev-Add-driver.patch
   ```

Success criterion: you have a clean patch file that represents
the driver submission, and it applies cleanly to a fresh tree.

Expected time: 30 minutes.

Common surprises:

- `git format-patch` produces a file per commit. If you have
  three commits on your branch, you will get three `.patch`
  files. For a driver submission that should be a single commit,
  amend or squash first.
- Trailing whitespace in the commit shows up as `^I` sequences
  in the patch. Remove it before committing.
- Line-ending issues. Make sure your editor is using LF, not
  CRLF.

### Lab 7: Draft a Review Cover Letter

Goal: Practise writing the description that accompanies a
submission.

Steps:

1. Open a text editor and write an email-style cover letter for
   your driver submission.
2. Include:
   - A subject line suitable for a mailing-list message.
   - A one-paragraph summary of what the driver does.
   - A description of the hardware supported.
   - A list of what was tested.
   - A statement of what feedback you are inviting.
3. Keep the tone professional and collaborative. You are asking
   for review, not demanding approval.
4. Save the letter as `cover-letter.txt` in your companion
   examples directory.
5. Share it with a friend or colleague for feedback before
   moving on.

Success criterion: the cover letter reads as a productive
invitation to review.

Expected time: 15 to 30 minutes.

### Lab 8: Dry-Run a Review Cycle

Goal: Rehearse the iteration side of the review cycle.

Steps:

1. Ask a colleague to read your patch and cover letter as if
   they were a reviewer.
2. Capture their feedback as a list of comments.
3. Treat each comment as a real review comment. Respond to each
   one: make the fix, explain your reasoning, or push back
   constructively.
4. Update the commit and regenerate the patch.
5. Repeat for at least two rounds of feedback.

Success criterion: you have experience iterating on a patch in
response to feedback, and your commit at the end is still a
single clean commit rather than a messy history.

Expected time: variable, depending on the colleague's
availability.

Variant: if a colleague is not available, ask a reviewer to read
the companion code and act as a mock reviewer. Or use an online
code-review simulator if one is available in your environment.

## Challenge Exercises

The challenge exercises are optional but highly recommended. Each
of them takes an idea from the chapter and pushes it into
territory that will exercise your judgement.

### Challenge 1: Audit a Historical Driver

Pick an older driver in `/usr/src/sys/dev/` that has been in the
tree for at least five years. Look at its current state and
identify:

- Parts of the copyright header that do not match modern
  conventions.
- Style violations that `checkstyle9.pl` flags.
- Manual-page sections that do not match modern style.
- Deprecated APIs the driver still uses.

Write up your findings as a short report. Do not submit a patch
to fix them (older drivers often have good reasons for their
historical form), but understand why they look the way they do.

The goal is to build an eye for the difference between modern
conventions and historical ones. After doing this exercise, you
will recognise at a glance which parts of a driver were written
recently and which are legacy.

Expected time: two hours.

### Challenge 2: Cross-Architecture Debug

Take your driver and attempt to build it for a non-native
architecture, such as `arm64` if you are on `amd64`:

```sh
cd /usr/src
make TARGET=arm64 buildkernel KERNCONF=GENERIC
```

Identify any warnings or errors that are specific to the target
architecture. Fix them. Rebuild. Repeat.

If your driver builds cleanly on both `amd64` and `arm64`, try
`i386`. If you want an extra challenge, try `powerpc64` or
`riscv64`. Each architecture will surface different kinds of
issues.

Write up a short note about what you found and how you fixed
it. The cross-architecture discipline is one of the things that
separates a casually-written driver from a production-grade one.

Expected time: three to six hours, depending on how many
architectures you try.

### Challenge 3: Manual Page Depth

Pick a driver in the tree whose manual page you find impressive.
Copy the structure of that page and use it as a template to
rewrite your own manual page at a similar level of depth.

Your rewritten manual page should:

- Have a SYNOPSIS that shows all the ways to load and configure
  the driver.
- Have a DESCRIPTION that gives a user a complete picture of
  what the driver does.
- Have a full HARDWARE section, including revision information
  where relevant.
- Have LOADER TUNABLES, SYSCTL VARIABLES, or DIAGNOSTICS
  sections if your driver has any of these features.
- Have a BUGS section that is honest about known issues.
- Pass `mandoc -Tlint` and `igor` clean.

The goal is to produce a manual page that reads like a
first-class example of the genre, not a minimal compliance
artefact.

Expected time: three to five hours.

### Challenge 4: Run a Mock Review

Partner with another reader of this book, or a colleague
familiar with FreeBSD. Exchange drivers with them. You review
their driver. They review yours.

As a reviewer, do the following for the driver you are reviewing:

- Run all the pre-submission tests yourself and capture the
  results.
- Read the code carefully. Make specific comments on anything
  that seems unclear, incorrect, or non-idiomatic.
- Read the manual page. Make comments on anything that seems
  incomplete or unclear.
- Write a summary review note that includes your overall
  impression, your requested changes, and any questions you
  have.

As the contributor receiving review, do the following:

- Read the feedback carefully.
- Respond to each comment constructively.
- Update the patch.
- Send the updated patch back.

Do at least two rounds. At the end, write a short reflection on
what you learned from both sides.

The goal is to experience both sides of the review process
before you ever submit a patch to the real project. After this
exercise, the first real review will feel much more familiar.

Expected time: variable, but at least a weekend.

### Challenge 5: Trace the Life of a Real Commit

Pick a recent driver-related commit in the FreeBSD tree, ideally
one that was contributed by a non-committer and sponsored. Use
`git log` to find it, or browse Phabricator archives.

Trace its history:

- When was the review first opened?
- What did the first version look like?
- What comments did reviewers leave?
- How did the author respond?
- How did the patch evolve?
- When was it finally committed?
- What does the final commit message say?

Write up a short narrative of what you found. This exercise
builds intuition for what a real review looks like from the
inside.

Expected time: two hours.

## Troubleshooting and Common Mistakes

Even with careful preparation, things can go wrong. This section
collects the most common problems first-time contributors
encounter and explains how to diagnose and fix them.

### Patch Rejected Because of Style

Symptom: the reviewers leave many small comments about
indentation, variable names, comment formatting, or return
statement parentheses.

Cause: the patch was submitted without running
`tools/build/checkstyle9.pl` first, or the author ignored some
warnings.

Fix: run `checkstyle9.pl` against every source file. Fix every
warning. Rebuild and re-test. Resubmit.

Prevention: make `checkstyle9.pl` part of your pre-submission
script. Run it before every submission.

### Patch Rejected Because of Manual-Page Issues

Symptom: the reviewer says the manual page has lint errors, or
does not match the project's mdoc style.

Cause: the manual page was not validated with `mandoc -Tlint`
before submission, or the one-sentence-per-line rule was not
followed.

Fix: run `mandoc -Tlint` against the manual page. Fix every
warning. Read the rendered output to verify that it reads well.
Resubmit.

Prevention: treat the manual page with the same care as the
code. Include it in your pre-submission script.

### Patch Does Not Apply Cleanly

Symptom: the reviewer reports that the patch does not apply to
current HEAD. Or the CI fails at the `git apply` stage.

Cause: the patch was generated against an older version of the
tree, and HEAD has moved since.

Fix: pull the latest HEAD, rebase your branch on top, resolve
any conflicts, retest, and regenerate the patch.

Prevention: rebase on current HEAD immediately before
submission. Do not submit a patch that was generated a week
ago.

### Kernel Panic on Load

Symptom: `kldload` panics the kernel.

Cause: often, a NULL pointer dereference in the driver's
`probe` or `attach` routine, or a missing initialisation step.

Fix: debug with the standard kernel-debugging tools (covered in
Chapter 34). Common specific causes:

- `device_get_softc(dev)` returning NULL because the
  `driver_t.size` field is not set to `sizeof(struct
  mydev_softc)`.
- `bus_alloc_resource_any(dev, SYS_RES_MEMORY, ...)` returning
  NULL and the driver not checking for the NULL before using the
  result.
- A static variable incorrectly initialised, causing undefined
  behaviour.

Prevention: test on a development VM before submission.
Repeatedly load and unload to catch initialisation bugs.

### Kernel Panic on Unload

Symptom: `kldunload` panics, or the module refuses to unload.

Cause: the detach path is incomplete. Common specific causes:

- A callout that is still scheduled when the softc is freed.
  Use `callout_drain`, not `callout_stop`.
- A taskqueue task that is still pending. Use `taskqueue_drain`
  on every task.
- An interrupt handler that is still installed when the
  resource is freed. Tear down the handler with
  `bus_teardown_intr` before calling `bus_release_resource`.
- A device node that is still open when `destroy_dev` is
  called. Use `destroy_dev_drain` if the node might be open.

Fix: audit the detach path. Make sure every resource is
released, every callout is drained, every task is drained, every
handler is torn down, and every device node is destroyed before
the softc is freed.

Prevention: structure the detach code in reverse of the attach
code. Every `attach` step has a corresponding `detach` step, and
the order is strict.

### Driver Builds But Does Not Probe

Symptom: the module loads, but when the hardware is present, the
driver does not attach to it. `pciconf -l` shows the device with
no driver.

Cause: usually, a mismatch in the `probe` routine between the
driver's expected vendor/device ID and the actual one. Or the
driver uses `ENXIO` incorrectly.

Fix: check the vendor and device IDs. Double-check with
`pciconf -lv`. Verify that `probe` returns `BUS_PROBE_DEFAULT`
or `BUS_PROBE_GENERIC` when the device matches, not an error
code.

Prevention: test against real hardware before submission.

### Manual Page Does Not Render

Symptom: `man 4 mydev` shows no output, or shows raw mdoc
source.

Cause: usually, the file is in the wrong place, or it is not
named correctly, or `makewhatis` has not been run.

Fix: verify the path (`/usr/share/man/man4/mydev.4`), verify the
name (must end in `.4`), and run `makewhatis /usr/share/man` to
rebuild the manual database.

Prevention: test the man page installation before submission.

### Reviewer Is Unresponsive

Symptom: you submitted a patch, responded to the initial
comments, and then the reviewer went silent.

Cause: reviewers are volunteers. Their time is finite. Sometimes
a patch slips off the radar.

Fix: wait at least a week. Then send a polite ping on the
review thread or the relevant mailing list. If still silent,
consider asking for another reviewer to pick it up.

Prevention: submit patches that are small, well-prepared, and
easy to review. Smaller patches get faster reviews.

### Patch Is Approved But Not Committed

Symptom: a reviewer has explicitly said the patch looks good,
but it has not been committed.

Cause: the reviewer may not be a committer, or they may be a
committer but waiting for a second opinion, or they may be busy
with other things.

Fix: politely ask whether anyone is in a position to commit the
patch. "Is anyone able to sponsor the commit of this patch? I
have responded to all feedback and the review is approved."

Prevention: none specifically; this is part of normal project
flow.

### Patch Committed But You Were Not Credited

Symptom: you look at the commit log and see your patch
committed, but the author field is wrong.

Cause: a committer may have accidentally applied the patch
without preserving authorship. This is rare but happens.

Fix: politely email the committer asking whether the authorship
can be corrected. A `git commit --amend` with the correct author
can fix it before push; after push, the commit is immutable, but
the committer can add a note or amend the original commit
message in rare cases.

Prevention: when generating a patch with `git format-patch`,
ensure your `user.name` and `user.email` are set correctly.

### Your Driver Was Accepted but Your Interface Choice Was Wrong

Symptom: your driver is in the tree, but you later realise that
the userland interface you designed was a poor fit.

Cause: design choices made before full usage experience
sometimes turn out wrong.

Fix: this is a real engineering problem that the project
handles regularly. The options include: adding a new interface
alongside the old one and deprecating the old one; documenting
the old interface as legacy and introducing a successor; or,
rarely, making a breaking change if the driver has few enough
users that the breakage is acceptable.

Prevention: talk to the list about interface design before
implementation, especially for interfaces that will be visible
to userland for a long time.

## Wrapping Up

Submitting a driver to the FreeBSD Project is a process with
many steps, but it is not a mysterious one. The steps, taken in
order, lead from a working driver on your machine to a
maintained driver in the FreeBSD source tree. The process
involves understanding how the project is organised, preparing
the files according to the project's conventions, handling the
licence correctly, writing a proper manual page, testing across
the architectures the project supports, generating a clean
patch, navigating the review process with patience, and
committing to the long arc of maintenance after the driver
merges.

A few themes have run through the chapter and deserve a final
explicit summary.

The first theme is that a working driver is not the same as an
upstream-ready driver. The code you wrote in the book was
working code; making it upstream-ready is additional work, and
most of that work is in small conventions rather than large
changes. Attention to those conventions is the difference
between a first submission that is welcomed and one that is
held up repeatedly in review.

The second theme is that upstream review is collaborative, not
adversarial. The reviewers on the other side of the patch are
trying to help your driver land in a form that the tree can
carry forward. Their comments are investments of their time, not
attacks on your competence. Responding to those comments
productively, patiently, and substantively is the craft of the
review process. First-time contributors who internalise this
framing have easier reviews than those who do not.

The third theme is that documentation, licence, and style are
part of engineering quality, not bureaucracy. The manual page
you write is the interface through which users will understand
your driver for as long as it lives. The licence you attach
determines whether the driver can be merged at all. The style
you follow determines whether future maintainers will understand
the code. None of these are administrative overhead; all of them
are part of the work of being a software engineer in a large
shared code base.

The fourth theme is that merge is a beginning, not an end. The
driver in the tree requires ongoing care: bug triage, API drift
fixes, release-time check-ins, and occasional enhancements. That
care is lighter than the initial submission, but it is real, and
it is part of what turns a one-shot submission into a sustained
contribution to the project.

The fifth and most important theme is that this is all
learnable. None of the skills in this chapter require talent
beyond what you have already built up over the book. They
require attention to detail, patience with iteration, and
willingness to engage with a community. Those qualities are ones
you can develop with practice. The committers in the FreeBSD
Project all started where you are now, as contributors writing
their first patches, and they built up their standing through
the same steady accumulation of careful work that you can.

Take a moment to appreciate what has changed in your toolkit.
Before this chapter, submitting a driver to FreeBSD was probably
a vague aspiration. Now it is a concrete process with a finite
number of steps, each of which you have seen in detail. The
labs have given you practice. The challenges have given you
depth. The mistakes section has given you a map of the common
pitfalls. If you decide to submit a real driver in the coming
weeks or months, you have everything you need to start.

Some of the specifics in this chapter will shift over time. The
Phabricator / GitHub balance may tilt further toward GitHub, or
back, or neither. The style-checking tools may evolve. The
review conventions may get small refinements. Where we know a
convention is in motion, we have flagged it. Where we cited a
specific file, we named it so you can open it yourself and
check the current state. The reader who trusts but verifies is
the reader the project benefits from most.

You are now, in the practical sense, prepared to contribute.
Whether you do so is up to you. Many readers of a book like
this one never contribute; that is fine, and the skills you
built here serve you in your own work regardless. Some readers
will contribute once, land a patch, and move on; that is also
fine, and the project thanks them. A smaller number will find
that they enjoy the collaboration enough to keep contributing,
and over time will become deeply involved. Any of these paths
is legitimate. The choice is yours.

## Bridge to Chapter 38: Final Thoughts and Next Steps

This chapter has been, in one sense, the culmination of the
practical arc of the book. You started without kernel knowledge,
worked through UNIX and C, learned the shape of a FreeBSD
driver, built character and network drivers, integrated with the
Newbus framework, and worked through a series of mastery topics
covering the specialised scenarios that production drivers
encounter. The chapter you have just finished walked you
through the process by which a driver you have built can become
part of the FreeBSD operating system itself, maintained by a
community of engineers and shipped to users on every release.

Chapter 38 is the closing chapter of the book. It is not another
technical chapter. Its role is different. It is a chance to take
stock of your progress, reflect on what you have learned, consider
where you stand now, and think about where you might go next.

Several themes from Chapter 37 will naturally carry into
Chapter 38. The idea that merge is a beginning rather than an
end, for example, applies not just to individual drivers but to
the reader's relationship with FreeBSD as a whole. Writing one
driver, or two, or ten, is a beginning; sustained engagement
with the project is the longer arc. The collaborative mindset
that this chapter argued for in the context of code review is
the same mindset that makes someone a valued community member
over time. The discipline of documentation, licensing, and
style that this chapter argued for in the context of a single
driver scales up to the discipline of being a careful engineer
in any large code base.

Chapter 38 will also address topics that this book did not
cover in full depth, such as filesystem integration, network-
stack integration (Netgraph, for example), USB composite
devices, PCI hotplug, SMP tuning, and NUMA-aware drivers. Each
of these is a substantial topic in its own right, and the
closing chapter will point you toward the resources you can use
to study them on your own. The book has given you the
foundation; the topics in Chapter 38 are the directions you can
extend that foundation into.

There are also the other BSDs. Much of what you have learned
transfers, with modifications, to OpenBSD and NetBSD. The
drivers you write for FreeBSD may find useful analogues in those
projects, and some of the mastery topics from Part 7 have direct
equivalents in each of the other BSDs. If you are interested in
the wider BSD world, Chapter 38 will suggest where to look.

And there is the question of community. The FreeBSD Project is
not an abstraction; it is a community of engineers,
documenters, release managers, and users who together produce
and maintain the operating system. Chapter 38 will reflect on
what it means to be part of that community, how to find your
place in it, and how to contribute to it beyond just driver
submissions. Translations, documentation, testing, bug triage,
and mentoring are all forms of contribution, and the project
values each of them.

One final reflection before we close this chapter. Submitting a
driver is, at its heart, an act of trust. You are offering your
code to a community of engineers who will carry it forward.
They, in turn, are offering their attention, their review time,
and their commit rights to a contributor who was a stranger
until this patch arrived. The trust goes both ways. It is built
up, over time, by many small acts of careful work and
responsible engagement. The first submission is the beginning of
that trust, not the end of it. By the time you are a committer,
if that is a path you choose, the trust is something you have
earned in hundreds of small interactions.

You have done most of the work of becoming someone the project
could trust. The rest is practice, and time, and patience.

Chapter 38 will close out the book with reflection, suggestions
for continued learning, and a final word on where you might go
from here. Take a breath, close your laptop for a
moment, and let the material of this chapter settle. When you
are ready, turn the page.
