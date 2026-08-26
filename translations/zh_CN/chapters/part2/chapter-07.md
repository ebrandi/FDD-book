---
title: "编写你的第一个驱动程序"
description: "一个动手实践指南，构建一个具有清晰生命周期规范的最小 FreeBSD 驱动程序。"
partNumber: 2
partName: "构建你的第一个驱动程序"
chapter: 7
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "AI辅助翻译为简体中文"
language: "zh-CN"
estimatedReadTime: 600
---

# 编写你的第一个驱动程序

## 读者指南与学习成果

欢迎来到第二部分。如果第一部分是你的基础——学习环境、语言和架构——那么**第二部分就是你开始构建的地方**。本章标志着你停止阅读关于驱动程序的内容，开始亲手编写一个。

但让我们先明确我们要构建什么，同样重要的是，我们暂时不构建什么。本章遵循**规范优先**的方法：你将编写一个最小的驱动程序，它能干净地挂载、正确地记录日志、创建一个简单的用户界面，并且分离时没有资源泄漏。没有花哨的 I/O，没有硬件寄存器访问，没有中断处理。这些内容将在以后出现，等你将规范变成习惯之后。

### 你将构建什么

当你进入下一章时，你将拥有一个名为 `myfirst` 的可工作 FreeBSD 14.3 驱动程序，它能够：

- **作为伪设备挂载**，使用 Newbus 框架
- **创建 `/dev/myfirst0` 节点**（占位符，只读预览）
- **暴露一个只读 sysctl**，显示基本运行时状态
- **使用 `device_printf()` 干净地记录生命周期事件**
- **使用单标签展开模式处理错误**
- **干净地分离**，没有资源泄漏或悬空指针

这个驱动程序暂时不会做任何令人兴奋的事情。它不会从硬件读取，不会处理中断，也不会处理数据包或数据块。它*会*做的是演示**生命周期规范**——这是每个生产驱动程序所依赖的基础。

### 你暂时**不会**构建什么

本章有意推迟了几个重要主题，以便你在增加复杂性之前掌握结构：

- **完整 I/O 语义**：`read(2)` 和 `write(2)` 将被占位。真正的读写路径在第 9 章出现，在第 8 章介绍设备文件策略和用户空间可见性之后。
- **硬件交互**：没有寄存器访问，没有 DMA，没有中断。这些在**第四部分**中介绍，当你有了坚实的基础之后。
- **PCI/USB/ACPI 细节**：本章使用伪设备（无总线依赖）。特定总线的挂载模式出现在第四部分（PCI、中断、DMA）和第六部分（USB、存储、网络）。
- **锁定和并发**：你会在 softc 中看到一个互斥锁，但我们不会练习复杂的并发路径，直到**第三部分**。
- **高级 sysctl**：目前只有一个只读节点。更大的 sysctl 树、写入处理程序和可调参数在第五部分再回来。

**为什么这很重要**：试图一次学习所有东西会导致困惑。通过保持范围狭窄，你将在添加下一层之前理解每个部分存在的*原因*。

### 预计时间投入

- **仅阅读**：2-3 小时吸收概念和代码演示
- **阅读 + 输入示例**：4-5 小时，如果你自己输入驱动程序代码
- **阅读 + 四个实验**：5-7 小时，包括构建、测试和验证周期
- **可选挑战**：为深入练习增加 2-3 小时

**建议节奏**：分两到三次会话完成。第一次会话学习脚手架和 Newbus 基础，第二次学习日志和错误处理，第三次进行实验和冒烟测试。

### 前提条件

开始之前，确保你有：

- **FreeBSD 14.3** 在你的实验环境中运行（虚拟机或裸机）
- **第 1-6 章已完成**（特别是第 2 章的实验设置和第 6 章的剖析导览）
- **已安装 `/usr/src`**，与你运行的内核版本匹配的 FreeBSD 14.3 源代码
- **第 4 章的基础 C 语言能力**
- **第 5 章的内核编程意识**

检查你的内核版本：

```bash
% freebsd-version -k
14.3-RELEASE
```

如果不匹配，请重新查看第 2 章的设置指南。

### 学习成果

完成本章后，你将能够：

- 从零开始搭建最小的 FreeBSD 驱动程序
- 实现并解释 probe/attach/detach 生命周期方法
- 安全地定义和使用驱动程序 softc 结构
- 使用 `make_dev_s()` 创建和销毁 `/dev` 节点
- 添加只读 sysctl 以实现可观测性
- 使用规范的展开处理错误（单 fail: 标签模式）
- 可靠地构建、加载、测试和卸载你的驱动程序
- 识别并修复常见初学者错误（资源泄漏、空指针解引用、缺失清理）

### 成功标准

当你知道成功时，你将看到：

- `kldload ./myfirst.ko` 无错误完成
- `dmesg -a` 显示你的挂载消息
- `ls -l /dev/myfirst0` 显示你的设备节点
- `sysctl dev.myfirst.0` 返回你的驱动程序状态
- `kldunload myfirst` 干净地清理，没有泄漏或崩溃
- 你可以可靠地重复加载/卸载循环
- 模拟的挂载失败干净地展开（负向路径测试）

### 本章的位置

你正在进入**第二部分 - 构建你的第一个驱动程序**，从理论到实践的桥梁：

- **第 7 章（本章）**：搭建具有干净 attach/detach 的最小驱动程序
- **第 8 章**：连接真正的 `open()`、`close()` 和设备文件语义
- **第 9 章**：实现基本的 `read()` 和 `write()` 路径
- **第 10 章**：处理缓冲、阻塞和 poll/select

每一章都在前一章的基础上添加一层功能。

### 关于 "Hello World" 与 "Hello Production" 的说明

你可能以前见过 "hello world" 内核模块：一个打印消息的 `MOD_LOAD` 事件处理程序。这对于检查构建系统是否工作很好，但它不是驱动程序。它不会挂载到任何东西上，不会创建用户界面，几乎不教授任何关于生命周期规范的内容。

本章的 `myfirst` 驱动程序不同。它仍然是最小的，但遵循你在每个生产 FreeBSD 驱动程序中看到的模式：

- 注册到 Newbus
- 正确实现 probe/attach/detach
- 管理资源（即使是微不足道的）
- 可靠地清理

把 `myfirst` 看作 **hello production**，而不是 hello world。从玩具到工具的跨越从这里开始。

### 如何使用本章

1. **按顺序阅读**：每一节都建立在前一节的基础上。不要跳过。
2. **自己输入代码**：肌肉记忆很重要。复制代码片段可以，但输入能巩固模式。
3. **完成实验**：它们是检查点，不是可选内容。每个实验在前进之前验证理解。
4. **使用总结检查清单**：在宣布胜利之前，运行冒烟测试检查清单（在本章末尾附近）。它能捕捉常见错误。
5. **保留日志**：记录什么有效、什么失败以及你学到了什么。未来的你会感谢你。

### 关于错误的说明

你*会*遇到错误。你会忘记初始化指针，你会跳过清理步骤，你会拼写错误的函数名。这是预期的，也是**健康的**。每个错误都是练习调试、阅读日志和理解因果关系的机会。

当出现问题时：

- 阅读完整的错误消息。FreeBSD 的内核消息很详细。
- 检查 `dmesg -a` 查看生命周期事件。
- 使用故障排除决策树（本章后面的部分）。
- 重新查看相关部分，将你的代码与示例进行比较。

不要匆忙略过错误。它们是教学时刻。

### 让我们开始

你已经完成了基础。你在第 6 章浏览了真正的驱动程序。现在是时候**构建你自己的**了。让我们从项目脚手架开始。



## 项目脚手架（KLD 骨架）

每个驱动程序都从一个脚手架开始，一个能够编译、加载和卸载但几乎不做任何事情的裸结构。把这想象成房子的框架：墙壁、门窗和家具以后再来。现在，我们正在构建支撑一切的基础和骨架。

在本节中，你将从头开始创建一个最小的 FreeBSD 14.3 驱动程序项目。到最后，你将拥有：

- 干净的目录结构
- 简单的 Makefile
- 包含绝对最小生命周期代码的 `.c` 文件
- 能产生 `myfirst.ko` 模块的工作构建

这个脚手架是**故意无聊的**。它还不会创建 `/dev` 节点，不会实现 sysctl，也不会做任何真正的工作。但它会教你构建周期、基本结构和干净进退的规范。掌握这些，其他一切都只是添加层。

### 目录布局

让我们为你的驱动程序创建一个工作空间。FreeBSD 源代码树中的约定是将驱动程序保存在 `/usr/src/sys/dev/<drivername>` 下，但对于你的第一个驱动程序，我们将在你的主目录中工作。这使你的实验隔离，并使重建变得简单。

创建结构：

```bash
% mkdir -p ~/drivers/myfirst
% cd ~/drivers/myfirst
```

你的工作目录将包含：

```text
~/drivers/myfirst/
├── myfirst.c      # Driver source code
└── Makefile       # Build instructions
```

就是这样。FreeBSD 的内核模块构建系统（`bsd.kmod.mk`）为你处理所有复杂性（编译器标志、包含路径、链接等）。

**为什么这样组织？**

- **单一目录**：将所有内容放在一起，易于清理（`rm -rf ~/drivers/myfirst`）。
- **以驱动程序命名**：当你有多个项目时，你知道 `~/drivers/myfirst` 包含什么。
- **匹配树模式**：`/usr/src/sys/dev/` 中真正的 FreeBSD 驱动程序遵循相同的"每个驱动程序一个目录"方法。

### 最小的 Makefile

FreeBSD 的构建系统对内核模块来说非常简单。用这三行创建 `Makefile`：

```makefile
# Makefile for myfirst driver

KMOD=    myfirst
SRCS=    myfirst.c

.include <bsd.kmod.mk>
```

**逐行解释：**

- `KMOD= myfirst` - 声明模块名称。这将产生 `myfirst.ko`。
- `SRCS= myfirst.c` - 列出源文件。我们目前只有一个。
- `.include <bsd.kmod.mk>` - 引入 FreeBSD 的内核模块构建规则。这一行替代了数百行手动 makefile 逻辑。

**重要：** `.include` 前的缩进是**制表符**，不是空格。如果你使用空格，`make` 会失败并显示晦涩的错误。（大多数编辑器可以配置为在你按 Tab 键时插入制表符。）

**`bsd.kmod.mk` 提供什么：**

- 内核代码的正确编译器标志（`-D_KERNEL`、`-ffreestanding` 等）
- 包含路径（`-I/usr/src/sys`、`-I/usr/src/sys/dev` 等）
- 创建 `.ko` 文件的链接规则
- 标准目标：`make`、`make clean`、`make install` 等

你不需要理解内部细节。只要知道 `.include <bsd.kmod.mk>` 免费给你一个工作的构建系统。

**测试 Makefile：**

在编写任何代码之前，测试构建设置：

```bash
% make clean
% ls
Makefile
```

现在，`make clean` 几乎不做任何事（还没有要删除的文件），但它确认 Makefile 语法有效。

### 最小化的 `myfirst.c`

现在创建 `myfirst.c`，实际的驱动程序源代码。第一个版本是**故意最小化的**：它编译、加载和卸载，但不创建设备，不处理 I/O，也不分配资源。

这是骨架：

```c
/*
 * Copyright (c) 2025 Your Name
 *
 * SPDX-License-Identifier: BSD-2-Clause
 * All rights reserved.
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>

/*
 * Module load/unload event handler.
 *
 * This function is called when the module is loaded (MOD_LOAD)
 * and unloaded (MOD_UNLOAD). For now, we just print messages.
 */
static int
myfirst_loader(module_t mod, int what, void *arg)
{
        int error = 0;

        switch (what) {
        case MOD_LOAD:
                printf("myfirst: driver loaded\n");
                break;
        case MOD_UNLOAD:
                printf("myfirst: driver unloaded\n");
                break;
        default:
                error = EOPNOTSUPP;
                break;
        }

        return (error);
}

/*
 * Module declaration.
 *
 * This ties the module name "myfirst" to the loader function above.
 */
static moduledata_t myfirst_mod = {
        "myfirst",              /* module name */
        myfirst_loader,         /* event handler */
        NULL                    /* extra arg (unused here) */
};

/*
 * DECLARE_MODULE registers this module with the kernel.
 *
 * Parameters:
 *   - module name: myfirst
 *   - moduledata: myfirst_mod
 *   - subsystem: SI_SUB_DRIVERS (driver subsystem)
 *   - order: SI_ORDER_MIDDLE (standard priority)
 */
DECLARE_MODULE(myfirst, myfirst_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(myfirst, 1);
```

**这段代码做什么：**

- **Includes**：引入模块基础设施和日志记录的内核头文件。
- **`myfirst_loader()`**：处理模块生命周期事件。现在只有 MOD_LOAD 和 MOD_UNLOAD。
- **`moduledata_t`**：将模块名称连接到加载器函数。
- **`DECLARE_MODULE()`**：向内核注册模块。这就是让 `kldload` 识别你的模块的原因。
- **`MODULE_VERSION()`**：将模块标记为版本 1（如果你将来更改导出的 ABI，请增加此值）。

**这段代码不做的事情（暂时）：**

- 不创建任何设备
- 不调用 `make_dev()`
- 不注册到 Newbus
- 不分配内存或资源

这只是**加载/卸载**，证明构建系统有效的绝对最小值。

### 构建和测试脚手架

让我们编译并加载这个最小模块：

**1. 构建：**

```bash
% make
machine -> /usr/src/sys/amd64/include
x86 -> /usr/src/sys/x86/include
i386 -> /usr/src/sys/i386/include
touch opt_global.h
Warning: Object directory not changed from original /usr/home/youruser/project/myfirst
cc  -O2 -pipe  -fno-strict-aliasing -Werror -D_KERNEL -DKLD_MODULE -nostdinc   -include /usr/home/youruser/project/myfirst/opt_global.h -I. -I/usr/src/sys -I/usr/src/sys/contrib/ck/include -fno-common  -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fdebug-prefix-map=./machine=/usr/src/sys/amd64/include -fdebug-prefix-map=./x86=/usr/src/sys/x86/include -fdebug-prefix-map=./i386=/usr/src/sys/i386/include    -MD  -MF.depend.myfirst.o -MTmyfirst.o -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -msoft-float  -fno-asynchronous-unwind-tables -ffreestanding -fwrapv -fstack-protector  -Wall -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wcast-qual -Wundef -Wno-pointer-sign -D__printf__=__freebsd_kprintf__ -Wmissing-include-dirs -fdiagnostics-show-option -Wno-unknown-pragmas -Wswitch -Wno-error=tautological-compare -Wno-error=empty-body -Wno-error=parentheses-equality -Wno-error=unused-function -Wno-error=pointer-sign -Wno-error=shift-negative-value -Wno-address-of-packed-member -Wno-format-zero-length   -mno-aes -mno-avx  -std=gnu17 -c myfirst.c -o myfirst.o
ld -m elf_x86_64_fbsd -warn-common --build-id=sha1 -T /usr/src/sys/conf/ldscript.kmod.amd64 -r  -o myfirst.ko myfirst.o
:> export_syms
awk -f /usr/src/sys/conf/kmod_syms.awk myfirst.ko  export_syms | xargs -J % objcopy % myfirst.ko
objcopy --strip-debug myfirst.ko
```

你会看到编译器输出。只要它以创建 `myfirst.ko` 结束且没有错误，你就好了。

**2. 验证构建输出：**

```bash
% ls -l myfirst.ko
-rw-r--r--  1 youruser youruser 11592 Nov  7 00:15 myfirst.ko
```

（文件大小会根据编译器和架构而变化。）

**3. 加载模块：**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 2
myfirst: driver loaded
```

**4. 检查它已加载：**

```bash
% kldstat | grep myfirst
 6    1 0xffffffff82a38000     20b8 myfirst.ko
```

你的模块现在是运行内核的一部分。

**5. 卸载模块：**

```bash
% sudo kldunload myfirst
% dmesg | tail -n 2
myfirst: driver unloaded
```

**6. 确认它已消失：**

```bash
% kldstat | grep myfirst
(no output)
```

完美。你的脚手架工作正常。

### 刚刚发生了什么？

让我们逐步追踪流程：

1. **构建：** `make` 调用 FreeBSD 内核模块构建系统，用内核标志编译 `myfirst.c` 并将其链接到 `myfirst.ko`。
2. **加载：** `kldload` 读取 `myfirst.ko`，将其链接到运行内核，并用 `MOD_LOAD` 调用你的 `myfirst_loader()` 函数。
3. **日志：** 你的 `printf()` 将 "myfirst: driver loaded" 写入内核消息缓冲区。
4. **卸载：** `kldunload` 用 `MOD_UNLOAD` 调用你的加载器，你打印一条消息，然后内核从内存中删除你的代码。

**关键洞察：** 这还不是 Newbus 驱动程序。没有 `probe()`、没有 `attach()`、没有设备。这只是一个加载和卸载的模块。把它看作**阶段 0**：在增加复杂性之前证明构建系统工作。

### 常见脚手架问题故障排除

**1. 问题：** `make` 失败并显示 "missing separator"

**原因：** 你的 Makefile 在 `.include` 前使用空格而不是制表符。

**修复：** 用制表符替换前导空格。

**2. 问题：** `kldload` 说 "Exec format error"

**原因：** 内核版本和 `/usr/src` 版本不匹配。

**修复：** 验证 `freebsd-version -k` 匹配你的源代码树。重建你的内核或重新克隆 `/usr/src` 为正确版本。

**3. 问题：** 模块加载但 `dmesg` 中没有消息

**原因：** 内核消息缓冲区可能已滚动，或 `printf()` 正在被速率限制。

**修复：** 使用 `dmesg -a` 查看所有消息，包括旧的。也检查 `sysctl kern.msgbuf_show_timestamp`。

**4. 问题：** `kldunload` 说 "module busy"

**原因：** 东西仍在使用你的模块（对于这个最小脚手架来说非常不可能）。

**修复：** 此处不适用，但稍后你会看到如果设备节点仍打开或资源未释放会出现这种情况。

### 干净构建实践

在迭代驱动程序时，尽早养成这些习惯：

**1. 重建前始终清理：**

```bash
% make clean
% make
```

这确保陈旧的目标文件不会污染你的构建。

**2. 重建前卸载：**

```bash
% sudo kldunload myfirst 2>/dev/null || true
% make clean && make
```

如果模块未加载，`kldunload` 会无害地失败。`|| true` 防止 shell 停止。

**3. 使用重建脚本：**

创建 `~/drivers/myfirst/rebuild.sh`：

```bash
#!/bin/sh
#
# FreeBSD kernel module rebuild script
# Usage: ./rebuild_module.sh <module_name>
#

set -e

# Configuration
MODULE_NAME="${1}"

# Colors for output (if terminal supports it)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m' # No Color
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
fi

# Helper functions
print_step() {
    printf "${BLUE}==>${NC} ${1}\n"
}

print_success() {
    printf "${GREEN}✓${NC} ${1}\n"
}

print_error() {
    printf "${RED}✗${NC} ${1}\n" >&2
}

print_warning() {
    printf "${YELLOW}!${NC} ${1}\n"
}

check_root() {
    if [ "$(id -u)" -ne 0 ]; then
        print_error "This script must be run as root or with sudo"
        exit 1
    fi
}

is_module_loaded() {
    kldstat -q -n "${1}" 2>/dev/null
}

# Validate arguments
if [ -z "${MODULE_NAME}" ]; then
    print_error "Usage: $0 <module_name>"
    exit 1
fi

# Validate source file exists
if [ ! -f "${MODULE_NAME}.c" ]; then
    print_error "Source file '${MODULE_NAME}.c' not found in current directory"
    exit 1
fi

# Check if we have root privileges
check_root

# Check if Makefile exists
if [ ! -f "Makefile" ]; then
    print_error "Makefile not found in current directory"
    exit 1
fi

# Step 1: Unload module if loaded
print_step "Checking if module '${MODULE_NAME}' is loaded..."
if is_module_loaded "${MODULE_NAME}"; then
    print_warning "Module is loaded, unloading..."
    
    # Capture dmesg state before unload
    DMESG_BEFORE_UNLOAD=$(dmesg | wc -l)
    
    if kldunload "${MODULE_NAME}" 2>/dev/null; then
        print_success "Module unloaded successfully"
    else
        print_error "Failed to unload module"
        exit 1
    fi
    
    # Verify unload
    sleep 1
    if is_module_loaded "${MODULE_NAME}"; then
        print_error "Module still loaded after unload attempt"
        exit 1
    fi
    print_success "Verified: module removed from memory"
    
    # Check dmesg for unload messages
    DMESG_AFTER_UNLOAD=$(dmesg | wc -l)
    DMESG_UNLOAD_NEW=$((DMESG_AFTER_UNLOAD - DMESG_BEFORE_UNLOAD))
    
    if [ ${DMESG_UNLOAD_NEW} -gt 0 ]; then
        echo
        print_step "Kernel messages from unload:"
        dmesg | tail -n ${DMESG_UNLOAD_NEW}
        echo
    fi
else
    print_success "Module not loaded, proceeding..."
fi

# Step 2: Clean build artifacts
print_step "Cleaning build artifacts..."
if make clean; then
    print_success "Clean completed"
else
    print_error "Clean failed"
    exit 1
fi

# Step 3: Build module
print_step "Building module..."
if make; then
    print_success "Build completed"
else
    print_error "Build failed"
    exit 1
fi

# Verify module file exists
if [ ! -f "./${MODULE_NAME}.ko" ]; then
    print_error "Module file './${MODULE_NAME}.ko' not found after build"
    exit 1
fi

# Step 4: Load module
print_step "Loading module..."
DMESG_BEFORE=$(dmesg | wc -l)

if kldload "./${MODULE_NAME}.ko"; then
    print_success "Module load command executed"
else
    print_error "Failed to load module"
    exit 1
fi

# Step 5: Verify module is loaded
sleep 1
print_step "Verifying module load..."

if is_module_loaded "${MODULE_NAME}"; then
    print_success "Module is loaded in kernel"
    
    # Show module info
    echo
    kldstat | head -n 1
    kldstat | grep "${MODULE_NAME}"
else
    print_error "Module not found in kldstat output"
    exit 1
fi

# Step 6: Check kernel messages
echo
print_step "Recent kernel messages from load:"
DMESG_AFTER=$(dmesg | wc -l)
DMESG_NEW=$((DMESG_AFTER - DMESG_BEFORE))

if [ ${DMESG_NEW} -gt 0 ]; then
    dmesg | tail -n ${DMESG_NEW}
else
    print_warning "No new kernel messages"
    dmesg | tail -n 5
fi

echo
print_success "Module '${MODULE_NAME}' rebuilt and loaded successfully!"
```

使其可执行：

```bash
% chmod +x rebuild.sh
```

现在你可以快速迭代：

```bash
% ./rebuild.sh myfirst
```

这个脚本卸载、清理、构建、加载并显示最近的内核消息，一气呵成。在开发期间这是一个巨大的时间节省者。

**注意：** 你可能想知道这个脚本是否需要如此复杂。对于一次性使用，它不需要。然而，内核模块开发涉及重复的卸载-重建-加载循环，通常每天数十次。使用适当的错误处理和验证来构建它，可以创建一个你在整个开发过程中自信地重用的工具，以后可以节省无数小时。更重要的是，这是练习防御性编程的完美机会：验证输入、检查每一步的错误、当出现问题时提供清晰的反馈。这些习惯将在你未来的所有开发工作中为你服务。

### 版本控制检查点

在前进之前，将你的脚手架提交到 Git（如果你使用版本控制，你应该使用）：

```bash
% cd ~/drivers/myfirst
% git init
% git add Makefile myfirst.c
% git commit -m "Initial scaffold: loads and unloads cleanly"
```

这给你一个已知良好的状态，如果以后破坏了东西可以返回。如果你使用远程仓库（GitHub、GitLab等），你可以用 `git push` 推送这些更改，但对于本地版本控制的好处来说这不是必需的。

### 下一步是什么？

你现在有一个工作的脚手架：一个能够构建、加载和卸载的模块。它还不是 Newbus 驱动程序，它也不创建任何用户可见的界面，但它是一个坚实的基础。

在下一节中，我们将添加**Newbus 集成**，将这个简单的模块转变为一个正确的伪设备驱动程序，它注册到设备树并实现 `probe()` 和 `attach()` 生命周期方法。

## Newbus：足以挂载

你已经构建了一个能够加载和卸载的脚手架。现在我们将把它转变为**Newbus 驱动程序**，一个注册到 FreeBSD 设备框架并遵循标准 `identify` / `probe` / `attach` / `detach` 生命周期的驱动程序。

这就是你的驱动程序停止作为被动模块并开始表现得像真正设备驱动程序的地方。到本节结束时，你将拥有一个能够：

- 作为伪设备注册到 `nexus` 总线
- 提供一个创建 `myfirst` 设备的 `identify()` 方法
- 实现 `probe()` 来声明设备
- 实现 `attach()` 来初始化（即使初始化暂时是最小的）
- 实现 `detach()` 来清理
- 正确记录生命周期事件

我们保持**刚好足够**来展示模式。还没有资源分配，还没有设备节点，还没有 sysctl。这些在后面的部分中介绍。现在，专注于理解**Newbus 如何调用你的代码**以及**每个方法应该做什么**。

### 为什么是 Newbus？

FreeBSD 使用 Newbus 来管理设备发现、驱动匹配和生命周期。即使对于伪设备（没有支持硬件的纯软件设备），遵循 Newbus 模式也能确保：

- 所有驱动程序的行为一致
- 与设备树正确集成
- 可靠的生命周期管理（attach / detach / suspend / resume）
- 与 `devinfo` 和 `kldunload` 等工具兼容

**心智模型：** Newbus 是内核的人力资源部门。它开设新职位（identify），为每个职位面试驱动程序（probe），雇佣最合适的（attach），并管理辞职（detach）。对于真正的硬件，总线自动发布职位。对于伪设备，你的驱动程序还要编写职位描述，这就是 `identify` 的作用。

### 最小 Newbus 模式

每个 Newbus 驱动程序遵循这个结构：

1. **定义设备方法**（`identify`、`probe`、`attach`、`detach`）作为函数
2. **创建方法表**，将 Newbus 方法名称映射到你的函数
3. **声明驱动结构**，包含方法表和 softc 大小
4. 使用 `DRIVER_MODULE()` **注册驱动程序**

对于挂载到真正总线（如 `pci` 或 `usb`）的驱动程序，总线自己枚举硬件并通过 `probe` 询问每个注册的驱动程序"这个设备是你的吗？"。伪设备没有硬件可枚举，所以我们必须告诉总线设备存在。这就是 `identify` 的工作。我们将在下面的第 4 步中介绍它，在 probe 和 attach 就位之后，这样在每个方法的角色清楚之前文件不会变得拥挤。

让我们逐步讲解每个部分。

### 步骤 1：包含 Newbus 头文件

在 `myfirst.c` 的顶部，添加这些包含（替换或补充脚手架中的最小包含）：

```c
#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>        /* For device_t, Newbus APIs */
#include <sys/conf.h>       /* For cdevsw (used later) */
```

**这些提供什么：**

- `<sys/bus.h>` - 核心 Newbus 类型（`device_t`、`device_method_t`）和函数（`device_printf`、`device_get_softc` 等）
- `<sys/conf.h>` - 字符设备开关结构（我们创建 `/dev` 节点时会用到）

### 步骤 2：定义你的 Softc

**softc**（软件上下文）是你的驱动程序的每设备私有数据结构。即使我们还没有存储任何有趣的东西，**每个 Newbus 驱动程序都有一个**。

在 `myfirst.c` 的包含之后附近添加这个：

```c
/*
 * Driver softc (software context).
 *
 * One instance of this structure exists per device.
 * Newbus allocates and zeroes it for us.
 */
struct myfirst_softc {
        device_t        dev;            /* Back-pointer to device_t */
        uint64_t        attach_time;    /* When we attached (ticks) */
        int             is_ready;       /* Simple flag */
};
```

**为什么这些字段？**

- `dev` - 方便的反向指针。让你调用 `device_printf(sc->dev, ...)` 而不必到处传递 `dev`。
- `attach_time` - 示例状态。我们将记录 `attach()` 运行的时间。
- `is_ready` - 另一个示例标志。展示你如何跟踪驱动程序状态。

**关键洞察：** 你从不需要自己 `malloc()` 或 `free()` softc。Newbus 根据你在驱动结构中声明的大小自动完成。

### 步骤 3：实现 Probe

`probe()` 方法回答一个问题：**"这个驱动程序是否匹配这个设备？"**

对于伪设备，答案总是肯定的（我们不检查 PCI ID 或硬件签名）。但我们仍然实现 `probe()` 来遵循模式并设置设备描述。

添加这个函数：

```c
/*
 * Probe method.
 *
 * Called by Newbus to see if this driver wants to handle this device.
 * For a pseudo-device created by our own identify method, we always accept.
 *
 * The return value is a priority. Higher values win when several drivers
 * are willing to take the same device. ENXIO means "not mine, reject".
 */
static int
myfirst_probe(device_t dev)
{
        device_set_desc(dev, "My First FreeBSD Driver");
        return (BUS_PROBE_DEFAULT);
}
```

**逐行解释：**

- `device_set_desc()` 设置一个人类可读的描述。它出现在 `devinfo -v` 和挂载消息中。字符串必须在设备生命周期内保持有效，所以始终在这里传递字符串字面量。如果你需要动态构建的描述，请改用 `device_set_desc_copy()`。
- `return (BUS_PROBE_DEFAULT)` 告诉 Newbus"我将处理这个设备，使用标准基础操作系统优先级。"

**Probe 规范：**

- **不要**在 `probe()` 中分配资源。如果另一个驱动程序赢了，你的资源会泄漏。
- **不要**在 `probe()` 中触摸硬件（此处不相关，但对真正的硬件驱动程序至关重要）。
- **要**快速返回。Probe 在启动和热插拔事件期间频繁调用。

**关于探测优先级值的说明。** 当几个驱动程序愿意接受同一个设备时，内核选择返回**最高**值的那个。`<sys/bus.h>` 中的常量反映了这种顺序，更具体的出价在数值上更大：

| 常量                    | 值 (FreeBSD 14.3) | 何时使用                                           |
|-------------------------|----------------------|----------------------------------------------------------|
| `BUS_PROBE_SPECIFIC`    | `0`                  | 只有这个驱动程序可能处理这个设备         |
| `BUS_PROBE_VENDOR`      | `-10`                | 供应商提供的驱动程序，胜过通用类驱动程序   |
| `BUS_PROBE_DEFAULT`     | `-20`                | 此类的标准基础操作系统驱动程序                   |
| `BUS_PROBE_LOW_PRIORITY`| `-40`                | 较旧或不太理想的驱动程序                           |
| `BUS_PROBE_GENERIC`     | `-100`               | 通用后备驱动程序                                   |
| `BUS_PROBE_NOWILDCARD`  | 非常大的负值  | 仅匹配显式创建的设备（例如通过 identify） |

`BUS_PROBE_DEFAULT` 是典型驱动程序的正确选择，包括我们的：我们在 `identify()` 中按名称标识自己的设备，所以不存在真正的竞争者，而且该值足够高，没有什么能击败我们。

### 步骤 4：实现 Attach

`attach()` 方法是你**初始化驱动程序**的地方。资源被分配，硬件被配置，设备节点被创建。现在，我们只是记录一条消息并填充 softc。

添加这个函数：

```c
/*
 * Attach method.
 *
 * Called after probe succeeds. Initialize the driver here.
 */
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);
        sc->dev = dev;
        sc->attach_time = ticks;  /* Record when we attached */
        sc->is_ready = 1;

        device_printf(dev, "Attached successfully at tick %lu\n",
            (unsigned long)sc->attach_time);

        return (0);
}
```

**这做什么：**

- `device_get_softc(dev)` - 检索 Newbus 为我们分配的 softc（最初已清零）。
- `sc->dev = dev` - 保存 `device_t` 反向指针以便使用。
- `sc->attach_time = ticks` - 记录当前内核滴答计数（一个简单的时间戳）。
- `sc->is_ready = 1` - 设置一个标志（暂时未使用，但展示你如何跟踪状态）。
- `device_printf()` - 记录带有我们设备名称前缀的挂载事件。
- `return (0)` - 成功。非零表示失败并中止挂载。

**Attach 规范：**

- **要**在这里分配资源（内存、锁、硬件映射）。
- **要**创建用户界面（`/dev` 节点、网络接口等）。
- **要**优雅地处理失败。如果出现问题，撤销你开始的工作并返回错误代码。
- **不要**还触摸用户空间。Attach 在模块加载或设备发现期间运行，在任何用户程序可以与你交互之前。

**错误处理预览：**

现在，attach 不能失败（我们没有做任何可能出错的事情）。后面的部分将添加资源分配，你将看到如何在失败时展开。

### 步骤 5：实现 Detach

`detach()` 方法是 `attach()` 的反面：拆除你构建的东西，释放你声明的东西，不留痕迹。

添加这个函数：

```c
/*
 * Detach method.
 *
 * Called when the driver is being unloaded or the device is removed.
 * Clean up everything you set up in attach().
 */
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        device_printf(dev, "Detaching (was attached for %lu ticks)\n",
            (unsigned long)(ticks - sc->attach_time));

        sc->is_ready = 0;

        return (0);
}
```

**这做什么：**

- 检索 softc（我们知道它存在，因为 attach 成功了）。
- 记录驱动程序挂载了多长时间（当前 `ticks` 减去 `attach_time`）。
- 清除 `is_ready` 标志（严格来说不需要，因为 softc 很快就会被释放，但这是好习惯）。
- 返回 0（成功）。

**Detach 规范：**

- **要**释放所有资源（销毁锁、释放内存、销毁设备节点）。
- **要**确保没有活动的 I/O 或回调可以在 detach 返回后到达你的代码。
- **要**如果设备正在使用且还不能分离（例如，打开的设备节点）则返回 `EBUSY`。
- **不要**假设 detach 返回后 softc 仍然有效。Newbus 会释放它。

**为什么 detach 重要：**

糟糕的 detach 实现是卸载时内核崩溃的第一大来源。如果你忘记销毁锁、释放资源或删除回调，当你的代码消失后访问该资源时就会崩溃。

### 步骤 6：实现 Identify

我们有 probe、attach 和 detach。它们告诉内核当 `myfirst` 设备出现时**做什么**。但是在 nexus 总线上还没有 `myfirst` 设备，nexus 也没有办法发明一个。我们必须在驱动程序注册时自己创建设备。这就是 `identify` 方法做的。

添加这个函数：

```c
/*
 * Identify method.
 *
 * Called by Newbus once, right after the driver is registered with the
 * parent bus. Its job is to create child devices that this driver will
 * then probe and attach.
 *
 * Real hardware drivers usually do not need an identify method, because
 * the bus (PCI, USB, ACPI, ...) enumerates devices on its own. A pseudo
 * device has nothing for the bus to find, so we add our single device
 * here, by name.
 */
static void
myfirst_identify(driver_t *driver, device_t parent)
{
        if (device_find_child(parent, driver->name, -1) != NULL)
                return;
        if (BUS_ADD_CHILD(parent, 0, driver->name, -1) == NULL)
                device_printf(parent, "myfirst: BUS_ADD_CHILD failed\n");
}
```

**逐行解释：**

- `device_find_child(parent, driver->name, -1)` 检查 `parent` 下面是否已存在名为 `myfirst` 的设备。如果我们不检查，重新加载模块（或总线的任何第二次遍历）会创建重复的设备。
- `BUS_ADD_CHILD(parent, 0, driver->name, -1)` 请求父总线创建一个名为 `myfirst` 的新子设备，顺序为 `0`，自动选择单元号。调用后，Newbus 将对我们的新子设备运行 `probe`，如果 probe 接受，就运行 attach。
- 我们在失败时记录但不恐慌。`BUS_ADD_CHILD` 可能在内存压力下失败，缺少伪设备不应该让系统宕机。

**这适合哪里。** `identify` 在每个驱动程序每个总线运行一次，当驱动程序首次挂载到该总线时。identify 之后，总线的正常探测和挂载机制接管。这是 FreeBSD 源代码树中 `cryptosoft`、`aesni` 和 `snd_dummy` 等驱动程序使用的相同模式，你以后可以浏览它们作为参考。

### 步骤 7：创建方法表

现在将你的函数连接到 Newbus 方法名称。在函数定义之后添加这个：

```c
/*
 * Device method table.
 *
 * Maps Newbus method names to our functions.
 */
static device_method_t myfirst_methods[] = {
        /* Device interface */
        DEVMETHOD(device_identify,      myfirst_identify),
        DEVMETHOD(device_probe,         myfirst_probe),
        DEVMETHOD(device_attach,        myfirst_attach),
        DEVMETHOD(device_detach,        myfirst_detach),

        DEVMETHOD_END
};
```

**这个表意味着什么：**

- `DEVMETHOD(device_identify, myfirst_identify)` 说"当总线邀请每个驱动程序创建其设备时，运行 `myfirst_identify()`。"
- `DEVMETHOD(device_probe, myfirst_probe)` 说"当内核调用 `DEVICE_PROBE(dev)` 时，运行 `myfirst_probe()`。"
- attach 和 detach 同理。
- `DEVMETHOD_END` 终止表并是必需的。

**幕后：** `DEVMETHOD()` 宏和 kobj 系统（内核对象）生成调度到你的函数的粘合代码。你不需要理解内部细节；只要知道这个表是 Newbus 找到你代码的方式。

### 步骤 8：声明驱动程序

在 `driver_t` 结构中将所有东西联系在一起：

```c
/*
 * Driver declaration.
 *
 * Specifies our method table and softc size.
 */
static driver_t myfirst_driver = {
        "myfirst",              /* Driver name */
        myfirst_methods,        /* Method table */
        sizeof(struct myfirst_softc)  /* Softc size */
};
```

**参数：**

- `"myfirst"` - 驱动程序名称（出现在日志中并作为设备名称前缀）。
- `myfirst_methods` - 指向你刚刚创建的方法表的指针。
- `sizeof(struct myfirst_softc)` - 告诉 Newbus 每个设备分配多少内存。

**为什么 softc 大小？** Newbus 每个设备实例分配一个 softc。通过在这里声明大小，你从不需要手动分配或释放它——Newbus 管理生命周期。

### 步骤 9：使用 DRIVER_MODULE 注册

用这个替换脚手架中旧的 `DECLARE_MODULE()` 宏：

```c
/*
 * Driver registration.
 *
 * Attach this driver under the nexus bus. Our identify method will
 * create the actual myfirst child device when the module loads.
 */

DRIVER_MODULE(myfirst, nexus, myfirst_driver, 0, 0);
MODULE_VERSION(myfirst, 1);
```

**这做什么：**

- `DRIVER_MODULE(myfirst, nexus, myfirst_driver, 0, 0)` 将 `myfirst` 注册为愿意挂载在 `nexus` 总线下的驱动程序。两个尾随零是可选的模块事件处理程序及其参数；我们的最小驱动程序不需要它们。
- `MODULE_VERSION(myfirst, 1)` 将模块标记为版本 1，以便其他模块可以声明对它的依赖。

**为什么是 `nexus`？**

`nexus` 是 FreeBSD 的根总线，每个架构设备树的顶部。第 6 章正确地建议你，对于真正的硬件驱动程序来说，`nexus` 很少是正确的父级：PCI 驱动程序属于 `pci` 下面，USB 驱动程序属于 `usbus` 下面，依此类推。伪设备不同。它们没有物理总线，所以 FreeBSD 源代码树中的约定是将它们挂载到 `nexus` 并通过 `identify` 方法自己创建子设备。这正是 `cryptosoft`、`aesni` 和 `snd_dummy` 所做的，也正是我们在这里做的。

### 步骤 10：删除旧的模块加载器

你不再需要脚手架中的 `myfirst_loader()` 函数或 `moduledata_t` 结构。Newbus 现在通过 `identify`、`probe`、`attach` 和 `detach` 驱动模块生命周期。完全删除那些旧片段。

你的 `myfirst.c` 现在应该有：

- Includes
- Softc 结构
- `myfirst_identify()`
- `myfirst_probe()`
- `myfirst_attach()`
- `myfirst_detach()`
- 方法表
- 驱动结构
- `DRIVER_MODULE()` 和 `MODULE_VERSION()`

不再有 `MOD_LOAD` 事件处理程序。

### 步骤 11：调整 Makefile

在你的 Makefile 中添加这一行：

```makefile
# Required for Newbus drivers: generates device_if.h and bus_if.h
SRCS+=   device_if.h bus_if.h
```

**为什么需要这个：**

FreeBSD 的 Newbus 框架使用构建在 kobj 之上的方法调度系统。方法表中的 `DEVMETHOD()` 条目引用生成的头文件 `device_if.h` 和 `bus_if.h` 中声明的方法标识符。`bsd.kmod.mk` 知道如何从 `/usr/src/sys/kern/device_if.m` 和 `/usr/src/sys/kern/bus_if.m` 构建这些，但它只在你将它们列在 `SRCS` 中时才这样做。如果你忘记这一行，编译时你会得到关于未知方法标识符的混淆错误。

### 构建和测试 Newbus 驱动程序

让我们编译和测试：

**1. 清理和构建：**

```bash
% make clean
% make
```

你应该看不到错误。

**2. 加载模块：**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
```

注意：

- 设备名称是 `myfirst0`（驱动程序名称 + 单元号）。
- 它挂载在 "on nexus0"（父总线）。
- 你的自定义挂载消息出现了。

**3. 检查设备树：**

```bash
% devinfo -v | grep myfirst
    myfirst0
```

你的驱动程序现在是设备树的一部分。

**4. 卸载：**

```bash
% sudo kldunload myfirst
% dmesg | tail -n 2
myfirst0: Detaching (was attached for 5432 ticks)
```

你的 detach 消息显示驱动程序挂载了多长时间。

**5. 验证它已消失：**

```bash
% devinfo -v | grep myfirst
(no output)
```

### 改变了什么？

与脚手架相比，你的驱动程序现在：

- **注册到 Newbus** 而不是使用简单的模块加载器。
- **添加子设备**（`myfirst0`）到设备树，通过 `identify`。
- **遵循 identify / probe / attach / detach 生命周期** 而不仅仅是加载/卸载。
- **自动分配和管理 softc**。

这是每个 FreeBSD 驱动程序的**基础模式**。掌握这个，其余的只是添加层。

### 常见 Newbus 错误（以及如何避免）

**错误 0：忘记伪设备上的 identify 方法**

**症状：** `kldload` 成功，但没有 `myfirst0` 设备出现，`dmesg` 中没有探测消息，`devinfo` 在 `nexus0` 下什么都不显示。驱动程序编译并加载了，但它从未挂载。

**原因：** 驱动程序用 `DRIVER_MODULE(..., nexus, ...)` 注册但没有提供 `device_identify` 方法。Nexus 没有什么可枚举的，所以 probe 和 attach 从未被调用。

**修复：** 添加步骤 6 中显示的 `identify` 方法并将 `DEVMETHOD(device_identify, myfirst_identify)` 放在方法表中。这是初学者的伪设备驱动程序"加载但什么都不做"的最常见原因。

---

**错误 1：在 probe 中分配资源**

**错误：**

```c
static int
myfirst_probe(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        sc->something = malloc(...);  /* BAD! */
        return (BUS_PROBE_DEFAULT);
}
```

**为什么错误：** 如果 probe 失败或另一个驱动程序赢了，你的分配会泄漏。

**正确：** 在 `attach()` 中分配，在那里你知道驱动程序已被选中。

---

**错误 2：忘记从 attach 返回 0**

**错误：**

```c
static int
myfirst_attach(device_t dev)
{
        /* ... setup ... */
        /* (missing return statement) */
}
```

**为什么错误：** 编译器可能警告，但返回值未定义。你可能意外返回垃圾，导致 attach 神秘失败。

**正确：** 始终在成功时以 `return (0)` 结束 attach 或在失败时以 `return (error_code)` 结束。

---

**错误 3：不在 detach 中清理**

**错误：**

```c
static int
myfirst_detach(device_t dev)
{
        device_printf(dev, "Detaching\n");
        return (0);
        /* (forgot to free resources, destroy locks, etc.) */
}
```

**为什么错误：** 资源泄漏。锁保持活动。下次加载可能会崩溃。

**正确：** Detach 必须撤销 attach 所做的一切。我们将在错误处理部分详细介绍清理模式。

### Newbus 生命周期时序图

```text
[ Boot or kldload ]
        |
        v
   identify(parent)  --> "What devices does this driver provide?"
        |                 (Pseudo-devices: BUS_ADD_CHILD here)
        |                 (Real hardware: usually omitted)
        v
    probe(dev)  --> "Is this device mine?"
        |            (Check IDs, set description)
        | (return a probe priority such as BUS_PROBE_DEFAULT)
        v
    attach(dev)  --> "Initialize and prepare for use"
        |            (Allocate resources, create surfaces)
        |            (If fails, undo what was done, return error)
        v
  [ Device ready, normal operation ]
        |
        | (time passes, I/O happens, sysctls read, etc.)
        |
        v
    detach(dev)  --> "Shutdown and cleanup"
        |            (Destroy surfaces, release resources)
        |            (Return EBUSY if still in use)
        v
    [ Module unloaded or device gone ]
```

**关键洞察：** 每个步骤都是独特的。Identify 创建设备，probe 声明它，attach 初始化，detach 清理。不要模糊边界。

### 快速自检

在前进之前，确保你能回答这些：

1. **我在哪里为驱动程序状态分配内存？**
   答：在 `attach()` 中，或者只使用 softc（Newbus 为你分配）。

2. **`device_get_softc()` 返回什么？**
   答：指向你的驱动程序每设备私有数据的指针（在这种情况下是 `struct myfirst_softc *`）。

3. **probe 何时被调用？**
   答：在设备枚举期间。对于真正的总线，当总线发现设备时发生。对于我们的伪设备，它发生在我们的 `identify` 方法调用 `BUS_ADD_CHILD()` 将 `myfirst` 设备放到 nexus 总线上之后立即。

4. **detach 必须做什么？**
   答：撤销 attach 所做的一切，释放资源，并确保之后没有代码路径可以到达驱动程序。

5. **为什么我们为此驱动程序使用 `nexus`，为什么它需要 `identify` 方法？**
   答：因为它是一个没有物理总线的伪设备。`nexus` 是纯软件设备的常规父级，但 nexus 没有设备可枚举，所以我们通过 `identify` 创建自己的设备。

如果这些答案有意义，你准备好进入下一节：使用 softc 添加真正的状态管理。

---

## softc 和生命周期状态

你已经看到了 softc 结构的声明、分配和检索，但我们还没有谈论**为什么它存在**或**如何正确使用它**。在本节中，我们将深入探讨 softc 模式：其中放什么、如何安全地初始化和访问它，以及如何避免常见陷阱。

softc 是你的驱动程序的**内存**。每个资源、每个锁、每个统计信息和每个标志都住在这里。正确处理这个是可靠驱动程序和在负载下崩溃的驱动程序之间的区别。

### 什么是 softc？

**softc**（软件上下文）是一个每设备结构，存储你的驱动程序操作所需的一切。把它想成驱动程序的"工作区"或"笔记本"，每个设备一个实例，保存使该特定设备工作的所有状态。

**关键属性：**

- **每设备：** 如果你的驱动程序处理多个设备（例如 `myfirst0`、`myfirst1`），每个都有自己 的 softc。
- **内核分配：** 你声明结构类型和大小；Newbus 分配并清零内存。
- **生命周期：** 从设备创建（在 `attach()` 之前）存在直到设备删除（在 `detach()` 之后）。
- **访问模式：** 在每个方法开始时用 `device_get_softc(dev)` 检索。

**为什么不用全局变量？**

全局变量不能处理多个设备。如果你将状态存储在全局变量中，`myfirst0` 和 `myfirst1` 会相互覆盖数据。softc 模式优雅地解决了这个问题：每个设备都有自己隔离的状态。

### softc 中应该放什么？

设计良好的 softc 包含：

**1. 标识和内务**

- `device_t dev` - 指向设备的反向指针（用于日志记录和回调）
- `int unit` - 设备单元号（通常从 `dev` 提取，但便于缓存）
- `char name[16]` - 如果你经常需要设备名称字符串

**2. 资源**

- `struct resource *mem_res` - MMIO 区域（用于硬件驱动程序）
- `int mem_rid` - 内存资源 ID
- `struct resource *irq_res` - 中断资源
- `void *irq_handler` - 中断处理程序 cookie
- `bus_dma_tag_t dma_tag` - DMA 标签（用于做 DMA 的驱动程序）

**3. 同步原语**

- `struct mtx mtx` - 保护共享状态的互斥锁
- `struct sx sx` - 如果需要则使用共享/独占锁
- `struct cv cv` - 用于睡眠/唤醒的条件变量

**4. 设备状态标志**

- `int is_attached` - 在 attach 中设置，在 detach 中清除
- `int is_open` - `/dev` 节点打开时设置
- `uint32_t flags` - 杂项状态的位域（运行、暂停、错误等）

**5. 统计和计数器**

- `uint64_t tx_packets` - 发送的数据包（网络驱动程序示例）
- `uint64_t rx_bytes` - 接收的字节
- `uint64_t errors` - 错误计数
- `time_t last_reset` - 统计上次清除的时间

**6. 驱动程序特定数据**

- 硬件寄存器、队列、缓冲区、工作结构，任何你驱动程序操作独特的东西。

**不应该放的：**

- **大缓冲区：** Softc 住在内核内存中（有线、不可分页）。大缓冲区应该用 `malloc()` 或 `contigmalloc()` 单独分配并从 softc 指向。
- **常量数据：** 使用 `const` 全局数组或静态表代替。
- **临时变量：** 函数局部变量可以。不要用每次操作的临时变量弄乱 softc。

### 我们的 myfirst Softc（最小示例）

让我们重新审视并扩展我们的 softc 定义：

```c
struct myfirst_softc {
        device_t        dev;            /* Back-pointer */
        int             unit;           /* Device unit number */

        struct mtx      mtx;            /* Protects shared state */

        uint64_t        attach_ticks;   /* When attach() ran */
        uint64_t        open_count;     /* How many times opened */
        uint64_t        bytes_read;     /* Bytes read from device */

        int             is_attached;    /* 1 if attach succeeded */
        int             is_open;        /* 1 if /dev node is open */
};
```

**逐字段解释：**

- `dev` - 标准反向指针。几乎每个驱动程序都包含这个。
- `unit` - 缓存的单元号（来自 `device_get_unit(dev)`）。可选但方便。
- `mtx` - 保护并发访问的互斥锁。即使我们还没有练习并发，现在包含它教会好习惯。
- `attach_ticks` - 我们何时挂载（内核滴答）。简单示例状态。
- `open_count` / `bytes_read` - 计数器。真正的驱动程序为统计和可观测性跟踪这些。
- `is_attached` / `is_open` - 生命周期状态的标志。在错误检查中有用。

**为什么现在用互斥锁？**

即使我们的最小驱动程序还不需要它，包含互斥锁教授**模式**。每个驱动程序最终都需要锁定，从一开始就设计进去比以后改造更容易。

我们还不会为了真正的共享数据保护使用互斥锁。并发、锁顺序和死锁陷阱在第三部分出现。现在，锁在这里建立生命周期模式，并使 detach 顺序安全。参见第 6 章的"销毁仍被线程持有的锁"陷阱。

### 在 attach() 中初始化 softc

Newbus 在调用 `attach()` 之前清零 softc，但你仍然需要显式初始化某些字段（锁、反向指针、标志）。

这是更新的 `attach()`：

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        /* Initialize back-pointer and unit */
        sc->dev = dev;
        sc->unit = device_get_unit(dev);

        /* Initialize mutex */
        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

        /* Record attach time */
        sc->attach_ticks = ticks;

        /* Set state flags */
        sc->is_attached = 1;
        sc->is_open = 0;

        /* Initialize counters */
        sc->open_count = 0;
        sc->bytes_read = 0;

        device_printf(dev, "Attached at tick %lu\n",
            (unsigned long)sc->attach_ticks);

        return (0);
}
```

**改变了什么：**

- **`mtx_init()`** - 初始化互斥锁。参数：
  - `&sc->mtx` - softc 中互斥锁字段的地址。
  - `device_get_nameunit(dev)` - 返回类似 "myfirst0" 的字符串（用于锁调试）。
  - `"myfirst"` - 锁类型名称（出现在锁跟踪中）。
  - `MTX_DEF` - 标准互斥锁（相对于自旋互斥锁）。
- **`sc->is_attached = 1`** - 表示我们现在准备好了的标志。
- **计数器初始化** - 显式清零它们（即使 Newbus 清零了整个 softc，显式做记录了意图）。

**规范：** 初始化所有以后会被测试的字段。不要假设"零意味着未初始化"总是正确的语义（对于标志，也许；对于指针，绝对不是）。

### 在 detach() 中销毁 softc

在 `detach()` 中，你必须撤销 `attach()` 所做的一切。对于 softc，这意味着：

- 销毁锁（互斥锁、sx、cv 等）
- 释放 softc 指向的任何内存或资源
- 清除标志（严格来说不需要，但这是好习惯）

更新的 `detach()`：

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc;
        uint64_t uptime;

        sc = device_get_softc(dev);

        /* Calculate how long we were attached */
        uptime = ticks - sc->attach_ticks;

        /* Refuse detach if device is open */
        if (sc->is_open) {
                device_printf(dev, "Cannot detach while device is open\n");
                return (EBUSY);
        }

        /* Log stats before shutting down */
        device_printf(dev, "Detaching: uptime %lu ticks, opened %lu times, read %lu bytes\n",
            (unsigned long)uptime,
            (unsigned long)sc->open_count,
            (unsigned long)sc->bytes_read);

        /* Destroy the mutex */
        mtx_destroy(&sc->mtx);

        /* Clear attached flag */
        sc->is_attached = 0;

        return (0);
}
```

**新内容：**

- **`if (sc->is_open) return (EBUSY)`** - 如果 `/dev` 节点仍打开则拒绝分离。这防止访问已释放资源的崩溃。
- **统计日志** - 显示驱动程序运行了多长时间以及它做了什么。
- **`mtx_destroy(&sc->mtx)`** - **关键。** 每个 `mtx_init()` 必须有匹配的 `mtx_destroy()`，否则你会泄漏内核锁资源。
- **清除标志** - 严格来说不需要（Newbus 很快会释放 softc），但这是好的防御性编程。

**常见错误：** 忘记 `mtx_destroy()`。如果你使用 WITNESS 或 INVARIANTS 内核，这会导致下次加载时的锁跟踪崩溃。

### 从其他方法访问 softc

每个需要状态的驱动程序方法都以相同方式开始：

```c
static int
myfirst_some_method(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        /* Now use sc-> to access state */
        ...
}
```

这是你将在每个 FreeBSD 驱动程序中看到的**惯用模式**。一行进入你驱动程序的世界。

**为什么不直接传递 softc？**

Newbus 方法定义为接收 `device_t`。softc 是你驱动程序的实现细节。通过 `device_get_softc()` 一致地检索它，你的驱动程序保持灵活（你可以更改 softc 结构而不更改方法签名）。

### 使用锁保护 softc

即使我们还没有添加并发操作，让我们预览当你添加时将使用的**锁定模式**。

**基本模式：**

```c
static void
myfirst_increment_counter(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        mtx_lock(&sc->mtx);
        sc->open_count++;
        mtx_unlock(&sc->mtx);
}
```

**规则：**

- **在修改共享状态之前加锁。**
- **一旦完成就解锁**（不要持有锁超过必要时间）。
- **永远不要在持有锁时返回**（除非你在做高级锁交接模式）。
- **如果持有多个锁则记录锁顺序**（以避免死锁）。

**何时需要这个：** 一旦你添加 `open()`、`read()`、`write()` 或任何可能并发运行的方法（用户程序从多个线程调用你的驱动程序，或中断处理程序更新统计）。

现在，互斥锁存在但未被使用。我们将在后面的部分添加并发入口点时使用它。

### Softc 最佳实践

**1. 保持有序**

将相关字段分组在一起：

```c
struct myfirst_softc {
        /* Identification */
        device_t        dev;
        int             unit;

        /* Synchronization */
        struct mtx      mtx;

        /* Resources */
        struct resource *mem_res;
        int             mem_rid;

        /* Statistics */
        uint64_t        tx_packets;
        uint64_t        rx_bytes;

        /* State flags */
        int             is_attached;
        int             is_open;
};
```

**2. 注释非明显字段**

```c
        int             pending_requests;  /* Must hold mtx to access */
        time_t          last_activity;     /* Protected by mtx */
```

**3. 对计数器使用固定宽度类型**

```c
        uint64_t        packets;  /* Not "unsigned long" */
        uint32_t        errors;   /* Not "int" */
```

**为什么？** 可移植性。`int` 和 `long` 大小因架构而异。`uint64_t` 总是 64 位。

**4. 避免填充浪费**

编译器插入填充来对齐字段。先排列大字段，然后小的：

```c
/* Good: no wasted padding */
struct example {
        uint64_t        big_counter;  /* 8 bytes */
        uint32_t        medium;       /* 4 bytes */
        uint32_t        medium2;      /* 4 bytes */
        uint16_t        small;        /* 2 bytes */
        uint8_t         tiny;         /* 1 byte */
        uint8_t         tiny2;        /* 1 byte */
};
```

**5. 清零你将测试的字段**

```c
        sc->is_open = 0;       /* Explicit, even though Newbus zeroed it */
        sc->bytes_read = 0;
```

**为什么？** 清晰。阅读代码的人知道你*打算*清零，而不是依赖隐式清零。

### 调试 softc 问题

**问题：** 内核崩溃"NULL pointer dereference"在你的驱动程序中。

**可能原因：** 你忘记检索 softc，或在 `dev` 可能无效的点之后检索。

**修复：** 始终在每个方法开始时 `sc = device_get_softc(dev);`。

---

**问题：** 互斥锁崩溃"already locked"或"not locked."

**可能原因：** 在 `attach()` 中忘记 `mtx_init()` 或 `mtx_lock()` / `mtx_unlock()` 调用不匹配。

**修复：** 检查你的 init/destroy 对。使用启用 WITNESS 的内核（内核配置中的 `options WITNESS`）来捕捉锁违规。

---

**问题：** 统计或标志似乎随机/损坏。

**可能原因：** 没有锁定的并发访问，或在 `detach()` 释放后访问 softc。

**修复：** 确保所有共享状态受互斥锁保护。确保 `detach()` 返回后没有代码路径（回调、定时器、线程）可以到达驱动程序。

### 快速自检

在前进之前，确保你理解：

1. **什么是 softc？**
   答：保存所有驱动程序状态的每设备私有数据结构。

2. **谁分配 softc？**
   答：Newbus，根据 `driver_t` 结构中声明的大小。

3. **你何时必须初始化互斥锁？**
   答：在 `attach()` 中，在任何可能使用它的代码之前。

4. **你何时必须销毁互斥锁？**
   答：在 `detach()` 中，在函数返回之前。

5. **为什么如果 `is_open` 为真我们拒绝 detach？**
   答：为了防止在用户程序仍然打开设备时释放资源，这会导致崩溃。

如果这些答案清楚，你准备好在下一节添加**日志规范**。

---

## 日志规范与 dmesg 卫生

一个行为良好的驱动程序**在应该说话时说话**，**在不应该说话时保持安静**。日志太多会淹没 `dmesg` 使调试更难；日志太少在出现问题时让用户和开发人员蒙在鼓里。本节教你在 FreeBSD 驱动程序中**何时、什么以及如何记录日志**。

到本节结束时，你将知道：

- 哪些事件**必须**记录（attach、错误、关键状态变化）
- 哪些事件**应该**记录（可选、调试级别信息）
- 哪些事件**绝不能**记录（每次数据包/每次操作的垃圾信息）
- 如何有效使用 `device_printf()`
- 如何为热路径创建速率限制日志
- 如何使你的日志可读和可操作

### 为什么日志重要

当驱动程序行为异常时，`dmesg` 通常是开发人员和用户首先看的地方。好的日志回答这样的问题：

- 驱动程序是否成功挂载？
- 它发现了什么硬件？
- 是否发生错误？为什么？
- 设备是否可操作或处于错误状态？

坏的日志会淹没控制台、隐藏关键消息或遗漏重要细节。

**心智模型：** 日志就像医生在检查期间做笔记。记录足够的信息以便以后诊断问题，但不要记录每次心跳。

### 驱动程序日志的黄金法则

**规则 1：记录生命周期事件**

始终记录：

- 成功的 attach（每个设备一行）
- Attach 失败（带原因）
- 成功的 detach（可选但推荐）
- Detach 失败（带原因）

**示例：**

```c
device_printf(dev, "Attached successfully\n");
device_printf(dev, "Failed to allocate memory resource: error %d\n", error);
```

---

**规则 2：记录错误**

当出现问题时，**始终记录什么和为什么**。包括：

- 什么操作失败
- 错误代码（errno 值）
- 上下文（如果相关）

**示例：**

```c
if (error != 0) {
        device_printf(dev, "Could not allocate IRQ resource: error %d\n", error);
        return (error);
}
```

**坏示例：**

```c
if (error != 0) {
        return (error);  /* User sees nothing! */
}
```

---

**规则 3：永远不要在热路径中记录日志**

"热路径" = 在正常操作期间频繁运行的代码（每个数据包、每次中断、每次读/写调用）。

**永远不要这样做：**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int flag)
{
        device_printf(dev, "Read called\n");  /* BAD: spams logs */
        ...
}
```

**为什么？** 如果程序在循环中从你的设备读取，你会每秒产生数千行日志，使控制台无法使用。

**何时记录热路径事件：** 仅在开发期间调试时，并且由默认禁用的调试标志或 sysctl 保护。

---

**规则 4：对设备特定消息使用 device_printf()**

`device_printf()` 自动为你的消息加上设备名称前缀：

```c
device_printf(dev, "Interrupt timeout\n");
```

输出：

```text
myfirst0: Interrupt timeout
```

这立即使**哪个设备**在说话变得清楚，特别是当存在多个实例时。

**不要使用普通的 `printf()`：**

```c
printf("Interrupt timeout\n");  /* Which device? Unknown. */
```

---

**规则 5：在重复错误路径中速率限制警告**

如果错误可能快速重复（例如，每帧的 DMA 超时），速率限制它：

```c
static int
myfirst_check_fifo(struct myfirst_softc *sc)
{
        if (fifo_is_full(sc)) {
                if (sc->log_fifo_full == 0) {
                        device_printf(sc->dev, "FIFO full, dropping packets\n");
                        sc->log_fifo_full = 1;  /* Only log once until cleared */
                }
                return (ENOSPC);
        }
        sc->log_fifo_full = 0;  /* Clear flag when condition resolves */
        return (0);
}
```

**此模式记录第一次出现，抑制重复，并在条件改变时再次记录。**

---

**规则 6：简洁和可操作**

比较：

**坏：**

```c
device_printf(dev, "Something went wrong in the code here\n");
```

**好：**

```c
device_printf(dev, "Failed to map BAR0 MMIO region: error %d\n", error);
```

好的示例告诉你**什么**失败了、**哪里**（BAR0）以及**如何**（错误代码）。

### 常见事件的日志模式

**Attach 成功：**

```c
device_printf(dev, "Attached successfully, hardware rev %d.%d\n",
    hw_major, hw_minor);
```

**Attach 失败：**

```c
device_printf(dev, "Attach failed: could not allocate IRQ\n");
goto fail;
```

**Detach：**

```c
device_printf(dev, "Detached, uptime %lu seconds\n",
    (unsigned long)(ticks - sc->attach_ticks) / hz);
```

**资源分配失败：**

```c
if (sc->mem_res == NULL) {
        device_printf(dev, "Could not allocate memory resource\n");
        error = ENXIO;
        goto fail;
}
```

**意外硬件状态：**

```c
if (status & DEVICE_ERROR_BIT) {
        device_printf(dev, "Hardware reported error 0x%x\n", status);
        /* attempt recovery or fail */
}
```

**首次打开：**

```c
if (sc->open_count == 0) {
        device_printf(dev, "Device opened for the first time\n");
}
```

（但仅当这不寻常或值得注意时；不要在生产环境中记录每次打开。）

### 速率限制日志宏（高级预览）

对于可能快速重复的错误，你可以定义速率限制日志宏：

```c
#define MYFIRST_RATELIMIT_HZ 1  /* Max once per second */

static int
myfirst_log_ratelimited(struct myfirst_softc *sc, const char *fmt, ...)
{
        static time_t last_log = 0;
        time_t now;
        va_list ap;

        now = time_second;
        if (now - last_log < MYFIRST_RATELIMIT_HZ)
                return (0);  /* Too soon, skip */

        last_log = now;

        va_start(ap, fmt);
        device_vprintf(sc->dev, fmt, ap);
        va_end(ap);

        return (1);
}
```

**使用：**

```c
if (error_condition) {
        myfirst_log_ratelimited(sc, "DMA timeout occurred\n");
}
```

这限制日志**每秒一次**，即使条件触发数千次。

**何时使用：** 仅用于可能淹没日志的热路径错误（中断风暴、队列溢出等）。对于 attach/detach 或罕见错误不需要。

### 开发与生产期间的日志内容

**开发（详细）：**

- 每个函数进入/退出（由调试标志保护）
- 寄存器读/写
- 状态转换
- 资源分配/释放

**生产（安静）：**

- Attach/detach 生命周期
- 错误
- 关键状态变化（链路启用/禁用、设备重置）
- 重复错误的首次出现

**过渡：** 开始时详细，然后在驱动程序稳定时精简。留下编译时或 sysctl 保护背后的调试日志以供将来故障排除。

### 使用 Sysctl 进行调试日志

不要硬编码详细程度，暴露一个 sysctl：

```c
static int myfirst_debug = 0;
SYSCTL_INT(_hw_myfirst, OID_AUTO, debug, CTLFLAG_RWTUN,
    &myfirst_debug, 0, "Enable debug logging");
```

然后包装调试日志：

```c
if (myfirst_debug) {
        device_printf(dev, "DEBUG: entering attach\n");
}
```

**好处：** 用户或开发人员可以启用日志而无需重新编译：

```bash
% sysctl hw.myfirst.debug=1
```

我们将在下一节详细介绍 sysctl；这只是预览。

### 检查日志

**查看所有内核消息：**

```bash
% dmesg -a
```

**查看最近的消息：**

```bash
% dmesg | tail -n 20
```

**搜索你的驱动程序：**

```bash
% dmesg | grep myfirst
```

**清除消息缓冲区（如果反复测试）：**

```bash
% sudo dmesg -c > /dev/null
```

（不总是明智，但在你想为测试准备干净状态时有用。）

### 示例：myfirst 中的日志

让我们为驱动程序添加规范的日志。更新 `attach()` 和 `detach()`：

**更新的 attach：**

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);
        sc->dev = dev;
        sc->unit = device_get_unit(dev);

        /* Initialize mutex */
        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

        /* Record attach time */
        sc->attach_ticks = ticks;
        sc->is_attached = 1;

        /* Log attach success */
        device_printf(dev, "Attached successfully at tick %lu\n",
            (unsigned long)sc->attach_ticks);

        return (0);
}
```

**更新的 detach：**

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc;
        uint64_t uptime_ticks;

        sc = device_get_softc(dev);

        /* Refuse detach if open */
        if (sc->is_open) {
                device_printf(dev, "Cannot detach: device is open\n");
                return (EBUSY);
        }

        /* Calculate uptime */
        uptime_ticks = ticks - sc->attach_ticks;

        /* Log detach */
        device_printf(dev, "Detaching: uptime %lu ticks, opened %lu times\n",
            (unsigned long)uptime_ticks,
            (unsigned long)sc->open_count);

        /* Cleanup */
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;

        return (0);
}
```

**我们正在记录什么：**

- **Attach：** 确认成功并记录时间。
- **Detach 拒绝：** 如果设备打开，解释为什么 detach 失败。
- **Detach 成功：** 显示运行时间和使用统计。

这为用户和开发人员提供了对生命周期事件的清晰可见性。

### 常见日志错误

**错误 1：在持有锁时记录日志**

**错误：**

```c
mtx_lock(&sc->mtx);
device_printf(dev, "Locked, doing work\n");  /* Can cause priority inversion */
/* ... work ... */
mtx_unlock(&sc->mtx);
```

**为什么错误：** `device_printf()` 可以阻塞（获取内部锁）。在持有互斥锁时调用它可能导致死锁或优先级反转。

**正确：**

```c
mtx_lock(&sc->mtx);
/* ... work ... */
mtx_unlock(&sc->mtx);

device_printf(dev, "Work completed\n");  /* Log after releasing lock */
```

---

**错误 2：可能交错的多行日志**

**错误：**

```c
printf("myfirst0: Attach starting\n");
printf("myfirst0: Step 1\n");
printf("myfirst0: Step 2\n");
```

**为什么错误：** 如果另一个驱动程序或内核组件在你的行之间记录日志，你的消息会变得碎片化。

**正确：**

```c
device_printf(dev, "Attach starting: step 1, step 2 completed\n");
```

或使用单个 `sbuf`（字符串缓冲区）并打印一次（高级）。

---

**错误 3：记录敏感数据**

不要记录：

- 用户数据（数据包内容、文件数据等）
- 加密密钥或秘密
- 任何违反隐私期望的东西

**始终假设日志是公开的。**

### 快速自检

在前进之前，确认你理解：

1. **你何时必须记录日志？**
   答：生命周期事件（attach/detach）、错误、关键状态变化。

2. **你何时不应该记录日志？**
   答：热路径（中断、读/写循环、每次数据包操作）。

3. **为什么使用 `device_printf()` 而不是 `printf()`？**
   答：自动包含设备名称，使日志更清晰。

4. **如何速率限制可能快速重复的日志？**
   答：使用标志或时间戳跟踪上次日志时间，并抑制重复。

5. **每个错误日志应该包含什么？**
   答：什么失败了、为什么（错误代码）以及足够诊断的上下文。

如果这些清楚，你准备好添加你的第一个用户可见界面：`/dev` 节点。

---

## 临时用户界面：/dev（仅预览）

每个驱动程序都需要一种让用户程序与其交互的方式。对于字符设备，该界面是 `/dev` 中的**设备节点**。在本节中，我们将创建 `/dev/myfirst0`，但我们还不会实现完整的 I/O，只是足以展示模式并证明设备可以从用户空间到达。

这是**预览**，不是完整实现。真正的 `read()` 和 `write()` 语义在**第 8 章和第 9 章**中出现。在这里，我们专注于：

- 使用 `make_dev_s()` 创建 `/dev` 节点
- 用占位方法定义 `cdevsw`（字符设备开关）
- 处理 `open()` 和 `close()` 以跟踪设备状态
- 在 `detach()` 中清理设备节点

把这想象成在装修房子之前**接通前门**。门打开和关闭，但房间是空的。

### 什么是字符设备开关（cdevsw）？

**cdevsw** 是一个包含字符设备操作函数指针的结构：`open`、`close`、`read`、`write`、`ioctl`、`mmap` 等。当用户程序调用 `open("/dev/myfirst0", ...)` 时，内核查找与该设备节点关联的 cdevsw 并调用你的 `d_open` 函数。

**结构定义**（缩写）：

```c
struct cdevsw {
        int     d_version;    /* D_VERSION (API version) */
        d_open_t        *d_open;      /* open(2) handler */
        d_close_t       *d_close;     /* close(2) handler */
        d_read_t        *d_read;      /* read(2) handler */
        d_write_t       *d_write;     /* write(2) handler */
        d_ioctl_t       *d_ioctl;     /* ioctl(2) handler */
        const char *d_name;   /* Device name */
        /* ... more fields ... */
};
```

**关键洞察：** 你为设备支持的操作提供实现，其他保留 `NULL`（内核将其解释为"不支持"或"默认行为"）。

### 为 myfirst 定义 cdevsw

我们将定义一个带有 `open` 和 `close` 处理程序的最小 cdevsw，并暂时占位 `read` / `write`。

在 `myfirst.c` 的 softc 定义之后附近添加这个：

```c
/* Forward declarations for cdevsw methods */
static d_open_t         myfirst_open;
static d_close_t        myfirst_close;
static d_read_t         myfirst_read;
static d_write_t        myfirst_write;

/*
 * Character device switch.
 *
 * Maps system calls to our driver functions.
 */
static struct cdevsw myfirst_cdevsw = {
        .d_version =    D_VERSION,
        .d_open =       myfirst_open,
        .d_close =      myfirst_close,
        .d_read =       myfirst_read,
        .d_write =      myfirst_write,
        .d_name =       "myfirst",
};
```

**这意味着什么：**

- `d_version = D_VERSION` - 必需的 API 版本戳。
- `d_open = myfirst_open` - 当用户调用 `open("/dev/myfirst0", ...)` 时，内核调用 `myfirst_open()`。
- `close`、`read`、`write` 同理。
- `d_name = "myfirst"` - 设备节点的基本名称（与单元号结合形成 `myfirst0`、`myfirst1` 等）。

### 实现 open()

当用户程序打开设备节点时调用 `open()` 处理程序。这是你以下操作的机会：

- 验证设备已准备好
- 跟踪打开状态（增加计数器、设置标志）
- 如果设备无法打开则返回错误（例如，独占访问、硬件未就绪）

添加这个函数：

```c
/*
 * open() handler.
 *
 * Called when a user program opens /dev/myfirst0.
 */
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc;

        sc = dev->si_drv1;  /* Retrieve softc from cdev */

        if (sc == NULL || !sc->is_attached) {
                return (ENXIO);  /* Device not ready */
        }

        mtx_lock(&sc->mtx);
        if (sc->is_open) {
                mtx_unlock(&sc->mtx);
                return (EBUSY);  /* Only allow one opener (exclusive access) */
        }

        sc->is_open = 1;
        sc->open_count++;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "Device opened (count: %lu)\n",
            (unsigned long)sc->open_count);

        return (0);
}
```

**这做什么：**

- **`sc = dev->si_drv1`** - 检索 softc。当我们创建设备节点时，我们将 softc 指针存放在这里。
- **`if (!sc->is_attached)`** - 健全性检查。如果设备未挂载，拒绝打开。
- **`if (sc->is_open) return (EBUSY)`** - 强制独占访问（一次只有一个打开者）。真正的设备可能允许多个打开者；这只是一个简单示例。
- **`sc->is_open = 1`** - 将设备标记为已打开。
- **`sc->open_count++`** - 增加生命周期打开计数器。
- **`device_printf()`** - 记录打开事件（暂时；你会在生产中删除这个）。

**锁定规范：** 我们在检查和更新 `is_open` 时持有互斥锁，确保线程安全。

### 实现 close()

当对打开设备的最后一个引用被释放时调用 `close()` 处理程序。在这里清理打开特定的状态。

添加这个函数：

```c
/*
 * close() handler.
 *
 * Called when the user program closes /dev/myfirst0.
 */
static int
myfirst_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
        struct myfirst_softc *sc;

        sc = dev->si_drv1;

        if (sc == NULL) {
                return (ENXIO);
        }

        mtx_lock(&sc->mtx);
        sc->is_open = 0;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "Device closed\n");

        return (0);
}
```

**这做什么：**

- 清除 `is_open` 标志。
- 记录关闭事件。
- 返回 0（成功）。

**简单模式：** `open()` 设置标志，`close()` 清除它们。

### 占位 read() 和 write()

我们将实现返回成功但什么都不做的最小占位。这证明设备节点正确连接而不承诺 I/O 语义。

**占位 read()：**

```c
/*
 * read() handler (stubbed).
 *
 * For now, just return EOF (0 bytes read).
 * Real implementation in Chapter 9.
 */
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* Return EOF immediately */
        return (0);
}
```

**占位 write()：**

```c
/*
 * write() handler (stubbed).
 *
 * For now, pretend we wrote everything.
 * Real implementation in Chapter 9.
 */
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* Pretend we consumed all bytes */
        uio->uio_resid = 0;
        return (0);
}
```

**这些做什么：**

- **`read()`** - 返回 0（EOF），表示"没有可用数据"。
- **`write()`** - 设置 `uio->uio_resid = 0`，表示"所有字节已写入"。

用户程序会将其视为"接受写入但丢弃它们"和"读取立即返回 EOF"的设备。暂时不有用，但证明管道工作。

### 在 attach() 中创建设备节点

现在我们将所有内容整合在一起。在 `attach()` 中，创建 `/dev` 节点并将其与 softc 关联。

在 `myfirst_attach()` 的末尾，`return (0)` 之前添加以下代码：

```c
        /* 创建 /dev 节点 */
        {
                struct make_dev_args args;
                int error;

                make_dev_args_init(&args);
                args.mda_devsw = &myfirst_cdevsw;
                args.mda_uid = UID_ROOT;
                args.mda_gid = GID_WHEEL;
                args.mda_mode = 0600;  /* rw------- (仅 root) */
                args.mda_si_drv1 = sc;  /* 存储 softc 指针 */

                error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
                if (error != 0) {
                        device_printf(dev, "创建设备节点失败: 错误 %d\n", error);
                        mtx_destroy(&sc->mtx);
                        return (error);
                }
        }

        device_printf(dev, "已创建 /dev/%s\n", devtoname(sc->cdev));
```

**这些代码做什么：**

- **`make_dev_args_init(&args)`** - 用默认值初始化 args 结构。
- **`args.mda_devsw = &myfirst_cdevsw`** - 将此 cdev 与我们的 cdevsw 关联。
- **`args.mda_uid / gid / mode`** - 设置所有权和权限。`0600` 表示仅 root 可读写。
- **`args.mda_si_drv1 = sc`** - 存储 softc 指针，以便 `open()` / `close()` 可以获取它。
- **`make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit)`** - 创建 `/dev/myfirst0`（或 `myfirst1` 等，基于单元号）。
- **错误处理：** 如果 `make_dev_s()` 失败，销毁互斥锁并返回错误。

**重要：** 我们将 `struct cdev *` 保存在 `sc->cdev` 中，以便稍后在 `detach()` 中销毁它。

**将 cdev 字段添加到 softc：**

更新 `struct myfirst_softc`：

```c
struct myfirst_softc {
        device_t        dev;
        int             unit;
        struct mtx      mtx;
        uint64_t        attach_ticks;
        uint64_t        open_count;
        uint64_t        bytes_read;
        int             is_attached;
        int             is_open;

        struct cdev     *cdev;  /* /dev 节点 */
};
```

### 在 detach() 中销毁设备节点

在 `detach()` 中，必须在 softc 被释放之前移除 `/dev` 节点。

在 `myfirst_detach()` 开头，`is_open` 检查之后添加以下代码：

```c
        /* 销毁 /dev 节点 */
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }
```

**这些代码做什么：**

- **`destroy_dev(sc->cdev)`** - 从文件系统中移除 `/dev/myfirst0`。任何打开的文件描述符都会失效，后续对它们的操作返回错误。
- **`sc->cdev = NULL`** - 清除指针（防御性编程）。

**顺序很重要：** 在销毁互斥锁或释放其他资源之前销毁设备节点。这确保在 detach 开始拆除资源后，没有用户空间操作可以到达你的驱动程序。

### 构建、测试和验证

让我们编译并测试新的设备节点：

**1. 清理并构建：**

```bash
% make clean && make
```

**2. 加载驱动程序：**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
```

**3. 检查设备节点：**

```bash
% ls -l /dev/myfirst0
crw-------  1 root  wheel  0x5a Nov  6 15:45 /dev/myfirst0
```

成功！设备节点已存在。

**4. 测试打开和关闭：**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
(无输出，立即 EOF)
```

检查 dmesg：

```bash
% dmesg | tail -n 2
myfirst0: Device opened (count: 1)
myfirst0: Device closed
```

你的 `open()` 和 `close()` 处理程序已运行。

**5. 测试写入：**

```bash
% sudo sh -c 'echo "hello" > /dev/myfirst0'
% dmesg | tail -n 2
myfirst0: Device opened (count: 2)
myfirst0: Device closed
```

写入成功（尽管数据被丢弃）。

**6. 卸载驱动程序：**

```bash
% sudo kldunload myfirst
% ls -l /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

设备节点在卸载时被正确销毁。

### 刚刚发生了什么？

- 你创建了字符设备开关（`cdevsw`），将系统调用映射到你的函数。
- 你实现了跟踪状态的 `open()` 和 `close()` 处理程序。
- 你将 `read()` 和 `write()` 打桩，证明管道工作正常。
- 你在 `attach()` 中创建了 `/dev/myfirst0`，在 `detach()` 中销毁了它。
- 用户程序现在可以 `open("/dev/myfirst0", ...)` 并与你的驱动程序交互。

### 常见设备节点错误

**错误 1：忘记在 detach 中销毁设备节点**

**错误：**

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        mtx_destroy(&sc->mtx);
        return (0);
        /* 忘记 destroy_dev(sc->cdev)! */
}
```

**为什么错误：** 设备节点在卸载后仍然存在。尝试打开它会导致内核崩溃（代码已消失，但节点仍在）。

**正确：** 始终在 detach 中调用 `destroy_dev()`。

---

**错误 2：在 open/close 中访问 softc 时不检查 is_attached**

**错误：**

```c
static int
myfirst_open(struct cdev *dev, ...)
{
        struct myfirst_softc *sc = dev->si_drv1;
        /* 不检查 sc 或 sc->is_attached 是否有效 */
        mtx_lock(&sc->mtx);  /* 可能是 NULL 或已释放! */
        ...
}
```

**为什么错误：** 如果 `detach()` 并发运行，softc 可能已失效。

**正确：** 在访问状态之前检查 `sc != NULL` 和 `sc->is_attached`。

---

**错误 3：使用 make_dev() 而不是 make_dev_s()**

**旧模式：**

```c
sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT, GID_WHEEL, 0600, "myfirst%d", sc->unit);
if (sc->cdev == NULL) {
        /* 错误处理 */
}
```

**为什么过时：** `make_dev()` 可能失败并返回 NULL，需要笨拙的错误检查。

**现代模式：** `make_dev_s()` 返回错误代码，使错误处理更清晰：

```c
error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
if (error != 0) {
        /* 处理错误 */
}
```

**在新代码中优先使用 `make_dev_s()`**。

### 快速自检

在继续之前，确认：

1. **什么是 cdevsw？**
   答案：将系统调用（`open`、`read`、`write` 等）映射到驱动程序函数的结构。

2. **open() 如何获取 softc？**
   答案：通过 `dev->si_drv1`，我们在创建设备节点时设置的。

3. **何时必须调用 destroy_dev()？**
   答案：在 `detach()` 中，在 softc 被释放之前。

4. **为什么在 open() 中检查 `is_attached`？**
   答案：确保设备尚未开始分离，这可能导致访问已释放的内存。

5. **占位 read() 返回什么？**
   答案：0（EOF），表示没有可用数据。

如果这些都清楚了，你就可以在下一节中添加 **通过 sysctl 实现可观测性**。

---


## 小型控制平面：只读 sysctl

`/dev` 中的设备节点让用户程序发送和接收数据，但这不是将驱动程序暴露给外部世界的唯一方式。**Sysctl** 提供了一个轻量级的控制和观测平面，让用户和管理员可以查询驱动程序状态、读取统计数据，并（可选地）在运行时调整参数。

在本节中，我们将添加一个 **只读 sysctl**，暴露基本的驱动程序统计数据。这让你初步了解 FreeBSD 的 sysctl 基础设施，而无需完全承诺实现读写可调参数或复杂层次结构（这些内容将在第 5 部分深入探讨可观测性和调试时返回）。

完成后，你将拥有：

- 在 `dev.myfirst.0.*` 下显示附加时间、打开次数和读取字节数的 sysctl 节点
- 理解静态与动态 sysctl
- 一个可以稍后扩展为更复杂可观测性的模式

### 为什么 Sysctl 很重要

Sysctl 提供 **带外可观测性**，这是一种无需打开设备或触发 I/O 即可检查驱动程序状态的方式。它们对于以下方面至关重要：

- **调试：** "驱动程序真的附加了吗？当前状态是什么？"
- **监控：** "此设备已打开多少次？有任何错误吗？"
- **调优：** （稍后介绍的读写 sysctl）"调整缓冲区大小或超时值。"

**示例用例：** 网络接口可能暴露 `dev.em.0.rx_packets` 和 `dev.em.0.tx_errors`，以便监控工具可以跟踪性能而无需分析数据包流。

**心智模型：** Sysctl 就像驱动程序侧面的"状态仪表板"，通过 `sysctl` 命令可见，不影响正常操作。

### FreeBSD Sysctl 树

Sysctl 是分层组织的，类似文件系统：

```ini
kern.ostype = "FreeBSD"
hw.ncpu = 8
dev.em.0.rx_packets = 123456
```

**常见顶级分支：**

- `kern.*` - 内核参数
- `hw.*` - 硬件信息
- `dev.*` - 设备特定节点（这是你的驱动程序所在位置）
- `net.*` - 网络栈参数

**你的驱动程序命名空间：** `dev.<驱动名>.<单元>.*`

对于 `myfirst`，即 `dev.myfirst.0.*` 用于第一个实例。

### 静态与动态 Sysctl

**静态 sysctl：**

- 在编译时使用 `SYSCTL_*` 宏声明
- 定义简单，但无法动态创建/销毁
- 适用于驱动程序范围的设置或常量

**示例：**

```c
static int myfirst_debug = 0;
SYSCTL_INT(_hw, OID_AUTO, myfirst_debug, CTLFLAG_RWTUN,
    &myfirst_debug, 0, "启用调试日志");
```

**动态 sysctl：**

- 在运行时创建（通常在 `attach()` 中）
- 可以在 `detach()` 中销毁
- 适用于每设备状态（如 `myfirst0`、`myfirst1` 等的统计）

**本章我们将使用动态 sysctl**，这样每个设备实例都有自己的节点。

### 将 Sysctl 上下文添加到 softc

动态 sysctl 需要 **sysctl 上下文**（`struct sysctl_ctx_list`）来跟踪你创建的节点。这使释上下文时自动清理。

将这些字段添加到 `struct myfirst_softc`：

```c
struct myfirst_softc {
        device_t        dev;
        int             unit;
        struct mtx      mtx;
        uint64_t        attach_ticks;
        uint64_t        open_count;
        uint64_t        bytes_read;
        int             is_attached;
        int             is_open;
        struct cdev     *cdev;

        /* 动态节点的 sysctl 上下文 */
        struct sysctl_ctx_list  sysctl_ctx;
        struct sysctl_oid       *sysctl_tree;  /* 子树的根 */
};
```

**这些字段做什么：**

- `sysctl_ctx` - 跟踪我们创建的所有 sysctl 节点。调用 `sysctl_ctx_free()` 时，所有节点自动销毁。
- `sysctl_tree` - `dev.myfirst.0.*` 的根 OID（对象标识符）。子节点附加于此。

### 在 attach() 中创建 Sysctl 树

将以下代码添加到 `myfirst_attach()`，在创建 `/dev` 节点之后：

```c
        /* 初始化 sysctl 上下文 */
        sysctl_ctx_init(&sc->sysctl_ctx);

        /* 创建设备 sysctl 树: dev.myfirst.0 */
        sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "stats", CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
            "驱动程序统计信息");

        if (sc->sysctl_tree == NULL) {
                device_printf(dev, "创建 sysctl 树失败\n");
                destroy_dev(sc->cdev);
                mtx_destroy(&sc->mtx);
                return (ENOMEM);
        }

        /* 添加单个 sysctl 节点 */
        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "attach_ticks", CTLFLAG_RD,
            &sc->attach_ticks, 0, "驱动程序附加时的时钟计数");

        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "open_count", CTLFLAG_RD,
            &sc->open_count, 0, "设备被打开的次数");

        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "bytes_read", CTLFLAG_RD,
            &sc->bytes_read, 0, "从设备读取的总字节数");

        device_printf(dev, "Sysctl 树已创建于 dev.myfirst.%d.stats\n",
            sc->unit);
```

**这些代码做什么：**

1. **`sysctl_ctx_init(&sc->sysctl_ctx)`** - 初始化上下文（必须首先执行）。

2. **`SYSCTL_ADD_NODE()`** - 创建子树节点 `dev.myfirst.0.stats`。参数：
   - `&sc->sysctl_ctx` - 拥有此节点的上下文。
   - `SYSCTL_CHILDREN(device_get_sysctl_tree(dev))` - 父节点（设备的 sysctl 树）。
   - `OID_AUTO` - 自动分配 OID 号。
   - `"stats"` - 节点名称。
   - `CTLFLAG_RD | CTLFLAG_MPSAFE` - 只读，MP 安全。
   - `0` - 处理函数（节点无）。
   - `"驱动程序统计信息"` - 描述。

3. **`SYSCTL_ADD_U64()`** - 添加 64 位无符号整数 sysctl。参数：
   - `&sc->sysctl_ctx` - 上下文。
   - `SYSCTL_CHILDREN(sc->sysctl_tree)` - 父节点（`stats` 子树）。
   - `OID_AUTO` - 自动分配 OID。
   - `"attach_ticks"` - 叶节点名称。
   - `CTLFLAG_RD` - 只读。
   - `&sc->attach_ticks` - 指向要暴露的变量的指针。
   - `0` - 格式提示（0 = 默认）。
   - `"时钟计数..."` - 描述。

4. **错误处理：** 如果节点创建失败，清理并返回 `ENOMEM`。

**结果：** 你现在有三个 sysctl：

- `dev.myfirst.0.stats.attach_ticks`
- `dev.myfirst.0.stats.open_count`
- `dev.myfirst.0.stats.bytes_read`

### 在 detach() 中销毁 Sysctl 树

清理很简单：释放上下文，所有节点自动销毁。

将以下内容添加到 `myfirst_detach()`，在销毁设备节点之后：

```c
        /* 释放 sysctl 上下文（销毁所有节点） */
        sysctl_ctx_free(&sc->sysctl_ctx);
```

就这样。一行代码清理所有内容。

**为什么安全：** `sysctl_ctx_free()` 遍历上下文列表并移除每个节点。只要你通过上下文创建了所有节点，清理就是自动的。

### 构建、加载和测试 Sysctl

**1. 清理并构建：**

```bash
% make clean && make
```

**2. 加载驱动程序：**

```bash
% sudo kldload ./myfirst.ko
% dmesg | tail -n 4
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
myfirst0: Sysctl tree created under dev.myfirst.0.stats
```

**3. 查询 sysctl：**

```bash
% sysctl dev.myfirst.0.stats
dev.myfirst.0.stats.attach_ticks: 123456
dev.myfirst.0.stats.open_count: 0
dev.myfirst.0.stats.bytes_read: 0
```

**4. 打开设备并再次检查：**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 1
```

计数器增加了！

**5. 卸载并验证清理：**

```bash
% sudo kldunload myfirst
% sysctl dev.myfirst.0.stats
sysctl: unknown oid 'dev.myfirst.0.stats'
```

Sysctl 已正确销毁。

### 让 Sysctl 更有用

目前，sysctl 只暴露原始数字。让我们让它们更用户友好。

**添加人类可读的运行时间 sysctl：**

不暴露原始时钟计数，而是计算以秒为单位的运行时间。

添加处理函数：

```c
/*
 * uptime_seconds 的 Sysctl 处理函数。
 *
 * 计算驱动程序已附加的时间，以秒为单位。
 */
static int
sysctl_uptime_seconds(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        uint64_t uptime;

        uptime = (ticks - sc->attach_ticks) / hz;

        return (sysctl_handle_64(oidp, &uptime, 0, req));
}
```

**在 attach() 中注册：**

```c
        SYSCTL_ADD_PROC(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "uptime_seconds", CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
            sc, 0, sysctl_uptime_seconds, "QU",
            "驱动程序附加后的秒数");
```

**测试：**

```bash
% sysctl dev.myfirst.0.stats.uptime_seconds
dev.myfirst.0.stats.uptime_seconds: 42
```

比原始时钟计数更易读！

### 只读与读写 Sysctl

我们的 sysctl 是只读的（`CTLFLAG_RD`）。要使它们可写，使用 `CTLFLAG_RW` 并添加验证输入的处理函数。

**示例（仅预览，暂不实现）：**

```c
static int
sysctl_set_debug_level(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new_level, error;

        new_level = sc->debug_level;
        error = sysctl_handle_int(oidp, &new_level, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);

        if (new_level < 0 || new_level > 3)
                return (EINVAL);  /* 拒绝无效值 */

        sc->debug_level = new_level;
        device_printf(sc->dev, "Debug level set to %d\n", new_level);

        return (0);
}
```

我们将在第 5 部分讨论调试和可观测性工具时回到读写 sysctl。目前，只读暴露已足够。

### Sysctl 最佳实践

**1. 暴露有意义的指标**

- 计数器（数据包、错误、打开、关闭）
- 状态标志（已附加、已打开、已启用）
- 派生值（运行时间、吞吐量、利用率）

**不要暴露：**

- 内部指针或地址（安全风险）
- 无意义的原始数据（使用处理函数格式化良好）

---

**2. 使用描述性名称和描述**

**好：**

```c
SYSCTL_ADD_U64(..., "rx_packets", ..., "接收的数据包");
```

**差：**

```c
SYSCTL_ADD_U64(..., "cnt1", ..., "计数器");
```

---

**3. 将相关 sysctl 分组在子树下**

```text
dev.myfirst.0.stats.*    （统计信息）
dev.myfirst.0.config.*   （可调参数）
dev.myfirst.0.debug.*    （调试标志和计数器）
```

---

**4. 保护并发访问**

如果 sysctl 读取或写入共享状态，持有适当的锁：

```c
static int
sysctl_read_counter(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        uint64_t value;

        mtx_lock(&sc->mtx);
        value = sc->some_counter;
        mtx_unlock(&sc->mtx);

        return (sysctl_handle_64(oidp, &value, 0, req));
}
```

---

**5. 在 detach 中清理**

始终在 detach 中调用 `sysctl_ctx_free(&sc->sysctl_ctx)`，否则会泄漏 OID。

### 常见 Sysctl 错误

**错误 1：忘记 sysctl_ctx_init**

**错误：**

```c
SYSCTL_ADD_NODE(&sc->sysctl_ctx, ...);  /* 上下文未初始化! */
```

**为什么错误：** 未初始化的上下文会导致崩溃或泄漏。

**正确：** 在添加节点之前，在 attach 中调用 `sysctl_ctx_init(&sc->sysctl_ctx)`。

---

**错误 2：在 detach 中不释放上下文**

**错误：**

```c
static int
myfirst_detach(device_t dev)
{
        /* ... 销毁其他资源 ... */
        return (0);
        /* 忘记 sysctl_ctx_free! */
}
```

**为什么错误：** Sysctl 节点在卸载后仍然存在。下次访问会崩溃。

**正确：** 始终在 detach 中 `sysctl_ctx_free(&sc->sysctl_ctx)`。

---

**错误 3：暴露原始指针**

**错误：**

```c
SYSCTL_ADD_PTR(..., "softc_addr", ..., &sc, ...);  /* 安全漏洞! */
```

**为什么错误：** 泄露内核地址空间布局（KASLR 绕过）。

**正确：** 永远不要通过 sysctl 暴露指针。

### 快速自检

在继续之前，确认：

1. **什么是 sysctl？**
   答案：通过 `sysctl` 命令暴露的内核变量或计算值。

2. **驱动程序 sysctl 位于哪里？**
   答案：在 `dev.<驱动名>.<单元>.*` 下。

3. **在添加节点之前必须在 attach() 中调用什么？**
   答案：`sysctl_ctx_init(&sc->sysctl_ctx)`。

4. **什么在 detach() 中清理所有 sysctl 节点？**
   答案：`sysctl_ctx_free(&sc->sysctl_ctx)`。

5. **为什么使用处理函数而不是直接暴露变量？**
   答案：计算派生值（如运行时间）或验证写入。

如果这些都清楚了，你就准备好学习下一节的 **错误处理和清理展开**。

---

## 错误路径与清理展开

到目前为止，我们编写 `attach()` 时假设一切都会成功。但真正的驱动程序必须优雅地处理失败：如果内存分配失败、资源不可用，或硬件行为异常，你的驱动程序必须 **撤销已开始的操作** 并返回错误，不留下部分状态。

本节教授 **单标签展开模式**，这是 FreeBSD 错误清理的标准惯用语。掌握这个，你的驱动程序就永远不会泄漏资源，无论失败发生在哪里。

### 为什么错误处理很重要

糟糕的错误处理会导致：

- **资源泄漏**（内存、锁、设备节点）
- **内核崩溃**（访问已释放内存、双重释放）
- **不一致状态**（设备半附加、锁已初始化但未销毁）

**现实影响：** 错误处理草率的驱动程序在正常操作期间可能工作正常，但在遇到异常失败时（内存不足、硬件缺失等）会导致系统崩溃。

**你的目标：** 确保 `attach()` 要么完全成功，要么完全失败，没有中间状态。

### 单标签展开模式

FreeBSD 内核代码使用 **基于 goto 的展开模式** 进行清理。看起来像这样：

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;
        int error;

        sc = device_get_softc(dev);
        sc->dev = dev;

        /* 步骤 1: 初始化互斥锁 */
        mtx_init(&sc->mtx, "myfirst", NULL, MTX_DEF);

        /* 步骤 2: 分配内存资源（示例） */
        sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
            &sc->mem_rid, RF_ACTIVE);
        if (sc->mem_res == NULL) {
                device_printf(dev, "分配内存资源失败\n");
                error = ENXIO;
                goto fail_mtx;
        }

        /* 步骤 3: 创建设备节点 */
        error = create_dev_node(sc);
        if (error != 0) {
                device_printf(dev, "创建设备节点失败: %d\n", error);
                goto fail_mem;
        }

        /* 步骤 4: 创建 sysctl */
        error = create_sysctls(sc);
        if (error != 0) {
                device_printf(dev, "创建 sysctl 失败: %d\n", error);
                goto fail_dev;
        }

        device_printf(dev, "Attached successfully\n");
        return (0);

fail_dev:
        destroy_dev(sc->cdev);
fail_mem:
        bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
fail_mtx:
        mtx_destroy(&sc->mtx);
        return (error);
}
```

**工作原理：**

- 每个初始化步骤都有对应的清理标签。
- 如果步骤失败，跳转到撤销 **已完成的操作** 的标签。
- 标签按初始化的 **逆序** 排列。
- 每个标签贯穿到下一个，所以步骤 4 的失败会撤销 3、2 和 1。

**为什么用这个模式？**

- **集中清理：** 所有错误路径汇聚到一个展开序列。
- **易于维护：** 添加新步骤只需添加一个 goto 和一个清理标签。
- **无重复：** 不在每个错误分支重复清理代码。

### 将模式应用于 myfirst

让我们重构 `attach()` 以正确处理错误。

**之前（无错误处理）：**

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        mtx_init(&sc->mtx, ...);
        create_dev_node(sc);
        create_sysctls(sc);

        return (0);  /* 如果失败怎么办？ */
}
```

**之后（带清理展开）：**

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;
        struct make_dev_args args;
        int error;

        sc = device_get_softc(dev);
        sc->dev = dev;
        sc->unit = device_get_unit(dev);

        /* 步骤 1: 初始化互斥锁 */
        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

        /* 步骤 2: 记录附加时间并初始化状态 */
        sc->attach_ticks = ticks;
        sc->is_attached = 1;
        sc->is_open = 0;
        sc->open_count = 0;
        sc->bytes_read = 0;

        /* 步骤 3: 创建 /dev 节点 */
        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_WHEEL;
        args.mda_mode = 0600;
        args.mda_si_drv1 = sc;

        error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
        if (error != 0) {
                device_printf(dev, "创建设备节点失败: %d\n", error);
                goto fail_mtx;
        }

        /* 步骤 4: 初始化 sysctl 上下文 */
        sysctl_ctx_init(&sc->sysctl_ctx);

        /* 步骤 5: 创建 sysctl 树 */
        sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "stats", CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
            "驱动程序统计信息");

        if (sc->sysctl_tree == NULL) {
                device_printf(dev, "创建 sysctl 树失败\n");
                error = ENOMEM;
                goto fail_dev;
        }

        /* 步骤 6: 添加 sysctl 节点 */
        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "attach_ticks", CTLFLAG_RD,
            &sc->attach_ticks, 0, "驱动程序附加时的时钟计数");

        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "open_count", CTLFLAG_RD,
            &sc->open_count, 0, "设备被打开的次数");

        SYSCTL_ADD_U64(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "bytes_read", CTLFLAG_RD,
            &sc->bytes_read, 0, "从设备读取的总字节数");

        device_printf(dev, "Attached successfully\n");
        return (0);

        /* 错误展开（按初始化逆序） */
fail_dev:
        destroy_dev(sc->cdev);
        sysctl_ctx_free(&sc->sysctl_ctx);
fail_mtx:
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;
        return (error);
}
```

**关键改进：**

- 检查每个可能失败的操作。
- 失败时跳转到适当的清理标签。
- 展开序列正好撤销已成功的操作。
- 所有路径返回错误代码（失败后绝不返回成功）。

### 标签命名约定

选择指示需要撤销内容的标签名：

- `fail_mtx` - 销毁互斥锁
- `fail_mem` - 释放内存资源
- `fail_dev` - 销毁设备节点
- `fail_irq` - 释放中断资源

或者使用数字：

- `fail1`, `fail2`, `fail3`

两种都可以，但描述性名称使代码更易读。

### 测试错误路径

**模拟失败** 以验证展开逻辑正确。

在互斥锁初始化后添加故意失败：

```c
        mtx_init(&sc->mtx, ...);

        /* 为测试模拟分配失败 */
        if (1) {  /* 改为 0 以禁用 */
                device_printf(dev, "模拟失败\n");
                error = ENXIO;
                goto fail_mtx;
        }
```

**构建并加载：**

```bash
% make clean && make
% sudo kldload ./myfirst.ko
```

**检查 dmesg：**

```bash
% dmesg | tail -n 2
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Simulated failure
```

**验证清理：**

```bash
% devinfo | grep myfirst
(无输出 - 设备未附加)

% ls /dev/myfirst*
ls: cannot access '/dev/myfirst*': No such file or directory
```

**关键观察：** 驱动程序干净地失败了。无设备节点，无 sysctl 泄漏，无崩溃。

现在禁用模拟失败并再次测试正常附加。

### 常见错误处理错误

**错误 1：不检查返回值**

**错误：**

```c
make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
/* 忘记检查错误! */
```

**为什么错误：** 如果 `make_dev_s()` 失败，`sc->cdev` 可能是 NULL 或垃圾值，而你继续执行，仿佛一切正常。

**正确：** 始终检查 `error` 并相应分支。

---

**错误 2：部分清理**

**错误：**

```c
fail_dev:
        destroy_dev(sc->cdev);
        return (error);
        /* 忘记销毁互斥锁! */
```

**为什么错误：** 互斥锁仍然初始化。下次加载会在重新初始化时崩溃。

**正确：** 每个标签必须撤销 **所有** 之前已初始化的内容。

---

**错误 3：双重清理**

**错误：**

```c
fail_dev:
        destroy_dev(sc->cdev);
        mtx_destroy(&sc->mtx);
        goto fail_mtx;

fail_mtx:
        mtx_destroy(&sc->mtx);  /* 销毁两次! */
        return (error);
}
```

**为什么错误：** 双重释放或双重销毁会导致崩溃。

**正确：** 每个资源应该只清理一次，在对应的标签处。

---

**错误 4：失败后返回成功**

**错误：**

```c
if (error != 0) {
        goto fail_mtx;
}
return (0);  /* 即使跳转到 fail_mtx! */
```

**为什么错误：** goto 绕过了返回，但该模式暗示所有错误路径必须 **返回错误代码**。

**正确：** 确保错误标签以 `return (error)` 结尾。

### 全貌：Attach 和 Detach

**Attach 逻辑：**

1. 按顺序初始化资源。
2. 检查每步是否失败。
3. 失败时，跳转到对应最后成功步骤的展开标签。
4. 展开标签按逆序贯穿，清理所有内容。

**Detach 逻辑：**

Detach 更简单，按 `attach()` 的逆序撤销所有内容，假设完全成功：

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* 如果设备已打开则拒绝 */
        if (sc->is_open) {
                device_printf(dev, "无法分离: 设备已打开\n");
                return (EBUSY);
        }

        device_printf(dev, "正在分离\n");

        /* 按 attach 的逆序撤销 */
        destroy_dev(sc->cdev);                /* 步骤 1: 先释放用户表面 */
        sysctl_ctx_free(&sc->sysctl_ctx);    /* 步骤 2: 释放 sysctl 上下文 */
        mtx_destroy(&sc->mtx);                /* 步骤 3: 最后销毁互斥锁 */
        sc->is_attached = 0;

        return (0);
}
```

**对称性：** 每个 `attach()` 步骤都有对应的 `detach()` 操作，按逆序执行。

**顺序说明：** 我们在释放 sysctl 上下文和销毁互斥锁之前销毁设备（`destroy_dev`）。这遵循第 6 章的陷阱指导："在锁之前销毁设备"。`destroy_dev()` 调用会阻塞，直到所有文件操作排空，确保设备消失后没有代码路径可以到达我们的锁。

### 防御性编程检查清单

在宣布错误处理完成之前，检查：

- [ ] 每个可能失败的函数都检查了
- [ ] 每个错误都设置了 `error` 并跳转到清理标签
- [ ] 每个清理标签正好撤销了之前的内容
- [ ] 标签按初始化的逆序排列
- [ ] `detach()` 按 `attach()` 的逆序撤销了所有内容
- [ ] 没有资源被释放两次
- [ ] 失败时没有资源泄漏

### 快速自检

在继续之前，确认：

1. **什么是单标签展开模式？**
   答案：一种基于 goto 的清理序列，每个标签按初始化的逆序撤销越来越多的资源。

2. **为什么清理标签是逆序的？**
   答案：因为你必须首先撤销最近的步骤，然后是更早的步骤，逆向遍历初始化。

3. **每个错误路径在返回前必须做什么？**
   答案：跳转到适当的清理标签，并确保执行 `return (error)`。

4. **如何测试错误路径？**
   答案：模拟失败（如强制分配失败）并验证清理正确（无泄漏、无崩溃）。

5. **何时 detach 应该拒绝继续？**
   答案：当设备仍在使用时（如 `is_open` 为 true），返回 `EBUSY`。

如果这些都清楚了，你就准备好探索 **FreeBSD 源码树中的真实驱动示例**。

---

## 树内锚点（仅供参考）

你已经从第一性原理构建了一个最小驱动。现在让我们 **锚定你的理解**，指向 FreeBSD 14.3 中展示你刚学到的相同模式的真实驱动。本节是 **导览**，而非详尽演练，把它当作阅读清单，当你想看生产代码如何应用本章经验时参考。

### 为什么要看真实驱动？

真实驱动向你展示：

- 模式如何扩展到复杂硬件
- 代码在上下文中是什么样（完整文件，不只是片段）
- 模式的变体（不同的 attach 逻辑、资源类型、错误处理风格）
- FreeBSD 惯用语和约定实践

**你现在不需要理解每个驱动每行代码**。目标是 **识别你已构建的脚手架**，看它如何扩展到更强大的驱动。

### 锚点 1：`/usr/src/sys/dev/null/null.c`

**这是什么：** `/dev/null`、`/dev/zero` 和 `/dev/full` 伪设备。

**为什么学习它：**

- 最简单的字符设备
- 无硬件、无资源，只有 cdevsw + MOD_LOAD 处理器
- 展示 `read()` 和 `write()` 如何实现（即使微不足道）
- 打桩 I/O 的好参考

**看什么：**

- `cdevsw` 结构（用 `grep -n cdevsw` 找到它们）
- `null_write()` 和 `zero_read()` 处理器
- 模块加载器（`null_modevent()`，用 `grep -n modevent` 找到它）
- `make_dev_credf()` 如何使用（用 `grep -n make_dev` 找到它）

**文件位置：**

```bash
% less /usr/src/sys/dev/null/null.c
```

**快速扫描：**

```bash
% grep -n "cdevsw\|make_dev" /usr/src/sys/dev/null/null.c
```

### 锚点 2：`/usr/src/sys/dev/led/led.c`

**这是什么：** LED 控制框架，被平台特定驱动用来将 LED 暴露为 `/dev/led/*`。

**为什么学习它：**

- 仍然简单，但展示资源管理（callout、链表）
- 演示动态设备创建（每个 LED）
- 使用锁（`struct mtx`）
- 展示驱动如何管理多个实例

**看什么：**

- `ledsc` 结构（文件顶部附近；用 `grep -n ledsc` 找到），类似于你的 softc
- `led_create()` 函数（用 `grep -n "led_create\|led_destroy"` 找到），动态创建设备节点
- `led_destroy()` 函数，清理模式
- 全局 LED 链表如何用互斥锁保护

**文件位置：**

```bash
% less /usr/src/sys/dev/led/led.c
```

**快速扫描：**

```bash
% grep -n "ledsc\|led_create\|led_destroy" /usr/src/sys/dev/led/led.c
```

### 锚点 3：`/usr/src/sys/net/if_tuntap.c`

**这是什么：** `tun` 和 `tap` 伪网络接口（隧道设备）。

**为什么学习它：**

- 混合驱动：字符设备 **和** 网络接口
- 展示如何注册 `ifnet`（网络栈）
- 更复杂的生命周期（克隆设备、每次打开状态）
- 真实世界锁定和并发的好例子

**看什么：**

- `struct tuntap_softc`（用 `grep -n "struct tuntap_softc"` 找到），比你的丰富得多
- `tun_create()` 函数，注册 `ifnet`
- `cdevsw` 以及它如何与网络侧协调
- `if_attach()` 和 `if_detach()` 用于网络集成

**文件位置：**

```bash
% less /usr/src/sys/net/if_tuntap.c
```

**警告：** 这是大型复杂文件（约 2000 行）。不要试图理解一切。专注于：

```bash
% grep -n "tuntap_softc\|if_attach\|make_dev" /usr/src/sys/net/if_tuntap.c | head -20
```

### 锚点 4：`/usr/src/sys/dev/uart/uart_bus_pci.c`

**这是什么：** UART（串口）设备的 PCI 附加粘合代码。

**为什么学习它：**

- 真实硬件驱动（PCI 总线）
- 展示 `probe()` 如何检查 PCI ID
- 演示资源分配（I/O 端口、IRQ）
- `attach()` 中的错误展开

**看什么：**

- `uart_pci_probe()` 函数（用 `grep -n uart_pci_probe` 找到），PCI ID 匹配
- `uart_pci_attach()` 函数，资源分配
- `bus_alloc_resource()` 和 `bus_release_resource()` 的使用
- `device_method_t` 表（用 `grep -n device_method` 找到）

**文件位置：**

```bash
% less /usr/src/sys/dev/uart/uart_bus_pci.c
```

**快速扫描：**

```bash
% grep -n "uart_pci_probe\|uart_pci_attach\|device_method" /usr/src/sys/dev/uart/uart_bus_pci.c
```

**提示：** 这个文件很小（约 250 行）且非常干净。这是真实 Newbus 驱动的好例子。

### 锚点 5：`DRIVER_MODULE` 和 `MODULE_VERSION` 模式

在驱动文件底部寻找这些宏：

```bash
% grep -rn 'DRIVER_MODULE\|MODULE_VERSION' /usr/src/sys/dev/null/ /usr/src/sys/dev/led/
```

你会看到与 `myfirst` 中使用的相同注册模式。对于附加到 `nexus` 并提供自己的 `identify` 方法的驱动，`/usr/src/sys/crypto/aesni/aesni.c` 和 `/usr/src/sys/dev/sound/dummy.c` 中的模式与你所写的最接近。

### 如何使用这些锚点

**1. 从 null.c 开始**

通读整个文件，它很短（约 220 行）。你应该能识别几乎所有内容。

**2. 浏览 led.c**

专注于结构和生命周期（创建/销毁）。不要陷入状态机。

**3. 预览 if_tuntap.c**

打开它，滚动浏览，注意混合结构（cdevsw + ifnet）。不要试图全部理解；只看形状。

**4. 学习 uart_bus_pci.c**

阅读 `probe()` 和 `attach()`。这是你通往真实硬件驱动的桥梁（第 4 部分涵盖）。

**5. 与你的驱动比较**

对每个锚点，问：

- 什么与我的 `myfirst` 驱动相似？
- 什么不同？
- 我看到了什么新概念（callout、ifnet、PCI 资源）？

**6. 记录下一步学习内容**

当你看到不熟悉的东西（如 `callout_reset`、`if_attach`、`bus_alloc_resource`），记下来。这些是后续章节的主题。

### 快速导览：驱动间的常见模式

| 模式                    | null.c       | led.c | if_tuntap.c | uart_pci.c | myfirst.c     |
|-------------------------|--------------|-------|-------------|------------|---------------|
| 使用 `cdevsw`           | 是           | 是    | 是          | 否         | 是            |
| 使用 `ifnet`            | 否           | 否    | 是          | 否         | 否            |
| Newbus probe/attach     | 否           | 否    | 否（克隆）  | 是         | 是            |
| 有 `identify` 方法     | 否           | 否    | 否          | 否         | 是            |
| 模块加载处理器          | 是           | 是    | 是          | 否（Newbus）| 否（Newbus）  |
| 分配 softc              | 否           | 否    | 是          | 是         | 是（Newbus）  |
| 使用锁                  | 否           | 是    | 是          | 是         | 是            |
| 分配总线资源            | 否           | 否    | 否          | 是         | 否            |
| 创建 `/dev` 节点        | 是           | 是    | 是          | 否         | 是            |

### 暂时跳过的内容

阅读这些驱动时，不要卡在：

- 硬件寄存器访问（`bus_read_4`、`bus_write_2`）
- 中断设置（`bus_setup_intr`、处理器注册）
- DMA（`bus_dma_tag_create`、`bus_dmamap_load`）
- 高级锁定（读多锁、锁顺序）
- 网络包处理（`mbuf` 链、`if_transmit`）

你将在专门的章节学习这些。现在，专注于 **结构和生命周期**。

### 自学练习

选择一个锚点（推荐初学者选择 `null.c`）并：

1. 通读整个文件
2. 识别 `cdevsw` 结构
3. 找到 `open`、`close`、`read`、`write` 处理器
4. 追踪模块加载/卸载流程
5. 与你的 `myfirst` 驱动比较

在你的实验日志中写一段话："我从 [驱动名] 学到了 [内容]。"

### 快速自检

进入实验之前，确认：

1. **为什么要看真实驱动？**
   答案：在上下文中看模式、学习惯用语、从最小示例过渡到生产代码。

2. **哪个驱动最简单？**
   答案：`null.c`，它只是伪设备，无状态或资源。

3. **哪个驱动展示混合结构？**
   答案：`if_tuntap.c`，它既是字符设备又是网络接口。

4. **阅读复杂驱动时应跳过什么？**
   答案：硬件特定细节（寄存器、DMA、中断），直到后续章节涵盖它们。

5. **如何使用这些锚点？**
   答案：作为参考示例，不是详细学习材料。浏览、比较、记录新概念、继续。

如果这些都清楚了，你就可以进行 **动手实验**，构建、测试和扩展你的驱动。

---

## 动手实验

你已经阅读了驱动结构、看到了模式、浏览了代码。现在是通过四个实践实验 **构建、测试和验证** 你的理解的时候。每个实验都是一个检查点，确保你在继续之前掌握了概念。

### 实验概览

| 实验 | 重点         | 时长     | 关键学习                     |
|------|--------------|----------|------------------------------|
| 7.1  | 源码寻宝     | 20-30 分钟 | 导航 FreeBSD 源码、识别模式   |
| 7.2  | 构建与加载   | 30-40 分钟 | 编译、加载、验证生命周期      |
| 7.3  | 设备节点     | 30-40 分钟 | 创建 `/dev`、测试 open/close |
| 7.4  | 错误处理     | 30-45 分钟 | 模拟失败、验证展开           |

**总时间：** 如果一次完成所有实验，约 2-2.5 小时。

**先决条件：**

- 第 2 章的 FreeBSD 14.3 实验环境
- 已安装 `/usr/src`
- 基本的 shell 和编辑器技能
- 前面章节的 `~/drivers/myfirst` 项目

让我们开始。

---

### 实验 7.1：源码寻宝

**目标：** 通过查找和识别驱动模式，建立对 FreeBSD 源码树的熟悉度。

**练习的技能：**

- 导航 `/usr/src/sys`
- 使用 `grep` 和 `find`
- 阅读真实驱动代码

**说明：**

**1. 定位 null 驱动：**

```bash
% find /usr/src/sys -name "null.c" -type f
```

预期输出：

```text
/usr/src/sys/dev/null/null.c
```

**2. 打开并扫描：**

```bash
% less /usr/src/sys/dev/null/null.c
```

**3. 找到 cdevsw 结构：**

在 `less` 中输入 `/cdevsw` 并按 Enter 搜索。

你应该会看到定义 `null_cdevsw`、`zero_cdevsw` 和 `full_cdevsw` 的行。

**4. 找到模块事件处理器：**

搜索 `modevent`：

```text
/modevent
```

你应该会看到 `null_modevent()`。

**5. 识别设备创建：**

搜索 `make_dev`：

```text
/make_dev
```

你应该找到三个调用，分别创建 `/dev/null`、`/dev/zero` 和 `/dev/full`。

**6. 与你的驱动比较：**

打开你的 `myfirst.c` 并比较：

- `null.c` 如何创建设备节点？（答案：模块加载器中的 `make_dev_credf`）
- 你的驱动如何创建它们？（答案：`attach()` 中的 `make_dev_s`）

**7. 找到 LED 驱动：**

```bash
% find /usr/src/sys -name "led.c" -path "*/dev/led/*"
```

**8. 扫描 softc：**

```bash
% grep -n "struct ledsc" /usr/src/sys/dev/led/led.c | head -5
```

你应该在文件顶部 `#include` 块之后看到 `struct ledsc` 定义。

**9. 对 if_tuntap 重复：**

```bash
% less /usr/src/sys/net/if_tuntap.c
```

搜索 `tuntap_softc`。注意它比你的最小 softc 丰富多少。

**10. 记录你的发现：**

在实验日志中写下：

```text
实验 7.1 完成：
- 定位了 null.c、led.c、if_tuntap.c
- 识别了 cdevsw、模块加载器和 softc 结构
- 与 myfirst 驱动比较了模式
- 关键发现：[你的观察]
```

**成功标准：**

- [ ] 找到了所有三个驱动文件
- [ ] 在每个中定位了 cdevsw 和 softc
- [ ] 识别了设备创建调用
- [ ] 与你的驱动比较

**如果卡住：** 使用 `grep -r "DRIVER_MODULE" /usr/src/sys/dev/null/` 找到关键宏。

---

### 实验 7.2：构建、加载和验证生命周期

**目标：** 编译你的驱动、加载到内核、验证生命周期事件、干净卸载。

**练习的技能：**

- 构建内核模块
- 使用 `kldload`/`kldunload` 加载/卸载
- 检查 `dmesg` 和 `devinfo`

**说明：**

**1. 导航到你的驱动：**

```bash
% cd ~/drivers/myfirst
```

**2. 清理并构建：**

```bash
% make clean
% make
```

验证 `myfirst.ko` 已创建：

```bash
% ls -lh myfirst.ko
-rwxr-xr-x  1 youruser yourgroup  8.5K Nov  6 16:00 myfirst.ko
```

**3. 加载模块：**

```bash
% sudo kldload ./myfirst.ko
```

**4. 检查内核消息：**

```bash
% dmesg | tail -n 5
```

预期输出：

```text
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
myfirst0: Sysctl tree created under dev.myfirst.0.stats
```

**5. 验证设备树：**

```bash
% devinfo -v | grep myfirst
  myfirst0
```

**6. 检查设备节点：**

```bash
% ls -l /dev/myfirst0
crw-------  1 root  wheel  0x5a Nov  6 16:00 /dev/myfirst0
```

**7. 查询 sysctl：**

```bash
% sysctl dev.myfirst.0.stats
dev.myfirst.0.stats.attach_ticks: 123456
dev.myfirst.0.stats.open_count: 0
dev.myfirst.0.stats.bytes_read: 0
```

**8. 卸载模块：**

```bash
% sudo kldunload myfirst
```

**9. 验证清理：**

```bash
% dmesg | tail -n 2
myfirst0: Detaching: uptime 1234 ticks, opened 0 times
```

```bash
% ls /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

```bash
% sysctl dev.myfirst.0
sysctl: unknown oid 'dev.myfirst.0'
```

**10. 重新加载并验证幂等性：**

```bash
% sudo kldload ./myfirst.ko
% sudo kldunload myfirst
% sudo kldload ./myfirst.ko
% sudo kldunload myfirst
```

所有循环都应该成功无错误。

**11. 记录结果：**

实验日志：

```text
实验 7.2 完成：
- 成功构建 myfirst.ko
- 无错误加载
- 在 dmesg 中验证附加消息
- 验证 /dev 节点和 sysctl
- 干净卸载
- 重复加载/卸载循环 3 次：全部成功
```

**成功标准：**

- [ ] 模块无错误构建
- [ ] 加载无内核崩溃
- [ ] dmesg 中出现附加消息
- [ ] 加载时 `/dev/myfirst0` 存在
- [ ] Sysctl 可读
- [ ] 卸载移除所有内容
- [ ] 重新加载可靠工作

**故障排除：**

- 如果构建失败，检查 Makefile 语法（制表符，不是空格）。
- 如果加载失败并提示 "Exec format error"，检查内核/源码版本匹配。
- 如果卸载提示 "module busy"，检查没有进程持有设备打开。

---

### 实验 7.3：测试设备节点 Open/Close

**目标：** 从用户空间与 `/dev/myfirst0` 交互，验证 `open()` 和 `close()` 处理器被调用。

**练习的技能：**

- 用户空间设备访问
- 监控驱动日志
- 追踪状态变化

**说明：**

**1. 加载驱动：**

```bash
% sudo kldload ./myfirst.ko
```

**2. 用 `cat` 打开设备（读）：**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
```

（无输出，立即 EOF）

**3. 检查日志：**

```bash
% dmesg | tail -n 3
myfirst0: Device opened (count: 1)
myfirst0: Device closed
```

**4. 写入设备：**

```bash
% sudo sh -c 'echo "test" > /dev/myfirst0'
```

**5. 再次检查日志：**

```bash
% dmesg | tail -n 3
myfirst0: Device opened (count: 2)
myfirst0: Device closed
```

**6. 验证 sysctl 计数器：**

```bash
% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 2
```

**7. 测试独占访问：**

打开两个终端。

终端 1：

```bash
% sudo sh -c 'exec 3<>/dev/myfirst0; sleep 10'
```

（这会保持设备打开 10 秒）

终端 2（快速，当终端 1 仍在睡眠时）：

```bash
% sudo sh -c 'cat < /dev/myfirst0'
cat: /dev/myfirst0: Device busy
```

成功！独占访问已强制执行。

**8. 尝试在打开时卸载：**

终端 1（保持设备打开）：

```bash
% sudo sh -c 'exec 3<>/dev/myfirst0; sleep 30'
```

终端 2：

```bash
% sudo kldunload myfirst
kldunload: can't unload file: Device busy
```

检查 dmesg：

```bash
% dmesg | tail -n 2
myfirst0: Cannot detach: device is open
```

完美！你的 `detach()` 正确拒绝在使用时卸载。

**9. 关闭并重试卸载：**

终端 1：等待 `sleep 30` 完成（或 Ctrl+C 中断）。

终端 2：

```bash
% sudo kldunload myfirst
（成功）
```

**10. 记录结果：**

实验日志：

```text
实验 7.3 完成：
- 用 cat 打开设备，验证 open/close 日志
- 用 echo 打开设备，验证计数器增加
- 独占访问强制执行（第二次打开返回 EBUSY）
- 打开时 detach 拒绝
- 关闭后 detach 成功
```

**成功标准：**

- [ ] Open 触发 `open()` 处理器（已记录）
- [ ] Close 触发 `close()` 处理器（已记录）
- [ ] Sysctl 计数器每次打开时增加
- [ ] 第二次打开返回 `EBUSY`
- [ ] 打开时 Detach 返回 `EBUSY`
- [ ] 关闭后 Detach 成功

**故障排除：**

- 如果没有看到 "Device opened" 日志，检查 `open()` 处理器中是否存在 `device_printf()`。
- 如果独占访问未强制执行，验证 `open()` 中的 `if (sc->is_open) return (EBUSY)` 检查。

---

### 实验 7.4：模拟附加失败并验证展开

**目标：** 在 `attach()` 中注入故意失败，验证清理正确（无泄漏、无崩溃）。

**练习的技能：**

- 测试错误路径
- 调试附加失败
- 验证资源清理

**说明：**

**1. 添加模拟失败：**

编辑 `myfirst.c` 并在 `attach()` 中的互斥锁初始化之后添加：

```c
        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

        /* 实验 7.4 的模拟失败 */
        device_printf(dev, "模拟附加失败用于测试\n");
        error = ENXIO;
        goto fail_mtx;

        /* （attach 的其余部分继续...） */
```

**2. 重新构建：**

```bash
% make clean && make
```

**3. 尝试加载：**

```bash
% sudo kldload ./myfirst.ko
kldload: can't load ./myfirst.ko: Device not configured
```

**4. 检查 dmesg：**

```bash
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: 模拟附加失败用于测试
```

**5. 验证无泄漏：**

```bash
% ls /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

```bash
% sysctl dev.myfirst.0
sysctl: unknown oid 'dev.myfirst.0'
```

```bash
% devinfo -v | grep myfirst
(无输出)
```

完美！设备附加失败，且没有资源残留。

**6. 尝试再次加载：**

```bash
% sudo kldload ./myfirst.ko
kldload: can't load ./myfirst.ko: Device not configured
```

仍然干净失败（无双重初始化崩溃）。

**7. 移除模拟失败：**

编辑 `myfirst.c` 并删除或注释掉模拟失败块。

**8. 重新构建并正常加载：**

```bash
% make clean && make
% sudo kldload ./myfirst.ko
% dmesg | tail -n 5
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: Attached successfully at tick 123456
myfirst0: Created /dev/myfirst0
myfirst0: Sysctl tree created under dev.myfirst.0.stats
```

成功！

**9. 测试另一个失败点：**

在创建设备节点之后注入失败：

```c
        error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
        if (error != 0) {
                device_printf(dev, "创建设备节点失败: %d\n", error);
                goto fail_mtx;
        }

        /* 设备节点创建后模拟失败 */
        device_printf(dev, "设备节点创建后模拟失败\n");
        error = ENOMEM;
        goto fail_dev;
```

**10. 重新构建并测试：**

```bash
% make clean && make
% sudo kldload ./myfirst.ko
kldload: can't load ./myfirst.ko: Cannot allocate memory
```

```bash
% dmesg | tail -n 3
myfirst0: <My First FreeBSD Driver> on nexus0
myfirst0: 设备节点创建后模拟失败
```

**11. 验证 `/dev` 节点已销毁：**

```bash
% ls /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

完美！即使节点已创建，错误路径也将其销毁。

**12. 移除模拟并恢复正常操作：**

删除第二个模拟失败，重新构建，并正常加载。

**13. 记录结果：**

实验日志：

```text
实验 7.4 完成：
- 互斥锁初始化后模拟失败：清理正确
- 设备节点创建后模拟失败：清理正确
- 两种情况都验证了无泄漏
- 验证了重复加载尝试不会崩溃
- 恢复了正常操作
```

**成功标准：**

- [ ] 互斥锁后模拟失败：无泄漏
- [ ] 设备节点后模拟失败：节点已销毁
- [ ] 多次加载尝试不会崩溃
- [ ] 移除模拟后恢复正常操作

**故障排除：**

- 如果看到崩溃，你的错误路径有 bug。检查每个 `goto` 是否跳转到正确的标签。
- 如果资源泄漏，确保每个清理标签可到达且正确。

---

### 实验完成！

你现在已：

- 导航了 FreeBSD 源码树（实验 7.1）
- 构建、加载并验证了你的驱动（实验 7.2）
- 测试了 open/close 和独占访问（实验 7.3）
- 用模拟失败验证了错误展开（实验 7.4）

**给自己鼓掌。** 你已经从阅读驱动变成了 **自己构建和测试一个**。这是一个重要的里程碑。

---

## 短练习

这些练习强化本章概念。如果你想继续前进之前加深理解，它们是 **可选但推荐的**。

### 练习 7.1：添加 Sysctl 标志

**任务：** 添加一个新的只读 sysctl，显示设备当前是否已打开。

**步骤：**

1. 在 `attach()` 中，添加：

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "is_open", CTLFLAG_RD,
    &sc->is_open, 0, "1 表示设备当前已打开");
```

2. 重新构建、加载并测试：

```bash
% sysctl dev.myfirst.0.stats.is_open
dev.myfirst.0.stats.is_open: 0

% sudo sh -c 'exec 3<>/dev/myfirst0; sysctl dev.myfirst.0.stats.is_open; exec 3<&-'
dev.myfirst.0.stats.is_open: 1
```

**预期结果：** 打开时标志显示 `1`，关闭后显示 `0`。

---

### 练习 7.2：记录首次和最后一次打开

**任务：** 修改 `open()` 只记录 **首次** 打开，`close()` 只记录 **最后一次** 关闭。

**提示：**

- 在增加前后检查 `sc->open_count`。
- 在 `close()` 中，减少计数器并检查是否到达零。

**预期行为：**

```bash
% sudo sh -c 'cat < /dev/myfirst0'
myfirst0: Device opened for the first time
myfirst0: Device closed (no more openers)

% sudo sh -c 'cat < /dev/myfirst0'
(无日志：不是首次打开)
myfirst0: Device closed (no more openers)
```

---

### 练习 7.3：添加"重置统计"Sysctl

**任务：** 添加一个只写 sysctl，将 `open_count` 和 `bytes_read` 重置为零。

**步骤：**

1. 定义处理函数：

```c
static int
sysctl_reset_stats(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int error, val;

        val = 0;
        error = sysctl_handle_int(oidp, &val, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);

        mtx_lock(&sc->mtx);
        sc->open_count = 0;
        sc->bytes_read = 0;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "Statistics reset\n");
        return (0);
}
```

2. 注册它：

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "reset_stats", CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE,
    sc, 0, sysctl_reset_stats, "I",
    "写入 1 以重置统计信息");
```

3. 测试：

```bash
% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 5

% sudo sysctl dev.myfirst.0.stats.reset_stats=1
dev.myfirst.0.stats.reset_stats: 0 -> 1

% dmesg | tail -n 1
myfirst0: Statistics reset

% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 0
```

---

### 练习 7.4：测试加载/卸载 100 次

**任务：** 编写一个脚本，加载和卸载你的驱动 100 次，检查是否有失败或泄漏。

**脚本（~/drivers/myfirst/stress_test.sh）：**

```bash
#!/bin/sh
set -e

for i in $(seq 1 100); do
        echo "迭代 $i"
        sudo kldload ./myfirst.ko
        sleep 0.1
        sudo kldunload myfirst
done

echo "压力测试完成：100 次循环"
```

**运行：**

```bash
% chmod +x stress_test.sh
% ./stress_test.sh
```

**预期结果：** 所有迭代成功无错误。

**如果失败：** 检查 `dmesg` 中的崩溃消息或泄漏资源。

---

### 练习 7.5：将你的驱动与 null.c 比较

**任务：** 打开 `/usr/src/sys/dev/null/null.c` 和你的 `myfirst.c` 并列。列出 5 个相似之处和 5 个不同之处。

**示例观察：**

**相似之处：**

1. 都使用 `cdevsw` 进行字符设备操作。
2. 都创建 `/dev` 节点。
3. 都有 `open` 和 `close` 处理器。
4. 都在读取时返回 EOF。
5. 都记录附加/分离事件。

**不同之处：**

1. `null.c` 使用 `MOD_LOAD` 处理器；`myfirst` 使用 Newbus。
2. `null.c` 没有 softc；`myfirst` 有。
3. `null.c` 创建多个设备（`null`、`zero`、`full`）；`myfirst` 创建一个。
4. `null.c` 不使用 sysctl；`myfirst` 使用。
5. `null.c` 是无状态的；`myfirst` 跟踪计数器。

---

## 可选挑战

这些是想要超越基础的读者的 **高级练习**。在完成所有实验和练习之前不要尝试这些。

### 挑战 7.1：实现简单读取缓冲

**目标：** 不立即返回 EOF，而是在 `read()` 时返回固定字符串。

**步骤：**

1. 向 softc 添加缓冲：

```c
        char    read_buffer[64];  /* 读取时返回的数据 */
        size_t  read_len;         /* 有效数据长度 */
```

2. 在 `attach()` 中，填充缓冲：

```c
        snprintf(sc->read_buffer, sizeof(sc->read_buffer),
            "Hello from myfirst driver!\n");
        sc->read_len = strlen(sc->read_buffer);
```

3. 在 `myfirst_read()` 中，将数据复制到用户空间：

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        size_t len;
        int error;

        len = MIN(uio->uio_resid, sc->read_len);
        if (len == 0)
                return (0);  /* EOF */

        error = uiomove(sc->read_buffer, len, uio);
        return (error);
}
```

4. 测试：

```bash
% sudo cat /dev/myfirst0
Hello from myfirst driver!
```

**预期行为：** 读取返回字符串一次，后续读取返回 EOF。

---

### 挑战 7.2：允许多个打开者

**目标：** 移除独占访问检查，让多个程序可以同时打开设备。

**步骤：**

1. 移除 `open()` 中的 `if (sc->is_open) return (EBUSY)` 检查。
2. 使用 **引用计数** 代替布尔标志：

```c
        int     open_refcount;  /* 当前打开者数量 */
```

3. 在 `open()` 中：

```c
        mtx_lock(&sc->mtx);
        sc->open_refcount++;
        mtx_unlock(&sc->mtx);
```

4. 在 `close()` 中：

```c
        mtx_lock(&sc->mtx);
        sc->open_refcount--;
        mtx_unlock(&sc->mtx);
```

5. 在 `detach()` 中，如果 `open_refcount > 0` 则拒绝：

```c
        if (sc->open_refcount > 0) {
                device_printf(dev, "Cannot detach: device has %d openers\n",
                    sc->open_refcount);
                return (EBUSY);
        }
```

6. 用两个终端测试：

终端 1：

```bash
% sudo sh -c 'exec 3<>/dev/myfirst0; sleep 30'
```

终端 2：

```bash
% sudo sh -c 'cat < /dev/myfirst0'
（成功，而不是返回 EBUSY）
```

---

### 挑战 7.3：添加写入计数器

**目标：** 跟踪已写入设备的字节数。

**步骤：**

1. 添加到 softc：

```c
        uint64_t        bytes_written;
```

**注意：** 这是第 7 章的丢弃写入处理程序。我们故意不在这里使用 `uiomove()` 或存储用户数据。完整的数据移动、缓冲和 `uiomove()` 在第 9 章。

2. 在 `myfirst_write()` 中：

```c
        size_t len = uio->uio_resid;

        mtx_lock(&sc->mtx);
        sc->bytes_written += len;
        mtx_unlock(&sc->mtx);

        uio->uio_resid = 0;
        return (0);
```

3. 通过 sysctl 暴露：

```c
SYSCTL_ADD_U64(&sc->sysctl_ctx, ..., "bytes_written", ...);
```

4. 测试：

```bash
% sudo sh -c 'echo "test" > /dev/myfirst0'
% sysctl dev.myfirst.0.stats.bytes_written
dev.myfirst.0.stats.bytes_written: 5
```

---

### 挑战 7.4：创建第二个设备（myfirst1）

**目标：** 手动创建第二个设备实例以测试多设备支持。

**提示：** 目前，你的驱动自动创建 `myfirst0`。要测试多设备，你需要触发第二次探测/附加循环。这很复杂（需要总线级操作或克隆），所以考虑仅作 **研究**。

**替代方案：** 研究 `/usr/src/sys/net/if_tuntap.c` 看它如何处理克隆设备（按需创建新实例）。

---

### 挑战 7.5：实现速率限制日志

**目标：** 为打开事件添加速率限制日志（每秒最多记录一次）。

**步骤：**

1. 添加到 softc：

```c
        time_t  last_open_log;
```

2. 在 `open()` 中：

```c
        time_t now = time_second;

        if (now - sc->last_open_log >= 1) {
                device_printf(sc->dev, "Device opened (count: %lu)\n",
                    (unsigned long)sc->open_count);
                sc->last_open_log = now;
        }
```

3. 通过快速打开测试：

```bash
% for i in $(seq 1 10); do sudo sh -c 'cat < /dev/myfirst0'; done
```

**预期行为：** 只出现少数日志消息（速率限制）。

---

## 陷阱与故障排除决策树

即使仔细编码，你也会遇到问题。本节提供一个 **决策树** 来快速诊断常见问题。

### 症状：驱动无法加载

**检查：**

- [ ] `freebsd-version -k` 是否与 `/usr/src` 版本匹配？
  - **否：** 重新构建内核或重新克隆 `/usr/src` 以获取正确版本。
  - **是：** 继续。

- [ ] `make` 是否无错误完成？
  - **否：** 阅读编译器错误消息。常见原因：
    - 缺少分号
    - 括号不匹配
    - 未定义函数（缺少包含）
  - **是：** 继续。

- [ ] `kldload` 是否失败并提示 "Exec format error"？
  - **是：** 内核/模块不匹配。使用匹配源代码重新构建。
  - **否：** 继续。

- [ ] `kldload` 是否失败并提示 "No such file or directory"？
  - **是：** 检查模块路径（`./myfirst.ko` vs `/boot/modules/myfirst.ko`）。
  - **否：** 继续。

- [ ] 检查 `dmesg` 中的附加错误：

```bash
% dmesg | tail -n 10
```

查找来自你驱动的错误消息。

---

### 症状：设备节点不出现

**检查：**

- [ ] `attach()` 是否成功？

```bash
% dmesg | grep myfirst
```

查找 "Attached successfully" 消息。

- [ ] 如果附加失败，错误处理是否运行？

在 dmesg 中查找错误消息。

- [ ] `make_dev_s()` 是否成功？

添加日志：

```c
device_printf(dev, "make_dev_s returned: %d\n", error);
```

- [ ] 设备节点名称是否正确？

```bash
% ls -l /dev/myfirst*
```

检查拼写和单元号。

---

### 症状：加载时内核崩溃

**检查：**

- [ ] 你是否忘记 `mtx_init()`？

在 `mtx_lock()` 期间崩溃 → 忘记初始化互斥锁。

- [ ] 你是否解引用了 NULL 指针？

崩溃提示 "NULL pointer dereference" → 检查 `device_get_softc()` 返回值。

- [ ] 你是否损坏了内存？

在内核配置中启用 WITNESS 和 INVARIANTS，然后重新构建：

```text
options WITNESS
options INVARIANTS
```

重启，重新加载你的驱动。WITNESS 会捕获锁违规。

---

### 症状：卸载时内核崩溃

**检查：**

- [ ] 你是否忘记 `destroy_dev()`？

卸载时，用户尝试访问设备节点会崩溃。

- [ ] 你是否忘记 `mtx_destroy()`？

启用 WITNESS 的内核在卸载时如果锁未销毁会崩溃。

- [ ] 你是否忘记 `sysctl_ctx_free()`？

Sysctl OID 泄漏可能导致重新加载时崩溃。

- [ ] 卸载时代码是否仍在运行？

检查：
  - 打开的设备节点（`sc->is_open` 应为 false）
  - 活动定时器或回调（本章未使用，但后续常见）

---

### 症状：卸载时提示 "Device busy"

**检查：**

- [ ] 设备是否仍然打开？

```bash
% fstat | grep myfirst
```

如果有进程打开设备，卸载会失败。

- [ ] 你是否从 `detach()` 返回了 `EBUSY`？

检查 `detach()` 逻辑：

```c
if (sc->is_open) {
        return (EBUSY);
}
```

---

### 症状：Sysctl 不出现

**检查：**

- [ ] `sysctl_ctx_init()` 是否运行？

- [ ] `SYSCTL_ADD_NODE()` 是否成功？

添加日志：

```c
if (sc->sysctl_tree == NULL) {
        device_printf(dev, "sysctl tree creation failed\n");
}
```

- [ ] Sysctl 路径是否正确？

```bash
% sysctl dev.myfirst
```

检查驱动名或单元号的拼写。

---

### 症状：Open/Close 不记录日志

**检查：**

- [ ] 你是否向处理器添加了 `device_printf()`？

- [ ] cdevsw 是否正确注册？

检查在 `make_dev_s()` 之前是否设置了 `args.mda_devsw = &myfirst_cdevsw`。

- [ ] `si_drv1` 是否设置正确？

```c
args.mda_si_drv1 = sc;
```

如果这是 NULL，`open()` 会失败。

---

### 症状：模块加载但什么都不做

**检查：**

- [ ] `attach()` 是否运行？

```bash
% dmesg | grep myfirst
```

如果完全没有输出且你的驱动附加到 `nexus`，最可能的原因是缺少 `identify` 方法。没有它，nexus 没有 `myfirst` 设备可探测，你的代码永远不会被调用。重新阅读上面的 **步骤 6：实现 Identify** 部分，确认方法表中存在 `DEVMETHOD(device_identify, myfirst_identify)`。

- [ ] 设备是否在正确的总线上？

对于伪设备，使用 `nexus`（并记得提供 `identify`）。对于 PCI，使用 `pci`。对于 USB，使用 `usbus`。

- [ ] `probe()` 是否返回非错误优先级？

如果 `probe()` 返回 `ENXIO`，驱动不会附加。对于我们的伪设备，`BUS_PROBE_DEFAULT` 是正确的值。

---

### 通用调试提示

**启用详细引导：**

```bash
% sudo sysctl boot.verbose=1
```

重新加载驱动以查看更详细的消息。

**使用 printf 调试：**

在关键点添加 `device_printf()` 语句以追踪执行流程。

**检查锁状态：**

如果使用 WITNESS，检查锁顺序：

```bash
% sysctl debug.witness.fullgraph
```

**崩溃后保存 dmesg：**

```bash
% sudo dmesg -a > panic.log
```

分析日志中的线索。

---

## 自我评估标准

使用此标准评估你在进入第 8 章之前的理解。

### 核心知识

**给自己评分（1-5，其中 5 = 完全自信）：**

- [ ] 我能解释什么是 softc 以及它为什么存在。（分数：__/5）
- [ ] 我理解探测/附加/分离生命周期。（分数：__/5）
- [ ] 我可以使用 `make_dev_s()` 创建设备节点。（分数：__/5）
- [ ] 我可以实现基本的 open/close 处理器。（分数：__/5）
- [ ] 我可以添加只读 sysctl。（分数：__/5）
- [ ] 我理解单标签展开模式。（分数：__/5）
- [ ] 我可以用模拟失败测试错误路径。（分数：__/5）

**总分：__/35**

**解释：**

- **30-35：** 优秀。你可以进入第 8 章。
- **25-29：** 良好。继续之前复习薄弱领域。
- **20-24：** 尚可。重新查看实验和练习。
- **<20：** 在本章花更多时间。

---

### 实践技能

**你能不看笔记完成这些吗？**

- [ ] 使用 `make` 构建内核模块。
- [ ] 使用 `kldload` 加载模块。
- [ ] 在 `dmesg` 中检查附加消息。
- [ ] 使用 `sysctl` 查询 sysctl。
- [ ] 使用 `cat` 或 shell 重定向打开设备节点。
- [ ] 使用 `kldunload` 卸载模块。
- [ ] 模拟附加失败。
- [ ] 验证失败后的清理。

**评分：** 每项技能 1 分。**目标：** 7/8 或更高。

---

### 代码阅读

**你能在真实 FreeBSD 代码中识别这些模式吗？**

- [ ] 识别 `cdevsw` 结构。
- [ ] 定位 `probe()`、`attach()`、`detach()` 方法。
- [ ] 找到 softc 结构定义。
- [ ] 识别 `DRIVER_MODULE()` 宏。
- [ ] 发现带 goto 标签的错误展开。
- [ ] 找到 `make_dev()` 或 `make_dev_s()` 调用。
- [ ] 识别 sysctl 创建（`SYSCTL_ADD_*`）。

**评分：** 每个模式 1 分。**目标：** 6/7 或更高。

---

### 概念理解

**判断对错：**

1. softc 由驱动程序在 `attach()` 中分配。（**错**。Newbus 从 `driver_t` 中声明的 size 分配它。）
2. `probe()` 应该分配资源。（**错**。`probe()` 只检查设备并决定是否认领它；`attach()` 进行分配。）
3. `detach()` 必须撤销 `attach()` 所做的所有操作。（**对。**）
4. 错误标签应该按初始化的逆序排列。（**对。**）
5. 如果模块正在卸载，可以跳过 `mtx_destroy()`。（**错**。每个 `mtx_init()` 都需要匹配的 `mtx_destroy()`。）
6. 模块卸载时 sysctl 会自动清理。（**错**。只有在你为每个设备上下文调用 `sysctl_ctx_free()` 时才清理。）
7. `make_dev_s()` 比 `make_dev()` 更安全。（**对**。它返回显式错误，避免了 `make_dev()` 可能失败但没有明确方式报告的竞争。）
8. 附加到 `nexus` 的伪设备必须提供 `identify` 方法。（**对**。没有它，总线没有设备可探测。）

**评分：** 每个正确答案 1 分。**目标：** 7/8 或更高。

---

### 总体评估

汇总你的分数：

- 核心知识：__/35
- 实践技能：__/8
- 代码阅读：__/7
- 概念理解：__/8

**总分：__/58**

**等级：**

- **51-58：** A（优秀掌握）
- **44-50：** B（良好理解）
- **35-43：** C（尚可，但复习薄弱领域）
- **<35：** 继续前重新学习本章

---

## 总结与展望

恭喜！你已经完成了第 7 章，从头开始构建了你的第一个 FreeBSD 驱动程序。让我们回顾你的成就并预览接下来的内容。

### 你构建了什么

你的 `myfirst` 驱动程序是最小的，但完整的：

- **生命周期规范：** 干净的探测/附加/分离，无泄漏。
- **用户表面：** 可靠打开和关闭的 `/dev/myfirst0` 节点。
- **可观测性：** 显示附加时间、打开次数和读取字节数的只读 sysctl。
- **错误处理：** 优雅恢复失败的单标签展开模式。
- **日志：** 正确使用 `device_printf()` 记录生命周期事件和错误。

这不是玩具。这是一个 **生产级脚手架**，每个 FreeBSD 驱动程序都是这样开始的。

### 你学到了什么

你现在理解：

- Newbus 如何发现和附加驱动程序
- softc 的作用（每设备状态）
- 如何创建和销毁设备节点
- 如何通过 sysctl 暴露指标
- 如何处理错误而不泄漏资源
- 如何测试生命周期路径（加载/卸载、打开/关闭、模拟失败）

这些技能是 **可转移的**。无论你编写 PCI 驱动程序、USB 驱动程序还是网络接口，你都会使用这些相同的模式。

### 还缺少什么（以及为什么）

你的驱动程序还不能做太多：

- **读/写语义：** 打桩，未实现。（**第 8 章和第 9 章**）
- **缓冲：** 无队列，无环形缓冲区。（**第 10 章**）
- **硬件交互：** 无寄存器、DMA、中断。（**第 4 部分**）
- **并发：** 互斥锁存在但未使用。（**第 3 部分**）
- **真实世界 I/O：** 无阻塞、无 poll/select。（**第 10 章**）

这是故意的。**在掌握结构之前掌握复杂性。** 你不会第一天学木工就建摩天大楼。你会从工作台开始，就像你在这里做的一样。

### 接下来是什么

**第 8 章，使用设备文件。** devfs 权限和所有权、持久节点，以及你将用来检查和测试设备的用户态探针。

**第 9 章和第 10 章，设备读写，加上高效处理输入输出。** 用 `uiomove` 实现读写、介绍缓冲和流控，定义阻塞、非阻塞和 `poll` 或 `kqueue` 语义及正确的错误处理。

---

### 你的下一步

**在进入第 8 章之前：**

1. **完成所有实验** 如果你还没有。
2. **尝试至少两个练习** 以强化模式。
3. **彻底测试你的驱动程序：** 加载/卸载 10 次、打开/关闭 10 次、模拟一个更多的失败。
4. **将代码提交到 Git：** 这是一个里程碑。

```bash
% cd ~/drivers/myfirst
% git add myfirst.c Makefile
% git commit -m "Chapter 7 complete: minimal driver with lifecycle discipline"
```

5. **休息一下。** 你值得。内核编程很紧张，巩固时间很重要。

**当你准备好第 8 章时：**

- 你将扩展这个相同的驱动程序（不是从头重新开始）。
- 你在这里构建的结构将继续沿用。
- 概念将增量构建，不会重置。

### 最后的话

从零开始构建驱动程序一开始可能感觉难以承受。但看看你完成了什么：

- 你只从 Makefile 和空白 `.c` 文件开始。
- 你构建了一个编译、加载、附加、操作、分离和干净卸载的驱动程序。
- 你测试了错误路径并验证了清理。
- 你通过 sysctl 暴露了状态，并创建了用户可访问的设备节点。

**这不是初学者的运气。这是能力。** 大多数通用软件工程师从未接触内核模块，更不用说构建具有规范附加和分离路径的驱动程序。你刚刚做到了。

从 "hello module" 到 "生产驱动" 的道路很长，但你已经迈出了最难的一步：**开始**。从这里开始的每一章都会增加一层能力，多一个工具包中的工具。

保持你的实验日志更新。继续实验。当某些事情不清楚时继续问"为什么？"。最重要的是，**继续构建**。

欢迎来到 FreeBSD 驱动开发的世界。你在这里有了一席之地。

### 第 8 章见

在下一章中，我们将通过实现真正的文件语义让你的设备节点焕发生机：管理每次打开状态、处理独占与共享访问，并为真实 I/O 做准备。

在此之前，享受你的成功。你构建了真实的东西，这值得庆祝。

*"任何事情的专家都曾是初学者。" - 海伦·海耶斯*

