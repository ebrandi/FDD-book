# Shared Build Notes

These notes apply to every lab in this chapter.

## Prerequisites

Each lab assumes:

- FreeBSD 14.3 or later.
- `/usr/src/` is populated (clone from git, or use
  `pkg install src-quarterly`, or extract the release source tarball).
- Standard build tools available (`make(1)`, `cc(1)`). These are in
  the base system on FreeBSD.
- Root privileges for `kldload` and `kldunload` (or membership in the
  `operator` group with appropriate `devfs(8)` rules).

## Building a Kernel Module

The labs use the standard FreeBSD kernel module build system.

```
$ make clean
$ make
```

This produces `evdemo.ko` in the current directory.

## Loading and Unloading

```
$ sudo kldload ./evdemo.ko       # load
$ sudo kldunload evdemo          # unload (no .ko suffix on unload)
$ kldstat | grep evdemo          # verify loaded
```

Always unload a previous build before reloading. A stale module in
the kernel will make the next `kldload` fail with "file exists."

## Running the Test Programs

Each lab includes one or more user-space programs that exercise the
driver. Build them with:

```
$ make test
```

Then run them as root so they can open `/dev/evdemo`:

```
$ sudo ./evdemo_test
$ sudo ./evdemo_test_poll
$ sudo ./evdemo_test_kqueue
$ sudo ./evdemo_test_sigio
```

The device node is created with mode 0600 and owner root:wheel, so
the test programs need elevated privileges to open it.

## Triggering Synthetic Events

Every `evdemo` variant exposes a `trigger` sysctl that posts a
synthetic event into the queue:

```
$ sudo sysctl dev.evdemo.trigger=1
```

Starting with Lab 5, a `burst` sysctl posts a batch of events at
once:

```
$ sudo sysctl dev.evdemo.burst=100
```

Use these sysctls to exercise the driver without having to write
custom test programs.

## Observing Queue State

Labs 5, 6, and 7 expose queue metrics through sysctl:

```
$ sysctl dev.evdemo.stats
```

Watch these counters in a loop to see the queue behaviour at
runtime:

```
$ while :; do sysctl dev.evdemo.stats; sleep 1; done
```

## Enabling INVARIANTS for a Single Module

If you are running a release kernel but want to test a module built
with `INVARIANTS`, make sure the kernel has `INVARIANT_SUPPORT` at
minimum, then build the module with:

```
$ CFLAGS="-DINVARIANTS -DINVARIANT_SUPPORT -DWITNESS" make
```

This enables the `mtx_assert()` calls inside the driver so that any
missing locks show up immediately rather than as silent races.

## Enabling DTrace on a Module

To let DTrace see structure fields by name in your module, build with
CTF information:

```
$ WITH_CTF=1 make
```

This adds CTF data to `evdemo.ko`, which DTrace uses to decode
structures.

## Troubleshooting the Build

If `make` fails with "bsd.kmod.mk: not found", the source tree is
either missing or incomplete. Install `src-quarterly` or clone
`/usr/src/` from the FreeBSD git tree.

If the build succeeds but `kldload` fails with "symbol not found,"
the kernel is older than the module expects. Rebuild the module
against the running kernel's source.
