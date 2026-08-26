---
title: "向 FreeBSD 项目提交驱动程序"
description: "向 FreeBSD 贡献驱动程序的流程与指南"
partNumber: 7
partName: "精通主题：特殊场景与边缘情况"
chapter: 37
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "AI辅助翻译为简体中文"
language: "zh-CN"
estimatedReadTime: 150
---

# 向 FreeBSD 项目提交驱动程序

## 引言

如果您从头开始跟随本书学习，您已经走过了很长的路。您从没有任何内核知识开始，学习了 UNIX 和 C，了解了 FreeBSD 驱动程序的结构，亲手构建了字符驱动程序和网络驱动程序，探索了 Newbus 框架，并完成了整个精通主题部分：可移植性、虚拟化、安全性、FDT、性能、高级调试、异步 I/O 和逆向工程。您现在已经达到了这样一个阶段：可以坐在实验机器前，打开文本编辑器，为 FreeBSD 尚不支持的设备编写驱动程序。这是一项严肃的工程技能，而且来之不易。

本章是工作向外的转折点。到目前为止，您构建的驱动程序只存在于您自己的系统中。您使用 `kldload` 加载它们，测试它们，调试它们，完成后再卸载它们。它们对您有用，也许对从您的代码库复制它们的几位朋友或同事也有用。这已经是值得的工作。但是，只存在于您机器上的驱动程序只能服务于碰巧找到您机器的人。存在于 FreeBSD 源代码树中的驱动程序则服务于每一位 FreeBSD 用户，在每一个版本中，在驱动程序支持的每一个架构上，只要代码还在维护，就会一直如此。价值的放大是巨大的，随之而来的责任就是本章的主题。

FreeBSD 项目自 20 世纪 90 年代初以来一直在接受外部开发者的贡献。数千人提交过补丁；数百人最终成为了提交者。新代码进入源代码树的过程并非官僚式的障碍训练。它是一个审查工作流程，旨在保持使 FreeBSD 值得信赖的品质：代码一致性、长期可维护性、跨架构可移植性、法律清晰性、认真的文档编写和持续的维护。这些品质中的每一项都是审查者代表每一位运行 FreeBSD 的用户所守护的。当您提交驱动程序时，您是在请求项目对其承担长期责任。审查过程是项目确认驱动程序值得这份责任的方式，也是项目帮助您将驱动程序塑造成可以获得肯定答案的方式。

这种框架很重要。新贡献者常常带着对立的期望来到审查过程，期望审查者寻找拒绝工作的理由。现实恰恰相反。审查者绝大多数情况下是在提供帮助。他们希望您的驱动程序被合并。他们希望它以一种在五个版本后仍然有效的形式被合并。他们希望它不会成为明年必须修改周围代码的人的维护负担。他们在第一轮补丁上留下的评论不是分数；它们是一个清单，当这些问题得到解决时，补丁就可以提交了。一个内化了这种框架的贡献者会发现审查过程是合作的而非充满压力的。

然而，有一个区别需要从一开始就弄清楚。一个能工作的驱动程序与一个准备好上游提交的驱动程序不是同一回事。一个能在您的笔记本电脑上加载、驱动您的硬件、卸载时不会崩溃的驱动程序只通过了前几个检查点。要准备好上游提交，它还需要通过项目的风格指南，携带适当的许可证，附带解释用户如何与之交互的手册页，在项目支持的每一个架构上构建，干净地集成到现有源代码树的布局中，并附带一个五年后其他审查者无需重建上下文就能读懂的提交消息。这些都不是形式主义。每一项的存在都是因为经验表明了那些跳过它们的代码库会发生什么。

本章围绕一个自然的工作流程组织。我们将首先从贡献者的角度看 FreeBSD 项目是如何组织的，贡献者和提交者之间的区别在实践中意味着什么。然后我们将演练为提交准备驱动程序的机械工作：使用什么文件布局，遵循什么代码风格，如何命名事物，如何编写让审查者感激的提交消息。我们将讨论许可和法律兼容性，因为即使是优秀的代码，如果其来源不清晰，也无法被接受。我们将花大量时间讨论手册页，因为手册页是驱动程序面向读者的一面，它值得与代码同样的关注。我们将演练测试期望，从本地构建到 `make universe`，我们将看到如何以审查者方便的形式生成补丁。我们将讨论审查过程的社交方面：如何与导师合作，如何回应反馈，如何在不失去动力的情况下迭代审查轮次。我们将以最持久的承诺结束，即驱动程序合并后的维护。

本章的配套代码位于 `examples/part-07/ch37-upstream/`，包含几个实用工件：一个参考驱动程序树布局，镜像了一个小型驱动程序在 `/usr/src/sys/dev/` 中的形态；一个示例手册页，您可以改编用于自己的驱动程序；一个提交前检查清单，您可以在发送补丁前用作最终审查；一封发给项目邮件列表的邮件草稿封面信；一个帮助脚本，以项目期望的约定生成补丁；以及一个提交前验证脚本，按正确顺序运行 lint、风格和构建检查。这些都不能替代理解底层材料，但它们将帮助您避免那些让首次贡献者浪费一两轮审查的常见错误。

在开始之前还有一点说明。本章不会教您 FreeBSD 项目的政治或治理历史。我们将涉及核心团队和各种委员会的角色，只限于贡献者导航项目所需的程度。如果您在阅读本章后对 FreeBSD 治理感兴趣，项目自己的文档是正确的下一步，我们将为您指明方向。本章的范围是将您编写的驱动程序转变为可以合并到上游的驱动程序的实际工作。

到本章结束时，您将清楚了解提交工作流程，对风格和文档约定有实际理解，对审查周期进行了排练，并对驱动程序进入源代码树后会发生什么有现实的认识。到本章结束时，您还不会成为 FreeBSD 提交者；项目仅在持续的高质量贡献历史后才授予提交权限，这是有意为之的设计。但您将知道如何做出第一次贡献，如何做好它，以及如何积累声誉，如果您选择追求这个方向，最终可能导向提交权限。

## 读者指南：如何使用本章

本章位于本书第 7 部分，紧接逆向工程章节，在结束章节之前。与许多前面的章节不同，这里的主题更多关于工作流程和纪律，而非内核内部。您不需要编写任何新的驱动程序代码来跟随本章，但如果您将所学应用于已经编写过的驱动程序，您将获益匪浅。

阅读时间适中。如果您不停下来尝试任何事情直接读下去，散文大约需要两到三个小时。如果您完成实验和挑战，请预留一个完整的周末或几个晚上。实验的结构是简短、集中的练习，您可以使用手头的任何小型驱动程序进行，包括前面章节的驱动程序之一、第 36 章的模拟驱动程序之一，或为本章编写的新驱动程序。

您不需要任何特殊硬件。FreeBSD 14.3 开发虚拟机，或一台您舒适运行构建和测试命令的裸机 FreeBSD 系统就足够了。实验将要求您对真实代码应用风格检查，将真实驱动程序构建为可加载模块，用 `mandoc(1)` 验证手册页，并对照一次性 git 分支排练补丁生成工作流程。不会触及真正的 FreeBSD Phabricator 或 GitHub，因此不存在意外向项目提交半成品工作的风险。

一个合理的阅读计划如下。在一个时段内阅读第 1 节和第 2 节；它们建立了 FreeBSD 开发如何运作以及驱动程序应如何布局的概念框架。休息一下。在第二个时段阅读第 3 节和第 4 节；它们涵盖许可和手册页，它们一起构成了提交的大部分文书工作。在第三个时段阅读第 5 节和第 6 节；它们涵盖测试和实际的补丁生成，这是本章从准备转向行动的地方。在第四个时段阅读第 7 节和第 8 节；它们涵盖贡献的人文和长期方面。实验最好在周末完成，如果第一遍暴露了您想改进的地方，有足够的时间重新做一遍。

如果您已经是一位自信的 FreeBSD 用户和自信的内核开发者，本章的材料在整体形态上会感觉熟悉，但在具体细节上可能仍然会让您惊讶。具体细节很重要。熟悉源代码树的审查者会在几秒钟内注意到文件布局是否符合约定，版权头部是否采用当前推荐的形式，手册页是否使用现代 mdoc 惯用法，提交消息是否遵循预期的主题行风格。提前把这些小事做对，决定了一次审查是一轮还是五轮的区别。

如果您是初学者，不要让具体细节吓倒您。项目中的每一位提交者都曾经是第一次补丁经过五轮审查才通过的人。编写好代码的能力是您在本书中已经建立起来的。提交好代码的能力是本章要增加的。您不会在第一次尝试时就做到完美。这很正常。重要的是您理解流程的形态，并且您以改进提交而非为其辩护的意图来对待每次审查。

本章的几项指南，特别是关于许可、手册页和审查工作流程的指南，反映了截至 FreeBSD 14.3 时 FreeBSD 项目的状态。项目在发展，一些具体约定可能随时间变化。在我们知道某个约定正在变化的地方，我们会标明。在我们引用源代码树中特定文件的地方，我们会命名该文件以便您自己打开并验证当前状态。信任但也验证的读者是项目最能从中受益的读者。

关于节奏的最后一点说明。本章刻意教授纪律和流程。几个部分在对小细节的坚持上几乎显得重复：尾随空格、正确的头部注释、精确的手册页宏用法。这种坚持是课程的一部分。FreeBSD 是一个拥有长期制度记忆的大型代码库，小细节是保持其可维护性的关键。如果您发现自己想要略读风格部分，请反而放慢速度。这种慢节奏就是技艺。

## 如何从本章获得最大收益

本章的组织方式适合线性阅读，但每一节都足够独立，您可以在需要时回到特定部分。几个习惯将帮助您吸收材料。

首先，在屏幕上打开 FreeBSD 源代码树阅读每一节。每次本章提到参考文件如 `/usr/src/share/man/man9/style.9` 时，打开它并略读。本章给您的是形态和动机；参考文件给您的是权威细节。将您在本章中读到的内容与源代码树实际内容进行交叉检查的习惯将在您作为 FreeBSD 贡献者的整个职业生涯中为您服务。

> **关于行号的说明。** 当本章指向源代码树中某个命名的基础设施，如 `make_dev_s`、`DRIVER_MODULE` 或 `style(9)` 规则本身时，指针锚定在该名称上。您稍后将看到的 `mydev.c:23` 风格检查记录指的是您自己正在进行中的驱动程序中的行，会随着您编辑而变化。无论哪种情况，持久的参考是符号，而不是数字：grep 名称而非依赖行号。

其次，在阅读时保留一个小笔记文件。每次某一节提到约定、必需部分或命令时，把它写下来。到本章结束时，您将拥有一个个性化的提交检查清单。配套的 `examples/part-07/ch37-upstream/` 目录包含一个检查清单模板，您可以从它开始，但您自己用自己的话输入的检查清单将比任何模板都更有用。

第三，在阅读时心中有一个自己的小型驱动程序。它可以是前面章节的 LED 驱动程序、第 36 章的模拟设备，或您为练习编写的字符驱动程序。本章将要求您想象为提交准备那个特定的驱动程序。针对具体驱动程序工作使指导比抽象吸收更加牢固。

第四，不要跳过实验。本章的实验简短而实用。大多数只需不到一小时。它们存在是因为提交过程的某些部分只有在对真实代码尝试后才会变得清晰。完成实验的读者将获得真正的肌肉记忆；跳过实验的读者将在六个月后重读本章时发现大部分内容没有留下来。

第五，把早期错误当作训练的一部分。第一次对代码运行 `tools/build/checkstyle9.pl` 时，您会看到警告。第一次对手册页运行 `mandoc -Tlint` 时，您会看到警告。第一次对源代码树运行 `make universe` 时，您至少会在一个架构上看到错误。每一个警告都在教您一些东西。项目中的审查者每天都在看到这些相同的警告；准备提交的技艺，在很大程度上，就是在其他人必须看到之前注意并修复它们的技艺。

最后，对本章的节奏保持耐心。后面几节花时间讨论看似社交或人际关系的材料：如何处理反馈，如何回应误解您补丁的审查者，如何建立导向赞助的关系。这些材料不是可选的。开源内核项目级别的软件工程是一门协作技艺，协作就是技艺本身。粗心地阅读这些部分将比粗心地阅读风格部分在实践中付出更大的代价。

您现在有了地图。让我们转向第一节，从贡献者的角度看 FreeBSD 项目是如何组织的。

## 第 1 节：理解 FreeBSD 贡献流程

### FreeBSD 项目实际上是什么

在讨论如何向 FreeBSD 项目贡献之前，我们需要清楚地了解项目是什么。FreeBSD 项目是一个志愿者和有偿贡献者社区，他们共同开发、测试、文档编写、发布和支持 FreeBSD 操作系统。该项目自 1993 年以来一直持续活跃。它围绕一组共享的源代码树、代码审查文化、发布工程纪律和关于内核、用户态、ports 和文档应该如何组合的机构知识体系组织而成。

该项目通常用三个词概括：源代码、ports 和文档。这对应三个主要的代码库或子项目，每个都有自己的维护者、审查者和约定。源代码，通常写作 `src`，是基础系统：内核、库、用户态工具，FreeBSD 安装附带的每一样东西。Ports 是可以在 FreeBSD 上构建的第三方软件集合，如编程语言、桌面环境和应用服务器。文档是手册、文章、书籍（如 Porter's Handbook 和 Developer's Handbook）、FreeBSD 网站和翻译基础设施。

设备驱动程序主要存在于 `src` 树中，因为它们是基础系统内核和对硬件的基础系统支持的一部分。当本章谈论提交驱动程序时，它是指提交到 `src` 树。Ports 和文档有自己的贡献管道，遵循类似的原则但细节不同。本章专门关注 `src`。

`src` 树很大。您可以通过浏览 `/usr/src/` 看到其顶层结构。手册页 `/usr/src/share/man/man7/development.7` 提供了对开发流程的简短、可读的介绍，文件 `/usr/src/CONTRIBUTING.md` 是项目自己当前的贡献者指南。如果在第一次提交前只读两个文件，就读那两个。我们将在本章中反复引用两者。

### 项目的决策结构

与其他一些大型项目相比，FreeBSD 使用相对扁平的决策结构。结构的核心是提交者群体，即对源代码仓库有写权限的开发者。提交者是在持续的高质量贡献历史后被现有提交者选举产生的。一个名为核心团队的九人选举机构处理某些类型的项目范围决策和争议。较小的团队如发布工程团队（re@）、安全官团队（so@）、Ports 管理团队（portmgr@）和文档工程团队（doceng@）负责特定领域。

就提交驱动程序而言，该结构的大部分在日常实践中并不重要。审查您驱动程序的人是个别提交者，他们恰好了解您驱动程序所适应的子系统。如果您的驱动程序是网络驱动程序，审查者可能是网络子系统的活跃人物。如果是 USB 驱动程序，审查者将是 USB 领域的活跃人物。核心团队不参与个别驱动程序提交；发布工程团队也不参与，尽管他们将决定您的驱动程序首次出现在哪个版本中，一旦它被合并。

实际的心理模型是这样的。FreeBSD 项目是一个大型工程师社区。其中一些人可以直接向源代码树提交。数量更多的人通过审查流程贡献。当您提交驱动程序时，您成为那个更大数量的一部分，审查流程是提交者社区评估驱动程序是否准备好以他们的共同责任进入源代码树的方式。

### 贡献者与提交者

贡献者和提交者之间的区别是项目运作的核心，也常被新来者误解。

贡献者是任何向项目提交更改的人。您第一次打开 Phabricator 审查或针对 FreeBSD 源代码树的 GitHub 拉取请求时就成为贡献者。成为贡献者没有正式流程。您只需提交工作。如果工作良好，它会被审查、修改，并最终由提交者代表您提交到源代码树。提交在 `Author:` 字段中携带您的姓名和电子邮件，因此即使您没有自己推送，您也能获得代码的功劳。

提交者是已被授予对某个代码仓库直接写权限的贡献者。提交权限是在持续的高质量贡献历史后授予的，通常跨越数年，并且只有在现有提交者提名和相关提交者群体投票后才会授予。提交权限伴随着责任：您被期望审查其他人的补丁，参与项目讨论，并长期承担您提交的代码的所有权。

这两个角色不是声望的层级。它们是劳动的分工。贡献者专注于编写和提交好的补丁。提交者专注于审查、集成和维护源代码树。一个拥有单个高价值补丁的贡献者比一个不积极参与的提交者对项目更有价值。项目依赖两者。

对于本章，您应该把自己看作贡献者。您的目标是产生一个提交者可以审查、接受并提交的提交。如果多年后，您发现自己有长期的贡献历史和与项目的持续关系，提交权限的问题可能会自然产生。但那是以后的问题。这里的重点是将您的第一次贡献做好。

### src 工作如何组织

`src` 仓库是一个单一的 git 树。主分支，在 git 中令人困惑地称为 `main`，但在发布工程语言中也称为 CURRENT，是所有活跃开发发生的地方。更改首先提交到 `main`。然后，如果更改是错误修复或适合稳定版本的小功能，它可能会被精选回 `stable/` 分支之一，这些分支对应于主要的 FreeBSD 版本，如 14 和 15。版本本身是 `stable/` 分支上的标记点。

作为驱动程序贡献者，您的默认目标是 `main`。您的补丁应该应用于当前的 `main`，应该针对当前的 `main` 构建，并应该针对当前的 `main` 测试。如果驱动程序是 FreeBSD 14 用户也想要的东西，提交者可能会选择在它在 `main` 中存在一段时间后将提交精选回相关的 `stable/` 分支，但那是提交者的决定，不是您在提交中做出的决定。

git 仓库在 `https://cgit.freebsd.org/src/` 可见，也在 `https://github.com/freebsd/freebsd-src` 镜像。您可以从任一克隆。权威推送 URL，对于有提交权限的人来说，是 `ssh://git@gitrepo.FreeBSD.org/src.git`，但作为贡献者您不会直接推送。您将生成补丁并通过审查工作流程发送它们。

### 设备驱动程序在源代码树中的位置

大多数设备驱动程序存在于 `/usr/src/sys/dev/` 下。这个目录包含数百个子目录，每个驱动程序或设备系列一个。如果您浏览它，您将看到 FreeBSD 支持的硬件的横截面：以太网芯片、SCSI 适配器、USB 设备、声卡、I/O 控制器和一长串其他类别。

值得了解的一小部分现有驱动程序子目录：

- `/usr/src/sys/dev/null/` 用于 `/dev/null` 字符设备。
- `/usr/src/sys/dev/led/` 用于通用 LED 框架。
- `/usr/src/sys/dev/uart/` 用于 UART 驱动程序。
- `/usr/src/sys/dev/virtio/` 用于 VirtIO 系列。
- `/usr/src/sys/dev/usb/` 用于 USB 子系统和 USB 端设备驱动程序。
- `/usr/src/sys/dev/re/` 用于 RealTek PCI/PCIe 以太网驱动程序。
- `/usr/src/sys/dev/e1000/` 用于英特尔千兆以太网驱动程序系列。
- `/usr/src/sys/dev/random/` 用于内核随机数子系统。

某些类别的驱动程序存在于其他地方。角色更多是关于网络堆栈而非设备本身的网络驱动程序有时存在于 `/usr/src/sys/net/` 下。类文件系统设备和伪设备有时存在于 `/usr/src/sys/fs/` 下。特定架构的驱动程序有时存在于 `/usr/src/sys/<arch>/` 下。然而，对于大多数初学者提交，问题将是 `/usr/src/sys/dev/` 下的哪个子目录是正确的家，答案几乎总是显而易见的。如果您的驱动程序用于新的网络芯片，它可能属于 `/usr/src/sys/dev/` 下自己的子目录，如果它扩展现有系列，可能位于现有系列子目录内。如果是 USB 设备，您可能会发现它存在于 `/usr/src/sys/dev/usb/` 下。如果不确定，搜索现有源代码树寻找类似驱动程序通常会告诉您您的驱动程序属于哪里。

### 驱动程序的后半部分：内核构建集成

除了驱动程序源文件本身，合并到 FreeBSD 的驱动程序在 `/usr/src/sys/modules/` 下有第二个家。此目录包含让驱动程序作为可加载内核模块构建的内核模块 Makefile。对于 `/usr/src/sys/dev/<driverdir>/` 中的每个驱动程序，通常在 `/usr/src/sys/modules/<moduledir>/` 中有一个对应的目录，包含一个小 Makefile，告诉构建系统如何组装模块。我们将在第 2 节详细查看该 Makefile。

少数驱动程序有额外的集成点。作为默认内核一部分提供的驱动程序列在 `/usr/src/sys/<arch>/conf/GENERIC` 下的架构配置文件中。带有设备树绑定的驱动程序可能在 `/usr/src/sys/dts/` 下有条目。暴露可调 sysctl 或加载器变量的驱动程序需要在相关文档中有条目。

作为首次贡献者，您不需要一次担心所有这些集成点。典型驱动程序提交的最小集合是 `/usr/src/sys/dev/<driver>/` 下的文件、`/usr/src/sys/modules/<driver>/` 下的 Makefile 和 `/usr/src/share/man/man4/<driver>.4` 下的手册页。除此之外的都是增量。

### 审查平台

FreeBSD 目前通过多个渠道接受源代码贡献。`/usr/src/CONTRIBUTING.md` 文件明确列出了它们：

- 针对 `https://github.com/freebsd/freebsd-src` 的 GitHub 拉取请求。
- 在 `https://reviews.freebsd.org/` 的 Phabricator 代码审查。
- 在 `https://bugs.freebsd.org/` 的 Bugzilla 工单附件。
- 直接访问 git 仓库，仅限提交者。

这些渠道各有自己的约定和首选用例。

Phabricator 是项目的传统代码审查平台。它处理完整的审查工作流程：多轮反馈、修订历史、内联评论、审查者分配和准备提交的补丁。大多数重要补丁，包括大多数驱动程序提交，都通过 Phabricator。您会看到它被称为"审查 D12345"或类似，其中 `D12345` 是 Phabricator 差异修订标识符。

GitHub 拉取请求是一个日益被接受的提交途径，特别是对于小型、独立、无争议的补丁。`CONTRIBUTING.md` 文件明确指出，当更改限于少于约十个文件和少于约两百行，并且通过 GitHub CI 作业且范围有限时，GitHub PR 效果良好。典型的小型驱动程序符合这些界限；具有许多文件和集成点的大型驱动程序可能更适合通过 Phabricator 处理。

Bugzilla 是项目的错误跟踪器。如果您的驱动程序修复了特定的报告错误，附加到相应 Bugzilla 条目的补丁是它的正确归宿。如果驱动程序是新工作而非错误修复，Bugzilla 通常不是正确的起点，尽管审查者可能会要求您打开 Bugzilla 工单以便工作有跟踪编号。

对于首次驱动程序提交，Phabricator 或 GitHub 拉取请求都是合适的。许多贡献者从 GitHub PR 开始，因为工作流程熟悉，如果审查超出 GitHub 处理良好的范围，则切换到 Phabricator。我们将在第 6 节演练两个途径。

审查平台的格局确实随时间变化，本章描述的具体 URL、范围限制和首选途径可能被 `/usr/src/CONTRIBUTING.md` 或项目贡献页面的更改所取代。上述流程细节最后于 2026-04-20 与源代码树中的 `CONTRIBUTING.md` 核对。在准备首次提交之前，重新阅读当前的 `CONTRIBUTING.md` 和 FreeBSD 文档站点链接的提交者指南；如果它们与本章不一致，请信任项目的文件，而不是本书。

### 练习：浏览源代码树并识别类似驱动程序

在进入第 2 节之前，花半小时浏览 `/usr/src/sys/dev/` 并建立对 FreeBSD 驱动程序外观的直觉。

选择三到四个范围与您打算提交的驱动程序大致相似的驱动程序，或您在本书中构建的任何驱动程序。对于每个驱动程序，查看：

- 目录内容。有多少源文件？多少头文件？文件名是什么？
- `/usr/src/sys/modules/` 下对应的 Makefile。它在 `KMOD=` 和 `SRCS=` 中列出了什么？
- `/usr/src/share/man/man4/` 下的手册页。打开它并注意章节结构。
- 主要 `.c` 文件中的版权头部。注意其格式。

您在这个练习中不是试图记住任何东西。您是在建立基线直觉。在您查看了三到四个真实驱动程序时，源代码树的约定将感觉不那么抽象。当第 2 节讨论文件去向和命名方式时，这些建议将落在您已经建立的思维图景之上。这是吸收材料的正确方式。

### 第 1 节小结

FreeBSD 项目是一个围绕三个主要子项目组织的长期社区：src、ports 和文档。设备驱动程序存在于 src 树中，主要在 `/usr/src/sys/dev/` 下，相应的模块 Makefile 在 `/usr/src/sys/modules/` 下，手册页在 `/usr/src/share/man/man4/` 下。贡献通过审查流程进入源代码树，由活跃于相关子系统的提交者处理。贡献者和提交者之间的区别是劳动分工，而非声望层级。您作为首次贡献者的目标是产生一个提交者可以审查、接受并提交的提交。

有了这个框架，我们现在可以转向关于驱动程序在提交前应该是什么样子的机械问题。第 2 节逐步演练准备工作。

## 第 2 节：让您的驱动程序准备好提交

### 能工作的驱动程序与提交就绪驱动程序之间的差距

一个能在测试机器上加载、运行和干净卸载的驱动程序是一个能工作的驱动程序。一个 FreeBSD 提交者可以审查、合并和维护的驱动程序是一个提交就绪的驱动程序。两者之间的差距几乎总是比首次贡献者预期的要大，弥合差距就是本节的工作。

差距有三个部分。第一个是布局：文件去向、命名方式和与现有构建系统的集成。第二个是风格：代码如何格式化、命名和注释，以及与项目的 `style(9)` 指南的匹配程度。第三个是呈现：提交如何打包、提交消息说什么、补丁如何为审查组织。这些在您知道要寻找什么时都不难，但每一个都涉及一二十个约定，它们共同决定审查者的第一印象是顺畅还是崎岖。

在开始之前，请花点时间理解这些约定为什么存在。FreeBSD 有三十年的积累代码。在此期间，数千个驱动程序进入了源代码树。您初次遇到时感觉武断的约定，几乎每一种情况都是早期痛苦经验的结果，社区决定不再重复。一个防止错误或减少审查摩擦来源的约定，会多次收回成本。当您遵循约定时，您正在受益于三十年的制度记忆。当您忽略它们时，您正在志愿重新学习那些教训，并让您的审查者再次经历它们。

### 文件去向

对于源代码树中的独立驱动程序，典型布局如下。假设您的驱动程序名为 `mydev`，它驱动一个 PCI 附加的传感器板。

- `/usr/src/sys/dev/mydev/mydev.c` 是主要驱动程序源文件。对于小型驱动程序，这可能是唯一的源文件。
- `/usr/src/sys/dev/mydev/mydev.h` 是驱动程序头文件。如果您只有一个 `.c` 文件且其内部声明不需要暴露，您可能不需要此头文件。
- `/usr/src/sys/dev/mydev/mydevreg.h` 是定义硬件寄存器和位域的头文件的常用名称。此约定（使用 `reg` 后缀）在源代码树中广泛使用，将寄存器定义与驱动程序内部声明分离是良好实践。
- `/usr/src/sys/modules/mydev/Makefile` 是将驱动程序构建为可加载内核模块的 Makefile。
- `/usr/src/share/man/man4/mydev.4` 是手册页。

您可能遇到不遵循此确切布局的现有驱动程序。在当前约定建立之前的较旧驱动程序有时将所有东西放在一处，或使用不同的文件名。约定在继续演变。对于新驱动程序，遵循现代布局将为您节省审查摩擦。

### `mydev.c` 中有什么

主要源文件通常按顺序包含：

1. 版权和许可证头部，采用我们将在第 3 节涵盖的格式。
2. `#include` 指令，通常从 `<sys/cdefs.h>` 和 `<sys/param.h>` 开始，然后是您的驱动程序需要的其他内核头文件。
3. 前向声明和静态变量。
4. 驱动程序方法：`probe`、`attach`、`detach`，以及您的 `device_method_t` 表引用的其他任何内容。
5. 任何辅助函数。
6. `device_method_t` 表、`driver_t` 结构、`DRIVER_MODULE` 注册和 `MODULE_VERSION` 声明。现代 FreeBSD 驱动程序不再声明 `static devclass_t` 变量；当前的 `DRIVER_MODULE` 签名接受五个参数（名称、总线、驱动程序、事件处理器、事件处理器参数），总线代码为您管理设备类。

一个组织良好的驱动程序文件有一种有经验的 FreeBSD 读者能立即识别的可见节奏。方法在引用它们的表之前。静态辅助函数靠近使用它们的方法。注册宏在最后，这样文件从依赖项通过函数到注册线性阅读。

### 一个最小的驱动程序文件

作为入门指南，这里是一个 `mydev.c` 的最小形态。它不是完整的，但展示了审查者期望看到的结构元素。您在前面章节已经看到这些宏各自的机制；这里我们关注它们如何在页面上排列自己。

```c
/*
 * Copyright (c) 2026 Your Name <you@example.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the
 * following conditions are met:
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/mydev/mydev.h>

static int	mydev_probe(device_t dev);
static int	mydev_attach(device_t dev);
static int	mydev_detach(device_t dev);

static int
mydev_probe(device_t dev)
{
		/* match your PCI vendor/device ID here */
		return (ENXIO);
}

static int
mydev_attach(device_t dev)
{
		/* allocate resources, initialise the device */
		return (0);
}

static int
mydev_detach(device_t dev)
{
		/* release resources, quiesce the device */
		return (0);
}

static device_method_t mydev_methods[] = {
		DEVMETHOD(device_probe,		mydev_probe),
		DEVMETHOD(device_attach,	mydev_attach),
		DEVMETHOD(device_detach,	mydev_detach),
		DEVMETHOD_END
};

static driver_t mydev_driver = {
		"mydev",
		mydev_methods,
		sizeof(struct mydev_softc),
};

DRIVER_MODULE(mydev, pci, mydev_driver, 0, 0);
MODULE_VERSION(mydev, 1);
MODULE_DEPEND(mydev, pci, 1, 1, 1);
```

几件事值得注意。版权头部以普通的 `/*` 注释开头，先写版权行，然后用 SPDX 行明确命名许可证。缩进使用制表符而非空格，正如 `style(9)` 要求的那样。函数声明使用制表符分隔，同样符合 `style(9)`。`DRIVER_MODULE` 和相关宏出现在底部，按构建系统期望的顺序。这是审查者期望看到的形态。

### 模块 Makefile

模块的 Makefile 通常很小。这里是一个现实的示例，模仿 `/usr/src/sys/modules/et/Makefile`：

```makefile
.PATH: ${SRCTOP}/sys/dev/mydev

KMOD=	mydev
SRCS=	mydev.c
SRCS+=	bus_if.h device_if.h pci_if.h

.include <bsd.kmod.mk>
```

几个约定编码在这个简短文件中。

`SRCTOP` 是一个构建系统变量，指向源代码树的顶部。使用它意味着 Makefile 无论在树中何处调用构建都能工作。不要硬编码 `/usr/src`。

`KMOD` 命名模块。这是 `kldload` 使用的内容。将其与驱动程序名称匹配。

`SRCS` 列出源文件。`.c` 文件是您的驱动程序源。看起来像 `bus_if.h` 和 `pci_if.h` 的 `.h` 文件不是常规头文件；它们是由构建系统从对应 `.m` 文件中的方法定义自动生成的。您列出它们以便构建系统知道在编译驱动程序之前生成它们。包含 `device_if.h` 因为每个驱动程序都使用 `device_method_t`；包含 `bus_if.h` 如果您的驱动程序使用 `bus_*` 方法；包含 `pci_if.h` 如果它是 PCI 驱动程序；以此类推。

`bsd.kmod.mk` 是标准的内核模块构建基础设施。在最后包含它给您所需的所有构建规则。

还有几项额外约定：

- 不要向琐碎 Makefile 添加版权头部。源代码树约定是像这样的小 Makefile 被视为机械文件，不携带许可证。具有实质逻辑的真实 Makefile 确实携带版权头部。
- 不要使用 GNU `make` 特性。FreeBSD 的基础构建系统使用源代码树中的 BSD make，而非 GNU make。
- 规则体保持使用制表符而非空格进行缩进。

### 头文件

如果您的驱动程序有用于内部声明的头文件，将其放在与 `.c` 文件相同的目录中。约定是将内部头命名为 `<driver>.h`，任何硬件寄存器定义命名为 `<driver>reg.h`。保持头文件范围狭窄。它应该声明在驱动程序的多个 `.c` 文件间使用的结构和常量，或与密切相关的子系统互操作所需的声明。它不应该将驱动程序内部细节泄漏到更广泛的内核命名空间中。

头文件以与 `.c` 文件相同的版权头部开始，然后是标准的包含保护：

```c
#ifndef _DEV_MYDEV_MYDEV_H_
#define _DEV_MYDEV_MYDEV_H_

/* header contents */

#endif /* _DEV_MYDEV_MYDEV_H_ */
```

包含保护名称遵循完整路径的约定，全大写，斜杠和点替换为下划线，开头和结尾加下划线。此约定在源代码树中一致，审查者会发现偏离。

### 遵循 `style(9)`：简短总结

完整的 FreeBSD 编码风格记录在 `/usr/src/share/man/man9/style.9`。您应该在提交驱动程序之前阅读该手册页，并在您的风格成熟时定期略读。这里我们将提取最容易让首次贡献者困惑的点。

#### 缩进和行宽

缩进使用真正的制表符，制表位为 8 列。未对齐到制表位的第二级和后续级缩进使用 4 个空格的额外缩进。行宽为 80 列；当断行会降低可读性或破坏被 grep 搜索的东西（如 panic 消息）时，允许少数例外。

#### 版权头部形式

版权头部是一个从第 1 列开始、以普通 `/*` 开头的多行注释。较旧的文件以 `/*-` 开头，那个破折号曾是许可证收集工具依赖的标记；`style(9)` 已不再要求新代码使用它。头部最先的重要行是一或多行 `Copyright`。版权行之后是 `SPDX-License-Identifier:` 后跟 SPDX 许可证代码，如 `BSD-2-Clause`。然后是可选的许可证文本。

#### 函数声明和定义

函数返回类型和存储类放在函数名之上的行。函数名从第 1 列开始。参数与名称放在同一行，除非超过 80 列，在这种情况下后续参数对齐到开括号。

正确：

```c
static int
mydev_attach(device_t dev)
{
		struct mydev_softc *sc;

		sc = device_get_softc(dev);
		return (0);
}
```

不正确，正如审查者会立即标记：

```c
static int mydev_attach(device_t dev) {
    struct mydev_softc *sc = device_get_softc(dev);
    return 0;
}
```

差异看起来很小：返回类型位置、开括号位置、使用空格而非制表符、单行声明并初始化、返回值缺少括号。每一个差异都违反 `style(9)`。它们共同使函数在源代码树中显得格格不入。审查者会要求您修复它们，事后修复比第一次正确编写更多工作。

#### 变量名和标识符约定

使用带下划线的小写标识符而非驼峰命名法。`mydev_softc`，而非 `MydevSoftc` 或 `mydevSoftc`。函数遵循相同约定。

常量和宏使用带下划线的大写：`MYDEV_REG_CONTROL`、`MYDEV_FLAG_INITIALIZED`。

驱动程序中全局变量很少；偏好使用 softc 中的每设备状态。当不可避免需要全局变量时，给它一个以驱动程序名称为前缀的名称，以避免与内核其余部分冲突。

#### 返回值括号

FreeBSD 风格要求 `return` 表达式使用括号：`return (0);` 而非 `return 0;`。这是追溯到原始 BSD 内核的约定，并相当严格地执行。

#### 注释

多行注释使用以下形式：

```c
/*
 * This is the opening of a multi-line comment.  Make it real
 * sentences.  Fill the lines to the column 80 mark so the
 * comment reads like a paragraph.
 */
```

单行注释可以使用传统的 `/* ... */` 或 `// ...` 形式。在文件内保持一致；不要混合风格。

注释应该解释为什么，而非什么。`/* iterate over the array */` 当读者可以看到循环时无用。`/* the hardware requires a read-back to flush the write before we proceed */` 有用，因为它解释了非显而易见的约束。

#### 错误消息

使用 `device_printf(dev, "message\n")` 用于设备特定日志输出。如果您手头有 `device_t`，不要从驱动程序直接使用 `printf`；`device_printf` 在消息前加上驱动程序和单元号，这是每个阅读内核日志的人期望看到的。

需要被 grep 搜索的错误消息应该保持在一行，即使超过 80 列。`style(9)` 手册页对此有明确规定。

#### 魔法数字

不要在代码体中使用魔法数字。硬件寄存器偏移、位掩码和状态代码应该是 `<driver>reg.h` 头文件中的命名常量。这使代码可读，并使在不可避免发现某些东西稍微有偏差时易于修补寄存器定义。

### 使用 `tools/build/checkstyle9.pl`

项目提供了一个位于 `/usr/src/tools/build/checkstyle9.pl` 的自动风格检查器。它是一个 Perl 脚本，读取源文件并警告常见风格违规。它不完美，一些警告将是假阳性或反映脚本不完全正确的约定，但它捕捉了大部分容易犯的错误。

在提交前对您的驱动程序运行它：

```sh
/usr/src/tools/build/checkstyle9.pl sys/dev/mydev/mydev.c
```

您将看到如下输出：

```text
mydev.c:23: missing blank line after variable declarations
mydev.c:57: spaces not tabs at start of line
mydev.c:91: return value not parenthesised
```

修复每个警告。重新运行。重复直到输出干净。

`CONTRIBUTING.md` 文件对此有明确规定："在您的 Git 分支上运行 `tools/build/checkstyle9.pl` 并消除所有错误。"审查者不想成为您的风格检查器。提交未经过 `checkstyle9.pl` 的代码浪费了他们的时间。

### 小心使用 `indent(1)`

FreeBSD 还提供了 `indent(1)`，一个 C 源代码重新格式化工具。它可以自动将文件重新格式化以符合 `style(9)` 的部分。它有用但非魔法。`indent(1)` 能很好地处理一些风格规则，如制表符缩进和括号位置，但对其他规则处理不佳或不处理，在某些情况下它通过以违反 `style(9)` 的方式重新格式化注释或函数签名使情况更糟，即使输入是正确的。

将 `indent(1)` 视为粗略的第一遍而非规范格式化工具。在文件上运行它以接近符合，然后仔细阅读输出并修复它出错的地方。不要作为无关补丁的一部分在现有源代码树文件上运行它；将风格更改与功能更改混合是审查反模式。

### 提交消息

好的提交消息做两件事。首先，它一眼告诉读者提交做了什么。其次，它更详细地告诉读者为什么提交这样做。主题行是第一项；正文是第二项。

FreeBSD 源代码树中的主题行约定如下：

```text
subsystem: Short description of the change
```

`subsystem` 前缀告诉读者哪部分源代码树受到影响。对于驱动程序提交，前缀通常是驱动程序名称：

```text
mydev: Add driver for MyDevice FC100
```

冒号后第一个单词大写，描述是片段而非完整句子。主题行大约 50 字符上限，72 为硬限制。用 `git log --oneline` 查看源代码树中最近的提交以看模式：

```text
rge: add disable_aspm tunable for PCIe power management
asmc: add automatic voltage/current/power/ambient sensor detection
tcp: use RFC 6191 for connection recycling in TIME-WAIT
pf: include all elements when hashing rules
```

提交消息正文在空行之后。它更详细地解释更改：更改做了什么，为什么需要它，它影响什么硬件或场景，以及未来读者可能需要知道的任何考虑事项。正文在 72 列处换行。

驱动程序提交的好提交消息可能如下：

```text
mydev: Add driver for FooCorp FC100 sensor board

This driver supports the FooCorp FC100 series of PCI-attached
environmental sensor boards, which expose a simple command and
status interface over a single BAR.  The driver implements
probe/attach/detach following the Newbus conventions, exposes a
character device for userland communication, and supports
sysctl-driven sampling configuration.

The FC100 is documented in the FooCorp Programmer's Reference
Manual version 1.4, which the maintainer has on file.  Tested on
amd64 and arm64 against a hardware sample; no errata were
observed during the test period.

Reviewed by:	someone@FreeBSD.org
MFC after:	2 weeks
```

消息中的几块是标准的。`Reviewed by:` 命名签署审查的提交者。`MFC after:` 建议一个期限，在此之后提交可以从 CURRENT 合并回 STABLE（MFC 代表 Merge From Current）。您作为贡献者不填写这些行；提交您的补丁的提交者会添加它们。

您编写的是正文：解释更改的描述段落。像您为五年后在 `git log` 中看到提交并想知道它是什么的未来读者编写它们那样编写。那个读者可能是您，或者是您离开后维护您的驱动程序的人。让提交消息对他们友好。

### Signed-off-by 和开发者原创证书

对于 GitHub 拉取请求特别，`CONTRIBUTING.md` 文件要求提交包含 `Signed-off-by:` 行。此行证明位于 `https://developercertificate.org/` 的开发者原创证书，简而言之是声明您有权在项目许可证下贡献代码。

添加 `Signed-off-by:` 很容易：

```sh
git commit -s
```

`-s` 标志添加如下形式的行：

```text
Signed-off-by: Your Name <you@example.com>
```

到提交消息。使用与提交作者行相同的姓名和电子邮件。

### 完整的提交就绪源代码树是什么样的

在此之后，您的驱动程序在 FreeBSD 源代码树中的树应该大致如下：

```text
/usr/src/sys/dev/mydev/
		mydev.c
		mydev.h              (optional)
		mydevreg.h           (optional but recommended)

/usr/src/sys/modules/mydev/
		Makefile

/usr/src/share/man/man4/
		mydev.4
```

您应该能够用以下命令构建模块：

```sh
cd /usr/src/sys/modules/mydev
make obj
make depend
make
```

并用以下命令验证手册页：

```sh
mandoc -Tlint /usr/src/share/man/man4/mydev.4
```

并用以下命令运行风格检查器：

```sh
/usr/src/tools/build/checkstyle9.pl /usr/src/sys/dev/mydev/mydev.c
```

如果这三项都无错误完成，您的驱动程序在机械上已准备好提交。还有许可、手册页内容、测试和补丁生成需要涵盖，我们将在接下来的部分进行。但基本布局现已到位，审查者打开补丁时将发现文件名、文件布局、风格和构建集成符合他们在源代码树中期望看到的。

### 第 2 节准备中的常见错误

在结束本节之前，让我们收集首次贡献者在准备中最常犯的错误。将其作为进入第 3 节前的快速自我检查。

- 文件位置错误。驱动程序存在于 `/usr/src/sys/dev/<driver>/`，而非 `/usr/src/sys/` 顶部。模块 Makefile 存在于 `/usr/src/sys/modules/<driver>/`。手册页存在于 `/usr/src/share/man/man4/`。
- 文件名与驱动程序不匹配。如果驱动程序是 `mydev`，主要源文件是 `mydev.c`，而非 `main.c` 或 `driver.c`。
- 缺少或错误的版权头部。头部以 `/*` 开头，版权行在最前，SPDX 标识符紧随其后，许可证文本与项目接受的许可证之一匹配。
- 使用空格而非制表符。`style(9)` 明确要求制表符，风格检查器将立即标记空格缩进。
- `return` 表达式缺少括号。一个反复出现的小错误，风格检查器会捕捉。
- 变量声明和代码之间缺少空行。另一个小约定，风格检查器会捕捉。
- 提交消息未遵循 `subsystem: Short description` 形式。审查者会要求您重写它。
- 尾随空格。`CONTRIBUTING.md` 文件明确指出尾随空格是审查者不喜欢的东西。
- Makefile 硬编码 `/usr/src` 而非使用 `${SRCTOP}`。

当您知道要寻找什么时，每一项都是容易的修复。当您不知道时，每一项都会给审查增加一轮来回。本节的目标是给您在提交前捕捉所有这些的知识。

### 第 2 节小结

让驱动程序准备好提交与其说是聪明不如说是注重细节。文件布局、风格、版权头部、Makefile、提交消息：每一项都有约定的形式，文件符合这些约定的驱动程序是审查者第一印象为"这看起来正确"的驱动程序。那个第一印象比任何其他单一因素更能决定补丁需要多少轮审查。

我们还没有详细讨论许可证本身，也没讨论手册页，也没讨论测试。这些是接下来三节的主题。但首次贡献者最容易出问题的源代码树机械准备现已涵盖。

让我们接下来讨论许可和每次 FreeBSD 贡献的法律考虑。

## 第 3 节：许可和法律考虑

### 为什么许可证在一开始就重要

让您的驱动程序被拒绝的最简单方法是弄错许可证。许可在 FreeBSD 中不是程序偏好；它是项目运作方式的基础。FreeBSD 操作系统在允许用户无惊吓地依赖的宽松许可证组合下发布。携带不兼容许可证、或不明确许可证、或无许可证的贡献无法被接受到源代码树中，无论代码在其他方面多么优秀。

这不是为了形式主义而进行的形式主义。这是一个实际必要。FreeBSD 在许多环境中使用，包括发货给数百万用户的商业产品。那些用户依赖项目的许可证来理解他们的义务。源代码树中单个携带意外许可证的文件可能使整个项目的下游用户面临他们未签署的义务。项目无法接受这种风险。

对于您作为贡献者，实际的教训是：预先把许可证弄对。这比在审查过程标记它后试图修复要容易得多得多。本节将演练项目接受什么、不接受什么，以及如何构建版权头部使您的提交顺利通过许可检查。

### FreeBSD 接受什么许可证

FreeBSD 项目作为默认偏好两条款 BSD 许可证，通常写作 BSD-2-Clause。这是 FreeBSD 本身大部分使用的宽松许可证，是新代码的默认推荐。BSD-2-Clause 允许以源代码和二进制形式重新分发，无论是否修改，只要保留版权声明和许可证文本。它不对下游用户施加分发源代码的要求，不要求兼容性声明，也没有可能使商业使用复杂化的专利授权条款。

三条款 BSD 许可证 BSD-3-Clause 也被接受。它添加了一条禁止在背书中使用作者姓名的条款。一些较旧的 FreeBSD 代码使用此形式，在大多数实际用途上等效。

源代码树中还有少数其他宽松许可证用于历史原因贡献的特定文件。MIT 风格许可证和 ISC 许可证在某些地方出现。由 Poul-Henning Kamp 引入的 Beerware 许可证，一个诙谐的宽松许可证，也出现在少数文件中，如 `/usr/src/sys/dev/led/led.c`。这些许可证与 FreeBSD 的整体许可方案兼容，并在它们伴随的特定代码被接受。

对于您正在自己编写的新驱动程序，正确的默认选择是 BSD-2-Clause。除非您有特定理由使用不同的许可证，否则使用 BSD-2-Clause。这是您的审查者期望的许可证，任何偏离都会触发您在首次提交时可能不想进行的对话。

### FreeBSD 不接受什么许可证

几种许可证与 FreeBSD 源代码树不兼容，它们下的代码无法合并。首次贡献者有时尝试使用的最常见的包括：

- GNU 通用公共许可证（GPL），任何版本。GPL 代码与 FreeBSD 的许可模式不兼容，因为它对下游用户施加了源代码分发义务，而源代码树的其余部分没有这种义务。FreeBSD 在用户态确实包含一些 GPL 许可的组件，如 GNU 编译器集合，但这些是特定的历史案例，不是新贡献的模板。驱动程序代码特别是 GPL 下不被接受的。
- 宽通用公共许可证（LGPL）。与 GPL 相同的理由。
- Apache 许可证，版本 2 或其他，除非有具体讨论和批准。Apache 许可证包含专利授权条款，以复杂的方式与宽松的 BSD 许可证交互。某些 Apache 许可的代码在特定上下文中被接受，但它不是新代码的默认选择。
- 各种形式的 MIT 许可证，虽然技术上宽松，但不是 FreeBSD 的默认选择。如果您有特定理由使用 MIT，在提交前与审查者讨论。
- 任何专有许可证。源代码树不能接受许可证限制重新分发或修改的代码。
- 许可不明确的代码，包括从许可未知的其他项目复制的代码、许可条款不明确的工具生成的代码，以及没有明确许可证声明而贡献的代码。

如果您正在移植或改编来自其他开源项目的代码，在开始之前仔细检查源项目的许可证。将 GPL 许可项目中的代码带入您的驱动程序会污染驱动程序，使其无法进入 FreeBSD 源代码树。

### 详细了解版权头部

源代码树中每个源文件顶部的版权头部有特定的结构，记录在 `style(9)` 中。让我们演练一个完整的头部并检查每一部分。

```c
/*
 * Copyright (c) 2026 Your Name <you@example.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the
 * following conditions are met:
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
```

开头是从第 1 列开始的普通 `/*`。您在源代码树中许多文件的顶部仍会看到 `/*-`：那个破折号曾是自动许可证收集脚本的标记，`style(9)` 多年来一直要求它。当前的 `style(9)` 已取消这一要求，因此新代码以 `/*` 开头，并依靠 SPDX 行来标识许可证。

紧接着开头的是版权行，然后是 SPDX-License-Identifier 行。SPDX 是用于以机器可读形式描述许可证的标准化词汇。该行以一种无法被误解的形式告诉收集器文件使用什么许可证。两条款 BSD 许可证使用 `BSD-2-Clause`，三条款 BSD 许可证使用 `BSD-3-Clause`。对于其他许可证，请查阅 `https://spdx.org/licenses/` 的 SPDX 标识符列表。不要发明标识符。注意顺序：`style(9)` 规定版权行写在 SPDX 标识符之前。较旧的文件顺序相反；新代码不应如此。

版权行命名年份和版权持有人。使用您的完整法定姓名，或您雇主的姓名（如果您是在雇佣下贡献工作），后跟一个足够稳定的电子邮件地址，以便多年后仍能联系到您。如果您作为个人贡献，请使用您的个人电子邮件而非一次性地址。

如果文件有多个作者，可能存在多个版权行。当您添加版权行时，添加到现有列表的底部，而非顶部。不要删除任何其他人的版权行。现有归属具有法律意义。

许可证文本本身紧随其后。上面复制的文本是标准的 BSD-2-Clause 文本。不要修改它。措辞具有法律特定性，更改它，即使以看起来更清晰的方式，也可能使许可证在法律上与项目接受的不同。

最后，在闭合 `*/` 之后、代码开始之前有一空行。此空行是源代码树的约定，在 `style(9)` 中有说明。其目的是视觉上的。

### 阅读现有头部以建立直觉

内化许可证头部约定的最好方法是查看源代码树中的真实头部。打开 `/usr/src/sys/dev/null/null.c` 并阅读其头部。打开 `/usr/src/sys/dev/led/led.c` 并阅读其头部（它在 Beerware 许可证下，一个不寻常但被接受的案例）。打开 `/usr/src/sys/dev/re/` 或 `/usr/src/sys/dev/e1000/` 下的一两个网络驱动程序并阅读它们的。十五分钟内您将吸收模式。

您会注意到几件事：

- 源代码树中一些较旧的文件没有 SPDX 标识符。这些早于 SPDX 约定。对于新贡献，请使用 SPDX。
- 一些较旧的文件顶部仍然有 `$FreeBSD$` 标签。这是 CVS 时代的标记，自项目转向 git 以来不再活跃。新贡献不包含 `$FreeBSD$` 标签。
- 一些文件有跨越多年多个贡献者的多个版权行。这是正常且正确的。当您向现有文件添加版权行时，追加它。
- 少数文件有非标准许可证（Beerware、MIT 风格、ISC）。这些是历史性的，按个案接受。不要将它们作为新贡献的模板。

### 衍生作品和外部代码

如果您的驱动程序完全是自己创作的，头部很简单。如果它包含从其他项目衍生的代码，情况更复杂。

您从其他项目复制或改编的任何代码都带有该项目的许可证。如果项目的许可证与 BSD 兼容，您可以使用该代码，但必须保留原始版权声明并使改编可见。如果许可证与 BSD 不兼容，如 GPL，您完全不能使用该代码。

源代码树中衍生作品的约定是保留原始版权行并作为单独的行添加您自己的：

```c
/*
 * Copyright (c) 1998 Original Author <original@example.com>
 * Copyright (c) 2026 Your Name <you@example.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * [licence text]
 */
```

如果原始许可证是 BSD-3-Clause 而您在 BSD-2-Clause 下贡献添加，文件实际上是 BSD-3-Clause 整体，因为三条款要求通过衍生作品传递。在 SPDX 标识符中使用两者中更严格的许可证，或者如果代码明显可分离，则在每节级别保持单独的许可。如有疑问，询问审查者。

如果代码取自特定外部来源，其来源相关，请在相关函数附近用注释提及：

```c
/*
 * Adapted from the NetBSD driver at
 * src/sys/dev/foo/foo.c, revision 1.23.
 */
```

这帮助审查者和未来的维护者了解来源。它也帮助任何人将错误追踪到其上游修复。

### 来自供应商来源的改编代码

硬件驱动程序的常见场景是供应商以某种许可证提供示例代码或参考驱动程序。如果供应商的代码在 BSD 兼容许可证下，您可能能够直接使用它，可能经过改编，保留供应商的版权。仔细阅读供应商的许可证。如果许可证与 BSD 不兼容，您不能在注定要进入源代码树的驱动程序中使用供应商的代码。您可能能够使用供应商的文档作为独立实现驱动程序的参考，但不能复制代码。

如果供应商在保密协议（NDA）下提供文档，情况更加微妙。NDA 通常禁止您披露文档。它可能不禁止您使用文档编写代码，但结果代码必须是您自己的工作，而非供应商提供的任何代码的副本。认真保持这条界限清晰。如果有任何疑问，在未获得法律建议前不要继续。

### 您未编写但正在提交的代码

如果您正在提交其他人编写的代码，如同事的贡献，您需要他们的明确许可和他们在头部中的版权行。您不能在他人不知情的情况下代表他人贡献代码。项目要求的 `Signed-off-by:` 行部分是用于追踪此的机制；该行证明的开发者原创证书包括声明您有权贡献代码的声明。

如果您是公司员工贡献雇佣下完成的工作，您的雇主通常持有版权，而非您。版权行应该命名雇主。许多公司有批准开源贡献的内部流程；在提交前遵循这些。一些公司偏好让其员工与 FreeBSD 基金会签署贡献者许可协议（CLA）以便清晰；如果您的公司这样做，在提交前与您的公司协调。

### 向现有驱动程序添加许可证头部

如果您正在改装您已编写但从未准备提交的驱动程序，您需要向每个文件添加正确的头部。步骤如下：

1. 确定许可证。对于新驱动程序，使用 BSD-2-Clause。
2. 用您的姓名、电子邮件和首次创建年份写版权行。
3. 在版权行之后写 SPDX 标识符行。
4. 粘贴标准 BSD-2-Clause 许可证文本。
5. 验证开头是第 1 列的 `/*`。
6. 验证闭合 `*/` 后有一空行。
7. 对每个文件重复：`.c` 文件、`.h` 文件、手册页（其中许可证以 `.\"` 风格注释而非 `/*` 风格出现），以及任何其他包含实质内容的文件。

对于 Makefile，如前所述，琐碎文件传统上省略许可证头部。第 2 节中显示的模块 Makefile 足够琐碎不需要头部。

### 验证头部

没有单一自动化工具验证 FreeBSD 版权头部的每个方面。`checkstyle9.pl` 脚本捕捉头部附近的某些格式错误。源代码树中的许可证工具基于 SPDX 行工作。然而，最可靠的验证是将您的头部直接与源代码树中已知良好的头部比较，如 `/usr/src/sys/dev/null/null.c` 中的头部或任何最近添加的驱动程序。

建立一个小习惯：当您打开新的源文件时，粘贴已知良好的头部作为第一个动作。这防止完全忘记头部的容易错误，并确保形状从一开始就正确。

### 第 3 节小结

许可是在一开始就正确处理就能节省大量时间的地方之一。FreeBSD 项目接受 BSD-2-Clause、BSD-3-Clause 和少数其他用于历史文件的宽松许可证。新贡献应默认 BSD-2-Clause。版权头部使用特定形式，以 `/*` 开头，后跟一或多条版权行，后跟 SPDX 标识符，然后是标准许可证文本。从其他项目衍生的代码携带其原始许可证义务向前传递，衍生作品必须保留原始归属。您未自己编写的代码需要作者的许可和归属。

处理好法律方面，我们可以转向手册页。源代码树中的每个驱动程序都附带手册页，编写一个好的手册页是首次贡献者最常低估努力的地方之一。第 4 节演练约定并提供一个您可以改编的模板。

## 第 4 节：为您的驱动程序编写手册页

### 为什么手册页重要

手册页是驱动程序面向用户的一面。当有人在源代码树中发现您的驱动程序并想知道它做什么时，他们不会阅读源代码。他们会运行 `man 4 mydev`。他们看到的将是他们大多数人对您的驱动程序拥有的唯一文档。如果手册页清晰、完整、组织良好，用户将理解驱动程序支持什么、如何使用它及其局限性。如果手册页缺失、稀疏或组织不良，用户将困惑，他们会提交实际上是文档问题的错误报告，他们将合理地形成对驱动程序的负面印象。

从项目的角度看，手册页是贡献的第一级工件。没有手册页的驱动程序无法合并。手册页差的驱动程序将在审查中被延迟，直到手册页达到标准。您应该将手册页视为驱动程序的一部分，而非事后思考。

从实际角度看，编写手册页本身往往是一种有用的纪律。向用户解释驱动程序做什么、支持什么硬件、暴露什么可调参数以及已知局限性的行为，迫使您清晰地表达这些东西。编写好的手册页常常暴露驱动程序设计尚未解决的问题并非罕见。因此，编写手册页是完成驱动程序工作的一部分，而非驱动程序完成后的一步。

### 手册页章节：快速入门

FreeBSD 中的手册页组织成编号章节。章节如下：

- 第 1 节：通用用户命令。
- 第 2 节：系统调用。
- 第 3 节：库调用。
- 第 4 节：内核接口（设备、设备驱动程序）。
- 第 5 节：文件格式。
- 第 6 节：游戏。
- 第 7 节：杂项和约定。
- 第 8 节：系统管理和特权命令。
- 第 9 节：内核内部（API 和子系统）。

您的驱动程序属于第 4 节。手册页文件放在 `/usr/src/share/man/man4/` 下，传统命名为 `<driver>.4`，例如 `mydev.4`。`.4` 后缀是手册页约定；它将文件标记为第 4 节页面。

文件本身以 mdoc 宏语言编写，而非纯文本。Mdoc 是一个结构化宏集，从更或少人类可读的源文件产生格式化的手册页。项目的 mdoc 风格记录在 `/usr/src/share/man/man5/style.mdoc.5`；您应该在编写第一个手册页之前阅读该文件，虽然它说的很多在您尝试编写一个之后会更有意义。

### 第 4 节手册页的结构

第 4 节手册页有成熟的结构。以下章节大致按此顺序出现：

1. `NAME`：驱动程序名称和一行描述。
2. `SYNOPSIS`：如何将驱动程序包含在内核中或作为模块加载。
3. `DESCRIPTION`：驱动程序做什么，以散文形式。
4. `HARDWARE`：驱动程序支持的硬件列表。此章节在第 4 节页面中是必需的，并被逐字绘制到发布硬件说明中。
5. `LOADER TUNABLES`、`SYSCTL VARIABLES`：如果驱动程序暴露可调参数，在这里记录它们。
6. `FILES`：设备节点和任何配置文件。
7. `EXAMPLES`：使用示例，当相关时。
8. `DIAGNOSTICS`：驱动程序日志消息的解释。
9. `SEE ALSO`：交叉引用到相关手册页和文档。
10. `HISTORY`：驱动程序首次出现时。
11. `AUTHORS`：驱动程序的主要作者。
12. `BUGS`：已知问题和局限性。

并非每个章节对每个驱动程序都是必需的。对于简单驱动程序，`NAME`、`DESCRIPTION`、`HARDWARE`、`SEE ALSO` 和 `HISTORY` 是最小集合。对于更复杂的驱动程序，根据相关情况添加其他。

### 一个最小的可工作手册页

这里是一个假设的 `mydev` 驱动程序的完整、可工作的第 4 节手册页。将其保存为 `mydev.4`，对它运行 `mandoc -Tlint`，您将看到它干净通过。这是您可以改编用于自己的驱动程序的那种手册页。

```text
.\"
.\" Copyright (c) 2026 Your Name <you@example.com>
.\"
.\" SPDX-License-Identifier: BSD-2-Clause
.\"
.\" Redistribution and use in source and binary forms, with or
.\" without modification, are permitted provided that the
.\" following conditions are met:
.\" 1. Redistributions of source code must retain the above
.\"    copyright notice, this list of conditions and the following
.\"    disclaimer.
.\" 2. Redistributions in binary form must reproduce the above
.\"    copyright notice, this list of conditions and the following
.\"    disclaimer in the documentation and/or other materials
.\"    provided with the distribution.
.\"
.\" THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY
.\" EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
.\" THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
.\" PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
.\" AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
.\" SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
.\" NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
.\" LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
.\" HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
.\" CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
.\" OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
.\" EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
.\"
.Dd April 20, 2026
.Dt MYDEV 4
.Os
.Sh NAME
.Nm mydev
.Nd driver for FooCorp FC100 sensor boards
.Sh SYNOPSIS
To compile this driver into the kernel,
place the following line in your
kernel configuration file:
.Bd -ragged -offset indent
.Cd "device mydev"
.Ed
.Pp
Alternatively, to load the driver as a
module at boot time, place the following line in
.Xr loader.conf 5 :
.Bd -literal -offset indent
mydev_load="YES"
.Ed
.Sh DESCRIPTION
The
.Nm
driver provides support for FooCorp FC100 series PCI-attached
environmental sensor boards.
It exposes a character device at
.Pa /dev/mydev0
that userland programs can open, read, and write using standard
system calls.
.Pp
Each attached board is enumerated with an integer unit number
beginning at 0.
The driver supports probe, attach, and detach through the
standard Newbus framework.
.Sh HARDWARE
The
.Nm
driver supports the following hardware:
.Pp
.Bl -bullet -compact
.It
FooCorp FC100 rev 1.0
.It
FooCorp FC100 rev 1.1
.It
FooCorp FC200 (compatibility mode)
.El
.Sh FILES
.Bl -tag -width ".Pa /dev/mydev0"
.It Pa /dev/mydev0
First unit of the driver.
.El
.Sh SEE ALSO
.Xr pci 4
.Sh HISTORY
The
.Nm
driver first appeared in
.Fx 15.0 .
.Sh AUTHORS
.An -nosplit
The
.Nm
driver and this manual page were written by
.An Your Name Aq Mt you@example.com .
```

那个手册页是完整、有效的第 4 节页面。它很短，因为假设的驱动程序简单。更复杂的驱动程序会有更大的 `DESCRIPTION`、`HARDWARE` 以及可能的 `LOADER TUNABLES`、`SYSCTL VARIABLES`、`DIAGNOSTICS` 和 `BUGS` 章节。但骨架相同。

让我们演练最常被误解的部分。

### 头部块

顶部的头部块是一组以 `.\"` 开始的注释行。这些是 mdoc 注释。它们不渲染到手册页输出。它们的存在是携带版权头部和给未来编辑的任何说明。

开头行是普通的 `.\"`，等同于 C 文件中的 `/*` 开头。和 C 文件一样，版权行在最前，SPDX 标识符紧随其后。

`.Dd` 宏设置文档日期。它格式化为月、日、年，使用完整月份名称。项目的 mdoc 风格是每当手册页内容有意义更改时更新 `.Dd`。不要为琐碎更改如空格修复增加日期，但要为任何语义更改增加。

`.Dt` 宏设置文档标题。约定是驱动程序名称大写，后跟章节号：`MYDEV 4`。

`.Os` 宏在页脚发出操作系统标识符。直接使用；mdoc 会从构建时宏填充正确的东西。

### NAME 章节

`.Sh NAME` 宏打开 NAME 章节。内容是一对宏：

```text
.Nm mydev
.Nd driver for FooCorp FC100 sensor boards
```

`.Nm` 设置正在记录的事物名称。一旦设置，页面其他地方不带参数的 `.Nm` 扩展为名称，这是我们避免一遍又一遍重复驱动程序名称的方式。

`.Nd` 是简短描述，一个"driver for ..."或"API for ..."或"device for ..."形式的句子片段。不要将第一个单词大写或在末尾添加句点。

### SYNOPSIS 章节

对于驱动程序，SYNOPSIS 通常显示两件事：如何将驱动程序编译到内核作为内置，以及如何作为模块加载。内置形式使用 `.Cd` 用于内核配置行。可加载形式显示 `loader.conf` 的 `_load="YES"` 条目。

如果驱动程序暴露用户态程序必须包含的头文件，或如果它暴露类似库的 API，SYNOPSIS 还可以包含 `.In` 用于包含指令和 `.Ft`/`.Fn` 用于函数原型。参见 `/usr/src/share/man/man4/led.4` 作为 SYNOPSIS 显示函数原型的驱动程序手册页示例。

### DESCRIPTION 章节

DESCRIPTION 章节是您以散文解释驱动程序做什么的地方。为安装了 FreeBSD、面前有硬件、想知道驱动程序提供什么的用户编写。

保持段落聚焦。使用 `.Pp` 分隔段落。使用 `.Nm` 指代驱动程序，而非键入驱动程序名称。使用 `.Pa` 用于路径名称，`.Xr` 用于到其他手册页的交叉引用，`.Ar` 用于参数名称，`.Va` 用于变量名称。

描述驱动程序行为、其生命周期（probe、attach、detach）、其设备节点结构，以及用户需要理解的任何概念以便交互。不要在此记录内部实现细节；源代码是它们的正确位置。

### HARDWARE 章节

HARDWARE 章节在第 4 节页面中是必需的。这是被逐字绘制到发布硬件说明的章节，它是用户查阅以查看其硬件是否被支持的文档。

此章节适用几项特定规则。这些规则记录在 `/usr/src/share/man/man5/style.mdoc.5`：

- 开头句子应该是这种形式："The .Nm driver supports the following <device class>:"后跟列表。
- 列表应该是 `.Bl -bullet -compact` 列表，每个 `.It` 条目一个硬件型号。
- 每个型号应该用其官方商业名称命名，而非内部代码名称或芯片修订。
- 列表应该包括已知工作的所有硬件，包括修订。
- 列表不应包括已知不工作的硬件；那些属于 `BUGS`。

对于全新驱动程序，列表可能很短。这没关系。对于在源代码树中存在了一段时间并积累了多种硬件变体支持的驱动程序，随着测试每个新变体，列表随时间增长。

### FILES 章节

FILES 章节列出驱动程序使用的设备节点和配置文件。使用 `.Bl -tag` 列表配合 `.Pa` 条目用于文件名称。例如：

```text
.Sh FILES
.Bl -tag -width ".Pa /dev/mydev0"
.It Pa /dev/mydev0
First unit of the driver.
.It Pa /dev/mydev1
Second unit of the driver.
.El
```

保持 `.Bl -tag -width` 值足够宽以容纳列表中最长的路径。如果宽度不匹配，列表将渲染不好。

### SEE ALSO 章节

SEE ALSO 章节交叉引用相关手册页。它写作逗号分隔的 `.Xr` 交叉引用列表，列表首先按章节号排序，然后在章节内按字母顺序：

```text
.Sh SEE ALSO
.Xr pci 4 ,
.Xr sysctl 8 ,
.Xr style 9
```

驱动程序的 SEE ALSO 通常包括它附加的总线（如 `pci(4)`、`usb(4)` 或 `iicbus(4)`），任何与它交互的用户态工具，以及任何对驱动程序实现核心的第 9 节 API。

### HISTORY 章节

HISTORY 章节说明驱动程序首次出现时。对于将首次出现在下一版本的全新驱动程序，写发布版本作为占位符：

```text
.Sh HISTORY
The
.Nm
driver first appeared in
.Fx 15.0 .
```

提交您的补丁的提交者将对照发布计划验证版本号并可能调整它。这没问题。

### AUTHORS 章节

AUTHORS 章节命名驱动程序的主要作者。使用 `.An -nosplit` 在顶部告诉 mdoc 不在名称边界处分割作者列表跨行。然后对每个作者使用 `.An`，用 `.Aq Mt` 用于电子邮件地址。

```text
.Sh AUTHORS
.An -nosplit
The
.Nm
driver was written by
.An Your Name Aq Mt you@example.com .
```

对于有多个作者的驱动程序，按贡献顺序列出，主要作者最先。

### 验证手册页

一旦手册页编写完成，用 `mandoc(1)` 验证它：

```sh
mandoc -Tlint /usr/src/share/man/man4/mydev.4
```

`mandoc -Tlint` 在严格模式下通过 mandoc 解析器运行页面并报告任何结构或语义问题。修复每个警告。干净的 `mandoc -Tlint` 运行是提交的前提条件。

您还可以渲染页面以查看它看起来像什么：

```sh
mandoc /usr/src/share/man/man4/mydev.4 | less -R
```

作为用户阅读渲染输出。如果某些东西读起来尴尬，修复源。如果交叉引用以您未预期的方式渲染，检查宏用法。至少阅读输出两次。

项目还推荐 `igor(1)` 工具，可从 ports 树作为 `textproc/igor` 获得。`igor` 捕捉 `mandoc` 不捕捉的散文级别问题，如双空格、不匹配引号和常见散文错误。用 `pkg install igor` 安装并在您的页面上运行：

```sh
igor /usr/src/share/man/man4/mydev.4
```

修复它产生的任何警告。

### 一行一句规则

FreeBSD mdoc 页的一个重要约定是一行一句规则。手册页源中的每个句子在新行开始，无论行宽如何。这无关显示格式；mdoc 会为显示重新流动文本。这是关于源可读性以及 `diff` 如何显示更改。当更改面向行时，手册页更改的 diff 显示哪些句子更改了；当句子跨行时，diff 更难阅读。

`CONTRIBUTING.md` 文件对此明确规定：

> Please be sure to observe the one-sentence-per-line rule so manual pages properly render. Any semantic changes to the manual pages should bump the date.

实践中这意味着您编写：

```text
The driver supports the FC100 family.
It attaches through the standard PCI bus framework.
Each unit exposes a character device under /dev/mydev.
```

而非：

```text
The driver supports the FC100 family. It attaches through the
standard PCI bus framework. Each unit exposes a character device
under /dev/mydev.
```

第一种形式是传统的，第二种不是。

### 常见手册页错误

几种错误在首次提交中反复出现：

- 缺少 HARDWARE 章节。第 4 节页面必须有它。如果您的驱动程序尚不支持硬件（因为它是一个伪设备），明确记录这一点。
- NAME 描述末尾有句点。`.Nd` 描述应该是没有尾随句点的片段。
- 大写的章节标题拼写错误。标题是规范的。使用 `DESCRIPTION`，而非 `DESCRIPTIONS`。使用 `SEE ALSO`，而非 `See Also`。使用 `HISTORY`，而非 `History`。
- 多句段落没有 `.Pp` 分隔。在散文中使用 `.Pp` 在段落之间。
- 进行语义更改时忘记增加 `.Dd`。如果您更改手册页内容，更新日期。
- 在适合 `.Ql`（字面引用）或纯文本的地方使用 `.Cm` 或 `.Nm`。
- 缺少或畸形的 `.Bl`/`.El` 对。每个列表必须正确打开和关闭。

运行 `mandoc -Tlint` 捕捉大多数这些。运行 `igor` 捕捉更多一些。阅读渲染输出捕捉剩余的。

### 阅读真实手册页

在定稿自己的手册页之前，花时间阅读真实的手册页。三个有用的模型：

- `/usr/src/share/man/man4/null.4` 是一个最小的页面。适合了解基本形态。
- `/usr/src/share/man/man4/led.4` 是一个稍微复杂的页面，显示带函数原型的 SYNOPSIS。
- `/usr/src/share/man/man4/re.4` 是一个功能完整的网络驱动程序页面。适合看 HARDWARE、LOADER TUNABLES、SYSCTL VARIABLES、DIAGNOSTICS 和 BUGS 的实际运作。

阅读每一项。在 `less` 中打开它们，阅读渲染版本，然后在编辑器中打开源。将渲染输出与源比较。您将看到宏如何产生格式化文本，您将通过潜移默化吸收约定。

### 第 4 节小结

手册页不是事后思考。它是与您的驱动程序一起发货的第一级工件，是主要的面向用户的文档。好的手册页有特定结构（NAME、SYNOPSIS、DESCRIPTION、HARDWARE 等），以 mdoc 编写，遵循一行一句规则，并通过 `mandoc -Tlint` 干净。页面值得与代码同样的关注。手册页差的驱动程序将在审查中被延迟直到页面达到标准；手册页好的驱动程序将轻松通过那部分审查。

有了许可证和手册页在手，您已经覆盖了驱动程序提交的所有文书工作。下一节转向测试的技术方面，因为干净编译并通过风格检查的驱动程序仍然需要在每个支持的架构上构建并在各种情况下正确行为。第 5 节演练那些测试。

## 第 5 节：在提交前测试您的驱动程序

### 重要的测试

提交前测试驱动程序不是单一行动。它是一系列验证，每一项检查不同的属性。通过所有它们的驱动程序是审查者可以在设计和意图方面而非在可预防的机械问题方面专注的驱动程序。跳过一些它们的驱动程序是会在审查期间出现可避免问题的驱动程序，每一个都增加一轮审查周期。

测试分为几类：

1. 代码风格测试，验证源代码符合 `style(9)`。
2. 手册页测试，验证 mdoc 源代码语法有效并干净渲染。
3. 本地构建测试，验证驱动程序作为可加载内核模块对当前 FreeBSD 源代码树构建。
4. 运行时测试，验证驱动程序加载、附加到设备、处理基本工作负载并干净分离。
5. 跨架构构建测试，验证驱动程序在项目支持的每个架构上编译。
6. Lint 和静态分析测试，捕捉编译器不标记但对更激进的工具可见的错误。

每一类有自己的工具和工作流程。本节按顺序演练它们。

### 代码风格测试

我们已经在第 2 节看到 `tools/build/checkstyle9.pl`。这里我们将详细说明其使用。

脚本位于 `/usr/src/tools/build/checkstyle9.pl`。它是 Perl 程序，所以您用 Perl 调用它：

```sh
perl /usr/src/tools/build/checkstyle9.pl /usr/src/sys/dev/mydev/mydev.c
```

或者，如果脚本可执行且 Perl 在其 shebang 行中：

```sh
/usr/src/tools/build/checkstyle9.pl /usr/src/sys/dev/mydev/mydev.c
```

输出是带行号的警告列表。典型警告包括：

- "space(s) before tab"
- "missing blank line after variable declarations"
- "unused variable"
- "return statement without parentheses"
- "function name is not followed by a newline"

每个警告映射到 `style(9)` 中的特定规则。修复每一个。重新运行。重复直到输出干净。

如果您发现自己不同意某个警告，先检查 `style(9)`。脚本可能产生假阳性，但那些很少。大多数时候，与风格检查器的分歧是对 `style(9)` 的误解。在争论前阅读手册页。

对驱动程序中的每个 `.c` 和 `.h` 文件运行 `checkstyle9.pl`。Makefile 不需要通过它，因为它不是 C 代码。

### 手册页测试

对于手册页，规范测试是 `mandoc -Tlint`：

```sh
mandoc -Tlint /usr/src/share/man/man4/mydev.4
```

修复每个警告。重新运行。重复直到输出干净。

此外，如果您安装了 `igor`，运行它：

```sh
igor /usr/src/share/man/man4/mydev.4
```

并渲染页面以作为用户阅读：

```sh
mandoc /usr/src/share/man/man4/mydev.4 | less -R
```

您还可以将页面安装到系统进行真实测试：

```sh
cp /usr/src/share/man/man4/mydev.4 /usr/share/man/man4/
makewhatis /usr/share/man
man 4 mydev
```

这最后一项检查有用，因为它验证 `man` 可以找到页面、`apropos` 可以通过 `whatis` 找到它，以及页面在标准 pager 中正确渲染。

### 本地构建测试

在对驱动程序做任何事情之前，验证它构建。从模块目录：

```sh
cd /usr/src/sys/modules/mydev
make clean
make obj
make depend
make
```

输出应该是模块对象目录中的单个 `mydev.ko` 文件。无警告，无错误。如果看到警告，修复它们。`style(9)` 明确指出不应忽略警告；引入警告的提交会被审查。

如果您在将加载模块的同一机器上运行，安装它：

```sh
sudo make install
```

这复制 `mydev.ko` 到 `/boot/modules/` 以便 `kldload` 可以找到它。

### 运行时测试

一旦模块构建并安装，测试它：

```sh
sudo kldload mydev
dmesg | tail
```

`dmesg` 输出应该显示您的驱动程序探测、附加到任何可用硬件并完成附加而无错误。如果没有匹配硬件，驱动程序应该简单不附加，这对于加载测试没问题。

像用户那样演练驱动程序。打开其设备节点，读写它们，运行您的驱动程序支持的操作，并观察任何诊断消息。在负载下运行。用多个同时打开者运行。用边缘情况输入运行。这些测试捕获编译器看不到的错误。

然后卸载：

```sh
sudo kldunload mydev
dmesg | tail
```

卸载应该安静完成，无"device busy"错误，无崩溃。如果卸载产生关于繁忙资源的警告，驱动程序的分离路径有泄漏；在提交前修复。

重复加载/卸载循环几次。加载并卸载一次的驱动程序与重复加载并卸载的驱动程序不同。分离路径的错误常常只在第二次或第三次卸载时出现，当第一次卸载遗留的状态干扰第二次加载时。

### 跨架构构建测试

FreeBSD 支持几种架构。截至 FreeBSD 14.3 的活跃架构包括：

- `amd64`（64 位 x86）。
- `arm64`（64 位 ARM，也叫 aarch64）。
- `i386`（32 位 x86）。
- `powerpc64` 和 `powerpc64le`（POWER）。
- `riscv64`（64 位 RISC-V）。
- `armv7`（32 位 ARM）。

在 `amd64` 上构建的驱动程序可能或不可能在所有其他上构建。常见跨架构问题包括：

- 整数大小假设。`long` 在 `amd64` 和 `arm64` 上是 64 位但在 `i386` 和 `armv7` 上是 32 位。如果您的代码假设 `sizeof(long) == 8`，它会在 32 位架构上出错。当大小重要时使用 `int64_t`、`uint64_t` 或类似固定大小类型。
- 指针大小假设。类似，指针在 `amd64` 上是 64 位在 `i386` 上是 32 位。在指针和整数间转换需要 `intptr_t`/`uintptr_t`。
- 字节序。一些架构是小端，一些是大端，一些可配置。如果您的驱动程序读取或写入网络字节序数据，使用显式字节交换宏（`htonl`、`htons`、`bswap_32` 等），而非手工转换。
- 对齐。一些架构对多字节加载执行严格对齐。访问硬件寄存器时使用 `memcpy` 或 `bus_space(9)` API 而非直接 cast。
- 总线抽象。`bus_space(9)` API 正确跨架构抽象硬件访问；使用内联 `volatile *` cast 不这样做。

捕捉跨架构问题的最好方法是针对每个架构构建驱动程序。幸运的是，FreeBSD 有一个构建目标精确做这件事：

```sh
cd /usr/src
make universe
```

`make universe` 为每个支持的架构构建 world 和 kernel。完整构建根据机器可能需要一小时或更多，所以不是每次更改都运行的东西，但它是指规范提交前测试。`/usr/src/` 中的 `Makefile` 描述它：

> `universe` - `Really` build everything (buildworld and all kernels on all architectures).

如果您不想构建所有东西，您可以只构建单个架构：

```sh
cd /usr/src
make TARGET=arm64 buildkernel KERNCONF=GENERIC
```

这更快且常常足以捕捉典型跨架构问题。

对于只有您的模块，有时可以交叉构建：

```sh
cd /usr/src
make buildenv TARGET_ARCH=aarch64
cd sys/modules/mydev
make
```

但 `make universe` 和 `make buildkernel TARGET=...` 是规范测试，任何严肃提交应该通过它们。

### tinderbox：universe 的失败追踪变体

`make universe` 的一个变体是 `make tinderbox`：

```sh
cd /usr/src
make tinderbox
```

Tinderbox 与 universe 相同，但在结束时报告失败的架构列表，如果有任何失败则退出错误。对于提交工作流程，这常常比 plain `universe` 更有用，因为失败列表是清晰的行动项目。

### 运行内核 Lint 工具

FreeBSD 的内核构建可选运行额外检查。`LINT` 内核配置是用每个驱动程序和选项开启构建的内核，这会暴露单特性内核遗漏的跨切面问题。为驱动程序提交构建 LINT 内核通常不是必需的，但它是触及广泛使用东西的有用健全检查。

`clang` 本身作为 FreeBSD 的默认编译器，在正常编译期间执行复杂的静态分析。用 `WARNS=6` 构建以看最激进的警告集：

```sh
cd /usr/src/sys/modules/mydev
make WARNS=6
```

并修复出现的任何警告。Clang 还有 scan-build 工具作为单独传递运行静态分析：

```sh
scan-build make
```

如果不可用，从 ports 树（`devel/llvm`）安装它。

### 在虚拟机中测试

本章大部分假设您在真实机器或虚拟机上测试。虚拟机对于驱动程序测试特别有用，因为崩溃的成本只是重启。两种常见方法：

- bhyve，FreeBSD 的原生 hypervisor。bhyve 下的 FreeBSD guest 可以是好的测试环境，特别对于使用 `virtio` 的网络驱动程序。
- QEMU。QEMU 可以模拟不同于 host 的架构，这使它有用用于跨架构运行时测试而无需每种架构的物理硬件。

对于跨架构运行时测试，QEMU 配合目标架构中的 FreeBSD 映像是好的工作流程。为目标架构构建模块，复制到 QEMU VM，并在那里运行 `kldload`。VM 内崩溃不影响您的 host。

### 针对 HEAD 测试

FreeBSD 源代码树的 `main` 分支在发布工程意义下有时被称为 HEAD。您的驱动程序应该针对 HEAD 构建和运行，因为那是您的补丁首先降落的地方。如果您一直针对较旧分支开发，在最终测试前更新到 HEAD：

```sh
cd /usr/src
git pull
```

然后重新构建和重新测试。内核 API 变化；针对六个月前源代码树构建的驱动程序可能需要小调整才能针对当前 HEAD 构建。

### 整个管道的 Shell 脚本

对于严肃提交，考虑将测试序列放入 shell 脚本。配套示例包含一个，但骨架简单：

```sh
#!/bin/sh
# pre-submission-test.sh
set -e

SRC=/usr/src/sys/dev/mydev
MOD=/usr/src/sys/modules/mydev
MAN=/usr/src/share/man/man4/mydev.4

echo "--- style check ---"
perl /usr/src/tools/build/checkstyle9.pl "$SRC"/*.c "$SRC"/*.h

echo "--- mandoc lint ---"
mandoc -Tlint "$MAN"

echo "--- local build ---"
(cd "$MOD" && make clean && make obj && make depend && make)

echo "--- load/unload cycle ---"
sudo kldload "$MOD"/mydev.ko
sudo kldunload mydev

echo "--- cross-architecture build (arm64) ---"
(cd /usr/src && make TARGET=arm64 buildkernel KERNCONF=GENERIC)

echo "all tests passed"
```

在每次提交前运行此脚本。如果它干净退出，您的驱动程序已清除所有机械测试。审查者然后可以专注设计。

### 测试不捕捉什么

测试告诉您驱动程序编译以及它在您测试的情况下工作。它不告诉您它在所有情况下工作。通过每个测试的驱动程序可能仍有只在罕见负载下、罕见硬件上或内核状态的罕见交错下出现的错误。

这正常。软件从未完全测试。提交前测试的角色不是证明驱动程序正确，而是捕捉容易捕捉的错误。设计级别错误、罕见竞态条件和微妙协议违规仍然会进入源代码树并会稍后被遇到它们的用户捕捉。那是提交后维护的用途，我们将在第 8 节覆盖。

### 第 5 节小结

测试是一个多阶段验证过程。风格检查、手册页 lint、本地构建、运行时加载/卸载循环、跨架构构建和静态分析各自测试不同属性。通过所有它们的驱动程序是处于审查形态的驱动程序。工具是标准的：`checkstyle9.pl`、`mandoc -Tlint`、`make`、`make universe`、`make tinderbox` 和 clang 的内置分析。纪律是按顺序运行它们，修复产生的每个警告，在它们都干净通过前不提交。

有了驱动程序测试，我们可以转向实际为审查提交它的机械工作。第 6 节演练补丁生成和提交工作流程。

## 第 6 节：提交补丁以供审查

### 在 FreeBSD 意义上，补丁是什么

补丁，在 FreeBSD 意义上，是一个可审查的更改单元。它可以是单个提交或一系列提交。它代表对源代码树的一个逻辑更改。对于新驱动程序提交，补丁通常是一两个引入新驱动程序文件、新模块 Makefile 和新手册页的提交。

补丁的机械形式是更改的文本表示，通常是统一 diff 格式。有几种方法生成这样的表示：

- `git diff` 产生两个提交之间或提交和工作树之间的 diff。
- `git format-patch` 为每个提交产生补丁文件，包含完整提交元数据，以适合电子邮件或附加到审查的形式。
- `arc diff`，来自 Phabricator 命令行工具，将当前工作状态发布为 Phabricator 修订。
- `gh pr create`，来自 GitHub 命令行工具，打开 GitHub 拉取请求。

正确的工具取决于您将补丁发送到哪里。对于 Phabricator，`arc diff` 是标准的。对于 GitHub，`gh pr create` 或 GitHub web UI 是标准的。对于邮件列表，`git format-patch` 配合 `git send-email` 是标准的。

它们都依赖相同的基础 git commit。在担心提交工具之前，确保提交本身是干净的。

### 准备提交

从 FreeBSD 源代码树的干净、最新克隆开始：

```sh
git clone https://git.FreeBSD.org/src.git /usr/src
```

或者，如果您已有克隆，更新它：

```sh
cd /usr/src
git fetch origin
git checkout main
git pull
```

为您的工作创建主题分支：

```sh
git checkout -b mydev-driver
```

进行更改：添加驱动程序文件、模块 Makefile 和手册页。运行第 5 节的所有测试。修复任何问题。

提交您的更改。提交应该是单个逻辑更改单元。如果您正在引入全新的驱动程序，一个提交通常合适："mydev: Add driver for FooCorp FC100 sensor boards."提交消息应遵循第 2 节的约定。

```sh
git add sys/dev/mydev/ sys/modules/mydev/ share/man/man4/mydev.4
git commit -s
```

`-s` 添加 `Signed-off-by:` 行。编辑器为提交消息打开；按照第 2 节的约定填写主题行和正文。

审查提交：

```sh
git show HEAD
```

阅读每一行。检查不相关文件未包含。检查不存在尾随空格。检查提交消息读起来好。如果有什么不对，修改：

```sh
git commit --amend
```

重复直到提交完全如您所愿。

### 生成审查补丁

一旦提交干净，生成补丁。对于 Phabricator 审查，`arc diff`：

```sh
cd /usr/src
arc diff main
```

`arc` 将检测您在主题分支上，生成 diff，并在您的浏览器中打开 Phabricator 审查。填写摘要，如果您知道任何审查者则标记他们，并提交。

对于 GitHub 拉取请求，推送您的分支并使用 `gh`：

```sh
git push origin mydev-driver
gh pr create --base main --head mydev-driver
```

或通过 GitHub web UI 打开拉取请求。拉取请求表单要求标题（使用提交主题行）和正文（使用提交正文）。标题和正文形成拉取请求的描述；它们应该匹配最终提交将携带的内容。

对于邮件列表提交或发给维护者的电子邮件：

```sh
git format-patch -1 HEAD
```

这产生一个类似 `0001-mydev-Add-driver-for-FooCorp-FC100-sensor-boards.patch` 的文件，包含提交。您可以将其附加到电子邮件或用 `git send-email` 内联发送。邮件列表提交今天比 Phabricator 或 GitHub 少见但仍被接受。

### 选择哪种途径

`CONTRIBUTING.md` 文件给出何时使用哪种途径的具体指导。简短版本：

- 当更改小（少于约 10 个文件和 200 行）、独立、通过 CI 干净且需要很少开发者时间降落时，GitHub 拉取请求优先。
- 对于较大更改、需要扩展审查的工作，以及维护者偏好 Phabricator 的子系统，Phabricator 优先。
- 当补丁修复特定报告的错误时，Bugzilla 合适。
- 当您知道子系统的维护者且更改足够小可以非正式处理时，直接发给提交者的电子邮件合适。

新驱动程序通常介于 GitHub 大小限制和 Phabricator 适合之间。如果您的驱动程序少于 10 个文件和 200 行，GitHub PR 可行。如果更大，先尝试 Phabricator。

无论您选择哪种，确保驱动程序已通过所有提交前测试。在 GitHub 和 Phabricator 上运行的 CI 都会捕捉问题，但如果您先捕捉它们，可以为每个人节省时间。

### 编写好的审查描述

补丁本身只是您提交的一半。另一半是描述：随补丁附带并解释它做什么、为什么需要以及如何测试的文本。

在 Phabricator 上，描述是 Summary 字段。在 GitHub 上，是 PR 正文。在邮件列表上，是电子邮件正文。

好的描述有三部分：

1. 一段补丁做什么的摘要。
2. 设计和任何有趣决定的讨论。
3. 测试了什么的列表。

对于驱动程序提交，典型描述可能是：

> This patch adds a driver for FooCorp FC100 environmental sensor boards. The boards are PCI-attached and expose a simple command-and-status interface over a single BAR. The driver implements probe/attach/detach following Newbus conventions, exposes a character device for userland interaction, and documents tunable sampling intervals via sysctl.
>
> The FC100 is documented in FooCorp's Programmer's Reference Manual version 1.4. The driver supports revisions 1.0 and 1.1 of the board, and operates the FC200 in its FC100-compatibility mode.
>
> Tested on amd64 and arm64 with a physical FC100 rev 1.1 board. Passes `make universe`, `mandoc -Tlint`, and `checkstyle9.pl`. Load/unload cycle verified 50 times without leaks.
>
> Reviewer suggestions welcome on the sysctl structure.

这个描述做了几件事正确。它以一种从未见过它的审查者可以理解的方式解释驱动程序。它建立了测试了什么。它明确邀请对特定设计问题的反馈。它读作合作的审查请求，而非"这是我的代码，合并它"的要求。

### 邮件列表的草稿邮件

即使您计划通过 Phabricator 或 GitHub 提交，给 FreeBSD 邮件列表之一发草稿邮件可能是有用的介绍。开发的通用邮件列表是 `freebsd-hackers@FreeBSD.org`；也有子系统列表如网络驱动程序的 `freebsd-net@` 和存储的 `freebsd-scsi@`。选择最匹配您的驱动程序所在子系统的列表，如果不确定，从 `freebsd-hackers@` 开始。

草稿邮件可能如下：

```text
To: freebsd-hackers@FreeBSD.org
Subject: New driver: FooCorp FC100 sensor boards

Hello,

I am working on a driver for FooCorp FC100 PCI-attached
environmental sensor boards. The boards are documented, I have
two hardware samples to test against, and the driver is in a
state that passes mandoc -Tlint, checkstyle9.pl, and make
universe clean.

Before I open a review, I wanted to ask the list if anyone has:

* Experience with similar sensor boards that might inform the
  sysctl structure.
* Strong preferences about whether the driver should expose a
  character device or a sysctl tree as the primary interface.
* Comments on the draft manual page (attached).

The code is available at https://github.com/<me>/<branch> for
anyone who wants to take an early look.

Thanks,
Your Name <you@example.com>
```

这是倾向于产生有用回应的那种电子邮件。它表明工作是严肃的，它提出具体问题，它提供查看代码的方式。许多成功的 FreeBSD 提交从这样的电子邮件开始。

对于本书，您不需要实际发送这样的电子邮件。配套示例包含一个草稿，您可以作为模板使用。第 4 节的练习包括编写自己的草稿。

### 提交后发生什么

一旦您提交，审查过程开始。确切流程取决于提交途径，但总体模式跨途径类似。

对于 Phabricator 审查：

- 修订出现在 Phabricator 的队列中。它自动订阅与子系统相关的任何邮件列表。
- 审查者可能从队列中捡起审查，或者您可以标记您认为相关的特定审查者。
- 审查者在特定行留下评论、一般评论和请求的更改。
- 您通过更新提交并运行 `arc diff --update` 来刷新修订，从而解决评论。
- 审查周期重复直到审查者满意。
- 提交者最终降落补丁，以您的名字作为作者记入。

对于 GitHub 拉取请求：

- PR 出现在 `freebsd/freebsd-src` 的 GitHub 队列中。
- CI 作业自动运行；它们必须通过。
- 审查者评论 PR、留下行评论或请求更改。
- 您通过将修复提交到您的分支来解决评论。最终，您将修复压缩到原始提交中。
- 当审查者准备好时，他们要么自己合并 PR（如果他们是提交者），要么将其引导到 Phabricator 进行更深入的审查。
- 合并的提交保留您的作者身份。

对于邮件列表提交：

- 邮件列表读者用反馈回应。
- 您根据反馈迭代并将更新版本作为对原始线程的回复发送。
- 当提交者准备好时，他们将提交补丁，以您记入。

在所有情况下，迭代是过程的一部分。以首次提交的确切形式进入源代码树的补丁很少。期望至少一轮反馈，常常几轮。每一轮是审查者帮助您润色提交。

### 响应时间和耐心

审查者是志愿者，即使是那些由雇主付费从事 FreeBSD 工作的人。他们的时间是有限的。审查的响应时间可能从几小时（对于小型、准备充分且符合审查者当前兴趣的补丁）到几周（对于大型、复杂且需要仔细阅读的补丁）。

如果您的补丁在合理时间内未收到响应，发送礼貌的 ping 是可接受的。通常约定：

- 在 ping 前至少等待一周。
- 保持 ping 简短："Just a friendly ping on this review, in case it slipped off anyone's radar."仅此而已。
- 不要每周 ping 多于一次。如果补丁在多次 ping 后仍未获得关注，问题可能不是审查者忘记了；可能是补丁需要更多工作，或了解子系统的审查者正忙于其他事情。
- 考虑在相关邮件列表上询问审查关注。公开询问有时比私下 ping 更有效。

不要用愤怒或压力回应审查沉默。项目由志愿者运营。比您希望的更长的审查不是人身侮辱。

### 迭代和补丁更新

每轮审查都会有您需要解决的评论。有些会小（重命名变量、添加注释、修复手册页中的拼写错误）。有些会更大（重写函数、更改接口、添加测试）。

解决每个评论。如果您不同意评论，解释您推理的回复；不要只是忽略它。审查者愿意被说服，但只有在您提出理由的情况下。

更新补丁时，保持提交历史干净。如果您早先推送了"fixup"提交，在最终提交前将其压缩到原始提交。源代码树提交应该每个逻辑完整；它们不应包含混乱的增量步骤。

GitHub PR 更新的工作流程通常如下：

```sh
# make the fixes
git add -p
git commit --amend
git push --force-with-lease
```

对于 Phabricator 更新：

```sh
# make the fixes
git add -p
git commit --amend
arc diff --update
```

强制推送时总是使用 `--force-with-lease` 而非 `--force`。`--force-with-lease` 在远程以您不知道的方式移动时拒绝推送，这防止意外覆盖审查者的更改。

### 提交中的常见错误

一些常见提交时错误：

- 提交草稿。先润色补丁。提交您知道未准备好的补丁浪费审查者的时间。
- 针对过时源代码树提交。在提交前针对当前 HEAD 变基。
- 包含不相关更改。每次提交应该是一个逻辑更改。风格清理、不相关的错误修复和随机改进应该是分开的提交。
- 不响应反馈。因为作者从未回复而停滞在审查中的补丁是会死亡的补丁。
- 防御性反击。审查者正在提供帮助。防御性地响应反馈是破坏关系的快速方式。
- 同时向多个途径提交相同补丁。选择一个途径。如果您提交到 Phabricator，不要同时打开包含相同内容的 GitHub PR。

### 第 6 节小结

一旦补丁就绪，为审查提交补丁是机械过程。补丁是一个（或一系列）带适当消息的提交，针对当前源代码树。提交途径取决于更改的大小和性质：小型独立更改去 GitHub PR，更大或更深入的更改去 Phabricator，特定错误修复可以附加到 Bugzilla 条目。随附的描述解释补丁并邀请审查。接下来是一个迭代审查周期，以提交者降落补丁结束。

下一节查看该迭代的人文方面：如何与导师或提交者合作、如何处理反馈，以及如何将首次提交转变为与项目长期关系的开始。

## 第 7 节：与导师或提交者合作

### 为什么人文方面重要

提交过程最终是与人的合作，而非与平台。您提交的补丁由有自己背景、工作量和经验的工程师审查，这些经验决定了驱动程序容易还是难以审查。您提交的成功或失败取决于您如何与这些人接触的程度与技术质量相当。

这个框架困扰一些首次贡献者，他们更希望技术工作独立存在。这种偏好可以理解，但不匹配现实。FreeBSD 是一个社区项目，不是代码提交服务。审查者提供他们的时间是因为他们关心项目并且他们喜欢帮助其他贡献者成功。当这种关心得到回报时，体验对每个人都是好的。当没有时，即使技术上好的补丁，体验也可能令人沮丧。

本节演练贡献过程的人文方面。有些会感觉显而易见。很多很少被明确讨论，这就是为什么首次贡献者有时即使在代码扎实的情况下也会跌跌撞撞。

### 导师的角色

导师，在 FreeBSD 语境中，是一个同意指导特定新贡献者完成首次提交的提交者。并非每个贡献都涉及导师；许多首次提交通过普通审查降落而无正式指导。但当导师参与时，关系有特定形态。

导师通常做这些事：

- 详细审查您的补丁，常常在进入更广泛审查之前。
- 帮助您了解项目约定和您正在工作的特定子系统。
- 代表您赞助提交，意味着他们将补丁提交到源代码树并以您为作者记入。
- 回答关于项目流程、风格和社交规范的问题。
- 如果后来提交权限变得合适，在提名讨论中为您担保。

导师不是替您做工作。他们在加速您融入项目。好的导师耐心、愿意解释、愿意在您走向错误方向时推回。好的被指导者勤奋、愿意倾听、愿意做迭代的工作。

寻找导师常常是自然发生的。它发生是因为您与特定提交者进行了几轮卓有成效的审查，他们提供了承担更有结构指导角色的角色。很少是因为您冷冰冰地询问。如果您对指导感兴趣，正确的举动是开始显著而卓有成效地贡献，让关系发展。

FreeBSD 项目在不同时期还有更正式的指导计划，包括针对特定人群或特定子系统。如果您想要一个有结构的起点，这些计划是寻找导师的正确地方。

### 赞助：提交途径

赞助者是为贡献者代表提交补丁的提交者。每个来自非提交者的贡献在提交时都通过赞助者。赞助者不一定与主要审查者是同一人，不一定是导师，尽管他们可以两者都是。

为补丁寻找赞助者通常简单。如果补丁已通过审查且至少一位提交者已批准，那位提交者通常愿意赞助提交。您不需要正式询问；当审查者准备好时提交会发生。

如果补丁已被审查但无人推进提交，礼貌的问题合适："Is anyone in a position to sponsor the commit of this patch?"在审查线程或相关邮件列表上询问通常会找到某人。

不要混淆赞助与抽象的支持。赞助者具体是运行 `git push` 降落您的补丁的人。他们承担少量责任：他们的名字出现在提交元数据中，并且他们隐含证明补丁准备好降落。

### 优雅地接受反馈

补丁上的反馈可能很难读，特别是第一次。审查者以审查代码的模式写作，这意味着他们命名需要更改的具体事物。即使对补丁的基本评估非常积极，那种模式也读作负面的。说"这是一个很好的开始，但这里有二十件事要修复"的审查对于首次提交是正常的。

对反馈的正确响应是解决它。对于每个评论：

- 仔细阅读。确保您理解审查者在要求什么。
- 如果评论清晰可操作，进行更改。不要仅仅因为建议不是您的第一选择就争论。
- 如果评论不清晰，请求澄清。"Can you say more about what you mean by X?"是完全可以接受的响应。
- 如果您不同意评论，回复解释您的推理。要具体："I thought about using X but went with Y because Z."审查者愿意被说服。
- 如果评论超出补丁范围，说明并提出单独处理。"Good catch, but this is really a separate change; I will send it as a follow-up."

永远不要以敌意回应。即使您相信审查者错了，也要冷静并带有推理地回应。陷入愤怒的审查线程是审查者会脱离的，您的补丁会停滞。

几个要避免的具体响应：

- "The code already works."代码工作是问题所在。问题是它是否匹配源代码树的约定和设计期望。
- "This is just style; the code is fine."风格是工程质量的一部分。当审查者询问风格时，他们不是在浪费您的时间。
- "Other drivers in the tree do it this way."他们可能确实如此，源代码树有很多不匹配现代约定的较旧驱动程序。新贡献的目标是匹配现代约定，而非重现历史漂移。
- "I will do that later."如果您说您稍后会做，审查者无法验证您会。现在做，或讨论为什么稍后修复合适。

审查过程是合作的。审查者不是您的对手。每个评论，即使您不同意的，也是审查者对您的补丁投入时间。用您自己的投资回应那种投资。

### 迭代和耐心

补丁审查设计上就是迭代的。典型首次驱动程序提交在降落前经历三到五轮审查。每轮根据审查者可用性和请求的更改大小，需要几天到几周。

对于新驱动程序，从首次提交到合并的总经过时间常常是几周。有时是几个月。这正常。FreeBSD 是一个谨慎的项目；仔细审查需要时间。

几个有助于迭代的习惯：

- 快速响应。您对反馈响应越快，审查推进越快。您这边的延迟对时间线的影响与审查者那边的延迟一样大。
- 批量小修复。如果审查者留下十条评论，在单个更新中修复所有十条而非发送十个单独更新。审查者偏好看到工作集成。
- 保持提交干净。迭代时，修改原始提交而非堆叠 fixup 提交。最终降落的提交应该是单个干净提交，而非混乱历史。
- 重新提交前测试。每轮迭代应通过与首次提交相同的提交前测试。不要在轮次间破坏测试。
- 总结每轮迭代。更新补丁时，审查上的简短回复说"updated to address all comments; specifically: did X, did Y, clarified Z"帮助审查者快速重新定位。

最重要的是，保持耐心。审查过程存在是因为代码质量重要。匆忙通过它会损害质量并产生快速降落但稍后制造问题的补丁。

### 处理分歧

偶尔，审查者会留下您真正不同意的反馈。评论不不清晰；您已经思考过，您相信审查者错了。您怎么做？

首先，考虑您可能错了。大多数时候，当审查者提出关切时，背后有一些您可能没看到的东西。审查者有您可能没有的源代码树、子系统和历史的背景。假设关切是合法的，直到您有相反的证据。

其次，如果经过思考您仍不同意，用推理回复。解释您的观点。引用代码、数据手册或源代码树的具体细节。请求审查者参与您的推理。

第三，如果分歧持续，温和升级。请求另一位提交者的第二意见。在相关邮件列表发帖描述问题。有时分歧揭示有多个可辩护的答案，项目尚未达到明确立场；那是有用信息以浮出水面。

第四，如果分歧仍持续且无解决，您有选择。您可以进行审查者请求的更改，即使您不同意，并降落补丁。或者您可以撤回补丁。两者都合法。项目文化不是强迫贡献者做他们不同意的事情，也不是橡皮图章提交者社区有顾虑的补丁。如果分歧是根本性的，撤回有时是正确结果。

这种深度的分歧很少。大多数反馈是实际的，要么明显正确要么明显可适应。当它们发生时，严肃分歧通常是关于有多个可辩护答案的设计选择。

### 建立长期关系

首次提交不是工作的终点。如果顺利，它可以是与项目长期关系的开始。许多提交者开始是首次贡献者，他们的早期补丁顺利，后来的补丁建立在这种信任上，他们的参与最终增长到提交权限有意义的程度。

建立那种关系不是关于表演。是关于持续而卓有成效地继续贡献。几个有帮助的习惯：

- 响应关于您的驱动程序的错误报告。如果用户报告错误，分类它，确认或否认，并跟进。作者响应迅速的驱动程序是项目重视的驱动程序。
- 审查其他人的补丁。一旦您熟悉子系统，您可以审查该子系统的新补丁。审查是您如何成为公认的专家，以及您如何内化子系统的约定。
- 参与讨论。邮件列表和 IRC 频道有正在进行的技术讨论。深思熟虑地参与是成为社区一部分的一部分。
- 保持您的驱动程序更新。如果内核 API 变化，更新您的驱动程序。如果新硬件变体出现，添加支持。驱动程序在合并时未完成；它是您正在照料的活工件。

这些都不是必需的。项目感激任何贡献，包括从未回来的贡献者的一次性补丁。但如果您对更深入的参与感兴趣，这些是途径。

### 识别现有维护者

FreeBSD 中的许多子系统有可识别的维护者或长期贡献者。找到他们有用，因为他们常常是相关工作的最好审查者。

几种识别维护者的方法：

- `git log --format="%an %ae" <file>` 显示谁提交了特定文件的更改。频繁出现的名字是活跃维护者。
- `git blame <file>` 显示谁写了每行。如果您正在扩展特定函数，写它的人常常是要问的人。
- `MAINTAINERS` 文件，如果存在，列出正式维护者。FreeBSD 没有单一的源代码树范围 MAINTAINERS 文件，但一些子系统有非正式等效物。
- 手册页的 `AUTHORS` 节命名主要作者。

对于扩展现有系列的驱动程序，现有驱动程序的作者通常是第一个接触的审查者。他们有背景和权威。对于全新领域中的全新驱动程序，通过在相关邮件列表上询问来找到审查者。

### 练习：识别类似驱动程序的维护者

继续之前，在源代码树中选择一个范围与您正在处理的驱动程序相似的驱动程序。使用 `git log` 识别其维护者。记下他们的姓名和电子邮件。然后阅读他们的一些提交并查看他们通常接触的审查者。这给您子系统人员是谁的心理模型，使提交的人文方面感觉更具体。

不期望您联系他们，除非您有具体问题。练习是关于建立意识。

### 第 7 节小结

审查过程的人文方面与技术方面一样重要。与您的补丁接触的导师或提交者是资源；以尊重对待他们、建设性地响应反馈、耐心迭代是接触的实际纪律。分歧会发生且通常是有成效的；防御性是要避免的主要风险。处理好首次提交可以是与项目长期关系的开始。

提交工作流程的最后一块是补丁在源代码树中后发生什么。第 8 节覆盖维护的长期弧线。

## 第 8 节：合并后维护和支持您的驱动程序

### 合并非终点

当您的补丁降落 FreeBSD 源代码树时，一个自然的感觉是工作完成了。驱动程序进去了。审查结束了。提交在历史中了。您可以继续了。

这种感觉可以理解，但图景不完整。将驱动程序合并到源代码树是一种不同种类工作的开始，而非驱动程序生命的终点。只要驱动程序在源代码树中，它就需要偶尔维护。只要它被使用，它偶尔会暴露错误。只要内核演进，其 API 会漂移，驱动程序需要跟随。

本节演练合并后维护图景。期望不重，但它们是真实的。合并后消失的作者留下的驱动程序是项目必须自己维护的，最终，如果无人接手，那可能是将驱动程序标记为废弃的理由。

### Bugzilla 监控

位于 `https://bugs.freebsd.org/` 的 FreeBSD Bugzilla 是项目的主要错误跟踪器。针对您的驱动程序提交的错误会出现在那里。您作为贡献者不需要订阅 Bugzilla，但您至少应该知道如何检查针对您的驱动程序的开放错误。

一个简单的检查方法：

```text
https://bugs.freebsd.org/bugzilla/buglist.cgi?component=kern&query_format=advanced&short_desc=mydev
```

将 `mydev` 替换为您的驱动程序名称。查询返回摘要提及您的驱动程序的错误。

如果提交了错误：

- 仔细阅读报告。
- 如果您可以复现，这样做。
- 如果您可以修复，准备补丁。补丁通过与任何其他更改相同的审查过程。
- 如果您无法复现，向报告者询问更多信息：FreeBSD 版本、硬件详情、相关日志输出。
- 如果是真实错误但您没有时间或能力修复，在错误报告中说明。有参与但当前无法修复的作者的错误与作者缺席的错误不同。至少，参与意味着其他查看错误的人有上下文可以工作。

Bugzilla 还承载增强请求（新功能的功能请求）。这些比错误报告优先级低，但它们是用户需要什么的有用信号。您不需要实现每个增强请求，但确认它们并讨论优先级是维护的一部分。

### 响应社区反馈

除了 Bugzilla，社区反馈可以通过其他几个渠道到达您：

- 用户直接电子邮件。
- 邮件列表上的讨论。
- IRC 频道上的问题。
- 相关工作审查线程上的评论。

响应迅速的维护者的期望不是您即时响应每一个这些。期望是您在驱动程序记录的电子邮件地址（手册页 `AUTHORS` 节和提交历史中的那个）可达，并且当您响应某事时，您卓有成效地这样做。

实际节奏可能如下：每周或每两周一次，检查您的驱动程序相关电子邮件和 Bugzilla 查询。响应任何等待的东西。分类任何新东西。保持响应时间合理，在一两周的尺度而非几个月。

如果您的情况变化，您不能再维护驱动程序，公开说明。如果需要已知，项目可以并且将会找到新维护者。最坏的结果是静默消失，留下未确认的错误和用户不确定驱动程序是否在被维护。

### 内核 API 漂移

FreeBSD 内核演进。您编写驱动程序时稳定的 API 可能变化。当这发生时，您的驱动程序需要更新，项目会首先看您进行更新。

几种通常影响驱动程序的 API 漂移：

- Newbus 框架更改：新方法签名、新方法类别、`device_method_t` 宏更改。
- 总线特定附加模式更改：PCI、USB、iicbus、spibus 等随时间演进。
- `bus_space(9)` 接口更改。
- 字符设备接口更改（`cdevsw`、`make_dev` 等）。
- 内存分配 API 更改（`malloc(9)`、`contigmalloc`、`bus_dma`）。
- 同步原语更改（`mtx`、`sx`、`rw`）。
- 旧 API 废弃以支持新 API。

通常，这些更改在提交前在邮件列表上宣布，有时它们带有"树清扫"提交，将旧 API 的所有用户更新为新 API。如果您的驱动程序在源代码树中，树清扫通常会自动更新它。但并非总是；有时清扫保守，留下无法机械更新的驱动程序给维护者处理。

一个好习惯：至少偶尔检查 `freebsd-current@` 了解影响您的驱动程序的 API 变更讨论。如果看到一个，检查您的驱动程序是否仍对当前 HEAD 构建。如果不能，发送补丁更新它。

### UPDATING 文件

项目在 `/usr/src/UPDATING` 维护一个 `UPDATING` 文件，列出源代码树中的重要更改，包括驱动程序可能需要响应的 API 更改。偶尔阅读它（特别是在更新您的源代码树之前）以查看是否有什么影响您的驱动程序。

典型的 UPDATING 条目可能是：

```text
20260315:
		The bus_foo_bar() API has changed to require an explicit
		flags argument.  Drivers using bus_foo_bar() should pass
		BUS_FOO_FLAG_DEFAULT to preserve historical behaviour.
		Drivers using bus_foo_bar_old() should migrate to the new
		API as bus_foo_bar_old() will be removed in FreeBSD 16.
```

如果您看到这样的条目提及您的驱动程序使用的函数，相应更新驱动程序。

### 树范围重构

偶尔，项目进行触及每个驱动程序的树范围重构。FreeBSD 历史上的例子包括：

- 从 `$FreeBSD` CVS 标签转换为仅 git 元数据。
- 在整个源代码树中引入 SPDX-License-Identifier 行。
- 大规模重命名 API，如 `make_dev` 系列或 `contigmalloc` 系列。

当树范围重构发生时，重构提交通常会与所有其他人的驱动程序一起更新您的驱动程序。您可能不需要做任何事情。但重构会显示在针对您的驱动程序的 `git log` 中，未来查看历史的开发者会看到它。理解发生了什么以便被询问时可以解释。

### 参与未来发布

FreeBSD 有大约一年一次主要版本的发布周期，点版本发布在更频繁的时间表上。无论您是否积极做任何事情，您的驱动程序都参与此周期。

几件事值得了解：

- 您的驱动程序为它所在分支的每个发布构建。如果它在 `main` 中，它将在下一个主要版本中。如果它也被精选到 `stable/` 分支，它将在该分支的下一个点版本中。
- 主要版本之前，发布工程团队可能要求维护者确认他们的驱动程序状态良好。如果您的驱动程序收到此类请求，响应。这是一个帮助项目计划发布的简单行动。
- 发布后，您的驱动程序在使用该版本的每个安装上的野外。发布后错误报告可能更频繁。

参与发布周期是轻量级维护形式。它主要由发布工程师在需要时可以联系到您组成。

### 保持代码更新：节奏

维护源代码树中驱动程序的合理节奏：

- 每月：检查 Bugzilla 中针对驱动程序的开放错误。响应任何未决事项。
- 每月：针对当前 HEAD 重新构建驱动程序并检查警告或失败。如果有任何失败，调查并修复。
- 每季度：重新阅读手册页。如果驱动程序自上次审查以来有更改，更新它。
- 主要版本前：对您的驱动程序当前状态运行完整提交前测试套件（风格、mandoc、构建、universe）。修复任何漂移。
- 当您有硬件样本和空闲下午：在硬件上演练驱动程序并确保它仍然工作。

这个节奏不是强制的。如果没有东西损坏，驱动程序可以几个月无维护。但有节奏在心保持驱动程序随时间健康。

### 练习：创建月度维护检查清单

结束本节前，打开文本文件并为您的驱动程序写下月度维护检查清单。包括：

- 显示针对驱动程序的错误的 Bugzilla 查询 URL。
- 针对当前 HEAD 重新构建驱动程序的命令。
- 运行风格和 lint 检查的命令。
- 检查 API 漂移的命令（例如，`grep` 废弃调用）。
- 用户可能联系您的电子邮件地址的说明。
- 如果进行语义更改，更新手册页日期的提醒。

将此检查清单与驱动程序源代码一起或在您的个人笔记中保存。写下来的行为将您提交到节奏。存在于纸上的检查清单会被遵循；只存在于记忆中的检查清单不会被。

### 当您无法再维护时

生活在变。工作在变。优先级在转移。在某个时刻，您可能发现无法像以前那样维护您的驱动程序。这正常，项目有流程处理它。

正确的举动是公开说明。选项：

- 在 `freebsd-hackers@` 或相关子系统列表发帖，说您正在退出驱动程序并邀请其他人接手。
- 提交一个标记为维护者转换问题的 Bugzilla 条目。
- 直接电子邮件审查过您补丁的提交者并告诉他们。

项目然后将找到新维护者，或将驱动程序标记为孤儿，或决定其他路径。重要的是状态已知。静默放弃比任何替代方案都更糟。

如果无人接手驱动程序且它继续被使用，项目最终可能将其标记为废弃。这不是失败；这是对无人积极照料的代码的合理响应。驱动程序可以被废弃、移除，稍后如果有人站出来再重新添加。源代码树的历史充满了这样的周期。

### 第 8 节小结

合并后维护是比初始提交更轻量级的活动，但它是真实的。期望是：监控 Bugzilla 中针对您的驱动程序的错误、响应联系您的用户、随着内核演进保持驱动程序对当前 HEAD 构建、参与发布周期，如果您无法继续维护，公开说明。随时间参与作者的驱动程序是项目在初始合并之外重视的驱动程序。

完成第 1 到 8 节，我们演练了驱动程序提交的完整弧线：从了解项目到准备文件、通过许可、手册页、测试、提交、审查迭代和长期维护。本章的剩余部分提供动手实验和挑战练习，让您对真实代码演练工作流程，然后是思维模型巩固和通往结束章节的桥梁。

## 动手实验

本章的实验设计用于对真实驱动程序进行。最简单的方法是采用您在本书期间已经编写的驱动程序，如前面章节的 LED 驱动程序或第 36 章的模拟设备，并带它通过提交准备的工作流程。

如果您手头没有驱动程序，`examples/part-07/ch37-upstream/` 中的配套示例包含一个骨架驱动程序您可以使用。

所有实验可以在 FreeBSD 14.3 开发虚拟机中完成。它们都不会向真正的 FreeBSD 项目提交任何东西，所以您可以自由工作而不必担心意外发布半成品。

### 实验 1：准备文件布局

目标：将现有驱动程序重新排列为传统 FreeBSD 布局。

步骤：

1. 确定您将要使用的驱动程序。称之为 `mydev`。
2. 创建目录结构：
   - `sys/dev/mydev/` 用于驱动程序源。
   - `sys/modules/mydev/` 用于模块 Makefile。
   - `share/man/man4/` 用于手册页。
3. 将 `.c` 和 `.h` 文件移动或复制到 `sys/dev/mydev/`。如有必要重命名它们，使主要源文件为 `mydev.c`、内部头文件为 `mydev.h`、任何硬件寄存器定义位于 `mydevreg.h`。
4. 按照第 2 节模板在 `sys/modules/mydev/Makefile` 编写模块 Makefile。
5. 用 `make` 构建模块。修复任何构建错误。
6. 验证模块可以用 `kldload` 加载并用 `kldunload` 卸载。

成功标准：驱动程序作为可加载模块构建，文件布局符合源代码树约定。

预期时间：小型驱动程序 30 到 60 分钟。

常见问题：

- 假设旧布局的包含路径。修复包含以使用 `<dev/mydev/mydev.h>` 而非 `"mydev.h"`。
- `SRCS` 中遗漏条目。如果有多个 `.c` 文件，列出全部。
- 缺少 `.PATH`。Makefile 需要 `.PATH: ${SRCTOP}/sys/dev/mydev` 以便 make 能找到源。

### 实验 2：审核代码风格

目标：使驱动程序源符合 `style(9)`。

步骤：

1. 对驱动程序中的每个 `.c` 和 `.h` 文件运行 `/usr/src/tools/build/checkstyle9.pl`。捕获输出。
2. 仔细阅读每个警告。与 `style(9)` 交叉参考以了解规则是什么。
3. 在源中修复每个警告。每批修复后重新运行风格检查器。
4. 当风格检查器干净时，用眼睛阅读源寻找检查器遗漏的任何东西：多行参数内的不一致缩进、注释风格、变量声明分组。
5. 确保每个未导出的函数有 `static` 关键字。确保每个导出的函数在头文件中有声明。

成功标准：风格检查器对驱动程序中的任何文件不产生警告。

预期时间：对于之前未经过风格审核的驱动程序一到三小时。

常见意外：

- 您以为可以的行上的空格而非制表符警告。检查器严格；信任它。
- 变量声明和代码之间空行警告。`style(9)` 要求空行。
- 返回表达式无括号警告。通过添加括号修复。

### 实验 3：添加版权头部

目标：确保每个源文件有正确的 FreeBSD 风格版权头部。

步骤：

1. 识别驱动程序中需要头部的每个文件：每个 `.c` 文件、每个 `.h` 文件和手册页。
2. 对于每个文件，检查现有头部。如果缺失或畸形，用已知良好的模板替换它。
3. 在 `.c` 和 `.h` 文件中使用 `/*` 作为头部开头。在手册页中使用 `.\"` 作为开头。
4. 将您的姓名和电子邮件添加到版权行。
5. 在版权行之后包含 SPDX-License-Identifier 行，带有适当许可证，通常为 `BSD-2-Clause`。
6. 包含标准许可证文本。
7. 验证文件以 `/*` 或 `.\"` 开头从第 1 列开始。

成功标准：每个文件有正确格式的头部，匹配已在源代码树中文件的约定。

预期时间：30 分钟。

验证：

- 将您的头部与 `/usr/src/sys/dev/null/null.c` 中的头部比较。它们应该结构相同。
- 如果您使用自动许可证收集工具，它应该识别您的头部。

### 实验 4：起草手册页

目标：为驱动程序编写完整的第 4 节手册页。

步骤：

1. 创建 `share/man/man4/mydev.4`。
2. 从本章第 4 节的模板或配套示例开始。
3. 为您的驱动程序填写每一节：
   - NAME 和 NAME 描述。
   - SYNOPSIS 显示如何在内核中编译或作为模块加载。
   - DESCRIPTION 以散文形式。
   - HARDWARE 列出支持的设备。
   - FILES 列出设备节点。
   - SEE ALSO 带相关交叉引用。
   - HISTORY 注明驱动程序首次出现。
   - AUTHORS 带您的姓名和电子邮件。
4. 全文遵循一行一句规则。
5. 运行 `mandoc -Tlint mydev.4` 并修复每个警告。
6. 用 `mandoc mydev.4 | less -R` 渲染页面并从用户视角阅读。修复任何尴尬的东西。
7. 如果您安装了 `igor`，运行它并处理其警告。

成功标准：`mandoc -Tlint` 无声，渲染页面读起来清晰。

预期时间：首次手册页一到两小时。

实验阅读作业：开始之前，阅读 `/usr/src/share/man/man4/null.4`、`/usr/src/share/man/man4/led.4` 和 `/usr/src/share/man/man4/re.4`。这三页跨越第 4 节手册页可能具有的复杂度范围，它们将给您关于您的应该是什么样的强烈直觉。

### 实验 5：构建和加载自动化

目标：编写自动化提交前构建和加载周期的 shell 脚本。

步骤：

1. 在配套示例目录中创建名为 `pre-submission-test.sh` 的脚本。
2. 脚本应该按顺序：
   - 对每个源文件运行风格检查器。
   - 对手册页运行 `mandoc -Tlint`。
   - 在模块目录中运行 `make clean && make obj && make depend && make`。
   - 用 `kldload` 加载结果模块。
   - 用 `kldunload` 卸载模块。
   - 清楚报告成功或失败。
3. 使用 `set -e` 使脚本在第一个错误时退出。
4. 包含宣布每个阶段的有帮助的 echo 语句。
5. 对您的驱动程序测试脚本。

成功标准：脚本对准备好提交的驱动程序干净运行，对有问题的驱动程序产生清晰的错误输出。

预期时间：简单脚本 30 分钟；如果添加润色则更长。

### 实验 6：生成提交补丁

目标：演练补丁生成工作流程而不实际提交。

步骤：

1. 在源代码树的一次性 git 克隆中，为您的驱动程序创建主题分支：

   ```sh
   git checkout -b mydev-driver
   ```

2. 添加驱动程序文件：

   ```sh
   git add sys/dev/mydev/ sys/modules/mydev/ share/man/man4/mydev.4
   ```

3. 按照第 2 节的约定用适当的消息提交：

   ```sh
   git commit -s
   ```

4. 生成补丁：

   ```sh
   git format-patch -1 HEAD
   ```

5. 阅读生成的 `.patch` 文件。验证它看起来干净：
   无不相关更改，无尾随空格，格式良好的提交消息。
6. 对新克隆应用补丁以验证它干净应用：

   ```sh
   git am < 0001-mydev-Add-driver.patch
   ```

成功标准：您有一个干净的补丁文件代表驱动程序提交，它干净应用于新源代码树。

预期时间：30 分钟。

常见意外：

- `git format-patch` 每个提交产生一个文件。如果您的分支上有三个提交，您将得到三个 `.patch` 文件。对于应该是单个提交的驱动程序提交，先修改或压缩。
- 提交中的尾随空格在补丁中显示为 `^I` 序列。提交前删除它。
- 行尾问题。确保您的编辑器使用 LF，而非 CRLF。

### 实验 7：起草审查封面信

目标：演练编写随提交附带的描述。

步骤：

1. 打开文本编辑器，为您的驱动程序提交编写电子邮件风格的封面信。
2. 包括：
   - 适合邮件列表消息的主题行。
   - 一段驱动程序做什么的摘要。
   - 支持的硬件的描述。
   - 测试了什么的列表。
   - 您邀请什么反馈的声明。
3. 保持语气专业和合作。您在请求审查，而非要求批准。
4. 将信件作为 `cover-letter.txt` 保存在您的配套示例目录中。
5. 继续之前与朋友或同事分享以获得反馈。

成功标准：封面信读作对审查的生产性邀请。

预期时间：15 到 30 分钟。

### 实验 8：排演审查周期

目标：排演审查周期的迭代方面。

步骤：

1. 请同事像审查者一样阅读您的补丁和封面信。
2. 将他们的反馈作为评论列表捕获。
3. 将每个评论视为真实审查评论。对每个评论响应：进行修复、解释您的推理或建设性地反驳。
4. 更新提交并重新生成补丁。
5. 重复至少两轮反馈。

成功标准：您有迭代补丁以响应反馈的经验，您的最终提交仍是单个干净提交而非混乱历史。

预期时间：可变，取决于同事可用性。

变体：如果同事不可用，请审查者阅读配套代码并担任模拟审查者。或在您的环境中使用在线代码审查模拟器（如果可用）。

## 挑战练习

挑战练习是可选的但强烈推荐。它们每一项都取本章的一个想法并推向锻炼您判断力的领域。

### 挑战 1：审核历史驱动程序

在 `/usr/src/sys/dev/` 中选择一个已在源代码树中至少五年的较旧驱动程序。查看其当前状态并识别：

- 不匹配现代约定的版权头部部分。
- `checkstyle9.pl` 标记的风格违规。
- 不匹配现代风格的手册页章节。
- 驱动程序仍使用的废弃 API。

将您的发现写成简短报告。不要提交补丁修复它们（较旧驱动程序常有历史形式的充分理由），而是理解它们为什么看起来是这样。

目标是建立区分现代约定和历史的东西的眼光。做完此练习后，您将一眼识别驱动程序的哪些部分是最近编写的，哪些是遗留。

预期时间：两小时。

### 挑战 2：跨架构调试

采用您的驱动程序并尝试为非本机架构构建它，例如如果您在 `amd64` 上则为 `arm64`：

```sh
cd /usr/src
make TARGET=arm64 buildkernel KERNCONF=GENERIC
```

识别特定于目标架构的任何警告或错误。修复它们。重新构建。重复。

如果您的驱动程序在 `amd64` 和 `arm64` 上都干净构建，尝试 `i386`。如果您想要额外挑战，尝试 `powerpc64` 或 `riscv64`。每个架构会暴露不同类型的问题。

写下关于您发现了什么以及如何修复它的简短说明。跨架构纪律是区分随意编写的驱动程序和生产级驱动程序的因素之一。

预期时间：三到六小时，取决于您尝试多少架构。

### 挑战 3：手册页深度

在源代码树中选择一个您印象深刻的驱动程序手册页。复制该页面的结构并将其用作模板以类似深度重写自己的手册页。

您重写的手册页应该：

- 有显示加载和配置驱动程序的所有方式的 SYNOPSIS。
- 有给用户完整画面驱动程序做什么的 DESCRIPTION。
- 有完整 HARDWARE 章节，包括相关的修订信息。
- 如果您的驱动程序有任何这些特性，有 LOADER TUNABLES、SYSCTL VARIABLES 或 DIAGNOSTICS 章节。
- 有诚实描述已知问题的 BUGS 章节。
- 通过 `mandoc -Tlint` 和 `igor` 干净。

目标是产生读起来像该类型一级范例的手册页，而非最低合规工件。

预期时间：三到五小时。

### 挑战 4：运行模拟审查

与本书的另一位读者或熟悉 FreeBSD 的同事合作。与它们交换驱动程序。您审查他们的驱动程序。他们审查您的。

作为审查者，为您正在审查的驱动程序做以下事情：

- 自己运行所有提交前测试并捕获结果。
- 仔细阅读代码。对任何似乎不清楚、不正确或不符合惯例的东西做具体评论。
- 阅读手册页。对任何似乎不完整或不清楚的东西做评论。
- 编写包含您的整体印象、请求的更改和您有任何问题的摘要审查说明。

作为接受审查的贡献者，做以下事情：

- 仔细阅读反馈。
- 建设性地响应每个评论。
- 更新补丁。
- 发回更新的补丁。

至少进行两轮。在最后，写下您从双方学到了什么的简短反思。

目标是在您向真实项目提交补丁之前体验审查过程的两边。此练习后，第一次真实审查会感觉熟悉得多。

预期时间：可变，但至少一个周末。

### 挑战 5：追踪真实提交的生命周期

在 FreeBSD 源代码树中选择一个最近的驱动程序相关提交，最好是由非提交者贡献并由他人赞助的提交。使用 `git log` 找到它，或浏览 Phabricator 档案。

追踪其历史：

- 审查何时首次打开？
- 第一版是什么样子的？
- 审查者留下什么评论？
- 作者如何响应？
- 补丁如何演进？
- 何时最终提交？
- 最终提交消息说什么？

写下您发现的简短叙述。此练习建立对真实审查内部看起来是什么样的直觉。

预期时间：两小时。

## 故障排除和常见错误

即使认真准备，事情也可能出错。本节收集首次贡献者遇到的最常见问题并解释如何诊断和修复它们。

### 补丁因风格被拒绝

症状：审查者留下许多关于缩进、变量名、注释格式或返回语句括号的小评论。

原因：补丁在未先运行 `tools/build/checkstyle9.pl` 情况下提交，或作者忽略了一些警告。

修复：对每个源文件运行 `checkstyle9.pl`。修复每个警告。重新构建和重新测试。重新提交。

预防：将 `checkstyle9.pl` 作为提交前脚本的一部分。每次提交前运行它。

### 补丁因手册页问题被拒绝

症状：审查者说手册页有 lint 错误，或不匹配项目的 mdoc 风格。

原因：手册页在提交前未用 `mandoc -Tlint` 验证，或未遵循一行一句规则。

修复：对手册页运行 `mandoc -Tlint`。修复每个警告。阅读渲染输出以验证读起来好。重新提交。

预防：用与代码同样的关注对待手册页。将其包含在提交前脚本中。

### 补丁无法干净应用

症状：审查者报告补丁无法应用于当前 HEAD。或 CI 在 `git apply` 阶段失败。

原因：补丁是针对较旧版本的源代码树生成的，HEAD 自那以来已移动。

修复：拉取最新 HEAD，在其上变基您的分支，解决任何冲突，重新测试并重新生成补丁。

预防：紧接提交前对当前 HEAD 变基。不要提交一周前生成的补丁。

### 加载时内核崩溃

症状：`kldload` 导致内核崩溃。

原因：通常是驱动程序的 `probe` 或 `attach` 例程中的 NULL 指针解引用，或缺少初始化步骤。

修复：用标准内核调试工具调试（在第 34 章覆盖）。常见具体原因：

- `device_get_softc(dev)` 返回 NULL，因为 `driver_t.size` 字段未设置为 `sizeof(struct mydev_softc)`。
- `bus_alloc_resource_any(dev, SYS_RES_MEMORY, ...)` 返回 NULL，驱动程序在使用结果前未检查 NULL。
- 静态变量错误初始化，导致未定义行为。

预防：在提交前在开发 VM 中测试。重复加载和卸载以捕捉初始化错误。

### 卸载时内核崩溃

症状：`kldunload` 崩溃，或模块拒绝卸载。

原因：分离路径不完整。常见具体原因：

- softc 释放时仍有调度的 callout。使用 `callout_drain`，而非 `callout_stop`。
- 仍有待处理的 taskqueue 任务。对每个任务使用 `taskqueue_drain`。
- 资源释放时中断处理器仍安装。在调用 `bus_release_resource` 之前用 `bus_teardown_intr` 拆除处理器。
- 调用 `destroy_dev` 时设备节点仍打开。如果节点可能打开，使用 `destroy_dev_drain`。

修复：审核分离路径。确保每个资源被释放、每个 callout 被排空、每个任务被排空、每个处理器被拆除、每个设备节点被销毁，在 softc 释放前。

预防：按附加代码的逆序组织分离代码。每个 `attach` 步骤有对应的 `detach` 步骤，顺序严格。

### 驱动程序构建但不探测

症状：模块加载，但硬件存在时驱动程序不附加。`pciconf -l` 显示设备无驱动程序。

原因：通常是驱动程序预期厂商/设备 ID 与实际 ID 在 `probe` 例程中不匹配。或驱动程序错误使用 `ENXIO`。

修复：检查厂商和设备 ID。用 `pciconf -lv` 双重检查。验证 `probe` 在设备匹配时返回 `BUS_PROBE_DEFAULT` 或 `BUS_PROBE_GENERIC`，而非错误代码。

预防：提交前针对真实硬件测试。

### 手册页不渲染

症状：`man 4 mydev` 无输出，或显示原始 mdoc 源代码。

原因：通常是文件在错误位置，或命名不正确，或 `makewhatis` 未运行。

修复：验证路径（`/usr/share/man/man4/mydev.4`）、验证名称（必须以 `.4` 结尾）、运行 `makewhatis /usr/share/man` 重建手册数据库。

预防：提交前测试手册页安装。

### 审查者无响应

症状：您提交了补丁、响应了初始评论，然后审查者静默了。

原因：审查者是志愿者。他们的时间有限。有时补丁滑出雷达。

修复：至少等一周。然后在审查线程或相关邮件列表发送礼貌 ping。如果仍然静默，考虑请求另一位审查者捡起。

预防：提交小型、准备良好、易于审查的补丁。较小补丁获得较快审查。

### 补丁已批准但未提交

症状：审查者明确说补丁看起来不错，但尚未提交。

原因：审查者可能不是提交者，或他们是提交者但等待第二意见，或他们忙于其他事情。

修复：礼貌询问是否有人能够提交补丁。"Is anyone able to sponsor the commit of this patch? I have responded to all feedback and the review is approved."

预防：无具体；这是正常项目流程的一部分。

### 补丁已提交但您未被记入

症状：您查看提交日志看到您的补丁已提交，但作者字段错误。

原因：提交者可能意外应用补丁未保留作者身份。这很少见但发生。

修复：礼貌电子邮件提交者询问是否可以更正作者身份。推送前的 `git commit --amend` 配合正确作者可以修复；推送后，提交是不可变的，但提交者可以添加说明或在罕见情况下修改原始提交消息。

预防：用 `git format-patch` 生成补丁时，确保您的 `user.name` 和 `user.email` 设置正确。

### 您的驱动程序被接受但您的接口选择错误

症状：您的驱动程序在源代码树中，但您后来意识到您设计的用户态接口是糟糕的契合。

原因：在完整使用经验之前做出的设计选择有时结果错误。

修复：这是项目定期处理的真实工程问题。选项包括：在旧接口旁边添加新接口并废弃旧的；将旧接口记录为遗留并引入后继；或者，很少，如果驱动程序用户足够少以至于破坏可接受，进行破坏性更改。

预防：在实现前在邮件列表上讨论接口设计，特别是将在用户态可见很长时间的接口。

## 总结

向 FreeBSD 项目提交驱动程序是一个有许多步骤的过程，但不是一个神秘的过程。按顺序执行的步骤将引导您从自己机器上的工作驱动程序到 FreeBSD 源代码树中维护的驱动程序。该过程涉及了解项目如何组织、按照项目约定准备文件、正确处理许可证、编写适当的手册页、跨项目支持的架构测试、生成干净的补丁、耐心地导航审查流程，并致力于驱动程序合并后的长期维护弧线。

贯穿本章的几个主题值得最后明确总结。

第一个主题是，工作驱动程序与上游就绪驱动程序不同。您在本书中编写的代码是工作代码；使其上游就绪是额外工作，那些工作大部分在于小约定而非大更改。对那些约定的关注是受欢迎的首次提交与在审查中反复停滞的首次提交之间的区别。

第二个主题是，上游审查是合作的，而非对立的。补丁另一边的审查者正在帮助您的驱动程序以源代码树可以向前承载的形式降落。他们的评论是他们时间的投资，而非对您能力的攻击。建设性地、耐心地和实质性地响应那些评论是审查过程的技艺。内化这种框架的首次贡献者比没有的审查更容易。

第三个主题是，文档、许可证和风格是工程质量的一部分，而非官僚主义。您编写的手册页是用户将理解您的驱动程序的接口，只要它存在。您附上的许可证决定驱动程序是否可以合并。您遵循的风格决定未来维护者是否会理解代码。这些都不是行政开销；它们是大型共享代码库中作为软件工程师工作的一部分。

第四个主题是，合并是开始而非结束。源代码树中的驱动程序需要持续照料：错误分类、API 漂移修复、发布时检查和偶尔增强。那种照料比初始提交轻，但它是真实的，它是一次性提交变为项目持续贡献的一部分。

第五个也是最重要的主题是，所有这些都是可学习的。本章中的技能不需要您在本书中已建立起来的能力之外的天赋。它们需要关注细节、迭代耐心和与社区接触的意愿。那些品质是您可以通过练习发展的。FreeBSD 项目中的提交者都从您现在的地方开始，作为编写第一批补丁的贡献者，他们通过您可以的同样稳定的认真工作积累建立起他们的地位。

花一点时间欣赏您的工具包中发生了什么变化。本章之前，向 FreeBSD 提交驱动程序可能是一个模糊的愿望。现在它是一个有有限步骤的具体过程，每个步骤您都已详细看到。实验给了您练习。挑战给了您深度。错误部分给了您常见陷阱的地图。如果您决定在未来几周或几个月提交真实驱动程序，您拥有开始所需的一切。

本章中的一些具体内容会随时间变化。Phabricator / GitHub 平衡可能进一步向 GitHub 倾斜，或向后，或两者皆非。风格检查工具可能演进。审查约定可能有小的改进。在我们知道约定正在变化的地方，我们标明了。在我们引用特定文件的地方，我们命名它以便您可以自己打开并检查当前状态。信任但也验证的读者是项目最能从中受益的读者。

您现在，在实践意义上，准备好贡献了。您是否这样做取决于您。像这样的书的许多读者从不贡献；这没关系，您在这里构建的技能在您自己的工作中为您服务，无论。一些读者将贡献一次，降落补丁，然后继续；那也没关系，项目感谢他们。较少的人会发现他们足够享受合作以继续贡献，随时间推移他们将深入参与。这些路径中的任何一条都是合法的。选择权在您。

## 通往第 38 章的桥梁：最后的想法和后续步骤

在某种意义上，本章一直是本书实践弧线的顶点。您从没有内核知识开始，学习了 UNIX 和 C，了解了 FreeBSD 驱动程序的形态，构建了字符和网络驱动程序，与 Newbus 框架集成，并通过一系列涵盖生产驱动程序遇到的专门场景的精通主题工作。您刚刚完成的章节带您走过将您构建的驱动程序成为 FreeBSD 操作系统本身的一部分的过程，由工程师社区维护并在每个版本中发货给用户。

第 38 章是本书的结束章节。它不是另一个技术章节。它的角色不同。它是一个机会来盘点您的进度、反思您学到了什么、考虑您现在站在哪里，并思考您接下来可能去哪里。

第 37 章的几个主题将自然延续到第 38 章。合并是开始而非结束的想法，例如，不仅适用于个别驱动程序，也适用于读者与 FreeBSD 整体的关系。编写一个驱动程序，或两个，或十个，是开始；与项目的持续接触是更长的弧线。本章在代码审查语境下主张的合作心态是使人随时间成为有价值的社区成员的相同心态。本章在个别驱动程序语境下主张的文档、许可和风格的纪律扩展为在任何大型代码库中成为认真工程师的纪律。

第 38 章还将解决本书未完全覆盖的主题，如文件系统集成、网络堆栈集成（例如 Netgraph）、USB 复合设备、PCI 热插拔、SMP 调优和 NUMA 感知驱动程序。每一个都是其本身的重要主题，结束章节将为您指向您可以用来自己学习它们的资源。本书给了您基础；第 38 章的主题是您可以将该基础扩展到的方向。

还有其他 BSD。您学到的大部分内容可以修改转移到 OpenBSD 和 NetBSD。您为 FreeBSD 编写的驱动程序可能在那些项目中找到有用的类似物，第 7 部分的一些精通主题在每个其他 BSD 中有直接等效物。如果您对更广泛的 BSD 世界感兴趣，第 38 章将建议去哪里看。

还有社区的问题。FreeBSD 项目不是抽象；它是共同生产、维护和支持操作系统的工程师、文档编写者、发布管理者和用户的社区。第 38 章将反思成为该社区一部分意味着什么、如何找到您在其中的位置，以及如何超越驱动程序提交本身为它做出贡献。翻译、文档、测试、错误分类和指导都是贡献形式，项目重视每一项。

在结束本章之前的最后一个反思。提交驱动程序，其核心，是一种信任行为。您将代码提供给将向前承载它的工程师社区。他们反过来将他们的关注、他们的审查时间和他们的提交权限提供给直到此补丁到达前是陌生人的贡献者。信任是双向的。它是通过许多认真工作和负责任接触的小行为随时间建立起来的。首次提交是那种信任的开始，而非结束。当您成为提交者时，如果您选择那条路，信任是您在数百次小交互中赢得的东西。

您已经完成了成为项目可以信任的人的大部分工作。其余是练习、时间和耐心。

第 38 章将以反思、继续学习的建议和关于您可以从这里去哪里的最后话语结束本书。深呼吸，合上笔记本电脑片刻，让本章的材料沉淀。当您准备好时，翻页。



