---
title: "A First Look at the C Programming Language"
description: "This chapter introduces the C programming language for complete beginners."
author: "Edson Brandi"
date: "2025-08-26"
status: "draft"
part: 1
chapter: 4
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 480
---

# A First Look at the C Programming Language

Before we can dive into writing FreeBSD device drivers, we need to learn the language they're written in. That language is C, short, powerful, and, admittedly, a little quirky. But don't worry, you don't need to be a programming expert to get started.

In this chapter, I'll walk you through the basics of the C programming language, assuming absolutely no prior experience. If you've never written a line of code in your life, you're in the right place. If you've done some programming in other languages like Python or JavaScript, that's fine too; C might feel a little more manual, but we'll tackle it together.

Our goal here isn't to become master C programmers in one chapter. Instead, I want to introduce you to the language gently, showing you its syntax, its building blocks, and how it works in the context of UNIX systems like FreeBSD. Along the way, I'll point out real-world examples taken directly from the FreeBSD source code to help ground the theory in actual practice.

By the time we're done, you'll be able to read and write basic C programs, understand the core syntax, and feel confident enough to take the next steps toward kernel development. But that part will come later, for now, let's focus on learning the essentials.

## Reader Guidance: How to Use This Chapter

This chapter is not just a quick read; it's both a **reference** and a **hands-on bootcamp** in C programming with a FreeBSD flavour. How much time you'll spend here depends on how deep you go:

- **Reading only:** Around **8 hours** to read all explanations and FreeBSD kernel examples at a beginner's pace.
- **Reading + labs:** Around **12-13 hours** if you pause to type, compile, and run each of the practical labs on your FreeBSD system.
- **Reading + labs + challenges:** Around **14-15 hours or more**, since the challenge exercises will require you to stop, think, debug, and sometimes revisit earlier material.

### How to Get the Most Out of This Chapter

- **Take it in sections.** Don't try to do it all in one sitting. Each section (variables, operators, control flow, pointers, etc.) can be studied independently and practised before moving on.
- **Type the code yourself.** Copy-pasting examples skips the muscle memory. Typing them builds fluency in C and FreeBSD's development environment.
- **Use the FreeBSD source tree.** Many examples link directly to real kernel code. Open the referenced files and read them in context to see how theory connects to production code.
- **Do the challenges last.** They're meant to consolidate everything. Attempt them once you feel comfortable with the main text and labs.

This chapter is long because C is the foundation for everything else in FreeBSD device drivers. Think of it as your **toolbox**: once you master it, all the later chapters will make much more sense.

## 4.1 Introduction

Let's start at the beginning: what is C, and why is it important to us?

### What is C?
C is a programming language created in the early 1970s by Dennis Ritchie at Bell Labs. It was designed to write operating systems, and that's still one of its biggest strengths today. In fact, most modern operating systems, including FreeBSD, Linux, and even parts of Windows and macOS, are written mainly in C.

C is fast, compact, and close to the hardware, but unlike assembly language, it's still readable and expressive. You can write efficient, powerful code with C, but it also expects you to be careful. There's no safety net: no automatic memory management, no runtime error messages, and not even built-in strings like in Python or JavaScript.

This might sound scary, but it's actually a feature. When writing drivers or working inside the kernel, you want control, and C gives you that control.

### Why Should I Learn C for FreeBSD?

FreeBSD is written almost entirely in C, and that includes the kernel, device drivers, userland tools, and system libraries. If you want to write code that interacts with the operating system, whether it's a new device driver or a custom kernel module, C is your entry point.
More specifically:

* The FreeBSD kernel APIs are written in C.
* All device drivers are implemented in C.
* Even debugging tools like dtrace and kgdb understand and expose C-level information.

So, to work with FreeBSD's internals, you'll need to understand how C code is written, structured, compiled, and used in the system.

### What If I've Never Programmed Before?

No problem! I'm writing this chapter with you in mind. We'll take it one step at a time, starting with the simplest possible program and slowly building our way up. You'll learn about:

* Variables and data types
* Functions and flow control
* Pointers, arrays, and structures
* How to read real code from the FreeBSD kernel

And don't worry if any of those terms are unfamiliar right now, they'll all make sense soon. I'll provide plenty of examples, explain every step in plain language, and help you build your confidence as we go.

### How Is This Chapter Organised?

Here's a quick preview of what's coming up:

* We'll begin by setting up your development environment on FreeBSD.
* Then, we'll walk through your first C program, the classic "Hello, World!".
* From there, we'll cover the syntax and semantics of C: variables, loops, functions, and more.
* We'll show you real examples from the FreeBSD source tree, so you can start learning how the system works under the hood.
* Finally, we'll wrap up with some good practices and a look at what's coming in the next chapter, where we begin applying C to the kernel world.

Are you ready? Let's jump into the next section and get your environment set up so you can run your first C program on FreeBSD.

## 4.2 Setting Up Your Environment

Before we can start writing C code, we need to set up a working development environment. The good news? If you're running FreeBSD, **you already have most of what you need**.

In this section, we'll:

* Verify your C compiler is installed
* Compile your first program manually
* Learn how to use Makefiles for convenience

Let's go step by step.

### Installing a C Compiler on FreeBSD

FreeBSD includes the Clang compiler as part of the base system, so you typically don't need to install anything extra to start writing C code.

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

Let's write the classic "Hello, World!" program in C. This will verify that your compiler and terminal are working correctly.

Open a text editor like `ee`, `vi`, or `nano`, and create a file called `hello.c`:

```c
	#include <stdio.h>

	int main(void) {
   	 	printf("Hello, World!\n");
   	 	return 0;
	}
```

Let's break this down:

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

Typing long compile commands can get annoying as your programs grow. That's where **Makefiles** come in handy.

A Makefile is a plain text file named Makefile that defines how to build your program. Here's a very simple one for our Hello World example:

```c
	# Makefile for hello.c

	hello: hello.c
		cc -o hello hello.c
```

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

As we move forward, we'll look at examples from the actual FreeBSD kernel source. To follow along, it's useful to have the FreeBSD source tree installed locally. 

To store a complete local copy of the FreeBSD source code, you will need approximately 3.6 GB of free disk space. You can install it using Git by running the following command:

	# git clone https://git.freebsd.org/src.git /usr/src

This will give you access to all source code, which we'll reference frequently throughout this book.

### Summary

You now have a working development setup on FreeBSD! It was simple, wasn't it?

Here's what you've accomplished:

* Verified the C compiler is installed
* Wrote and compiled your first C program
* Learned how to use Makefiles
* Cloned the FreeBSD source tree for future reference

These tools are all you need to start learning C, and later, to build your own kernel modules and drivers. In the next section, we'll look at what makes up a typical C program and how it's structured.

## 4.3 Anatomy of a C Program

Now that you've compiled your first "Hello, World!" program, let's take a closer look at what's actually going on inside that code. In this section, we'll break down the basic structure of a C program and explain what each part does, step by step.

We'll also introduce how this structure appears in the FreeBSD kernel code, so you can begin recognising familiar patterns in real-world systems programming.

### The Basic Structure

Every C program follows a similar structure:

```c
	#include <stdio.h>

	int main(void) {
    	printf("Hello, World!\n");
    	return 0;
	}
```

Let's dissect this line by line.

### `#include` Directives: Adding Libraries

```c
	#include <stdio.h>
```

This line is handled by the **C preprocessor** before the program is compiled. It tells the compiler to include the contents of a system header file.

* `<stdio.h>` is a standard header file that provides I/O functions like printf.
* Anything you include this way is pulled into your program at compile time.

In FreeBSD source code, you'll often see many `#include` directives at the top of a file. Here's an example from the FreeBSD kernel file `sys/kern/kern_shutdown.c`:

```c
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
```

These headers define macros, constants, and function prototypes used in the kernel. For now, just remember: `#include` brings in definitions you want to use.

### The `main()` Function: Where Execution Begins

```c
	int main(void) {
```

* This is the **entry point of your program**. When your program runs, it starts here.
* The `int` means the function returns an integer to the operating system.
* void means it takes no arguments.

In user programs, `main()` is where you write your logic. In the kernel, however, there's **no** `main()` function like this; the kernel has its own bootstrapping process. But FreeBSD kernel modules and subsystems still define **entry points** that act in similar ways.

For example, device drivers use functions like:

```c	
	static int
	mydriver_probe(device_t dev)
```

And they are registered with the kernel during initialisation; these behave like a `main()` for specific subsystems.

### Statements and Function Calls

```c
    printf("Hello, World!\n");
```

This is a **statement**, a single instruction that performs some action.

* `printf()` is a function provided by `<stdio.h>` that prints formatted output.
* `"Hello, World!\n"` is a string literal, with `\n` meaning "new line".

**Important Note:** In kernel code, you don't use the `printf()` function from the Standard C Library (libc). Instead, the FreeBSD kernel provides its own internal version of `printf()` tailored for kernel-space output, a distinction we'll explore in more detail later in the book.

### Return Values

```c
	    return 0;
	}
```

This tells the operating system that the program completed successfully.
Returning `0`usually means "**no error**".

You'll see a similar pattern in kernel code where functions return 0 for success and a non-zero value for failure.

### Bonus Learning Point About Return Values

Let's see a practical example from sys/kern/kern_exec.c:

```c
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
```

Return Values in `exec_map_first_page()`:

* `return (EACCES);`
Returned when the executable file's vnode (`imgp->vp`) has no associated virtual memory object (`v_object`). Without this object, the kernel cannot map the file into memory. This is treated as a **permission/access error**, using the standard `EACCES` error code (`"Permission Denied"`).

* `return (EIO);`
Returned when the kernel fails to retrieve a valid memory page from the file via `vm_page_grab_valid_unlocked()`. This may happen due to an I/O failure, memory issue, or file corruption. The `EIO` code (`"Input/Output Error"`) signals a **low-level failure** in reading or allocating memory for the file.

* `return (0);`
Returned on successful completion of the function. This indicates that the kernel has successfully grabbed the first page of the executable file, mapped it into memory, and stored the address of the header in `imgp->image_header`. A return value of `0` is the standard kernel convention for indicating success.

The use of `errno`-style error codes like `EIO` and `EACCES` ensures consistent error handling throughout the kernel, making it easier for driver developers and kernel programmers to propagate errors reliably and interpret failure conditions in a familiar, standardised way.

The FreeBSD kernel makes extensive use of `errno`-style error codes to represent different failure conditions consistently. Don't worry if they seem unfamiliar at first, as we move forward, you'll naturally encounter many of them, and I'll help you understand how they work and when to use them. 

For a complete list of standard error codes and their meanings, you can refer to the FreeBSD manual page:

	% man 2 intro

### Putting It All Together

Let's revisit our Hello World program, now with full comments:

```c
	#include <stdio.h>              // Include standard I/O library
	
	int main(void) {                // Entry point of the program
	    printf("Hello, World!\n");  // Print a message to the terminal
	    return 0;                   // Exit with success
	}
```

In this short example, you've already seen:

* A preprocessor directive
* A function definition
* A standard library call
* A return statement

These are the **building blocks of C** and you'll see them repeated everywhere, including deep inside FreeBSD's kernel source code.

### Summary

In this section, you've learned:

* The structure of a C program
* How #include and main() work
* What printf() and return do
* How similar structures appear in FreeBSD's kernel code

The more C code you read, both your own and from FreeBSD, the more these patterns will become second nature.

## 4.4 Variables and Data Types

In any programming language, variables are how you store and manipulate data. In C, variables are a little more "manual" than in higher-level languages, but they give you the control you need to write fast, efficient programs, and that's precisely what operating systems like FreeBSD require.

In this section, we'll explore:

* How to declare and initialise variables
* The most common data types in C
* How FreeBSD uses them in kernel code
* Some tips to avoid common beginner mistakes

Let's start with the basics.

### What Is a Variable?

A variable is like a labeled box in memory where you can store a value, such as a number, a character, or even a block of text.

Here's a simple example:

```c
	int counter = 0;
```

This tells the compiler:

* Allocate enough memory to store an integer
* Call that memory location counter
* Put the number 0 in it to start

### Declaring Variables

In C, you must declare the type of every variable before using it. This is different from languages like Python, where the type is determined automatically.

Here's how to declare different types of variables:

```c
	int age = 30;             // Integer (whole number)
	float temperature = 98.6; // Floating-point number
	char grade = 'A';         // Single character
```

You can also declare multiple variables at once:

```c
	int x = 10, y = 20, z = 30;
```

Or leave them uninitialized (but be careful, as uninitialized variables contain garbage values!):

```c
	int count; // May contain anything!
```

Always initialise your variables, not just because it's good C practice, but because in kernel development, uninitialized values can lead to subtle and dangerous bugs, including kernel panics, unpredictable behaviour, and security vulnerabilities. In userland, mistakes might crash your program; in the kernel, they can compromise the stability of the entire system. 

Unless you have a very specific and justified reason not to (such as performance-critical code paths where the value is immediately overwritten), make initialisation the rule, not the exception.

### Common C Data Types

Here are the core types you'll use most often:

| Type       | Description                                | Example               |
| ---------- | ------------------------------------------ | --------------------- |
| `int`      | Integer (typically 32-bit)                 | `int count = 1;`      |
| `unsigned` | Non-negative integer                       | `unsigned size = 10;` |
| `char`     | A single 8-bit character                   | `char c = 'A';`       |
| `float`    | Floating-point number (\~6 decimal digits) | `float pi = 3.14;`    |
| `double`   | Double-precision float (\~15 digits)       | `double g = 9.81;`    |
| `void`     | Represents "no value" (used for functions) | `void print()`        |

### Type Qualifiers

C provides **type qualifiers** to give more information about how a variable should behave:

* `const`: This variable can't be changed.
* `volatile`: The value can change unexpectedly (used with hardware!).
* `unsigned`: The variable cannot hold negative numbers.

Example:

```c
	const int max_users = 100;
	volatile int status_flag;
```

The `volatile` qualifier can be important in FreeBSD kernel development, but only in very specific contexts, such as accessing hardware registers or dealing with interrupt-driven updates. It tells the compiler not to optimise accesses to a variable, which is critical when values can change outside of normal program flow. 

However, `volatile` is not a substitute for proper synchronisation and should not be used for coordinating access between threads or CPUs. For that, the FreeBSD kernel provides dedicated primitives like mutexes and atomic operations, which offer both compiler and CPU-level guarantees.

### Constant Values and #define

In C programming and especially in kernel development, it's very common to define constant values using the #define directive:

```c
	#define MAX_DEVICES 64
```

This line doesn't declare a variable. Instead, it's a **preprocessor macro**, which means the C preprocessor will **replace every occurrence of** `MAX_DEVICES` **with** `64` before the actual compilation begins. This replacement happens **textually**, and the compiler never even sees the name `MAX_DEVICES`.

### Why Use #define for Constants?

Using `#define` for constant values has several advantages in kernel code:

* **Improves readability**: Instead of seeing magic numbers (like 64) scattered throughout the code, you see meaningful names like MAX_DEVICES.
* **Makes code easier to maintain**: If the maximum number of devices ever needs to change, you update it in one place, and the change is reflected wherever it's used.
* **Keeps kernel code lightweight**: Kernel code often avoids runtime overhead, and #define constants don't allocate memory or exist in the symbol table; they simply get replaced during preprocessing.

### Real Example From FreeBSD

You will find many `#define` lines in `sys/sys/param.h`, for example:

```c
	#define MAXHOSTNAMELEN 256  /* max hostname size */
```

This defines the maximum number of characters allowed in a system hostname, and it's used throughout the kernel and system utilities to enforce a consistent limit. The value 256 is now standardised and can be reused wherever the hostname length is relevant.

### Watch Out: There Is No Type Checking

Because `#define` simply performs textual substitution, it does not respect types or scoping. 

For example:

```c
	#define PI 3.14
```

This works, but it can lead to problems in certain contexts (e.g., integer promotion, unintended precision loss). For more complex or type-sensitive constants, you may prefer using `const` variables or `enums` in userland, but in the kernel, especially in headers, `#define` is often chosen for efficiency and compatibility.

### Best Practices for #define Constants in Kernel Development

* Use **ALL CAPS** for macro names to distinguish them from variables.
* Add comments to explain what the constant represents.
* Avoid defining constants that depend on runtime values.
* Prefer `#define` over `const` in header files or when targeting C89 compatibility (which is still common in kernel code).

### Best Practices for Variables

Writing correct and robust kernel code starts with disciplined variable usage. The tips below will help you avoid subtle bugs, improve code readability, and align with FreeBSD kernel development conventions.

**Always initialise your variables**: Never assume a variable starts at zero or any default value, especially in kernel code, where behaviour must be deterministic. An uninitialized variable could hold random garbage from the stack, leading to unpredictable behaviour, memory corruption, or kernel panics. Even when the variable will be overwritten soon, it's often safer and more transparent to initialise it explicitly unless performance measurements prove otherwise.

**Don't use variables before assigning a value**: This is one of the most common bugs in C, and compilers won't always catch it. In the kernel, using an uninitialized variable can result in silent failures or catastrophic system crashes. Always trace your logic to ensure every variable is assigned a valid value before use, especially if it influences memory access or hardware operations.

**Use `const` whenever the value shouldn't change**:
Using `const` is more than good style; it helps the compiler enforce read-only constraints and catch unintended modifications. This is particularly important when:

* Passing read-only pointers into functions
* Protecting configuration structures or table entries
* Marking driver data that must not change after initialisation

In kernel code, this can even lead to compiler optimisations and make the code easier to reason about for reviewers and maintainers.

**Use `unsigned` for values that can't be negative (like sizes or counters)**: Variables that represent quantities like buffer sizes, loop counters, or device counts should be declared as `unsigned` types (`unsigned int`, `size_t`, or `uint32_t`, etc.). This improves clarity and prevents logic bugs, especially when comparing with other `unsigned` types, which can cause unexpected behaviour if signed values are mixed in.

**Prefer fixed-width types in kernel code (`uint32_t`, `int64_t`, etc.)**: Kernel code must behave predictably across architectures (e.g., 32-bit vs 64-bit systems). Types like `int`, `long`, or `short` can vary in size depending on the platform, which can lead to portability issues and alignment bugs. Instead, FreeBSD uses standard types from `<sys/types.h>` such as:

* `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`
* `int32_t`, `int64_t`, etc.

These types ensure your code has a known, fixed layout and avoids surprises when compiling or running on different hardware.

**Pro Tip**: When in doubt, look at existing FreeBSD kernel code, especially drivers and subsystems close to what you're working on. The variable types and initialisation patterns used there are often based on years of hard-earned lessons from real-world systems.

### Summary

In this section, you've learned:

* How to declare and initialise variables
* The most important data types in C
* What type qualifiers like const and volatile do
* How to spot and understand variable declarations in FreeBSD's kernel code

You now have the tools to store and work with data in C, and you've already seen how FreeBSD uses the same concepts in production-quality kernel code.

## 4.5 Operators and Expressions

So far, we've learned how to declare and initialise variables. Now it's time to make them do something! In this section, we'll dive into operators and expressions, the mechanisms in C that allow you to compute values, compare them, and control program logic.

We'll cover:

* Arithmetic operators
* Comparison operators
* Logical operators
* Bitwise operators (lightly)
* Assignment operators
* Real examples from FreeBSD kernel code

### What Is an Expression?

In C, an expression is anything that produces a value. For example:

```c
	int a = 3 + 4;
```

Here, `3 + 4` is an expression that evaluates to `7`. The result is then assigned to `a`.

Operators are what you use to **build expressions**.

### Arithmetic Operators

These are used for basic math:

| Operator | Meaning        | Example | Result                     |
| -------- | -------------- | ------- | -------------------------- |
| `+`      | Addition       | `5 + 2` | `7`                        |
| `-`      | Subtraction    | `5 - 2` | `3`                        |
| `*`      | Multiplication | `5 * 2` | `10`                       |
| `/`      | Division       | `5 / 2` | `2`    (integer division!) |
| `%`      | Modulus        | `5 % 2` | `1`    (remainder)         |

**Note**: In C, division of two integers **discards the decimal part**. To get floating-point results, at least one operand must be a `float` or `double`.

### Comparison Operators

These are used to compare two values and return either `true (1)` or `false (0)`:

| Operator | Meaning               | Example  | Result           |
| -------- | --------------------- | -------- | ---------------- |
| `==`     | Equal to              | `a == b` | `1` if equal     |
| `!=`     | Not equal to          | `a != b` | `1` if not equal |
| `<`      | Less than             | `a < b`  | `1` if true      |
| `>`      | Greater than          | `a > b`  | `1` if true      |
| `<=`     | Less than or equal    | `a <= b` | `1` if true      |
| `>=`     | Greater than or equal | `a >= b` | `1` if true      |

These are heavily used in `if`, `while`, and `for` statements to control program flow.

### Logical Operators

Used to combine or invert conditions:

| Operator | Name        | Description                               | Example                  | Result                      |
| -------- | ----------- | ----------------------------------------- | ------------------------ | --------------------------- |
| &&     | Logical AND | True if **both** conditions are true     | (a > 0) && (b < 5)     | `1` if both are true        |
| \|\|     | Logical OR  | True if **either** condition is true     | (a == 0) \|\| (b > 10)   | `1` if at least one is true |
| !      | Logical NOT | Reverses the truth value of the condition | !done                  | `1` if `done` is false      |


These are especially useful in complex conditionals, like:

```c
	if ((a > 0) && (b < 100)) {
    	// both conditions must be true
	}
```

Tip: In C, any non-zero value is considered "true," and zero is considered "false".
	
### Assignment and Compound Assignment

The `=` operator assigns a value:

```c
	x = 5; // assign 5 to x
```

Compound assignment combines operation and assignment:

| Operator | Meaning             | Example   | Equivalent to |
| -------- | ------------------- | --------- | ------------- |
| `+=`     | Add and assign      | `x += 3;` | `x = x + 3;`  |
| `-=`     | Subtract and assign | `x -= 2;` | `x = x - 2;`  |
| `*=`     | Multiply and assign | `x *= 4;` | `x = x * 4;`  |
| `/=`     | Divide and assign   | `x /= 2;` | `x = x / 2;`  |
| `%=`     | Modulus and assign  | `x %= 3;` | `x = x % 3;`  |

### Bitwise Operators

In kernel development, bitwise operators are standard. Here's a light preview:

| Operator | Meaning     | Example  |
| -------- | ----------- | -------- |
| &      | Bitwise AND | a & b  |
| \|     | Bitwise OR  | a \| b  |
| ^      | Bitwise XOR | a ^ b  |
| ~      | Bitwise NOT | ~a     |
| <<     | Left shift  | a << 2 |
| >>     | Right shift | a >> 1 |

We'll cover these in detail later when we work with flags, registers, and hardware I/O.

### Real Example from FreeBSD: sys/kern/tty_info.c

Let's look at a real example from the FreeBSD source code. 

Open the file `sys/kern/tty_info.c`and look for the function `thread_compare()` starting on line 109, you will see the code below:

```c
	static int
	thread_compare(struct thread *td, struct thread *td2)
	{
        int runa, runb;
        int slpa, slpb;
        fixpt_t esta, estb;
 
        if (td == NULL)
                return (1);
 
        /*
         * Fetch running stats, pctcpu usage, and interruptable flag.
         */
        thread_lock(td);
        runa = TD_IS_RUNNING(td) || TD_ON_RUNQ(td);
        slpa = td->td_flags & TDF_SINTR;
        esta = sched_pctcpu(td);
        thread_unlock(td);
        thread_lock(td2);
        runb = TD_IS_RUNNING(td2) || TD_ON_RUNQ(td2);
        estb = sched_pctcpu(td2);
        slpb = td2->td_flags & TDF_SINTR;
        thread_unlock(td2);
        /*
         * see if at least one of them is runnable
         */
        switch (TESTAB(runa, runb)) {
        case ONLYA:
                return (0);
        case ONLYB:
                return (1);
        case BOTH:
                break;
        }
        /*
         *  favor one with highest recent cpu utilization
         */
        if (estb > esta)
                return (1);
        if (esta > estb)
                return (0);
        /*
         * favor one sleeping in a non-interruptible sleep
         */
        switch (TESTAB(slpa, slpb)) {
        case ONLYA:
                return (0);
        case ONLYB:
                return (1);
        case BOTH:
                break;
        }

        return (td < td2);
	}
```

We are interested in this fragment of code:

```c
	...
	runa = TD_IS_RUNNING(td) || TD_ON_RUNQ(td);
	...
	return (td < td2);
```

Explanation:

* `TD_IS_RUNNING(td)` and `TD_ON_RUNQ(td)` are macros that return boolean values.
* The logical OR `||` checks if either condition is true.
* The result is assigned to `runa.

Later, this line:

```c
	return (td < td2);
```

Uses the less-than operator to compare two pointers (`td` and `td2`). This is valid in C; pointer comparisons are common when choosing between resources.

Another real expression in that same file can be found at line 367:

```c
	pctcpu = (sched_pctcpu(td) * 10000 + FSCALE / 2) >> FSHIFT;
```

This expression:

* Multiplies the CPU usage estimate by 10,000
* Adds half the scale factor for rounding
* Then performs a **bitwise right shift** to scale it down
* It's an optimised way to compute `(value * scale) / divisor` using bit shifts instead of division

### Summary

In this section, you've learned:

* What expressions are in C
* How to use arithmetic, comparison, and logical operators
* How to assign values and use compound assignments
* How bitwise operations show up in kernel code
* How FreeBSD uses these expressions to control logic and calculations

This section builds the foundation for conditional execution and looping, which we'll explore next.

## 4.6 Control Flow

So far, we've learned how to declare variables and write expressions. But programs need to do more than compute values; they need to **make decisions** and **repeat actions**. This is where **control flow comes** in.

Control flow statements allow you to:

* Choose between different paths (`if`, `else`, `switch`)
* Repeat operations using loops (`for`, `while`, `do...while`)
* Exit loops early (`break`, `continue`)

These are the **decision-making tools of C**, and they're essential for writing meaningful programs, from small utilities to operating system kernels.

### Understanding the `if`, `else`, and `else if`

One of the most basic ways to control the flow of a C program is with the `if` statement. It lets your code make decisions based on whether a condition is true or false.

```c
	if (x > 0) {
	    printf("x is positive\n");
	} else if (x < 0) {
	    printf("x is negative\n");
	} else {
	    printf("x is zero\n");
	}
```

Here's how it works step by step:

1. `if (x > 0)` – The program checks the first condition. If it's true, the block inside runs and the rest of the chain is skipped.

1. `else if (x < 0)` – If the first condition was false, this second one is checked. If it's true, it's block runs and the chain ends.

1. `else` – If none of the previous conditions are true, the code inside `else` runs.

**Important syntax rules:**

* Each condition must be inside **parentheses** `( )`.
* Each block of code is surrounded by **curly braces `{ }`**, even if it's only one line (this prevents common mistakes).

You can see a real example of `if` , `if else` and `else` usage flow control in the function `ifhwioctl()` that starts at line 2407 of `sys/net/if.c` file, the fragment that we are interested in starts at line 2537:

```c
	/* Copy only (length-1) bytes so if_description is always NUL-terminated. */
	/* The length parameter counts the terminating NUL. */
	if (ifr_buffer_get_length(ifr) > ifdescr_maxlen)
	    return (ENAMETOOLONG);
	else if (ifr_buffer_get_length(ifr) == 0)
	    descrbuf = NULL;
	else {
	    descrbuf = if_allocdescr(ifr_buffer_get_length(ifr), M_WAITOK);
	    error = copyin(ifr_buffer_get_buffer(ifr), descrbuf,
	        ifr_buffer_get_length(ifr) - 1);
	    if (error) {
	        if_freedescr(descrbuf);
	        break;
	    }
	}
```

This fragment handles a request from user space to set a description for a network interface, for example, giving `em0` a human-readable label like "Main uplink port". The code checks the length of the description provided and decides what to do next.

Let's walk through the flow control step by step:

1. First `if` – Checks whether the description is too long to fit.
	* If **true**, the function immediately stops and returns an error code (`ENAMETOOLONG`).
	* If **false**, execution moves on to the next condition.
1. `else if` – Runs only if the first condition was **false**.
	* If the length is exactly zero, it means the user didn't provide a description, so the code sets `descrbuf` to `NULL`.
	* If **false**, the program moves on to the final `else`.
1. Final `else` – Executes when neither of the previous conditions are true.
	* Allocates memory for the description and copies the provided text into it.
	* If copying fails, it frees the memory and exits the loop or function.

**How the flow works:**

* Only one of these three paths runs each time.
* The first matching condition "wins", and the rest are skipped.
* This is a classic example of using `if / else if / else` to handle mutually exclusive conditions,  reject invalid input, handle the empty case, or process a valid value.

In C, `if / else if / else` chains provide a straightforward way to handle several possible outcomes in a single structure. The program checks each condition in order, and as soon as one is true, that block runs and the rest are skipped. This simple rule keeps your logic predictable and easy to follow. In the FreeBSD kernel, you'll see this pattern everywhere, from network stack functions to device drivers, because it ensures that only the correct code path runs for each situation, making the system's decision-making both efficient and reliable.

### Understanding the `switch` and `case`

A switch statement is a decision-making structure that's useful when you need to compare one variable against multiple possible values. Instead of writing a long chain of if and else if statements, you can list each possible value as a case.

Here's a simple example:

```c
	switch (cmd) {
	    case 0:
	        printf("Zero\n");
	        break;
	    case 1:
	        printf("One\n");
	        break;
	    default:
	        printf("Unknown\n");
	        break;
	}
```

* The switch checks the value of `cmd`.
* Each case is a possible value that `cmd` might have.
* The `break` statement tells the program to stop checking further cases once a match is found. Without `break`, execution will continue into the next case, a behaviour called **fall-through**.
* The `default` case runs if none of the listed cases match.

You can see a real use of switch in the FreeBSD kernel inside the function `thread_compare()` (starting at line 109 in `sys/kern/tty_info.c`). The fragment we're interested in is from lines 134 to 141:

```c
	switch (TESTAB(runa, runb)) {
	    case ONLYA:
	        return (0);
	    case ONLYB:
	        return (1);
	    case BOTH:
	        break;
	}
```

**What This Code Does**

This code decides which of two threads is "more interesting" for the scheduler based on whether each thread is runnable.

* `runa` and `runb` are flags that indicate if the first thread (`a`) and the second thread (`b`) are runnable.
* The macro `TESTAB(a, b)` combines those flags into a single value. This result can be one of three predefined constants:
	* `ONLYA` - Only thread A is runnable.
	* `ONLYB` - Only thread B is runnable.
	* `BOTH` - Both threads are runnable.

The switch works like this:

1. Case `ONLYA` – If only thread A is runnable, return `0`.
1. Case `ONLYB` – If only thread B is runnable, return `1`.
1. Case `BOTH` – If both threads are runnable, don't return immediately; instead, `break` so the rest of the function can handle this situation.

In short, `switch` statements provide a clean and efficient way to handle multiple possible outcomes from a single expression, avoiding the clutter of long `if / else if` chains. In the FreeBSD kernel, they are often used to react to different commands, flags, or states, as in our example, which decides between thread A, thread B, or both. Once you become comfortable reading switch structures, you'll start to recognise them throughout kernel code as a go-to pattern for organising decision-making logic in a clear, maintainable way.

### Understanding the `for` Loops

A `for` loop in C is perfect when you know **how many times** you want to repeat something. It sets things up in a compact, easy-to-read style:

```c
	for (int i = 0; i < 10; i++) {
	    printf("%d\n", i);
	}
```

* Start at `i = 0`
* Repeat while `i < 10`
* Increment `i` each time by 1 (`i++`)

A widespread beginner error is related to off-by-one errors (`<=` vs `<`), and forgetting the increment (which can cause an infinite loop).

You can see a real for loop inside `sys/net/iflib.c`, in the function `netmap_fl_refill()`, which begins at line 859. The fragment we care about is the inner batching loop at lines 922–949: 

```c
	for (i = 0; n > 0 && i < IFLIB_MAX_RX_REFRESH; n--, i++) {
	    struct netmap_slot *slot = &ring->slot[nm_i];
	    uint64_t paddr;
	    void *addr = PNMB(na, slot, &paddr);
	    /* ... work per buffer ... */
	    nm_i = nm_next(nm_i, lim);
	    nic_i = nm_next(nic_i, lim);
	}
```

**What this loop does**

* The driver is refilling receive buffers so the NIC can keep receiving packets.
* It processes buffers in batches: up to `IFLIB_MAX_RX_REFRESH` each time.
* `i` counts how many buffers we've handled in this batch.
* `n` is the total remaining buffers to refill; it decrements every iteration.
* For each buffer, the code grabs its slot, figures out the physical address, readies it for DMA, then advances the ring indices (`nm_i`, `nic_i`).
* The loop stops when either the batch is full (`i` hits the max) or there's nothing left to do (`n == 0`). The batch is then "published" to the NIC by the code right after the loop.

In essence, a `for` loop is the go-to choice when you have a clear limit on how many times something should run. It packages initialisation, condition checking, and iteration updates into a single, compact header, making the flow easy to follow. 

In FreeBSD's kernel code, this structure is everywhere from scanning arrays to walking network ring buffers, because it keeps repetitive work both predictable and efficient. Our example from `netmap_fl_refill()` shows precisely how this works in practice: 

the loop counts through a fixed-size batch of buffers, stopping either when the batch is full or when there's no more work left, then hands that batch off to the NIC. Once you get comfortable reading for loops like this, you'll spot them throughout the kernel and understand how they keep complex systems running smoothly.

### Understanding the `while` Loop

In C, a while loop is a control structure that allows your program to repeat a block of code as long as a certain condition remains true.

Think of it like telling your program, "Keep doing this task while this rule is true. Stop as soon as the rule becomes false."

Lets see a example:

```c
	int i = 0;
	
	while (i < 10) {
	    printf("%d\n", i);
	    i++;
	}
```

**Variable Initialization**

`int i = 0;`

* We create a variable `i` and set its value to `0`.
* This will be our `counter`, keeping track of how many times the loop has run.

**The `while` Condition**

`while (i < 10)`

* Before each repetition, C checks the condition `i < 10`.
* If the condition is **true**, the block inside the loop is executed.
* If the condition is **false**, the loop stops, and the program continues after the loop.

**The Loop Body**

```c
	{
	    printf("%d\n", i);
	    i++;
	}
```

`printf("%d\n", i);` - Prints the value of `i` followed by a newline (`\n`).
`i++;` - Increases `i` by 1 after each iteration. This step is crucial; without it, `i` would stay 0 forever, and the loop would never end, creating an infinite loop.

**Key Points to Remember**

* A while loop **may not run at all** if the condition is false from the start.
* Always ensure something inside the loop **changes the condition** over time, or you risk an infinite loop.
* In FreeBSD kernel code, `while` loops are common for:
	* Polling hardware status registers until a device is ready.
	* Waiting for a buffer to be filled.
	* Implementing retry mechanisms.

You can see a real example of `while` loop usage in the function `netmap_fl_refill()` that starts at line 858 of `sys/net/iflib.c` file.

This time, I've decided to show you the complete source code for this FreeBSD kernel function because it offers an excellent opportunity to see several concepts from this chapter working together in a real-world context. 

To make it easier to follow, I've added explanatory comments at key points so you can connect the theory to the actual implementation. Don't worry if you don't fully understand every detail right now; this is normal when first looking at kernel code. 

For our discussion, pay special attention to the while loop that begins at line 915, as it's the part we will explore in depth. Look for `while (n > 0) {` in the code below:

```c
	/*
 	* netmap_fl_refill
 	* ----------------
 	* This function refills receive (RX) buffers in a netmap-enabled
 	* FreeBSD network driver so the NIC can continue receiving packets.
 	*
 	* It is called in two main situations:
 	*   1. Initialization (driver start/reset)
 	*   2. RX synchronization (during packet reception)
 	*
 	* The core idea: figure out how many RX slots need refilling,
 	* then load and map each buffer so the NIC can use it.
 	*/
	static int
	netmap_fl_refill(iflib_rxq_t rxq, struct netmap_kring *kring, bool init)
	{
	    struct netmap_adapter *na = kring->na;
	    u_int const lim = kring->nkr_num_slots - 1;
	    struct netmap_ring *ring = kring->ring;
	    bus_dmamap_t *map;
	    struct if_rxd_update iru;
	    if_ctx_t ctx = rxq->ifr_ctx;
	    iflib_fl_t fl = &rxq->ifr_fl[0];
	    u_int nic_i_first, nic_i;   // NIC descriptor indices
	    u_int nm_i;                 // Netmap ring index
	    int i, n;                   // i = batch counter, n = buffers to process
	#if IFLIB_DEBUG_COUNTERS
	    int rf_count = 0;
	#endif
	
	    /*
	     * Figure out how many buffers (n) we need to refill.
	     * - In init mode: refill almost the whole ring (minus those in use)
	     * - In normal mode: refill from hardware current to ring head
	     */
	    if (__predict_false(init)) {
	        n = kring->nkr_num_slots - nm_kr_rxspace(kring);
	    } else {
	        n = kring->rhead - kring->nr_hwcur;
	        if (n == 0)
	            return (0); /* Nothing to do, ring already full. */
	        if (n < 0)
	            n += kring->nkr_num_slots; /* wrap-around adjustment */
	    }
	
	    // Prepare refill update structure
	    iru_init(&iru, rxq, 0 /* flid */);
	    map = fl->ifl_sds.ifsd_map;
	
	    // Starting positions
	    nic_i = fl->ifl_pidx;             // NIC producer index
	    nm_i = netmap_idx_n2k(kring, nic_i); // Convert NIC index to netmap index
	
	    // Sanity checks for init mode
	    if (__predict_false(init)) {
	        MPASS(nic_i == 0);
	        MPASS(nm_i == kring->nr_hwtail);
	    } else
	        MPASS(nm_i == kring->nr_hwcur);
	
	    DBG_COUNTER_INC(fl_refills);
	
	    /*
	     * OUTER LOOP:
	     * Keep processing until we have refilled all 'n' needed buffers.
	     */
	    while (n > 0) {
	#if IFLIB_DEBUG_COUNTERS
	        if (++rf_count == 9)
	            DBG_COUNTER_INC(fl_refills_large);
	#endif
	        nic_i_first = nic_i; // Save where this batch starts
	
	        /*
	         * INNER LOOP:
	         * Process up to IFLIB_MAX_RX_REFRESH buffers in one batch.
	         * This avoids calling hardware refill for every single buffer.
	         */
	        for (i = 0; n > 0 && i < IFLIB_MAX_RX_REFRESH; n--, i++) {
	            struct netmap_slot *slot = &ring->slot[nm_i];
	            uint64_t paddr;
	            void *addr = PNMB(na, slot, &paddr); // Get buffer address and phys addr
	
	            MPASS(i < IFLIB_MAX_RX_REFRESH);
	
	            // If the buffer address is invalid, reinitialize the ring
	            if (addr == NETMAP_BUF_BASE(na))
	                return (netmap_ring_reinit(kring));
	
	            // Save the physical address and NIC index for this buffer
	            fl->ifl_bus_addrs[i] = paddr + nm_get_offset(kring, slot);
	            fl->ifl_rxd_idxs[i] = nic_i;
	
	            // Load or reload DMA mapping if necessary
	            if (__predict_false(init)) {
	                netmap_load_map(na, fl->ifl_buf_tag,
	                    map[nic_i], addr);
	            } else if (slot->flags & NS_BUF_CHANGED) {
	                netmap_reload_map(na, fl->ifl_buf_tag,
	                    map[nic_i], addr);
	            }
	
	            // Synchronize DMA so the NIC can safely read the buffer
	            bus_dmamap_sync(fl->ifl_buf_tag, map[nic_i],
	                BUS_DMASYNC_PREREAD);
	
	            // Clear "buffer changed" flag
	            slot->flags &= ~NS_BUF_CHANGED;
	
	            // Move to next position in both netmap and NIC rings (circular increment)
	            nm_i = nm_next(nm_i, lim);
	            nic_i = nm_next(nic_i, lim);
	        }
	
	        /*
	         * Tell the hardware to make these new buffers available.
	         * This happens once per batch for efficiency.
	         */
	        iru.iru_pidx = nic_i_first;
	        iru.iru_count = i;
	        ctx->isc_rxd_refill(ctx->ifc_softc, &iru);
	    }
	
	    // Update software producer index
	    fl->ifl_pidx = nic_i;
	
	    // Ensure we refilled exactly up to the intended position
	    MPASS(nm_i == kring->rhead);
	    kring->nr_hwcur = nm_i;
	
	    // Final DMA sync for descriptors
	    bus_dmamap_sync(fl->ifl_ifdi->idi_tag, fl->ifl_ifdi->idi_map,
	        BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		
	    // Flush the buffers to the NIC
	    ctx->isc_rxd_flush(ctx->ifc_softc, rxq->ifr_id, fl->ifl_id,
	        nm_prev(nic_i, lim));
	    DBG_COUNTER_INC(rxd_flush);
	
	    return (0);
	}
```

**Understanding the `while (n > 0)` Loop in `sys/net/iflib.c`**

The loop we're about to study looks like this:

```c
	while (n > 0) {
	    ...
	}
```

It comes from **iflib** (the Interface Library) in FreeBSD's network stack, in a section of code that connects **netmap** with network drivers.

Netmap is a high-performance packet I/O framework designed for very fast packet processing. In this context, the kernel uses the loop to **refill receive buffers**, ensuring the network interface card (NIC) always has space ready to store incoming packets, keeping data flowing smoothly at high speed.

Here, `n` is simply the number of buffers that still need to be prepared. The loop works through them in **efficient batches**, processing a few at a time until all are ready. This batching approach reduces overhead and is a common technique in high-performance network drivers.

**What the `while (n > 0)` Really Does**

As we've just seen, `n` is the count of receive buffers still waiting to be prepared. This loop's job is simple in concept:

*"Work through those buffers in batches until there are none left."*

Each pass of the loop prepares a group of buffers and hands them off to the NIC. If there's still work to do, the loop runs again, ensuring that by the end, all required buffers are ready for incoming packets.

**What Happens Inside the while (n > 0) Loop**

Each time the loop runs, it processes one batch of buffers. Here's the breakdown:

1. **Debug Tracking** – If the driver is compiled with debugging enabled, it may update counters that track how often large batches of buffers are refilled. This is just for performance monitoring.
1. **Batch Setup** – The driver remembers where this batch starts (`nic_i_first`) so it can later tell the NIC exactly which slots were updated.
1. **Inner Batch Processing** – Inside the loop, there's another for loop that refills up to a maximum number of buffers at a time (IFLIB_MAX_RX_REFRESH). For each buffer in this batch:
	* Look up the buffer's address and physical location in memory.
	* Check if the buffer is valid. If not, reinitialise the receive ring.
	* Store the physical address and slot index so the NIC knows where to place incoming data.
	* If the buffer has changed or this is the first initialisation, update its DMA (Direct Memory Access) mapping.
	* Synchronise the buffer for reading so the NIC can safely use it.
	* Clear any "buffer changed" flags.
	* Move to the next buffer position in the ring.
1. **Publishing the Batch to the NIC** – Once the batch is ready, the driver calls a function to tell the NIC: 

"These new buffers are ready for use."

By breaking the work into manageable batches and looping until every buffer is ready, this while loop ensures the NIC is always prepared to receive incoming data without interruption. It's a small but crucial part of keeping packet flow continuous in a high-performance networking environment. 

Even if some of the lower-level details—like DMA mapping or ring indices aren't fully clear yet, the key takeaway is this: 

Loops like this are the engine that quietly keeps the system running at full speed. As you progress through the book, these concepts will become second nature, and you'll start to recognise similar patterns across many parts of the FreeBSD kernel.

### Understanding `do...while` Loops

A `do...while` loop is a variation of the while loop where the **loop body runs at least once**, and then repeats only **if the condition remains true**:

```c
	int i = 0;
	do {
 	   printf("%d\n", i);
	    i++;
	} while (i < 10);
```

* The loop always executes the code inside at least once, even if the condition is false to begin with.
* Afterwards, it checks the condition (`i < 10`) to decide whether to repeat.

In the FreeBSD kernel, you'll often see this pattern inside macros designed to behave like single statements. For example, in `sys/sys/timespec.h`, you'll find an example of his type of loop starting at line 44:

```c
	#define TIMESPEC_TO_TIMEVAL(tv, ts) \
	    do { \
	        (tv)->tv_sec = (ts)->tv_sec; \
	        (tv)->tv_usec = (ts)->tv_nsec / 1000; \
	    } while (0)
```

**What This Macro Does**

1. **Assign Seconds**: Copies `tv_sec` from the source (ts) to the target (tv).

2. **Convert and Assign Microseconds**: Divides `ts->tv_nsec` by 1000 to convert nanoseconds to microseconds and stores that in `tv_usec`.

3. `do...while (0)`: Wraps the two statements so that when this macro is used, it behaves syntactically like a single statement, even if followed by a semicolon, preventing issues in constructs like:

```c
	if (x) TIMESPEC_TO_TIMEVAL(tv, ts);
	else ...
```

While `do...while (0)` may look odd, it's a solid C idiom used to make macro expansions safe and predictable in all contexts (like inside conditional statements). It ensures that the entire macro behaves like one statement and avoids accidentally creating half-executed code. Understanding this helps you read and avoid subtle bugs in kernel code that rely heavily on macros for clarity and safety.

### Understanding `break` and `continue`

When working with loops in C, sometimes you need to change the normal flow:

1. `break` – Immediately exits the loop, even if the loop condition could still be true.
1. `continue` – Skips the rest of the current iteration and jumps directly to the loop's next iteration.

Here's a simple example:

```c
	for (int i = 0; i < 10; i++) {
	    if (i == 5)
	        continue; // Skip the number 5, move to the next i
	    if (i == 8)
	        break;    // Stop the loop entirely when i reaches 8
	    printf("%d\n", i);	
	}
```

**How This Works Step-by-Step**

1. The loop starts with `i = 0` and runs `while i < 10.`

1. When `i == 5`, the continue statement runs:
	* The rest of the loop body is skipped.
	* The loop moves directly to `i++` and checks the condition again.
1. When `i == 8`, the break statement runs:
	* The loop stops immediately.
	* Control jumps to the first line of code after the loop.

Output of the Code

```c
	0
	1
	2
	3
	4
	6
	7
```

`5` is skipped because of `continue`.

The loop ends at `8` because of `break`.

You can see a real example of `break` and `continue` usage in the function `if_purgeaddrs(ifp)` that starts at line 994 of `sys/net/if.c` file.

```c
	/*
 	* Remove any unicast or broadcast network addresses from an interface.
 	*/
	void
	if_purgeaddrs(struct ifnet *ifp)
	{
	        struct ifaddr *ifa;

	#ifdef INET6
	        /*
	         * Need to leave multicast addresses of proxy NDP llentries
	         * before in6_purgeifaddr() because the llentries are keys
	         * for in6_multi objects of proxy NDP entries.
	         * in6_purgeifaddr()s clean up llentries including proxy NDPs
	         * then we would lose the keys if they are called earlier.
	         */
	        in6_purge_proxy_ndp(ifp);
	#endif
	        while (1) {
	                struct epoch_tracker et;
	
	                NET_EPOCH_ENTER(et);
	                CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
	                        if (ifa->ifa_addr->sa_family != AF_LINK)
	                                break;
	                }
	                NET_EPOCH_EXIT(et);
	
	                if (ifa == NULL)
	                        break;
	#ifdef INET
	                /* XXX: Ugly!! ad hoc just for INET */
	                if (ifa->ifa_addr->sa_family == AF_INET) {
	                        struct ifreq ifr;
	
	                        bzero(&ifr, sizeof(ifr));
	                        ifr.ifr_addr = *ifa->ifa_addr;
	                        if (in_control(NULL, SIOCDIFADDR, (caddr_t)&ifr, ifp,
	                            NULL) == 0)
	                                continue;
	                }
	#endif /* INET */
	#ifdef INET6
	                if (ifa->ifa_addr->sa_family == AF_INET6) {
	                        in6_purgeifaddr((struct in6_ifaddr *)ifa);
	                        /* ifp_addrhead is already updated */
	                        continue;
	                }
	#endif /* INET6 */
	                IF_ADDR_WLOCK(ifp);
	                CK_STAILQ_REMOVE(&ifp->if_addrhead, ifa, ifaddr, ifa_link);
	                IF_ADDR_WUNLOCK(ifp);
	                ifa_free(ifa);
	        }
	}
```

**What this function does**

`if_purgeaddrs(ifp)` removes all non-link-layer addresses from a network interface. In plain words, it walks the list of addresses attached to the interface and deletes unicast or broadcast addresses that belong to IPv4 or IPv6. Some families are handled by calling helpers who update the lists for us. Anything not handled by a helper is explicitly removed and freed.

**How the loop is organised**

The outer `while (1)` repeats until there are no more removable addresses. Each pass:

1. Enters the network epoch (`NET_EPOCH_ENTER`) to safely walk the interface address list.
1. Scans the list with `CK_STAILQ_FOREACH` to find the **first address after the `AF_LINK entries**. Link-layer entries come first and are not purged here.
1. Leaves the epoch and then decides what to do with the address it found.

**Where the `break` statements act**

Break inside the list scan:

```c
	if (ifa->ifa_addr->sa_family != AF_LINK)
	break;
```

The scan stops as soon as it reaches the first non-AF_LINK address. We only need one target per pass.

Break after the scan:

```c
	if (ifa == NULL)
	    break;
```

If the scan did not find any non-AF_LINK address, there is nothing left to purge. The outer `while` ends.

**Where the `continue` statements act**

IPv4 address handled by ioctl:

```c
	if (in_control(...) == 0)
	    continue;
```

For IPv4, `in_control(SIOCDIFADDR)` removes the address and updates the list. Since that work is done, we skip the manual removal below and continue to the next outer-loop pass to look for the next address.

IPv6 address removed by helper:

```c
	in6_purgeifaddr((struct in6_ifaddr *)ifa);
	/* list already updated */
	continue;
```

For IPv6, `in6_purgeifaddr()` also updates the list. There is nothing more to do in this pass, so we continue to the next one.

**The fallback removal path**

If the address was neither handled by the IPv4 nor the IPv6 helpers, the code takes the generic path:

```c
	IF_ADDR_WLOCK(ifp);
	CK_STAILQ_REMOVE(&ifp->if_addrhead, ifa, ifaddr, ifa_link);
	IF_ADDR_WUNLOCK(ifp);
	ifa_free(ifa);
```

This explicitly removes the address from the list and frees it.

In loops, `break` and `continue` are precision tools for controlling execution flow. In the `if_purgeaddrs()` function from FreeBSD's `sys/net/if.c`, `break` stops the search when there are no more addresses to remove, or halts the inner scan as soon as a target address is found. `continue` skips the generic removal step when a specialised IPv4 or IPv6 routine has already handled the work, jumping straight to the next pass through the outer loop. This design lets the function repeatedly find one removable address at a time, remove it using the most appropriate method, and keep going until no non-link-layer addresses remain. 

The key takeaway is that well-placed break and continue statements keep loops efficient and focused, avoiding wasted work and making the code's intent clear, a pattern you'll encounter often in FreeBSD's kernel for both clarity and performance.

### Pro Tip: Always Use Braces `{}`

In C, if you omit braces after an if, only one statement is actually controlled by the if. This can easily lead to mistakes:

```c
	if (x > 0)
		printf("Positive\n");   // Runs only if x > 0
		printf("Always runs\n"); // Always runs! Not part of the if
```

This is a common source of bugs because the second printf appears to be inside the if, but it isn't.

To avoid confusion and accidental logic errors, always use braces, even for a single statement:

```c
	if (x > 0) {
	    printf("Positive\n");
	}
```

This makes your intent explicit, keeps your code safe from subtle changes, and follows the style used in the FreeBSD source tree.

**Also Safer for Future Changes**

When you always use braces, it's much safer to modify the code later:

```c
	if (x > 0) {
	    printf("x is positive\n");
	    log_positive(x);   // Adding this won't break logic!
	}
```

### Summary

In this section, you've learned:

* How to make decisions using if, else, and switch
* How to write loops using for, while, and do...while
* How to exit or skip iterations with break and continue
* How FreeBSD uses control flow to walk lists and make kernel decisions

You now have the tools to control the logic and flow of your programs, which is the core of programming itself.

## 4.7 Functions

In C, a **function** is like a dedicated workshop in a large factory; it is a self-contained area where a specific task is carried out, start to finish, without disturbing the rest of the production line. When you need that task done, you simply send the work there, and the function delivers the result.

Functions are one of the most important tools you have as a programmer because they let you:

* **Break down complexity**: Large programs become easier to understand when split into smaller, focused operations.
* **Reuse logic**: Once written, a function can be called anywhere, saving you from typing (and debugging) the same code repeatedly.
* **Improve clarity**: A descriptive function name turns a block of cryptic code into a clear statement of intent.

You've already seen functions at work:

* `main()` — the starting point of every C program.
* `printf()` — a library function that handles formatted output for you.

In the FreeBSD kernel, you'll find functions everywhere, from low-level routines that copy data between memory regions to specialised ones that communicate with hardware. For example, when a network packet arrives, the kernel doesn't put all the processing logic in one giant block of code. Instead, it calls a series of functions, each responsible for a clear, isolated step in the process.

In this section, you'll learn how to create your own functions, giving you the power to write clean, modular code. This isn't just good style in FreeBSD device driver development; it's the foundation for stability, reusability, and long-term maintainability.

**How a Function Call Works in Memory**

When your program calls a function, something important happens behind the scenes:

```
	   +---------------------+
	   | Return Address      | <- Where to resume execution after the function ends
	   +---------------------+
	   | Function Arguments  | <- Copies of the values you pass in
	   +---------------------+
	   | Local Variables     | <- Created fresh for this call
	   +---------------------+
	   | Temporary Data      | <- Space the compiler needs for calculations
	   +---------------------+
	        ... Stack ...
```

**Step-by-step:**

1. **The caller pauses** - your program stops at the function call and saves the **return address** on the stack so it knows where to continue afterwards.
1. **Arguments are placed** - the values you pass to the function (parameters) are stored, either in registers or on the stack, depending on the platform.
1. **Local variables are created** — the function gets its own workspace in memory, separate from the caller's variables.
1. **The function runs** — it executes its statements in order, possibly calling other functions along the way.
1. **A return value is sent back** — if the function produces a result, it is placed in a register (commonly eax on x86) for the caller to pick up.
1. **Cleanup and resume** — the function's workspace is removed from the stack, and the program continues where it left off.

**Why do you need to understand that?**

In kernel programming, every function call has a cost in both time and memory. Understanding this process will help you write efficient driver code and avoid subtle bugs, especially when working with low-level routines where stack space is limited.

### Defining and Declaring Functions

Every function in C follows a simple recipe. To create one, you need to specify four things:

1. **Return type** – what kind of value the function gives back to the caller.
	* Example: `int` means the function will return an integer.
	* If it doesn't return anything, we use the keyword `void`.

1. **Name** – a unique, descriptive label for your function, so you can call it later.
	* Example: `read_temperature()` is much clearer than `rt()`.
	
1. **Parameters** – zero or more values the function needs to do its job.
	* 	Each parameter has its own type and name.
	* 	If there are no parameters, use void in the list to make it explicit.

1. **Body** – the block of code, enclosed in {} braces, that performs the task.
	* This is where you write the actual instructions.

**General form:**

```c
	return_type function_name(parameter_list)
	{
	    // statements
	    return value; // if return_type is not void
	}
```

**Example:** A function to add two numbers and return the result

```c
	int add(int a, int b)
	{
	    int sum = a + b;
	    return sum;
	}
```

**Declaration vs. Definition**

A lot of beginners get tripped up here, so let's make it crystal clear:

* **Declaration** tells the compiler that a function exists, what it's called, what parameters it takes, and what it returns, but it does not provide the code for it.
* **Definition** is where you actually write the body of the function, the full implementation that does the work.

Think of it like planning and building a workshop:

* **Declaration**: putting up a sign saying *"This workshop exists, here's what it's called, and here's the kind of work it does."*
* **Definition**: actually building the workshop, stocking it with tools, and hiring workers to do the job.

**Example:**

```c
	// Function declaration (prototype)
	int add(int a, int b);
	
	// Function definition
	int add(int a, int b)
	{
	    int sum = a + b;
	    return sum;
	}
```

**Why declarations are useful**

In small single-file programs, you can just put the definition before you call the function and be done. But in larger programs, especially in FreeBSD drivers, code is often split across many files.

For example:

* The function `mydevice_probe()` might be **defined** in `mydevice.c`.
* Its **declaration** will go into a header file `mydevice.h` so that other parts of the driver, or even the kernel, can call it without knowing the details of how it works.

When the compiler sees the declaration, it knows how to check that calls to `mydevice_probe()` use the right number and types of parameters, even before it sees the definition.

**FreeBSD driver perspective**

When writing a driver:

* Declarations often live in `.h` header files.
* Definitions live in `.c` source files.
* The kernel will call your driver's functions (like `probe()`, `attach()`, `detach()`) based on the declarations it sees in your driver's headers, without caring exactly how you implement them as long as the signatures match.

Understanding this difference will save you a lot of compiler errors, especially "implicit declaration of function" or "undefined reference" errors, which are among the most common mistakes beginners hit when starting with C.

**How Declarations and Definitions Work Together**

In small programs, you might write the function's definition before `main()` and be done.
But in real projects, like a FreeBSD device driver, code is split into header files (`.h`) for declarations and source files (`.c`) for definitions.

```
          +----------------------+
          |   mydevice.h         |   <-- Header file
          |----------------------|
          | int my_probe(void);  |   // Declaration (prototype)
          | int my_attach(void); |   // Declaration
          +----------------------+
                     |
                     v
          +----------------------+
          |   mydevice.c         |   <-- Source file
          |----------------------|
          | #include "mydevice.h"|   // Include declarations
          |                      |
          | int my_probe(void)   |   // Definition
          | {                    |
          |     /* detect hw */  |
          |     return 0;        |
          | }                    |
          |                      |
          | int my_attach(void)  |   // Definition
          | {                    |
          |     /* init hw  */   |
          |     return 0;        |
          | }                    |
          +----------------------+
```

**How it works:**

1. Declaration in the header file tells the compiler: *"These functions exist somewhere, here's what they look like."*
1. Definition in the source file provides the actual code.
1. Any other `.c` file that includes `mydevice.h` can now call these functions, and the compiler will check the parameters and return types.
1. At link time, the function calls are connected to their definitions.

**In the context of FreeBSD drivers:**

* You might have `mydevice.c` containing the driver logic, and `mydevice.h` holding the function declarations shared across the driver.
* The kernel build system will compile your `.c` files and link them into a kernel module.
* If the declarations don't match the definitions exactly, you'll get compiler errors — which is why keeping them in sync is critical.

**Common mistakes with functions and how to fix them**

1) Calling a function before the compiler knows it exists
Symptom: "implicit declaration of function" warning or error.
Fix: Add a declaration in a header file and include it, or place the definition above its first use.

2) Declaration and definition do not match
Symptom: "conflicting types" or odd runtime bugs.
Fix: Make the signature identical in both places. Same return type, parameter types, and qualifiers in the same order.

3) Forgetting `void` for a function with no parameters
Symptom: The compiler may think the function takes unknown arguments.
Fix: Use int `my_fn(void)` instead of int `my_fn()`.

4) Returning a value from a `void` function or forgetting to return a value
Symptom: "void function cannot return a value" or "control reaches end of non-void function."
Fix: For non-void functions, always return the right type. For `void`, do not return a value.

5) Returning pointers to local variables
Symptom: Random crashes or garbage data.
Fix: Do not return the address of a stack variable. Use dynamically allocated memory or pass a buffer in as a parameter.

6) Mismatched `const` or pointer levels between declaration and definition
Symptom: Type mismatch errors or subtle bugs.
Fix: Keep qualifiers consistent. If the declaration has `const char *`, the definition must match exactly.

7) Multiple definitions across files
Symptom: Linker error "multiple definition of …".
Fix: Only one definition per function. If a helper should be private to a file, mark it `static` in that `.c` file.

8) Putting function definitions in headers by accident
Symptom: Multiple definition linker errors when the header is included by several `.c` files.
Fix: Headers should usually have declarations only. If you really need code in a header, make it `static inline` and keep it small.

9) Missing includes for functions you call
Symptom: Implicit declarations or wrong default types.
Fix: Include the correct system or project header that declares the function you are calling, for example `#include <stdio.h>` for `printf`.

10) Kernel specific: undefined symbols when building a module
Symptom: Linker error "undefined reference" while building your KMOD.
Fix: Ensure the function is actually defined in your module or exported by the kernel, that the declaration matches the definition, and that the right source files are part of the module build.

11) Kernel specific: using a helper that is meant to be file local
Symptom: "undefined reference" from other files or unexpected symbol visibility.
Fix: Mark internal helpers as `static` to restrict visibility. Expose only what other files must call through your header.

12) Choosing poor names
Symptom: Hard to read code and name collisions.
Fix: Use descriptive, project prefixed names, for example `mydev_read_reg`, not `readreg`.

**Hands-on exercise: Splitting Declarations and Definitions**

For this exercise, we will create 3 files.

`mydevice.h` - This header file declares the functions and makes them available to any .c file that includes it.

```c
	#ifndef MYDEVICE_H
	#define MYDEVICE_H

	// Function declarations (prototypes)
	void mydevice_probe(void);
	void mydevice_attach(void);
	void mydevice_detach(void);

	#endif // MYDEVICE_H
```

`mydevice.c` - This source file contains the actual definitions (the working code).

```c
	#include <stdio.h>
	#include "mydevice.h"

	// Function definitions
	void mydevice_probe(void)
	{
	    printf("[mydevice] Probing hardware... done.\n");
	}

	void mydevice_attach(void)
	{
	    printf("[mydevice] Attaching device and initializing resources...\n");
	}

	void mydevice_detach(void)
	{
	    printf("[mydevice] Detaching device and cleaning up.\n");
	}
```

`main.c` - This is the "user" of the functions. It just includes the header and calls them.

```c
	#include "mydevice.h"

	int main(void)
	{
	    mydevice_probe();
	    mydevice_attach();
	    mydevice_detach();
	    return 0;
	}
```

**How to Compile and Run on FreeBSD**

Open a terminal in the folder with the three files and run:

```
	cc -Wall -o myprogram main.c mydevice.c
	./myprogram
```

Expected output:

```
	[mydevice] Probing hardware... done.
	[mydevice] Attaching device and initialising resources...
	[mydevice] Detaching device and cleaning up.
```

**Why this matters for FreeBSD driver development**

In a real FreeBSD kernel module,

* `mydevice.h` would hold your driver's public API (function declarations).
* `mydevice.c` would have the full implementations of those functions.
* The kernel (or other parts of the driver) would include the header to know how to call into your code, without needing to see the actual implementation details.

This exact pattern is how `probe()`, `attach()`, and `detach()` routines are structured in actual device drivers. Learning it now will make those later chapters feel familiar.
	
Understanding the relationship between declarations and definitions is a cornerstone of C programming, and it becomes even more important when you step into the world of FreeBSD device drivers. In kernel development, functions are rarely defined and used in the same file; they are spread across multiple source and header files, compiled separately, and linked together into a single module. A clear separation between **what a function does** (its declaration) and **how it does it** (its definition) keeps code organized, reusable, and easier to maintain. Master this concept now, and you'll be well-prepared for the more complex modular structures you'll encounter when we begin building real kernel drivers.

### Calling Functions

Once you've defined a function, the next step is to call it, that is, to tell the program, *"Hey, go run this block of code now and give me the result".*

Calling a function is as simple as writing its name followed by parentheses containing any required arguments.

If the function returns a value, you can store that value in a variable, pass it to another function, or use it directly in an expression.

**Example:**

```c
int result = add(3, 4);
printf("Result is %d\n", result);
```

Here's what happens step-by-step when this code runs:

1. The program encounters `add(3, 4)` and pauses its current work.
1. It jumps to the `add()` function's definition, giving it two arguments: `3` and `4`.
1. Inside `add()`, the parameters `a` and `b` receive the values `3` and `4`.
1. The function calculates `sum = a + b` and then executes `return sum;`.
1. The returned value `7` travels back to the calling point and gets stored in the variable `result`.
1. The `printf()` function then displays:

```c
	Result is 7
```

**FreeBSD Driver Connection**

When you call a function in a FreeBSD driver, you're often asking the kernel or your own driver logic to perform a very specific task, for example:

* Calling `bus_space_read_4()` to read a 32-bit hardware register.
* Calling your own `mydevice_init()` to prepare a device for use.

The principle is exactly the same as the `add()` example: 

The function takes parameters, does its job, and returns control to where it was called. The difference in kernel space is that the "job" might involve talking directly to hardware or managing system resources, but the calling process is identical.

**Tip for Beginners**
Even if a function doesn't return a value (its return type is `void`), calling it still triggers its entire body to run. In drivers, many important functions don't return anything but perform critical work like initializing hardware or setting up interrupts.

Function Call Flow
When your program calls a function, control jumps from the current point in your code to the function's definition, runs its statements, and then comes back.
Example flow for add(3, 4) inside main():

```c
main() starts
    |
    v
Calls add(3, 4)  -----------+
    |                       |
    v                       |
Inside add():               |
    a = 3                   |
    b = 4                   |
    sum = a + b  // sum=7   |
    return sum;             |
    |                       |
    +----------- Back to main()
                                |
                                v
result = 7
printf("Result is 7")
main() ends
```

**What to notice:**

* The program's "path" temporarily leaves `main()` when the function is called.
* The parameters in the function get copies of the values passed in.
* The return statement sends a value back to where the function was called.
* After the call, execution continues right where it left off.

**FreeBSD driver analogy:**

When the kernel calls your driver's `attach()` function, the exact same process happens. The kernel jumps into your code, you run your initialization logic, and then control returns to the kernel so it can continue loading devices. Whether in user space or kernel space, function calls follow the same flow.

**Try It Yourself – Simulating a Driver Function Call**

In this exercise, you'll write a small program that mimics calling a driver function to read a "hardware register" value.

We'll simulate it in user space so you can compile and run it easily on your FreeBSD system.

**Step 1 — Define the function**

Create a file called `driver_sim.c` and start with this function:

```c
#include <stdio.h>

// Function definition
unsigned int read_register(unsigned int address)
{
    // Simulated register value based on address
    unsigned int value = address * 2; 
    printf("[driver] Reading register at address 0x%X...\n", address);
    return value;
}
```

**Step 2 — Call the function from `main()`**

In the same file, add `main()` below your function:

```c
int main(void)
{
    unsigned int reg_addr = 0x10; // pretend this is a hardware register address
    unsigned int data;

    data = read_register(reg_addr); // Call the function
    printf("[driver] Value read: 0x%X\n", data);

    return 0;
}
```

**Step 3 — Compile and run**

```
% cc -Wall -o driver_sim driver_sim.c
./driver_sim
```

**Expected output:**

```c
[driver] Reading register at address 0x10...
[driver] Value read: 0x20
```

**What You Learned**
* You called a function by name, passing it a parameter.
* The parameter got a copy of your value (`0x10`) inside the function.
* The function calculated a result and sent it back with `return`.
* Execution continued exactly where it left off.

In a real driver, `read_register()` might use the `bus_space_read_4()` kernel API to access a physical hardware register instead of multiplying a number. The function call flow, however, is exactly the same.

### Functions with No Return Value: `void`

Not every function needs to send data back to the caller.

Sometimes, you just want the function to do something, print a message, initialise hardware, log a status and then finish.

In C, when a function **does not return anything**, you declare its return type as void.

**Example:**

```c
void say_hello(void)
{
    printf("Hello, World!\n");
}
```

Here's what's happening:

* void before the name means: *"This function will not return a value".*
* The `(void)` in the parameter list means: *"This function takes no arguments".*
* Inside the braces `{}`, we place the statements we want to execute when the function is called.

**Calling it:**

```c
say_hello();
```

This will print:

```c
Hello, World!
```

**Common beginner mistakes with void functions**

1. **Forgetting `void`in the parameter list**

	```c
		void say_hello()     //  Works, but less explicit — avoid in new code
		void say_hello(void) // Best practice
	```
In old C code, `()` without void means *"this function takes an unspecified number of arguments"*, which can cause confusion.

1. **Trying to return a value from a void function**

	```c
		void test(void)
		{
    	return 42; //  Compiler error
		}
	```
	
1. Assigning the result of a void function

	```c
	int x = say_hello(); //  Compiler error
	```

Now that you've seen the most common pitfalls, let's take a step back and understand why the void keyword is important in the first place.

**Why `void` matters**

Marking a function with `void` clearly tells both the compiler and human readers that this function's purpose is to perform an action, not to produce a result.

If you try to use the "return value" from a `void` function, the compiler will stop you, which helps catch mistakes early.
	
**FreeBSD driver perspective**

In FreeBSD drivers, many important functions are void because they are all about doing work, not returning data.

For example:

* `mydevice_reset(void)` — might reset the hardware to a known state.
* `mydevice_led_on(void)` — might turn on a status LED.
* `mydevice_log_status(void)` — might print debugging information to the kernel log.

The kernel doesn't care about a return value in these cases, it just expects your function to perform its action.

While `void` functions in drivers don't return values, that doesn't mean they can't communicate important information. There are still several ways to signal events or issues back to the rest of the system.

**Tip for Beginners**

In driver code, even though `void` functions don't return data, they can still report errors or events by:

* Writing to a global or shared variable.
* Logging messages with `device_printf()` or `printf()`.
* Triggering other functions that handle error states.

Understanding void functions is important because in real-world FreeBSD driver development, not every task produces data to return; many simply perform an action that prepares the system or the hardware for something else. Whether it's initializing a device, cleaning up resources, or logging a status message, these functions still play a critical role in the overall behavior of your driver. By recognizing when a function should return a value and when it should simply do its job and return nothing, you'll write cleaner, more purposeful code that matches the way the FreeBSD kernel itself is structured.

### Function Declarations (Prototypes)

In C, it's a good habit and often essential to **declare** a function before you use it.

A function declaration, also called a **prototype**, tells the compiler:

* The function's name.
* The type of value it returns (if any).
* The number, types, and order of its parameters.

This way, the compiler can check that your function calls are correct, even if the actual definition (the body of the function) appears later in the file or in a different file entirely.

**Let's see a basic example**

```c
#include <stdio.h>

// Function declaration (prototype)
int add(int a, int b);

int main(void)
{
    int result = add(3, 4);
    printf("Result: %d\n", result);
    return 0;
}

// Function definition
int add(int a, int b)
{
    return a + b;
}
```

When the compiler reads the prototype for `add()` before `main()`, it immediately knows:

* the function's name is `add`,
* it takes two `int` parameters, and
* it will return an `int`.

Later, when the compiler finds the definition, it checks that the name, parameters, and return type match the prototype exactly. If they don't, it raises an error.

### Why prototypes matter

Placing the prototype before a function is called provides several benefits:

1. **Prevents unnecessary warnings and errors**: If you call a function before the compiler knows it exists, you'll often get an *"implicit declaration of function"* warning or even a compilation error.

1. **Catches mistakes early**: If your call passes the wrong number or types of arguments, the compiler will flag the problem immediately instead of letting it cause unpredictable behaviour at runtime.

1. **Enables modular programming**: Prototypes allow you to split your program into multiple source files. You can keep the function definitions in one file and the calls to them in another, with the prototypes stored in a shared header file.

By declaring your functions before you use them, either at the top of your .c file or in a .h header, you're not just keeping the compiler happy; you're building code that's easier to organise, maintain, and scale.

Now that you understand why prototypes are important, let's look at the two most common places to put them: directly in your `.c ` file or in a shared header file.

### Prototypes in header files

Although you can write prototypes directly at the top of your `.c` file, the more common and scalable approach is to place them in **header files** (`.h`).

This allows multiple `.c` files to share the same declarations without repeating them.

**Example:**

`mathutils.h`

```c 
#ifndef MATHUTILS_H
#define MATHUTILS_H

int add(int a, int b);
int subtract(int a, int b);

#endif // MATHUTILS_H
```

`main.c`

```c
#include <stdio.h>
#include "mathutils.h"

int main(void)
{
    printf("Sum: %d\n", add(3, 4));
    printf("Difference: %d\n", subtract(10, 4));
    return 0;
}
```

`mathutils.c`

```c
#include "mathutils.h"

int add(int a, int b)
{
    return a + b;
}

int subtract(int a, int b)
{
    return a - b;
}
```

This pattern keeps your code organized and avoids having to manually keep multiple prototypes in sync across files.

### FreeBSD driver perspective

In FreeBSD driver development, prototypes are essential because the kernel often needs to call into your driver without knowing how your functions are implemented.

For example, in your driver's header file you might declare:

```c
int mydevice_init(void);
void mydevice_start_transmission(void);
```

These tell the kernel or bus subsystem that your driver has these functions available, even if the actual definitions live deep inside your `.c` files.

The build system compiles all the pieces together and links the calls to the correct implementations.

### Try It Yourself – Moving a Function Below `main()`

One of the main reasons to use prototypes is so you can call a function that hasn't been defined yet in the file. Let's see this in action.

**Step 1 — Start without a prototype**

```c
#include <stdio.h>

int main(void)
{
    int result = add(3, 4); // This will cause a compiler warning or error
    printf("Result: %d\n", result);
    return 0;
}

// Function definition
int add(int a, int b)
{
    return a + b;
}
```

Compile it:

```c
cc -Wall -o testprog testprog.c
```

You'll likely get a warning such as:

```c
testprog.c:5:18:
warning: call to undeclared function 'add'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
    5 |     int result = add(3, 4); // This will cause a compiler warning or error
      |                  ^
1 warning generated.

```

**Step 2 — Fix it with a prototype**

Add the function prototype before `main()` like this:

```c
#include <stdio.h>

// Function declaration (prototype)
int add(int a, int b);

int main(void)
{
    int result = add(3, 4); // No warnings now
    printf("Result: %d\n", result);
    return 0;
}

// Function definition
int add(int a, int b)
{
    return a + b;
}
```

Recompile, the warning is gone, and the program runs:

```
Result: 7
```

**Note:** Depending on the compiler you use, the warning message might look a little different from the example shown above, but the meaning will be the same.

By adding a prototype, you've just seen how the compiler can recognize a function and validate its use even before it sees the actual code. This same principle is what allows the FreeBSD kernel to call into your driver; it doesn't need the whole function body up front, only the declaration. In the next section, we'll look at how this works in a real driver, where prototypes in header files act as the kernel's "map" to your driver's capabilities.

### FreeBSD Driver Connection

In the FreeBSD kernel, function prototypes are the way the system "introduces" your driver's functions to the rest of the codebase.

When the kernel wants to interact with your driver, it doesn't search for the function's code directly; it relies on the function's declaration to know the name, parameters, and return type.

For example, during device detection, the kernel might call your `probe()` function to check whether a specific piece of hardware is present. The actual definition of `probe()` could be deep inside your `mydriver.c` file, but the **prototype** lives in your driver's header file (`mydriver.h`). That header is included by the kernel or bus subsystem so it can compile code that calls `probe()` without needing to see its full implementation.

This arrangement ensures two critical things:

1. **Compiler validation**: The compiler can confirm that any calls to your functions use the correct parameters and return type.
1. **Linker resolution**: When building the kernel or your driver module, the linker knows exactly which compiled function body to connect to the calls.

Without correct prototypes, the kernel build could fail or, worse, compile but behave unpredictably at runtime. In kernel programming, that's not just a bug, it could mean a crash.

**Example — Prototypes in a FreeBSD Driver**

`mydriver.h` — Driver header file with prototypes:

```c
#ifndef _MYDRIVER_H_
#define _MYDRIVER_H_

#include <sys/types.h>
#include <sys/bus.h>

// Public entry points: declared here so bus/framework code can call them
// These are the entry points the kernel will call during the device's lifecycle
// Function prototypes (declarations)
int  mydriver_probe(device_t dev);
int  mydriver_attach(device_t dev);
int  mydriver_detach(device_t dev);

#endif /* _MYDRIVER_H_ */
```

Here, we declare three key entry points `probe()`, `attach()`, and `detach()`, but don't include their bodies.

The kernel or bus subsystem will include this header so it knows how to call these functions during device lifecycle events.

`mydriver.c` — Driver source file with definitions:

```c
/*
 * FreeBSD device driver lifecycle (quick map)
 *
 * 1) Kernel enumerates devices on a bus
 *    The bus framework walks hardware and creates device_t objects.
 *
 * 2) probe()
 *    The kernel asks your driver if it supports a given device.
 *    You inspect IDs or capabilities and return a score.
 *      - Return ENXIO if this driver does not match.
 *      - Return BUS_PROBE_DEFAULT or a better score if it matches.
 *
 * 3) attach()
 *    Called after a successful probe to bring the device online.
 *    Typical work:
 *      - Allocate resources (memory, IRQ) with bus_alloc_resource_any()
 *      - Map registers and set up bus_space
 *      - Initialize hardware to a known state
 *      - Set up interrupts and handlers
 *    Return 0 on success, or an errno if something fails.
 *
 * 4) Runtime
 *    Your driver services requests. This may include:
 *      - Interrupt handlers
 *      - I/O paths invoked by upper layers or devfs interfaces
 *      - Periodic tasks, callouts, or taskqueues
 *
 * 5) detach()
 *    Called when the device is being removed or the module unloads.
 *    Cleanup tasks:
 *      - Quiesce hardware, stop DMA, disable interrupts
 *      - Tear down handlers and timers
 *      - Unmap registers and release resources with bus_release_resource()
 *    Return 0 on success, or an errno if detach must be denied.
 *
 * 6) Optional lifecycle events
 *      - suspend() and resume() during power management
 *      - shutdown() during system shutdown
 *
 * Files to remember
 *    - mydriver.h declares the entry points that the kernel and bus code will call
 *    - mydriver.c defines those functions and contains the implementation details
 */
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/systm.h>   // device_printf
#include "mydriver.h"

/*
 * mydriver_probe()
 * Called early during device enumeration.
 * Purpose: decide if this driver matches the hardware represented by dev.
 * Return: BUS_PROBE_DEFAULT for a normal match, a better score for a strong match,
 *         or ENXIO if the device is not supported.
 */
int
mydriver_probe(device_t dev)
{
    device_printf(dev, "Probing device...\n");

    /*
     * Here you would usually check vendor and device IDs or use bus-specific
     * helper routines. If the device is not supported, return (ENXIO).
     */

    return (BUS_PROBE_DEFAULT);
}

/*
 * mydriver_attach()
 * Called after a successful probe when the kernel is ready to attach the device.
 * Purpose: allocate resources, map registers, initialise hardware, register interrupts,
 *          and make the device ready for use.
 * Return: 0 on success, or an errno value (like ENOMEM or EIO) on failure.
 */
int
mydriver_attach(device_t dev)
{
    device_printf(dev, "Attaching device and initializing resources...\n");

    /*
     * Typical steps you will add here:
     * 1) Allocate device resources (I/O memory, IRQs) with bus_alloc_resource_any().
     * 2) Map register space and set up bus_space tags and handles.
     * 3) Initialise hardware registers to a known state.
     * 4) Set up interrupt handlers if needed.
     * 5) Create device nodes or child devices if this driver exposes them.
     * On any failure, release what you allocated and return an errno.
     */

    return (0);
}

/*
 * mydriver_detach()
 * Called when the device is being detached or the module is unloading.
 * Purpose: stop the hardware, free resources, and leave the system clean.
 * Return: 0 on success, or an errno value if detach must be refused.
 */
int
mydriver_detach(device_t dev)
{
    device_printf(dev, "Detaching device and cleaning up...\n");

    /*
     * Typical steps you will add here:
     * 1) Disable interrupts and stop DMA or timers.
     * 2) Tear down interrupt handlers.
     * 3) Unmap register space and free bus resources with bus_release_resource().
     * 4) Destroy any device nodes or sysctl entries created at attach time.
     */

    return (0);
}
```

**Why this works:**

* The `.h` file exposes only the **function interfaces** to the rest of the kernel.
* The `.c` file contains the **full implementations of the functions declared in the header**.
* The build system compiles all the source files, and the linker connects calls to the correct function bodies.
* The kernel can call these functions without knowing how they work internally; it only needs the prototypes.

Understanding how the kernel uses your driver's function prototypes is more than just a formality; it's a safeguard for correctness and stability. In kernel programming, even a slight mismatch between a declaration and a definition can lead to build failures or unpredictable runtime behaviour. That's why experienced FreeBSD developers follow a few best practices to keep their prototypes clean, consistent, and easy to maintain. Let's go over some of those tips next.

### Tip for Kernel Code

When you start writing FreeBSD drivers, function prototypes aren't just a formality; they're a key part of keeping your code organised and error-free in a large, multi-file project. In the kernel, where functions are often called from deep within the system, a mismatch between a declaration and its definition can cause build failures or subtle bugs that are hard to track down.

To avoid problems and keep your headers clean:

* **Always match parameter types exactly** between the declaration and the definition; the return type, parameter list, and order must be identical.
* **Include qualifiers like `const` and `*` consistently** so you don't accidentally change how parameters are treated between the declaration and the definition.
* **Group related prototypes together** in header files so they're easy to find. For example, put all initialisation functions in one section, and hardware access functions in another.

Function prototypes may seem like a small detail in C, but they are the glue that holds multi-file projects and especially kernel code together. By declaring your functions before they are used, you give the compiler the information it needs to catch mistakes early, keep your code organised, and allow different parts of a program to communicate cleanly. 

In FreeBSD driver development, well-structured prototypes in header files enable the kernel to interact with your driver reliably, without knowing its internal details. Mastering this habit now is non-negotiable if you want to write stable, maintainable drivers. 

In the next section, we'll explore real examples from the FreeBSD source tree to see exactly how prototypes are used throughout the kernel, from core subsystems to actual device drivers. This will not only reinforce what you've learned here, but also help you recognise the patterns and conventions that experienced FreeBSD developers follow every day.

### Real Example from the FreeBSD 14.3 Source Tree: `device_printf()`

Now that you understand how function declarations and definitions work, let's walk through a concrete example from the FreeBSD kernel. We will follow `device_printf()` from its prototype in a header, to its definition in the kernel source, and finally to a real driver that calls it during initialisation. This shows the full path a function takes in real code and why prototypes are critical in driver development.

**1) Prototype — where it is declared**

The `device_printf()` function is declared in the FreeBSD kernel's bus interface header `sys/sys/bus.h`. Any driver source that includes this header can call it safely because the compiler knows its signature in advance.

```c
int	device_printf(device_t dev, const char *, ...) __printflike(2, 3);
```

What each part means:

* `int` is the return type. The function returns the number of characters printed, similar to `printf(9)`.
* `device_t dev` is a handle to the device that owns the message, which allows the kernel to prefix the output with the device name and unit, for example `vtnet0:`.
* `const char *` is the format string, the same idea used by `printf`.
* `...` indicates a variable argument list. You can pass values that match the format string.
* `__printflike(2, 3)` is a compiler hint used in FreeBSD. It tells the compiler that parameter 2 is the format string and that type checking for additional arguments starts at parameter 3. This enables compile time checks for format specifiers and argument types.

Because this declaration lives in a shared header, any driver that includes `<sys/sys/bus.h>` can call `device_printf()` without needing to know how it is implemented.

**2) Definition — where it is implemented**

Here is the actual implementation of `device_printf()` in `sys/kern/subr_bus.c` from **FreeBSD 14.3**. The function builds a prefix with the device name and unit, appends your formatted message, and counts how many characters are produced. I have added extra comments to help you understand how this function works.

```c
/**
 * @brief Print the name of the device followed by a colon, a space
 * and the result of calling vprintf() with the value of @p fmt and
 * the following arguments.
 *
 * @returns the number of characters printed
 */
int
device_printf(device_t dev, const char * fmt, ...)
{
        char buf[128];                               // Fixed buffer for sbuf to use
        struct sbuf sb;                              // sbuf structure that manages safe string building
        const char *name;                            // Will hold the device's base name (e.g., "igc")
        va_list ap;                                  // Handle for variable argument list
        size_t retval;                               // Count of characters produced by the drain

        retval = 0;                                  // Initialise the output counter

        sbuf_new(&sb, buf, sizeof(buf), SBUF_FIXEDLEN);
                                                    // Initialise sbuf 'sb' over 'buf' with fixed length

        sbuf_set_drain(&sb, sbuf_printf_drain, &retval);
                                                    // Set a "drain" callback that counts characters
                                                    // Every time sbuf emits bytes, sbuf_printf_drain
                                                    // updates 'retval' through this pointer

        name = device_get_name(dev);                // Query the device base name (may be NULL)

        if (name == NULL)                           // If we do not know the name
                sbuf_cat(&sb, "unknown: ");         // Prefix becomes "unknown: "
        else
                sbuf_printf(&sb, "%s%d: ", name, device_get_unit(dev));
                                                    // Otherwise prefix "name" + unit number, e.g., "igc0: "

        va_start(ap, fmt);                          // Start reading the variable arguments after 'fmt'
        sbuf_vprintf(&sb, fmt, ap);                 // Append the formatted message into the sbuf
        va_end(ap);                                 // Clean up the variable argument list

        sbuf_finish(&sb);                           // Finalise the sbuf so its contents are complete
        sbuf_delete(&sb);                           // Release sbuf resources associated with 'sb'

        return (retval);                            // Return the number of characters printed
}
```

**What to notice**

* The code uses sbuf to assemble the message safely. The drain callback updates retval so the function can return the number of characters produced.
*  The device prefix comes from `device_get_name()` and `device_get_unit()`. If the name is not available, it falls back to `unknown:`.
*  It accepts a format string and variable arguments, handled by `va_list`, `va_start`, and `va_end`, then forwards them to `sbuf_vprintf()`.

**3) Real driver use — where it is called in practice**

Here is a clear example from `sys/dev/virtio/virtqueue.c` that calls `device_printf()` while initialising a virtqueue to use indirect descriptors. And like I did for step 2 above, I have added extra comments to help you understand how this function works.

```c
static int
virtqueue_init_indirect(struct virtqueue *vq, int indirect_size)
{
        device_t dev;
        struct vq_desc_extra *dxp;
        int i, size;

        dev = vq->vq_dev;                               // Cache the device handle for logging and feature checks

        if (VIRTIO_BUS_WITH_FEATURE(dev, VIRTIO_RING_F_INDIRECT_DESC) == 0) {
                /*
                 * Driver asked to use indirect descriptors, but the device did
                 * not negotiate this feature. We do not fail the init here.
                 * Return 0 so the queue can still be used without this feature.
                 */
                if (bootverbose)
                        device_printf(dev, "virtqueue %d (%s) requested "
                            "indirect descriptors but not negotiated\n",
                            vq->vq_queue_index, vq->vq_name);
                return (0);                             // Continue without indirect descriptors
        }

        size = indirect_size * sizeof(struct vring_desc); // Total bytes for one indirect table
        vq->vq_max_indirect_size = indirect_size;        // Remember maximum entries per indirect table
        vq->vq_indirect_mem_size = size;                 // Remember bytes per indirect table
        vq->vq_flags |= VIRTQUEUE_FLAG_INDIRECT;         // Mark the queue as using indirect descriptors

        for (i = 0; i < vq->vq_nentries; i++) {          // For each descriptor in the main queue
                dxp = &vq->vq_descx[i];                  // Access per-descriptor extra bookkeeping

                dxp->indirect = malloc(size, M_DEVBUF, M_NOWAIT);
                                                         // Allocate an indirect descriptor table for this entry
                if (dxp->indirect == NULL) {
                        device_printf(dev, "cannot allocate indirect list\n");
                                                         // Tag the error with the device name and unit
                        return (ENOMEM);                 // Tell the caller that allocation failed
                }

                dxp->indirect_paddr = vtophys(dxp->indirect);
                                                         // Record the physical address for DMA use
                virtqueue_init_indirect_list(vq, dxp->indirect);
                                                         // Initialise the table contents to a known state
        }

        return (0);                                      // Success. The queue now supports indirect descriptors
}
```

**What this driver code is doing**

This helper prepares a virtqueue to use indirect descriptors, a VirtIO feature that allows each top level descriptor to reference a separate table of descriptors. That makes it possible to describe larger I/O requests efficiently. The function first checks whether the device actually negotiated the `VIRTIO_RING_F_INDIRECT_DESC` feature. If not, and if `bootverbose` is enabled, it uses `device_printf()` to log an informative message that includes the device prefix, then carries on without the feature. If the feature is present, it computes the size of the indirect descriptor table, marks the queue as indirect capable, and iterates over every descriptor in the ring. For each one it allocates an indirect table, logs an error with `device_printf()` if allocation fails, records the physical address for DMA, and initialises the table. This is a typical pattern in real drivers: check a feature, allocate resources, log meaningful messages tagged with the device, and handle errors cleanly.

**Why this example matters**

You have now seen the complete journey:

* **Prototype** in a shared header tells the compiler how to call the function and enables compile time checks.
* **Definition** in the kernel source implements the behaviour, using helpers like sbuf to assemble messages safely.
* **Real usage** in a driver shows how the function is called during initialisation and error paths, producing logs that are easy to trace back to a specific device.

This is the same pattern you will follow when writing your own driver helpers. Declare them in your header so the rest of the driver, and sometimes the kernel, can call them. Implement them in your `.c` files with small, focused logic. Call them from `probe()`, `attach()`, interrupt handlers, and teardown. Prototypes are the bridge that lets these pieces work together cleanly.

By now, you've seen how a function prototype, its implementation, and its real-world usage come together inside the FreeBSD kernel. From the declaration in a shared header, through the implementation in kernel code, to the call site inside a real driver, each step shows why prototypes are the "glue" that lets different parts of the system communicate cleanly. In driver development, they ensure the kernel can call into your code with complete confidence about the parameters and return type no guesswork, no surprises. Getting this right is a matter of both correctness and maintainability, and it's a habit you'll use in every driver you write.

Before we go further into writing complex driver logic, we need to understand one of the most fundamental concepts in C programming: variable scope. Scope determines where a variable can be accessed in your code, how long it stays alive in memory, and what parts of the program can modify it. In FreeBSD driver development, misunderstanding scope can lead to elusive bugs from uninitialised values corrupting hardware state to variables mysteriously changing between function calls. By mastering scope rules, you'll gain fine-grained control over your driver's data, ensuring that values are only visible where they should be, and that critical state is preserved or isolated as needed. In the next section, we'll break down scope into clear, practical categories and show you how to apply them effectively in kernel code.

### Variable Scope in Functions

In programming, **scope** defines the boundaries within which a variable can be seen and used. In other words, it tells us where in the code a variable is visible and who is allowed to read or change its value.

When a variable is declared inside a function, we say it has **local scope**. Such a variable comes into existence when the function starts running and disappears as soon as the function finishes. No other function can see it, and even within the same function, it may be invisible if declared inside a more restricted block, such as inside a loop or an `if` statement.

This form of isolation is a powerful safeguard. It prevents accidental interference from other parts of the program, ensures that one function cannot inadvertently change the internal workings of another, and makes the program's behaviour more predictable. By keeping variables confined to the places they are needed, you make your code easier to reason about, maintain, and debug.

To make this idea more concrete, let's look at a short example in C. We'll create a function with a variable that lives entirely inside it. You'll see how the variable works perfectly within its own function, but becomes completely invisible the moment we step outside that function's boundaries.

```c
#include <stdio.h>

void print_number(void) {
    int x = 42;      // x has local scope: only visible inside print_number()
    printf("%d\n", x);
}

int main(void) {
    print_number();
    // printf("%d\n", x); //  ERROR: 'x' is not in scope here
    return 0;
}
```

Here, the variable x is declared inside `print_number()`, which means it is created when the function starts and destroyed when the function ends. If we try to use `x` in `main()`, the compiler complains because `main()` has no knowledge of `x`—it lives in a separate, private workspace. This "one workspace per function" rule is one of the foundations of reliable programming: it keeps code modular, avoids accidental changes from unrelated parts of the program, and helps you reason about the behaviour of each function independently.

**Why Local Scope Is Good**

Local scope brings three key benefits to your code:

* Prevents bugs — a variable inside one function cannot accidentally overwrite or be overwritten by another function's variable, even if they share the same name.
* Keeps code predictable — you always know exactly where a variable can be read or modified, making it easier to follow and reason about the program's flow.
* Improves efficiency — the compiler can often keep local variables in CPU registers, and any stack space they use is automatically freed when the function returns.

By keeping variables confined to the smallest area where they're needed, you reduce the chances of interference, make debugging easier, and help the compiler optimise performance.

**Why scope matters in driver development**

In FreeBSD device drivers, you'll often manipulate temporary values—buffer sizes, indices, error codes, flags that are relevant only within a specific operation (e.g., probing a device, initialising a queue, handling an interrupt). Keeping these values local prevents cross-talk between concurrent paths and avoids subtle race conditions. In kernel space, small mistakes propagate fast; tight, local scope is your first line of defence.

**From Simple Scope to Real Kernel Code**

You've just seen how a local variable inside a small C program lives and dies within its function. Now, let's step into a real FreeBSD driver and see exactly the same principle at work, but this time in code that interacts with actual hardware.

We'll look at part of the VirtIO subsystem, which is used for virtual devices in environments like QEMU or bhyve. This example comes from the function `virtqueue_init_indirect()` that is located between the lines 230 and 271 in the file `sys/dev/virtio/virtqueue.c` in FreeBSD 14.3 source code, which sets up "indirect descriptors" for a virtual queue. Watch how variables are declared, used, and limited to the function's own scope, just like in our earlier `print_number()` example. 

Note: I've added some extra comments to highlight what's happening at each step.

```c
static int
virtqueue_init_indirect(struct virtqueue *vq, int indirect_size)
{
    // Local variable: holds the device reference for easy access
    // Only exists inside this function
    device_t dev;

    // Local variable: temporary pointer to a descriptor structure
    // Used during loop iterations to point to the right element
    struct vq_desc_extra *dxp;

    // Local variables: integer values used for temporary calculations
    // 'i' will be our loop counter, 'size' will hold the calculated memory size
    int i, size;

    // Initialise 'dev' with the device associated with this virtqueue
    // 'dev' is local, so it's only valid here — other functions cannot touch it
    dev = vq->vq_dev;

    // Check if the device supports the INDIRECT_DESC feature
    // This is done through a bus-level feature negotiation
    if (VIRTIO_BUS_WITH_FEATURE(dev, VIRTIO_RING_F_INDIRECT_DESC) == 0) {
        /*
         * If the driver requested indirect descriptors, but they were not
         * negotiated, we print a message (only if bootverbose is on).
         * Then we return 0 to indicate initialisation continues without them.
         */
        if (bootverbose)
            device_printf(dev, "virtqueue %d (%s) requested "
                "indirect descriptors but not negotiated\n",
                vq->vq_queue_index, vq->vq_name);
        return (0); // At this point, all locals are destroyed
    }

    // Calculate the memory size needed for the indirect descriptors
    size = indirect_size * sizeof(struct vring_desc);

    // Store these values in the virtqueue structure for later use
    vq->vq_max_indirect_size = indirect_size;
    vq->vq_indirect_mem_size = size;

    // Mark this virtqueue as using indirect descriptors
    vq->vq_flags |= VIRTQUEUE_FLAG_INDIRECT;

    // Loop through all entries in the virtqueue
    for (i = 0; i < vq->vq_nentries; i++) {
        // Point 'dxp' to the i-th descriptor entry in the queue
        dxp = &vq->vq_descx[i];

        // Allocate memory for the indirect descriptor list
        dxp->indirect = malloc(size, M_DEVBUF, M_NOWAIT);

        // If allocation fails, log an error and stop initialisation
        if (dxp->indirect == NULL) {
            device_printf(dev, "cannot allocate indirect list\n");
            return (ENOMEM); // Locals are still destroyed upon return
        }

        // Get the physical address of the allocated memory
        dxp->indirect_paddr = vtophys(dxp->indirect);

        // Initialise the allocated descriptor list
        virtqueue_init_indirect_list(vq, dxp->indirect);
    }

    // Successfully initialised indirect descriptors — locals end their life here
    return (0);
}
```

**Understanding the Scope in This Code**

Even though this is production-level kernel code, the principle is the same as in the tiny example we just saw. The variables `dev`, `dxp`, `i`, and `size` are all declared inside `virtqueue_init_indirect()` and exist only while this function is running. Once the function returns, whether it's at the end or early via a return statement, those variables vanish, freeing their stack space for other uses.

Notice that this keeps things safe: the loop counter `i` can't be accidentally reused in another part of the driver, and the `dxp` pointer is re-initialised for each call to the function. In driver development, this is a critical local scope that ensures that temporary work variables won't collide with names or data in other parts of the kernel. The isolation you learned about in the simple `print_number()` example applies here in exactly the same way, just at a higher level of complexity and with real hardware resources involved.

**Common Beginner Mistakes (and How to Avoid Them)**

One of the quickest ways to get into trouble is to store the address of a local variable in a structure that outlives the function. Once the function returns, that memory is reclaimed and can be overwritten at any time, leading to mysterious crashes. Another issue is "over-sharing", using too many global variables for convenience, which can cause unpredictable results if multiple execution paths modify them at the same time. And finally, be careful not to shadow variables (reusing a name inside an inner block), which can lead to confusion and hard-to-spot bugs.

**Wrapping Up and Moving Forward**

The lesson here is simple but powerful: local scope makes your code safer, easier to test, and more maintainable. In FreeBSD device drivers, it is the right tool for per-call, temporary data. Long-lived information should be stored in properly designed per-device structures, keeping your driver organised and avoiding accidental data sharing.

Now that you understand **where** a variable can be used, it is time to look at **how long** it exists. This is called **variable storage duration**, and it affects whether your data lives on the stack, in static storage, or on the heap. Knowing the difference is key to writing robust, efficient drivers, and that's precisely where we are headed next.

### Variable Storage Duration

So far, you've learned where a variable can be used in your program, as well as its scope. But there's another equally important property: how long the variable actually exists in memory. This is called its storage duration.

While scope is about visibility in the code, storage duration is about lifetime in memory. A variable's storage duration determines:

* **When** the variable is created.
* **When** it is destroyed.
* **Where** it lives (stack, static storage, heap).

Understanding storage duration is critical in FreeBSD driver development because we often handle resources that must persist across function calls (like device state) alongside temporary values that must vanish quickly (like loop counters or temporary buffers).

### The Three Main Storage Durations in C

When you create a variable in C, you're not just giving it a name and a value, you're also deciding **how long that value will live in memory**. This "lifetime" is what we call the **storage** duration. Even two variables that look similar in the code can behave very differently depending on how long they stick around.

Let's break down the three main types you'll encounter, starting with the most common in day-to-day programming.

**Automatic Storage Duration (stack variables)**

Think of these as short-term helpers. They are born the moment a function starts running and disappear the instant the function finishes. You don't have to create or destroy them manually; C takes care of that for you.

Automatic variables:

* Are declared inside functions without the `static` keyword.
* Are created when the function is called and destroyed when it returns.
* Live on the **stack**, a section of memory that's automatically managed by the program.
* Are perfect for quick, temporary jobs like loop counters, temporary pointers, or small scratch buffers.

Because they vanish when the function ends, you can't keep their address for later use; doing so leads to one of the most common beginner mistakes in C.

Small Example:

```c
#include <stdio.h>

void greet_user(void) {
    char name[] = "FreeBSD"; // automatic storage, stack memory
    printf("Hello, %s!\n", name);
} // 'name' is destroyed here

int main(void) {
    greet_user();
    return 0;
}
```

Here, `name` lives only while `greet_user()` runs. When the function exits, the stack space is freed automatically.

**Static Storage Duration (globals and `static` variables)**

Now imagine a variable that doesn't come and go with a function call, instead, it's **always there** from the moment your program (or in kernel space, your driver module) loads until it ends. This is **static storage**.

Static variables:

* Are declared outside functions or inside functions with the `static` keyword.
* Are created **once** when the program/module starts.
* Remain in memory until the program/module ends.
* Live in a dedicated **static memory** area.
* Are great for things like per-device state structures or lookup tables that are needed throughout the program's lifetime.

However, since they stick around, you must be careful in driver code shared, long-lived data can be accessed by multiple execution paths, so you may need locks or other synchronization to avoid conflicts.

Small Example:

```c
#include <stdio.h>

static int counter = 0; // static storage, exists for the entire program

void increment(void) {
    counter++;
    printf("Counter = %d\n", counter);
}

int main(void) {
    increment();
    increment();
    return 0;
}
```

`counter` keeps its value between calls to `increment()` because it never leaves memory until the program ends.

**Dynamic Storage Duration (heap allocation)**

Sometimes you don't know in advance how much memory you'll need, or you need to keep something around even after the function that created it has finished. That's where dynamic storage comes in: you request memory at runtime, and you decide when it goes away.

Dynamic variables:

* Are created explicitly at runtime with `malloc()`/`free()` in user space, or `malloc(9)`/`free(9)` in the FreeBSD kernel.
* Exist until you explicitly free them.
* Live in the **heap**, a pool of memory managed by the operating system or kernel.
* Are perfect for things like buffers whose size depends on hardware parameters or user input.

The flexibility comes with responsibility: forget to free them, and you'll have a memory leak. Free them too soon, and you might crash the system by accessing invalid memory.

Small Example:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char *msg = malloc(32); // dynamic storage
    if (!msg) return 1;
    strcpy(msg, "Hello from dynamic memory!");
    printf("%s\n", msg);
    free(msg); // must free to avoid leaks
    return 0;
}
```

Here, the program decides at runtime to allocate 32 bytes. The memory is under your control, so you must free it when done.

### Bridging Theory and Practice

So far, we've looked at these storage durations in an abstract way. But concepts really sink in when you see them in the wild, inside a real FreeBSD driver or subsystem function. Kernel code often mixes these durations: a few automatic locals for temporary values, some static structures for persistent state, and carefully managed dynamic memory for resources that come and go during runtime.

To make this clearer, let's walk through a real function from the FreeBSD 14.3 source tree. By following each variable and seeing how it's declared, used, and eventually discarded or freed, you'll gain an intuitive feel for how lifetime and scope interact in real-world kernel work.


| Duration  | Created                 | Destroyed           | Memory area    | Typical declarations                          | Good driver use cases                               | Common pitfalls                                 | FreeBSD APIs to know                |
| --------- | ----------------------- | ------------------- | -------------- | --------------------------------------------- | --------------------------------------------------- | ----------------------------------------------- | ----------------------------------- |
| Automatic | On function entry       | On function return  | Stack          | Local variables without `static`              | Scratch values in fast paths and interrupt handlers | Returning addresses of locals. Oversized locals | N/A                                 |
| Static    | When module loads       | When module unloads | Static storage | File scope variables or `static` inside funcs | Persistent device state. Constant tables. Tunables  | Hidden shared state. Missing locks on SMP       | `sysctl(9)` patterns for tunables   |
| Dynamic   | When you call allocator | When you free it    | Heap           | Pointers returned by allocators               | Buffers sized at probe time. Lifetime spans calls   | Leaks. Use after free. Double free              | `malloc(9)`, `free(9)`, `M_*` types |


### Real Example from FreeBSD 14.3

Before we move on, let's look at how these storage duration concepts appear in production-quality FreeBSD code. Our example comes from the network interface subsystem, specifically from the `_if_delgroup_locked()` function in `sys/net/if.c` (lines 1474 to 1512 in FreeBSD 14.3). This function removes an interface from a named interface group, updates reference counts, and frees memory when the group becomes empty.

As in our earlier, simpler examples, you'll see **automatic** variables created and destroyed entirely within the function, **dynamic** memory being released explicitly with `free(9)`, and, elsewhere in the same file, **static** variables that persist for the module's entire lifetime. By walking through this function, you'll see lifetime and scope management in action not just in an isolated snippet, but in the complex, interconnected world of the FreeBSD kernel.

Note: I've added some extra comments to highlight what's happening at each step.

```c
/*
 * Helper function to remove a group out of an interface.  Expects the global
 * ifnet lock to be write-locked, and drops it before returning.
 */
static void
_if_delgroup_locked(struct ifnet *ifp, struct ifg_list *ifgl,
    const char *groupname)
{
    struct ifg_member *ifgm;   // [Automatic] (stack) pointer: used only in this call
    bool freeifgl;             // [Automatic] (stack) flag: should we free the group?

    IFNET_WLOCK_ASSERT();      // sanity: we entered with the write lock held

    /* Remove the (interface,group) link from the interface's local list. */
    IF_ADDR_WLOCK(ifp);
    CK_STAILQ_REMOVE(&ifp->if_groups, ifgl, ifg_list, ifgl_next);
    IF_ADDR_WUNLOCK(ifp);

    /*
     * Find and remove this interface from the group's member list.
     * 'ifgm' is a LOCAL cursor; it does not escape this function
     * (classic automatic storage).
     */
    CK_STAILQ_FOREACH(ifgm, &ifgl->ifgl_group->ifg_members, ifgm_next) {
                      /* [Automatic] 'ifgm' is a local iterator only */
        if (ifgm->ifgm_ifp == ifp) {
            CK_STAILQ_REMOVE(&ifgl->ifgl_group->ifg_members, ifgm,
                ifg_member, ifgm_next);
            break;
        }
    }

    /*
     * Decrement the group's reference count.  If we just removed the
     * last member, mark the group for freeing after we drop locks.
     */
    if (--ifgl->ifgl_group->ifg_refcnt == 0) {
        CK_STAILQ_REMOVE(&V_ifg_head, ifgl->ifgl_group, ifg_group,
            ifg_next);
        freeifgl = true;
    } else {
        freeifgl = false;
    }
    IFNET_WUNLOCK();           // we promised to drop the global lock before return

    /*
     * Wait for readers in the current epoch to finish before freeing memory
     * (RCU-style safety in the networking stack).
     */
    NET_EPOCH_WAIT();

    /* Notify listeners that the group membership changed. */
    EVENTHANDLER_INVOKE(group_change_event, groupname);

    if (freeifgl) {
        /* Group became empty: fire detach event and free the group object. */
        EVENTHANDLER_INVOKE(group_detach_event, ifgl->ifgl_group);
        free(ifgl->ifgl_group, M_TEMP);  // [Dynamic] (heap) storage being returned
    }

    /* Free the (interface,group) membership nodes allocated earlier. */
    free(ifgm, M_TEMP);   // [Dynamic] the 'member' record
    free(ifgl, M_TEMP);   // [Dynamic] the (ifnet, group) link record
}
```

What to notice

* `[Automatic]` `ifgm` and `freeifgl` live only for this call. They cannot outlive the function.
* `[Dynamic]` frees return heap objects that were allocated earlier in the driver life cycle. The lifetime crosses function boundaries and must be released on the exact success path shown here.
* `[Static]` is not used in this function. In the same file you will find persistent configuration and counters that exist from load to unload. Those are `[Static]`.


**Understanding the Storage Durations in This Function**

If you follow `_if_delgroup_locked()` from start to finish, you can watch all three storage durations in C play their part. The variables `ifgm` and `freeifgl` are automatic, which means they are born when the function is called, live entirely on the stack, and disappear the moment the function returns. They are private to this call, so nothing outside can accidentally change them, and they cannot change anything outside either.

A little further down, the calls to `free(...)` deal with dynamic storage. The pointers passed to `free()` were created earlier in the driver's life, often with `malloc()` during initialisation routines like `if_addgroup()`. Unlike stack variables, this memory stays around until the driver deliberately lets it go. Freeing it here tells the kernel, *"I'm done with this; you can reuse it for something else."*

This function doesn't use static variables directly, but in the same file (`if.c`), you will find examples like debugging flags declared with `YSCTL_INT` that live for as long as the kernel module is loaded. These variables keep their values across function calls and are a reliable place to store configuration or diagnostics that need to persist.

Each choice here is intentional.

* Automatic variables keep temporary state safe inside the function.
* Dynamic memory gives flexibility at runtime, allowing the driver to adjust and then clean up when done.
* Static storage, found elsewhere in the same codebase, supports persistent, shared information.

Put together, this is a clear, real-world example of how lifetime and visibility work hand in hand in FreeBSD driver code. It is not just theory from a C textbook, it is the day-to-day reality of writing drivers that are reliable, efficient, and safe to run in the kernel.

### Why Storage Duration Matters in FreeBSD Drivers

In kernel development, storage duration is not just an academic detail; it's directly tied to system stability, performance, and even security. A wrong choice here can take down the entire operating system.

In FreeBSD drivers, the right storage duration ensures that data lives exactly as long as needed, no more and no less:

* **Automatic variables** are ideal for short-lived, private state, such as temporary values in an interrupt handler. They vanish automatically when the function ends, avoiding long-term clutter in memory.
* **Static variables** can safely store hardware state or configuration that must persist across calls, but they introduce shared state that may require locking in SMP systems to avoid race conditions.
* **Dynamic allocations** give you flexibility when buffer sizes depend on runtime conditions like device probing results, but they must be explicitly freed to avoid leaks and freeing too soon risks accessing invalid memory.

Mistakes with storage duration can be catastrophic in the kernel. Keeping a pointer to a stack variable beyond the function's life is almost guaranteed to cause corruption. Forgetting to free dynamic memory ties up resources until a reboot. Overusing static variables can turn shared state into a performance bottleneck.

Understanding these trade-offs is not optional. In driver code, often triggered by hardware events in unpredictable contexts, correct lifetime management is a foundation for writing code that is safe, efficient, and maintainable.

### Common Beginner Mistakes

When you are new to C and especially to kernel programming, it is surprisingly easy to misuse storage duration without even realising it. One classic trap with automatic variables is returning the address of a local variable from a function. At first, it might seem harmless after all, the variable was right there a moment ago, but the moment the function returns, that memory is reclaimed for other uses. Accessing it later is like reading a letter you already burned; the result is undefined behaviour, and in the kernel, that can mean an instant crash.

Static variables can cause trouble differently. Because they persist across function calls, a value left over from a previous run of the function might influence the next run in unexpected ways. This is particularly dangerous if you assume that every call starts with a "clean slate." In reality, static variables remember everything, even when you wish they wouldn't.

Dynamic memory has its own set of hazards. Forgetting to `free()` something you allocated means the memory will be tied up until the system is restarted, a problem known as a memory leak. In kernel space, where resources are precious, a leak can slowly degrade the system. Freeing the same pointer twice is even worse, it can corrupt kernel memory structures and bring down the whole machine.

Being aware of these patterns early on helps you avoid them when working on real driver code, where the cost of a mistake is often far greater than in user-space programming.

### Wrapping Up

We have explored the three main storage durations in C: automatic, static, and dynamic. Each one has its place, and the right choice depends on how long you need the data to live and who should be able to see it. The safest general rule is to choose the smallest necessary lifetime for your variables. This limits their exposure, reduces the risk of unintended interactions, and often makes the compiler's job easier.

In FreeBSD driver development, careful management of variable lifetimes is not optional; it is a fundamental skill. Done right, it helps you write code that is predictable, efficient, and resilient under load. With these principles in mind, you are ready to explore the next piece of the puzzle: understanding how variable linkage affects visibility across files and modules.

### Variable Linkage (Visibility Across Files)

So far, we've explored **scope** (where a name is visible inside your code) and **storage duration** (how long an object exists in memory). The third and final piece in this visibility puzzle is **linkage**, the rule that decides whether code in other source files can refer to a given name.

In C (and in FreeBSD kernel code), programs are often split into multiple `.c` files plus the header files they include. Each `.c` file and its headers form a translation unit. By default, most names you define are visible only inside the translation unit where they're declared. If you want other files to see them or, **often more importantly**, to hide them, linkage is the mechanism that controls that access.

### The three kinds of linkage in C

Think of linkage as *"who outside this file can see this name?"*:

* **External linkage:** A name is visible across translation units. Global variables and functions defined at file scope without static have external linkage. Other files can refer to them by declaring extern (for variables) or including a prototype (for functions).
* **Internal linkage:** A name is visible only within the current file. You get internal linkage by writing static at file scope (for variables or functions). This is how you keep helpers and the private state hidden from the rest of the kernel/program.
* **No linkage:** A name is visible only within its own block (e.g., variables inside a function). These are locals; they can't be named from outside their scope at all.

### A tiny two-file illustration

To really see linkage in action, let's build the smallest possible program that spans two `.c` files. This will let us test all three cases, external, internal, and no linkage, side by side. We'll create one file (`foo.c`) that defines a few variables and a helper function, and another file (`main.c`) that tries to use them.

Below, `shared_counter` has **external linkage** (visible in both files), `internal_flag` has **internal linkage** (visible only inside `foo.c`), and the locals inside `increment()` have **no linkage** (visible only in that function).

`foo.c`

```c
#include <stdio.h>

/* 
 * Global variable with external linkage:
 * - Visible to other files (translation units) in the program.
 * - No 'static' keyword means it has external linkage by default.
 */
int shared_counter = 0;

/* 
 * File-private variable with internal linkage:
 * - The 'static' keyword at file scope means this name
 *   is only visible inside foo.c.
 */
static int internal_flag = 1;

/*
 * Function with external linkage by default:
 * - Can be called from other files if they declare its prototype.
 */
void increment(void) {
    /* 
     * Local variable with no linkage:
     * - Exists only during this function call.
     * - Cannot be accessed from anywhere else.
     */
    int step = 1;

    if (internal_flag)         // Only code in foo.c can see internal_flag
        shared_counter += step; // Modifies the global shared_counter

    printf("Counter: %d\n", shared_counter);
}
```

`main.c`

```c
#include <stdio.h>

/*
 * 'extern' tells the compiler:
 * - This variable exists in another file (foo.c).
 * - Do not allocate storage for it here.
 */
extern int shared_counter;

/*
 * Forward declaration for the function defined in foo.c:
 * - Lets us call increment() from this file.
 */
void increment(void);

int main(void) {
    increment();            // Calls increment() from foo.c
    shared_counter += 10;   // Legal: shared_counter has external linkage
    increment();

    // internal_flag = 0;   // ERROR: not visible here (internal linkage in foo.c)
    return 0;
}
```

The pattern generalizes directly to kernel code: keep helpers and private state `static` in one `.c` file, expose only the minimal surface via headers (prototypes) or intentionally exported globals.

### Real FreeBSD 14.3 Example: External vs. Internal vs. No Linkage

Let's ground this in the FreeBSD network stack (`sys/net/if.c`). We'll look at:

1. a **global** variable with **external** linkage (`ifqmaxlen`),
1. **file-private** toggles with **internal linkage** (`log_link_state_change`, `log_promisc_mode_change`), and
1. a **function** with a **local variable** (no linkage) (`sysctl_ifcount()`), plus how it's exposed via `SYSCTL_PROC`.

**1) External linkage: a tunable global**

In `sys/net/if.c`, `ifqmaxlen` is a global integer that other parts of the kernel can reference. That's **external linkage**.

```c
int ifqmaxlen = IFQ_MAXLEN;  // external linkage: visible to other files
```

You'll also see it referenced from the SYSCTL tree setup:

```c
SYSCTL_INT(_net_link, OID_AUTO, ifqmaxlen, CTLFLAG_RDTUN,
    &ifqmaxlen, 0, "max send queue size");
```

This exposes the global through `sysctl`, so administrators can read/tune it at boot (depending on flags).

**2) Internal linkage: file-private toggles**

Right above, the file defines two static integers. Because they're **static** at file scope, they have **internal** linkage only `if.c` can name them:

```c
/* Log link state change events */
static int log_link_state_change = 1;

SYSCTL_INT(_net_link, OID_AUTO, log_link_state_change, CTLFLAG_RW,
    &log_link_state_change, 0,
    "log interface link state change events");

/* Log promiscuous mode change events */
static int log_promisc_mode_change = 1;

SYSCTL_INT(_net_link, OID_AUTO, log_promisc_mode_change, CTLFLAG_RDTUN,
    &log_promisc_mode_change, 1,
    "log promiscuous mode change events");
```

Later in the same file, `log_link_state_change` is used to decide whether to print a message, but only code inside `if.c` can refer to that symbol by name:

```c
if (log_link_state_change)
    if_printf(ifp, "link state changed to %s\n",
        (link_state == LINK_STATE_UP) ? "UP" : "DOWN");
```

See `sys/net/if.c` for the static definitions and the reference in `do_link_state_change()`.

**3) No linkage (locals) + how a private function is exported via SYSCTL**

Here's the full `sysctl_ifcount()` function (as in FreeBSD 14.3), with line-by-line commentary. Notice how `rv` is a local; it has no linkage and exists only for the duration of this call.

Note: I've added some extra comments to highlight what's happening at each step.

```c
/* sys/net/if.c */

/*
 * 'static' at file scope:
 * - Gives the function internal linkage (only visible in if.c).
 * - Other files cannot call sysctl_ifcount() directly.
 */
static int
sysctl_ifcount(SYSCTL_HANDLER_ARGS)  // SYSCTL handler signature used in the kernel
{
    /*
     * Local variable with no linkage:
     * - Exists only during this function call.
     * - Tracks the highest interface index in the current vnet.
     */
    int rv = 0;

    /*
     * IFNET_RLOCK():
     * - Acquires a read lock on the ifnet index table.
     * - Ensures safe concurrent access in an SMP kernel.
     */
    IFNET_RLOCK();

    /*
     * Loop through interface indices from 1 up to the current max (if_index).
     * If an entry is in use and belongs to the current vnet,
     * update rv with the highest index seen.
     */
    for (int i = 1; i <= if_index; i++)
        if (ifindex_table[i].ife_ifnet != NULL &&
            ifindex_table[i].ife_ifnet->if_vnet == curvnet)
            rv = i;

    /*
     * Release the read lock on the ifnet index table.
     */
    IFNET_RUNLOCK();

    /*
     * Return rv to user space via the sysctl framework.
     * - sysctl_handle_int() handles copying the value to the request buffer.
     */
    return (sysctl_handle_int(oidp, &rv, 0, req));
}
```

The function is then **registered** with the sysctl tree so other kernel parts (and user space via `sysctl`) can invoke it without needing external linkage to the function name:

```c
/*
 * SYSCTL_PROC:
 * - Creates a sysctl entry named 'ifcount' under:
 *   net.link.generic.system
 * - Flags: integer type, vnet-aware, read-only.
 * - Calls sysctl_ifcount() when queried.
 * - Even though sysctl_ifcount() is static, the sysctl framework
 *   acts as the public interface to its result.
 */
SYSCTL_PROC(_net_link_generic_system, IFMIB_IFCOUNT, ifcount,
    CTLTYPE_INT | CTLFLAG_VNET | CTLFLAG_RD, NULL, 0,
    sysctl_ifcount, "I", "Maximum known interface index");
```

This pattern is common in the kernel: the function itself has **internal linkage** (`static`), but it's exposed through a registration mechanism (sysctl, eventhandler, devfs methods, etc.). 

### Why this matters for drivers

* **Encapsulation with internal linkage:** Use static at file scope to keep helpers and private state inside a single .c file. This reduces accidental coupling and eliminates a whole class of "who changed this?" bugs under SMP.
* **Safe temporaries with no linkage:** Prefer locals for per-call data so nothing outside the function can modify it. This helps ensure correctness and makes concurrency easier to reason about.
* **Intentional exposure through interfaces:** When you need to share information, expose it through a registration mechanism such as SYSCTL_PROC, an eventhandler, or devfs methods, rather than exporting function names directly.

In `sys/net/if.c`, you can see all three visibility levels in action:

* **External linkage:** `ifqmaxlen` is a global variable accessible to other files.
* **Internal linkage:** `log_link_state_change` and `log_promisc_mode_change` are file-private toggles.
* **No linkage:** Local variable `rv` inside `sysctl_ifcount()`, exposed intentionally via `SYSCTL_PROC`.

### Common beginner pitfalls (and how to sidestep them)

A few patterns trip people up when they first juggle scope, storage duration, and linkage:

* **Using file-private helpers from another file.** If you see "undefined reference" at link time for a helper you thought was global, check for a `static` on its definition. If it's truly meant to be shared, move the prototype to a header and remove `static` from the definition. If not, keep it private and call it indirectly via a registered interface (like sysctl or an ops table).
* **Accidentally exporting private state.** A bare `int myflag;` at file scope has external linkage. If you intended it to be file-local, write `static int myflag;`. This one keyword prevents cross-file name collisions and unintended writes.
* **Leaning on globals instead of passing arguments.** If two unrelated call paths tweak the same global, you've invited heisenbugs. Prefer locals and function parameters, or encapsulate shared state in a per-device struct referenced through `softc`.
* **Beginners often confuse** `static` in file scope (**linkage control**) with `static` inside a function (**storage duration control**). In file scope, static hides a symbol from other files (linkage control). Inside a function, static makes a variable keep its value between calls (storage duration control).

### Wrapping up

You now understand **scope**, **storage duration**, and **linkage**, the three pillars that define where a variable can be used, how long it exists, and who can access it. These concepts form the foundation for managing state in any C program, and they are especially critical in FreeBSD drivers, where per-call locals, file-private helpers, and global kernel state must coexist without interfering with one another.

Next, we'll see what happens when you pass those variables into a function. In C, function parameters are copies of the original values, so changes inside the function won't affect the originals unless you pass their addresses. Understanding this behaviour is key to writing driver code that updates state intentionally, avoids subtle bugs, and communicates data effectively between functions.

## 4.8 Parameters Are Copies

When you call a function in C, the values you pass to it are **copied** into the function's parameters. The function then works with those copies, not the originals. This is known as **call by value**, and it means that any changes made to the parameter inside the function are lost when the function returns; the caller's variables remain untouched.

This is different from some other programming languages that use "pass by reference" by default, where a function can directly modify a caller's variable without special syntax. In C, if you want a function to modify something outside its own scope, you must give it the **address** of that thing. That's done using **pointers**, which we'll explore in depth in the next section.

Understanding this behaviour is critical in FreeBSD driver development. Many driver functions perform setup work, check for conditions, or calculate values without touching the caller's variables unless they are explicitly passed a pointer. This design helps maintain isolation between different parts of the kernel, reducing the risk of unintended side effects.

### A Simple Example: Modifying a Copy

To see this in action, we'll write a short program that passes an integer to a function. Inside the function, we'll try to change it. If C worked the way many beginners expect, this would update the original value. But because parameters in C are **copies**, the change will only affect the function's local version, leaving the original untouched.

```c
#include <stdio.h>

void modify(int x) {
    x = 42;  // Only updates the function's own copy
}

int main(void) {
    int original = 5;
    modify(original);
    printf("%d\n", original);  // Still prints 5, not 42!
    return 0;
}
```

Here, `modify()` changes its local version of `x`, but the original variable in `main()` stays at 5. The copy disappears as soon as `modify()` returns, leaving `main()`'s data untouched.

If you do want to change the original variable inside a function, you must pass a reference to it rather than a copy. In C, that reference takes the form of a pointer, which lets the function work directly with the original data in memory. Don't worry if pointers sound mysterious, we'll cover them thoroughly in the next section.

### A Real Example from FreeBSD 14.3

This concept shows up in production kernel code all the time. Let's see a real function from 'sys/net/if.c' in FreeBSD 14.3 that removes an interface from a group (this function is located between lines 1470 and 1512). Pay special attention to the **parameters** at the top: `ifp`, `ifgl`, and `groupname`. Each is a  **copy** of the value that the caller passed in. They're local to this function call, even though they **refer to** shared kernel objects.

In the listing below, I've added extra comments so you can see exactly what's happening at each step.

Notice how these parameters are local copies, even though they hold pointers to shared kernel data.

```c
/*
 * Helper function to remove a group out of an interface.  Expects the global
 * ifnet lock to be write-locked, and drops it before returning.
 */
static void
_if_delgroup_locked(struct ifnet *ifp, struct ifg_list *ifgl,
    const char *groupname)
{
        struct ifg_member *ifgm;   // local (automatic) variable: lives only during this call
        bool freeifgl;             // local flag on the stack: also per-call

        /*
         * PARAMETERS ARE COPIES:
         *  - 'ifp' is a copy of a pointer to the interface object.
         *  - 'ifgl' is a copy of a pointer to a (interface,group) link record.
         *  - 'groupname' is a copy of a pointer to constant text.
         * The pointer VALUES are copied, but they still refer to the same kernel data
         * as the caller's originals. Reassigning 'ifp' or 'ifgl' here wouldn't affect
         * the caller; modifying the *pointed-to* structures does persist.
         */

        IFNET_WLOCK_ASSERT();  // sanity: we entered with the global ifnet write lock held

        // Remove the (ifnet,group) link from the interface's list.
        IF_ADDR_WLOCK(ifp);
        CK_STAILQ_REMOVE(&ifp->if_groups, ifgl, ifg_list, ifgl_next);
        IF_ADDR_WUNLOCK(ifp);

        // Walk the group's member list and remove this interface from it.
        CK_STAILQ_FOREACH(ifgm, &ifgl->ifgl_group->ifg_members, ifgm_next) {
                if (ifgm->ifgm_ifp == ifp) {
                        CK_STAILQ_REMOVE(&ifgl->ifgl_group->ifg_members, ifgm,
                            ifg_member, ifgm_next);
                        break;
                }
        }

        // Decrement the group's reference count; if it hits zero, mark for free.
        if (--ifgl->ifgl_group->ifg_refcnt == 0) {
                CK_STAILQ_REMOVE(&V_ifg_head, ifgl->ifgl_group, ifg_group,
                    ifg_next);
                freeifgl = true;
        } else {
                freeifgl = false;
        }
        IFNET_WUNLOCK();  // drop the global ifnet lock before potentially freeing memory

        // Wait for current readers to exit the epoch section before freeing (RCU-style safety).
        NET_EPOCH_WAIT();

        // Notify listeners that a group membership changed (uses the 'groupname' pointer).
        EVENTHANDLER_INVOKE(group_change_event, groupname);

        if (freeifgl) {
                // If the group is now empty: announce detach and free the group object.
                EVENTHANDLER_INVOKE(group_detach_event, ifgl->ifgl_group);
                free(ifgl->ifgl_group, M_TEMP);
        }

        // Free the membership record and the (ifnet,group) link record.
        free(ifgm, M_TEMP);
        free(ifgl, M_TEMP);
}
```

In this kernel example, the parameters behave like they're passed "by reference" because they hold addresses to kernel objects. However, the pointer values themselves are still copies.

**What This Shows**

Here, `ifp`, `ifgl`, and `groupname` are copies of what the caller passed. If we reassigned `ifp = NULL;` inside this function, the caller's ifp would be unaffected. But because the pointer values still point to real kernel structures, changes to those structures, like removing from lists or freeing memory, are seen system-wide.

Meanwhile, `ifgm` and `freeifgl` are purely local automatic variables. They live only while this function runs and vanish immediately after it returns.

This mirrors our tiny user-space example exactly; the only difference is that here, the parameters are pointers into complex, shared kernel data.

### Why This Matters in FreeBSD Driver Development

In driver code, understanding that parameters are copies helps you avoid dangerous assumptions:

* If you change the parameter variable itself (like reassigning a pointer), the caller won't see that change.
* If you change the object the pointer refers to, the caller and possibly the rest of the kernel will see the change, so you must be sure it's safe.
* Passing large structures by value creates full copies on the stack; passing pointers shares the same data.

This distinction is vital for writing predictable, race-free kernel code.

### Common Beginner Mistakes

When working with parameters in C, especially in FreeBSD kernel code, beginners often get caught in subtle traps that stem from not fully grasping the "copy" rule. 

Let's look at some of the most common:

1. **Passing a structure by value instead of a pointer**: 
You expect changes to update the original, but they only update your local copy.
Example: passing a struct ifreq by value and wondering why the interface isn't reconfigured.
2. **Forgetting that a pointer grants write access**: 
Passing `struct mydev *` gives the callee full ability to change the device state. Without proper locking, this can corrupt kernel data.
3. **Confusing a pointer copy with copying data**: 
Reassigning the pointer parameter (`ptr = NULL;`) doesn't affect the caller's pointer.
Modifying the pointed-to object (`ptr->field = 42;`) does affect the caller.
4. **Copying large structures by value in kernel space**
This wastes CPU time and risks overflowing the limited kernel stack.
5. **Failing to document modification intent**: 
If your function will modify its input, make it evident in the function name, comments, and parameter type.

**Rule of Thumb**: 
Pass by value to keep data safe. Pass a pointer only when you intend to modify the data and make that intent explicit.

### Wrapping Up

You've now seen that parameters in C work by **value**: every function receives its own private copy of what you pass, even if that value is an address pointing to shared data. This model gives you both safety and responsibility: safety, because variables themselves are isolated between caller and callee; responsibility, because the data being pointed to may still be shared and mutable.

Next, we'll shift focus from individual variables to collections of data that C programmers (and FreeBSD drivers) use constantly: **arrays and strings**.

## 4.9 Arrays and Strings in C

In the previous section, you learned that function parameters are passed by value. That lesson sets the stage for working with **arrays and strings**, two of the most common structures in C. Arrays give you a way to handle collections of elements in contiguous memory. In contrast, strings are simply arrays of characters with a special terminator.

Both are central to FreeBSD driver development: arrays become buffers that move data in and out of hardware, and strings carry device names, configuration options, and environment variables.

In this section, we will build from the basics, highlight common pitfalls, and then connect the concepts to real FreeBSD kernel code, concluding with hands-on labs.

### Declaring and Using Arrays

An array in C is a fixed-size collection of elements, all of the same type, stored in contiguous memory. Once defined, its size cannot change.

```c
int numbers[5];        // Declares an array of 5 integers
```

You can initialize an array at the time of declaration:

```c
int primes[3] = {2, 3, 5};  // Initialize with values
```

Each element is accessed by its index, starting at zero:

```c
primes[0] = 7;           // Change the first element
int second = primes[1];  // Read the second element (3)
```

In memory, arrays are laid out sequentially. If `numbers` starts at address 1000 and each integer takes 4 bytes, then `numbers[0]` is at 1000, `numbers[1]` at 1004, `numbers[2]` at 1008, and so on. This detail becomes very important when we study pointers.

### Strings in C

Unlike some languages where strings are a distinct type, in C, a string is simply an array of characters terminated with a special `'\0'` character known as the **null terminator**.

```c
char name[6] = {'E', 'd', 's', 'o', 'n', '\0'};
```

A more convenient form lets the compiler insert the null terminator for you:

```c
char name[] = "Edson";  // Stored as E d s o n \0
```

Strings can be accessed and modified character by character:

```c
name[0] = 'A';  // Now the string reads "Adson"
```

If the terminating `'\0'` is missing, functions that expect a string will continue reading memory until they hit a zero byte somewhere else. This often results in garbage output, memory corruption, or kernel crashes.

### 4.9.3 Common String Functions (`<string.h>`)

The C standard library provides helper functions for strings. Although you cannot use the full standard library within the FreeBSD kernel, many equivalents are available. It is important to know the standard ones first:

```c
#include <string.h>

char src[] = "FreeBSD";
char dest[20];

strcpy(dest, src);          // Copy src into dest
int len = strlen(dest);     // Get string length
int cmp = strcmp(src, dest); // Compare two strings
```

Frequently used functions include:

- `strcpy()` - copy one string into another (unsafe, no bounds checking).
- `strncpy()` - safer variant, lets you specify maximum characters.
- `strlen()` - count characters before the null terminator.
- `strcmp()` - compare two strings lexicographically.

**Warning**: many standard functions like `strcpy()` are unsafe because they do not check buffer sizes. In kernel development, this can corrupt memory and cause the system to crash. Safer variants such as `strncpy()` or kernel-provided helpers should always be preferred.

### Why This Matters in FreeBSD Drivers

Arrays and strings are not just a basic C feature; they're at the heart of how FreeBSD drivers manage data. Nearly every driver you write or study relies on them in one form or another:

- **Buffers** that temporarily hold data moving between hardware and the kernel, such as keystrokes, network packets, or bytes written to disk.
- **Device names** like `/dev/ttyu0` or `/dev/random` are presented to user space by the kernel.
- **Configuration tunables (sysctl)** that depend on arrays and strings to store parameter names and values.
- **Lookup tables** are fixed-size arrays that hold supported hardware IDs, feature flags, or hardware-to-human-readable name mappings.

Because arrays and strings interact closely with hardware interfaces, mistakes here have consequences far beyond a user-space crash. While a runaway write in user-space might only crash that process, the same bug in kernel-space **can overwrite critical memory**, cause a kernel panic, corrupt data, or even open a security hole.

A real-world example makes this point clear. In **CVE-2024-45288**, the FreeBSD `libnv` library (used in both the kernel and userland) mishandled arrays of strings: it assumed strings were null-terminated without verifying their termination. A maliciously crafted `nvlist` could cause memory beyond the allocated buffer to be read or written, leading to kernel panic or even privilege escalation. The fix required explicit checks, safer memory allocation, and overflow protection.

Here's a simplified before/after look at the bug and its correction:

```c
/*
 * CVE-2024-45288 Analysis: Missing Null-Termination in libnv String Arrays
 * 
 * VULNERABILITY: A missing null-termination character in the last element 
 * of an nvlist array string can lead to writing outside the allocated buffer.
 */

// BEFORE (Vulnerable Code):
static char **
nvpair_unpack_string_array(bool isbe __unused, nvpair_t *nvp,
    const char *data, size_t *leftp)
{
    char **value, *tmp, **valuep;
    size_t ii, size, len;

    tmp = (char *)(uintptr_t)data;
    size = nvp->nvp_datasize;
    
    for (ii = 0; ii < nvp->nvp_nitems; ii++) {
        len = strnlen(tmp, size - 1) + 1;
        size -= len;
        // BUG: No check if tmp[len-1] is actually '\0'!
        tmp += len;
    }

    // BUG: nv_malloc does not zero-initialize
    value = nv_malloc(sizeof(*value) * nvp->nvp_nitems);
    if (value == NULL)
        return (NULL);
    // ...
}

// AFTER (Fixed Code):
static char **
nvpair_unpack_string_array(bool isbe __unused, nvpair_t *nvp,
    const char *data, size_t *leftp)
{
    char **value, *tmp, **valuep;
    size_t ii, size, len;

    tmp = (char *)(uintptr_t)data;
    size = nvp->nvp_datasize;
    
    for (ii = 0; ii < nvp->nvp_nitems; ii++) {
        len = strnlen(tmp, size - 1) + 1;
        size -= len;
        
        // FIX: Explicitly check null-termination
        if (tmp[len - 1] != '\0') {
            ERRNO_SET(EINVAL);
            return (NULL);
        }
        tmp += len;
    }

    // FIX: Use nv_calloc to zero-initialize
    value = nv_calloc(nvp->nvp_nitems, sizeof(*value));
    if (value == NULL)
        return (NULL);
    // ...
}
```

#### Visualising the Missing '\0' Problem

```
CVE-2024-45288

Legend:
  [..] = allocated bytes for one string element in the nvlist array
   \0  = null terminator
   XX  = unrelated memory beyond the allocated buffer (must not be touched)

------------------------------------------------------------------------------
BEFORE (vulnerable): last string not null-terminated
------------------------------------------------------------------------------

nvlist data region (simplified):

  +---- element[0] ----+ +---- element[1] ----+ +---- element[2] ----+
  | 'F' 'r' 'e' 'e' \0 | | 'B' 'S' 'D'   \0  | | 'b' 'u' 'g'  '!'  |XX|XX|XX|...
  +--------------------+ +--------------------+ +--------------------+--+--+--+
                                                        ^
                                                        |
                                          strnlen(tmp, size-1) walks here,
                                          never sees '\0', keeps going...
                                          size -= len is computed as if '\0'
                                          existed, later code writes past end

Effect:
  - Readers assume a proper C-string. They continue until a random zero byte in XX.
  - Writers may copy len bytes including overflow into XX.
  - Result can be buffer overflow, memory corruption, or kernel panic.

------------------------------------------------------------------------------
AFTER (fixed): explicit check for null-termination + safer allocation
------------------------------------------------------------------------------

  +---- element[0] ----+ +---- element[1] ----+ +---- element[2] ----+
  | 'F' 'r' 'e' 'e' \0 | | 'B' 'S' 'D'   \0  | | 'b' 'u' 'g'  '!' \0|XX|XX|XX|...
  +--------------------+ +--------------------+ +--------------------+--+--+--+
                                                        ^
                                                        |
                                   check: tmp[len-1] == '\0' ? OK : EINVAL

Changes:
  - The loop validates the final byte of each element is '\0'.
  - If not, it fails early with EINVAL. No overflow occurs.
  - Allocation uses nv_calloc(nitems, sizeof(*value)), memory is zeroed.

Tip for kernel developers:
  Always check termination when parsing external or untrusted data.
  Do not rely on strnlen alone. Validate tmp[len-1] == '\0' before use.
```

#### Root Cause Analysis:

1. **Missing null-termination check**
   - `strnlen()` was used to find string length.
   - The code assumed strings ended with `'\0'`.
   - No verification that `tmp[len-1] == '\0'`.
2. **Uninitialized memory**
   - `nv_malloc()` does not clear memory.
   - Changed to `nv_calloc()` to avoid leaking old memory contents.
3. **Integer overflow**
   - In related header checks, nvlh_size could overflow when added to sizeof(nvlhdrp)
   - Added explicit overflow checks.

#### Impact:

- Buffer overflow in kernel or userland.
- Privilege escalation is possible.
- System panic and memory corruption.

### Mini Lab: The Danger of a Missing `'\0'`

To illustrate the subtlety of this class of bug, try the following small program in user space.

```c
#include <stdio.h>

int main() {
    // Deliberately forget the null terminator
    char broken[5] = {'B', 'S', 'D', '!', 'X'};  

    // Print as if it were a string
    printf("Broken string: %s\n", broken);

    // Now with proper termination
    char fixed[6] = {'B', 'S', 'D', '!', 'X', '\0'};
    printf("Fixed string: %s\n", fixed);

    return 0;
}
```

**What to do:**

- Compile and run.
- The first print may show random garbage after `"BSD!X"`, because `printf("%s")` keeps reading memory until it stumbles on a zero byte.
- The second print works as expected.

**Lesson:** This is the same mistake that caused CVE-2024-45288 in FreeBSD. In user space, you get garbage or a crash. In kernel space, you risk a panic or privilege escalation. Always remember: **no `'\0'`, no string.**

**Note**: This example shows how a **tiny omission, forgetting to check for a `'\0'`, can become a serious vulnerability**. That's why professional FreeBSD driver developers are disciplined when handling arrays and strings: they always track buffer sizes, always validate string termination, and always use safe allocation and copy functions. The security and stability of the system depend on it.

### Real Example from FreeBSD 14.3 Source Code

FreeBSD stores its kernel environment as an **array of C strings**, each of the form `"name=value"`. This is a perfect real-world example of arrays and strings in action.

The array itself is declared in `sys/kern/kern_environment.c`:

```c
// sys/kern/kern_environment.c, line 83
char **kenvp;    // Array of pointers to strings like "name=value"
```

Each `kenvp[i]` points to a null-terminated string. For example:

```c
kenvp[0] → "kern.ostype=FreeBSD"
kenvp[1] → "hw.model=Intel(R) Core(TM) i7"
...
```

To look up a variable by name, FreeBSD uses the helper `_getenv_dynamic_locked()`:

```c
// sys/kern/kern_environment.c, lines 495-511
static char *
_getenv_dynamic_locked(const char *name, int *idx)
{
    char *cp;   // Pointer to the current "name=value" string
    int len, i;

    len = strlen(name);  // Get the length of the variable name

    // Walk through each string in kenvp[]
    for (cp = kenvp[0], i = 0; cp != NULL; cp = kenvp[++i]) {
        // Compare prefix: does "cp" start with "name"?
        if ((strncmp(cp, name, len) == 0) &&
            (cp[len] == '=')) {   // Ensure it's exactly "name="
            
            if (idx != NULL)
                *idx = i;   // Optionally return the index

            // Return pointer to the value part (after '=')
            return (cp + len + 1);
        }
    }

    // Not found
    return (NULL);
}
```

**Step by step explanation:**

1. The function receives a variable name, such as `"kern.ostype"`.
2. It measures its length.
3. It loops through the array `kenvp[]`. Each entry is a string like `"name=value"`.
4. It compares the prefix of each entry with the requested name.
5. If it matches and is followed by `'='`, it returns a pointer **just past the '='**, so the caller gets only the value.
   - For `"kern.ostype=FreeBSD"`, the return value points to `"FreeBSD"`.
6. If no entry matches, it returns `NULL`.

The public interface `kern_getenv()` wraps this logic with safe copying and locking:

```c
// sys/kern/kern_environment.c, lines 561-582
char *
kern_getenv(const char *name)
{
    char *cp, *ret;
    int len;

    if (dynamic_kenv) {
        // Compute maximum safe size for a "name=value" string
        len = KENV_MNAMELEN + 1 + kenv_mvallen + 1;

        // Allocate a buffer (zeroed) for the result
        ret = uma_zalloc(kenv_zone, M_WAITOK | M_ZERO);

        mtx_lock(&kenv_lock);
        cp = _getenv_dynamic(name, NULL);   // Look up variable
        if (cp != NULL)
            strlcpy(ret, cp, len);          // Safe copy into buffer
        mtx_unlock(&kenv_lock);

        // If not found, free the buffer and return NULL
        if (cp == NULL) {
            uma_zfree(kenv_zone, ret);
            ret = NULL;
        }
    } else {
        // Early boot path: static environment
        ret = _getenv_static(name);
    }

    return (ret);
}
```

**What to notice:**

- `kenvp` is an **array of strings** used as a lookup table.
- `_getenv_dynamic_locked()` walks the array, uses `strncmp()` and pointer arithmetic to isolate the value.
- `kern_getenv()` wraps this in a safe API: it locks access, copies the value with `strlcpy()`, and ensures memory ownership is clear (the caller must later `freeenv()` the result).

This real kernel code ties together almost everything we have discussed so far: **arrays of strings, null-terminated strings, standard string functions, and pointer arithmetic**.

### Common Beginner Pitfalls

Arrays and strings in C look simple, but they hide many traps for beginners. Small mistakes that in user space would only crash your program can, in kernel space, bring down the entire operating system. Here are the most common issues:

- **Off-by-one errors**
   The most classic mistake is writing outside the valid range of an array. If you declare `int items[5];`, the valid indices are `0` through `4`. Writing to `items[5]` is already one past the end, and you are corrupting memory.
   *Avoid it:* always think in terms of "zero to size minus one," and double-check loop bounds carefully.
- **Forgetting the null terminator**
   A string in C must end with `'\0'`. If you forget it, functions like `printf("%s", ...)` will keep reading memory until they randomly find a zero byte, often printing garbage or causing a crash.
   *Avoid it:* let the compiler add the terminator by writing `char name[] = "FreeBSD";` instead of manually filling character arrays.
- **Using unsafe functions**
   Functions like `strcpy()` and `strcat()` perform no bounds checking. If the destination buffer is too small, they will happily overwrite memory past its end. In kernel code, this can cause panics or even security vulnerabilities.
   *Avoid it:* use safer alternatives such as `strlcpy()` or `strlcat()`, which require you to pass the size of the buffer.
- **Assuming arrays know their own length**
   In higher-level languages, arrays often "know" how big they are. In C, an array is just a pointer to a block of memory; its size is not stored anywhere.
   *Avoid it:* keep track of the size explicitly, usually in a separate variable, and pass it along with the array whenever you share it between functions.
- **Mixing up arrays and pointers**
   Arrays and pointers are closely related in C, but not identical. For example, you cannot reassign an array the way you reassign a pointer, and `sizeof(array)` is not the same as `sizeof(pointer)`. Confusing the two leads to subtle bugs.
   *Avoid it:* remember: arrays "decay" into pointers when passed to functions, but at the declaration level they are distinct.

In user programs, these mistakes usually stop at a segmentation fault. In kernel drivers, they can overwrite scheduler data, corrupt I/O buffers, or break synchronization structures, leading to crashes or exploitable vulnerabilities. This is why FreeBSD developers are disciplined when working with arrays and strings: every buffer has a known size, every string has a checked terminator, and safe functions are preferred by default.

### Hands-On Lab 1: Arrays in Practice

In this first lab you will practice the mechanics of arrays: declaring, initializing, looping over them, and modifying elements.

```c
#include <stdio.h>

int main() {
    // Declare and initialize an array of 5 integers
    int values[5] = {10, 20, 30, 40, 50};

    printf("Initial array contents:\n");
    for (int i = 0; i < 5; i++) {
        printf("values[%d] = %d\n", i, values[i]);
    }

    // Modify one element
    values[2] = 99;

    printf("\nAfter modification:\n");
    for (int i = 0; i < 5; i++) {
        printf("values[%d] = %d\n", i, values[i]);
    }

    return 0;
}
```

**What to Try Next**

1. Change the array size to 10 but only initialize the first 3 elements. Print all 10 and notice that uninitialized ones default to zero (in this case, because the array was initialized with braces).
2. Move the `values[2] = 99;` line into the loop and try modifying every element. This is the same pattern drivers use when filling buffers with new data from hardware.
3. (Optional curiosity) Try printing `values[5]`. This is one step past the last valid element. On your system you might see garbage or nothing unusual, but in the kernel it could overwrite sensitive memory and crash the OS. Treat it as forbidden.

### Hands-On Lab 2: Strings and the Null Terminator

This lab focuses on strings. You will see what happens when you forget the terminating `'\0'`, and then you'll practice comparing strings in a way that mirrors how FreeBSD drivers search configuration options.

**Incorrect version (missing `'\0'`):**

```c
#include <stdio.h>

int main() {
    char word[5] = {'H', 'e', 'l', 'l', 'o'};
    printf("Broken string: %s\n", word);
    return 0;
}
```

**Correct version:**

```c
#include <stdio.h>

int main() {
    char word[6] = {'H', 'e', 'l', 'l', 'o', '\0'};
    printf("Fixed string: %s\n", word);
    return 0;
}
```

**What to Try Next**

1. Replace `"Hello"` with a longer word but keep the array size the same. See what happens when the word does not fit.
2. Declare `char msg[] = "FreeBSD";` without specifying a size and print it. Notice how the compiler automatically adds the null terminator for you.

**Kernel-Flavoured Bonus Challenge**

In the kernel, environment variables are stored as strings of the form `"name=value"`. Drivers often need to compare names to find the right variable. Let's simulate that:

```c
#include <stdio.h>
#include <string.h>

int main() {
    // Simulated environment variables (like entries in kenvp[])
    char *env[] = {
        "kern.ostype=FreeBSD",
        "hw.model=Intel(R) Core(TM)",
        "kern.version=14.3-RELEASE",
        NULL
    };

    // Target to search
    const char *name = "kern.ostype";
    int len = strlen(name);

    for (int i = 0; env[i] != NULL; i++) {
        // Compare prefix
        if (strncmp(env[i], name, len) == 0 && env[i][len] == '=') {
            printf("Found %s, value = %s\n", name, env[i] + len + 1);
            break;
        }
    }

    return 0;
}
```

Run it, and you will see:

```
Found kern.ostype, value = FreeBSD
```

This is almost exactly what `_getenv_dynamic_locked()` does inside the FreeBSD kernel: it compares names and, if they match, returns a pointer to the value after the `'='`.

### Wrapping Up

In this section, you explored arrays and strings from both the C language perspective and the FreeBSD kernel perspective. You saw how arrays give you fixed-size storage, how strings depend on the null terminator, and how these simple constructs underpin core driver mechanisms such as device names, sysctl parameters, and kernel environment variables.

You also discovered how subtle mistakes,  like writing past an array boundary or forgetting a terminator, can escalate into severe bugs or vulnerabilities, as illustrated by real FreeBSD CVEs.

### Recap Quiz - Arrays and Strings

**Instructions:** answer without running code first, then verify on your system. Keep answers short and specific.

1. In C, what makes a character array a "string"? Explain what happens if that element is missing.
2. Given `int a[5];`, list the valid indices and say what is undefined behavior for indexing.
3. Why is `strcpy(dest, src)` risky in kernel code, and what should you prefer instead? Briefly explain why.
4. Look at this snippet and say exactly what the return value points to if it matches:

```c
int len = strlen(name);
if (strncmp(cp, name, len) == 0 && cp[len] == '=')
    return (cp + len + 1);
```

1. In `sys/kern/kern_environment.c`, what is the type and role of `kenvp`, and how does `_getenv_dynamic_locked()` use it at a high level?

### Challenge Exercises

If you feel confident, try these challenges. They are designed to push your skills a bit further and prepare you for real driver work.

1. **Array Rotation:** Write a program that rotates the contents of an integer array by one position. For example, `{1, 2, 3, 4}` becomes `{2, 3, 4, 1}`.
2. **String Trimmer:** Write a function that removes the newline character (`'\n'`) from the end of a string if present. Test it with input from `fgets()`.
3. **Environment Lookup Simulation:** Extend the kernel-flavoured lab from this section. Add a function `char *lookup(char *env[], const char *name)` that takes an array of `"name=value"` strings and returns the value part. Handle the case where the name is not found by returning `NULL`.
4. **Buffer Size Check:** Write a function that safely copies one string into another buffer and explicitly reports an error if the destination is too small. Compare your implementation with `strlcpy()`.

### Looking Ahead

In the next section, we will connect arrays and strings to the deeper concept of **pointers and memory**. You will learn how arrays decay into pointers, how memory addresses are manipulated, and how the FreeBSD kernel allocates and frees memory safely. This is where you begin to see how data structures and memory management form the backbone of every device driver.

## 4.10 Pointers and Memory

Welcome to one of the most mysterious and magical topics in your C journey: **pointers**.

By now, you've probably heard things like:

- "Pointers are hard."
- "C is powerful because of pointers."
- "You can shoot yourself in the foot with pointers."

Those statements aren't wrong, but don't worry. I'm going to walk you through it carefully, step by step. Our goal is to **demystify pointers**, not memorize obscure syntax. And because we're learning with FreeBSD in mind, I'll also point out where and how pointers are used in the real kernel source (without overwhelming you).

When you understand pointers, you'll unlock the true potential of C, especially when it comes to writing system-level code and interacting with the operating system at a low level.

### What Is a Pointer?

So far, we've worked with variables like `int`, `char`, and `float`. These are familiar and friendly; you declare them, assign them values, and print them. Easy, right?

Now we're going to talk about something that doesn't store a value directly, but rather **stores the location of a value**.

This magical concept is called a **pointer**, and it's one of the most powerful tools in C, especially when you're writing low-level code like device drivers in FreeBSD.

#### Analogy: Memory as a Row of Lockers

Imagine computer memory as a long row of lockers, each with its own number:

```c
[1000] = 42  
[1004] = 99  
[1008] = ???  
```

Each **locker** is a memory address, and the **value** inside is your data.

When you create a variable in C:

```c
int score = 42;
```

You're saying:

> *"Please give me a locker big enough to store an `int`, and put `42` in it."*

But what if you want to *know where* that variable is stored? 

That's where **pointers** come in.

#### A First Pointer Program

Here's a gentle example to show what a pointer is, and what it can do:

```c
#include <stdio.h>

int main(void) {
    int score = 42;             // A normal integer variable
    int *ptr;                   // Declare a pointer to an integer

    ptr = &score;               // Set ptr to the address of score using the & operator

    printf("score = %d\n", score);                 // Prints: 42
    printf("ptr points to address %p\n", (void *)ptr);   // Prints the memory address of score
    printf("value at that address = %d\n", *ptr);  // Dereference the pointer to get the value: 42

    return 0;
}
```

**Line-by-Line Breakdown**

| Line              | Explanation                                                  |
| ----------------- | ------------------------------------------------------------ |
| `int score = 42;` | Declares a regular `int` and sets it to 42.                  |
| `int *ptr;`       | Declares a pointer named `ptr` that can store the address of an `int`. |
| `ptr = &score;`   | The `&` operator gets the memory address of `score`. That address is now stored in `ptr`. |
| `*ptr`            | The `*` operator (called dereference) means: "go to the address stored in `ptr` and get the value there." |

#### Pointers in the Kernel

Let's look at a real pointer declaration in FreeBSD.

Remember our good friend, the `tty_info()` function from `sys/kern/tty_info.c`?

Inside it, at line 288, you'll find this line:

```c
struct proc *p, *ppick;
```

Here, `p` and `ppick` are **pointers** to a `struct proc`, which represents a process.

What this line means:

- `p` and `ppick` don't *store* processes; they **point to** process structures in memory.
- In FreeBSD, almost all kernel structures are accessed via pointers because data is passed around and shared across kernel subsystems.

Later in the same function, at line 333, we see:

```c
/*
* Pick the most interesting process and copy some of its
* state for printing later.  This operation could rely on stale
* data as we can't hold the proc slock or thread locks over the
* whole list. However, we're guaranteed not to reference an exited
* thread or proc since we hold the tty locked.
*/
p = NULL;
LIST_FOREACH(ppick, &tp->t_pgrp->pg_members, p_pglist)
    if (proc_compare(p, ppick))
        p = ppick;
```

Here:

- `LIST_FOREACH()` is walking over a linked list of processes.
- `ppick` is pointing to each process in the group.
- The function `proc_compare()` helps pick the *"most interesting"* process.
- And `p` gets assigned to point to that process.

> Don't worry if the kernel example feels a little dense right now. The key takeaway is simple: 
>
> ***in FreeBSD, pointers are everywhere because kernel structures are almost always shared and referenced instead of copied.***

#### A Simple Analogy

Think of pointers as **labels with GPS coordinates**. Instead of carrying the treasure, they tell you where to dig.

- A **regular variable** holds the value.
- A **pointer** holds the address of the value.

This is extremely useful in systems programming, where we often pass **references to data** rather than the data itself.

#### Quick Check: Test Your Understanding

Can you tell what this code will print?

```c
int num = 25;
int *p = &num;

printf("num = %d\n", num);
printf("*p = %d\n", *p);
```

Answer:

```c
num = 25
*p = 25
```

Because both `num` and `*p` refer to the **same location** in memory.

#### Summary

- A pointer is a variable that **stores a memory address**.
- Use `&` to get the address of a variable.
- Use `*` to access the value at a pointer's address.
- Pointers are heavily used in FreeBSD (and all OS kernels) because they allow efficient access to shared and dynamic data.

#### Mini Hands-On Lab — Your First Pointers

**Goal**
Build confidence with the three core pointer moves: taking an address with `&`, storing it in a pointer, and reading or writing through that pointer with `*`.

**Starter code**

```c
#include <stdio.h>

int main(void) {
    int value = 10;
    int *p = NULL;              // Good habit: initialize pointers

    /* 1. Point p to value using the address of operator */

    /* 2. Print value, the address stored in p, and the value via *p */

    /* 3. Change value through the pointer, then print value again */

    /* 4. Declare another int named other = 99 and re point p to it.
          Print *p and other to confirm they match. */

    return 0;
}
```

**Tasks**

1. Set `p` to the address of `value`.
2. Print:
   - `value = ...`
   - `p points to address ...` (use `printf("p points to address %p\n", (void*)p);`)
   - `*p = ...`
3. Write through the pointer with `*p = 20;` and print `value` again.
4. Create `int other = 99;`, then set `p = &other;` and print `*p` and `other`.

**Expected output example** (address will differ):

```c
value = 10
p points to address 0x...
*p = 10
value after write through p = 20
other = 99
*p after re pointing = 99
```

**Stretch exercise**

- Add `int *q = p;` and then set `*q = 123;`. Print both `*p` and `other`. What happened?

- Write a helper function:

  ```c
  void set_twice(int *x) { *x = *x * 2; }
  ```

  Call it with `set_twice(&value);` and observe the result.

#### Common Beginner Pitfalls with Pointers

If pointers feel slippery, you're not alone. Most C beginners run into the same traps over and over, and even experienced developers occasionally fall into them.

- **Using an uninitialized pointer**
   Declaring `int *p;` without setting it to something valid leaves `p` pointing to "somewhere" in memory.
   → Always initialize pointers (to `NULL` or a valid address).
- **Confusing the pointer with the data**
   Beginners often mix up `p` (the address) with `*p` (the value at that address). Writing to the wrong one can silently corrupt memory.
   → Ask yourself: am I working with the pointer or the pointee?
- **Losing track of ownership**
   If a pointer refers to memory that was freed or that belongs to a different part of the program, using it again is a serious bug (a "dangling pointer").
   → We'll learn strategies to manage memory safely later.
- **Forgetting about types**
   A pointer to `int` is not the same as a pointer to `char`. Mixing types can cause subtle errors because the compiler uses the type to decide how many bytes to step through in memory.
   → Always match pointer types carefully.
- **Assuming all addresses are valid**
   Just because a pointer contains a number doesn't mean that address is safe to use. The kernel is full of memory that your code must not touch without permission.
   → Never invent or guess addresses; only use valid ones from the kernel or OS.

These mistakes are not small annoyances; in kernel development, they can bring down the entire system. The good news is that by understanding how pointers work and building safe habits, you'll learn to avoid them.

#### Why Pointers Matter in Driver Development

So why spend so much time learning pointers? Because pointers are the language of the kernel!

- In user programs, you often work with copies of data. In the kernel, **copying is too expensive**, so we pass around pointers instead.
- Device drivers constantly need to share state between different parts of the system (processes, threads, hardware buffers). Pointers are how we make that possible.
- Pointers let us build flexible structures like **linked lists, queues, and tables**, which are everywhere in FreeBSD's source code.
- Most importantly, **hardware itself is accessed through memory addresses**. If you want to talk to a device, you'll often be handed a pointer to its registers or buffers.

Understanding pointers is not just about writing clever C code. It's about speaking the kernel's native tongue. Without them, you cannot build safe, efficient, or even functional device drivers.

#### Wrapping Up

We've just taken our first careful step into the world of pointers: variables that don't hold data directly but instead hold the location of data. This shift in perspective is what makes C so powerful and, at the same time, so dangerous if misunderstood.

Pointers let us share information between parts of a program without copying it around, which is essential in an operating system kernel where efficiency and precision matter. But this power also comes with responsibility: mixing up addresses, dereferencing invalid pointers, or forgetting what memory a pointer refers to can easily crash your program or, in the case of a driver, the entire operating system.

That's why understanding pointers is not just an academic exercise, but a survival skill for FreeBSD driver development.

In the next section, we'll move from the big picture into the nuts and bolts: how to correctly **declare pointers**, how to **use them in practice**, and how to start building **safe habits from the very beginning**.

### Declaring and Using Pointers

Now that you know what a pointer is, a variable that stores a memory address instead of a direct value, it is time to learn how to declare and use them in your programs. This is where the idea of a pointer stops being abstract and becomes something you can experiment with in code.

We will move carefully, step by step, with small and fully commented examples. You will see how to declare a pointer, how to assign it the address of another variable, how to dereference it to reach the stored value, and how to modify data indirectly. Along the way, we will look at real FreeBSD kernel code that relies on pointers every day.

#### Declaring a Pointer

In C, you declare a pointer using the star symbol `*`. The general pattern looks like this:

```c
int *ptr;
```

This line means:

*"I am declaring a variable called `ptr`, and it will hold the address of an integer."*

The `*` here does not mean that the name of the variable is `*ptr`. It is part of the type declaration, telling the compiler that `ptr` is not a plain integer but a pointer to an integer.

Let's see a complete program that you can type and run:

```c
#include <stdio.h>

int main(void) {
    int value = 10;       // Declare a regular integer
    int *ptr;             // Declare a pointer to an integer

    ptr = &value;         // Store the address of 'value' in 'ptr'

    printf("The value is: %d\n", value);         
    printf("Address of value is: %p\n", (void *)&value); 
    printf("Pointer ptr holds: %p\n", (void *)ptr);       
    printf("Value through ptr: %d\n", *ptr);     

    return 0;
}
```

Run this program and compare the output. You will see that `ptr` contains the same address as `&value`, and when you use `*ptr`, you get back the integer stored there.

Think of it like this: writing down a friend's street address is `&value`. Saving that address in your contacts list is `ptr`. Actually going to that house to say hello (or grab a snack) is `*ptr`.

#### The Importance of Initialisation

One of the most important rules about pointers is: never use them before you assign them a valid address. An uninitialised pointer holds garbage data, which means it may point to a random memory location. If you try to dereference it, the program will almost certainly crash.

Here is an unsafe example:

```c
int *dangerous_ptr;
printf("%d\n", *dangerous_ptr);  // Undefined behaviour!
```

Since `dangerous_ptr` was never assigned a valid address, the program will attempt to read some unpredictable area of memory. In user programs this usually causes a crash. In kernel code it can be far worse, leading to corruption of critical data structures and even security vulnerabilities. This is why being disciplined about initialisation is so important when programming for FreeBSD.

#### A Real Example from FreeBSD

If you open the file `sys/kern/tty_info.c`, you will find the following declaration inside the `tty_info()` function, at line 289:

```c
struct thread *td, *tdpick;
```

Both `td` and `tdpick` are pointers to a structure called `thread`. FreeBSD uses these pointers to walk through all threads that belong to a process. Later in the code, at line 341, you will see these pointers being utilised:

```c
FOREACH_THREAD_IN_PROC(p, tdpick)
    if (thread_compare(td, tdpick))
        td = tdpick;
```

The kernel is looping through each thread in process `p`. It compares them using the helper function `thread_compare()`, and if one is a better match, it updates the pointer `td` to refer to that thread.

Notice that `td` itself is just a label. What changes is the address it holds, which in turn tells the kernel which thread to focus on. This pattern is extremely common in the kernel: pointers are declared at the top of a function, then updated step by step as the function works its way through structures.

#### Modifying a Value Through a Pointer

Another classic use of pointers is indirect modification. Let's walk through a simple program:

```c
#include <stdio.h>

int main(void) {
    int age = 30;           // A regular variable
    int *p = &age;          // Pointer to that variable

    printf("Before: age = %d\n", age);

    *p = 35;                // Change the value through the pointer

    printf("After: age = %d\n", age);

    return 0;
}
```

When we run this code, it prints `30` before and `35` after. We never assigned directly to `age`, but by dereferencing `p`, we reached into its memory location and changed the value stored there.

This technique is used everywhere in system programming. Functions that need to return more than one value, or that must directly alter data structures inside the kernel, rely on pointers. Without them, it would be impossible to write efficient drivers or manage complex objects like processes, devices, and memory buffers.

#### Mini Hands-On Lab — Pointer Chains

**Goal**
Learn how multiple pointers can point to the same variable, and how a pointer-to-pointer works in practice. This prepares you for real kernel patterns where functions receive not just data but pointers to pointers that need updating.

**Starter Code**

```c
#include <stdio.h>

int main(void) {
    int value = 42;
    int *p = &value;      // p points to value
    int **pp = &p;        // pp points to p (pointer to pointer)

    /* 1. Print value directly, through p, and through pp */
    
    /* 2. Change value to 100 using *p */
    
    /* 3. Change value to 200 using **pp */
    
    /* 4. Make p point to a new variable other = 77, via pp */
    
    /* 5. Print value, other, *p, and **pp to observe the changes */
    
    return 0;
}
```

**Tasks**

1. Print the same integer in three different ways:
   - Directly (`value`)
   - Indirectly with `*p`
   - Double indirection with `**pp`
2. Assign `100` to `value` using `*p`, then print `value`.
3. Assign `200` to `value` using `**pp`, then print `value`.
4. Declare a second variable `int other = 77;`
    Use the double pointer (`*pp = &other;`) to make `p` point to `other` instead.
5. Print `other`, `*p`, and `**pp`. Confirm that all three match.

**Expected Output (addresses will differ)**

```c
value = 42
*p = 42
**pp = 42
value after *p write = 100
value after **pp write = 200
other = 77
*p now points to other = 77
**pp also sees 77
```

**Stretch Exercise**

Write a function that takes a pointer to a pointer:

```c
void redirect(int **pp, int *new_target) {
    *pp = new_target;
}
```

- Call it to redirect `p` from `value` to `other`.
- Print `*p` afterwards to see the result.

This is a common idiom in kernel code, where functions receive a pointer to a pointer so they can safely update what the caller's pointer points to.

#### Common Beginner Pitfalls at Declaration and Use

When you start declaring and using pointers, a new set of mistakes can creep in. They are different from the conceptual traps we already covered, and each has a simple way to avoid it.

One mistake is misplacing the star `*` in a declaration. Writing `int* a, b;` looks like you declared two pointers, but in reality only `a` is a pointer, and `b` is a plain integer. To avoid this confusion, always write the star next to each variable name: `int *a, *b;`. This makes it explicit that both are pointers.

Another trap is assigning a pointer without matching the type. For instance, storing the address of a `char` in an `int *` may compile with warnings but is unsafe. Always ensure the pointer type matches the type of the variable you are pointing to. If you need to work with different types, use casts carefully and deliberately, not by accident.

A common error is dereferencing too early. Beginners sometimes write `*p` before assigning `p` a valid address, which leads to undefined behaviour. Get into the habit of initialising pointers at declaration. Use `NULL` if you do not yet have a valid address, and only dereference after you have assigned a real target.

Another pitfall is overthinking addresses. Printing or comparing the raw numeric value of pointers is rarely meaningful. What matters is the relationship between a pointer and its pointee. Focus on using `%p` in `printf` to display addresses for debugging, but remember that addresses themselves are not portable values you can calculate casually.

Finally, declaring multiple pointers in one line without care often causes subtle errors. A line like `int *a, b, c;` gives you one pointer and two integers, not three pointers. To avoid mistakes, keep pointer declarations simple and clear, and never assume all variables in a list share the same pointer type.

By adopting these habits early, clear declarations, matching types, safe initialisation, and careful dereferencing, you will build a strong foundation for working with pointers in larger programs and in FreeBSD kernel code.

#### Why This Matters in Driver Development

Declaring and using pointers correctly is more than a matter of style. In FreeBSD drivers, you will often see entire groups of pointers declared together, each one tied to a subsystem of the kernel or a hardware resource. If you misdeclare one of these pointers, you might end up mixing integers and addresses, leading to very subtle bugs.

Consider linked lists in the kernel. A device driver might declare several pointers to structures like `struct mbuf` (network buffers) or `struct cdev` (device entries). These pointers are chained together to form queues, and every declaration must be precise. One missing star or one mismatched type can mean that a list traversal ends in a kernel panic.

Another reason declaration matters is efficiency. Kernel code often creates pointers that refer to existing objects instead of making copies. Declaring pointers correctly means you can traverse large structures, like the list of threads in a process, without duplicating data or wasting memory.

The lesson is clear: understanding how to declare and use pointers properly gives you the vocabulary to describe kernel objects, navigate them, and connect them together safely.

#### Wrapping Up

At this point, you have moved beyond simply knowing what a pointer is. You can now declare a pointer, initialise it, print its address, dereference it, and even use it to modify a variable indirectly. You have seen how FreeBSD uses these techniques in real code, such as iterating through process threads or updating kernel structures. You also practised with pointer chains and saw how a pointer-to-pointer lets you redirect another pointer, a pattern that will appear often in kernel APIs.

What makes this knowledge powerful is that it transforms your ability to work with functions. Passing pointers into functions allows those functions to update the caller's data, to redirect a pointer to a new target, or to return multiple results at once. In kernel development, this is not a rare pattern but the norm. Drivers almost always interact with the kernel and hardware by passing pointers into functions and receiving updated pointers back.

In the next section, we will explore **Pointers and Functions**, and you will see how this combination becomes the standard way to write flexible, efficient, and safe code inside FreeBSD.

### Pointers and Functions

Back in Section 4.7, we learned that function parameters are always passed by value. This means that when you call a function, it usually receives only a copy of the variable you provide. As a result, the function can't change the original value that lives in the caller.

Now that we've introduced pointers, we have a new possibility. By passing a pointer into a function, you give it direct access to the caller's memory. This is the standard way in C and especially in FreeBSD kernel code to let functions modify data outside their own scope or return multiple results.

Let's walk through the difference step by step.

#### First Try: Passing by Value (Doesn't Work)

```c
#include <stdio.h>

void set_to_zero(int n) {
    n = 0;  // Only changes the copy
}

int main(void) {
    int x = 10;

    set_to_zero(x);  
    printf("x is now: %d\n", x);  // Still prints 10!

    return 0;
}
```

Here, the function `set_to_zero()` receives a copy of `x`. That copy is modified, but the real `x` in `main()` never changes.

#### Second Try: Passing by Pointer (Works)

```c
#include <stdio.h>

void set_to_zero(int *n) {
    *n = 0;  // Follow the pointer and change the real variable
}

int main(void) {
    int x = 10;

    set_to_zero(&x);  // Give the function the address of x
    printf("x is now: %d\n", x);  // Prints 0!

    return 0;
}
```

This time the caller sends the address of `x` using `&x`. Inside the function, `*n` lets us reach into that memory location and actually change the variable in `main()`.

This pattern is simple but incredibly powerful. It transforms functions from isolated workshops into tools that can work directly on the caller's data.

#### Why This Matters in the Kernel

In kernel code, this technique is not just helpful, it's essential. Kernel functions often need to report back multiple pieces of information. Since C does not allow multiple return values, the usual approach is to pass pointers to variables or structures that the function can fill in.

Here's an example you can find in the FreeBSD source file `sys/kern/tty_info.c`, at line 382:

```c
rufetchcalc(p, &ru, &utime, &stime);
```

The function `rufetchcalc()` fills in statistics about CPU usage for a process. It can't simply "return" all three results, so it accepts pointers to variables where it will write the data.

Let's simplify this with a small simulation:

```c
#include <stdio.h>

// A simplified kernel-style function
void get_times(int *user, int *system) {
    *user = 12;     // Fake "user time"
    *system = 8;    // Fake "system time"
}

int main(void) {
    int utime, stime;

    get_times(&utime, &stime);  

    printf("User time: %d\n", utime);
    printf("System time: %d\n", stime);

    return 0;
}
```

Here, `get_times()` updates both `utime` and `stime` in one call. That's precisely how kernel code returns complex results without extra overhead.

#### Common Beginner Pitfalls

Pointers with functions are a frequent stumbling block for beginners. Watch out for these mistakes:

- **Forgetting the `&` in the call**: If you write `set_to_zero(x)` instead of `set_to_zero(&x)`, you'll pass the value instead of the address, and nothing will change.
- **Assigning the pointer, not the value**: Inside the function, writing `n = 0;` only overwrites the pointer itself. You must use `*n = 0;` to change the caller's variable.
- **Overstepping responsibilities**: A function should not free or reallocate memory that belongs to the caller unless it is explicitly designed to do so. Otherwise, you risk creating dangling pointers.

The safest habit is always to be clear about what the pointer represents, and to think carefully before modifying anything that belongs to the caller.

#### Hands-On Lab: Write Your Own Setter

Here's a small challenge to test your understanding. Complete the function so that it doubles the value of the variable given:

```c
#include <stdio.h>

void double_value(int *n) {
    // TODO: Write code that makes *n twice as large
}

int main(void) {
    int x = 5;

    double_value(&x);
    printf("x is now: %d\n", x);  // Should print 10

    return 0;
}
```

If your function works, you've just written your first function that modifies a caller's variable using a pointer, precisely the kind of operation you'll use constantly in device drivers.

#### Wrapping Up: Pointers Unlock New Possibilities

By themselves, functions only work with copies. With pointers, functions gain the ability to modify the caller's variables and to pass back multiple results efficiently. This pattern shows up everywhere in FreeBSD, from memory management to process scheduling, and it is one of the most essential techniques you can master as a future driver developer.

Now that we've seen how pointers connect with functions, let's take the next step. Pointers also form a natural partnership with another key feature of C: arrays. In the next section, we'll explore **Pointers and Arrays: A Powerful Duo**, and you'll discover how these two concepts work together to make memory access both flexible and efficient.

### Pointers and Arrays: A Powerful Duo

In C, arrays and pointers are like close friends. They are not the same thing, but they are deeply connected, and in practice, they often work together. This connection shows up constantly in kernel code, where performance and direct memory access matter. If you can understand how arrays and pointers interact, you will unlock a powerful toolset for navigating buffers, strings, and hardware data.

#### What's the Connection?

There is a straightforward rule that explains most of the relationship:

**In most expressions, the name of an array acts like a pointer to its first element.**

That means if you declare:

```c
int numbers[3] = {10, 20, 30};
```

Then the name `numbers` can be treated as if it were the same as `&numbers[0]`. So the line:

```c
int *ptr = numbers;
```

Is equivalent to:

```c
int *ptr = &numbers[0];
```

The pointer `ptr` now points directly to the first element of the array.

#### A Simple Example

```c
#include <stdio.h>

int main(void) {
    int values[3] = {100, 200, 300};

    int *p = values;  // 'values' behaves like &values[0]

    printf("First value: %d\n", *p);         // prints 100
    printf("Second value: %d\n", *(p + 1));  // prints 200
    printf("Third value: %d\n", *(p + 2));   // prints 300

    return 0;
}
```

Here, the pointer `p` starts at the first element. Adding `1` to `p` moves it forward by one integer, and so on. This is called **pointer arithmetic**, and we will study its rules more carefully in the next section. For now, the key idea is that arrays and pointers share the same memory layout, which makes moving through an array with a pointer both natural and efficient.

#### Using Arrays and Pointers in FreeBSD

The FreeBSD kernel makes heavy use of this connection. A good example is found inside `sys/kern/tty_info.c` in the `tty_info()` function, at line 384:

```c
strlcpy(comm, p->p_comm, sizeof comm);
```

Here, `p->p_comm` is a character array that belongs to a process structure. The variable `comm` is another array declared locally, at line 295:

```c
char comm[MAXCOMLEN + 1];
```

The function `strlcpy()` copies the string from one array into another. Under the hood, it uses pointer arithmetic to walk through each character until the copy is done. You do not need to see those details to use it, but it is important to know that arrays and pointers make this possible. This is why so many kernel functions operate on "char *" even though you often start with a character array.

#### Arrays and Pointers: The Differences That Matter

Since arrays and pointers behave in similar ways, it is tempting to think they are the same thing. But they are not, and understanding the differences will help you avoid many subtle bugs.

When you declare an array, the compiler reserves a fixed block of memory large enough to hold all its elements. The array's name represents this memory location, and that association cannot be changed. For example, if you declare `int a[5];`, the compiler allocates space for five integers, and `a` will always refer to that same block of memory. You cannot later reassign `a` to point somewhere else.

A pointer, by contrast, is a variable that stores an address. It does not allocate storage for multiple items by itself. Instead, it can point to any valid memory location you choose. For instance, `int *p;` creates a pointer that may later hold the address of the first element of an array, the address of a single variable, or memory that has been allocated dynamically. You can also reassign the pointer freely, making it a much more flexible tool.

Another key distinction is that the compiler knows the size of an array, but it does not track the size of the memory a pointer refers to. This means the array's boundaries are known at compile time, while a pointer only knows where it starts, not how far it extends. That responsibility falls on you as the programmer.

These rules can be summarised in plain language. An array is a fixed block of storage, like a house built on a specific lot of land. A pointer is like a set of keys: you can use them to access that house, but tomorrow you might use the same keys to open another house entirely. Both are useful, but they serve different purposes, and the distinction becomes crucial in kernel code where memory management and safety cannot be left to chance.

#### Common Beginner Pitfall: Off-by-One Errors

Because arrays and pointers are so closely related, the same mistake can happen in two different guises: stepping one element too far.

With arrays, the error looks like this:

```c
int items[3] = {1, 2, 3};
printf("%d\n", items[3]);  // Invalid! Out of bounds
```

Here, the valid indices are `0`, `1`, and `2`. Using `3` goes past the end.

With pointers, the same error can happen more subtly:

```c
int items[3] = {1, 2, 3};
int *p = items;

printf("%d\n", *(p + 3));  // Also invalid, same as items[3]
```

In both cases, you are asking for memory beyond the array's boundary. The compiler will not stop you, and the program may even seem to run correctly sometimes, which makes the bug even more dangerous.

In user programs, this usually means corrupted data or a crash. In kernel code, it can mean memory corruption, a kernel panic, or even a security hole. That is why experienced FreeBSD developers are extremely careful when writing loops that walk through arrays or buffers with pointers. The loop condition is just as important as the loop body.

##### Safety Habit

The best way to avoid off-by-one errors is to make your loop boundaries explicit and double-check them. If an array has `n` elements, valid indices always run from `0` to `n - 1`. When using a pointer, think in terms of "how many elements have I advanced?" rather than "how many bytes."

For example:

```c
for (int i = 0; i < 3; i++) {
    printf("%d\n", items[i]);  // Safe, i goes 0..2
}

for (int i = 0; i < 3; i++) {
    printf("%d\n", *(p + i));  // Safe, same rule
}
```

By making the upper limit part of your loop condition, you ensure you never walk past the end of the array. This habit will save you from many subtle bugs, especially when you move from small exercises into real kernel code.

#### Why FreeBSD Style Embraces This Duo

Many FreeBSD subsystems manage buffers as arrays while navigating them with pointers. This combination lets the kernel avoid unnecessary copying, keep operations efficient, and interact directly with hardware. Whether you are looking at character device buffers, network packet rings, or process command names, you will see this pattern again and again.

By mastering the array–pointer relationship, you will be able to read and write kernel code more confidently, recognizing when the code is simply walking through memory one element at a time.

#### Hands-On Lab: Walking an Array with a Pointer

Try writing a small program that prints all the elements of an array using a pointer instead of array indexing.

```c
#include <stdio.h>

int main(void) {
    int numbers[5] = {10, 20, 30, 40, 50};
    int *p = numbers;

    for (int i = 0; i < 5; i++) {
        printf("numbers[%d] = %d\n", i, *(p + i));
    }

    return 0;
}
```

Now, modify the loop so that instead of writing `*(p + i)`, you increment the pointer directly:

```c
for (int i = 0; i < 5; i++) {
    printf("numbers[%d] = %d\n", i, *p);
    p++;
}
```

Notice that the result is the same. This is the power of combining pointers and arrays. Try experimenting by starting the pointer at `&numbers[2]` and see what gets printed.

#### Wrapping Up: Arrays and Pointers Work Best Together

You have now seen how arrays and pointers fit together. Arrays provide the structure, while pointers provide the flexibility to navigate memory efficiently. In the FreeBSD kernel, this combination is everywhere, from device buffers to string manipulation. Always remember the two golden rules: `array[i]` is equivalent to `*(array + i)`, and you must never step outside the bounds of the array.

In the next section, we will explore Pointer Arithmetic more deeply. You will learn how incrementing a pointer works under the hood, why it follows the size of the type, and what boundaries you must respect to avoid stepping into dangerous memory.

### Pointer Arithmetic and Boundaries

Now that you know how pointers can point to individual variables and even to arrays, we are ready to take the next step: learning how to move those pointers around. This ability is called **pointer arithmetic**.

The name might sound intimidating, but the idea is simple. Imagine a row of boxes placed neatly side by side. A pointer is like your finger pointing to one of those boxes. Pointer arithmetic is nothing more than moving your finger forward or backward to reach another box.

#### What Is Pointer Arithmetic?

When you add or subtract an integer from a pointer, C advances the pointer by **elements**, not by raw bytes. The step size depends on the type the pointer refers to:

- If `p` is an `int *`, then `p + 1` moves by `sizeof(int)` bytes.
- If `q` is a `char *`, then `q + 1` moves by `sizeof(char)` bytes (which is always 1).
- If `r` is a `double *`, then `r + 1` moves by `sizeof(double)` bytes.

This behaviour is what makes pointer arithmetic natural for walking arrays, because arrays live in contiguous memory. Each "+1" lands you precisely on the next element, not in the middle of it.

Let’s see a complete program that demonstrates this. I have added comments to allow you to understand what's happening at each step. 

Save it as `pointer_arithmetic_demo.c`:

```c
/*
 * Pointer Arithmetic Demo (commented)
 *
 * This program shows that when you add 1 to a pointer, it moves by
 * the size of the element type (in elements, not raw bytes).
 * It prints addresses and values for int, char, and double arrays,
 * and then shows how to compute the distance between two pointers
 * using ptrdiff_t. All accesses stay within bounds.
 */

#include <stdio.h>    // printf
#include <stddef.h>   // ptrdiff_t, size_t

int main(void) {
    /*
     * Three arrays of different element types.
     * Arrays live in contiguous memory, which is why pointer arithmetic
     * can step through them safely when we stay within bounds.
     */
    int    arr[]  = {10, 20, 30, 40};
    char   text[] = {'A', 'B', 'C', 'D'};
    double nums[] = {1.5, 2.5, 3.5};

    /*
     * Pointers to the first element of each array.
     * In most expressions, an array name "decays" to a pointer to its first element.
     * So arr has type "array of int", but here it becomes "int *" automatically.
     */
    int    *p = arr;
    char   *q = text;
    double *r = nums;

    /*
     * Compute the number of elements in each array.
     * sizeof(array) gives the total bytes in the array object.
     * sizeof(array[0]) gives the bytes in one element.
     * Dividing the two gives the element count.
     */
    size_t n_ints    = sizeof(arr)  / sizeof(arr[0]);
    size_t n_chars   = sizeof(text) / sizeof(text[0]);
    size_t n_doubles = sizeof(nums) / sizeof(nums[0]);

    printf("Pointer Arithmetic Demo\n\n");

    /*
     * INT DEMO
     * p + i moves i elements forward, which is i * sizeof(int) bytes.
     * We print both the address and the value to see how it steps.
     * %zu is the correct format specifier for size_t.
     * %p prints a pointer address; cast to (void *) for correct printf typing.
     */
    printf("== int demo ==\n");
    printf("sizeof(int) = %zu bytes\n", sizeof(int));
    for (size_t i = 0; i < n_ints; i++) {
        printf("p + %zu -> address %p, value %d\n",
               i, (void*)(p + i), *(p + i));   // *(p + i) reads the i-th element
    }
    printf("\n");

    /*
     * CHAR DEMO
     * For char, sizeof(char) is 1 by definition.
     * q + i advances one byte per step.
     */
    printf("== char demo ==\n");
    printf("sizeof(char) = %zu byte\n", sizeof(char));
    for (size_t i = 0; i < n_chars; i++) {
        printf("q + %zu -> address %p, value '%c'\n",
               i, (void*)(q + i), *(q + i));
    }
    printf("\n");

    /*
     * DOUBLE DEMO
     * For double, sizeof(double) is typically 8 on modern systems.
     * r + i advances by 8 bytes per step on those systems.
     */
    printf("== double demo ==\n");
    printf("sizeof(double) = %zu bytes\n", sizeof(double));
    for (size_t i = 0; i < n_doubles; i++) {
        printf("r + %zu -> address %p, value %.1f\n",
               i, (void*)(r + i), *(r + i));
    }
    printf("\n");

    /*
     * Pointer difference
     * Subtracting two pointers that point into the same array
     * yields a value of type ptrdiff_t that represents the distance
     * in elements, not bytes.
     *
     * Here, a points to arr[0] and b points to arr[3].
     * The difference b - a is 3 elements.
     */
    int *a = &arr[0];
    int *b = &arr[3];
    ptrdiff_t diff = b - a; // distance in ELEMENTS

    /*
     * %td is the correct format specifier for ptrdiff_t.
     * We also verify that advancing a by diff elements lands exactly on b.
     */
    printf("Pointer difference (b - a) = %td elements\n", diff);
    printf("Check: a + diff == b ? %s\n", (a + diff == b) ? "yes" : "no");

    /*
     * Program ends successfully.
     */
    return 0;
}
```

Compile and run it on FreeBSD:

```sh
% cc -Wall -Wextra -o pointer_arithmetic_demo pointer_arithmetic_demo.c
% ./pointer_arithmetic_demo
```

You will see how each type moves by its element size. The `int *` steps by 4 bytes (on most modern FreeBSD systems), the `char *` steps by 1, and the `double *` steps by 8. Notice how the addresses jump accordingly, while the values are fetched correctly with `*(pointer + i)`.

The final check with `b - a` shows that pointer subtraction is also measured in **elements, not bytes**. If `a` points to the start of the array and `b` points three elements ahead, then `b - a` gives `3`.

This program demonstrates the essential rule of pointer arithmetic: **C moves pointers in units of the pointed-to type**. That is why it works so well with arrays, but also why you must be careful, a wrong step can quickly take you beyond the valid memory region.

#### Walking Through Arrays with Pointers

This property makes pointers especially useful when working with arrays. Since arrays are laid out in contiguous blocks of memory, a pointer can naturally step through them one element at a time.

```c
#include <stdio.h>

int main() {
    int numbers[] = {1, 2, 3, 4, 5};
    int *ptr = numbers;  // Start at the first element

    for (int i = 0; i < 5; i++) {
        printf("Element %d: %d\n", i, *(ptr + i));
        // *(ptr + i) is equivalent to numbers[i]
    }

    return 0;
}
```

Here, the expression `*(ptr + i)` asks C to move forward `i` positions from the starting pointer, then fetch the value at that location. The result is identical to `numbers[i]`. In fact, C allows both notations interchangeably. Whether you write `numbers[i]` or `*(numbers + i)`, you are doing the same thing.

#### Staying Within Boundaries

Pointer arithmetic is powerful, but it comes with a serious responsibility: you must never move beyond the memory that belongs to your array. If you do, you step into undefined behaviour. That can mean a crash, corrupted memory, or silent errors that appear much later.

```c
#include <stdio.h>

int main() {
    int data[] = {42, 77, 99};
    int *ptr = data;

    // Wrong! This goes past the last element.
    printf("Invalid access: %d\n", *(ptr + 3));

    return 0;
}
```

The array `data` has three elements, valid at indices 0, 1, and 2. But `ptr + 3` points to a place immediately after the last element. C does not stop you, but the result is unpredictable.

The safe way is to always respect the number of elements in your array. Instead of hardcoding the size, you can calculate it:

```c
for (int i = 0; i < sizeof(data) / sizeof(data[0]); i++) {
    printf("%d\n", *(data + i));
}
```

This expression divides the total size of the array by the size of a single element, giving the correct element count regardless of the array's length.

#### A Glimpse into the FreeBSD Kernel

Pointer arithmetic shows up frequently in the FreeBSD kernel. Sometimes it is used directly with arrays, but more often it appears while navigating through structures linked together in memory. Let us look at a small excerpt adapted from `sys/kern/tty_info.c` (near line 333):

```c
struct proc *p, *ppick;

p = NULL;
LIST_FOREACH(ppick, &tp->t_pgrp->pg_members, p_pglist) {
    if (proc_compare(p, ppick))
        p = ppick;
}
```

This loop is not using `ptr + 1` as in our array examples, but it is doing the same conceptual job: moving through memory by following pointer links. Instead of stepping through integers in a row, it walks through a chain of process structures connected in a list. The lesson is the same: pointers let you move from one element to the next, but you must always be careful to stay within the intended bounds of the structure.

#### Common Beginner Pitfalls

1. **Forgetting that pointers move by type size**: If you assume `p + 1` moves one byte, you will misunderstand the result. It always moves by the size of the type the pointer refers to.
2. **Going past array limits**: Accessing `arr[5]` when the array has only 5 elements (valid indices 0-4) is a classic off-by-one mistake.
3. **Mixing arrays and pointers too casually**: Although `arr[i]` and `*(arr + i)` are equivalent, an array name itself is not a modifiable pointer. Beginners sometimes try to reassign an array name as if it were a variable, which is not allowed.

To avoid these traps, always calculate sizes carefully, use `sizeof` when possible, and keep in mind that arrays and pointers are close relatives but not identical twins.

#### Tip: Arrays Decay into Pointers

When you pass an array to a function, C actually gives the function only a pointer to the first element. The function has no way to know how many elements exist. For safety, always pass the array size along with the pointer. This is why so many C library and FreeBSD kernel functions include both a buffer pointer and a length parameter.

#### Mini Hands-On Lab: Walking Safely with Pointer Arithmetic

In this lab, you will experiment with pointer arithmetic on arrays, see how to walk through memory step by step, and learn how to detect and prevent stepping outside array boundaries.

Create a file called `lab_pointer_bounds.c` with the following code:

```c
#include <stdio.h>

int main(void) {
    int values[] = {5, 10, 15, 20};
    int *ptr = values;  // Start at the first element
    int length = sizeof(values) / sizeof(values[0]);

    printf("Array has %d elements\n", length);

    // Walk forward through the array
    for (int i = 0; i < length; i++) {
        printf("Forward step %d: %d\n", i, *(ptr + i));
    }

    // Walk backwards using pointer arithmetic
    for (int i = length - 1; i >= 0; i--) {
        printf("Backward step %d: %d\n", i, *(ptr + i));
    }

    // Demonstrate boundary checking
    int index = 4; // Out-of-bounds index
    if (index >= 0 && index < length) {
        printf("Safe access: %d\n", *(ptr + index));
    } else {
        printf("Index %d is out of bounds, refusing to access\n", index);
    }

    return 0;
}
```

##### Step 1: Compile and run

```sh
% cc -o lab_pointer_bounds lab_pointer_bounds.c
% ./lab_pointer_bounds
```

You should see output that walks forward and backward through the array. Notice how both directions are handled using the same pointer, just with different arithmetic.

##### Step 2: Try breaking the rule

Now, change the boundary check:

```c
printf("Unsafe access: %d\n", *(ptr + 4));
```

Compile and run again. On your system, it might print a random number, or it might crash. This is **undefined behaviour** in action. The program stepped outside the array’s safe memory and read garbage. On FreeBSD in user space this might only crash your program, but in kernel space the same mistake could crash the whole system.

##### Step 3: Think like a kernel developer

Kernel code often passes around buffers and pointers, but the kernel does not automatically check array limits for you. Safe coding habits like the check we used earlier, are critical:

```c
if (index >= 0 && index < length) { ... }
```

Always validate that you are within valid bounds before dereferencing a pointer.

**Key Takeaways from This Lab**

- Pointer arithmetic lets you move forward and backwards through arrays.
- Arrays do not carry their length with them; you must track it yourself.
- Accessing memory beyond array boundaries is undefined behaviour.
- In FreeBSD kernel code, these mistakes can lead to panics or vulnerabilities, so always include boundary checks.

#### Challenge Questions

1. **Forward and backwards in one pass**: Write a function `void walk_both(const int *base, size_t n)` that prints pairs `(base[i], base[n-1-i])` using only pointer arithmetic, no array indexing. Stop when the pointers meet or cross.
2. **Bounds checked accessor**: Implement `int get_at(const int *base, size_t n, size_t i, int *out)` that returns 0 on success and a non-zero error code if `i` is out of range. Use only pointer arithmetic to read the value.
3. **Find first match**: Write `int *find_first(int *base, size_t n, int target)` that returns a pointer to the first occurrence or `NULL` if not found. Walk using a moving pointer from `base` to `base + n`.
4. **Reverse in place**: Create `void reverse_in_place(int *base, size_t n)` that swaps elements from the ends toward the middle using two pointers. Do not use indexing.
5. **Safe slice print**: Write `void print_slice(const int *base, size_t n, size_t start, size_t count)` that prints at most `count` elements beginning at `start`, but never steps beyond `n`.
6. **Off by one detector**: Introduce an off-by-one bug in a loop, then add a runtime check that detects it. Fix the loop and confirm the check remains silent.
7. **Stride walk**: Treat the array as records of `stride` ints. Write `void walk_stride(const int *base, size_t n, size_t stride)` that visits only the first element of each record.
8. **Pointer difference**: Given two pointers `a` and `b` into the same array, compute the element distance using `ptrdiff_t`. Verify that `a + distance == b`.
9. **Guarded dereference**: Implement `int try_deref(const int *p, const int *begin, const int *end, int *out)` that only dereferences if `p` lies within `[begin, end)`.
10. **Refactor into functions**: Rewrite the lab so that walking, printing, and boundary checking are separate functions that all use pointer arithmetic.

#### Wrapping Up

Pointer arithmetic gives you a new way to move through memory. It allows you to scan arrays efficiently, traverse structures, and interact with hardware buffers. But with this power comes the danger of stepping outside the safe zone. In user programs, that might crash only your program. In kernel code, a mistake could crash the entire system or open a security hole.

As you continue your journey, keep the mental image of walking along a row of boxes. Step carefully, never wander off the edge, and always count how many boxes you really have. With this habit, you will be ready for the next topic: using pointers to access not just plain values, but entire **structures**, the building blocks of more complex data in FreeBSD.

### Pointers to Structs

In C programming, especially when writing device drivers or working inside the FreeBSD kernel, you will constantly encounter **structs**. A struct is a way of grouping several related variables together under one name. These variables, called *fields*, can be of different types, and together they represent a more complex entity.

Because structs are often large and frequently shared between parts of the kernel, we usually interact with them through **pointers**. Understanding how to work with pointers to structs is, therefore, a crucial skill for reading kernel code and writing drivers of your own.

Let’s build this step by step.

#### What Is a Struct?

A struct groups multiple variables into a single unit. For example:

```c
struct Point {
    int x;
    int y;
};
```

This defines a new type called `struct Point`, which has two integer fields: `x` and `y`.

We can create a variable of this type and assign values to its fields:

```c
struct Point p1;
p1.x = 10;
p1.y = 20;
```

At this point, `p1` is a concrete object in memory, holding two integers side by side.

#### Introducing Pointers to Structs

Just like we can have a pointer to an integer or a pointer to a character, we can also have a pointer to a struct:

```c
struct Point *ptr;
```

If we already have a variable of type `struct Point`, we can store its address in the pointer:

```c
ptr = &p1;
```

Now `ptr` holds the address of `p1`. To reach the fields of the struct through this pointer, C provides two notations.

#### Accessing Struct Fields Through Pointers

Suppose we have:

```c
struct Point p1 = {10, 20};
struct Point *ptr = &p1;
```

There are two ways to access the fields via the pointer:

```c
// Method 1: Explicit dereference
printf("x = %d\n", (*ptr).x);

// Method 2: Arrow operator
printf("y = %d\n", ptr->y);
```

Both are correct, but the arrow operator (`->`) is much cleaner and is the style you will see everywhere in FreeBSD kernel code.

So:

```c
ptr->x
```

Means the same as:

```c
(*ptr).x
```

But it is easier to read and write.

#### Why Pointers Are Preferred

Passing an entire struct around by value can be expensive, since C would need to copy every field each time. Instead, the kernel almost always passes around *pointers to structs*. This way, only an address (a small integer) is passed, and different parts of the system can all work with the same underlying object.

This is particularly important in device drivers, where structs often represent significant and complex entities such as devices, processes, or threads.

#### A Real FreeBSD Example

Let’s look at a real snippet from the FreeBSD source tree. 

In `sys/kern/tty_info.c`, the `tty_info()` function works with a `struct proc`, which represents a process. This code appears **around lines 331-350** in the FreeBSD 14.3 source: 

```c
struct proc *p, *ppick;

p = NULL;

// Walk through all members of the foreground process group
LIST_FOREACH(ppick, &tp->t_pgrp->pg_members, p_pglist)
        // Use proc_compare() to decide if this candidate is "better"
        if (proc_compare(p, ppick))
                p = ppick;

// Later in the function, access the chosen process
pid = p->p_pid;                        // get the process ID
strlcpy(comm, p->p_comm, sizeof comm); // copy the process name into a local buffer
```

Here’s what happens, step by step:

- `p` and `ppick` are pointers to `struct proc`. The code initialises `p` to `NULL`, then iterates over the foreground process group with `LIST_FOREACH`. 
- On each step, it calls `proc_compare()` to decide whether the current candidate `ppick` is "better" than the one already chosen; if so, it updates `p`. Later, it reads fields from the selected process via `p->p_pid` and `p->p_comm`. 

This is a typical kernel pattern: **select a struct instance via pointer traversal, then access its fields through `->`.**

**Note:** `proc_compare()` encapsulates the selection logic: it prefers runnable processes, then the one with higher recent CPU usage, de-prioritises zombies, and breaks ties by choosing the higher PID.

#### A Quick Bridge: what `LIST_FOREACH` does

Beginners often see `LIST_FOREACH(...)` and wonder what magic is happening. There’s no magic, it’s a macro that walks a **singly-linked list**. Its common BSD style is:

```c
LIST_FOREACH(item, &head->list_field, link_field) {
    /* use item->field ... */
}
```

- `item` is the loop variable that points to each element.
- `&head->list_field` is the list head you’re iterating.
- `link_field` is the name of the link that chains elements together (the "next" pointer field in each node).

In our snippet, `ppick` is the loop variable, `&tp->t_pgrp->pg_members` is the list of processes in the foreground process group, and `p_pglist` is the link field inside each `struct proc`. Each iteration points `ppick` at the next process, allowing the code to compare and ultimately select the one stored in `p`. 

This small macro hides the pointer-chasing details so your code reads like: "for each process in this process group, consider it as a candidate."

#### A Minimal User-Space Example

Here’s a simple program you can compile and run yourself to get hands-on practice:

```c
#include <stdio.h>
#include <string.h>

// Define a simple struct
struct Device {
    int id;
    char name[20];
};

int main() {
    struct Device dev1 = {42, "tty0"};
    struct Device *dev_ptr = &dev1;

    // Access fields through the pointer
    printf("Device ID: %d\n", dev_ptr->id);
    printf("Device Name: %s\n", dev_ptr->name);

    // Change values through the pointer
    dev_ptr->id = 43;
    strcpy(dev_ptr->name, "ttyS1");

    // Show updated values
    printf("Updated Device ID: %d\n", dev_ptr->id);
    printf("Updated Device Name: %s\n", dev_ptr->name);

    return 0;
}
```

This mirrors what happens in the kernel. We define a struct, create an instance of it, then use a pointer to access and modify its fields.

#### Common Beginner Pitfalls with Struct Pointers

Working with pointers to structs is straightforward once you get used to it, but beginners often fall into the same traps. Let’s look at the most frequent mistakes and how to avoid them.

**1. Forgetting to Initialise the Pointer**
 A pointer that hasn’t been given a value points to "somewhere" in memory,  which usually means garbage. Accessing it causes undefined behaviour, often leading to crashes.

```c
struct Device *d;  // Uninitialised, points to who-knows-where
d->id = 42;        // Undefined behaviour
```

**How to avoid it:** Always initialise your pointer, either to `NULL` or to the address of a real struct.

```c
struct Device dev1;
struct Device *d = &dev1; // Safe: d points to dev1
```

**2. Confusing `.` and `->`**
 Remember: use the dot (`.`) to access a field when you have a real struct variable, and use the arrow (`->`) when you have a pointer to a struct. Mixing them up is a common beginner error.

```c
struct Point p1 = {1, 2};
struct Point *ptr = &p1;

printf("%d\n", p1.x);    // Dot for variables
printf("%d\n", ptr->x);  // Arrow for pointers
printf("%d\n", ptr.x);   // Error: ptr is not a struct
```

**How to avoid it:** Ask yourself: "Am I working with a pointer or the struct itself?" That tells you which operator to use.

**3. Dereferencing NULL Pointers**
 If a pointer is set to `NULL` (which is common as an initial or error state), dereferencing it will immediately crash your program.

```c
struct Device *d = NULL;
printf("%d\n", d->id);  // Crash: d is NULL
```

**How to avoid it:** Always check pointers before dereferencing:

```c
if (d != NULL) {
    printf("%d\n", d->id);
}
```

In kernel code, this check is especially important. Dereferencing a NULL pointer inside the kernel can bring the whole system down.

**4. Using a Pointer After the Struct Has Gone Out of Scope**
 Pointers don’t "own" the struct they point to. If the struct disappears (for example, because it was a local variable in a function that has returned), the pointer becomes invalid, a *dangling pointer*.

```c
struct Device *make_device(void) {
    struct Device d = {99, "tty0"};
    return &d; // Dangerous: d disappears after function returns
}
```

**How to avoid it:** Never return the address of a local variable. Allocate dynamically (with `malloc` in user programs or `malloc(9)` in the kernel) if you need a struct to outlive the function that creates it.

**5. Assuming a Struct Is Small Enough to Copy Around**
 In user programs, you can sometimes get away with passing structs by value. But in kernel code, structs often represent large, complex objects,  sometimes with embedded lists or pointers of their own. Copying them accidentally can cause subtle and serious bugs.

```c
struct Device dev1 = {1, "tty0"};
struct Device dev2 = dev1; // Copies all fields, not a shared reference
```

**How to avoid it:** Pass pointers to structs instead of copying them, unless you are certain that a shallow copy is intended.

**Main Takeaway:**
The most common mistakes with struct pointers come from forgetting what a pointer really is: just an address. Always initialise pointers, distinguish carefully between `.` and `->`, check for NULL, and be mindful of scope and lifetime. In kernel development, a single misstep with a struct pointer can destabilise the entire system, so adopting safe habits early will serve you well.

#### Mini Hands-On Lab: Struct Pointer Pitfalls (No `malloc` yet)

This lab reproduces common mistakes with pointers to structs and then shows safe, beginner-friendly alternatives using only stack variables and function "out-parameters".

Create a file `lab_struct_pointer_pitfalls_nomalloc.c`:

```c
/*
 * lab_struct_pointer_pitfalls_nomalloc.c
 *
 * Classic pitfalls with pointers to structs, rewritten to avoid malloc.
 * Build and run each case and read the console output as you go.
 */

#include <stdio.h>
#include <string.h>

struct Device {
    int  id;
    char name[16];
};

/* -----------------------------------------------------------
 * Case 1: Uninitialised pointer (UB) vs. correctly initialised
 * ----------------------------------------------------------- */
static void case_uninitialised_pointer(void) {
    printf("\n[Case 1] Uninitialised pointer vs initialised\n");

    /* Wrong: d has no valid target. Dereferencing is undefined. */
    /* struct Device *d; */
    /* d->id = 42;  // Do NOT do this */

    /* Right: point to a real object or keep NULL until assigned. */
    struct Device dev = { 1, "tty0" };
    struct Device *ok = &dev;
    ok->id = 2;
    strcpy(ok->name, "ttyS0");
    printf(" ok->id=%d ok->name=%s\n", ok->id, ok->name);

    struct Device *maybe = NULL;
    if (maybe == NULL) {
        printf(" maybe is NULL, avoiding dereference\n");
    }
}

/* -----------------------------------------------------------
 * Case 2: Dot vs arrow confusion
 * ----------------------------------------------------------- */
static void case_dot_vs_arrow(void) {
    printf("\n[Case 2] Dot vs arrow\n");

    struct Device dev = { 7, "console0" };
    struct Device *p = &dev;

    /* Correct usage */
    printf(" dev.id=%d dev.name=%s\n", dev.id, dev.name);    /* variable: use .  */
    printf(" p->id=%d p->name=%s\n", p->id, p->name);        /* pointer:  use -> */

    /* Uncomment to observe the compiler error for teaching purposes */
    /* printf("%d\n", p.id); */ /* p is a pointer, not a struct */
}

/* -----------------------------------------------------------
 * Case 3: NULL pointer dereference vs guarded access
 * ----------------------------------------------------------- */
static void case_null_deref(void) {
    printf("\n[Case 3] NULL dereference guard\n");

    struct Device *p = NULL;

    /* Wrong: would crash */
    /* printf("%d\n", p->id); */

    /* Right: guard before dereferencing if pointer may be NULL */
    if (p != NULL) {
        printf(" id=%d\n", p->id);
    } else {
        printf(" p is NULL, skipping access\n");
    }
}

/* -----------------------------------------------------------
 * Case 4: Dangling pointer (returning address of a local)
 * and two safe alternatives WITHOUT malloc
 * ----------------------------------------------------------- */

/* Dangerous factory: returns address of a local variable (dangling) */
static struct Device *make_device_bad(void) {
    struct Device d = { 99, "bad-local" };
    return &d; /* The address becomes invalid when the function returns */
}

/* Safe alternative A: initialiser that writes into caller-provided struct */
static void init_device(struct Device *out, int id, const char *name) {
    if (out == NULL) return;
    out->id = id;
    strncpy(out->name, name, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
}

/* Safe alternative B: return-by-value (fine for small, plain structs) */
static struct Device make_device_value(int id, const char *name) {
    struct Device d;
    d.id = id;
    strncpy(d.name, name, sizeof(d.name) - 1);
    d.name[sizeof(d.name) - 1] = '\0';
    return d; /* Returned by value: the caller receives its own copy */
}

static void case_dangling_and_safe_alternatives(void) {
    printf("\n[Case 4] Dangling vs safe init (no malloc)\n");

    /* Wrong: pointer becomes dangling immediately */
    struct Device *bad = make_device_bad();
    (void)bad; /* Do not dereference; it is invalid. */
    printf(" bad points to invalid memory; we will not dereference it\n");

    /* Safe A: caller owns storage; callee fills it via pointer */
    struct Device owned_a;
    init_device(&owned_a, 123, "owned-A");
    printf(" owned_a.id=%d owned_a.name=%s\n", owned_a.id, owned_a.name);

    /* Safe B: small plain struct returned by value */
    struct Device owned_b = make_device_value(124, "owned-B");
    printf(" owned_b.id=%d owned_b.name=%s\n", owned_b.id, owned_b.name);
}

/* -----------------------------------------------------------
 * Case 5: Accidental struct copy vs pointer sharing
 * ----------------------------------------------------------- */
static void case_copy_vs_share(void) {
    printf("\n[Case 5] Copy vs share\n");

    struct Device a = { 1, "tty0" };

    /* Accidental copy: b is a separate struct */
    struct Device b = a;
    strcpy(b.name, "tty1");
    printf(" after copy+edit: a.name=%s, b.name=%s\n", a.name, b.name);

    /* Intentional sharing via pointer */
    struct Device *pa = &a;
    strcpy(pa->name, "tty2");
    printf(" after shared edit: a.name=%s (via pa)\n", a.name);
}

/* Bonus: safe initialisation pattern via out-parameter */
static void case_safe_init_pattern(void) {
    printf("\n[Bonus] Safe initialisation via pointer\n");
    struct Device dev;
    init_device(&dev, 55, "control0");
    printf(" dev.id=%d dev.name=%s\n", dev.id, dev.name);
}

int main(void) {
    case_uninitialised_pointer();
    case_dot_vs_arrow();
    case_null_deref();
    case_dangling_and_safe_alternatives();
    case_copy_vs_share();
    case_safe_init_pattern();
    return 0;
}
```

Build and run:

```sh
% cc -Wall -Wextra -o lab_struct_pointer_pitfalls_nomalloc lab_struct_pointer_pitfalls_nomalloc.c
% ./lab_struct_pointer_pitfalls_nomalloc
```

What to notice:

1. **Uninitialised versus initialised**
    You never dereference an uninitialised pointer. Point it to a real object or keep it as `NULL` until you have one.
2. **Dot versus arrow**
    Use `.` with a struct variable, `->` with a pointer. If you uncomment the `p.id` line, the compiler will flag it.
3. **NULL dereference**
    Always guard against `NULL` when there is any chance the pointer might be unset.
4. **Dangling pointer without malloc**
    Returning the address of a local variable is unsafe because the local goes out of scope. Two safe options that require no heap:
    
    - Let the caller **provide the storage** and pass a pointer to be filled in.
    
    - **Return a small, plain struct by value** when a copy is intended.
    
5. **Copy vs share**
    Copying a struct makes a separate object; editing one does not alter the other. Using a pointer means both names refer to the same object.

#### Why this matters for driver code

Kernel code passes pointers to structs everywhere. The habits you just practiced are foundational: initialise pointers, choose the correct operator, guard against `NULL`, avoid dangling pointers by respecting scope, and be deliberate about copying versus sharing. These patterns keep kernel code safe and predictable, long before you ever need dynamic allocation.

#### Challenge Questions: Pointers to Structs

Try these exercises to make sure you really understand how struct pointers work. Write small C programs for each one and experiment with the output.

1. **Dot vs Arrow**
    Write a program that creates a `struct Point` and a pointer to it. Print the fields twice: once using the dot operator (`.`) and once using the arrow operator (`->`). Explain why one works with the variable and the other with the pointer.

2. **Struct Initialiser Function**
    Write a function `void init_point(struct Point *p, int x, int y)` that fills in a struct given a pointer. Call it from `main` with a local variable and print the result.

3. **Returning by Value vs Returning a Pointer**
    Write two functions:

   - `struct Point make_point_value(int x, int y)` that returns a struct by value.
   - `struct Point *make_point_pointer(int x, int y)` that (wrongly) returns a pointer to a local struct.
      What happens if you use the second function? Why is it dangerous?

4. **Safe NULL Handling**
    Modify the program so that a pointer may be set to `NULL`. Write a function `print_point(const struct Point *p)` that safely prints `"(null)"` if `p` is `NULL` instead of crashing.

5. **Copy vs Share**
    Create two structs: one by copying another (`struct Point b = a;`) and one by sharing via pointer (`struct Point *pb = &a;`). Change the values in each and print both. What differences do you observe?

6. **Mini Linked List**
    Define a simple struct:

   ```c
   struct Node {
       int value;
       struct Node *next;
   };
   ```

   Manually create three nodes and chain them together (`n1 -> n2 -> n3`). Use a pointer to walk through the list and print the values. This mimics what `LIST_FOREACH` does in the kernel.

#### Wrapping Up

Pointers to structs are one of the most important idioms in kernel programming. They allow you to work with complex objects efficiently, without copying large blocks of memory, and they provide the foundation for navigating linked lists and device tables.

You’ve now seen how:

- Structs group related fields into a single object.
- Pointers to structs let you access and modify those fields efficiently.
- The arrow operator (`->`) is the preferred way to reach struct fields through a pointer.
- Real kernel code relies heavily on struct pointers to represent processes, threads, and devices.

With the challenge questions, you can now test yourself and confirm you really understand how struct pointers behave in C.

The natural next step is to see what happens when we combine these ideas with **arrays**. Arrays of pointers and pointer arrays are a powerful duo that appear everywhere in kernel code, from device tables to argument lists.

Let’s continue and learn about the next topic:  **Arrays of Pointers and Pointer Arrays**.

### Arrays of Pointers and Pointer Arrays

This is one of those topics that many C beginners find tricky: the difference between an **array of pointers** and a **pointer to an array**. The declarations look confusingly similar, but they describe very different things. The difference is not just academic. Arrays of pointers are everywhere in FreeBSD code, while pointers to arrays are less common but still important to understand because they appear in contexts where hardware requires contiguous memory blocks.

We will first look at arrays of pointers, then at pointers to arrays, and then connect them to real FreeBSD examples and driver development.

#### Array of Pointers

An array of pointers is simply an array where each element is itself a pointer. Instead of holding values directly, the array holds addresses pointing to values stored elsewhere.

##### Example: Array of Strings

```c
#include <stdio.h>

int main() {
    // Array of 3 pointers to const char (i.e., strings)
    const char *messages[3] = {
        "Welcome",
        "to",
        "FreeBSD"
    };

    for (int i = 0; i < 3; i++) {
        printf("Message %d: %s\n", i, messages[i]);
    }

    return 0;
}
```

Here, `messages` is an array of three pointers. Each element, such as `messages[0]`, holds the address of a string literal. When passed to `printf`, it prints the string.

This is exactly the same structure as the `argv` parameter to `main()`: it is just an array of pointers to characters.

##### Real FreeBSD Example: Locale Names

Looking into FreeBSD 14.3 source code, more specifically in `bin/sh/var.c`, around line 130:

```c
static const char *const locale_names[7] = {
    "LANG", "LC_ALL", "LC_COLLATE", "LC_CTYPE",
    "LC_MESSAGES", "LC_MONETARY", "LC_NUMERIC",
};
```

This is an **array of constant character pointers**. Each element points to a string literal with the name of a locale category. The shell uses this array to check or set environment variables consistently. This is a compact, idiomatic way to store a table of names.

##### Real FreeBSD Example: SNMP Transport Names

Looking into FreeBSD 14.3 source code, more specifically, in `contrib/bsnmp/lib/snmpclient.c`, around line 1887:

```c
static const char *const trans_list[] = {
    "udp", "tcp", "local", NULL
};
```

This is another array of string pointers, terminated by `NULL`. The SNMP client library uses this list to recognise valid transport names. The use of `NULL` as a terminator is a very common C idiom.

**Note**: Arrays of pointers are common in FreeBSD because they allow flexible, dynamic lookup tables without copying large amounts of data. Instead of storing the strings inline, the array stores pointers to them.

#### Pointer to an Array

A pointer to an array is very different from an array of pointers. Instead of pointing to scattered objects, it points to one single, contiguous array block. The syntax can look intimidating, but the underlying idea is simple: the pointer represents the entire array as a unit.

##### Example: Pointer to an Array of Integers

```c
#include <stdio.h>

int main() {
    int numbers[5] = {1, 2, 3, 4, 5};

    // Pointer to an array of 5 integers
    int (*p)[5] = &numbers;

    printf("Third number: %d\n", (*p)[2]);

    return 0;
}
```

Breaking it down:

- `int (*p)[5];` declares `p` as a pointer to an array of 5 integers.
- `p = &numbers;` makes `p` point to the whole `numbers` array.
- `(*p)[2]` first dereferences the pointer (giving us the array) and then indexes into it.

##### Another Example with Structures

```c
#include <stdio.h>

#define SIZE 4

struct Point {
    int x, y;
};

int main(void) {
    struct Point pts[SIZE] = {
        {0, 0}, {1, 2}, {2, 4}, {3, 6}
    };

    struct Point (*parray)[SIZE] = &pts;

    printf("Third element: x=%d, y=%d\n",
           (*parray)[2].x, (*parray)[2].y);

    // Modify via the pointer
    (*parray)[2].x = 42;
    (*parray)[2].y = 84;

    printf("After modification: x=%d, y=%d\n",
           pts[2].x, pts[2].y);

    return 0;
}
```

Here, `parray` points to the entire array of four `struct Point`. Accessing through it is equivalent to accessing the original array directly, but it emphasises that the pointer represents the array as a single unit.

You have now seen two different examples of pointers to arrays. Both show how a single pointer can name a whole, contiguous region of memory. The natural question is how often this appears in real FreeBSD code.

##### Why You Rarely See This in FreeBSD

Literal `T (*p)[N]` declarations are uncommon in the kernel source. FreeBSD developers usually represent fixed-size blocks in one of two ways:

- Wrap the array inside a `struct`, which keeps size and type information together and leaves room for metadata.
- Pass a base pointer along with an explicit length, especially for buffers and I/O regions.

This style makes the code more transparent, easier to maintain, and integrates better with kernel subsystems. The random subsystem is a good example, where structures carry fixed arrays that are treated as single units by the code paths that process them. For background on how drivers and subsystems feed entropy into the kernel, see the `random_harvest(9)` manual page. 

##### Real FreeBSD Example: Struct with a Fixed-Size Array

FreeBSD 14.3 source, `sys/dev/random/random_harvestq.h`, lines 33-44:

```c
#define HARVESTSIZE     2       /* Max length in words of each harvested entropy unit */

/* These are used to queue harvested packets of entropy. The entropy
 * buffer size is pretty arbitrary.
 */
struct harvest_event {
        uint32_t        he_somecounter;         /* fast counter for clock jitter */
        uint32_t        he_entropy[HARVESTSIZE];/* some harvested entropy */
        uint8_t         he_size;                /* harvested entropy byte count */
        uint8_t         he_destination;         /* destination pool of this entropy */
        uint8_t         he_source;              /* origin of the entropy */
};
```

This is not a raw `T (*p)[N]` declaration, yet it captures the same idea in a form that is clearer and more practical for the kernel. The `struct` groups a fixed array `he_entropy[HARVESTSIZE]` with related fields. Code then passes a pointer to `struct harvest_event`, treating the entire block as one object. In `random_harvestq.c` you can see how an instance is filled and processed, including copying into `he_entropy` and setting the size and metadata fields, which reinforces that the array is handled as part of a single unit.

Even though raw pointers to arrays are rare in the tree, understanding them helps you recognise why kernel code tends to wrap arrays in structures or pair a base pointer with an explicit length. Conceptually, it is the same pattern of referring to a contiguous block as a whole.

#### Mini Hands-On Lab

Let’s test your understanding. For each declaration, decide whether it is an **array of pointers** or a **pointer to an array**. Then explain how you would access the third element.

1. `const char *names[] = { "a", "b", "c", NULL };`
2. `int (*ring)[64];`
3. `struct foo *ops[8];`
4. `char (*line)[80];`

**Check yourself:**

1. Array of pointers. Third element with `names[2]`.
2. Pointer to an array of 64 ints. Use `(*ring)[2]`.
3. Array of pointers to `struct foo`. Use `ops[2]`.
4. Pointer to an array of 80 chars. Use `(*line)[2]`.

#### Challenge Questions

1. Why is `argv` in `main(int argc, char *argv[])` considered an array of pointers rather than a pointer to an array?
2. In kernel code, why do developers prefer to use a `struct` wrapping a fixed-size array instead of a raw pointer-to-array declaration?
3. How does using `NULL` as a terminator in arrays of pointers simplify iteration?
4. Imagine a driver that manages a ring of DMA descriptors. Would you expect this to be represented as an array of pointers or a pointer to an array? Why?
5. What could go wrong if you mistakenly treated a pointer to an array as if it were an array of pointers?

#### Why This Matters for Kernel and Driver Development

In FreeBSD device drivers, **arrays of pointers** appear constantly. They are used for option lists, function pointer tables, protocol name arrays, and sysctl handlers. This idiom saves space and allows code to iterate flexibly through lists without knowing their exact size in advance.

**Pointers to arrays**, while rarer, are conceptually important because they match the way hardware often works. A NIC, for example, may expect a contiguous ring buffer of descriptors. In practice FreeBSD developers usually hide the raw pointer-to-array inside a `struct` that describes the ring, but the underlying idea is identical: the driver is passing around "a single block of fixed-size elements."

Understanding both patterns is part of thinking like a systems programmer. It ensures you will not confuse two declarations that look similar but behave differently, which prevents subtle and painful bugs.

#### Wrapping Up

By now you can clearly see the difference between an array of pointers and a pointer to an array. You have also seen why this distinction matters when reading or writing real code. Arrays of pointers give flexibility by letting each element point to different objects, while a pointer to an array treats a whole block of memory as a single unit.

With this foundation in place, we are ready to take the next step: moving from fixed arrays to dynamically allocated memory. In the following section on **Dynamic Memory Allocation**, you will learn how to use functions such as `malloc`, `calloc`, `realloc`, and `free` to create arrays at runtime. We will connect this to pointers by showing how to allocate each element separately, how to request one contiguous block when you need a pointer to an array, and how to clean up properly if something goes wrong. This transition from static to dynamic memory is essential for real systems programming and will prepare you for the way memory is managed inside the FreeBSD kernel.

## 4.11 Dynamic Memory Allocation

So far, most of the memory we used in examples was **fixed-size**: arrays with a known length or structures allocated on the stack. But when writing system-level code like FreeBSD device drivers, you often don't know in advance how much memory you'll need. Maybe a device reports the number of buffers only after probing, or the amount of data depends on user input. That's when **dynamic memory allocation** comes into play.

Dynamic allocation allows your code to **ask the system for memory while it's running**, and to give it back when it's no longer needed. This flexibility is essential for drivers, where hardware and workload conditions can change at runtime.

### User Space vs Kernel Space

In user space, you've probably seen functions like:

- `malloc(size)` - allocates a block of memory.
- `calloc(count, size)` - allocates and zeroes a block.
- `free(ptr)` - releases previously allocated memory.

Example:

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int *nums;
    int count = 5;

    nums = malloc(count * sizeof(int));
    if (nums == NULL) {
        printf("Memory allocation failed!\n");
        return 1;
    }

    for (int i = 0; i < count; i++) {
        nums[i] = i * 10;
        printf("nums[%d] = %d\n", i, nums[i]);
    }

    free(nums); // Always release what you allocated
    return 0;
}
```

In user space, memory comes from the **heap**, managed by the C runtime and the operating system.

But inside the **FreeBSD kernel**, we cannot use `<stdlib.h>`'s `malloc()` or `free()`. The kernel has its own allocator, designed with stricter rules and better tracking. The kernel API is documented in `malloc(9)`.

### Visualising Memory in User Space

```
+-----------------------------------------------------------+
|                        STACK (grows down)                 |
|   - Local variables                                       |
|   - Function call frames                                  |
|   - Fixed-size arrays                                     |
+-----------------------------------------------------------+
|                           ^                                |
|                           |                                |
|                        HEAP (grows up)                     |
|   - malloc()/calloc()/free()                               |
|   - Dynamic structures and arrays                          |
+-----------------------------------------------------------+
|                     DATA SEGMENT                          |
|   - Globals                                               |
|   - static variables                                      |
|   - .data (initialized) / .bss (zero-initialized)         |
+-----------------------------------------------------------+
|                     CODE / TEXT SEGMENT                   |
|   - Program instructions                                  |
+-----------------------------------------------------------+
```

**Note:** Stack and heap grow toward each other at runtime. Fixed-size data lives on the stack or in the data segment, while dynamic allocations come from the heap.

### Kernel-Space Allocation with malloc(9)

To allocate memory in the FreeBSD kernel you use:

```c
#include <sys/malloc.h>

void *malloc(size_t size, struct malloc_type *type, int flags);
void free(void *addr, struct malloc_type *type);
```

Example from the kernel:

```c
char *buf = malloc(1024, M_TEMP, M_WAITOK | M_ZERO);
/* ... use buf ... */
free(buf, M_TEMP);
```

**Breaking it down:**

- `1024` → the number of bytes.
- `M_TEMP` → memory type tag (explained below).
- `M_WAITOK` → wait if memory is temporarily unavailable.
- `M_ZERO` → ensure the block is zeroed.
- `free(buf, M_TEMP)` → release the memory.

### Kernel malloc(9) Workflow

```
┌──────────────────────────┐
│ Driver code              │
│                          │
│ ptr = malloc(size,       │
│               TYPE,      │
│               FLAGS);    │
└─────────────┬────────────┘
              │ request
              v
┌──────────────────────────┐
│ Kernel allocator         │
│  - Typed pools (TYPE)    │
│  - Honors FLAGS:         │
│      M_WAITOK / NOWAIT   │
│      M_ZERO              │
│  - Accounting & tracing  │
└─────────────┬────────────┘
              │ returns pointer
              v
┌──────────────────────────┐
│ Driver uses buffer       │
│  - Fill/IO/queues/etc.   │
│  - Lifetime under driver │
│    responsibility        │
└─────────────┬────────────┘
              │ later
              v
┌──────────────────────────┐
│ free(ptr, TYPE);         │
│  - Returns memory to     │
│    kernel pool           │
│  - TYPE must match       │
└──────────────────────────┘
```

**Note:** In the kernel, every allocation is **typed** and controlled by flags. Pair every `malloc(9)` with a `free(9)` on all code paths, including errors.

### Memory Types and Flags

One unique aspect of FreeBSD's kernel allocator is the **type system**: every allocation must be tagged. This makes debugging and leak tracking easier.

Some common types:

- `M_TEMP` - temporary allocations.
- `M_DEVBUF` - buffers for device drivers.
- `M_TTY` - terminal subsystem memory.

Common flags:

- `M_WAITOK` - sleep until memory is available.
- `M_NOWAIT` - return immediately if memory cannot be allocated.
- `M_ZERO` - zero out memory before returning.

This explicit style encourages safe, predictable memory usage in critical kernel code.

### Hands-On Lab 1: Allocating and Freeing a Buffer

In this exercise, we'll create a simple kernel module that allocates memory when loaded and frees it when unloaded.

**my_malloc_module.c**

```c
#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>

static char *buffer;
#define BUFFER_SIZE 128

MALLOC_DEFINE(M_MYBUF, "my_malloc_buffer", "Buffer for malloc module");

static int
load_handler(module_t mod, int event, void *arg)
{
    switch (event) {
    case MOD_LOAD:
        buffer = malloc(BUFFER_SIZE, M_MYBUF, M_WAITOK | M_ZERO);
        if (buffer == NULL)
            return (ENOMEM);

        snprintf(buffer, BUFFER_SIZE, "Hello from kernel space!\n");
        printf("my_malloc_module: %s", buffer);
        return (0);

    case MOD_UNLOAD:
        if (buffer != NULL) {
            free(buffer, M_MYBUF);
            printf("my_malloc_module: Memory freed\n");
        }
        return (0);
    default:
        return (EOPNOTSUPP);
    }
}

static moduledata_t my_malloc_mod = {
    "my_malloc_module", load_handler, NULL
};

DECLARE_MODULE(my_malloc_module, my_malloc_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
```

**What to do:**

1. Build with `make`.
2. Load with `kldload ./my_malloc_module.ko`.
3. Check `dmesg` for the message.
4. Unload with `kldunload my_malloc_module`.

You'll see how memory is reserved and later freed.

When you load and unload the module, you will see the allocation and freeing in action.

### Hands-On Lab 2: Allocating an Array of Structures

Now let's extend the idea by creating an array of structs dynamically.

```c
struct my_entry {
    int id;
    char name[32];
};

MALLOC_DEFINE(M_MYSTRUCT, "my_struct_array", "Array of my_entry");

static struct my_entry *entries;
#define ENTRY_COUNT 5
```

On load:

- Allocate memory for five entries.
- Initialize each one with an ID and name.
- Print them out.

On unload:

- Free the memory.

This exercise mirrors what real drivers do when they keep track of device states, DMA buffers, or I/O queues.

### Real FreeBSD Example: Building the corefile path in `coredump_vnode.c`

Let's look at a real example from FreeBSD's source code: `sys/kern/coredump_vnode.c`. This function builds the path for a process core dump. It allocates a temporary buffer, uses it to assemble the path string, and later frees it. I've added additional comments to the example code below to make it easier for you to understand what happens at each step:

```c
/*
 * corefile_open(...) builds the final core file path into a temporary
 * kernel buffer named `name`. The buffer must be large enough to hold
 * a path, so MAXPATHLEN is used. The buffer is freed by the caller
 * (see the coredump_vnode() snippet further below).
 */
static int
corefile_open(const char *comm, uid_t uid, pid_t pid, struct thread *td,
    int compress, int signum, struct vnode **vpp, char **namep)
{
    struct sbuf sb;
    const char *format;
    char *hostname, *name;
    int indexpos, indexlen, ncores;

    hostname = NULL;
    format = corefilename;

    /* 
     * Allocate a zeroed path buffer from the kernel allocator.
     * - size: MAXPATHLEN bytes (fits a full path)
     * - M_TEMP: a temporary allocation type tag (helps tracking/debug)
     * - M_WAITOK: ok to sleep if memory is briefly unavailable
     * - M_ZERO: return the buffer zeroed (no stale data)
     */
    name = malloc(MAXPATHLEN, M_TEMP, M_WAITOK | M_ZERO);  /* <-- allocate */
    indexlen = 0;
    indexpos = -1;
    ncores = num_cores;

    /*
     * Initialize an sbuf that writes directly into `name`.
     * SBUF_FIXEDLEN means: do not auto-grow, error if too long.
     */
    (void)sbuf_new(&sb, name, MAXPATHLEN, SBUF_FIXEDLEN);

    /*
     * The format string (kern.corefile) may include tokens like %N, %P, %U.
     * Iterate, expand tokens, and append to `sb`. If %H (hostname) appears,
     * allocate a second small buffer for it, then free it immediately after.
     */
    /* ... formatting loop omitted for brevity ... */

    /* hostname was conditionally allocated above; free it now if used */
    free(hostname, M_TEMP);                                 /* <-- free small temp */

    /*
     * If compression is requested, append a suffix like ".gz" or ".zst".
     * If the sbuf overflowed, clean up and return ENOMEM.
     */
    if (sbuf_error(&sb) != 0) {
        sbuf_delete(&sb);           /* dispose sbuf wrapper (no malloc here) */
        free(name, M_TEMP);         /* <-- free on error path */
        return (ENOMEM);
    }
    sbuf_finish(&sb);
    sbuf_delete(&sb);

    /*
     * On success, return `name` to the caller via namep.
     * Ownership of `name` transfers to the caller, who must free it.
     */
    *namep = name;
    return (0);
}
```

What to notice here:

- The buffer is **typed** with `M_TEMP` to help kernel memory accounting and leak detection. This is a FreeBSD kernel convention that you'll reuse for your own drivers by defining your own `MALLOC_DEFINE` tag. 
- `M_WAITOK` is chosen because this path can safely sleep; the kernel allocator will wait rather than fail spuriously. If you are in a context where sleeping is unsafe, you must use `M_NOWAIT` and handle allocation failure immediately. 
- Error paths **free what they allocate** before returning. This is the habit to ingrain early: every `malloc(9)` must have a clear and reliable `free(9)` in all paths. 

Now let's see where the caller cleans up:

```c
/*
 * coredump_vnode(...) calls corefile_open() to obtain both the vnode (vp)
 * and the dynamically built `name` path. After it finishes writing the core
 * or handling errors, it must free `name`. This snippet shows both another
 * temporary allocation (for cwd when needed) and the final free(name).
 */
static int
coredump_vnode(struct thread *td, off_t limit)
{
    struct vnode *vp;
    char *name;         /* corefile path returned by corefile_open() */
    char *fullpath, *freepath = NULL;
    size_t fullpathsize;
    struct sbuf *sb;
    int error, error1;

    /* Build name + open/create target vnode */
    error = corefile_open(p->p_comm, cred->cr_uid, p->p_pid, td,
        compress_user_cores, sig, &vp, &name);
    if (error != 0)
        return (error);

    /* ... write/extend coredump, lock ranges, set attributes, etc ... */

    /*
     * When emitting a devctl notification, if the core path is relative,
     * allocate a small temporary buffer to fetch the current working dir,
     * then free it once we've appended it to the sbuf.
     */
    if (name[0] != '/') {
        fullpathsize = MAXPATHLEN;
        freepath = malloc(fullpathsize, M_TEMP, M_WAITOK);   /* <-- allocate temp */
        if (vn_getcwd(freepath, &fullpath, &fullpathsize) != 0) {
            free(freepath, M_TEMP);                          /* <-- free on error */
            /* ... fall through to cleanup below ... */
        }
        /* use fullpath ... */
        free(freepath, M_TEMP);                              /* <-- free on success */
        /* ... continue building notification ... */
    }

out:
    /*
     * Close the vnode we opened and then free the dynamically built `name`.
     * This pairs with the malloc(MAXPATHLEN, ...) in corefile_open().
     */
    error1 = vn_close(vp, FWRITE, cred, td);
    if (error == 0)
        error = error1;
    free(name, M_TEMP);                                      /* <-- final free */
    return (error);
}
```

This example highlights several important practices that apply directly to device driver development. Temporary kernel strings and small work buffers are often created with `malloc(9)` and must always be released, whether the code succeeds or fails, as shown in the careful cleanup logic of `coredump_vnode()`. T

he choice between `M_WAITOK` and `M_NOWAIT` also depends on context: in code paths where the kernel can safely sleep, `M_WAITOK` ensures the allocation will eventually succeed, while in contexts such as interrupt handlers, where sleeping is forbidden, `M_NOWAIT` must be used and a `NULL` pointer handled immediately. Finally, keeping allocations local and freeing them as soon as their last use is complete reduces the risk of memory leaks and use-after-free errors. 

The handling of the short-lived `freepath` buffer is a clear demonstration of this principle in practice.

### Why This Matters in FreeBSD Device Drivers

In real-world drivers, memory needs are rarely predictable. A network card might advertise the number of receive descriptors only after you probe it. A storage controller could require buffers sized according to device-specific registers. Some devices maintain tables that grow or shrink depending on the workload, such as pending I/O requests or active sessions. All of these cases require **dynamic memory allocation**.

Static arrays cannot cover such situations because they are fixed at compile time, wasting memory if oversized or failing outright if undersized. With `malloc(9)` and `free(9)`, a driver can adapt to the actual hardware and workload, allocating exactly what is needed and returning memory once it is no longer in use.

However, this flexibility comes with responsibility. Unlike in user space, memory management errors in the kernel can destabilise the entire system. A missed `free()` becomes a memory leak that weakens long-term stability. An invalid pointer access after freeing can crash the kernel instantly. Overruns and underruns can silently corrupt memory structures used by other subsystems, sometimes turning into security vulnerabilities.

This is why learning to allocate, use, and release memory correctly is one of the foundational skills for FreeBSD driver developers. Getting this right ensures that your driver not only works under normal conditions but also behaves safely under stress, making the system reliable as a whole.

#### Real Driver Scenarios

Here are some practical cases where dynamic memory allocation is essential in FreeBSD device drivers:

- **Network drivers:** Allocate rings of packet descriptors whose size depends on the NIC's capabilities.
- **USB drivers:** Create transfer buffers sized to the maximum packet length reported by the device.
- **PCI storage controllers:** Build command tables that expand with the number of active requests.
- **Character devices:** Manage per-open data structures that exist only while a user process holds the device open.

These examples show that dynamic allocation is not just an academic exercise: it is a daily requirement for making real drivers interact safely and efficiently with hardware.

### Common Beginner Pitfalls

Dynamic allocation in kernel code introduces some traps that are easy to overlook:

**1. Leaking memory on error paths**
 It's not enough to free memory in the "happy path." If an error occurs after you've allocated but before the function exits, forgetting to free will leak memory inside the kernel.
 *Tip:* Always trace every exit path and make sure each allocated block is either used or freed. Using a single cleanup label at the end of your function is a common pattern in FreeBSD.

**2. Freeing with the wrong type**
 Every `malloc(9)` call is tagged with a type. Freeing with a mismatched type may confuse the kernel's memory accounting and debugging tools.
 *Tip:* Define a custom tag for your driver with `MALLOC_DEFINE()` and always free with that same tag.

**3. Assuming allocation always succeeds**
 In user space, `malloc()` often succeeds unless the system is badly constrained. In the kernel, especially with `M_NOWAIT`, allocation can legitimately fail.
 *Tip:* Always check for `NULL` and handle the failure gracefully.

**4. Choosing the wrong allocation flag**
 Using `M_WAITOK` in contexts that cannot sleep (like interrupt handlers) can deadlock the kernel. Using `M_NOWAIT` when sleeping is safe may force needless failure handling.
 *Tip:* Understand the context of your allocation and pick the correct flag.

### Challenge Questions

1. In a driver's `detach()` routine, what can happen if you forget to free dynamically allocated buffers?
2. Why is it important that the type passed to `free(9)` matches the one used in `malloc(9)`?
3. Imagine you allocate memory with `M_NOWAIT` during an interrupt. The call returns `NULL`. What should your driver do next?
4. Why is checking every error path after a successful allocation just as important as freeing on the success path?
5. If you use `M_WAITOK` inside an interrupt filter, what dangerous condition might arise?

### Wrapping Up

You have now seen how C's dynamic memory allocation works in user space and how FreeBSD extends this idea with its own `malloc(9)` and `free(9)` for the kernel. You learned why allocations must always be paired with cleanups, how memory types and flags guide safe allocation, and how real FreeBSD code uses these patterns every day.

Dynamic allocation gives your driver the flexibility to adapt to hardware and workload demands, but it also introduces new responsibilities. Handling every error path, choosing the right flags, and keeping allocations short-lived are the habits that separate safe kernel code from fragile code.

In the next section, we will build directly on this foundation by looking at **memory safety in kernel code**. There, you will learn techniques to protect against leaks, overflows, and use-after-free errors, making your driver not only functional but also reliable and secure.

## 4.12 Memory Safety in Kernel Code

When writing kernel code, especially device drivers, we are working in a privileged and unforgiving environment. There is no safety net. In user-space programming, a crash usually terminates only your process. In kernel-space, a single bug can panic or reboot the entire operating system. That is why memory safety is not optional. It is the foundation of stable and secure FreeBSD driver development.

You must constantly remember that the kernel is persistent and long-running. A memory leak will accumulate for as long as the system remains up. A buffer overflow can silently overwrite unrelated data structures and later trigger a mysterious crash. Using an uninitialized pointer can panic the system instantly.

This section introduces the most common mistakes, shows you how to avoid them, and gives you practice through real experiments, both in user space and inside a small kernel module.

### What Can Go Wrong?

Most kernel bugs caused by beginners can be traced to unsafe memory handling. Let's list the most frequent and dangerous ones:

- **Using uninitialized pointers**: a pointer that is not set to a valid address contains garbage. Dereferencing it usually causes a panic.
- **Accessing freed memory (use-after-free)**: once memory is released, it must never be touched again. Doing so corrupts memory and destabilises the kernel.
- **Memory leaks**: failing to call `free()` after `malloc()` means the memory remains reserved forever, slowly consuming kernel resources.
- **Buffer overflows**: writing beyond the end of a buffer overwrites unrelated memory. This can corrupt kernel state or introduce security vulnerabilities.
- **Off-by-one array errors**: accessing one index past the end of an array is enough to destroy adjacent kernel data.

Unlike user space, where tools like `valgrind` can sometimes save you, in kernel programming these errors can lead to instant crashes or subtle corruption that is very difficult to debug.

### Best Practices for Safer Kernel Code

FreeBSD provides mechanisms and conventions to help developers write robust code. Follow these guidelines:

1. **Always initialise pointers.**
    If you do not yet have a valid memory address, set the pointer to `NULL`. This makes accidental dereferences easier to detect.

   ```c
   struct my_entry *ptr = NULL;
   ```

2. **Check the result of `malloc()`.**
    Memory allocation may fail. Never assume success.

   ```c
   ptr = malloc(sizeof(*ptr), M_MYTAG, M_NOWAIT);
   if (ptr == NULL) {
       // Handle gracefully, avoid panic
   }
   ```

3. **Free what you allocate.**
    Every `malloc()` must have a matching `free()`. In kernel space, leaks accumulate until reboot.

   ```c
   free(ptr, M_MYTAG);
   ```

4. **Avoid buffer overflows.**
    Use safer functions such as `strlcpy()` or `snprintf()`, which take the buffer size as an argument.

   ```c
   strlcpy(buffer, "FreeBSD", sizeof(buffer));
   ```

5. **Use `M_ZERO` to avoid garbage values.**
    This flag ensures allocated memory starts clean.

   ```c
   ptr = malloc(sizeof(*ptr), M_MYTAG, M_WAITOK | M_ZERO);
   ```

6. **Use proper allocation flags.**

   - `M_WAITOK` is used when allocation can safely sleep until memory becomes available.
   - `M_NOWAIT` must be used in interrupt handlers or any context where sleeping is forbidden.

### A Real Example from FreeBSD 14.3

In the FreeBSD source tree, memory is often managed through pre-allocated buffers rather than frequent dynamic allocations. Here is a snippet from `sys/kern/tty_info.c`, it's located at line 303:

```c
(void)sbuf_new(&sb, tp->t_prbuf, tp->t_prbufsz, SBUF_FIXEDLEN);
sbuf_set_drain(&sb, sbuf_tty_drain, tp);
```

What happens here?

- `sbuf_new()` creates a string buffer (`sb`) using an already allocated memory region (`tp->t_prbuf`).
- The size is fixed (`tp->t_prbufsz`) and protected by the `SBUF_FIXEDLEN` flag, ensuring no writes beyond the limit.
- `sbuf_set_drain()` then specifies a controlled function (`sbuf_tty_drain`) to handle buffer output.

This pattern demonstrates a safe kernel strategy: memory is allocated once during subsystem initialisation and carefully reused, rather than repeatedly allocated and freed. It reduces fragmentation, avoids allocation failures at runtime, and keeps memory usage predictable.

### Dangerous Code to Avoid

The following snippet is **wrong** because it uses a pointer that was never given a valid address:

```c
struct my_entry *ptr;   // Declared, but not initialised. 'ptr' contains garbage.

ptr->id = 5;            // Crash risk: dereferencing an uninitialised pointer
```

`ptr` does not point anywhere valid. When you try to access `ptr->id`, the kernel will likely panic because you are touching memory you do not own. In user space, this would usually be a segmentation fault. In kernel space, it can crash the whole system.

### The Correct Pattern

Below is a safe version that allocates memory, checks that the allocation worked, uses the memory, and then releases it. The comments explain each step and why it matters in kernel code.

```c
#include <sys/param.h>
#include <sys/malloc.h>

/*
 * Give your allocations a custom tag. This helps the kernel track who owns
 * the memory and makes debugging leaks much easier.
 */
MALLOC_DECLARE(M_MYTAG);                 // Declare the tag (usually in a header)
MALLOC_DEFINE(M_MYTAG, "mydriver", "My driver allocations"); // Define the tag

struct my_entry {
    int id;
};

void example(void)
{
    struct my_entry *ptr = NULL;  // Start with NULL to avoid using garbage

    /*
     * Allocate enough space for ONE struct my_entry.
     * We use sizeof(*ptr) so if the type of 'ptr' changes,
     * the size stays correct automatically.
     *
     * Flags:
     *  - M_WAITOK: allocation is allowed to sleep until memory is available.
     *              Use this only in contexts where sleeping is safe
     *              (for example, during driver attach or module load).
     *
     *  - M_ZERO:   zero-fill the memory so all fields start in a known state.
     *              This prevents accidental use of uninitialised data.
     */
    ptr = malloc(sizeof(*ptr), M_MYTAG, M_WAITOK | M_ZERO);

    if (ptr == NULL) {
        /*
         * Always check for failure, even with M_WAITOK.
         * If allocation fails, handle it gracefully: log, unwind, or return.
         */
        printf("mydriver: allocation failed\n");
        return;
    }

    /*
     * At this point 'ptr' is valid and zero-initialised.
     * It is now safe to access its fields.
     */
    ptr->id = 5;

    /*
     * ... use 'ptr' for whatever work is needed ...
     */

    /*
     * When you are done, free the memory with the SAME tag you used to allocate.
     * Pairing malloc/free is essential in kernel code to avoid leaks
     * that accumulate for the entire uptime of the machine.
     */
    free(ptr, M_MYTAG);
    ptr = NULL;  // Optional but helpful to prevent accidental reuse
}
```

### Why this pattern matters

1. **Initialise pointers**: starting with `NULL` makes accidental use obvious during reviews and easier to catch in tests.
2. **Size safely**: `sizeof(*ptr)` follows the pointer's type automatically, reducing the chance of wrong sizes when refactoring.
3. **Pick the right flags**:
   - Use `M_WAITOK` when the code can sleep, such as during attach, open, or module load paths.
   - Use `M_NOWAIT` in interrupt handlers or other non-sleepable contexts, and handle `NULL` immediately.
4. **Zero on allocate**: `M_ZERO` prevents hidden state from previous allocations, which avoids surprising behaviour.
5. **Always free**: every `malloc()` must be paired with `free()` using the same tag. This is non-negotiable in kernel code.
6. **Set to NULL after free**: it reduces the risk of use-after-free bugs if the pointer is referenced later by mistake.

### If your context do not allow sleep

Sometimes you are in a context where sleeping is forbidden, such as an interrupt handler. In that case use `M_NOWAIT`, check immediately for failure, and defer work if needed:

```c
ptr = malloc(sizeof(*ptr), M_MYTAG, M_NOWAIT | M_ZERO);
if (ptr == NULL) {
    /* Defer the work or drop it safely; do NOT block here. */
    return;
}
```

Keeping these habits from the beginning will save you from many of the most painful kernel crashes and midnight debugging sessions.

### Hands-On Lab 1: Crashing with an Uninitialized Pointer

This exercise demonstrates why you must never use a pointer before giving it a valid address. We will first write a broken program that uses an uninitialised pointer, then fix it with `malloc()`.

#### Broken Version: `lab1_crash.c`

```c
#include <stdio.h>

struct data {
    int value;
};

int main(void) {
    struct data *ptr;  // Declared but not initialised

    // At this point, 'ptr' points to some random location in memory.
    // Trying to use it will cause undefined behaviour.
    ptr->value = 42;   // Segmentation fault very likely here

    printf("Value: %d\n", ptr->value);

    return 0;
}
```

#### What to Expect

- This program compiles without warnings.
- When you run it, it will almost certainly crash with a segmentation fault.
- The crash happens because `ptr` does not point to valid memory, yet we try to write to `ptr->value`.
- In the kernel, this same mistake would likely panic the entire system.

#### What is wrong here?

```
STACK (main)
+---------------------------+
| ptr : ??? (uninitialised) |  -> not a real address you own
+---------------------------+

HEAP
+---------------------------+
|     no allocation yet     |
+---------------------------+

Action performed:
  write 42 into ptr->value

Result:
  You are dereferencing a garbage address. Crash.
```

#### Fixed Version: `lab1_fixed.c`

```c
#include <stdio.h>
#include <stdlib.h>  // For malloc() and free()

struct data {
    int value;
};

int main(void) {
    struct data *ptr;

    // Allocate memory for ONE struct data on the heap
    ptr = malloc(sizeof(struct data));
    if (ptr == NULL) {
        // Always check in case malloc fails
        printf("Allocation failed!\n");
        return 1;
    }

    // Now 'ptr' points to valid memory, safe to use
    ptr->value = 42;
    printf("Value: %d\n", ptr->value);

    // Always free what you allocate
    free(ptr);

    return 0;
}
```

#### What Changed

- We used `malloc()` to allocate enough space for one `struct data`.
- We checked that the result was not `NULL`.
- We safely wrote into the struct's field.
- We freed the memory before exiting, preventing a leak.

#### Why this works

```
STACK (main)
+---------------------------+
| ptr : 0xHHEE...          |  -> valid heap address returned by malloc
+---------------------------+

HEAP
+---------------------------+
| struct data block         |
|   value = 42              |
+---------------------------+

Actions:
  1) ptr = malloc(sizeof(struct data))  -> ptr now points to a valid block
  2) ptr->value = 42                    -> write inside your own block
  3) free(ptr)                          -> return memory to the system

Result:
  No crash. No leak.
```

### Hands-On Lab 2: Memory Leak and the Forgotten Free

This exercise shows what happens when you forget to release allocated memory. In user space, leaks disappear when your program exits. In the kernel, leaks accumulate for the system's entire uptime, which is why this habit must be fixed early.

#### Leaky Version: `lab2_leak.c`

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    // Allocate 128 bytes of memory
    char *buffer = malloc(128);
    if (buffer == NULL) return 1;

    // Copy a string into the allocated buffer
    strcpy(buffer, "FreeBSD device drivers are awesome!");
    printf("%s\n", buffer);

    // Memory was allocated but never freed
    // This is a memory leak
    return 0;
}
```

#### What to Expect

- The program prints the string normally.
- You may not notice the problem right away because the OS reclaims process memory when the program exits.
- In the kernel this would be serious. The memory would remain allocated across operations and only a reboot clears it.

#### Leak vs program exit

```
Before exit:

STACK (main)
+---------------------------+
| buffer : 0xABCD...       |  -> heap address
+---------------------------+

HEAP
+--------------------------------------------------+
| 128-byte block                                   |
| "FreeBSD device drivers are awesome!\0 ..."      |
+--------------------------------------------------+

Action:
  Program returns without free(buffer)

Consequence in user space:
  OS reclaims process memory at exit, so you do not notice.

Consequence in kernel space:
  The block remains allocated across operations and accumulates.
```

#### Fixed Version: `lab2_fixed.c`

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char *buffer = malloc(128);
    if (buffer == NULL) return 1;

    strcpy(buffer, "FreeBSD device drivers are awesome!");
    printf("%s\n", buffer);

    // Free the memory once we are done
    free(buffer);

    return 0;
}
```

#### What Changed

- We added `free(buffer);`.
- This single line ensures that all memory is returned to the system. Make this a habit.

#### Proper lifecycle

```
1) Allocation
   HEAP: [ 128-byte block ]  <- buffer points here

2) Use
   Write string into the block, then print it

3) Free
   free(buffer)
   HEAP: [ block returned to allocator ]
   buffer (optional) -> set to NULL to avoid accidental reuse
```

### Detecting Memory Leaks with AddressSanitizer

On FreeBSD, when compiling user-space programs with Clang, you can detect leaks automatically using AddressSanitizer:

```sh
% cc -fsanitize=address -g -o lab2_leak lab2_leak.c
% ./lab2_leak
```

You will see a report indicating memory was allocated and never freed. Although AddressSanitizer does not apply to kernel code, the lesson is identical. Always release what you allocate.

### Mini-Lab 3: Memory Allocation in a Kernel Module

Now let's try a FreeBSD kernel experiment. Create `memlab.c`:

```c
#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

MALLOC_DEFINE(M_MEMLAB, "memlab", "Memory Lab Example");

static void *buffer = NULL;

static int
memlab_load(struct module *m, int event, void *arg)
{
    int error = 0;

    switch (event) {
    case MOD_LOAD:
        printf("memlab: Loading module\n");

        buffer = malloc(128, M_MEMLAB, M_WAITOK | M_ZERO);
        if (buffer == NULL) {
            printf("memlab: malloc failed!\n");
            error = ENOMEM;
        } else {
            printf("memlab: allocated 128 bytes\n");
        }
        break;

    case MOD_UNLOAD:
        printf("memlab: Unloading module\n");

        if (buffer != NULL) {
            free(buffer, M_MEMLAB);
            printf("memlab: memory freed\n");
        }
        break;

    default:
        error = EOPNOTSUPP;
        break;
    }

    return error;
}

static moduledata_t memlab_mod = {
    "memlab", memlab_load, NULL
};

DECLARE_MODULE(memlab, memlab_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
```

Compile and load:

```sh
% cc -O2 -pipe -nostdinc -I/usr/src/sys -D_KERNEL -DKLD_MODULE \
  -fno-common -o memlab.o -c memlab.c
% cc -shared -nostdlib -o memlab.ko memlab.o
% sudo kldload ./memlab.ko
```

Unload with:

```sh
% sudo kldunload memlab
```

#### Detecting Leaks

Comment out the `free()` line, recompile, and load/unload several times. Now inspect memory:

```
% vmstat -m | grep memlab
```

You will see lines like:

```yaml
memlab        128   4   4   0   0   1
```

indicating four allocations of 128 bytes are still "in use" because they were never freed. With the fix in place, the line disappears after unload.

#### Optional: DTrace View

To see allocations live:

```sh
% sudo dtrace -n 'fbt::malloc:entry { trace(arg1); }'
```

When you load the module, you will see the `128` bytes being allocated.

#### Challenge: Prove the Leak

It is one thing to read that kernel leaks accumulate, but another to **see it with your own eyes**. This short experiment will let you prove it on your own system.

1. Open the `memlab.c` module you created earlier and **comment out the `free(buffer, M_MEMLAB);` line** in the unload function. This means the module will allocate memory on load, but never release it on unload.

2. Rebuild the module and then **load and unload it four times in a row**:

   ```sh
   % sudo kldload ./memlab.ko
   % sudo kldunload memlab
   % sudo kldload ./memlab.ko
   % sudo kldunload memlab
   % sudo kldload ./memlab.ko
   % sudo kldunload memlab
   % sudo kldload ./memlab.ko
   % sudo kldunload memlab
   ```

3. Now inspect the kernel's memory allocation table with:

   ```sh
   % vmstat -m | grep memlab
   ```

   You should see output similar to:

   ```yaml
   memlab        128   4   4   0   0   1
   ```

   This means four allocations of 128 bytes were made, and none were freed. Each time you loaded the module, the kernel allocated more memory that was never released.

4. Finally, **restore the `free()` line**, recompile, and repeat the load/unload cycle. This time, when you run `vmstat -m | grep memlab`, the line should disappear after unload, confirming that memory is released properly.

This simple test demonstrates a critical fact: in user space, leaks usually vanish when your process exits. In kernel space, leaks **survive across module reloads** and continue to accumulate. In production systems, such mistakes are not just messy; they are fatal. Over time, leaks can exhaust all available kernel memory and cause the system to crash.

### Common Beginner Pitfalls

Memory safety is one of the hardest lessons for new C programmers, and in kernel space the consequences are much harsher. Let's highlight a few traps that beginners often fall into, and how you can avoid them:

- **Forgetting to free memory.**
   Every `malloc()` must have a matching `free()` in the proper cleanup path. If you allocate during module load, remember to free during module unload. This habit prevents leaks that otherwise accumulate for the entire uptime of the system.
- **Using freed memory.**
   Accessing a pointer after `free()` is called is a classic bug known as *use-after-free*. The pointer may still contain the old address, tricking you into thinking it is valid. A safe habit is to set the pointer to `NULL` immediately after freeing it. That way, any accidental use will be obvious.
- **Choosing the wrong allocation flag.**
   FreeBSD provides different allocation behaviours for different contexts. If you call `malloc()` with `M_WAITOK`, the kernel may put the thread to sleep until memory becomes available, which is fine during module load or attach, but catastrophic inside an interrupt handler. Conversely, `M_NOWAIT` never sleeps and fails immediately if memory is not available. Learning to pick the correct flag is an essential skill.
- **Skipping malloc tags.**
   Always use `MALLOC_DEFINE()` to give your driver a custom memory tag. These tags appear in `vmstat -m` and make debugging leaks much easier. Without them, your allocations may be lumped into generic categories, making it difficult to trace where memory is coming from.

By keeping these pitfalls in mind and practising the good habits shown earlier, you will dramatically reduce the risk of introducing memory bugs into your drivers. These lessons might feel repetitive now, but in real kernel development they are the difference between a stable driver and one that crashes production systems.

### Golden Rules for Kernel Memory

```
1. Every malloc() must have a matching free().
2. Never use a pointer before initialising it (or after freeing it).
3. Use the correct allocation flag (M_WAITOK or M_NOWAIT) for the context.
```

Keep these three rules in mind whenever you write kernel code. They may look simple, but following them consistently is what separates a stable FreeBSD driver from a crash-prone one.

### Pointers Recap: The Lifecycle of Memory in C

```
Step 1: Declare a pointer
-------------------------
struct data *ptr;

   STACK
   +-------------------+
   | ptr : ???         |  -> uninitialised (dangerous!)
   +-------------------+


Step 2: Allocate memory
-----------------------
ptr = malloc(sizeof(struct data));

   STACK                          HEAP
   +-------------------+          +----------------------+
   | ptr : 0xABCD...   |  ----->  | struct data block    |
   +-------------------+          |   value = ???        |
                                  +----------------------+


Step 3: Use the memory
----------------------
ptr->value = 42;

   HEAP
   +----------------------+
   | struct data block    |
   |   value = 42         |
   +----------------------+


Step 4: Free the memory
-----------------------
free(ptr);
ptr = NULL;

   STACK
   +-------------------+
   | ptr : NULL        |  -> safe, prevents reuse
   +-------------------+

   HEAP
   +----------------------+
   | (block released)     |
   +----------------------+
```

**Golden Reminder**:

- Never use an uninitialised pointer.
- Always check your allocations.
- Free what you allocate, and set pointers to `NULL` after freeing.

### Pointers Recap Quiz

Test yourself with these quick questions before moving on. Answers are at the end of this chapter.

1. What happens if you declare a pointer but never initialise it and then dereference it?
2. Why should you always check the return value of `malloc()`?
3. What is the purpose of the `M_ZERO` flag when allocating memory in the FreeBSD kernel?
4. After calling `free(ptr, M_TAG);`, why is it a good habit to set `ptr = NULL;`?
5. In which contexts must you use `M_NOWAIT` instead of `M_WAITOK` when allocating memory in kernel code?

### Wrapping Up

With this section, we have reached the end of our journey through **pointers in C**. Along the way you learned what pointers are, how they relate to arrays and structures, and why they are so powerful but also so dangerous. We concluded with one of the most important lessons: **memory safety**.

Every pointer must be treated with care. Every allocation must be checked. Every buffer must have a known size. In user space, mistakes usually crash only your program. In kernel space, the very same mistakes can corrupt memory or bring down the entire operating system.

By following FreeBSD's allocation patterns, checking results, freeing memory diligently, and using debugging tools like `vmstat -m` and DTrace, you will be on the path to writing drivers that are both stable and reliable.

In the next section, we'll cover **structures** and **typedefs** in C. Structures allow you to group related data, making your code more organized and expressive. Typedefs will enable you to assign meaningful names to complex types, improving readability. Together, they form the basis of almost every real kernel subsystem and are a natural step after mastering pointers.

*continue soon...*
