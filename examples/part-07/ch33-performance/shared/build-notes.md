# Shared Build Notes

These notes apply to every lab in this chapter.

## Prerequisites

Each lab assumes:

- FreeBSD 14.3 or later.
- `/usr/src/` is populated (run `svnlite co` or use `pkg install
  src-quarterly` or a release tarball).
- Standard build tools available (`make(1)`, `cc(1)`). These are in
  the base system on FreeBSD.

## Building a Kernel Module

The labs use the standard FreeBSD kernel module build system.

```
$ make clean
$ make
```

This produces a `.ko` file in the current directory. The file name
matches the `KMOD` variable in the `Makefile`.

## Loading and Unloading

```
$ sudo kldload ./perfdemo.ko      # load
$ sudo kldunload perfdemo         # unload (no .ko suffix on unload)
$ kldstat | grep perfdemo         # verify
```

Always unload a test module before rebuilding. A stale module in the
kernel confuses the next build.

## Running the Workload

Most labs use `dd(1)` as a simple read workload:

```
$ dd if=/dev/perfdemo of=/dev/null bs=4096 count=100000
```

For concurrent loads, run multiple `dd` processes in parallel:

```
$ for i in 1 2 3 4; do dd if=/dev/perfdemo of=/dev/null bs=4096 \
    count=25000 & done; wait
```

## Reading Counters

```
$ sysctl hw.perfdemo
```

The exact names depend on the lab; each lab's README lists them.

## Measuring Latency With `time(1)`

```
$ time dd if=/dev/perfdemo of=/dev/null bs=4096 count=100000
```

## Measuring Elapsed Time Inside a Script

```
#!/bin/sh
start=$(date +%s.%N)
dd if=/dev/perfdemo of=/dev/null bs=4096 count=100000 2>/dev/null
end=$(date +%s.%N)
echo "elapsed: $(echo $end - $start | bc)s"
```

## Cleaning Up

Always unload test modules when you are finished:

```
$ sudo kldunload perfdemo
```
