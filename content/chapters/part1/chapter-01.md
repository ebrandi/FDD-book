---
title: "Introduction - From Curiosity to Contribution"
description: "Discover why FreeBSD matters, what device drivers do, and how this book will guide your journey."
partNumber: 1
partName: "Foundations: FreeBSD, C, and the Kernel"
chapter: 1
lastUpdated: "2025-08-24"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
date: "2025-08-24"
estimatedReadTime: 30
---

*"What begins as curiosity becomes skill, and what becomes skill empowers the next generation."* - Edson Brandi

# Chapter 1: Introduction - From Curiosity to Contribution

## My Journey: From Curiosity to Career

Every book begins with a reason to exist. For this one, the reason is deeply personal. My own path into technology was anything but conventional. I did not start as a computer science student or grow up surrounded by computers. My early academic journey was in chemistry, not computing. I spent my days with experiments, formulas, and laboratory equipment, tools that, at first glance, seemed far removed from operating systems and device drivers.

**From Chemistry to Technology: A True Story About Curiosity and Change**

In 1995, I was a chemistry student at Unicamp (State University of Campinas), one of Brazil’s top universities. I had no plan to work with infrastructure, software, or systems. But I had questions. I wanted to know how computers really worked, not just how to use them, but how the pieces fit together under the hood.

That search for answers led me to FreeBSD. What fascinated me most was not only the way the system behaved but that I had access to its source code. This was not just an operating system to use; it was one I could study, line by line. That possibility became the fuel for my growing passion.

At first, I could only explore FreeBSD on the university’s machines. During the day, my time was consumed by chemistry studies, but I longed for more hours to dive deeper. I couldn’t let this curiosity interfere with my degree. Coming from a humble background, I was the first in my family to reach university, and I carried that responsibility with pride.

In those early years, I had no computer of my own. Living on a limited budget, I barely managed the basics of being away from home. For nearly two years, my studies of FreeBSD took place in the chemistry institute’s library, where I devoured every document I could find about Unix and system programming. But without my own machine, I couldn’t put theory into practice for many of the things I was learning.

Finally, in 1997, I managed to assemble my first computer: a 386DX running at 40 MHz, with 8 MB of RAM and a 240 MB hard drive. The parts came from a generous friend I had met in the FreeBSD community, Adriano Martins Pereira. Over the years, he became such a close friend that he would later stand beside me as one of my groomsmen at my wedding.

I will never forget the moment I brought home the more than 20 floppy disks to install **FreeBSD 2.1.7**. It might seem trivial now, but for me it was transformative. For the first time, I could experiment with FreeBSD at night and on weekends, testing everything I had been reading about for two years. Looking back, I doubt I would be where I am today without the determination to nurture that spark of curiosity.

When I first touched FreeBSD, I understood very little of what I was seeing. The installation messages felt cryptic, the commands unfamiliar. Yet each small success, booting into a prompt, configuring a network card, compiling a kernel, felt like unlocking a new part of a hidden world. That thrill of discovery never fully goes away.

Over time, I wanted to share what I was learning. I created **FreeBSD Primeiros Passos** (FreeBSD First Steps), a small website in Portuguese that introduced newcomers to the system. It was simple, but it helped many people. That effort grew into the **Brazilian FreeBSD User Group (FUG-BR)**, which brought enthusiasts together to learn, exchange knowledge, and even meet in person at conferences. Speaking at early editions of **FISL**, Brazil’s International Free Software Forum, was one of my proudest moments. Standing in front of an audience to share something that had changed my life was unforgettable.

Around that time, I also co-created the **FreeBSD LiveCD Tool Set**. Back in the late 1990s, installing an operating system like FreeBSD could feel daunting. Our tool allowed users to boot a complete FreeBSD environment directly from a CD without touching their hard drive. It was a simple idea, but it lowered the barrier for countless new users.

By 2002, I became one of the founders of **FreeBSD Brasil** ([https://www.freebsdbrasil.com.br](https://www.freebsdbrasil.com.br)), a company dedicated to training, consulting, and supporting businesses with FreeBSD. It was an opportunity to bridge the gap between open source ideals and professional, real-world applications. Although I’m no longer involved, FreeBSD Brasil continues to this day, helping companies across Brazil adopt FreeBSD in their operations.

Eventually, my work led me deeper into the FreeBSD Project itself. I became a **committer**, focusing on documentation and translations. Today, I am part of the **FreeBSD Documentation Engineering Team (DocEng)**, helping maintain the systems that keep FreeBSD’s documentation alive, updated, and accessible worldwide.

All of this, every project, every friendship, every opportunity,  grew from that first decision to explore FreeBSD out of sheer curiosity.

Even though my formal studies were in chemistry, my career shifted toward technology. I worked in infrastructure engineering, managing on-premise datacenters, then building systems in the cloud, and eventually leading teams in software and product development across multiple industries in Brazil. Today, I serve as **IT Director at Teya**, a fintech company in London, helping design the systems that power businesses across Europe.

And it all began with one question: **How does this work?**

I share this story because I want you to see what is possible. You don’t need a degree in computer science to become a great developer, sysadmin, or technology leader. What you need is curiosity, persistence, and the patience to keep learning when things get hard.

FreeBSD helped me unlock my potential. This book is here to help you unlock yours.

## FreeBSD in Context

Before we dive into writing drivers, let’s take a moment to appreciate the stage on which all this happens.

### A Brief History

FreeBSD traces its roots back to the **Berkeley Software Distribution (BSD)**, a derivative of the original UNIX developed at AT&T in the early 1970s. At the University of California, Berkeley, students and researchers added groundbreaking features that shaped modern operating systems, including the **TCP/IP networking stack** that still powers the internet today.

While many commercial UNIX variants rose and fell over the decades, FreeBSD endured. The project was officially founded in 1993, built on the work of the Berkeley CSRG (Computer Systems Research Group), and guided by a commitment to openness, technical excellence, and long-term stability. Over thirty years later, it is still actively developed and maintained, a rare achievement in the fast-moving world of technology.

FreeBSD has powered **research labs, universities, internet service providers, and enterprise products**. It has also been quietly present behind some of the busiest websites and networks in the world, chosen for its reliability and performance when failure is not an option.

More than a piece of software, FreeBSD represents a **continuum of knowledge**. By studying it, you are not only learning a modern operating system but also connecting with decades of accumulated engineering wisdom that continues to influence computing today.

### Why FreeBSD Is Special

FreeBSD is far more than just another operating system. It combines the depth of a mature UNIX tradition with the focus and care of a modern engineering project. Over the years, it has earned a reputation for qualities that set it apart:

- **Stability**: FreeBSD systems are famous for their ability to run for months or even years without rebooting. Internet service providers, data centers, and research institutions rely on this predictability for mission-critical workloads.
- **Performance**: The operating system consistently demonstrates excellent performance under demanding conditions. From high-throughput networking to complex storage systems, FreeBSD has been chosen in environments where efficiency is not optional but essential.
- **Clarity of design**: Unlike many other UNIX-like systems, FreeBSD is developed as a single, cohesive whole rather than a collection of components from different sources. Its codebase has a reputation for being well-structured and approachable, making it not just a platform to run but also a platform to learn from. For someone interested in systems programming, the source code is as valuable as the binaries.
- **Culture of documentation**: The project has always placed a high value on documentation. The FreeBSD Handbook and the many manual pages are written and maintained with the same care as the code itself. This reflects a core principle of the community: knowledge should be as accessible as the software.

Beyond these qualities, FreeBSD also stands out for its **license and philosophy**. The BSD license is permissive, giving both individuals and companies the freedom to use, adapt, and even commercialize their work without the obligation to open-source every change. This balance has encouraged widespread adoption in industry while keeping the project open and community-driven.

FreeBSD also has a **strong sense of stewardship**. Developers in the project are not just writing code for today’s needs; they are maintaining a system that has been evolving for decades, with careful attention to long-term stability and clean design. This mindset makes it an excellent environment for learning, because decisions are not made hastily but with a view of how they will shape the system years into the future.

For beginners, this means FreeBSD is not only a robust system to use but also a *teacher*. Exploring its tools, its source code, and its documentation provides lessons in how operating systems are built and how good software communities sustain themselves.

### Kernel Insight

Did you know? Apple’s macOS and iOS draw heavily from BSD code, including parts of FreeBSD. When you browse the web on an iPhone or MacBook, you’re relying on decades of BSD engineering that has been tested and trusted in countless environments.

This lineage highlights an important truth: BSD is not a relic of the past. It is living technology, still shaping the systems people use every day. FreeBSD, in particular, has remained fully open and community-driven, offering the same foundation of reliability that large companies use, but without hiding it behind closed doors. When you study FreeBSD, you are looking at the same DNA that runs through some of the world’s most advanced operating systems.

### Clearing Misconceptions

One common misunderstanding is to assume that FreeBSD is *"just another Linux"*. It is not. While both share UNIX roots, FreeBSD takes a very different approach.

Linux is a kernel combined with userland tools assembled from many independent projects. FreeBSD, on the other hand, is developed as a **complete operating system**, where the kernel, libraries, compiler toolchain, and core utilities are maintained together under a single project. This unified design makes FreeBSD feel consistent and cohesive, with fewer surprises when moving between components.

Another misconception is that FreeBSD is *"only for servers"*. While it is indeed trusted in server environments, FreeBSD also runs on desktops, laptops, and embedded systems. Its flexibility is part of what has allowed it to survive and evolve for decades while other UNIX variants have disappeared.

### Real-World Uses

FreeBSD is everywhere, though often invisibly. When you stream a show from Netflix, there is a good chance that FreeBSD is delivering the content to you through Netflix's global content delivery network. Networking companies like Juniper and storage providers like NetApp build products on top of FreeBSD, trusting its stability for their customers.

Even closer to home, FreeBSD powers firewalls, NAS devices, and appliances you might have in your office or living room. Projects like pfSense and FreeNAS (now TrueNAS) are based on FreeBSD, bringing enterprise-grade networking and storage into homes and small businesses worldwide.

And of course, FreeBSD has a long tradition in **research and education**. Universities use it in computer science curricula and labs, where having open access to the complete source code of a production-grade operating system is invaluable. Whether you realize it or not, you’ve probably already depended on FreeBSD in your daily life.

### Why FreeBSD for Driver Development?

For someone learning about device drivers, FreeBSD offers a rare balance. Its codebase is modern and production-ready, but it is also clean and approachable compared to many alternatives. Developers often describe FreeBSD’s kernel as "readable," a trait that matters greatly when you are just starting out.

The project also has a strong tradition of documentation. The FreeBSD Handbook, man pages, and developer guides provide a level of guidance that is hard to find in other open source kernels. This means you won’t be left guessing how things fit together.

For professionals, FreeBSD is more than a teaching tool. It is widely respected in areas where performance, networking, and storage are critical. Learning how to write drivers here prepares you for work that extends far beyond FreeBSD itself; it builds skills in systems programming, debugging, and hardware interaction that are transferable to many platforms.

Perhaps most importantly, FreeBSD encourages learning by participation. Its open, collaborative culture welcomes contributions from beginners and experts alike. Starting here, you’re not just learning in isolation; you are stepping into a community that values clarity, quality, and curiosity.

## Drivers and the Kernel - A First Glimpse

Now that you know why FreeBSD matters, let’s peek at the world you are about to explore.

At the heart of every operating system lies the **kernel**, the core that never sleeps. It orchestrates memory, manages processes, and directs communication with hardware. Most users never notice it, yet every keystroke, network packet, and disk read depends on its decisions.

### Why Drivers Matter

A kernel without drivers would be like a conductor without musicians. Drivers are the interpreters that let hardware and software communicate with each other. Your keyboard, network card, and graphics adapter are just silent pieces of silicon until a driver tells the kernel how to use them. Without drivers, even the most powerful hardware is nothing more than inert circuitry.

### Everyday Technology, Powered by Drivers

Drivers are everywhere, working quietly in the background. When you plug in a USB stick and see it appear on your desktop, that’s a driver at work. When you connect to Wi-Fi, adjust your screen brightness, or listen to sound through your headphones, all of these actions depend on drivers. They are invisible to most users, but they are what make computers feel alive and responsive.

Think about it: every modern convenience of computing, from cloud servers processing millions of requests per second to the phone in your pocket, depends on the unseen labour of device drivers.

### Beginner’s Tip

If terms like *kernel*, *driver*, or *module* feel abstract right now, don’t worry. This chapter is just the trailer, a preview of the whole story. In the coming chapters, we’ll break these ideas down step by step until the pieces start to click together.

### A Teaser of FreeBSD’s Kernel World

One of FreeBSD’s strengths is its **modular design**. Drivers can be loaded and unloaded dynamically as kernel modules, giving you the freedom to experiment without rebuilding the entire system. This flexibility is a gift for learners: you can try out code, test it, and remove it when you are done.

In this book, you will begin with the simplest form of drivers, character drivers, and progressively move toward more complex subsystems like PCI devices, USB peripherals, and even high-performance features such as DMA.

For now, hold on to this simple truth: **drivers are the bridge between possibility and reality in computing.** They turn abstract code into working hardware, and by learning how to write them, you are learning how to connect ideas with the physical world.

## Your Journey Through This Book

This book is not just a reference; it is a guided journey, designed to take you step by step from the very basics to advanced concepts in FreeBSD driver development. You won’t just read, you’ll practice, experiment, and build real code along the way.

### Who This Book Is For

This book was written with inclusivity in mind, especially for readers who may feel that systems programming is beyond their reach. It is for:

- **Beginners** who may know little about C, UNIX, or kernels, but are willing to learn.
- **Developers** who are curious about how operating systems work under the hood.
- **Professionals** who already use FreeBSD (or similar systems) and want to deepen their knowledge by learning how drivers are actually built.

If you bring curiosity and persistence, you will find a path here that starts where you are and builds your confidence chapter by chapter.

### Who This Book Isn’t For

Not every book fits every reader, and that is intentional. This book may not be the right fit if:

- You are looking for a **quick copy-and-paste manual**. This journey emphasizes understanding and practice, not shortcuts.
- You are already a **seasoned kernel developer**. The pace starts from the ground up, introducing fundamentals carefully before moving on to complex topics.
- You expect a **comprehensive hardware reference manual**. This is not an encyclopedic listing of every device or bus specification. Instead, it focuses on practical, real-world driver development in FreeBSD.

### What You’ll Learn and Gain

This book gives you a structured, hands-on path into FreeBSD driver development. Along the way, you will:

- Begin with the foundations: installing FreeBSD, learning UNIX tools, and writing C programs.
- Progress into building and loading your own drivers.
- Explore concurrency, synchronization, and direct interaction with hardware.
- Learn to debug, test, and integrate your drivers into the FreeBSD ecosystem.

By the end, you will not only know how FreeBSD drivers are written, but also have the confidence to keep exploring and perhaps even contribute your own work back to the community.

### The Learning Path Ahead

This book is organized as a journey, with each part building on the one before it. You will start with the essentials, learning the FreeBSD environment, basic UNIX tools, and C programming fundamentals before gradually stepping into kernel space and driver development. From there, the path expands into more advanced areas of practice:

- **Part 1:** Foundations: FreeBSD, C, and the Kernel
- **Part 2:** Building Your First Driver
- **Part 3:** Concurrency and Synchronization
- **Part 4:** Hardware and Platform-Level Integration
- **Part 5:** Debugging, Tools, and Real-World Practices
- **Part 6:** Writing Transport-Specific Drivers
- **Part 7:** Mastery Topics: Special Scenarios and Edge Cases
- **Appendices:** Quick references, extra exercises, and resources

Along this path, you will reach important milestones. You will write and load your own kernel modules, build character drivers, and explore how FreeBSD handles PCI, USB, and networking. You will also learn how to debug and profile your code, and how to submit your work upstream to the FreeBSD Project.

When I first started writing drivers, I felt intimidated. *"Kernel programming"* sounded like something reserved for experts in dark rooms full of servers. The truth is more straightforward. It is still programming, only with more explicit rules, greater responsibility, and a bit more power. Once you understand that, the fear gives way to excitement. That is the spirit in which this learning path was designed: approachable, progressive, and rewarding.

## How to Get the Most from This Book

Learning kernel programming is not just about reading; it’s about patience, practice, and persistence. The following principles will help you make the most of this journey.

### Build the Right Mindset

At first, kernels and drivers may feel overwhelming. That’s normal. The secret is to take things one step at a time. Don’t rush through chapters. Let each concept settle, and give yourself room to experiment and make mistakes.

### Approach Exercises Seriously

This is a hands-on book. Every chapter includes labs and tutorials designed to turn abstract ideas into real experience. The only way to truly learn kernel programming is by doing it yourself, typing the code, running it, breaking it, and fixing it again.

### Expect Challenges and Learn from Mistakes

You will face errors, failed compilations, and perhaps even a kernel panic or two. That is not failure; it is part of the process. Some mistakes will be small, others frustrating, but each one is an opportunity to refine your understanding and grow stronger. The most successful developers are not those who avoid mistakes, but those who persist, turning setbacks into stepping stones.

### Beginner’s Tip

Don’t be afraid of mistakes; treat them as milestones. Every failed driver load or kernel panic is proof that you’re experimenting and learning. With practice, these mistakes will become your best teachers.

### Engage with the Community

FreeBSD is built by a community of volunteers and professionals who share their time and expertise. Don’t try to learn in isolation. Use the mailing lists, forums, and chat channels. Ask questions, share your progress, and contribute when you can. Becoming part of the community is one of the most rewarding ways to learn.

### Kernel Insight

One reason FreeBSD is an excellent learning platform is its modular driver system. You can write a small driver, load it into the kernel, test it, and unload it, all without rebooting the machine. This makes experimentation safer and faster than you might expect, and it lowers the barrier to trying out new ideas.

### Stay Motivated

Remember: you are not just learning to write code,  you are learning to shape how an entire operating system interacts with hardware. That is a rare and empowering skill. When progress feels slow, remind yourself that every small step is moving you closer to understanding and influencing the core of a modern operating system.

## Closing Words

This first chapter has been about setting the stage. You’ve walked through my journey, seen why FreeBSD is worth your attention, and caught a first glimpse of the role device drivers play in modern systems. The road ahead will bring challenges, but also the satisfaction of overcoming them step by step.

This book is, in many ways, the guide I wish I had when I was starting out. If it saves you even a fraction of the confusion I faced and gives you the same spark of excitement that kept me going, then it has already achieved its purpose.

So take a deep breath. You’re about to move from inspiration to action. In the next chapter, we’ll roll up our sleeves and set up your FreeBSD lab,  the environment where all your learning will take place.

In chemistry, I learned, the lab was where theory met practice. In this journey, your computer is the lab, and it’s time to prepare it.
