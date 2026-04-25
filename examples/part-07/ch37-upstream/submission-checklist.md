# Pre-Submission Checklist

This checklist mirrors the gates a FreeBSD reviewer will apply to
a new driver submission. Use it as a starting point and adapt it
to your own driver. Type your own version in your own words.
A checklist you have typed yourself will be more useful than one
you copied verbatim: the act of typing forces you to think about
each item.

Treat every item as a blocker. Do not submit until you can tick
every box with full confidence.

## File Layout

- [ ] Driver sources live under `sys/dev/<driver>/`.
- [ ] The main source file is named `<driver>.c`.
- [ ] The internal header, if any, is named `<driver>.h`.
- [ ] Hardware register definitions, if any, live in
  `<driver>reg.h`.
- [ ] The module Makefile lives at `sys/modules/<driver>/Makefile`.
- [ ] The manual page lives at `share/man/man4/<driver>.4`.
- [ ] No files live in ad-hoc locations outside these paths.

## Copyright Headers

- [ ] Every `.c` and `.h` file starts in column 1 with `/*-`.
- [ ] Every file has an `SPDX-License-Identifier:` line
  immediately after the opening marker.
- [ ] The SPDX code matches the licence text that follows.
- [ ] Every file has at least one `Copyright (c) YEAR Name` line.
- [ ] The licence text matches one of the approved templates
  documented in Section 3 of Chapter 37.
- [ ] The manual page uses `.\"-` (with a dash) as its opening
  marker, not `.\"`.

## Code Style

- [ ] `/usr/src/tools/build/checkstyle9.pl` runs clean against
  every `.c` and `.h` file.
- [ ] Indentation uses real tabs, not spaces.
- [ ] Tab stops are set to 8 columns; continuation indents use
  4 spaces.
- [ ] Lines are no longer than 80 columns except where an
  exception is warranted (panic strings, grep anchors).
- [ ] Every function that is not declared in a header is
  `static`.
- [ ] Return expressions are parenthesised: `return (0);` not
  `return 0;`.
- [ ] Variable declarations are grouped at the top of each
  function with a blank line separating them from the first
  statement.
- [ ] Function definitions have the return type on the line
  above the function name, with the name starting in column 1.
- [ ] Identifier names follow the `snake_case` convention, not
  `camelCase` or `PascalCase`.

## Build Integration

- [ ] The module Makefile includes `bsd.kmod.mk`.
- [ ] `SRCS` lists every `.c` file in the driver, plus any
  auto-generated `*_if.h` files that the driver uses.
- [ ] `.PATH` uses `${SRCTOP}/sys/dev/<driver>`, not a
  hardcoded path.
- [ ] `make` in the module directory succeeds from a clean
  tree.
- [ ] `make clean && make obj && make depend && make` produces
  a single `.ko` file and no warnings.

## Runtime

- [ ] `kldload <driver>` succeeds with no warnings or
  error messages.
- [ ] `kldstat` shows the module loaded.
- [ ] The driver detaches cleanly under `kldunload <driver>`.
- [ ] A load/unload cycle repeated 50 times produces no kernel
  panics, no memory leaks (confirmed with `vmstat -m`), and no
  unexpected messages in `dmesg`.
- [ ] If the driver exposes a device node, it is created on
  attach and removed on detach.
- [ ] If the driver creates sysctl variables, they appear on
  attach and disappear on detach.

## Cross-Architecture Build

- [ ] `make universe` completes without errors for the
  architectures the driver is expected to support.
- [ ] At a minimum, `amd64` and `arm64` build cleanly.
- [ ] If the driver uses architecture-specific code, every
  supported architecture has been tested.

## Manual Page

- [ ] `mandoc -Tlint share/man/man4/<driver>.4` is silent.
- [ ] The manual page has `NAME`, `SYNOPSIS`, `DESCRIPTION`,
  `HARDWARE`, `FILES`, `SEE ALSO`, `HISTORY`, and `AUTHORS`
  sections.
- [ ] The `HARDWARE` section uses the `.Bl -bullet -compact`
  list style required by `style.mdoc(5)`.
- [ ] Every sentence starts on its own line.
- [ ] `igor(1)` runs clean (optional but recommended).
- [ ] The page renders clearly when viewed with
  `mandoc <driver>.4 | less -R`.

## Commit Message

- [ ] The subject line is in the form
  `<subsystem>: <brief description>`.
- [ ] The subject line is no longer than 72 characters.
- [ ] The body wraps at 72 columns.
- [ ] The body explains what the driver does and why it is
  being added.
- [ ] The commit message ends with a `Signed-off-by:` line.
- [ ] The commit message does not reference internal company
  URLs, internal ticket IDs, or internal review tools that
  external contributors cannot reach.

## Review Preparation

- [ ] The submission description is written as a one-paragraph
  summary, a design discussion, and a testing summary.
- [ ] The description names the hardware supported and the
  versions tested.
- [ ] The description invites feedback on specific design
  questions.
- [ ] The branch on which the commit lives is up to date with
  `origin/main`.
- [ ] `git show HEAD` reads cleanly end to end.
- [ ] No unrelated files are included in the commit.
- [ ] No trailing whitespace is present.

## Post-Submission Readiness

- [ ] You have subscribed to the Bugzilla product for the
  driver's subsystem.
- [ ] You have a plan for responding to review feedback within
  a reasonable time.
- [ ] You have noted the UPDATING file so you can watch for
  tree-wide changes that will affect the driver.
- [ ] You understand that the driver is your long-term
  responsibility, not just a one-time submission.

## Final Gate

- [ ] Someone other than you has read the patch and the manual
  page and thinks both are clear.
- [ ] You can explain, in one paragraph, why this driver
  belongs in the tree.
- [ ] You are prepared to iterate on feedback and not be
  defensive about the code.

If every box is ticked, you are ready to submit.
