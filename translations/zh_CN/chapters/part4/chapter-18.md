---
title: "编写PCI驱动程序"
description: "第18章将模拟的myfirst驱动程序转变为真正的PCI驱动程序。它教授PCI拓扑、FreeBSD如何枚举PCI和PCIe设备、驱动程序如何通过供应商和设备ID进行探测和附加、BAR如何通过bus_alloc_resource_any成为bus_space标签和句柄、如何在真实BAR上执行附加时初始化、如何在PCI路径上保持第17章模拟处于非活动状态，以及干净的反向分离路径如何按相反顺序拆除整个附加。驱动程序从1.0-simulated发展到1.1-pci，获得一个新的pci专用文件，并为第18章在真实中断处理程序方面做好第19章的准备。"
partNumber: 4
partName: "硬件与平台级集成"
chapter: 18
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "AI辅助翻译为简体中文"
language: "zh-CN"
estimatedReadTime: 225
---

# 编写PCI驱动程序

## 读者指南与学习目标

第17章以一个从外部看起来像真实设备、从内部行为也像真实设备的驱动程序结束。`myfirst`模块在版本`1.0-simulated`时拥有一个寄存器块、一个基于`bus_space(9)`的访问器层、一个带有能产生自主状态变化的callout的模拟后端、一个故障注入框架、一个命令-响应协议、统计计数器,以及三个活的文档文件(`HARDWARE.md`、`LOCKING.md`、`SIMULATION.md`)。驱动程序中的每个寄存器访问仍然通过`CSR_READ_4(sc, off)`、`CSR_WRITE_4(sc, off, val)`和`CSR_UPDATE_4(sc, off, clear, set)`进行。硬件层(`myfirst_hw.c`和`myfirst_hw.h`)是一个产生标签和句柄的薄包装器,模拟层(`myfirst_sim.c`和`myfirst_sim.h`)是使这些寄存器活动起来的东西。驱动程序本身不知道寄存器块是真实的硅片还是`malloc(9)`分配。

这种模糊性是第16章和第17章给我们的礼物,第18章是我们兑现它的地方。驱动程序现在将遇到真实的PCI硬件。寄存器块将不再来自内核的堆;它将来自设备的基址寄存器,在启动时由固件分配,由内核映射到具有设备内存属性的虚拟地址范围,并作为`struct resource *`交给驱动程序。访问器层不变。编译时开关保持第17章模拟作为没有PCI测试环境的读者的单独构建可用;在PCI构建上,模拟callout不运行,因此它们不会意外写入真实设备的寄存器。变化的是标签和句柄的来源点:不再由`malloc(9)`产生,它们将由`bus_alloc_resource_any(9)`针对newbus树中的PCI子设备产生。

第18章的范围正是这个过渡。它教授什么是PCI、FreeBSD如何在newbus树中表示PCI设备、驱动程序如何通过供应商和设备ID匹配设备、BAR如何在配置空间中出现并成为`bus_space`资源、如何在真实BAR上进行附加时初始化而不干扰设备,以及分离如何按相反顺序释放所有内容。它涵盖配置空间访问器`pci_read_config(9)`和`pci_write_config(9)`、能力遍历器`pci_find_cap(9)`和`pci_find_extcap(9)`,以及PCIe高级错误报告的简要介绍,以便读者知道它在哪里,而不必立即处理它。结尾进行小型但重要的重构,将新的PCI特定代码拆分到自己的文件中,将驱动程序标记为`1.1-pci`,并对模拟和真实PCI构建运行完整的回归测试。

第18章刻意将自己限制在探测-附加流程及其依赖的内容。通过`bus_setup_intr(9)`的真实中断处理程序、过滤器加ithread组合,以及处理程序可以做什么和不可以做什么的规则属于第19章。MSI和MSI-X,连同它们暴露的更丰富的PCIe能力,是第20章的内容。描述符环、分散-聚集DMA、设备写入周围的缓存一致性,以及完整的`bus_dma(9)`故事是第20章和第21章的内容。特定芯片组上的配置空间怪癖、挂起和恢复期间的电源管理状态机,以及SR-IOV是后面的章节。本章停留在它能良好覆盖的范围内,当某个主题值得单独一章时明确交接。

第四部分的层次叠加。第16章教授寄存器访问的词汇;第17章教授如何像设备一样思考;第18章教授如何遇到真实的设备。第19章将教你如何对设备所说做出反应,第20章和第21章将教你如何让设备直接访问RAM。每一层都依赖于前一层。第18章是你第一次将newbus树视为不仅仅是抽象图表,第三部分建立的纪律是让这次接触诚实的基础。

### 为什么PCI子系统值得单独一章

此时你可能在想为什么PCI子系统需要单独一章。模拟已经给了我们寄存器;真实硬件会给我们同样的寄存器。为什么不简单地说"调用`bus_alloc_resource_any`,将返回的句柄传递给`bus_read_4`,然后继续"?

两个原因。

第一个是PCI子系统是现代FreeBSD中使用最广泛的总线,围绕它的newbus约定是每个其他总线驱动程序模仿的约定。理解PCI探测-附加流程的读者可以阅读ACPI附加流程、USB附加流程、SD卡附加流程和virtio附加流程,而无需重新学习。模式在细节上有所不同,但形状是PCI的。在整个规范总线上花一整章就是在每个总线借用的模式上花一整章。

第二个是PCI引入了之前章节没有为你准备的概念。配置空间是每个设备的第二个地址空间,与BAR本身分开,设备在其中宣传它是什么和它需要什么。供应商和设备ID是一个十六位加十六位的元组,驱动程序将其与支持的设备表匹配。子供应商和子系统ID是第二级元组,用于区分由不同供应商围绕通用芯片组构建的卡。类代码让驱动程序匹配广泛的类别(任何USB主控制器、任何UART),当设备特定的表过于狭窄时。BAR作为三十二位或六十四位地址存在于配置空间中,驱动程序从不直接解引用。PCI能力是驱动程序在附加时读取的额外元数据链表。这些每一个都是新词汇;每一个都是第18章不仅仅是第17章上螺栓的一个章节的原因。

本章还因其是`myfirst`驱动程序获得其第一个真实总线子设备的章节而确立其位置。直到现在,驱动程序作为具有单个隐式实例的内核模块存在,通过`kldload`手动附加,通过`kldunload`分离。第18章之后,驱动程序将是适当的PCI总线子设备,由内核的newbus代码枚举,当存在匹配设备时自动附加,当设备离开时自动分离,并在`devinfo -v`中作为具有父设备(`pci0`)、单元(`myfirst0`、`myfirst1`)和一组已声明资源的设备可见。这个变化是从"恰好存在的模块"到"内核知道的设备的驱动程序"的变化。后续每个第四部分章节都假设你已经做到了。

### 第17章将驱动程序留在了何处

开始前需要验证几个先决条件。第18章扩展第17章第5阶段结束时产生的驱动程序,标记为版本`1.0-simulated`。如果以下任何项目感觉不确定,请在开始本章前返回第17章。

- 你的驱动程序干净编译并在`kldstat -v`中标识自身为`1.0-simulated`。
- softc携带`sc->hw`(来自第16章的`struct myfirst_hw *`)和`sc->sim`(来自第17章的`struct myfirst_sim *`)。每个寄存器访问通过`sc->hw`进行;每个模拟行为位于`sc->sim`之下。
- 十六个32位寄存器的寄存器映射跨越偏移量`0x00`到`0x3c`,第17章添加的内容(`SENSOR`、`SENSOR_CONFIG`、`DELAY_MS`、`FAULT_MASK`、`FAULT_PROB`、`OP_COUNTER`)已就位。
- `CSR_READ_4`、`CSR_WRITE_4`和`CSR_UPDATE_4`包装`bus_space_read_4`、`bus_space_write_4`和读-修改-写辅助函数。每次访问在调试内核上断言`sc->mtx`被持有。
- 传感器callout每秒运行一次,以十秒为周期振荡`SENSOR`。命令callout以可配置延迟每个命令触发一次。故障注入框架处于活动状态。
- 模块不依赖基础内核之外的任何东西;它是`kldload`可加载的独立驱动程序。
- `HARDWARE.md`、`LOCKING.md`和`SIMULATION.md`是最新的。
- `INVARIANTS`、`WITNESS`、`WITNESS_SKIPSPIN`、`DDB`、`KDB`和`KDB_UNATTENDED`在你的测试内核中启用。

这就是第18章扩展的驱动程序。新增内容在量上仍然适中:一个新文件(`myfirst_pci.c`)、一个新头文件(`myfirst_pci.h`)、一组新的探测和附加例程、对`myfirst_hw_attach`的小改动以接受资源而非分配缓冲区、扩展的分离顺序、版本升级到`1.1-pci`、新的`PCI.md`文档,以及更新的回归脚本。心智模型的变化比行数暗示的要大。

### 你将学到什么

当你完成本章时,你应该能够:

- 用一段话描述PCI和PCIe是什么,方式让初学者能够理解,明确放置总线、设备、功能、BAR、配置空间、供应商ID和设备ID的关键词汇。
- 阅读`pciconf -lv`和`devinfo -v`的输出,定位驱动程序作者关心的信息:B:D:F元组、供应商和设备ID、类、子类、接口、声明的资源和父总线。
- 编写一个最小的PCI驱动程序,向`pci`总线注册探测例程,匹配特定的供应商和设备ID,返回有意义的`BUS_PROBE_*`优先级,通过`device_printf`打印匹配的设备,并干净卸载。
- 附加和分离PCI驱动程序,匹配newbus的预期生命周期:探测首先运行(有时两次),附加每个匹配设备运行一次,分离在内核移除设备或驱动程序时运行一次,softc以正确顺序释放。
- 正确使用`DRIVER_MODULE(9)`和`MODULE_DEPEND(9)`,命名总线(`pci`)和模块依赖(`pci`),以便内核的模块加载器和newbus枚举器理解关系。
- 解释什么是BAR、固件如何在启动时分配它们、内核如何发现它们,以及为什么驱动程序不选择地址。
- 用`bus_alloc_resource_any(9)`声明PCI内存BAR,用`rman_get_bustag(9)`和`rman_get_bushandle(9)`提取其bus_space标签和句柄,并将它们交给第16章访问器层,而不更改CSR宏。
- 识别BAR何时是跨越两个配置空间槽的64位BAR、`PCIR_BAR(index)`如何工作,以及为什么按简单整数增量计算BAR在64位BAR上不总是安全的。
- 使用`pci_read_config(9)`和`pci_write_config(9)`读取通用访问器不覆盖的设备特定配置空间字段,理解宽度参数(1、2或4)和副作用约定。
- 用`pci_find_cap(9)`遍历设备的PCI能力列表以定位标准能力(电源管理、MSI、MSI-X、PCI Express),用`pci_find_extcap(9)`遍历PCIe扩展能力列表以到达现代能力如高级错误报告。
- 当设备稍后将发起DMA时调用`pci_enable_busmaster(9)`,识别为什么命令寄存器的MEMEN和PORTEN位通常在附加时已被总线驱动程序设置,知道何时需要手动断言有缺陷的设备。
- 编写附加时初始化序列,在PCI路径上保持第17章模拟后端处于非活动状态,同时为没有PCI测试环境的读者保留仅模拟构建(通过编译时开关)。
- 编写分离路径,严格按附加的相反顺序释放资源,即使在存在部分附加失败的情况下也不会泄漏资源或双重释放。
- 在bhyve或QEMU客户机中对真实PCI设备测试驱动程序,使用不与基础系统驱动程序冲突的供应商和设备ID,观察完整的附加、操作、分离和卸载周期。
- 将PCI特定代码拆分到自己的文件中,更新模块的`SRCS`行,将驱动程序标记为`1.1-pci`,并产生简短的`PCI.md`文档,记录驱动程序支持的供应商和设备ID。
- 在高层次描述MSI、MSI-X和PCIe AER在PCI图景中的位置,知道哪个后续章节接续每个主题。

列表很长;每个项目都很窄。本章的重点是组合。

### 本章不覆盖的内容

几个相邻主题被明确延后,以保持第18章专注。

- **真实中断处理程序**。用于`SYS_RES_IRQ`的`bus_alloc_resource_any(9)`、`bus_setup_intr(9)`、过滤器处理程序和ithread处理程序之间的划分、`INTR_TYPE_*`标志、`INTR_MPSAFE`,以及处理程序内部可以做什么和不可以做什么的规则属于第19章。第18章的驱动程序仍然通过用户空间写入和第17章callout进行轮询;它从不接受真实中断。
- **MSI和MSI-X**。`pci_alloc_msi(9)`、`pci_alloc_msix(9)`、向量分配、每队列中断路由和MSI-X表布局是第20章的内容。第18章仅在列出PCI能力时提及这些作为未来工作。
- **DMA**。`bus_dma(9)`标签、`bus_dmamap_create(9)`、`bus_dmamap_load(9)`、分散-聚集列表、弹跳缓冲区和缓存一致的描述符环是第20章和第21章的内容。第18章将BAR视为一组内存映射寄存器,除此之外无其他。
- **PCIe AER处理**。引入了高级错误报告的存在,因为读者应该知道这个主题存在。实现订阅AER事件、解码不可纠正错误寄存器并参与系统范围恢复的故障处理程序是后续章节的主题。
- **热插拔、设备移除和实时运行时挂起**。PCI设备在运行时到达或离开触发驱动程序必须遵守的特定newbus序列;大多数驱动程序仅通过拥有正确的分离路径就能遵守它。第18章演示正确的分离路径,将运行时电源管理留给第22章,将热插拔留给第七部分(第32章关于嵌入式平台和第35章关于异步I/O和事件处理)。
- **向虚拟机传递**。`bhyve(8)`和`vmm(4)`可以将真实PCI设备传递给客户机,这是测试的有用技术。第18章简要提及。更深入的处理属于它服务于该主题的章节。
- **SR-IOV和虚拟功能**。单个设备宣传多个虚拟功能(每个都有自己的配置空间)的PCIe能力超出了初学者章节的范围。
- **特定芯片组怪癖**。真实驱动程序通常携带特定硅片特定修订的勘误和变通方法的冗长列表。第18章针对常见情况;本书后面的故障排除章节覆盖遇到怪癖时如何推理。

停留在这些界限内使第18章保持为关于PCI子系统及其驱动程序在其中的位置的章节。词汇是可迁移的;后面特定的章节将词汇应用于中断、DMA和电源。

### 预计时间投入

- **仅阅读**:四到五个小时。PCI拓扑和newbus序列在概念上很小但在细节上很密集,每部分都值得慢读。
- **阅读加输入示例代码**:十到十二小时,分两到三次会话。驱动程序分四个阶段演进;每个阶段都是第17章代码库上的小型但真实的重构。
- **阅读加所有实验和挑战**:十六到二十小时,分四到五次会话,包括搭建bhyve或QEMU实验环境、阅读真实FreeBSD树中的`uart_bus_pci.c`和`virtio_pci_modern.c`,以及对模拟和真实PCI运行回归测试。

第2节、第3节和第5节是最密集的。如果探测-附加序列或BAR分配路径在第一次阅读时感觉陌生,那是正常的。停下来,重读第3节的BAR如何成为标签和句柄的图表,当画面清晰后继续。

### 先决条件

在开始本章之前,确认:

- 你的驱动程序源代码匹配第17章第5阶段(`1.0-simulated`)。起点假设第16章硬件层、第17章模拟后端、完整的`CSR_*`访问器系列、同步头文件,以及第三部分介绍的每个原语。
- 你的实验机器运行FreeBSD 14.3,磁盘上有`/usr/src`并与运行内核匹配。
- 调试内核已构建、安装并正常启动,启用了`INVARIANTS`、`WITNESS`、`WITNESS_SKIPSPIN`、`DDB`、`KDB`和`KDB_UNATTENDED`。
- 你的实验主机上有`bhyve(8)`或`qemu-system-x86_64`可用,你可以启动运行调试内核的FreeBSD客户机。本书中bhyve客户机是规范选择;QEMU在本章的每个实验中同样有效。
- `devinfo(8)`和`pciconf(8)`工具在你的路径中。两者都在基础系统中。

如果以上任何项目不稳固,现在修复它,而不是在尝试从移动的基础上推理第18章时修复。PCI代码比模拟代码更不宽容,因为驱动程序期望与真实总线行为之间的不匹配通常会表现为探测失败、附加失败或内核页面错误。

### 如何从本章获得最大收益

四个习惯会很快得到回报。

首先,保持`/usr/src/sys/dev/pci/pcireg.h`和`/usr/src/sys/dev/pci/pcivar.h`已加书签。第一个文件是PCI和PCIe配置空间的权威寄存器映射;每个以`PCIR_`、`PCIM_`或`PCIZ_`开头的宏都在那里定义。第二个文件是PCI访问器函数(`pci_get_vendor`、`pci_read_config`、`pci_find_cap`等)及其文档注释的权威列表。阅读这两个文件一次大约需要一小时,可以消除本章其余部分可能需要的猜测。

> **关于行号的说明。** 我们稍后将依赖的声明,如`pci_read_config`、`pci_find_cap`和`PCIR_*`寄存器偏移宏,位于`pcivar.h`和`pcireg.h`中的稳定名称下。每当本章给你这些文件中的地标时,地标就是符号。行号会随版本漂移;名称不会。Grep查找符号并相信你的编辑器报告。

其次,在你的实验主机和客户机上运行`pciconf -lv`,并在阅读时保持输出在终端中打开。本章中的每个词汇项目(供应商、设备、类、子类、能力、资源)都原文出现在该输出中。一个已经为自己的硬件读过`pciconf -lv`的读者比没有读过的读者会觉得PCI子系统更不抽象。

第三,手动输入更改并运行每个阶段。PCI代码是小型拼写错误变成静默不匹配的地方。将`0x1af4`错拼为`0x1af5`不会产生编译错误;它产生一个干净编译但从不在的驱动程序。逐字符输入值,对照测试目标检查它们,并确认`kldstat -v`显示驱动程序声明预期设备是防止一天困惑调试的习惯。

第四,在第2节后阅读`/usr/src/sys/dev/uart/uart_bus_pci.c`,在第5节后阅读`/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`。第一个文件是第18章教授的模式的简单示例,编写在初学者可以跟随的级别。第二个文件是稍微丰富的示例,展示了真实现代驱动程序如何将模式与额外机制组合。两者都不需要逐行理解;两者都值得仔细的首次阅读。

### 本章路线图

各节按顺序如下:

1. **什么是PCI以及为什么它重要。** 总线、拓扑、B:D:F元组、FreeBSD如何通过`pci(4)`子系统表示PCI,以及驱动程序作者如何通过`pciconf -lv`和`devinfo -v`感知所有这些。概念基础。
2. **探测和附加PCI设备。** 从驱动程序侧看的newbus舞蹈:`device_method_t`、探测、附加、分离、恢复、挂起、`DRIVER_MODULE(9)`、`MODULE_DEPEND(9)`、供应商和设备ID匹配,以及第18章驱动程序的第一阶段(`1.1-pci-stage1`)。
3. **理解和声明PCI资源。** 什么是BAR、固件如何分配它、`bus_alloc_resource_any(9)`如何声明它,以及返回的`struct resource *`如何变成`bus_space_tag_t`和`bus_space_handle_t`。第二阶段(`1.1-pci-stage2`)。
4. **通过`bus_space(9)`访问设备寄存器。** 第16章访问器层如何在不修改的情况下卡入新资源、CSR宏如何传递,以及驱动程序的第一次真实PCI读取如何发生。
5. **驱动程序附加时初始化。** `pci_enable_busmaster(9)`、`pci_read_config(9)`、`pci_find_cap(9)`、`pci_find_extcap(9)`,以及驱动程序在附加时执行的一小部分配置空间操作。在PCI路径上保持第17章模拟处于非活动状态。第三阶段(`1.1-pci-stage3`)。
6. **支持分离和资源清理。** 按相反顺序释放资源、处理部分附加失败、`device_delete_child`,以及分离回归脚本。
7. **测试PCI驱动程序行为。** 搭建bhyve或QEMU客户机,暴露驱动程序识别的设备,观察附加,对真实BAR操作驱动程序,从用户空间用`pciconf -r`和`pciconf -w`读写配置空间,并使用`devinfo -v`和`dmesg`追踪驱动程序的世界观。
8. **重构和版本化你的PCI驱动程序。** 最终拆分到`myfirst_pci.c`,新的`PCI.md`,版本升级到`1.1-pci`,以及回归传递。

八个节之后是动手实验、挑战练习、故障排除参考、总结第18章故事并开启第19章的收尾,以及带有第20章前向指针的到第19章的桥梁。本章末尾的参考和速查材料旨在在你学习后续第四部分章节时重读;第18章的词汇是每个后续PCI系列章节重用的词汇。

如果是第一次阅读,请按线性顺序阅读并按顺序做实验。如果是复习,第3节和第5节独立存在,适合单次阅读。



## 第1节:什么是PCI以及为什么它重要

读到本章的读者已经围绕模拟寄存器块构建了完整的驱动程序。访问器层、命令-响应协议、锁定规则、故障注入框架和分离顺序都已就绪。驱动程序对非真实的唯一让步是寄存器的来源:它们来自内核堆中的`malloc(9)`分配,而不是总线远端的硅片。第1节介绍将改变这一点的子系统。PCI是现代计算中使用最广泛的外围总线。它也是FreeBSD驱动程序的规范newbus子设备。理解什么是PCI、它如何到达这里、FreeBSD如何表示它是本章其余每节的基础。

### 简短历史,以及为什么它重要

PCI代表外围组件互连。它由Intel在1990年代初引入,作为前几代PC扩展总线(ISA、EISA、VESA本地总线等)的替代品,它们都无法扩展到现代外设很快需要的速度和宽度。原始PCI规范描述了一个并行、共享、时钟驱动的总线,携带三十二位数据,时钟频率为33 MHz,让单个设备可以请求并持有总线进行事务。一些修订将宽度增加到六十四位,将时钟提高到66 MHz,并为服务器平台引入了信号变体(PCI-X),但基本形状仍然是共享并行总线。

PCI Express,称为PCIe,是现代继任者。它几乎不变地保持PCI的软件可见模型,但用点对点串行链路的集合替换物理总线。PCI有许多设备共享一组电线,而PCIe让每个设备通过自己的通道(或通道集合,常用最多十六个,某些高端卡最多三十二个)连接到芯片组的根复合体。每通道带宽已通过连续几代攀升,从PCIe第1代的2.5 Gb/s到PCIe第5代的32 Gb/s及更高。

为什么这段历史对驱动程序作者重要?因为软件模型在过渡中没有改变。从驱动程序的角度看,PCIe设备仍然有配置空间、仍然有BAR、仍然有供应商和设备ID、仍然有能力,仍然遵循PCI建立的探测-附加-分离生命周期。物理层改变了;软件词汇没有。这是计算中少数几个三十年前的接口仍然是你在FreeBSD源代码中读到的东西的地方之一,软件模型的连续性使这成为可能。为1995年的PCI编写的代码可以在做一些更新以支持新能力的情况下,驱动2026年的PCIe第5代设备。

这种连续性有一个重要的实际后果。当本书提到"PCI"时,它几乎总是意味着"PCI或PCIe"。内核的`pci(4)`子系统处理两者。当区别重要时,例如当出现PCIe独有的功能如MSI-X或AER时,本书会指出。在其他地方,"PCI"和"PCIe"在驱动程序级别是同一事物。

### PCI设备在现代机器上的位置

打开过去二十年制造的几乎任何笔记本电脑、台式机或服务器,你会发现PCIe设备。明显的是附加卡:插槽中的网络适配器、另一个插槽中的显卡、主板M.2插槽上的NVMe驱动器、Mini-PCIe子卡上的Wi-Fi模块。不明显的是集成的:与SATA端口对话的存储控制器是PCI设备;USB主控制器是PCI设备;板载以太网是PCI设备;音频编解码器是PCI设备;平台的集成显卡是PCI设备。CPU和外部世界之间芯片组到设备互连上的一切几乎都肯定是PCI设备。

内核在启动时枚举这些设备。固件(系统BIOS或UEFI)遍历总线,读取每个设备的配置空间,分配BAR,并将控制权交给操作系统。操作系统重新遍历总线,构建自己的表示,并附加驱动程序。FreeBSD的`pci(4)`驱动程序是执行此遍历的程序。当系统多用户时,机器中的每个PCI设备都已被枚举,每个BAR都被分配了内核虚拟地址,每个匹配驱动程序的设备都已被附加。

一个实际演示:在任何FreeBSD系统上运行`pciconf -lv`。每个条目显示一个设备及其B:D:F(总线、设备、功能)地址、其供应商和设备ID、其子供应商和子系统ID、其类和子类、其当前驱动程序绑定(如果有),以及它是什么的人类可读描述。这些条目是内核看到的;描述是`pciconf`从其内部数据库查找的内容。在你的实验主机上运行此命令是最好的快速介绍,让你了解机器的PCI拓扑是什么样子。

### 总线-设备-功能元组

PCI设备的地址有三个组件。它们一起称为**总线-设备-功能元组**,或B:D:F,或有时简称"PCI地址"。

**总线号**是设备所在的物理或逻辑PCI总线。一台机器通常有一个主总线(总线0),加上PCI-to-PCI桥后面的额外总线。一台笔记本电脑可能有总线0、2、3和4;一台服务器可能有几十个。每个总线为8位宽,因此原始PCI规范支持最多256条总线。PCIe通过增强配置访问机制ECAM将其扩展到16位(65,536条总线)。

**设备号**是总线上的插槽。每条总线最多可容纳32个设备。在PCIe上,物理链路的点对点性质意味着每个桥在其每个下游总线上有一个设备;在这种情况下,设备号本质上总是0。在传统PCI上,多个设备共享一条总线,每个都有自己的设备号。

**功能号**是正在寻址的多功能设备的哪个功能。单个物理设备最多可暴露8个功能,每个都有自己的配置空间,每个都作为独立的PCI设备呈现。多功能设备很常见:典型的x86芯片组将其USB主控制器呈现为单个物理单元的多个功能;存储控制器可能在单独的功能上呈现SATA、IDE和AHCI。单功能设备(常见情况)使用功能0。

组合元组在FreeBSD的`pciconf`输出中写为`pciN:D:F`,其中N是域加总线值。在作者的测试机器上,`pci0:0:2:0`指域0、总线0、设备2、功能0,这在Intel平台上通常是集成显卡。此表示在FreeBSD版本间稳定;你会在内核的启动消息中、`dmesg`中、`devinfo -v`中和总线文档中看到它。

驱动程序很少直接关心B:D:F值。newbus子系统将其隐藏在`device_t`句柄后。但驱动程序作者关心,因为两件事使用B:D:F:系统管理员(在安装或故障排除时将B:D:F与插槽或物理设备匹配),以及内核消息(当设备附加、分离或行为异常时在`dmesg`中打印它)。当你在启动日志中看到`pci0:3:0:0: <Intel Corporation Ethernet Controller ...>`时,你正在阅读B:D:F。

### 配置空间及其内容

PCI区分每个设备的两个地址空间。第一个是BAR集合,它将设备的寄存器映射到主机内存(或I/O端口)空间;这是第16章称为"MMIO"的内容,也是第18章第3节和第4节将探索的内容。第二个是**配置空间**,它是每个设备一个小型、结构化的内存块,描述设备本身。

配置空间是供应商ID、设备ID、类代码、修订、BAR地址、能力列表指针和许多其他元数据字段所在的地方。它在传统PCI上为256字节,在PCIe上扩展到4096字节。前六十四字节的布局在每个PCI设备间标准化;其余空间用于能力和扩展能力。

驱动程序通过`pci_read_config(9)`和`pci_write_config(9)`接口到达配置空间。这两个函数接受设备句柄、配置空间的字节偏移和宽度(1、2或4字节),并返回或接受`uint32_t`值。宽度参数让驱动程序读取或写入一个字节、一个十六位字段或一个三十二位字段;内核将其转换为平台的正确底层访问原语。

驱动程序需要了解的关于配置空间的大部分内容已经被newbus层提取并缓存在设备的ivar中。这就是为什么驱动程序可以调用`pci_get_vendor(dev)`、`pci_get_device(dev)`、`pci_get_class(dev)`和`pci_get_subclass(dev)`而无需手动读取配置空间。这些访问器在`/usr/src/sys/dev/pci/pcivar.h`中定义,并通过`PCI_ACCESSOR`宏扩展为读取缓存值的内联函数。这些值在枚举时读取一次,之后保存在设备的ivar中。

对于通用访问器不覆盖的所有内容,`pci_read_config(9)`和`pci_write_config(9)`是后备。例如:如果设备的数据手册说"固件修订在配置空间偏移0x48,作为32位小端整数",驱动程序通过调用`pci_read_config(dev, 0x48, 4)`读取该值。内核安排访问,使得返回值是数据手册指定的每个架构上的小端值。

### 供应商ID和设备ID:匹配如何发生

PCI设备识别的核心是一对十六位值,称为供应商ID和设备ID。

**供应商ID**由PCI特别兴趣组(PCI-SIG)分配给制造PCI设备的公司。Intel是0x8086。Broadcom有几个(最初是0x14e4和其他通过收购获得)。Red Hat和Linux社区的虚拟化项目共享0x1af4用于virtio设备。每个PCI设备在其配置空间的`VENDOR`字段中携带其供应商的ID。

**设备ID**由供应商分配给每个特定产品。Intel的0x10D3是82574L千兆以太网控制器。Broadcom的0x165F是特定的NetXtreme BCM5719变体。Red Hat的virtio范围中的0x1001是virtio-block。供应商维护自己的设备ID分配。

**子供应商ID**和**子系统ID**一起形成第二级元组。它们识别构建芯片组的板卡,而不是芯片组本身。同一个Intel 82574L以太网芯片可能出现在Dell服务器上,子供应商为0x1028,在HP服务器上子供应商为0x103c,在通用OEM板卡上子供应商为0x8086。驱动程序可以使用子供应商或子系统来应用特定怪癖、打印更有用的识别字符串或在略有不同的板级行为之间选择。

驱动程序的探测例程匹配这些ID。在最简单的情况下,驱动程序有一个静态表列出每个支持的供应商-设备对;探测遍历表并在匹配时返回`BUS_PROBE_DEFAULT`,在不匹配时返回`ENXIS`。在更复杂的情况下,驱动程序还检查子供应商和子系统,或遍历更广泛的基于类的匹配,或两者都使用。`/usr/src/sys/dev/uart/`中的`uart_bus_pci.c`文件以可读的规模展示了这个模式。

第18章的驱动程序将使用简单的表格形式。表将持有一个或两个条目。我们目标的供应商和设备ID是bhyve或QEMU客户机将为合成测试设备暴露的,教学路径将在加载`myfirst`之前卸载否则会声明相同ID的基础系统驱动程序。

### FreeBSD如何枚举PCI设备

从"固件已设置总线"到"驱动程序的附加例程运行"的步骤值得概述理解,因为理解它们使探测和附加序列感觉必然而非神秘。

首先,平台的总线枚举代码运行。在x86上,它位于`/usr/src/sys/dev/pci/`下,由平台特定的附加代码驱动(x86使用ACPI桥和传统主桥;arm64使用基于设备树的主桥)。枚举遍历总线,读取每个设备的供应商和设备ID,读取每个设备的BAR,并记录它发现的内容。

其次,内核的newbus层为每个发现的设备创建一个`device_t`,并将其添加为PCI总线设备(`pci0`、`pci1`等)的子设备。每个子设备有一个设备方法表占位符;newbus代码还不知道哪个驱动程序将绑定。子设备有ivar:供应商、设备、子供应商、子系统、类、子类、接口、修订、B:D:F和资源描述符都缓存在ivar中供后续访问。

第三,内核邀请每个注册的驱动程序探测每个设备。每个驱动程序的`probe`方法按优先级顺序调用。驱动程序检查设备的供应商、设备和它需要的任何其他内容,并返回一小集合值之一:

- 负数:"我匹配此设备并想要它"。值越接近零意味着越高优先级。供应商和设备ID匹配的标准层级是`BUS_PROBE_DEFAULT`,即`-20`。`BUS_PROBE_VENDOR`是`-10`并胜过它;`BUS_PROBE_GENERIC`是`-100`并输给它。第2节列出完整的层级集合。
- `0`:"我以绝对优先级匹配此设备"。`BUS_PROBE_SPECIFIC`层级。没有其他驱动程序能超越它。
- 正errno(通常是`ENXIO`):"我不匹配此设备"。

内核选择返回数值最小值的驱动程序并附加它。如果两个驱动程序返回相同值,先注册的获胜。分层优先级让通用驱动程序与设备特定驱动程序共存:通用驱动程序返回`BUS_PROBE_GENERIC`,特定驱动程序返回`BUS_PROBE_DEFAULT`,特定驱动程序获胜,因为`-20`比`-100`更接近零。

第四,内核调用获胜驱动程序的`attach`方法。驱动程序分配其softc(通常由newbus预分配),用`bus_alloc_resource_any(9)`声明资源,设置中断,并注册字符设备或网络接口或设备暴露给用户空间的任何东西。如果`attach`返回0,设备是活的。如果`attach`返回errno,内核分离驱动程序(在现代newbus中附加失败时不严格需要调用`detach`;驱动程序期望在返回错误前干净地回退)。

第五,内核移动到下一个设备。过程重复直到每个PCI设备都已被探测,每个找到匹配的设备都已被附加。

分离是反向的:当设备被移除时内核调用每个驱动程序的`detach`方法(通过`devctl detach`或在模块卸载时),驱动程序释放在附加中声明的一切,按相反顺序。

这就是第18章教授驱动程序如何遵循的newbus舞蹈。第2节编写它的第一个版本;第3节到第6节添加每个额外能力;第8节将其合并为一个干净的模块。

### 从驱动程序视角看pci(4)子系统

驱动程序看不到总线枚举。它看到的是设备句柄（`device_t dev`）和一组访问器调用。`/usr/src/sys/dev/pci/pcivar.h`头文件定义了这些访问器。核心的有：

- `pci_get_vendor(dev)`以`uint16_t`返回供应商ID。
- `pci_get_device(dev)`以`uint16_t`返回设备ID。
- `pci_get_subvendor(dev)`和`pci_get_subdevice(dev)`返回子供应商和子系统。
- `pci_get_class(dev)`、`pci_get_subclass(dev)`和`pci_get_progif(dev)`返回类代码字段。
- `pci_get_revid(dev)`返回修订版。
- `pci_read_config(dev, reg, width)`从配置空间读取。
- `pci_write_config(dev, reg, val, width)`向配置空间写入。
- `pci_find_cap(dev, cap, &capreg)`查找标准PCI能力；成功返回0，不存在返回ENOENT。
- `pci_find_extcap(dev, cap, &capreg)`查找PCIe扩展能力；返回约定相同。
- `pci_enable_busmaster(dev)`在命令寄存器中设置总线主控启用位。
- `pci_disable_busmaster(dev)`清除它。

这是第18章的词汇表。本章的每一节都使用这些访问器中的一个或多个。对这个列表的形状感到舒适的读者已经准备好开始编写PCI代码了。

### 现实世界中的常见PCI设备

在继续之前，简要浏览一下PCI呈现的设备。

**网络接口控制器。** NIC几乎都是PCI设备。Intel `em(4)`驱动程序用于8254x系列、Intel `ix(4)`驱动程序用于82599系列、Intel `ixl(4)`驱动程序用于X710系列，以及Broadcom `bge(4)` / `bnxt(4)`驱动程序用于NetXtreme系列，都位于`/usr/src/sys/dev/e1000/`或`/usr/src/sys/dev/ixl/`或`/usr/src/sys/dev/bge/`下。它们是大型、生产级驱动程序，实际上练习了第四部分的每个主题。

**存储控制器。** AHCI SATA控制器、NVMe驱动器、SAS HBA和RAID控制器都是PCI。`ahci(4)`、`nvme(4)`、`mpr(4)`、`mpi3mr(4)`等位于`/usr/src/sys/dev/`下。这些是树中维护得最好的驱动程序之一。

**USB主控制器。** xHCI、EHCI和OHCI控制器是PCI。通用主控制器驱动程序附加到每个控制器，USB子系统处理其上的一切。`xhci(4)`是现代系统的标准。

**显卡和集成显卡。** FreeBSD中的GPU驱动程序大多在树外维护（来自drm-kmod port的DRM驱动程序），但它们的总线附加是标准PCI。

**音频控制器。** HDA编解码器、较老的AC'97桥和各种USB附属音频设备都以某种方式通过PCI到达系统。`snd_hda(4)`是通常的附加点。

**虚拟机中的Virtio设备。** 当FreeBSD客户机在bhyve、KVM、VMware或Hyper-V下运行时，半虚拟化设备显示为PCI。Virtio-network、virtio-block、virtio-entropy和virtio-console对客户机来说都像PCI设备。`virtio_pci(4)`驱动程序首先附加，并为每个传输特定的virtio驱动程序发布子节点。

**机器自己的芯片组组件。** 平台的LPC桥、SMBus控制器、热传感器接口和各种杂项控制功能都是PCI。

如果你曾想过为什么FreeBSD源代码树如此之大，PCI设备生态系统是大部分答案。上面列表中的每个设备都需要驱动程序。你在第18章构建的驱动程序很小，几乎不做任何事；示例中的驱动程序很大，因为它们在PCI总线上实现了真实的协议。但它们每一个的形状都是第18章所教授的。

### 模拟PCI设备：bhyve和QEMU

拥有完整测试硬件集的读者可以跳过此小节。其他人都依赖虚拟化来提供他们将驱动的PCI设备。

FreeBSD内置的`bhyve(8)`管理程序可以向客户机呈现一组模拟的PCI设备。常见的有`virtio-net`、`virtio-blk`、`virtio-rnd`、`virtio-console`、`ahci-hd`、`ahci-cd`、`e1000`、`xhci`和一个帧缓冲设备。每个都有众所周知的供应商和设备ID；客户机的PCI枚举器将它们视为真实PCI设备；客户机的驱动程序像附加到真实硬件一样附加到它们。在bhyve下运行FreeBSD客户机是本书让读者驱动程序能够附加到PCI设备的标准方式。

带有KVM的QEMU（在Linux主机上）或带有HVF加速器（在macOS主机上）提供了bhyve模拟设备的超集，加上一些专门为测试设计的设备。`pci-testdev`设备（供应商0x1b36，设备0x0005）是一个刻意简化的PCI设备，用于内核测试代码；它有两个BAR（一个内存，一个I/O），写入特定偏移触发特定行为。第18章第7节可以使用bhyve下的virtio-rnd设备或QEMU下的pci-testdev作为目标。

对于教学路径，本书以bhyve下的virtio-rnd设备为目标。原因是bhyve随每个FreeBSD安装一起提供，而QEMU需要额外的包。代价很小：virtio-rnd设备在基本系统中有一个真实驱动程序（`virtio_random(4)`），本章将展示如何阻止该驱动程序声明设备，以便`myfirst`可以声明它。

关于教学路径选择的重要说明。`myfirst`驱动程序不是真实的virtio-rnd驱动程序。它不知道如何说virtio-rnd协议；它将BAR视为一组不透明寄存器，并为了演示而读取和写入它们。这对本章的目的（证明驱动程序可以附加、读取、写入和分离）很好，但对生产使用不好。第18章是PCI附加序列的实践介绍，不是如何编写virtio驱动程序的教程。当你完成本章时，你拥有的驱动程序仍然是`myfirst`教学驱动程序，现在能够附加到PCI总线而不仅仅是kldload路径。

### 将第18章放在驱动程序演进中

快速映射一下`myfirst`驱动程序去过哪里以及将去哪里。

- **版本0.1到0.8**（第一部分到第三部分）：驱动程序学习了驱动程序生命周期、cdev机制、并发原语和协调。
- **版本0.9-coordination**（第15章末）：完整锁纪律、条件变量、sx锁、callout、taskqueue、计数信号量。
- **版本0.9-mmio**（第16章末）：`bus_space(9)`支持的寄存器块、CSR宏、访问日志、`myfirst_hw.c`中的硬件层。
- **版本1.0-simulated**（第17章末）：动态寄存器行为、改变状态的callout、命令-响应协议、故障注入、`myfirst_sim.c`中的模拟层。
- **版本1.1-pci**（第18章末，我们的目标）：模拟可切换，当驱动程序附加到真实PCI设备时，BAR成为寄存器块，`myfirst_hw_attach`使用`bus_alloc_resource_any`而不是`malloc`，第16章访问器层指向真实硅片。
- **版本1.2-intr**（第19章）：通过`bus_setup_intr(9)`注册的真实中断处理程序，以便驱动程序可以反应设备自己的状态变化而不是轮询。
- **版本1.3-msi**（第20章）：MSI和MSI-X，给驱动程序更丰富的中断路由故事。
- **版本1.4-dma**（第20章和第21章）：`bus_dma(9)`标签、描述符环和第一次真实DMA传输。

每个版本都是前一个版本之上的一层。第18章是一层，小到可以清晰地教授，大到有影响。

### 练习：读取你自己的PCI拓扑

在第2节之前，一个简短的练习让词汇表具体化。

在你的实验主机上，运行：

```sh
sudo pciconf -lv
```

你将看到内核枚举的每个PCI设备的列表。每个条目大致如下：

```text
em0@pci0:0:25:0:        class=0x020000 rev=0x03 hdr=0x00 vendor=0x8086 device=0x15ba subvendor=0x8086 subdevice=0x2000
    vendor     = 'Intel Corporation'
    device     = 'Ethernet Connection (2) I219-LM'
    class      = network
    subclass   = ethernet
```

从列表中选择三个设备。对于每个，识别：

- 设备在FreeBSD中的逻辑名称（前导`name@pciN:B:D:F`字符串）。
- 供应商和设备ID。
- 类和子类（有意义的英文类别，不只是十六进制代码）。
- 设备是否绑定了驱动程序（例如，`em0`绑定到`em(4)`；只有`none0@...`的条目没有驱动程序）。

在阅读本章其余部分时将此输出保持在终端中。第2节到第5节介绍的每个词汇项都指向你可以在这里找到的字段。练习的目的是将抽象词汇锚定到机器上的一组具体设备。

如果你在没有FreeBSD机器的情况下阅读本书，以下片段是作者实验主机上`pciconf -lv`的输出，截断为前三个设备：

```text
hostb0@pci0:0:0:0:      class=0x060000 rev=0x00 hdr=0x00 vendor=0x8086 device=0x3e31
    vendor     = 'Intel Corporation'
    device     = '8th Gen Core Processor Host Bridge/DRAM Registers'
    class      = bridge
    subclass   = HOST-PCI
pcib0@pci0:0:1:0:       class=0x060400 rev=0x00 hdr=0x01 vendor=0x8086 device=0x1901
    vendor     = 'Intel Corporation'
    device     = '6th-10th Gen Core Processor PCIe Controller (x16)'
    class      = bridge
    subclass   = PCI-PCI
vgapci0@pci0:0:2:0:     class=0x030000 rev=0x00 hdr=0x00 vendor=0x8086 device=0x3e9b
    vendor     = 'Intel Corporation'
    device     = 'CoffeeLake-H GT2 [UHD Graphics 630]'
    class      = display
    subclass   = VGA
```

三个设备，三个驱动程序，三个类代码。主桥（`hostb0`）是PCI到内存总线的桥；PCI桥（`pcib0`）是通向GPU插槽的PCI到PCI桥；VGA类设备（`vgapci0`）是Coffee Lake芯片组上的集成显卡。它们每一个都遵循第18章教授的探测-附加-分离舞蹈。改变的是驱动程序。总线舞蹈不变。

### 第1节收尾

PCI是现代系统的典型外设总线，是FreeBSD典型的newbus子级。它由PCI和PCIe共享，它们在物理层不同但呈现相同的软件可见模型。每个PCI设备都有B:D:F地址、配置空间、一组BAR、供应商ID、设备ID和在内核newbus树中的位置。驱动程序的工作是通过ID匹配一个或多个设备，声明它们的BAR，并通过某种用户空间接口暴露它们的行为。FreeBSD的`pci(4)`子系统进行枚举；驱动程序进行附加。

第1节的词汇表是本章其余部分使用的词汇表。B:D:F、配置空间、BAR、供应商和设备ID、类代码、能力和newbus探测-附加-分离序列。如果其中任何一个感觉不熟悉，请在继续之前重读相关小节。第2节使用词汇表构建驱动程序的第一个版本。

## 第2节:探测和附加PCI设备

第1节确立了什么是PCI以及FreeBSD如何表示它。第2节是驱动程序最终使用该词汇的地方。这里的目标是搭建最小可行PCI驱动程序:一个注册为PCI总线候选、匹配特定供应商和设备ID、在`dmesg`中打印匹配成功横幅、并干净卸载的驱动程序。还没有BAR声明。还没有寄存器访问。只是骨架。

骨架很重要。它在隔离中引入探测-附加-分离序列,在BAR和资源和配置空间遍历挤占画面之前。一个手动编写此骨架一次、然后键入`kldload ./myfirst.ko`、看到`dmesg`报告驱动程序探测和附加、并键入`kldunload myfirst`看到分离侧干净触发的读者,已经为后续所有内容建立了正确的心智模型。每个后续第四部分章节都假设这个心智模型。

### 探测-附加-分离契约

每个newbus驱动程序在其生命周期核心有三个方法。`probe`询问"这是我懂得如何驱动的设备吗?"`attach`说"是的,我想要它,这是我声明它的方式"。`detach`说"释放此设备,我要离开了"。

**探测**。内核对已枚举总线的每个设备,为每个已注册对该总线感兴趣的驱动程序调用一次。驱动程序读取设备的供应商和设备ID(以及它需要决定的任何其他内容),如果想要设备则返回优先级值,如果不想要则返回`ENXIO`。优先级系统是让特定驱动程序胜过通用驱动程序的方式:返回`BUS_PROBE_DEFAULT`的驱动程序在两者都想要同一设备时胜过返回`BUS_PROBE_GENERIC`的驱动程序。如果没有驱动程序返回匹配,设备保持未声明(你会在`devinfo -v`中看到这作为`nonea@pci0:...`条目)。

一个微妙的点:**探测可能对给定设备调用多次**。newbus重新探测机制存在以处理运行时出现的设备(热插拔)或从挂起返回的设备。好的探测是幂等的:它读取相同状态,做出相同决定,返回相同值。探测绝不能分配资源、设置定时器、注册中断或做任何需要撤销的事情。它只检查并决定。

第二个微妙的点:**探测在附加之前运行,但在内核已分配设备资源之后**。BAR、IRQ和配置空间都可以从探测访问。这意味着探测可以通过`pci_read_config`读取设备特定的寄存器,以在需要时通过修订或硅ID区分离片的变体。真实驱动程序偶尔这样做。第18章的驱动程序不需要;供应商和设备ID就够了。

**附加**。在探测选择获胜者后每个设备调用一次。驱动程序的附加例程是真正工作发生的地方:softc初始化、资源分配、寄存器映射、字符设备创建和设备在启动时需要的任何配置。如果附加返回0,设备是活的;内核认为驱动程序绑定到设备并继续。如果附加返回非零值,内核将附加视为失败。驱动程序必须在返回错误前清理它分配的任何东西;现代newbus在这种情况下不调用分离(旧约定会,所以旧驱动程序仍结构化其错误路径以处理它)。

**分离**。当驱动程序从设备解绑时调用。调用是附加的镜像:附加分配的一切,分离释放。附加设置的一切,分离拆除。附加注册的一切,分离取消注册。顺序是严格的:分离必须按附加的反向顺序撤销。这里的错误会在卸载时产生内核崩溃、最佳情况下泄漏的资源或最坏情况下微妙的释放后使用错误。

**恢复**和**挂起**是可选方法。它们在系统挂起和恢复时调用,给驱动程序机会在电源事件中保存和恢复设备状态。第18章的驱动程序在第一阶段不实现任何一个方法;当主题服务于材料时,我们将在后面的章节添加恢复。

还有其他方法(`shutdown`、`quiesce`、`identify`)对于基本PCI驱动程序不太重要。第18章骨架只注册三个核心方法加`DEVMETHOD_END`。

### 设备方法表

FreeBSD的newbus机制通过表到达驱动程序方法。表是`device_method_t`条目的数组,每个条目将方法名映射到实现它的C函数。表以`DEVMETHOD_END`结束,这只是一个归零条目,告诉newbus"这里没有更多方法了"。

表在驱动程序源代码中声明,文件作用域,像这样:

```c
static device_method_t myfirst_pci_methods[] = {
	DEVMETHOD(device_probe,		myfirst_pci_probe),
	DEVMETHOD(device_attach,	myfirst_pci_attach),
	DEVMETHOD(device_detach,	myfirst_pci_detach),
	DEVMETHOD_END
};
```

每个`DEVMETHOD(name, func)`扩展为`{ name, func }`初始化器。newbus层通过在此表中查找名称来到达驱动程序方法。如果方法未注册(例如,`device_resume`不在此表中),newbus层使用默认实现;对于`resume`,默认是无操作,对于`probe`,默认是`ENXIO`。

方法名在`/usr/src/sys/sys/bus.h`中定义,由newbus构建系统扩展。每个方法对应驱动程序必须匹配的函数原型。例如,`device_probe`方法的原型是:

```c
int probe(device_t dev);
```

驱动程序实现必须有那个确切签名。类型不匹配产生编译错误,而不是运行时神秘;如果你的探测签名错误,构建将失败。

### 驱动程序结构

与方法表一起,驱动程序声明一个`driver_t`。此结构将方法表、softc大小和短名称绑定在一起:

```c
static driver_t myfirst_pci_driver = {
	"myfirst",
	myfirst_pci_methods,
	sizeof(struct myfirst_softc),
};
```

名称(`"myfirst"`)是newbus在编号单元实例时将使用的。第一个附加的设备变成`myfirst0`,第二个`myfirst1`,以此类推。此名称是`devinfo -v`显示的,以及用户空间工具(如`/dev/myfirst0`,如果驱动程序用该名称创建cdev)暴露的。

softc大小告诉newbus为每个设备的softc分配多少字节。分配是自动的:在附加运行时,`device_get_softc(dev)`返回指向请求大小的归零块的指针。驱动程序不为softc本身调用`malloc`;它使用newbus给它的。这是第10章以来`myfirst`驱动程序一直在使用的便利;它对PCI更重要,因为每个单元有自己的softc而newbus管理生命周期。

### DRIVER_MODULE和MODULE_DEPEND

驱动程序通过两个宏粘合到PCI总线。第一个是`DRIVER_MODULE(9)`:

```c
DRIVER_MODULE(myfirst, pci, myfirst_pci_driver, NULL, NULL);
```

此宏的扩展执行几件事。它将驱动程序注册为`pci`总线的子候选,将`driver_t`包装在内核模块描述符中。它调度驱动程序参与`pci`总线枚举的每个设备的探测。它提供可选模块事件处理程序的钩子(两个`NULL`分别用于模块init和cleanup;我们暂时留空)。

第一个参数是模块名称,必须匹配`driver_t`中的名称。第二个参数是总线名称;`pci`是PCI总线驱动程序的newbus名称。第三个参数是驱动程序本身。其余参数是可选回调。

宏有一个微妙的后果:驱动程序将参与系统中每个PCI总线的探测。如果你有多个PCI域,驱动程序将被提供给每个域上的每个设备。探测的工作是只对驱动程序实际支持的设备说"是";内核的工作是询问。

第二个宏是`MODULE_DEPEND(9)`:

```c
MODULE_DEPEND(myfirst, pci, 1, 1, 1);
```

这告诉模块加载器`myfirst.ko`依赖于`pci`内核模块。三个数字是最小、首选和最大版本。对版本1的零到一依赖是常见情况。加载器使用此信息拒绝在内核的PCI子系统不存在时加载`myfirst.ko`(这在真实系统上基本从不为假,但检查是好习惯)。

没有`MODULE_DEPEND`,加载器可能在早期启动时、PCI子系统还不可用时加载`myfirst.ko`,导致`DRIVER_MODULE`尝试针对还不存在的总线注册时崩溃。有了它,加载器正确序列化加载。

### 通过供应商和设备ID匹配

探测例程是供应商和设备匹配发生的地方。模式是静态表和循环。考虑一个最小版本:

```c
static const struct myfirst_pci_id {
	uint16_t	vendor;
	uint16_t	device;
	const char	*desc;
} myfirst_pci_ids[] = {
	{ 0x1af4, 0x1005, "Red Hat / Virtio entropy source (demo target)" },
	{ 0, 0, NULL }
};

static int
myfirst_pci_probe(device_t dev)
{
	uint16_t vendor = pci_get_vendor(dev);
	uint16_t device = pci_get_device(dev);
	const struct myfirst_pci_id *id;

	for (id = myfirst_pci_ids; id->desc != NULL; id++) {
		if (id->vendor == vendor && id->device == device) {
			device_set_desc(dev, id->desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}
```

几件事值得注意。表很小且静态,每个支持的设备一个条目。`pci_get_vendor`和`pci_get_device`读取缓存的ivar,所以调用很便宜。比较是简单循环;表短到不需要哈希。`device_set_desc`安装一个人类可读描述,`pciconf -lv`和`dmesg`将在设备附加时打印。`BUS_PROBE_DEFAULT`是供应商特定匹配的标准优先级;它胜过通用基于类的驱动程序但输给任何显式返回更负值的驱动程序。

一个微妙但重要的点:此探测例程目标是基础系统`virtio_random(4)`驱动程序通常声明的virtio-rnd(熵)设备。如果两个驱动程序都加载,系统的优先级规则决定获胜者。`virtio_random`注册`BUS_PROBE_DEFAULT`,`myfirst`也一样。决胜局是注册顺序,这是可变的。保证`myfirst`附加的可靠方法是先卸载`virtio_random`再加载`myfirst`。第7节将展示如何。

第二点说明:上面示例中的供应商和设备ID目标是virtio设备。真实硬件的真实PCI驱动程序会目标其ID尚未被基础系统驱动程序声明的芯片。对于生产驱动程序,列表将包括目标芯片组每个支持的变体,通常带有识别硅修订的描述性字符串。`uart_bus_pci.c`有六十多个条目;`ix(4)`有一百多个。

### 探测优先级层级

FreeBSD定义几个探测优先级层级,定义在`/usr/src/sys/sys/bus.h`:

- `BUS_PROBE_SPECIFIC` = 0。驱动程序精确匹配设备。没有其他驱动程序能超越它。
- `BUS_PROBE_VENDOR` = -10。驱动程序是供应商提供的,应该胜过任何通用的。
- `BUS_PROBE_DEFAULT` = -20。供应商和设备ID匹配的标准层级。
- `BUS_PROBE_LOW_PRIORITY` = -40。较低优先级匹配,通常用于只在没有其他驱动程序声明设备时才想成为默认的驱动程序。
- `BUS_PROBE_GENERIC` = -100。如果没有更特定的存在,附加到一类设备的通用驱动程序。
- `BUS_PROBE_HOOVER` = -1000000。绝对最后手段;想要没有其他驱动程序声明的设备的驱动程序。
- `BUS_PROBE_NOWILDCARD` = -2000000000。newbus识别机制使用的特殊情况标记。

你将编写或阅读的大多数驱动程序使用`BUS_PROBE_DEFAULT`。有些使用`BUS_PROBE_VENDOR`如果期望与通用驱动程序共存。少数使用`BUS_PROBE_GENERIC`或更低作为其后备模式。第18章的驱动程序全程使用`BUS_PROBE_DEFAULT`。

优先级值按约定为负数,使得数值最小的值获胜。更特定的驱动程序有更负的值。这在第一次阅读时是反直觉的;心智模型是"距离完美的距离,向下测量"。`BUS_PROBE_SPECIFIC`为零距离。`BUS_PROBE_GENERIC`差一百个单位。

### 编写最小PCI驱动程序

综上所述,这是第18章第1阶段驱动程序,作为单个自包含文件呈现,从第17章骨架中生长。文件名是`myfirst_pci.c`;它在第18章是新的,与现有的`myfirst.c`、`myfirst_hw.c`和`myfirst_sim.c`并存。

```c
/*
 * Copyright (c) 2026 Edson Brandi
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * myfirst_pci.c -- 第18章第1阶段PCI探测/附加骨架。
 *
 * 在此阶段驱动程序只探测、附加和分离。
 * 它尚未声明BAR或触摸设备寄存器。第3节
 * 添加资源分配。第5节将访问器层连接到
 * 声明的BAR。
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "myfirst.h"
#include "myfirst_pci.h"

static const struct myfirst_pci_id myfirst_pci_ids[] = {
	{ MYFIRST_VENDOR_REDHAT, MYFIRST_DEVICE_VIRTIO_RNG,
	    "Red Hat Virtio entropy source (myfirst demo target)" },
	{ 0, 0, NULL }
};

static int
myfirst_pci_probe(device_t dev)
{
	uint16_t vendor = pci_get_vendor(dev);
	uint16_t device = pci_get_device(dev);
	const struct myfirst_pci_id *id;

	for (id = myfirst_pci_ids; id->desc != NULL; id++) {
		if (id->vendor == vendor && id->device == device) {
			device_set_desc(dev, id->desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	sc->dev = dev;
	device_printf(dev,
	    "attaching: vendor=0x%04x device=0x%04x revid=0x%02x\n",
	    pci_get_vendor(dev), pci_get_device(dev), pci_get_revid(dev));
	device_printf(dev,
	    "           subvendor=0x%04x subdevice=0x%04x class=0x%02x\n",
	    pci_get_subvendor(dev), pci_get_subdevice(dev),
	    pci_get_class(dev));

	/*
	 * 第1阶段没有资源要声明和没有东西要初始化
	 * 除了softc指针。第2阶段将添加BAR分配。
	 */
	return (0);
}

static int
myfirst_pci_detach(device_t dev)
{
	device_printf(dev, "detaching\n");
	return (0);
}

static device_method_t myfirst_pci_methods[] = {
	DEVMETHOD(device_probe,		myfirst_pci_probe),
	DEVMETHOD(device_attach,	myfirst_pci_attach),
	DEVMETHOD(device_detach,	myfirst_pci_detach),
	DEVMETHOD_END
};

static driver_t myfirst_pci_driver = {
	"myfirst",
	myfirst_pci_methods,
	sizeof(struct myfirst_softc),
};

DRIVER_MODULE(myfirst, pci, myfirst_pci_driver, NULL, NULL);
MODULE_DEPEND(myfirst, pci, 1, 1, 1);
MODULE_VERSION(myfirst, 1);
```

伴随头文件`myfirst_pci.h`:

```c
/*
 * Copyright (c) 2026 Edson Brandi
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * myfirst_pci.h -- myfirst驱动程序的第18章PCI接口。
 */

#ifndef _MYFIRST_PCI_H_
#define _MYFIRST_PCI_H_

#include <sys/types.h>

/* 第18章演示的目标供应商和设备ID。 */
#define MYFIRST_VENDOR_REDHAT		0x1af4
#define MYFIRST_DEVICE_VIRTIO_RNG	0x1005

/* 支持设备表中的单个条目。 */
struct myfirst_pci_id {
	uint16_t	vendor;
	uint16_t	device;
	const char	*desc;
};

#endif /* _MYFIRST_PCI_H_ */
```

`Makefile`需要小更新:

```makefile
# 第18章第1阶段myfirst驱动程序的Makefile。

KMOD=  myfirst
SRCS=  myfirst.c myfirst_hw.c myfirst_sim.c myfirst_pci.c cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.1-pci-stage1\"

.include <bsd.kmod.mk>
```

从第17章第5阶段变化了三件事。 `myfirst_pci.c`添加到了`SRCS`。版本字符串升级到`1.1-pci-stage1`。其他不需要改变。

### 驱动程序加载时发生什么

遍历加载序列使骨架具体化。

读者调用`kldload ./myfirst.ko`。内核的模块加载器读取模块的元数据。它看到`MODULE_DEPEND(myfirst, pci, ...)`声明并验证`pci`模块已加载。(在运行的内核上总是如此,所以此检查通过。)它看到`DRIVER_MODULE(myfirst, pci, ...)`声明并注册驱动程序作为PCI总线的探测候选。

内核然后遍历系统中每个PCI设备并为每个调用`myfirst_pci_probe`。大多数探测返回`ENXIO`,因为供应商和设备ID不匹配。一个探测,针对客户机中的virtio-rnd设备,返回`BUS_PROBE_DEFAULT`。内核选择`myfirst`作为该设备的驱动程序。

如果virtio-rnd设备已经附加到`virtio_random`,新驱动程序的探测结果与现有绑定竞争。内核不会仅因为新驱动程序出现就自动重新绑定设备;相反,`myfirst`不会附加。要强制重新绑定,读者必须先分离现有驱动程序:`devctl detach virtio_random0`,或`kldunload virtio_random`。第7节将遍历这个过程。

一旦内核决定`myfirst`获胜,它分配一个新的softc(`driver_t`中请求的`sizeof(struct myfirst_softc)`块),将其清零,并调用`myfirst_pci_attach`。附加例程运行。它打印一个短横幅。它返回0。内核将设备标记为已附加。

`dmesg`显示序列:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> port 0x6040-0x605f mem 0xc1000000-0xc100001f irq 19 at device 5.0 on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
```

`devinfo -v`显示设备及其父设备、资源和驱动程序绑定。`pciconf -lv`显示它时带有`myfirst0`作为其绑定名称。

在卸载时,反向发生。`kldunload myfirst`对每个附加的设备调用`myfirst_pci_detach`。分离打印自己的横幅,返回0,内核释放softc。`DRIVER_MODULE`从PCI总线注销驱动程序。模块加载器从内存中移除`myfirst.ko`镜像。

### device_printf以及为什么它重要

一个值得强调的小细节。骨架使用`device_printf(dev, ...)`而不是`printf(...)`。区别很小但重要。

`printf`打印到内核日志而没有任何前缀。一行说"attaching"很难与特定设备关联;日志中充满了来自系统中每个驱动程序的消息。`device_printf(dev, ...)`用驱动程序的名称和单元号为消息添加前缀:"myfirst0: attaching"。前缀使日志可读,即使同时附加了多个驱动程序实例(`myfirst0`、`myfirst1`等)。

在FreeBSD源代码树中约定是严格的:每个在有`device_t`可用的代码路径中的驱动程序使用`device_printf`,并且只在非常早期的模块init或模块卸载时回退到`printf`,那里句柄不可用。习惯使用`device_printf`的读者产生易于阅读和诊断的日志;到处使用`printf`的读者产生其他贡献者会要求更正的日志。

### Softc和device_get_softc

第17章驱动程序已经有了softc结构。第18章的附加例程只是重用它,添加了一项:`device_t`存储在`sc->dev`中,以便后续代码(包括第16章访问器和第17章模拟)可以到达它。

提醒:`device_get_softc(dev)`返回newbus预分配的softc指针。softc在附加运行前被清零,因此每个字段从零或NULL或false开始。softc在分离返回后由newbus自动释放;驱动程序不对其调用`free`。

这值得指出,因为它不同于旧FreeBSD驱动程序和某些Linux驱动程序中基于`malloc`的softc模式。在newbus中,总线拥有softc生命周期。在旧模式中,驱动程序拥有它,必须记住分配和释放。在旧模型中忘记分配导致附加中的空指针解引用;忘记释放导致分离中的内存泄漏。现代newbus中这两种失败模式都不存在,因为总线处理这两种操作。

### 面对卸载的探测-附加-分离顺序

驱动程序作者的一个重要细节是当`kldunload myfirst`运行时,一个或多个`myfirst`设备已附加时会发生什么。

模块加载器的卸载路径首先尝试分离绑定到驱动程序的每个设备。对于每个设备,它调用驱动程序的`detach`方法。如果`detach`返回0,设备被认为已解绑;softc被释放。如果`detach`返回非零(通常是`EBUSY`),模块加载器中止卸载:模块保持加载,设备保持附加,卸载返回错误。这就是驱动程序在工作进行中时拒绝离开的方式。

`myfirst`驱动程序的分离通常应该成功,因为当用户要求卸载时驱动程序的面向用户状态是空闲的。但是正在积极服务请求的驱动程序(例如,其cdev上有打开文件描述符的磁盘驱动程序)从分离返回`EBUSY`并强制用户先关闭描述符。

对于第18章第1阶段,分离是单行的:打印横幅并返回0。在后续阶段,分离将获取锁、取消callout、释放资源,最后在一切拆除后返回0。

### 第1阶段输出:成功是什么样子

读者加载驱动程序。在有virtio-rnd设备附加的bhyve客户机上,并先卸载`virtio_random`,`dmesg`应该打印类似:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
```

`kldstat -v | grep myfirst`显示驱动程序已加载。`devinfo -v | grep myfirst`显示设备附加到`pci0`。`pciconf -lv | grep myfirst`确认匹配。

在卸载时:

```text
myfirst0: detaching
```

设备返回未声明状态。(或者返回`virtio_random`,如果该模块被重新加载。)

如果virtio-rnd设备不存在,不会发生附加;驱动程序加载,但`devinfo`中没有`myfirst0`出现。如果驱动程序在没有该设备的主机上加载,也会发生同样的情况:探测针对系统中的每个PCI设备运行,探测对每个返回`ENXIO`,不发生附加。这是正确和预期的行为;驱动程序是有耐心的。

### 此阶段的常见错误

作者见过初学者陷入的陷阱简短列表。

**忘记`MODULE_DEPEND`。** 驱动程序加载,但在启动早期崩溃,因为PCI模块还未初始化。添加声明修复它。一旦你知道去寻找它,症状很容易识别。

**`DRIVER_MODULE`中的名称错误。** 名称必须匹配`driver_t`中的`"name"`字符串。不匹配产生驱动程序加载但从不探测设备的微妙错误。修复是使两者匹配;约定是两者都使用驱动程序的短名称。

**从探测返回错误值。** 初学者有时从探测返回0,认为"零意味着成功"。零是`BUS_PROBE_SPECIFIC`,这是最强的可能匹配;驱动程序将胜过任何想要同一设备的其他驱动程序。这几乎从不是你的意思。对标准匹配返回`BUS_PROBE_DEFAULT`。

**返回正错误码。** newbus约定是探测返回负优先级值或正errno。返回错误符号是常见拼写错误。`ENXIO`是正确的"我不匹配"返回。

**在探测中留下资源已分配。** 探测必须是副作用自由的。如果探测分配资源,它必须在返回前释放。最干净的方法是从不从不从探测分配;在附加中做所有事情。

**混淆`pci_get_vendor`与`pci_read_config`**。两者不同。`pci_get_vendor`读取缓存的ivar。`pci_read_config(dev, PCIR_VENDOR, 2)`读取活动的配置空间。两者对此字段产生相同值,但一个是便宜的内联函数,另一个是总线事务。使用访问器。

**忘记包含正确的头文件。** `dev/pci/pcireg.h`定义`PCIR_*`常量。`dev/pci/pcivar.h`定义`pci_get_vendor`等。两者都需要包含。编译器错误通常是`pci_get_vendor`的"未定义标识符";修复是缺少的include。

**`MODULE_VERSION`名称冲突。** 第一个参数必须匹配驱动程序名称。`MODULE_VERSION(myfirst, 1)`可以。`MODULE_VERSION(myfirst_pci, 1)`不行,因为`myfirst_pci`是文件名,不是模块名。模块加载器通过`DRIVER_MODULE`中注册的名称查找模块。

每一个都是可恢复的。调试内核会捕获其中一些(加载早于pci的情况产生调试内核漂亮打印的崩溃)。其他产生微妙的错误行为,最容易被仔细测试每次更改后的加载-附加-分离-卸载周期捕获。

### 检查点:第1阶段工作

在移动到第3节之前,确认第1阶段驱动程序端到端工作。

在bhyve或QEMU客户机上:

- `kldload virtio_pci`(如果尚未加载)。
- `kldunload virtio_random`(如果已加载;如果未加载则优雅失败)。
- `kldload ./myfirst.ko`。
- `kldstat -v | grep myfirst`应该显示模块已加载。
- `devinfo -v | grep myfirst`应该显示`myfirst0`附加在`pci0`下。
- `dmesg`应该显示附加横幅。
- `kldunload myfirst`。
- `dmesg`应该显示分离横幅。
- `devinfo -v | grep myfirst`应该什么都不显示。

如果所有这些都通过,你有一个工作的第1阶段驱动程序。下一步是声明BAR。

### 总结第2节

探测-附加-分离序列是每个PCI驱动程序的骨架。第2节以最小可能形式构建它:匹配一个供应商-设备对的探测、打印横幅的附加、打印另一个横幅的分离,以及足够多的胶水(`DRIVER_MODULE`、`MODULE_DEPEND`、`MODULE_VERSION`)使内核的模块加载器和newbus枚举器接受它。

第1阶段骨架尚未做的事:声明BAR、读取寄存器、启用总线主控、遍历能力列表、创建cdev或协调PCI构建与第17章模拟构建。这些每一个都是本章后续节的主题。骨架重要是因为每个后续主题都插入其中而不重塑它。附加随着章节进行从两行函数增长到二十行函数;探测保持原样。

第3节介绍BAR。它解释它们是什么、如何被分配以及驱动程序如何声明BAR描述的内存范围。到第3节结束时,驱动程序将持有其BAR的`struct resource *`以及准备交给第16章访问器层的标签和句柄对。



## 第3节:理解和声明PCI资源

有了探测和简单附加,驱动程序知道何时找到了它想驱动的设备。它还不知道的是如何到达该设备的寄存器。第3节关闭这个差距。它从PCI规范中的BAR是什么开始,遍历固件和内核如何设置它,最后是驱动程序代码,该代码声明BAR并将其转换为第16章访问器可以不变使用的`bus_space`标签和句柄。

本节的要点是让"BAR"这个词具体化。完成第3节的读者应该能用一句话回答:BAR是配置空间字段,设备在其中说"这是我需要多少内存(或多少I/O端口),以及固件将其映射到主机地址空间后你如何到达它"。本节其余所有内容都建立在这个句子上。

### 确切地说,BAR是什么

每个PCI设备通过基址寄存器宣传它需要的资源。标准PCI设备头(非桥类型)有六个BAR,每个四字节宽,位于配置空间偏移`0x10`、`0x14`、`0x18`、`0x1c`、`0x20`和`0x24`。在FreeBSD的`/usr/src/sys/dev/pci/pcireg.h`中,这些偏移由宏`PCIR_BAR(n)`产生,其中n范围从0到5。

每个BAR描述一个地址范围。BAR的低位告诉软件范围是在内存空间还是I/O端口空间。如果低位为零,范围是内存映射的;如果为一,范围在I/O端口地址空间。低位以上的所有内容是地址;确切字段布局取决于BAR类型。

对于内存映射BAR,布局是:

- 位0:`0`表示内存。
- 位2-1:类型。`0b00`表示32位,`0b10`表示64位,`0b01`保留(以前是"低于1MB")。
- 位3:可预取。`1`如果设备承诺读取无副作用,因此CPU可能预取并合并访问。
- 位31-4(或63-4对于64位):地址。

64位BAR占用两个连续的BAR槽。低槽保存地址的低32位(带有类型位);高槽保存地址的高32位。遍历BAR列表的驱动程序必须识别何时遇到64位BAR并跳过已消耗的高槽。

对于I/O端口BAR:

- 位0:`1`表示I/O。
- 位1:保留。
- 位31-2:端口地址。

I/O端口BAR在现代设备上不太常见。大多数现代PCIe设备专门使用内存映射BAR。第18章专注于内存映射BAR。

### BAR如何获得地址

BAR分两次写入。第一次是硅设计师指定的:从BAR读取返回设备的要求。低位类型字段是只读的。地址字段是可读写,但有一个陷阱:向地址字段写入全1并读回告诉固件范围有多大。设备返回一个值,其中低位(大小以下的位)为零,高位(设备不实现的位)返回写入的内容。固件将回读解释为大小掩码。

第二次分配实际地址。固件(BIOS或UEFI)遍历每个PCI设备上的每个BAR,注意每个需要的大小,划分主机地址空间以满足所有这些,并将分配的地址写回每个BAR。到操作系统启动时,每个BAR都有一个真实地址,操作系统可以使用它到达设备。

操作系统可以可选地重做分配,如果它想(为了热插拔支持或如果固件做了糟糕的工作)。FreeBSD主要接受固件的分配;`hw.pci.realloc_bars` sysctl和`bus_generic_probe`逻辑处理需要重新分配的不常见情况。

从驱动程序的角度看,所有这些都已完成,在附加运行时。BAR有地址,地址已映射到内核虚拟空间,驱动程序只需要按编号请求资源。

### rid参数和PCIR_BAR

驱动程序通过调用`bus_alloc_resource_any(9)`并带有标识分配哪个BAR的资源ID(通常称为rid)来声明BAR。对于内存映射BAR,rid是BAR的配置空间偏移,由宏`PCIR_BAR(n)`产生:

- `PCIR_BAR(0)` = `0x10` (BAR 0)
- `PCIR_BAR(1)` = `0x14` (BAR 1)
- ...
- `PCIR_BAR(5)` = `0x24` (BAR 5)

传递`PCIR_BAR(0)`给`bus_alloc_resource_any`请求BAR 0。传递`PCIR_BAR(1)`请求BAR 1。宏是`pcireg.h`中的一行:

```c
#define	PCIR_BAR(x)	(PCIR_BARS + (x) * 4)
```

其中`PCIR_BARS`是`0x10`。

初学者有时传递`0`或`1`作为`rid`,在分配失败时感到惊讶。`rid`不是BAR索引;它是偏移。使用`PCIR_BAR(index)`,除非你有特定理由传递原始偏移。

### 资源类型:SYS_RES_MEMORY vs SYS_RES_IOPORT

`bus_alloc_resource_any`接受一个类型参数,告诉内核驱动程序想要什么类型的资源。对于内存BAR,类型是`SYS_RES_MEMORY`。对于I/O端口BAR,类型是`SYS_RES_IOPORT`。对于中断,它是`SYS_RES_IRQ`。少量资源类型在`/usr/src/sys/arm64/include/resource.h`(以及每个架构的等价物)中定义;内存、I/O端口和IRQ是PCI驱动程序正常使用的三种。

PCI配置空间本身不通过`bus_alloc_resource_any`声明。驱动程序通过`pci_read_config(9)`和`pci_write_config(9)`到达它,它们通过PCI总线驱动程序路由访问,而无需资源句柄。

不知道其BAR是内存还是I/O的驱动程序可以检查配置空间中BAR的低位以找出。知道(因为数据手册这么说,或因为设备在本章中一直是MMIO)的驱动程序只需传递正确的类型。

大多数PCIe设备将其主接口放在内存空间,并在I/O端口空间有可选的兼容窗口。驱动程序通常首先请求内存BAR,如果失败则回退到I/O端口BAR。第18章的驱动程序只请求内存;它目标的virtio-rnd设备将其寄存器暴露在内存BAR中。

### RF_ACTIVE标志

`bus_alloc_resource_any`还接受一个flags参数。两个最常设置的标志是:

- `RF_ACTIVE`:作为分配的一部分激活资源。没有此标志,分配保留资源但不映射它;驱动程序必须单独调用`bus_activate_resource(9)`。有了它,资源在一个步骤中分配和激活。
- `RF_SHAREABLE`:资源可以与其他驱动程序共享。这对中断很重要(在传统系统上多个设备之间共享的IRQ);对内存BAR不太重要。

对于内存BAR,常见情况是单独的`RF_ACTIVE`。对于可能共享的IRQ,它是`RF_ACTIVE | RF_SHAREABLE`。第18章只使用`RF_ACTIVE`。

### 详细介绍bus_alloc_resource_any

函数签名是:

```c
struct resource *bus_alloc_resource_any(device_t dev, int type,
    int *rid, u_int flags);
```

三个参数,加一个返回值。

`dev`是设备句柄。`type`是`SYS_RES_MEMORY`、`SYS_RES_IOPORT`或`SYS_RES_IRQ`。`rid`是指向持有资源ID的整数的指针;内核可能更新它(例如,当驱动程序传递通配符时告诉驱动程序实际使用了哪个槽)。`flags`是上述位掩码。

返回值是`struct resource *`。成功时非NULL;失败时NULL。资源句柄是每个后续操作(读、写、释放)使用的。

一个典型调用看起来像:

```c
int rid = PCIR_BAR(0);
struct resource *bar;

bar = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
if (bar == NULL) {
	device_printf(dev, "cannot allocate BAR0\n");
	return (ENXIO);
}
```

调用后,`bar`指向一个已分配和激活的资源;`rid`可能已被内核更新,如果它选择了与驱动程序请求不同的槽(对于通配符分配,这就是所选槽变得可见的地方)。

### 从资源到标签和句柄

资源句柄是驱动程序到BAR的连接,但第16章访问器层期望一个`bus_space_tag_t`和`bus_space_handle_t`,而不是`struct resource *`。两个助手将一个变成另一个:

- `rman_get_bustag(res)`返回`bus_space_tag_t`。
- `rman_get_bushandle(res)`返回`bus_space_handle_t`。

两者都是在`/usr/src/sys/sys/rman.h`中定义的内联访问器函数。资源内部存储标签和句柄;访问器返回存储的值。驱动程序然后将标签和句柄存储在自己的状态(在第18章,在硬件层的`struct myfirst_hw`中),以便第16章访问器可以使用它们。

模式很短:

```c
sc->hw->regs_tag = rman_get_bustag(bar);
sc->hw->regs_handle = rman_get_bushandle(bar);
```

这两行之后,`CSR_READ_4(sc, off)`和`CSR_WRITE_4(sc, off, val)`对真实BAR工作。驱动程序中没有其他代码需要知道后端已改变。

### rman_get_size和rman_get_start

两个额外的助手提取资源覆盖的地址范围:

- `rman_get_size(res)`返回字节数。
- `rman_get_start(res)`返回物理或总线起始地址。

驱动程序使用`rman_get_size`来健全性检查BAR足够大以容纳驱动程序期望的寄存器。BAR小于章节期望的设备要么是错误识别的(错误的设备在ID对后),要么是驱动程序不支持的变体。在任何一种情况下,附加中失败的健全性检查比运行时损坏的访问更好。

`rman_get_start`主要用于诊断日志记录。BAR的物理地址不是驱动程序直接解引用的东西(内核的映射是标签和句柄包装的东西),但打印它有助于调试时将`pciconf -lv`输出连接到驱动程序的视图。

### 释放BAR

与`bus_alloc_resource_any`对应的是`bus_release_resource(9)`。签名是:

```c
int bus_release_resource(device_t dev, int type, int rid, struct resource *res);
```

`dev`、`type`和`rid`匹配分配调用;`res`是分配返回的句柄。成功时,函数返回0;失败时返回errno。失败很少见,因为资源刚由此驱动程序分配,但防御性驱动程序检查返回值并在失败时记录日志。

驱动程序应始终释放它分配的每个资源,按分配的相反顺序。第18章的驱动程序在第2阶段分配一个BAR;它将在分离中释放那个BAR。后续阶段,在第19到21章引入中断和DMA后,将分配更多。

### 附加中的部分失败

关于附加的一个微妙点。如果驱动程序成功声明了BAR但随后在后续步骤失败(例如,设备的预期`DEVICE_ID`寄存器不匹配),驱动程序必须在返回错误前释放BAR。忘记释放是资源泄漏:内核的资源管理器仍然认为BAR已由此驱动程序分配,即使驱动程序已返回。下一次附加尝试将失败。

习语是熟悉的基于goto的清理模式:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int rid, error;

	sc->dev = dev;
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail;
	}

	error = myfirst_hw_attach_pci(sc);
	if (error != 0)
		goto fail_release;

	/* ... */
	return (0);

fail_release:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail:
	return (error);
}
```

`goto`级联是一个习语,不是代码异味。它将清理代码保持在一个地方,并使分配和释放配对对称。该模式在第15章引入用于互斥锁和callout清理;这里它扩展到资源清理。第18章的最终附加使用更长版本的级联来处理softc初始化、BAR分配、硬件层附加和cdev创建作为四个分阶段的分配。

### Softc中存储什么

第18章向softc添加了几个字段。它们在`myfirst.h`中声明(主驱动程序头文件,不是`myfirst_pci.h`,因为softc跨所有层共享)。

```c
struct myfirst_softc {
	device_t dev;
	/* ... 第10到17章字段 ... */

	/* 第18章PCI字段。 */
	struct resource	*bar_res;
	int		 bar_rid;
	bool		 pci_attached;
};
```

`bar_res`是声明BAR的句柄。`bar_rid`是用于分配它的资源ID(存储以便分离可以将正确的值传递给`bus_release_resource`)。`pci_attached`是一个标志,后续代码用它来区分真实PCI附加路径和模拟附加路径。

一个BAR对第18章驱动程序就足够了。更复杂设备的驱动程序会有`bar0_res`、`bar0_rid`、`bar1_res`、`bar1_rid`等,每对匹配一个BAR。virtio-rnd设备只有一个BAR,所以驱动程序只有一对。

### 第2阶段附加

将分配放入第2阶段的附加例程:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error = 0;

	sc->dev = dev;

	/* 将BAR0分配为内存资源。 */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		return (ENXIO);
	}

	device_printf(dev, "BAR0 allocated: %#jx bytes at %#jx\n",
	    (uintmax_t)rman_get_size(sc->bar_res),
	    (uintmax_t)rman_get_start(sc->bar_res));

	sc->pci_attached = true;
	return (error);
}
```

一个成功的第2阶段附加打印一行类似:

```text
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
```

大小和地址取决于客户机的布局。重要的部分是分配成功,大小是驱动程序期望的(virtio-rnd设备暴露至少32字节的寄存器),分离路径释放资源。

### 第2阶段分离

第2阶段分离需要释放在附加中分配的内容:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}
	sc->pci_attached = false;
	device_printf(dev, "detaching\n");
	return (0);
}
```

`if`守卫是防御性的:原则上,当分离在成功附加后调用时,`sc->bar_res`非NULL,但添加检查成本很低,并使分离对将来重构中可能出现的部分失败情况具有鲁棒性。释放后将`bar_res`设置为NULL可防止如果稍后再次调用分离时的双重释放。

### 第2阶段尚未做的事

在第2阶段结束时,驱动程序分配了BAR但没对它做任何事情。标签和句柄可用但尚未连接到第16章访问器。第17章的模拟仍在运行,但它针对`malloc(9)`分配的寄存器块运行,而不是针对真实BAR。

第4节填补这个差距。它从第2阶段获取标签和句柄并将它们交给`myfirst_hw_attach`,以便`CSR_READ_4`和`CSR_WRITE_4`在真实硅上操作。第4节之后,第17章模拟变成运行时选项而不是唯一后端。

### 验证第2阶段

在移动到第4节之前,确认第2阶段端到端工作。

```sh
# 在bhyve客户机上:
sudo kldunload virtio_random  # 可能未加载
sudo kldload ./myfirst.ko
sudo dmesg | grep myfirst | tail -5
```

输出应该看起来像:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
```

`devinfo -v | grep -A 2 myfirst0`应该显示资源声明:

```text
myfirst0
    pnpinfo vendor=0x1af4 device=0x1005 ...
    resources:
        memory: 0xc1000000-0xc100001f
```

`devinfo -v`打印的内存范围与驱动程序打印的范围匹配。这确认分配成功且内核看到BAR已由`myfirst`声明。

卸载并验证清理:

```sh
sudo kldunload myfirst
sudo devinfo -v | grep myfirst  # 应该什么都不返回
```

没有残留设备,没有泄漏资源。第2阶段完成。

### BAR分配中的常见错误

典型陷阱的简短列表。

**将`0`作为BAR 0的rid传递。** rid是`PCIR_BAR(0)` = `0x10`,不是`0`。传递`0`请求偏移0的资源,即`PCIR_VENDOR`字段;分配失败或产生意外结果。始终使用`PCIR_BAR(index)`。

**忘记`RF_ACTIVE`。** 没有此标志,`bus_alloc_resource_any`分配但不激活。此时从标签和句柄读取是未定义行为。症状通常是页面错误或垃圾值。修复是传递`RF_ACTIVE`。

**使用错误的资源类型。** 对内存BAR传递`SYS_RES_IOPORT`产生立即分配失败。对I/O端口BAR传递`SYS_RES_MEMORY`也产生同样结果。类型必须匹配BAR的实际类型。如果驱动程序事先不知道(支持内存和I/O两种变体的通用驱动程序),它从配置空间读取`PCIR_BAR(index)`并检查低位。

**部分失败时未释放。** 一个常见的初学者错误:附加声明BAR,后续步骤失败,函数返回错误,BAR从未释放。资源泄漏。下一次附加尝试失败,因为BAR仍被声明。

**在访问器层完成之前释放BAR。** 反向错误:分离过早释放BAR,在可能仍在读取它的callout或任务排空之前。症状是`kldunload`后不久callout内的页面错误。修复是在释放之前排空所有可能访问BAR的内容。

**混淆`rman_get_size`与`rman_get_end`。** `rman_get_size(res)`返回字节数。`rman_get_end(res)`返回最后一个字节的地址(起始加大小减一)。对BAR大小的健全性检查使用`rman_get_size`;对诊断打印使用`rman_get_start`和`rman_get_end`。

**假设BAR按任何特定顺序排列。** 驱动程序必须显式命名它想要的BAR(通过传递`PCIR_BAR(n)`)。一些设备将其主BAR放在索引0;一些放在索引2。数据手册(或特定设备的`pciconf -lv`输出)说明它在哪里。假设BAR 0而不验证是一个常见错误。

### 关于64位BAR的说明

第18章使用的virtio-rnd设备有一个32位BAR,所以这里显示的分配无需特殊处理即可工作。对于有64位BAR的设备,有两个重要细节:

首先,BAR在配置空间BAR表中占用两个连续的槽。BAR 0(在偏移`0x10`)持有低32位;BAR 1(在偏移`0x14`)持有高32位。按简单整数增量遍历BAR表的驱动程序会错误地将BAR 1视为单独的BAR。正确的遍历读取每个BAR的类型位,如果当前槽是64位BAR则跳过下一个槽。

其次,传递给`bus_alloc_resource_any`的`rid`是较低槽的偏移。内核识别64位类型并将这对视为单个资源。驱动程序不需要为64位BAR分配两个资源;一个带有`rid = PCIR_BAR(0)`的分配处理两个槽。

对于第18章的驱动程序,这是学术性的;目标设备有32位BAR。但稍后处理有64位BAR设备的读者将需要这些细节。`/usr/src/sys/dev/pci/pcireg.h`定义了`PCIM_BAR_MEM_TYPE`、`PCIM_BAR_MEM_32`和`PCIM_BAR_MEM_64`来帮助BAR检查。

### 可预取与非可预取BAR

一个相关细节。如果BAR的第3位被设置,则它是可预取的。可预取意味着"从此范围的读取没有副作用,因此CPU可以像普通RAM一样缓存、预取和合并访问"。非可预取意味着"读取有副作用,因此每次访问必须命中设备;CPU绝不能缓存、预取或合并"。

设备寄存器几乎总是非可预取的。从状态寄存器读取可能清除标志;预取的读取将是一个灾难性的错误。设备内存(显卡上的帧缓冲区,或NIC上的环形缓冲区)通常是可预取的。

驱动程序不直接控制预取属性;BAR声明它是什么,内核相应地设置映射。驱动程序的工作是正确使用`bus_space_read_*`和`bus_space_write_*`。`bus_space`层处理排序和缓存细节。试图聪明地绕过`bus_space`并直接解引用指针的驱动程序可能意外获得非可预取BAR上的缓存映射,产生一个在理想条件下工作但在负载下神秘失败的驱动程序。

第16章一般性地论证了`bus_space`;第18章第3节确认该论证扩展到真实PCI设备。没有捷径。

### 总结第3节

BAR是设备暴露寄存器的地址范围。固件在启动时分配BAR地址;内核在PCI枚举期间读取它们;驱动程序在附加时通过带有正确类型、`rid`和标志的`bus_alloc_resource_any(9)`声明它们。返回的`struct resource *`携带一个`bus_space_tag_t`和`bus_space_handle_t`,`rman_get_bustag(9)`和`rman_get_bushandle(9)`提取它们。分离必须按相反顺序释放每个分配的资源。

第2阶段驱动程序分配BAR 0但尚未使用它。第4节将标签和句柄连接到第16章访问器层,因此`CSR_READ_4`和`CSR_WRITE_4`最终在真实BAR而不是`malloc(9)`块上操作。



## 第4节:通过bus_space(9)访问设备寄存器

第3节以驱动程序手中的标签和句柄结束。标签和句柄指向真实的BAR;第16章访问器期望的正是那对。第4节建立连接。它教授如何将PCI分配的标签和句柄交给硬件层,确认第16章`CSR_*`宏在不修改的情况下对真实PCI BAR工作,通过`bus_space_read_4(9)`读取驱动程序的第一个真实寄存器,并讨论第16章引入且PCI路径将重用的访问模式(`bus_space_read_multi`、`bus_space_read_region`、屏障)。

第4节的主题是连续性。读者已经编写了两章的寄存器访问代码。第18章不改变那个代码。改变的是标签和句柄的来源。访问器完全是访问器;包装宏完全是包装宏;锁定规则完全是锁定规则。这是`bus_space(9)`抽象的回报。驱动程序的上层不知道也不应该知道寄存器块的来源已改变。

### 重温第16章访问器

简短提醒。`myfirst_hw.c`定义了驱动程序其余部分调用的三个公共函数:

- `myfirst_reg_read(sc, off)` 从给定偏移的寄存器返回一个32位值。
- `myfirst_reg_write(sc, off, val)` 将一个32位值写入给定偏移的寄存器。
- `myfirst_reg_update(sc, off, clear, set)` 执行读-修改-写:读取、清除给定比特、设置给定比特、写入。

所有三个都由`myfirst_hw.h`中定义的`CSR_*`宏包装:

- `CSR_READ_4(sc, off)` 扩展为`myfirst_reg_read(sc, off)`。
- `CSR_WRITE_4(sc, off, val)` 扩展为`myfirst_reg_write(sc, off, val)`。
- `CSR_UPDATE_4(sc, off, clear, set)` 扩展为`myfirst_reg_update(sc, off, clear, set)`。

访问器通过`struct myfirst_hw`中的两个字段到达`bus_space`:

- 类型为`bus_space_tag_t`的`hw->regs_tag`
- 类型为`bus_space_handle_t`的`hw->regs_handle`

`myfirst_reg_read`内部的实际调用是:

```c
value = bus_space_read_4(hw->regs_tag, hw->regs_handle, offset);
```

而`myfirst_reg_write`内部:

```c
bus_space_write_4(hw->regs_tag, hw->regs_handle, offset, value);
```

这些行不知道PCI。它们不知道`malloc`。它们不知道`hw->regs_tag`是来自第16章中的模拟pmap设置还是来自第18章中的`rman_get_bustag(9)`调用。它们的契约不变。

### 标签和句柄的两个来源

第16章使用了一个技巧,在x86上从`malloc(9)`分配产生标签和句柄。技巧很简单:x86的`bus_space`实现使用`x86_bus_space_mem`作为内存映射访问的标签,句柄只是一个虚拟地址。一个`malloc`分配的缓冲区有一个虚拟地址,所以将缓冲区指针转换为`bus_space_handle_t`产生一个可用的句柄。这个技巧是x86特有的;在其他架构上,模拟块需要不同的方法。

第18章使用正确的路由:`bus_alloc_resource_any(9)`将BAR作为资源分配,`rman_get_bustag(9)`和`rman_get_bushandle(9)`提取内核设置的标签和句柄。驱动程序看不到物理地址;它看不到虚拟映射;它看到内核的平台代码正确设置的不透明标签和句柄。访问器使用它们,寄存器读取命中真实设备。

这是PCI集成的基本形状。标签和句柄的两个不同来源。使用它们的一组访问器。驱动程序在附加时选择哪个来源是活动的,访问器不需要知道选择了哪个。

### 扩展myfirst_hw_attach

第16章的`myfirst_hw_attach`分配一个`malloc(9)`缓冲区并合成标签和句柄。第18章需要第二个代码路径,接受现有的标签和句柄(来自PCI BAR)并直接存储它们。最简单的方法是重命名第16章版本并为PCI路径引入新版本。

新头文件,为第18章调整:

```c
/* 第16章行为:分配一个malloc支持的寄存器块。 */
int myfirst_hw_attach_sim(struct myfirst_softc *sc);

/* 第18章行为:使用已分配的资源。 */
int myfirst_hw_attach_pci(struct myfirst_softc *sc,
    struct resource *bar, bus_size_t bar_size);

/* 共享拆解;对任一后端安全。 */
void myfirst_hw_detach(struct myfirst_softc *sc);
```

PCI路径附加直接存储标签和句柄:

```c
int
myfirst_hw_attach_pci(struct myfirst_softc *sc, struct resource *bar,
    bus_size_t bar_size)
{
	struct myfirst_hw *hw;

	if (bar_size < MYFIRST_REG_SIZE) {
		device_printf(sc->dev,
		    "BAR is too small: %ju bytes, need at least %u\n",
		    (uintmax_t)bar_size, (unsigned)MYFIRST_REG_SIZE);
		return (ENXIO);
	}

	hw = malloc(sizeof(*hw), M_MYFIRST, M_WAITOK | M_ZERO);

	hw->regs_buf = NULL;			/* 无malloc块 */
	hw->regs_size = (size_t)bar_size;
	hw->regs_tag = rman_get_bustag(bar);
	hw->regs_handle = rman_get_bushandle(bar);
	hw->access_log_enabled = true;
	hw->access_log_head = 0;

	sc->hw = hw;

	device_printf(sc->dev,
	    "hardware layer attached to BAR: %zu bytes "
	    "(tag=%p handle=%p)\n",
	    hw->regs_size, (void *)hw->regs_tag,
	    (void *)hw->regs_handle);
	return (0);
}
```

几件事值得注意。`hw->regs_buf`是NULL,因为这次没有支持寄存器的`malloc`分配;BAR的内核映射是标签和句柄指向的东西。`hw->regs_size`是BAR的大小,已对驱动程序期望的最小大小进行了健全性检查。标签和句柄来自PCI附加路径分配的`struct resource *`。`myfirst_hw`中的其他一切不变。

共享分离是两个后端汇聚的地方:

```c
void
myfirst_hw_detach(struct myfirst_softc *sc)
{
	struct myfirst_hw *hw;

	if (sc->hw == NULL)
		return;

	hw = sc->hw;
	sc->hw = NULL;

	/*
	 * 只有在模拟附加产生了缓冲区时才释放模拟支持缓冲区。
	 * PCI路径将regs_buf设置为NULL,并将regs_size保留为BAR大小;
	 * BAR本身由PCI层释放(参见myfirst_pci_detach)。
	 */
	if (hw->regs_buf != NULL) {
		free(hw->regs_buf, M_MYFIRST);
		hw->regs_buf = NULL;
	}
	free(hw, M_MYFIRST);
}
```

分离是干净的。硬件层知道如何拆除第16章支持缓冲区或什么也不做,取决于它是如何附加的。BAR本身不是硬件层的责任;PCI层拥有它。这种分离是让第18章重用第16章代码而不重写第16章拆解的原因。

### 第一个真实寄存器读取

有了硬件层连接,驱动程序的第一个真实读取变得可能。在第17章,第一次读取是固定的`DEVICE_ID`寄存器,模拟预填充为`0x4D594649`(ASCII的"MYFI","我的第一个")。virtio-rnd设备不在那个偏移暴露`DEVICE_ID`寄存器;其BAR偏移0的配置空间是一个virtio特定的布局,以设备特性寄存器开始。

对于第18章教学路径,我们不需要说virtio-rnd协议。驱动程序读取BAR的前32位字并记录该值。该值是virtio-rnd设备第一个寄存器的任何内容(正在运行的virtio-rnd设备的virtio遗留设备配置的前32位对我们的驱动程序没有特别意义)。读取的要点是证明BAR访问工作。

执行此操作的代码(在`myfirst_pci_attach`中,BAR分配和硬件层附加之后):

```c
uint32_t first_word;

MYFIRST_LOCK(sc);
first_word = CSR_READ_4(sc, 0x00);
MYFIRST_UNLOCK(sc);

device_printf(dev, "first register read: 0x%08x\n", first_word);
```

锁加锁包装是第16章规则。读取在底层通过`bus_space_read_4`进行。输出行在附加时出现在`dmesg`中:

```text
myfirst0: first register read: 0x10010000
```

确切值取决于virtio-rnd设备的当前状态。一个看到任何值(而不是页面错误或在客户机崩溃的垃圾读取)的读者已经确认BAR分配工作、标签和句柄正确、访问器层正在真实硅上操作。

### 完整访问器系列

第16章驱动程序专门使用`bus_space_read_4`和`bus_space_write_4`,因为寄存器映射全是32位寄存器。真实PCI设备有时需要8位、16位或64位读取,有时需要一次读取或写入许多连续寄存器的块操作。`bus_space`系列覆盖所有这些:

- `bus_space_read_1`、`_2`、`_4`、`_8`:单字节、16位、32位或64位读取。
- `bus_space_write_1`、`_2`、`_4`、`_8`:单字节、16位、32位或64位写入。
- `bus_space_read_multi_*`:从同一寄存器偏移读取多个值(对FIFO读取有用)。
- `bus_space_write_multi_*`:向同一寄存器偏移写入多个值。
- `bus_space_read_region_*`:将一系列寄存器读取到内存缓冲区。
- `bus_space_write_region_*`:将内存缓冲区写入一系列寄存器。
- `bus_space_set_multi_*`:向同一寄存器多次写入同一值。
- `bus_space_set_region_*`:向一系列寄存器写入同一值。
- `bus_space_barrier`:在访问之间强制排序。

每个变体有宽度后缀作为一个单独条目。系列是对称和可预测的,一旦你见过它。

对于第18章的驱动程序,只需要`_4`。寄存器映射全程是32位的。如果后续驱动程序使用有16位寄存器的设备,读者只需将`_4`改为`_2`。如果需要,`CSR_*`宏可以扩展以覆盖多种宽度:

```c
#define CSR_READ_1(sc, off)       myfirst_reg_read_1((sc), (off))
#define CSR_READ_2(sc, off)       myfirst_reg_read_2((sc), (off))
#define CSR_WRITE_1(sc, off, val) myfirst_reg_write_1((sc), (off), (val))
#define CSR_WRITE_2(sc, off, val) myfirst_reg_write_2((sc), (off), (val))
```

在`myfirst_hw.c`中有相应的访问器函数。第18章不需要这些,但驱动程序作者应该知道它们存在。

### bus_space_read_multi vs bus_space_read_region

两个块操作值得再看,因为命名容易混淆。

`bus_space_read_multi_4(tag, handle, offset, buf, count)`从BAR的同一偏移读取`count`个32位值,全部存入`buf`。这是FIFO的正确操作:固定偏移的寄存器是FIFO的读端口,每次读取消耗一个条目。手动编写带有`bus_space_read_4`的类似循环也可以工作,但块版本通常更快且意图更清晰。

`bus_space_read_region_4(tag, handle, offset, buf, count)`从从`offset`开始的连续偏移读取`count`个32位值,存入`buf`。这是寄存器块的正确操作:驱动程序想将寄存器映射的一个范围快照到本地缓冲区。手动编写带有`bus_space_read_4`并递增偏移的循环也可以等效工作;块版本更清晰地表达意图。

区别在于BAR中的偏移是否前进。`_multi`保持偏移固定。`_region`让它前进。当你想的是`_region`时写成`_multi`,会从同一寄存器读取四次,而不是四个不同的寄存器。这是一个经典的混淆,避免它的方法是仔细阅读变体名称并记住"multi = 一个端口,多次访问"vs"region = 一个端口范围,每个一次访问"。

### 屏障何时重要

第16章引入`bus_space_barrier(9)`作为防止寄存器访问周围CPU重排序和编译器重排序的守卫。规则是:当驱动程序在两次访问之间有排序要求(例如,必须在读取之前发生的写入,或必须在另一次写入之前发生的写入),插入屏障。

对于第18章的驱动程序,访问器层已经在有副作用的写入周围包装了一个屏障(在`myfirst_reg_write_barrier`内部定义,第16章定义)。第17章模拟后端不需要额外的屏障,因为访问是对RAM。PCI后端可能需要比模拟更多的屏障,因为真实设备内存在某些架构上比RAM具有更弱的排序语义。

在x86上的常见情况:`bus_space_write_4`对内存映射BAR与对同一BAR的其他写入是强排序的;不需要显式屏障。在具有设备内存属性的arm64上,对同一BAR的写入也是排序的。在具有较弱内存模型的其他架构上,可能需要显式屏障。`bus_space(9)`手册页指定每个架构的默认排序保证;关心可移植性的驱动程序包含屏障,即使在x86不需要它们。

第18章的驱动程序为了教学目的存在于x86上,使用屏障的方式与第16章相同:在有副作用的CTRL写入之后(启动命令、触发状态改变、清除中断)。第17章`myfirst_reg_write_barrier`助手仍然是正确的入口点。

### 真实BAR上的访问日志

第16章的访问日志是一个环形缓冲区,记录每次寄存器访问及其时间戳、偏移、值和上下文标签。在模拟后端上,日志显示模式如"用户空间写入CTRL,然后callout读取STATUS"。在真实PCI后端上,日志显示相同的形状:驱动程序对BAR的任何访问都通过访问器,每个访问器写入一个条目。

这种连续性是一个安静但重要的特性。调试模拟问题的开发者可以查阅访问日志;调试真实硬件问题的开发者可以查阅相同的访问日志。技术是可迁移的。代码不改变。第7节的测试规则依赖于这种连续性。

关于访问日志和真实BAR的一个说明:如果设备有时在读取上有副作用(清除锁定的状态位、推进FIFO指针、触发已提交的写入完成),日志将记录读取值和驱动程序的后续动作。读取日志可以揭示否则不可见的时间问题。一个驱动程序快速连续两次读取STATUS,而第二次读取看到不同的比特,因为第一次读取的副作用干扰了,这种错误将在日志中清晰显示。对于第18章,这还无所谓;对于第19章及后续章节,这非常重要。

### 一个小小的微妙之处:CSR宏不知道PCI

值得指出。`CSR_*`宏不接受标签或句柄。它们只接受softc和偏移。其他一切都在访问器函数内部。

这意味着:当驱动程序从第17章模拟转换为第18章真实BAR时,驱动程序中没有一个调用点改变。`CSR_READ_4(sc, MYFIRST_REG_STATUS)`在过渡前后都做正确的事。`CSR_WRITE_4`和`CSR_UPDATE_4`也是如此。

回报是具体的。第17章的驱动程序可能有三十或四十个通过CSR宏读或写寄存器的调用点。如果这些宏接受标签和句柄,第18章需要更新每一个。因为它们只接受softc,第18章只需要改变硬件层的附加例程。第16章引入并第17章保持的在寄存器级别以上隐藏低层细节的纪律,在这里支付了最大的红利。

这是一个值得记住的模式。当你编写驱动程序时,定义一小部分访问器函数,在寄存器级别以上隐藏一切:标签、句柄、锁、日志、屏障。向驱动程序其余部分只暴露softc和偏移。使用访问器的代码然后不在乎寄存器是模拟的、真实PCI、真实USB、真实I2C还是真实其他任何东西。抽象在广泛的传输范围内保持。本书第七部分在讨论为可移植性重构驱动程序时将回到这个主题;第18章是读者第一次看到支付的红利。

### 第2阶段到第3阶段:连接在一起

第2阶段附加分配了BAR但没有将其交给硬件层。第3阶段附加两者都做。相关代码是完整附加:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	error = myfirst_init_softc(sc);	/* 第10-15章: 锁、softc字段 */
	if (error != 0)
		return (error);

	/* 分配BAR0。 */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail_softc;
	}

	/* 将BAR交给硬件层。 */
	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release;

	/* 从BAR读取一个诊断字。 */
	MYFIRST_LOCK(sc);
	sc->bar_first_word = CSR_READ_4(sc, 0x00);
	MYFIRST_UNLOCK(sc);
	device_printf(dev, "BAR[0x00] = 0x%08x\n", sc->bar_first_word);

	sc->pci_attached = true;
	return (0);

fail_release:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}
```

以及匹配的分离:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	sc->pci_attached = false;
	myfirst_hw_detach(sc);
	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}
	myfirst_deinit_softc(sc);
	device_printf(dev, "detaching\n");
	return (0);
}
```

附加序列是严格的:初始化softc(锁、字段),分配BAR,对BAR附加硬件层,执行附加需要的任何寄存器读取,将驱动程序标记为已附加。分离按反向撤销每一步:标记为未附加,分离硬件层(释放其包装结构),释放BAR,反初始化softc。

第5节将用额外的PCI特定步骤扩展附加:启用总线主控、遍历能力列表、读取子供应商特定的配置空间字段。附加的形状保持不变;中间增长。

### bus_space-on-PCI过渡中的常见错误

陷阱简短列表。

**将资源指针转换为使用`rman_get_bustag` / `rman_get_bushandle`。** 初学者有时写`hw->regs_tag = (bus_space_tag_t)bar`。这在大多数架构上不编译,在其余架构上编译成废话。使用访问器。

**混淆资源句柄与标签。** 标签是总线标识(内存或I/O);句柄是地址。`rman_get_bustag`返回标签;`rman_get_bushandle`返回句柄。交换它们产生立即崩溃或静默错误读取。仔细阅读函数名。

**PCI附加时未清零硬件状态。** 带有`M_ZERO`的`malloc(9)`清零结构。没有`M_ZERO`,`access_log_head`等字段以垃圾开始。环形缓冲区绕到任意索引,日志不可读。

**分离时未释放硬件状态。** 对称错误:PCI分离释放BAR但忘记调用`myfirst_hw_detach`。硬件包装结构泄漏。`vmstat -m`随时间显示泄漏。

**持有锁之前读取BAR。** 第16章规则是:每个CSR访问在`sc->mtx`下。附加时无锁读取违反每个后续访问假设的不变量。即使在单个CPU上碰巧工作,调试内核上的`WITNESS`也会抱怨。即使是附加时的读取也要持有锁。

**意外写入只读寄存器。** 在模拟后端上,写入只读寄存器只是更新`malloc`分配的缓冲区(模拟的读取侧忽略写入并返回固定值)。在真实PCI上,写入只读寄存器要么被静默忽略,要么导致某些设备特定的副作用。两种情况都不是驱动程序作者期望的。阅读数据手册并只写入可写寄存器。

**当驱动程序想的是`_region_4`时调用`bus_space_read_multi_4`。** 两个函数有相同的签名和非常不同的语义。使用`_multi`读取一个寄存器范围会用相同的值(固定偏移的当前值)重复`count`次填充缓冲区。使用`_region`读取一个范围会用连续的寄存器值填充缓冲区。错误在检查值之前是静默的。

### 总结第4节

第16章访问器层从模拟到真实PCI寄存器的过渡不变。唯一的变化是在`myfirst_hw_attach_pci`中,它用`rman_get_bustag(9)`和`rman_get_bushandle(9)`在PCI分配的资源上产生的标签和句柄替换`malloc(9)`支持缓冲区。`CSR_*`宏、访问日志、锁定规则、ticker任务和第16章和第17章代码库的每个其他部分继续不变工作。

驱动程序的第一个真实PCI寄存器读取在附加时发生。读取的值在virtio-rnd协议意义上没有意义;它是BAR映射活动且访问器正在读取真实硅的证明。第5节进一步进行附加序列:它引入`pci_enable_busmaster(9)`(为未来DMA使用)、用`pci_find_cap(9)`和`pci_find_extcap(9)`遍历PCI能力列表、解释驱动程序何时直接读取配置空间字段、展示第17章模拟如何在PCI路径上保持非活动状态以便其callout不会将任意值写入真实设备的寄存器。



## 第5节:驱动程序附加时初始化

第2节到第4节从无到有构建附加例程,直到一个完全连接的PCI附加,它声明BAR、将其交给硬件层,并执行其第一个寄存器访问。第5节完成附加故事。它介绍了PCI驱动程序在附加时通常执行的一小部分配置空间操作,解释了每个何时以及为何需要,遍历PCI能力列表以发现设备的可选特性,展示了第17章模拟如何在PCI路径上保持非活动状态(以便其callout不会写入真实设备),并创建了第10章驱动程序已经暴露的相同cdev。

到第5节结束时,驱动程序作为一个PCI驱动程序完成。它附加到真实设备,将设备带到驱动程序可以使用它的状态,暴露与第10章到第17章迭代相同的用户空间接口,并准备好在第19章用真实中断处理程序扩展。

### 附加时检查清单

一个工作的PCI驱动程序的附加例程通常按大致此顺序执行:

1. **初始化softc。** 设置`sc->dev`、初始化锁、初始化条件和callout、清除计数器。
2. **分配资源。** 声明BAR(或BAR)、声明IRQ资源(在第19章)、以及任何其他总线资源。
3. **激活设备特性。** 如果驱动程序将使用DMA,启用总线主控。设置设备需要的配置空间比特。
4. **遍历能力。** 找到驱动程序支持的PCI能力并记录它们的寄存器偏移。
5. **附加硬件层。** 将BAR交给访问器层。
6. **初始化设备。** 执行设备特定的启动序列:复位、特性协商、队列设置。这是设备的数据手册所说的使设备可用所需的任何内容。
7. **注册用户空间接口。** 创建cdev、网络接口或驱动程序暴露的任何内容。
8. **启用中断。** 注册中断处理程序(第19章)并在设备的INTR_MASK寄存器中取消屏蔽中断。
9. **将驱动程序标记为已附加。** 设置一个其他代码可以检查的标志。

不是每个驱动程序都执行每一步。被动设备的驱动程序(无DMA、无中断,只是读取和写入)跳过总线主控和中断设置。不需要用户空间接口的设备的驱动程序跳过cdev创建。但顺序是稳定的:资源第一,设备特性第二,硬件层第三,设备启动第四,用户空间第五,中断最后。不按顺序执行会产生竞争条件,中断在驱动程序准备好处理它之前到达,或用户空间访问到达部分初始化的驱动程序。

第18章的驱动程序执行第1步到第7步。第8步是第19章。第9步是第10章已经处理的细节。

### pci_enable_busmaster和命令寄存器

PCI命令寄存器位于配置空间偏移`PCIR_COMMAND`(`0x04`),作为16位字段。该寄存器中对大多数驱动程序重要的三位是:

- `PCIM_CMD_MEMEN`(`0x0002`):启用设备的内存BAR。必须在驱动程序可以读取或写入任何内存BAR之前设置。
- `PCIM_CMD_PORTEN`(`0x0001`):启用设备的I/O端口BAR。必须在驱动程序可以读取或写入任何I/O端口BAR之前设置。
- `PCIM_CMD_BUSMASTEREN`(`0x0004`):启用设备作为总线主控发起DMA。必须在设备可以自己读取或写入RAM之前设置。

PCI总线驱动程序在激活BAR时自动设置`MEMEN`和`PORTEN`。一个已成功调用`bus_alloc_resource_any`并带有`RF_ACTIVE`且已接收非NULL结果的驱动程序不需要手动设置这些位;总线驱动程序已经做了。

`BUSMASTEREN`不同。总线驱动程序不会自动设置它,因为不是每个驱动程序都需要DMA。将程序其设备读取或写入系统RAM的驱动程序(NIC、存储控制器、GPU)必须显式设置`BUSMASTEREN`。只读和写设备自己的BAR(无DMA)的驱动程序不需要设置它。

助手`pci_enable_busmaster(dev)`设置该位。其反向,`pci_disable_busmaster(dev)`,清除它。第18章的驱动程序不使用DMA,不调用`pci_enable_busmaster`。第20章和第21章将会。

关于直接读取命令寄存器的说明。驱动程序总是可以用`pci_read_config(dev, PCIR_COMMAND, 2)`读取命令寄存器并检查单个位。对于大多数驱动程序这是不必要的;内核已经配置了相关位。出于诊断目的(一个想在附加时记录设备命令寄存器状态的驱动程序),这很好。

### 读取配置空间字段

大多数驱动程序需要读取至少少量通用访问器不覆盖的配置空间字段。示例包括:

- 在供应商特定偏移处的特定固件版本号。
- PCIe能力结构内部的PCIe链路状态字段。
- 供应商特定能力数据。
- 多功能设备的子系统特定识别字段。

原语是`pci_read_config(dev, offset, width)`。偏移是配置空间的字节偏移。宽度是1、2或4字节。返回值是`uint32_t`(较窄的宽度右对齐)。

一个具体示例。PCI类代码占用配置空间字节`0x09`到`0x0b`:

- 字节`0x09`:编程接口(progIF)。
- 字节`0x0a`:子类。
- 字节`0x0b`:类。

一次作为32位值读取所有三个给出高三个字节中的类、子类和progIF(低字节是修订ID)。缓存访问器`pci_get_class`、`pci_get_subclass`、`pci_get_progif`和`pci_get_revid`各自提取单个字段;驱动程序很少需要手动做这件事。

对于供应商特定字段,驱动程序必须手动读取。模式是:

```c
uint32_t fw_rev = pci_read_config(dev, 0x48, 4);
device_printf(dev, "firmware revision 0x%08x\n", fw_rev);
```

偏移`0x48`是占位符;真实偏移是设备数据手册指定的任何内容。从设备不实现的偏移读取返回`0xffffffff`或设备特定的默认值;`0xffffffff`是PCI上经典的"无设备"值。

### pci_write_config和副作用契约

对应的是`pci_write_config(dev, offset, value, width)`。它将`value`写入`offset`处的配置空间字段,截断到`width`字节。

关于配置空间写入的一个关键点:某些字段是只读的。写入只读字段要么被静默忽略(常见情况),要么导致设备特定的错误。驱动程序必须在发出写入前从PCI规范或设备数据手册知道哪些字段是可写的。

第二个关键点:某些字段在读或写上有副作用。命令寄存器,例如,有副作用:设置`MEMEN`启用内存BAR;清除它禁用它们。读取命令寄存器没有副作用。驱动程序必须理解它触摸的每个字段的语义。

助手`pci_enable_busmaster`在底层使用`pci_write_config`设置一个比特。当不存在特定助手时,驱动程序总是可以直接使用`pci_read_config`和`pci_write_config`来操作字段。

### pci_find_cap:遍历能力列表

PCI设备通过能力链接列表宣传可选特性。每个能力是配置空间中的小块,从一个字节的能力ID和一个字节的"下一个指针"开始。列表从存储在设备`PCIR_CAP_PTR`字段中的偏移(配置空间偏移`0x34`)开始,并跟随`next`指针,直到`0`终止链。

驱动程序可能找到的标准能力包括:

- `PCIY_PMG`(`0x01`):电源管理。
- `PCIY_MSI`(`0x05`):消息信号中断。
- `PCIY_EXPRESS`(`0x10`):PCI Express。任何PCIe设备都有这个。
- `PCIY_MSIX`(`0x11`):MSI-X。比MSI更丰富的中断路由机制。
- `PCIY_VENDOR`(`0x09`):供应商特定能力。

驱动程序通过`pci_find_cap(9)`遍历列表:

```c
int capreg;

if (pci_find_cap(dev, PCIY_EXPRESS, &capreg) == 0) {
	device_printf(dev, "PCIe capability at offset 0x%x\n", capreg);
}
if (pci_find_cap(dev, PCIY_MSI, &capreg) == 0) {
	device_printf(dev, "MSI capability at offset 0x%x\n", capreg);
}
if (pci_find_cap(dev, PCIY_MSIX, &capreg) == 0) {
	device_printf(dev, "MSI-X capability at offset 0x%x\n", capreg);
}
```

函数成功时返回0并在`*capreg`中存储能力的偏移。失败时(能力不存在)返回`ENOENT`且不修改`*capreg`。

返回的偏移是能力第一个寄存器在配置空间中的字节偏移。该寄存器通常是能力ID本身;驱动程序可以通过回读并检查预期ID来确认。能力中的后续字节定义了特性特定字段。

第18章的驱动程序在附加时遍历能力列表并记录存在哪些能力。列表给驱动程序作者一种设备提供什么的感觉。MSI和MSI-X与第20章相关;电源管理与第22章相关。在第18章,驱动程序只是记录存在和偏移。

### pci_find_extcap:PCIe扩展能力

PCIe引入了第二个列表,称为扩展能力,位于配置空间偏移`0x100`以上。这是现代特性如高级错误报告、虚拟通道、访问控制服务和SR-IOV所在的地方。列表结构与遗留能力列表类似,但使用16位ID和4字节偏移。

遍历器是`pci_find_extcap(9)`。签名与`pci_find_cap`相同:

```c
int capreg;

if (pci_find_extcap(dev, PCIZ_AER, &capreg) == 0) {
	device_printf(dev, "AER capability at offset 0x%x\n", capreg);
}
```

扩展能力ID在`/usr/src/sys/dev/pci/pcireg.h`中定义在以`PCIZ_`开头的名称下(相对于标准能力的`PCIY_`)。前缀是助记符:`PCIY`代表"PCI capabilitY"(较旧的列表),`PCIZ`代表"PCI eXtended"(Z在Y之后)。

第18章的驱动程序不订阅AER或任何其他扩展能力。它在附加时遍历扩展列表并记录发现的内容,就像它遍历标准列表一样。这有两个目的:它给读者一个PCIe能力在野外的样子的视图,并且它练习`pci_find_extcap`以便读者见过两个遍历器。

### PCIe AER:简介

高级错误报告(AER)是一个PCIe扩展能力,让系统检测和报告某些类别的PCI级错误:不可纠正的事务错误、可纠正的错误、畸形TLP、完成超时等。能力是可选的;不是每个PCIe设备都实现它。

在FreeBSD上,PCI总线驱动程序(`pci(4)`,在`/usr/src/sys/dev/pci/pci.c`中实现)在探测期间遍历每个设备的扩展能力列表,在存在时定位AER能力,并将其用于系统级错误记录。驱动程序通常不注册自己的AER回调;总线集中处理AER并将可纠正和不可纠正的错误记录到内核消息缓冲区。想要自定义处理的驱动程序通过`pci_find_extcap(dev, PCIZ_AER, &offset)`返回的偏移读取AER状态寄存器,并根据`/usr/src/sys/dev/pci/pcireg.h`中的比特布局解码它们。

对于第18章的驱动程序,提到AER是为了完成PCIe能力图景。驱动程序不订阅AER事件。第20章在其"通过MSI-X向量的PCIe AER恢复"讨论中再次拿起这个主题,以解释驱动程序拥有的AER处理程序将在何处挂钩到中断章节构建的MSI-X管道中。完整的端到端AER恢复实现超出了本书范围;想要深入了解的读者可以研究`/usr/src/sys/dev/pci/pci.c`中的`pci_add_child_clear_aer`和`pcie_apei_error`,以及`/usr/src/sys/dev/pci/pcireg.h`中的`PCIR_AER_*`和`PCIM_AER_*`比特布局。

关于命名的简短旁注:"AER"在大多数FreeBSD对话中逐字母发音("ay-ee-ar")。pcireg头中的能力ID是`PCIZ_AER` = `0x0001`。

### 将模拟与真实PCI后端组合

第17章的驱动程序作为独立模块附加(`kldload myfirst`触发附加)。第18章的驱动程序附加到PCI设备。两个附加路径都需要设置相同的上层状态(sfc、cdev、一些每实例字段)。问题是如何组合它们。

第18章的驱动程序通过单个编译时开关解决此问题,选择哪些附加路径是活动的,并通过在绑定到真实PCI设备时**不**运行第17章模拟的callout。逻辑很简单:

- 如果在构建时定义了`MYFIRST_SIMULATION_ONLY`,驱动程序完全省略`DRIVER_MODULE`。没有PCI附加;模块的行为完全像第17章驱动程序,`kldload`通过第17章模块事件处理程序生成一个模拟实例。
- 如果未定义`MYFIRST_SIMULATION_ONLY`(第18章的默认值),驱动程序声明`DRIVER_MODULE(myfirst, pci, ...)`。模块是可加载的。当存在匹配的PCI设备时,`myfirst_pci_attach`运行。第17章模拟callout未在PCI路径上启动;访问器层指向真实BAR,模拟后端保持空闲。想要模拟的读者显式重新激活它,通过sysctl或通过`MYFIRST_SIMULATION_ONLY`构建。

`myfirst_pci.c`中的编译时守卫很短:

```c
#ifndef MYFIRST_SIMULATION_ONLY
DRIVER_MODULE(myfirst, pci, myfirst_pci_driver, NULL, NULL);
MODULE_DEPEND(myfirst, pci, 1, 1, 1);
#endif
```

而`myfirst_pci_attach`故意跳过`myfirst_sim_enable(sc)`。第17章传感器callout、命令callout和故障注入机制保持休眠。它们存在于代码中但从不调度,当后端是真实PCI BAR时;这防止模拟`CTRL.GO`比特写入真实设备的寄存器。

在没有匹配PCI设备的主机上的读者仍然可以选择直接运行第17章模拟:用`MYFIRST_SIMULATION_ONLY=1`构建、`kldload`,驱动程序的行为完全像第17章结束时那样。两个构建共享每个文件;选择在编译时发生。

读者可能选择的替代方案:将驱动程序拆分为两个模块。`myfirst_core.ko`持有硬件层、模拟、cdev和锁。`myfirst_pci.ko`持有PCI附加。`myfirst_core.ko`总是可加载的并提供模拟。`myfirst_pci.ko`依赖`myfirst_core.ko`并在其上添加PCI支持。

这是真实FreeBSD驱动程序在芯片组有多个传输变体时使用的方法。`uart(4)`驱动程序有`uart.ko`作为核心和`uart_bus_pci.ko`作为PCI附加;`virtio(4)`有`virtio.ko`作为核心和`virtio_pci.ko`作为PCI传输。本书后面关于多传输驱动程序的章节回到这个模式。

对于第18章,更简单的方法(一个带编译时开关的模块)就足够了。想要练习拆分的读者可以在章节末尾作为挑战尝试。

### 为什么模拟Callout在真实PCI上保持静默

一个值得明确指出的说明。当第18章驱动程序附加到真实virtio-rnd设备时,BAR不持有第17章寄存器映射。偏移`0x00`是virtio遗留设备特性寄存器,不是`CTRL`。偏移`0x12`是virtio `device_status`寄存器,不是第17章的`INTR_STATUS`。让第17章传感器callout写入`SENSOR_CONFIG`(在第17章偏移`0x2c`)或让命令callout在`0x00`写入`CTRL`,会将任意字节插入virtio设备的寄存器。

在bhyve客户机上,这不是灾难性的(客户机是一次性的),但这是糟糕的纪律。正确的行为是:模拟callout只在访问器层由模拟缓冲区支持时运行。当访问器层由真实BAR支持时,模拟保持关闭。第18章的`myfirst_pci_attach`通过从不调用`myfirst_sim_enable`来强制执行这一点。cdev仍然工作,`CSR_READ_4`仍然读取真实BAR,驱动程序的其余部分正常运行。callout只是不触发。

这是一个小设计决策,有真实的后果:驱动程序可以安全地附加到真实PCI设备而不破坏设备的状态。稍后调整驱动程序到不同设备(第17章模拟确实匹配其寄存器映射)的读者可以用sysctl重新启用callout并观察它们驱动真实硅。对于virtio-rnd教学目标,callout保持休眠。

### 在PCI驱动程序中创建cdev

第10章驱动程序在模块加载时用`make_dev(9)`创建了一个cdev。在第18章PCI驱动程序中,`make_dev(9)`在附加时运行,每个PCI设备一次。cdev的名称包含单元号:`/dev/myfirst0`、`/dev/myfirst1`等。

代码很熟悉:

```c
sc->cdev = make_dev(&myfirst_cdevsw, device_get_unit(dev), UID_ROOT,
    GID_WHEEL, 0600, "myfirst%d", device_get_unit(dev));
if (sc->cdev == NULL) {
	error = ENXIO;
	goto fail_hw;
}
sc->cdev->si_drv1 = sc;
```

`device_get_unit(dev)`返回newbus分配的单元号。带有该单元号作为参数的`"myfirst%d"`产生每实例设备名称。`si_drv1`赋值让cdev的`open`、`close`、`read`、`write`和`ioctl`入口点从cdev恢复softc。

分离路径用`destroy_dev(9)`销毁cdev:

```c
if (sc->cdev != NULL) {
	destroy_dev(sc->cdev);
	sc->cdev = NULL;
}
```

此代码完全是第10章模式;没有任何新内容。在这里包含它的意义在于,它自然地适合PCI附加排序:softc、BAR、硬件层、cdev,以及(稍后第19章)中断。在分离中反向排序。完成。

### 完整的第3阶段附加

结合第5节的每一部分,第3阶段附加:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error, capreg;

	sc->dev = dev;
	sc->unit = device_get_unit(dev);
	error = myfirst_init_softc(sc);
	if (error != 0)
		return (error);

	/* 第1步:分配BAR0。 */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail_softc;
	}

	/* 第2步:遍历PCI能力(信息性)。 */
	if (pci_find_cap(dev, PCIY_EXPRESS, &capreg) == 0)
		device_printf(dev, "PCIe capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_MSI, &capreg) == 0)
		device_printf(dev, "MSI capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_MSIX, &capreg) == 0)
		device_printf(dev, "MSI-X capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_PMG, &capreg) == 0)
		device_printf(dev, "Power Management capability at 0x%x\n",
		    capreg);
	if (pci_find_extcap(dev, PCIZ_AER, &capreg) == 0)
		device_printf(dev, "PCIe AER extended capability at 0x%x\n",
		    capreg);

	/* 第3步:对BAR附加硬件层。 */
	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release;

	/* 第4步:创建cdev。 */
	sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT,
	    GID_WHEEL, 0600, "myfirst%d", sc->unit);
	if (sc->cdev == NULL) {
		error = ENXIO;
		goto fail_hw;
	}
	sc->cdev->si_drv1 = sc;

	/* 第5步:读取一个诊断字。 */
	MYFIRST_LOCK(sc);
	sc->bar_first_word = CSR_READ_4(sc, 0x00);
	MYFIRST_UNLOCK(sc);
	device_printf(dev, "BAR[0x00] = 0x%08x\n", sc->bar_first_word);

	sc->pci_attached = true;
	return (0);

fail_hw:
	myfirst_hw_detach(sc);
fail_release:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}
```

结构正是本节开头的附加时检查列表,带有使顺序显式的标签(`第1步`、`第2步`等)。`goto`级联干净地处理部分失败。每个失败标签撤销最近成功的步骤,链接到它之前的那个。

之前见过此模式的读者(在第15章的带多个原语的复杂附加中,或在任何分配多个资源的FreeBSD驱动程序中)会立即认出它。第一次见到的读者可能会受益于手动追踪每个步骤的假设失败并验证正确数量的清理发生。

### 验证第3阶段

第3阶段附加时的预期`dmesg`输出:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
myfirst0: PCIe capability at 0x0
myfirst0: MSI-X capability at 0x0
myfirst0: hardware layer attached to BAR: 32 bytes (tag=... handle=...)
myfirst0: BAR[0x00] = 0x10010000
```

(能力偏移对virtio遗留设备为0,因为bhyve模拟不暴露PCIe能力;用QEMU的virtio-rng-pci测试的读者可能看到非零偏移。)

看到所有四行(附加、能力遍历、硬件附加、BAR读取)的读者已确认第3阶段完成。

`ls /dev/myfirst*`应该显示`/dev/myfirst0`。打开该设备、写入一个字节并读取一个字节的用户空间程序应该看到第17章模拟路径在运作(命令-响应协议仍在表层运行,即使BAR现在是真实的;第17章和第18章尚不在数据路径级别交互,它们只共享访问器层)。

分离验证反向:

```text
myfirst0: detaching
```

`/dev/myfirst0`消失。BAR释放。softc释放。没有泄漏,没有警告,没有卡住状态。

### 总结第5节

附加时初始化是许多小步骤的组合。每个步骤分配或配置一件事。步骤按照从设备向外构建驱动程序状态的严格顺序:资源优先,然后特性,然后设备特定启动,然后用户空间接口,然后(第19章)中断。分离路径按反向撤销每一步。

第18章添加到此模式的PCI特定部分是`pci_enable_busmaster`(我们的驱动程序不需要,预留给第20和21章)、能力遍历器`pci_find_cap(9)`和`pci_find_extcap(9)`、通过`pci_read_config(9)`和`pci_write_config(9)`进行的配置空间读写,以及PCIe AER的简要介绍,读者将在后续章节中回到它。

第6节深入覆盖分离侧。大意熟悉,但细节(处理部分附加失败、拆除顺序、与可能仍在运行的callout和任务的交互)值得有自己的章节。

## 第6节：支持分离和资源清理

附加将驱动程序启动。分离将其关闭。这两条路径是镜像，但不是完全对称的镜像。分离有一些附加所没有的顾虑：其他代码可能仍在运行的可能性（callout、任务、文件描述符、中断处理程序）、当驱动程序有调用者尚未静默的工作时需要拒绝分离，以及避免在最后一次活动访问和释放softc之间发生释放后使用。第6节是关于正确处理这些顾虑的。

第6节的目标是一个严格、完整且易于审计的分离例程。它释放附加声明的每个资源。它排空可能仍在运行的每个callout和任务。它在释放硬件层之前销毁cdev。它在硬件层不再需要BAR之后释放BAR。并且它以本书读者可以一步步阅读和理解的方式完成所有这些。

### 核心规则：逆序

分离最重要的规则是逆序。附加采取的每一步，分离以相反的顺序撤销。如果附加分配了A，然后B，然后C，那么分离释放C，然后B，然后A。

这条规则听起来微不足道。在实践中，忘记它或稍微弄错顺序是新驱动程序中内核崩溃最常见的原因之一。典型症状：一个callout在分离期间触发，从softc字段读取，而该字段已经被释放。或者：当BAR被释放时cdev仍然存在，一个打开了cdev的用户空间进程触发读取，该读取解引用了一个未映射的地址。

第15章的分离模式是第18章的正确模型。附加从设备向外构建状态；分离从设备向内拆除状态。分离释放的任何东西都不能再被其他任何东西使用。

### 第18章分离顺序

分离顺序，与第3阶段的附加相匹配：

1. 将驱动程序标记为不再附加（`sc->pci_attached = false`）。
2. 取消任何用户空间访问路径：销毁cdev，以便没有新的`open`或`ioctl`可以开始，不接受新请求。
3. 排空可能正在运行的callout和任务（`myfirst_quiesce`）。
4. 如果附加了模拟后端则分离它（释放`sc->sim`）。在PCI路径上这是一个空操作，因为模拟没有被附加。
5. 分离硬件层（释放`sc->hw`；不释放BAR）。
6. 通过`bus_release_resource`释放BAR资源。
7. 拆除softc状态：销毁锁，销毁条件变量，释放任何分配的内存。
8. （第19章的添加，为完整性而提及）释放IRQ资源。

对于第18章，分离代码如下：

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	/* 如果有东西仍在使用设备则拒绝分离。 */
	if (myfirst_is_busy(sc))
		return (EBUSY);

	sc->pci_attached = false;

	/* 拆除cdev，以便没有新的用户空间访问开始。 */
	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	/* 排空callout和任务。无论模拟是否
	 * 曾在此实例上启用都是安全的。 */
	myfirst_quiesce(sc);

	/* 如果模拟后端被附加则释放它。PCI
	 * 路径使sc->sim == NULL，所以这是空操作。 */
	if (sc->sim != NULL)
		myfirst_sim_detach(sc);

	/* 分离硬件层（释放包装器结构）。 */
	myfirst_hw_detach(sc);

	/* 释放BAR。 */
	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}

	/* 拆除softc状态。 */
	myfirst_deinit_softc(sc);

	device_printf(dev, "detached\n");
	return (0);
}
```

代码比第2阶段的分离更长，因为每一步都是自己的关注点。结构易于阅读：每一行或块释放一件事，按附加的逆序。审计分离的读者可以根据附加检查每一步并确认对称性。

### myfirst_is_busy：何时拒绝分离

一个有打开的cdev、正在执行的命令或任何其他正在进行的工作的驱动程序不能安全地分离。从分离返回`EBUSY`告诉内核的模块加载器不要动该驱动程序。

第10到15章的驱动程序有一个简单的忙碌检查：cdev上是否有任何打开的文件描述符？第17章扩展了它以包括正在执行的模拟命令。第18章重用相同的检查：

```c
static bool
myfirst_is_busy(struct myfirst_softc *sc)
{
	bool busy;

	MYFIRST_LOCK(sc);
	busy = (sc->open_count > 0) || sc->command_in_flight;
	MYFIRST_UNLOCK(sc);
	return (busy);
}
```

检查在锁下进行，因为`open_count`和`command_in_flight`可以被其他代码路径修改（cdev的`open`和`close`入口点、第17章的命令callout）。没有锁，检查可能会看到不一致的视图，拒绝或允许分离的决定会与正在进行的打开或关闭竞争。确切的字段名来自第10章的softc（`open_count`）和第17章的添加（`command_in_flight`）；使用不同名称的softc的读者应在此处替换本地名称。

从分离返回`EBUSY`在`kldunload`上产生可见的错误：

```text
# kldunload myfirst
kldunload: can't unload file: Device busy
```

用户然后关闭打开的文件描述符，取消正在执行的命令，或做任何其他需要的事情来排空忙碌状态，然后重试。这是预期的行为；一个从不拒绝分离的驱动程序是一个可以从其用户下面被拆除的驱动程序。

### 静默Callout和任务

第17章的模拟每秒运行一个传感器callout；每个命令触发一个命令callout；偶尔触发一个忙碌恢复callout。第16章的硬件层通过任务队列运行一个定时器任务。在PCI后端上，模拟callout没有启用（如第5节所解释），所以如果它们从未运行，它们的`callout_drain`是一个安全的空操作。硬件层定时器任务仍然活动，必须被排空。

对于callout正确的原语是`callout_drain(9)`。它等待直到callout不在运行并阻止任何未来的触发。对于任务正确的原语是`taskqueue_drain(9)`。它等待直到任务完成运行并阻止任何进一步的入队。

第17章API公开了两个封装模拟callout生命周期的函数：`myfirst_sim_disable(sc)`停止调度新的触发（它需要持有`sc->mtx`），`myfirst_sim_detach(sc)`排空每个callout并释放模拟状态（它不能持有`sc->mtx`）。PCI驱动程序中的单个`myfirst_quiesce`辅助函数安全地组合它们：

```c
static void
myfirst_quiesce(struct myfirst_softc *sc)
{
	if (sc->sim != NULL) {
		MYFIRST_LOCK(sc);
		myfirst_sim_disable(sc);
		MYFIRST_UNLOCK(sc);
	}

	if (sc->tq != NULL && sc->hw != NULL)
		taskqueue_drain(sc->tq, &sc->hw->reg_ticker_task);
}
```

在PCI路径上`sc->sim`是NULL（模拟后端没有附加），所以第一个块完全被跳过。在仅模拟的构建中，如果模拟被附加，`myfirst_sim_disable`在锁下停止callout，随后的`myfirst_sim_detach`（稍后在分离序列中调用）在没有锁的情况下排空它们。

这种分割很重要，因为`callout_drain`必须在**没有**持有`sc->mtx`的情况下调用：callout体本身可能尝试获取互斥锁，持有它会导致死锁。第13章教授了这个规则；第18章通过将排空路由到`myfirst_sim_detach`来遵守它，后者不获取任何锁。

在`myfirst_quiesce`返回后，除了分离路径本身之外，没有其他东西在对softc运行。后续的拆除步骤可以安全地接触`sc->hw`和BAR。

### 在硬件层之后释放BAR

顺序很重要。`myfirst_hw_detach`在`bus_release_resource`之前调用，因为`myfirst_hw_detach`仍然需要tag和handle有效（例如，如果在硬件拆除期间有任何最后机会的读取；第18章版本不进行此类读取，但防御性代码保持顺序，以防后续扩展添加它们）。

在`myfirst_hw_detach`返回后，`sc->hw`是NULL。存储在（现已释放的）`myfirst_hw`结构中的tag和handle消失了。此时驱动程序中没有代码可以读取或写入BAR。然后可以安全地释放BAR。

如果顺序颠倒（先释放BAR，然后`myfirst_hw_detach`），硬件拆除代码将持有陈旧的tag和handle；任何访问都将是释放后使用。在x86上，这个bug可能是静默的；在具有更严格内存权限的架构上，访问将导致页面错误。

### 分离期间的失败

与附加不同，分离通常预期会成功。内核的卸载路径调用分离；如果分离返回非零值，卸载中止，但分离本身不应使资源处于不一致状态。惯例是分离返回0（成功）或`EBUSY`（因为驱动程序正在使用而拒绝卸载）。返回任何其他错误是不寻常的，通常表明驱动程序有bug。

如果资源释放失败（例如，`bus_release_resource`返回错误），驱动程序应该记录失败但继续分离。留下部分释放的状态比记录并继续更糟糕；内核会在关机时抱怨泄漏的资源，但驱动程序不会崩溃。第18章的驱动程序因此不检查`bus_release_resource`的返回值；释放要么成功，要么留下不可恢复的内核状态，驱动程序对此都无能为力。

### 分离 vs 模块卸载 vs 设备移除

三种不同的事件可以触发分离。

**模块卸载**（`kldunload myfirst`）：用户请求移除模块。内核的卸载路径对绑定到模块的每个设备调用分离，一次一个。如果每个分离返回0，模块被卸载。如果任何分离返回非零值，模块保持加载，卸载返回错误。

**用户设备移除**（`devctl detach myfirst0`）：用户请求从驱动程序分离特定设备，而不卸载模块。驱动程序的分离为该设备运行；模块保持加载，仍可以附加到其他设备。

**硬件设备移除**（热插拔，例如从支持热插拔的插槽中移除PCIe卡，或管理程序移除虚拟设备）：PCI总线检测到变化并在设备上调用分离。驱动程序的分离运行。如果设备稍后重新插入，驱动程序的探测和附加再次运行。

所有三条路径运行相同的`myfirst_pci_detach`函数。驱动程序不需要区分它们。代码相同，因为义务相同：释放附加分配的一切。

### 部分附加失败和分离路径

一个值得解释的微妙情况。如果附加中途失败并返回错误，内核（在现代newbus中）不会对部分附加的驱动程序调用分离。驱动程序自己的goto级联处理清理。

第5节的附加代码有一个goto级联，精确撤销成功的步骤。如果在硬件层附加后cdev创建失败，级联在返回之前释放硬件层和BAR。如果在BAR分配后硬件层附加失败，级联在返回之前释放BAR。每个失败标签撤销一步。

一个常见的初学者错误是编写跳过步骤的goto级联。例如：

```c
fail_hw:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
```

这跳过了`myfirst_hw_detach`步骤。如果硬件层附加成功但cdev创建失败，级联在没有拆除硬件层的情况下释放BAR，泄漏硬件层的包装器结构。正确的级联调用成功附加需要撤销的每个展开步骤。

一些驱动程序使用的一种技术：将附加安排为一系列`myfirst_init_*`辅助函数，将分离安排为匹配的一系列`myfirst_uninit_*`辅助函数，并有一个单一的`myfirst_fail`函数根据附加进展的程度遍历展开列表。这对于非常复杂的驱动程序更清晰；对于第18章的驱动程序，goto级联更简单且更易读。

### 级联的具体演练

让我们跟踪如果cdev创建在第3阶段失败会发生什么。附加已经：

1. 初始化softc（成功）。
2. 分配BAR0（成功）。
3. 遍历能力（总是成功；只是读取）。
4. 附加硬件层（成功）。
5. 尝试创建cdev：由于某种错误失败（磁盘满？不太可能；在测试中读者可以通过从模拟的`make_dev`返回NULL来模拟这一点）。

级联运行`fail_hw`，它调用`myfirst_hw_detach`（撤销步骤4），然后`fail_release`，它释放BAR（撤销步骤2），然后`fail_softc`，它去初始化softc（撤销步骤1）。步骤3的"撤销"是空操作（能力遍历不分配任何东西）。附加返回错误。

如果读者手工跟踪这一点，清理显然是完整的：softc已去初始化，BAR已释放，硬件层已分离。没有泄漏。没有部分状态。测试与完整附加后完整分离的测试相同：失败的附加返回后`vmstat -m | grep myfirst`应该显示零分配。

### 分离 vs 恢复：预览

为完整性：第18章没有实现的挂起和恢复路径看起来类似于分离和附加，但保留更多状态。挂起静默驱动程序（排空callout，停止用户空间访问），在softc中记录设备状态，并让系统断电。恢复从保存的状态重新初始化设备，重启callout，重新启用用户空间访问，并返回。

一个只实现附加和分离的驱动程序无法干净地挂起；内核将拒绝挂起带有非挂起感知驱动程序附加的系统。`myfirst`驱动程序足够小，第18章不担心这个问题；第22章关于电源管理会重新讨论这个主题。

### 总结第6节

分离是附加，反向播放，顶部检查`EBUSY`，在任何拆除之前有静默步骤。规则很简单；纪律在于一致地执行。附加分配的每个资源，分离释放。附加设置的每个状态，分离拆除。附加启动的每个callout，分离排空。顺序是附加的逆序。

对于第18章的驱动程序，第3阶段的分离是六步：如果忙碌则拒绝，销毁cdev，静默模拟和硬件callout，分离硬件层，释放BAR，去初始化softc。每一步都是自己的关注点。每一步都可以根据附加进行审计。每一步都可以单独测试。

第7节是测试部分。它搭建bhyve或QEMU实验室，在真实的PCI硅（模拟的）上演练附加-分离周期，并教导读者使用`pciconf`、`devinfo`、`dmesg`和几个小型用户空间程序验证驱动程序的行为。



## 第7节：测试PCI驱动程序行为

驱动程序的存在是为了与设备对话。第18章的驱动程序已经编写和编译完成，但尚未针对任何设备运行。第7节完成这个闭环。它引导读者在bhyve或QEMU中搭建FreeBSD客户机，向客户机暴露virtio-rnd PCI设备，加载驱动程序，观察完整的附加-操作-分离-卸载周期，从用户空间演练cdev，使用`pciconf -r`和`pciconf -w`读写配置空间，并通过`devinfo -v`和`dmesg`确认驱动程序的世界观与内核匹配。

测试是第2章到第17章最终回报的地方。本书建立的每个习惯（手动输入代码、阅读FreeBSD源代码、每个阶段后运行回归测试、保持实验室日志）都服务于第18章的测试纪律。本节很长，因为真实的PCI测试有真实的活动部件；每个部件都值得仔细演练。

### 测试环境

第18章的规范测试环境是在FreeBSD 14.3主机上运行的FreeBSD 14.3客户机，使用`bhyve(8)`。客户机通过bhyve的`virtio-rnd`传递接收一个模拟的virtio-rnd设备。客户机运行读者的调试内核。`myfirst`驱动程序在客户机内编译和加载；它附加到virtio-rnd设备，读者从客户机内部演练它。

等效环境使用Linux或macOS主机上的`qemu-system-x86_64`，运行调试内核的FreeBSD 14.3客户机。QEMU的`-device virtio-rng-pci`完成与bhyve的virtio-rnd相同的工作。其他一切完全相同。

本节其余部分除非另有说明，假设使用bhyve。QEMU上的读者替换等效命令；概念直接迁移。

### 准备bhyve客户机

作者的实验室脚本大致如下，为清晰起见已编辑：

```sh
#!/bin/sh
set -eu

# 加载bhyve的内核模块。
kldload -n vmm nmdm if_bridge if_tap

# 准备网络桥接。
# ifconfig bridge0 create 2>/dev/null || true
# ifconfig bridge0 addm em0 addm tap0
# ifconfig tap0 up
# ifconfig bridge0 up

# 启动客户机。
bhyve -c 2 -m 2048 -H -w \
    -s 0:0,hostbridge \
    -s 1:0,lpc \
    -s 2:0,virtio-net,tap0 \
    -s 3:0,virtio-blk,/dev/zvol/zroot/vm/freebsd143/disk0 \
    -s 4:0,virtio-rnd \
    -l com1,/dev/nmdm0A \
    -l bootrom,/usr/local/share/uefi-firmware/BHYVE_UEFI.fd \
    vm:fbsd-14.3-lab
```

关键行是`-s 4:0,virtio-rnd`。它在PCI插槽4功能0上附加一个virtio-rnd设备。客户机的PCI枚举器将在`pci0:0:4:0`看到一个设备，供应商为0x1af4，设备为0x1005，这正是第18章驱动程序探测表匹配的ID对。

其他插槽携带主桥、LPC、网络（tap桥接）和存储（zvol支持的块）。整体客户机拥有启动和运行多用户所需的一切，加上一个用于我们驱动程序的PCI设备。

偏好`vm(8)`（来自`vm-bhyve`端口的FreeBSD实用程序）的读者的更短形式：

```sh
vm create -t freebsd-14.3 fbsd-lab
vm configure fbsd-lab  # 编辑vm.conf并添加：
#   passthru0="0/0/0"        # 如果使用传递，这里不需要
#   virtio_rnd="1"            # 添加virtio-rnd设备
vm start fbsd-lab
```

`vm-bhyve`隐藏了bhyve命令行的细节。两种形式产生等效的实验室环境。

QEMU上的读者使用：

```sh
qemu-system-x86_64 -cpu host -m 2048 -smp 2 \
    -drive file=freebsd-14.3-lab.img,if=virtio \
    -netdev tap,id=net0,ifname=tap0 -device virtio-net,netdev=net0 \
    -device virtio-rng-pci \
    -bios /usr/share/qemu/OVMF_CODE.fd \
    -serial stdio
```

`-device virtio-rng-pci`行完成与bhyve等效的工作。

### 验证客户机看到设备

在客户机内部，首次启动后，virtio-rnd设备应该可见：

```sh
pciconf -lv
```

查找类似以下的条目：

```text
virtio_random0@pci0:0:4:0: class=0x00ff00 rev=0x00 hdr=0x00 vendor=0x1af4 device=0x1005 subvendor=0x1af4 subdevice=0x0004
    vendor     = 'Red Hat, Inc.'
    device     = 'Virtio entropy'
    class      = old
```

该条目告诉你三件事。首先，客户机的PCI枚举器发现了设备。其次，基础系统`virtio_random(4)`驱动程序已声明它（前导名称`virtio_random0`是线索）。第三，B:D:F是`0:0:4:0`，与bhyve `-s 4:0,virtio-rnd`配置匹配。

如果条目缺失，要么bhyve命令行没有包含`virtio-rnd`，要么客户机启动时没有加载`virtio_pci.ko`。两者都可修复：检查bhyve命令，重启客户机，或手动`kldload virtio_pci`。

### 为myfirst准备客户机

在标准FreeBSD 14.3 `GENERIC`内核上，`virtio_random`不是编译进去的；它作为可加载模块（`virtio_random.ko`）发布。在你想要加载`myfirst`时它是否已声明设备取决于平台。在现代系统上，`devmatch(8)`可能会在启动后不久看到匹配的PCI设备时自动加载`virtio_random.ko`。在刚启动的客户机上，如果`devmatch`还没有触发，virtio-rnd设备可能仍然未被声明。

首先检查：

```sh
kldstat | grep virtio_random
pciconf -lv | grep -B 1 virtio_random
```

如果两个命令都没有显示`virtio_random`，设备未被声明，你可以跳过下一步。

如果`virtio_random`已声明设备，卸载它：

```sh
sudo kldunload virtio_random
```

如果模块可卸载（未被固定、未被使用），此操作成功，virtio-rnd设备变为未被声明。`devinfo -v`现在在`pci0`下显示它，没有驱动程序绑定。

如果你想要一个跨重启永不自动加载`virtio_random`的稳定设置，添加到`/boot/loader.conf`：

```text
hint.virtio_random.0.disabled="1"
```

这可以防止启动时绑定，而不从系统中删除模块镜像。或者，在`/etc/devd.conf`或`devmatch.blocklist`下添加条目（或在运行时使用`dev.virtio_random.0.%driver` sysctl）阻止驱动程序附加。对于第18章的教学路径，每个测试会话一次简单的`kldunload`就足够了。

第18章的测试在开发期间使用第一种方法（快速迭代），当读者想要一个稳定的设置用于重复测试时使用第二种方法。

值得一提的第三种方法：内核的`dev.NAME.UNIT.%parent`和`dev.NAME.UNIT.%driver` sysctl描述绑定但不更改它们。要强制重新绑定，使用`devctl detach`和`devctl set driver`：

```sh
sudo devctl detach virtio_random0
sudo devctl set driver -f pci0:0:4:0 myfirst
```

`-f`标志强制设置，即使另一个驱动程序已声明设备。这是读者想要在不重新加载模块的情况下切换驱动程序的脚本测试中使用的精确命令。

### 加载myfirst并观察附加

移除`virtio_random`后，加载`myfirst`：

```sh
sudo kldload ./myfirst.ko
```

观察`dmesg`中的附加：

```sh
sudo dmesg | tail -20
```

预期输出（第3阶段）：

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> mem 0xc1000000-0xc100001f at device 4.0 on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
myfirst0: PCIe capability at 0x0
myfirst0: MSI-X capability at 0x0
myfirst0: hardware layer attached to BAR: 32 bytes (tag=... handle=...)
myfirst0: BAR[0x00] = 0x10010000
```

（virtio遗留设备的能力偏移为0，因为bhyve模拟没有暴露PCIe能力；使用QEMU的virtio-rng-pci测试的读者可能会看到非零偏移。）

cdev `/dev/myfirst0`存在：

```sh
ls -l /dev/myfirst*
```

`devinfo -v`显示设备：

```sh
devinfo -v | grep -B 1 -A 4 myfirst
```

```text
pci0
    myfirst0
        pnpinfo vendor=0x1af4 device=0x1005 ...
        resources:
            memory: 0xc1000000-0xc100001f
```

这是驱动程序附加到设备，对用户空间可见，准备好被演练。

### 演练cdev

`myfirst`驱动程序的cdev路径是第10章到第17章的接口。它接受`open`、`close`、`read`和`write`系统调用。在第17章仅模拟构建中，读取从模拟callout填充的命令-响应环形缓冲区拉取数据。在第18章PCI构建中，模拟未附加；cdev仍然响应`open`、`read`、`write`和`close`，但数据路径没有活动callout喂养它。读取返回底层第10章循环缓冲区包含的内容（启动时通常为空）；写入将数据排队到同一缓冲区。

这是第18章的预期行为。本章测试的重点是证明驱动程序附加到真实的PCI设备，BAR是活动的，cdev可从用户空间到达，分离正确清理。第19章添加将使cdev的数据路径对真实设备有意义的中断路径。

一个小型用户空间程序来演练cdev：

```c
/* 最小读写测试。 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    int fd = open("/dev/myfirst0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    char buf[16];
    ssize_t n = read(fd, buf, sizeof(buf));
    printf("read returned %zd\n", n);

    close(fd);
    return 0;
}
```

编译并运行：

```sh
cc -o myfirst_test myfirst_test.c
./myfirst_test
```

输出可能是短读、零读或`EAGAIN`（取决于第10章缓冲区是否有任何数据就绪）。重要的是读取路径不会使内核崩溃，不会在`dmesg`中产生错误，并以定义的结果返回用户空间。

写入测试同样直接：

```c
char cmd[16] = "hello\n";
write(fd, cmd, 6);
```

写入将数据推入第10章循环缓冲区。在PCI后端上，数据留在缓冲区中直到读取器将其拉出；没有模拟callout运行来处理它。测试的是周期运行而不崩溃且`dmesg`保持安静。

### 从用户空间读写配置空间

`pciconf(8)`有两个标志让用户空间程序直接检查和修改PCI配置空间：

- `pciconf -r <selector> <offset>:<length>`读取配置空间字节并以十六进制打印。
- `pciconf -w <selector> <offset> <value>`将值写入特定偏移。

选择器标识设备。它可以是设备的驱动程序名称（`myfirst0`）或其B:D:F（`pci0:0:4:0`）。

读取示例：

```sh
sudo pciconf -r myfirst0 0x00:8
```

输出：

```text
00: 1a f4 05 10 07 05 10 00
```

字节是配置空间的前八字节，按顺序：供应商ID（`1af4`，小端）、设备ID（`1005`）、命令寄存器（`0507`，设置了`MEMEN`和`BUSMASTER`）、状态寄存器（`0010`）。

写入示例（危险，不要随意这样做）：

```sh
sudo pciconf -w myfirst0 0x04 0x0503
```

这清除`BUSMASTER`并保留`MEMEN`设置。对设备的影响取决于设备；在运行中的设备上，它可能导致DMA操作失败。对于驱动程序不对其使用DMA的设备（第18章的情况），更改本质上无害但也本质上无意义。

读者应只在有意的诊断场景中使用`pciconf -w`，并充分了解后果。将垃圾值写入错误的字段可能会使设备、总线或内核死锁。

### devinfo -v及其告诉你的内容

`devinfo -v`是newbus树检查器。它遍历系统中的每个设备并打印每个设备及其资源、其父设备、其单元号及其子设备。对于驱动程序作者，它是"内核认为驱动程序拥有什么"的规范参考。

输出片段，针对`myfirst0`设备：

```text
nexus0
  acpi0
    pcib0
      pci0
        myfirst0
            pnpinfo vendor=0x1af4 device=0x1005 subvendor=0x1af4 subdevice=0x0004 class=0x00ff00
            resources:
                memory: 0xc1000000-0xc100001f
        virtio_pci0
            pnpinfo vendor=0x1af4 device=0x1000 ...
            resources: ...
        ... (其他pci子设备)
```

树显示从根（nexus）向下通过平台（x86上的ACPI）、PCI桥（pcib）、PCI总线（pci0），最后是该总线上的设备的路径。`myfirst0`是`pci0`的子设备。其资源列表显示声明的内存BAR。

使用`devinfo -v | grep -B 1 -A 5 myfirst`只提取相关块是树很大时的标准技术。

### dmesg作为诊断工具

`dmesg`是内核的消息缓冲区。内核中的每个`device_printf`、`printf`和`KASSERT`失败都会出现在`dmesg`中。对于驱动程序作者，它是主要的调试界面。

在加载、操作和卸载驱动程序时追踪`dmesg`是你早期发现细微问题的方式。典型会话：

```sh
# 在第二个终端启动dmesg追踪。
dmesg -w
```

然后，在主终端：

```sh
sudo kldload ./myfirst.ko
```

追踪终端实时显示附加消息。运行测试：

```sh
./myfirst_test
```

追踪终端显示驱动程序在测试期间发出的任何消息。卸载：

```sh
sudo kldunload myfirst
```

追踪终端显示分离消息。

如果任何步骤产生意外警告或错误，你实时看到它。没有追踪`dmesg`，你可能会错过指示潜在问题的单个警告。

### 使用devctl模拟热插拔

`devctl(8)`让用户空间程序模拟真实热插拔或设备移除会生成的newbus事件。常见调用：

```sh
# 强制设备分离（调用驱动程序的分离方法）。
sudo devctl detach myfirst0

# 重新附加设备（调用驱动程序的探测和附加）。
sudo devctl attach myfirst0

# 禁用设备（防止将来的探测绑定）。
sudo devctl disable myfirst0

# 重新启用已禁用的设备。
sudo devctl enable myfirst0

# 重新扫描总线（等效于热插拔通知）。
sudo devctl rescan pci0
```

对于测试第18章的分离路径，`devctl detach myfirst0`是主要工具。它在不需要卸载模块的情况下演练分离代码。驱动程序的分离运行；cdev消失；BAR被释放；设备回到未被声明。

随后的`devctl attach`重新触发探测和附加。如果探测成功（供应商和设备ID仍然匹配）且附加成功，设备再次绑定。这是读者用来测试驱动程序可以附加、分离和重新附加而不泄漏资源的周期。

在循环中运行此周期是标准回归模式：

```sh
for i in 1 2 3 4 5; do
    sudo devctl detach myfirst0
    sudo devctl attach myfirst0
done
sudo vmstat -m | grep myfirst
```

如果`vmstat -m`在循环后显示`myfirst` malloc类型的当前分配为零，驱动程序是干净的：每次附加分配，每次分离释放，总量平衡。

### 简单的回归脚本

综合起来，一个验证第3阶段PCI路径的脚本：

```sh
#!/bin/sh
#
# 第18章第3阶段回归测试。
# 在暴露virtio-rnd设备的bhyve客户机内运行。

set -eu

echo "=== 卸载virtio_random（如果存在）==="
kldstat | grep -q virtio_random && kldunload virtio_random || true

echo "=== 加载myfirst ==="
kldload ./myfirst.ko
sleep 1

echo "=== 检查附加 ==="
devinfo -v | grep -q 'myfirst0' || { echo FAIL: no attach; exit 1; }

echo "=== 检查BAR声明 ==="
devinfo -v | grep -A 3 'myfirst0' | grep -q 'memory:' || \
    { echo FAIL: no BAR; exit 1; }

echo "=== 演练cdev ==="
./myfirst_test
sleep 1

echo "=== 分离-附加周期 ==="
for i in 1 2 3; do
    devctl detach myfirst0
    sleep 0.5
    devctl attach pci0:0:4:0
    sleep 0.5
done

echo "=== 卸载myfirst ==="
kldunload myfirst
sleep 1

echo "=== 检查泄漏 ==="
vmstat -m | grep -q myfirst && echo WARN: myfirst malloc type still present

echo "=== 成功 ==="
```

脚本遵循可重复的模式：设置、加载、检查附加、检查资源、演练、周期、卸载、检查泄漏。每次更改驱动程序后运行此脚本是早期发现回归的方式。

### 出于诊断目的读取配置空间

使用`pciconf -r`验证驱动程序对配置空间的视图与用户空间视图匹配的小型示例。

在驱动程序内部，附加路径通过`pci_get_vendor`读取供应商ID，通过`pci_get_device`读取设备ID。用户空间通过`pciconf -r myfirst0 0x00:4`读取相同的字节。

预期输出：

```text
00: f4 1a 05 10
```

字节是供应商ID（`0x1af4`）和设备ID（`0x1005`），按小端顺序。反转字节得到供应商`0x1af4`和设备`0x1005`，与驱动程序的探测表匹配。

做这个检查不是生产中会做的事；PCI子系统经过良好测试，值是可靠的。它作为学习练习有用：它证明驱动程序对配置空间的视图与用户空间看到的匹配，它巩固读者对`pci_get_vendor`如何关联到底层字节的理解。

### 第7节不测试什么

第7节验证第18章驱动程序附加到真实的PCI设备、声明BAR、暴露cdev、通过第16章访问器层读取BAR、干净分离、释放BAR并卸载而不泄漏。它不测试：

- 中断处理。驱动程序不注册中断；第19章会。
- MSI或MSI-X。第20章会。
- DMA。第21章会。
- 设备特定协议。第17章模拟的命令-响应协议不是virtio-rnd协议，所以写入的结果没有意义。第18章的驱动程序不是virtio-rnd驱动程序。

想要实际实现virtio-rnd协议的驱动程序的读者应该阅读`/usr/src/sys/dev/virtio/random/virtio_random.c`。它是一个干净、专注的驱动程序，完成第18章的读者应该能够理解。

### 第7节收尾

测试PCI驱动程序意味着搭建一个驱动程序可以遇到设备的环境。对于第18章，该环境是向客户机暴露virtio-rnd设备的bhyve或QEMU客户机。工具是`pciconf -lv`（查看设备）、`kldload`和`kldunload`（加载和卸载驱动程序）、`devinfo -v`（查看newbus树和驱动程序资源）、`devctl`（模拟热插拔）、`dmesg`（查看诊断消息）和`vmstat -m`（检查泄漏）。纪律是每次更改后运行可重复的脚本，检查其输出，并在继续之前修复任何警告或失败。

本节末尾的回归脚本是每个读者应该适应自己驱动程序和实验室的模板。连续运行十次并看到每次相同输出是驱动程序可靠的证明。运行一次就崩溃是驱动程序有第18章纪律（附加顺序、分离顺序、资源配对）本应防止的错误的迹象；修复通常很小。

第8节是教学主体的最后一节。它将第18章代码重构为最终形式，将版本提升到`1.1-pci`，编写新的`PCI.md`，并为第19章做准备。



## 第8节：重构和版本化你的PCI驱动程序

PCI驱动程序现在工作了。第8节是整理部分。它将第18章代码合并为干净、可维护的结构，更新驱动程序的`Makefile`和模块元数据，编写将与`LOCKING.md`、`HARDWARE.md`和`SIMULATION.md`并存的`PCI.md`文档，将版本提升到`1.1-pci`，并对模拟和真实PCI后端运行完整的回归测试。

走到这一步的读者可能会想跳过第8节。与前面的节相比它很无聊。它不引入任何新的PCI概念。诱惑是真实的，也是一个错误。重构是将工作驱动程序转变为可维护驱动程序的东西。今天工作但组织混乱的驱动程序在第19章（中断到来时）、第20章（MSI和MSI-X到来时）、第20章和第21章（DMA到来时）以及每个后续章节中扩展都会很痛苦。第8节做的几行整理在第四部分其余部分及以后都会产生回报。

### 最终文件布局

在第18章结束时，`myfirst`驱动程序由这些文件组成：

```text
myfirst.c       - 主驱动程序：softc、cdev、模块事件、数据路径。
myfirst.h       - 共享声明：softc、锁宏、原型。
myfirst_hw.c    - 第16章硬件访问层：CSR_*访问器、
                   访问日志、sysctl处理程序。
myfirst_hw.h    - 第16章寄存器映射和访问器声明，
                   在第17章扩展。
myfirst_sim.c   - 第17章模拟后端：callout、故障
                   注入、命令-响应。
myfirst_sim.h   - 第17章模拟接口。
myfirst_pci.c   - 第18章PCI附加：探测、附加、分离、
                   DRIVER_MODULE、MODULE_DEPEND。
myfirst_pci.h   - 第18章PCI声明：ID表条目结构、
                   供应商和设备ID常量。
myfirst_sync.h  - 第3部分同步原语。
cbuf.c / cbuf.h - 第10章循环缓冲区，仍在使用。
Makefile        - kmod构建：KMOD、SRCS、CFLAGS。
HARDWARE.md     - 第16/17章寄存器映射文档。
LOCKING.md      - 第15章及以后的锁规则文档。
SIMULATION.md   - 第17章模拟后端文档。
PCI.md          - 第18章PCI支持文档。
```

拆分与第17章预期的相同。`myfirst_pci.c`和`myfirst_pci.h`是新的。其他每个文件在第18章之前就存在，并且要么已扩展（`myfirst_hw.c`获得了`myfirst_hw_attach_pci`），要么保持不变。驱动程序的主文件（`myfirst.c`）增加了几行以添加PCI相关的softc字段和对PCI特定分离助手的调用；它没有显著增长。

值得陈述的经验法则：每个文件应该有一个职责。`myfirst.c`是驱动程序的集成点；它将每部分绑在一起。`myfirst_hw.c`是关于硬件访问。`myfirst_sim.c`是关于模拟硬件。`myfirst_pci.c`是关于附加到真实的PCI硬件。当读者打开一个文件时，他们应该能够从文件名预测其中的内容。当第19章添加`myfirst_intr.c`时，预测将成立：该文件是关于中断的。

### 最终Makefile

```makefile
# 第18章myfirst驱动程序的Makefile。
#
# 合并第10-15章驱动程序、第16章硬件层、
# 第17章模拟后端和第18章PCI附加。
# 驱动程序可通过kldload(8)作为独立内核模块加载；
# 加载后，它自动附加到任何供应商/设备ID
# 匹配myfirst_pci_ids[]条目的PCI设备（见myfirst_pci.c）。

KMOD=  myfirst
SRCS=  myfirst.c myfirst_hw.c myfirst_sim.c myfirst_pci.c cbuf.c

# 版本字符串。随任何用户可见更改更新此行。
CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.1-pci\"

# 可选：不带PCI支持构建（仅模拟）。
# CFLAGS+= -DMYFIRST_SIMULATION_ONLY

# 可选：不带模拟回退构建（仅PCI）。
# CFLAGS+= -DMYFIRST_PCI_ONLY

.include <bsd.kmod.mk>
```

四个SRCS，一个版本字符串，两个注释掉的编译选项。构建是一个命令：

```sh
make
```

输出是`myfirst.ko`，可通过`kldload`加载到任何FreeBSD 14.3内核。

### 版本字符串

版本字符串从`1.0-simulated`移动到`1.1-pci`。提升反映驱动程序获得了新能力（真实PCI支持）而不改变任何用户可见行为（cdev仍然做它做的事）。次版本提升是合适的；主版本提升会暗示不兼容的更改。

后续章节将继续编号：第19章后`1.2-intr`，第20章后`1.3-msi`，第20章和第21章后`1.4-dma`，等等。到第四部分结束时，驱动程序将在`1.4-dma`左右，每个次版本反映一个重要的能力添加。

版本字符串在两个地方可见：`kldstat -v`显示它，驱动程序加载时的`dmesg`横幅打印它。想要知道运行哪个版本驱动程序的用户或系统管理员可以grep `dmesg`查找横幅。

### PCI.md文档

一个新文档加入驱动程序语料库。`PCI.md`很短；其工作是描述驱动程序提供的PCI支持，形式供未来读者查阅而无需阅读源代码。

```markdown
# myfirst驱动程序中的PCI支持

## 支持的设备

截至版本1.1-pci，myfirst附加到匹配以下供应商/设备ID对的PCI设备：

| Vendor | Device | 描述                                    |
| ------ | ------ | ---------------------------------------------- |
| 0x1af4 | 0x1005 | Red Hat/virtio-rnd（演示目标；见README）   |

此列表维护在`myfirst_pci.c`的静态数组`myfirst_pci_ids[]`中。添加新的支持设备需要：

1. 向`myfirst_pci_ids[]`添加一个条目，包含供应商和
   设备ID以及人类可读描述。
2. 验证驱动程序的BAR布局和寄存器映射与新设备兼容。
3. 针对新设备测试驱动程序。
4. 更新此文档。

## 附加行为

驱动程序的探测例程在匹配时返回`BUS_PROBE_DEFAULT`，否则返回`ENXIO`。附加将BAR0分配为内存资源，遍历PCI能力列表（电源管理、MSI、MSI-X、PCIe、PCIe AER如果存在），针对BAR附加第16章硬件层，并创建`/dev/myfirstN`。第17章模拟后端不在PCI路径上附加；驱动程序的访问器读写真实BAR，模拟callout不运行。

## 分离行为

如果驱动程序有打开的文件描述符或进行中的命令，分离拒绝进行（返回`EBUSY`）。否则它销毁cdev、排空任何活动callout和任务、分离硬件层、释放BAR并去初始化softc。

## 模块依赖

驱动程序的`MODULE_DEPEND`声明：

- `pci`，版本1：内核的PCI子系统。

没有声明其他模块依赖。

## 已知限制

- 驱动程序当前不处理中断。见第19章了解中断处理扩展。
- 驱动程序当前不支持DMA。见第20章和第21章了解DMA扩展。
- 第17章模拟后端不在PCI路径上附加。模拟的callout和命令协议在仅模拟构建（`-DMYFIRST_SIMULATION_ONLY`）中仍然可用，供没有匹配PCI硬件的读者使用。

## 另见

- `HARDWARE.md`了解寄存器映射。
- `SIMULATION.md`了解模拟后端。
- `LOCKING.md`了解锁规则。
- `README.md`了解如何搭建bhyve测试环境。
```

此文档与驱动程序源代码并排存在。未来读者（三个月后的作者自己，或贡献者，或端口维护者）可以在五分钟内阅读它并理解驱动程序的PCI故事而无需打开代码。

### 更新LOCKING.md

`LOCKING.md`已经记录了第11章到第17章的锁规则。第18章添加两个小项：

1. 分离顺序：`destroy_dev`、静默callout、`myfirst_hw_detach`、`bus_release_resource`和`myfirst_deinit_softc`的新步骤，按该顺序。
2. 附加失败级联：goto标签（`fail_hw`、`fail_release`、`fail_softc`）及其每个撤销的内容。

更新是现有文档中的几行。第18章没有引入新锁；第15章锁层次结构不变。

### 更新HARDWARE.md

`HARDWARE.md`已经记录了第16章和第17章寄存器映射。第18章添加一个小项：

- 驱动程序附加的BAR是BAR 0，用`rid = PCIR_BAR(0)`请求，作为`SYS_RES_MEMORY`用`RF_ACTIVE`分配。标签和句柄用`rman_get_bustag(9)`和`rman_get_bushandle(9)`提取。

这就是全部添加。寄存器映射本身在第18章没有改变；相同的偏移、相同的宽度、相同的位定义。

### 回归测试

重构完成后，第18章的完整回归测试是：

1. **干净编译。** `make`产生`myfirst.ko`而无警告。CFLAGS从第4章起已包含`-Wall -Werror`；如果出现任何警告，构建失败。
2. **无错误加载。** `kldload ./myfirst.ko`成功，`dmesg`显示模块级横幅。
3. **附加到真实PCI设备。** 在有virtio-rnd设备的bhyve客户机中，驱动程序附加，`dmesg`显示完整的第18章附加序列。
4. **创建并演练cdev。** `/dev/myfirst0`存在，`open`/`read`/`write`/`close`工作，没有内核消息指示错误。
5. **遍历能力。** `dmesg`显示客户机virtio-rnd暴露的任何能力的能力偏移。
6. **从用户空间读取配置空间。** `pciconf -r myfirst0 0x00:8`产生预期字节。
7. **干净分离。** `devctl detach myfirst0`在`dmesg`中产生分离横幅；cdev消失；`vmstat -m | grep myfirst`显示零活动分配。
8. **干净重新附加。** `devctl attach pci0:0:4:0`重新触发探测和附加；完整周期再次运行。
9. **干净卸载。** `kldunload myfirst`成功；`kldstat -v | grep myfirst`不返回任何内容。
10. **无泄漏。** `vmstat -m | grep myfirst`不返回任何内容。

第7节的回归脚本按顺序运行步骤1到10并报告成功或第一个失败。每次更改后运行它是早期发现回归的纪律。

### 重构完成了什么

在第18章开始时，`myfirst`驱动程序是一个模拟。它有一个`malloc(9)`支持的寄存器块、一个模拟后端和一个详尽的测试框架。它不附加到真实硬件；它是一个手动加载的模块。

在第18章结束时，驱动程序是一个PCI驱动程序。当存在真实PCI设备时它附加到它。它通过标准FreeBSD总线分配API声明设备的BAR。它使用第16章访问器层通过`bus_space(9)`读写设备的寄存器。第17章模拟通过编译时开关（`-DMYFIRST_SIMULATION_ONLY`）对没有匹配PCI硬件的读者仍然可用，但默认构建针对PCI路径并让模拟callout空闲。附加和分离路径遵循每个其他FreeBSD驱动程序使用的newbus约定。

代码明显是FreeBSD风格的。布局是真实驱动程序在有不同模拟、硬件和总线职责时使用的布局。词汇是真实驱动程序共享的词汇。第一次打开驱动程序的贡献者发现熟悉的结构，阅读文档，并可以按子系统导航代码。

### 关于符号可见性的简短说明

对比第17章驱动程序和第18章驱动程序的读者会注意到几个函数已更改可见性。第17章中一些`static`函数现在被导出（非静态），因为`myfirst_pci.c`需要它们。示例包括`myfirst_init_softc`、`myfirst_deinit_softc`和`myfirst_quiesce`。

约定是：只在其自己文件内调用的函数是`static`。跨文件调用（但只在此驱动程序内）的函数是非静态的，在`myfirst.h`或其他项目本地头文件中声明。可从其他模块调用（罕见，通常只通过KPI）的函数通过内核风格符号表显式导出；这与第18章无关。

重构没有向驱动程序外导出任何新符号；它只是将几个函数从文件本地提升到驱动程序本地。被提升困扰的读者有两个选择：将函数保留在`myfirst.c`中并通过`myfirst_pci.c`调用的小助手调用它们（多一层间接），或接受提升并在源代码注释中记录它。本书选择后者；驱动程序足够小，偶尔的驱动程序本地导出很容易审计。

### 第8节收尾

重构同样在代码上很小但在组织上很重要。新文件拆分、新文档文件、现有文档文件的更新、版本提升和回归测试。每一步都很便宜；一起它们将工作驱动程序转变为可维护的驱动程序。

第18章驱动程序完成了。本章以实验、挑战、故障排除和通往第19章的桥梁结束，在那里PCI附加的驱动程序获得真实的中断处理程序。第20章然后添加MSI和MSI-X；第20章和第21章添加DMA。这些章节中的每一个都会添加一个文件（`myfirst_intr.c`、`myfirst_dma.c`）并扩展附加和分离路径。第18章建立的形状将保持。



## 一起阅读真实驱动程序：uart_bus_pci.c

前面八节一步步构建了第18章的驱动程序。在实验之前，值得花时间与一个遵循相同模式的真实FreeBSD驱动程序在一起。`/usr/src/sys/dev/uart/uart_bus_pci.c`是一个干净的例子。它是`uart(4)`驱动程序的PCI附加，处理PCI附加的串口：调制解调卡、芯片组集成UART、管理程序串口模拟，以及企业服务器使用的控制台重定向芯片。

在编写第18章驱动程序之后阅读这个文件是模式识别的一个简短练习。文件中没有任何新内容。每一行都映射到第18章教授的概念。该文件有366行；本节遍历重要部分，标记每个部分对应哪个第18章概念。

### 文件顶部

```c
/*
 * Copyright (c) 2006 Marcel Moolenaar All rights reserved.
 * Copyright (c) 2001 M. Warner Losh <imp@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 ...
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_cpu.h>
```

SPDX许可证标签是BSD-2-Clause，标准的FreeBSD许可证。包含列表与第18章的`myfirst_pci.c`几乎相同。`dev/pci/pcivar.h`和`dev/pci/pcireg.h`包含是PCI子系统接口；`dev/uart/uart.h`等是驱动程序的内部头文件，第18章驱动程序没有等价物。

### 方法表和驱动程序结构

```c
static device_method_t uart_pci_methods[] = {
	DEVMETHOD(device_probe,		uart_pci_probe),
	DEVMETHOD(device_attach,	uart_pci_attach),
	DEVMETHOD(device_detach,	uart_pci_detach),
	DEVMETHOD(device_resume,	uart_bus_resume),
	DEVMETHOD_END
};

static driver_t uart_pci_driver = {
	uart_driver_name,
	uart_pci_methods,
	sizeof(struct uart_softc),
};
```

四个方法条目，不是三个：`uart(4)`还实现了`device_resume`以支持系统挂起和恢复。恢复函数是`uart_bus_resume`，位于核心`uart(4)`驱动程序中，在每个UART附加变体中重用。第18章的驱动程序跳过了`resume`；生产质量的驱动程序通常实现它。

`driver_t`的名称是`uart_driver_name`，在核心UART驱动程序的其他地方定义为`"uart"`。softc大小是`sizeof(struct uart_softc)`，一个在`uart_bus.h`中定义的结构。

### ID表

```c
struct pci_id {
	uint16_t	vendor;
	uint16_t	device;
	uint16_t	subven;
	uint16_t	subdev;
	const char	*desc;
	int		rid;
	int		rclk;
	int		regshft;
};
```

表条目比第18章的更丰富。`subven`和`subdev`字段让匹配可以区分围绕共享芯片组构建的来自不同供应商的卡。`rid`字段携带BAR的配置空间偏移（不同板卡使用不同的BAR）。`rclk`携带参考时钟频率，单位Hz，因制造商而异。`regshft`携带寄存器移位（一些板卡将其UART寄存器放在4字节边界上，一些放在8字节边界上）。

```c
static const struct pci_id pci_ns8250_ids[] = {
	{ 0x1028, 0x0008, 0xffff, 0, "Dell Remote Access Card III", 0x14,
	    128 * DEFAULT_RCLK },
	{ 0x1028, 0x0012, 0xffff, 0, "Dell RAC 4 Daughter Card Virtual UART",
	    0x14, 128 * DEFAULT_RCLK },
	/* ... many more entries ... */
	{ 0xffff, 0, 0xffff, 0, NULL, 0, 0 }
};
```

该表有几十个条目。每个都是`uart(4)`驱动程序支持的板卡。子供应商值`0xffff`意味着"匹配任何子供应商"。最后一条是哨兵。

第18章的驱动程序有一个条目，因为它针对一个演示设备。`uart_bus_pci.c`有几十个，因为UART硬件生态系统很大，驱动程序必须枚举每个支持的变体。

### 探测例程

```c
static int
uart_pci_probe(device_t dev)
{
	struct uart_softc *sc;
	const struct pci_id *id;
	struct pci_id cid = {
		.regshft = 0,
		.rclk = 0,
		.rid = 0x10 | PCI_NO_MSI,
		.desc = "Generic SimpleComm PCI device",
	};
	int result;

	sc = device_get_softc(dev);

	id = uart_pci_match(dev, pci_ns8250_ids);
	if (id != NULL) {
		sc->sc_class = &uart_ns8250_class;
		goto match;
	}
	if (pci_get_class(dev) == PCIC_SIMPLECOMM &&
	    pci_get_subclass(dev) == PCIS_SIMPLECOMM_UART &&
	    pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A) {
		id = &cid;
		sc->sc_class = &uart_ns8250_class;
		goto match;
	}
	return (ENXIO);

match:
	result = uart_bus_probe(dev, id->regshft, 0, id->rclk,
	    id->rid & PCI_RID_MASK, 0, 0);
	if (result > 0)
		return (result);
	if (sc->sc_sysdev == NULL)
		uart_pci_unique_console_match(dev);
	if (id->desc)
		device_set_desc(dev, id->desc);
	return (result);
}
```

探测比第18章的更复杂。它首先搜索供应商/设备ID表。如果失败，它回退到基于类的匹配：任何类为`PCIC_SIMPLECOMM`（简单通信）且子类为`PCIS_SIMPLECOMM_UART`（UART控制器）且接口早于`PCIP_SIMPLECOMM_UART_16550A`（早于16550A意味着"经典UART没有增强功能"）的设备。这是让驱动程序处理通用UART控制器的后备探测，即使它们的供应商和设备ID不在表中。

`match:`标签可以从任一路径到达。它调用`uart_bus_probe`（核心UART驱动程序的探测辅助函数）并传入条目的寄存器移位、参考时钟和BAR偏移。返回值要么是`BUS_PROBE_*`优先级，要么是正错误码。第18章的驱动程序直接返回`BUS_PROBE_DEFAULT`；`uart(4)`委托给`uart_bus_probe`，因为核心驱动程序有额外的检查。

`pci_get_class`、`pci_get_subclass`和`pci_get_progif`访问器返回第18章描述的类代码字段。它们在这里的使用是基于类匹配的具体示例。

### 附加例程

```c
static int
uart_pci_attach(device_t dev)
{
	struct uart_softc *sc;
	const struct pci_id *id;
	int count;

	sc = device_get_softc(dev);

	id = uart_pci_match(dev, pci_ns8250_ids);
	if ((id == NULL || (id->rid & PCI_NO_MSI) == 0) &&
	    pci_msi_count(dev) == 1) {
		count = 1;
		if (pci_alloc_msi(dev, &count) == 0) {
			sc->sc_irid = 1;
			device_printf(dev, "Using %d MSI message\n", count);
		}
	}

	return (uart_bus_attach(dev));
}
```

附加很短。它重新匹配设备（因为探测的匹配状态在探测/附加调用之间不保留），检查设备是否支持MSI（单向量），如果可用则分配一个MSI向量，然后委托给`uart_bus_attach`进行实际附加。

这是第18章没有使用的模式。`uart(4)`利用MSI（当可用时），否则回退到传统IRQ。本书第20章将介绍MSI和MSI-X；`uart(4)`的附加是一个预览。

一些表条目中的`PCI_NO_MSI`标志标记已知MSI有问题或不可靠的板卡；对于这些板卡，附加跳过MSI并依赖传统IRQ。

### 分离例程

```c
static int
uart_pci_detach(device_t dev)
{
	struct uart_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_irid != 0)
		pci_release_msi(dev);

	return (uart_bus_detach(dev));
}
```

八行，每一行都有意义。如果分配了MSI则释放它。委托给`uart_bus_detach`进行其余的拆除。

第18章的分离更长，因为`myfirst`驱动程序不委托给核心驱动程序；一切都在PCI文件中。`uart(4)`将公共拆除分解到`uart_bus_detach`中，从每个附加变体的分离中调用。

### DRIVER_MODULE行

```c
DRIVER_MODULE(uart, pci, uart_pci_driver, NULL, NULL);
```

一行。模块名是`uart`（匹配`driver_t`）。总线是`pci`。两个`NULL`是`uart(4)`不需要的模块init和cleanup处理程序。

第18章的驱动程序有同样的行，用`myfirst`代替`uart`。

### 本次遍历教授什么

`uart_bus_pci.c`有366行。大约60行是代码；其余是ID表（250+条目，许多跨多行）和UART处理特定的辅助函数。

代码形状上几乎无法与第18章的驱动程序区分开来。一个`pci_id`结构。一个ID表。一个匹配表的探测。一个声明BAR的附加（通过`uart_bus_attach`）。一个释放一切的分离。`DRIVER_MODULE`。`MODULE_DEPEND`。差异都是UART特定特性：子供应商匹配、基于类的后备、MSI分配、寄存器移位和参考时钟字段。

在读完第18章后发现`uart_bus_pci.c`可读的读者已经理解了本章要点。第18章驱动程序是一个真正的FreeBSD PCI驱动程序，而不是玩具。它缺少一些特性（MSI、恢复、DMA），后面的章节将添加，但其骨架是树中每个真实驱动程序的骨架。

在`uart_bus_pci.c`之后值得阅读进行对比：`/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`，它是现代（非遗留）virtio PCI附加。它比`uart_bus_pci.c`更丰富，因为它处理virtio的分层传输，但形状相同。



## 深入了解PCI能力列表

第5节介绍了`pci_find_cap(9)`和`pci_find_extcap(9)`作为发现设备可选特性的工具。本小节更深入一层，展示能力列表在配置空间中的结构以及驱动程序如何遍历整个列表而不是查找特定能力。

### 遗留能力列表的结构

遗留能力列表位于配置空间的前256字节中。它从存储在设备`PCIR_CAP_PTR`字段中的偏移（配置空间偏移`0x34`）开始。该偏移处的字节是能力ID；紧接着的下一个字节是下一个能力的偏移（或零表示这是最后一个）；能力的其余字节是特性特定的。

最小能力头是两个字节：

```text
偏移 0: 能力ID（一个字节，值如 PCIY_MSI = 0x05）
偏移 1: 下一个指针（一个字节，下一个能力的偏移，0 表示结束）
```

遍历列表的驱动程序从`PCIR_CAP_PTR`读取能力指针，然后通过读取每个能力的`next`字节跟随链，直到到达零。

代码中的具体遍历：

```c
static void
myfirst_dump_caps(device_t dev)
{
	uint8_t ptr, id;
	int safety = 64;  /* 防止格式错误的列表 */

	ptr = pci_read_config(dev, PCIR_CAP_PTR, 1);
	while (ptr != 0 && safety-- > 0) {
		id = pci_read_config(dev, ptr, 1);
		device_printf(dev,
		    "遗留能力 ID 0x%02x 在偏移 0x%02x\n", id, ptr);
		ptr = pci_read_config(dev, ptr + 1, 1);
	}
}
```

`safety`计数器防止`next`指针形成循环的格式错误配置空间。行为良好的设备从不会产生这种情况，但防御性代码将配置空间视为潜在对抗性的。

遍历打印每个能力的ID和偏移。驱动程序然后可以将ID与`PCIY_*`常量匹配并处理它支持的那些。

### 扩展能力列表的结构

PCIe扩展能力列表从偏移`PCIR_EXTCAP`（`0x100`）开始并使用4字节头。布局在`/usr/src/sys/dev/pci/pcireg.h`中编码：

```text
位 15:0   能力ID    (PCIM_EXTCAP_ID,       掩码 0x0000ffff)
位 19:16  能力版本 (PCIM_EXTCAP_VER,     掩码 0x000f0000)
位 31:20  下一个指针     (PCIM_EXTCAP_NEXTPTR,  掩码 0xfff00000)
```

FreeBSD在原始掩码之上暴露三个辅助宏：

- `PCI_EXTCAP_ID(header)` 返回能力ID。
- `PCI_EXTCAP_VER(header)` 返回版本。
- `PCI_EXTCAP_NEXTPTR(header)` 返回下一个指针（已移位到其自然范围）。

12位下一个指针总是4字节对齐的；下一个指针为零终止列表。

使用辅助宏的遍历：

```c
static void
myfirst_dump_extcaps(device_t dev)
{
	uint32_t header;
	int off = PCIR_EXTCAP;
	int safety = 64;

	while (off != 0 && safety-- > 0) {
		header = pci_read_config(dev, off, 4);
		if (header == 0 || header == 0xffffffff)
			break;
		device_printf(dev,
		    "扩展能力 ID 0x%04x 版本 %u 在偏移 0x%03x\n",
		    PCI_EXTCAP_ID(header), PCI_EXTCAP_VER(header), off);
		off = PCI_EXTCAP_NEXTPTR(header);
	}
}
```

遍历器读取4字节头并用辅助宏解包。零或全1头意味着没有扩展能力（后者是非PCIe设备对任何扩展能力读取返回的内容）。

### 为什么遍历很重要

驱动程序很少需要完整遍历。`pci_find_cap`和`pci_find_extcap`是常用接口：驱动程序请求特定能力，要么得到偏移要么得到`ENOENT`。想要为诊断目的转储完整能力列表的驱动程序使用上面显示的遍历器。

理解结构的价值在于阅读数据手册。一个说"设备从偏移0xa0开始实现MSI能力"的数据手册是在说：配置空间偏移`0xa0`处的字节是能力ID（将等于`0x05`表示MSI），`0xa1`处的字节是下一个指针，从`0xa2`开始的字节是MSI能力结构。`pci_find_cap(dev, PCIY_MSI, &capreg)`返回`capreg = 0xa0`因为那是能力所在的位置。

访问能力结构的驱动程序从`capreg + offset`读取，其中`offset`在能力自己的结构中定义。特定字段有特定偏移；pcireg.h头文件将偏移定义为`PCIR_MSI_*`。

### 遍历特定能力的字段

一个示例。MSI能力有几个驱动程序关心的字段，在相对于能力头的特定偏移处：

```text
PCIR_MSI_CTRL (0x02): 消息控制（16位，启用、向量计数）
PCIR_MSI_ADDR (0x04): 消息地址低位（32位）
PCIR_MSI_ADDR_HIGH (0x08): 消息地址高位（32位，仅64位）
PCIR_MSI_DATA (0x08 或 0x0c): 消息数据（16位）
```

从`pci_find_cap(dev, PCIY_MSI, &capreg)`得到`capreg`的驱动程序读取消息控制寄存器：

```c
uint16_t msi_ctrl = pci_read_config(dev, capreg + PCIR_MSI_CTRL, 2);
```

宏`PCIR_MSI_CTRL`是`0x02`；完整偏移是`capreg + 0x02`。类似模式适用于每个能力。

对于第18章，这种级别的细节不需要，因为驱动程序不使用MSI。第20章会使用，并使用辅助函数（`pci_alloc_msi`、`pci_alloc_msix`、`pci_enable_msi`、`pci_enable_msix`）来隐藏原始字段访问。这里显示的遍历器主要对诊断和阅读数据手册有用。



## 深入了解配置空间

第1节和第5节介绍了配置空间；本小节补充驱动程序作者应该知道的一些实践细节。

### 配置空间布局

每个PCI配置空间的前64字节是标准化的。布局是：

| 偏移 | 宽度 | 字段 |
|--------|-------|-------|
| 0x00 | 2 | 供应商ID |
| 0x02 | 2 | 设备ID |
| 0x04 | 2 | 命令寄存器 |
| 0x06 | 2 | 状态寄存器 |
| 0x08 | 1 | 修订ID |
| 0x09 | 3 | 类代码（progIF，子类，类） |
| 0x0c | 1 | 缓存行大小 |
| 0x0d | 1 | 延迟定时器 |
| 0x0e | 1 | 头类型 |
| 0x0f | 1 | BIST（内建自测试） |
| 0x10 | 4 | BAR 0 |
| 0x14 | 4 | BAR 1 |
| 0x18 | 4 | BAR 2 |
| 0x1c | 4 | BAR 3 |
| 0x20 | 4 | BAR 4 |
| 0x24 | 4 | BAR 5 |
| 0x28 | 4 | CardBus CIS指针 |
| 0x2c | 2 | 子系统供应商ID |
| 0x2e | 2 | 子系统设备ID |
| 0x30 | 4 | 扩展ROM基址 |
| 0x34 | 1 | 能力列表指针 |
| 0x35 | 7 | 保留 |
| 0x3c | 1 | 中断线 |
| 0x3d | 1 | 中断引脚 |
| 0x3e | 1 | 最小授权 |
| 0x3f | 1 | 最大延迟 |

从0x40到0xff的字节保留用于设备特定用途和遗留能力列表（从存储在`PCIR_CAP_PTR`中的偏移开始）。

PCIe将配置空间扩展到4096字节。从0x100到0xfff的字节持有扩展能力列表，从偏移`0x100`开始并遵循其自己的4字节对齐能力链。

### 头类型

`PCIR_HDRTYPE`（`0x0e`）处的字节区分三种PCI配置头类型：

- `0x00`：标准设备（第18章假设的类型）。
- `0x01`：PCI-to-PCI桥（连接二级总线到主总线的桥）。
- `0x02`：CardBus桥（PC卡桥；日益过时）。

偏移`0x10`之后的布局因头类型而异。标准设备的驱动程序使用偏移`0x10`到`0x24`作为BAR；桥的驱动程序使用相同偏移作为二级总线号、从属总线号和桥特定寄存器。

`PCIR_HDRTYPE`的高位指示多功能设备：如果设置，设备有功能0以外的功能。内核的PCI枚举器使用此位决定是否探测功能1到7。

### 命令和状态

命令寄存器（`PCIR_COMMAND`，偏移`0x04`）持有控制设备PCI级行为的启用位：

- `PCIM_CMD_PORTEN` (0x0001)：启用I/O BAR。
- `PCIM_CMD_MEMEN` (0x0002)：启用内存BAR。
- `PCIM_CMD_BUSMASTEREN` (0x0004)：允许设备发起DMA。
- `PCIM_CMD_SERRESPEN` (0x0100)：报告系统错误。
- `PCIM_CMD_INTxDIS` (0x0400)：禁用传统INTx断言（当驱动程序使用MSI或MSI-X时使用）。

内核在资源激活期间自动设置`MEMEN`和`PORTEN`。如果使用DMA，驱动程序通过`pci_enable_busmaster`设置`BUSMASTEREN`。当驱动程序成功分配了MSI或MSI-X向量并想防止设备也引发传统中断时，设置`INTxDIS`。

状态寄存器（`PCIR_STATUS`，偏移`0x06`）持有粘性位，驱动程序读取以了解PCI级事件：设备收到主中止、目标中止、奇偶校验错误或信令系统错误。关心PCI错误恢复的驱动程序定期或在错误处理程序中读取状态寄存器；不关心的驱动程序（大多数驱动程序，在第18章级别）忽略它。

### 读取比可用宽度更宽的内容

`pci_read_config(dev, offset, width)`接受宽度1、2或4。它从不接受宽度8，即使一些64位字段（64位BAR）存在于配置空间中。读取64位BAR的驱动程序作为两个32位读取：

```c
uint32_t bar_lo = pci_read_config(dev, PCIR_BAR(0), 4);
uint32_t bar_hi = pci_read_config(dev, PCIR_BAR(1), 4);
uint64_t bar_64 = ((uint64_t)bar_hi << 32) | bar_lo;
```

注意这读取*配置空间*BAR，驱动程序在内核分配资源后很少需要。内核的分配返回相同信息作为`struct resource *`，其起始地址通过`rman_get_start`可用。

### 配置空间读取中的对齐

配置空间访问按设计对齐。宽度1的读取可以从任何偏移开始；宽度2的读取必须从偶数偏移开始；宽度4的读取必须从可被4整除的偏移开始。未对齐的访问（例如，在偏移`0x03`处的宽度4读取）不被PCI总线的配置事务支持，在一些实现上将返回未定义值或错误。配置空间前64字节的每个标准字段布局使其自然宽度自然对齐，所以以文档偏移和宽度读取每个字段的驱动程序从不会遇到对齐问题。

读取布局不清楚的供应商特定字段的驱动程序应该以数据手册指定的宽度读取。不要假设16位字段的32位宽读取在高比特中返回定义良好的值。PCI规范要求未使用的字节通道返回零，但谨慎的驱动程序只读取它需要的宽度。

### 写入配置空间：注意事项

配置空间写入的三个注意事项。

首先，某些字段是粘性的：一旦设置，它们不会清除。命令寄存器的`INTxDIS`位是一个例子。向该位写入零并不在所有情况下重新启用传统中断；设备可能锁存禁用状态。需要切换此类位的驱动程序必须写入完整寄存器（读-修改-写），并且可能需要容忍设备忽略清除写入。

其次，某些字段是RW1C（"读-写-1清除"）。向该位写入1清除它；写入0是空操作。状态寄存器的错误位都是RW1C。想清除粘性错误位的驱动程序向该位位置写入1。

第三，某些写入有定时要求。例如，电源管理能力控制寄存器在状态转换后需要10毫秒的稳定时间。写入此类字段的驱动程序必须尊重定时，通常使用`DELAY(9)`或`pause_sbt(9)`调用。

对于第18章的驱动程序，只有探测的ID读取和能力遍历器的读取会触及配置空间。没有进行写入。第19章以后将添加写入（启用中断、清除状态位）；每个写入在引入时会有相关的注意事项说明。



## 深入了解bus_space抽象

第4节在不改变的情况下对真实BAR使用了第16章访问器层。本小节更深入地描述`bus_space`层在底层做了什么以及为什么它重要。

### 什么是bus_space_tag_t

在x86上，`bus_space_tag_t`是一个选择两个地址空间之一的整数：内存（`X86_BUS_SPACE_MEM`）和I/O端口（`X86_BUS_SPACE_IO`）。标签告诉访问器发出哪些CPU指令：内存访问使用普通加载和存储指令；I/O端口访问使用`in`和`out`。

在arm64上，`bus_space_tag_t`是指向函数指针结构（`struct bus_space`）的指针。标签编码的不仅是内存与I/O，还包括字节序和访问粒度等属性。

在每个平台上，标签对驱动程序是不透明的。驱动程序存储它，将其传递给`bus_space_read_*`和`bus_space_write_*`，从不检查其内容。包含`machine/bus.h`引入平台特定定义。

### 什么是bus_space_handle_t

在x86的内存空间上，`bus_space_handle_t`是一个内核虚拟地址。访问器将其解引用为适当宽度的`volatile`指针。

在x86的I/O端口空间上，`bus_space_handle_t`是一个I/O端口号（0到65535）。访问器使用带有端口号的`in`或`out`指令。

在arm64上，`bus_space_handle_t`是内核虚拟地址，类似于x86内存空间。平台的MMU配置为将物理BAR映射到具有设备内存属性的虚拟范围。

句柄对驱动程序也是不透明的。与标签一起，它唯一标识特定资源驻留的地址范围。

### bus_space_read_4内部发生了什么

在x86内存空间中，`bus_space_read_4(tag, handle, offset)`大致扩展为：

```c
static inline uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset)
{
	return (*(volatile uint32_t *)(handle + offset));
}
```

一个volatile指针解引用。`volatile`关键字防止编译器缓存值或将访问重排序超过其他volatile访问。

在x86 I/O端口空间中，实现使用`inl`指令：

```c
static inline uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset)
{
	uint32_t value;
	__asm volatile ("inl %w1, %0" : "=a"(value) : "Nd"(handle + offset));
	return (value);
}
```

标签在两个实现之间选择。在arm64和其他平台上，标签更丰富，实现通过函数指针表分发。

### 为什么抽象重要

使用`bus_space_read_4`和`bus_space_write_4`的驱动程序在每个支持平台上编译为正确的CPU指令。驱动程序作者不需要知道BAR是内存还是I/O；不需要编写平台特定代码；不需要用正确的访问属性注释指针。`bus_space`层处理所有这些。

绕过`bus_space`并解引用原始指针的驱动程序可能在x86上偶然工作（因为内核的pmap层恰好以指针访问工作的方式设置映射）。在arm64上它会失败：设备内存以阻止普通内存访问模式正确工作的属性映射。

教训是：始终使用`bus_space`或包装它的第16章访问器。永远不要解引用指向设备内存的原始指针，即使你知道虚拟地址。

### bus_read与bus_space_read命名

FreeBSD有两族做本质上相同事情的访问器函数。较老的`bus_space_read_*`族接受标签、句柄和偏移。较新的`bus_read_*`族接受`struct resource *`和偏移，并在内部从资源中提取标签和句柄。

较新的族更方便；驱动程序只存储资源，不需要单独存储标签和句柄。较旧的族更灵活；驱动程序可以从头构建标签和句柄（第16章模拟使用的）。

第18章的驱动程序使用较旧的族，因为它继承自第16章。重写可以毫无语义改变地使用较新的族。两族产生相同的输出。本书选择教授标签和句柄的故事，因为它使抽象显式；较新的族隐藏抽象，这对于编写驱动程序更友好，但对于教学不够直观。

参考：较新的族成员命名为`bus_read_4(res, offset)`和`bus_write_4(res, offset, value)`。它们在`/usr/src/sys/sys/bus.h`中定义为内联函数，提取标签和句柄并委托给`bus_space_read_*`和`bus_space_write_*`。



## 动手实验

本章的实验结构为渐进式检查点。每个实验建立在前一个之上。完成所有五个实验的读者将拥有一个完整的PCI驱动程序、一个bhyve测试环境、一个回归脚本和一个小型诊断工具库。实验1和2可以在没有客户机的任何FreeBSD机器上完成；实验3、4和5需要带有virtio-rnd设备的bhyve或QEMU客户机。

每个实验的时间预算假设读者已经阅读相关章节并理解概念。仍在学习的读者应该分配更多时间。

### 实验1：探索你的PCI拓扑

时间：三十分钟到一小时，取决于你想花多少时间理解。

目标：在你自己的系统上建立对PCI的直觉。

步骤：

1. 在你的实验主机上运行`pciconf -lv`并将输出重定向到文件：`pciconf -lv > ~/pci-inventory.txt`。
2. 计算设备数：`wc -l ~/pci-inventory.txt`。除以一个估计值（通常输出中每个设备5行）得到大概的设备计数。
3. 从清单中识别以下类别的设备：
   - 主桥（类 = bridge，子类 = HOST-PCI）
   - PCI-PCI桥（类 = bridge，子类 = PCI-PCI）
   - 网络控制器（类 = network）
   - 存储控制器（类 = mass storage）
   - USB主控制器（类 = serial bus，子类 = USB）
   - 显卡（类 = display）
4. 对于以上每一项，注意：
   - 设备的`name@pciN:B:D:F`字符串。
   - 供应商和设备ID。
   - 驱动程序绑定（查看前面的名称，在`@`之前）。
5. 选择一个PCI设备（任何未声明的设备，显示为`none@...`的效果最好）。记下其B:D:F。
6. 运行`devinfo -v | grep -B 1 -A 5 <B:D:F>`并注意资源。
7. 将资源列表与`pciconf -lv`条目中的BAR信息进行比较。

预期观察：

- 现代系统上的大多数设备在`pci0`（主总线）或PCIe桥后面的总线上。你的机器可能有三到十条可见的总线。
- 每个设备至少有供应商和设备ID。许多有子供应商和子系统ID。
- 大多数设备绑定到驱动程序。一些（特别是在笔记本电脑上，制造商发布FreeBSD尚不支持的硬件）未被声明。
- `devinfo -v`中的资源列表与`pciconf -lv`中可见的BAR信息匹配。地址是固件分配的。

这个实验是关于建立词汇量。没有代码。没有驱动程序。只是阅读。

### 实验2：在纸上编写探测骨架

时间：一到两小时。

目标：通过手写内化探测-附加-分离序列。

步骤：

1. 在编辑器中打开一个空文件`myfirst_pci_sketch.c`。
2. 不看第2节的完成代码，编写：
   - 一个`myfirst_pci_id`结构。
   - 一个`myfirst_pci_ids[]`表，有一个假设供应商`0x1234`和设备`0x5678`的条目。
   - 一个匹配表的`myfirst_pci_probe`函数。
   - 一个打印`device_printf(dev, "attached\n")`的`myfirst_pci_attach`函数。
   - 一个打印`device_printf(dev, "detached\n")`的`myfirst_pci_detach`函数。
   - 一个带有探测、附加、分离的`device_method_t`表。
   - 一个带有驱动程序名称的`driver_t`。
   - 一个`DRIVER_MODULE`行。
   - 一个`MODULE_DEPEND`行。
   - 一个`MODULE_VERSION`行。
3. 将你的草图与第2节代码比较。注意每个差异。
4. 对于每个差异，问：是我错了，还是因为某种原因工作方式不同？
5. 更新你的草图以匹配第2节代码中你错的部分。

预期结果：

- 你可能会在第一遍忘记`MODULE_DEPEND`和`MODULE_VERSION`。
- 你可能在探测中使用`0`而不是`BUS_PROBE_DEFAULT`（常见初学者错误）。
- 你可能忘记探测中的`device_set_desc`调用。
- 你可能在附加和分离中使用`printf`而不是`device_printf`。
- 你可能忘记方法表末尾的`DEVMETHOD_END`。

这些每一个都是产生真实bug的真实错误。在自己的草图中而不是在凌晨两点的编译驱动程序中发现它们是实验的意义所在。

### 实验3：在bhyve客户机中加载第1阶段驱动程序

时间：两到三小时，包括设置客户机（如果你还没有）。

目标：观察探测-附加-分离序列的实际运行。

步骤：

1. 如果你还没有运行FreeBSD 14.3的bhyve客户机，设置一个。规范的配方在`/usr/share/examples/bhyve/`或FreeBSD手册中。在客户机的bhyve命令行中包含一个`virtio-rnd`设备：`-s 4:0,virtio-rnd`。
2. 在客户机内部，列出PCI设备：`pciconf -lv | grep -B 1 -A 2 0x1005`。注意virtio-rnd条目是否绑定到`virtio_random`（前面的`virtio_random0@...`），`none`（未声明），还是完全缺失（检查你的bhyve命令行）。
3. 将第18章第1阶段源代码复制到客户机（scp、共享文件系统或任何你喜欢的方法）。
4. 在客户机内部，在驱动程序的源目录中：`make`。验证生成了`myfirst.ko`。
5. 如果`virtio_random`在第2步声称了设备，卸载它：`sudo kldunload virtio_random`。如果设备已经是未声明的（`none`），跳过这一步。
6. 加载`myfirst`：`sudo kldload ./myfirst.ko`。
7. 检查附加：`dmesg | tail -10`。你应该看到第1阶段附加横幅。
8. 检查设备：`devinfo -v | grep -B 1 -A 3 myfirst`。你应该看到`myfirst0`作为`pci0`的子设备。
9. 检查绑定：`pciconf -lv | grep myfirst`。你应该看到以`myfirst0`为设备名称的条目。
10. 卸载驱动程序：`sudo kldunload myfirst`。
11. 检查分离：`dmesg | tail -5`。你应该看到第1阶段分离横幅。
12. 如果你在第5步卸载了`virtio_random`并想恢复它：`sudo kldload virtio_random`。

预期结果：

- 每一步都产生预期输出。
- 如果第7步的`dmesg`不显示附加横幅，驱动程序没有探测到设备。检查你是否卸载了任何其他可能声称它的驱动程序。
- 如果第7步显示附加横幅但第8步不显示`myfirst0`，newbus中有一个记账bug；不太可能，但如果你看到它值得报告。
- 如果第10步以`Device busy`失败，驱动程序的分离返回`EBUSY`。在第1阶段没有打开的cdev；失败是意外的。检查分离代码。

这个实验是读者的驱动程序第一次遇到真实设备。情感回报是真实的：`dmesg`中的`myfirst0: attaching`是驱动程序工作的证明。

### 实验4：声明BAR并读取寄存器

时间：两到三小时。

目标：将第1阶段驱动程序扩展到第2阶段（BAR分配）和第3阶段（首次真实寄存器读取）。

步骤：

1. 从实验3的第1阶段驱动程序开始，编辑`myfirst_pci.c`添加第2阶段的BAR分配。编译。加载。在`dmesg`中验证BAR分配横幅。
2. 验证资源可见：`devinfo -v | grep -A 3 myfirst0`应该显示内存资源。
3. 卸载。验证分离干净地释放了BAR。
4. 再次编辑`myfirst_pci.c`添加第3阶段的能力遍历和首次寄存器读取。编译。加载。在`dmesg`中验证能力输出。
5. 验证`CSR_READ_4`通过读取BAR的前四个字节并与`pciconf -r myfirst0 0x00:4`的前四个字节比较来操作真实BAR。（这些是不同的；一个是配置空间，另一个是BAR。比较的要点是两者都产生合理的值而不崩溃。）
6. 运行第7节的完整回归脚本。验证它无错误完成。

预期结果：

- BAR分配成功且资源在`devinfo -v`中可见。
- 能力遍历可能对virtio-rnd设备显示零偏移（遗留布局不像现代设备那样有PCI能力）；这是正常的。
- 首次寄存器读取返回非零值；确切值取决于设备当前状态。

如果任何步骤产生崩溃或页面错误，参考第7节的常见错误并重新检查每一步对照第3节的分配纪律和第4节的标签和句柄代码。

### 实验5：操作cdev并验证分离清理

时间：两到三小时。

目标：证明完整的第18章驱动程序端到端工作。

步骤：

1. 从实验4的第3阶段驱动程序开始，编写一个小的用户空间程序（`myfirst_test.c`）打开`/dev/myfirst0`，读取最多64字节，写入16字节，并关闭设备。
2. 编译并运行程序。观察输出。确保没有内核消息报告错误。
3. 在第二个终端中，用`dmesg -w`追踪`dmesg`。
4. 多次运行程序，观察任何警告或错误。
5. 用`devctl detach myfirst0; devctl attach pci0:0:4:0`运行分离-附加循环十次。验证`dmesg`每个循环显示干净的附加和分离横幅。
6. 循环后，运行`vmstat -m | grep myfirst`并验证`myfirst` malloc类型有零活动分配。
7. 卸载驱动程序。验证`kldstat -v | grep myfirst`返回空。
8. 重新加载驱动程序。验证附加再次触发。

预期结果：

- 每一步都成功。
- 第6步的`vmstat -m`检查是最重要的。如果它显示分离循环后有活动分配，有需要修复的泄漏。
- 附加-分离-重新附加循环是稳定的。驱动程序可以无限期地绑定、解绑、重新绑定。

这个实验是回归证明。十次通过实验5没有问题的驱动程序是第19章可以安全扩展的驱动程序。

### 实验总结

五个实验总共花费十到十五小时。它们产生一个完整的PCI驱动程序、一个工作的测试环境、一个回归脚本和一个小型工具箱，读者可以在后续章节中重用。完成了所有五个实验的读者已经做了相当于把本章读两遍的动手实践：概念建立在运行、修复和观察的代码基础上。

如果任何实验受阻（BAR分配失败、能力遍历产生错误、分离泄漏资源），停下来诊断。本章末尾的故障排除部分覆盖常见失败模式。实验经过校准可以工作；如果实验不工作，要么实验有微妙错误（罕见），要么读者的环境有与作者不同的细节（常见得多）。无论哪种情况，诊断才是真正学习发生的地方。



## 挑战练习

挑战建立在实验基础上。每个挑战是可选的：本章没有它们也是完整的。但完成它们的读者将巩固所学内容并以章节未涉及的方式扩展驱动程序。

### 挑战1：支持第二个供应商和设备ID

扩展`myfirst_pci_ids[]`添加第二个条目。目标是不同的bhyve模拟设备：`virtio-blk`（供应商`0x1af4`，设备`0x1001`）或`virtio-net`（`0x1af4`，`0x1000`）。卸载相应的基础系统驱动程序（`virtio_blk`或`virtio_net`），加载`myfirst`，并验证附加找到新设备。

这个练习在代码上很琐碎（一个表条目）但锻炼读者对如何做出探测决定的理解。更改后，如果卸载驱动程序，两个virtio设备都将被`myfirst`认领。

### 挑战2：打印完整能力链

扩展`myfirst_pci_attach`中的能力遍历代码打印列表中的每个能力，不只是驱动程序知道的那些。从`PCIR_CAP_PTR`开始遍历遗留能力列表并跟随`next`指针；对于每个能力，打印ID和偏移。对从偏移`0x100`开始的扩展能力列表做同样的事。

这个练习超出了本章对`pci_find_cap`的处理。它需要阅读`/usr/src/sys/dev/pci/pcireg.h`以找到能力和扩展能力头的布局。典型virtio-rnd设备上的输出可能稀疏；在真实硬件PCIe设备上更丰富。

### 挑战3：为配置空间访问实现简单ioctl

扩展cdev的`ioctl`入口点接受读取配置空间的请求。定义新的`ioctl`命令`MYFIRST_IOCTL_PCI_READ_CFG`，接受`{ offset, width }`输入并返回`uint32_t`值。实现在`sc->mtx`下调用`pci_read_config`。

编写一个用户空间程序使用新`ioctl`逐字节读取配置空间的前16字节并打印它们。

这个练习向读者介绍自定义ioctl，这是向用户空间暴露驱动程序特定行为的常见模式，无需添加新系统调用。

### 挑战4：如果BAR太小则拒绝附加

第18章驱动程序假设BAR 0至少有`MYFIRST_REG_SIZE`（64）字节。具有相同供应商和设备ID的不同设备可能暴露更小的BAR。扩展附加路径读取`rman_get_size(sc->bar_res)`，与`MYFIRST_REG_SIZE`比较，如果BAR太小则拒绝附加（清理后返回`ENXIO`）。

通过人为将`MYFIRST_REG_SIZE`设置为大于实际BAR大小的值来验证行为。驱动程序应该拒绝附加且`dmesg`应该打印信息性消息。

### 挑战5：将驱动程序拆分为两个模块

使用第5节概述的技术，将驱动程序拆分为`myfirst_core.ko`（硬件层、模拟、cdev、锁）和`myfirst_pci.ko`（PCI附加）。添加`MODULE_DEPEND(myfirst_pci, myfirst_core, 1, 1, 1)`声明。验证`kldload myfirst_pci`自动加载`myfirst_core`作为依赖。

这个练习是一个适度的重构。它向读者介绍跨模块符号可见性（哪些函数需要从`myfirst_core`导出到`myfirst_pci`）和模块加载器的依赖解析。结果是驱动程序的通用机制与其PCI特定附加之间的干净分离。

### 挑战6：使用类和子类匹配重新实现探测

不是按供应商和设备ID匹配，而是扩展探测例程也按类和子类匹配。例如，匹配类`PCIC_BASEPERIPH`（基础外设）中子类匹配所选值的任何设备。当基于类的匹配成功但没有供应商/设备特定条目匹配时，返回`BUS_PROBE_GENERIC`（较低优先级匹配）。

这个练习教读者驱动程序如何共存。供应商特定匹配胜过类匹配（通过返回`BUS_PROBE_DEFAULT` vs `BUS_PROBE_GENERIC`）。后备驱动程序可以声明没有特定驱动程序识别的设备。

### 挑战7：添加报告驱动程序PCI状态的只读sysctl

添加sysctl `dev.myfirst.N.pci_info`返回描述驱动程序PCI附加的短字符串：供应商和设备ID、子供应商和子系统、B:D:F以及BAR大小和地址。使用`sbuf_printf`格式化字符串。

结果是驱动程序对设备视图的用户空间可读转储。这对诊断有用并成为更复杂设备的驱动程序重用的模式。

### 挑战8：模拟失败的附加

引入sysctl `hw.myfirst.fail_attach`，当设置为1时，导致附加在声明BAR后失败。验证goto级联正确清理，附加失败后`vmstat -m | grep myfirst`显示零泄漏。

这个练习运行第6节描述但实验序列没有显式测试的部分失败路径。这是确认展开级联正确的最佳方式。

### 挑战总结

八个挑战，涵盖一系列难度。完成四或五个的读者已经显著加深了理解。完成全部八个的读者本质上编写了第二个第18章。

保存你的解决方案。其中几个（挑战1、挑战3、挑战7）是第19章扩展的自然起点。



## 故障排除和常见错误

本节整合读者在第18章实验中可能遇到的常见失败模式。每个条目命名症状、可能原因和修复方法。

### "驱动程序不附加；dmesg无横幅"

症状：`kldload ./myfirst.ko`返回成功。`dmesg | tail`没有显示来自`myfirst`的任何内容。`devinfo -v`没有列出`myfirst0`。

可能原因：

1. 另一个驱动程序已经认领了目标设备。检查`pciconf -lv`找到设备并查看哪个驱动程序（如果有）绑定了。如果`virtio_random0`拥有virtio-rnd设备，探测优先级平局归`virtio_random`，`myfirst`从不附加。修复：先`kldunload virtio_random`。

2. `myfirst_pci_ids[]`中的供应商或设备ID错误。对照客户机的实际设备检查。修复：更正ID。

3. 探测例程有总是返回`ENXIO`的bug。检查比较是比较`vendor`和`device`与表条目，而不是与它们自己比较。修复：仔细重读探测代码。

4. `DRIVER_MODULE`声明缺失或错误。检查第三个参数是`driver_t`且第二个是`"pci"`。修复：更正声明。

### "kldload导致内核崩溃"

症状：`kldload ./myfirst.ko`在返回前崩溃内核。

可能原因：

1. 缺少`MODULE_DEPEND(myfirst, pci, ...)`。驱动程序尝试针对尚未初始化的总线注册。修复：添加声明。

2. 驱动程序的初始化调用了模块加载时不存在的函数。罕见，但如果驱动程序定义了在总线准备好之前访问`device_*`函数的`MOD_LOAD`处理程序，可能发生。

3. `driver_t`中声明的softc大小错误。如果附加代码期望不在声明结构中的字段，内核会写越分配的块并崩溃。修复：确保`sizeof(struct myfirst_softc)`匹配结构定义。

调试内核很擅长捕获这三者；`ddb`中的回溯会命名崩溃发生的函数。

### "BAR分配以NULL失败"

症状：`bus_alloc_resource_any`返回NULL。`dmesg`说"cannot allocate BAR0"。

可能原因：

1. 错误的`rid`。BAR 0用`PCIR_BAR(0)`，不是`0`。修复：使用宏。

2. 错误的类型。如果设备的BAR 0是I/O端口（配置空间中BAR的低位置位），传递`SYS_RES_MEMORY`会失败。用`pci_read_config(dev, PCIR_BAR(0), 4)`读取BAR值并检查低位。修复：使用正确的类型。

3. BAR已被另一个驱动程序或BIOS分配。在bhyve客户机上不太可能；在BIOS配置错误的真机上可能。修复：在`devinfo -v`中检查声明的资源。

4. 缺少`RF_ACTIVE`标志。资源被分配但未激活。句柄对`bus_space`访问不可用。修复：添加`RF_ACTIVE`。

### "CSR_READ_4返回0xffffffff"

症状：寄存器读取返回全1。读者期望非零值。

可能原因：

1. BAR未激活。检查`bus_alloc_resource_any`调用中的`RF_ACTIVE`。

2. 标签和句柄交换。读`rman_get_bustag`返回标签；`rman_get_bushandle`返回句柄。以错误顺序传递给`bus_space_read_4`产生未定义行为。

3. 偏移错误。BAR是32字节；在偏移64处读取超出末尾。调试内核的`myfirst_reg_read`中的`KASSERT`捕获这一点。

4. 设备已复位或断电。一些设备关闭时返回全1。用`pci_read_config(dev, PCIR_COMMAND, 2)`读取命令寄存器；如果返回`0xffff`，设备无响应。

### "kldunload返回Device busy"

症状：`kldunload myfirst`以`Device busy`失败。

可能原因：

1. 一个用户空间进程打开了`/dev/myfirst0`。关闭进程。用`fstat /dev/myfirst0`检查。

2. 驱动程序有正在执行的命令（模拟callout、任务队列工作）。等待几秒并重试。

3. 分离函数错误地无条件返回`EBUSY`。检查分离代码。

4. 驱动程序的忙碌检查有对未初始化字段的陈旧引用。检查没有描述符打开时`sc->open_count`为零。

### "dmesg说'detach中清理失败'"

症状：`dmesg`显示来自分离路径的警告。

可能原因：

1. 分离运行时callout仍被调度。检查在驱动程序的softc清理前调用了`callout_drain`。

2. 任务队列工作项仍待处理。检查调用了`taskqueue_drain`。

3. 分离时cdev打开。`destroy_dev`调用应该阻塞直到关闭，但如果驱动程序先释放其他资源，关闭会找到陈旧状态。修复顺序：在释放依赖资源前销毁cdev。

### "ioctl或read返回意外错误"

症状：用户空间系统调用返回读者未预期的错误（EINVAL、ENODEV、ENXIO等）。

可能原因：

1. cdev的入口点检查驱动程序未设置的状态。例如：第10章驱动程序检查`sc->is_attached`；第18章驱动程序可能忘记设置它。

2. 用户空间的ioctl命令号与驱动程序中的不匹配。检查`_IOR`/`_IOW`/`_IOWR`宏并确认类型相同。

3. 锁顺序错误。cdev入口点以与某些其他代码冲突的顺序获取锁。调试内核上的`WITNESS`报告这一点。

### "vmstat -m显示泄漏的分配"

症状：加载-卸载循环后，`vmstat -m | grep myfirst`显示非零"Allocations"或"InUse"。

可能原因：

1. 附加中的malloc在分离中未释放。通常是硬件层包装结构或sysctl缓冲区。

2. callout未被排空。callout分配一个小结构；如果它在分离后运行，结构泄漏。

3. `M_MYFIRST` malloc类型用于softc。Newbus自动释放softc；驱动程序不应在附加时`malloc(M_MYFIRST, sizeof(softc))`。softc由newbus分配。

### "pci_find_cap对我知道设备有的能力返回ENOENT"

症状：`pci_find_cap(dev, PCIY_EXPRESS, &capreg)`返回`ENOENT`，但设备是PCIe设备应该有PCI Express能力。

可能原因：

1. 设备是PCIe插槽中的遗留PCI设备（它工作是因为PCIe向后兼容PCI）。遗留设备没有PCI Express能力。通过读取`pci_get_class(dev)`并与预期比较来检查。

2. 能力列表损坏或空。用`pci_read_config(dev, PCIR_CAP_PTR, 1)`直接读取`PCIR_CAP_PTR`；如果返回零，设备没有实现能力。

3. 错误的能力ID。`PCIY_EXPRESS`是`0x10`，不是`0x1f`。检查`pcireg.h`找到正确的常量。

4. 状态寄存器的`PCIM_STATUS_CAPPRESENT`位为零。此位告诉PCI子系统设备实现了能力列表。没有它，列表不存在。该位在`PCIR_STATUS`中。

### "模块卸载，但dmesg显示卸载期间页面错误"

症状：`kldunload myfirst`看起来成功，但`dmesg`显示卸载期间发生的页面错误。

可能原因：

1. callout在`myfirst_hw_detach`后但在驱动程序返回前触发。callout访问了已被设为NULL的`sc->hw`。修复：确保在`myfirst_hw_detach`前调用`callout_drain`。

2. 任务队列工作项在资源释放后运行。修复：确保在释放任务触及的任何东西前调用`taskqueue_drain`。

3. 用户空间进程仍打开`/dev/myfirst0`。`destroy_dev`调用快速完成，但任何针对cdef的未完成I/O继续直到进程关闭描述符或退出。修复：确保所有用户空间消费者在分离前关闭cdev；在紧急情况下，`devctl detach`后杀死进程有效。

### "devinfo -v显示驱动程序附加但cdev不出现"

症状：`devinfo -v | grep myfirst`显示`myfirst0`，但`ls /dev/myfirst*`返回空。

可能原因：

1. `make_dev`调用失败且附加未检查返回值。在`make_dev`后检查`sc->cdev`；如果是NULL，调用失败。

2. cdev名称不是`myfirst%d`。检查`make_dev`调用的格式字符串。设备节点路径使用传递给`make_dev`的确切字符串。

3. `cdevsw`结构未注册或方法错误。检查`myfirst_cdevsw`正确初始化。

4. 陈旧的`/dev`条目隐藏新条目。尝试`sudo devfs rule -s 0 apply`或重启。在现代FreeBSD上不太可能但在边缘情况可能。

### "附加花费很长时间返回"

症状：`kldload ./myfirst.ko`挂起数秒或数分钟。

可能原因：

1. 附加中的`DELAY`或`pause_sbt`调用太长。检查能力遍历或设备启动中的隐藏延迟。

2. `bus_alloc_resource_any`调用被阻塞在另一个驱动程序分配的资源上。在PCI上罕见；在I/O端口空间有限的平台上更常见。

3. 能力遍历器中的无限循环。格式错误的设备可能产生循环；遍历器中的safety计数器防止这一点。

4. `callout_init_mtx`调用正在等待另一个代码路径持有的锁。死锁；检查`dmesg`中的`WITNESS`输出。

### "启动时驱动程序附加但前几秒不产生输出"

症状：带有启动时加载的`myfirst`的客户机重启后，驱动程序附加但花费数秒产生任何日志输出。

可能原因：

1. 模块在启动早期加载，在控制台完全初始化之前。消息在内核缓冲区中但尚未写入控制台。检查`dmesg`找消息；它们应该存在。

2. callout被调度但尚未触发。第17章的传感器callout每秒运行一次；第一个滴答在附加后一秒。

3. 驱动程序正在等待需要时间完成的条件。不是第18章的问题，但在等待设备完成复位的驱动程序中可能。

### "第一次附加失败后第二次附加尝试成功"

症状：配置错误的内核上`kldload`失败；修复配置后第二次`kldload`成功。这实际上是预期行为。

可能原因：内核的模块加载器在加载尝试之间是无状态的。失败的加载移除任何部分状态。后续加载用新鲜状态再试。症状不是bug。

### "每次加载-卸载循环后vmstat -m InUse增长"

症状：`myfirst` malloc类型显示每次循环少量字节的`InUse`内存增加。

可能原因：

1. 附加或分离中的泄漏太小在单个循环中不明显但会累积。运行100次循环并观察增长。

2. `myfirst_hw`或`myfirst_sim`包装结构被分配但未释放。检查分离路径调用`myfirst_hw_detach`和`myfirst_sim_detach`（如果加载了模拟）。

3. sysctl处理程序中的字符串或类似小分配泄漏。检查sysctl处理程序中创建但未删除的`sbuf`。

`vmstat -m`输出有`Requests`、`InUse`、`MemUse`列。`Requests`是曾经进行的分配总数。`InUse`是当前分配的数量。`MemUse`是总字节数。健康的驱动程序在分离和卸载后`InUse`回到零。

### 故障排除总结

这些失败每一个都是可恢复的。调试内核（带有`INVARIANTS`、`WITNESS`和`KDB`）用有用的消息捕获大多数。运行调试内核并仔细阅读消息的读者会在不到一小时内在大多数第18章bug中修复它。

如果bug难以解决，下一步是再次阅读本章相关部分。上面的故障排除列表很短，因为本章的教学经过精心设计以防止这些失败。当失败发生时，问题通常是"我违反了哪一节的纪律？"答案通常在第二次阅读时显而易见。

### 调试内核检查清单

如果你认真对待驱动程序开发和调试，构建一个调试内核。可靠捕获PCI驱动程序bug的配置选项是：

```text
options INVARIANTS
options INVARIANT_SUPPORT
options WITNESS
options WITNESS_SKIPSPIN
options DEBUG_VFS_LOCKS
options DEBUG_MEMGUARD
options DIAGNOSTIC
options DDB
options KDB
options KDB_UNATTENDED
options MALLOC_DEBUG_MAXZONES=8
```

在所有这些启用的内核下通过回归测试的驱动程序是很少产生生产bug的驱动程序。运行时成本是显著的（内核更慢，特别是`WITNESS`给每次锁操作增加可测量的开销），但调试价值巨大。

构建调试内核：

```sh
cd /usr/src
sudo make buildkernel KERNCONF=GENERIC-DEBUG
sudo make installkernel KERNCONF=GENERIC-DEBUG
sudo shutdown -r now
```

使用调试内核进行所有驱动程序开发；仅在性能基准测试时切回`GENERIC`。



## 收尾

第18章将模拟驱动程序变成了PCI驱动程序。起点是`1.0-simulated`，一个带有`malloc(9)`支持寄存器块和第17章模拟使寄存器呼吸的模块。终点是`1.1-pci`，相同模块带有一个新文件（`myfirst_pci.c`）、一个新头文件（`myfirst_pci.h`）和现有文件的一小部分扩展。访问器层没有改变。命令-响应协议没有改变。锁纪律没有改变。改变的是访问器使用的标签和句柄的来源。

过渡经过八节。第1节作为概念介绍了PCI，涵盖拓扑、B:D:F元组、配置空间、BAR、供应商和设备ID以及`pci(4)`子系统。第2节编写了探测-附加-分离骨架，通过`DRIVER_MODULE(9)`和`MODULE_DEPEND(9)`绑定到PCI总线。第3节解释了什么是BAR并通过`bus_alloc_resource_any(9)`声明一个。第4节将声明的BAR连接到第16章访问器层，完成从模拟到真实寄存器访问的过渡。第5节添加了附加时管道：用`pci_find_cap(9)`和`pci_find_extcap(9)`进行能力发现、cdev创建以及让第17章模拟在PCI路径上保持非活动的纪律。第6节用严格的逆序、忙碌检查、callout和任务排空以及部分失败恢复巩固了分离路径。第7节在bhyve或QEMU客户机中测试了驱动程序，演练了驱动程序暴露的每条路径。第8节将代码重构为最终形状并记录结果。

第18章没有做的是中断处理。bhyve下的virtio-rnd设备有中断线；我们的驱动程序没有为它注册处理程序；设备内部状态变化不到达驱动程序。cdef仍然可达，但数据路径在PCI构建上没有活跃的生产者（第17章模拟callout没有运行）。第19章引入真正的处理程序，它将给数据路径一个生产者。

第18章完成的是跨越一个门槛。到第17章末尾，`myfirst`驱动程序是一个教学模块：它存在是因为我们加载它，不是因为任何设备需要它。从第18章开始，驱动程序是一个PCI驱动程序：它存在是因为内核枚举了一个设备而我们的探测说是。newbus机制现在承载驱动程序。每个后续第四部分章节扩展它而不改变这种基本关系。

文件布局已增长：`myfirst.c`、`myfirst_hw.c`、`myfirst_hw.h`、`myfirst_sim.c`、`myfirst_sim.h`、`myfirst_pci.c`、`myfirst_pci.h`、`myfirst_sync.h`、`cbuf.c`、`cbuf.h`、`myfirst.h`。文档已增长：`HARDWARE.md`、`LOCKING.md`、`SIMULATION.md`、`PCI.md`。测试套件已增长：bhyve或QEMU设置脚本、回归脚本、小型用户空间测试程序。每一个都是一个层；每一个在特定章节引入，现在是驱动程序故事的永久部分。

### 第19章前的反思

下一章前的暂停。第18章教授了PCI子系统和newbus附加舞蹈。你在这里练习的模式（探测-附加-分离、资源声明-释放、标签-句柄提取、能力发现）是你整个驱动程序编写生涯中都会用到的模式。它们适用于第21章的USB附加舞蹈，就像你刚刚编写的PCI舞蹈一样，适用于你可能为真实网卡编写的NIC驱动程序，就像你刚刚扩展的演示驱动程序一样。PCI技能是永久的。

第18章还教授了分离时严格逆序的纪律。附加中的goto级联、镜像分离、忙碌检查、静默步骤：这些是让驱动程序在其生命周期中不泄漏资源的模式。它们适用于每种驱动程序，不仅是PCI。内化了第18章分离纪律的读者将编写更干净的第19章、第20章和第21章代码。

还有一个观察。第16章访问器层的回报现在可见。编写了第16章访问器并想知道"这值得吗？"的读者可以看第18章第3阶段附加并看到答案。驱动程序的上层代码（每个使用`CSR_READ_4`、`CSR_WRITE_4`或`CSR_UPDATE_4`的调用点）在后端从模拟切换到真实PCI时完全没有改变。这就是一个好的抽象所买到的：下层的一个大变化在上层花费零变化。第16章访问器是抽象。第18章是证明。

### 如果你被卡住该怎么办

两个建议。

首先，关注第7节的回归脚本。如果脚本端到端运行没有错误，驱动程序在工作；对内部细节的每一个困惑都是装饰性的。如果脚本失败，第一个失败的步骤是调试的起点。

第二，打开`/usr/src/sys/dev/uart/uart_bus_pci.c`慢慢阅读。文件有366行。每一行都是第18章教授或引用的模式。在第18章后阅读它应该感觉熟悉：探测、附加、分离、ID表、`DRIVER_MODULE`、`MODULE_DEPEND`。在第18章后发现该文件可读的读者已经取得了本章的真正进步。

第三，第一遍跳过挑战。实验为第18章校准；挑战假设章节材料已经稳固。如果现在觉得遥不可及，在第19章后再回来。

第18章的目标是让驱动程序遇到真实硬件。如果它已经做到了，第四部分的其余部分将感觉像自然的进展：第19章添加中断，第20章添加MSI和MSI-X，第20章和第21章添加DMA。每一章扩展第18章建立的内容。



## 通向第19章的桥梁

第19章标题为*处理中断*。其范围是第18章故意没有涉及的主题：让设备异步告诉驱动程序发生了什么的路径。第17章的模拟使用callout产生自主状态变化。第18章的真实PCI驱动程序完全忽略设备的中断线。第19章通过`bus_setup_intr(9)`注册处理程序，将其附加到通过`bus_alloc_resource_any(9)`与`SYS_RES_IRQ`分配的IRQ资源，并教会驱动程序对设备自己的信号做出反应。

第18章以四个具体方式准备了基础。

首先，**你有一个PCI附加的驱动程序**。第18章`1.1-pci`的驱动程序分配一个BAR，声明一个内存资源，并让每个newbus钩子到位。第19章添加一个更多资源（一个IRQ）和一对更多调用（`bus_setup_intr`和`bus_teardown_intr`）。附加和分离流程的其余部分保持原位。

第二，**你有一个可以从中断上下文调用的访问器层**。第16章访问器持有`sc->mtx`；需要读或写寄存器的中断处理程序获取`sc->mtx`并调用`CSR_READ_4`或`CSR_WRITE_4`。第19章处理程序将与访问器组合而无需任何新管道。

第三，**你有一个容纳IRQ拆除的分离顺序**。第18章分离在序列中的特定点释放BAR；第19章分离将在释放BAR之前释放IRQ资源。goto级联扩展一个标签；模式不改变。

第四，**你有一个产生中断的测试环境**。带有virtio-rnd设备的bhyve或QEMU客户机是第19章使用的相同环境；virtio-rnd设备的中断线是第19章处理程序接收的内容。不需要新的实验设置。

第19章将涵盖的具体主题：

- 什么是中断，与轮询callout对比。
- FreeBSD中断处理程序的两阶段模型：过滤器（快，在中断上下文）和ithread（慢，在内核线程上下文）。
- 带`SYS_RES_IRQ`的`bus_alloc_resource_any(9)`。
- `bus_setup_intr(9)`和`bus_teardown_intr(9)`。
- `INTR_TYPE_*`和`INTR_MPSAFE`标志。
- 中断处理程序可以做什么和不可以做什么（不能睡眠，不能阻塞锁，不能`malloc(M_WAITOK)`）。
- 中断时读取状态寄存器以决定发生了什么。
- 清除中断标志以防止重入。
- 安全记录中断。
- 中断与第16章访问日志的交互。
- 一个最小中断处理程序，递增计数器并记录。

你不需要提前阅读。第18章是足够的准备。带上你`1.1-pci`的`myfirst`驱动程序、你的`LOCKING.md`、`HARDWARE.md`、`SIMULATION.md`、新的`PCI.md`、启用`WITNESS`的内核和回归脚本。第19章从第18章结束的地方开始。

第20章还差两章；值得简短的前向指针。MSI和MSI-X将替换单一遗留中断线为更丰富的路由机制：分离任务有分离向量，中断聚合，每队列亲和性。`pci_alloc_msi(9)`和`pci_alloc_msix(9)`函数是第18章介绍的PCI子系统的一部分；我们把它们留给第20章，因为MSI-X特别需要对中断处理比第18章准备引入的更深入理解。如果读者在能力遍历中看了`PCIY_MSI`和`PCIY_MSIX`偏移并想知道它们是什么，第20章是答案。

硬件对话正在深化。词汇是你的；协议是你的；纪律是你的。第19章添加下一个缺失的部分。



## 参考：本章使用的PCI头偏移

第18章引用的配置空间偏移的紧凑参考，来自`/usr/src/sys/dev/pci/pcireg.h`。编写PCI代码时保持此参考在手。

| 偏移 | 宏 | 宽度 | 含义 |
|--------|-------|-------|---------|
| 0x00 | `PCIR_VENDOR` | 2 | 供应商ID |
| 0x02 | `PCIR_DEVICE` | 2 | 设备ID |
| 0x04 | `PCIR_COMMAND` | 2 | 命令寄存器 |
| 0x06 | `PCIR_STATUS` | 2 | 状态寄存器 |
| 0x08 | `PCIR_REVID` | 1 | 修订ID |
| 0x09 | `PCIR_PROGIF` | 1 | 编程接口 |
| 0x0a | `PCIR_SUBCLASS` | 1 | 子类 |
| 0x0b | `PCIR_CLASS` | 1 | 类 |
| 0x0c | `PCIR_CACHELNSZ` | 1 | 缓存行大小 |
| 0x0d | `PCIR_LATTIMER` | 1 | 延迟定时器 |
| 0x0e | `PCIR_HDRTYPE` | 1 | 头类型 |
| 0x0f | `PCIR_BIST` | 1 | 内建自测试 |
| 0x10 | `PCIR_BAR(0)` | 4 | BAR 0 |
| 0x14 | `PCIR_BAR(1)` | 4 | BAR 1 |
| 0x18 | `PCIR_BAR(2)` | 4 | BAR 2 |
| 0x1c | `PCIR_BAR(3)` | 4 | BAR 3 |
| 0x20 | `PCIR_BAR(4)` | 4 | BAR 4 |
| 0x24 | `PCIR_BAR(5)` | 4 | BAR 5 |
| 0x2c | `PCIR_SUBVEND_0` | 2 | 子系统供应商 |
| 0x2e | `PCIR_SUBDEV_0` | 2 | 子系统设备 |
| 0x34 | `PCIR_CAP_PTR` | 1 | 能力列表开始 |
| 0x3c | `PCIR_INTLINE` | 1 | 中断线 |
| 0x3d | `PCIR_INTPIN` | 1 | 中断引脚 |

### 命令寄存器位

| 位 | 宏 | 含义 |
|-----|-------|---------|
| 0x0001 | `PCIM_CMD_PORTEN` | 启用I/O空间 |
| 0x0002 | `PCIM_CMD_MEMEN` | 启用内存空间 |
| 0x0004 | `PCIM_CMD_BUSMASTEREN` | 启用总线主控 |
| 0x0008 | `PCIM_CMD_SPECIALEN` | 启用特殊周期 |
| 0x0010 | `PCIM_CMD_MWRICEN` | 内存写无效 |
| 0x0020 | `PCIM_CMD_PERRESPEN` | 奇偶校验错误响应 |
| 0x0040 | `PCIM_CMD_SERRESPEN` | SERR#启用 |
| 0x0400 | `PCIM_CMD_INTxDIS` | 禁用INTx生成 |

### 能力ID（遗留）

| 值 | 宏 | 含义 |
|-------|-------|---------|
| 0x01 | `PCIY_PMG` | 电源管理 |
| 0x05 | `PCIY_MSI` | 消息信号中断 |
| 0x09 | `PCIY_VENDOR` | 供应商特定 |
| 0x10 | `PCIY_EXPRESS` | PCI Express |
| 0x11 | `PCIY_MSIX` | MSI-X |

### 扩展能力ID（PCIe）

| 值 | 宏 | 含义 |
|-------|-------|---------|
| 0x0001 | `PCIZ_AER` | 高级错误报告 |
| 0x0002 | `PCIZ_VC` | 虚拟通道 |
| 0x0003 | `PCIZ_SERNUM` | 设备序列号 |
| 0x0004 | `PCIZ_PWRBDGT` | 电源预算 |
| 0x000d | `PCIZ_ACS` | 访问控制服务 |
| 0x0010 | `PCIZ_SRIOV` | 单根I/O虚拟化 |

需要其他PCI常量的读者应直接打开`/usr/src/sys/dev/pci/pcireg.h`。文件注释良好；查找特定偏移或位花费不到一分钟。



## 参考：与第16章和第17章模式的比较

第18章在何处扩展第16章和第17章以及何处引入真正新材料的并排比较。

| 模式 | 第16章 | 第17章 | 第18章 |
|---------|-----------|-----------|-----------|
| 寄存器访问 | `CSR_READ_4`等 | 相同API，未改变 | 相同API，未改变 |
| 访问日志 | 引入 | 用故障注入条目扩展 | 未改变 |
| 锁纪律 | 每次访问持有`sc->mtx` | 相同，加callout | 相同 |
| 文件布局 | 添加`myfirst_hw.c` | 添加`myfirst_sim.c` | 添加`myfirst_pci.c` |
| 寄存器映射 | 10个寄存器，40字节 | 16个寄存器，60字节 | 相同 |
| 附加例程 | 简单（`malloc`块） | 简单（`malloc`块加模拟设置） | 真实PCI BAR声明 |
| 分离例程 | 简单 | 相同加callout排空 | 相同加BAR释放 |
| 模块加载 | `kldload`触发加载 | 相同 | `kldload`加PCI探测 |
| 设备实例 | 全局（隐式） | 全局 | 每PCI设备，编号 |
| BAR | 不适用 | 不适用 | BAR 0，`SYS_RES_MEMORY`，`RF_ACTIVE` |
| 能力遍历 | 不适用 | 不适用 | `pci_find_cap` / `pci_find_extcap` |
| cdev | 模块加载时创建 | 相同 | 每附加创建 |
| 版本 | 0.9-mmio | 1.0-simulated | 1.1-pci |
| 文档 | 引入`HARDWARE.md` | 引入`SIMULATION.md` | 引入`PCI.md` |

第18章构建在第16章和第17章之上而不破坏任何东西。每个早期章节的能力都被保留；真实PCI附加作为组合现有结构的新后端添加。`1.1-pci`的驱动程序是`1.0-simulated`驱动程序的严格超集。



## 参考：来自真实FreeBSD PCI驱动程序的模式

`/usr/src/sys/dev/`树中重复出现的模式的简短巡览。每个模式都是来自真实驱动程序的具体片段，为可读性稍作重写，带有指向文件的指针和关于为什么该模式重要的简短说明。在第18章后阅读这些模式巩固词汇。

### 模式：按类型遍历BAR

来自`/usr/src/sys/dev/e1000/if_em.c`：

```c
for (rid = PCIR_BAR(0); rid < PCIR_CIS;) {
	val = pci_read_config(dev, rid, 4);
	if (EM_BAR_TYPE(val) == EM_BAR_TYPE_IO) {
		break;
	}
	rid += 4;
	if (EM_BAR_MEM_TYPE(val) == EM_BAR_MEM_TYPE_64BIT)
		rid += 4;
}
```

这个循环遍历BAR表寻找I/O端口BAR。它读取每个BAR的配置空间值，检查其类型位，并推进4字节（一个BAR槽）或8字节（两个槽，对于64位内存BAR）。循环在`PCIR_CIS`（CardBus指针，刚好在BAR表之后）或当找到I/O BAR时终止。

为什么重要：在支持一系列硬件修订的混合内存和I/O BAR的驱动程序上，BAR布局不固定。动态遍历是正确方法。第18章的驱动程序针对一个已知BAR布局的设备，不需要此遍历器；像`em(4)`这样覆盖一系列芯片的驱动程序需要。

### 模式：按类、子类和progIF匹配

来自`/usr/src/sys/dev/uart/uart_bus_pci.c`：

```c
if (pci_get_class(dev) == PCIC_SIMPLECOMM &&
    pci_get_subclass(dev) == PCIS_SIMPLECOMM_UART &&
    pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A) {
	id = &cid;
	sc->sc_class = &uart_ns8250_class;
	goto match;
}
```

这个片段是基于类的后备。如果供应商和设备匹配失败，探测回退到匹配任何在类代码中广告"简单通信/UART/16550A之前"的设备。progIF字段区分16450、16550A及后续变体；该片段特别针对较老的。

为什么重要：类代码让驱动程序附加到特定匹配表没有枚举的设备系列。只要类代码是标准的，不在`uart(4)`表中的供应商的UART芯片仍被处理。该模式对于编程接口是类定义的标准化设备类型（AHCI、xHCI、UART、NVMe、HD Audio）效果良好。

### 模式：条件MSI分配

来自`/usr/src/sys/dev/uart/uart_bus_pci.c`：

```c
id = uart_pci_match(dev, pci_ns8250_ids);
if ((id == NULL || (id->rid & PCI_NO_MSI) == 0) &&
    pci_msi_count(dev) == 1) {
	count = 1;
	if (pci_alloc_msi(dev, &count) == 0) {
		sc->sc_irid = 1;
		device_printf(dev, "Using %d MSI message\n", count);
	}
}
```

这个片段在设备支持且驱动程序没有用`PCI_NO_MSI`标记条目时分配MSI。`pci_msi_count(dev)`调用返回设备广告的MSI向量数；`pci_alloc_msi`分配它们。`sc->sc_irid = 1`行反映分配给MSI资源的rid（MSI资源从rid 1开始；遗留IRQ使用rid 0）。

为什么重要：MSI在现代系统上优于遗留IRQ，因为它避免了INTx引脚的IRQ共享问题。支持MSI并在MSI不可用时回退到遗留IRQ的驱动程序是正确的模式。第20章详细讨论MSI；这里的片段是预览。

### 模式：分离时IRQ释放

来自`/usr/src/sys/dev/uart/uart_bus_pci.c`：

```c
static int
uart_pci_detach(device_t dev)
{
	struct uart_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_irid != 0)
		pci_release_msi(dev);

	return (uart_bus_detach(dev));
}
```

分离释放MSI（如果分配了）并将其余委托给`uart_bus_detach`。`sc->sc_irid != 0`检查防止在驱动程序使用遗留IRQ时调用`pci_release_msi`；在未分配时释放MSI是错误。

为什么重要：附加分配的每个资源必须在分离时释放。驱动程序通过状态跟踪它分配了什么（这里，`sc_irid != 0`意味着使用了MSI）并相应释放。第19章和第20章将以类似模式扩展第18章的分离。

### 模式：读取供应商特定配置字段

来自`/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`（简化）：

```c
cap_offset = 0;
while (pci_find_next_cap(dev, PCIY_VENDOR, cap_offset, &cap_offset) == 0) {
	uint8_t cap_type = pci_read_config(dev,
	    cap_offset + VIRTIO_PCI_CAP_TYPE, 1);
	if (cap_type == VIRTIO_PCI_CAP_COMMON_CFG) {
		/* This is the capability we're looking for. */
		break;
	}
}
```

这遍历列表中的每个供应商特定能力（ID = `PCIY_VENDOR` = `0x09`），检查每个的供应商定义类型字节，直到找到驱动程序想要的。`pci_find_next_cap`函数是`pci_find_cap`的迭代版本，从上次调用离开的地方继续。

为什么重要：当多个能力共享相同ID时（如virtio的供应商特定能力），驱动程序必须遍历并通过读取能力自己的类型字段来消歧。`pci_find_next_cap`函数专门为此情况存在。

### 模式：电源感知恢复处理程序

来自各种驱动程序：

```c
static int
myfirst_pci_resume(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	/* Restore the device to its pre-suspend state. */
	MYFIRST_LOCK(sc);
	CSR_WRITE_4(sc, MYFIRST_REG_CTRL, sc->saved_ctrl);
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, sc->saved_intr_mask);
	MYFIRST_UNLOCK(sc);

	/* Re-enable the user-space interface. */
	return (0);
}
```

挂起处理程序保存设备状态；恢复处理程序恢复它。该模式对于支持挂起到内存（S3）或挂起到磁盘（S4）的系统重要；不实现挂起和恢复的驱动程序阻止系统进入那些状态。

第18章的驱动程序不实现挂起和恢复。第22章添加它们。

### 模式：响应设备特定错误状态

来自`/usr/src/sys/dev/e1000/if_em.c`：

```c
if (reg_icr & E1000_ICR_RXO)
	sc->rx_overruns++;
if (reg_icr & E1000_ICR_LSC)
	em_handle_link(ctx);
if (reg_icr & E1000_ICR_INT_ASSERTED) {
	/* ... */
}
```

中断后，驱动程序读取中断原因寄存器（`reg_icr`）并根据设置的位分发。每个位对应不同事件：接收溢出、链路状态变化、一般中断。驱动程序对每个采取不同动作。

为什么重要：真实驱动程序处理许多事件类型。分发模式从第17章故障注入熟悉，那里模拟可以注入不同故障类型。第19章将引入此模式的中断处理版本。

### 模式：使用sysctl暴露驱动程序配置

来自任意数量的驱动程序：

```c
SYSCTL_ADD_U32(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO,
    "max_retries", CTLFLAG_RW,
    &sc->max_retries, 0,
    "Maximum retry attempts");
```

驱动程序通过sysctl暴露可调参数。参数可以从用户空间用`sysctl dev.myfirst.0.max_retries`读或写。暴露少量此类可调参数的驱动程序给其操作员一种无需重建驱动程序调整行为的方式。

为什么重要：sysctl是每驱动程序可调参数的正确位置。内核命令行选项（启动时设置的可调参数）仅用于早期启动参数；运行时调整通过sysctl进行。

### 模式：在能力结构中记录支持特性

来自`/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`：

```c
sc->vtpci_modern_res.vtprm_common_cfg_cap_off = common_cfg_off;
sc->vtpci_modern_res.vtprm_notify_cap_off = notify_off;
sc->vtpci_modern_res.vtprm_isr_cfg_cap_off = isr_cfg_off;
sc->vtpci_modern_res.vtprm_device_cfg_cap_off = device_cfg_off;
```

驱动程序将每个能力的偏移存储在每设备状态结构中。稍后需要访问能力寄存器的代码通过存储的偏移到达它。

为什么重要：附加后，驱动程序不应需要重新遍历能力列表。在附加时存储偏移节省每次访问的遍历。第18章的驱动程序为信息目的遍历能力，但不存储偏移因为它不使用它们。关心能力的真实驱动程序存储其偏移。

### 模式总结

上述模式是FreeBSD PCI驱动程序的通用货币。在陌生代码中识别它们的读者是可以从树中任何驱动程序学习的读者。第18章教授了基础模式；真实驱动程序在其上层叠特定变化。特定变化总是很小的（这里有基于类的匹配，那里有MSI分配）；基础模式才是反复出现的内容。

在第18章和实验完成后，从`/usr/src/sys/dev/`中选一个你感兴趣的驱动程序（也许是你拥有的设备，或也许只是你认可名称的一个），阅读其PCI附加。用本节作为检查清单：驱动程序使用哪些模式？跳过哪些？为什么？在三四个不同驱动程序上做过此练习的驱动程序作者已经建立了巨大的模式识别储备。



## 参考：关于PCI驱动哲学的结束语

一段结束本章的话，值得在实验后回来看。

PCI驱动程序的工作不是理解设备。PCI驱动程序的工作是以内核可以使用的形式将设备呈现给内核。对设备的理解（其寄存器意味着什么、它讲什么协议、它维护什么不变量）属于驱动程序的上层：硬件抽象、协议实现、用户空间接口。PCI层是一个狭窄的东西。它匹配一个供应商和设备ID。它声明一个BAR。它将BAR交给上层。它注册一个中断处理程序。它将控制权交给上层。它的存在是为了连接驱动程序身份的两半：属于硬件的设备半部分和属于内核的软件半部分。

编写了第18章驱动程序的读者已经编写了一个PCI层。它很小。驱动程序的其余部分使它有用。在第19章，驱动程序的PCI层将获得一个更多责任（中断注册）。在第20章，它将获得MSI和MSI-X。在第20章和第21章，它将管理DMA标签。每一个都是PCI层现有角色的狭窄扩展。没有一个改变PCI层的基本性质。

对于这个读者和本书的未来读者，第18章PCI层是`myfirst`驱动程序架构的永久部分。每个后续章节都假设它。每个后续章节都扩展它。驱动程序的整体复杂性将增长，但PCI层将保持第18章制造的样子：设备与驱动程序其余部分之间的连接器，小而可预测。

第18章教授的技能不是"如何为virtio-rnd编写驱动程序"。它是"如何将驱动程序连接到PCI设备，无论设备是什么"。该技能是可迁移的，它是你编写的每个PCI驱动程序都会为你服务的技能。



## 参考：第18章快速参考卡

第18章介绍的词汇、API、宏和过程的紧凑摘要。在研究第19章及后续章节时用作单页刷新器。

### 词汇

- **PCI**：外围组件互连，Intel在1990年代初引入的共享并行总线。
- **PCIe**：PCI Express，现代串行继任者。与PCI软件可见模型相同。
- **B:D:F**：总线、设备、功能。PCI设备的地址。在FreeBSD输出中写为`pciN:B:D:F`。
- **配置空间**：每个PCI设备暴露的小型元数据区域。PCI上256字节，PCIe上4096字节。
- **BAR**：基址寄存器。配置空间中的一个字段，设备在其中广告它需要的地址范围。
- **供应商ID**：PCI-SIG分配给制造商的16位标识符。
- **设备ID**：供应商分配给特定产品的16位标识符。
- **子供应商/子系统ID**：识别板卡而非芯片组的辅助16+16位元组。
- **能力列表**：配置空间中可选特性块的链表。
- **扩展能力列表**：PCIe特定的列表，从偏移`0x100`开始。

### 基本API

- `pci_get_vendor(dev)` / `pci_get_device(dev)`：读取缓存的ID字段。
- `pci_get_class(dev)` / `pci_get_subclass(dev)` / `pci_get_progif(dev)` / `pci_get_revid(dev)`：读取缓存的分类字段。
- `pci_get_subvendor(dev)` / `pci_get_subdevice(dev)`：读取缓存的子系统标识。
- `pci_read_config(dev, offset, width)` / `pci_write_config(dev, offset, val, width)`：原始配置空间访问（宽度1、2或4）。
- `pci_find_cap(dev, cap, &offset)` / `pci_find_next_cap(dev, cap, start, &offset)`：遍历遗留能力列表。
- `pci_find_extcap(dev, cap, &offset)` / `pci_find_next_extcap(dev, cap, start, &offset)`：遍历PCIe扩展能力列表。
- `pci_enable_busmaster(dev)` / `pci_disable_busmaster(dev)`：切换总线主控启用位。
- `pci_msi_count(dev)` / `pci_msix_count(dev)`：报告MSI和MSI-X向量计数。
- `pci_alloc_msi(dev, &count)` / `pci_alloc_msix(dev, &count)`：分配MSI或MSI-X向量（第20章）。
- `pci_release_msi(dev)`：释放MSI或MSI-X。
- `bus_alloc_resource_any(dev, type, &rid, flags)`：声明一个资源（BAR、IRQ等）。
- `bus_release_resource(dev, type, rid, res)`：释放一个声明的资源。
- `rman_get_bustag(res)` / `rman_get_bushandle(res)`：提取`bus_space`标签和句柄。
- `rman_get_start(res)` / `rman_get_size(res)` / `rman_get_end(res)`：检查资源的范围。
- `bus_space_read_4(tag, handle, off)` / `bus_space_write_4(tag, handle, off, val)`：低级访问器。
- `bus_read_4(res, off)` / `bus_write_4(res, off, val)`：基于资源的简写。

### 基本宏

- `DEVMETHOD(device_probe, probe_fn)` 等：填充方法表。
- `DEVMETHOD_END`：终止方法表。
- `DRIVER_MODULE(name, bus, driver, modev_fn, modev_arg)`：针对总线注册驱动程序。
- `MODULE_DEPEND(name, dep, minver, prefver, maxver)`：声明模块依赖。
- `MODULE_VERSION(name, version)`：声明驱动程序的版本。
- `PCIR_BAR(n)`：计算BAR `n`的配置空间偏移。
- `BUS_PROBE_DEFAULT`、`BUS_PROBE_GENERIC`、`BUS_PROBE_VENDOR`、`BUS_PROBE_SPECIAL`：探测优先级值。
- `SYS_RES_MEMORY`、`SYS_RES_IOPORT`、`SYS_RES_IRQ`：资源类型。
- `RF_ACTIVE`、`RF_SHAREABLE`：资源分配标志。

### 常见过程

**将PCI驱动程序附加到特定设备ID：**

1. 编写一个读取`pci_get_vendor(dev)`和`pci_get_device(dev)`的探测，与表比较，匹配返回`BUS_PROBE_DEFAULT`否则`ENXIO`。
2. 编写一个调用`bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE)`并带有`rid = PCIR_BAR(0)`的附加。
3. 用`rman_get_bustag`和`rman_get_bushandle`提取标签和句柄。
4. 将它们存储在访问器层可以到达的地方。

**在分离时释放PCI资源：**

1. 排空可能访问资源的任何callout或任务。
2. 用`bus_release_resource(dev, type, rid, res)`释放资源。
3. 将存储的资源指针设为NULL。

**加载冲突的基础系统驱动程序之前卸载它：**

```sh
sudo kldunload virtio_random   # 或任何拥有设备的驱动程序
sudo kldload ./myfirst.ko
```

**强制设备从一个驱动程序重新绑定到另一个：**

```sh
sudo devctl detach <driver0_name>
sudo devctl set driver -f <pci_selector> <new_driver_name>
```

### 有用命令

- `pciconf -lv`：列出每个PCI设备及其ID、类和驱动程序绑定。
- `pciconf -r <selector> <offset>:<length>`：转储配置空间字节。
- `pciconf -w <selector> <offset> <value>`：写入配置空间值。
- `devinfo -v`：转储带有资源和绑定的newbus树。
- `devctl detach`、`attach`、`disable`、`enable`、`rescan`：在运行时控制总线绑定。
- `dmesg`、`dmesg -w`：查看（并追踪）内核消息缓冲区。
- `kldstat -v`：列出带详细信息的已加载模块。
- `kldload`、`kldunload`：加载和卸载内核模块。
- `vmstat -m`：按malloc类型报告内存分配。

### 值得收藏的文件

- `/usr/src/sys/dev/pci/pcireg.h`：PCI寄存器定义（`PCIR_*`、`PCIM_*`、`PCIY_*`、`PCIZ_*`）。
- `/usr/src/sys/dev/pci/pcivar.h`：PCI访问器函数声明。
- `/usr/src/sys/sys/bus.h`：newbus方法和资源宏。
- `/usr/src/sys/sys/rman.h`：资源管理器访问器。
- `/usr/src/sys/sys/module.h`：模块注册宏。
- `/usr/src/sys/dev/uart/uart_bus_pci.c`：一个清晰、可读的示例PCI驱动程序。
- `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`：现代传输示例。



## 参考：第18章术语词汇表

为想要第18章词汇紧凑提醒的读者准备的简短词汇表。

**AER（高级错误报告）**：一种PCIe扩展能力，向操作系统报告事务层错误。

**附加（Attach）**：驱动程序实现的newbus方法，用于获取特定设备实例的所有权。每个设备调用一次，在探测成功后。

**BAR（基址寄存器）**：配置空间中的一个字段，设备在其中广告它需要映射的一个地址范围。

**总线主控（Bus Master）**：在PCI总线上发起自己事务的设备。DMA必需。通过命令寄存器的`BUSMASTEREN`位启用。

**能力（Capability）**：配置空间中的可选特性块。通过遍历能力列表发现。

**类代码（Class code）**：对设备功能进行分类的三字节分类（类、子类、编程接口）。

**cdev**：`/dev/`中的字符设备节点，由`make_dev(9)`创建。

**配置空间（Configuration space）**：每设备的元数据区域。PCI上256字节，PCIe上4096字节。

**分离（Detach）**：撤销附加所做一切的newbus方法。每个设备调用一次，当驱动程序解绑时。

**device_t**：newbus层传递给驱动程序方法的不透明句柄。

**DRIVER_MODULE**：向总线注册驱动程序并将其包装为内核模块的宏。

**ENXIO**：探测返回的errno，表示"我不匹配此设备"。

**EBUSY**：分离返回的errno，表示"我拒绝分离；驱动程序正在使用中"。

**IRQ**：中断请求。在PCI中，`PCIR_INTLINE`配置空间字段保存遗留IRQ号。

**遗留中断（INTx）**：从PCI继承的基于引脚的中断机制。在现代系统上已被MSI和MSI-X取代。

**MMIO（内存映射I/O）**：通过类似内存的加载和存储指令访问设备寄存器的模式。

**MSI / MSI-X**：消息信号中断；使用对特定内存地址的写入而非引脚断言的中断机制。第20章。

**Newbus**：FreeBSD的设备树抽象。每个设备有父总线和驱动程序。

**PCI**：较老的并行总线标准。

**PCIe**：PCI的现代串行继任者。与PCI软件兼容。

**PIO（端口映射I/O）**：通过x86 `in`和`out`指令访问设备寄存器的模式。基本已过时。

**探测（Probe）**：测试驱动程序是否能处理特定设备的newbus方法。必须是幂等的。

**资源（Resource）**：内核管理的设备资源（内存范围、I/O端口范围、IRQ）的通用名称。通过`bus_alloc_resource_any(9)`分配。

**Softc（软件上下文）**：驱动程序维护的每设备状态结构。通过`driver_t`确定大小，由newbus分配。

**子类（Subclass）**：类代码的中间字节；细化类。

**子供应商/子系统ID**：第二级标识元组，细化主要供应商/设备对以区分不同的板卡设计。

**供应商ID**：PCI-SIG分配的16位制造商标识符。
