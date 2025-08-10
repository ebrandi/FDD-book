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

```c
	#include <stdio.h>

	int main(void) {
   	 	printf("Hello, World!\n");
   	 	return 0;
	}
```	
	
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

```c
	#include <stdio.h>

	int main(void) {
    	printf("Hello, World!\n");
    	return 0;
	}
```

Let’s dissect this line by line.

### `#include` Directives: Adding Libraries

```c
	#include <stdio.h>
```

This line is handled by the **C preprocessor** before the program is compiled. It tells the compiler to include the contents of a system header file.

* `<stdio.h>` is a standard header file that provides I/O functions like printf.
* Anything you include this way is pulled into your program at compile time.

In FreeBSD source code, you'll often see many `#include` directives at the top of a file. Here’s an example from the FreeBSD kernel file `sys/kern/kern_shutdown.c`:

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

In user programs, `main()` is where you write your logic. In the kernel, however, there’s **no** `main()` function like this; the kernel has its own bootstrapping process. But FreeBSD kernel modules and subsystems still define **entry points** that act in similar ways.

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

**Important Note:** In kernel code, you don’t use the `printf()` function from the Standard C Library (libc). Instead, the FreeBSD kernel provides its own internal version of `printf()` tailored for kernel-space output, a distinction we'll explore in more detail later in the book.

### Return Values

```c
	    return 0;
	}
```	

This tells the operating system that the program completed successfully.
Returning `0`usually means "**no error**".

You’ll see a similar pattern in kernel code where functions return 0 for success and a non-zero value for failure.

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

The FreeBSD kernel makes extensive use of `errno`-style error codes to represent different failure conditions consistently. Don’t worry if they seem unfamiliar at first, as we move forward, you’ll naturally encounter many of them, and I’ll help you understand how they work and when to use them. 

For a complete list of standard error codes and their meanings, you can refer to the FreeBSD manual page:

	% man 2 intro

### Putting It All Together

Let’s revisit our Hello World program, now with full comments:

```c
	#include <stdio.h>              // Include standard I/O library
	
	int main(void) {                // Entry point of the program
	    printf("Hello, World!\n");  // Print a message to the terminal
	    return 0;                   // Exit with success
	}
```	

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

```c
	int counter = 0;
```	

This tells the compiler:

* Allocate enough memory to store an integer
* Call that memory location counter
* Put the number 0 in it to start

### Declaring Variables

In C, you must declare the type of every variable before using it. This is different from languages like Python, where the type is determined automatically.

Here’s how to declare different types of variables:

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
* **Keeps kernel code lightweight**: Kernel code often avoids runtime overhead, and #define constants don’t allocate memory or exist in the symbol table; they simply get replaced during preprocessing.

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
	
Tip: In C, any non-zero value is considered “true,” and zero is considered “false”.
	
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
* It’s an optimised way to compute `(value * scale) / divisor` using bit shifts instead of division

### Summary

In this section, you’ve learned:

* What expressions are in C
* How to use arithmetic, comparison, and logical operators
* How to assign values and use compound assignments
* How bitwise operations show up in kernel code
* How FreeBSD uses these expressions to control logic and calculations

This section builds the foundation for conditional execution and looping, which we’ll explore next.

## Control Flow

So far, we’ve learned how to declare variables and write expressions. But programs need to do more than compute values; they need to **make decisions** and **repeat actions**. This is where **control flow comes** in.

Control flow statements allow you to:

* Choose between different paths (`if`, `else`, `switch`)
* Repeat operations using loops (`for`, `while`, `do...while`)
* Exit loops early (`break`, `continue`)

These are the **decision-making tools of C**, and they’re essential for writing meaningful programs, from small utilities to operating system kernels.

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

Here’s how it works step by step:

1. `if (x > 0)` – The program checks the first condition. If it’s true, the block inside runs and the rest of the chain is skipped.

1. `else if (x < 0)` – If the first condition was false, this second one is checked. If it’s true, it's block runs and the chain ends.

1. `else` – If none of the previous conditions are true, the code inside `else` runs.

**Important syntax rules:**

* Each condition must be inside **parentheses** `( )`.
* Each block of code is surrounded by **curly braces `{ }`**, even if it’s only one line (this prevents common mistakes).

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

Let’s walk through the flow control step by step:

1. First `if` – Checks whether the description is too long to fit.
	* If **true**, the function immediately stops and returns an error code (`ENAMETOOLONG`).
	* If **false**, execution moves on to the next condition.
1. `else if` – Runs only if the first condition was **false**.
	* If the length is exactly zero, it means the user didn’t provide a description, so the code sets `descrbuf` to `NULL`.
	* If **false**, the program moves on to the final `else`.
1. Final `else` – Executes when neither of the previous conditions are true.
	* Allocates memory for the description and copies the provided text into it.
	* If copying fails, it frees the memory and exits the loop or function.

**How the flow works:**

* Only one of these three paths runs each time.
* The first matching condition “wins", and the rest are skipped.
* This is a classic example of using `if / else if / else` to handle mutually exclusive conditions,  reject invalid input, handle the empty case, or process a valid value.

In C, `if / else if / else` chains provide a straightforward way to handle several possible outcomes in a single structure. The program checks each condition in order, and as soon as one is true, that block runs and the rest are skipped. This simple rule keeps your logic predictable and easy to follow. In the FreeBSD kernel, you’ll see this pattern everywhere, from network stack functions to device drivers, because it ensures that only the correct code path runs for each situation, making the system’s decision-making both efficient and reliable.

### Understanding the `switch` and `case`

A switch statement is a decision-making structure that’s useful when you need to compare one variable against multiple possible values. Instead of writing a long chain of if and else if statements, you can list each possible value as a case.

Here’s a simple example:

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

You can see a real use of switch in the FreeBSD kernel inside the function `thread_compare()` (starting at line 109 in `sys/kern/tty_info.c`). The fragment we’re interested in is from lines 134 to 141:

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

This code decides which of two threads is “more interesting” for the scheduler based on whether each thread is runnable.

* `runa` and `runb` are flags that indicate if the first thread (`a`) and the second thread (`b`) are runnable.
* The macro `TESTAB(a, b)` combines those flags into a single value. This result can be one of three predefined constants:
	* `ONLYA` - Only thread A is runnable.
	* `ONLYB` - Only thread B is runnable.
	* `BOTH` - Both threads are runnable.

The switch works like this:

1. Case `ONLYA` – If only thread A is runnable, return `0`.
1. Case `ONLYB` – If only thread B is runnable, return `1`.
1. Case `BOTH` – If both threads are runnable, don’t return immediately; instead, `break` so the rest of the function can handle this situation.

In short, `switch` statements provide a clean and efficient way to handle multiple possible outcomes from a single expression, avoiding the clutter of long `if / else if` chains. In the FreeBSD kernel, they are often used to react to different commands, flags, or states, as in our example, which decides between thread A, thread B, or both. Once you become comfortable reading switch structures, you’ll start to recognise them throughout kernel code as a go-to pattern for organising decision-making logic in a clear, maintainable way.

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
* `i` counts how many buffers we’ve handled in this batch.
* `n` is the total remaining buffers to refill; it decrements every iteration.
* For each buffer, the code grabs its slot, figures out the physical address, readies it for DMA, then advances the ring indices (`nm_i`, `nic_i`).
* The loop stops when either the batch is full (`i` hits the max) or there’s nothing left to do (`n == 0`). The batch is then “published” to the NIC by the code right after the loop.

In essence, a `for` loop is the go-to choice when you have a clear limit on how many times something should run. It packages initialisation, condition checking, and iteration updates into a single, compact header, making the flow easy to follow. 

In FreeBSD’s kernel code, this structure is everywhere from scanning arrays to walking network ring buffers, because it keeps repetitive work both predictable and efficient. Our example from `netmap_fl_refill()` shows precisely how this works in practice: 

the loop counts through a fixed-size batch of buffers, stopping either when the batch is full or when there’s no more work left, then hands that batch off to the NIC. Once you get comfortable reading for loops like this, you’ll spot them throughout the kernel and understand how they keep complex systems running smoothly.

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

This time, I’ve decided to show you the complete source code for this FreeBSD kernel function because it offers an excellent opportunity to see several concepts from this chapter working together in a real-world context. 

To make it easier to follow, I’ve added explanatory comments at key points so you can connect the theory to the actual implementation. Don’t worry if you don’t fully understand every detail right now; this is normal when first looking at kernel code. 

For our discussion, pay special attention to the while loop that begins at line 915, as it’s the part we will explore in depth. Look for `while (n > 0) {` in the code below:

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

The loop we’re about to study looks like this:

```c
	while (n > 0) {
	    ...
	}
```
	
It comes from **iflib** (the Interface Library) in FreeBSD’s network stack, in a section of code that connects **netmap** with network drivers.

Netmap is a high-performance packet I/O framework designed for very fast packet processing. In this context, the kernel uses the loop to **refill receive buffers**, ensuring the network interface card (NIC) always has space ready to store incoming packets, keeping data flowing smoothly at high speed.

Here, `n` is simply the number of buffers that still need to be prepared. The loop works through them in **efficient batches**, processing a few at a time until all are ready. This batching approach reduces overhead and is a common technique in high-performance network drivers.

**What the `while (n > 0)` Really Does**

As we’ve just seen, `n` is the count of receive buffers still waiting to be prepared. This loop’s job is simple in concept:

*“Work through those buffers in batches until there are none left.”*

Each pass of the loop prepares a group of buffers and hands them off to the NIC. If there’s still work to do, the loop runs again, ensuring that by the end, all required buffers are ready for incoming packets.

**What Happens Inside the while (n > 0) Loop**

Each time the loop runs, it processes one batch of buffers. Here’s the breakdown:

1. **Debug Tracking** – If the driver is compiled with debugging enabled, it may update counters that track how often large batches of buffers are refilled. This is just for performance monitoring.
1. **Batch Setup** – The driver remembers where this batch starts (`nic_i_first`) so it can later tell the NIC exactly which slots were updated.
1. **Inner Batch Processing** – Inside the loop, there’s another for loop that refills up to a maximum number of buffers at a time (IFLIB_MAX_RX_REFRESH). For each buffer in this batch:
	* Look up the buffer’s address and physical location in memory.
	* Check if the buffer is valid. If not, reinitialise the receive ring.
	* Store the physical address and slot index so the NIC knows where to place incoming data.
	* If the buffer has changed or this is the first initialisation, update its DMA (Direct Memory Access) mapping.
	* Synchronise the buffer for reading so the NIC can safely use it.
	* Clear any “buffer changed” flags.
	* Move to the next buffer position in the ring.
1. **Publishing the Batch to the NIC** – Once the batch is ready, the driver calls a function to tell the NIC: 

“These new buffers are ready for use.”

By breaking the work into manageable batches and looping until every buffer is ready, this while loop ensures the NIC is always prepared to receive incoming data without interruption. It’s a small but crucial part of keeping packet flow continuous in a high-performance networking environment. 

Even if some of the lower-level details—like DMA mapping or ring indices aren’t fully clear yet, the key takeaway is this: 

Loops like this are the engine that quietly keeps the system running at full speed. As you progress through the book, these concepts will become second nature, and you’ll start to recognise similar patterns across many parts of the FreeBSD kernel.

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
	
While `do...while (0)` may look odd, it’s a solid C idiom used to make macro expansions safe and predictable in all contexts (like inside conditional statements). It ensures that the entire macro behaves like one statement and avoids accidentally creating half-executed code. Understanding this helps you read and avoid subtle bugs in kernel code that rely heavily on macros for clarity and safety.

### Understanding `break` and `continue`

When working with loops in C, sometimes you need to change the normal flow:

1. `break` – Immediately exits the loop, even if the loop condition could still be true.
1. `continue` – Skips the rest of the current iteration and jumps directly to the loop’s next iteration.

Here’s a simple example:

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

In loops, `break` and `continue` are precision tools for controlling execution flow. In the `if_purgeaddrs()` function from FreeBSD’s `sys/net/if.c`, `break` stops the search when there are no more addresses to remove, or halts the inner scan as soon as a target address is found. `continue` skips the generic removal step when a specialised IPv4 or IPv6 routine has already handled the work, jumping straight to the next pass through the outer loop. This design lets the function repeatedly find one removable address at a time, remove it using the most appropriate method, and keep going until no non-link-layer addresses remain. 

The key takeaway is that well-placed break and continue statements keep loops efficient and focused, avoiding wasted work and making the code’s intent clear, a pattern you’ll encounter often in FreeBSD’s kernel for both clarity and performance.

### Pro Tip: Always Use Braces `{}`

In C, if you omit braces after an if, only one statement is actually controlled by the if. This can easily lead to mistakes:

```c
	if (x > 0)
		printf("Positive\n");   // Runs only if x > 0
		printf("Always runs\n"); // Always runs! Not part of the if
```
	    
This is a common source of bugs because the second printf appears to be inside the if, but it isn’t.

To avoid confusion and accidental logic errors, always use braces, even for a single statement:

```c
	if (x > 0) {
	    printf("Positive\n");
	}
```
	
This makes your intent explicit, keeps your code safe from subtle changes, and follows the style used in the FreeBSD source tree.

**Also Safer for Future Changes**

When you always use braces, it’s much safer to modify the code later:

```c
	if (x > 0) {
	    printf("x is positive\n");
	    log_positive(x);   // Adding this won't break logic!
	}
```

### Summary

In this section, you’ve learned:

* How to make decisions using if, else, and switch
* How to write loops using for, while, and do...while
* How to exit or skip iterations with break and continue
* How FreeBSD uses control flow to walk lists and make kernel decisions

You now have the tools to control the logic and flow of your programs, which is the core of programming itself.

## Functions

In C, a **function** is like a dedicated workshop in a large factory; it is a self-contained area where a specific task is carried out, start to finish, without disturbing the rest of the production line. When you need that task done, you simply send the work there, and the function delivers the result.

Functions are one of the most important tools you have as a programmer because they let you:

* **Break down complexity**: Large programs become easier to understand when split into smaller, focused operations.
* **Reuse logic**: Once written, a function can be called anywhere, saving you from typing (and debugging) the same code repeatedly.
* **Improve clarity**: A descriptive function name turns a block of cryptic code into a clear statement of intent.

You’ve already seen functions at work:

* `main()` — the starting point of every C program.
* `printf()` — a library function that handles formatted output for you.

In the FreeBSD kernel, you’ll find functions everywhere, from low-level routines that copy data between memory regions to specialised ones that communicate with hardware. For example, when a network packet arrives, the kernel doesn’t put all the processing logic in one giant block of code. Instead, it calls a series of functions, each responsible for a clear, isolated step in the process.

In this section, you’ll learn how to create your own functions, giving you the power to write clean, modular code. This isn’t just good style in FreeBSD device driver development; it’s the foundation for stability, reusability, and long-term maintainability.

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
1. **Local variables are created** — the function gets its own workspace in memory, separate from the caller’s variables.
1. **The function runs** — it executes its statements in order, possibly calling other functions along the way.
1. **A return value is sent back** — if the function produces a result, it is placed in a register (commonly eax on x86) for the caller to pick up.
1. **Cleanup and resume** — the function’s workspace is removed from the stack, and the program continues where it left off.

**Why do you need to understand that?**

In kernel programming, every function call has a cost in both time and memory. Understanding this process will help you write efficient driver code and avoid subtle bugs, especially when working with low-level routines where stack space is limited.

### Defining and Declaring Functions

Every function in C follows a simple recipe. To create one, you need to specify four things:

1. **Return type** – what kind of value the function gives back to the caller.
	* Example: `int` means the function will return an integer.
	* If it doesn’t return anything, we use the keyword `void`.

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

A lot of beginners get tripped up here, so let’s make it crystal clear:

* **Declaration** tells the compiler that a function exists, what it’s called, what parameters it takes, and what it returns, but it does not provide the code for it.
* **Definition** is where you actually write the body of the function, the full implementation that does the work.

Think of it like planning and building a workshop:

* **Declaration**: putting up a sign saying *“This workshop exists, here’s what it’s called, and here’s the kind of work it does.”*
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
* The kernel will call your driver’s functions (like `probe()`, `attach()`, `detach()`) based on the declarations it sees in your driver’s headers, without caring exactly how you implement them as long as the signatures match.

Understanding this difference will save you a lot of compiler errors, especially “implicit declaration of function” or “undefined reference” errors, which are among the most common mistakes beginners hit when starting with C.

**How Declarations and Definitions Work Together**

In small programs, you might write the function’s definition before `main()` and be done.
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

1. Declaration in the header file tells the compiler: *“These functions exist somewhere, here’s what they look like.”*
1. Definition in the source file provides the actual code.
1. Any other `.c` file that includes `mydevice.h` can now call these functions, and the compiler will check the parameters and return types.
1. At link time, the function calls are connected to their definitions.

**In the context of FreeBSD drivers:**

* You might have `mydevice.c` containing the driver logic, and `mydevice.h` holding the function declarations shared across the driver.
* The kernel build system will compile your `.c` files and link them into a kernel module.
* If the declarations don’t match the definitions exactly, you’ll get compiler errors — which is why keeping them in sync is critical.

**Common mistakes with functions and how to fix them**

1) Calling a function before the compiler knows it exists
Symptom: “implicit declaration of function” warning or error.
Fix: Add a declaration in a header file and include it, or place the definition above its first use.

2) Declaration and definition do not match
Symptom: “conflicting types” or odd runtime bugs.
Fix: Make the signature identical in both places. Same return type, parameter types, and qualifiers in the same order.

3) Forgetting `void` for a function with no parameters
Symptom: The compiler may think the function takes unknown arguments.
Fix: Use int `my_fn(void)` instead of int `my_fn()`.

4) Returning a value from a `void` function or forgetting to return a value
Symptom: “void function cannot return a value” or “control reaches end of non-void function.”
Fix: For non-void functions, always return the right type. For `void`, do not return a value.

5) Returning pointers to local variables
Symptom: Random crashes or garbage data.
Fix: Do not return the address of a stack variable. Use dynamically allocated memory or pass a buffer in as a parameter.

6) Mismatched `const` or pointer levels between declaration and definition
Symptom: Type mismatch errors or subtle bugs.
Fix: Keep qualifiers consistent. If the declaration has `const char *`, the definition must match exactly.

7) Multiple definitions across files
Symptom: Linker error “multiple definition of …”.
Fix: Only one definition per function. If a helper should be private to a file, mark it `static` in that `.c` file.

8) Putting function definitions in headers by accident
Symptom: Multiple definition linker errors when the header is included by several `.c` files.
Fix: Headers should usually have declarations only. If you really need code in a header, make it `static inline` and keep it small.

9) Missing includes for functions you call
Symptom: Implicit declarations or wrong default types.
Fix: Include the correct system or project header that declares the function you are calling, for example `#include <stdio.h>` for `printf`.

10) Kernel specific: undefined symbols when building a module
Symptom: Linker error “undefined reference” while building your KMOD.
Fix: Ensure the function is actually defined in your module or exported by the kernel, that the declaration matches the definition, and that the right source files are part of the module build.

11) Kernel specific: using a helper that is meant to be file local
Symptom: “undefined reference” from other files or unexpected symbol visibility.
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

`main.c` - This is the “user” of the functions. It just includes the header and calls them.

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

* `mydevice.h` would hold your driver’s public API (function declarations).
* `mydevice.c` would have the full implementations of those functions.
* The kernel (or other parts of the driver) would include the header to know how to call into your code, without needing to see the actual implementation details.

This exact pattern is how `probe()`, `attach()`, and `detach()` routines are structured in actual device drivers. Learning it now will make those later chapters feel familiar.
	
Understanding the relationship between declarations and definitions is a cornerstone of C programming, and it becomes even more important when you step into the world of FreeBSD device drivers. In kernel development, functions are rarely defined and used in the same file; they are spread across multiple source and header files, compiled separately, and linked together into a single module. A clear separation between **what a function does** (its declaration) and **how it does it** (its definition) keeps code organized, reusable, and easier to maintain. Master this concept now, and you’ll be well-prepared for the more complex modular structures you’ll encounter when we begin building real kernel drivers.

Continue...