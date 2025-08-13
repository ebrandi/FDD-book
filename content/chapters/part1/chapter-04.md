---
title: "A First Look at the C Programming Language"
description: "This chapter introduces the C programming language for complete beginners."
author: "Edson Brandi"
date: "2025-08-10"
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

### Calling Functions

Once you’ve defined a function, the next step is to call it, that is, to tell the program, *“Hey, go run this block of code now and give me the result”.*

Calling a function is as simple as writing its name followed by parentheses containing any required arguments.

If the function returns a value, you can store that value in a variable, pass it to another function, or use it directly in an expression.

**Example:**

```c
int result = add(3, 4);
printf("Result is %d\n", result);
```

Here’s what happens step-by-step when this code runs:

1. The program encounters `add(3, 4)` and pauses its current work.
1. It jumps to the `add()` function’s definition, giving it two arguments: `3` and `4`.
1. Inside `add()`, the parameters `a` and `b` receive the values `3` and `4`.
1. The function calculates `sum = a + b` and then executes `return sum;`.
1. The returned value `7` travels back to the calling point and gets stored in the variable `result`.
1. The `printf()` function then displays:

```c
	Result is 7
```

**FreeBSD Driver Connection**

When you call a function in a FreeBSD driver, you’re often asking the kernel or your own driver logic to perform a very specific task, for example:

* Calling `bus_space_read_4()` to read a 32-bit hardware register.
* Calling your own `mydevice_init()` to prepare a device for use.

The principle is exactly the same as the `add()` example: 

The function takes parameters, does its job, and returns control to where it was called. The difference in kernel space is that the “job” might involve talking directly to hardware or managing system resources, but the calling process is identical.

**Tip for Beginners**
Even if a function doesn’t return a value (its return type is `void`), calling it still triggers its entire body to run. In drivers, many important functions don’t return anything but perform critical work like initializing hardware or setting up interrupts.

Function Call Flow
When your program calls a function, control jumps from the current point in your code to the function’s definition, runs its statements, and then comes back.
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

* The program’s “path” temporarily leaves `main()` when the function is called.
* The parameters in the function get copies of the values passed in.
* The return statement sends a value back to where the function was called.
* After the call, execution continues right where it left off.

**FreeBSD driver analogy:**

When the kernel calls your driver’s `attach()` function, the exact same process happens. The kernel jumps into your code, you run your initialization logic, and then control returns to the kernel so it can continue loading devices. Whether in user space or kernel space, function calls follow the same flow.

**Try It Yourself – Simulating a Driver Function Call**

In this exercise, you’ll write a small program that mimics calling a driver function to read a “hardware register” value.

We’ll simulate it in user space so you can compile and run it easily on your FreeBSD system.

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

Here’s what’s happening:

* void before the name means: *“This function will not return a value”.*
* The `(void)` in the parameter list means: *“This function takes no arguments”.*
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
In old C code, `()` without void means *“this function takes an unspecified number of arguments”*, which can cause confusion.

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

Now that you’ve seen the most common pitfalls, let’s take a step back and understand why the void keyword is important in the first place.

**Why `void` matters**

Marking a function with `void` clearly tells both the compiler and human readers that this function’s purpose is to perform an action, not to produce a result.

If you try to use the “return value” from a `void` function, the compiler will stop you, which helps catch mistakes early.
	
**FreeBSD driver perspective**

In FreeBSD drivers, many important functions are void because they are all about doing work, not returning data.

For example:

* `mydevice_reset(void)` — might reset the hardware to a known state.
* `mydevice_led_on(void)` — might turn on a status LED.
* `mydevice_log_status(void)` — might print debugging information to the kernel log.

The kernel doesn’t care about a return value in these cases, it just expects your function to perform its action.

While `void` functions in drivers don’t return values, that doesn’t mean they can’t communicate important information. There are still several ways to signal events or issues back to the rest of the system.

**Tip for Beginners**

In driver code, even though `void` functions don’t return data, they can still report errors or events by:

* Writing to a global or shared variable.
* Logging messages with `device_printf()` or `printf()`.
* Triggering other functions that handle error states.

Understanding void functions is important because in real-world FreeBSD driver development, not every task produces data to return; many simply perform an action that prepares the system or the hardware for something else. Whether it’s initializing a device, cleaning up resources, or logging a status message, these functions still play a critical role in the overall behavior of your driver. By recognizing when a function should return a value and when it should simply do its job and return nothing, you’ll write cleaner, more purposeful code that matches the way the FreeBSD kernel itself is structured.

## Function Declarations (Prototypes)

In C, it’s a good habit and often essential to **declare** a function before you use it.

A function declaration, also called a **prototype**, tells the compiler:

* The function’s name.
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

* the function’s name is `add`,
* it takes two `int` parameters, and
* it will return an `int`.

Later, when the compiler finds the definition, it checks that the name, parameters, and return type match the prototype exactly. If they don’t, it raises an error.

### Why prototypes matter

Placing the prototype before a function is called provides several benefits:

1. **Prevents unnecessary warnings and errors**: If you call a function before the compiler knows it exists, you’ll often get an *“implicit declaration of function”* warning or even a compilation error.

1. **Catches mistakes early**: If your call passes the wrong number or types of arguments, the compiler will flag the problem immediately instead of letting it cause unpredictable behaviour at runtime.

1. **Enables modular programming**: Prototypes allow you to split your program into multiple source files. You can keep the function definitions in one file and the calls to them in another, with the prototypes stored in a shared header file.

By declaring your functions before you use them, either at the top of your .c file or in a .h header, you’re not just keeping the compiler happy; you’re building code that’s easier to organise, maintain, and scale.

Now that you understand why prototypes are important, let’s look at the two most common places to put them: directly in your `.c ` file or in a shared header file.

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

For example, in your driver’s header file you might declare:

```c
int mydevice_init(void);
void mydevice_start_transmission(void);
```

These tell the kernel or bus subsystem that your driver has these functions available, even if the actual definitions live deep inside your `.c` files.

The build system compiles all the pieces together and links the calls to the correct implementations.

### Try It Yourself – Moving a Function Below `main()`

One of the main reasons to use prototypes is so you can call a function that hasn’t been defined yet in the file. Let’s see this in action.

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

You’ll likely get a warning such as:

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

By adding a prototype, you’ve just seen how the compiler can recognize a function and validate its use even before it sees the actual code. This same principle is what allows the FreeBSD kernel to call into your driver; it doesn’t need the whole function body up front, only the declaration. In the next section, we’ll look at how this works in a real driver, where prototypes in header files act as the kernel’s “map” to your driver’s capabilities.

### FreeBSD Driver Connection

In the FreeBSD kernel, function prototypes are the way the system “introduces” your driver’s functions to the rest of the codebase.

When the kernel wants to interact with your driver, it doesn’t search for the function’s code directly; it relies on the function’s declaration to know the name, parameters, and return type.

For example, during device detection, the kernel might call your `probe()` function to check whether a specific piece of hardware is present. The actual definition of `probe()` could be deep inside your `mydriver.c` file, but the **prototype** lives in your driver’s header file (`mydriver.h`). That header is included by the kernel or bus subsystem so it can compile code that calls `probe()` without needing to see its full implementation.

This arrangement ensures two critical things:

1. **Compiler validation**: The compiler can confirm that any calls to your functions use the correct parameters and return type.
1. **Linker resolution**: When building the kernel or your driver module, the linker knows exactly which compiled function body to connect to the calls.

Without correct prototypes, the kernel build could fail or, worse, compile but behave unpredictably at runtime. In kernel programming, that’s not just a bug, it could mean a crash.

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

Here, we declare three key entry points `probe()`, `attach()`, and `detach()`, but don’t include their bodies.

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

Understanding how the kernel uses your driver’s function prototypes is more than just a formality; it’s a safeguard for correctness and stability. In kernel programming, even a slight mismatch between a declaration and a definition can lead to build failures or unpredictable runtime behaviour. That’s why experienced FreeBSD developers follow a few best practices to keep their prototypes clean, consistent, and easy to maintain. Let’s go over some of those tips next.

### Tip for Kernel Code

When you start writing FreeBSD drivers, function prototypes aren’t just a formality; they’re a key part of keeping your code organised and error-free in a large, multi-file project. In the kernel, where functions are often called from deep within the system, a mismatch between a declaration and its definition can cause build failures or subtle bugs that are hard to track down.

To avoid problems and keep your headers clean:

* **Always match parameter types exactly** between the declaration and the definition; the return type, parameter list, and order must be identical.
* **Include qualifiers like `const` and `*` consistently** so you don’t accidentally change how parameters are treated between the declaration and the definition.
* **Group related prototypes together** in header files so they’re easy to find. For example, put all initialisation functions in one section, and hardware access functions in another.

Function prototypes may seem like a small detail in C, but they are the glue that holds multi-file projects and especially kernel code together. By declaring your functions before they are used, you give the compiler the information it needs to catch mistakes early, keep your code organised, and allow different parts of a program to communicate cleanly. 

In FreeBSD driver development, well-structured prototypes in header files enable the kernel to interact with your driver reliably, without knowing its internal details. Mastering this habit now is non-negotiable if you want to write stable, maintainable drivers. 

In the next section, we’ll explore real examples from the FreeBSD source tree to see exactly how prototypes are used throughout the kernel, from core subsystems to actual device drivers. This will not only reinforce what you’ve learned here, but also help you recognise the patterns and conventions that experienced FreeBSD developers follow every day.

### Real Example from the FreeBSD 14.3 Source Tree: `device_printf()`

Now that you understand how function declarations and definitions work, let’s walk through a concrete example from the FreeBSD kernel. We will follow `device_printf()` from its prototype in a header, to its definition in the kernel source, and finally to a real driver that calls it during initialisation. This shows the full path a function takes in real code and why prototypes are critical in driver development.

**1) Prototype — where it is declared**

The `device_printf()` function is declared in the FreeBSD kernel’s bus interface header `sys/sys/bus.h`. Any driver source that includes this header can call it safely because the compiler knows its signature in advance.

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

By now, you’ve seen how a function prototype, its implementation, and its real-world usage come together inside the FreeBSD kernel. From the declaration in a shared header, through the implementation in kernel code, to the call site inside a real driver, each step shows why prototypes are the “glue” that lets different parts of the system communicate cleanly. In driver development, they ensure the kernel can call into your code with complete confidence about the parameters and return type no guesswork, no surprises. Getting this right is a matter of both correctness and maintainability, and it’s a habit you’ll use in every driver you write.

Before we go further into writing complex driver logic, we need to understand one of the most fundamental concepts in C programming: variable scope. Scope determines where a variable can be accessed in your code, how long it stays alive in memory, and what parts of the program can modify it. In FreeBSD driver development, misunderstanding scope can lead to elusive bugs from uninitialised values corrupting hardware state to variables mysteriously changing between function calls. By mastering scope rules, you’ll gain fine-grained control over your driver’s data, ensuring that values are only visible where they should be, and that critical state is preserved or isolated as needed. In the next section, we’ll break down scope into clear, practical categories and show you how to apply them effectively in kernel code.

## Variable Scope in Functions

In programming, **scope** defines the boundaries within which a variable can be seen and used. In other words, it tells us where in the code a variable is visible and who is allowed to read or change its value.

When a variable is declared inside a function, we say it has **local scope**. Such a variable comes into existence when the function starts running and disappears as soon as the function finishes. No other function can see it, and even within the same function, it may be invisible if declared inside a more restricted block, such as inside a loop or an `if` statement.

This form of isolation is a powerful safeguard. It prevents accidental interference from other parts of the program, ensures that one function cannot inadvertently change the internal workings of another, and makes the program’s behaviour more predictable. By keeping variables confined to the places they are needed, you make your code easier to reason about, maintain, and debug.

To make this idea more concrete, let’s look at a short example in C. We’ll create a function with a variable that lives entirely inside it. You’ll see how the variable works perfectly within its own function, but becomes completely invisible the moment we step outside that function’s boundaries.

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

Here, the variable x is declared inside `print_number()`, which means it is created when the function starts and destroyed when the function ends. If we try to use `x` in `main()`, the compiler complains because `main()` has no knowledge of `x`—it lives in a separate, private workspace. This “one workspace per function” rule is one of the foundations of reliable programming: it keeps code modular, avoids accidental changes from unrelated parts of the program, and helps you reason about the behaviour of each function independently.

**Why Local Scope Is Good**

Local scope brings three key benefits to your code:

* Prevents bugs — a variable inside one function cannot accidentally overwrite or be overwritten by another function’s variable, even if they share the same name.
* Keeps code predictable — you always know exactly where a variable can be read or modified, making it easier to follow and reason about the program’s flow.
* Improves efficiency — the compiler can often keep local variables in CPU registers, and any stack space they use is automatically freed when the function returns.

By keeping variables confined to the smallest area where they’re needed, you reduce the chances of interference, make debugging easier, and help the compiler optimise performance.

**Why scope matters in driver development**

In FreeBSD device drivers, you’ll often manipulate temporary values—buffer sizes, indices, error codes, flags that are relevant only within a specific operation (e.g., probing a device, initialising a queue, handling an interrupt). Keeping these values local prevents cross-talk between concurrent paths and avoids subtle race conditions. In kernel space, small mistakes propagate fast; tight, local scope is your first line of defence.

**From Simple Scope to Real Kernel Code**

You’ve just seen how a local variable inside a small C program lives and dies within its function. Now, let’s step into a real FreeBSD driver and see exactly the same principle at work, but this time in code that interacts with actual hardware.

We’ll look at part of the VirtIO subsystem, which is used for virtual devices in environments like QEMU or bhyve. This example comes from the function `virtqueue_init_indirect()` that is located between the lines 230 and 271 in the file `sys/dev/virtio/virtqueue.c` in FreeBSD 14.3 source code, which sets up “indirect descriptors” for a virtual queue. Watch how variables are declared, used, and limited to the function’s own scope, just like in our earlier `print_number()` example. 

Note: I’ve added some extra comments to highlight what’s happening at each step.

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

Even though this is production-level kernel code, the principle is the same as in the tiny example we just saw. The variables `dev`, `dxp`, `i`, and `size` are all declared inside `virtqueue_init_indirect()` and exist only while this function is running. Once the function returns, whether it’s at the end or early via a return statement, those variables vanish, freeing their stack space for other uses.

Notice that this keeps things safe: the loop counter `i` can’t be accidentally reused in another part of the driver, and the `dxp` pointer is re-initialised for each call to the function. In driver development, this is a critical local scope that ensures that temporary work variables won’t collide with names or data in other parts of the kernel. The isolation you learned about in the simple `print_number()` example applies here in exactly the same way, just at a higher level of complexity and with real hardware resources involved.

**Common Beginner Mistakes (and How to Avoid Them)**

One of the quickest ways to get into trouble is to store the address of a local variable in a structure that outlives the function. Once the function returns, that memory is reclaimed and can be overwritten at any time, leading to mysterious crashes. Another issue is “over-sharing”, using too many global variables for convenience, which can cause unpredictable results if multiple execution paths modify them at the same time. And finally, be careful not to shadow variables (reusing a name inside an inner block), which can lead to confusion and hard-to-spot bugs.

**Wrapping Up and Moving Forward**

The lesson here is simple but powerful: local scope makes your code safer, easier to test, and more maintainable. In FreeBSD device drivers, it is the right tool for per-call, temporary data. Long-lived information should be stored in properly designed per-device structures, keeping your driver organised and avoiding accidental data sharing.

Now that you understand **where** a variable can be used, it is time to look at **how long** it exists. This is called **variable storage duration**, and it affects whether your data lives on the stack, in static storage, or on the heap. Knowing the difference is key to writing robust, efficient drivers, and that’s precisely where we are headed next.

## Variable Storage Duration

So far, you’ve learned where a variable can be used in your program, as well as its scope. But there’s another equally important property: how long the variable actually exists in memory. This is called its storage duration.

While scope is about visibility in the code, storage duration is about lifetime in memory. A variable’s storage duration determines:

* **When** the variable is created.
* **When** it is destroyed.
* **Where** it lives (stack, static storage, heap).

Understanding storage duration is critical in FreeBSD driver development because we often handle resources that must persist across function calls (like device state) alongside temporary values that must vanish quickly (like loop counters or temporary buffers).

### The Three Main Storage Durations in C

When you create a variable in C, you’re not just giving it a name and a value, you’re also deciding **how long that value will live in memory**. This “lifetime” is what we call the **storage** duration. Even two variables that look similar in the code can behave very differently depending on how long they stick around.

Let’s break down the three main types you’ll encounter, starting with the most common in day-to-day programming.

**Automatic Storage Duration (stack variables)**

Think of these as short-term helpers. They are born the moment a function starts running and disappear the instant the function finishes. You don’t have to create or destroy them manually; C takes care of that for you.

Automatic variables:

* Are declared inside functions without the `static` keyword.
* Are created when the function is called and destroyed when it returns.
* Live on the **stack**, a section of memory that’s automatically managed by the program.
* Are perfect for quick, temporary jobs like loop counters, temporary pointers, or small scratch buffers.

Because they vanish when the function ends, you can’t keep their address for later use; doing so leads to one of the most common beginner mistakes in C.

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

Now imagine a variable that doesn’t come and go with a function call, instead, it’s **always there** from the moment your program (or in kernel space, your driver module) loads until it ends. This is **static storage**.

Static variables:

* Are declared outside functions or inside functions with the `static` keyword.
* Are created **once** when the program/module starts.
* Remain in memory until the program/module ends.
* Live in a dedicated **static memory** area.
* Are great for things like per-device state structures or lookup tables that are needed throughout the program’s lifetime.

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

Sometimes you don’t know in advance how much memory you’ll need, or you need to keep something around even after the function that created it has finished. That’s where dynamic storage comes in: you request memory at runtime, and you decide when it goes away.

Dynamic variables:

* Are created explicitly at runtime with `malloc()`/`free()` in user space, or `malloc(9)`/`free(9)` in the FreeBSD kernel.
* Exist until you explicitly free them.
* Live in the **heap**, a pool of memory managed by the operating system or kernel.
* Are perfect for things like buffers whose size depends on hardware parameters or user input.

The flexibility comes with responsibility: forget to free them, and you’ll have a memory leak. Free them too soon, and you might crash the system by accessing invalid memory.

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

So far, we’ve looked at these storage durations in an abstract way. But concepts really sink in when you see them in the wild, inside a real FreeBSD driver or subsystem function. Kernel code often mixes these durations: a few automatic locals for temporary values, some static structures for persistent state, and carefully managed dynamic memory for resources that come and go during runtime.

To make this clearer, let’s walk through a real function from the FreeBSD 14.3 source tree. By following each variable and seeing how it’s declared, used, and eventually discarded or freed, you’ll gain an intuitive feel for how lifetime and scope interact in real-world kernel work.


| Duration  | Created                 | Destroyed           | Memory area    | Typical declarations                          | Good driver use cases                               | Common pitfalls                                 | FreeBSD APIs to know                |
| --------- | ----------------------- | ------------------- | -------------- | --------------------------------------------- | --------------------------------------------------- | ----------------------------------------------- | ----------------------------------- |
| Automatic | On function entry       | On function return  | Stack          | Local variables without `static`              | Scratch values in fast paths and interrupt handlers | Returning addresses of locals. Oversized locals | N/A                                 |
| Static    | When module loads       | When module unloads | Static storage | File scope variables or `static` inside funcs | Persistent device state. Constant tables. Tunables  | Hidden shared state. Missing locks on SMP       | `sysctl(9)` patterns for tunables   |
| Dynamic   | When you call allocator | When you free it    | Heap           | Pointers returned by allocators               | Buffers sized at probe time. Lifetime spans calls   | Leaks. Use after free. Double free              | `malloc(9)`, `free(9)`, `M_*` types |


### Real Example from FreeBSD 14.3

Before we move on, let’s look at how these storage duration concepts appear in production-quality FreeBSD code. Our example comes from the network interface subsystem, specifically from the `_if_delgroup_locked()` function in `sys/net/if.c` (lines 1474 to 1512 in FreeBSD 14.3). This function removes an interface from a named interface group, updates reference counts, and frees memory when the group becomes empty.

As in our earlier, simpler examples, you’ll see **automatic** variables created and destroyed entirely within the function, **dynamic** memory being released explicitly with `free(9)`, and, elsewhere in the same file, **static** variables that persist for the module’s entire lifetime. By walking through this function, you’ll see lifetime and scope management in action not just in an isolated snippet, but in the complex, interconnected world of the FreeBSD kernel.

Note: I’ve added some extra comments to highlight what’s happening at each step.

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

A little further down, the calls to `free(...)` deal with dynamic storage. The pointers passed to `free()` were created earlier in the driver’s life, often with `malloc()` during initialisation routines like `if_addgroup()`. Unlike stack variables, this memory stays around until the driver deliberately lets it go. Freeing it here tells the kernel, *“I’m done with this; you can reuse it for something else.”*

This function doesn’t use static variables directly, but in the same file (`if.c`), you will find examples like debugging flags declared with `YSCTL_INT` that live for as long as the kernel module is loaded. These variables keep their values across function calls and are a reliable place to store configuration or diagnostics that need to persist.

Each choice here is intentional.

* Automatic variables keep temporary state safe inside the function.
* Dynamic memory gives flexibility at runtime, allowing the driver to adjust and then clean up when done.
* Static storage, found elsewhere in the same codebase, supports persistent, shared information.

Put together, this is a clear, real-world example of how lifetime and visibility work hand in hand in FreeBSD driver code. It is not just theory from a C textbook, it is the day-to-day reality of writing drivers that are reliable, efficient, and safe to run in the kernel.

### Why Storage Duration Matters in FreeBSD Drivers

In kernel development, storage duration is not just an academic detail; it’s directly tied to system stability, performance, and even security. A wrong choice here can take down the entire operating system.

In FreeBSD drivers, the right storage duration ensures that data lives exactly as long as needed, no more and no less:

* **Automatic variables** are ideal for short-lived, private state, such as temporary values in an interrupt handler. They vanish automatically when the function ends, avoiding long-term clutter in memory.
* **Static variables** can safely store hardware state or configuration that must persist across calls, but they introduce shared state that may require locking in SMP systems to avoid race conditions.
* **Dynamic allocations** give you flexibility when buffer sizes depend on runtime conditions like device probing results, but they must be explicitly freed to avoid leaks and freeing too soon risks accessing invalid memory.

Mistakes with storage duration can be catastrophic in the kernel. Keeping a pointer to a stack variable beyond the function’s life is almost guaranteed to cause corruption. Forgetting to free dynamic memory ties up resources until a reboot. Overusing static variables can turn shared state into a performance bottleneck.

Understanding these trade-offs is not optional. In driver code, often triggered by hardware events in unpredictable contexts, correct lifetime management is a foundation for writing code that is safe, efficient, and maintainable.

### Common Beginner Mistakes

When you are new to C and especially to kernel programming, it is surprisingly easy to misuse storage duration without even realising it. One classic trap with automatic variables is returning the address of a local variable from a function. At first, it might seem harmless after all, the variable was right there a moment ago, but the moment the function returns, that memory is reclaimed for other uses. Accessing it later is like reading a letter you already burned; the result is undefined behaviour, and in the kernel, that can mean an instant crash.

Static variables can cause trouble differently. Because they persist across function calls, a value left over from a previous run of the function might influence the next run in unexpected ways. This is particularly dangerous if you assume that every call starts with a “clean slate.” In reality, static variables remember everything, even when you wish they wouldn’t.

Dynamic memory has its own set of hazards. Forgetting to `free()` something you allocated means the memory will be tied up until the system is restarted, a problem known as a memory leak. In kernel space, where resources are precious, a leak can slowly degrade the system. Freeing the same pointer twice is even worse, it can corrupt kernel memory structures and bring down the whole machine.

Being aware of these patterns early on helps you avoid them when working on real driver code, where the cost of a mistake is often far greater than in user-space programming.

### Wrapping Up

We have explored the three main storage durations in C: automatic, static, and dynamic. Each one has its place, and the right choice depends on how long you need the data to live and who should be able to see it. The safest general rule is to choose the smallest necessary lifetime for your variables. This limits their exposure, reduces the risk of unintended interactions, and often makes the compiler’s job easier.

In FreeBSD driver development, careful management of variable lifetimes is not optional; it is a fundamental skill. Done right, it helps you write code that is predictable, efficient, and resilient under load. With these principles in mind, you are ready to explore the next piece of the puzzle: understanding how variable linkage affects visibility across files and modules.

*Continue soon...*