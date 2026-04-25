<p align="center">
  <img src="https://freebsd.edsonbrandi.com/images/book-cover.jpg" alt="FreeBSD Device Drivers book cover" width="300">
</p>

<h1 align="center">FreeBSD Device Drivers</h1>
<p align="center"><strong>From First Steps to Kernel Mastery</strong></p>
<p align="center"><em>by Edson Brandi · Version 2.0 (April 2026)</em></p>

<p align="center">
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-MIT-blue.svg"></a>
  <img alt="FreeBSD" src="https://img.shields.io/badge/FreeBSD-14.x-red.svg">
  <img alt="Pages" src="https://img.shields.io/badge/pages-4%2C500%2B-green.svg">
  <img alt="Status" src="https://img.shields.io/badge/status-v2.0%20released-brightgreen.svg">
  <img alt="Languages" src="https://img.shields.io/badge/languages-en__US%20%7C%20pt__BR%20%7C%20es__ES-yellow.svg">
</p>


---

## About This Book

**FreeBSD Device Drivers: From First Steps to Kernel Mastery** is a free, open-source book that takes you from "I've never written kernel code" to "I can write, debug, and submit production-quality FreeBSD drivers." It is a guided course rather than a reference, structured around 38 chapters, 6 appendices, and dozens of hands-on labs that compile and load on a real FreeBSD 14.x system.

The book is aimed at readers who are *willing to learn* rather than are already qualified. It begins with UNIX fundamentals and the C language, walks step by step through every concept the kernel will demand of you, and only then opens the door to driver development. By the time you reach DMA, interrupts, and PCI work, the vocabulary feels earned, not imposed.

> *"Kernel programming is still programming, only with more explicit rules, greater responsibility, and a bit more power. Once you understand that, the fear gives way to excitement."* (from Chapter 1)

## Why This Book?

There are excellent FreeBSD kernel references already, including `man 9`, the Architecture Handbook, and the Newbus papers. What has been missing is a single text that:

- **Starts from zero.** UNIX, C, and the FreeBSD environment are taught before any kernel code is written.
- **Targets FreeBSD 14.x specifically.** Every API, every example, every lab was verified against the FreeBSD 14.3 source tree.
- **Treats labs as first-class.** Roughly half of the recommended study time is hands-on. The same patterns (attach, cleanup, locking) recur across chapters until they become reflex.
- **Builds one driver across the whole book.** The `myfirst` driver evolves chapter by chapter, gaining synchronisation, then hardware access, then interrupts, then DMA. You see the same code mature in your own hands.
- **Covers the full lifecycle.** From "Hello Kernel Module" all the way to submitting a Phabricator review and shepherding a driver into the FreeBSD tree.

## Who This Book Is For

- **Beginners** who know little about C, UNIX, or kernels but are willing to learn.
- **Developers** curious about how operating systems actually work under the hood.
- **Professionals** who already use FreeBSD (or similar systems) and want to deepen their knowledge by learning how drivers are built in practice.

## Who This Book Isn't For

- Readers looking for a quick copy-and-paste manual. The book emphasises understanding over shortcuts.
- Seasoned kernel developers who don't need the foundations. The pace starts from the ground up.
- Readers wanting an encyclopaedic hardware reference. The focus is real-world FreeBSD driver development, not exhaustive bus or device specifications.

## What You'll Learn

The book is organised into seven parts that build on each other:

| Part | Title | Focus |
|------|-------|-------|
| **1** | Foundations: FreeBSD, C, and the Kernel | Lab setup, UNIX, C for kernel work, driver anatomy |
| **2** | Building Your First Driver | Character drivers, device files, read/write, I/O |
| **3** | Concurrency and Synchronization | Threads, mutexes, condvars, timers, taskqueues, semaphores |
| **4** | Hardware and Platform-Level Integration | PCI, interrupts, MSI/MSI-X, DMA, power management |
| **5** | Debugging, Tools, and Real-World Practices | Tracing, KGDB, advanced debugging, performance tuning |
| **6** | Writing Transport-Specific Drivers | USB, serial, storage/VFS, network drivers |
| **7** | Mastery Topics: Special Scenarios and Edge Cases | Portability, virtualisation, security, embedded, reverse engineering, upstream submission |

By the end, you will have written and loaded your own kernel modules, built a character driver, handled real interrupts and DMA, debugged kernel panics, profiled your driver under load, and walked through every step of contributing your work back to the FreeBSD Project.

## Book Stats

| | |
|---|---|
| **Pages** | 4,500+ |
| **Chapters** | 38 |
| **Appendices** | 6 |
| **Reading time** | ~100 hours |
| **Lab time** | ~100 hours |
| **Total study time** | ~200 hours (≈6 months at 5 hrs/week) |
| **Target FreeBSD release** | 14.3 |
| **Languages** | English (original) · Brazilian Portuguese (AI-translated) · Spanish (AI-translated) |
| **Formats** | PDF · EPUB · HTML · Markdown source |

## Full Table of Contents

<details>
<summary><strong>Click to expand the complete chapter list</strong></summary>

### Part 1: Foundations of FreeBSD, C, and the Kernel
1. Introduction: From Curiosity to Contribution
2. Setting Up Your Lab
3. A Gentle Introduction to UNIX
4. A First Look at the C Programming Language
5. Understanding C for FreeBSD Kernel Programming
6. The Anatomy of a FreeBSD Driver

### Part 2: Building Your First Driver
7. Writing Your First Driver
8. Working with Device Files
9. Reading and Writing to Devices
10. Handling Input and Output Efficiently

### Part 3: Concurrency and Synchronization
11. Concurrency in Drivers
12. Synchronization Mechanisms
13. Timers and Delayed Work
14. Taskqueues and Deferred Work
15. More Synchronization: Conditions, Semaphores, and Coordination

### Part 4: Hardware and Platform-Level Integration
16. Accessing Hardware
17. Simulating Hardware
18. Writing a PCI Driver
19. Handling Interrupts
20. Advanced Interrupt Handling
21. DMA and High-Speed Data Transfer
22. Power Management

### Part 5: Debugging, Tools, and Real-World Practices
23. Debugging and Tracing
24. Integrating with the Kernel
25. Advanced Topics and Practical Tips

### Part 6: Writing Transport-Specific Drivers
26. USB and Serial Drivers
27. Working with Storage Devices and the VFS Layer
28. Writing a Network Driver

### Part 7: Mastery Topics
29. Portability and Driver Abstraction
30. Virtualisation and Containerization
31. Security Best Practices
32. Device Tree and Embedded Development
33. Performance Tuning and Profiling
34. Advanced Debugging Techniques
35. Asynchronous I/O and Event Handling
36. Creating Drivers Without Documentation (Reverse Engineering)
37. Submitting Your Driver to the FreeBSD Project
38. Final Thoughts and Next Steps

### Appendices
- **A:** FreeBSD Kernel API Reference
- **B:** Algorithms and Logic for Systems Programming
- **C:** Hardware Concepts for Driver Developers
- **D:** Operating System Concepts
- **E:** Navigating FreeBSD Kernel Internals
- **F:** Benchmark Harness and Results

</details>

## How to Read the Book

The recommended pace is one chapter per week at roughly five hours of weekly study. That schedule puts the whole book within reach across an academic year. Some chapters (especially Chapter 4 on C, and the Part 4 hardware chapters) naturally span multiple weeks.

**The labs are strongly recommended.** Kernel programming rewards muscle memory in a way few disciplines do. The same attach pattern, the same cleanup chain, and the same locking shape appear chapter after chapter and driver after driver. Typing those patterns, compiling them, loading them into a running kernel, and watching them fail on purpose is the single most effective way to internalise them.

If you already know C, UNIX, and the general shape of an OS kernel, fast-path notes throughout Part 1 tell you which sections to read carefully and which you can skim.

## Download the Book

Version 2.0 is available now on the [Releases page](https://github.com/ebrandi/FDD-book/releases/latest) in three languages and three formats:

| Language | PDF | EPUB | HTML |
|----------|:---:|:----:|:----:|
| **English** (original) | [PDF](https://github.com/ebrandi/FDD-book/releases/latest/download/freebsd-device-drivers-v2.0-en_US.pdf) | [EPUB](https://github.com/ebrandi/FDD-book/releases/latest/download/freebsd-device-drivers-v2.0-en_US.epub) | [HTML](https://github.com/ebrandi/FDD-book/releases/latest/download/freebsd-device-drivers-v2.0-en_US.html.zip) |
| **Português (Brasil)**, AI-translated | [PDF](https://github.com/ebrandi/FDD-book/releases/latest/download/freebsd-device-drivers-v2.0-pt_BR.pdf) | [EPUB](https://github.com/ebrandi/FDD-book/releases/latest/download/freebsd-device-drivers-v2.0-pt_BR.epub) | [HTML](https://github.com/ebrandi/FDD-book/releases/latest/download/freebsd-device-drivers-v2.0-pt_BR.html.zip) |
| **Español**, AI-translated | [PDF](https://github.com/ebrandi/FDD-book/releases/latest/download/freebsd-device-drivers-v2.0-es_ES.pdf) | [EPUB](https://github.com/ebrandi/FDD-book/releases/latest/download/freebsd-device-drivers-v2.0-es_ES.epub) | [HTML](https://github.com/ebrandi/FDD-book/releases/latest/download/freebsd-device-drivers-v2.0-es_ES.html.zip) |

You can also browse the Markdown source directly in the [`content/`](./content) directory, or build the book yourself with [`scripts/build-book.sh`](./scripts/build-book.sh).

### About the translations

The **English version is the original and authoritative version of the book.** The Brazilian Portuguese and Spanish editions were translated using AI and **have not yet undergone a full human technical review.** They are published to make the material accessible to more readers, but they may contain translation mistakes, awkward wording, or technical inaccuracies introduced during translation.

If something in a translated edition seems unclear, inconsistent, or technically questionable, please refer to the English version as the source of truth. Help with reviewing and improving the translations is very welcome (see [Contributing](#contributing) below).

### Known issues in v2.0

This is a **draft release** of a very large book. A few things to be aware of:

- Some source-code blocks in the PDF, EPUB, and HTML editions may overflow the page or wrap awkwardly. These are presentation issues that will be improved in a future release; the content itself is correct.
- If a code example is hard to read in any of the rendered formats, **the Markdown files in this repository are the source of truth.** Open the relevant file under [`content/`](./content) for a clean version.
- Translation review for pt_BR and es_ES is planned for the near future, as the author's free time allows.

## Repository Structure

```
FDD-book/
├── content/              # Book content (Markdown)
│   ├── chapters/         # Chapters by Part
│   └── appendices/       # Appendices A-F
├── examples/             # Source code from the book
├── translations/
│   ├── pt_BR/            # Brazilian Portuguese (AI-translated)
│   └── es_ES/            # Spanish (AI-translated)
└── scripts/              # Build and utility scripts
```

## Contributing

Contributions of every kind are welcome, including corrections, clarifications, new examples, translations, and reviews from FreeBSD developers and learners alike.

### Ways to contribute

- **Content:** add new chapters, refine existing material
- **Technical review:** review chapters for accuracy against FreeBSD 14.x
- **Translation review:** help review and improve the AI-translated **pt_BR** and **es_ES** editions; native speakers with FreeBSD/kernel experience are especially welcome
- **New translations:** help translate the book into another language
- **Code:** improve examples, build scripts, and tooling
- **Issues:** report bugs, factual errors, unclear passages, or formatting problems

### Reporting an issue

When filing an issue, please include:

- The language version you were reading (`en_US`, `pt_BR`, `es_ES`)
- The format used (PDF, EPUB, HTML, or Markdown)
- The chapter or section where the problem appears
- A short explanation of the issue, and a suggested correction if you have one

### Workflow

1. Fork the repository
2. Create a branch: `git checkout -b feature/your-change`
3. Make your changes and test the build with `scripts/build-book.sh`
4. Commit with a clear message: `git commit -m "Chapter 18: clarify BAR mapping"`
5. Push and open a Pull Request

When you're stuck while reading the book, **filing an issue helps**. If a passage seems wrong or a lab fails unexpectedly, every report makes the next reader's path smoother.

## Frequently Asked Questions

### Why does this book exist?

The honest answer is that the FreeBSD Project needs new contributors, and the path into kernel and driver work has always been steeper than it should be. Most existing material assumes you already know UNIX, already know C well, already know what a bus is, and already know how to read a kernel source tree. That works for the people who are already most of the way there. It does very little for the curious developer who wants to start.

The goal of this book is to lower that on-ramp. If even a small number of readers finish it and go on to submit patches, review code, write new drivers, or eventually become FreeBSD committers, the book has done its job. **Training the next generation of FreeBSD contributors is the reason this work was written**.

### Do I need to know C before starting?

No. Chapters 4 and 5 teach C from the ground up, focusing on the parts of the language that matter for kernel work (pointers, structures, memory layout, the preprocessor, and calling conventions). If you already know C well, sidebars in those chapters tell you what to skim and what to read carefully.

### Do I need to know UNIX or FreeBSD?

No. Chapter 2 walks you through installing FreeBSD in a VM or on bare metal, and Chapter 3 introduces the UNIX command line, filesystem, processes, permissions, and editors. By the end of Part 1 you will have a working lab and the vocabulary to use it.

### Do I need real hardware?

For most of the book, no. A virtual machine running FreeBSD 14.x is enough for the foundations, the first driver chapters, concurrency and synchronization, and a large portion of the debugging material. Real hardware becomes useful (but is still not strictly required) when you reach the PCI, interrupt, and DMA chapters in Part 4. Those chapters are written so that the concepts make sense even if you only run them in a VM.

### Will this help me write Linux drivers?

Indirectly, yes. The kernel programming discipline transfers very well: locking strategy, memory management, interrupt context, DMA mapping, the difference between sleeping and non-sleeping code paths, defensive cleanup ordering. The specific APIs differ. After reading this book you will not know the Linux device model, but you will recognise its problems and the shape of its solutions, and you will be able to read Linux Device Drivers (LDD) much more easily.

### Why FreeBSD 14.x specifically?

Every API, every example, and every lab was planned to be executed under FreeBSD 14.3 source tree and the corresponding `man 9` pages. Targeting a specific release lets the book be precise about function signatures, header locations, and behaviour. The concepts will outlive 14.x by many years; the exact line numbers and small API details will not, and the book is honest about that.

### Is this an official FreeBSD Project publication?

No. This is an independent educational book about FreeBSD device driver development. It is not an official publication of the FreeBSD Project. The author is a FreeBSD committer and a member of the Documentation Engineering Team (DocEng), but the book reflects his work and views, not an official Project position.

### How long will this actually take me?

If you read carefully and do the labs, plan for around 200 hours of total work. That is roughly 100 hours of reading and 100 hours of hands-on lab time. At five hours a week that is about a six-month evening project; at ten hours a week, a focused two-month sprint. Reading without doing the labs cuts the time roughly in half but also cuts the value: kernel programming rewards muscle memory in a way few disciplines do.

### Can I skip the labs?

You can, but you probably shouldn't. The labs are where prose becomes reflex. Patterns like attach ordering, cleanup unwinding, and lock acquisition shape recur in every chapter, and the only reliable way to internalise them is to type them, compile them, load them into a running kernel, and watch them fail on purpose. Readers who skip the labs report progress that feels smooth at first and then quietly stalls around Part 3 or Part 4.

### How do I contribute back to FreeBSD after reading this?

Chapter 37 covers the full submission workflow: how to prepare a patch, how to use Phabricator (the FreeBSD code review system), how to find a committer to sponsor your work, how to respond to review feedback, and how to shepherd a driver into the tree. The earlier chapters build the technical skill; Chapter 37 builds the social workflow. Both matter.

### The book is huge. Where should I start?

Start at Chapter 1 unless you have a reason not to. The book is cumulative; later chapters lean on vocabulary and habits established earlier. If you already know C, UNIX, and the general shape of an OS kernel, the fast-path notes inside Part 1 tell you what to skim. If a specific subsystem is what brought you here (USB, networking, storage, PCI), it is fine to read Parts 1 and 2 carefully and then jump ahead, but expect to circle back when terms from earlier chapters reappear.

### I found a mistake. What should I do?

Open an issue on GitHub. Include the language version, the format you were reading, the chapter or section, a short description of the problem, and a suggested correction if you have one. Every report makes the next reader's experience better. Translation issues in the pt_BR and es_ES editions are especially welcome, since those have not yet had a full human technical review.

### Is the book really free?

Yes. It is released under the MIT License. You can read it, share it, print it, quote it, build on it, and translate it. Attribution is appreciated but not required for personal use. If you want to support the work, the most useful things you can do are: tell other people about the book, file issues when you find problems, contribute reviews or translations, and (if it eventually leads you there) submit your own work to the FreeBSD Project.

## About the Author

I'm Edson Brandi. My path into technology was anything but conventional. I started as a chemistry student at Unicamp in Brazil in 1995, with no plan to work with computers, but with one persistent question: *how does this actually work?* That question led me to FreeBSD, and FreeBSD has shaped my career ever since.

In the years that followed I founded the **Brazilian FreeBSD User Group (FUG-BR)**, co-created the **FreeBSD LiveCD Tool Set**, and in 2002 co-founded **FreeBSD Brasil**, a company providing FreeBSD training, consulting, and support that still operates today. I'm a FreeBSD committer and currently a member of the **FreeBSD Documentation Engineering Team (DocEng)**, helping maintain the systems that keep FreeBSD's documentation alive and accessible worldwide.

Professionally, I've spent my career in infrastructure and engineering leadership across multiple industries, and today I serve as IT Director at a fintech company in London.

I wrote this book because I want other curious people to have the on-ramp I never had. You don't need a computer science degree to write kernel code. What you need is curiosity, persistence, patience, and a guide that meets you where you are.

Edson Brandi · ebrandi@FreeBSD.org

## License

This book and its accompanying source code are released under the **MIT License**. See [LICENSE](LICENSE) for the full text. You are free to read, share, and build on this work; attribution is appreciated.

## Acknowledgements

- The FreeBSD development community
- All contributors, reviewers, and translators
- Everyone who has ever filed an issue or asked a question that improved a chapter

## Links

- **Repository:** https://github.com/ebrandi/FDD-book
- **Issues:** https://github.com/ebrandi/FDD-book/issues
- **Discussions:** https://github.com/ebrandi/FDD-book/discussions
- **Author:** Edson Brandi · ebrandi@FreeBSD.org

---

<p align="center"><em>If this book helps you, please star the repository and share it with someone else who's curious about how computers really work.</em></p>
