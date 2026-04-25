# Shared Build Notes

These notes apply to every lab in this chapter.

## Prerequisites

Each lab assumes:

- FreeBSD 14.3 or later.
- `/usr/src/` is populated (clone from git, or use
  `pkg install src-quarterly`, or extract the release source tarball).
- Standard build tools available (`make(1)`, `cc(1)`). These are in
  the base system on FreeBSD.
- A dump device configured (use `dumpon -l` to check).
- For Lab 3, Lab 5, and some challenges: a kernel built from
  `GENERIC-DEBUG` with appropriate options.

## Building a Debug Kernel

Several labs assume you are running a debug kernel. The minimum
configuration is `GENERIC-DEBUG`, which lives at
`/usr/src/sys/amd64/conf/GENERIC-DEBUG` (or the equivalent path for
your architecture).

```
# cd /usr/src
# make buildkernel KERNCONF=GENERIC-DEBUG
# make installkernel KERNCONF=GENERIC-DEBUG
# shutdown -r now
```

For Lab 5, add `options DEBUG_MEMGUARD` to your kernel config before
building.

## Building a Kernel Module

The labs use the standard FreeBSD kernel module build system.

```
$ make clean
$ make
```

This produces `bugdemo.ko` in the current directory.

## Loading and Unloading

```
$ sudo kldload ./bugdemo.ko       # load
$ sudo kldunload bugdemo          # unload (no .ko suffix on unload)
$ kldstat | grep bugdemo          # verify
```

Always unload a test module before rebuilding. A stale module in the
kernel confuses the next build.

## Running the Test Program

Each lab includes a small user-space program, `bugdemo_test`, that
exercises the driver. Build and run:

```
$ make test
$ ./bugdemo_test <subcommand>
```

The available subcommands vary by lab and are documented in each
lab's README.

## Configuring Dumps

Before running a lab that deliberately panics, confirm a dump device
is configured:

```
# dumpon -l
```

If nothing is listed, configure your swap partition:

```
# dumpon /dev/ada0p3
```

Persist in `/etc/rc.conf`:

```
dumpdev="/dev/ada0p3"
savecore_enable="YES"
```

## Reading a Dump

After a panic and reboot, dumps appear in `/var/crash/`:

```
# ls -l /var/crash/
# cat /var/crash/info.0
# kgdb /boot/kernel/kernel /var/crash/vmcore.0
```

## Cleaning Up

Always unload test modules when you are finished:

```
$ sudo kldunload bugdemo
```

If a deliberate panic has rebooted your VM, the module is of course
gone. Kernel dumps remain in `/var/crash/` until you remove them.

## Enabling INVARIANTS for a Single Module

If you are running a release kernel but want to test a module built
with `INVARIANTS`, make sure the kernel has `INVARIANT_SUPPORT` at
minimum, then build the module with:

```
$ CFLAGS="-DINVARIANTS -DINVARIANT_SUPPORT -DWITNESS" make
```

The module will use INVARIANTS-mode macros while the kernel itself
stays in release mode. This is a reasonable compromise for testing
assertions without a full debug kernel rebuild.

## Enabling DTrace on a Module

To let DTrace see structure fields by name in your module, build with
CTF information:

```
$ WITH_CTF=1 make
```

This adds CTF data to `bugdemo.ko`, which DTrace uses to decode
structures.
