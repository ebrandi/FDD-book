# Build Notes for Chapter 36 Examples

This file collects the build-related notes that apply across the
chapter's labs. Read it before working through the labs.

## FreeBSD Version

The labs are written and tested against FreeBSD 14.3. They use only
public, stable interfaces from `bus_space(9)`, the device driver
framework, and the kernel module Makefile system. They should also
build on FreeBSD 14.x stable branches and on -CURRENT, but minor
adjustments may be required.

## Source Tree

You need a copy of the FreeBSD kernel source under `/usr/src/`. The
Makefiles use `SYSDIR?= /usr/src/sys` to locate the kernel headers
and the build infrastructure. If your source tree is in a different
location, override `SYSDIR` on the `make` command line.

## The `bsd.kmod.mk` Pattern

Every kernel module in this chapter uses the same minimal Makefile
pattern. The pattern is:

```
KMOD=   <module-name>
SRCS=   <list-of-sources>

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

`KMOD` is the module's name. The build will produce a file named
`KMOD.ko` in the build directory.

`SRCS` lists the source files that make up the module. For modules
that are children of the PCI bus, include the generated header files
`device_if.h`, `bus_if.h`, and `pci_if.h` so that `make` knows to
build them on demand.

`SYSDIR` points at the kernel source tree. The `?=` form lets the
operator override it without editing the Makefile.

`bsd.kmod.mk` is the include file that does all the actual work. It
sets up the compiler flags, the linker flags, the dependencies, and
the install rules for kernel modules.

## Common Build Targets

After a Makefile of the form above, the standard make targets are:

- `make` - build the module.
- `make clean` - remove build artifacts.
- `make load` - load the module with `kldload(8)`.
- `make unload` - unload the module with `kldunload(8)`.
- `make install` - install the module to `/boot/modules/`. Most labs
  do not need this; load and unload from the build directory
  instead.

If a lab also has user-space test programs, the lab's own README
will document the additional targets used to build them.

## Loading and Unloading

Every kernel module in this chapter is designed to be loadable and
unloadable cleanly. The expected workflow is:

1. Build the module with `make`.
2. Load it with `sudo kldload ./<module>.ko`.
3. Exercise it through whatever interface the lab provides (sysctls,
   device files, test programs).
4. Unload it with `sudo kldunload <module>`.

The leading `./` on `kldload` is important. It tells `kldload(8)`
to load the module from the current directory rather than from the
system module path.

## Permissions

Loading and unloading kernel modules requires root. Use `sudo` (or
`doas`, if you prefer) to execute the commands.

The sysctls exposed by the modules are readable by anyone by
default. The lab READMEs document which sysctls are read-only and
which take values, where applicable.

## Development VM

All of the labs in this chapter are designed to run safely on a
development virtual machine. None of them deliberately panics the
kernel, but a bug in your variants of the code can. Using a VM means
that a panic costs you a few seconds of reboot time, not a
disrupted host system.

A 4 GB VM with 20 GB of disk and a recent FreeBSD 14.3 install is
more than enough for everything in this chapter.

## Cleaning Up

Each lab includes `make clean` in its Makefile. Running it after you
are done with a lab removes build artifacts and keeps the working
directory tidy.

If you load a module and forget to unload it, `kldstat -v` will show
it in the loaded list. You can unload it from there with
`kldunload <name>`. If a module refuses to unload (the kernel reports
that it is in use), check whether you have a process with an open
file descriptor on a device node the module created.

## Safety Reminder

Read Section 10 of the chapter before attaching any of these
modules to real hardware. The `regprobe` module is read-only by
construction, but the act of attaching any kernel module to an
unknown device is itself an action that should be performed
deliberately, not casually. The chapter's safety advice applies to
the lab work as much as to real reverse engineering.
