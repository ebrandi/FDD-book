---
title: "A Gentle Introduction to UNIX"
description: "This chapter provides a hands-on introduction to UNIX and FreeBSD basics."
author: "Edson Brandi"
date: "2025-08-23"
status: "complete"
part: 1
chapter: 3
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 120
---

# A Gentle Introduction to UNIX

Now that your FreeBSD system is installed and running, it's time to get comfortable living inside it. FreeBSD isn't just an operating system, it's part of a long tradition that started with UNIX more than fifty years ago.

In this chapter, we'll take our first real tour of the system. You'll learn how to navigate the filesystem, run commands in the shell, manage processes, and install applications. Along the way, you'll see how FreeBSD inherits the UNIX philosophy of simplicity and consistency, and why that matters for us as future driver developers.

Think of this chapter as your **survival guide** for working inside FreeBSD. Before we start diving into C code and kernel internals, you'll need to be comfortable moving around the system, manipulating files, and using the tools that every developer relies on daily.

By the end of this chapter, you won't just know *what UNIX is*; you'll be using FreeBSD confidently as both a user and an aspiring systems programmer.

## Reader Guidance: How to Use This Chapter

This chapter is not just something to skim, it's designed to be both a **reference** and a **hands-on bootcamp**. How long it takes depends on how you approach it:

- **Reading only:** About **2 hours** to go through the text and examples at a comfortable beginner's pace.
- **Reading + labs:** About **4 hours** if you pause to type and run each of the hands-on labs in your own FreeBSD system.
- **Reading + challenges:** About **6 hours or more** if you also complete the full set of 46 challenge exercises at the end.

Recommendation: Don't try to do everything in one sitting. Break the chapter into sections, and after each one, run the lab before moving on. Save the challenges for when you feel confident and want to test your mastery.

## Introduction: Why UNIX Matters

Before we start writing device drivers for FreeBSD, we need to pause and talk about the foundation they stand on: **UNIX**.

Every driver you'll ever write for FreeBSD, every system call you'll explore, every kernel message you'll read, they all make sense only when you understand the operating system they live in. For a beginner, the world of UNIX can feel mysterious, filled with odd commands and a very different philosophy compared to Windows or macOS. But once you learn its logic, you'll see it's not only approachable but also elegant and incredibly powerful.

This chapter is about giving you a **gentle introduction** to UNIX as it appears in FreeBSD. By the end, you'll feel comfortable navigating the system, working with files, running commands, managing processes, installing applications, and even writing small scripts to automate your tasks. These are everyday skills for any FreeBSD developer and absolutely essential before we dive into kernel development.

### Why Should You Learn UNIX Before Writing Drivers?

Think of it like this: if writing drivers is like building an engine, UNIX is the entire car around it. You need to know where the fuel goes, how the dashboard works, and what the controls do before you can safely swap parts under the hood.

Here are a few reasons why learning UNIX basics is essential:

- **Everything in UNIX is connected.** Files, devices, processes, they all follow consistent rules. Once you know these rules, the system becomes predictable.
- **FreeBSD is a direct descendant of UNIX.** The commands, filesystem layout, and overall philosophy are not add-ons; they are part of its DNA.
- **Drivers integrate with userland.** Even though your code will run in the kernel, it will interact with user programs, files, and processes. Understanding the userland environment helps you design drivers that feel natural and intuitive.
- **Debugging requires UNIX skills.** When your driver misbehaves, you'll rely on tools like `dmesg`, `sysctl`, and shell commands to figure out what's happening.

### What You Will Learn in This Chapter

By the end of this chapter, you will:

- Understand what UNIX is and how FreeBSD fits into its family.
- Be able to use the shell to run commands and manage files.
- Navigate the FreeBSD filesystem and know where things live.
- Manage users, groups, and file permissions.
- Monitor processes and system resources.
- Install and remove applications using FreeBSD's package manager.
- Automate tasks with shell scripts.
- Peek into FreeBSD internals with tools like `dmesg` and `sysctl`.

All along the way, I'll give you **hands-on labs** so you can practice. Reading about UNIX is not enough; you need to **touch the system**. Each lab will involve real commands you'll run on a FreeBSD installation, so by the time you reach the end of this chapter, you won't just understand UNIX, you'll be using it confidently.

### The Bridge to Device Drivers

Why are we spending an entire chapter on UNIX basics if this is a book about writing drivers? Because drivers don't exist in isolation. When you eventually load your own kernel module, you'll see it appear under `/dev`. When you test it, you'll use shell commands to read and write to it. When you debug it, you'll rely on system logs and monitoring tools.

So think of this chapter as laying down the **operating system literacy** you need before becoming a driver developer. Once you have it, everything else will feel less intimidating and far more logical.

### Wrapping Up

In this opening section, we looked at why UNIX matters for anyone who wants to write FreeBSD drivers. Drivers don't live in isolation; they exist inside a larger operating system that follows rules, conventions, and a philosophy inherited from UNIX. Understanding this foundation is what makes everything else, from using the shell to debugging drivers, logical instead of mysterious.

With that motivation in mind, it's time to ask the natural next question: **what exactly is UNIX?** To move forward, we'll take a closer look at its history, its guiding principles, and the key concepts that still shape FreeBSD today.

## What Is UNIX?

Before you can get comfortable using FreeBSD, it helps to understand what UNIX is and why it matters. UNIX isn't just a piece of software, it's a family of operating systems, a set of design choices, and even a philosophy that has shaped computing for more than fifty years. FreeBSD is one of its most important modern descendants, so learning UNIX is like studying the family tree to see where FreeBSD fits.

### A Brief History of UNIX

UNIX was born in **1969** at Bell Labs, when Ken Thompson and Dennis Ritchie created a lightweight operating system for a PDP-7 minicomputer. At a time when mainframes were huge, costly, and complex, UNIX stood out because it was **small, elegant, and designed for experimentation**.

The **1973 rewrite in C** was the turning point. For the first time, an operating system was portable: you could move UNIX to different hardware by recompiling it, not by rewriting everything from scratch. This was unheard of in the 1970s and changed the trajectory of system design forever.

**BSD at Berkeley** is the part of the story that leads directly to FreeBSD. Graduate students and researchers at the University of California, Berkeley, took AT&T's UNIX source code and extended it with modern features:

- **Virtual memory** (so programs weren't limited by physical RAM).
- **Networking** (the TCP/IP stack that still powers the internet today).
- **The C shell** with scripting and job control.

In the **1990s**, after legal disputes over UNIX source code were resolved, the FreeBSD Project was launched. Its mission: to carry the BSD tradition forward, freely and openly, for anyone to use, modify, and share.

**Today**, FreeBSD is a direct continuation of that lineage. It is not a UNIX imitation; it is UNIX heritage alive and well.

You might be thinking, *"Why should I care?"*. You should because when you peek into `/usr/src` or type commands like `ls` and `ps`, you're not just using software, you're benefiting from decades of problem-solving and craftsmanship, the work of thousands of developers who built and polished these tools long before you.

### The UNIX Philosophy

UNIX is not only a system; it's a **mindset**. Understanding its philosophy will make everything else, from basic commands to device drivers, feel more natural.

1. **Do one thing, and do it well.**
    Instead of giant, all-in-one programs, UNIX gives you focused tools.

   Example: `grep` only searches text. It doesn't open files, edit them, or format results; it leaves that to other tools.

2. **Everything is a file.**
    Files are not just documents; they are the way you interact with almost everything: devices, processes, sockets, logs.

   Analogy: Think of the entire system as a library. Every book, desk, and even the librarian's notebook is part of the same filing system.

3. **Build small tools, then combine them.**
    This is the genius of the **pipe operator (`|`)**. You take the output of one program and use it as the input for another.

   Example:

   ```sh
   ps -aux | grep ssh
   ```

   Here, one program lists all processes, and another filters only the ones related to SSH. Neither program knows about the other, but the shell glues them together.

4. **Use plain text whenever possible.**
    Text files are easy to read, edit, share, and debug. FreeBSD's `/etc/rc.conf` (system configuration) is just a plain text file. No binary registries, no proprietary formats.

When you start writing device drivers, you'll see this philosophy everywhere: your driver will expose a **simple interface under `/dev`**, behave predictably, and integrate smoothly with other tools.

### UNIX-like Systems Today

The word "UNIX" today refers less to a single operating system and more to a **family of UNIX-like systems**.

- **FreeBSD** - Your focus in this book. Used in servers, networking gear, firewalls, and embedded systems. Known for reliability and documentation. Many commercial appliances (routers, storage systems) silently run FreeBSD under the hood.
- **Linux** - Created in 1991, inspired by UNIX principles. Popular in data centers, embedded devices, and supercomputers. Unlike FreeBSD, Linux is not a direct UNIX descendant but shares the same interface and ideas.
- **macOS and iOS** - Built on Darwin, a BSD-based foundation. macOS is a UNIX-certified OS, meaning its command line tools behave like FreeBSD's. If you use a Mac, you already have a UNIX system.
- **Others** - Commercial variants like AIX, Solaris, or HP-UX still exist but are rare outside enterprise contexts.

Why this matters: Once you learn FreeBSD, you'll feel comfortable on almost any other UNIX-like system. The commands, filesystem layout, and philosophy all carry over.

### Key Concepts and Terms

Here are some essential UNIX terms you'll see throughout this book:

- **Kernel** - The heart of the OS. It manages memory, CPU, devices, and processes. Your drivers will live here.
- **Shell** - The program that interprets your commands. It's your main tool for talking to the system.
- **Userland** - Everything outside the kernel: commands, libraries, daemons. It's where you'll spend most of your time as a user.
- **Daemon** - A background service (like `sshd` for remote logins or `cron` for scheduled tasks).
- **Process** - A running program. Each command creates a process.
- **File descriptor** - A numeric handle that the kernel gives programs to work with files or devices. For example, 0 = standard input, 1 = standard output, 2 = standard error.

Tip: Don't worry about memorizing these yet. Think of them as characters you'll meet again later in the story. By the time you write a driver, you'll know them like old friends.

### How UNIX Differs from Windows

If you've mostly used Windows, the UNIX approach will feel different at first. Here are a few contrasts:

- **Drives vs. unified tree**
   Windows uses drive letters (`C:\`, `D:\`). UNIX has a single tree rooted at `/`. Disks and partitions are mounted into this tree.
- **Registry vs. text files**
   Windows centralizes settings in the Registry. UNIX uses plain-text configuration files under `/etc` and `/usr/local/etc`. You can open them with any text editor.
- **GUI focus vs. CLI focus**
   While Windows assumes a graphical interface, UNIX treats the command line as the primary tool. Graphical environments exist, but the shell is always available and powerful.
- **Permissions model**
   UNIX was multi-user from day one. Every file has permissions (read, write, execute) for the owner, group, and others. This makes security and sharing simpler and more consistent.

These differences explain why UNIX often feels "stricter" but also more transparent. Once you get used to it, the consistency becomes a huge advantage.

### Everyday UNIX in Your Life

Even if you've never logged into a FreeBSD system before, UNIX is already around you:

- Your Wi-Fi router or NAS may run FreeBSD or Linux.
- Netflix uses FreeBSD servers to deliver streaming video.
- Sony's PlayStation uses a FreeBSD-based OS.
- macOS and iOS are direct descendants of BSD UNIX.
- Android phones run Linux, another UNIX-like system.

Learning FreeBSD is not just about writing drivers, it's about learning the **language of modern computing**.

### Hands-On Lab: Your First UNIX Commands

Let's make this concrete. Open a terminal in the FreeBSD you installed in the previous chapter and try:

```sh
% uname -a
```

This prints system details: the OS, the system name, release version, kernel build, and machine type. On FreeBSD 14.x, you might see:

```
FreeBSD freebsd.edsonbrandi.com 14.3-RELEASE FreeBSD 14.3-RELEASE releng/14.3-n271432-8c9ce319fef7 GENERIC amd64
```

Now try the commands:

```sh
% date
% whoami
% hostname
```

- `date` - shows the current time and date.
- `whoami` - tells you which user account you're logged in as.
- `hostname` - shows the machine's network name.

Finally, a small experiment with UNIX's *"everything is a file"* idea:

```sh
% echo "Hello FreeBSD" > /tmp/testfile
% cat /tmp/testfile
```

You just created a file, wrote to it, and read it back. This is the same model you'll later use to talk to your own drivers.

### Wrapping Up

In this section, you learned that UNIX is not just an operating system but a family of ideas and design principles that shaped modern computing. You saw how FreeBSD fits into this history as a direct descendant of BSD UNIX, why its philosophy of small tools and plain text makes it powerful, and how many of the concepts you'll rely on as a driver developer, like processes, daemons, and file descriptors, have been part of UNIX since the beginning.

But knowing what UNIX is only gets us halfway. To really use FreeBSD, you need a way to **interact with it**. That's where the shell comes in, the command interpreter that lets you speak the system's language. In the next section, we'll start using the shell to run commands, explore the filesystem, and get hands-on experience with the tools that every FreeBSD developer depends on daily.

## The Shell: Your Window Into FreeBSD

Now that you know what UNIX is and why it matters, it's time to start **talking to the system**. The way you do that in FreeBSD (and other UNIX-like systems) is through the **shell**.

Think of the shell as both an **interpreter** and a **translator**: you type a command in human-readable form, and the shell passes it to the operating system to execute. It's the window between you and the UNIX world.

### What Is a Shell?

At its core, the shell is just a program, but a very special one. It listens to what you type, figures out what you mean, and asks the kernel to carry it out.

Some common shells include:

- **sh** - The original Bourne shell. Simple and reliable.
- **csh / tcsh** - The C shell and its enhanced version, with scripting features inspired by the C language. tcsh is FreeBSD's default for new users.
- **bash** - The Bourne Again Shell, very popular in Linux.
- **zsh** - A modern, user-friendly shell with many conveniences.

On FreeBSD 14.x, if you log in as a normal user, you'll probably be using **tcsh**. If you log in as the root administrator, you may see **sh**. Don't worry if you're not sure which shell you have, we'll cover how to check in just a moment.

Why this matters for driver developers: you'll use the shell constantly to compile, load, and test your drivers. Knowing how to navigate it is as important as knowing how to turn the ignition key in a car.

### How to Know Which Shell You're Using

FreeBSD comes with more than one shell, and you might notice slight differences between them, for example, the prompt might look different, or certain shortcuts might behave differently. Don't worry: the **core UNIX commands work the same** no matter which shell you're in. Still, it's helpful to know which shell you're currently using, especially if you later decide to write scripts or customize your environment.

Type:

```sh
% echo $SHELL
```

You'll see something like:

```sh
/bin/tcsh
```

or

```sh
/bin/sh
```

This tells you your default shell. You don't need to change it now; just be aware that shells may look slightly different but share the same basic commands.

**Hands-On Tip**
There's also a quick way to check which shell is running for your current process:

```sh
% echo $0
```

This may show `-tcsh`, `sh`, or something else. It's slightly different from `$SHELL`, because `$SHELL` tells you your **default shell** (the one you get when you log in), while `$0` tells you the **shell you're actually running right now**. If you ever start a different shell inside your session (for example by typing `sh` at the prompt), `$0` will reflect that.

### The Structure of a Command

Every shell command follows the same simple pattern:

```sh
command [options] [arguments]
```

- **command** - The program you want to run.
- **options** - Flags that change how it behaves (usually starting with `-`).
- **arguments** - The targets of the command, like filenames or directories.

Example:

```sh
% ls -l /etc
```

- `ls` = list directory contents.
- `-l` = option for "long format."
- `/etc` = argument (the directory to list).

This consistency is one of UNIX's strengths: once you learn the pattern, every command feels familiar.

### Essential Commands for Beginners

Let's explore the core commands you'll use constantly.

#### Navigating Directories

- **pwd** - Print Working Directory
   Shows where you are in the filesystem.

  ```sh
  % pwd
  ```

  Output:

  ```
  /home/dev
  ```

- **cd** - Change Directory
   Moves you into another directory.

  ```sh
  % cd /etc
  % pwd
  ```

  Output:

  ```
  /etc
  ```

- **ls** - List
   Shows the contents of a directory.

  ```sh
  % ls
  ```

  Output might include:

  ```
  rc.conf   ssh/   resolv.conf
  ```

**Tip**: Try `ls -lh` for human-readable file sizes.

#### Managing Files and Directories

- **mkdir** - Make Directory

  ```sh
  % mkdir projects
  ```

- **rmdir** - Remove Directory (only if empty)

  ```sh
  % rmdir projects
  ```

- **cp** - Copy

  ```sh
  % cp file1.txt file2.txt
  ```

- **mv** - Move (or rename)

  ```sh
  % mv file2.txt notes.txt
  ```

- **rm** - Remove (delete)

  ```sh
  % rm notes.txt
  ```

**Warning**: `rm` does not ask for confirmation. Once removed, the file is gone unless you have a backup. This is a common beginner pitfall.

#### Viewing File Contents

- **cat** - Concatenate and display file contents

  ```sh
  % cat /etc/rc.conf
  ```

- **less** - View file contents with scrolling

  ```sh
  % less /etc/rc.conf
  ```

  Use arrow keys or spacebar, press `q` to quit.

- **head / tail** - Show the beginning or end of a file, `-n` parameter specifies the number of lines you want to see

  ```sh
  % head -n 5 /etc/rc.conf
  % tail -n 5 /etc/rc.conf
  ```

#### Editing Files

Sooner or later, you'll need to edit a configuration file or a source file. FreeBSD ships with a few editors, each with different strengths:

- **ee (Easy Editor)**

  - Installed by default.
  - Designed to be beginner-friendly, with visible menus at the top of the screen.
  - To save, press **Esc**, then choose *"Leave editor"* → *"Save changes."*
  - Great choice if you've never used a UNIX editor before.

- **vi / vim**

  - The traditional UNIX editor, always available.
  - Extremely powerful, but it has a steep learning curve.
  - Beginners often get stuck because `vi` starts in *command mode* instead of insert mode.
  - To start typing text: press **i**, write your text, then press **Esc** followed by `:wq` to save and quit.
  - You don't need to master it now, but every sysadmin and developer eventually learns at least the basics of `vi`.

- **nano**

  - Not part of the FreeBSD base system, but can be installed easily running the following command while logged as root:

    ```sh
    # pkg install nano
    ```

  - Very beginner-friendly, with shortcuts listed at the bottom of the screen.

  - If you're coming from Linux distributions like Ubuntu, you may already know it.

**Tip for Beginners**
Start with `ee` to get comfortable editing files on FreeBSD. Once you're ready, learn the basics of `vi`, it will always be there for you, even in rescue environments or minimal systems where nothing else is installed.

##### **Hands-On Lab: Your First Edits**

1. Create and edit a new file with `ee`:

   ```sh
   % ee hello.txt
   ```

   Write a short line of text, save, and exit.

2. Try the same with `vi`:

   ```sh
   % vi hello.txt
   ```

   Press `i` to insert, type something new, then press `Esc` and type `:wq` to save and quit.

3. If you installed `nano`:

   ```sh
   % nano hello.txt
   ```

   Notice how the bottom line shows commands like `^O` for save and `^X` for exit.

##### **Common Beginner Pitfall: Stuck in `vi`**

Almost every UNIX beginner has faced this: you open a file with `vi`, start pressing keys, and nothing happens the way you expect. Worse, you can't figure out how to quit.

Here's what's happening:

- `vi` starts in **command mode**, not typing mode.
- To insert text, press **i** (insert).
- To return to command mode, press **Esc**.
- To save and quit: type `:wq` and press Enter.
- To quit without saving: type `:q!` and press Enter.

**Tip**: If you accidentally open `vi` and just want to escape, press **Esc**, type `:q!`, and hit Enter. That will exit without saving.

### Tips and Shortcuts

Once you get comfortable typing commands, you'll quickly discover that the shell has many built-in features to save time and reduce mistakes. Learning these early will make you feel at home much faster.

**Note about FreeBSD shells:**

- The **default login shell** for new users is usually **`/bin/tcsh`**, which supports tab completion, history navigation with arrow keys, and many interactive shortcuts.
- The more minimal **`/bin/sh`** shell is excellent for scripting and system use, but it does not provide conveniences like tab completion or arrow-key history out of the box.
- So if some shortcuts below don't seem to work, check which shell you are using (`echo $SHELL`).

#### Tab completion (tcsh)

 Start typing a command or filename and press `Tab`. The shell will try to complete it for you.

```sh
% cd /et<Tab>
```

Becomes:

```sh
% cd /etc/
```

If there's more than one match, press `Tab` twice to see a list of possibilities.
This feature is not available in `/bin/sh`.

#### Command history (tcsh)

 Press the **Up arrow** to recall the last command, and keep pressing it to walk further back in time. Press the **Down arrow** to move forward again.

```sh
% sysctl kern.hostname
```

You don't have to retype it, just hit the Up arrow and press Enter.
In `/bin/sh`, you don't get arrow-key navigation (but you can still run commands again with `!!`).

#### Wildcards (globbing)

 Works in *all* shells, including `/bin/sh`.

```sh
% ls *.conf
```

Lists all files that start with `host` and end with `.conf`.

```sh
% ls host?.conf
```

Matches files like `host1.conf`, `hostA.conf`, but not `hosts.conf`.

#### Editing on the command line (tcsh)

 In `tcsh` you can move the cursor left and right with arrow keys, or use shortcuts:

- **Ctrl+A** → Move to the beginning of the line.
- **Ctrl+E** → Move to the end of the line.
- **Ctrl+U** → Erase everything from the cursor to the start of the line.

- **Repeating commands quickly (all shells)**

  ```sh
  % !!
  ```

  Re-executes your last command.

  ```sh
  % !ls
  ```

  Repeats the last command that started with `ls`.

**Tip**:  If you want a friendlier interactive shell, stick with **`/bin/tcsh`** (the FreeBSD default for users). If you later want advanced customization, you can install shells like `bash` or `zsh` from packages or ports. But for scripting, always use **`/bin/sh`**, since it's guaranteed to be present and is the system's standard.

### Hands-On Lab: Navigating and Managing Files

Let's practice:

1. Go to your home directory:

   ```sh
   % cd ~
   ```

2. Create a new directory:

   ```sh
   % mkdir unix_lab
   % cd unix_lab
   ```

3. Create a new file:

   ```sh
   % echo "Hello FreeBSD" > hello.txt
   ```

4. View the file:

   ```sh
   % cat hello.txt
   ```

5. Make a copy:

   ```sh
   % cp hello.txt copy.txt
   % ls
   ```

6. Rename it:

   ```sh
   % mv copy.txt renamed.txt
   ```

7. Delete the renamed file:

   ```sh
   % rm renamed.txt
   ```

By completing these steps, you've just navigated the filesystem, created files, copied them, renamed them, and removed them, the daily bread and butter of UNIX work.

### Wrapping Up

The shell is your **gateway into FreeBSD**. Every interaction with the system, whether running commands, compiling code, or testing a driver, flows through it. In this section, you learned what the shell is, how commands are structured, and how to perform basic navigation and file management.

Next, we'll explore **how FreeBSD organizes its filesystem**. Understanding the layout of directories like `/etc`, `/usr`, and `/dev` will give you a mental map of the system, which is especially important when we start dealing with device drivers that live under `/dev`.

## The FreeBSD Filesystem Layout

In Windows, you may be used to drives like `C:\` and `D:\`. In UNIX and FreeBSD, there are no drive letters. Instead, everything lives in a **single tree of directories** that starts at the root `/`.

This is called a **hierarchical filesystem**. At the very top is `/`, and everything else branches out beneath it like folders inside folders. Devices, configuration files, and user data are all organized inside this tree.

Here's a simplified map:

```
/
├── bin       → Essential user commands (ls, cp, mv)
├── sbin      → System administration commands (ifconfig, shutdown)
├── etc       → Configuration files
├── usr
│   ├── bin   → Non-essential user commands
│   ├── sbin  → Non-essential system admin tools
│   ├── local → Software installed by pkg or ports
│   └── src   → FreeBSD source code
├── var       → Logs, mail, spools, temp runtime data
├── home      → User home directories
├── dev       → Device files
└── boot      → Kernel and boot loader
```

And here's a table with some of the most important directories you'll work with:

| Directory    | Purpose                                                |
| ------------ | ------------------------------------------------------ |
| `/`          | Root of the entire system. Everything begins here.     |
| `/bin`       | Essential command-line tools (used during early boot). |
| `/sbin`      | System binaries (like `init`, `ifconfig`).             |
| `/usr/bin`   | User command-line tools and programs.                  |
| `/usr/sbin`  | System-level tools used by administrators.             |
| `/usr/src`   | FreeBSD source code (kernel, libraries, drivers).      |
| `/usr/local` | Where packages and installed software go.              |
| `/boot`      | Kernel and boot loader files.                          |
| `/dev`       | Device nodes, files that represent devices.            |
| `/etc`       | System configuration files.                            |
| `/home`      | User home directories (like `/home/dev`).              |
| `/var`       | Log files, mail spools, runtime files.                 |
| `/tmp`       | Temporary files, cleared on reboot.                    |

Understanding this layout is critical for a driver developer because some directories, especially `/dev`, `/boot`, and `/usr/src`, are directly tied to the kernel and drivers. But even outside of those, knowing where things live helps you navigate confidently.

**Base system vs local software**: One key idea in FreeBSD is the separation between the base system and user-installed software. The base system: kernel, libraries, and essential tools live in `/bin`, `/sbin`, `/usr/bin`, and `/usr/sbin`. Everything you install later with pkg or ports goes into `/usr/local`. This separation keeps your core OS stable while letting you add and update software freely.

### Devices as Files: `/dev`

One of UNIX's most powerful ideas is that **devices appear as files** under `/dev`.

Examples:

- `/dev/null`: A "black hole" that discards anything you write to it.
- `/dev/zero`: Outputs an endless stream of zero bytes.
- `/dev/random`: Provides random data.
- `/dev/ada0`: Your first SATA disk.
- `/dev/da0`: A USB storage device.
- `/dev/tty`: Your terminal.

You can interact with these devices using the same tools you use for files:

```sh
% echo "test" > /dev/null
% head -c 10 /dev/zero | hexdump
```

Later in this book, when you create a driver, it will expose a file here, for example, `/dev/hello`. Writing to that file will actually run your driver's code inside the kernel.

### Absolute vs. Relative Paths

When navigating the filesystem, paths can be:

- **Absolute** - Start at the root `/`. Example: `/etc/rc.conf`
- **Relative** - Start from your current location. Example: `../notes.txt`

Example:

```sh
% cd /etc      # absolute path
% cd ..        # relative path: move up one directory
```

**Remember**: `/` always means the root of the system, while `.` means "here" and `..` means "one level up."

#### Example: Navigating with Absolute vs Relative Paths

Suppose your home directory contains this structure:

```
/home/dev/unix_lab/
├── docs/
│   └── notes.txt
├── code/
│   └── test.c
└── tmp/
```

- To open `notes.txt` with an **absolute path**:

  ```sh
  % cat /home/dev/unix_lab/docs/notes.txt
  ```

- To open it with a **relative path** from inside `/home/dev/unix_lab`:

  ```sh
  % cd /home/dev/unix_lab
  % cat docs/notes.txt
  ```

- Or, if you are already inside the `docs` directory:

  ```sh
  % cd /home/dev/unix_lab/docs
  % cat ./notes.txt
  ```

Absolute paths always work, no matter where you are, while relative paths depend on your current directory. As a developer, you'll often prefer absolute paths in scripts (more predictable) and relative paths when working interactively (faster to type).

### Hands-On Lab: Exploring the Filesystem

Let's practice exploring FreeBSD's layout:

1. Print your current location:

```sh
   % pwd
```

2. Go to the root directory and list its contents:

   ```sh
   % cd /
   % ls -lh
   ```

3. Peek into the `/etc` directory:

   ```sh
   % ls /etc
   % head -n 5 /etc/rc.conf
   ```

4. Explore `/var/log` and view system logs:

   ```sh
   % ls /var/log
   % tail -n 10 /var/log/messages
   ```

5. Check devices under `/dev`:

   ```sh
   % ls /dev | head
   ```

This lab gives you a "mental map" of the FreeBSD filesystem and shows how configuration files, logs, and devices are all organized in predictable places.

### Wrapping Up

In this section, you've learned that FreeBSD uses a **single, hierarchical filesystem** starting at `/`, with key directories dedicated to system binaries, configuration, logs, user data, and devices. You also saw how `/dev` treats devices as files, a powerful concept that you'll rely on when you write drivers.

But files and directories aren't just about structure; they're also about **who can access them**. UNIX is a multi-user system, and every file has an owner, a group, and permission bits that control what can be done with it. In the next section, we'll explore **users, groups, and permissions**, and you'll learn how FreeBSD keeps the system both secure and flexible.

## Users, Groups, and Permissions

One of the biggest differences between UNIX and systems like early Windows is that UNIX was designed from the beginning as a **multi-user operating system**. That means it assumes multiple people (or services) can use the same machine at once, and it enforces rules about who can do what.

This design is essential for security, stability, and collaboration, and as a driver developer, you'll need to understand it well, because permissions often control who can access your driver's device file.

### Users and Groups

Every person or service that uses FreeBSD does so under a **user account**.

- A **user** has a username, a numeric ID (UID), and a home directory.
- A **group** is a collection of users, identified by a group name and group ID (GID).

Each user belongs to at least one group, and permissions can be applied both to individuals and to groups.

You can see your current identity with:

   ```sh
% whoami
% id
   ```

Example output:

```
dev
uid=1001(dev) gid=1001(dev) groups=1001(dev), 0(wheel)
```

Here:

- Your username is `dev`.
- Your UID is `1001`.
- Your primary group is `dev`.
- You also belong to the `wheel` group, which allows access to administrative privileges (via `su` or `sudo`).

### File Ownership

In FreeBSD, every file and directory has an **owner** (a user) and a **group**.

Let's check with `ls -l`:

```sh
% ls -l hello.txt
```

Output:

```
-rw-r--r--  1 dev  dev  12 Aug 23 10:15 hello.txt
```

Breaking it down:

- `-rw-r--r--` = permissions (we'll cover in a moment).
- `1` = number of links (not important for now).
- `dev` = owner (the user who created the file).
- `dev` = group (the group associated with the file).
- `12` = file size in bytes.
- `Aug 23 10:15` = last modification time.
- `hello.txt` = filename.

So this file belongs to user `dev` and group `dev`.

### Permissions

Permissions control what users can do with files and directories. There are three categories of users:

1. **Owner** - the user who owns the file.
2. **Group** - members of the file's group.
3. **Others** - everyone else.

And three kinds of permission bits:

- **r** = read (can view contents).
- **w** = write (can modify or delete).
- **x** = execute (for programs or, in directories, the ability to enter them).

Example:

```
-rw-r--r--
```

This means:

- **Owner** = read + write.
- **Group** = read only.
- **Others** = read only.

So the owner can modify the file, but everyone else can only look at it.

### Changing Permissions

To modify permissions, you use the **chmod** command.

Two ways:

**Symbolic mode**

```sh
% chmod u+x script.sh
```

This adds execute permission (`+x`) for the user (`u`).

**Octal mode**

```sh
% chmod 755 script.sh
```

Here, numbers represent permissions:

- 7 = rwx
- 5 = r-x
- 0 = ---

So `755` means: owner = rwx, group = r-x, others = r-x.

### Changing Ownership

Sometimes you need to change who owns a file. Use `chown`:

   ```sh
% chown root:wheel hello.txt
   ```

Now the file is owned by root and the group wheel.

**Note**: Changing ownership usually requires administrator privileges.

### Practical Scenario: Project Directory

Suppose you're working on a project with a teammate, and you both need access to the same files. 

Here's how you'd set it up, run these commands as root:

1. Create a group called `proj`:

	```
   # pw groupadd proj
	```

2. Add both users to the group:

   ```
   # pw groupmod proj -m dev,teammate
   ```

3. Create a directory and assign it to the group:

   ```
   # mkdir /home/projdir
   # sudo chown dev:proj /home/projdir
   ```

4. Set group permissions so members can write:

   ```
   # chmod 770 /home/projdir
   ```

Now both users can work in `/home/projdir`, while others cannot access it.

This is exactly how UNIX systems enforce collaboration securely.

### Hands-On Lab: Permissions in Action

Let's practice:

1. Create a new file:

   ```sh
   % echo "secret" > secret.txt
   ```

2. Check its default permissions:

   ```sh
   % ls -l secret.txt
   ```

3. Remove read access for others:

   ```sh
   % chmod o-r secret.txt
   % ls -l secret.txt
   ```

4. Add execute permission for the user:

   ```sh
   % chmod u+x secret.txt
   % ls -l secret.txt
   ```

5. Try to change ownership (will require root):

   ```
   % sudo chown root secret.txt
   % ls -l secret.txt
   ```

Beware that `sudo` will ask for your password to execute the command `chown` above in the step 5.

With these commands, you've controlled access to files at a very fine level, a concept that applies directly when we create drivers, since drivers also have device files under `/dev` with ownership and permission rules. 

### Wrapping Up

In this section, you learned that FreeBSD is a **multi-user system** where every file has an owner, a group, and permission bits that control access. You saw how to inspect and change permissions, how to manage ownership, and how to set up collaboration safely with groups.

These rules may seem simple, but they are the backbone of FreeBSD's security model. Later, when you write drivers, your device files under `/dev` will also have ownership and permissions, controlling who can open and use them.

Next, we'll look at **processes**, the running programs that make the system alive. You'll learn how to see what's running, how to manage processes, and how FreeBSD keeps everything organized behind the scenes.

## Processes and System Monitoring

So far, you've learned how to navigate the filesystem and manage files. But an operating system isn't just about files on disk; it's about **programs running in memory**. These running programs are called **processes**, and understanding them is essential for both daily use and driver development.

### What Is a Process?

A process is a program in motion. When you run a command like `ls`, FreeBSD:

1. Loads the program into memory.
2. Assigns it a **process ID (PID)**.
3. Gives it resources like CPU time and memory.
4. Tracks it until it finishes or is stopped.

Processes are how FreeBSD manages everything that happens on your system. From the shell you type into, to daemons in the background, to your web browser, they're all processes.

**For driver developers**: When you write a driver, **processes in userland will talk to it**. Knowing how processes are created and managed helps you understand how drivers are used.

### Foreground vs. Background Processes

Usually, when you run a command, it runs in the **foreground**, meaning you can't do anything else in that terminal until it finishes.

Example:

   ```sh
% sleep 10
   ```

This command pauses for 10 seconds. During that time, your terminal is "blocked."

To run a process in the **background**, add an `&` at the end:

```sh
% sleep 10 &
```

Now you get your prompt back immediately, and the process runs in the background.

You can see background jobs with:

```sh
% jobs
```

And bring one back to the foreground:

```sh
% fg %1
```

(where `%1` is the job number in the list you see with `jobs`).

### Viewing Processes

To see which processes are running, use `ps`:

```
ps aux
```

Sample output:

```
USER   PID  %CPU %MEM  VSZ   RSS  TT  STAT STARTED    TIME COMMAND
root     1   0.0  0.0  1328   640  -  Is   10:00AM  0:00.01 /sbin/init
dev   1024   0.0  0.1  4220  2012  -  S    10:05AM  0:00.02 -tcsh
dev   1055   0.0  0.0  1500   800  -  R    10:06AM  0:00.00 ps aux
```

Here:

- `PID` = process ID.
- `USER` = who started it.
- `%CPU` / `%MEM` = resources being used.
- `COMMAND` = the program running.

#### Watching Processes and System Load with `top`

While `ps` gives you a snapshot of processes at a single moment, sometimes you want a **live view** of what's happening on your system. That's where the `top` command comes in.

```sh
% top
```

This opens a continuously updating display of system activity. By default, it refreshes every 2 seconds. To quit, press **q**.

The `top` screen shows:

- **Load averages** (how busy your system is, averaged over 1, 5, and 15 minutes).
- **Uptime** (how long the system has been running).
- **CPU usage** (user, system, idle).
- **Memory and swap usage**.
- **A list of processes**, sorted by CPU usage, so you can see which programs are working hardest.

**Example of `top` Output (simplified):**  .

```text
last pid:  3124;  load averages:  0.06,  0.12,  0.14                                            up 0+20:43:11  11:45:09
17 processes:  1 running, 16 sleeping
CPU:  0.0% user,  0.0% nice,  0.0% system,  0.0% interrupt,  100% idle
Mem: 5480K Active, 1303M Inact, 290M Wired, 83M Buf, 387M Free
Swap: 1638M Total, 1638M Free

  PID USERNAME    THR PRI NICE   SIZE    RES STATE    C   TIME    WCPU COMMAND
 3124 dev           1  20    0    15M  3440K CPU3     3   0:00   0.03% top
 2780 dev           1  20    0    23M    11M select   0   0:00   0.01% sshd-session
  639 root          1  20    0    14M  2732K select   2   0:02   0.00% syslogd
  435 root          1  20    0    15M  4012K select   2   0:04   0.00% devd
  730 root          1  20    0    14M  2612K nanslp   0   0:00   0.00% cron
  697 root          2  20    0    18M  4388K select   3   0:00   0.00% qemu-ga
 2778 root          1  20    0    23M    11M select   1   0:00   0.00% sshd-session
  726 root          1  20    0    23M  9164K select   3   0:00   0.00% sshd
  760 root          1  68    0    14M  2272K ttyin    1   0:00   0.00% getty
```

Here we can see:

- The system has been up for more than a day.
- Load averages are very low (system is idle).
- CPU is mostly idle.
- Memory is mostly free.
- The `yes` command (a test program that just outputs "y" endlessly) is using almost all the CPU.

##### Quick check with `uptime`

If you don't need the full detail of `top`, you can use:

```
% uptime
```

Which shows something like:

```
 3:45PM  up 2 days,  4:11,  2 users,  load averages:  0.32,  0.28,  0.25
```

This tells you:

- Current time.
- How long the system has been running.
- How many users are logged in.
- Load averages (1, 5, 15 minutes).

**Tip**: Load averages are a quick way to see if your system is overloaded. On a single-CPU system, a load average of `1.00` means the CPU is fully busy. On a 4-core system, `4.00` means all cores are fully loaded.

**Hands-On Lab: Watching the System**

1. Run `uptime` and note your system's load averages.

2. Open two terminals on your FreeBSD machine.

3. On the first terminal, start a busy process:

   ```sh
   % yes > /dev/null &
   ```

4. On the second terminal run `top` to see how much CPU the `yes` process is using.

5. Stop the `yes` command with `kill %1` or `pkill yes`, or just by executing a `ctrl+c` on the first terminal

6. Run `uptime` again, notice how the load average is slightly higher than before, but it will drop back down over time.

### Stopping Processes

Sometimes a process misbehaves or needs to be stopped. You can use:

- **kill** - Send a signal to a process.

	```sh
	% kill 1055
	```

  (replace 1055 with the actual PID).

- **kill -9** - Force a process to terminate immediately.

  ```sh
  % kill -9 1055
  ```

Use `kill -9` only when necessary, because it doesn't give the program a chance to clean up.

When you use `kill`, you're not literally *"killing"* a process with brute force; you're sending it a **signal**. Signals are messages the kernel delivers to processes.

- By default, `kill` sends **SIGTERM (signal 15)**, which politely asks the process to terminate. Well-behaved programs clean up and exit.
- If a process refuses, you can send **SIGKILL (signal 9)** with `kill -9 PID`. This forces the process to stop immediately, without cleanup.
- Another useful one is **SIGHUP (signal 1)**, often used to tell daemons (background services) to reload their configuration.

Try this:

  ```sh
% sleep 100 &
% ps aux | grep sleep
% kill -15 <PID>   # try with SIGTERM first
% kill -9 <PID>    # if still running, use SIGKILL
  ```

As a future driver developer, this distinction matters. Your code might need to handle termination gracefully, cleaning up resources instead of leaving the kernel in an unstable state.

#### Process Hierarchy: Parents and Children

Every process in FreeBSD (and in UNIX systems in general) has a **parent** process that started it. For example, when you type a command in the shell, the shell process is the parent, and the command you run becomes its child.

You can view this relationship using `ps` with custom columns:

```sh
% ps -o pid,ppid,command | head -10
```

Example of the output (simplified):

```yaml
  PID  PPID COMMAND
    1     0 /sbin/init
  534     1 /usr/sbin/cron
  720   534 /bin/sh
  721   720 sleep 100
```

Here you can see:

- Process **1** is `init`, the ancestor of all processes.
- `cron` was started by `init`.
- A `sh` shell process was started by `cron`.
- The `sleep 100` process was started by the shell.

Understanding process hierarchy matters when debugging: if a parent dies, its children may be **adopted by `init`**. Later, when you work on drivers, you'll see how system daemons and services create and manage child processes that interact with your code.

### Monitoring System Resources

FreeBSD provides simple commands to check system health:

- **df -h** - Show disk usage.

	```sh
	% df -h
	```

  Example:

  ```yaml
  Filesystem  Size  Used  Avail Capacity  Mounted on
  /dev/ada0p2  50G   20G    28G    42%    /
  ```

- **du -sh** - Show size of a directory.

  ```
  % du -sh /var/log
  ```

- **freebsd-version** - Show OS version.

  ```
  % freebsd-version
  ```

- **sysctl** - Query system information.

  ```sh
  % sysctl hw.model
  % sysctl hw.ncpu
  ```

Output might show your CPU model and number of cores.

Later, when writing drivers, you'll often use `dmesg` and `sysctl` to monitor how your driver interacts with the system.

### Hands-On Lab: Working with Processes

Let's practice:

1. Run a sleep command in the background:

      ```sh
      % sleep 30 &
      ```

2. Check running jobs:

   ```sh
   % jobs
   ```

3. List processes:

   ```sh
   % ps aux | grep sleep
   ```

4. Stop the process:

   ```sh
   % kill <PID>
   ```

5. Run `top` and watch system activity. Press `q` to quit.

6. Check system info:

   ```sh
   % sysctl hw.model
   % sysctl hw.ncpu
   ```

### Wrapping Up

In this section, you learned that processes are the living, running programs inside FreeBSD. You saw how to start them, move them between foreground and background, inspect them with `ps` and `top`, and stop them with `kill`. You also explored basic system monitoring commands to check disk, CPU, and memory usage.

Processes are essential because they make the system alive, and as a driver developer, the programs that use your driver will always run as processes.

But monitoring processes is only part of the story. To do real work, you'll need more tools than those included in the base system. FreeBSD provides a clean and powerful way to install and manage extra software, from simple utilities like `nano` to large applications like web servers. In the next section, we'll dive into the **FreeBSD package system and the Ports Collection**, so you can extend your system with the software you need.

## Installing and Managing Software

FreeBSD is designed as a lean and reliable operating system. Out of the box, you get a rock-solid **base system**, the kernel, system libraries, essential tools, and configuration files. Everything beyond that, editors, compilers, servers, monitoring tools, and even desktop environments, is considered **third-party software**, and FreeBSD provides two excellent ways to install it:

1. **pkg** - The binary package manager: fast, simple, and convenient.
2. **The Ports Collection** - A massive source-based build system that allows fine-tuned customization.

Together, they give FreeBSD one of the most flexible and powerful software ecosystems in the UNIX world.

### Binary Packages with pkg

The `pkg` tool is FreeBSD's modern package manager. It gives you access to **tens of thousands of prebuilt applications** maintained by the FreeBSD ports team.

When you install a package with `pkg`, here's what happens:

- The tool fetches a **binary package** from the FreeBSD mirrors.
- Dependencies are downloaded automatically.
- Files are installed under `/usr/local`.
- The package database tracks what was installed, so you can update or remove it later.

#### Common Commands

- Update the package repository:

   ```sh
  % sudo pkg update
  ```

- Search for software:

  ```sh
  % sudo pkg search htop
  ```

- Install software:

  ```sh
  % sudo pkg install htop
  ```

- Upgrade all packages:

  ```sh
  % sudo pkg upgrade
  ```

- Remove software:

  ```sh
  % sudo pkg delete htop
  ```

For beginners, `pkg` is the fastest and safest way to get software installed.

### The FreeBSD Ports Collection

The **Ports Collection** is one of FreeBSD's crown jewels. It is a **giant tree of build recipes** (called "ports") located under `/usr/ports`. Each port contains:

- A **Makefile** describing how to fetch, patch, configure, and build the software.
- Checksums for verifying integrity.
- Metadata about dependencies and licensing.

When you build software from ports, FreeBSD downloads the source code from the original project's site, applies FreeBSD-specific patches, and compiles it locally on your system.

#### Why Use Ports?

So why would anyone bother building from source when prebuilt packages are available?

- **Customization** - Many applications have optional features. With ports, you can choose exactly what to enable or disable during compilation.
- **Optimization** - Advanced users may want to compile with flags tuned for their hardware.
- **Bleeding-edge options** - Sometimes new features are available in ports before they make it into binary packages.
- **Consistency with pkg** - Ports and packages share the same underlying infrastructure. In fact, packages are built from ports by the FreeBSD build cluster.

#### Getting and Exploring the Ports Tree

The Ports Collection lives under `/usr/ports`, but on a fresh FreeBSD system this directory may not exist yet. Let's check:

```sh
% ls /usr/ports
```

If you see categories such as `archivers`, `editors`, `net`, `security`, `sysutils`, and `www`, then Ports is installed. If the directory is missing, you'll need to fetch the Ports tree yourself.

#### Installing the Ports Collection with Git

The official and recommended way is to use **Git**:

1. Make sure `git` is installed:

   ```sh
   % sudo pkg install git
   ```

2. Clone the official Ports repository:

   ```sh
   % sudo git clone https://git.FreeBSD.org/ports.git /usr/ports
   ```

   This will create `/usr/ports` and fill it with the entire Ports Collection. The initial clone can take some time, since it contains thousands of applications.

3. To update the ports tree later, just run:

   ```sh
   % cd /usr/ports
   % sudo git pull
   ```

There is also an older tool called `portsnap`, but **Git is the modern, recommended method** because it keeps your tree directly in sync with the FreeBSD project's repository.

#### Browsing the Ports

Once Ports is installed, explore it:

```sh
% cd /usr/ports
% ls
```

You'll see files and categories such as:

```
CHANGES         UIDs            comms           ftp             mail            portuguese      x11
CONTRIBUTING.md UPDATING        converters      games           math            print           x11-clocks
COPYRIGHT       accessibility   databases       german          misc            russian         x11-drivers
GIDs            arabic          deskutils       graphics        multimedia      science         x11-fm
Keywords        archivers       devel           hebrew          net             security        x11-fonts
MOVED           astro           dns             hungarian       net-im          shells          x11-servers
Makefile        audio           editors         irc             net-mgmt        sysutils        x11-themes
Mk              benchmarks      emulators       japanese        net-p2p         textproc        x11-toolkits
README          biology         filesystems     java            news            ukrainian       x11-wm
Templates       cad             finance         korean          polish          vietnamese
Tools           chinese         french          lang            ports-mgmt      www
```

Each category has subdirectories for specific applications. For example:

```sh
% cd /usr/ports/sysutils/memdump
% ls
```

Here you'll find files like `Makefile`, `distinfo`, `pkg-descr` and possibly a `files/` directory. These are the "ingredients" FreeBSD uses to build the application: the `Makefile` defines the process, `distinfo` ensures integrity, `pkg-descr` describes what this software does, and `files/` contains any FreeBSD-specific patches.

#### Building from Ports

Example: installing `memdump` from ports.

```sh
% cd /usr/ports/sysutils/memdump
% sudo make install clean
```

During the build, you may see a menu of options, such as enabling sensors or colours, installing documentation, etc. This is where ports shine, you control what features are compiled.

The `make install clean` process does three things:

- **install** - builds and installs the program.
- **clean** - removes temporary build files.

#### Mixing Ports and Packages

A common question: *Can I mix packages and ports?*

Yes, they're compatible, since both are built from the same source tree. However, if you rebuild something from ports with custom options, you should be careful not to accidentally overwrite it with a binary package update later.

Many users install most things with `pkg`, but use ports for specific applications where customization is important.

### Where Installed Software Goes

Both `pkg` and ports install third-party software under `/usr/local`. This keeps them separate from the base system.

Typical locations:

- **Binaries** → `/usr/local/bin`
- **Libraries** → `/usr/local/lib`
- **Configuration** → `/usr/local/etc`
- **Man pages** → `/usr/local/man`

Try:

```sh
% which nano
```

Output:

```
/usr/local/bin/nano
```

This confirms that nano came from packages/ports, not from the base system.

### Practical Example: Installing vim and htop

Let's try both methods.

#### Using pkg

```sh
% sudo pkg install vim htop
```

Run them:

```sh
% vim test.txt
% htop
```

#### Using Ports

```sh
% cd /usr/ports/sysutils/htop
% sudo make install clean
```

Run it:

```sh
% htop
```

Notice how the ports version may ask you about optional features during build, while pkg installs with defaults.

### Hands-On Lab: Managing Software

1. Update your package repository:

	```sh
	% sudo pkg update
	```

2. Install lynx with pkg:

   ```sh
   % sudo pkg install lynx
   % lynx https://www.freebsd.org
   ```

3. Search for bsdinfo:

   ```sh
   % pkg search bsdinfo
   ```

4. Install bsdinfo from ports:

   ```sh
   % cd /usr/ports/sysutils/bsdinfo
   % sudo make install clean
   ```

5. Run bsdinfo to confirm it is now installed:

   ```sh
   % bsdinfo
   ```

6. Remove nano:

   ```sh
   % sudo pkg delete nano
   ```

You've now installed, run, and removed software with both pkg and ports, two complementary methods that give FreeBSD its flexibility.

### Wrapping Up

In this section, you learned how FreeBSD handles third-party software:

- The **pkg system** gives you fast, easy binary installations.
- The **Ports Collection** offers source-based flexibility and customization.
- Both methods install under `/usr/local`, keeping the base system separate and clean.

Understanding this ecosystem is key to FreeBSD culture. Many administrators install common tools with `pkg` and turn to ports when they need fine-grained control. As a developer, you'll appreciate both approaches: pkg for convenience, and ports when you want to see exactly how software is built and integrated.

But applications are only part of the story. The FreeBSD **base system**, the kernel and core utilities also need regular updates to stay secure and reliable. In the next section, we'll learn how to use `freebsd-update` to keep the operating system itself current, so you always have a solid foundation to build on.

## Keeping FreeBSD Up to Date

One of the most important habits you can develop as a FreeBSD user is keeping your system up to date. Updates fix security issues, squash bugs, and sometimes add support for new hardware. Unlike the command `pkg update && pkg upgrade`, which update your applications, **`freebsd-update` command is used to update the base operating system itself**, including the kernel and core utilities.

Keeping your system current ensures you're running FreeBSD safely and gives you the same solid foundation other developers are building on.

### Why Updates Matter

- **Security:** Like any software, FreeBSD occasionally has security vulnerabilities. Updates patch these quickly.
- **Stability:** Bug fixes improve reliability, which is critical if you're developing drivers.
- **Compatibility:** Updates bring support for new CPUs, chipsets, and other hardware.

Don't think of updates as optional. They're part of responsible system administration.

### The `freebsd-update` Tool

FreeBSD makes updating simple with the `freebsd-update` tool. It works by:

1. **Fetching** information about available updates.
2. **Applying** binary patches to your system.
3. If needed, **rebooting** into the updated kernel.

This is much easier than rebuilding the system from source (which we'll learn later, when we need that level of control).

### The Update Workflow

Here's the standard process:

1. **Fetch available updates**

   ```sh
   % sudo freebsd-update fetch
   ```

   This contacts the FreeBSD update servers and downloads any security patches or bug fixes for your version.

2. **Review changes**
    After fetching, `freebsd-update` may show you a list of configuration files that would be modified.
    Example:

   ```yaml
   The following files will be updated as part of updating to 14.1-RELEASE-p3:
   /bin/ls
   /sbin/init
   /etc/rc.conf
   ```

   Don't panic! This doesn't mean your system is broken, it just means some files will be updated.

   - If system configuration files like `/etc/rc.conf` have changed in the base system, you'll be asked to review differences.
   - `freebsd-update` uses a merge tool to show side-by-side changes.
   - For beginners: if you're not sure, it's usually safe to **accept the default (keep your local version)**. You can always read `/var/db/freebsd-update` logs later.

**Tip:** If you're uncomfortable merging config files at this point, you can skip changes and check them manually later.

3. **Install updates**

   ```sh
   % sudo freebsd-update install
   ```

   This step applies the updates that were downloaded.

   - If the update includes only userland programs (like `ls`, `cp`, libraries), you're done.
   - If the update includes a **kernel patch**, you'll be asked to **reboot** after installation.

### Example Session

Here's what a normal update might look like:

```sh
% sudo freebsd-update fetch
Looking up update.FreeBSD.org mirrors... 3 mirrors found.
Fetching metadata signature for 14.3-RELEASE from update1.FreeBSD.org... done.
Fetching metadata index... done.
Fetching 1 patches..... done.
Applying patches... done.
The following files will be updated as part of updating to 14.3-RELEASE-p1:
    /bin/ls
    /bin/ps
    /sbin/init
% sudo freebsd-update install
Installing updates... done.
```

If the kernel was updated:

```sh
% sudo reboot
```

After reboot, your system is fully patched.

### Kernel Updates with `freebsd-update`

One of the powerful things about `freebsd-update` is that it can update the kernel itself. You don't have to rebuild it manually unless you want to run a custom kernel (we'll cover that later in the book).

This means that for most users, staying secure and current is just a matter of running `fetch` + `install` regularly.

### Upgrading to a New Release with `freebsd-update`

In addition to applying security and bug fix patches, `freebsd-update` can also upgrade your system to a **new FreeBSD release**. For example, if you are running **FreeBSD 14.2** and want to upgrade to **14.3**, the process is straightforward.

The workflow has three steps:

1. **Fetch upgrade files**

   ```sh
   % sudo freebsd-update upgrade -r 14.3-RELEASE
   ```

   Replace `14.3-RELEASE` with the release you want to upgrade to.

2. **Install the new components**

   ```sh
   % sudo freebsd-update install
   ```

   This installs the first stage of updates. If the kernel was updated, you will need to reboot:

   ```sh
   % sudo reboot
   ```

3. **Repeat install**
    After reboot, run the install step again to finish updating the rest of the system:

   ```sh
   % sudo freebsd-update install
   ```

At the end, you'll be running the new release. You can confirm with:

```sh
% freebsd-version
```

**Tip**: Release upgrades can sometimes involve configuration file merges (just like security updates). If in doubt, keep your local versions, you can always compare with the new defaults stored under `/var/db/freebsd-update/`.

And remember, it's also a good idea to update your **packages** after a release upgrade, since they are built against the new system libraries:

```sh
% sudo pkg update
% sudo pkg upgrade
```

### Hands-On Lab: Running Your First Update

1. Check your current FreeBSD version:

   ```sh
   % freebsd-version -kru
   ```

   - `-k` → kernel
   - `-r` → running
   - `-u` → userland

2. Run `freebsd-update fetch` to see if updates are available.

3. Carefully read any messages about configuration file merges. If unsure, choose to **keep your version**.

4. Run `freebsd-update install` to apply updates.

5. If the kernel was updated, reboot:

   ```sh
   % sudo reboot
   ```

**Common Beginner Pitfall: Fear of Config File Merges**

When `freebsd-update` asks you to merge changes, it can look intimidating, with lots of text, plus/minus symbols, and prompts. Don't worry.

- If in doubt, keep your local version of files like `/etc/rc.conf` or `/etc/hosts`.
- The system will still work.
- You can always inspect the new default files later (they're stored in `/var/db/freebsd-update/`).

With time, you'll get comfortable resolving these merges, but in the beginning, **choosing to keep your configuration is the safe route**.

### Wrapping Up

With just two commands, `freebsd-update fetch` and `freebsd-update install`, you now know how to keep your FreeBSD base system patched and secure. This process takes only a few minutes but ensures your environment is safe and reliable for development work.

Later, when we begin working on the kernel and writing drivers, we'll also learn how to build and install a custom kernel from source. But for now, you already have the essential knowledge to maintain your system like a pro.

And since checking for updates is something you may want to do regularly, wouldn't it be nice if the system could take care of some of these chores for you automatically? That's exactly what we'll look at next: **scheduling and automation** with tools like `cron`, `at`, and `periodic`.

## Scheduling and Automation

One of UNIX's greatest strengths is that it was designed to let the computer handle repetitive tasks for you. Instead of waiting until midnight to run a backup or logging in every morning to start a monitoring script, you can tell FreeBSD:

> *"Run this command for me at this time, every day, forever."*

This not only saves time but also makes your system more reliable. In FreeBSD, the main tools for this are:

1. **cron** - for recurring tasks, like backups or monitoring.
2. **at** - for one-time jobs you want to schedule later.
3. **periodic** - FreeBSD's built-in system for routine maintenance tasks.

### Why Automate Tasks?

Automation matters because enhance our:

- **Consistency** - A job scheduled with cron will always run, even if you forget.
- **Efficiency** - Instead of manually repeating commands, you write them once.
- **Reliability** - Automation helps avoid mistakes. Computers don't forget to rotate logs on Sunday night.
- **System maintenance** - FreeBSD itself relies heavily on cron and periodic to keep the system healthy (rotate logs, update databases, run security checks).

### cron: The Automation Workhorse

The `cron` daemon runs continuously in the background. Every minute, it checks a list of scheduled jobs (stored in crontabs) and runs those that match the current time.

Each user has their own **crontab**, and the system has a global one. This means you can schedule personal tasks (like cleaning files in your home directory) without touching system jobs.

### Understanding the crontab Format

The crontab format has **five fields** that describe *when* to run a job, followed by the command itself:

   ```yaml
minute   hour   day   month   weekday   command
   ```

- **minute**: 0–59
- **hour**: 0–23 (24-hour clock)
- **day**: 1–31
- **month**: 1–12
- **weekday**: 0–6 (0 = Sunday, 6 = Saturday)

A mnemonic to help you remember: *"My Hungry Dog Must Wait."* (Minute, Hour, Day, Month, Weekday)

#### Examples of cron jobs

- Run every day at midnight:

	```
	0 0 * * * /usr/bin/date >> /home/dev/midnight.log
	```

- Run every 15 minutes:

  ```
  */15 * * * * /home/dev/scripts/check_disk.sh
  ```

- Run every Monday at 8 AM:

  ```
  0 8 * * 1 echo "Weekly meeting" >> /home/dev/reminder.txt
  ```

- Run at 3:30 AM on the first of every month:

  ```
  30 3 1 * * /usr/local/bin/backup.sh
  ```

### Editing and Managing Crontabs

To edit your personal crontab:

  ```
crontab -e
  ```

This opens your crontab in the default editor (`vi` or `ee`).

To list your jobs:

```
crontab -l
```

To remove your crontab:

```
crontab -r
```

### Where Do Logs Go?

When cron runs a job, its output (stdout and stderr) is sent by **email** to the user who owns the job. On FreeBSD, these emails are delivered locally and stored in `/var/mail/username`.

You can also redirect output to a log file to make things easier:

```
0 0 * * * /home/dev/backup.sh >> /home/dev/backup.log 2>&1
```

Here:

- `>>` appends output to `backup.log`.
- `2>&1` redirects error messages (stderr) into the same file.

This way, you always know what your cron jobs did, even if you don't check system mail.

### at: One-Time Scheduling

Sometimes you don't want a recurring job, you just want something to run later, once. That's where **at** comes in. 

The usage is pretty simple, let's see some examples:

- Run a command 10 minutes from now:

```sh
  echo "echo Hello FreeBSD > /home/dev/hello.txt" | at now + 10 minutes
```

- Run a command tomorrow at 9 AM:

  ```sh
  echo "/usr/local/bin/htop" | at 9am tomorrow
  ```

Jobs scheduled with `at` are queued and run exactly once. You can list them with `atq` and remove them with `atrm`.

### periodic: FreeBSD's Maintenance Helper

FreeBSD comes with a built-in housekeeping system called **periodic**. It's a framework of shell scripts that handle routine maintenance tasks for you, so you don't have to remember them manually.

These tasks run automatically at **daily, weekly, and monthly intervals**, thanks to entries already configured in the system-wide cron file `/etc/crontab`. This means a freshly installed FreeBSD system already takes care of many chores without you lifting a finger.

#### Where the Scripts Live

The scripts are organized in directories under `/etc/periodic`:

```
/etc/periodic/daily
/etc/periodic/weekly
/etc/periodic/monthly
/etc/periodic/security
```

- **daily/** - jobs that run every day (log rotation, security checks, database updates).
- **weekly/** - jobs that run once a week (like updating the locate database).
- **monthly/** - jobs that run once a month (like monthly accounting reports).
- **security/** - additional checks focused on system security.

#### What periodic Does by Default

Some examples of the jobs included out of the box:

- **Security checks** - looks for setuid binaries, insecure file permissions, or known vulnerabilities.
- **Log rotation** - compresses and archives logs under `/var/log` so they don't grow forever.
- **Database updates** - rebuilds helper databases, like the one used by the `locate` command.
- **Temporary file cleanup** - removes leftovers in `/tmp` and other cache directories.

After they run, periodic scripts usually send a summary of their results to the **root user's mailbox** (read it by running `mail` as root).

**Common Beginner Pitfall: "Nothing Happened!"**

Many new FreeBSD users run their system for a few days, knowing periodic is supposed to run jobs daily, but they never see any output and assume it didn't work. In reality, periodic's reports are sent to the **root user's mail**, not displayed on screen.

To read them, log in as root and run:

```
# mail
```

Press Enter to open the mailbox and view the reports. You can quit the mail program by typing `q`.

**Tip:** If you prefer to receive these reports in your normal user's inbox, you can configure mail forwarding in `/etc/aliases` so that root's mail is redirected to your user account.

#### Running periodic Manually

You don't have to wait for cron to trigger them. You can run the full sets of jobs manually:

```sh
% sudo periodic daily
% sudo periodic weekly
% sudo periodic monthly
```

Or run just one script directly, for example:

```sh
% sudo /etc/periodic/security/100.chksetuid
```

#### Customizing periodic with `periodic.conf`

Periodic is not a black box. Its behavior is controlled through `/etc/periodic.conf` and `/etc/periodic.conf.local`.

**Best practice**: never edit the scripts themselves. Instead, override their behavior in `periodic.conf`, this keeps your changes safe when FreeBSD updates the base system.

Here are some common options you might use:

- **Enable or disable jobs**

  ```
  daily_status_security_enable="YES"
  daily_status_network_enable="NO"
  ```

- **Control log handling**

  ```
  daily_clean_hoststat_enable="YES"
  weekly_clean_pkg_enable="YES"
  ```

- **Enable locate database update**

  ```
  weekly_locate_enable="YES"
  ```

- **Control tmp cleanup**

  ```
  daily_clean_tmps_enable="YES"
  daily_clean_tmps_days="3"
  ```

- **Security reports**

  ```
  daily_status_security_inline="YES"
  daily_status_security_output="mail"
  ```

To see all available options, use the command `man periodic.conf`

#### Discovering All Available Checks

By now you know periodic runs daily, weekly, and monthly jobs, but you may wonder: *what exactly are all these checks, and what do they do?*

There are several ways to explore them:

1. **List the scripts directly**

   ```sh
   % ls /etc/periodic/daily
   % ls /etc/periodic/weekly
   % ls /etc/periodic/monthly
   % ls /etc/periodic/security
   ```

   You'll see files with names like `100.clean-disks` or `480.leapfile-ntpd`, the script names are descriptive and will give you an idea about what the script does. The numbers help control the order in which they run.

2. **Read the documentation**

   The man pages `periodic(8)` and `periodic.conf(5)` explain many of the available scripts and their options. For example:

   ```
   man periodic.conf
   ```

   Gives you a summary of configuration variables and what they control.

3. **Check the script headers**
    Open any script in `/etc/periodic/*/` with `less` and read the first few comment lines. They usually contain a human-readable explanation of the script's purpose.

This means you never have to guess what periodic is doing; you can always inspect the scripts, preview their behaviour, or read the official documentation.

#### Why This Matters for Developers

For everyday users, periodic keeps the system tidy and secure without extra effort. But as a developer, you may later want to:

- Add a **custom periodic script** to test your driver or monitor its health once a day.
- Rotate or clean up custom log files created by your driver.
- Run automated integrity checks (e.g., verify your driver's device node exists and responds).

By hooking into periodic, you leverage the same framework FreeBSD itself uses for its own housekeeping.

**Hands-On Lab: Exploring and Customizing periodic**

1. List the available daily scripts:

   ```sh
   % ls /etc/periodic/daily
   ```

2. Run them manually:

   ```sh
   % sudo periodic daily
   ```

3. Open `/etc/periodic.conf` (create it if it doesn't exist) and add:

   ```sh
   weekly_locate_enable="YES"
   ```

4. Preview what the weekly jobs will do:

   ```sh
   % sudo periodic weekly
   ```

5. Trigger the weekly jobs and then try:

   ```sh
   % locate passwd
   ```

### Hands-On Lab: Automating Tasks

1. Schedule a job to run every minute for testing:

```sh
   % crontab -e
   */1 * * * * echo "Hello from cron: $(date)" >> /home/dev/cron_test.log
```

2. Wait a few minutes and check the file:

   ```sh
   % tail -n 5 /home/dev/cron_test.log
   ```

3. Schedule a one-time job with `at`:

   ```sh
   % echo "date >> /home/dev/at_test.log" | at now + 2 minutes
   ```

   Check later:

   ```sh
   % cat /home/dev/at_test.log
   ```

4. Run a periodic task manually:

   ```sh
   % sudo periodic daily
   ```

   You'll see reports on log files, security, and system status.

### Common Pitfalls for Beginners

- Forgetting to set **full paths**. Cron jobs don't use the same environment as your shell, so always use full paths (`/usr/bin/ls` instead of just `ls`).
- Forgetting to redirect output. If you don't, results may be mailed to you silently.
- Overlapping jobs. Be careful not to schedule jobs that conflict or run too frequently.

### Why This Matters for Driver Developers

You might wonder why we're spending time on cron jobs and scheduled tasks. The answer is that automation is **a developer's best friend**. When you begin writing device drivers, you'll often want to:

- Schedule automatic tests of your driver (for example, checking whether it loads and unloads cleanly every night).
- Rotate and archive kernel logs to keep track of driver behavior over time.
- Run periodic diagnostics that interact with your driver's `/dev` node and record results for analysis.

By mastering cron and periodic now, you'll already know how to set up these background routines later, saving yourself time and catching bugs early.

### Wrapping Up

In this section, you learned how FreeBSD automates tasks using three main tools:

- **cron** for recurring jobs,
- **at** for one-time scheduling,
- **periodic** for built-in system maintenance.

You practiced creating jobs, checked their output, and learned how FreeBSD itself relies on automation to stay healthy.

Automation is powerful, but sometimes you need to go beyond fixed schedules. You might want to chain commands together, use loops, or add logic to decide what happens. That's where **shell scripting** comes in. In the next section, we'll write your first scripts and see how to create custom automation tailored to your needs.

## Introduction to Shell Scripting

You have learned to run commands one by one. Shell scripting lets you **save those commands into a reusable program**. On FreeBSD, the native and recommended shell for scripting is **`/bin/sh`**. This shell follows the POSIX standard and is available on every FreeBSD system.

> **Important note for Linux users**
>  On many Linux distributions, examples use **bash**. On FreeBSD, **bash is not part of the base system**. You can install it with `pkg install bash`, where it will live under `/usr/local/bin/bash`. For portable, dependency-free scripts on FreeBSD, use `#!/bin/sh`.

We will build this section progressively: shebang and execution, variables and quoting, conditions, loops, functions, working with files, return codes, and basic debugging. Every example script below is **fully commented** so a complete beginner can follow it.

### 1) Your first script: shebang, make it executable, run it

Create a file named `hello.sh`:

```sh
#!/bin/sh
# hello.sh   a first shell script using FreeBSD's native /bin/sh
# Print a friendly message with the current date and the active user.

# 'date' prints the current date and time
# 'whoami' prints the current user
echo "Hello from FreeBSD!"
echo "Date: $(date)"
echo "User: $(whoami)"
```

**Tip: What Does `#!` (Shebang) Mean?**

The first line of this script is:

```
#!/bin/sh
```

This is called the **shebang line**. The two characters `#!` tell the system *which program should interpret the script*.

- `#!/bin/sh` means: "run this script using the **sh** shell."
- On other systems you might also see `#!/bin/tcsh`, `#!/usr/bin/env python3`, or `#!/usr/bin/env bash`.

When you make a script executable and run it, the system looks at this line to decide which interpreter to use. Without it, the script may fail or behave differently depending on your login shell.

**Rule of thumb**: Always include a shebang line at the top of your scripts. On FreeBSD, `#!/bin/sh` is the safest and most portable choice.

Now let's make the script executable and run it:

```sh
% chmod +x hello.sh       # give the user execute permission
% ./hello.sh              # run it from the current directory
```

If you get "Permission denied", you forgot `chmod +x`.
If you get "Command not found", you probably typed `hello.sh` without `./` and the current directory is not included in system `PATH`.

**Tip**: Don't feel pressured to master all scripting features at once. Start small, write a 2–3 line script that prints your username and the date. Once you're comfortable, add conditions (`if`), then loops, then functions. Shell scripting is like LEGO: build one block at a time.

### 2) Variables and quoting

Shell variables are untyped strings. Assign with `name=value` and reference with `$name`. There must be **no spaces** around `=`.

```sh
#!/bin/sh
# vars.sh   demonstrate variables and proper quoting

name="dev"
greeting="Welcome"
# Double quotes preserve spaces and expand variables.
echo "$greeting, $name"
# Single quotes prevent expansion. This prints the literal characters.
echo '$greeting, $name'

# Command substitution captures output of a command.
today="$(date +%Y-%m-%d)"
echo "Today is $today"
```

Common beginner pitfalls:

- Using spaces around `=`: `name = dev` is an error.
- Forgetting quotes when variables may contain spaces. Use `"${var}"` as a habit.

### 3) Exit status and short circuit operators

Every command returns an **exit status**. Zero means success. Nonzero means error. The shell lets you chain commands using `&&` and `||`.

```sh
#!/bin/sh
# status.sh   show exit codes and conditional chaining

# Try to list a directory that exists. 'ls' should return 0.
ls /etc && echo "Listing /etc succeeded"

# Try something that fails. 'false' always returns nonzero.
false || echo "Previous command failed, so this message appears"

# You can test the last status explicitly using $?
echo "Last status was $?"
```

### 4) Tests and conditions: `if`, `[ ]`, files and numbers

Use `if` with the `test` command or its bracket form `[ ... ]`. There must be spaces inside the brackets.

```sh
#!/bin/sh
# ifs.sh   demonstrate file and numeric tests

file="/etc/rc.conf"

# -f tests if a regular file exists
if [ -f "$file" ]; then
  echo "$file exists"
else
  echo "$file does not exist"
fi

num=5
if [ "$num" -gt 3 ]; then
  echo "$num is greater than 3"
fi

# String tests
user="$(whoami)"
if [ "$user" = "root" ]; then
  echo "You are root"
else
  echo "You are $user"
fi
```

Useful file tests:

- `-e` exists
- `-f` regular file
- `-d` directory
- `-r` readable
- `-w` writable
- `-x` executable

Numeric comparisons:

- `-eq` equal
- `-ne` not equal
- `-gt` greater than
- `-ge` greater or equal
- `-lt` less than
- `-le` less or equal

### 5) Loops: `for` and `while`

Loops let you repeat work over files or lines of input.

```sh
#!/bin/sh
# loops.sh   for and while loops in /bin/sh

# A 'for' loop over pathnames. Always quote expansions to handle spaces safely.
for f in /etc/*.conf; do
  echo "Found conf file: $f"
done

# A 'while' loop to read lines from a file safely.
# The 'IFS=' and 'read -r' avoid trimming spaces and backslash escapes.
count=0
while IFS= read -r line; do
  count=$((count + 1))
done < /etc/hosts
echo "The /etc/hosts file has $count lines"
```

Arithmetic in POSIX sh uses `$(( ... ))` for simple integer math.

### 6) Case statements for tidy branching

`case` is great when you have several patterns to match.

```sh
#!/bin/sh
# case.sh   handle options with a case statement

action="$1"   # first command line argument

case "$action" in
  start)
    echo "Starting service"
    ;;
  stop)
    echo "Stopping service"
    ;;
  restart)
    echo "Restarting service"
    ;;
  *)
    echo "Usage: $0 {start|stop|restart}" >&2
    exit 2
    ;;
esac
```

### 7) Functions to organize your script

Functions keep code readable and reusable.

```sh
#!/bin/sh
# functions.sh - Demonstrates using functions and command-line arguments in a shell script.
#
# Usage:
#   ./functions.sh NUM1 NUM2
# Example:
#   ./functions.sh 5 7
#   This will output: "[INFO] 5 + 7 = 12"

# A simple function to print informational messages
say() {
  # "$1" represents the first argument passed to the function
  echo "[INFO] $1"
}

# A function to sum two integers
sum() {
  # "$1" and "$2" are the first and second arguments
  local a="$1"
  local b="$2"

  # Perform arithmetic expansion to add them
  echo $((a + b))
}

# --- Main script execution starts here ---

# Make sure the user provided two arguments
if [ $# -ne 2 ]; then
  echo "Usage: $0 NUM1 NUM2"
  exit 1
fi

say "Beginning work"

# Call the sum() function with the provided arguments
result="$(sum "$1" "$2")"

# Print the result in a nice format
say "$1 + $2 = $result"
```

### 8) A practical example: a tiny backup script

This script creates a timestamped archive of a directory into `~/backups`. It uses only base utilities available on FreeBSD Base System.

```sh
#!/bin/sh
# backup.sh   create a timestamped tar archive of a directory
# Usage: ./backup.sh /path/to/source
# Notes:
#  - Uses /bin/sh so it runs on a clean FreeBSD 14.x install.
#  - Creates ~/backups if it does not exist.
#  - Names the archive sourcebasename-YYYYMMDD-HHMMSS.tar.gz

set -eu
# set -e: exit immediately if any command fails
# set -u: treat use of unset variables as an error

# Validate input
if [ $# -ne 1 ]; then
  echo "Usage: $0 /path/to/source" >&2
  exit 2
fi

src="$1"

# Verify that source is a directory
if [ ! -d "$src" ]; then
  echo "Error: $src is not a directory" >&2
  exit 3
fi

# Prepare destination directory
dest="${HOME}/backups"
mkdir -p "$dest"

# Build a safe archive name using only the last path component
base="$(basename "$src")"
stamp="$(date +%Y%m%d-%H%M%S)"
archive="${dest}/${base}-${stamp}.tar.gz"

# Create the archive
# tar(1) is in the base system. The flags mean:
#  - c: create  - z: gzip  - f: file name  - C: change to directory
tar -czf "$archive" -C "$(dirname "$src")" "$base"

echo "Backup created: $archive"
```

Run it:

```sh
% chmod +x backup.sh
% ./backup.sh ~/directory_you_want_to_backup
```

You will find the archive under `~/backups`.

### 9) Working with temporary files safely

Never hardcode names like `/tmp/tmpfile`. Use `mktemp(1)` from the base system.

```sh
#!/bin/sh
# tmp_demo.sh   create and clean a temporary file safely

set -eu

tmpfile="$(mktemp -t myscript)"
# Arrange cleanup on exit for success or error
cleanup() {
  [ -f "$tmpfile" ] && rm -f "$tmpfile"
}
trap cleanup EXIT

echo "Temporary file is $tmpfile"
echo "Hello temp" > "$tmpfile"
echo "Contents: $(cat "$tmpfile")"
```

`trap` schedules a function to run when the script exits, which prevents stale files.

### 10) Debugging your scripts

- `set -x` prints each command before executing it. Add it near the top and remove once fixed.
- `echo` progress messages so the user knows what is happening.
- Check return codes and handle failures explicitly.
- Log to a file by redirecting output: `mycmd >> ~/my.log 2>&1`.

Example:

```sh
#!/bin/sh
# debug_demo.sh   show simple tracing

# set -x comment to enable verbose trace:
set -x

echo "Step 1"
ls /etc >/dev/null

echo "Step 2"
date
```

### 11) Edge cases and script compatibility

On occasion you may encounter edge cases that can make scripting more difficult. For example strange file names. UNIX allows for a filename to be made of any ascii characters except for the forward slash `/` which is used to demarcate directories and subdirectors, and the null terminator `\0` (more on the null terminator in Chapter 04). This means that unexpected characters can such as the new line character `\n` can be found in filenames. Let's create one now and experiment with our scripts.

In your home directory create a file with a filename containing a newline character

```sh
% touch $'first_line\nsecond_line.txt'
```

If we run the `ls` command after creating the file we will see that it has a strange name:

```sh
% ls
first_line?second_line.txt
```

Why does the filename have a question mark in the middle? Because the file's name is actually saved as two lines thanks to the `\n` character we added when creating the file:

```
first_line
second_line.txt
```

The question mark appears in the name because the 'ls' command tries to output all file names onto a single line. It substitutes the new line character for a question mark.

Let's see how this effects our scripts. This small script will go through our home directory and state whenever it finds a file.

```sh
#!/bin/sh
# list.sh - list the files in the user's home directory
#
# Usage:
#   ./list.sh
# A simple script that lists and counts the number of files inside of the user's home directory

set -eu

cd "${HOME}"

find . -maxdepth 1 -type f ! -name ".*" -print |
{
  count=0
  while IFS= read -r f; do
    # String the leading "./" from path
    fname=${f#./}
    echo "File found: '$fname'"
    count=$((count + 1))
  done
  echo "Total files found: $count"
}
```

Running the script, we will see that it gets confused by the name:

```sh
% ./list.sh
File found: 'first_line'
File found: 'second_line.txt'
Total files found: 2
```

What is going on? The -print flag in the `find` expression is outputs one path per line. Because this strange file has a newline character in its name -print gives two outputs. As a result our script thinks that the file's strange name is actually two files. To try to solve this we can instead use the -print0 flag which will pipe the names whenever it encounters a null terminator.

```sh
#!/bin/sh
# list2.sh - list the files in the user's home directory
#
# Usage:
#   ./list2.sh
# A simple script that lists and counts the number of files inside of the user's home directory, outputting the file names with a null terminator

set -eu

cd "${HOME}"

find . -maxdepth 1 -type f ! -name ".*" -print0 |
{
  count=0
  while IFS= read -r f; do
    # String the leading "./" from path
    fname=${f#./}
    echo "File found: '$fname'"
    count=$((count + 1))
  done
  echo "Total files found: $count"
}
```

However, when we run this we will encounter a different problem

```sh
% ./list2.sh
File found: 'first_line'
Total files found: 1
```

The script is correctly counting the number of files in the home directory, but it is not correctly reading the name. In the find expression the -print0 flag now gives a single output whenever it encounters a null terminator instead of a new line character (remember, UNIX names cannot contain a null terminator). This gives a single output for the file name, but this single output is not being read correctly by the script. Inside of our while loop we use the expression `IFS= read -r` which tells the loop to run when the Internal Field Separator variable reads in a new line. Our revised find expression provides a single output demarcated by a null terminator, but the read command will read until the end of a line. We will need to update our while loop to change the demarcation to a null terminator.

```sh
#!/bin/sh
# list3.sh - list the files in the user's home directory
#
# Usage:
#   ./list3.sh
# A simple script that lists and counts the number of files inside of the user's home directory, outputting the file names with a null terminator and reading with a null terminator demarcation.

set -eu

cd "${HOME}"

find . -maxdepth 1 -type f ! -name ".*" -print0 |
{
  count=0
  # -d '' tells read to demarcate when it encounters a null character
  while IFS= read -r -d '' f; do
    # String the leading "./" from path
    fname=${f#./}
    echo "File found: '$fname'"
    count=$((count + 1))
  done
  echo "Total files found: $count"
}
```

Running our newest revision we encounter a different error and our script does not function at all

```sh
% ./list3.sh
read: Illegal option -d
Total files found: 0
```

What is happening? It is saying that -d is an illegal operation and that our new code to demarcate at '' is not allowed. If we search online we can see many scripts that have the flags -r -d for the `read` command. What is happening is that many of these scripts are using a different shell which can have different shell builtin commands. The command `read` used in our script is not an executable binary the same way some of our other commands like `ls` are. Instead `read` is a command built into the shell itself. As a result, different shells can have different builtin commands with different flags. The -d flag is not apart of the traditional **sh** shell, but is instead a part of the **bash** version of `read`.

The Bourne Again Shell or **bash** is commonly used on many unix-like systems and is installed as the default on many, but not all, Linux distributions. Its scripting language is a superset of that of the classic Bourne shell meaning that most if not all scripts written for the **sh** shell will run on the **bash** shell, but as **bash** builds upon **sh** it means that it is not backwards compatible. There are many commands and flags included in **bash** that do not work on on the the **sh** shell. Lets update our script one more time changing the shebang to use bash.

If you have not already done so, you can install **bash** with `pkg install bash`.

Now we can update the shebang to use the _bash_ binary. Note that as it is not a part of the base system it will live under a different location in the filesystem.

```sh
#!/usr/local/bin/sh
# list4.sh - list the files in the user's home directory
#
# Usage:
#   ./list4.sh
# A simple script that lists and counts the number of files inside of the user's home directory, outputting the file names with a null terminator and reading with a null terminator demarcation. Updated to use the bash shell

set -eu

cd "${HOME}"

find . -maxdepth 1 -type f ! -name ".*" -print0 |
{
  count=0
  # -d '' tells read to demarcate when it encounters a null character
  while IFS= read -r -d '' f; do
    # String the leading "./" from path
    fname=${f#./}
    echo "File found: '$fname'"
    count=$((count + 1))
  done
  echo "Total files found: $count"
}
```

Running the script with the updated shebang will let us use the features of **bash** not included in **sh**.

```sh
% ./list4.sh
File found: 'first_line
second_line.txt'
Total files found: 1
```

The script now outputs the correct number of files found and with the correct file name, including the line break we inserted when creating the file.

Because of the popularity of **bash** it is common to find documentation for commands and flags that will run on **bash** but not **sh**. It is not unusual for developers to write scripts developed for _bash_ rather than for the traditional **sh**. However, this results in a dependency and requires a shell that may not be installed on the system by default. This is also the cause when writing scripts for other popular shells such as **zsh**, **ksh**, and **dash** that expand beyond the POSIX standards set by the traditional Bourne shell. For maximum POSIX compatibility it is best to write and run scripts on `/bin/sh`.

### 12) Putting it together: organize downloads by type

This small utility sorts files in `~/Downloads` into subfolders by extension. It demonstrates loops, case, tests and safety checks. Like the previous script, it requires that we use the **bash** shell.

```sh
#!/bin/sh
# organize_downloads.sh - Tidy ~/Downloads by file extension
#
# Usage:
#   ./organize_downloads.sh
#
# Creates subdirectories like Documents, Images, Audio, Video, Archives, Other
# and moves matched files into them safely.

set -eu

# Bash command to allow pipeline commands to run in current shell process
# and allow pipeline commands to update values of variable
shopt -s lastpipe

downloads="${HOME}/Downloads"

# Ensure the Downloads directory exists
if [ ! -d "$downloads" ]; then
  echo "Downloads directory not found at $downloads" >&2
  exit 1
fi

cd "$downloads"

# Create target folders if missing
mkdir -p Documents Images Audio Video Archives Other

# Use find to safely list all regular (non-hidden) files
# -print0 ensures null-delimited results, safe for strange filenames
count=0
find . -maxdepth 1 -type f ! -name ".*" -print0 |
while IFS= read -r -d '' f; do
  # Strip leading "./" from path
  fname=${f#./}

  # Convert filename extension to lowercase for matching
  lower=$(printf '%s' "$fname" | tr '[:upper:]' '[:lower:]')

  case "$lower" in
    *.pdf|*.txt|*.md|*.doc|*.docx)  dest="Documents" ;;
    *.png|*.jpg|*.jpeg|*.gif|*.bmp) dest="Images" ;;
    *.mp3|*.wav|*.flac)             dest="Audio" ;;
    *.mp4|*.mkv|*.mov|*.avi)        dest="Video" ;;
    *.zip|*.tar|*.gz|*.tgz|*.bz2)   dest="Archives" ;;
    *)                              dest="Other" ;;
  esac

  echo "Moving '$fname' -> $dest/"
  mv -n -- "$fname" "$dest/"   # -n prevents overwriting existing files
  count=$((count + 1))
done

if [ $count -eq 0 ]; then
  echo "No files to organize."
else
  echo "Done. Organized $count file(s)."
fi
```

### Hands-on Lab: three mini tasks

1. **Write a logger**
    Create `logger.sh` that appends a timestamped line to `~/activity.log` with the current directory and user. Run it, then view the log with `tail`.
2. **Check disk space**
    Create `check_disk.sh` that warns when root filesystem usage exceeds 80 percent. Use `df -h /` and parse the percentage with `${var%%%}` style trimming or a simple `awk`. Exit with status 1 if above the threshold so cron can alert you.
3. **Wrap your backup**
    Create `backup_cron.sh` that calls `backup.sh` from earlier and logs output to `~/backup.log`. Add a crontab entry to run it daily at 3 AM. Remember to use full paths inside the script.

All scripts should start with `#!/bin/sh`, contain comments explaining each step, use quotes around variable expansions, and handle errors where sensible.

### Common beginner pitfalls and how to avoid them

- **Using bash features in `#!/bin/sh` scripts.** Stick to POSIX constructs. If you require bash, say so in the shebang and remember it is under `/usr/local/bin/bash` on FreeBSD.
- **Forgetting to quote variables.** Use `"${var}"` to prevent word splitting and globbing surprises.
- **Assuming the same environment under cron.** Always use full paths and redirect output to a log file.
- **Hardcoding temporary file names.** Use `mktemp` and `trap` to clean up.
- **Spaces around `=` in assignments.** `name=value` is correct. `name = value` is not.

### Wrapping up

In this section you learned the **native FreeBSD way** to automate work with portable scripts that run on any clean FreeBSD install. You can now write small programs with `/bin/sh`, handle arguments, test conditions, loop through files, define functions, use temporary files safely, and debug issues with simple tools. In your driver journey, scripts will help you repeat tests, gather logs, and package builds reliably.

But remember: you don't need to memorize every construct or command option. Part of being productive in UNIX is knowing where to **find the right information at the right time**. Every developer, from beginner to expert, constantly looks up man pages, handbooks, and online resources.

That's exactly what we'll cover next. In the following section, you'll learn how to use FreeBSD's built-in documentation, the famous FreeBSD Handbook, and the community around the project. These resources will become your companions as you continue your journey into device driver development.

## Seeking Help and Documentation in FreeBSD

No one, not even the most experienced developer, remembers every command, option, or system call. The real strength of a UNIX system like FreeBSD is that it ships with **excellent documentation** and has a supportive community that can help when you get stuck.

In this section, we'll explore the main ways to get information: **man pages, the FreeBSD Handbook, online resources, and the community**. By the end, you'll know exactly where to look when you have a question, whether it's about using `ls` or writing a device driver.

### The Power of man Pages

The **manual pages**, or **man pages**, are the built-in reference system for UNIX. Every command, system call, library function, configuration file, and kernel interface has a man page.

You read them with the `man` command, for example:

```
% man ls
```

This opens the documentation for `ls`, the command to list directory contents. Use the spacebar to scroll, `q` to quit.

#### Man Page Sections

FreeBSD organizes man pages into numbered sections. The same name may exist in multiple sections, so you specify which one you want.

- **1** - User commands (e.g., `ls`, `cp`, `ps`)
- **2** - System calls (e.g., `open(2)`, `write(2)`)
- **3** - Library functions (C standard library, math functions)
- **4** - Device drivers and special files (e.g., `null(4)`, `random(4)`)
- **5** - File formats and conventions (`passwd(5)`, `rc.conf(5)`)
- **7** - Miscellaneous (protocols, conventions)
- **8** - System administration commands (e.g., `ifconfig(8)`, `shutdown(8)`)
- **9** - Kernel developer interfaces (critical for driver writers!)

Example:

```sh
% man 2 open      # system call open()
% man 9 bus_space # kernel function for accessing device registers
```

#### Man Section 9: The Kernel Developer's Manual

Most FreeBSD users live in section **1** (user commands) and administrators spend a lot of time in section **8** (system management). But as a driver developer, you'll spend much of your time in **section 9**.

Section 9 contains the **kernel developer interfaces** documentation for functions, macros, and subsystems that are only available inside the kernel.

Some examples:

- `man 9 device` - Overview of device driver interfaces.
- `man 9 bus_space` - Accessing hardware registers.
- `man 9 mutex` - Synchronization primitives for the kernel.
- `man 9 taskqueue` - Scheduling deferred work in the kernel.
- `man 9 malloc` - Memory allocation inside the kernel.

Unlike section 2 (system calls) or section 3 (libraries), these are **not available in user space**. They are part of the kernel itself, and you'll use them when writing drivers and kernel modules.

Think of section 9 as the **developer's API manual for the FreeBSD kernel**.

#### Hands-On Preview

You don't need to understand all the details yet, but you can take a peek:

```sh
% man 9 device
% man 9 bus_dma
% man 9 sysctl
```

You'll see that the style is different from user command man pages: these are focused on **kernel functions, structures, and usage examples**.

Later in this book, we'll constantly refer to section 9 as we introduce new kernel features. Consider it your most important companion for the road ahead.

#### Searching the man Pages

If you don't know the exact command name, use the `-k` flag (equivalent to `apropos`):

```
man -k network
```

This shows every man page related to networking.

Another example:

```
man -k disk | less
```

This will show you tools, drivers, and system calls related to disks.

### The FreeBSD Handbook

The **FreeBSD Handbook** is the official, comprehensive guide to the operating system.

You can read it online:

https://docs.freebsd.org/en/books/handbook/

The Handbook covers:

- Installing FreeBSD
- System administration
- Networking
- Storage and filesystems
- Security and jails
- Advanced topics

The Handbook is an **excellent complement to this book**. While we focus on device driver development, the Handbook gives you broad system knowledge that you can always come back to.

#### Other Documentation

- **Online man pages**: https://man.freebsd.org
- **FreeBSD Wiki**: https://wiki.freebsd.org (community-maintained notes, HOWTOs, and work-in-progress documentation).
- **Developer's Handbook**: https://docs.freebsd.org/en/books/developers-handbook is aimed at programmers.
- **Porter's Handbook**: https://docs.freebsd.org/en/books/porters-handbook if you package software for FreeBSD.

### Community and Support

Documentation will get you far, but sometimes you need to talk to real people. FreeBSD has an active and welcoming community.

- **Mailing lists**: https://lists.freebsd.org
  - `freebsd-questions@` is for general user help.
  - `freebsd-hackers@` is for development discussions.
  - `freebsd-drivers@` is specific to device driver development.
- **FreeBSD Forums**: https://forums.freebsd.org a friendly and beginner-friendly place to ask questions.
- **User Groups**:
  - Around the world, there are **FreeBSD and BSD user groups** that organize meetups, talks, and workshops.
  - Examples include *NYCBUG (New York City BSD User Group)*, *BAFUG (Bay Area FreeBSD User Group)*, and many university-based clubs.
  - You can usually find them via the FreeBSD Wiki, local tech mailing lists, or meetup.com.
  - If you don't find one nearby, consider starting a small group, even a handful of enthusiasts meeting online or in person can become a valuable support network.
- **Chat**:
  - **IRC** on Libera.Chat (`#freebsd`).
  - **Discord** communities exist and are pretty active, use this link to join: https://discord.com/invite/freebsd
- **Reddit**: https://reddit.com/r/freebsd

User groups and forums are especially valuable because you can often ask questions in your native language, or even meet people who are contributing to FreeBSD in your area.

#### How to Ask for Help

At some point, everyone gets stuck. One of FreeBSD's strengths is its active and supportive community, but to get useful answers, you need to ask clear, complete, and respectful questions.

When you post to a mailing list, forum, IRC, or Discord channel, include:

- **Your FreeBSD version**
   Run:

  ```sh
  % uname -a
  ```

  This tells helpers exactly which release, patch level, and architecture you are using.

- **What you were trying to do**
   Describe your goal, not just the command that failed. Helpers can sometimes suggest a better approach than the one you attempted.

- **The exact error messages**
   Copy and paste the error text instead of paraphrasing it. Even small differences matter.

- **Steps to reproduce the problem**
   If someone else can repeat your issue, they can often solve it much faster.

- **What you already tried**
   Mention commands, configuration changes, or documentation you consulted. This shows you've made an effort and prevents people from suggesting things you already did.

#### Example of a Poor Help Request

> "Ports aren't working, how do I fix it?"

This leaves out version, commands, errors, and context. Nobody can answer without guessing.

#### Example of a Good Help Request

> "I'm running FreeBSD 14.3-RELEASE on amd64. I tried building `htop` from ports with `cd /usr/ports/sysutils/htop && make install clean`. The build failed with the error:
>
> ```
> error: ncurses.h: No such file or directory
> ```
>
> I already tried `pkg install ncurses`, but the error remains. What should I check next?"

This is short but complete; version, command, error, and troubleshooting steps are all there.

**Tip**: Always stay polite and patient. Remember, most FreeBSD contributors are **volunteers**. A clear, respectful question not only increases your chances of a helpful reply, but also builds goodwill in the community.

### Hands-On Lab: Exploring Documentation

1. Open the man page for `ls`. Find and try at least two options you didn't know.

   ```sh
   % man ls
   ```

2. Use `man -k` to search for commands related to disks.

   ```sh
   % man -k disk | less
   ```

3. Open the man page for `open(2)` and compare it to `open(3)`. What's the difference?

4. Peek into the kernel developer documentation:

   ```sh
   % man 9 device
   ```

5. Visit https://docs.freebsd.org/ and find the page on system startup (`rc.d`). Compare it to `man rc.conf`.

### Wrapping Up

FreeBSD gives you powerful tools to teach yourself. The **man pages** are your first stop; they're always on your system, always up to date, and cover everything from basic commands to kernel APIs. The **Handbook** is your big-picture guide, and the **community** mailing lists, forums, user groups, and online chat are there to help when you need human answers.

Later, as you write drivers, you'll rely heavily on man pages (especially section 9) and on discussions in FreeBSD mailing lists and forums. Knowing how to find information is just as important as memorizing commands.

Next, we will look inside the system to **peek at kernel messages and tunables**. Tools like `dmesg` and `sysctl` let you see what the kernel is doing and will become essential when you start loading and testing your own device drivers.

## Peeking into the Kernel and System State

At this point, you know how to move around in FreeBSD, manage files, control processes, and even write scripts. That makes you a capable user. But writing drivers means stepping into the **mind of the kernel**. You'll need to see what FreeBSD itself sees:

- What hardware was detected?
- Which drivers were loaded?
- What tunable knobs exist inside the kernel?
- How do devices appear to the operating system?

FreeBSD gives you **three magical windows into the kernel's state**:

1. **`dmesg`** - the kernel's diary.
2. **`sysctl`** - the control panel full of switches and meters.
3. **`/dev`** - the doorway where devices show up as files.

These three tools will become your **companions**. Every time you add or debug a driver, you'll use them. Let's explore them now, step by step.

### dmesg: Reading the Kernel's Diary

Imagine FreeBSD as a pilot starting an airplane. As the system boots, the kernel checks its hardware: CPUs, memory, disks, USB devices and each driver reports back. Those messages are not lost; they're stored in a buffer you can read at any time with:

```sh
% dmesg | less
```

You'll see lines like:

```yaml
Copyright (c) 1992-2023 The FreeBSD Project.
Copyright (c) 1979, 1980, 1983, 1986, 1988, 1989, 1991, 1992, 1993, 1994
        The Regents of the University of California. All rights reserved.
FreeBSD is a registered trademark of The FreeBSD Foundation.
FreeBSD 14.3-RELEASE releng/14.3-n271432-8c9ce319fef7 GENERIC amd64
FreeBSD clang version 19.1.7 (https://github.com/llvm/llvm-project.git llvmorg-19.1.7-0-gcd708029e0b2)
VT(vga): text 80x25
CPU: AMD Ryzen 7 5800U with Radeon Graphics          (1896.45-MHz K8-class CPU)
  Origin="AuthenticAMD"  Id=0xa50f00  Family=0x19  Model=0x50  Stepping=0
  Features=0x1783fbff<FPU,VME,DE,PSE,TSC,MSR,PAE,MCE,CX8,APIC,SEP,MTRR,PGE,MCA,CMOV,PAT,PSE36,MMX,FXSR,SSE,SSE2,HTT>
  Features2=0xfff83203<SSE3,PCLMULQDQ,SSSE3,FMA,CX16,SSE4.1,SSE4.2,x2APIC,MOVBE,POPCNT,TSCDLT,AESNI,XSAVE,OSXSAVE,AVX,F16C,RDRAND,HV>
  AMD Features=0x2e500800<SYSCALL,NX,MMX+,FFXSR,Page1GB,RDTSCP,LM>
  AMD Features2=0x8003f7<LAHF,CMP,SVM,CR8,ABM,SSE4A,MAS,Prefetch,OSVW,PCXC>
  Structured Extended Features=0x219c07ab<FSGSBASE,TSCADJ,BMI1,AVX2,SMEP,BMI2,ERMS,INVPCID,RDSEED,ADX,SMAP,CLFLUSHOPT,CLWB,SHA>
  Structured Extended Features2=0x40061c<UMIP,PKU,OSPKE,VAES,VPCLMULQDQ,RDPID>
  Structured Extended Features3=0xac000010<FSRM,IBPB,STIBP,ARCH_CAP,SSBD>
  XSAVE Features=0xf<XSAVEOPT,XSAVEC,XINUSE,XSAVES>
  IA32_ARCH_CAPS=0xc000069<RDCL_NO,SKIP_L1DFL_VME,MDS_NO>
  AMD Extended Feature Extensions ID EBX=0x1302d205<CLZERO,XSaveErPtr,WBNOINVD,IBPB,IBRS,STIBP,STIBP_ALWAYSON,SSBD,VIRT_SSBD,PSFD>
  SVM: NP,NRIP,VClean,AFlush,NAsids=16
  ...
  ...
```

This is the kernel telling you:

- **what hardware it found**,
- **which driver claimed it**,
- and sometimes, **what went wrong**.

Later in this book, when you load your own driver, `dmesg` is where you'll look for your first "Hello, kernel!" message.

The output of `dmesg` can be very long, you can use `grep` to filter and see only what you need, for example:

```sh
% dmesg | grep ada
```

This will show only messages about disk devices (`ada0`, `ada1`).

### sysctl: The Kernel's Control Panel

If `dmesg` is the diary, `sysctl` is the **dashboard full of knobs and meters**. It exposes thousands of kernel variables at runtime: some read-only (system info), others tunable (system behavior).

Try this commands:

```
% sysctl kern.ostype
% sysctl kern.osrelease
% sysctl hw.model
% sysctl hw.ncpu
```

Output might look like:

```
kern.ostype: FreeBSD
kern.osrelease: 14.3-RELEASE
hw.model: AMD Ryzen 7 5800U with Radeon Graphics
hw.ncpu: 8
```

Here you just asked the kernel:

- What OS am I running?
- What version?
- What CPU?
- How many cores?

#### Exploring Everything

To see all parameters that you can finetune with `sysctl`, you can run the command below:

```sh
% sysctl -a | less
```

This prints the **entire control panel** thousands of values. They are organized by categories:

- `kern.*` - kernel properties and settings.
- `hw.*` - hardware information.
- `net.*` - network stack details.
- `vfs.*` - filesystem settings.
- `debug.*` - debugging variables (often useful for developers).

It's overwhelming at first, but don't worry, you'll learn to fish out what matters.

#### Changing Values

Some sysctls are writable. For example:

```sh
% sudo sysctl kern.hostname=myfreebsd
% hostname
```

You've just changed your hostname at runtime.

Important: Changes made this way disappear after reboot unless saved in `/etc/sysctl.conf`.

### /dev: Where Devices Come to Life

Now for the most exciting part.

FreeBSD represents devices as **special files** inside `/dev`. This is one of UNIX's most elegant ideas:

> If everything is a file, then everything can be accessed in a consistent way.

Run:

```sh
% ls -d /dev/* | less
```

You'll see a sea of names:

- `/dev/null`- the "black hole" where data goes to disappear.
- `/dev/zero` - an infinite stream of zeros.
- `/dev/random` - cryptographically secure random numbers.
- `/dev/tty` - your terminal.
- `/dev/ada0` - your SATA disk.
- `/dev/da0` - a USB disk.

Try interacting:

```sh
echo "Testing" > /dev/null         # silently discards output
head -c 16 /dev/zero | hexdump     # shows zeros in hex
head -c 16 /dev/random | hexdump   # random bytes from the kernel
```

Later, when you create your first driver, it will show up here as a file named  `/dev/hello`. Reading or writing to that file will trigger **your kernel code**. This is the moment when you'll feel the bridge between userland and the kernel.

### Hands-On Lab: Your First Peek Inside

1. View all kernel messages:

	```sh
   % dmesg | less
	```

2. Find your storage devices:

   ```sh
   % dmesg | grep ada
   ```

3. Ask the kernel about your CPU:

   ```sh
   % sysctl hw.model
   % sysctl hw.ncpu
   ```

4. Change your hostname temporarily:

   ```sh
   % sudo sysctl kern.hostname=mytesthost
   % hostname
   ```

5. Interact with special device files:

   ```
   % echo "Hello FreeBSD" > /dev/null
   % head -c 8 /dev/zero | hexdump
   % head -c 8 /dev/random | hexdump
   ```

With this short lab, you've already read kernel messages, queried kernel variables, and touched device nodes, exactly what professional developers do daily.

### From Shell to Hardware: The Big Picture

To understand why tools like `dmesg`, `sysctl`, and `/dev` are so powerful, it helps to picture how FreeBSD is layered:

```
+----------------+
|   User Space   |  ← Commands you run: ls, ps, pkg, scripts
+----------------+
        ↓
+----------------+
|   Shell (sh)   |  ← Interprets your commands into syscalls
+----------------+
        ↓
+----------------+
|    Kernel      |  ← Handles processes, memory, devices, filesystems
+----------------+
        ↓
+----------------+
|   Hardware     |  ← CPU, RAM, disks, USB, network cards
+----------------+
```

Whenever you type a command in the shell, it travels down this stack:

- The **shell** interprets it.
- The **kernel** executes it by managing processes, memory, and devices.
- The **hardware** responds.

Then the results bubble back up for you to see.

Understanding this flow is essential for driver developers: when you interact with `/dev`, you're connecting directly to the kernel, which in turn talks to the hardware.

### Common Beginner Pitfalls

Exploring the kernel can be exciting, but here are some common mistakes to watch out for:

1. **Confusing `dmesg` with system logs**

   - `dmesg` only shows the kernel's ring buffer, not all logs.
   - Old messages may disappear after new ones push them out.
   - For complete logs, check `/var/log/messages`.

2. **Forgetting that `sysctl` changes don't persist**

   - If you change a setting with `sysctl`, it resets at reboot.

   - To make it permanent, add it to `/etc/sysctl.conf`.

   - Example:

   ```sh
     % echo 'kern.hostname="myhost"' | sudo tee -a /etc/sysctl.conf
   ```

3. **Overwriting files in `/dev`**

   - `/dev` entries aren't normal files; they're live connections to the kernel.
   - Redirecting output to them can have real effects.
   - Writing to `/dev/null` is safe, but writing random data to `/dev/ada0` (your disk) could destroy it.
   - Rule of thumb: explore `/dev/null`, `/dev/zero`, `/dev/random`, and `/dev/tty`, but leave storage devices (`ada0`, `da0`) alone unless you know exactly what you're doing.

4. **Expecting `/dev` entries to stay the same**

   - Devices appear and disappear as hardware is added or removed.
   - For example, plugging in a USB stick may create `/dev/da0`.
   - Don't hardcode device names into scripts without verifying.

5. **Not using full paths in automation**

   - Cron and other automated tools may not have the same `PATH` as your shell.
   - Always use full paths (`/sbin/sysctl`, `/bin/echo`) when scripting kernel interactions.

### Wrapping Up

In this section, you opened three magical windows into FreeBSD's kernel:

- `dmesg`- the diary of the system, recording hardware detection and driver messages.
- `sysctl` - the control panel that reveals (and sometimes adjusts) kernel settings.
- `/dev`- the place where devices come to life as files.

The **big picture** to remember is this: whenever you type a command, it travels through the shell, down into the kernel, and finally to the hardware. The results then bubble back up for you to see. Tools like `dmesg`, `sysctl`, and `/dev` let you peek into that flow and see what the kernel is doing behind the scenes.

These aren't just abstract tools; they're exactly how you'll see your **own driver** appear in the system. When you load your module, you'll watch `dmesg` light up, you may expose a knob with `sysctl`, and you'll interact with your device node under `/dev`.

Before we move on to the next chapter and start learning about C programming, let's pause to consolidate everything you've learned in this chapter. In the next section, we'll review the key ideas and give you a set of challenges to practice, exercises that will help lock in these new skills and prepare you for the journey ahead.

## Putting It All Together: Your FreeBSD Journey So Far

Congratulations! You've just taken your **first deep dive into UNIX and FreeBSD**. What started as abstract ideas is now becoming practical skills. You can move around the system, manage files, edit and install software, control processes, automate tasks, and even peek into the kernel's inner workings.

Let's take a moment to review what you've accomplished in this chapter:

- **What UNIX is and why it matters** - A philosophy of simplicity, modularity, and "everything is a file," inherited by FreeBSD.
- **The Shell** - Your window into the system, where commands follow the consistent structure of `command [options] [arguments]`.
- **Filesystem Layout** - A single hierarchy starting at `/`, with special roles for directories like `/etc`, `/usr/local`, `/var`, and `/dev`.
- **Users, Groups, and Permissions** - The foundation of FreeBSD's security model, controlling who can read, write, or execute.
- **Processes** - Programs in motion, with tools like `ps`, `top`, and `kill` to manage them.
- **Installing Software** - Using `pkg` for quick binary installs, and the **Ports Collection** for source-based flexibility.
- **Automation** - Scheduling tasks with `cron`, one-time jobs with `at`, and maintenance with `periodic`.
- **Shell Scripting** - Turning repetitive commands into reusable programs using FreeBSD's native `/bin/sh`.
- **Peeking into the Kernel** - Using `dmesg`, `sysctl`, and `/dev` to observe the system at a deeper level.

That's a lot, but don't worry if you don't feel like an expert yet. The goal of this chapter was not perfection, but **comfort**: comfort in the shell, comfort in exploring FreeBSD, and comfort in seeing how UNIX works under the hood. That comfort will carry forward as we begin writing real code for the system.

### Practice Ground

To make sure these skills stick, here are **46 exercises**. 

They're grouped by topic, so you can practice section by section or mix them up as you like.

### Filesystem and Navigation (8 exercises)

1. Use `pwd` to confirm your current directory, then move into `/etc` and back into your home directory using `cd`.
2. Create a directory `unix_playground` in your home. Inside it, create three subdirectories: `docs`, `code`, and `tmp`.
3. Inside `unix_playground/docs`, create a file called `readme.txt` with the text "Welcome to FreeBSD". Use `echo` and output redirection.
4. Copy `readme.txt` into the `tmp` directory. Verify both files exist with `ls -l`.
5. Rename the file in `tmp` to `copy.txt`. Then delete it with `rm`.
6. Use `find` to locate every `.conf` file inside `/etc`.
7. Use absolute paths to copy `/etc/hosts` into your `docs` directory. Then use relative paths to move it into `tmp`.
8. Use `ls -lh` to display file sizes in human-readable format. Which file in `/etc` is the largest?

### Users, Groups, and Permissions (6 exercises)

1. Create a file called `secret.txt` in your home directory. Make it readable only by you.
2. Create a directory `shared` and give read/write access to everyone (mode 777). Test it by writing a file into it.
3. Use `id` to list your user's UID, GID, and groups.
4. Use `ls -l` on `/etc/passwd` and `/etc/master.passwd`. Compare their permissions and explain why they differ.
5. Create a file and change its owner to `root` using `sudo chown`. Try editing it as a normal user. What happens?
6. Add a new user with `sudo adduser`. Set a password, log in as that user, and check their default home directory.

### Processes and System Monitoring (7 exercises)

1. Start a process in the foreground with `sleep 60`. While it runs, open another terminal and use `ps` to find it.
2. Start the same process in the background with `sleep 60 &`. Use `jobs` and `fg` to bring it back to the foreground.
3. Use `top` to find which process is consuming the most CPU at the moment.
4. Start a `yes` process (`yes > /dev/null &`) to flood the CPU. Watch it in `top`, then stop it with `kill`.
5. Check how long your system has been running with `uptime`.
6. Use `df -h` to see how much disk space is available on your system. Which filesystem is mounted on `/`?
7. Run `sysctl vm.stats.vm.v_page_count` to see the number of memory pages on your system.

### Installing and Managing Software (pkg and Ports) (6 exercises)

1. Use `pkg search` to look for a text editor other than `nano`. Install it, run it, then remove it.
2. Install the `htop` package with `pkg`. Compare its output to the built-in `top`.
3. Explore the Ports Collection by navigating to `/usr/ports/editors/nano`. Look at the Makefile.
4. Build `nano` from ports with `sudo make install clean`. Did it ask you about options?
5. Update your ports tree using `git`. Which categories were updated?
6. Use `which` to locate where the binary of `nano` or `htop` was installed. Check whether it's under `/usr/bin` or `/usr/local/bin`.

### Automation and Scheduling (cron, at, periodic) (6 exercises)

1. Write a cron job that logs the current date and time every 2 minutes into `~/time.log`. Wait and check it with `tail`.
2. Write a cron job that cleans up all `.tmp` files in your home directory every night at midnight.
3. Use the `at` command to schedule a message to yourself 5 minutes from now.
4. Run `sudo periodic daily` and read its output. What kinds of tasks does it perform?
5. Add a cron job that runs `df -h` every day at 8 AM and logs the result to `~/disk.log`.
6. Redirect cron job output into a custom log file (`~/cron_output.log`). Confirm that both normal output and errors are captured.

### Shell Scripting (/bin/sh) (7 exercises)

1. Write a script `hello_user.sh` that prints your username, current date, and number of processes running. Make it executable and run it.
2. Write a script `organize.sh` that moves all `.txt` files from your home directory into a folder called `texts`. Add comments to explain each step.
3. Modify `organize.sh` to also create subdirectories by file type (`images`, `docs`, `archives`).
4. Write a script `disk_alert.sh` that warns you if root filesystem usage exceeds 80%.
5. Write a script `logger.sh` that appends a timestamped entry to `~/activity.log` with the current directory and user.
6. Write a script `backup.sh` that creates a `.tar.gz` archive of `~/unix_playground` into `~/backups/`.
7. Extend `backup.sh` so that it keeps only the last 5 backups and deletes older ones automatically.

### Peeking into the Kernel (dmesg, sysctl, /dev) (6 exercises)

1. Use `dmesg` to find the model of your primary disk.
2. Use `sysctl hw.model` to display your CPU model and `sysctl hw.ncpu` to display how many cores you have.
3. Change your hostname temporarily using `sysctl kern.hostname=mytesthost`. Check it with `hostname`.
4. Use `ls /dev` to list device nodes. Identify which ones represent disks, terminals, and virtual devices.
5. Use `head -c 16 /dev/random | hexdump` to read 16 random bytes from the kernel.
6. Plug in a USB stick (if available) and run `dmesg | tail`. Can you see which new `/dev/` entry appeared?

### Wrapping Up

With these **46 exercises**, you've covered every major topic in this chapter:

- Filesystem navigation and layout
- Users, groups, and permissions
- Processes and monitoring
- Software installation with pkg and ports
- Automation with cron, at, and periodic
- Shell scripting with FreeBSD's native `/bin/sh`
- Kernel introspection with dmesg, sysctl, and /dev

By completing them, you'll move from being a *passive reader* to an **active UNIX practitioner**. You'll not only know how FreeBSD works, you'll have *lived inside it*.

These exercises are the **muscle memory** you'll need when we start programming. When we dive into C and later kernel development, you'll already be fluent in the daily tools of a UNIX developer.

### Looking Ahead

The next chapter will introduce the **C programming language**, the language of the FreeBSD kernel. This is the tool you'll use to create device drivers. Don't worry if you've never programmed before, we'll build your understanding step by step, just as we did with UNIX in this chapter.

By combining your new UNIX literacy with C programming skills, you'll be ready to begin shaping the FreeBSD kernel itself.
