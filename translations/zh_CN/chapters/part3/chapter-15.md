---
title: "更多同步机制：条件变量、信号量与协调"
description: "第3部分的最后一章：用于准入控制的计数信号量、针对读多写少场景的精炼 sx(9) 模式、可中断和超时感知的等待、跨组件握手，以及一个让驱动程序的同步故事对未来维护者真正可读的封装层。"
partNumber: 3
partName: "并发与同步"
chapter: 15
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "待定"
translator: "AI辅助翻译为简体中文"
estimatedReadTime: 165
language: "zh-CN"
---

*"始于好奇者终成技艺，成于技艺者赋能后人。"* — Edson Brandi

# 更多同步机制：条件变量、信号量与协调

## 读者指南与学习目标

在第14章结束时，你的 `myfirst` 驱动程序达到了一个与第3部分开始时截然不同的状态。它有一个已文档化的数据路径互斥锁、两个条件变量、一个配置 sx 锁、三个 callout、一个包含三个任务的私有任务队列，以及一个以正确顺序排空每个原语的分离路径。这个驱动程序第一次不再只是处理程序的集合。它是一个协作的同步原语组合，能够在负载下提供有界、安全的行为。

第15章是关于将这种组合推向更远。大多数真正的驱动程序最终会发现，互斥锁和基本条件变量，即使与 sx 锁和任务队列结合使用，也不总是能够轻松表达特定问题的原语。驱动程序可能需要限制并发写入者的数量、强制执行可重用硬件槽的有界池、协调 callout 和任务之间的握手、让缓慢的读取操作响应信号而不丢失已取得的部分进度，或者以一种每个代码片段都能廉价检查的方式跨多个子系统暴露关闭状态。这些形状中的每一个都可以用你已经知道的东西解决，但每一个都有一个原语或惯用法使解决方案直接且代码可读。本章逐一教授这些原语和惯用法，将它们应用于驱动程序，并用一个小的封装层将结果串联起来，将分散的调用转化为命名词汇表。

本章也是第3部分的最后一章。第15章之后，第4部分开始，本书转向硬件。第3部分教你的每一个原语，从第11章的第一个 `mtx_init` 到第14章的最后一个 `taskqueue_drain`，都会伴随你进入第4部分。本章中的协调模式不是一个附加主题。它们是驱动程序携带到面向硬件章节的同步工具箱的最后一块拼图。

### 为什么本章值得独立成章

你可以跳过本章。第14章结束时的驱动程序是功能性的、经过测试的，在技术上是正确的。它的互斥锁和条件变量纪律是健全的。它的分离顺序有效。它的任务队列是干净的。

驱动程序缺少的，也是第15章添加的，是一小组针对特定协调形状的更锋利工具，这些形状用互斥锁和基本条件变量表达起来很别扭。计数信号量是几行代码，表达"一次最多 N 个参与者"；用互斥锁、计数器和条件变量表达相同的不变量需要更多行代码并隐藏意图。带有 `sx_try_upgrade` 的精炼 sx 模式让读路径偶尔提升为写者而不释放其槽位并与其他潜在写者竞争；没有这个原语，你需要编写别扭的重试循环。正确的 `cv_timedwait_sig` 使用区分了 EINTR 和 ERESTART，区分了"调用者被中断"和"截止时间已到"；朴素的等待让调用者挂起或在任何信号到达时放弃部分工作。

学习这些工具的回报不仅仅是当前章节的重构会更干净。而是当你在一年后阅读生产 FreeBSD 驱动程序时，你会立即识别这些形状。当 `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c` 在每个请求的信号量上调用 `sema_wait` 以阻塞直到硬件完成时，你会知道作者在想什么。当网络驱动程序在统计信息更新路径中使用 `sx_try_upgrade` 时，你会知道为什么那是正确的选择。没有第15章，这些调用是不透明的。有了第15章，它们是显而易见的。

另一个回报是可维护性。将同步词汇表分散在百处之地的驱动程序很难更改。将同步封装在小的命名层中的驱动程序（即使只是头文件中的一组内联函数）很容易更改。第6节明确介绍了封装；在本章结束时，你的驱动程序将有一个小的 `myfirst_sync.h`，命名它使用的每个协调原语。稍后添加新的同步状态变成了扩展头文件的练习，而不是在文件中分散新的 `mtx_lock`/`mtx_unlock` 调用。

### 第14章留给驱动程序的状态

在开始之前需要验证的几个先决条件。第15章扩展第14章第4阶段结束时产生的驱动程序（版本 `0.8-taskqueues`）。如果以下任何一项感觉不确定，请在开始本章之前返回第14章。

- 你的 `myfirst` 驱动程序可以干净地编译，并标识自己为版本 `0.8-taskqueues`。
- 它在 `sc->mtx`（数据路径互斥锁）周围使用 `MYFIRST_LOCK`/`MYFIRST_UNLOCK` 宏。
- 它在 `sc->cfg_sx`（配置 sx）周围使用 `MYFIRST_CFG_SLOCK`/`MYFIRST_CFG_XLOCK`。
- 它使用两个命名条件变量（`sc->data_cv`、`sc->room_cv`）用于阻塞读和写。
- 它通过 `cv_timedwait_sig` 和 `read_timeout_ms` sysctl 支持定时读取。
- 它有三个 callout（`heartbeat_co`、`watchdog_co`、`tick_source_co`）及其间隔 sysctl。
- 它有一个私有任务队列（`sc->tq`），包含三个任务（`selwake_task`、`bulk_writer_task`、`reset_delayed_task`）。
- 锁顺序 `sc->mtx -> sc->cfg_sx` 在 `LOCKING.md` 中有文档记录并由 `WITNESS` 强制执行。
- `INVARIANTS`、`WITNESS`、`WITNESS_SKIPSPIN`、`DDB`、`KDB` 和 `KDB_UNATTENDED` 在你的测试内核中已启用；你已经构建并启动了它。
- 第14章压力套件在调试内核上干净运行。

那个驱动程序就是我们在第15章中扩展的内容。添加在代码量上适中，但在其启用的内容上意义重大。驱动程序的数据路径在机械级别上没有改变；改变的是它用来讨论并发的词汇表。

### 你将学到什么

完成本章后，你将能够：

- 识别何时互斥锁加条件变量不是特定不变量的正确原语，并命名替代方案（信号量、sx 升级模式、带内存屏障的原子标志、每 CPU 计数器或封装的协调器函数）。
- 解释计数信号量是什么，它与互斥锁和二进制信号量有何不同，以及为什么 FreeBSD 的 `sema(9)` API 专门是没有所有权概念的计数信号量 API。
- 正确使用 `sema_init`、`sema_wait`、`sema_post`、`sema_trywait`、`sema_timedwait`、`sema_value` 和 `sema_destroy`，包括调用 `sema_destroy` 时不能有等待者存在的生命周期契约。
- 描述 FreeBSD 内核信号量的已知限制：无优先级继承、无信号可中断等待，以及 `/usr/src/sys/kern/kern_sema.c` 中关于为什么它们不是互斥锁加条件变量的通用替代品的指导。
- 使用 `sx_try_upgrade`、`sx_downgrade`、`sx_xlocked` 和 `sx_slock` 模式精炼驱动程序的 sx 使用，以干净地表达读多写少的工作负载。
- 区分 `cv_wait`、`cv_wait_sig`、`cv_timedwait` 和 `cv_timedwait_sig`，并知道每个在超时、信号和正常唤醒时返回什么。
- 正确处理信号可中断等待返回的 EINTR 和 ERESTART 值，以便驱动程序上的 `read(2)` 和 `write(2)` 对 `SIGINT` 等信号响应合理。
- 使用受驱动程序互斥锁保护的小状态标志构建 callout、任务和用户线程之间的跨组件握手。
- 引入一个 `myfirst_sync.h` 头文件，命名驱动程序使用的每个同步原语，以便未来的贡献者可以在一处更改锁定策略。
- 正确使用 `atomic(9)` API 进行小型无锁协调步骤，特别是需要跨上下文可见而不带锁的关闭标志。
- 编写压力测试，故意触发驱动程序同步中的竞争条件，并确认原语处理它们。
- 将驱动程序重构为版本 `0.9-coordination`，并用信号量和协调部分更新 `LOCKING.md`。

这是一长串列表。每一项都很小；本章的价值在于组合。

### 本章不涵盖的内容

几个相邻主题被明确推迟，以保持第15章专注。

- **硬件中断处理程序和 `FILTER` 与 `ITHREAD` 执行上下文之间的完整分割。** 第4部分介绍 `bus_setup_intr(9)` 和实际的中断故事。第15章只在它们说明你可能重用的同步模式时提到中断相关的上下文。
- **大规模无锁数据结构。** `atomic(9)` 系列涵盖小型协调标志；它不涵盖 SMR、危险指针、RCU 类似物或完整的无锁队列。第15章简要触及原子操作和 epoch；更深层的无锁故事属于专门的内核内部讨论。
- **详细的调度器调优。** 线程优先级、RT 类、优先级继承、CPU 亲和性：超出范围。我们选择合理的默认值并继续前进。
- **用户空间 POSIX 信号量和 SysV IPC。** 内核中的 `sema(9)` 是不同的东西。第15章专注于内核原语。
- **性能微基准测试。** Lockstat 和 DTrace 锁分析得到提及，而不是完整处理。书中稍后的专门性能章节（如果存在）将承担该负载。
- **跨进程协调原语。** 某些驱动程序需要与用户空间助手协调；那个问题根本不同，属于稍后关于基于 ioctl 协议的章节。

坚守这些界限保持本章的心理模型连贯。第15章添加协调工具包；第4部分和后续章节将工具包应用于面向硬件的场景。

### 预估时间投入

- **仅阅读**：约三到四小时。API 面积小但组合需要一些思考。
- **阅读加输入工作示例**：七到九小时，分两次会话。驱动程序分四个阶段演化。
- **阅读加所有实验和挑战**：十二到十六小时，分三四次会话，包括对易竞争代码路径运行压力测试的时间。

如果你发现第5节（跨子系统协调）在第一次通过时令人困惑，那是正常的。材料在概念上简单，但需要同时记住驱动程序的几个部分。停下来，重读第5节中的工作握手，当图表稳定后继续。

### 先决条件

在开始本章之前，确认：

- 你的驱动程序源代码与第14章第4阶段（`stage4-final`）匹配。起点假设每个第14章原语、每个第13章 callout、每个第12章条件变量和 sx，以及第11章并发 IO 模型。
- 你的实验机器运行 FreeBSD 14.3，磁盘上有 `/usr/src` 且与运行内核匹配。
- 调试内核已构建、安装并干净启动，启用了 `INVARIANTS`、`WITNESS`、`WITNESS_SKIPSPIN`、`DDB`、`KDB` 和 `KDB_UNATTENDED`。
- 你对第14章分离顺序理解得足够好，可以在不迷失的情况下扩展它。
- 你从第12章和第13章对 `cv_wait_sig` 和 `cv_timedwait_sig` 有舒适的心理模型。

如果以上任何一项不稳定，现在修复它，而不是推动通过第15章并试图从一个移动的基础推理。第15章的原语比之前介绍的更锋利，它们放大驱动程序已经拥有的任何纪律（或缺乏纪律）。

### 如何从本章获得最大收益

三个习惯会很快得到回报。

首先，将 `/usr/src/sys/kern/kern_sema.c` 和 `/usr/src/sys/sys/sema.h` 加入书签。实现很短，不到两百行，这是理解 FreeBSD 信号量实际做什么的最短路径。仔细阅读一次 `_sema_wait`、`_sema_post` 和 `_sema_timedwait`。知道信号量是"计数器加互斥锁加条件变量，封装在 API 中"使本章其余部分感觉显而易见。

> **关于行号的说明。** 本章中指向源代码的每个指针都挂在函数、宏或结构名称上，而不是数字行号。`kern_sema.c` 中的 `sema_init` 和 `_sema_wait`，以及 `/usr/src/sys/kern/kern_sx.c` 中的 `sx_try_upgrade`，在 FreeBSD 14.x 点发布版本中将保持可通过这些名称找到；每个占用的行号可能随着周围代码的修订而漂移。如有疑问，grep 符号。

其次，将每个新原语与你用旧原语会写的内容进行比较。练习"如果我没有 `sema(9)`，我会如何表达这个？"是有启发性的。用互斥锁和条件变量编写替代方案通常是可能的，但信号量版本通常只有一半长度且更清晰。看到对比是原语价值变得具体的方式。

第三，手动输入更改并在 `WITNESS` 下运行每个阶段。如果你在调试内核上运行，高级同步错误几乎总是在第一次接触时被 `WITNESS` 检测到；它们在生产内核上几乎总是静默的，直到第一次崩溃。`examples/part-03/ch15-more-synchronization/` 下的配套源代码是参考版本，但手动输入一次 `sema_init(&sc->writers_sema, 4, "myfirst writers")` 的肌肉记忆比阅读十次更有价值。

### 本章路线图

各节按顺序为：

1. **当互斥锁和条件变量不够时。** 调查受益于不同原语的问题形状。
2. **在 FreeBSD 内核中使用信号量。** 深入介绍 `sema(9)` API，以及作为第15章驱动程序第1阶段的写入者上限重构。
3. **读多写少场景和共享访问。** 精炼的 sx(9) 模式，包括 `sx_try_upgrade` 和 `sx_downgrade`，以及作为第2阶段的小型统计缓存重构。
4. **带超时和中断的条件变量。** 仔细处理 `cv_timedwait_sig`、EINTR 与 ERESTART、部分进度处理，以及让读取者观察行为的 sysctl 调优。驱动程序的第3阶段。
5. **模块或子系统之间的同步。** 通过小状态标志在 callout、任务和用户线程之间握手。入门级别的原子操作和内存排序。第4阶段从这里开始。
6. **同步与模块化设计。** `myfirst_sync.h` 头文件、命名纪律，以及当同步被封装时驱动程序如何改变形状。
7. **测试高级同步。** 压力套件、故障注入，以及让你看到原语工作的可观察性 sysctl。
8. **重构与版本控制。** 第4阶段完成、版本升级到 `0.9-coordination`、`LOCKING.md` 扩展，以及第3部分结束回归测试。

八个部分之后是动手实验、挑战练习、故障排除参考、结束第3部分的总结，以及开启第4部分的通往第16章的桥梁。第13章和第14章结尾的相同参考和速查材料在这里结尾再次出现。

如果这是你第一次阅读，请线性阅读并按顺序进行实验。如果你正在复习，第5节和第6节独立存在，适合单次阅读。



## 第1节：当互斥锁和条件变量不够时

第11章的互斥锁和第12章的条件变量是 FreeBSD 驱动程序同步的默认原语。几乎每个驱动程序都使用它们。许多驱动程序只使用它们。对于一大类问题，这种组合是完全正确的：互斥锁保护共享状态，条件变量让等待者睡眠直到状态匹配谓词，两者一起干净地表达"等待状态变得可接受"和"告诉其他线程状态已改变"。

本节讨论那些默认原语显得别扭的问题。不是互斥锁加条件变量组合无法表达不变量，而是它比其他原语更冗长、更易出错地表达它。识别这些形状是使用正确工具的第一步。

### 不匹配的形状

每个同步原语都有一个关于它保护什么的基础模型。互斥锁保护互斥：一次最多一个线程在锁内执行。条件变量保护谓词：等待者睡眠直到谓词变为真，信号发送者断言谓词已改变。两者组合是因为条件变量的等待自动释放并重新获取互斥锁，这让等待者在锁下观察谓词，在睡眠期间释放锁，在唤醒时重新获得锁。

当你要保护的不变量最好不用"最多一个"或"等待谓词"描述时，不匹配就出现了。一些常见的形状反复出现。

**有界准入。** 不变量是"一次最多 N 个某物"。对于 N 等于 1，互斥锁是自然的。对于 N 大于 1，互斥锁加条件变量加计数器版本需要你编写显式计数器、在互斥锁下测试它、如果计数器达到 N 则在条件变量上睡眠、进入时递减、退出时重新信号通知、并重新发现正确的唤醒策略。信号量原语用三个调用表达相同的不变量：`sema_init(&s, N, ...)`、入口处 `sema_wait(&s)`、出口处 `sema_post(&s)`。

**读多写少状态和偶尔提升。** 不变量是"许多读取者并发，或一个写入者；当读取者检测到需要写入时，提升"。sx(9) 锁原生处理多读取者或单写入者部分。提升部分（`sx_try_upgrade`）是一个原语，互斥锁加条件变量版本必须用类似读写锁的计数器和重试逻辑来模拟。

**必须在信号中断中保留部分进度的谓词。** 一个 `read(2)` 已经复制了一半请求的字节，现在正在睡眠等待更多，应该在被信号中断时返回已复制的字节而不是 EINTR。`cv_timedwait_sig` 给你 EINTR 和 ERESTART 的区别；用原始 `cv_wait` 加定期信号检查编写等效代码是可能的但容易出错。

**跨组件关闭协调。** 驱动程序的几个部分（callout、任务、用户线程）需要一致地观察"驱动程序正在关闭"。受互斥锁保护的标志是一个选项。对于这个特定模式，在写入者上有 seq-cst 栅栏、在读取者上有获取加载的原子标志通常更便宜、更清晰，本章将展示何时选择哪个。

**速率限制重试。** "最多每 100 毫秒执行一次此操作，如果已在进行中则跳过。" 用互斥锁和定时器可以表达，但任务队列加超时任务加"已调度"标志上的原子测试和设置通常更干净。这个模式在第14章末尾出现过；第15章精炼它。

对于每种形状，第15章选择一个适合的原语并排显示重构。目的不是争论信号量或 sx 升级或原子标志"更好"。目的是让你选择匹配问题的工具，以便你的驱动程序对下一个打开它的人来说读起来干净。

### 一个具体的动机示例：太多写入者

一个动机示例使不匹配具体化。假设驱动程序想要限制并发写入者的数量。"并发写入者"指同时在初始验证之后处于 `myfirst_write` 处理程序内的用户线程。限制是一个小整数，比如四，作为 sysctl 调优旋钮暴露。

互斥锁加计数器版本如下：

```c
/* 在 softc 中: */
int writers_active;
int writers_limit;   /* 通过 sysctl 配置。 */
struct cv writer_cv;

/* 在 myfirst_write 入口处: */
MYFIRST_LOCK(sc);
while (sc->writers_active >= sc->writers_limit) {
        int error = cv_wait_sig(&sc->writer_cv, &sc->mtx);
        if (error != 0) {
                MYFIRST_UNLOCK(sc);
                return (error);
        }
        if (!sc->is_attached) {
                MYFIRST_UNLOCK(sc);
                return (ENXIO);
        }
}
sc->writers_active++;
MYFIRST_UNLOCK(sc);

/* 在出口处: */
MYFIRST_LOCK(sc);
sc->writers_active--;
cv_signal(&sc->writer_cv);
MYFIRST_UNLOCK(sc);
```

每一行都是必要的。循环处理虚假唤醒和信号返回。信号检查保留部分进度（如果有）。`is_attached` 检查确保我们在分离后不继续。cv_signal 唤醒下一个等待者。调用者必须记得递减。

信号量版本如下：

```c
/* 在 softc 中: */
struct sema writers_sema;

/* 在 attach 中: */
sema_init(&sc->writers_sema, 4, "myfirst writers");

/* 在 destroy 中: */
sema_destroy(&sc->writers_sema);

/* 在 myfirst_write 入口处: */
sema_wait(&sc->writers_sema);
if (!sc->is_attached) {
        sema_post(&sc->writers_sema);
        return (ENXIO);
}

/* 在出口处: */
sema_post(&sc->writers_sema);
```

五行运行时逻辑，包括附加检查。原语直接表达不变量。看到 `sema_wait(&sc->writers_sema)` 的读者一眼就理解意图。

注意信号量版本放弃了什么。`sema_wait` 不是信号可中断的（我们将在第2节看到，FreeBSD 的 `sema_wait` 内部使用 `cv_wait`，而不是 `cv_wait_sig`）。如果需要可中断性，你回退到互斥锁加条件变量版本，或将 `sema_trywait` 与单独的可中断等待结合。每个原语都有其权衡；第2节命名它们。

更广泛的观点是，两个版本都不是"错误的"。互斥锁加计数器版本是正确的，已在驱动程序中使用了几十年。信号量版本是正确的，对于这个特定不变量更清晰。知道两者让你为手头的特定约束选择正确的一个。

### 本节其余部分预览本章

第1节故意简短。本章其余部分在各自的章节中展开每个形状，并各自重构 `myfirst` 驱动程序：

- 第2节进行写入者上限信号量重构作为第1阶段。
- 第3节进行读多写少 sx 精炼作为第2阶段。
- 第4节进行可中断等待精炼作为第3阶段。
- 第5节进行跨组件握手作为第4阶段的一部分。
- 第6节将同步词汇表提取到 `myfirst_sync.h`。
- 第7节编写压力测试。
- 第8节将所有内容串联起来并发布 `0.9-coordination`。

在深入之前，一个一般性观察。第15章的更改在代码行数上很小。整个章节可能向驱动程序添加不到两百行。它在心理模型上添加的更大。我们引入的每个原语表达在第14章驱动程序中隐式的不变量；使其显式是大部分价值所在。

### 第1节总结

互斥锁和条件变量覆盖大多数驱动程序同步。当不变量是"最多 N"、"多读取者或单写入者偶尔提升"、"可中断等待带部分进度"、"跨组件关闭"或"速率限制重试"时，不同的原语更直接地表达意图并留下更少的错误空间。第2节介绍这些原语中的第一个，计数信号量。



## 第2节：在 FreeBSD 内核中使用信号量

计数信号量是一个小型原语。在内部它是一个计数器、一个互斥锁和一个条件变量；API 将这三者封装成以计数器和等待正值语义作为主要接口的操作。FreeBSD 的内核信号量位于 `/usr/src/sys/sys/sema.h` 和 `/usr/src/sys/kern/kern_sema.c`。整个实现不到两百行。阅读一次是理解 API 保证的最快方式。

本节深入介绍 API，比较信号量与互斥锁和条件变量，遍历写入者上限重构作为第15章驱动程序的第1阶段，并命名随原语而来的权衡。

### 计数信号量，精确定义

计数信号量保存一个非负整数。API 暴露两个核心操作：

- `sema_post(&s)` 递增计数器。如果有人因计数器为零而等待，其中一人被唤醒。
- `sema_wait(&s)` 如果计数器为正则递减计数器。如果计数器为零，调用者睡眠直到 `sema_post` 递增它，然后递减并返回。

这两个操作组合给你有界准入。用 N 初始化信号量。每个参与者在入口调用 `sema_wait`，在出口调用 `sema_post`。不变量"最多 N 个参与者处于其等待和发布之间"自动保持。

FreeBSD 计数信号量与二进制信号量（只能是 0 或 1）不同，计数器可以高于 1。二进制信号量实际上是互斥锁，有一个重要区别：信号量没有所有权概念。任何线程都可以调用 `sema_post`；任何线程都可以调用 `sema_wait`。相比之下，互斥锁必须由获取它的同一线程释放。这种缺乏所有权的特性正是信号量最擅长的用例所重要的：一个发布者发布，一个消费者等待，它们可能是不同的线程。

### 数据结构

数据结构，来自 `/usr/src/sys/sys/sema.h`：

```c
struct sema {
        struct mtx      sema_mtx;       /* 通用保护锁。 */
        struct cv       sema_cv;        /* 等待者。 */
        int             sema_waiters;   /* 等待者数量。 */
        int             sema_value;     /* 信号量值。 */
};
```

四个字段。`sema_mtx` 是信号量自己的内部互斥锁。`sema_cv` 是等待者阻塞的条件变量。`sema_waiters` 计算当前阻塞的等待者数量（用于诊断目的和避免不必要的广播）。`sema_value` 是计数器本身。

你永远不会直接接触这些字段。API 是契约；结构在这里显示一次以便你可以可视化原语是什么。

### API

来自 `/usr/src/sys/sys/sema.h`：

```c
void sema_init(struct sema *sema, int value, const char *description);
void sema_destroy(struct sema *sema);
void sema_post(struct sema *sema);
void sema_wait(struct sema *sema);
int  sema_timedwait(struct sema *sema, int timo);
int  sema_trywait(struct sema *sema);
int  sema_value(struct sema *sema);
```

**`sema_init`**：用给定的初始值和人类可读描述初始化信号量。描述由内核跟踪设施使用。值必须是非负的；`sema_init` 用 `KASSERT` 断言这一点。

**`sema_destroy`**：拆除信号量。调用 `sema_destroy` 时必须确保没有等待者存在；实现断言这一点。通常你通过设计保证：销毁发生在分离中，在每个可能 `sema_wait` 的路径被静默之后。

**`sema_post`**：递增计数器。如果有等待者，唤醒其中一人。总是成功。

**`sema_wait`**：如果计数器为正，递减并返回。否则在内部条件变量上睡眠直到 `sema_post` 递增计数器，然后递减并返回。**`sema_wait` 不是信号可中断的**；它在底层使用 `cv_wait`，不是 `cv_wait_sig`。信号不会唤醒等待者。如果需要可中断性，`sema_wait` 是错误的工具；直接使用互斥锁加条件变量模式。

**`sema_timedwait`**：与 `sema_wait` 相同但受 `timo` ticks 限制。成功返回 0（值被递减），超时返回 `EWOULDBLOCK`。内部使用 `cv_timedwait`，所以也不是信号可中断的。

**`sema_trywait`**：非阻塞变体。如果值成功递减返回 1，如果值已经为零返回 0。注意不寻常的约定：1 表示成功，0 表示失败。大多数 FreeBSD 内核 API 成功返回 0；`sema_trywait` 是一个例外。使用它读写代码时要小心。

**`sema_value`**：返回当前计数器值。对诊断有用；对做出同步决定无用，因为值在调用返回后立即可能改变。

### 信号量不是什么

FreeBSD 内核信号量没有三个属性。每一个都很重要。

**无优先级继承。** `/usr/src/sys/kern/kern_sema.c` 顶部的注释很明确：

> Priority propagation will not generally raise the priority of semaphore "owners" (a misnomer in the context of semaphores), so should not be relied upon in combination with semaphores.

如果你正在保护一个资源，一个高优先级线程正在等待由低优先级线程持有的信号量，低优先级线程不会继承高优先级。这是无所有权设计的后果：没有"持有者"可以提升。对于优先级继承很重要的资源，改用互斥锁或 `lockmgr(9)` 锁。

**不可信号中断。** `sema_wait` 和 `sema_timedwait` 不被信号中断。在 `sema_wait` 中阻塞的 `read(2)` 或 `write(2)` 在用户发送 SIGINT 时不会返回 EINTR 或 ERESTART。如果你的系统调用需要响应信号，你不能无条件地在 `sema_wait` 中阻塞。两个常用的变通方法：将等待结构化为 `sema_trywait` 加在单独条件变量上的可中断睡眠，或保持 `sema_wait` 但安排生产者（`sema_post` 的代码）在关闭进行时也发布。

**无所有权。** 任何线程都可以发布；任何线程都可以等待。对于生产者-消费者形状，这是一个特性，而不是错误，其中一个线程发出完成信号，另一个线程等待它。如果你期待互斥锁式的所有权语义，这会是一个意外。

知道原语不是什么与知道它是什么同样重要。FreeBSD 内核信号量是一个小型、专注的工具。在它适合的地方使用它；在它不适合的地方选择不同的原语。

### 一个真实示例：Hyper-V storvsc

在驱动程序重构之前，简短看一下大量使用 `sema(9)` 的真实 FreeBSD 驱动程序。Hyper-V 存储驱动程序位于 `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c`。它使用每请求信号量来阻塞线程等待硬件完成。模式：

```c
/* 在请求提交路径中: */
sema_init(&request->synch_sema, 0, "stor_synch_sema");
/* ... 向虚拟机监控程序发送命令 ... */
sema_wait(&request->synch_sema);
/* 此时完成处理程序已发布；工作完成。 */
sema_destroy(&request->synch_sema);
```

在完成回调中（从不同上下文运行）：

```c
sema_post(&request->synch_sema);
```

信号量初始化为零，所以 `sema_wait` 阻塞。当硬件完成并且驱动程序的完成处理程序运行时，它发布，提交线程解除阻塞。信号量的无所有权特性正是使此模式工作的原因：不同的线程（完成处理程序）执行发布，而不是执行等待的线程。

同一驱动程序使用第二个信号量（`hs_drain_sema`）在关闭期间进行排空协调。关闭路径在信号量上等待；请求完成路径在所有未完成请求完成后发布。

这些模式不是发明。它们是 FreeBSD 树中 `sema(9)` 的规范用法。第15章重构使用"最多 N 个写入者"不变量的变体。基本思想是相同的。

### 写入者上限重构：第1阶段

第15章对驱动程序的第一个更改添加一个计数信号量，限制并发 `myfirst_write` 调用者的数量。上限可通过 sysctl 配置，默认为 4。

此更改不是关于性能。驱动程序已经可以处理许多并发写入者；cbuf 受互斥锁保护，写入在那里序列化。此更改是关于将"最多 N 个写入者"不变量表达为一级原语。真正的驱动程序可能因为更实质性的原因使用此模式（固定大小的 DMA 描述符池、有界深度的硬件命令队列、具有发送窗口的串行设备）；重构是在你可以运行和观察的上下文中学习原语的教学载体。

### Softc 添加

向 `struct myfirst_softc` 添加三个成员：

```c
struct sema     writers_sema;
int             writers_limit;              /* 当前配置的限制。 */
int             writers_trywait_failures;   /* 诊断计数器。 */
```

`writers_sema` 是信号量本身。`writers_limit` 记录当前配置的值，以便 sysctl 处理程序可以检测更改。`writers_trywait_failures` 计算写入者尝试进入但无法进入并返回 EAGAIN（对于 `O_NONBLOCK` 打开）或 EWOULDBLOCK（对于有界等待）的次数。

### 初始化和销毁信号量

在 `myfirst_attach` 中，在任何可能调用 `sema_wait` 的代码之前（所以通常在 attach 早期的其他 `sema_init`/`cv_init` 调用旁边）：

```c
sema_init(&sc->writers_sema, 4, "myfirst writers");
sc->writers_limit = 4;
sc->writers_trywait_failures = 0;
```

初始值 4 匹配默认限制。如果我们稍后动态提高限制，我们将调整信号量的值以匹配；第2节展示如何做。

在 `myfirst_detach` 中，在每个可能 `sema_wait` 的路径被静默之后（在第1阶段，这意味着在 `is_attached` 被清除且所有用户系统调用已返回或以 ENXIO 失败之后）：

```c
sema_destroy(&sc->writers_sema);
```

这里有一个微妙的点，值得慢下来理解。`sema_destroy` 断言没有等待者存在；更重要的是，它然后调用 `mtx_destroy` 销毁信号量的内部互斥锁，调用 `cv_destroy` 销毁其内部条件变量。如果任何线程仍在任何 `sema_*` 函数内执行，该线程可能即将重新获取内部互斥锁，而 `mtx_destroy` 竞争并在其之前释放它。这是释放后使用，不仅仅是断言失败。

天真的治疗方法"只需发布 `writers_limit` 个槽位来唤醒阻塞的等待者，然后销毁"*几乎*正确但有一个真正的竞争。被唤醒的线程带着内部 `sema_mtx` 持有从 `cv_wait` 返回，然后需要执行 `sema_waiters--` 和最终的 `mtx_unlock`。如果分离线程在被唤醒的线程到达其最终解锁之前运行 `sema_destroy`，内部互斥锁在其下面被销毁。

在实践中，该窗口很短（被唤醒的线程通常在 `cv_signal` 后几微秒内运行），但正确性意味着我们不能依赖"通常有效"。治疗方法是一个小扩展：跟踪每个可能当前在 `sema_*` 代码内的线程，并在调用 `sema_destroy` 之前等待该计数达到零。

我们添加 `sc->writers_inflight`，一个驱动程序视为原子的 int。写入路径在调用 `sema_wait` 之前递增它，在调用匹配的 `sema_post` 之后递减它。分离路径在发布唤醒槽位后等待计数器达到零：

```c
/* 在写入路径早期: */
atomic_add_int(&sc->writers_inflight, 1);
if (!sc->is_attached) {
        atomic_subtract_int(&sc->writers_inflight, 1);
        return (ENXIO);
}
... sema_wait / work / sema_post ...
atomic_subtract_int(&sc->writers_inflight, 1);

/* 在分离中，发布后: */
while (atomic_load_acq_int(&sc->writers_inflight) > 0)
        pause("myfwrd", 1);
sema_destroy(&sc->writers_sema);
```

这为什么有效：任何可能使用信号量内部状态的线程都已被计数。分离等待直到每个计数的线程完成其最终的 `sema_post`，到递减触发时，它已经从每个 `sema_*` 函数返回。当 `sema_destroy` 运行时，没有线程仍持有或即将获取内部互斥锁。

这个模式值得记住，因为它是通用的：任何销毁与正在进行的调用者竞争的外部原语都可以用相同方式排空。`sema(9)` 是直接的例子；每当需要干净拆除没有内置排空的原语时，你会在真正的驱动程序中看到此计数器的变体。

本章的第1阶段到第4阶段驱动程序都实现了此模式。第6节将逻辑封装在 `myfirst_sync_writer_enter`/`myfirst_sync_writer_leave` 中，以便调用点自然读取；正在进行的计数隐藏在包装器中。

### 在写入路径中使用信号量

在 `myfirst_write` 主体周围添加 `sema_wait`/`sema_post`：

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t want, put, room;
        ssize_t nbefore;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        /* 第15章：强制执行写入者上限。 */
        if (ioflag & IO_NDELAY) {
                if (!sema_trywait(&sc->writers_sema)) {
                        MYFIRST_LOCK(sc);
                        sc->writers_trywait_failures++;
                        MYFIRST_UNLOCK(sc);
                        return (EAGAIN);
                }
        } else {
                sema_wait(&sc->writers_sema);
        }
        if (!sc->is_attached) {
                sema_post(&sc->writers_sema);
                return (ENXIO);
        }

        nbefore = uio->uio_resid;
        while (uio->uio_resid > 0) {
                /* ... 与第14章相同的主体 ... */
        }

        sema_post(&sc->writers_sema);
        return (0);
}
```

几件值得注意的事。

`IO_NDELAY`（非阻塞）情况使用 `sema_trywait`，成功返回 1，失败返回 0。注意反转的约定：`if (!sema_trywait(...))` 意味着"如果我们未能获取"。初学者经常错过这一点；每次都要仔细阅读返回值。

在 `sema_trywait` 失败时，非阻塞调用者得到 EAGAIN。诊断计数器在互斥锁下递增（短暂的互斥锁获取/释放，与信号量无关）。

阻塞情况使用 `sema_wait`。它不是信号可中断的，所以阻塞在 `sema_wait` 中的 `write(2)` 不能被 SIGINT 中断。这是一个重要的属性；用户必须知道它。对于当前驱动程序，信号量在实践中很少被争用（默认限制 4 是宽裕的），所以可中断性关切很大程度上是理论上的。如果限制是 1 且写入者真正排队，你可能想要重新考虑在这里使用信号量，而是使用可中断原语。第4节返回这个权衡。

等待返回后，我们检查 `is_attached`。如果在我们阻塞时发生了分离，我们不得继续写入；我们发布信号量（恢复计数）并返回 ENXIO。

退出路径的 `sema_post` 在每个成功路径上运行。一个常见的错误是在早期返回时忘记它（例如，如果中间验证失败）。通常的纪律是通过清理模式使发布无条件：获取，然后所有后续退出通过一个共同的清理点。

### 限制的 Sysctl 处理程序

驱动程序的用户可能希望在运行时调整写入者上限。sysctl 处理程序：

```c
static int
myfirst_sysctl_writers_limit(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error, delta;

        old = sc->writers_limit;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (new < 1 || new > 64)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        delta = new - sc->writers_limit;
        sc->writers_limit = new;
        MYFIRST_UNLOCK(sc);

        if (delta > 0) {
                /* 提高了限制：发布额外的槽位。 */
                int i;
                for (i = 0; i < delta; i++)
                        sema_post(&sc->writers_sema);
        }
        /*
         * 降低是尽力而为：我们无法从已在写入路径中的线程
         * 回收已发布的槽位。新条目将在计数器排空到新上限
         * 以下时观察到较低的限制。
         */
        return (0);
}
```

有趣的细节。

提高限制需要向信号量发布额外的槽位。如果旧限制是 4，新限制是 6，我们需要发布两次，以便两个更多的写入者可以同时进入。

降低限制更难。信号量无法"消耗"多余的槽位。如果当前计数器是 4，我们想要限制 2，我们无法减少计数器，除非等待写入者进入并在其退出时不发布。这很复杂，很少值得编写代码。相反，简单的方法：降低 `writers_limit` 字段，让信号量在写入者进入但不替换时自然排空到新水平。sysctl 处理程序注释记录了此行为。

互斥锁只为 `writers_limit` 读/写而持有，不为 sema_post 循环持有。在 `sema_post` 周围获取互斥锁也是不正确的：`sema_post` 获取自己的内部互斥锁，我们将引入一个没有其他地方使用的锁顺序 `sc->mtx -> sc->writers_sema.sema_mtx`。由于 `writers_limit` 是我们实际保护的唯一字段，互斥锁窗口很小。

### 观察效果

加载第1阶段后，做一些实验。

使用小的 shell 循环启动许多并发写入者：

```text
# for i in 1 2 3 4 5 6 7 8; do
    (yes "writer-$i" | dd of=/dev/myfirst bs=512 count=100 2>/dev/null) &
done
```

八个写入者同时启动。在 `writers_limit=4`（默认值）下，四个进入写入循环，其他四个在 `sema_wait` 中阻塞。当一个完成并调用 `sema_post` 时，一个阻塞的唤醒。吞吐量略低于无限制（因为任何时刻只有四个写入者积极进行），但 cbuf 永远不会有超过四个写入者争用互斥锁。

实时观察信号量值：

```text
# sysctl dev.myfirst.0.stats.writers_sema_value
dev.myfirst.0.stats.writers_sema_value: 0
```

在压力测试期间，该值应接近零。当没有写入者存在时，它应等于 `writers_limit`。

动态调整限制：

```text
# sysctl dev.myfirst.0.writers_limit=2
```

重新运行八个写入者压力测试。两个写入者进行；六个阻塞。吞吐量相应下降。

将限制调回：

```text
# sysctl dev.myfirst.0.writers_limit=8
```

所有八个写入者并发进行。

通过使用非阻塞写入者（通过带 `O_NONBLOCK` 的 `open`）检查 trywait 失败计数器：

```text
# ./nonblock_writer_stress.sh
# sysctl dev.myfirst.0.stats.writers_trywait_failures
```

每当非阻塞写入者因信号量为零而被拒绝时，计数增长。

### 常见错误

初学者使用 `sema(9)` 时犯的错误简表。每一个都咬过真正的驱动程序；每一个都有简单的规则。

**在错误路径上忘记 `sema_post`。** 如果写入路径有一个绕过 `sema_post` 的 `return (error)`，信号量泄漏一个槽位。足够多的泄漏后，信号量永久为零，所有写入者阻塞。修复是将 `sema_post` 放在所有退出流经的单一清理块中，或审查每个返回语句以确认它发布。

**在不能睡眠的上下文中 `sema_wait`。** `sema_wait` 阻塞。它不能从 callout 回调、中断过滤器或任何其他非睡眠上下文调用。`WITNESS` 断言在调试内核上捕获这一点；生产内核可能静默死锁或恐慌。

**在等待者存在时销毁信号量。** `sema_destroy` 断言没有等待者存在。在驱动程序的分离中，仔细的做法是在销毁之前排空每个可能等待的路径。如果分离顺序错误（在等待者唤醒之前销毁），断言在调试内核上触发，销毁在生产内核上静默损坏。

**在需要信号可中断的地方使用 `sema_wait`。** 用户期望 `read(2)` 和 `write(2)` 响应 SIGINT。如果系统调用在 `sema_wait` 中阻塞，它不会。要么选择不同的原语，要么构建代码使 `sema_wait` 足够短，信号延迟可接受。

**混淆 `sema_trywait` 的返回值。** 成功返回 1，失败返回 0。大多数 FreeBSD 内核 API 成功返回 0。错误读取返回值产生与预期相反的行为。始终仔细检查这一个。

**假设优先级继承。** 如果不变量要求高优先级等待者提升将发布的线程的有效优先级，`sema(9)` 不会这样做。改用互斥锁或 `lockmgr(9)` 锁。

### 关于何时不使用信号量的说明

为了完整起见，简短列出 `sema(9)` 是错误工具的情况。

- **当不变量是"资源的独占所有权"时。** 那是互斥锁。初始化为 1 的信号量近似它，但失去所有权语义和优先级继承。
- **当等待者必须是信号可中断时。** 使用 `cv_wait_sig` 或 `cv_timedwait_sig` 加你自己的计数器。
- **当工作很短且争用很高时。** 信号量的内部互斥锁是单一序列化点。对于非常短的关键部分，开销可能占主导地位。
- **当需要优先级继承时。** 使用互斥锁或 `lockmgr(9)`。
- **当你需要不仅仅是计数时。** 如果不变量是"等待直到这个特定的复杂谓词成立"，互斥锁和测试谓词的条件变量是正确的工具。

对于驱动程序的写入者上限用例，这些取消资格都不适用。信号量是正确的工具，重构很小，结果代码可读。第15章驱动程序的第1阶段保留新词汇表并继续。

### 第2节总结

计数信号量是一个计数器、一个互斥锁和一个条件变量封装成小型 API。`sema_init`、`sema_wait`、`sema_post`、`sema_trywait`、`sema_timedwait`、`sema_value` 和 `sema_destroy` 覆盖整个表面。该原语理想用于有界准入和生产者-消费者完成形状，其中生产者和消费者是不同的线程。它缺乏优先级继承、信号可中断性和所有权，这些限制是真实的。第15章驱动程序的第1阶段应用了写入者上限信号量；下一节应用读多写少 sx 精炼。



## 第3节：读多写少场景和共享访问

第12章的 sx 锁已经在驱动程序中。`sc->cfg_sx` 保护 `myfirst_config` 结构，配置 sysctl 在读取时以共享模式获取它，在写入时以独占模式获取它。该模式是正确的，对于配置用例是足够的。本节精炼 sx 模式以覆盖稍微不同的形状：一个读多写少的缓存，其中读取者偶尔注意到缓存需要更新，必须短暂提升为写入者。

本节还介绍了驱动程序尚未使用的一些 sx 操作：`sx_try_upgrade`、`sx_downgrade`、`sx_xlocked` 和一些内省宏。第2阶段驱动程序重构添加一个由自己的 sx 保护的小型统计缓存，并使用提升模式在轻度争用下刷新缓存。

### 读多写少缓存问题

第2阶段重构的一个具体动机问题。假设驱动程序想要暴露一个计算统计信息，"过去 10 秒内每秒平均写入字节数"。该统计信息计算代价高（需要遍历每秒历史缓冲区）且经常被读取（每次 sysctl 读取、每次心跳日志行）。朴素的实现在每次读取时重新计算。更好的实现缓存结果并定期使缓存失效。

缓存有三个属性：

1. 读取远多于写入。任意数量的线程可以同时读取；只有偶尔的缓存刷新需要写入。
2. 读取者有时检测到缓存是过时的。当发生这种情况时，读取者希望短暂提升为写入者，刷新缓存，然后返回读取。
3. 刷新缓存需要几微秒。提升并刷新的读取者仍然希望快速释放独占锁。

sx 锁原生处理属性 1 和 3：许多读取者可以同时持有 `sx_slock`；持有 `sx_xlock` 的写入者排除读取者。属性 2 需要 `sx_try_upgrade`。

### `sx_try_upgrade` 和 `sx_downgrade`

第12章没有介绍的两个 sx 锁操作。

`sx_try_upgrade(&sx)` 尝试原子地将共享锁提升为独占锁。成功返回非零，失败返回零。失败意味着另一个线程也持有共享锁（独占与其他读取者不可表示；只有调用线程是唯一共享持有者时提升才能成功）。成功时，共享锁消失，调用者现在持有独占锁。

`sx_downgrade(&sx)` 原子地将独占锁降级为共享锁。总是成功。独占持有者变为共享持有者；其他共享锁定者可以加入。

读带偶尔提升的模式：

```c
sx_slock(&sx);
if (cache_stale(&cache)) {
        if (sx_try_upgrade(&sx)) {
                /* 提升为独占。 */
                refresh_cache(&cache);
                sx_downgrade(&sx);
        } else {
                /*
                 * 提升失败：另一个读取者持有锁。
                 * 释放共享锁，获取独占锁，
                 * 刷新，降级。
                 */
                sx_sunlock(&sx);
                sx_xlock(&sx);
                if (still_stale)
                        refresh_cache(&cache);
                sx_downgrade(&sx);
        }
}
use_cache(&cache);
sx_sunlock(&sx);
```

三件事值得注意。

快乐的路径是 `sx_try_upgrade` 成功。提升是原子的：锁从未被释放和重新获取，所以没有其他写入者可以插入其间。对于读取者很少相互争用的读多写少工作负载，此路径占主导地位。

当 `sx_try_upgrade` 失败时的回退路径完全放弃共享锁，从头获取独占锁，并重新检查过时谓词。重新检查是必要的：在放弃共享锁和获取独占锁之间，另一个线程可能已经刷新了缓存。没有重新检查，你会冗余刷新。

`sx_downgrade` 后的最终 `sx_sunlock` 总是正确的，因为降级状态是共享的。

此模式在 FreeBSD 源代码树中惊人地常见。在 `/usr/src/sys/` 下搜索 `sx_try_upgrade`，你会在几个子系统中找到它，包括 VFS 和路由表更新。

### 一个实际应用：Stage 2 驱动程序

第15章驱动程序的第2阶段添加一个由自己的 sx 锁保护的小型统计缓存。缓存保存单个整数，"截至上次刷新时，过去 10 秒内写入的字节数"，以及记录上次刷新缓存时间的时间戳。

softc 添加：

```c
struct sx       stats_cache_sx;
uint64_t        stats_cache_bytes_10s;
uint64_t        stats_cache_last_refresh_ticks;
```

缓存有效性基于时间戳。如果当前 `ticks` 与 `stats_cache_last_refresh_ticks` 的差值超过 `hz`（一秒钟的 tick 数），则认为缓存已过时。任何对缓存值的 sysctl 读取都会触发过时检查；如果过时，读取者提升并刷新。

### 缓存刷新函数

对于教学版本，刷新函数很简单：它只是读取当前计数器并记录当前时间。

```c
static void
myfirst_stats_cache_refresh(struct myfirst_softc *sc)
{
        KASSERT(sx_xlocked(&sc->stats_cache_sx),
            ("stats cache not exclusively locked"));
        sc->stats_cache_bytes_10s = counter_u64_fetch(sc->bytes_written);
        sc->stats_cache_last_refresh_ticks = ticks;
}
```

`KASSERT` 记录契约：此函数必须在独占持有 sx 锁的情况下调用。调试内核会在运行时捕获违规。

### Sysctl 处理程序

读取缓存值的 sysctl 处理程序：

```c
static int
myfirst_sysctl_stats_cached(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        uint64_t value;
        int stale;

        sx_slock(&sc->stats_cache_sx);
        stale = (ticks - sc->stats_cache_last_refresh_ticks) > hz;
        if (stale) {
                if (sx_try_upgrade(&sc->stats_cache_sx)) {
                        myfirst_stats_cache_refresh(sc);
                        sx_downgrade(&sc->stats_cache_sx);
                } else {
                        sx_sunlock(&sc->stats_cache_sx);
                        sx_xlock(&sc->stats_cache_sx);
                        if ((ticks - sc->stats_cache_last_refresh_ticks) > hz)
                                myfirst_stats_cache_refresh(sc);
                        sx_downgrade(&sc->stats_cache_sx);
                }
        }
        value = sc->stats_cache_bytes_10s;
        sx_sunlock(&sc->stats_cache_sx);

        return (sysctl_handle_64(oidp, &value, 0, req));
}
```

形状与上一小节的模式匹配。值得仔细阅读一次。在回退路径中过时检查发生两次：一次决定是否要获取独占锁，一次在获取后确认过时状态仍然适用。

### Attach 和 Detach

在 `myfirst_attach` 中初始化 sx，与现有的 `cfg_sx` 并列：

```c
sx_init(&sc->stats_cache_sx, "myfirst stats cache");
sc->stats_cache_bytes_10s = 0;
sc->stats_cache_last_refresh_ticks = 0;
```

在 `myfirst_detach` 中销毁，在 taskqueue 和互斥锁拆除之后（sx 在锁图中排在互斥锁之后；为了与初始化对称，在互斥锁之后销毁它）：

```c
sx_destroy(&sc->stats_cache_sx);
```

销毁时不应该有等待者。如果 detach 与 sysctl 竞争，读取者可能仍在进行中，但 detach 路径不访问缓存 sx，所以两者不会直接冲突。如果 detach 进行时有 sysctl 正在执行，sysctl 框架持有自己的引用，上下文将按顺序拆除。

### 观察效果

快速读取缓存统计信息一千次：

```text
# for i in $(jot 1000 1); do
    sysctl -n dev.myfirst.0.stats.bytes_written_10s >/dev/null
done
```

大多数读取命中缓存而无需提升。只有缓存过期后的第一次读取会刷新。结果：在读多写少负载下，统计缓存 sx 的争用接近零。

通过 DTrace 观察刷新率：

```text
# dtrace -n '
  fbt::myfirst_stats_cache_refresh:entry {
        @[execname] = count();
  }
' -c 'sleep 10'
```

应该显示大约每秒十次刷新（每次缓存过期一次），无论到达多少读取请求。

### sx 宏词汇表

本章尚未使用但值得了解的几个宏和辅助函数。

`sx_xlocked(&sx)` 如果当前线程独占持有 sx 则返回非零。在断言内部有用。不能告诉你其他线程是否持有它；没有等效的查询方法。

`sx_xholder(&sx)` 返回独占持有者的线程指针，如果没有线程独占持有则返回 NULL。在调试输出中有用。

`sx_assert(&sx, what)` 断言锁状态的属性。`SX_LOCKED`、`SX_SLOCKED`、`SX_XLOCKED`、`SX_UNLOCKED`、`SX_XLOCKED | SX_NOTRECURSED` 等都是有效的。当启用 `INVARIANTS` 时，不匹配会引发 panic。

对于第15章重构，我们在缓存刷新 KASSERT 中使用 `sx_xlocked`。其他宏在你需要时可用。

### 权衡与注意事项

一些值得指出的权衡。

**共享锁有开销。** 共享模式的 sx 仍然需要在内部自旋锁上自旋加上几个原子操作。对于极热路径（每秒数千万次操作），这可能是可测量的。带有 seq-cst 栅栏的 `atomic(9)` 有时更便宜。对于驱动程序的工作负载，sx 是可以的。

**提升失败是真实可能性。** 有许多并发读取者的工作负载会频繁看到 `sx_try_upgrade` 失败。回退路径（放弃共享，获取独占，重新检查）做正确的事但延迟稍高。对于升级罕见的真正读多写少工作负载，成功路径占主导。

**Sx 锁可以睡眠。** 与互斥锁不同，sx 的慢路径会阻塞。不要从不能睡眠的上下文（没有睡眠锁初始化的 callout、中断过滤器等）调用 `sx_slock`、`sx_xlock`、`sx_try_upgrade` 或 `sx_downgrade`。第13章解释旧的 `CALLOUT_MPSAFE` 标志已弃用；现代测试是 callout 是通过 `callout_init(, 0)` 还是 `callout_init_mtx(, &mtx, 0)` 设置的。

**锁顺序仍然重要。** 向驱动程序添加 sx 意味着向锁图添加新节点。每个持有多个锁的代码路径必须遵守一致的顺序。第15章驱动程序的最终锁顺序是 `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx`；`WITNESS` 会强制执行它。

### 第3节总结

sx 锁自然地覆盖多读取者或单写入者。`sx_try_upgrade` 和 `sx_downgrade` 将其扩展到读多写少带提升。带有快乐路径提升和回退重新检查的模式是表达"读取者注意到需要短暂写入"的规范方式。驱动程序的第2阶段用此模式添加了小型统计缓存；第3阶段将优化信号可中断等待。


## 第4节：带超时和中断的条件变量

`cv_wait_sig` 和 `cv_timedwait_sig` 原语已经在驱动程序中了。第12章介绍了它们；第13章为 tick-source 驱动程序优化了它们。本节迈出下一步：区分这些原语产生的返回值，展示如何正确处理 EINTR 和 ERESTART，并重构驱动程序的读取路径以在信号中断时保留部分进度。这是第15章驱动程序的 Stage 3。

与前几节不同，本节不引入新原语。它引入了一种使用你已经知道的原语的规范。

### 返回值的含义

`cv_wait_sig`、`cv_timedwait_sig`、带 `PCATCH` 标志的 `mtx_sleep` 以及类似的信号感知等待可以返回几个值：

- **0**：正常唤醒。调用者被匹配的 `cv_signal` 或 `cv_broadcast` 唤醒。重新检查谓词；如果为真，继续；如果为假，再次等待。
- **EINTR**：被安装了处理程序的信号中断。调用者应放弃等待，执行适当的清理，并向其自身的调用者返回 EINTR。
- **ERESTART**：被处理程序指定自动重启的信号中断。内核将重新调用系统调用。驱动程序应向系统调用层返回 ERESTART，由其安排重启。
- **EWOULDBLOCK**：仅来自定时等待。超时在任何唤醒或信号到达之前触发。

EINTR 和 ERESTART 之间的区别很重要，因为驱动程序通过系统调用路径将这些值返回，用户空间以不同方式处理它们：

- 如果系统调用返回 EINTR，用户空间的 `read(2)` 或 `write(2)` 返回 -1，errno 设置为 EINTR。未安装 SA_RESTART 信号处理程序的用户代码会显式看到这个结果。
- 如果系统调用返回 ERESTART，系统调用机制透明地重启系统调用。用户空间在这个级别永远看不到信号传递；信号处理程序运行了，但 read 调用继续进行。

实际后果：如果你的 `cv_wait_sig` 返回 EINTR，用户将在他们的 `read(2)` 中看到 EINTR，他们可能期望的任何部分进度必须是显式的（按照约定，读取返回信号之前复制的字节数，而不是错误）。如果它返回 ERESTART，重启发生，读取从内核认为合适的地方继续。

### 部分进度约定

`read(2)` 和 `write(2)` 的 UNIX 约定：如果信号在传输了一些数据后到达，系统调用返回传输的字节数，而不是错误。如果没有传输数据，系统调用返回 EINTR（或重启，取决于信号处置）。

转换为驱动程序：在读取路径的入口处，记录初始的 `uio_resid`。当阻塞等待返回信号错误时，将当前的 `uio_resid` 与记录的比较。如果取得了进度，返回 0（系统调用层将其转换为"返回复制的字节数"）。如果没有取得进度，返回信号错误。

第12章驱动程序已经通过 `nbefore` 局部变量和"返回 -1 给调用者以指示部分进度"的技巧为 `myfirst_read` 实现了这个约定。第15章优化了处理，使其显式，并将其扩展到写入路径。

### 重构的读取路径

第14章 Stage 4 读取路径具有这种形状：

```c
while (uio->uio_resid > 0) {
        MYFIRST_LOCK(sc);
        error = myfirst_wait_data(sc, ioflag, nbefore, uio);
        if (error != 0) {
                MYFIRST_UNLOCK(sc);
                return (error == -1 ? 0 : error);
        }
        ...
}
```

而 `myfirst_wait_data` 返回 -1 以发出"部分进度；向用户返回 0"的信号。这个约定正确但晦涩。Stage 3 重构用命名哨兵替换 -1 魔术值，并在注释中记录约定：

```c
#define MYFIRST_WAIT_PARTIAL    (-1)    /* partial progress already made */

static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error, timo;

        MYFIRST_ASSERT(sc);
        while (cbuf_used(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore) {
                        /*
                         * Some bytes already delivered on earlier loop
                         * iterations. Do not block further; return
                         * "partial progress" so the caller returns 0
                         * to the syscall layer, which surfaces the
                         * partial byte count.
                         */
                        return (MYFIRST_WAIT_PARTIAL);
                }
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);

                timo = sc->read_timeout_ms;
                if (timo > 0) {
                        int ticks_total = (timo * hz + 999) / 1000;
                        error = cv_timedwait_sig(&sc->data_cv, &sc->mtx,
                            ticks_total);
                } else {
                        error = cv_wait_sig(&sc->data_cv, &sc->mtx);
                }
                switch (error) {
                case 0:
                        break;
                case EWOULDBLOCK:
                        return (EAGAIN);
                case EINTR:
                case ERESTART:
                        if (uio->uio_resid != nbefore)
                                return (MYFIRST_WAIT_PARTIAL);
                        return (error);
                default:
                        return (error);
                }
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}
```

与第14章版本相比有几处变化。

魔术 -1 现在是 `MYFIRST_WAIT_PARTIAL`，并有注释解释其含义。

cv 等待后的错误处理对每个返回值的含义是显式的。EWOULDBLOCK 变为 EAGAIN（这是"稍后重试"的传统用户可见错误）。EINTR 和 ERESTART 被检查部分进度：如果传递了任何字节，我们返回部分哨兵；如果没有，我们传播信号错误。

`default` 情况处理 cv 等待可能返回的任何其他错误。目前内核的 `cv_timedwait_sig` 只返回上面列出的值，但显式处理意外情况是一个值得保持的习惯。

### 调用者的处理

在 `myfirst_read` 中，哨兵的处理变得稍微清晰：

```c
while (uio->uio_resid > 0) {
        MYFIRST_LOCK(sc);
        error = myfirst_wait_data(sc, ioflag, nbefore, uio);
        if (error != 0) {
                MYFIRST_UNLOCK(sc);
                if (error == MYFIRST_WAIT_PARTIAL)
                        return (0);
                return (error);
        }
        ...
}
```

读者可以一目了然地看到"部分进度"意味着什么。以后添加新早期退出原因的维护者知道要检查它应该传播给用户还是作为部分进度被抑制。

### 写入路径获得相同处理

第14章写入路径已经在 `myfirst_wait_room` 中实现了部分进度处理。Stage 3 在那里应用相同的重构：用 `MYFIRST_WAIT_PARTIAL` 替换 -1，使错误处理 switch 显式，并记录约定。

写入路径的一个小额外变化。写入路径第2节的 sema_wait 不是信号可中断的。在添加信号量之前，阻塞的写入可以通过 `myfirst_wait_room` 内部的 `cv_wait_sig` 被中断。添加信号量后，在 `sema_wait` 上阻塞的写入（等待写入者槽位）不可中断。

这可以接受吗？对于大多数工作负载可以，因为写入者上限通常不受争用。对于上限为 1 且写入者真正长时间排队的工作负载，用户会期望 SIGINT 能工作。这是一个显式的权衡；第5节将展示如何通过在 `sema_trywait` 周围分层信号感知等待来使等待可中断。

对于 Stage 3，我们接受默认情况下的不可中断 `sema_wait`，并在注释中注明这个权衡：

```c
/*
 * The writer-cap semaphore wait is not signal-interruptible. For a
 * workload where the cap is rarely contended this is acceptable. If
 * you set writers_limit=1 and create a real queue of writers, consider
 * the interruptible alternative in Section 5.
 */
sema_wait(&sc->writers_sema);
```

### 可中断等待模式

对于现在就想要完全可中断版本的读者：将 `sema_trywait` 与使用 cv 进行可中断睡眠的重试循环结合。代码适度冗长，这就是为什么第15章将其推迟到可选小节。

```c
static int
myfirst_writer_enter_interruptible(struct myfirst_softc *sc)
{
        int error;

        MYFIRST_LOCK(sc);
        while (!sema_trywait(&sc->writers_sema)) {
                if (!sc->is_attached) {
                        MYFIRST_UNLOCK(sc);
                        return (ENXIO);
                }
                error = cv_wait_sig(&sc->writers_wakeup_cv, &sc->mtx);
                if (error != 0) {
                        MYFIRST_UNLOCK(sc);
                        return (error);
                }
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

这需要第二个 cv（`writers_wakeup_cv`），退出路径在每次 `sema_post` 后发出信号：

```c
sema_post(&sc->writers_sema);
/* Wake one interruptible waiter so they can retry sema_trywait. */
cv_signal(&sc->writers_wakeup_cv);
```

可中断版本正确保留 EINTR/ERESTART 处理。它比普通的 `sema_wait` 版本更长，对于大多数驱动程序，权衡不值得额外代码。但当需要时，模式是存在的。

### 常见错误

**将 EWOULDBLOCK 视为正常唤醒。** 定时等待在计时器触发时返回 EWOULDBLOCK。将其视为 0 并重新测试谓词是错误的：谓词可能仍然为假，循环无限旋转。

**将 EINTR 视为可恢复的唤醒。** EINTR 意味着调用者应放弃等待。一个没有 EINTR 处理的 `while (... != 0) cv_wait_sig(...)` 循环永远不会将信号传播回用户空间。

**忘记部分进度检查。** 一个复制了一半字节并被中断的读取应该返回那一半；一个简单的实现返回 EINTR 且复制了零字节，丢失了部分数据。

**混淆 `cv_wait` 与信号可中断的调用者。** `cv_wait`（不带 `_sig`）即使在信号传递期间也会阻塞。使用 `cv_wait` 的系统调用不能被中断；用户的 `SIGINT` 在谓词被满足之前什么也不做。在系统调用上下文中始终使用 `cv_wait_sig`。

**忘记在唤醒后重新检查谓词。** 信号和 cv_signal 都会唤醒等待者。唤醒时谓词可能不为真（API 允许虚假唤醒）。总是在循环中检查谓词。

### 第4节总结

信号可中断等待有四个不同的返回值：0（正常）、EINTR（无重启的信号）、ERESTART（带重启的信号）、EWOULDBLOCK（超时）。每个都有特定含义，驱动程序必须显式处理每个值。部分进度约定（返回迄今为止复制的字节，而不是错误）是读取和写入的 UNIX 标准。驱动程序的 Stage 3 应用了这个规范，并使部分进度哨兵显式。第5节进一步推进协调故事。



## 第5节：模块或子系统之间的同步

到目前为止，驱动程序中的每个原语都局限于单个函数或文件节。一个互斥锁保护缓冲区；一个 cv 发信号给读取者；一个信号量限制写入者；一个 sx 缓存统计信息。每个原语在一个地方解决一个问题。

真正的驱动程序有跨越子系统的协调。一个 callout 触发，需要一个任务完成其工作，需要一个用户线程看到结果状态，并需要另一个子系统注意到关闭正在进行中。你已经知道的原语就足够了；困难在于组合它们，使跨组件握手显式且可维护。

本节教授组合。它介绍了一个用于跨上下文关闭可见性的小型原子标志规范、callout 和任务之间的状态标志握手，以及第6节将形式化的包装层的开端。第15章驱动程序的 Stage 4 从这里开始。

### 关闭标志问题

驱动程序 detach 中的反复出现的问题：几个上下文需要知道关闭正在进行。第14章驱动程序使用 `sc->is_attached` 作为此标志，大多数情况下在互斥锁下读取，偶尔未受保护地读取（带有注释"在处理程序入口处的读取可以不受保护"）。这行得通，但有两个微妙的问题。

首先，未受保护的读取在纯 C 中技术上是未定义行为。并发写入者和未同步的读取者是数据竞争；编译器可以以假设没有并发访问的方式转换代码。当前内核编译器很少这样做，但代码不是严格可移植的，未来的编译器可能会破坏它。

其次，互斥锁保护的读取即使你只想快速"窥视是否已关闭"也会跨锁串行化。在热路径中，这个成本是可测量的。

现代规范：对读取使用 `atomic_load_int`，对写入使用 `atomic_store_int`（或 `atomic_store_rel_int`）。这些操作由 C 内存模型定义为良好有序且无竞争的。它们也非常便宜：在 x86 上是带有正确屏障的普通加载或存储；在其他架构上是单个原子指令。

### 一页纸的原子 API

`/usr/src/sys/sys/atomic_common.h` 和特定于架构的头文件定义了原子操作。你最常使用的：

- `atomic_load_int(p)`：原子地读取 `*p`。无内存屏障。
- `atomic_load_acq_int(p)`：以获取语义原子地读取 `*p`。后续内存访问不能在加载之前重排序。
- `atomic_store_int(p, v)`：原子地将 `v` 写入 `*p`。无内存屏障。
- `atomic_store_rel_int(p, v)`：以释放语义原子地将 `v` 写入 `*p`。之前的内存访问不能在存储之后重排序。
- `atomic_fetchadd_int(p, v)`：返回旧的 `*p` 并原子地设置 `*p = *p + v`。
- `atomic_cmpset_int(p, old, new)`：如果 `*p == old`，设置 `*p = new` 并返回 1；否则返回 0。

对于关闭标志，模式是：

- 写入者（detach）：`atomic_store_rel_int(&sc->is_attached, 0)`。释放确保任何先前的状态更改（排空、cv 广播）在标志变为 0 之前可见。
- 读取者（任何上下文）：`if (atomic_load_acq_int(&sc->is_attached) == 0) { ... }`。获取确保任何后续检查看到写入者意图的状态。

本章在读取侧关闭检查中使用 `atomic_load_acq_int`，在 detach 路径中使用 `atomic_store_rel_int`。这使跨每个上下文的关闭可见性正确，而不会在热路径中引入互斥锁成本。

### 为什么不只是互斥锁保护的标志？

一个合理的问题。答案是"因为对于这个特定不变量，原子模式更便宜且同样正确"。标志正好有两个状态（1 和 0），转换是单向的（从 1 到 0，然后在此生命周期中永不回到 1），没有读取者需要与其他状态更改的原子性；每个读取者只想要"它仍然附加吗？"。

对于具有多个字段或双向转换的不变量，互斥锁是正确的工具。对于单调的一位标志，原子胜出。

### 应用原子标志

Stage 4 重构将 `sc->is_attached` 读取转换为原子加载，这些读取当前在互斥锁之外发生。需要更改的地方：

- `myfirst_open`：入口检查 `if (sc == NULL || !sc->is_attached)`。
- `myfirst_read`：`devfs_get_cdevpriv` 后的入口检查。
- `myfirst_write`：`devfs_get_cdevpriv` 后的入口检查。
- `myfirst_poll`：入口检查。
- 每个 callout 回调：`if (!sc->is_attached) return;`。
- 每个任务回调的等效检查（如果有）。
- `myfirst_tick_source` 在获取互斥锁后（这个在互斥锁下；它可以是原子加载但不必要）。

`myfirst_wait_data`、`myfirst_wait_room` 内部以及 cv 唤醒后的阻塞重新检查中的互斥锁持有检查保持原样：它们已经被互斥锁串行化。

detach 写入变为：

```c
MYFIRST_LOCK(sc);
if (sc->active_fhs > 0) {
        MYFIRST_UNLOCK(sc);
        return (EBUSY);
}
atomic_store_rel_int(&sc->is_attached, 0);
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
MYFIRST_UNLOCK(sc);
```

存储释放与其他上下文中的原子加载获取配对。存储之前发生的任何状态更改（例如，任何先前的关闭准备）对后来执行加载获取的任何线程可见。

处理程序入口检查变为：

```c
if (sc == NULL || atomic_load_acq_int(&sc->is_attached) == 0)
        return (ENXIO);
```

对于 callout 回调，检查在互斥锁下；我们将其保留在互斥锁下，以与回调的其余串行化保持一致。一些驱动程序甚至将 callout 检查转换为原子读取以提高性能；第15章驱动程序不这样做，因为互斥锁成本在 callout 触发率下可以忽略不计。

### Callout 到任务的握手

一个不同的跨组件协调问题。假设 watchdog callout 检测到停滞并希望触发任务中的恢复操作。callout 本身不能进行恢复（它可能睡眠、调用用户空间等）。当前驱动程序通过从 callout 入队任务来解决这个问题。它没有解决的是"如果上一个恢复仍在进行中，不要入队任务"。

一个小状态标志解决它。添加到 softc：

```c
int recovery_in_progress;   /* 0 or 1; protected by sc->mtx */
```

Callout：

```c
static void
myfirst_watchdog(void *arg)
{
        struct myfirst_softc *sc = arg;
        /* ... existing watchdog logic ... */

        if (stall_detected && !sc->recovery_in_progress) {
                sc->recovery_in_progress = 1;
                taskqueue_enqueue(sc->tq, &sc->recovery_task);
        }

        /* ... re-arm as before ... */
}
```

任务：

```c
static void
myfirst_recovery_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        /* ... recovery work ... */

        MYFIRST_LOCK(sc);
        sc->recovery_in_progress = 0;
        MYFIRST_UNLOCK(sc);
}
```

标志由互斥锁保护（两次写入都在互斥锁下发生；callout 中的读取在互斥锁下发生，因为 callout 通过 `callout_init_mtx` 持有互斥锁）。不变量"一次最多一个恢复任务"被保留。在恢复期间触发的 watchdog 看到标志设置并不入队。

这是一个最小示例，但模式可推广。每当驱动程序需要协调"只在 Y 尚未发生时做 X"，由适当锁保护的状态标志是正确的工具。

### Stage 4 Softc

综合起来。Stage 4 添加这些字段：

```c
/* Semaphore and its diagnostic fields (from Stage 1). */
struct sema     writers_sema;
int             writers_limit;
int             writers_trywait_failures;

/* Stats cache (from Stage 2). */
struct sx       stats_cache_sx;
uint64_t        stats_cache_bytes_10s;
uint64_t        stats_cache_last_refresh_ticks;

/* Recovery coordination (new in Stage 4). */
int             recovery_in_progress;
struct task     recovery_task;
int             recovery_task_runs;
```

所有三个字段形成一个连贯的子系统协调基础。第6节封装词汇。第8节发布最终版本。

### 一个小节的内存排序

内存排序可能感觉抽象；具体总结有帮助。

在强有序架构（x86、amd64）上，对齐的 int 大小值的普通加载和存储相对于其他对齐的 int 大小值是原子的。普通的 `int flag = 0` 写入对所有其他 CPU 迅速可见。你很少需要屏障。

在弱有序架构（arm64、riscv、powerpc）上，编译器和 CPU 可以自由重排序加载和存储，只要顺序对单个线程看起来正确。一个 CPU 的普通写入可能延迟对另一个 CPU 可见，另一个 CPU 上的读取可能相对彼此重排序。

`atomic(9)` API 掩盖了差异。`atomic_store_rel_int` 和 `atomic_load_acq_int` 在每个架构上产生正确的屏障。你不需要知道哪个架构是弱或强的；你使用 API，正确的事情发生。

对于第15章驱动程序，在 detach 写入上使用 `atomic_store_rel_int`，在入口检查上使用 `atomic_load_acq_int`，给你一个在 x86 和 arm64 上都正确工作的驱动程序。如果驱动程序曾经在 arm64 系统上发布（FreeBSD 14.3 很好地支持 arm64），这个规范会有回报。

### 第5节总结

驱动程序中的跨组件协调使用与本地同步相同的原语，只是组合。原子 API 以正确的内存排序覆盖廉价的关闭标志。由适当锁保护的状态标志跨 callout、任务和用户线程协调"最多一个"不变量。第15章驱动程序的 Stage 4 添加了两种模式。第6节迈出下一步，将同步词汇封装在专用头文件中。



## 第6节：同步与模块化设计

驱动程序现在使用五种同步原语：一个互斥锁、两个条件变量、两个 sx 锁、一个计数信号量和原子操作。每个都出现在源代码中的多个位置。第一次阅读文件的维护者必须从分散的调用点重构同步策略。

本节将同步词汇封装在一个小头文件 `myfirst_sync.h` 中，命名驱动程序执行的每个操作。头文件不添加新原语；它给现有原语可读名称，并在一个地方记录其契约。第15章驱动程序的 Stage 4 引入头文件并更新主源代码以使用它。

在我们继续之前关于状态的说明。`myfirst_sync.h` 包装器是**一个建议，而不是 FreeBSD 约定**。`/usr/src/sys/dev` 下的大多数驱动程序直接调用 `mtx_lock`、`sx_xlock`、`cv_wait` 等；它们不提供私有同步头文件。如果你浏览源代码树，你不会找到每个驱动程序都提供这样一个层的社区期望。FreeBSD 社区*确实*期望的是一个清晰、记录的锁顺序和一个审查者可以遵循的 `LOCKING.md` 风格注释块，而我们在第3部分的每一章都满足了这个期望。包装头文件是一种在树内外多个中等规模驱动程序中运行良好的风格扩展；它对本书有价值，因为它将同步词汇变成你可以命名、审计和在一个地方更改的东西。如果你未来的驱动程序不需要额外的可读性，跳过头文件并将原语调用保留在源代码中是一个完全正常的选择。底层规范——锁顺序、detach 时排空、显式契约——才是重要的；包装器是保持该规范可见的一种方式，不是唯一方式。

### 为什么要封装

三个具体好处。

**可读性。** 一个读取 `myfirst_sync_writer_enter(sc)` 的代码路径确切告诉读者调用做什么。同样的代码路径写成 `if (ioflag & IO_NDELAY) { if (!sema_trywait(&sc->writers_sema)) ...` 是正确的，但告诉读者更少。

**可更改性。** 如果同步策略改变（例如，写入者上限信号量被第4节的基于 cv 的可中断等待替换），更改发生在头文件的一个地方。`myfirst_write` 中的调用点不改变。

**可验证性。** 头文件是记录同步契约的唯一地方。代码审查可以通过 grep 头文件来验证"每个进入是否有匹配的离开？"。没有头文件，审查必须遍历每个调用点。

封装的成本是最小的。一个 100 到 200 行的头文件。半小时的重构。现代编译器内联掉的一层轻微间接。

### `myfirst_sync.h` 的形状

头文件命名每个同步操作。它不定义新结构；结构保留在 softc 中。它提供包装原语的内联函数或宏。

一个草图：

```c
#ifndef MYFIRST_SYNC_H
#define MYFIRST_SYNC_H

#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/sema.h>
#include <sys/condvar.h>

struct myfirst_softc;       /* Forward declaration. */

/* Data-path mutex operations. */
static __inline void    myfirst_sync_lock(struct myfirst_softc *sc);
static __inline void    myfirst_sync_unlock(struct myfirst_softc *sc);
static __inline void    myfirst_sync_assert_locked(struct myfirst_softc *sc);

/* Configuration sx operations. */
static __inline void    myfirst_sync_cfg_read_begin(struct myfirst_softc *sc);
static __inline void    myfirst_sync_cfg_read_end(struct myfirst_softc *sc);
static __inline void    myfirst_sync_cfg_write_begin(struct myfirst_softc *sc);
static __inline void    myfirst_sync_cfg_write_end(struct myfirst_softc *sc);

/* Writer-cap semaphore operations. */
static __inline int     myfirst_sync_writer_enter(struct myfirst_softc *sc,
                            int ioflag);
static __inline void    myfirst_sync_writer_leave(struct myfirst_softc *sc);

/* Stats cache sx operations. */
static __inline void    myfirst_sync_stats_cache_read_begin(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_read_end(
                            struct myfirst_softc *sc);
static __inline int     myfirst_sync_stats_cache_try_promote(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_downgrade(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_write_begin(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_write_end(
                            struct myfirst_softc *sc);

/* Attach-flag atomic operations. */
static __inline int     myfirst_sync_is_attached(struct myfirst_softc *sc);
static __inline void    myfirst_sync_mark_detaching(struct myfirst_softc *sc);

#endif /* MYFIRST_SYNC_H */
```

每个函数正好包装一个原语调用加上调用点需要的任何约定。例如，`myfirst_sync_writer_enter` 接受 `ioflag` 参数，并在 `sema_trywait`（用于 `IO_NDELAY`）和 `sema_wait` 之间选择。调用者不需要知道 trywait 与 wait 的逻辑；头文件负责。

### 实现

每个函数都是一个简单的内联包装器。示例实现（针对最有趣的几个）：

```c
static __inline void
myfirst_sync_lock(struct myfirst_softc *sc)
{
        mtx_lock(&sc->mtx);
}

static __inline void
myfirst_sync_unlock(struct myfirst_softc *sc)
{
        mtx_unlock(&sc->mtx);
}

static __inline void
myfirst_sync_assert_locked(struct myfirst_softc *sc)
{
        mtx_assert(&sc->mtx, MA_OWNED);
}

static __inline int
myfirst_sync_writer_enter(struct myfirst_softc *sc, int ioflag)
{
        if (ioflag & IO_NDELAY) {
                if (!sema_trywait(&sc->writers_sema)) {
                        mtx_lock(&sc->mtx);
                        sc->writers_trywait_failures++;
                        mtx_unlock(&sc->mtx);
                        return (EAGAIN);
                }
        } else {
                sema_wait(&sc->writers_sema);
        }
        if (!myfirst_sync_is_attached(sc)) {
                sema_post(&sc->writers_sema);
                return (ENXIO);
        }
        return (0);
}

static __inline void
myfirst_sync_writer_leave(struct myfirst_softc *sc)
{
        sema_post(&sc->writers_sema);
}

static __inline int
myfirst_sync_is_attached(struct myfirst_softc *sc)
{
        return (atomic_load_acq_int(&sc->is_attached));
}

static __inline void
myfirst_sync_mark_detaching(struct myfirst_softc *sc)
{
        atomic_store_rel_int(&sc->is_attached, 0);
}
```

`writer_enter` 包装器是最复杂的；其他都是单行代码。这种形状的头文件产生零运行时开销（编译器内联每个调用）并增加大量可读性。

### 源代码如何改变

主源代码中的每个 `mtx_lock(&sc->mtx)` 变为 `myfirst_sync_lock(sc)`。每个 `sema_wait(&sc->writers_sema)` 变为 `myfirst_sync_writer_enter(sc, ioflag)` 或其变体。每个 `atomic_load_acq_int(&sc->is_attached)` 变为 `myfirst_sync_is_attached(sc)`。

主源代码读起来更清晰：

```c
/* Before: */
if (ioflag & IO_NDELAY) {
        if (!sema_trywait(&sc->writers_sema)) {
                MYFIRST_LOCK(sc);
                sc->writers_trywait_failures++;
                MYFIRST_UNLOCK(sc);
                return (EAGAIN);
        }
} else {
        sema_wait(&sc->writers_sema);
}
if (!sc->is_attached) {
        sema_post(&sc->writers_sema);
        return (ENXIO);
}

/* After: */
error = myfirst_sync_writer_enter(sc, ioflag);
if (error != 0)
        return (error);
```

五行意图变成一行。想知道 `myfirst_sync_writer_enter` 做什么的读者打开头文件阅读实现。接受接口的读者继续往下读。

### 命名约定

在同步包装层中挑选名称的简短规范。

**命名操作，而非原语。** `myfirst_sync_writer_enter` 描述调用者在做什么（进入写入者节）。`myfirst_sync_sema_wait` 会描述原语（调用 sema_wait），这不太有用。

**为有作用域的获取使用 enter/leave 对。** 每个 `enter` 都有匹配的 `leave`。这在视觉上使驱动程序是否总是释放其获取的东西变得明显。

**为共享/独占访问使用 read/write 对。** 共享用 `cfg_read_begin`/`cfg_read_end`；独占用 `cfg_write_begin`/`cfg_write_end`。begin/end 后缀反映调用点的结构。

**对返回布尔类值的谓词使用 `is_`。** `myfirst_sync_is_attached` 读起来像英语。

**对原子状态转换使用 `mark_`。** `myfirst_sync_mark_detaching` 描述转换。

### 不应放入头文件的内容

头文件应该包装同步原语，而不是业务逻辑。一个获取锁并且还做了"有趣"工作的函数应该留在主源代码中；只有纯粹的锁操作属于头文件。

头文件也不应该隐藏重要细节。例如，`myfirst_sync_writer_enter` 返回 `EAGAIN` 或 `ENXIO` 或 0；调用者必须检查。一个静默地在 `ENXIO` 上"返回"的头文件会隐藏重要的错误路径。包装器的契约必须显式。

### 相关规范：断言

头文件是放置记录不变量的断言的好地方。必须在互斥锁下调用的函数可以在入口处调用 `myfirst_sync_assert_locked(sc)`：

```c
static void
myfirst_some_helper(struct myfirst_softc *sc)
{
        myfirst_sync_assert_locked(sc);
        /* ... */
}
```

在调试内核（带 `INVARIANTS`）上，如果辅助函数在没有互斥锁的情况下被调用，断言就会触发。在生产内核上，断言被省略。

第14章代码使用 `MYFIRST_ASSERT`；第15章重构将其保留为 `myfirst_sync_assert_locked`，行为相同。

### 简短 WITNESS 演练：在不可睡眠锁下睡眠

第34章演练了两个互斥锁之间的锁顺序反转。一类单独的 WITNESS 警告同样常见且同样容易预防，值得在此简短提及，因为它正好落在第15章领域的正中间：互斥锁和 sx 锁之间的交互。

想象配置读取路径的第一次尝试重构。作者刚刚为配置 blob 添加了一个新的 `sx_slock` 用于读多访问，并且没有思考就从仍然持有数据路径互斥锁的代码路径中调用它：

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error;

        MYFIRST_LOCK(sc);                /* mtx_lock: non-sleepable */
        sx_slock(&sc->cfg_sx);           /* sx_slock: sleepable */
        error = myfirst_copy_out_locked(sc, uio);
        sx_sunlock(&sc->cfg_sx);
        MYFIRST_UNLOCK(sc);
        return (error);
}
```

代码编译通过，在非调试内核的轻度测试中似乎运行正确。在用 `options WITNESS` 构建的内核上加载并运行相同的实验，控制台报告类似这样的内容：

```text
lock order reversal: (sleepable after non-sleepable)
 1st 0xfffff800...  myfirst_sc_mtx (mutex) @ /usr/src/sys/modules/myfirst/myfirst.c:...
 2nd 0xfffff800...  myfirst_cfg_sx (sx) @ /usr/src/sys/modules/myfirst/myfirst.c:...
stack backtrace:
 #0 witness_checkorder+0x...
 #1 _sx_slock+0x...
 #2 myfirst_read+0x...
```

此报告中有两件事值得仔细阅读。括号中的"（sleepable after non-sleepable）"确切告诉你反转是什么：线程先获取了不可睡眠锁（互斥锁），然后请求可睡眠锁（sx 锁）。WITNESS 拒绝这个，因为 `sx_slock` 可以睡眠，而持有不可睡眠锁时睡眠是定义的内核错误类别：调度器无法在不迁移互斥锁等待者的情况下将线程移出 CPU，使 `MTX_DEF` 廉价的不变量不再成立。第二件事是 WITNESS 在路径第一次运行时就报告它，远在任何真正的争用之前。你不必重现竞争；警告在排序本身上触发。

修复是排序规范，而不是不同的原语。先获取 sx 锁，再获取互斥锁：

```c
sx_slock(&sc->cfg_sx);
MYFIRST_LOCK(sc);
error = myfirst_copy_out_locked(sc, uio);
MYFIRST_UNLOCK(sc);
sx_sunlock(&sc->cfg_sx);
```

或者，对大多数驱动程序更好的做法是，在 sx 锁下将配置读入本地快照，在接触数据路径互斥锁之前释放 sx 锁。`myfirst_sync.h` 中的封装在这里有帮助，因为锁顺序契约在一个地方被命名和记录；看到 `myfirst_sync_cfg_slock` 后跟 `myfirst_sync_lock` 的审查可以一目了然地确认排序。

此演练故意保持比第34章的更短。错误类别不同，教训特定于第15章围绕的可睡眠/不可睡眠区别。更广泛的锁顺序反转演练属于调试章节；这个属于读者首次在同一代码路径中组合 sx 锁和互斥锁的地方。

### 第6节总结

一个小的同步头文件命名驱动程序执行的每个操作并集中契约。主源代码读起来更清晰；头文件是维护者理解或更改策略的唯一地方。Stage 4 的 `myfirst_sync.h` 不添加新原语；它封装了第2到第5节中的原语。第7节编写验证整个组合的测试。



## 第7节：测试高级同步

本章引入的每个原语都有失败模式。一个缺少 `sema_post` 的信号量泄漏槽位。一个不重新检查谓词的 sx 升级冗余刷新。忽略 EINTR 的信号可中断等待会死锁调用者。没有正确锁或原子规范的状态标志读取会静默读取过时值。

本节是关于编写在用户发现之前暴露这些失败模式的测试。这些测试不是纯意义上的单元测试；它们是在并发负载下运行驱动程序并检查不变量的压力工具。第15章配套源代码包含三个测试程序；本节逐一介绍。

### 为什么压力测试重要

同步错误很少在单线程测试中出现。一个被遗忘的 `sema_post` 直到足够多的写入者通过信号量耗尽槽位之前是不可见的。一个错误的原子读取直到特定的交错发生之前是不可见的。一个 detach 竞争直到 detach 和卸载在真正并发下发生之前是不可见的。

压力测试通过在暴露交错的配置中运行驱动程序来发现这些错误。许多并发读取者和写入者。快速 tick source。频繁的 detach/重载循环。同时的 sysctl 写入。驱动程序工作得越努力，潜在错误就越可能暴露。

这些测试不替代 `WITNESS` 或 `INVARIANTS`。`WITNESS` 在任何负载下捕获锁顺序违规。`INVARIANTS` 捕获结构性违规。压力测试捕获静态和轻量级动态检查都无法检测到的逻辑错误。

### 测试 1：写入者上限正确性

写入者上限信号量的不变量是"一次最多 `writers_limit` 个写入者在 `myfirst_write` 中"。一个测试程序启动许多并发写入者，每个写入几个字节，在其写入开始时记录进程 ID 在一个小标记中，然后继续。一个监控进程在后台读取 cbuf 并计算并发标记。

测试在 `examples/part-03/ch15-more-synchronization/tests/writer_cap_test.c` 中：

```c
/*
 * writer_cap_test: start N writers and verify no more than
 * writers_limit are simultaneously inside the write path.
 */
#include <sys/param.h>
#include <sys/time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define N_WRITERS 16

int
main(int argc, char **argv)
{
        int fd, i;
        char buf[64];
        int writers = (argc > 1) ? atoi(argv[1]) : N_WRITERS;

        for (i = 0; i < writers; i++) {
                if (fork() == 0) {
                        fd = open("/dev/myfirst", O_WRONLY);
                        if (fd < 0)
                                err(1, "open");
                        snprintf(buf, sizeof(buf), "w%d\n", i);
                        for (int j = 0; j < 100; j++) {
                                write(fd, buf, strlen(buf));
                                usleep(1000);
                        }
                        close(fd);
                        _exit(0);
                }
        }
        while (wait(NULL) > 0)
                ;
        return (0);
}
```

测试启动 `N_WRITERS` 个进程，每个写入 100 条短消息，间隔 1 毫秒。一个读取者进程读取 `/dev/myfirst` 并观察交错。

一个简单的不变量检查：读取者一次读取 100 字节并记录在该窗口中出现多少个不同的写入者前缀。如果 `writers_limit` 是 4，读取者在任何 100 字节窗口中应该看到最多 4 个前缀（加减）。超过 4 个表示上限未被强制执行。

更严格的检查使用 `sysctl dev.myfirst.0.stats.writers_trywait_failures` 观察 `O_NONBLOCK` 模式下的失败率。如果你设置 `writers_limit=2` 并运行 16 个非阻塞写入者，大多数应该看到 EAGAIN；失败计数应该快速增长。

### 测试 2：统计缓存并发

统计缓存不变量是"许多读取者并发，每秒最多刷新一次"。一个测试：

- 启动 32 个并发读取者进程，每个在紧密循环中读取 `dev.myfirst.0.stats.bytes_written_10s`。
- 启动 1 个持续向设备写入的写入者。
- 通过 DTrace 观察缓存刷新率：

```text
# dtrace -n '
  fbt::myfirst_stats_cache_refresh:entry {
        @["refreshes"] = count();
  }
  tick-10s { printa(@); exit(0); }
'
```

预期：10 秒内大约 10 次刷新（每次过期一次）。不是 10 秒 32 次；缓存消除了每个读取者的重新计算。

如果刷新率在争用下激增，`sx_try_upgrade` 快速路径失败太频繁，回退（释放并重新获取）引入了竞争。驱动程序代码应该正确处理这个；如果没有，测试暴露错误。

### 测试 3：负载下 Detach

detach 不变量是"即使每个第14章和第15章原语都在负载下，detach 也能干净完成"。一个 detach 测试：

```text
# ./stress_all.sh &
# STRESS_PID=$!
# sleep 5
# kldunload myfirst
# kill -TERM $STRESS_PID
```

其中 `stress_all.sh` 运行：

- 几个并发写入者。
- 几个并发读取者。
- 以 1 毫秒启用的 tick source。
- 以 100 毫秒启用的心跳。
- 以 1 秒启用的 watchdog。
- 偶尔的 bulk_writer_flood sysctl 写入。
- 偶尔的 writers_limit sysctl 调整。
- 偶尔的统计缓存读取。

detach 应该完成。如果它挂起或崩溃，排序规范有错误。在第14章和第15章代码正确排序的情况下，测试应该可靠通过。

### 使用 DTrace 观察

DTrace 是同步调试的手术刀。一些有用的单行命令：

**按 cv 名称计数 cv 唤醒。**

```text
# dtrace -n 'fbt::cv_signal:entry { @[stringof(arg0)] = count(); }'
```

解释输出需要知道你的 cv 名称；驱动程序的是 `"myfirst data"` 和 `"myfirst room"`。

**计数信号量操作。**

```text
# dtrace -n '
  fbt::_sema_wait:entry { @[probefunc] = count(); }
  fbt::_sema_post:entry { @[probefunc] = count(); }
'
```

在平衡的驱动程序中，长时间运行后 post 计数等于 wait 计数（加上 `sema_init` 的初始值）。

**观察任务触发。**

```text
# dtrace -n 'fbt::taskqueue_run_locked:entry { @[execname] = count(); }'
```

显示哪些 taskqueue 线程在运行，对于确认私有 taskqueue 正在获取工作很有用。

**观察 callout 触发。**

```text
# dtrace -n 'fbt::callout_reset:entry { @[probefunc] = count(); }'
```

显示 callout 被重新武装的频率，应该匹配配置的间隔率。

在你的压力工作负载下运行每个单行命令。计数应该匹配你的预期。意外的不平衡是调试的起点。

### WITNESS、INVARIANTS 和调试内核

第一道防线仍然是调试内核。如果 `WITNESS` 对锁顺序不满意，在发布前修复顺序。如果 `INVARIANTS` 在 `cbuf_*` 或 `sema_*` 中断言，在发布前修复调用者。这些检查很便宜；在开发中不运行它们是虚假节约。

一些预期或应避免的 `WITNESS` 输出：

- **"acquiring duplicate lock of same type"**：你意外获取了已经持有的锁。检查调用路径。
- **"lock order reversal"**：两个锁在不同路径上以不同顺序获取。选择一个顺序，强制执行，更新 `LOCKING.md`。
- **"blockable sleep from an invalid context"**：你从不允许睡眠的上下文中调用了可以睡眠的东西。检查上下文是否是 callout 或中断。

`dmesg` 中来自你的驱动程序的每个 `WITNESS` 警告都是一个错误。将它们视为等效于 panic；修复每一个。

### 回归规范

在第15章每个阶段之后，运行：

1. 第11章 IO 冒烟测试（基本读取、写入、打开、关闭）。
2. 第12章同步测试（有界读取、sx 保护的配置）。
3. 第13章定时器测试（心跳、watchdog、各种速率的 tick source）。
4. 第14章 taskqueue 测试（poll 等待者、批量写入洪泛、延迟重置）。
5. 第15章测试（写入者上限、统计缓存、负载下 detach）。

整个套件应该在调试内核上通过。如果任何测试失败，回归是最近的；回滚到上一个阶段，找到差异，并调试。

### 第7节总结

高级同步需要高级测试。运行并发写入者、读取者和 sysctls 的压力测试暴露单线程测试遗漏的错误。DTrace 使同步原语的内部可观察。`WITNESS` 和 `INVARIANTS` 捕获剩余的问题。在调试内核上运行整个栈是驱动程序能获得的最接近"足够好"的测试。第8节结束第3部分。



## 第8节：重构和版本化你的协调驱动程序

Stage 4 是第15章的整合阶段。它不添加新功能；它重组和记录第15章的添加，更新 `LOCKING.md`，将版本提升到 `0.9-coordination`，并在第11到15章之间运行完整的回归套件。

本节逐步介绍整合过程，用第15章内容扩展 `LOCKING.md`，并结束第3部分。

### 文件组织

第15章重构引入了 `myfirst_sync.h`。文件列表变为：

- `myfirst.c`：主驱动程序源代码。
- `cbuf.c`、`cbuf.h`：与第13章相同。
- `myfirst_sync.h`：带同步包装器的新头文件。
- `Makefile`：除了将 `myfirst_sync.h` 添加到源代码依赖的头文件之外没有变化（如果需要 `make` 依赖跟踪）。

在 `myfirst.c` 内部，第15章的组织遵循与第14章相同的模式，有几处添加：

1. 包含（现在包含 `myfirst_sync.h`）。
2. Softc 结构（扩展了第15章字段）。
3. 文件句柄结构（未变）。
4. cdevsw 声明（未变）。
5. 缓冲区辅助函数（未变）。
6. 缓存辅助函数（新增；`myfirst_stats_cache_refresh`）。
7. 条件变量等待辅助函数（修订为显式 EINTR/ERESTART 处理）。
8. Sysctl 处理程序（扩展了 writers_limit、stats_cache、recovery）。
9. Callout 回调（修订为在适当位置使用 atomic_load_acq_int 检查 is_attached）。
10. 任务回调（扩展了 recovery_task）。
11. Cdev 处理程序（修订为使用 myfirst_sync_* 包装器）。
12. 设备方法（attach/detach 扩展了信号量、sx 和原子标志规范）。
13. 模块胶水代码（版本升级）。

每个节的关键变化是使用 `myfirst_sync.h` 包装器替代第14章代码中的直接原语调用。这在 attach、detach 和每个处理程序中都可见。

### `LOCKING.md` 更新

第14章 `LOCKING.md` 有互斥锁、cv、sx、callout 和任务的节。第15章添加了信号量、协调和更新的锁顺序节。

```markdown
## Semaphores

The driver owns one counting semaphore:

- `writers_sema`: caps concurrent writers at `sc->writers_limit`.
  Default limit: 4. Range: 1-64. Configurable via the
  `dev.myfirst.N.writers_limit` sysctl.

Semaphore operations happen outside `sc->mtx`. The internal `sema_mtx`
is not in the documented lock order because it does not conflict with
`sc->mtx`; the driver never holds `sc->mtx` across a `sema_wait` or
`sema_post`.

Lowering `writers_limit` below the current semaphore value is
best-effort: the handler lowers the target and lets new entries
observe the lower cap as the value drains. Raising posts additional
slots immediately.

### Sema Drain Discipline

The driver tracks `writers_inflight` as an atomic int. It is
incremented before any `sema_*` call (specifically at the top of
`myfirst_sync_writer_enter`) and decremented after the matching
`sema_post` (in `myfirst_sync_writer_leave` or on every error
return).

Detach waits for `writers_inflight` to reach zero before calling
`sema_destroy`. This closes the use-after-free race where a woken
waiter is between `cv_wait` return and its final
`mtx_unlock(&sema->sema_mtx)` when `sema_destroy` tears down the
internal mutex.

`sema_destroy` itself is called only after:

1. `is_attached` has been cleared atomically.
2. `writers_limit` wake-up slots have been posted to the sema.
3. `writers_inflight` has been observed to reach zero.
4. Every callout, task, and selinfo has been drained.

## Coordination

The driver uses three cross-component coordination mechanisms:

1. **Atomic is_attached flag.** Read via `atomic_load_acq_int`, written
   via `atomic_store_rel_int` in detach. Allows every context (callout,
   task, user thread) to check shutdown state without acquiring
   `sc->mtx`.
2. **recovery_in_progress state flag.** Protected by `sc->mtx`. Set by
   the watchdog callout, cleared by the recovery task. Ensures at most
   one recovery task is pending or running at a time.
3. **Stats cache sx.** Shared reads, occasional upgrade-promote-
   downgrade for refresh. See the Stats Cache section.

## Stats Cache

The `stats_cache_sx` protects a small cached statistic. The refresh
pattern is:

```c
sx_slock(&sc->stats_cache_sx);
if (stale) {
        if (sx_try_upgrade(&sc->stats_cache_sx)) {
                refresh();
                sx_downgrade(&sc->stats_cache_sx);
        } else {
                sx_sunlock(&sc->stats_cache_sx);
                sx_xlock(&sc->stats_cache_sx);
                if (still_stale)
                        refresh();
                sx_downgrade(&sc->stats_cache_sx);
        }
}
value = sc->stats_cache_bytes_10s;
sx_sunlock(&sc->stats_cache_sx);
```text

## Lock Order

The complete driver lock order is:

```text
sc->mtx  ->  sc->cfg_sx  ->  sc->stats_cache_sx
```text

`WITNESS` enforces this order. The writer-cap semaphore's internal
mutex is not in the graph because the driver never holds `sc->mtx`
(or any other driver lock) across a `sema_wait`/`sema_post` call.

## Detach Ordering (updated)

1. Refuse detach if `sc->active_fhs > 0`.
2. Clear `sc->is_attached` under `sc->mtx` via
   `atomic_store_rel_int`.
3. `cv_broadcast(&sc->data_cv)`; `cv_broadcast(&sc->room_cv)`.
4. Release `sc->mtx`.
5. Post `writers_limit` wake-up slots to `writers_sema`.
6. Wait for `writers_inflight == 0` (sema drain).
7. Drain the three callouts.
8. Drain every task including recovery_task.
9. `seldrain(&sc->rsel)`, `seldrain(&sc->wsel)`.
10. `taskqueue_free(sc->tq)`; `sc->tq = NULL`.
11. `sema_destroy(&sc->writers_sema)` (safe: drain completed).
12. `sx_destroy(&sc->stats_cache_sx)`.
13. Destroy cdev, free sysctl context, destroy cbuf, counters,
    cvs, cfg_sx, mtx.
```

### 版本升级

版本字符串从 `0.8-taskqueues` 提升到 `0.9-coordination`：

```c
#define MYFIRST_VERSION "0.9-coordination"
```

以及驱动程序的探测字符串：

```c
device_set_desc(dev, "My First FreeBSD Driver (Chapter 15 Stage 4)");
```

### 最终回归测试

第15章回归套件添加了自己的测试，但也重新运行每个前面章节的测试。一个紧凑的顺序：

1. **干净构建**在启用了所有常用选项的调试内核上。
2. **加载**驱动程序。
3. **第11章测试**：基本读取、写入、打开、关闭、重置。
4. **第12章测试**：有界读取、定时读取、cv 广播、sx 配置。
5. **第13章测试**：各种速率的 callout、watchdog 检测、tick source。
6. **第14章测试**：poll 等待者、合并洪泛、延迟重置、负载下 detach。
7. **第15章测试**：写入者上限正确性、统计缓存并发、带部分进度的信号中断、满负载下 detach。
8. **WITNESS 通过**：每个测试，`dmesg` 中零警告。
9. **DTrace 验证**：唤醒计数和任务触发符合预期。
10. **长时间压力测试**：数小时负载，带周期性 detach-重载循环。

每个测试通过。每个 `WITNESS` 警告被解决。驱动程序达到 `0.9-coordination`，第3部分完成。

### 文档审计

一次最终文档审查。

- `myfirst.c` 文件顶部注释更新了第15章词汇。
- `myfirst_sync.h` 有总结设计的文件顶部注释。
- `LOCKING.md` 有信号量、协调和统计缓存节。
- 章节的 `README.md` 在 `examples/part-03/ch15-more-synchronization/` 下描述每个阶段。
- 每个 sysctl 都有描述字符串。

更新文档感觉像是开销。它是下一个维护者可以更改的驱动程序和他们必须重写的驱动程序之间的区别。

### 最终审计检查清单

- [ ] 每个 `sema_wait` 是否有匹配的 `sema_post`？
- [ ] 每个 `sx_slock` 是否有匹配的 `sx_sunlock`？
- [ ] 每个 `sx_xlock` 是否有匹配的 `sx_xunlock`？
- [ ] 每个原子读取是否使用 `atomic_load_acq_int`，每个原子写入是否在排序重要时使用 `atomic_store_rel_int`？
- [ ] 每个 `cv_*_sig` 调用是否显式处理 EINTR、ERESTART 和 EWOULDBLOCK？
- [ ] 每个阻塞等待的调用者是否记录并检查部分进度？
- [ ] 每个同步原语是否都包装在 `myfirst_sync.h` 中？
- [ ] 每个原语是否出现在 `LOCKING.md` 中？
- [ ] `LOCKING.md` 中的 detach 排序是否准确？
- [ ] 驱动程序是否通过完整的回归套件？

一个能干净回答每个项目的驱动程序是一个你可以放心交给另一个工程师的驱动程序。

### 第8节总结

Stage 4 整合。头文件就位，`LOCKING.md` 是最新的，版本反映了新能力，回归套件通过，审计干净。驱动程序是 `0.9-coordination`，同步故事完成。

第3部分也完成了。五章，五个原语逐一添加，每个都与之前的组合。实验和挑战后面的总结节框架了第3部分完成了什么以及第4部分将如何使用它。



## 附加主题：原子操作、`epoch(9)` 回顾与内存排序

第15章的主体教授了基本内容。三个相关主题值得稍深入的提及，因为它们在实际驱动程序代码中反复出现，因为底层思想完善了同步故事。

### 更深入的原子操作

第15章驱动程序使用了三个原子原语：`atomic_load_acq_int`、`atomic_store_rel_int`，以及通过 `counter(9)` 隐式使用的 `atomic_fetchadd_int`。`atomic(9)` 家族比这三个操作所暗示的更大、更有结构。

**读-修改-写原语。**

- `atomic_fetchadd_int(p, v)`：返回 `*p` 的旧值，设置 `*p += v`。用于自由运行的计数器。
- `atomic_cmpset_int(p, old, new)`：如果 `*p == old`，设置 `*p = new` 并返回 1；否则返回 0 且不修改。经典的比较并交换。用于实现无锁状态机。
- `atomic_cmpset_acq_int`、`atomic_cmpset_rel_int`：带获取或释放语义的变体。
- `atomic_readandclear_int(p)`：返回旧值并设置 `*p = 0`。用于"取当前值，重置"。
- `atomic_set_int(p, v)`：设置位 `*p |= v`。用于标志设置协调。
- `atomic_clear_int(p)`：清除位 `*p &= ~v`。用于标志清除协调。
- `atomic_swap_int(p, v)`：返回旧 `*p`，设置 `*p = v`。用于获取指针所有权。

**宽度变体。** `atomic_load_int`、`atomic_load_long`、`atomic_load_32`、`atomic_load_64`、`atomic_load_ptr`。名称中的整数大小匹配 C 类型。使用匹配你变量类型的那个。

**屏障变体。** `atomic_thread_fence_acq`、`atomic_thread_fence_rel`、`atomic_thread_fence_acq_rel`、`atomic_thread_fence_seq_cst`。纯屏障，对先前和后续内存访问排序，不原子修改特定位置。偶尔有用。

选择正确的变体是一个小规范。对于读取者轮询的标志，使用 `atomic_load_acq`。对于在先前设置之后提交标志的写入者，使用 `atomic_store_rel`。对于自由运行且读取者从不同步的计数器，使用 `atomic_fetchadd`（无屏障）。对于基于 CAS 的状态机，使用 `atomic_cmpset`。

### 一页纸的 `epoch(9)`

第14章简要介绍了 `epoch(9)`；这里是一个稍深的概述，在驱动程序编写者有用知道的范围内。

epoch 是一个短暂的同步屏障，无需锁即可保护读多数据结构。读取共享数据的代码通过 `epoch_enter(epoch)` 进入 epoch，通过 `epoch_exit(epoch)` 离开。epoch 保证通过 `epoch_call(epoch, cb, ctx)` 释放的任何对象在请求释放时处于 epoch 内的每个读取者退出之前不会被真正回收。

这在精神上类似于 Linux 的 RCU（读-复制-更新），但具有不同的人机工程学。FreeBSD epoch 是一个更粗糙的工具；它保护大型、很少变化的结构，如 ifnet 列表。

想要使用 epoch 的驱动程序通常不创建自己的。它使用内核提供的 epoch 之一，最常见的是网络状态的 `net_epoch_preempt`。网络代码之外的驱动程序编写者很少直接使用 epoch。

驱动程序编写者应该知道的是如何识别其他代码中的模式，以及 taskqueue 的 `NET_TASK_INIT` 何时创建在 epoch 内运行的任务。第14节已涵盖此内容。

### 内存排序，稍深入

原子 API 隐藏了内存排序的架构特定细节。当你在匹配对中使用 `atomic_store_rel_int` 和 `atomic_load_acq_int` 时，在每个架构上插入正确的屏障。你不需要知道细节。

但一页纸的直觉有帮助。

在 x86 上，每个加载都是获取加载，每个存储都是释放存储，在硬件级别。CPU 的内存模型是"全存储顺序"。所以 x86 上的 `atomic_load_acq_int` 只是一个普通 `MOV`，没有额外指令。`atomic_store_rel_int` 也是一个普通 `MOV`。

在 arm64 上，加载和存储具有较弱的默认排序。编译器为 `atomic_load_acq_int` 插入 `LDAR`（加载获取），为 `atomic_store_rel_int` 插入 `STLR`（存储释放）。这些很便宜（几个周期）但不是免费的。

影响：正确性方面，你在两个架构上编写相同的代码。性能方面，x86 为屏障"不付任何代价"，而 arm64 付出小成本。对于像关闭标志这样的罕见操作，两个架构上的成本都可以忽略不计。

进一步的影响：仅在 x86 上测试不足以验证内存排序。在 x86 上使用普通加载的代码，如果省略了原子屏障，在 arm64 上可能会死锁或行为异常。FreeBSD 14.3 很好地支持 arm64；发布给在 arm64 硬件上运行的用户的驱动程序需要在较弱的内存模型上正确。一致地使用 `atomic(9)` API 是你无需每次调用考虑架构就能保证这一点的方式。

### 何时选择每种工具

一个小决策树来结束本节。

- **保护一个被很多上下文读取、很少写入的小不变量？** `atomic_load_acq_int` / `atomic_store_rel_int`。
- **保护一个与其他状态有复杂关系的小不变量？** 互斥锁。
- **等待一个谓词？** 互斥锁加条件变量。
- **等待一个谓词并需要信号处理？** 互斥锁加 `cv_wait_sig` 或 `cv_timedwait_sig`。
- **最多允许 N 个参与者？** `sema(9)`。
- **多个读取者或一个写入者，偶尔提升？** `sx(9)` 加 `sx_try_upgrade`。
- **稍后在线程上下文中运行代码？** `taskqueue(9)`。
- **在截止时间运行代码？** `callout(9)`。
- **跨上下文协调关闭？** 原子标志 + cv 广播（用于阻塞的等待者）。
- **在网络代码中保护读多结构？** `epoch(9)`。

这个决策树就是第3部分一直在构建的心智地图。第15章添加了最后几个分支。第16章将把地图应用于硬件。

### 附加主题总结

原子操作、`epoch(9)` 和内存排序完善了同步工具包。对于大多数驱动程序，常见情况是一个互斥锁加一个 cv 加偶尔的原子操作；其他原语用于特定形状。了解整个集合让你可以无需猜测就选择正确的工具。



## 动手实验

四个实验将第15章材料应用于具体任务。每个实验分配一个会话。实验1和2最重要；实验3和4拓展读者。

### 实验1：观察写入者上限强制执行

**目标。** 确认写入者上限信号量限制并发写入者，并且该限制可在运行时配置。

**设置。** 构建并加载 Stage 4 驱动程序。从配套源代码编译 `writer_cap_test` 辅助程序。

**步骤。**

1. 验证默认限制：`sysctl dev.myfirst.0.writers_limit`。应该是 4。
2. 检查信号量值：`sysctl dev.myfirst.0.stats.writers_sema_value`。也应该是 4（无活跃写入者）。
3. 在后台启动十六个阻塞写入者：
   ```text
   # for i in $(jot 16 1); do
       (cat /dev/urandom | head -c 10000 > /dev/myfirst) &
   done
   ```
4. 观察运行时的信号量值。大部分时间应该接近零（所有槽位在使用中）。
5. 将限制降低到 2：
   ```text
   # sysctl dev.myfirst.0.writers_limit=2
   ```
6. 观察信号量值最终降到 0 并保持在那里（写入者排空快于重新进入）。
7. 将限制提升到 8：
   ```text
   # sysctl dev.myfirst.0.writers_limit=8
   ```
8. 驱动程序立即发布四个额外槽位；在 `sema_wait` 中阻塞的写入者唤醒并进入。
9. 等待所有写入者完成；验证最终信号量值等于当前限制。

**预期结果。** 信号量作为准入控制器；在运行时重新配置它即重新配置限制。在重负载下信号量排空到零；当负载缓解时，它重新填充到配置的限制。

**变体。** 使用以 `O_NONBLOCK` 打开的辅助程序尝试非阻塞写入者。当信号量耗尽时观察 `sysctl dev.myfirst.0.stats.writers_trywait_failures` 增长。

### 实验2：统计缓存争用

**目标。** 观察统计缓存在读多工作负载下以少量刷新服务多次读取。

**设置。** Stage 4 驱动程序已加载。DTrace 可用。

**步骤。**

1. 启动 32 个并发读取者进程，每个在紧密循环中读取缓存的统计：
   ```text
   # for i in $(jot 32 1); do
       (while :; do
           sysctl -n dev.myfirst.0.stats.bytes_written_10s >/dev/null
       done) &
   done
   ```
2. 在单独的终端中，通过 DTrace 观察缓存刷新率：
   ```text
   # dtrace -n 'fbt::myfirst_stats_cache_refresh:entry { @ = count(); }'
   ```
3. 让工作负载运行 30 秒，然后 Ctrl-C DTrace。记录计数。

**预期结果。** 30 秒内大约 30 次刷新：每秒一次，无论多少读取者在读取。如果你看到明显更多刷新，缓存被过于激进地失效；如果明显更少，读取者实际上没有触发过期路径。

**变体。** 与写入工作负载并行运行测试（也使用 `/dev/myfirst`）。刷新率不应改变：刷新由缓存过期触发，而非写入。

### 实验3：信号处理和部分进度

**目标。** 确认被信号中断的 `read(2)` 返回迄今为止复制的字节数，而不是 EINTR。

**设置。** Stage 4 驱动程序已加载。停止的 tick source，空缓冲区。

**步骤。**

1. 启动一个请求 4096 字节、无超时的读取者：
   ```text
   # dd if=/dev/myfirst bs=4096 count=1 > /tmp/out 2>&1 &
   # READER=$!
   ```
2. 以慢速启用 tick source：
   ```text
   # sysctl dev.myfirst.0.tick_source_interval_ms=500
   ```
   读取者缓慢累积字节，每 500 毫秒一个。
3. 大约 2 秒后，向读取者发送 SIGINT：
   ```text
   # kill -INT $READER
   ```
4. `dd` 报告信号之前复制的字节数。

**预期结果。** `dd` 报告部分结果（例如，4096 请求中复制了 4 字节）并以 0 退出（部分成功），而不是错误。驱动程序将部分字节计数返回给系统调用层；系统调用层将其作为正常短读取返回。

**变体。** 将 `read_timeout_ms` 设置为 1000 并重复。驱动程序应返回部分结果（如果任何字节到达）或 EAGAIN（如果超时在零字节时先触发）。信号处理仍应保留部分字节。

### 实验4：最大负载下 Detach

**目标。** 确认在完全并发负载下 detach 排序正确。

**设置。** Stage 4 驱动程序已加载。所有第14章和第15章压力工具已编译。

**步骤。**

1. 启动完整压力套件：
   - 8 个并发写入者。
   - 4 个并发读取者。
   - 以 1 毫秒的 tick source。
   - 以 100 毫秒的心跳。
   - 以 1 秒的 watchdog。
   - 每 100 毫秒的 sysctl 洪泛：`bulk_writer_flood=1000`。
   - 每 500 毫秒的 sysctl 洪泛：在 1 和 8 之间调整 `writers_limit`。
   - 并发 sysctl 读取 `stats.bytes_written_10s`。
2. 让压力运行 30 秒以确保最大负载。
3. 卸载驱动程序：
   ```text
   # kldunload myfirst
   ```
4. 终止压力进程。观察卸载干净完成。

**预期结果。** 卸载成功（不是 EBUSY，不是 panic，没有挂起）。所有压力进程优雅失败（open 返回 ENXIO，未完成的读/写返回 ENXIO 或短结果）。`dmesg` 没有 `WITNESS` 警告。

**变体。** 重复循环 20 次（加载、压力、卸载）。每个循环行为应相同。如果一个循环 panic 或挂起，存在竞争；调查。



## 挑战练习

挑战超出章节主体。它们是可选的；每个巩固一个特定的第15章想法。

### 挑战1：用可中断等待替换信号量

写入者上限使用 `sema_wait`，它不是信号可中断的。重写写入路径的准入控制以使用 `sema_trywait` 加可中断的 `cv_wait_sig` 循环。保留部分进度约定。

预期结果：等待槽位的写入者可以干净地被 SIGINT。加分：使用 `myfirst_sync.h` 中的封装，使 `myfirst_sync_writer_enter` 具有相同的签名但不同的内部实现。

### 挑战2：带后台刷新器的读多模式

统计缓存在读取者注意到过期时按需刷新。更改设计使缓存由周期性 callout 刷新，读取者从不触发刷新。比较结果代码与升级-提升-降级模式。

预期结果：理解何时按需缓存优于后台缓存。答案：按需更简单（无需管理 callout 生命周期），但会在没人读取的缓存上浪费刷新。后台更可预测但需要额外原语。大多数驱动程序对小缓存选择按需，对大缓存选择后台。

### 挑战3：多个写入者上限信号量

想象驱动程序分层在一个存储后端上，该后端对不同类别的 IO 有独立的池："小写入"和"大写入"。添加第二个信号量，独立限制大写入，有自己的限制。进入写入路径的写入者根据其 `uio_resid` 选择获取哪个信号量。

预期结果：多个信号量的实践经验。思考：如果写入者获取了"大"信号量，然后 `uio` 结果是小的，写入路径是要释放并重新获取吗？还是保留已获取的槽位？记录你的选择。

### 挑战4：基于原子的恢复标志消除

用原子比较并交换替换 `recovery_in_progress` 状态标志。Watchdog 执行 `atomic_cmpset_int(&sc->recovery_in_progress, 0, 1)`；成功时入队任务。任务用 `atomic_store_rel_int` 清除标志。

预期结果：恢复机制不再需要互斥锁。在正确性、复杂性和可观察性方面比较两个实现。

### 挑战5：假设读取路径的 Epoch

研究 `/usr/src/sys/sys/epoch.h` 和 `/usr/src/sys/kern/subr_epoch.c`。以注释而非代码的形式概述你如何将读取路径转换为使用私有 epoch，保护一个写入者偶尔更新的"当前配置"指针。

预期结果：一份包含权衡的书面提案。由于驱动程序的当前配置很小且 sx 已经很好地处理了它，这是一个思维练习而非实际重构。重点是理解 epoch 何时是更好的选择。

### 挑战6：压力测试特定竞争

从第7节中选择一个竞争条件。编写一个可靠触发它的脚本。验证 Stage 4 驱动程序不表现出该错误。验证故意破坏的版本（回退修复）确实表现出该错误。

预期结果：看到竞争条件按需产生的满足感，以及生产代码处理它的安心。

### 挑战7：阅读真实驱动程序的同步词汇

打开 `/usr/src/sys/dev/bge/if_bge.c`（或类似的中等大小驱动程序，有许多原语）。遍历其 attach 和 detach 路径。计数：

- 互斥锁。
- 条件变量。
- Sx 锁。
- 信号量（如果有；许多驱动程序没有）。
- Callout。
- 任务。
- 原子操作。

写一页关于驱动程序同步策略的总结。与 `myfirst` 比较。真实驱动程序做了什么不同的事情，为什么？

预期结果：阅读真实驱动程序的同步策略是让你自己的感觉熟悉的最快方式。一次这样的阅读后，打开 `/usr/src/sys/dev/` 中的任何其他驱动程序变得更容易。



## 故障排除参考

第15章常见问题的平面参考列表。

### 信号量死锁或泄漏

- **写入者堆积且永远不继续。** 信号量的计数器为零，没有人 post。检查：每个 `sema_wait` 是否有匹配的 `sema_post`？是否有忘记 post 的早期返回路径？
- **`sema_destroy` 因 "waiters" 断言而 panic。** 销毁发生时线程仍在 `sema_wait` 内。修复：确保 detach 路径在销毁之前静止所有潜在等待者。通常这意味着先清除 `is_attached` 并做 cv 广播。
- **`sema_trywait` 返回意外值。** 记住：成功返回 1，失败返回 0。与大多数 FreeBSD API 相反。重新检查调用点逻辑。

### Sx 锁问题

- **`sx_try_upgrade` 总是失败。** 调用线程可能与其他读取者共享 sx。检查：是否有路径在不同线程中持久持有 `sx_slock`？
- **sx 与另一个锁之间死锁。** 锁顺序违规。在 `WITNESS` 下运行；内核会命名违规。
- **`sx_downgrade` 没有 `sx_try_upgrade` 对。** 确保在降级之前 sx 实际上是独占持有的。`sx_xlocked(&sx)` 断言这一点。

### 信号处理问题

- **`read(2)` 不响应 SIGINT。** 阻塞等待是 `cv_wait`（不是 `cv_wait_sig`）或 `sema_wait`。转换为信号可中断的变体。
- **`read(2)` 返回 EINTR 且部分字节丢失。** 缺少部分进度检查。在信号错误路径上添加 `uio_resid != nbefore` 检查。
- **`read(2)` 在 EINTR 后循环。** 循环在信号错误时继续而不是返回。添加 EINTR/ERESTART 处理。

### 原子和内存排序

- **关闭标志检查遗漏。** 一个上下文用普通加载读取 `sc->is_attached` 并看到过时值。转换为 `atomic_load_acq_int`。
- **写入顺序在 arm64 上未被观察到。** 写入跨架构重排序。对 detach 前序列中的最后一次写入使用 `atomic_store_rel_int`。

### 协调错误

- **恢复任务运行多次。** 状态标志未受保护或原子 CAS 被误用。要么使用互斥锁保护的标志模式，要么审查 CAS 逻辑。
- **恢复任务从不运行。** 标志从未被清除，或 watchdog 未入队。用每个路径上的 `device_printf` 检查哪一边有错误。

### 测试问题

- **压力测试有时通过有时失败。** 一个低概率的竞争。运行更多迭代、增加并发，或添加时间噪声（在随机点 `usleep`）来暴露它。
- **DTrace 探针不触发。** 内核构建时没有 FBT 探针，或函数被内联消除了。检查 `dtrace -l | grep myfirst`。
- **WITNESS 警告淹没日志。** 不要忽略它们。每个警告都是真实错误。一次修复一个并迭代。

### Detach 问题

- **`kldunload` 返回 EBUSY。** 仍有打开的文件描述符。关闭它们并重试。
- **`kldunload` 挂起。** 一个排空正在等待一个无法完成的原语。通常是任务或 callout。在 kldunload 线程上使用 `procstat -kk` 找到它卡在哪里。
- **卸载期间内核 panic。** 任务或 callout 回调中的释放后使用；排序错误。根据实际代码审查 `LOCKING.md` 中的 detach 序列。



## 第3部分总结

第15章是第3部分的最后一章。第3部分有一个特定使命：给 `myfirst` 驱动程序一个完整的同步故事，从第11章的第一个互斥锁到第15章的最后一个原子标志。使命完成了。

第3部分交付内容的简短清单。

### 互斥锁（第11章）

第一个原语。一个睡眠互斥锁保护驱动程序的共享数据免受并发访问。每个接触 cbuf、打开计数、活跃文件句柄计数或其他互斥锁保护字段的路径首先获取 `sc->mtx`。`WITNESS` 强制执行规则。

### 条件变量（第12章）

等待原语。两个 cv（`data_cv`、`room_cv`）让读取者和写入者睡眠直到缓冲区状态可接受。`cv_wait_sig` 和 `cv_timedwait_sig` 使等待信号可中断和有时间限制。

### 共享/独占锁（第12章）

读多原语。`sc->cfg_sx` 保护配置结构。读取用共享获取，写入用独占获取。第15章添加了带升级-提升-降级模式的 `sc->stats_cache_sx`。

### Callout（第13章）

时间原语。三个 callout（心跳、watchdog、tick source）给驱动程序内部时间，无需专用线程。`callout_init_mtx` 使它们感知锁；`callout_drain` 使它们安全拆除。

### Taskqueue（第14章）

延迟工作原语。一个带三个任务（selwake、批量写入者、恢复）的私有 taskqueue 将工作从受约束的上下文移动到线程上下文。Detach 序列在释放队列之前排空每个任务。

### 信号量（第15章）

有界准入原语。`writers_sema` 限制并发写入者。API 很小且无所有权；驱动程序对非阻塞入口使用 `sema_trywait`，对阻塞使用 `sema_wait`。

### 原子操作（第15章）

跨上下文标志。`is_attached` 上的 `atomic_load_acq_int` 和 `atomic_store_rel_int` 使关闭标志以正确的内存排序对每个上下文可见。

### 封装（第15章）

维护原语。`myfirst_sync.h` 将每个同步操作包装在命名函数中。未来的读者通过阅读一个头文件理解驱动程序的同步策略。

### 驱动程序现在能做什么

第3部分结束时驱动程序能力的简短清单：

- 在有界环形缓冲区上服务并发读取者和写入者。
- 阻塞读取者直到数据到达，带可选超时。
- 阻塞写入者直到有空间，带可选超时。
- 通过可配置信号量限制并发写入者。
- 暴露由 sx 锁保护的配置（调试级别、昵称、软字节限制）。
- 发出周期性心跳日志行。
- 通过 watchdog 检测停滞的缓冲区排空。
- 通过 tick source callout 注入合成数据。
- 通过任务从 callout 回调延迟 `selwakeup`。
- 通过可配置的批量写入者演示任务合并。
- 通过超时任务调度延迟重置。
- 通过升级感知 sx 模式暴露缓存统计。
- 跨每个原语无竞争地协调 detach。
- 在阻塞操作期间正确响应信号。
- 在读写下遵守部分进度语义。

这是一个实质性的驱动程序。`0.9-coordination` 的 `myfirst` 模块是每个真实 FreeBSD 驱动程序使用的同步模式的紧凑但完整的示例。模式可以迁移。

### 第3部分未涵盖的内容

第3部分故意留给后面部分的简短主题列表：

- 硬件中断（第4部分）。
- 内存映射寄存器访问（第4部分）。
- DMA 和总线空间操作（第4部分）。
- PCI 设备匹配（第4部分）。
- USB 和网络特定子系统（第6部分）。
- 高级性能调优（后面的专门章节）。

第3部分专注于内部同步故事。第4部分将添加面向硬件的故事。同步故事不会消失；它成为硬件故事所依赖的基础。

### 回顾

你从第11章开始，驱动程序一次支持一个用户。你在第15章结束，驱动程序支持多个用户，协调多种工作，并在负载下干净拆除。一路上你学习了内核的主要同步原语，每个都是为特定不变量引入的，每个都与之前的组合。

学习模式是有意的。每章引入一个新概念，在小重构中将其应用于驱动程序，在 `LOCKING.md` 中记录，并添加回归测试。结果是一个同步不是意外的驱动程序。每个原语都有存在的理由；每个原语都被记录；每个原语都被测试。

这个规范是第3部分教授的最持久的东西。特定原语（互斥锁、cv、sx、callout、taskqueue、信号量、原子）是通货，但"选择正确的原语，记录它，测试它"的规范是投资。用这个规范构建的驱动程序能经受增长、维护交接和令人惊讶的负载模式。没有它构建的驱动程序积累微妙的错误和难以解释的崩溃。

第4部分打开了硬件之门。你现在知道的原语与你同在。你练习过的规范是让你添加面向硬件的故事而不迷失的东西。

花点时间。这是一个真正的成就。然后转到第16章。

## 第3部分检查点

五章的同步是大量材料。在第4部分打开硬件之门之前，值得确认原语和规范已经稳固。

在第3部分结束时，你应该能够自信地完成以下每一项：

- 在 `mutex(9)`、`sx(9)`、`rw(9)`、`cv(9)`、`callout(9)`、`taskqueue(9)`、`sema(9)` 和 `atomic(9)` 家族之间选择，清楚知道每个不变量适合哪个，而不是凭习惯或猜测。
- 在 `LOCKING.md` 中记录驱动程序的锁定，命名每个原语、它执行的不变量、它保护的数据以及调用者必须遵循的规则。
- 使用 `mtx_sleep`/`wakeup` 或 `cv_wait`/`cv_signal` 实现睡眠-唤醒握手，并解释为什么选择其中一个而不是另一个。
- 用 `callout(9)` 调度定时工作，包括 detach 下的取消，不留悬空定时器。
- 通过 `taskqueue(9)` 推迟重型或有序工作，包括防止任务对已释放状态运行的 detach 时排空。
- 在 `INVARIANTS` 和 `WITNESS` 内核下保持 `myfirst` 干净运行，同时多线程压力测试冲击每个入口点。

如果其中任何一个仍然模糊，重新回顾引入它们的实验：

- 锁定规范和回归：实验 11.2（用 INVARIANTS 验证锁定规范）、实验 11.4（构建多线程测试器）和实验 11.7（长时间运行压力）。
- 条件变量和 sx：实验 12.2（添加有界读取）、实验 12.5（检测故意锁顺序反转）和实验 12.7（验证快照和应用模式在争用下成立）。
- Callout 和定时工作：实验 13.1（添加心跳 Callout）和实验 13.4（验证带活跃 Callout 的 Detach）。
- Taskqueue 和延迟工作：第14章的实验 2（测量负载下的合并）和实验 3（验证 Detach 排序）。
- 信号量：第15章的实验 1（观察写入者上限强制执行）和实验 4（最大负载下 Detach）。

第4部分将在第3部分刚构建的一切之上叠加硬件。具体来说，后续章节将期望：

- 同步模型已内化而非死记硬背，这样中断上下文可以作为又一种调用者添加，而不是新的规则宇宙。
- Detach 排序被视为跨原语的单个共享规范，因为第4部分将把中断拆除和总线资源释放添加到同一链中。
- 继续以 `INVARIANTS` 和 `WITNESS` 作为默认开发内核的舒适性，因为第4部分更难的错误通常会在表现为可见 panic 之前很久就触发两者之一。

如果这些都成立，第4部分触手可及。如果某个仍然感觉不稳固，修复方法是通过相关实验再跑一圈，而不是向前推进。

## 通往第16章的桥梁

第16章开启本书第4部分。第4部分的标题是*硬件和平台级集成*，第16章是*硬件基础和 Newbus*。第4部分的使命是给驱动程序一个硬件故事：驱动程序如何向内核的总线层宣告自己，如何与内核发现的硬件匹配，如何接收中断，如何访问内存映射寄存器，以及如何管理 DMA。

第3部分的同步故事不会消失。它成为第4部分构建的基础。硬件中断处理程序在你现在知道如何推理的上下文中运行（不睡眠、不可睡眠锁、不用 uiomove）。它通过你现在知道如何使用的原语与驱动程序的其余部分通信（taskqueue 用于延迟工作、互斥锁用于串行化、原子标志用于关闭）。区别在于驱动程序现在还必须直接与硬件对话，而硬件有自己的规则。

第16章以三种具体方式准备硬件基础。

首先，**你已经知道上下文边界**。第3章教会你 callout、任务和用户线程各有自己的规则。中断添加了一个更严格规则的上下文。心智模型（"我在什么上下文中；我在这里能安全做什么"）直接迁移。

其次，**你已经知道 detach 排序**。第3部分跨五个原语建立了 detach 规范。第4部分添加了两个（中断拆除、资源释放）插入同一规范中。排序规则增长；形状不变。

第三，**你已经知道 `LOCKING.md` 是一个活文档**。第16章添加硬件资源节。规范相同；词汇扩展。

第16章将涵盖的具体主题：

- `newbus(9)` 框架：驱动程序如何被识别、探测和附加。
- `device_t`、`devclass`、`driver_t` 和 `device_method_t`。
- `bus_alloc_resource` 和 `bus_release_resource` 用于内存映射区域、IRQ 线和其他资源。
- `bus_setup_intr` 和 `bus_teardown_intr` 用于中断注册。
- 过滤处理程序与中断线程。
- newbus 和 PCI 子系统之间的关系（为第17章做准备）。

你不需要提前阅读。第15章就是充分的准备。带上你的 `0.9-coordination` 的 `myfirst` 驱动程序、你的 `LOCKING.md`、你启用 `WITNESS` 的内核和你的测试套件。第16章从第15章结束的地方开始。

一个简短的结束反思。你开始第3部分时的驱动程序知道如何服务一个系统调用。你现在拥有的驱动程序有一个完整的内部同步故事，有六种原语，每种都为特定形状的不变量选择，每种都封装在可读的包装层中，每种都被记录，每种都被测试。它准备好面对硬件了。

硬件是下一章。然后是再下一章。然后是第4部分的每一章。基础已建成。工具在台上。蓝图已准备好。

翻页。


## 参考：生产前同步审计

在发布同步密集的驱动程序之前，运行此审计。每个项目是一个问题；每个都应能自信回答。

### 互斥锁审计

- [ ] 每个 `sc->mtx` 持有区域是否在每条路径上以 `mtx_unlock` 结束？
- [ ] 互斥锁是否总是在调用 `uiomove`、`copyin`、`copyout`、`selwakeup` 或任何其他可睡眠操作之前释放？
- [ ] 是否有在 cv 等待上持有互斥锁的地方？如果有，等待是否是预期的原语？
- [ ] 锁顺序 `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx` 是否在任何地方都被遵守？

### 条件变量审计

- [ ] 每个 cv 等待是否在系统调用上下文中调用 `cv_wait_sig` 或 `cv_timedwait_sig`？
- [ ] EINTR 是否在适当的地方以部分进度保留的方式处理？
- [ ] ERESTART 是否正确传播？
- [ ] 每个 cv 是否在 detach 时有匹配的广播？
- [ ] 唤醒是否在持有互斥锁时进行以保证正确性（或为吞吐量释放互斥锁，权衡已记录）？

### Sx 审计

- [ ] 每个 `sx_slock` 是否有匹配的 `sx_sunlock`？
- [ ] 每个 `sx_xlock` 是否有匹配的 `sx_xunlock`？
- [ ] 每个 `sx_try_upgrade` 失败路径是否正确处理释放并重新获取后的重新检查？
- [ ] 每个 `sx_downgrade` 是否发生在实际独占持有的锁上？

### 信号量审计

- [ ] 每个 `sema_wait` 是否在每条路径上有匹配的 `sema_post`？
- [ ] 信号量是否仅在所有等待者静止后才销毁？
- [ ] `sema_trywait` 的返回值（成功返回 1，失败返回 0）是否被正确读取？
- [ ] 如果信号量与可中断系统调用一起使用，`sema_wait` 的不可中断性是否被记录？
- [ ] 是否有一个在调用 `sema_destroy` 之前 detach 排空的飞行中计数器（例如 `writers_inflight`）？
- [ ] 计数器增量和计数器减量之间的每条路径是否实际使用了信号量（没有绕过增量的早期返回）？

### Callout 审计

- [ ] 每个 callout 是否使用带适当锁的 `callout_init_mtx`？
- [ ] 每个 callout 回调是否检查 `is_attached` 并在为假时提前退出？
- [ ] 每个 callout 是否在它接触的状态被释放之前在 detach 时排空？

### 任务审计

- [ ] 每个任务回调在接触共享状态时是否持有适当的锁？
- [ ] 每个任务回调是否只在未持有驱动程序锁时调用 `selwakeup`？
- [ ] 每个任务是否在入队它的 callout 被排空后在 detach 时排空？
- [ ] 私有 taskqueue 是否在每个任务被排空后释放？

### 原子审计

- [ ] 每个关闭标志读取是否使用 `atomic_load_acq_int`？
- [ ] 每个关闭标志写入是否使用 `atomic_store_rel_int`？
- [ ] 是否有其他原子操作由特定的内存排序需求证明？

### 跨组件审计

- [ ] 每个跨组件状态标志是否有明确的所有权（哪个路径设置，哪个路径清除）？
- [ ] 标志是否由适当的锁或原子规范保护？
- [ ] 握手是否记录在 `LOCKING.md` 中？

### 文档审计

- [ ] `LOCKING.md` 是否列出每个原语？
- [ ] `LOCKING.md` 是否记录 detach 排序？
- [ ] `LOCKING.md` 是否记录锁顺序？
- [ ] 是否解释了任何微妙的跨组件握手？

### 测试审计

- [ ] 驱动程序是否在 `WITNESS` 下进行了长时间压力测试，无警告？
- [ ] 是否在满负载下测试了 detach 循环？
- [ ] 信号中断测试是否确认部分进度保留？
- [ ] 是否测试了运行时配置更改（sysctl 调整）？

通过此审计的驱动程序是一个你可以发布的驱动程序。



## 参考：同步原语速查表

### 何时使用哪个

| 原语 | 最适合 | 不适合 |
|---|---|---|
| `struct mtx` (MTX_DEF) | 短临界区；互斥。 | 等待条件。 |
| `struct cv` + mtx | 等待谓词；信号唤醒。 | 有界准入。 |
| `struct sx` | 读多状态；共享读取偶尔写入。 | 高争用。 |
| `struct sema` | 有界准入；生产者-消费者完成。 | 可中断等待。 |
| `callout` | 基于时间的工作。 | 必须睡眠的工作。 |
| `taskqueue` | 延迟线程上下文工作。 | 亚微秒延迟。 |
| `atomic_*` | 小跨上下文标志；无锁协调。 | 复杂不变量。 |
| `epoch` | 网络代码中的读多共享结构。 | 没有共享结构的驱动程序。 |

### API 快速参考

**互斥锁。**
- `mtx_init(&mtx, name, type, MTX_DEF)`
- `mtx_lock(&mtx)`、`mtx_unlock(&mtx)`
- `mtx_assert(&mtx, MA_OWNED)`
- `mtx_destroy(&mtx)`

**条件变量。**
- `cv_init(&cv, name)`
- `cv_wait(&cv, &mtx)`、`cv_wait_sig`
- `cv_timedwait(&cv, &mtx, timo)`、`cv_timedwait_sig`
- `cv_signal(&cv)`、`cv_broadcast(&cv)`
- `cv_destroy(&cv)`

**Sx。**
- `sx_init(&sx, name)`
- `sx_slock(&sx)`、`sx_sunlock(&sx)`
- `sx_xlock(&sx)`、`sx_xunlock(&sx)`
- `sx_try_upgrade(&sx)`、`sx_downgrade(&sx)`
- `sx_xlocked(&sx)`、`sx_xholder(&sx)`
- `sx_destroy(&sx)`

**信号量。**
- `sema_init(&s, value, name)`
- `sema_wait(&s)`、`sema_timedwait(&s, timo)`、`sema_trywait(&s)`
- `sema_post(&s)`
- `sema_value(&s)`
- `sema_destroy(&s)`

**Callout。**
- `callout_init_mtx(&co, &mtx, 0)`
- `callout_reset(&co, ticks, fn, arg)`
- `callout_stop(&co)`、`callout_drain(&co)`

**Taskqueue。**
- `TASK_INIT(&t, 0, fn, ctx)`、`TIMEOUT_TASK_INIT(...)`
- `taskqueue_create(name, flags, enqueue, ctx)`
- `taskqueue_start_threads(&tq, count, pri, name, ...)`
- `taskqueue_enqueue(tq, &t)`、`taskqueue_enqueue_timeout(...)`
- `taskqueue_cancel(tq, &t, &pend)`、`taskqueue_drain(tq, &t)`
- `taskqueue_free(tq)`

**原子。**
- `atomic_load_acq_int(p)`、`atomic_store_rel_int(p, v)`
- `atomic_fetchadd_int(p, v)`、`atomic_cmpset_int(p, old, new)`
- `atomic_set_int(p, v)`、`atomic_clear_int(p, v)`
- `atomic_thread_fence_seq_cst()`

### 上下文规则

| 上下文 | 可睡眠？ | 可睡眠锁？ | 备注 |
|---|---|---|---|
| 系统调用 | 是 | 是 | 完整线程上下文。 |
| Callout 回调（感知锁） | 否 | 否 | 注册的互斥锁被持有。 |
| 任务回调（线程支持） | 是 | 是 | 未持有驱动程序锁。 |
| 任务回调（fast/swi） | 否 | 否 | SWI 上下文。 |
| 中断过滤 | 否 | 否 | 非常有限。 |
| 中断线程 | 否 | 否 | 比过滤稍多。 |
| Epoch 节 | 否 | 否 | 非常有限。 |

### 部分进度约定

在被中断之前复制了 N 字节的 `read(2)` 或 `write(2)` 应返回 N（作为成功的部分短读/写），而不是 EINTR。驱动程序的等待辅助函数在部分路径上返回哨兵（`MYFIRST_WAIT_PARTIAL`）；调用者将其转换为 0，以便系统调用层返回字节计数。

### Detach 排序

第15章 detach 的规范顺序：

1. 如果 `active_fhs > 0` 则拒绝。
2. 原子地清除 `is_attached`。
3. 广播所有 cv；释放互斥锁。
4. 排空所有 callout。
5. 排空所有任务（包括超时任务和恢复）。
6. `seldrain` 用于 rsel、wsel。
7. 释放 taskqueue。
8. 销毁信号量。
9. 销毁统计缓存 sx。
10. 销毁 cdev、sysctl、cbuf、计数器。
11. 销毁 cv、cfg sx、互斥锁。

记住这个形状。当你添加新原语时调整顺序。



## 参考：进一步阅读

### 手册页

- `sema(9)`：内核信号量。
- `sx(9)`：共享/独占锁。
- `mutex(9)`：互斥锁原语。
- `condvar(9)`：条件变量。
- `atomic(9)`：原子操作。
- `epoch(9)`：基于 epoch 的同步。
- `locking(9)`：内核锁定原语概述。

### 源文件

- `/usr/src/sys/kern/kern_sema.c`：信号量实现。
- `/usr/src/sys/sys/sema.h`：信号量 API。
- `/usr/src/sys/kern/kern_sx.c`：sx 实现。
- `/usr/src/sys/sys/sx.h`：sx API。
- `/usr/src/sys/kern/kern_mutex.c`：互斥锁实现。
- `/usr/src/sys/kern/kern_condvar.c`：cv 实现。
- `/usr/src/sys/kern/subr_epoch.c`：epoch 实现。
- `/usr/src/sys/sys/epoch.h`：epoch API。
- `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c`：真实世界的 `sema` 使用。
- `/usr/src/sys/dev/bge/if_bge.c`：丰富的同步示例。

### 书籍和外部材料

- *The Design and Implementation of the FreeBSD Operating System* (McKusick et al.)：有关于内核同步子系统的详细章节。
- *FreeBSD Handbook*，开发者部分：关于内核锁定的章节。
- FreeBSD 邮件列表档案：搜索原语名称（`taskqueue`、`sema`、`sx`）揭示历史设计讨论。

### 建议阅读顺序

对于高级同步的新读者：

1. `mutex(9)`、`condvar(9)`：基本原语。
2. `sx(9)`：读多原语。
3. `sema(9)`：有界准入原语。
4. `atomic(9)`：跨上下文工具。
5. `epoch(9)`：网络驱动程序的无锁读工具。
6. 一个真实驱动程序源代码：`/usr/src/sys/dev/bge/if_bge.c` 或类似。

按顺序阅读需要一个完整下午，给你一个坚实的脑图。



## 参考：第15章术语词汇表

**计数信号量。** 一个持有非负整数的原语，支持等待（递减，如果为零则阻塞）和发布（递增，唤醒一个等待者）。

**二进制信号量。** 一个只持有 0 或 1 的计数信号量。行为上类似于互斥锁但没有所有权。

**优先级继承。** 一种调度器技术，等待锁的高优先级线程临时提升当前持有者的优先级。FreeBSD 互斥锁支持它；信号量不支持。

**信号可中断等待。** 一个阻塞原语（例如 `cv_wait_sig`），在信号到达时返回 EINTR 或 ERESTART。调用者可以放弃等待并传递信号。

**部分进度约定。** UNIX 标准行为：传输了一些字节然后被中断的 `read(2)` 或 `write(2)` 将字节计数作为成功返回，而不是错误。

**EINTR 与 ERESTART。** 两个信号返回码。EINTR 作为 errno EINTR 传递到用户空间。ERESTART 导致系统调用层根据信号处置透明地重启系统调用。

**获取屏障。** 加载上的内存屏障，防止后续内存访问在加载之前重排序。

**释放屏障。** 存储上的内存屏障，防止先前内存访问在存储之后重排序。

**比较并交换 (CAS)。** 一个原子操作，仅在当前值匹配期望值时写入新值。无锁状态机的基础。

**升级-提升-降级。** 一个 sx 模式：获取共享、检测需要写入、尝试升级为独占、写入、降级回共享。

**合并。** Taskqueue 属性：同一任务的冗余入队合并到单个带递增计数器的待处理状态，而不是单独链接。

**封装层。** 一个头文件（`myfirst_sync.h`），命名驱动程序执行的每个同步操作，以便策略可以在一个地方更改并一目了然地理解。

**状态标志。** softc 中记录特定条件是否正在进行的小整数。由适当的锁或原子规范保护。

**跨组件握手。** 使用状态标志、cv 或原子的多个执行上下文（callout、任务、用户线程）之间的协调。



第15章到此结束。第4部分接下来开始。


## 参考：逐行阅读 `kern_sema.c`

`sema(9)` 的实现短到可以端到端阅读。这样做一次巩固了原语实际做什么的心智模型。文件是 `/usr/src/sys/kern/kern_sema.c`，不到两百行。一个叙述性遍历如下。

### `sema_init`

```c
void
sema_init(struct sema *sema, int value, const char *description)
{

        KASSERT((value >= 0), ("%s(): negative value\n", __func__));

        bzero(sema, sizeof(*sema));
        mtx_init(&sema->sema_mtx, description, "sema backing lock",
            MTX_DEF | MTX_NOWITNESS | MTX_QUIET);
        cv_init(&sema->sema_cv, description);
        sema->sema_value = value;

        CTR4(KTR_LOCK, "%s(%p, %d, \"%s\")", __func__, sema, value, description);
}
```

六行逻辑。断言初始值非负。将结构清零。初始化内部互斥锁；注意标志 `MTX_NOWITNESS | MTX_QUIET`，它们告诉 `WITNESS` 不要跟踪内部互斥锁（因为信号量本身是用户关心的，而不是其后备互斥锁）。初始化内部 cv。设置计数器。

含义：信号量实际上是一个互斥锁加一个 cv 加一个计数器，组装成一个小包。理解这个组装就是理解原语。

### `sema_destroy`

```c
void
sema_destroy(struct sema *sema)
{
        CTR3(KTR_LOCK, "%s(%p) \"%s\"", __func__, sema,
            cv_wmesg(&sema->sema_cv));

        KASSERT((sema->sema_waiters == 0), ("%s(): waiters\n", __func__));

        mtx_destroy(&sema->sema_mtx);
        cv_destroy(&sema->sema_cv);
}
```

跟踪之后的两行逻辑。断言没有等待者存在。销毁内部互斥锁和 cv。断言是强制你在销毁之前静止等待者的原因；违反它会使调试内核 panic。

### `_sema_post`

```c
void
_sema_post(struct sema *sema, const char *file, int line)
{

        mtx_lock(&sema->sema_mtx);
        sema->sema_value++;
        if (sema->sema_waiters && sema->sema_value > 0)
                cv_signal(&sema->sema_cv);

        CTR6(KTR_LOCK, "%s(%p) \"%s\" v = %d at %s:%d", __func__, sema,
            cv_wmesg(&sema->sema_cv), sema->sema_value, file, line);

        mtx_unlock(&sema->sema_mtx);
}
```

三行逻辑。锁定、递增、如果有等待者则信号一个。信号取决于两件事：等待者计数非零（如果没人在等待则无需信号）和值为正（值为零的信号会唤醒一个会立即再次睡眠的等待者）。第二个条件微妙；它防止 `sema_value` 在 post 和当前 post 之间变正然后再次变为零的情况。实际上在简单使用中，两个条件都为真。

### `_sema_wait`

```c
void
_sema_wait(struct sema *sema, const char *file, int line)
{

        mtx_lock(&sema->sema_mtx);
        while (sema->sema_value == 0) {
                sema->sema_waiters++;
                cv_wait(&sema->sema_cv, &sema->sema_mtx);
                sema->sema_waiters--;
        }
        sema->sema_value--;

        CTR6(KTR_LOCK, "%s(%p) \"%s\" v = %d at %s:%d", __func__, sema,
            cv_wmesg(&sema->sema_cv), sema->sema_value, file, line);

        mtx_unlock(&sema->sema_mtx);
}
```

四行逻辑。锁定。当值为零时循环：递增等待者、在 cv 上等待、递减等待者。一旦值为正，递减并解锁。

两个观察。

循环是使原语对虚假唤醒安全的原因。`cv_wait` 可以在没有调用 `cv_signal` 的情况下返回。循环每次重新检查值，所以虚假唤醒只是重新睡眠。

等待使用 `cv_wait`，而不是 `cv_wait_sig`。这就是使 `sema_wait` 不可中断的原因。对调用者的信号什么也不做。循环继续直到真正的 `sema_post` 到达。

### `_sema_timedwait`

```c
int
_sema_timedwait(struct sema *sema, int timo, const char *file, int line)
{
        int error;

        mtx_lock(&sema->sema_mtx);

        for (error = 0; sema->sema_value == 0 && error == 0;) {
                sema->sema_waiters++;
                error = cv_timedwait(&sema->sema_cv, &sema->sema_mtx, timo);
                sema->sema_waiters--;
        }
        if (sema->sema_value > 0) {
                sema->sema_value--;
                error = 0;
                /* ... tracing ... */
        } else {
                /* ... tracing ... */
        }

        mtx_unlock(&sema->sema_mtx);
        return (error);
}
```

稍微复杂一点。循环使用 `cv_timedwait`，同样不是 `cv_timedwait_sig`，所以定时等待也不可中断。当值为正或错误变为非零（通常是 `EWOULDBLOCK`）时循环退出。

循环后：如果值为正，我们声明它并返回 0（成功）。否则，我们返回错误（`EWOULDBLOCK`）。注意在错误情况下，cv 错误从最后一次循环迭代中保留。

源代码中的注释指出的一个微妙之处：虚假唤醒会重置有效的超时间隔，因为每次迭代使用新的 timo。这意味着实际等待可能比调用者请求的稍长，但绝不会更短。`EWOULDBLOCK` 返回最终会触发。

### `_sema_trywait`

```c
int
_sema_trywait(struct sema *sema, const char *file, int line)
{
        int ret;

        mtx_lock(&sema->sema_mtx);

        if (sema->sema_value > 0) {
                sema->sema_value--;
                ret = 1;
        } else {
                ret = 0;
        }

        mtx_unlock(&sema->sema_mtx);
        return (ret);
}
```

两行逻辑。锁定。如果值为正，递减并返回 1。否则返回 0。无阻塞、无 cv、无等待者计数。

### `sema_value`

```c
int
sema_value(struct sema *sema)
{
        int ret;

        mtx_lock(&sema->sema_mtx);
        ret = sema->sema_value;
        mtx_unlock(&sema->sema_mtx);
        return (ret);
}
```

一行逻辑。返回当前值。互斥锁释放后值可以立即改变，所以结果是快照，不是保证。对诊断有用。

### 观察

阅读整个文件需要十分钟。最后你理解：

- 信号量由互斥锁和 cv 构建。
- `sema_wait` 不是信号可中断的，因为它使用 `cv_wait`。
- `sema_destroy` 断言没有等待者，这就是为什么你必须在销毁之前静止。
- 合并不存在（每次 post 将计数器减一，没有"待处理"计数）。
- 原语简单；其契约精确。

这种阅读是掌握任何内核原语的最短路径。`sema(9)` 文件是一个特别好的入门，因为它如此简短。


## 参考：跨驱动程序标准化同步原语

随着驱动程序积累更多原语，一致性比聪明更重要。

### 一个命名约定

第15章的约定，读者可以采用或修改：

- **互斥锁**：`sc->mtx`。每个驱动程序一个。如果驱动程序需要多个，每个都有用途后缀：`sc->tx_mtx`、`sc->rx_mtx`。
- **条件变量**：`sc-><purpose>_cv`。例如 `data_cv`、`room_cv`。
- **Sx 锁**：`sc-><purpose>_sx`。例如 `cfg_sx`、`stats_cache_sx`。
- **信号量**：`sc-><purpose>_sema`。例如 `writers_sema`。
- **Callout**：`sc-><purpose>_co`。例如 `heartbeat_co`。
- **任务**：`sc-><purpose>_task`。例如 `selwake_task`。
- **超时任务**：`sc-><purpose>_delayed_task`。例如 `reset_delayed_task`。
- **原子标志**：`sc-><purpose>` 作为 `int`。无后缀；类型说明故事。例如 `is_attached`、`recovery_in_progress`。
- **互斥锁下的状态标志**：与原子相同；softc 中的注释命名锁。

### 一个初始化/销毁模式

每个原语都有规范的初始化和销毁。attach 中的顺序镜像 detach 中的逆序。

Attach 顺序：
1. 互斥锁。
2. Cv。
3. Sx 锁。
4. 信号量。
5. Taskqueue。
6. Callout。
7. 任务。
8. 原子标志（无需初始化；由 softc 清零初始化）。

Detach 顺序（大致逆向）：
1. 清除原子标志。
2. 广播 cv。
3. 释放互斥锁。
4. 排空 callout。
5. 排空任务。
6. 释放 taskqueue。
7. 销毁信号量。
8. 销毁 sx。
9. 销毁 cv。
10. 销毁互斥锁。

经验法则：以初始化相反的顺序销毁，并在销毁它接触的东西之前排空任何仍可触发的东西。

### 一个封装模式

第6节的 `myfirst_sync.h` 模式可扩展。每个原语都有包装器。每个包装器以其做什么命名，而不是它包装的原语。

### 一个 LOCKING.md 模板

每个原语一个 `LOCKING.md` 节。每节命名：

- 原语。
- 其用途。
- 其生命周期。
- 其与其他原语的契约（锁顺序、所有权、与原子标志的交互）。

添加到驱动程序的新原语添加新节。修改的原语更改其现有节。文档始终是最新的。

### 为什么要标准化

好处与第14章相同。减少认知负荷。更少的错误。更容易审查。更容易交接。成本很小且是一次性的。


## 参考：每个原语何时是错误的

同步原语是工具。每个工具都有误用。一个简短的反模式列表，供识别。

### 互斥锁误用

- **跨睡眠持有互斥锁。** 阻止其他线程取得进展；如果睡眠依赖于另一个线程需要互斥锁的状态，可能导致死锁。
- **以不一致的顺序嵌套互斥锁。** 创建锁顺序循环；`WITNESS` 会捕获它。
- **在原子操作就够的地方使用互斥锁。** 对于经常检查的单比特标志，互斥锁比需要的更重。
- **在需要互斥锁的辅助函数中缺少 `mtx_assert`。** 没有断言，辅助函数可以在未持有互斥锁的上下文中调用；错误可能是静默的。

### Cv 误用

- **在系统调用上下文中使用 `cv_wait`。** 无法被信号中断；使系统调用无响应。
- **唤醒后不重新检查谓词。** 虚假唤醒是允许的；假设唤醒意味着谓词为真的代码是有错误的。
- **不持有互斥锁就发信号。** API 通常允许但通常不明智；如果时机不巧，等待者可能错过信号。
- **在需要 `cv_broadcast` 的地方使用 `cv_signal`。** 只唤醒一个等待者的 detach 路径使其他等待者保持阻塞。

### Sx 误用

- **在互斥锁就够的地方使用 sx。** Sx 在无争用情况下比互斥锁有更高的开销；如果没有共享访问好处，互斥锁更简单。
- **忘记 sx 可以睡眠。** Sx 不能从不可睡眠上下文获取。用 `callout_init_mtx(, &mtx, 0)` 初始化的 callout 可以；过滤中断不行。（历史注释：较旧的 `CALLOUT_MPSAFE` 标志命名了相同的区别。第13章介绍了其弃用。）
- **使用 `sx_try_upgrade` 但没有回退。** 一个不处理失败的简单升级与另一个升级者竞争。

### 信号量误用

- **期望信号中断。** `sema_wait` 不会中断。
- **期望优先级继承。** `sema` 不会提升发布者的优先级。
- **带等待者销毁。** 使调试内核 panic，静默损坏生产内核。
- **在错误路径上忘记 post。** 泄漏槽位；最终信号量排空到零，所有等待者阻塞。

### 原子误用

- **在需要获取/释放的地方使用普通原子。** 在 x86 上正确，在 arm64 上破坏。始终考虑内存排序。
- **用单个原子保护复杂不变量。** 如果不变量涉及多个字段，单独的原子不够；需要锁。
- **在简单加载-存储就够的地方使用 CAS。** 浪费一条原子指令。

### 模式误用

- **从互斥锁加计数器加 cv 滚动你自己的信号量。** 内核的 `sema(9)` 已经做了这个；重新发明它创造维护债务。
- **从互斥锁加计数器滚动你自己的读写锁。** `sx(9)` 做了这个；重新发明它创造维护债务。
- **在驱动程序源代码中滚动你自己的封装。** 把它放在头文件中（`myfirst_sync.h`）；不要内联重复。

识别反模式是良好同步的一半。另一半是一开始就选择正确的原语，本章主体已涵盖。


## 参考：可观察性速查表

### 驱动程序应公开的 Sysctl 旋钮

对于第15章，将这些添加到驱动程序的 sysctl 树（在 `dev.myfirst.N.stats.*` 或 `dev.myfirst.N.*` 中，视情况而定）：

- `writers_limit`：当前写入者上限。
- `stats.writers_sema_value`：信号量值的快照。
- `stats.writers_trywait_failures`：被拒绝的非阻塞写入者计数。
- `stats.stats_cache_refreshes`：缓存刷新计数。
- `stats.recovery_task_runs`：恢复调用计数。
- `stats.is_attached`：当前原子标志值。

这些只读计数器给操作员一个窗口，无需调试器即可查看驱动程序的同步行为。

### DTrace 探针

**按 cv 计数 cv 信号：**

```text
dtrace -n 'fbt::cv_signal:entry { @[stringof(args[0]->cv_description)] = count(); }'
```

**计数信号量等待和发布：**

```text
dtrace -n '
  fbt::_sema_wait:entry { @["wait"] = count(); }
  fbt::_sema_post:entry { @["post"] = count(); }
'
```

**测量信号量等待延迟：**

```text
dtrace -n '
  fbt::_sema_wait:entry { self->t = timestamp; }
  fbt::_sema_wait:return /self->t/ {
        @ = quantize(timestamp - self->t);
        self->t = 0;
  }
'
```

对理解写入者在实践中在写入者上限上阻塞多久有用。

**测量 sx 争用：**

```text
dtrace -n '
  lockstat:::sx-block-enter /arg0 == (uintptr_t)&sc_addr/ {
        @ = count();
  }
'
```

用你的 sx 的实际地址替换 `sc_addr`。显示 sx 阻塞的频率。

**观察恢复的 taskqueue_run_locked：**

```text
dtrace -n 'fbt::myfirst_recovery_task:entry { printf("recovery at %Y", walltimestamp); }'
```

每次恢复任务触发时打印时间戳。

### procstat

`procstat -t | grep myfirst`：显示 taskqueue 工作线程及其状态。

`procstat -kk <pid>`：特定线程的内核栈。当某事卡住时有用。

### ps

`ps ax | grep taskq`：按名称列出每个 taskqueue 工作线程。

### ddb

`db> show witness`：转储 WITNESS 锁图。

`db> show locks`：列出当前持有的锁。

`db> show sleepchain <tid>`：遍历睡眠链以查找死锁。


## 参考：阶段性差异摘要

从第14章 Stage 4 到第15章 Stage 4 的驱动程序差异的紧凑摘要，供想要一目了然看到整个变化的读者。

### Stage 1 差异 (v0.8 -> v0.8+writers_sema)

**Softc 添加：**

```c
struct sema     writers_sema;
int             writers_limit;
int             writers_trywait_failures;
int             writers_inflight;   /* atomic int; drain counter */
```

**Attach 添加：**

```c
sema_init(&sc->writers_sema, 4, "myfirst writers");
sc->writers_limit = 4;
sc->writers_trywait_failures = 0;
sc->writers_inflight = 0;
```

**Detach 添加（在 is_attached=0 和所有 cv 广播之后）：**

```c
sema_destroy(&sc->writers_sema);
```

**新 sysctl 处理程序：** `myfirst_sysctl_writers_limit`。

**写入路径更改：** 入口处的信号量获取、出口处的释放、通过 sema_trywait 的 O_NONBLOCK。

### Stage 2 差异 (+stats_cache_sx)

**Softc 添加：**

```c
struct sx       stats_cache_sx;
uint64_t        stats_cache_bytes_10s;
uint64_t        stats_cache_last_refresh_ticks;
```

**Attach 添加：**

```c
sx_init(&sc->stats_cache_sx, "myfirst stats cache");
sc->stats_cache_bytes_10s = 0;
sc->stats_cache_last_refresh_ticks = 0;
```

**Detach 添加（在互斥锁销毁之后）：**

```c
sx_destroy(&sc->stats_cache_sx);
```

**新辅助函数：** `myfirst_stats_cache_refresh`。

**新 sysctl 处理程序：** `myfirst_sysctl_stats_cached`。

### Stage 3 差异 (EINTR/ERESTART + 部分进度)

**无 softc 更改。**

**等待辅助函数重构：** `MYFIRST_WAIT_PARTIAL` 哨兵；对 cv 等待的错误码显式 switch。

**调用者更改：** 对哨兵的显式检查。

### Stage 4 差异（协调 + 封装）

**Softc 添加：**

```c
int             recovery_in_progress;
struct task     recovery_task;
int             recovery_task_runs;
```

**Attach 添加：**

```c
TASK_INIT(&sc->recovery_task, 0, myfirst_recovery_task, sc);
sc->recovery_in_progress = 0;
sc->recovery_task_runs = 0;
```

**Detach 添加：**

```c
taskqueue_drain(sc->tq, &sc->recovery_task);
```

**原子标志转换：** 处理程序和回调中的 `is_attached` 读取变为 `atomic_load_acq_int`；detach 写入变为 `atomic_store_rel_int`。

**新头文件：** 带内联包装器的 `myfirst_sync.h`。

**源代码编辑：** 主源代码中的每个原语特定调用变为包装器调用。

**Watchdog 重构：** 在停滞时入队恢复任务，由 `recovery_in_progress` 标志保护。

**版本升级：** `MYFIRST_VERSION "0.9-coordination"`。

### 总增加行数

跨四个阶段的大致计数：

- Softc：约 10 个字段。
- Attach：约 15 行。
- Detach：约 10 行。
- 新函数：约 80 行（sysctl 处理程序、recovery_task、辅助函数）。
- 修改的函数：约 20 行编辑。
- 头文件：约 150 行。

驱动程序净增加：大约 300 行。与第14章增加的大约 100 行和第13章增加的 400 行比较。第15章增加量适中；但其启用的东西很大。


## 参考：第15章驱动程序生命周期

明确第15章添加的生命周期摘要。

### Attach 序列

1. `mtx_init(&sc->mtx, ...)`。
2. `cv_init(&sc->data_cv, ...)`、`cv_init(&sc->room_cv, ...)`。
3. `sx_init(&sc->cfg_sx, ...)`。
4. `sx_init(&sc->stats_cache_sx, ...)`。
5. `sema_init(&sc->writers_sema, 4, ...)`。
6. `sc->tq = taskqueue_create(...)`; `taskqueue_start_threads(...)`。
7. `callout_init_mtx(&sc->heartbeat_co, ...)`，再加两个。
8. `TASK_INIT(&sc->selwake_task, ...)`，再加三个（包括 recovery_task）。
9. `TIMEOUT_TASK_INIT(sc->tq, &sc->reset_delayed_task, ...)`。
10. Softc 字段初始化。
11. `sc->bytes_read = counter_u64_alloc(M_WAITOK)`; bytes_written 同样。
12. `cbuf_init(&sc->cb, ...)`。
13. `make_dev_s(...)` 用于 cdev。
14. Sysctl 树设置。
15. `sc->is_attached = 1`（初始存储不是严格原子排序的，因为没有读取者可以在附加之前看到 softc）。

### 运行时

- 用户线程通过 open/close/read/write 进入/离开。
- Callout 周期性触发。
- 任务在入队时触发。
- Watchdog 检测停滞，入队恢复任务。
- 原子标志通过入口检查中的 `myfirst_sync_is_attached` 读取。

### Detach 序列

1. `myfirst_detach` 被调用。
2. 检查 `active_fhs > 0`；如果则返回 `EBUSY`。
3. `atomic_store_rel_int(&sc->is_attached, 0)`。
4. `cv_broadcast(&sc->data_cv)`、`cv_broadcast(&sc->room_cv)`。
5. 释放 `sc->mtx`。
6. `callout_drain` 三次。
7. `taskqueue_drain` 三次（selwake、bulk_writer、recovery）。
8. `taskqueue_drain_timeout` 一次（reset_delayed）。
9. `seldrain` 两次。
10. `taskqueue_free(sc->tq)`。
11. `sema_destroy(&sc->writers_sema)`。
12. `destroy_dev` 两次。
13. `sysctl_ctx_free`。
14. `cbuf_destroy`、`counter_u64_free` 两次。
15. `sx_destroy(&sc->stats_cache_sx)`。
16. `cv_destroy` 两次。
17. `sx_destroy(&sc->cfg_sx)`。
18. `mtx_destroy(&sc->mtx)`。

### 序列注意事项

- attach 中的每个原语初始化在 detach 中都有匹配的销毁，以逆序进行。
- 每个排空在被排空的东西被释放之前发生。
- 原子标志是 `active_fhs` 检查后的第一步，所以每个后续观察者都看到关闭。
- taskqueue 在信号量被销毁之前释放，因为任务可能（在某些扩展设计中）等待信号量。

记住这个生命周期是读者可以用第3部分做的最有用的事情。


## 参考：成本和比较

同步原语成本的简洁表格。

| 原语 | 无争用成本 | 争用成本 | 睡眠？ |
|---|---|---|---|
| `atomic_load_acq_int` | amd64 上约 1 ns | 约 1 ns（相同） | 否 |
| `atomic_fetchadd_int` | 约 10 ns | 约 100 ns | 否 |
| `mtx_lock`（无争用） | 约 20 ns | 微秒 | 慢路径睡眠 |
| `cv_wait_sig` | 不适用 | 完整调度器唤醒 | 是 |
| `sx_slock` | 约 30 ns | 微秒 | 慢路径睡眠 |
| `sx_xlock` | 约 30 ns | 微秒 | 慢路径睡眠 |
| `sx_try_upgrade` | 约 30 ns | 不适用（快速失败） | 否 |
| `sema_wait` | 约 40 ns | 唤醒延迟 | 是 |
| `sema_post` | 约 30 ns | 约 100 ns | 否 |
| `callout_reset` | 约 100 ns | 不适用 | 否 |
| `taskqueue_enqueue` | 约 50 ns | 唤醒延迟 | 否 |

上列数字是典型 FreeBSD 14.3 amd64 硬件上的数量级估计，争用列中的微秒条目对应于同类机器上的低微秒唤醒延迟。实际数字取决于缓存状态、争用和系统负载，并且可以跨 CPU 代际移动两倍或更多。使用此表决定优化哪里；运行一次需要数百纳秒的调用在每系统调用运行一次的路径上不是瓶颈。参见附录 F 以在你自己的硬件上对这些数字进行可重现基准测试。


## 参考：同步演练

作为一个完全具体的例子，这里是第15章 Stage 4 驱动程序中阻塞 `read(2)` 从系统调用入口到数据传递的完整控制流。

1. 用户调用 `read(fd, buf, 4096)`。
2. 内核的 VFS 层路由到 `myfirst_read`。
3. `myfirst_read` 调用 `devfs_get_cdevpriv` 获取 fh。
4. `myfirst_read` 调用 `myfirst_sync_is_attached(sc)`：
   - 展开为 `atomic_load_acq_int(&sc->is_attached)`。
   - 返回 1（已附加）。
5. 循环进入：`while (uio->uio_resid > 0)`。
6. `myfirst_sync_lock(sc)`：
   - 展开为 `mtx_lock(&sc->mtx)`。
   - 获取互斥锁。
7. `myfirst_wait_data(sc, ioflag, nbefore, uio)`：
   - `while (cbuf_used == 0)`：缓冲区为空。
   - 不是部分（nbefore == uio_resid）。
   - 不是 IO_NDELAY。
   - `read_timeout_ms` 为 0，所以使用 `cv_wait_sig`。
   - `cv_wait_sig(&sc->data_cv, &sc->mtx)`：
     - 释放互斥锁。
     - 线程在 cv 上睡眠。
   - 时间流逝。一个写入者（或 tick_source）调用 `cv_signal(&sc->data_cv)`。
   - 线程唤醒，`cv_wait_sig` 重新获取互斥锁，返回 0。
   - `!sc->is_attached`：假。
   - 循环重新迭代：`cbuf_used` 现在 > 0。
   - 退出循环；返回 0。
8. `myfirst_buf_read(sc, bounce, take)`：
   - 调用 `cbuf_read(&sc->cb, bounce, take)`。
   - 将数据复制到弹跳缓冲区。
   - 递增 `bytes_read` 计数器。
9. `myfirst_sync_unlock(sc)`。
10. `cv_signal(&sc->room_cv)`：如果有的话唤醒一个阻塞的写入者。
11. `selwakeup(&sc->wsel)`：唤醒任何等待写入的轮询者。
12. `uiomove(bounce, got, uio)`：从内核空间复制到用户空间。
13. 循环继续：检查 `uio->uio_resid > 0`；最终退出。
14. 返回 0 给系统调用层。
15. 系统调用层将复制的字节数返回给用户。

第15章的每个原语在演练中都可见：

- 原子标志（步骤 4）。
- 互斥锁（步骤 6、7、9）。
- 带信号处理的 cv 等待（步骤 7 的 cv_wait_sig）。
- 计数器（步骤 8 的 counter_u64_add）。
- Cv 信号（步骤 10）。
- Selwakeup（步骤 11，按规范在互斥锁外完成）。

像这样的演练是一个有用的交叉检查。驱动程序词汇中的每个原语都在读取路径上被演练。如果你能从记忆中叙述演练，同步已经内化。


## 参考：最小工作模板

为了复制和改编的方便，一个以最小形式编译并演示第15章核心添加的模板。每个元素都在章节中介绍过；模板组装它们。

```c
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/mutex.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <sys/priority.h>

struct template_softc {
        device_t           dev;
        struct mtx         mtx;
        struct sx          stats_sx;
        struct sema        admission_sema;
        struct taskqueue  *tq;
        struct task        work_task;
        int                is_attached;
        int                work_in_progress;
};

static void
template_work_task(void *arg, int pending)
{
        struct template_softc *sc = arg;
        mtx_lock(&sc->mtx);
        /* Work under mutex if needed. */
        sc->work_in_progress = 0;
        mtx_unlock(&sc->mtx);
        /* Unlocked work here. */
}

static int
template_attach(device_t dev)
{
        struct template_softc *sc = device_get_softc(dev);
        int error;

        sc->dev = dev;
        mtx_init(&sc->mtx, device_get_nameunit(dev), "template", MTX_DEF);
        sx_init(&sc->stats_sx, "template stats");
        sema_init(&sc->admission_sema, 4, "template admission");

        sc->tq = taskqueue_create("template taskq", M_WAITOK,
            taskqueue_thread_enqueue, &sc->tq);
        if (sc->tq == NULL) { error = ENOMEM; goto fail_sema; }
        error = taskqueue_start_threads(&sc->tq, 1, PWAIT,
            "%s taskq", device_get_nameunit(dev));
        if (error != 0) goto fail_tq;

        TASK_INIT(&sc->work_task, 0, template_work_task, sc);

        atomic_store_rel_int(&sc->is_attached, 1);
        return (0);

fail_tq:
        taskqueue_free(sc->tq);
fail_sema:
        sema_destroy(&sc->admission_sema);
        sx_destroy(&sc->stats_sx);
        mtx_destroy(&sc->mtx);
        return (error);
}

static int
template_detach(device_t dev)
{
        struct template_softc *sc = device_get_softc(dev);

        atomic_store_rel_int(&sc->is_attached, 0);

        taskqueue_drain(sc->tq, &sc->work_task);
        taskqueue_free(sc->tq);
        sema_destroy(&sc->admission_sema);
        sx_destroy(&sc->stats_sx);
        mtx_destroy(&sc->mtx);
        return (0);
}

/* Entry on the hot path: */
static int
template_hotpath_enter(struct template_softc *sc)
{
        sema_wait(&sc->admission_sema);
        if (!atomic_load_acq_int(&sc->is_attached)) {
                sema_post(&sc->admission_sema);
                return (ENXIO);
        }
        return (0);
}

static void
template_hotpath_leave(struct template_softc *sc)
{
        sema_post(&sc->admission_sema);
}
```

模板不是一个完整的驱动程序。它展示了章节介绍的原语的形状。一个带此模板加上其余第14章及更早模式的真实驱动程序将是一个功能完整的同步设备驱动程序。


## 参考：与 POSIX 用户空间同步的比较

许多读者从用户空间系统编程转向 FreeBSD 内核驱动程序工作。一个简短的比较阐明了映射。

| 概念 | POSIX 用户空间 | FreeBSD 内核 |
|---|---|---|
| 互斥锁 | `pthread_mutex_t` | `struct mtx` |
| 条件变量 | `pthread_cond_t` | `struct cv` |
| 读写锁 | `pthread_rwlock_t` | `struct sx` |
| 信号量 | `sem_t`（或 `sem_open`） | `struct sema` |
| 线程创建 | `pthread_create` | `kproc_create`、`kthread_add` |
| 延迟工作 | 无直接模拟；通常自己实现 | `struct task` + `struct taskqueue` |
| 周期执行 | `timer_create` + `signal` | `struct callout` |
| 原子操作 | `<stdatomic.h>` 或 `__atomic_*` | `atomic(9)` |

原语在形状上相似但在细节上不同。关键区别：

- 内核互斥锁有优先级继承；POSIX 互斥锁只有带 `PRIO_INHERIT` 属性才有。
- 内核 cv 无名称；POSIX cv 也是匿名的，所以这里对等。
- 内核 sx 锁比 pthread_rwlock 更灵活（try_upgrade、downgrade）。
- 内核信号量不是信号可中断的；POSIX 信号量（某些变体）是。
- 内核 taskqueue 无直接 POSIX 模拟；POSIX 线程池是自己实现的。
- 内核原子操作比旧的 C11 原子更全面（更多操作、更好的屏障控制）。

熟悉 POSIX 同步的读者会发现内核原语在微调后直观。主要调整是上下文意识：内核代码不能假设阻塞能力。


## 参考：工作模式目录

十个在实际驱动程序中反复出现的同步模式。每个都是第15章介绍的原语的变体。

### 模式1：带边界队列的生产者/消费者

第14章的 cbuf 加第12章的 cv 已经实现了这个。生产者写入，消费者读取，互斥锁保护队列，cv 发信号表示空到非空和满到非满转换。

### 模式2：完成信号量

提交者将信号量初始化为 0 并等待。完成处理程序发布。提交者解除阻塞。用于请求-应答模式。参见 `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c`。

### 模式3：准入控制

一个初始化为 N 的信号量。每个参与者在入口 `sema_wait`，在出口 `sema_post`。第15章 Stage 1 使用此模式。

### 模式4：升级-提升-降级读取缓存

一个带基于过期刷新的 sx 锁。读取者获取共享；过期时他们 try_upgrade、刷新、降级。第15章 Stage 2 使用此模式。

### 模式5：原子标志协调

一个由许多上下文读取、由一个写入的原子标志。读取使用 `atomic_load_acq`，写入使用 `atomic_store_rel`。第15章 Stage 4 对 `is_attached` 使用此模式。

### 模式6：最多一个状态标志

一个由锁或 CAS 保护的状态标志。"操作开始"路径设置标志；"操作结束"路径清除标志。第15章 Stage 4 对 `recovery_in_progress` 使用此模式。

### 模式7：信号可中断有界等待

带显式 EINTR/ERESTART/EWOULDBLOCK 处理的 `cv_timedwait_sig`。第15章 Stage 3 优化此模式。

### 模式8：通过 Callout 周期刷新

一个 callout 周期性调用刷新函数。刷新短暂持有锁。当刷新间隔固定时比模式4更简单。

### 模式9：通过引用计数延迟拆卸

对象有引用计数。"释放"递减；当计数达到零时实际释放发生。原子递减确保正确性。

### 模式10：跨子系统握手

两个子系统通过共享状态标志加 cv 或信号量协调。一个发信号"我这边完成了"；另一个等待。用于分阶段关闭。

知道这些模式使阅读真实驱动程序源代码更快。每个第15章原语都是这些模式中一个或多个的构建块，每个真实驱动程序选择其工作负载需要的模式。


## 参考：第3部分术语词汇表

作为最终参考，跨越第11到15章的综合词汇表。

**原子操作。** 在硬件级别以无并发干扰的可能性执行的读取、写入或读-修改-写原语。

**屏障。** 防止编译器或 CPU 将内存操作重排序越过特定点的指令。

**阻塞等待。** 一个使调用者睡眠直到条件满足或超时触发的同步操作。

**广播。** 唤醒在 cv 或信号量上阻塞的每个线程。

**Callout。** 调度在将来特定 tick 计数运行的延迟函数。

**合并。** 将多个请求折叠为单个操作（例如，任务入队到 `ta_pending`）。

**条件变量。** 允许线程睡眠直到另一个线程发信号状态改变的原语。

**上下文。** 代码路径的执行环境（系统调用、callout、任务、中断等），有自己的关于什么操作安全的规则。

**计数器。** 用于无锁统计的每 CPU 累加器原语（`counter(9)`）。

**排空。** 等待直到待处理操作不再待处理且当前未执行。

**入队。** 将工作项添加到队列。通常触发消费者的唤醒。

**Epoch。** 允许共享结构无锁读取的同步机制；写入者通过 `epoch_call` 延迟回收。

**独占锁。** 最多由一个线程持有的锁；写入者使用独占模式。

**过滤中断。** 在硬件上下文中运行且对其能做什么有严重限制的中断处理程序。

**Grouptaskqueue。** 带每 CPU 工作队列的可扩展 taskqueue 变体；由高速率网络驱动程序使用。

**可中断等待。** 可以被信号唤醒的阻塞等待，返回 EINTR 或 ERESTART。

**内存排序。** 关于 CPU 间内存访问可见性和顺序的规则。

**互斥锁。** 确保互斥的原语；一次最多一个线程在锁内。

**部分进度。** 在中断或超时触发时读取或写入上已复制的字节；按约定作为成功以短计数返回。

**优先级继承。** 高优先级等待者临时提升当前持有者优先级的调度器机制。

**释放屏障。** 存储上的屏障，确保先前访问不能在存储之后重排序。

**信号量。** 带非负计数器的原语；`post` 递增，`wait` 递减（如果为零则阻塞）。

**共享锁。** 一次由许多线程持有的锁；读取者使用共享模式。

**自旋锁。** 慢路径忙等待而不是睡眠的锁。`MTX_SPIN` 互斥锁。

**Sx 锁。** 共享/独占锁；FreeBSD 的读写锁原语。

**任务。** 带回调和上下文的延迟工作项，提交给 taskqueue。

**Taskqueue。** 由一个或多个工作线程服务待处理任务的队列。

**超时。** 等待应放弃并返回 EWOULDBLOCK 的持续时间。

**超时任务。** 通过 `taskqueue_enqueue_timeout` 调度在特定未来时刻的任务。

**升级。** 在不释放的情况下将共享 sx 锁提升为独占（`sx_try_upgrade`）。

**唤醒。** 唤醒在 cv、信号量或睡眠队列上阻塞的线程。


第3部分到此结束。第4部分从第16章*硬件基础和 Newbus* 开始。


## 参考：调试场景演练

一个现实同步错误的叙述性演练。想象你继承了一个同事编写的驱动程序。同事在度假。用户报告"在重负载下，驱动程序在 detach 时 panic，堆栈跟踪以 `selwakeup` 结束"。

本节演练你如何使用第15章工具箱诊断和修复问题。

### 步骤1：重现

首要任务：获得可靠的重现。没有它，修复只是猜测。

首先阅读错误报告了解细节。"重负载"加"在 detach 时 panic"是运行中的工作者与 detach 路径之间存在竞争的强烈提示。以 `selwakeup` 结束的堆栈跟踪表明 panic 在 selinfo 代码内部。

编写一个重现场景的最小压力脚本：

```text
#!/bin/sh
kldload ./myfirst.ko
sysctl dev.myfirst.0.tick_source_interval_ms=1
(while :; do dd if=/dev/myfirst of=/dev/null bs=1 count=100 2>/dev/null; done) &
READER=$!
sleep 5
kldunload myfirst
kill -TERM $READER
```

在循环中运行脚本直到 panic 触发。在调试内核上，这是几秒钟内就会 panic 的那种错误。在生产内核上，可能需要更长时间。

### 步骤2：捕获堆栈

调试内核启用了 `KDB`。panic 后，你进入内核调试器。命令：

- `bt`：panicking 线程的堆栈跟踪。
- `show locks`：panic 时刻持有的锁。
- `show witness`：WITNESS 状态。

记录一切。在生产崩溃时，收集内核转储；用 `crashinfo` 获取并用 `kgdb` 离线分析。

此错误的典型堆栈：

```text
panic: selwakeup: freed selinfo
kdb_enter
vpanic
panic
selwakeup
myfirst_selwake_task
taskqueue_run_locked
taskqueue_thread_loop
fork_exit
```

Panic 在 `selwakeup` 中，从 `myfirst_selwake_task` 调用，从 taskqueue 工作者调用。`selwakeup` 的断言说给它的 selinfo 已被释放。

### 步骤3：识别竞争

错误在堆栈中可见。任务回调调用了 `selwakeup(&sc->rsel)`，但 `sc->rsel` 已被排空或释放。两个可能原因：

A. Detach 路径在排空任务之前 `seldrain` 了 selinfo。然后任务在 `seldrain` 之后对已释放状态触发。
B. Detach 路径在排空任务之前释放了 softc（包含 rsel）。然后任务对已释放内存触发。

两者都是排序违规。查看驱动程序中的实际 detach 代码：

```c
/* Current order in the buggy driver: */
seldrain(&sc->rsel);
seldrain(&sc->wsel);
taskqueue_drain(sc->tq, &sc->selwake_task);
```

` seldrain` 在 `taskqueue_drain` 之前。这正好是原因 A。修复是交换顺序。

### 步骤4：应用修复

将 detach 更改为：

```c
taskqueue_drain(sc->tq, &sc->selwake_task);
seldrain(&sc->rsel);
seldrain(&sc->wsel);
```

任务排空现在保证任务不在运行。只有那时 `seldrain` 才继续。

但等等。任务仍可能从运行中的 callout 入队。检查：callout 是否在任务排空之前被排空？再次阅读代码：

```c
/* Full order after fix: */
MYFIRST_CO_DRAIN(&sc->heartbeat_co);
MYFIRST_CO_DRAIN(&sc->watchdog_co);
MYFIRST_CO_DRAIN(&sc->tick_source_co);
taskqueue_drain(sc->tq, &sc->selwake_task);
seldrain(&sc->rsel);
seldrain(&sc->wsel);
```

Callout 被排空，然后任务被排空，然后 sel 被排空。这是正确的顺序。

### 步骤5：验证

使用修复再次运行重现脚本。Panic 应不再触发。运行 100 次以获得信心。在调试内核上，竞争会迅速暴露；100 次干净运行是修复正确的有力证据。

添加一个演练此特定场景的回归测试。测试应该是驱动程序测试套件的一部分，这样错误不会返回。

### 步骤6：文档

在 `LOCKING.md` 中用注释更新，解释为什么顺序是这样。一个考虑因某种原因重新排序排空的未来维护者会看到注释并重新考虑。

### 要点

- 错误在堆栈跟踪中可见；技能是识别跟踪意味着什么。
- 修复是一行（重新排序两个调用）；诊断才是工作。
- 调试内核使错误可重现；没有它，错误将是间歇性和神秘的。
- 测试套件防止回归；没有它，未来的重构可能静默地重新引入错误。

第14章已经教授了这个特定的排序规则。一个没有内化第14章的同事编写的生产驱动程序很容易有这个错误。章节的规范和第15章测试章一起是让你的代码远离这个错误的原因。

这是一个简短的、人为的场景。真实错误更微妙。相同的方法论适用：重现、捕获、识别、修复、验证、文档。第3部分的原语和规范是"识别"步骤的工具箱，通常是最难的。


## 参考：阅读真实驱动程序的同步

作为一个具体的、考试式的练习：在 `/usr/src/sys/dev/` 中选择一个以太网驱动程序并遍历其同步词汇。本节简要遍历 `/usr/src/sys/dev/ale/if_ale.c` 作为练习模板。

`ale(4)` 驱动程序是 Atheros AR8121/AR8113/AR8114 的 10/100/1000 以太网驱动程序。它不大（几千行）且有清晰的结构。

### 它使用的原语

打开文件并搜索原语。

```text
$ grep -c 'mtx_init\|mtx_lock\|mtx_unlock' /usr/src/sys/dev/ale/if_ale.c
```

驱动程序使用一个互斥锁（`sc->ale_mtx`）。通过 `ALE_LOCK(sc)` 和 `ALE_UNLOCK(sc)` 宏统一获取。

它使用 callout：`sc->ale_tick_ch` 用于周期性链路状态轮询。

它使用 taskqueue：`sc->ale_tq`，用 `taskqueue_create_fast` 创建，用 `taskqueue_start_threads(..., 1, PI_NET, ...)` 启动。使用快速变体（自旋互斥锁支持）是因为中断过滤器入队到它上面。

它在队列上使用一个任务：`sc->ale_int_task` 用于中断后处理。

它不使用 `sema` 或 `sx`。驱动程序的不变量适合单个互斥锁。

它使用原子操作：几个对硬件寄存器的 `atomic_set_32` 和 `atomic_clear_32` 调用（通过 `CSR_WRITE_4` 和类似）。这些用于硬件寄存器操作，不是驱动程序级协调。

### 它演示的模式

**过滤器加任务中断拆分。** `ale_intr` 是过滤器，在硬件处屏蔽 IRQ 并入队任务。`ale_int_task` 是任务，在线程上下文中处理中断工作。

**用于链路轮询的 Callout。** `ale_tick` 是一个重新武装自己的周期性 callout，用于链路状态轮询。

**标准 detach 排序。** `ale_detach` 排空 callout、排空任务、释放 taskqueue、销毁互斥锁。与 `myfirst` 相同的模式。

### 它不演示的内容

- 没有 sx 锁。配置由单个互斥锁保护。
- 没有信号量。没有有界准入。
- 没有 epoch。驱动程序不从异常上下文直接接触网络状态。
- 没有 `sx_try_upgrade`。没有读多缓存。

### 要点

`ale(4)` 驱动程序使用其工作负载需要的第3部分原语子集。需要 sx 锁或信号量的驱动程序会添加它；`ale(4)` 不需要，所以它不添加。

像这样阅读一个真实驱动程序比阅读章节两次更有价值。选择一个驱动程序，阅读 30 分钟，写下它使用什么原语以及为什么。

用更大的驱动程序做同样的练习（`bge(4)`、`iwm(4)`、`mlx5(4)` 是好候选）。注意词汇如何扩展。有更多状态的驱动程序需要更多原语；有更简单状态的驱动程序使用更少。

第15章结束时的 `myfirst` 驱动程序使用第3部分引入的每个原语。大多数真实驱动程序使用子集。两者都有效；选择取决于工作负载。


## 参考：完整的 `myfirst_sync.h` 设计

`examples/part-03/ch15-more-synchronization/stage4-final/` 下的配套源代码包含完整的 `myfirst_sync.h`。作为参考，这是一个可用作模板的完整版本。

```c
/*
 * Copyright (c) 2026 Edson Brandi
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * myfirst_sync.h: the named synchronisation vocabulary of the
 * myfirst driver.
 *
 * Every primitive the driver uses has a wrapper here. The main
 * source calls these wrappers; the wrappers are inlined away, so
 * the runtime cost is zero. The benefit is a readable,
 * centralised, and easily-changeable synchronisation strategy.
 *
 * This file depends on the definition of `struct myfirst_softc`
 * in myfirst.c, which must be included before this header.
 */

#ifndef MYFIRST_SYNC_H
#define MYFIRST_SYNC_H

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/sema.h>
#include <sys/condvar.h>

/*
 * Data-path mutex. Single per-softc. Protects cbuf, counters, and
 * most of the per-softc state.
 */
static __inline void
myfirst_sync_lock(struct myfirst_softc *sc)
{
        mtx_lock(&sc->mtx);
}

static __inline void
myfirst_sync_unlock(struct myfirst_softc *sc)
{
        mtx_unlock(&sc->mtx);
}

static __inline void
myfirst_sync_assert_locked(struct myfirst_softc *sc)
{
        mtx_assert(&sc->mtx, MA_OWNED);
}

/*
 * Configuration sx. Protects the myfirst_config structure. Read
 * paths take shared; sysctl writers take exclusive.
 */
static __inline void
myfirst_sync_cfg_read_begin(struct myfirst_softc *sc)
{
        sx_slock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_read_end(struct myfirst_softc *sc)
{
        sx_sunlock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_write_begin(struct myfirst_softc *sc)
{
        sx_xlock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_write_end(struct myfirst_softc *sc)
{
        sx_xunlock(&sc->cfg_sx);
}

/*
 * Writer-cap semaphore. Caps concurrent writers at
 * sc->writers_limit. Returns 0 on success, EAGAIN if O_NONBLOCK
 * and semaphore is exhausted, ENXIO if detach happened while
 * blocked.
 */
static __inline int
myfirst_sync_writer_enter(struct myfirst_softc *sc, int ioflag)
{
        if (ioflag & IO_NDELAY) {
                if (!sema_trywait(&sc->writers_sema)) {
                        mtx_lock(&sc->mtx);
                        sc->writers_trywait_failures++;
                        mtx_unlock(&sc->mtx);
                        return (EAGAIN);
                }
        } else {
                sema_wait(&sc->writers_sema);
        }
        if (!atomic_load_acq_int(&sc->is_attached)) {
                sema_post(&sc->writers_sema);
                return (ENXIO);
        }
        return (0);
}

static __inline void
myfirst_sync_writer_leave(struct myfirst_softc *sc)
{
        sema_post(&sc->writers_sema);
}

/*
 * Stats cache sx. Protects a small cached statistic.
 */
static __inline void
myfirst_sync_stats_cache_read_begin(struct myfirst_softc *sc)
{
        sx_slock(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_read_end(struct myfirst_softc *sc)
{
        sx_sunlock(&sc->stats_cache_sx);
}

static __inline int
myfirst_sync_stats_cache_try_promote(struct myfirst_softc *sc)
{
        return (sx_try_upgrade(&sc->stats_cache_sx));
}

static __inline void
myfirst_sync_stats_cache_downgrade(struct myfirst_softc *sc)
{
        sx_downgrade(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_write_begin(struct myfirst_softc *sc)
{
        sx_xlock(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_write_end(struct myfirst_softc *sc)
{
        sx_xunlock(&sc->stats_cache_sx);
}

/*
 * Attach-flag atomic operations. Every context that needs to
 * check "are we still attached?" uses these.
 */
static __inline int
myfirst_sync_is_attached(struct myfirst_softc *sc)
{
        return (atomic_load_acq_int(&sc->is_attached));
}

static __inline void
myfirst_sync_mark_detaching(struct myfirst_softc *sc)
{
        atomic_store_rel_int(&sc->is_attached, 0);
}

static __inline void
myfirst_sync_mark_attached(struct myfirst_softc *sc)
{
        atomic_store_rel_int(&sc->is_attached, 1);
}

#endif /* MYFIRST_SYNC_H */
```

文件不到 200 行，包括注释。它命名每个原语操作。它增加零运行时开销。它是未来维护者查看以理解或更改同步策略的唯一位置。


## 参考：扩展实验："破坏原语"练习

一个可选的拓展第15章材料的实验。对于第15章引入的每个原语，故意破坏它并观察失败。

这在教学上很有价值，因为看到失败模式使正确用法具体化。

### 破坏写入者上限

在 Stage 1 中，移除写入路径中的一个 `sema_post`（例如，在错误返回路径上）。重新构建。运行写入者上限测试。`writers_sema_value` sysctl 应该随时间向下漂移且永不恢复。最终所有写入者阻塞。这演示了为什么 post 必须在每条路径上。

恢复 post。验证漂移停止且信号量在负载下保持平衡。

### 破坏 Sx 升级

在 Stage 2 中，移除升级回退后的重新检查：

```c
sx_sunlock(&sc->stats_cache_sx);
sx_xlock(&sc->stats_cache_sx);
/* re-check removed */
myfirst_stats_cache_refresh(sc);
sx_downgrade(&sc->stats_cache_sx);
```

重新构建。在重读取者负载下，刷新快速连续发生多次，因为多个读取者竞争进入回退路径。刷新计数器增长速度远快于每秒一次。

恢复重新检查。验证计数器稳定回每秒一次刷新。

### 破坏部分进度处理

在 Stage 3 中，移除 EINTR 路径上的部分进度检查：

```c
case EINTR:
case ERESTART:
        return (error);  /* Partial check removed. */
```

重新构建。运行信号处理实验。部分完成读取期间的 SIGINT 现在返回 EINTR 而不是部分字节计数。期望 UNIX 约定的用户空间代码会感到惊讶。

恢复检查。验证部分进度再次工作。

### 破坏原子读取

在 Stage 4 中，在读取路径入口检查中用普通读取 `sc->is_attached` 替换 `atomic_load_acq_int(&sc->is_attached)`。在 x86 上这仍然工作（强内存模型）。在 arm64 上它可能偶尔错过 detach，产生 ENXIO-或非 ENXIO 竞争。

如果你没有 arm64 硬件，这个很难在实验上演示。在智力上理解它并继续。

恢复原子。规范相同，无论测试结果如何。

### 破坏 Detach 排序

交换 Stage 4 detach 中 `seldrain` 和 `taskqueue_drain` 的顺序（如前面的调试场景）。运行带 detach 循环的压力测试。观察最终的 panic。

恢复正确顺序。验证稳定性。

### 破坏信号量销毁生命周期

在 taskqueue 完全排空之前早调用 `sema_destroy(&sc->writers_sema)`。在调试内核上，当线程仍在 `sema_wait` 内时，这会以 "sema: waiters" panic。KASSERT 触发。

恢复正确顺序。销毁在所有等待者排空后发生。

### 为什么这很重要

故意破坏代码是不舒服的。它也是内化为什么正确代码以这种方式编写的最快方式。每个第15章原语都有失败模式；实时看到它们使正确用法难以忘怀。

在为每个原语运行破坏并观察练习后，章节的材料会感觉坚实。你不仅知道做什么，还知道为什么，以及如果跳过步骤会发生什么。


## 参考：何时拆分驱动程序

一个属于这里而不是特定节的元观察。

第3部分已经将 `myfirst` 驱动程序开发到中等大小：主源代码约 1200 行，加上 `myfirst_sync.h` 头文件，加上 cbuf。对于真实驱动程序来说这很小。真实驱动程序范围从 2000 行（简单设备支持）到 30000 行或更多（带卸载支持的网络驱动程序）。

驱动程序在多大小时值得拆分成多个源文件？

一些启发式规则。

- **1000 行以下**：一个文件。多个文件的开销超过可读性好处。
- **1000 到 5000 行**：一个文件仍然可以。在文件中使用清晰的节标记。
- **5000 到 15000 行**：两个或三个文件。典型拆分：主 attach/detach 逻辑在 `foo.c`，专用子系统逻辑（例如，环形缓冲区管理器）在 `foo_ring.c`，硬件寄存器定义在 `foo_reg.h`。
- **超过 15000 行**：需要模块化设计。一个共享结构的头文件；几个子系统的实现文件；一个顶层 `foo.c` 将它们绑在一起。

第15章 Stage 4 的 `myfirst` 驱动程序作为单个文件加同步头文件很舒适。随着后面章节添加硬件特定逻辑，自然的拆分会出现：寄存器定义用 `myfirst_reg.h`，中断相关代码用 `myfirst_intr.c`，数据路径用 `myfirst_io.c`。当硬件故事证明它们合理时，这些拆分将在第4部分发生。

经验法则：当文件超过你一次能在脑中容纳的大小时拆分。对于大多数读者，这大约是 2000 到 5000 行。如果子系统边界自然，更早拆分；如果代码如此交错以至于拆分会感觉牵强，更晚拆分。


## 参考：第3部分最终总结

第3部分是五个原语及其组合的演练。一个最终总结框架了已完成的工作。

**第11章**将并发引入驱动程序。一个用户变成多个用户。互斥锁使这安全。

**第12章**引入阻塞。读取者和写入者可以等待状态改变。条件变量使等待高效；sx 锁使配置访问可扩展。

**第13章**引入时间。Callout 让驱动程序在选定的时刻自主行动。感知锁的 callout 和 detach 时排空使定时器安全。

**第14章**引入延迟工作。任务让 callout 和其他边缘上下文将工作交给实际上可以完成工作的线程。私有 taskqueue 和合并使原语高效。

**第15章**引入剩余的协调原语。信号量限制并发。优化的 sx 模式启用读多缓存。带部分进度的信号可中断等待保留 UNIX 约定。原子操作使跨上下文标志廉价。封装使整个词汇可读。

总之，五章构建了一个有完整内部同步故事的驱动程序。`0.9-coordination` 的驱动程序没有缺失同步功能；它关心的每个不变量都有命名原语、包装头文件中的命名操作、`LOCKING.md` 中的命名节和压力套件中的测试。

第4部分添加硬件故事。同步故事保留。


## 参考：第15章交付物检查清单

在关闭第15章之前，确认所有交付物就位。

### 章节内容

- [ ] `content/chapters/part3/chapter-15.md` 存在。
- [ ] 第1节到第8节已编写。
- [ ] 附加主题节已编写。
- [ ] 动手实验已编写。
- [ ] 挑战练习已编写。
- [ ] 故障排除参考已编写。
- [ ] 第3部分总结已编写。
- [ ] 通往第16章的桥梁已编写。

### 示例

- [ ] `examples/part-03/ch15-more-synchronization/` 目录存在。
- [ ] `stage1-writers-sema/` 有工作驱动程序。
- [ ] `stage2-stats-cache/` 有工作驱动程序。
- [ ] `stage3-interruptible/` 有工作驱动程序。
- [ ] `stage4-final/` 有整合的驱动程序。
- [ ] 每个阶段有 `Makefile`。
- [ ] `stage4-final/` 有 `myfirst_sync.h`。
- [ ] `labs/` 有测试程序和脚本。
- [ ] `README.md` 描述每个阶段。
- [ ] `LOCKING.md` 有更新的同步映射。

### 文档

- [ ] 主源代码有总结第15章添加的文件顶部注释。
- [ ] 每个新 sysctl 有描述字符串。
- [ ] 每个新结构字段有注释。
- [ ] `LOCKING.md` 节与驱动程序匹配。

### 测试

- [ ] Stage 4 驱动程序干净构建。
- [ ] Stage 4 驱动程序通过 WITNESS。
- [ ] 第11-14章回归测试仍然通过。
- [ ] 第15章特定测试通过。
- [ ] 负载下 detach 干净。

通过此检查清单的驱动程序和章节完成。


## 参考：实验邀请

在关闭之前，一个最终邀请。

第15章驱动程序是学习载体，不是发布产品。它使用的每个原语都是真实的；它演示的每个技术都用于真实驱动程序。但驱动程序本身是人为设计的，以便在一个地方演练完整的原语范围。真实驱动程序通常使用子集。

当你关闭第3部分时，考虑在章节工作示例之外实验。

- 添加第二种准入控制：一个同时限制并发读取者和写入者的信号量。它改善还是损害系统？为什么？
- 添加一个使单个写操作（不是整个驱动程序）超时的 watchdog。用超时任务实现它。你遇到什么边缘情况？
- 将配置 sx 转换为一组原子字段。用 DTrace 测量性能差异。你会发布哪个设计？为什么？
- 用 C 编写一个用户态测试工具，以 shell 无法的方式演练驱动程序。你选择什么原语？
- 阅读 `/usr/src/sys/dev/` 中的真实驱动程序并识别它做出的单个同步决策。你同意这个决策吗？你会选择什么替代方案，为什么？

每个实验是一两天的工作。每个教授的比一章文本更多。`myfirst` 驱动程序是一个实验室；FreeBSD 源代码是一个图书馆；你自己的好奇心是教学大纲。

第3部分已经教你原语。其余是练习。祝第4部分好运。


## 参考：cv_signal 与 cv_broadcast，精确说明

阅读或编写驱动程序代码时的一个反复问题：信号应该唤醒一个等待者（cv_signal）还是所有等待者（cv_broadcast）？答案并不总是明显的；本参考深入探讨。

### 语义差异

`cv_signal(&cv)` 唤醒当前阻塞在 cv 上的最多一个线程。如果多个线程在等待，正好一个唤醒；内核选择哪个（通常是 FIFO，但这不被 API 保证）。

`cv_broadcast(&cv)` 唤醒当前阻塞在 cv 上的所有线程。每个阻塞线程唤醒并重新争用互斥锁。

### 何时 `cv_signal` 正确

两个条件必须同时成立才能使 `cv_signal` 安全。

**任何一个等待者都能满足状态改变。** 如果你发出信号是因为有界缓冲区中的一个槽位变得可用，而任何等待者都可以取该槽位，信号是适当的。单个唤醒足够。

**所有等待者等价。** 如果每个等待者运行相同的谓词并且会对唤醒做出相同的响应，信号是适当的。只唤醒一个避免了唤醒所有然后除一个外立即重新睡眠的惊群效应。

经典例子：一个生产者/消费者，新鲜生产了一个项目。唤醒一个消费者足够；唤醒所有会唤醒许多消费者，然后他们会看到空队列并重新睡眠。

### 何时 `cv_broadcast` 正确

几个特定情况使广播成为正确选择。

**多个等待者可能成功。** 如果状态改变解除阻塞多于一个等待者（例如，"有界缓冲区从满变为 10 个槽位空闲"），广播唤醒它们全部，每个都可以尝试。只信号一个会使其他等待者保持阻塞，尽管进展是可能的。

**不同的等待者有不同的谓词。** 如果一些等待者在等待"字节 > 0"，另一些在等待"字节 > 100"，信号一个可能唤醒谓词不满足的等待者，而另一个谓词满足的等待者保持睡眠。广播确保每个等待者重新评估自己的谓词。

**关闭或状态失效。** 当驱动程序 detach 时，每个等待者必须看到改变并退出。`cv_broadcast` 是必需的，因为每个等待者都必须返回，不只是一个。

第12-15章驱动程序在 detach 时使用 `cv_broadcast`（`cv_broadcast(&sc->data_cv)`、`cv_broadcast(&sc->room_cv)`）正是这个原因。它在正常缓冲区状态转换上使用 `cv_signal`，因为每个转换最多高效地解除阻塞一个等待者。

### 一个微妙的情况：重置 Sysctl

第12章添加了一个清除 cbuf 的重置 sysctl。重置后，缓冲区为空且有完整空间。哪个唤醒正确？

```c
cv_broadcast(&sc->room_cv);   /* Room is now fully available. */
```

驱动程序使用 `cv_broadcast`。为什么不是信号？因为重置解除阻塞了可能许多都在等待空间的所有写入者。唤醒它们全部让它们全部重新检查。信号只会唤醒一个；其他会保持阻塞直到写入路径稍后按字节发出信号。

这是"多个等待者可能成功"的情况。广播正确。

### 成本考虑

`cv_broadcast` 比 `cv_signal` 更昂贵。每个唤醒线程让调度器工作，每个唤醒并立即重新睡眠的线程付出上下文切换开销。对于有多个等待者的 cv，广播可能昂贵。

对于典型有一两个等待者的 cv，成本差异可忽略。使用语义上正确的那个。

### 经验法则

- **一个字节到达后的阻塞读取唤醒**：`cv_signal`。一个字节最多解除阻塞一个读取者。
- **字节排空后的阻塞写入唤醒**：取决于排空了多少字节。如果排空一个字节，信号可以。如果缓冲区被重置清空，广播。
- **Detach**：总是 `cv_broadcast`。每个等待者必须退出。
- **重置或可能解除阻塞多个的状态失效**：`cv_broadcast`。
- **正常增量状态改变**：`cv_signal`。

如有疑问，`cv_broadcast` 正确（只是更昂贵）。当你能证明它足够时优先使用信号。


## 参考：你编写自己信号量的罕见情况

一个思维实验。如果 `sema(9)` 不存在，你将如何仅用互斥锁和 cv 实现计数信号量？

```c
struct my_sema {
        struct mtx      mtx;
        struct cv       cv;
        int             value;
};

static void
my_sema_init(struct my_sema *s, int value, const char *name)
{
        mtx_init(&s->mtx, name, NULL, MTX_DEF);
        cv_init(&s->cv, name);
        s->value = value;
}

static void
my_sema_destroy(struct my_sema *s)
{
        mtx_destroy(&s->mtx);
        cv_destroy(&s->cv);
}

static void
my_sema_wait(struct my_sema *s)
{
        mtx_lock(&s->mtx);
        while (s->value == 0)
                cv_wait(&s->cv, &s->mtx);
        s->value--;
        mtx_unlock(&s->mtx);
}

static void
my_sema_post(struct my_sema *s)
{
        mtx_lock(&s->mtx);
        s->value++;
        cv_signal(&s->cv);
        mtx_unlock(&s->mtx);
}
```

紧凑、正确，在简单情况下功能上与 `sema(9)` 相同。阅读这个使 `sema(9)` 内部在做什么变得清晰：正是这样，包装在 API 中。

如果这么简单为什么 `sema(9)` 存在？几个原因：

- 它将代码从每个否则会重新发明它的驱动程序中分解出来。
- 它提供一个有跟踪支持的、有文档的、测试过的原语。
- 它优化 post 以在没有等待者时避免 cv_signal。
- 它为代码审查提供一致的词汇。

同样的论点适用于每个内核原语。你可以滚动自己的互斥锁、cv、sx、taskqueue、callout。你不会因为内核的原语经过更好的测试、更好的文档记录、并被社区更好地理解。使用它们。

例外是不在内核中的原语。如果你的驱动程序需要一个没有内核原语提供的特定同步习语，实现它是合理的。仔细记录它。


## 参考：关于内核同步的临别观察

一个跨越第3部分的观察。

每个内核同步原语都由更简单的原语构建。底部是自旋锁（技术上，内存位置上的比较并交换，加屏障）。自旋锁上是互斥锁（自旋锁加优先级继承加睡眠）。互斥锁上是条件变量（睡眠队列加互斥锁交接）。条件变量上是 sx 锁（cv 加读取者计数器）。Sx 锁上是信号量（cv 加计数器）。信号量上是更高级的原语（taskqueue、gtaskqueue、epoch）。

每一层添加特定能力并隐藏下面的复杂性。当你调用 `sema_wait` 时，你不考虑它内部的 cv、cv 内部的互斥锁、互斥锁内部的自旋锁、自旋锁内部的 CAS。抽象工作。

这种分层的回报是你可以一次推理一层。知道分层的回报是当一层失败时，你可以下降到下面一层并调试。

第3部分按顺序介绍了每层的原语。第4部分使用它们。如果第4部分的错误使你困惑，诊断可能需要下降：从"taskqueue 卡住"到"任务回调在互斥锁上阻塞"到"互斥锁被等待 cv 的线程持有"到"cv 在等待一个永远不会发生的状态改变，因为不同的错误"。这种下降的工具就是你现在知道的原语。

那是第3部分真正的回报。不是一个特定的驱动程序模式，虽然那很有价值。不是一个特定的 API 调用集合，虽然那些是必要的。真正的回报是一个随问题复杂性扩展的同步心智模型。那个模型是带你通过第4部分和本书其余部分的东西。


第15章完成。第3部分完成。

继续到第16章。

## 参考：关于测试规范的最终说明

第3部分的每一章都以测试结束。规范是一致的：添加一个原语，重构驱动程序，编写一个演练它的测试，运行整个回归套件，更新 `LOCKING.md`。

这个规范是将一系列章节转化为可维护的代码体的原因。没有它，驱动程序将是各自工作但组合时崩溃的功能拼凑。有了它，每章的添加与之前的组合。

在第4部分保持规范。硬件引入新原语（中断处理程序、资源分配、DMA 标签）和新失败模式（硬件级竞争、DMA 损坏、寄存器排序意外）。每个添加值得有自己的测试、自己的文档条目、自己到现有回归套件的集成。

规范的成本是每章少量额外工作。好处是驱动程序，无论处于什么开发阶段，总是可发布的。你可以把它交给同事，它会工作。你可以把它放六个月，回来，仍然理解它做什么。你可以再添加一个功能，而不必担心不相关的东西会破坏。

那是第3部分的最终回报。不只是原语；不只是模式；一个工作的规范。

