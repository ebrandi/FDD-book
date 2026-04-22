---
title: "Writing Your First Driver"
description: "A hands-on walkthrough that builds a minimal FreeBSD driver with clean lifecycle discipline."
partNumber: 2
partName: "Building Your First Driver"
chapter: 7
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 600
---

# Writing Your First Driver

## Reader Guidance & Outcomes

Welcome to Part 2. If Part 1 was your foundation, learning the environment, the language, and the architecture, **Part 2 is where you build**. This chapter marks the moment you stop reading about drivers and start writing one.

But let's be clear about what we're building and, just as importantly, what we're not building yet. This chapter follows a **discipline-first** approach: you'll write a minimal driver that attaches cleanly, logs properly, creates a simple user surface, and detaches without leaks. No fancy I/O, no hardware register access, no interrupt handling. Those come later, once the discipline is second nature.

### What You'll Build

By the time you move on to the next chapter, you will have a working FreeBSD 14.3 driver called `myfirst` that:

- **Attaches as a pseudo-device** using the Newbus framework
- **Creates a `/dev/myfirst0` node** (stubbed, read-only preview)
- **Exposes a read-only sysctl** showing basic runtime state
- **Logs lifecycle events** cleanly with `device_printf()`
- **Handles errors** with a single-label unwind pattern
- **Detaches cleanly** with no resource leaks or dangling pointers

This driver won't do anything exciting yet. It won't read from hardware, it won't handle interrupts, and it won't process packets or blocks. What it *will* do is demonstrate **lifecycle discipline** the foundation every production driver depends on.

### What You'll **Not** Build (Yet)

This chapter deliberately defers several important topics so you can master structure before complexity:

- **Full I/O semantics**: `read(2)` and `write(2)` will be stubbed. Real read and write paths arrive in Chapter 9, after Chapter 8 has covered device-file policy and userland visibility.
- **Hardware interaction**: No register access, no DMA, no interrupts. Those are covered in **Part 4** when you have a solid foundation.
- **PCI/USB/ACPI specifics**: We use a pseudo-device (no bus dependency) in this chapter. Bus-specific attachment patterns appear in Part 4 (PCI, interrupts, DMA) and Part 6 (USB, storage, network).
- **Locking and concurrency**: You'll see a mutex in the softc, but we won't exercise complex concurrent paths until **Part 3**.
- **Advanced sysctls**: Just one read-only node for now. Larger sysctl trees, write handlers, and tunables come back in Part 5.

**Why this matters:** Trying to learn everything at once leads to confusion. By keeping the scope narrow, you'll understand *why* each piece exists before adding the next layer.

### Estimated Time Investment

- **Reading only**: 2-3 hours to absorb concepts and code walkthroughs
- **Reading + typing examples**: 4-5 hours if you type the driver code yourself
- **Reading + all four labs**: 5-7 hours, including build, test, and verification cycles
- **Optional challenges**: Add 2-3 hours for deep-dive exercises

**Recommended pace:** Break this into two or three sessions. Session 1 through the scaffold and Newbus basics, session 2 through logging and error handling, session 3 for labs and smoke tests.

### Prerequisites

Before starting, ensure you have:

- **FreeBSD 14.3** running in your lab (VM or bare metal)
- **Chapters 1-6 completed** (especially Chapter 2's lab setup and Chapter 6's anatomy tour)
- **`/usr/src` installed** with FreeBSD 14.3 sources matching your running kernel
- **Basic C fluency** from Chapter 4
- **Kernel programming awareness** from Chapter 5

Check your kernel version:

```bash
% freebsd-version -k
14.3-RELEASE
```

If this doesn't match, revisit Chapter 2's setup guidance.

### Learning Outcomes

By completing this chapter, you will be able to:

- Scaffold a minimal FreeBSD driver from scratch
- Implement and explain probe/attach/detach lifecycle methods
- Define and use a driver softc structure safely
- Create and destroy `/dev` nodes using `make_dev_s()`
- Add a read-only sysctl for observability
- Handle errors with disciplined unwinding (single fail: label pattern)
- Build, load, test, and unload your driver reliably
- Identify and fix common beginner mistakes (resource leaks, null dereferences, missing cleanup)

### Success Criteria

You'll know you've succeeded when:

- `kldload ./myfirst.ko` completes without errors
- `dmesg -a` shows your attach message
- `ls -l /dev/myfirst0` displays your device node
- `sysctl dev.myfirst.0` returns your driver's state
- `kldunload myfirst` cleans up without leaks or panics
- You can repeat the load/unload cycle reliably
- A simulated attach failure unwinds cleanly (negative-path test)

### Where This Chapter Fits

You're entering **Part 2 - Building Your First Driver**, the bridge from theory to practice:

- **Chapter 7 (this chapter)**: scaffold a minimal driver with clean attach/detach
- **Chapter 8**: wire up real `open()`, `close()`, and device-file semantics
- **Chapter 9**: implement basic `read()` and `write()` paths
- **Chapter 10**: handle buffering, blocking, and poll/select

Each chapter builds on the previous, adding one layer of functionality at a time.

### A Note on "Hello World" Versus "Hello Production"

You've probably seen "hello world" kernel modules before: a `MOD_LOAD` event handler that prints a message. That's fine for checking if the build system works, but it's not a driver. It doesn't attach to anything, doesn't create user surfaces, and teaches almost nothing about lifecycle discipline.

This chapter's `myfirst` driver is different. It's still minimal, but it follows the patterns you'll see in every production FreeBSD driver:

- Registers with Newbus
- Implements probe/attach/detach correctly
- Manages resources (even if they're trivial)
- Cleans up reliably

Think of `myfirst` as **hello production**, not hello world. The jump from toy to tool starts here.

### How to Use This Chapter

1. **Read sequentially**: Each section builds on the previous. Don't skip ahead.
2. **Type the code yourself**: Muscle memory matters. Copying snippets is fine, but typing cements patterns.
3. **Complete the labs**: They're checkpoints, not optional extras. Each lab validates understanding before moving forward.
4. **Use the capstone checklist**: Before declaring victory, run through the smoke-test checklist (near the end of this chapter). It catches common mistakes.
5. **Keep a log**: Record what worked, what failed, and what you learned. Future-you will thank you.

### A Word About Mistakes

You *will* encounter errors. You'll forget to initialize a pointer, you'll skip a cleanup step, you'll typo a function name. That's expected and **healthy**. Every error is a chance to practice debugging, reading logs, and understanding cause and effect.

When something breaks:

- Read the full error message. FreeBSD's kernel messages are detailed.
- Check `dmesg -a` for lifecycle events.
- Use the troubleshooting decision tree (section later in this chapter).
- Revisit the relevant section and compare your code to the examples.

Don't rush past errors. They're teaching moments.

### Let's Begin

You've completed the foundation. You've walked through real drivers in Chapter 6. Now it's time to **build your own**. Let's start with the project scaffold.



## Project Scaffold (KLD skeleton)

Every driver begins with a scaffold, a bare-bones structure that compiles, loads, and unloads without doing much of anything. Think of this as the frame of a house: walls, doors, and furniture come later. Right now, we're building the foundation and skeleton that holds everything together.

In this section, you'll create a minimal FreeBSD 14.3 driver project from scratch. By the end, you'll have:

- A clean directory structure
- A simple Makefile
- A `.c` file with the absolute minimum lifecycle code
- A working build that produces a `myfirst.ko` module

This scaffold is **intentionally boring**. It won't create `/dev` nodes yet, it won't implement sysctls, and it won't do any real work. But it will teach you the build cycle, the basic structure, and the discipline of clean entry and exit. Master this, and everything else is just adding layers.

### Directory Layout

Let's create a workspace for your driver. Convention in the FreeBSD source tree is to keep drivers under `/usr/src/sys/dev/<drivername>`, but for your first driver, we'll work in your home directory. This keeps your experiments isolated and makes rebuilding simple.

Create the structure:

```bash
% mkdir -p ~/drivers/myfirst
% cd ~/drivers/myfirst
```

Your working directory will contain:

```text
~/drivers/myfirst/
├── myfirst.c      # Driver source code
└── Makefile       # Build instructions
```

That's it. FreeBSD's kernel module build system (`bsd.kmod.mk`) handles all the complexity (compiler flags, include paths, linking, etc.) for you.

**Why this organization?**

- **Single directory**: Keeps everything together, easy to clean (`rm -rf ~/drivers/myfirst`).
- **Named after the driver**: When you have multiple projects, you know what `~/drivers/myfirst` contains.
- **Matches tree patterns**: Real FreeBSD drivers in `/usr/src/sys/dev/` follow the same "one directory per driver" approach.

### The Minimal Makefile

FreeBSD's build system is remarkably simple for kernel modules. Create `Makefile` with these three lines:

```makefile
# Makefile for myfirst driver

KMOD=    myfirst
SRCS=    myfirst.c

.include <bsd.kmod.mk>
```

**Line by line:**

- `KMOD= myfirst` - Declares the module name. This will produce `myfirst.ko`.
- `SRCS= myfirst.c` - Lists source files. We only have one for now.
- `.include <bsd.kmod.mk>` - Pulls in FreeBSD's kernel module build rules. This single line replaces hundreds of lines of manual makefile logic.

**Important:** The indentation before `.include` is a **tab character**, not spaces. If you use spaces, `make` will fail with a cryptic error. (Most editors can be configured to insert tabs when you press the Tab key.)

**What `bsd.kmod.mk` provides:**

- Correct compiler flags for kernel code (`-D_KERNEL`, `-ffreestanding`, etc.)
- Include paths (`-I/usr/src/sys`, `-I/usr/src/sys/dev`, etc.)
- Linking rules for creating `.ko` files
- Standard targets: `make`, `make clean`, `make install`, etc.

You don't need to understand the internal details. Just know that `.include <bsd.kmod.mk>` gives you a working build system for free.

**Test the Makefile:**

Before writing any code, test the build setup:

```bash
% make clean
% ls
Makefile
```

Right now, `make clean` does almost nothing (no files to delete yet), but it confirms the Makefile syntax is valid.

### The Bare-Bones `myfirst.c`

Now create `myfirst.c`, the actual driver source. This first version is **minimal by design**: it compiles, loads, and unloads, but doesn't create devices, doesn't handle I/O, and doesn't allocate resources.

Here's the skeleton:

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 Your Name
 * All rights reserved.
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>

/*
 * Module load/unload event handler.
 *
 * This function is called when the module is loaded (MOD_LOAD)
 * and unloaded (MOD_UNLOAD). For now, we just print messages.
 */
static int
myfirst_loader(module_t mod, int what, void *arg)
{
        int error = 0;

        switch (what) {
        case MOD_LOAD:
                printf("myfirst: driver loaded\n");
                break;
        case MOD_UNLOAD:
                printf("myfirst: driver unloaded\n");
                break;
        default:
                error = EOPNOTSUPP;
                break;
        }

        return (error);
}

/*
 * Module declaration.
 *
 * This ties the module name "myfirst" to the loader function above.
 */
static moduledata_t myfirst_mod = {
        "myfirst",              /* module name */
        myfirst_loader,         /* event handler */
        NULL                    /* extra arg (unused here) */
};

/*
 * DECLARE_MODULE registers this module with the kernel.
 *
 * Parameters:
 *   - module name: myfirst
 *   - moduledata: myfirst_mod
 *   - subsystem: SI_SUB_DRIVERS (driver subsystem)
 *   - order: SI_ORDER_MIDDLE (standard priority)
 */
DECLARE_MODULE(myfirst, myfirst_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(myfirst, 1);
```

**What this code does:**

- **Includes**: Pull in kernel headers for module infrastructure and logging.
- **`myfirst_loader()`**: Handles module lifecycle events. Right now, just MOD_LOAD and MOD_UNLOAD.
- **`moduledata_t`**: Connects the module name to the loader function.
- **`DECLARE_MODULE()`**: Registers the module with the kernel. This is what makes `kldload` recognize your module.
- **`MODULE_VERSION()`**: Stamps the module with version 1 (increment this if you change the exported ABI in the future).

**What this code doesn't do (yet):**

- Doesn't create any devices
- Doesn't call `make_dev()`
- Doesn't register with Newbus
- Doesn't allocate memory or resources

This is **load/unload only**, the absolute minimum to prove the build system works.

### Build and Test the Scaffold

Let's compile and load this minimal module:

**1. Build:**

```bash
% make
machine -> /usr/src/sys/amd64/include
x86 -> /usr/src/sys/x86/include
i386 -> /usr/src/sys/i386/include
touch opt_global.h
Warning: Object directory not changed from original /usr/home/youruser/project/myfirst
cc  -O2 -pipe  -fno-strict-aliasing -Werror -D_KERNEL -DKLD_MODULE -nostdinc   -include /usr/home/youruser/project/myfirst/opt_global.h -I. -I/usr/src/sys -I/usr/src/sys/contrib/ck/include -fno-common  -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fdebug-prefix-map=./machine=/usr/src/sys/amd64/include -fdebug-prefix-map=./x86=/usr/src/sys/x86/include -fdebug-prefix-map=./i386=/usr/src/sys/i386/include    -MD  -MF.depend.myfirst.o -MTmyfirst.o -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -msoft-float  -fno-asynchronous-unwind-tables -ffreestanding -fwrapv -fstack-protector  -Wall -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wcast-qual -Wundef -Wno-pointer-sign -D__printf__=__freebsd_kprintf__ -Wmissing-include-dirs -fdiagnostics-show-option -Wno-unknown-pragmas -Wswitch -Wno-error=tautological-compare -Wno-error=empty-body -Wno-error=parentheses-equality -Wno-error=unused-function -Wno-error=pointer-sign -Wno-error=shift-negative-value -Wno-address-of-packed-member -Wno-format-zero-length   -mno-aes -mno-avx  -std=gnu17 -c myfirst.c -o myfirst.o
ld -m elf_x86_64_fbsd -warn-common --build-id=sha1 -T /usr/src/sys/conf/ldscript.kmod.amd64 -r  -o myfirst.ko myfirst.o
:> export_syms
awk -f /usr/src/sys/conf/kmod_syms.awk myfirst.ko  export_syms | xargs -J % objcopy % myfirst.ko
objcopy --strip-debug myfirst.ko
```

You'll see compiler output. As long as it ends with `myfirst.ko` being created and no errors, you're good.

**2. Verify the build output:**

```bash
% ls -l myfirst.ko
-rw-r--r--  1 youruser youruser 11592 Nov  7 00:15 myfirst.ko
```

(File size will vary based on compiler and architecture.)

**3. Load the module:**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 2
myfirst: driver loaded
```

**4. Check it's loaded:**

```bash
% kldstat | grep myfirst
 6    1 0xffffffff82a38000     20b8 myfirst.ko
```

Your module is now part of the running kernel.

**5. Unload the module:**

```bash
% sudo kldunload myfirst
% dmesg | tail -n 2
myfirst: driver unloaded
```

**6. Confirm it's gone:**

```bash
% kldstat | grep myfirst
(no output)
```

Perfect. Your scaffold works.

### What Just Happened?

Let's trace the flow step by step:

1. **Build:** `make` invoked the FreeBSD kernel module build system, which compiled `myfirst.c` with kernel flags and linked it into `myfirst.ko`.
2. **Load:** `kldload` read `myfirst.ko`, linked it into the running kernel, and called your `myfirst_loader()` function with `MOD_LOAD`.
3. **Log:** Your `printf()` wrote "myfirst: driver loaded" to the kernel message buffer.
4. **Unload:** `kldunload` called your loader with `MOD_UNLOAD`, you printed a message, then the kernel removed your code from memory.

**Key insight:** This isn't a Newbus driver yet. There's no `probe()`, no `attach()`, no devices. It's just a module that loads and unloads. Think of it as **stage 0**: proving the build system works before adding complexity.

### Troubleshooting Common Scaffold Issues

**1. Problem:** `make` fails with "missing separator"

**Cause:** Your Makefile uses spaces instead of tabs before `.include`.

**Fix:** Replace the leading spaces with a tab character.

**2. Problem:** `kldload` says "Exec format error"

**Cause:** Mismatch between your kernel version and `/usr/src` version.

**Fix:** Verify `freebsd-version -k` matches your source tree. Rebuild your kernel or re-clone `/usr/src` for the correct version.

**3. Problem:** Module loads but no message in `dmesg`

**Cause:** Kernel message buffer might have scrolled, or `printf()` is being rate-limited.

**Fix:** Use `dmesg -a` to see all messages, including older ones. Also check `sysctl kern.msgbuf_show_timestamp`.

**4. Problem:** `kldunload` says "module busy"

**Cause:** Something is still using your module (very unlikely with this minimal scaffold).

**Fix:** Not applicable here, but later you'll see this if device nodes are still open or resources aren't released.

### Clean Build Practices

As you iterate on your driver, adopt these habits early:

**1. Always clean before rebuilding:**

```bash
% make clean
% make
```

This ensures stale object files don't contaminate your build.

**2. Unload before rebuilding:**

```bash
% sudo kldunload myfirst 2>/dev/null || true
% make clean && make
```

If the module isn't loaded, `kldunload` fails harmlessly. The `|| true` prevents the shell from stopping.

**3. Use a rebuild script:**

Create `~/drivers/myfirst/rebuild.sh`:

```bash
#!/bin/sh
#
# FreeBSD kernel module rebuild script
# Usage: ./rebuild_module.sh <module_name>
#

set -e

# Configuration
MODULE_NAME="${1}"

# Colors for output (if terminal supports it)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m' # No Color
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
fi

# Helper functions
print_step() {
    printf "${BLUE}==>${NC} ${1}\n"
}

print_success() {
    printf "${GREEN}✓${NC} ${1}\n"
}

print_error() {
    printf "${RED}✗${NC} ${1}\n" >&2
}

print_warning() {
    printf "${YELLOW}!${NC} ${1}\n"
}

check_root() {
    if [ "$(id -u)" -ne 0 ]; then
        print_error "This script must be run as root or with sudo"
        exit 1
    fi
}

is_module_loaded() {
    kldstat -q -n "${1}" 2>/dev/null
}

# Validate arguments
if [ -z "${MODULE_NAME}" ]; then
    print_error "Usage: $0 <module_name>"
    exit 1
fi

# Validate source file exists
if [ ! -f "${MODULE_NAME}.c" ]; then
    print_error "Source file '${MODULE_NAME}.c' not found in current directory"
    exit 1
fi

# Check if we have root privileges
check_root

# Check if Makefile exists
if [ ! -f "Makefile" ]; then
    print_error "Makefile not found in current directory"
    exit 1
fi

# Step 1: Unload module if loaded
print_step "Checking if module '${MODULE_NAME}' is loaded..."
if is_module_loaded "${MODULE_NAME}"; then
    print_warning "Module is loaded, unloading..."
    
    # Capture dmesg state before unload
    DMESG_BEFORE_UNLOAD=$(dmesg | wc -l)
    
    if kldunload "${MODULE_NAME}" 2>/dev/null; then
        print_success "Module unloaded successfully"
    else
        print_error "Failed to unload module"
        exit 1
    fi
    
    # Verify unload
    sleep 1
    if is_module_loaded "${MODULE_NAME}"; then
        print_error "Module still loaded after unload attempt"
        exit 1
    fi
    print_success "Verified: module removed from memory"
    
    # Check dmesg for unload messages
    DMESG_AFTER_UNLOAD=$(dmesg | wc -l)
    DMESG_UNLOAD_NEW=$((DMESG_AFTER_UNLOAD - DMESG_BEFORE_UNLOAD))
    
    if [ ${DMESG_UNLOAD_NEW} -gt 0 ]; then
        echo
        print_step "Kernel messages from unload:"
        dmesg | tail -n ${DMESG_UNLOAD_NEW}
        echo
    fi
else
    print_success "Module not loaded, proceeding..."
fi

# Step 2: Clean build artifacts
print_step "Cleaning build artifacts..."
if make clean; then
    print_success "Clean completed"
else
    print_error "Clean failed"
    exit 1
fi

# Step 3: Build module
print_step "Building module..."
if make; then
    print_success "Build completed"
else
    print_error "Build failed"
    exit 1
fi

# Verify module file exists
if [ ! -f "./${MODULE_NAME}.ko" ]; then
    print_error "Module file './${MODULE_NAME}.ko' not found after build"
    exit 1
fi

# Step 4: Load module
print_step "Loading module..."
DMESG_BEFORE=$(dmesg | wc -l)

if kldload "./${MODULE_NAME}.ko"; then
    print_success "Module load command executed"
else
    print_error "Failed to load module"
    exit 1
fi

# Step 5: Verify module is loaded
sleep 1
print_step "Verifying module load..."

if is_module_loaded "${MODULE_NAME}"; then
    print_success "Module is loaded in kernel"
    
    # Show module info
    echo
    kldstat | head -n 1
    kldstat | grep "${MODULE_NAME}"
else
    print_error "Module not found in kldstat output"
    exit 1
fi

# Step 6: Check kernel messages
echo
print_step "Recent kernel messages from load:"
DMESG_AFTER=$(dmesg | wc -l)
DMESG_NEW=$((DMESG_AFTER - DMESG_BEFORE))

if [ ${DMESG_NEW} -gt 0 ]; then
    dmesg | tail -n ${DMESG_NEW}
else
    print_warning "No new kernel messages"
    dmesg | tail -n 5
fi

echo
print_success "Module '${MODULE_NAME}' rebuilt and loaded successfully!"
```

Make it executable:

```bash
% chmod +x rebuild.sh
```

Now you can iterate quickly:

```bash
% ./rebuild.sh myfirst
```

This script unloads, cleans, builds, loads, and shows recent kernel messages, all in one go. It's a huge time-saver during development. 

**Note:** You might wonder if this script needs to be this complex. For a one-time use, it doesn't. However, kernel module development involves repeated cycles of unload-rebuild-load, often dozens of times per day. Building it with proper error handling and validation creates a tool you'll confidently reuse throughout your development process, saving you countless hours later. More importantly, this is a perfect opportunity to practice defensive programming: validating inputs, checking for errors at each step, and providing clear feedback when something goes wrong. These habits will serve you well in all your future development work.

### Version Control Checkpoint

Before moving forward, commit your scaffold to Git (if you're using version control, and you should be):

```bash
% cd ~/drivers/myfirst
% git init
% git add Makefile myfirst.c
% git commit -m "Initial scaffold: loads and unloads cleanly"
```

This gives you a known-good state to return to if you break something later. If you're using a remote repository (GitHub, GitLab, etc.), you can push these changes with `git push`, but it's not required for local version control benefits.

### What's Next?

You now have a working scaffold: a module that builds, loads, and unloads. It's not a Newbus driver yet, and it doesn't create any user-visible surfaces, but it's a solid foundation.

In the next section, we'll add **Newbus integration**, transforming this simple module into a proper pseudo-device driver that registers with the device tree and implements the `probe()` and `attach()` lifecycle methods.

## Newbus: Just Enough to Attach

You've built a scaffold that loads and unloads. Now we'll transform it into a **Newbus driver**, one that registers with FreeBSD's device framework and follows the standard `identify` / `probe` / `attach` / `detach` lifecycle.

This is where your driver stops being a passive module and starts behaving like a real device driver. By the end of this section, you'll have a driver that:

- Registers as a pseudo-device on the `nexus` bus
- Provides an `identify()` method that creates the `myfirst` device on the bus
- Implements `probe()` to claim the device
- Implements `attach()` to initialize (even though initialization is minimal for now)
- Implements `detach()` to clean up
- Logs lifecycle events properly

We're keeping this **just enough** to show the pattern. No resource allocation yet, no device nodes, no sysctls. Those come in later sections. Right now, focus on understanding **how Newbus calls your code** and **what each method should do**.

### Why Newbus?

FreeBSD uses Newbus to manage device discovery, driver matching, and lifecycle. Even for pseudo-devices (software-only devices with no backing hardware), following the Newbus pattern ensures:

- Consistent behavior across all drivers
- Proper integration with the device tree
- Reliable lifecycle management (attach / detach / suspend / resume)
- Compatibility with tools like `devinfo` and `kldunload`

**Mental model:** Newbus is the kernel's HR department. It opens new positions (identify), interviews drivers for each position (probe), hires the best fit (attach), and manages resignations (detach). For real hardware, the bus posts the position automatically. For a pseudo-device, your driver also writes the job description, which is what `identify` is for.

### The Minimal Newbus Pattern

Every Newbus driver follows this structure:

1. **Define device methods** (`identify`, `probe`, `attach`, `detach`) as functions
2. **Create a method table** mapping Newbus method names to your functions
3. **Declare a driver structure** that includes the method table and softc size
4. **Register the driver** with `DRIVER_MODULE()`

For drivers that attach to a real bus such as `pci` or `usb`, the bus enumerates hardware on its own and asks every registered driver "is this device yours?" through `probe`. A pseudo-device has no hardware to enumerate, so we have to tell the bus that the device exists. That is the job of `identify`. We will introduce it in step 4 below, after probe and attach are in place, so the role of each method is clear before the file gets crowded.

Let's walk through each piece step by step.

### Step 1: Include Newbus Headers

At the top of `myfirst.c`, add these includes (replacing or augmenting the minimal includes from the scaffold):

```c
#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>        /* For device_t, Newbus APIs */
#include <sys/conf.h>       /* For cdevsw (used later) */
```

**What these provide:**

- `<sys/bus.h>` - Core Newbus types (`device_t`, `device_method_t`) and functions (`device_printf`, `device_get_softc`, etc.)
- `<sys/conf.h>` - Character device switch structures (we'll use this when creating `/dev` nodes)

### Step 2: Define Your Softc

The **softc** (software context) is your driver's per-device private data structure. Even though we're not storing anything interesting yet, **every Newbus driver has one**.

Add this near the top of `myfirst.c`, after the includes:

```c
/*
 * Driver softc (software context).
 *
 * One instance of this structure exists per device.
 * Newbus allocates and zeroes it for us.
 */
struct myfirst_softc {
        device_t        dev;            /* Back-pointer to device_t */
        uint64_t        attach_time;    /* When we attached (ticks) */
        int             is_ready;       /* Simple flag */
};
```

**Why these fields?**

- `dev` - Convenient back-pointer. Lets you call `device_printf(sc->dev, ...)` without passing `dev` everywhere.
- `attach_time` - Example state. We'll record when `attach()` ran.
- `is_ready` - Another example flag. Shows how you'd track driver state.

**Key insight:** You never `malloc()` or `free()` the softc yourself. Newbus does it automatically based on the size you declare in the driver structure.

### Step 3: Implement Probe

The `probe()` method answers one question: **"Does this driver match this device?"**

For a pseudo-device, the answer is always yes (we're not checking PCI IDs or hardware signatures). But we still implement `probe()` to follow the pattern and set a device description.

Add this function:

```c
/*
 * Probe method.
 *
 * Called by Newbus to see if this driver wants to handle this device.
 * For a pseudo-device created by our own identify method, we always accept.
 *
 * The return value is a priority. Higher values win when several drivers
 * are willing to take the same device. ENXIO means "not mine, reject".
 */
static int
myfirst_probe(device_t dev)
{
        device_set_desc(dev, "My First FreeBSD Driver");
        return (BUS_PROBE_DEFAULT);
}
```

**Line by line:**

- `device_set_desc()` sets a human-readable description. It appears in `devinfo -v` and in attach messages. The string must remain valid for the device's lifetime, so always pass a string literal here. If you ever need a dynamically built description, use `device_set_desc_copy()` instead.
- `return (BUS_PROBE_DEFAULT)` tells Newbus "I will handle this device, with the standard base-OS priority."

**Probe discipline:**

- **Don't** allocate resources in `probe()`. If another driver wins, your resources would leak.
- **Don't** touch hardware in `probe()` (not relevant here, but essential for real hardware drivers).
- **Do** return quickly. Probe is called frequently during boot and hot-plug events.

**A note on probe priority values.** When several drivers are willing to take the same device, the kernel picks the one whose `probe()` returned the **highest** value. The constants in `<sys/bus.h>` reflect that ordering, with the more specific bids being numerically larger:

| Constant                | Value (FreeBSD 14.3) | When to use it                                           |
|-------------------------|----------------------|----------------------------------------------------------|
| `BUS_PROBE_SPECIFIC`    | `0`                  | Only this driver can possibly handle this device         |
| `BUS_PROBE_VENDOR`      | `-10`                | Vendor-supplied driver, beats the generic class driver   |
| `BUS_PROBE_DEFAULT`     | `-20`                | Standard base-OS driver for this class                   |
| `BUS_PROBE_LOW_PRIORITY`| `-40`                | Older or less desirable driver                           |
| `BUS_PROBE_GENERIC`     | `-100`               | Generic fallback driver                                  |
| `BUS_PROBE_NOWILDCARD`  | very large negative  | Only match devices created explicitly (e.g. by identify) |

`BUS_PROBE_DEFAULT` is the right choice for a typical driver, including ours: we identify our own device by name in `identify()` so no real competitor exists, and the value is high enough that nothing will beat us.

### Step 4: Implement Attach

The `attach()` method is where you **initialize your driver**. Resources get allocated, hardware gets configured, device nodes get created. For now, we'll just log a message and populate the softc.

Add this function:

```c
/*
 * Attach method.
 *
 * Called after probe succeeds. Initialize the driver here.
 */
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);
        sc->dev = dev;
        sc->attach_time = ticks;  /* Record when we attached */
        sc->is_ready = 1;

        device_printf(dev, "Attached successfully at tick %lu\n",
            (unsigned long)sc->attach_time);

        return (0);
}
```

**What this does:**

- `device_get_softc(dev)` - Retrieves the softc that Newbus allocated for us (zeroed initially).
- `sc->dev = dev` - Saves the `device_t` back-pointer for convenience.
- `sc->attach_time = ticks` - Records the current kernel tick count (a simple timestamp).
- `sc->is_ready = 1` - Sets a flag (not used yet, but shows how you'd track state).
- `device_printf()` - Logs the attach event with our device name prefix.
- `return (0)` - Success. Non-zero would indicate failure and abort attachment.

**Attach discipline:**

- **Do** allocate resources here (memory, locks, hardware mappings).
- **Do** create user surfaces (`/dev` nodes, network interfaces, etc.).
- **Do** handle failures gracefully. If something goes wrong, undo what you started and return an error code.
- **Don't** touch user-space yet. Attach runs during module load or device discovery, before any user program can interact with you.

**Error handling preview:**

Right now, attach can't fail (we're not doing anything that could go wrong). Later sections will add resource allocation, and you'll see how to unwind on failure.

### Step 5: Implement Detach

The `detach()` method is the inverse of `attach()`: tear down what you built, release what you claimed, and leave no traces.

Add this function:

```c
/*
 * Detach method.
 *
 * Called when the driver is being unloaded or the device is removed.
 * Clean up everything you set up in attach().
 */
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        device_printf(dev, "Detaching (was attached for %lu ticks)\n",
            (unsigned long)(ticks - sc->attach_time));

        sc->is_ready = 0;

        return (0);
}
```

**What this does:**

- Retrieves the softc (we know it exists because attach succeeded).
- Logs how long the driver was attached (current `ticks` minus `attach_time`).
- Clears the `is_ready` flag (not strictly necessary since the softc will be freed soon, but good practice).
- Returns 0 (success).

**Detach discipline:**

- **Do** release all resources (locks destroyed, memory freed, device nodes destroyed).
- **Do** ensure no active I/O or callbacks can reach your code after detach returns.
- **Do** return `EBUSY` if the device is in use and can't be detached yet (e.g., open device nodes).
- **Don't** assume softc is still valid after detach returns. Newbus will free it.

**Why detach matters:**

Poor detach implementations are the #1 source of kernel panics on unload. If you forget to destroy a lock, free a resource, or remove a callback, you'll crash when that resource is accessed after your code is gone.

### Step 6: Implement Identify

We have probe, attach, and detach. They tell the kernel **what to do** when a `myfirst` device shows up. But there is no `myfirst` device on the nexus bus yet, and nexus has no way to invent one. We have to create the device ourselves at the moment our driver is registered. That is what an `identify` method does.

Add this function:

```c
/*
 * Identify method.
 *
 * Called by Newbus once, right after the driver is registered with the
 * parent bus. Its job is to create child devices that this driver will
 * then probe and attach.
 *
 * Real hardware drivers usually do not need an identify method, because
 * the bus (PCI, USB, ACPI, ...) enumerates devices on its own. A pseudo
 * device has nothing for the bus to find, so we add our single device
 * here, by name.
 */
static void
myfirst_identify(driver_t *driver, device_t parent)
{
        if (device_find_child(parent, driver->name, -1) != NULL)
                return;
        if (BUS_ADD_CHILD(parent, 0, driver->name, -1) == NULL)
                device_printf(parent, "myfirst: BUS_ADD_CHILD failed\n");
}
```

**Line by line:**

- `device_find_child(parent, driver->name, -1)` checks whether a `myfirst` device already exists below `parent`. If we did not check, reloading the module (or any second pass over the bus) would create duplicate devices.
- `BUS_ADD_CHILD(parent, 0, driver->name, -1)` asks the parent bus to create a new child device named `myfirst`, at order `0`, with an automatically chosen unit number. After this call, Newbus will run our `probe` against the new child, and if probe accepts, attach.
- We log on failure but do not panic. `BUS_ADD_CHILD` can fail under memory pressure, and a missing pseudo-device should not take the system down.

**Where this fits.** `identify` runs once per driver per bus, when the driver is first attached to that bus. After identify, the bus's normal probe and attach machinery takes over. This is the same pattern used by drivers such as `cryptosoft`, `aesni`, and `snd_dummy` in the FreeBSD source tree, which you can browse later as references.

### Step 7: Create the Method Table

Now connect your functions to Newbus method names. Add this after your function definitions:

```c
/*
 * Device method table.
 *
 * Maps Newbus method names to our functions.
 */
static device_method_t myfirst_methods[] = {
        /* Device interface */
        DEVMETHOD(device_identify,      myfirst_identify),
        DEVMETHOD(device_probe,         myfirst_probe),
        DEVMETHOD(device_attach,        myfirst_attach),
        DEVMETHOD(device_detach,        myfirst_detach),

        DEVMETHOD_END
};
```

**What this table means:**

- `DEVMETHOD(device_identify, myfirst_identify)` says "when the bus invites every driver to create its devices, run `myfirst_identify()`."
- `DEVMETHOD(device_probe, myfirst_probe)` says "when the kernel calls `DEVICE_PROBE(dev)`, run `myfirst_probe()`."
- Same for attach and detach.
- `DEVMETHOD_END` terminates the table and is required.

**Behind the scenes:** The `DEVMETHOD()` macro and the kobj system (kernel objects) generate the glue code that dispatches to your functions. You don't need to understand the internals; just know that this table is how Newbus finds your code.

### Step 8: Declare the Driver

Tie everything together in a `driver_t` structure:

```c
/*
 * Driver declaration.
 *
 * Specifies our method table and softc size.
 */
static driver_t myfirst_driver = {
        "myfirst",              /* Driver name */
        myfirst_methods,        /* Method table */
        sizeof(struct myfirst_softc)  /* Softc size */
};
```

**Parameters:**

- `"myfirst"` - Driver name (shows up in logs and as the device name prefix).
- `myfirst_methods` - Pointer to the method table you just created.
- `sizeof(struct myfirst_softc)` - Tells Newbus how much memory to allocate per device.

**Why the softc size?** Newbus allocates one softc per device instance. By declaring the size here, you never manually allocate or free it, Newbus manages the lifetime.

### Step 9: Register with DRIVER_MODULE

Replace the old `DECLARE_MODULE()` macro from the scaffold with this:

```c
/*
 * Driver registration.
 *
 * Attach this driver under the nexus bus. Our identify method will
 * create the actual myfirst child device when the module loads.
 */

DRIVER_MODULE(myfirst, nexus, myfirst_driver, 0, 0);
MODULE_VERSION(myfirst, 1);
```

**What this does:**

- `DRIVER_MODULE(myfirst, nexus, myfirst_driver, 0, 0)` registers `myfirst` as a driver willing to attach below the `nexus` bus. The two trailing zeros are an optional module event handler and its argument; our minimal driver does not need them.
- `MODULE_VERSION(myfirst, 1)` stamps the module with version 1, so other modules can declare a dependency on it.

**Why `nexus`?**

`nexus` is FreeBSD's root bus, the top of every architecture's device tree. Chapter 6 advised you, correctly, that `nexus` is rarely the right parent for a real hardware driver: a PCI driver belongs under `pci`, a USB driver under `usbus`, and so on. Pseudo-devices are different. They have no physical bus, so the convention in the FreeBSD source tree is to attach them to `nexus` and create the child device themselves through an `identify` method. This is exactly what `cryptosoft`, `aesni`, and `snd_dummy` do, and exactly what we are doing here.

### Step 10: Remove the Old Module Loader

You no longer need the `myfirst_loader()` function or `moduledata_t` struct from the scaffold. Newbus now drives the module lifecycle through `identify`, `probe`, `attach`, and `detach`. Remove those old pieces entirely.

Your `myfirst.c` should now have:

- Includes
- Softc structure
- `myfirst_identify()`
- `myfirst_probe()`
- `myfirst_attach()`
- `myfirst_detach()`
- Method table
- Driver structure
- `DRIVER_MODULE()` and `MODULE_VERSION()`

No more `MOD_LOAD` event handler.

### Step 11: Adjust the Makefile

Add this line to your Makefile:

```makefile
# Required for Newbus drivers: generates device_if.h and bus_if.h
SRCS+=   device_if.h bus_if.h
```

**Why this is needed:**

FreeBSD's Newbus framework uses a method dispatch system built on top of kobj. The `DEVMETHOD()` entries in your method table refer to method identifiers declared in the generated headers `device_if.h` and `bus_if.h`. `bsd.kmod.mk` knows how to build these from `/usr/src/sys/kern/device_if.m` and `/usr/src/sys/kern/bus_if.m`, but it only does so if you list them in `SRCS`. If you forget this line you will get a confusing error about unknown method identifiers when you compile.

### Build and Test the Newbus Driver

Let's compile and test:

**1. Clean and build:**

```bash
% make clean
% make
```

You should see no errors.

**2. Load the module:**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
```

Notice:

- The device name is `myfirst0` (driver name + unit number).
- It attached "on nexus0" (the parent bus).
- Your custom attach message appeared.

**3. Check the device tree:**

```bash
% devinfo -v | grep myfirst
    myfirst0
```

Your driver is now part of the device tree.

**4. Unload:**

```bash
% sudo kldunload myfirst
% dmesg | tail -n 2
myfirst0: Detaching (was attached for 5432 ticks)
```

Your detach message shows how long the driver was attached.

**5. Verify it's gone:**

```bash
% devinfo -v | grep myfirst
(no output)
```

### What Changed?

Compared to the scaffold, your driver now:

- **Registers with Newbus** instead of using a simple module loader.
- **Adds a child device** (`myfirst0`) to the device tree through `identify`.
- **Follows the identify / probe / attach / detach lifecycle** instead of just load/unload.
- **Allocates and manages a softc** automatically.

This is the **foundation pattern** for every FreeBSD driver. Master this, and the rest is just adding layers.

### Common Newbus Mistakes (and How to Avoid Them)

**Mistake 0: Forgetting the identify method on a pseudo-device**

**Symptom:** `kldload` succeeds, but no `myfirst0` device appears, no probe message in `dmesg`, and `devinfo` shows nothing under `nexus0`. The driver compiled and loaded, but it never attached.

**Cause:** The driver was registered with `DRIVER_MODULE(..., nexus, ...)` but no `device_identify` method was provided. Nexus has nothing to enumerate, so probe and attach are never called.

**Fix:** Add the `identify` method shown in Step 6 and put `DEVMETHOD(device_identify, myfirst_identify)` in the method table. This is the most common reason a beginner's pseudo-device driver "loads but does nothing."

---

**Mistake 1: Allocating resources in probe**

**Wrong:**

```c
static int
myfirst_probe(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        sc->something = malloc(...);  /* BAD! */
        return (BUS_PROBE_DEFAULT);
}
```

**Why it's wrong:** If probe fails or another driver wins, your allocation leaks.

**Right:** Allocate in `attach()`, where you know the driver has been selected.

---

**Mistake 2: Forgetting to return 0 from attach**

**Wrong:**

```c
static int
myfirst_attach(device_t dev)
{
        /* ... setup ... */
        /* (missing return statement) */
}
```

**Why it's wrong:** Compiler might warn, but the return value is undefined. You might accidentally return garbage, causing attach to fail mysteriously.

**Right:** Always end attach with `return (0)` on success or `return (error_code)` on failure.

---

**Mistake 3: Not cleaning up in detach**

**Wrong:**

```c
static int
myfirst_detach(device_t dev)
{
        device_printf(dev, "Detaching\n");
        return (0);
        /* (forgot to free resources, destroy locks, etc.) */
}
```

**Why it's wrong:** Resources leak. Locks remain active. Next load might panic.

**Right:** Detach must undo everything attach did. We'll cover the cleanup pattern in detail in the error handling section.

### Newbus Lifecycle Timing Diagram

```text
[ Boot or kldload ]
        |
        v
   identify(parent)  --> "What devices does this driver provide?"
        |                 (Pseudo-devices: BUS_ADD_CHILD here)
        |                 (Real hardware: usually omitted)
        v
    probe(dev)  --> "Is this device mine?"
        |            (Check IDs, set description)
        | (return a probe priority such as BUS_PROBE_DEFAULT)
        v
    attach(dev)  --> "Initialize and prepare for use"
        |            (Allocate resources, create surfaces)
        |            (If fails, undo what was done, return error)
        v
  [ Device ready, normal operation ]
        |
        | (time passes, I/O happens, sysctls read, etc.)
        |
        v
    detach(dev)  --> "Shutdown and cleanup"
        |            (Destroy surfaces, release resources)
        |            (Return EBUSY if still in use)
        v
    [ Module unloaded or device gone ]
```

**Key insight:** Each step is distinct. Identify creates the device, probe claims it, attach initializes, detach cleans up. Don't blur the boundaries.

### Quick Self-Check

Before moving forward, ensure you can answer these:

1. **Where do I allocate memory for driver state?**
   Answer: In `attach()`, or just use the softc (Newbus allocates it for you).

2. **What does `device_get_softc()` return?**
   Answer: A pointer to your driver's per-device private data (`struct myfirst_softc *` in this case).

3. **When is probe called?**
   Answer: During device enumeration. For a real bus, that happens when the bus discovers a device. For our pseudo-device, it happens right after our `identify` method calls `BUS_ADD_CHILD()` to put a `myfirst` device on the nexus bus.

4. **What must detach do?**
   Answer: Undo everything attach did, release resources, and ensure no code paths can reach the driver afterward.

5. **Why do we use `nexus` for this driver, and why does it need an `identify` method?**
   Answer: Because it's a pseudo-device with no physical bus. `nexus` is the conventional parent for software-only devices, but nexus has no devices to enumerate, so we create our own through `identify`.

If those answers make sense, you're ready for the next section: adding real state management with the softc.

---

## softc and Lifecycle State

You've seen the softc structure declared, allocated, and retrieved, but we haven't talked about **why it exists** or **how to use it properly**. In this section, we'll explore the softc pattern in depth: what goes in it, how to initialize and access it safely, and how to avoid common pitfalls.

The softc is your driver's **memory**. Every resource, every lock, every statistic, and every flag lives here. Getting this right is the difference between a reliable driver and one that panics under load.

### What Is the softc?

The **softc** (software context) is a per-device structure that stores everything your driver needs to operate. Think of it as the driver's "workspace" or "notebook", one instance per device, holding all the state that makes that particular device work.

**Key properties:**

- **Per-device:** If your driver handles multiple devices (e.g., `myfirst0`, `myfirst1`), each gets its own softc.
- **Kernel-allocated:** You declare the structure type and size; Newbus allocates and zeroes the memory.
- **Lifetime:** Exists from device creation (before `attach()`) until device deletion (after `detach()`).
- **Access pattern:** Retrieve with `device_get_softc(dev)` at the start of every method.

**Why not global variables?**

Global variables can't handle multiple devices. If you stored state in globals, `myfirst0` and `myfirst1` would clobber each other's data. The softc pattern solves this elegantly: each device has its own isolated state.

### What Belongs in the softc?

A well-designed softc contains:

**1. Identification and housekeeping**

- `device_t dev` - Back-pointer to the device (for logging and callbacks)
- `int unit` - Device unit number (often extracted from `dev`, but handy to cache)
- `char name[16]` - Device name string if you need it frequently

**2. Resources**

- `struct resource *mem_res` - MMIO regions (for hardware drivers)
- `int mem_rid` - Resource ID for memory
- `struct resource *irq_res` - Interrupt resource
- `void *irq_handler` - Interrupt handler cookie
- `bus_dma_tag_t dma_tag` - DMA tag (for drivers that do DMA)

**3. Synchronization primitives**

- `struct mtx mtx` - Mutex for protecting shared state
- `struct sx sx` - Shared/exclusive lock if needed
- `struct cv cv` - Condition variable for sleeping/waking

**4. Device state flags**

- `int is_attached` - Set in attach, cleared in detach
- `int is_open` - Set when `/dev` node is open
- `uint32_t flags` - Bitfield for misc state (running, suspended, error, etc.)

**5. Statistics and counters**

- `uint64_t tx_packets` - Packets transmitted (network driver example)
- `uint64_t rx_bytes` - Bytes received
- `uint64_t errors` - Error count
- `time_t last_reset` - When stats were last cleared

**6. Driver-specific data**

- Hardware registers, queues, buffers, work structures, anything unique to your driver's operation.

**What doesn't belong:**

- **Large buffers:** Softc lives in kernel memory (wired, non-pageable). Big buffers should be allocated separately with `malloc()` or `contigmalloc()` and pointed to from the softc.
- **Constant data:** Use `const` global arrays or static tables instead.
- **Temporary variables:** Function locals are fine. Don't clutter the softc with per-operation temporaries.

### Our myfirst Softc (Minimal Example)

Let's revisit and expand our softc definition:

```c
struct myfirst_softc {
        device_t        dev;            /* Back-pointer */
        int             unit;           /* Device unit number */

        struct mtx      mtx;            /* Protects shared state */

        uint64_t        attach_ticks;   /* When attach() ran */
        uint64_t        open_count;     /* How many times opened */
        uint64_t        bytes_read;     /* Bytes read from device */

        int             is_attached;    /* 1 if attach succeeded */
        int             is_open;        /* 1 if /dev node is open */
};
```

**Field by field:**

- `dev` - Standard back-pointer. Almost every driver includes this.
- `unit` - Cached unit number (from `device_get_unit(dev)`). Optional but convenient.
- `mtx` - Mutex to protect concurrent access. Even though we're not exercising concurrency yet, including it now teaches good habits.
- `attach_ticks` - When we attached (kernel ticks). Simple example state.
- `open_count` / `bytes_read` - Counters. Real drivers track these for stats and observability.
- `is_attached` / `is_open` - Flags for lifecycle state. Useful in error checking.

**Why a mutex now?**

Even though our minimal driver doesn't need it yet, including the mutex teaches the **pattern**. Every driver will eventually need locking, and it's easier to design it in from the start than retrofit it later.

We don't use the mutex for real shared-data protection yet. Concurrency, lock ordering, and deadlock pitfalls arrive in Part 3. For now, the lock is here to establish the lifecycle pattern, and to make detach ordering safe. See the "destroying locks while threads still hold them" pitfall from Chapter 6.

### Initializing the softc in attach()

Newbus zeroes the softc before calling `attach()`, but you still need to initialize certain fields explicitly (locks, back-pointers, flags).

Here's the updated `attach()`:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        /* Initialize back-pointer and unit */
        sc->dev = dev;
        sc->unit = device_get_unit(dev);

        /* Initialize mutex */
        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

        /* Record attach time */
        sc->attach_ticks = ticks;

        /* Set state flags */
        sc->is_attached = 1;
        sc->is_open = 0;

        /* Initialize counters */
        sc->open_count = 0;
        sc->bytes_read = 0;

        device_printf(dev, "Attached at tick %lu\n",
            (unsigned long)sc->attach_ticks);

        return (0);
}
```

**What changed:**

- **`mtx_init()`** - Initializes the mutex. Parameters:
  - `&sc->mtx` - Address of the mutex field in the softc.
  - `device_get_nameunit(dev)` - Returns a string like "myfirst0" (used in lock debugging).
  - `"myfirst"` - Lock type name (appears in lock traces).
  - `MTX_DEF` - Standard mutex (as opposed to spin mutex).
- **`sc->is_attached = 1`** - Flag that we're now ready.
- **Counter initialization** - Explicitly zero them (even though Newbus zeroed the whole softc, being explicit documents intent).

**Discipline:** Initialize all fields that will be tested later. Don't assume "zero means uninitialized" is always correct semantics (for flags, maybe; for pointers, definitely not).

### Destroying the softc in detach()

In `detach()`, you must undo everything `attach()` did. For the softc, that means:

- Destroy locks (mutexes, sx, cv, etc.)
- Free any memory or resources pointed to by the softc
- Clear flags (not strictly necessary, but good hygiene)

Updated `detach()`:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc;
        uint64_t uptime;

        sc = device_get_softc(dev);

        /* Calculate how long we were attached */
        uptime = ticks - sc->attach_ticks;

        /* Refuse detach if device is open */
        if (sc->is_open) {
                device_printf(dev, "Cannot detach while device is open\n");
                return (EBUSY);
        }

        /* Log stats before shutting down */
        device_printf(dev, "Detaching: uptime %lu ticks, opened %lu times, read %lu bytes\n",
            (unsigned long)uptime,
            (unsigned long)sc->open_count,
            (unsigned long)sc->bytes_read);

        /* Destroy the mutex */
        mtx_destroy(&sc->mtx);

        /* Clear attached flag */
        sc->is_attached = 0;

        return (0);
}
```

**What's new:**

- **`if (sc->is_open) return (EBUSY)`** - Refuse to detach if the `/dev` node is still open. This prevents crashes from accessing freed resources.
- **Stats logging** - Shows how long the driver was up and what it did.
- **`mtx_destroy(&sc->mtx)`** - **Critical.** Every `mtx_init()` must have a matching `mtx_destroy()`, or you leak kernel lock resources.
- **Clear flags** - Not strictly required (Newbus will free the softc soon), but good defensive programming.

**Common mistake:** Forgetting `mtx_destroy()`. This causes lock tracking panics on the next load if you're using WITNESS or INVARIANTS kernels.

### Accessing the softc from Other Methods

Every driver method that needs state starts the same way:

```c
static int
myfirst_some_method(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        /* Now use sc-> to access state */
        ...
}
```

This is the **idiomatic pattern** you'll see in every FreeBSD driver. One line to enter your driver's world.

**Why not pass softc directly?**

Newbus methods are defined to receive `device_t`. The softc is an implementation detail of your driver. By consistently retrieving it via `device_get_softc()`, your driver stays flexible (you could change the softc structure without changing method signatures).

### Using Locks to Protect the softc

Even though we haven't added concurrent operations yet, let's preview the **locking pattern** you'll use when you do.

**Basic pattern:**

```c
static void
myfirst_increment_counter(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        mtx_lock(&sc->mtx);
        sc->open_count++;
        mtx_unlock(&sc->mtx);
}
```

**Rules:**

- **Lock before modifying shared state.**
- **Unlock as soon as you're done** (don't hold locks longer than necessary).
- **Never return with a lock held** (unless you're doing advanced lock handoff patterns).
- **Document lock order** if you hold multiple locks (to avoid deadlock).

**When you'll need this:** As soon as you add `open()`, `read()`, `write()`, or any method that could run concurrently (user programs calling your driver from multiple threads, or interrupt handlers updating stats).

For now, the mutex exists but isn't exercised. We'll use it in later sections when we add concurrent entry points.

### Softc Best Practices

**1. Keep it organized**

Group related fields together:

```c
struct myfirst_softc {
        /* Identification */
        device_t        dev;
        int             unit;

        /* Synchronization */
        struct mtx      mtx;

        /* Resources */
        struct resource *mem_res;
        int             mem_rid;

        /* Statistics */
        uint64_t        tx_packets;
        uint64_t        rx_bytes;

        /* State flags */
        int             is_attached;
        int             is_open;
};
```

**2. Comment non-obvious fields**

```c
        int             pending_requests;  /* Must hold mtx to access */
        time_t          last_activity;     /* Protected by mtx */
```

**3. Use fixed-width types for counters**

```c
        uint64_t        packets;  /* Not "unsigned long" */
        uint32_t        errors;   /* Not "int" */
```

**Why?** Portability. `int` and `long` sizes vary by architecture. `uint64_t` is always 64 bits.

**4. Avoid padding waste**

The compiler inserts padding to align fields. Arrange large fields first, then smaller:

```c
/* Good: no wasted padding */
struct example {
        uint64_t        big_counter;  /* 8 bytes */
        uint32_t        medium;       /* 4 bytes */
        uint32_t        medium2;      /* 4 bytes */
        uint16_t        small;        /* 2 bytes */
        uint8_t         tiny;         /* 1 byte */
        uint8_t         tiny2;        /* 1 byte */
};
```

**5. Zero fields you'll test**

```c
        sc->is_open = 0;       /* Explicit, even though Newbus zeroed it */
        sc->bytes_read = 0;
```

**Why?** Clarity. Someone reading the code knows you *intended* zero, not that you relied on implicit zeroing.

### Debugging softc Issues

**Problem:** Kernel panic "NULL pointer dereference" in your driver.

**Likely cause:** You forgot to retrieve the softc, or retrieved it after a point where `dev` might be invalid.

**Fix:** Always `sc = device_get_softc(dev);` at the start of every method.

---

**Problem:** Mutex panic "already locked" or "not locked."

**Likely cause:** Forgot `mtx_init()` in `attach()` or mismatched `mtx_lock()` / `mtx_unlock()` calls.

**Fix:** Check your init/destroy pairs. Use WITNESS-enabled kernels (`options WITNESS` in kernel config) to catch lock violations.

---

**Problem:** Stats or flags seem random/corrupted.

**Likely cause:** Concurrent access without locking, or accessing softc after `detach()` freed it.

**Fix:** Ensure all shared state is protected by the mutex. Ensure no code paths (callbacks, timers, threads) can reach the driver after `detach()` returns.

### Quick Self-Check

Before moving forward, ensure you understand:

1. **What is the softc?**
   Answer: Per-device private data structure holding all driver state.

2. **Who allocates the softc?**
   Answer: Newbus, based on the size declared in the `driver_t` structure.

3. **When must you initialize the mutex?**
   Answer: In `attach()`, before any code that might use it.

4. **When must you destroy the mutex?**
   Answer: In `detach()`, before the function returns.

5. **Why do we refuse detach if `is_open` is true?**
   Answer: To prevent freeing resources while user programs still have the device open, which would cause a crash.

If those answers are clear, you're ready to add **logging discipline** in the next section.

---

## Logging Etiquette & dmesg Hygiene

A well-behaved driver **talks when it should** and **stays quiet when it shouldn't**. Logging too much floods `dmesg` and makes debugging harder; logging too little leaves users and developers blind when something goes wrong. This section teaches you **when, what, and how to log** in a FreeBSD driver.

By the end, you'll know:

- Which events **must** be logged (attach, errors, critical state changes)
- Which events **should** be logged (optional, debug-level info)
- Which events **must not** be logged (per-packet/per-operation spam)
- How to use `device_printf()` effectively
- How to create rate-limited logging for hot paths
- How to make your logs readable and actionable

### Why Logging Matters

When a driver misbehaves, `dmesg` is often the first place developers and users look. Good logs answer questions like:

- Did the driver attach successfully?
- What hardware did it find?
- Did an error occur? Why?
- Is the device operational or in an error state?

Bad logs spam the console, hide critical messages, or omit important details.

**Mental model:** Logging is like a doctor taking notes during an exam. Write enough to diagnose problems later, but don't record every heartbeat.

### The Golden Rules of Driver Logging

**Rule 1: Log lifecycle events**

Always log:

- Successful attach (one line per device)
- Attach failures (with reason)
- Successful detach (optional but recommended)
- Detach failures (with reason)

**Example:**

```c
device_printf(dev, "Attached successfully\n");
device_printf(dev, "Failed to allocate memory resource: error %d\n", error);
```

---

**Rule 2: Log errors**

When something goes wrong, **always log what and why**. Include:

- What operation failed
- Error code (errno value)
- Context (if relevant)

**Example:**

```c
if (error != 0) {
        device_printf(dev, "Could not allocate IRQ resource: error %d\n", error);
        return (error);
}
```

**Bad example:**

```c
if (error != 0) {
        return (error);  /* User sees nothing! */
}
```

---

**Rule 3: Never log in hot paths**

"Hot path" = code that runs frequently during normal operation (every packet, every interrupt, every read/write call).

**Never do this:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int flag)
{
        device_printf(dev, "Read called\n");  /* BAD: spams logs */
        ...
}
```

**Why?** If a program reads from your device in a loop, you'll generate thousands of log lines per second, rendering the console unusable.

**When to log hot-path events:** Only for debugging during development, and guarded by a debug flag or sysctl that's disabled by default.

---

**Rule 4: Use device_printf() for device-specific messages**

`device_printf()` automatically prefixes your message with the device name:

```c
device_printf(dev, "Interrupt timeout\n");
```

Output:

```text
myfirst0: Interrupt timeout
```

This makes it immediately clear **which device** is talking, especially when multiple instances exist.

**Don't use plain `printf()`:**

```c
printf("Interrupt timeout\n");  /* Which device? Unknown. */
```

---

**Rule 5: Rate-limit warnings in repetitive error paths**

If an error can repeat rapidly (e.g., DMA timeout on every frame), rate-limit it:

```c
static int
myfirst_check_fifo(struct myfirst_softc *sc)
{
        if (fifo_is_full(sc)) {
                if (sc->log_fifo_full == 0) {
                        device_printf(sc->dev, "FIFO full, dropping packets\n");
                        sc->log_fifo_full = 1;  /* Only log once until cleared */
                }
                return (ENOSPC);
        }
        sc->log_fifo_full = 0;  /* Clear flag when condition resolves */
        return (0);
}
```

**This pattern logs the first occurrence, suppresses repeats, and logs again when the condition changes.**

---

**Rule 6: Be concise and actionable**

Compare:

**Bad:**

```c
device_printf(dev, "Something went wrong in the code here\n");
```

**Good:**

```c
device_printf(dev, "Failed to map BAR0 MMIO region: error %d\n", error);
```

The good example tells you **what** failed, **where** (BAR0), and **how** (error code).

### Logging Patterns for Common Events

**Attach success:**

```c
device_printf(dev, "Attached successfully, hardware rev %d.%d\n",
    hw_major, hw_minor);
```

**Attach failure:**

```c
device_printf(dev, "Attach failed: could not allocate IRQ\n");
goto fail;
```

**Detach:**

```c
device_printf(dev, "Detached, uptime %lu seconds\n",
    (unsigned long)(ticks - sc->attach_ticks) / hz);
```

**Resource allocation failure:**

```c
if (sc->mem_res == NULL) {
        device_printf(dev, "Could not allocate memory resource\n");
        error = ENXIO;
        goto fail;
}
```

**Unexpected hardware state:**

```c
if (status & DEVICE_ERROR_BIT) {
        device_printf(dev, "Hardware reported error 0x%x\n", status);
        /* attempt recovery or fail */
}
```

**First open:**

```c
if (sc->open_count == 0) {
        device_printf(dev, "Device opened for the first time\n");
}
```

(But only if this is unusual or noteworthy; don't log every open in production.)

### Rate-Limited Logging Macro (Advanced Preview)

For errors that might repeat rapidly, you can define a rate-limited logging macro:

```c
#define MYFIRST_RATELIMIT_HZ 1  /* Max once per second */

static int
myfirst_log_ratelimited(struct myfirst_softc *sc, const char *fmt, ...)
{
        static time_t last_log = 0;
        time_t now;
        va_list ap;

        now = time_second;
        if (now - last_log < MYFIRST_RATELIMIT_HZ)
                return (0);  /* Too soon, skip */

        last_log = now;

        va_start(ap, fmt);
        device_vprintf(sc->dev, fmt, ap);
        va_end(ap);

        return (1);
}
```

**Usage:**

```c
if (error_condition) {
        myfirst_log_ratelimited(sc, "DMA timeout occurred\n");
}
```

This limits the log to **once per second**, even if the condition triggers thousands of times.

**When to use:** Only for hot-path errors that could spam logs (interrupt storms, queue overflows, etc.). Not needed for attach/detach or rare errors.

### What to Log During Development vs Production

**Development (verbose):**

- Every function entry/exit (guarded by a debug flag)
- Register reads/writes
- State transitions
- Resource allocation/deallocation

**Production (quiet):**

- Attach/detach lifecycle
- Errors
- Critical state changes (link up/down, device reset)
- First occurrence of repetitive errors

**Transition:** Start verbose, then pare down as the driver stabilizes. Leave debug logging behind compile-time or sysctl guards for future troubleshooting.

### Using Sysctls for Debug Logging

Instead of hardcoding verbosity, expose a sysctl:

```c
static int myfirst_debug = 0;
SYSCTL_INT(_hw_myfirst, OID_AUTO, debug, CTLFLAG_RWTUN,
    &myfirst_debug, 0, "Enable debug logging");
```

Then wrap debug logs:

```c
if (myfirst_debug) {
        device_printf(dev, "DEBUG: entering attach\n");
}
```

**Benefit:** Users or developers can enable logging without recompiling:

```bash
% sysctl hw.myfirst.debug=1
```

We'll cover sysctls in detail in the next section; this is just a preview.

### Inspecting Logs

**View all kernel messages:**

```bash
% dmesg -a
```

**View recent messages:**

```bash
% dmesg | tail -n 20
```

**Search for your driver:**

```bash
% dmesg | grep myfirst
```

**Clear the message buffer (if testing repeatedly):**

```bash
% sudo dmesg -c > /dev/null
```

(Not always advisable, but useful when you want a clean slate for testing.)

### Example: Logging in myfirst

Let's add disciplined logging to our driver. Update `attach()` and `detach()`:

**Updated attach:**

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);
        sc->dev = dev;
        sc->unit = device_get_unit(dev);

        /* Initialize mutex */
        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

        /* Record attach time */
        sc->attach_ticks = ticks;
        sc->is_attached = 1;

        /* Log attach success */
        device_printf(dev, "Attached successfully at tick %lu\n",
            (unsigned long)sc->attach_ticks);

        return (0);
}
```

**Updated detach:**

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc;
        uint64_t uptime_ticks;

        sc = device_get_softc(dev);

        /* Refuse detach if open */
        if (sc->is_open) {
                device_printf(dev, "Cannot detach: device is open\n");
                return (EBUSY);
        }

        /* Calculate uptime */
        uptime_ticks = ticks - sc->attach_ticks;

        /* Log detach */
        device_printf(dev, "Detaching: uptime %lu ticks, opened %lu times\n",
            (unsigned long)uptime_ticks,
            (unsigned long)sc->open_count);

        /* Cleanup */
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;

        return (0);
}
```

**What we're logging:**

- **Attach:** Confirms success and records when.
- **Detach refusal:** If device is open, explain why detach failed.
- **Detach success:** Shows uptime and usage stats.

This gives users and developers clear visibility into lifecycle events.

### Common Logging Mistakes

**Mistake 1: Logging inside locks**

**Wrong:**

```c
mtx_lock(&sc->mtx);
device_printf(dev, "Locked, doing work\n");  /* Can cause priority inversion */
/* ... work ... */
mtx_unlock(&sc->mtx);
```

**Why it's wrong:** `device_printf()` can block (acquiring internal locks). Calling it while holding your mutex can lead to deadlock or priority inversion.

**Right:**

```c
mtx_lock(&sc->mtx);
/* ... work ... */
mtx_unlock(&sc->mtx);

device_printf(dev, "Work completed\n");  /* Log after releasing lock */
```

---

**Mistake 2: Multi-line logs that can interleave**

**Wrong:**

```c
printf("myfirst0: Attach starting\n");
printf("myfirst0: Step 1\n");
printf("myfirst0: Step 2\n");
```

**Why it's wrong:** If another driver or kernel component logs between your lines, your message gets fragmented.

**Right:**

```c
device_printf(dev, "Attach starting: step 1, step 2 completed\n");
```

Or use a single `sbuf` (string buffer) and print it once (advanced).

---

**Mistake 3: Logging sensitive data**

Don't log:

- User data (packet contents, file data, etc.)
- Cryptographic keys or secrets
- Anything that violates privacy expectations

**Always assume logs are public.**

### Quick Self-Check

Before moving forward, confirm you understand:

1. **When must you log?**
   Answer: Lifecycle events (attach/detach), errors, critical state changes.

2. **When must you not log?**
   Answer: Hot paths (interrupts, read/write loops, per-packet operations).

3. **Why use `device_printf()` instead of `printf()`?**
   Answer: Automatically includes device name, making logs clearer.

4. **How do you rate-limit a log that could repeat rapidly?**
   Answer: Use a flag or timestamp to track last log time, and suppress repeats.

5. **What should every error log include?**
   Answer: What failed, why (error code), and enough context to diagnose.

If those are clear, you're ready to add your first user-visible surface: a `/dev` node.

---

## A Temporary User Surface: /dev (Preview Only)

Every driver needs a way for user programs to interact with it. For character devices, that surface is a **device node** in `/dev`. In this section, we'll create `/dev/myfirst0`, but we won't implement full I/O yet, just enough to show the pattern and prove the device is reachable from user-space.

This is a **preview**, not the full implementation. Real `read()` and `write()` semantics come in **Chapter 8 and 9**. Here, we're focusing on:

- Creating the `/dev` node using `make_dev_s()`
- Defining a `cdevsw` (character device switch) with stubbed methods
- Handling `open()` and `close()` to track device state
- Cleaning up the device node in `detach()`

Think of this as **wiring the front door** before furnishing the house. The door opens and closes, but the rooms are empty.

### What Is a Character Device Switch (cdevsw)?

The **cdevsw** is a structure containing function pointers for character device operations: `open`, `close`, `read`, `write`, `ioctl`, `mmap`, etc. When a user program calls `open("/dev/myfirst0", ...)`, the kernel looks up the cdevsw associated with that device node and calls your `d_open` function.

**Structure definition** (abbreviated):

```c
struct cdevsw {
        int     d_version;    /* D_VERSION (API version) */
        d_open_t        *d_open;      /* open(2) handler */
        d_close_t       *d_close;     /* close(2) handler */
        d_read_t        *d_read;      /* read(2) handler */
        d_write_t       *d_write;     /* write(2) handler */
        d_ioctl_t       *d_ioctl;     /* ioctl(2) handler */
        const char *d_name;   /* Device name */
        /* ... more fields ... */
};
```

**Key insight:** You provide implementations for the operations your device supports, and leave others `NULL` (which the kernel interprets as "not supported" or "default behavior").

### Defining the cdevsw for myfirst

We'll define a minimal cdevsw with `open` and `close` handlers, and stub `read` / `write` for now.

Add this near the top of `myfirst.c`, after the softc definition:

```c
/* Forward declarations for cdevsw methods */
static d_open_t         myfirst_open;
static d_close_t        myfirst_close;
static d_read_t         myfirst_read;
static d_write_t        myfirst_write;

/*
 * Character device switch.
 *
 * Maps system calls to our driver functions.
 */
static struct cdevsw myfirst_cdevsw = {
        .d_version =    D_VERSION,
        .d_open =       myfirst_open,
        .d_close =      myfirst_close,
        .d_read =       myfirst_read,
        .d_write =      myfirst_write,
        .d_name =       "myfirst",
};
```

**What this means:**

- `d_version = D_VERSION` - Required API version stamp.
- `d_open = myfirst_open` - When user calls `open("/dev/myfirst0", ...)`, kernel calls `myfirst_open()`.
- Similar for `close`, `read`, `write`.
- `d_name = "myfirst"` - Base name for device nodes (combined with unit number to form `myfirst0`, `myfirst1`, etc.).

### Implementing open()

The `open()` handler is called when a user program opens the device node. It's your chance to:

- Verify the device is ready
- Track open state (increment counters, set flags)
- Return an error if the device can't be opened (e.g., exclusive access, hardware not ready)

Add this function:

```c
/*
 * open() handler.
 *
 * Called when a user program opens /dev/myfirst0.
 */
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc;

        sc = dev->si_drv1;  /* Retrieve softc from cdev */

        if (sc == NULL || !sc->is_attached) {
                return (ENXIO);  /* Device not ready */
        }

        mtx_lock(&sc->mtx);
        if (sc->is_open) {
                mtx_unlock(&sc->mtx);
                return (EBUSY);  /* Only allow one opener (exclusive access) */
        }

        sc->is_open = 1;
        sc->open_count++;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "Device opened (count: %lu)\n",
            (unsigned long)sc->open_count);

        return (0);
}
```

**What this does:**

- **`sc = dev->si_drv1`** - Retrieves the softc. When we create the device node, we'll stash the softc pointer here.
- **`if (!sc->is_attached)`** - Sanity check. If the device isn't attached, refuse to open.
- **`if (sc->is_open) return (EBUSY)`** - Enforce exclusive access (only one opener at a time). Real devices might allow multiple openers; this is just a simple example.
- **`sc->is_open = 1`** - Mark device as open.
- **`sc->open_count++`** - Increment lifetime open counter.
- **`device_printf()`** - Log the open event (for now; you'd remove this in production).

**Lock discipline:** We hold the mutex while checking and updating `is_open`, ensuring thread safety.

### Implementing close()

The `close()` handler is called when the last reference to the open device is released. Clean up open-specific state here.

Add this function:

```c
/*
 * close() handler.
 *
 * Called when the user program closes /dev/myfirst0.
 */
static int
myfirst_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
        struct myfirst_softc *sc;

        sc = dev->si_drv1;

        if (sc == NULL) {
                return (ENXIO);
        }

        mtx_lock(&sc->mtx);
        sc->is_open = 0;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "Device closed\n");

        return (0);
}
```

**What this does:**

- Clears the `is_open` flag.
- Logs the close event.
- Returns 0 (success).

**Simple pattern:** `open()` sets flags, `close()` clears them.

### Stubbing read() and write()

We'll implement minimal stubs that return success but do nothing. This proves the device node is wired correctly without committing to I/O semantics yet.

**Stub read():**

```c
/*
 * read() handler (stubbed).
 *
 * For now, just return EOF (0 bytes read).
 * Real implementation in Chapter 9.
 */
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* Return EOF immediately */
        return (0);
}
```

**Stub write():**

```c
/*
 * write() handler (stubbed).
 *
 * For now, pretend we wrote everything.
 * Real implementation in Chapter 9.
 */
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* Pretend we consumed all bytes */
        uio->uio_resid = 0;
        return (0);
}
```

**What these do:**

- **`read()`** - Returns 0 (EOF), meaning "no data available."
- **`write()`** - Sets `uio->uio_resid = 0`, meaning "all bytes written."

User programs will see this as a device that "accepts writes but discards them" and "reads return EOF immediately." Not useful yet, but proves the plumbing works.

### Creating the Device Node in attach()

Now we tie it all together. In `attach()`, create the `/dev` node and associate it with your softc.

Add this to the end of `myfirst_attach()`, just before `return (0)`:

```c
        /* Create /dev node */
        {
                struct make_dev_args args;
                int error;

                make_dev_args_init(&args);
                args.mda_devsw = &myfirst_cdevsw;
                args.mda_uid = UID_ROOT;
                args.mda_gid = GID_WHEEL;
                args.mda_mode = 0600;  /* rw------- (root only) */
                args.mda_si_drv1 = sc;  /* Stash softc pointer */

                error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
                if (error != 0) {
                        device_printf(dev, "Failed to create device node: error %d\n", error);
                        mtx_destroy(&sc->mtx);
                        return (error);
                }
        }

        device_printf(dev, "Created /dev/%s\n", devtoname(sc->cdev));
```

**What this does:**

- **`make_dev_args_init(&args)`** - Initializes the args structure with defaults.
- **`args.mda_devsw = &myfirst_cdevsw`** - Associates this cdev with our cdevsw.
- **`args.mda_uid / gid / mode`** - Sets ownership and permissions. `0600` means root read/write only.
- **`args.mda_si_drv1 = sc`** - Stores the softc pointer so `open()` / `close()` can retrieve it.
- **`make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit)`** - Creates `/dev/myfirst0` (or `myfirst1`, etc., based on unit number).
- **Error handling:** If `make_dev_s()` fails, destroy the mutex and return the error.

**Important:** We save the `struct cdev *` in `sc->cdev` so we can destroy it later in `detach()`.

**Add the cdev field to the softc:**

Update `struct myfirst_softc`:

```c
struct myfirst_softc {
        device_t        dev;
        int             unit;
        struct mtx      mtx;
        uint64_t        attach_ticks;
        uint64_t        open_count;
        uint64_t        bytes_read;
        int             is_attached;
        int             is_open;

        struct cdev     *cdev;  /* /dev node */
};
```

### Destroying the Device Node in detach()

In `detach()`, you must remove the `/dev` node **before** the softc is freed.

Add this near the start of `myfirst_detach()`, after the `is_open` check:

```c
        /* Destroy /dev node */
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }
```

**What this does:**

- **`destroy_dev(sc->cdev)`** - Removes `/dev/myfirst0` from the filesystem. Any open file descriptors are invalidated, and subsequent operations on them return errors.
- **`sc->cdev = NULL`** - Clear the pointer (defensive programming).

**Order matters:** Destroy the device node **before** destroying the mutex or freeing other resources. This ensures no user-space operations can reach your driver after detach starts tearing things down.

### Build, Test, and Verify

Let's compile and test the new device node:

**1. Clean and build:**

```bash
% make clean && make
```

**2. Load the driver:**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
```

**3. Check the device node:**

```bash
% ls -l /dev/myfirst0
crw-------  1 root  wheel  0x5a Nov  6 15:45 /dev/myfirst0
```

Success! The device node exists.

**4. Test open and close:**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
(no output, immediate EOF)
```

Check dmesg:

```bash
% dmesg | tail -n 2
myfirst0: Device opened (count: 1)
myfirst0: Device closed
```

Your `open()` and `close()` handlers ran.

**5. Test write:**

```bash
% sudo sh -c 'echo "hello" > /dev/myfirst0'
% dmesg | tail -n 2
myfirst0: Device opened (count: 2)
myfirst0: Device closed
```

Write succeeded (though the data was discarded).

**6. Unload the driver:**

```bash
% sudo kldunload myfirst
% ls -l /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

The device node was correctly destroyed on unload.

### What Just Happened?

- You created a character device switch (`cdevsw`) mapping syscalls to your functions.
- You implemented `open()` and `close()` handlers that track state.
- You stubbed `read()` and `write()` to prove the plumbing works.
- You created `/dev/myfirst0` in `attach()` and destroyed it in `detach()`.
- User programs can now `open("/dev/myfirst0", ...)` and interact with your driver.

### Common Device Node Mistakes

**Mistake 1: Forgetting to destroy the device node in detach**

**Wrong:**

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        mtx_destroy(&sc->mtx);
        return (0);
        /* Forgot destroy_dev(sc->cdev)! */
}
```

**Why it's wrong:** The device node persists after unload. Attempting to open it crashes the kernel (code is gone, but the node remains).

**Right:** Always call `destroy_dev()` in detach.

---

**Mistake 2: Accessing softc in open/close without checking is_attached**

**Wrong:**

```c
static int
myfirst_open(struct cdev *dev, ...)
{
        struct myfirst_softc *sc = dev->si_drv1;
        /* No check if sc or sc->is_attached is valid */
        mtx_lock(&sc->mtx);  /* Might be NULL or freed! */
        ...
}
```

**Why it's wrong:** If `detach()` runs concurrently, the softc might be invalid.

**Right:** Check `sc != NULL` and `sc->is_attached` before accessing state.

---

**Mistake 3: Using make_dev() instead of make_dev_s()**

**Old pattern:**

```c
sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT, GID_WHEEL, 0600, "myfirst%d", sc->unit);
if (sc->cdev == NULL) {
        /* Error handling */
}
```

**Why it's dated:** `make_dev()` can fail and return NULL, requiring awkward error checks.

**Modern pattern:** `make_dev_s()` returns an error code, making error handling cleaner:

```c
error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
if (error != 0) {
        /* Handle error */
}
```

**Prefer `make_dev_s()`** in new code.

### Quick Self-Check

Before moving forward, confirm:

1. **What is the cdevsw?**
   Answer: A structure mapping syscalls (`open`, `read`, `write`, etc.) to driver functions.

2. **How does open() retrieve the softc?**
   Answer: Via `dev->si_drv1`, which we set when creating the device node.

3. **When must you call destroy_dev()?**
   Answer: In `detach()`, before the softc is freed.

4. **Why do we check `is_attached` in open()?**
   Answer: To ensure the device hasn't started detaching, which could lead to accessing freed memory.

5. **What does the stub read() return?**
   Answer: 0 (EOF), indicating no data available.

If those are clear, you're ready to add **observability via sysctl** in the next section.

---


## A Tiny Control Plane: Read-Only sysctl

Device nodes in `/dev` let user programs send and receive data, but they're not the only way to expose your driver to the outside world. **Sysctls** provide a lightweight control and observability plane, letting users and administrators query driver state, read statistics, and (optionally) tune parameters at runtime.

In this section, we'll add a **read-only sysctl** that exposes basic driver statistics. This gives you a taste of FreeBSD's sysctl infrastructure without committing to full read-write tunables or complex hierarchies (those return when we look at observability and debugging in Part 5).

By the end, you'll have:

- A sysctl node under `dev.myfirst.0.*` showing attach time, open count, and bytes read
- Understanding of static vs dynamic sysctls
- A pattern you can extend later for more complex observability

### Why Sysctls Matter

Sysctls provide **out-of-band observability**, a way to inspect driver state without opening the device or triggering I/O. They're essential for:

- **Debugging:** "Is the driver actually attached? What's the current state?"
- **Monitoring:** "How many times has this device been opened? Any errors?"
- **Tuning:** (Read-write sysctls, covered later) "Adjust the buffer size or timeout value."

**Example use case:** A network interface might expose `dev.em.0.rx_packets` and `dev.em.0.tx_errors` so monitoring tools can track performance without analyzing packet flows.

**Mental model:** Sysctls are like a "status dashboard" on the side of your driver, visible via `sysctl` commands without affecting normal operation.

### The FreeBSD Sysctl Tree

Sysctls are organized hierarchically, like a filesystem:

```ini
kern.ostype = "FreeBSD"
hw.ncpu = 8
dev.em.0.rx_packets = 123456
```

**Common top-level branches:**

- `kern.*` - Kernel parameters
- `hw.*` - Hardware information
- `dev.*` - Device-specific nodes (this is where your driver goes)
- `net.*` - Network stack parameters

**Your driver's namespace:** `dev.<drivername>.<unit>.*`

For `myfirst`, that means `dev.myfirst.0.*` for the first instance.

### Static vs Dynamic Sysctls

**Static sysctls:**

- Declared at compile time using `SYSCTL_*` macros
- Simple to define, but can't be created/destroyed dynamically
- Good for driver-wide settings or constants

**Example:**

```c
static int myfirst_debug = 0;
SYSCTL_INT(_hw, OID_AUTO, myfirst_debug, CTLFLAG_RWTUN,
    &myfirst_debug, 0, "Enable debug logging");
```

**Dynamic sysctls:**

- Created at runtime (usually in `attach()`)
- Can be destroyed in `detach()`
- Good for per-device state (like stats for `myfirst0`, `myfirst1`, etc.)

**For this chapter, we'll use dynamic sysctls** so each device instance has its own nodes.

### Adding a Sysctl Context to the softc

Dynamic sysctls require a **sysctl context** (`struct sysctl_ctx_list`) to track nodes you create. This makes cleanup automatic when you free the context.

Add these fields to `struct myfirst_softc`:

```c
struct myfirst_softc {
        device_t        dev;
        int             unit;
        struct mtx      mtx;
        uint64_t        attach_ticks;
        uint64_t        open_count;
        uint64_t        bytes_read;
        int             is_attached;
        int             is_open;
        struct cdev     *cdev;

        /* Sysctl context for dynamic nodes */
        struct sysctl_ctx_list  sysctl_ctx;
        struct sysctl_oid       *sysctl_tree;  /* Root of our subtree */
};
```

**What these fields do:**

- `sysctl_ctx` - Tracks all sysctl nodes we create. When we call `sysctl_ctx_free()`, all nodes are destroyed automatically.
- `sysctl_tree` - Root OID (Object Identifier) for `dev.myfirst.0.*`. Child nodes attach here.

### Creating the Sysctl Tree in attach()

Add this code to `myfirst_attach()`, after creating the `/dev` node:

```c
        /* Initialize sysctl context */
        sysctl_ctx_init(&sc->sysctl_ctx);

        /* Create device sysctl tree: dev.myfirst.0 */
        sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "stats", CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
            "Driver statistics");

        if (sc->sysctl_tree == NULL) {
                device_printf(dev, "Failed to create sysctl tree\n");
                destroy_dev(sc->cdev);
                mtx_destroy(&sc->mtx);
                return (ENOMEM);
        }

        /* Add individual sysctl nodes */
        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "attach_ticks", CTLFLAG_RD,
            &sc->attach_ticks, 0, "Tick count when driver attached");

        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "open_count", CTLFLAG_RD,
            &sc->open_count, 0, "Number of times device was opened");

        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "bytes_read", CTLFLAG_RD,
            &sc->bytes_read, 0, "Total bytes read from device");

        device_printf(dev, "Sysctl tree created under dev.myfirst.%d.stats\n",
            sc->unit);
```

**What this does:**

1. **`sysctl_ctx_init(&sc->sysctl_ctx)`** - Initializes the context (must be first).

2. **`SYSCTL_ADD_NODE()`** - Creates a subtree node `dev.myfirst.0.stats`. Parameters:
   - `&sc->sysctl_ctx` - Context that owns this node.
   - `SYSCTL_CHILDREN(device_get_sysctl_tree(dev))` - Parent (the device's sysctl tree).
   - `OID_AUTO` - Auto-assign an OID number.
   - `"stats"` - Node name.
   - `CTLFLAG_RD | CTLFLAG_MPSAFE` - Read-only, MP-safe.
   - `0` - Handler function (none for a node).
   - `"Driver statistics"` - Description.

3. **`SYSCTL_ADD_U64()`** - Adds a 64-bit unsigned integer sysctl. Parameters:
   - `&sc->sysctl_ctx` - Context.
   - `SYSCTL_CHILDREN(sc->sysctl_tree)` - Parent (`stats` subtree).
   - `OID_AUTO` - Auto-assign OID.
   - `"attach_ticks"` - Leaf name.
   - `CTLFLAG_RD` - Read-only.
   - `&sc->attach_ticks` - Pointer to the variable to expose.
   - `0` - Format hint (0 = default).
   - `"Tick count..."` - Description.

4. **Error handling:** If node creation fails, clean up and return `ENOMEM`.

**Result:** You now have three sysctls:

- `dev.myfirst.0.stats.attach_ticks`
- `dev.myfirst.0.stats.open_count`
- `dev.myfirst.0.stats.bytes_read`

### Destroying the Sysctl Tree in detach()

Cleanup is simple: free the context, and all nodes are destroyed automatically.

Add this to `myfirst_detach()`, after destroying the device node:

```c
        /* Free sysctl context (destroys all nodes) */
        sysctl_ctx_free(&sc->sysctl_ctx);
```

That's it. One line cleans up everything.

**Why it's safe:** `sysctl_ctx_free()` walks the context's list and removes each node. As long as you created them all via the context, cleanup is automatic.

### Build, Load, and Test Sysctls

**1. Clean and build:**

```bash
% make clean && make
```

**2. Load the driver:**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 4
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
myfirst0: Sysctl tree created under dev.myfirst.0.stats
```

**3. Query the sysctls:**

```bash
% sysctl dev.myfirst.0.stats
dev.myfirst.0.stats.attach_ticks: 123456
dev.myfirst.0.stats.open_count: 0
dev.myfirst.0.stats.bytes_read: 0
```

**4. Open the device and check again:**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 1
```

The counter incremented!

**5. Unload and verify cleanup:**

```bash
% sudo kldunload myfirst
% sysctl dev.myfirst.0.stats
sysctl: unknown oid 'dev.myfirst.0.stats'
```

Sysctls were correctly destroyed.

### Making Sysctls More Useful

Right now, the sysctls just expose raw numbers. Let's make them more user-friendly.

**Add a human-readable uptime sysctl:**

Instead of exposing raw tick counts, compute uptime in seconds.

Add a handler function:

```c
/*
 * Sysctl handler for uptime_seconds.
 *
 * Computes how long the driver has been attached, in seconds.
 */
static int
sysctl_uptime_seconds(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        uint64_t uptime;

        uptime = (ticks - sc->attach_ticks) / hz;

        return (sysctl_handle_64(oidp, &uptime, 0, req));
}
```

**Register it in attach():**

```c
        SYSCTL_ADD_PROC(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "uptime_seconds", CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
            sc, 0, sysctl_uptime_seconds, "QU",
            "Seconds since driver attached");
```

**Test:**

```bash
% sysctl dev.myfirst.0.stats.uptime_seconds
dev.myfirst.0.stats.uptime_seconds: 42
```

Much more readable than raw tick counts!

### Read-Only vs Read-Write Sysctls

Our sysctls are read-only (`CTLFLAG_RD`). To make them writable, use `CTLFLAG_RW` and add a handler that validates input.

**Example (preview only, not implemented now):**

```c
static int
sysctl_set_debug_level(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new_level, error;

        new_level = sc->debug_level;
        error = sysctl_handle_int(oidp, &new_level, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);

        if (new_level < 0 || new_level > 3)
                return (EINVAL);  /* Reject invalid values */

        sc->debug_level = new_level;
        device_printf(sc->dev, "Debug level set to %d\n", new_level);

        return (0);
}
```

We'll return to read-write sysctls in Part 5, when we look at debugging and observability tooling in depth. For now, read-only exposure is enough.

### Sysctl Best Practices

**1. Expose meaningful metrics**

- Counters (packets, errors, opens, closes)
- State flags (attached, open, enabled)
- Derived values (uptime, throughput, utilization)

**Don't expose:**

- Internal pointers or addresses (security risk)
- Meaningless raw data (use handlers to format nicely)

---

**2. Use descriptive names and descriptions**

**Good:**

```c
SYSCTL_ADD_U64(..., "rx_packets", ..., "Packets received");
```

**Bad:**

```c
SYSCTL_ADD_U64(..., "cnt1", ..., "Counter");
```

---

**3. Group related sysctls under subtrees**

```text
dev.myfirst.0.stats.*    (statistics)
dev.myfirst.0.config.*   (tunable parameters)
dev.myfirst.0.debug.*    (debug flags and counters)
```

---

**4. Protect concurrent access**

If a sysctl reads or writes shared state, hold the appropriate lock:

```c
static int
sysctl_read_counter(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        uint64_t value;

        mtx_lock(&sc->mtx);
        value = sc->some_counter;
        mtx_unlock(&sc->mtx);

        return (sysctl_handle_64(oidp, &value, 0, req));
}
```

---

**5. Clean up in detach**

Always call `sysctl_ctx_free(&sc->sysctl_ctx)` in detach, or you leak OIDs.

### Common Sysctl Mistakes

**Mistake 1: Forgetting sysctl_ctx_init**

**Wrong:**

```c
SYSCTL_ADD_NODE(&sc->sysctl_ctx, ...);  /* Context not initialized! */
```

**Why it's wrong:** Uninitialized context causes panics or leaks.

**Right:** Call `sysctl_ctx_init(&sc->sysctl_ctx)` in attach before adding nodes.

---

**Mistake 2: Not freeing the context in detach**

**Wrong:**

```c
static int
myfirst_detach(device_t dev)
{
        /* ... destroy other resources ... */
        return (0);
        /* Forgot sysctl_ctx_free! */
}
```

**Why it's wrong:** Sysctl nodes persist after unload. Next access crashes.

**Right:** Always `sysctl_ctx_free(&sc->sysctl_ctx)` in detach.

---

**Mistake 3: Exposing raw pointers**

**Wrong:**

```c
SYSCTL_ADD_PTR(..., "softc_addr", ..., &sc, ...);  /* Security hole! */
```

**Why it's wrong:** Leaks kernel address space layout (KASLR bypass).

**Right:** Never expose pointers via sysctls.

### Quick Self-Check

Before moving forward, confirm:

1. **What is a sysctl?**
   Answer: A kernel variable or computed value exposed via the `sysctl` command.

2. **Where do driver sysctls live?**
   Answer: Under `dev.<drivername>.<unit>.*`.

3. **What must you call in attach() before adding nodes?**
   Answer: `sysctl_ctx_init(&sc->sysctl_ctx)`.

4. **What cleans up all sysctl nodes in detach()?**
   Answer: `sysctl_ctx_free(&sc->sysctl_ctx)`.

5. **Why use a handler instead of exposing a variable directly?**
   Answer: To compute derived values (like uptime) or validate writes.

If those are clear, you're ready to learn **error handling and clean unwind** in the next section.

---

## Error Paths & Clean Unwind

So far, we've written `attach()` assuming everything succeeds. But real drivers must handle failures gracefully: if memory allocation fails, if a resource isn't available, or if hardware misbehaves, your driver must **undo what it started** and return an error without leaving partial state behind.

This section teaches the **single-label unwind pattern**, the standard FreeBSD idiom for error cleanup. Master this, and your driver will never leak resources, no matter where failure strikes.

### Why Error Handling Matters

Poor error handling causes:

- **Resource leaks** (memory, locks, device nodes)
- **Kernel panics** (accessing freed memory, double-frees)
- **Inconsistent state** (device half-attached, locks initialized but not destroyed)

**Real-world impact:** A driver with sloppy error paths might work fine during normal operation, then panic the system when it encounters an unusual failure (out of memory, missing hardware, etc.).

**Your goal:** Ensure `attach()` either fully succeeds or fully fails, with no middle ground.

### The Single-Label Unwind Pattern

FreeBSD kernel code uses a **goto-based unwind pattern** for cleanup. It looks like this:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;
        int error;

        sc = device_get_softc(dev);
        sc->dev = dev;

        /* Step 1: Initialize mutex */
        mtx_init(&sc->mtx, "myfirst", NULL, MTX_DEF);

        /* Step 2: Allocate memory resource (example) */
        sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
            &sc->mem_rid, RF_ACTIVE);
        if (sc->mem_res == NULL) {
                device_printf(dev, "Failed to allocate memory resource\n");
                error = ENXIO;
                goto fail_mtx;
        }

        /* Step 3: Create device node */
        error = create_dev_node(sc);
        if (error != 0) {
                device_printf(dev, "Failed to create device node: %d\n", error);
                goto fail_mem;
        }

        /* Step 4: Create sysctls */
        error = create_sysctls(sc);
        if (error != 0) {
                device_printf(dev, "Failed to create sysctls: %d\n", error);
                goto fail_dev;
        }

        device_printf(dev, "Attached successfully\n");
        return (0);

fail_dev:
        destroy_dev(sc->cdev);
fail_mem:
        bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
fail_mtx:
        mtx_destroy(&sc->mtx);
        return (error);
}
```

**How it works:**

- Each initialization step has a corresponding cleanup label.
- If a step fails, jump to the label that undoes everything **completed so far**.
- Labels are arranged in **reverse order** of initialization.
- Each label falls through to the next, so a failure at step 4 undoes 3, 2, and 1.

**Why this pattern?**

- **Centralized cleanup:** All error paths converge on one unwind sequence.
- **Easy to maintain:** Adding a new step means adding one goto and one cleanup label.
- **No duplication:** You don't repeat cleanup code in every error branch.

### Applying the Pattern to myfirst

Let's refactor our `attach()` to handle errors properly.

**Before (no error handling):**

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        mtx_init(&sc->mtx, ...);
        create_dev_node(sc);
        create_sysctls(sc);

        return (0);  /* What if something failed? */
}
```

**After (with clean unwind):**

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;
        struct make_dev_args args;
        int error;

        sc = device_get_softc(dev);
        sc->dev = dev;
        sc->unit = device_get_unit(dev);

        /* Step 1: Initialize mutex */
        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

        /* Step 2: Record attach time and initialize state */
        sc->attach_ticks = ticks;
        sc->is_attached = 1;
        sc->is_open = 0;
        sc->open_count = 0;
        sc->bytes_read = 0;

        /* Step 3: Create /dev node */
        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_WHEEL;
        args.mda_mode = 0600;
        args.mda_si_drv1 = sc;

        error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
        if (error != 0) {
                device_printf(dev, "Failed to create device node: %d\n", error);
                goto fail_mtx;
        }

        /* Step 4: Initialize sysctl context */
        sysctl_ctx_init(&sc->sysctl_ctx);

        /* Step 5: Create sysctl tree */
        sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "stats", CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
            "Driver statistics");

        if (sc->sysctl_tree == NULL) {
                device_printf(dev, "Failed to create sysctl tree\n");
                error = ENOMEM;
                goto fail_dev;
        }

        /* Step 6: Add sysctl nodes */
        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "attach_ticks", CTLFLAG_RD,
            &sc->attach_ticks, 0, "Tick count when driver attached");

        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "open_count", CTLFLAG_RD,
            &sc->open_count, 0, "Number of times device was opened");

        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "bytes_read", CTLFLAG_RD,
            &sc->bytes_read, 0, "Total bytes read from device");

        device_printf(dev, "Attached successfully\n");
        return (0);

        /* Error unwinding (in reverse order of initialization) */
fail_dev:
        destroy_dev(sc->cdev);
        sysctl_ctx_free(&sc->sysctl_ctx);
fail_mtx:
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;
        return (error);
}
```

**Key improvements:**

- Every operation that can fail is checked.
- Failures jump to the appropriate cleanup label.
- The unwind sequence undoes exactly what succeeded.
- All paths return an error code (never return success after a failure).

### Label Naming Convention

Choose label names that indicate what needs undoing:

- `fail_mtx` - Destroy the mutex
- `fail_mem` - Release memory resource
- `fail_dev` - Destroy device node
- `fail_irq` - Release interrupt resource

Or use numbers if you prefer:

- `fail1`, `fail2`, `fail3`

Either works, but descriptive names make the code easier to read.

### Testing Error Paths

**Simulate a failure** to verify your unwind logic works.

Add a deliberate failure after mutex init:

```c
        mtx_init(&sc->mtx, ...);

        /* Simulate allocation failure for testing */
        if (1) {  /* Change to 0 to disable */
                device_printf(dev, "Simulated failure\n");
                error = ENXIO;
                goto fail_mtx;
        }
```

**Build and load:**

```bash
% make clean && make
% sudo kldload ./myfirst.ko
```

**Check dmesg:**

```bash
% dmesg | tail -n 2
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Simulated failure
```

**Verify cleanup:**

```bash
% devinfo | grep myfirst
(no output - device didn't attach)

% ls /dev/myfirst*
ls: cannot access '/dev/myfirst*': No such file or directory
```

**Key observation:** The driver failed cleanly. No device node, no sysctl leaks, no panic.

Now disable the simulated failure and test normal attach again.

### Common Error Handling Mistakes

**Mistake 1: Not checking return values**

**Wrong:**

```c
make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
/* Forgot to check error! */
```

**Why it's wrong:** If `make_dev_s()` fails, `sc->cdev` might be NULL or garbage, and you proceed as if everything's fine.

**Right:** Always check `error` and branch accordingly.

---

**Mistake 2: Partial cleanup**

**Wrong:**

```c
fail_dev:
        destroy_dev(sc->cdev);
        return (error);
        /* Forgot to destroy mutex! */
```

**Why it's wrong:** Mutex remains initialized. Next load panics on re-init.

**Right:** Each label must undo **everything** initialized before it.

---

**Mistake 3: Double-cleanup**

**Wrong:**

```c
fail_dev:
        destroy_dev(sc->cdev);
        mtx_destroy(&sc->mtx);
        goto fail_mtx;

fail_mtx:
        mtx_destroy(&sc->mtx);  /* Destroyed twice! */
        return (error);
}
```

**Why it's wrong:** Double-free or double-destroy causes panics.

**Right:** Each resource should be cleaned up exactly once, at its corresponding label.

---

**Mistake 4: Returning success after failure**

**Wrong:**

```c
if (error != 0) {
        goto fail_mtx;
}
return (0);  /* Even if we jumped to fail_mtx! */
```

**Why it's wrong:** The goto bypasses the return, but the pattern implies all error paths must **return an error code**.

**Right:** Ensure error labels end with `return (error)`.

### The Full Picture: Attach and Detach

**Attach logic:**

1. Initialize resources in order.
2. Check each step for failure.
3. On failure, jump to the unwind label corresponding to the last successful step.
4. Unwind labels fall through in reverse order, cleaning up everything.

**Detach logic:**

Detach is simpler, undo everything in reverse order of `attach()`, assuming full success:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* Refuse if device is open */
        if (sc->is_open) {
                device_printf(dev, "Cannot detach: device is open\n");
                return (EBUSY);
        }

        device_printf(dev, "Detaching\n");

        /* Undo in reverse order of attach */
        destroy_dev(sc->cdev);                /* Step 1: Drop user surface first */
        sysctl_ctx_free(&sc->sysctl_ctx);    /* Step 2: Free sysctl context */
        mtx_destroy(&sc->mtx);                /* Step 3: Destroy mutex last */
        sc->is_attached = 0;

        return (0);
}
```

**Symmetry:** Every `attach()` step has a corresponding `detach()` action, in reverse order.

**Ordering note:** We destroy the device (`destroy_dev`) before freeing the sysctl context and destroying the mutex. This follows the Chapter 6 pitfall guidance: "Destroy device before locks." The `destroy_dev()` call blocks until all file operations drain, ensuring no code paths can reach our locks after the device is gone.

### Defensive Programming Checklist

Before declaring your error paths done, check:

- [ ] Every function that can fail is checked
- [ ] Every error sets `error` and jumps to a cleanup label
- [ ] Every cleanup label undoes exactly what preceded it
- [ ] Labels are in reverse order of initialization
- [ ] `detach()` undoes everything `attach()` did, in reverse order
- [ ] No resource is freed twice
- [ ] No resource is leaked on failure

### Quick Self-Check

Before moving forward, confirm:

1. **What is the single-label unwind pattern?**
   Answer: A goto-based cleanup sequence where each label undoes progressively more resources, in reverse order of initialization.

2. **Why are cleanup labels in reverse order?**
   Answer: Because you must undo the most recent step first, then earlier steps, walking backward through initialization.

3. **What must every error path do before returning?**
   Answer: Jump to the appropriate cleanup label and ensure `return (error)` is executed.

4. **How do you test error paths?**
   Answer: Simulate failures (e.g., force an allocation to fail) and verify cleanup is correct (no leaks, no panics).

5. **When should detach refuse to proceed?**
   Answer: When the device is still in use (e.g., `is_open` is true), return `EBUSY`.

If those are clear, you're ready to explore **real driver examples in the FreeBSD source tree**.

---

## Look-in-Tree Anchors (Reference Only)

You've built a minimal driver from first principles. Now let's **anchor your understanding** by pointing you to real FreeBSD 14.3 drivers that demonstrate the same patterns you've just learned. This section is a **guided tour**, not an exhaustive walk-through, think of it as a reading list for when you want to see how production code applies the lessons from this chapter.

### Why Look at Real Drivers?

Real drivers show you:

- How patterns scale to complex hardware
- What code looks like in context (full file, not just snippets)
- Variations on the patterns (different attach logic, resource types, error handling styles)
- FreeBSD idioms and conventions in practice

**You're not expected to understand every line** of these drivers right now. The goal is to **recognize the scaffolding** you've already built, and see how it extends to more capable drivers.

### Anchor 1: `/usr/src/sys/dev/null/null.c`

**What it is:** The `/dev/null`, `/dev/zero`, and `/dev/full` pseudo-devices.

**Why study it:**

- Simplest possible character devices
- No hardware, no resources, just cdevsw + MOD_LOAD handler
- Shows how `read()` and `write()` are implemented (even if trivial)
- Good reference for stubbed I/O

**What to look for:**

- The `cdevsw` structures (use `grep -n cdevsw` to find them)
- The `null_write()` and `zero_read()` handlers
- The module loader (`null_modevent()`, use `grep -n modevent` to find it)
- How `make_dev_credf()` is used (use `grep -n make_dev` to find it)

**File location:**

```bash
% less /usr/src/sys/dev/null/null.c
```

**Quick scan:**

```bash
% grep -n "cdevsw\|make_dev" /usr/src/sys/dev/null/null.c
```

### Anchor 2: `/usr/src/sys/dev/led/led.c`

**What it is:** The LED control framework, used by platform-specific drivers to expose LEDs as `/dev/led/*`.

**Why study it:**

- Still simple, but shows resource management (callouts, lists)
- Demonstrates dynamic device creation per LED
- Uses locking (`struct mtx`)
- Shows how drivers manage multiple instances

**What to look for:**

- The `ledsc` structure (near the top of the file; use `grep -n ledsc` to find it), analogous to your softc
- The `led_create()` function (use `grep -n "led_create\|led_destroy"` to find it), which creates device nodes dynamically
- The `led_destroy()` function, cleanup pattern
- How the global LED list is protected with a mutex

**File location:**

```bash
% less /usr/src/sys/dev/led/led.c
```

**Quick scan:**

```bash
% grep -n "ledsc\|led_create\|led_destroy" /usr/src/sys/dev/led/led.c
```

### Anchor 3: `/usr/src/sys/net/if_tuntap.c`

**What it is:** The `tun` and `tap` pseudo-network interfaces (tunnel devices).

**Why study it:**

- Hybrid driver: character device **and** network interface
- Shows how to register with `ifnet` (the network stack)
- More complex lifecycle (clone devices, per-open state)
- Good example of real-world locking and concurrency

**What to look for:**

- The `struct tuntap_softc` (use `grep -n "struct tuntap_softc"` to find it), much richer than yours
- The `tun_create()` function, which registers the `ifnet`
- The `cdevsw` and how it coordinates with the network side
- Use of `if_attach()` and `if_detach()` for network integration

**File location:**

```bash
% less /usr/src/sys/net/if_tuntap.c
```

**Warning:** This is a large, complex file (~2000 lines). Don't try to understand everything. Focus on:

```bash
% grep -n "tuntap_softc\|if_attach\|make_dev" /usr/src/sys/net/if_tuntap.c | head -20
```

### Anchor 4: `/usr/src/sys/dev/uart/uart_bus_pci.c`

**What it is:** PCI attachment glue for UART (serial port) devices.

**Why study it:**

- Real hardware driver (PCI bus)
- Shows how `probe()` checks PCI IDs
- Demonstrates resource allocation (I/O ports, IRQs)
- Error unwinding in `attach()`

**What to look for:**

- The `uart_pci_probe()` function (use `grep -n uart_pci_probe` to find it), PCI ID matching
- The `uart_pci_attach()` function, resource allocation
- Use of `bus_alloc_resource()` and `bus_release_resource()`
- The `device_method_t` table (use `grep -n device_method` to find it)

**File location:**

```bash
% less /usr/src/sys/dev/uart/uart_bus_pci.c
```

**Quick scan:**

```bash
% grep -n "uart_pci_probe\|uart_pci_attach\|device_method" /usr/src/sys/dev/uart/uart_bus_pci.c
```

**Tip:** This file is small (~250 lines) and very clean. It's a great example of a real Newbus driver.

### Anchor 5: `DRIVER_MODULE` and `MODULE_VERSION` Patterns

Look for these macros at the bottom of driver files:

```bash
% grep -rn 'DRIVER_MODULE\|MODULE_VERSION' /usr/src/sys/dev/null/ /usr/src/sys/dev/led/
```

You will see the same registration patterns you used in `myfirst`. For drivers that attach to `nexus` and provide their own `identify` method, the pattern in `/usr/src/sys/crypto/aesni/aesni.c` and `/usr/src/sys/dev/sound/dummy.c` is the closest match to what you wrote.

### How to Use These Anchors

**1. Start with null.c**

Read the whole file, it's short (~220 lines). You should recognize almost everything.

**2. Skim led.c**

Focus on the structure and lifecycle (creation/destruction). Don't get lost in the state machine.

**3. Preview if_tuntap.c**

Open it, scroll through, notice the hybrid structure (cdevsw + ifnet). Don't try to understand it all; just see the shape.

**4. Study uart_bus_pci.c**

Read `probe()` and `attach()`. This is your bridge to real hardware drivers (covered in Part 4).

**5. Compare to your driver**

For each anchor, ask:

- What's similar to my `myfirst` driver?
- What's different?
- What new concepts do I see (callouts, ifnet, PCI resources)?

**6. Note what to learn next**

When you see something unfamiliar (e.g., `callout_reset`, `if_attach`, `bus_alloc_resource`), jot it down. These are topics for later chapters.

### Quick Tour: Common Patterns Across Drivers

| Pattern                   | null.c       | led.c | if_tuntap.c | uart_pci.c | myfirst.c     |
|---------------------------|--------------|-------|-------------|------------|---------------|
| Uses `cdevsw`             | yes          | yes   | yes         | no         | yes           |
| Uses `ifnet`              | no           | no    | yes         | no         | no            |
| Newbus probe/attach       | no           | no    | no (clone)  | yes        | yes           |
| Has an `identify` method  | no           | no    | no          | no         | yes           |
| Module load handler       | yes          | yes   | yes         | no (Newbus)| no (Newbus)   |
| Allocates softc           | no           | no    | yes         | yes        | yes (Newbus)  |
| Uses locking              | no           | yes   | yes         | yes        | yes           |
| Allocates bus resources   | no           | no    | no          | yes        | no            |
| Creates `/dev` nodes      | yes          | yes   | yes         | no         | yes           |

### What to Skip (For Now)

When reading these drivers, don't get stuck on:

- Hardware register access (`bus_read_4`, `bus_write_2`)
- Interrupt setup (`bus_setup_intr`, handler registration)
- DMA (`bus_dma_tag_create`, `bus_dmamap_load`)
- Advanced locking (read-mostly locks, lock order)
- Network packet handling (`mbuf` chains, `if_transmit`)

You'll learn these in their dedicated chapters. Right now, focus on **structure and lifecycle**.

### Self-Study Exercise

Pick one anchor (`null.c` recommended for beginners) and:

1. Read the entire file
2. Identify the `cdevsw` structure
3. Find the `open`, `close`, `read`, `write` handlers
4. Trace the module load/unload flow
5. Compare to your `myfirst` driver

Write a paragraph in your lab logbook: "Here's what I learned from [driver name]."

### Quick Self-Check

Before moving to the labs, confirm:

1. **Why look at real drivers?**
   Answer: To see patterns in context, learn idioms, and bridge from minimal examples to production code.

2. **Which driver is simplest?**
   Answer: `null.c`, it's pseudo-only with no state or resources.

3. **Which driver shows hybrid structure?**
   Answer: `if_tuntap.c`, it's both a character device and a network interface.

4. **What should you skip when reading complex drivers?**
   Answer: Hardware-specific details (registers, DMA, interrupts) until later chapters cover them.

5. **How should you use these anchors?**
   Answer: As reference examples, not detailed study material. Skim, compare, note new concepts, move on.

If those are clear, you're ready for **hands-on labs** where you'll build, test, and extend your driver.

---

## Hands-On Labs

You've read about driver structure, seen the patterns, and walked through the code. Now it's time to **build, test, and validate** your understanding through four practical labs. Each lab is a checkpoint, ensuring you've mastered the concepts before moving forward.

### Lab Overview

| Lab | Focus | Duration | Key Learning |
|-----|-------|----------|-------------|
| Lab 7.1 | Source Hunt | 20-30 min | Navigate FreeBSD source, identify patterns |
| Lab 7.2 | Build & Load | 30-40 min | Compile, load, verify lifecycle |
| Lab 7.3 | Device Node | 30-40 min | Create `/dev`, test open/close |
| Lab 7.4 | Error Handling | 30-45 min | Simulate failures, verify unwinding |

**Total time:** 2-2.5 hours if you complete all labs in one session.

**Prerequisites:**

- FreeBSD 14.3 lab environment from Chapter 2
- `/usr/src` installed
- Basic shell and editor skills
- `~/drivers/myfirst` project from earlier sections

Let's begin.

---

### Lab 7.1: Source Code Scavenger Hunt

**Objective:** Build familiarity with the FreeBSD source tree by finding and identifying driver patterns.

**Skills practiced:**

- Navigating `/usr/src/sys`
- Using `grep` and `find`
- Reading real driver code

**Instructions:**

**1. Locate the null driver:**

```bash
% find /usr/src/sys -name "null.c" -type f
```

Expected output:

```text
/usr/src/sys/dev/null/null.c
```

**2. Open and scan:**

```bash
% less /usr/src/sys/dev/null/null.c
```

**3. Find the cdevsw structures:**

Search for `cdevsw` inside `less` by typing `/cdevsw` and pressing Enter.

You should land on lines defining `null_cdevsw`, `zero_cdevsw`, and `full_cdevsw`.

**4. Find the module event handler:**

Search for `modevent`:

```text
/modevent
```

You should see `null_modevent()`.

**5. Identify device creation:**

Search for `make_dev`:

```text
/make_dev
```

You should find three calls creating `/dev/null`, `/dev/zero`, and `/dev/full`.

**6. Compare to your driver:**

Open your `myfirst.c` and compare:

- How does `null.c` create device nodes? (Answer: `make_dev_credf` in the module loader)
- How does your driver create them? (Answer: `make_dev_s` in `attach()`)

**7. Find the LED driver:**

```bash
% find /usr/src/sys -name "led.c" -path "*/dev/led/*"
```

**8. Scan for softc:**

```bash
% grep -n "struct ledsc" /usr/src/sys/dev/led/led.c | head -5
```

You should see the `struct ledsc` definition near the top of the file, right after the `#include` block.

**9. Repeat for if_tuntap:**

```bash
% less /usr/src/sys/net/if_tuntap.c
```

Search for `tuntap_softc`. Notice how much richer it is than your minimal softc.

**10. Record your findings:**

In your lab logbook, write:

```text
Lab 7.1 Completed:
- Located null.c, led.c, if_tuntap.c
- Identified cdevsw, module loader, and softc structures
- Compared patterns to myfirst driver
- Key insight: [your observation]
```

**Success criteria:**

- [ ] Found all three driver files
- [ ] Located cdevsw and softc in each
- [ ] Identified device creation calls
- [ ] Compared to your driver

**If stuck:** Use `grep -r "DRIVER_MODULE" /usr/src/sys/dev/null/` to find key macros.

---

### Lab 7.2: Build, Load, and Verify Lifecycle

**Objective:** Compile your driver, load it into the kernel, verify lifecycle events, and unload cleanly.

**Skills practiced:**

- Building kernel modules
- Loading/unloading with `kldload`/`kldunload`
- Inspecting `dmesg` and `devinfo`

**Instructions:**

**1. Navigate to your driver:**

```bash
% cd ~/drivers/myfirst
```

**2. Clean and build:**

```bash
% make clean
% make
```

Verify `myfirst.ko` was created:

```bash
% ls -lh myfirst.ko
-rwxr-xr-x  1 youruser yourgroup  8.5K Nov  6 16:00 myfirst.ko
```

**3. Load the module:**

```bash
% sudo kldload ./myfirst.ko
```

**4. Check kernel messages:**

```bash
% dmesg | tail -n 5
```

Expected output:

```text
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
myfirst0: Sysctl tree created under dev.myfirst.0.stats
```

**5. Verify device tree:**

```bash
% devinfo -v | grep myfirst
  myfirst0
```

**6. Check device node:**

```bash
% ls -l /dev/myfirst0
crw-------  1 root  wheel  0x5a Nov  6 16:00 /dev/myfirst0
```

**7. Query sysctls:**

```bash
% sysctl dev.myfirst.0.stats
dev.myfirst.0.stats.attach_ticks: 123456
dev.myfirst.0.stats.open_count: 0
dev.myfirst.0.stats.bytes_read: 0
```

**8. Unload the module:**

```bash
% sudo kldunload myfirst
```

**9. Verify cleanup:**

```bash
% dmesg | tail -n 2
myfirst0: Detaching: uptime 1234 ticks, opened 0 times
```

```bash
% ls /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

```bash
% sysctl dev.myfirst.0
sysctl: unknown oid 'dev.myfirst.0'
```

**10. Reload and verify idempotency:**

```bash
% sudo kldload ./myfirst.ko
% sudo kldunload myfirst
% sudo kldload ./myfirst.ko
% sudo kldunload myfirst
```

All cycles should succeed without errors.

**11. Record results:**

Lab logbook:

```text
Lab 7.2 Completed:
- Built myfirst.ko successfully
- Loaded without errors
- Verified attach messages in dmesg
- Verified /dev node and sysctls
- Unloaded cleanly
- Repeated load/unload cycle 3 times: all succeeded
```

**Success criteria:**

- [ ] Module builds without errors
- [ ] Loads without kernel panic
- [ ] Attach message appears in dmesg
- [ ] `/dev/myfirst0` exists while loaded
- [ ] Sysctls readable
- [ ] Unload removes everything
- [ ] Reload works reliably

**Troubleshooting:**

- If build fails, check Makefile syntax (tabs, not spaces).
- If load fails with "Exec format error," check kernel/source version match.
- If unload says "module busy," check nothing is holding the device open.

---

### Lab 7.3: Test Device Node Open/Close

**Objective:** Interact with `/dev/myfirst0` from user-space and verify your `open()` and `close()` handlers are called.

**Skills practiced:**

- User-space device access
- Monitoring driver logs
- Tracking state changes

**Instructions:**

**1. Load the driver:**

```bash
% sudo kldload ./myfirst.ko
```

**2. Open the device with `cat` (read):**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
```

(No output, immediate EOF)

**3. Check logs:**

```bash
% dmesg | tail -n 3
myfirst0: Device opened (count: 1)
myfirst0: Device closed
```

**4. Write to the device:**

```bash
% sudo sh -c 'echo "test" > /dev/myfirst0'
```

**5. Check logs again:**

```bash
% dmesg | tail -n 3
myfirst0: Device opened (count: 2)
myfirst0: Device closed
```

**6. Verify sysctl counter:**

```bash
% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 2
```

**7. Test exclusive access:**

Open two terminals.

Terminal 1:

```bash
% sudo sh -c 'exec 3<>/dev/myfirst0; sleep 10'
```

(This holds the device open for 10 seconds)

Terminal 2 (quickly, while terminal 1 is still sleeping):

```bash
% sudo sh -c 'cat < /dev/myfirst0'
cat: /dev/myfirst0: Device busy
```

Success! Exclusive access is enforced.

**8. Try to unload while open:**

Terminal 1 (keep the device open):

```bash
% sudo sh -c 'exec 3<>/dev/myfirst0; sleep 30'
```

Terminal 2:

```bash
% sudo kldunload myfirst
kldunload: can't unload file: Device busy
```

Check dmesg:

```bash
% dmesg | tail -n 2
myfirst0: Cannot detach: device is open
```

Perfect! Your `detach()` correctly refuses to unload while in use.

**9. Close and retry unload:**

Terminal 1: Wait for `sleep 30` to finish (or Ctrl+C to interrupt).

Terminal 2:

```bash
% sudo kldunload myfirst
(succeeds)
```

**10. Record results:**

Lab logbook:

```text
Lab 7.3 Completed:
- Opened device with cat, verified open/close logs
- Opened device with echo, verified counter increment
- Exclusive access enforced (second open returned EBUSY)
- Detach refused while device open
- Detach succeeded after close
```

**Success criteria:**

- [ ] Open triggers `open()` handler (logged)
- [ ] Close triggers `close()` handler (logged)
- [ ] Sysctl counter increments on each open
- [ ] Second open returns `EBUSY`
- [ ] Detach returns `EBUSY` while open
- [ ] Detach succeeds after close

**Troubleshooting:**

- If you don't see "Device opened" logs, check that `device_printf()` is present in your `open()` handler.
- If exclusive access isn't enforced, verify the `if (sc->is_open) return (EBUSY)` check in `open()`.

---

### Lab 7.4: Simulate Attach Failure and Verify Unwinding

**Objective:** Inject a deliberate failure into `attach()` and verify cleanup is correct (no leaks, no panics).

**Skills practiced:**

- Testing error paths
- Debugging attach failures
- Verifying resource cleanup

**Instructions:**

**1. Add a simulated failure:**

Edit `myfirst.c` and add this after mutex initialization in `attach()`:

```c
        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

        /* SIMULATED FAILURE FOR LAB 7.4 */
        device_printf(dev, "Simulating attach failure for testing\n");
        error = ENXIO;
        goto fail_mtx;

        /* (rest of attach continues...) */
```

**2. Rebuild:**

```bash
% make clean && make
```

**3. Try to load:**

```bash
% sudo kldload ./myfirst.ko
kldload: can't load ./myfirst.ko: Device not configured
```

**4. Check dmesg:**

```bash
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Simulating attach failure for testing
```

**5. Verify no leaks:**

```bash
% ls /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

```bash
% sysctl dev.myfirst.0
sysctl: unknown oid 'dev.myfirst.0'
```

```bash
% devinfo -v | grep myfirst
(no output)
```

Perfect! The device failed to attach, and no resources were left behind.

**6. Try to load again:**

```bash
% sudo kldload ./myfirst.ko
kldload: can't load ./myfirst.ko: Device not configured
```

Still fails cleanly (no double-initialization panics).

**7. Remove the simulated failure:**

Edit `myfirst.c` and delete or comment out the simulated failure block.

**8. Rebuild and load normally:**

```bash
% make clean && make
% sudo kldload ./myfirst.ko
% dmesg | tail -n 5
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
myfirst0: Sysctl tree created under dev.myfirst.0.stats
```

Success!

**9. Test another failure point:**

Inject failure after creating the device node:

```c
        error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
        if (error != 0) {
                device_printf(dev, "Failed to create device node: %d\n", error);
                goto fail_mtx;
        }

        /* SIMULATED FAILURE AFTER DEV NODE CREATION */
        device_printf(dev, "Simulating failure after dev node creation\n");
        error = ENOMEM;
        goto fail_dev;
```

**10. Rebuild and test:**

```bash
% make clean && make
% sudo kldload ./myfirst.ko
kldload: can't load ./myfirst.ko: Cannot allocate memory
```

```bash
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Simulating failure after dev node creation
```

**11. Verify `/dev` node was destroyed:**

```bash
% ls /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

Perfect! Even though the node was created, the error path destroyed it.

**12. Remove simulation and restore normal operation:**

Delete the second simulated failure, rebuild, and load normally.

**13. Record results:**

Lab logbook:

```text
Lab 7.4 Completed:
- Simulated failure after mutex init: cleanup correct
- Simulated failure after dev node creation: cleanup correct
- Verified no leaks in either case
- Verified no panics on repeated load attempts
- Restored normal operation
```

**Success criteria:**

- [ ] Simulated failure after mutex: no leaks
- [ ] Simulated failure after dev node: node destroyed
- [ ] Multiple load attempts don't panic
- [ ] Normal operation restored after removing simulation

**Troubleshooting:**

- If you see a panic, your error path has a bug. Check that every `goto` jumps to the correct label.
- If resources leak, ensure each cleanup label is reachable and correct.

---

### Labs Complete!

You've now:

- Navigated the FreeBSD source tree (Lab 7.1)
- Built, loaded, and verified your driver (Lab 7.2)
- Tested open/close and exclusive access (Lab 7.3)
- Verified error unwinding with simulated failures (Lab 7.4)

**Pat yourself on the back.** You've moved from reading about drivers to **building and testing one yourself**. This is a major milestone.

---

## Short Exercises

These exercises reinforce the concepts from this chapter. They're **optional but recommended** if you want to deepen your understanding before moving forward.

### Exercise 7.1: Add a Sysctl Flag

**Task:** Add a new read-only sysctl showing whether the device is currently open.

**Steps:**

1. In `attach()`, add:

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "is_open", CTLFLAG_RD,
    &sc->is_open, 0, "1 if device is currently open");
```

2. Rebuild, load, and test:

```bash
% sysctl dev.myfirst.0.stats.is_open
dev.myfirst.0.stats.is_open: 0

% sudo sh -c 'exec 3<>/dev/myfirst0; sysctl dev.myfirst.0.stats.is_open; exec 3<&-'
dev.myfirst.0.stats.is_open: 1
```

**Expected result:** The flag shows `1` while open, `0` after close.

---

### Exercise 7.2: Log First and Last Open

**Task:** Modify `open()` to log only the **first** open and `close()` to log only the **last** close.

**Hints:**

- Check `sc->open_count` before and after incrementing.
- In `close()`, decrement a counter and check if it reaches zero.

**Expected behavior:**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
myfirst0: Device opened for the first time
myfirst0: Device closed (no more openers)

% sudo sh -c 'cat < /dev/myfirst0'
(no log: not the first open)
myfirst0: Device closed (no more openers)
```

---

### Exercise 7.3: Add a "Reset Stats" Sysctl

**Task:** Add a write-only sysctl that resets `open_count` and `bytes_read` to zero.

**Steps:**

1. Define a handler:

```c
static int
sysctl_reset_stats(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int error, val;

        val = 0;
        error = sysctl_handle_int(oidp, &val, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);

        mtx_lock(&sc->mtx);
        sc->open_count = 0;
        sc->bytes_read = 0;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "Statistics reset\n");
        return (0);
}
```

2. Register it:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "reset_stats", CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE,
    sc, 0, sysctl_reset_stats, "I",
    "Write 1 to reset statistics");
```

3. Test:

```bash
% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 5

% sudo sysctl dev.myfirst.0.stats.reset_stats=1
dev.myfirst.0.stats.reset_stats: 0 -> 1

% dmesg | tail -n 1
myfirst0: Statistics reset

% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 0
```

---

### Exercise 7.4: Test Load/Unload 100 Times

**Task:** Write a script that loads and unloads your driver 100 times, checking for any failures or leaks.

**Script (~/drivers/myfirst/stress_test.sh):**

```bash
#!/bin/sh
set -e

for i in $(seq 1 100); do
        echo "Iteration $i"
        sudo kldload ./myfirst.ko
        sleep 0.1
        sudo kldunload myfirst
done

echo "Stress test completed: 100 cycles"
```

**Run:**

```bash
% chmod +x stress_test.sh
% ./stress_test.sh
```

**Expected result:** All iterations succeed without errors.

**If it fails:** Check `dmesg` for panic messages or leaked resources.

---

### Exercise 7.5: Compare Your Driver to null.c

**Task:** Open `/usr/src/sys/dev/null/null.c` side-by-side with your `myfirst.c`. List 5 similarities and 5 differences.

**Example observations:**

**Similarities:**

1. Both use `cdevsw` for character device operations.
2. Both create `/dev` nodes.
3. Both have `open` and `close` handlers.
4. Both return EOF on read.
5. Both log attach/detach events.

**Differences:**

1. `null.c` uses `MOD_LOAD` handler; `myfirst` uses Newbus.
2. `null.c` doesn't have a softc; `myfirst` does.
3. `null.c` creates multiple devices (`null`, `zero`, `full`); `myfirst` creates one.
4. `null.c` doesn't use sysctls; `myfirst` does.
5. `null.c` is stateless; `myfirst` tracks counters.

---

## Optional Challenges

These are **advanced exercises** for readers who want to push beyond the basics. Don't attempt these until you've completed all labs and exercises.

### Challenge 7.1: Implement a Simple Read Buffer

**Goal:** Instead of returning EOF immediately, return a fixed string on `read()`.

**Steps:**

1. Add a buffer to the softc:

```c
        char    read_buffer[64];  /* Data to return on read */
        size_t  read_len;         /* Length of valid data */
```

2. In `attach()`, populate the buffer:

```c
        snprintf(sc->read_buffer, sizeof(sc->read_buffer),
            "Hello from myfirst driver!\n");
        sc->read_len = strlen(sc->read_buffer);
```

3. In `myfirst_read()`, copy data to user-space:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        size_t len;
        int error;

        len = MIN(uio->uio_resid, sc->read_len);
        if (len == 0)
                return (0);  /* EOF */

        error = uiomove(sc->read_buffer, len, uio);
        return (error);
}
```

4. Test:

```bash
% sudo cat /dev/myfirst0
Hello from myfirst driver!
```

**Expected behavior:** Reading returns the string once, then EOF on subsequent reads.

---

### Challenge 7.2: Allow Multiple Openers

**Goal:** Remove the exclusive-access check so multiple programs can open the device simultaneously.

**Steps:**

1. Remove the `if (sc->is_open) return (EBUSY)` check in `open()`.
2. Use a **reference count** instead of a boolean flag:

```c
        int     open_refcount;  /* Number of current openers */
```

3. In `open()`:

```c
        mtx_lock(&sc->mtx);
        sc->open_refcount++;
        mtx_unlock(&sc->mtx);
```

4. In `close()`:

```c
        mtx_lock(&sc->mtx);
        sc->open_refcount--;
        mtx_unlock(&sc->mtx);
```

5. In `detach()`, refuse if `open_refcount > 0`:

```c
        if (sc->open_refcount > 0) {
                device_printf(dev, "Cannot detach: device has %d openers\n",
                    sc->open_refcount);
                return (EBUSY);
        }
```

6. Test with two terminals:

Terminal 1:

```bash
% sudo sh -c 'exec 3<>/dev/myfirst0; sleep 30'
```

Terminal 2:

```bash
% sudo sh -c 'cat < /dev/myfirst0'
(succeeds instead of returning EBUSY)
```

---

### Challenge 7.3: Add a Write Counter

**Goal:** Track how many bytes have been written to the device.

**Steps:**

1. Add to softc:

```c
        uint64_t        bytes_written;
```


**Note:** This is a discarding write handler for Chapter 7 only. We deliberately do not use `uiomove()` or store user data here. Full data movement, buffering, and `uiomove()` live in Chapter 9.

2. In `myfirst_write()`:

```c
        size_t len = uio->uio_resid;

        mtx_lock(&sc->mtx);
        sc->bytes_written += len;
        mtx_unlock(&sc->mtx);

        uio->uio_resid = 0;
        return (0);
```

3. Expose via sysctl:

```c
SYSCTL_ADD_U64(&sc->sysctl_ctx, ..., "bytes_written", ...);
```

4. Test:

```bash
% sudo sh -c 'echo "test" > /dev/myfirst0'
% sysctl dev.myfirst.0.stats.bytes_written
dev.myfirst.0.stats.bytes_written: 5
```

---

### Challenge 7.4: Create a Second Device (myfirst1)

**Goal:** Manually create a second device instance to test multi-device support.

**Hint:** Right now, your driver creates `myfirst0` automatically. To test multiple devices, you'd need to trigger a second probe/attach cycle. This is complex (requires bus-level manipulation or cloning), so consider it **research only**.

**Alternate approach:** Study `/usr/src/sys/net/if_tuntap.c` to see how it handles clone devices (creating new instances on demand).

---

### Challenge 7.5: Implement Rate-Limited Logging

**Goal:** Add a rate-limited log for open events (log at most once per second).

**Steps:**

1. Add to softc:

```c
        time_t  last_open_log;
```

2. In `open()`:

```c
        time_t now = time_second;

        if (now - sc->last_open_log >= 1) {
                device_printf(sc->dev, "Device opened (count: %lu)\n",
                    (unsigned long)sc->open_count);
                sc->last_open_log = now;
        }
```

3. Test by opening rapidly:

```bash
% for i in $(seq 1 10); do sudo sh -c 'cat < /dev/myfirst0'; done
```

**Expected behavior:** Only a few log messages appear (rate-limited).

---

## Pitfalls & Troubleshooting Decision Tree

Even with careful coding, you'll encounter issues. This section provides a **decision tree** to diagnose common problems quickly.

### Symptom: Driver Won't Load

**Check:**

- [ ] Does `freebsd-version -k` match `/usr/src` version?
  - **No:** Rebuild kernel or re-clone `/usr/src` for correct version.
  - **Yes:** Continue.

- [ ] Does `make` complete without errors?
  - **No:** Read compiler error messages. Common causes:
    - Missing semicolons
    - Mismatched braces
    - Undefined functions (missing includes)
  - **Yes:** Continue.

- [ ] Does `kldload` fail with "Exec format error"?
  - **Yes:** Kernel/module mismatch. Rebuild with matching sources.
  - **No:** Continue.

- [ ] Does `kldload` fail with "No such file or directory"?
  - **Yes:** Check module path (`./myfirst.ko` vs `/boot/modules/myfirst.ko`).
  - **No:** Continue.

- [ ] Check `dmesg` for attach errors:

```bash
% dmesg | tail -n 10
```

Look for error messages from your driver.

---

### Symptom: Device Node Doesn't Appear

**Check:**

- [ ] Did `attach()` succeed?

```bash
% dmesg | grep myfirst
```

Look for "Attached successfully" message.

- [ ] If attach failed, did error handling run?

Look for error messages in dmesg.

- [ ] Did `make_dev_s()` succeed?

Add logging:

```c
device_printf(dev, "make_dev_s returned: %d\n", error);
```

- [ ] Is the device node's name correct?

```bash
% ls -l /dev/myfirst*
```

Check spelling and unit number.

---

### Symptom: Kernel Panic on Load

**Check:**

- [ ] Did you forget `mtx_init()`?

Panic during `mtx_lock()` → forgot to initialize mutex.

- [ ] Did you dereference a NULL pointer?

Panic with "NULL pointer dereference" → check `device_get_softc()` return value.

- [ ] Did you corrupt memory?

Enable WITNESS and INVARIANTS in your kernel config, then rebuild:

```text
options WITNESS
options INVARIANTS
```

Reboot, reload your driver. WITNESS will catch lock violations.

---

### Symptom: Kernel Panic on Unload

**Check:**

- [ ] Did you forget `destroy_dev()`?

Unload panics when user tries to access the device node.

- [ ] Did you forget `mtx_destroy()`?

WITNESS-enabled kernels will panic on unload if locks aren't destroyed.

- [ ] Did you forget `sysctl_ctx_free()`?

Sysctl OID leaks can cause panics on reload.

- [ ] Is code still running when you unload?

Check for:
  - Open device nodes (`sc->is_open` should be false)
  - Active timers or callbacks (not used in this chapter, but common later)

---

### Symptom: "Device Busy" on Unload

**Check:**

- [ ] Is the device still open?

```bash
% fstat | grep myfirst
```

If a process has the device open, unload will fail.

- [ ] Did you return `EBUSY` from `detach()`?

Check `detach()` logic:

```c
if (sc->is_open) {
        return (EBUSY);
}
```

---

### Symptom: Sysctls Don't Appear

**Check:**

- [ ] Did `sysctl_ctx_init()` run?

- [ ] Did `SYSCTL_ADD_NODE()` succeed?

Add logging:

```c
if (sc->sysctl_tree == NULL) {
        device_printf(dev, "sysctl tree creation failed\n");
}
```

- [ ] Is the sysctl path correct?

```bash
% sysctl dev.myfirst
```

Check for typos in driver name or unit number.

---

### Symptom: Open/Close Not Logging

**Check:**

- [ ] Did you add `device_printf()` to the handlers?

- [ ] Is the cdevsw registered correctly?

Check that `args.mda_devsw = &myfirst_cdevsw` is set before `make_dev_s()`.

- [ ] Is `si_drv1` set correctly?

```c
args.mda_si_drv1 = sc;
```

If this is NULL, `open()` will fail.

---

### Symptom: Module Loads But Does Nothing

**Check:**

- [ ] Did `attach()` run?

```bash
% dmesg | grep myfirst
```

If you see no output at all and your driver attaches to `nexus`, the most likely cause is a missing `identify` method. Without it, nexus has no `myfirst` device to probe and your code is never called. Re-read the **Step 6: Implement Identify** section above and confirm that `DEVMETHOD(device_identify, myfirst_identify)` is present in your method table.

- [ ] Is the device on the correct bus?

For pseudo-devices, use `nexus` (and remember to provide `identify`). For PCI, use `pci`. For USB, use `usbus`.

- [ ] Did `probe()` return a non-error priority?

If `probe()` returns `ENXIO`, the driver will not attach. For our pseudo-device, `BUS_PROBE_DEFAULT` is the right value.

---

### General Debugging Tips

**Enable verbose boot:**

```bash
% sudo sysctl boot.verbose=1
```

Reload your driver to see more detailed messages.

**Use printf debugging:**

Add `device_printf()` statements at key points to trace execution flow.

**Check lock state:**

If using WITNESS, check lock order:

```bash
% sysctl debug.witness.fullgraph
```

**Save dmesg after panic:**

```bash
% sudo dmesg -a > panic.log
```

Analyze the log for clues.

---

## Self-Assessment Rubric

Use this rubric to evaluate your understanding before moving to Chapter 8.

### Core Knowledge

**Rate yourself (1-5, where 5 = fully confident):**

- [ ] I can explain what the softc is and why it exists. (Score: __/5)
- [ ] I understand probe/attach/detach lifecycle. (Score: __/5)
- [ ] I can create a device node using `make_dev_s()`. (Score: __/5)
- [ ] I can implement basic open/close handlers. (Score: __/5)
- [ ] I can add a read-only sysctl. (Score: __/5)
- [ ] I understand the single-label unwind pattern. (Score: __/5)
- [ ] I can test error paths with simulated failures. (Score: __/5)

**Total Score: __/35**

**Interpretation:**

- **30-35:** Excellent. You're ready for Chapter 8.
- **25-29:** Good. Review weak areas before proceeding.
- **20-24:** Adequate. Revisit labs and exercises.
- **<20:** Spend more time on this chapter.

---

### Practical Skills

**Can you do these without looking at notes?**

- [ ] Build a kernel module with `make`.
- [ ] Load a module with `kldload`.
- [ ] Check attach messages in `dmesg`.
- [ ] Query sysctls with `sysctl`.
- [ ] Open a device node with `cat` or shell redirection.
- [ ] Unload a module with `kldunload`.
- [ ] Simulate an attach failure.
- [ ] Verify cleanup after failure.

**Scoring:** 1 point per skill. **Target:** 7/8 or higher.

---

### Code Reading

**Can you recognize these patterns in real FreeBSD code?**

- [ ] Identify a `cdevsw` structure.
- [ ] Locate `probe()`, `attach()`, `detach()` methods.
- [ ] Find the softc structure definition.
- [ ] Identify `DRIVER_MODULE()` macro.
- [ ] Spot error unwinding with goto labels.
- [ ] Find `make_dev()` or `make_dev_s()` calls.
- [ ] Identify sysctl creation (`SYSCTL_ADD_*`).

**Scoring:** 1 point per pattern. **Target:** 6/7 or higher.

---

### Conceptual Understanding

**True or False:**

1. The softc is allocated by the driver in `attach()`. (**False**. Newbus allocates it from the size declared in `driver_t`.)
2. `probe()` should allocate resources. (**False**. `probe()` only inspects the device and decides whether to claim it; `attach()` does the allocation.)
3. `detach()` must undo everything `attach()` did. (**True**.)
4. Error labels should be in reverse order of initialization. (**True**.)
5. You can skip `mtx_destroy()` if the module is unloading. (**False**. Every `mtx_init()` needs a matching `mtx_destroy()`.)
6. Sysctls are automatically cleaned up when the module unloads. (**False**. Only if you call `sysctl_ctx_free()` on the per-device context you initialized.)
7. `make_dev_s()` is safer than `make_dev()`. (**True**. It returns an explicit error and avoids the race where `make_dev()` could fail without a clear way to report it.)
8. A pseudo-device that attaches to `nexus` must provide an `identify` method. (**True**. Without it the bus has no device to probe.)

**Scoring:** 1 point per correct answer. **Target:** 7/8 or higher.

---

### Overall Assessment

Add your scores:

- Core Knowledge: __/35
- Practical Skills: __/8
- Code Reading: __/7
- Conceptual Understanding: __/8

**Total: __/58**

**Grade:**

- **51-58:** A (Excellent mastery)
- **44-50:** B (Good understanding)
- **35-43:** C (Adequate, but review weak areas)
- **<35:** Revisit chapter before proceeding

---

## Wrapping Up & Pointers Forward

Congratulations! You've completed Chapter 7 and built your first FreeBSD driver from the ground up. Let's reflect on what you've accomplished and preview what's ahead.

### What You Built

Your `myfirst` driver is minimal but complete:

- **Lifecycle discipline:** Clean probe/attach/detach with no leaks.
- **User surface:** `/dev/myfirst0` node that opens and closes reliably.
- **Observability:** Read-only sysctls showing attach time, open count, and bytes read.
- **Error handling:** Single-label unwind pattern that recovers gracefully from failures.
- **Logging:** Proper use of `device_printf()` for lifecycle events and errors.

This isn't a toy. It's a **production-quality scaffold**, the same foundation every FreeBSD driver starts with.

### What You Learned

You now understand:

- How Newbus discovers and attaches drivers
- The role of the softc (per-device state)
- How to create and destroy device nodes
- How to expose metrics via sysctls
- How to handle errors without leaking resources
- How to test lifecycle paths (load/unload, open/close, simulated failures)

These skills are **transferable**. Whether you write a PCI driver, a USB driver, or a network interface, you'll use these same patterns.

### What's Still Missing (And Why)

Your driver doesn't do much yet:

- **Read/write semantics:** Stubbed, not implemented. (**Chapter 8 and 9**)
- **Buffering:** No queues, no ring buffers. (**Chapter 10**)
- **Hardware interaction:** No registers, no DMA, no interrupts. (**Part 4**)
- **Concurrency:** Mutex present but not exercised. (**Part 3**)
- **Real-world I/O:** No blocking, no poll/select. (**Chapter 10**)

This was intentional. **Master structure before complexity.** You wouldn't learn carpentry by building a skyscraper on day one. You'd start with a workbench, just like you did here.

### What's Next

**Chapter 8, Working with Device Files.** devfs permissions and ownership, persistent nodes, and the userland probes you will use to inspect and test your device.

**Chapters 9 and 10, Reading and Writing to Devices, plus Handling Input and Output Efficiently.** Implement read and write with `uiomove`, introduce buffering and flow control, and define blocking, non-blocking, and `poll` or `kqueue` semantics with proper error handling.

---

### Your Next Steps

**Before moving to Chapter 8:**

1. **Complete all labs** in this chapter if you haven't already.
2. **Attempt at least two exercises** to reinforce patterns.
3. **Test your driver thoroughly:** Load/unload 10 times, open/close 10 times, simulate one more failure.
4. **Commit your code to Git:** This is a milestone.

```bash
% cd ~/drivers/myfirst
% git add myfirst.c Makefile
% git commit -m "Chapter 7 complete: minimal driver with lifecycle discipline"
```

5. **Take a break.** You've earned it. Kernel programming is intense, and consolidation time matters.

**When you're ready for Chapter 8:**

- You'll extend this same driver (no restart from scratch).
- The structure you built here will carry forward.
- Concepts will build incrementally, not reset.

### A Final Word

Building a driver from scratch can feel overwhelming at first. But look at what you've accomplished:

- You started with nothing but a Makefile and a blank `.c` file.
- You built a driver that compiles, loads, attaches, operates, detaches, and unloads cleanly.
- You tested error paths and verified cleanup.
- You exposed state via sysctls and created a user-accessible device node.

**This is not beginner's luck. This is competence.** Most general-purpose software developers never touch a kernel module, let alone build one with disciplined attach and detach paths. You just did.

The journey from "hello module" to "production driver" is long, but you've taken the hardest step: **starting**. Every chapter from here adds one more layer of capability, one more tool in your kit.

Keep your lab logbook updated. Keep experimenting. Keep asking "why?" when something doesn't make sense. And most importantly, **keep building**.

Welcome to the world of FreeBSD driver development. You've earned your place here.

### See You in Chapter 8

In the next chapter, we'll breathe life into your device node by implementing real file semantics: managing per-open state, handling exclusive vs shared access, and preparing the ground for actual I/O.

Until then, enjoy your success. You've built something real, and that's worth celebrating.

*"The expert at anything was once a beginner." - Helen Hayes*
