---
title: "A First Look at the C Programming Language"
description: "This chapter introduces the C programming language for complete beginners."
author: "Edson Brandi"
date: "2025-08-05"
status: "draft"
part: 1
chapter: 4
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 25
---

# A First Look at the C Programming Language

Before we can dive into writing FreeBSD device drivers, we need to learn the language they’re written in. That language is C, short, powerful, and, admittedly, a little quirky. But don’t worry, you don’t need to be a programming expert to get started.

In this chapter, I’ll walk you through the basics of the C programming language, assuming absolutely no prior experience. If you’ve never written a line of code in your life, you’re in the right place. If you’ve done some programming in other languages like Python or JavaScript, that’s fine too; C might feel a little more manual, but we’ll tackle it together.

Our goal here isn’t to become master C programmers in one chapter. Instead, I want to introduce you to the language gently, showing you its syntax, its building blocks, and how it works in the context of UNIX systems like FreeBSD. Along the way, I’ll point out real-world examples taken directly from the FreeBSD source code to help ground the theory in actual practice.

By the time we’re done, you’ll be able to read and write basic C programs, understand the core syntax, and feel confident enough to take the next steps toward kernel development. But that part will come later, for now, let’s focus on learning the essentials.

## 4.1 Introduction

Let’s start at the beginning: what is C, and why is it important to us?

### What is C?
C is a programming language created in the early 1970s by Dennis Ritchie at Bell Labs. It was designed to write operating systems, and that’s still one of its biggest strengths today. In fact, most modern operating systems, including FreeBSD, Linux, and even parts of Windows and macOS, are written mainly in C.

C is fast, compact, and close to the hardware, but unlike assembly language, it’s still readable and expressive. You can write efficient, powerful code with C, but it also expects you to be careful. There’s no safety net: no automatic memory management, no runtime error messages, and not even built-in strings like in Python or JavaScript.

This might sound scary, but it’s actually a feature. When writing drivers or working inside the kernel, you want control, and C gives you that control.

### Why Should I Learn C for FreeBSD?

FreeBSD is written almost entirely in C, and that includes the kernel, device drivers, userland tools, and system libraries. If you want to write code that interacts with the operating system, whether it’s a new device driver or a custom kernel module, C is your entry point.
More specifically:

* The FreeBSD kernel APIs are written in C.
* All device drivers are implemented in C.
* Even debugging tools like dtrace and kgdb understand and expose C-level information.

So, to work with FreeBSD’s internals, you’ll need to understand how C code is written, structured, compiled, and used in the system.

### What If I’ve Never Programmed Before?

No problem! I’m writing this chapter with you in mind. We’ll take it one step at a time, starting with the simplest possible program and slowly building our way up. You’ll learn about:

* Variables and data types
* Functions and flow control
* Pointers, arrays, and structures
* How to read real code from the FreeBSD kernel

And don’t worry if any of those terms are unfamiliar right now, they’ll all make sense soon. I’ll provide plenty of examples, explain every step in plain language, and help you build your confidence as we go.

### How Is This Chapter Organised?

Here’s a quick preview of what’s coming up:

* We’ll begin by setting up your development environment on FreeBSD.
* Then, we’ll walk through your first C program, the classic “Hello, World!”.
* From there, we’ll cover the syntax and semantics of C: variables, loops, functions, and more.
* We’ll show you real examples from the FreeBSD source tree, so you can start learning how the system works under the hood.
* Finally, we’ll wrap up with some good practices and a look at what’s coming in the next chapter, where we begin applying C to the kernel world.

Are you ready? Let’s jump into the next section and get your environment set up so you can run your first C program on FreeBSD.

## 4.2 Setting Up Your Environment

Before we can start writing C code, we need to set up a working development environment. The good news? If you’re running FreeBSD, **you already have most of what you need**.

In this section, we’ll:

* Verify your C compiler is installed
* Compile your first program manually
* Learn how to use Makefiles for convenience

Let’s go step by step.

### Installing a C Compiler on FreeBSD

FreeBSD includes the Clang compiler as part of the base system, so you typically don’t need to install anything extra to start writing C code.

To confirm that Clang is installed and working, open a terminal and run:

	% cc --version	

You should see output like this:

	FreeBSD clang version 19.1.7 (https://github.com/llvm/llvm-project.git 
	llvmorg-19.1.7-0-gcd708029e0b2)
	Target: aarch64-unknown-freebsd14.3
	Thread model: posix
	InstalledDir: /usr/bin

If `cc` is not found, you can install the base development utilities running the following command as root:

	# pkg install llvm

But for almost all standard FreeBSD setups, Clang should already be ready to use.

Let’s write the classic “Hello, World!” program in C. This will verify that your compiler and terminal are working correctly.

Open a text editor like `ee`, `vi`, or `nano`, and create a file called `hello.c`:

	#include <stdio.h>

	int main(void) {
   	 	printf("Hello, World!\n");
   	 	return 0;
	}
	
Let’s break this down:

* `#include <stdio.h>` tells the compiler to include the Standard I/O header file, which provides printf.
* `int main(void)` defines the main entry point of the program.
* `printf(...)` writes a message to the terminal.
* `return 0;` indicates successful execution.

Now save the file and compile it:

	% cc -o hello hello.c

This tells Clang to:

* Compile `hello.c`
* Output the result to a file called `hello`

Run it:

	% ./hello
	Hello, World!
	
	
Congratulations! You just compiled and ran your first C program on FreeBSD.

### Using Makefiles

Typing long compile commands can get annoying as your programs grow. That’s where **Makefiles** come in handy.

A Makefile is a plain text file named Makefile that defines how to build your program. Here's a very simple one for our Hello World example:

	# Makefile for hello.c

	hello: hello.c
		cc -o hello hello.c
	
Attention: Every command line that will be executed by the shell within a Makefile rule must begin with a tab character, not spaces. If you use spaces, the make execution will fail."

To use it:

Save this in a file called Makefile (note the capital "M")
Run make in the same directory:


	% make
	cc -o hello hello.c

This is especially helpful when your project grows to include multiple files.

**Important Note:** One of the most common mistakes when writing your first Makefile is forgetting to use a TAB character at the beginning of each command line in a rule. In Makefiles, every line that should be executed by the shell must start with a TAB, not spaces. If you accidentally use spaces instead, `make` will produce an error and fail to run. This detail often trips up beginners, so be sure to check your indentation carefully!

This error will appear as shown below:

	% make
	make: "/home/ebrandi/hello/Makefile" line 4: Invalid line type
	make: Fatal errors encountered -- cannot continue
	make: stopped in /home/ebrandi/hello

### Installing the FreeBSD Source Code

As we move forward, we’ll look at examples from the actual FreeBSD kernel source. To follow along, it’s useful to have the FreeBSD source tree installed locally. 

To store a complete local copy of the FreeBSD source code, you will need approximately 3.6 GB of free disk space. You can install it using Git by running the following command:

	# git clone https://git.freebsd.org/src.git /usr/src
	
This will give you access to all source code, which we’ll reference frequently throughout this book.

### Summary

You now have a working development setup on FreeBSD! It was simple, wasn't it?

Here’s what you’ve accomplished:

* Verified the C compiler is installed
* Wrote and compiled your first C program
* Learned how to use Makefiles
* Cloned the FreeBSD source tree for future reference

These tools are all you need to start learning C, and later, to build your own kernel modules and drivers. In the next section, we’ll look at what makes up a typical C program and how it’s structured.

## 4.3 Anatomy of a C Program

Now that you've compiled your first "Hello, World!" program, let’s take a closer look at what’s actually going on inside that code. In this section, we’ll break down the basic structure of a C program and explain what each part does, step by step.

We’ll also introduce how this structure appears in the FreeBSD kernel code, so you can begin recognising familiar patterns in real-world systems programming.

### The Basic Structure

Every C program follows a similar structure:

	#include <stdio.h>

	int main(void) {
    	printf("Hello, World!\n");
    	return 0;
	}
Let’s dissect this line by line.

### `#include` Directives: Adding Libraries

	#include <stdio.h>

This line is handled by the **C preprocessor** before the program is compiled. It tells the compiler to include the contents of a system header file.

* `<stdio.h>` is a standard header file that provides I/O functions like printf.
* Anything you include this way is pulled into your program at compile time.

In FreeBSD source code, you'll often see many `#include` directives at the top of a file. Here’s an example from the FreeBSD kernel file `sys/kern/kern_shutdown.c`:

	#include <sys/cdefs.h>
	#include "opt_ddb.h"
	#include "opt_ekcd.h"
	#include "opt_kdb.h"
	#include "opt_panic.h"
	#include "opt_printf.h"
	#include "opt_sched.h"
	#include "opt_watchdog.h"
	
	#include <sys/param.h>
	#include <sys/systm.h>
	#include <sys/bio.h>
	#include <sys/boottrace.h>
	#include <sys/buf.h>
	#include <sys/conf.h>
	#include <sys/compressor.h>
	#include <sys/cons.h>
	#include <sys/disk.h>
	#include <sys/eventhandler.h>
	#include <sys/filedesc.h>
	#include <sys/jail.h>
	#include <sys/kdb.h>
	#include <sys/kernel.h>
	#include <sys/kerneldump.h>
	#include <sys/kthread.h>
	#include <sys/ktr.h>
	#include <sys/malloc.h>
	#include <sys/mbuf.h>
	#include <sys/mount.h>
	#include <sys/priv.h>
	#include <sys/proc.h>
	#include <sys/reboot.h>
	#include <sys/resourcevar.h>
	#include <sys/rwlock.h>
	#include <sys/sbuf.h>
	#include <sys/sched.h>
	#include <sys/smp.h>
	#include <sys/stdarg.h>
	#include <sys/sysctl.h>
	#include <sys/sysproto.h>
	#include <sys/taskqueue.h>
	#include <sys/vnode.h>
	#include <sys/watchdog.h>

	#include <crypto/chacha20/chacha.h>
	#include <crypto/rijndael/rijndael-api-fst.h>
	#include <crypto/sha2/sha256.h>
	
	#include <ddb/ddb.h>
	
	#include <machine/cpu.h>
	#include <machine/dump.h>
	#include <machine/pcb.h>
	#include <machine/smp.h>

	#include <security/mac/mac_framework.h>
	
	#include <vm/vm.h>
	#include <vm/vm_object.h>
	#include <vm/vm_page.h>
	#include <vm/vm_pager.h>
	#include <vm/swap_pager.h>
	
	#include <sys/signalvar.h>

These headers define macros, constants, and function prototypes used in the kernel. For now, just remember: `#include` brings in definitions you want to use.

### The `main()` Function: Where Execution Begins

	int main(void) {

* This is the **entry point of your program**. When your program runs, it starts here.
* The `int` means the function returns an integer to the operating system.
* void means it takes no arguments.

In user programs, `main()` is where you write your logic. In the kernel, however, there’s **no** `main()` function like this; the kernel has its own bootstrapping process. But FreeBSD kernel modules and subsystems still define **entry points** that act in similar ways.

For example, device drivers use functions like:

	
	static int
	mydriver_probe(device_t dev)
	

And they are registered with the kernel during initialisation; these behave like a `main()` for specific subsystems.

### Statements and Function Calls

    printf("Hello, World!\n");

This is a **statement**, a single instruction that performs some action.

* `printf()` is a function provided by `<stdio.h>` that prints formatted output.
* `"Hello, World!\n"` is a string literal, with `\n` meaning "new line".

**Important Note:** In kernel code, you don’t use the `printf()` function from the Standard C Library (libc). Instead, the FreeBSD kernel provides its own internal version of `printf()` tailored for kernel-space output, a distinction we'll explore in more detail later in the book.

### Return Values

	    return 0;
	}
	

This tells the operating system that the program completed successfully.
Returning `0`usually means "**no error**".

You’ll see a similar pattern in kernel code where functions return 0 for success and a non-zero value for failure.

### Bonus Learning Point About Return Values

Let's see a practical example from sys/kern/kern_exec.c:

	exec_map_first_page(struct image_params *imgp)
	{
        vm_object_t object;
        vm_page_t m;
        int error;

        if (imgp->firstpage != NULL)
                exec_unmap_first_page(imgp);
                
        object = imgp->vp->v_object;
        if (object == NULL)
                return (EACCES);
	#if VM_NRESERVLEVEL > 0
        if ((object->flags & OBJ_COLORED) == 0) {
                VM_OBJECT_WLOCK(object);
                vm_object_color(object, 0);
                VM_OBJECT_WUNLOCK(object);
        }
	#endif
        error = vm_page_grab_valid_unlocked(&m, object, 0,
            VM_ALLOC_COUNT(VM_INITIAL_PAGEIN) |
            VM_ALLOC_NORMAL | VM_ALLOC_NOBUSY | VM_ALLOC_WIRED);

        if (error != VM_PAGER_OK)
                return (EIO);
        imgp->firstpage = sf_buf_alloc(m, 0);
        imgp->image_header = (char *)sf_buf_kva(imgp->firstpage);

        return (0);
	}


Return Values in `exec_map_first_page()`:

* `return (EACCES);`
Returned when the executable file's vnode (`imgp->vp`) has no associated virtual memory object (`v_object`). Without this object, the kernel cannot map the file into memory. This is treated as a **permission/access error**, using the standard `EACCES` error code (`"Permission Denied"`).

* `return (EIO);`
Returned when the kernel fails to retrieve a valid memory page from the file via `vm_page_grab_valid_unlocked()`. This may happen due to an I/O failure, memory issue, or file corruption. The `EIO` code (`"Input/Output Error"`) signals a **low-level failure** in reading or allocating memory for the file.

* `return (0);`
Returned on successful completion of the function. This indicates that the kernel has successfully grabbed the first page of the executable file, mapped it into memory, and stored the address of the header in `imgp->image_header`. A return value of `0` is the standard kernel convention for indicating success.

The use of `errno`-style error codes like `EIO` and `EACCES` ensures consistent error handling throughout the kernel, making it easier for driver developers and kernel programmers to propagate errors reliably and interpret failure conditions in a familiar, standardised way.

The FreeBSD kernel makes extensive use of `errno`-style error codes to represent different failure conditions consistently. Don’t worry if they seem unfamiliar at first, as we move forward, you’ll naturally encounter many of them, and I’ll help you understand how they work and when to use them. 

For a complete list of standard error codes and their meanings, you can refer to the FreeBSD manual page:

	% man 2 intro

### Putting It All Together

Let’s revisit our Hello World program, now with full comments:

	#include <stdio.h>              // Include standard I/O library
	
	int main(void) {                // Entry point of the program
	    printf("Hello, World!\n");  // Print a message to the terminal
	    return 0;                   // Exit with success
	}
	

In this short example, you’ve already seen:

* A preprocessor directive
* A function definition
* A standard library call
* A return statement

These are the **building blocks of C** and you’ll see them repeated everywhere, including deep inside FreeBSD’s kernel source code.

Continue...