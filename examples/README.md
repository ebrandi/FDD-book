# Examples Directory

This directory contains the downloadable companion files for **FreeBSD Device Drivers: From First Steps to Kernel Mastery**. Each folder mirrors the place in the book where the file is introduced, so you can move from a chapter or appendix straight to the source code, Makefiles, scripts, configurations, and supporting artifacts the text asks you to type, edit, compile, run, test, or load.

The companion tree is meant to make the labs and walkthroughs easier to follow. It is not a replacement for the prose. Read the chapter as you work through the matching folder.

## Directory Structure

The tree is organized by part, then by chapter, then by example:

```
examples/
├── README.md                  # This file
├── part-01/                   # Part 1: Foundations: FreeBSD, C, and the Kernel
│   ├── ch02-setting-up-your-lab/
│   ├── ch03-a-gentle-introduction-to-unix/
│   ├── ch04-a-first-look-at-the-c-programming-language/
│   ├── ch05-understanding-c-for-freebsd-kernel-programming/
│   └── ch06-the-anatomy-of-a-freebsd-driver/
├── part-02/                   # Part 2: Building Your First Driver
│   ├── ch07-writing-your-first-driver/
│   ├── ch08-working-with-device-files/
│   ├── ch09-reading-and-writing/
│   └── ch10-handling-io-efficiently/
├── part-03/                   # Part 3: Concurrency and Synchronization
│   ├── ch11-concurrency/
│   ├── ch12-synchronization-mechanisms/
│   ├── ch13-timers-and-delayed-work/
│   ├── ch14-taskqueues-and-deferred-work/
│   └── ch15-more-synchronization/
├── part-04/                   # Part 4: Hardware and Platform-Level Integration
│   ├── ch16-accessing-hardware/
│   ├── ch17-simulating-hardware/
│   ├── ch18-writing-pci-driver/
│   ├── ch19-handling-interrupts/
│   ├── ch20-advanced-interrupts/
│   ├── ch21-dma/
│   └── ch22-power/
├── part-05/                   # Part 5: Debugging, Tools, and Real-World Practices
│   ├── ch23-debug/
│   ├── ch24-integration/
│   └── ch25-advanced/
├── part-06/                   # Part 6: Writing Transport-Specific Drivers
│   ├── ch26-usb-serial/
│   ├── ch27-storage-vfs/
│   └── ch28-network-driver/
├── part-07/                   # Part 7: Mastery Topics: Special Scenarios and Edge Cases
│   ├── ch29-portability/
│   ├── ch30-virtualisation/
│   ├── ch31-security/
│   ├── ch32-fdt-embedded/
│   ├── ch33-performance/
│   ├── ch34-advanced-debugging/
│   ├── ch35-async-io/
│   ├── ch36-reverse-engineering/
│   ├── ch37-upstream/
│   └── ch38-final-thoughts/
└── appendices/
    ├── appendix-a-freebsd-kernel-api-reference/
    ├── appendix-b-algorithms-and-logic-for-systems-programming/
    ├── appendix-c-hardware-concepts-for-driver-developers/
    ├── appendix-d-operating-system-concepts/
    ├── appendix-e-navigating-freebsd-kernel-internals/
    └── appendix-f-benchmarks/
```

Chapter 1 has no companion files, which is why no `ch01-...` folder appears under `part-01/`.

## How Each Chapter Folder Is Organized

Inside a chapter folder, the material is grouped by how the book uses it. The exact mix depends on the chapter, but the recurring patterns are:

* `stageN-name/` for chapters that build a driver in successive stages. Each stage is a self-contained, buildable snapshot, so you can compile and load any stage on its own and compare it to the next one.
* `labNN-name/` for hands-on labs the chapter walks through.
* `challengeNN-name/` for the optional challenge exercises that follow the labs.
* `shared/` for files used by more than one lab in the same chapter, such as common headers or helper scripts.
* `scripts/` for chapter-level helper scripts that automate setup, testing, or measurement.
* `userland/` for user-space test programs that pair with a kernel module from the same chapter.
* `final/` or `stageN-final/` for the final converged version of a chapter-long driver.
* Topical folders, such as `configs/`, `demos/`, `patterns/`, `cheatsheets/`, `cbuf-userland/`, `hwsim-standalone/`, `templates/`, and similar, when a chapter introduces material that does not fit the lab or stage pattern.

## What You Will Find Inside an Example

A typical example folder contains some combination of:

* **Source files** (`.c`, `.h`) for the driver, helper code, or user-space program.
* **A Makefile** that includes `bsd.kmod.mk` for kernel modules, or `bsd.prog.mk` for user-space programs.
* **Scripts** (`.sh`, DTrace `.d`, `awk`, and similar) used during the lab or its measurement steps.
* **Configuration fragments** (loader.conf snippets, sysctl files, FDT overlays) when the lab needs them.
* **Test artifacts** such as fixed input data, expected output, or trace files used for comparison.

## Building and Loading an Example

Each example is intended to be built from its own folder. There is no top-level aggregator Makefile. To build and load a kernel module example, change into the folder shown in the chapter and run `make`:

```sh
# From the repository root
cd examples/part-02/ch07-writing-your-first-driver/stage1-newbus

# Build the module against the running kernel
make

# Load it
sudo kldload ./myfirst.ko

# Inspect it
kldstat | grep myfirst
dmesg | tail

# Unload it when you are done
sudo kldunload myfirst
```

User-space helper programs follow the same pattern, but produce an executable instead of a `.ko`.

## Prerequisites

Before you build any example, make sure your lab system has:

1. A working FreeBSD installation, ideally **FreeBSD 14.3** or later, set up using the procedure in Chapter 2.
2. The matching kernel source tree under `/usr/src/` (the system source, not a separate copy).
3. The base development tools, which are part of the FreeBSD base system: `cc` (Clang), `make`, `kldload(8)`, `kldunload(8)`, `kldstat(8)`, and the standard build infrastructure under `/usr/share/mk/`.
4. Root privileges for `kldload` and `kldunload`. The examples assume you can use `sudo` or are running as `root` in a dedicated lab environment.

Some later chapters add specific prerequisites, such as DTrace, `pmcstat`, a USB device, a virtual machine for PCI passthrough, or an FDT-capable board. Those are listed in the chapter that introduces them.

## Mapping Folders to the Book

The folders below match the canonical chapter sequence.

### Part 1: Foundations: FreeBSD, C, and the Kernel

* Chapter 1, *Introduction: From Curiosity to Contribution*, has no companion files.
* `part-01/ch02-setting-up-your-lab/` — preparing the FreeBSD lab system, with starter configuration files and the first hello-world module.
* `part-01/ch03-a-gentle-introduction-to-unix/` — UNIX fundamentals, shell scripting, automation, and portability examples used throughout the book.
* `part-01/ch04-a-first-look-at-the-c-programming-language/` — early C exercises, multi-file programs, a driver-skeleton lab, and crash and memory labs.
* `part-01/ch05-understanding-c-for-freebsd-kernel-programming/` — kernel-oriented C demos and a series of safety-focused labs (memory, echo, logging, error handling).
* `part-01/ch06-the-anatomy-of-a-freebsd-driver/` — the first end-to-end module skeletons, common driver patterns, and supporting scripts.

### Part 2: Building Your First Driver

* `part-02/ch07-writing-your-first-driver/` — staged construction of the first Newbus pseudo-device (`stage0-scaffold`, `stage1-newbus`, `stage2-final`) plus labs and helper scripts.
* `part-02/ch08-working-with-device-files/` — staged refinements of `/dev` node creation (structured names, aliases, per-handle state, multiple nodes, safe destroy with drain), with `devfs` and `jail` materials and a userland tester.
* `part-02/ch09-reading-and-writing/` — staged read and write implementations (static message, generic `read`/`write`, echo) and a matching userland program.
* `part-02/ch10-handling-io-efficiently/` — circular buffers, blocking I/O, a `poll(2)` refactor, and an `mmap(2)` stage, with userland helpers including a circular-buffer tester.

### Part 3: Concurrency and Synchronization

* `part-03/ch11-concurrency/` — race demonstrations, concurrent-safe versions, and `KASSERT`-instrumented variants, with a userland exerciser.
* `part-03/ch12-synchronization-mechanisms/` — condition variables, bounded reads, `sx` lock based configuration, and a final converged module.
* `part-03/ch13-timers-and-delayed-work/` — heartbeat, watchdog, tick-source, and final variants of a timer-driven driver.
* `part-03/ch14-taskqueues-and-deferred-work/` — first task, private taskqueue, coalescing, and final stages, with a `labs/` folder.
* `part-03/ch15-more-synchronization/` — semaphores for writers, a stats cache, interruptible waits, and a final consolidated example.

### Part 4: Hardware and Platform-Level Integration

* `part-04/ch16-accessing-hardware/` — register-map, `bus_space(9)`, synchronized, and final stages, plus a standalone hardware simulator (`hwsim-standalone/`) and labs.
* `part-04/ch17-simulating-hardware/` — backend, integration, timing, faults, and final stages of a richer simulator (`hwsim2-standalone/`) and labs.
* `part-04/ch18-writing-pci-driver/` — probe, BAR mapping, integrated, and final stages for a PCI driver, with labs.
* `part-04/ch19-handling-interrupts/` — registration, handler, simulation, and final stages of an interrupt-driven driver.
* `part-04/ch20-advanced-interrupts/` — single MSI, multi-vector MSI, MSI-X, and final stages.
* `part-04/ch21-dma/` — allocation, polling, interrupt-driven, and final stages of a DMA-capable driver.
* `part-04/ch22-power/` — skeleton, quiesce, restore, and final stages of a power-management aware driver.

### Part 5: Debugging, Tools, and Real-World Practices

* `part-05/ch23-debug/` — logging, SDT probes, and refactor stages, plus debugging labs.
* `part-05/ch24-integration/` — `devfs` integration, `ioctl(2)`, and `sysctl(9)` stages, plus integration labs.
* `part-05/ch25-advanced/` — ten focused labs covering log floods, errno audits, tunable reboots, failure injection, shutdown handlers, multi-cycle stress runs, capability negotiation, sysctl validation, log audits, and a compatibility matrix.

### Part 6: Writing Transport-Specific Drivers

* `part-06/ch26-usb-serial/` — nine USB and serial labs, from exploring the USB stack to a `ucom` skeleton and TTY troubleshooting, with a `shared/` folder for environment checks.
* `part-06/ch27-storage-vfs/` — twelve labs covering `geom(4)` exploration, a storage skeleton, BIO handling, UFS mount, persistence, safe unmount, DTrace, and stress testing, plus ten challenge exercises building toward an `mdconfig`-like tool.
* `part-06/ch28-network-driver/` — seven labs from skeleton to BPF and detach handling, plus six challenges including iperf3, sysctls, and netgraph.

### Part 7: Mastery Topics: Special Scenarios and Edge Cases

* `part-07/ch29-portability/` — twelve labs evolving a driver from a monolithic implementation through accessor abstractions, backend split, simulation, conditional compilation, endianness handling, a portability matrix, userland testing, sysctls, runtime detection, and CI, ending in a final consolidated build.
* `part-07/ch30-virtualisation/` — labs for hypervisor detection, jail device handling, VNET, PCI passthrough, plus a `vtedu/` virtio-style educational driver and a `shared/` folder.
* `part-07/ch31-security/` — paired unsafe and fixed labs covering bounds checking, information leaks, privilege handling, rate limiting, detach safety, and secure design.
* `part-07/ch32-fdt-embedded/` — FDT hello-world, overlays, debugging a broken tree, and two `edled` driver variants, with a shared support folder.
* `part-07/ch33-performance/` — performance counters, DTrace scripts, `pmcstat`, cache-aligned data, interrupt taskqueue handoff, and a final v2.3 consolidated build.
* `part-07/ch34-advanced-debugging/` — `KASSERT`, `kgdb`, debug kernel, tracing, `memguard`, remote `gdb`, and a `lab-challenges/` folder.
* `part-07/ch35-async-io/` — synchronous baseline, `poll(2)`, `kqueue(2)`, `SIGIO`, an event queue, a v2.5 async build, and a stress test.
* `part-07/ch36-reverse-engineering/` — identification, capture, register-map building, mock device construction, safe wrapper, pseudo-datasheet authoring, plus open-ended challenges and a `exercise-your-own/` folder.
* `part-07/ch37-upstream/` — a sample driver and sample man page suitable as templates when preparing a contribution, plus helper scripts.
* `part-07/ch38-final-thoughts/` — a template driver, project templates, and closing scripts to seed your own work after the book.

### Appendices

* `appendices/appendix-a-freebsd-kernel-api-reference/` — kernel API reference material with `cheatsheets/` and `scripts/`.
* `appendices/appendix-b-algorithms-and-logic-for-systems-programming/` — algorithm and data-structure cheatsheets relevant to driver work.
* `appendices/appendix-c-hardware-concepts-for-driver-developers/` — hardware-side cheatsheets used when reading datasheets and reasoning about buses, registers, and timing.
* `appendices/appendix-d-operating-system-concepts/` — operating-system concept cheatsheets that complement the kernel material in the main chapters.
* `appendices/appendix-e-navigating-freebsd-kernel-internals/` — cheatsheets for moving around the FreeBSD source tree, finding subsystems, and reading kernel code effectively.
* `appendices/appendix-f-benchmarks/` — benchmark harness and results, organized by area: `dtrace/`, `iflib/`, `sync/`, and `timecounter/`.

## Version Compatibility

The companion files target:

* **FreeBSD 14.3** as the primary reference release. Most examples build and load unchanged on FreeBSD 14.x, and many work on later releases without modification.
* **amd64** as the primary architecture, with several chapters in Part 7 also exercised on **arm64** for portability and FDT material.
* The base toolchain shipped with FreeBSD (Clang and `bmake`).

## Troubleshooting

The most common issues, and where to look first:

1. **Build errors** usually mean the kernel source is not present at `/usr/src/` or does not match the running kernel. Reinstall the matching `src.txz` for your release.
2. **Module load failures** are best diagnosed with `dmesg | tail` immediately after the failed `kldload`. The driver almost always logs why it refused to attach.
3. **Permission errors** mean you are not running `kldload`, `kldunload`, or `sysctl` writes as root. Use `sudo` or work in a dedicated lab VM.
4. **Architecture mismatches** appear when a module was built on a different host. Rebuild from the same source tree as the running kernel.

For deeper guidance, read the troubleshooting sections in Chapter 23 (*Debugging and Tracing*) and Chapter 34 (*Advanced Debugging Techniques*).

## Contributing

If you spot a problem with an example, or you would like to suggest an improvement:

1. Reproduce the issue on a clean FreeBSD lab system using the same release the book targets.
2. Capture the relevant output, such as `dmesg`, `kldstat`, build logs, or DTrace traces.
3. Open an issue or pull request describing what you observed, what you expected, and the exact steps to reproduce it.
4. Keep changes consistent with the chapter's didactic intent. The companion files exist to support the prose, not to evolve independently of it.

## License

The companion files are distributed under the same license as the main project. See the top-level `LICENSE` file for the full terms.

---

These files are intended for learning and lab work on a dedicated FreeBSD system. Always test in a virtual machine or other isolated environment before running unfamiliar driver code on hardware you care about.
