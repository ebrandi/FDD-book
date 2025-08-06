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

### Summary

In this section, you’ve learned:

* The structure of a C program
* How #include and main() work
* What printf() and return do
* How similar structures appear in FreeBSD’s kernel code

The more C code you read, both your own and from FreeBSD, the more these patterns will become second nature.

## Variables and Data Types

In any programming language, variables are how you store and manipulate data. In C, variables are a little more "manual" than in higher-level languages, but they give you the control you need to write fast, efficient programs, and that’s precisely what operating systems like FreeBSD require.

In this section, we’ll explore:

* How to declare and initialise variables
* The most common data types in C
* How FreeBSD uses them in kernel code
* Some tips to avoid common beginner mistakes

Let’s start with the basics.

### What Is a Variable?

A variable is like a labeled box in memory where you can store a value, such as a number, a character, or even a block of text.

Here’s a simple example:

	int counter = 0;
	

This tells the compiler:

* Allocate enough memory to store an integer
* Call that memory location counter
* Put the number 0 in it to start

### Declaring Variables

In C, you must declare the type of every variable before using it. This is different from languages like Python, where the type is determined automatically.

Here’s how to declare different types of variables:

	int age = 30;             // Integer (whole number)
	float temperature = 98.6; // Floating-point number
	char grade = 'A';         // Single character

You can also declare multiple variables at once:

	int x = 10, y = 20, z = 30;

Or leave them uninitialized (but be careful, as uninitialized variables contain garbage values!):

	int count; // May contain anything!

Always initialise your variables, not just because it’s good C practice, but because in kernel development, uninitialized values can lead to subtle and dangerous bugs, including kernel panics, unpredictable behaviour, and security vulnerabilities. In userland, mistakes might crash your program; in the kernel, they can compromise the stability of the entire system. 

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

* `const`: This variable can’t be changed.
* `volatile`: The value can change unexpectedly (used with hardware!).
* `unsigned`: The variable cannot hold negative numbers.

Example:

	const int max_users = 100;
	volatile int status_flag;

The `volatile` qualifier can be important in FreeBSD kernel development, but only in very specific contexts, such as accessing hardware registers or dealing with interrupt-driven updates. It tells the compiler not to optimise accesses to a variable, which is critical when values can change outside of normal program flow. 

However, `volatile` is not a substitute for proper synchronisation and should not be used for coordinating access between threads or CPUs. For that, the FreeBSD kernel provides dedicated primitives like mutexes and atomic operations, which offer both compiler and CPU-level guarantees.

### Constant Values and #define

In C programming and especially in kernel development, it's very common to define constant values using the #define directive:

	#define MAX_DEVICES 64

This line doesn't declare a variable. Instead, it's a **preprocessor macro**, which means the C preprocessor will **replace every occurrence of** `MAX_DEVICES` **with** `64` before the actual compilation begins. This replacement happens **textually**, and the compiler never even sees the name `MAX_DEVICES`.

### Why Use #define for Constants?

Using `#define` for constant values has several advantages in kernel code:

* **Improves readability**: Instead of seeing magic numbers (like 64) scattered throughout the code, you see meaningful names like MAX_DEVICES.
* **Makes code easier to maintain**: If the maximum number of devices ever needs to change, you update it in one place, and the change is reflected wherever it's used.
* **Keeps kernel code lightweight**: Kernel code often avoids runtime overhead, and #define constants don’t allocate memory or exist in the symbol table; they simply get replaced during preprocessing.

### Real Example From FreeBSD

You will find many `#define` lines in `sys/sys/param.h`, for example:

	#define MAXHOSTNAMELEN 256  /* max hostname size */
	
This defines the maximum number of characters allowed in a system hostname, and it's used throughout the kernel and system utilities to enforce a consistent limit. The value 256 is now standardised and can be reused wherever the hostname length is relevant.

### Watch Out: There Is No Type Checking

Because `#define` simply performs textual substitution, it does not respect types or scoping. 

For example:

	#define PI 3.14
	
This works, but it can lead to problems in certain contexts (e.g., integer promotion, unintended precision loss). For more complex or type-sensitive constants, you may prefer using `const` variables or `enums` in userland, but in the kernel, especially in headers, `#define` is often chosen for efficiency and compatibility.

### Best Practices for #define Constants in Kernel Development

* Use **ALL CAPS** for macro names to distinguish them from variables.
* Add comments to explain what the constant represents.
* Avoid defining constants that depend on runtime values.
* Prefer `#define` over `const` in header files or when targeting C89 compatibility (which is still common in kernel code).

### Best Practices for Variables

Writing correct and robust kernel code starts with disciplined variable usage. The tips below will help you avoid subtle bugs, improve code readability, and align with FreeBSD kernel development conventions.

**Always initialise your variables**: Never assume a variable starts at zero or any default value, especially in kernel code, where behaviour must be deterministic. An uninitialized variable could hold random garbage from the stack, leading to unpredictable behaviour, memory corruption, or kernel panics. Even when the variable will be overwritten soon, it’s often safer and more transparent to initialise it explicitly unless performance measurements prove otherwise.

**Don't use variables before assigning a value**: This is one of the most common bugs in C, and compilers won’t always catch it. In the kernel, using an uninitialized variable can result in silent failures or catastrophic system crashes. Always trace your logic to ensure every variable is assigned a valid value before use, especially if it influences memory access or hardware operations.

**Use `const` whenever the value shouldn’t change**:
Using `const` is more than good style; it helps the compiler enforce read-only constraints and catch unintended modifications. This is particularly important when:

* Passing read-only pointers into functions
* Protecting configuration structures or table entries
* Marking driver data that must not change after initialisation

In kernel code, this can even lead to compiler optimisations and make the code easier to reason about for reviewers and maintainers.

**Use `unsigned` for values that can’t be negative (like sizes or counters)**: Variables that represent quantities like buffer sizes, loop counters, or device counts should be declared as `unsigned` types (`unsigned int`, `size_t`, or `uint32_t`, etc.). This improves clarity and prevents logic bugs, especially when comparing with other `unsigned` types, which can cause unexpected behaviour if signed values are mixed in.

**Prefer fixed-width types in kernel code (`uint32_t`, `int64_t`, etc.)**: Kernel code must behave predictably across architectures (e.g., 32-bit vs 64-bit systems). Types like `int`, `long`, or `short` can vary in size depending on the platform, which can lead to portability issues and alignment bugs. Instead, FreeBSD uses standard types from `<sys/types.h>` such as:

* `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`
* `int32_t`, `int64_t`, etc.

These types ensure your code has a known, fixed layout and avoids surprises when compiling or running on different hardware.

**Pro Tip**: When in doubt, look at existing FreeBSD kernel code, especially drivers and subsystems close to what you're working on. The variable types and initialisation patterns used there are often based on years of hard-earned lessons from real-world systems.

### Summary

In this section, you’ve learned:

* How to declare and initialise variables
* The most important data types in C
* What type qualifiers like const and volatile do
* How to spot and understand variable declarations in FreeBSD’s kernel code

You now have the tools to store and work with data in C, and you've already seen how FreeBSD uses the same concepts in production-quality kernel code.

## Operators and Expressions

So far, we've learned how to declare and initialise variables. Now it's time to make them do something! In this section, we’ll dive into operators and expressions, the mechanisms in C that allow you to compute values, compare them, and control program logic.

We’ll cover:

* Arithmetic operators
* Comparison operators
* Logical operators
* Bitwise operators (lightly)
* Assignment operators
* Real examples from FreeBSD kernel code

### What Is an Expression?

In C, an expression is anything that produces a value. For example:

	int a = 3 + 4;

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

	if ((a > 0) && (b < 100)) {
    	// both conditions must be true
	}
	
### Assignment and Compound Assignment

The `=` operator assigns a value:

	x = 5; // assign 5 to x
	
Compound assignment combines operation and assignment:

| Operator | Meaning             | Example   | Equivalent to |
| -------- | ------------------- | --------- | ------------- |
| `+=`     | Add and assign      | `x += 3;` | `x = x + 3;`  |
| `-=`     | Subtract and assign | `x -= 2;` | `x = x - 2;`  |
| `*=`     | Multiply and assign | `x *= 4;` | `x = x * 4;`  |
| `/=`     | Divide and assign   | `x /= 2;` | `x = x / 2;`  |
| `%=`     | Modulus and assign  | `x %= 3;` | `x = x % 3;`  |

### Bitwise Operators

In kernel development, bitwise operators are standard. Here’s a light preview:

| Operator | Meaning     | Example  |
| -------- | ----------- | -------- |
| &      | Bitwise AND | a & b  |
| \|     | Bitwise OR  | a \| b  |
| ^      | Bitwise XOR | a ^ b  |
| ~      | Bitwise NOT | ~a     |
| <<     | Left shift  | a << 2 |
| >>     | Right shift | a >> 1 |

We’ll cover these in detail later when we work with flags, registers, and hardware I/O.

### Real Example from FreeBSD: sys/kern/tty_info.c

Let’s look at a real example from the FreeBSD source code. 

Open the file `sys/kern/tty_info.c`and look for the function `thread_compare()` starting on line 109, you will see the code below:

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
	
We are interested in this fragment of code:

	...
	runa = TD_IS_RUNNING(td) || TD_ON_RUNQ(td);
	...
	return (td < td2);

Explanation:

* `TD_IS_RUNNING(td)` and `TD_ON_RUNQ(td)` are macros that return boolean values.
* The logical OR `||` checks if either condition is true.
* The result is assigned to `runa.

Later, this line:

	return (td < td2);

Uses the less-than operator to compare two pointers (`td` and `td2`). This is valid in C; pointer comparisons are common when choosing between resources.

Another real expression in that same file can be found at line 367:

	pctcpu = (sched_pctcpu(td) * 10000 + FSCALE / 2) >> FSHIFT;
	
This expression:

* Multiplies the CPU usage estimate by 10,000
* Adds half the scale factor for rounding
* Then performs a **bitwise right shift** to scale it down
* It’s an optimised way to compute `(value * scale) / divisor` using bit shifts instead of division

### Summary

In this section, you’ve learned:

* What expressions are in C
* How to use arithmetic, comparison, and logical operators
* How to assign values and use compound assignments
* How bitwise operations show up in kernel code
* How FreeBSD uses these expressions to control logic and calculations

This section builds the foundation for conditional execution and looping, which we’ll explore next.

Continue...