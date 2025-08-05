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

Before we can start writing C code, we need to set up a working development environment. The good news? If you’re running FreeBSD, you already have most of what you need.

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

Typing long compile commands can get annoying as your programs grow. That’s where Makefiles come in handy.

continue...
