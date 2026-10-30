# 环境、构建与调试

2026-10-30当前：已按用户要求回退到今天10:26:13的1200档版本（AuTop环岛/十字改造之前）。12项共享源码/配置哈希及重新编译的AXF/HEX/BIN均与当时记录完全一致；1200/900/800/800mm/s保持。Keil0错误0警告，灰度181断言通过；本次未重跑全套回归和双向仿真，历史结果见[10:26版本报告](../outputs/reports/align1200-2026-10-30.md)。后续入口改造与修复报告保留为历史，不代表当前固件；未烧录。

## 2026-10-29 当前分段图传固件

主工程已整合主循环分段图传及同步建链起跑门控，Keil全量0错误0警告；Code140512、RO13296、RW300、ZI133344字节。AXF/HEX/BIN已重新生成到`outputs/firmware/`并与`car/build/objects/`核对。未烧录、未测实车帧率。参数保持原配置，范围与验证见[整合报告](../outputs/reports/main-loop-integration-2026-10-29.md)。

## 历史：2026-10-14运动稳定性固件

2026-10-14运动稳定性交付采用Keil6.21全量构建，0错误0警告；发布及实际下载路径AXF/HEX/BIN已同步，未烧录。参数、资源占用、固件哈希与当轮验证见[交付报告](../outputs/reports/motion-stability-2026-10-14.md)。后续规范整理使用隔离工程验证，不覆盖现有发布与下载缓存，见[规范整理报告](../outputs/reports/project-normalization-2026-10-14.md)。

## 历史：2026-10-13旧模型800–1200mm/s验收

历史记录（2026-10-13旧模型，已由10月14日交付替代）：当时默认四档800mm/s，新增800/1000/1200完整验证预设；编码器采用1024×68/30，PWM1000‰、单轮保护2500mm/s、舵机40µs/20ms、速度Ki1.0。三档双向及选定扰动/延迟工况通过，130项工程、47项仿真回归通过，Keil0错误0警告。未刷写或实车验证；1200档弯中实际约1091–1103mm/s，模型和实物标定边界见[本轮报告](../outputs/reports/field-robustness-2026-10-13.md)。

## 多设备入口

工具查找、热点本地头文件和编辑器变量见[本地环境](local-environment.md)。当前构建脚本支持显式路径、环境变量、本地配置和有限自动发现。以下各历史环境表属于当时机器记录，不作为当前电脑安装路径的确认。

2026-10-13热点专用入口、脚本和本机头文件集中到`wifi/`，维护入口见[wifi/README](../wifi/README.md)。固件的`car/config/camera_debug_config.h`通过相对引用读取`../../wifi/camera_debug_local.h`；构建脚本检查该新路径。本机头文件被Git忽略，首次使用需生成配置；此次仅迁移路径，未重新运行Keil构建或刷写。

## 历史：250档正式发布构建与配置验证（2026-10-13）

正式源码、仿真默认和新增实测基线均为350/350/350/250mm/s；元素与Q<65限250，终点100，既有控制、视觉及保护参数保持。107项工程测试跨批通过、仿真38/38、双向基准及扰动整圈零车轮出界无故障；GUI加载/重置、宏映射和实际倍速通过。Keil全量0错误0警告，Code117088/RO13208/RW300/ZI105836字节，发布AXF/HEX/BIN与构建缓存一致。HEX SHA-256为72a5655baac1e2c3172ae8b78fbf4a4551c6327d83297237748e7b6e2fafe253。用户此前250档烧录与实测通过按反馈记录；本轮只重建发布和软件验收，未烧录或操作车辆。详情与回退见[完整配置报告](../outputs/reports/config250-2026-10-13.md)。

## 历史：用户手动元素250档编译、烧录与实测（2026-10-13）

已读取正式源码，确认AUTO_ELEMENT_SPEED_MM_S=250.0f，四档为350/350/350/250mm/s。用户反馈250档已重新编译、烧录并通过实测；本次仅登记说明，未重新编译、烧录或操作车辆，也未独立核验用户烧录产物。下述元素180档历史固件SHA-256不作为用户已烧录250档的固件身份。历史测试数量、构建体积与校验和保留原始含义，详见[实测补充](../outputs/reports/element-fault-2026-10-13.md)。

## 历史：连续弯道元素诊断构建（2026-10-13）

正式速度升级为350/350/350/180mm/s，扩充元素上下文/状态历史、ELEMENT诊断冻结，并修正出环接弯道的退出条件。真实Keil全量0错误0警告，Code117088/RO13208/RW300/ZI105836字节。发布AXF/HEX/BIN与构建缓存逐字节一致；260/200对照及350/350最终源码仿真、校验和回退见[本轮报告](../outputs/reports/element-fault-2026-10-13.md)。未烧录或操作车辆。

## 历史：元素诊断与弯道260/200构建（2026-10-13）

主工程350/260/200/180mm/s，入口确认/十字来向及控制诊断已更新；真实Keil全量0错误0警告，Code116728/RO13208/RW300/ZI105040字节。AXF/HEX/BIN与实际构建缓存逐字节一致，HEX SHA-256为cd8037d75427a452b29a43d611ff91c250b1953d2226f7352b60514f571520f9。双向闭环与扰动结果、GUI实际倍率及回归证据见[本轮报告](../outputs/reports/element-speed-2026-10-13.md)。未烧录、未操作车辆。

## 历史：场测提速构建（2026-10-13）

用户批准350/220/160/180mm/s速度档，主工程仅改变autonomous_config.h四个速度宏。真实Keil全量构建0错误0警告，Code115128/RO13208/RW300/ZI104872字节；AXF/HEX/BIN已发布并与car/build/objects逐字节核对。HEX SHA-256为ca6002ab48922b2130e4789cec7a4b2750ab7c37377b4917107704fbbd18eeaa。本轮未烧录或操作车辆；相关测试的固定等待时间失败、临时副本验证和备份详见[交付报告](../outputs/reports/field-speed-2026-10-13.md)。

## 历史：控制优化构建（2026-10-12）

元素识别与提速修改已全量构建：Keil5.39 / Armclang6.21，0错误0警告，Code115120/RO13200/RW300/ZI104872字节。在本任务临时副本执行现有build.ps1，src/include/config/library与主工程逐文件一致，AXF/HEX/BIN已交付outputs/firmware，未烧录。95项工程行为回归与25项仿真回归通过，历史15项PWM测试预期不符已修正。

构建脚本4项外部工具替身测试另列：本机Windows拒绝启动临时假UV4.exe，未纳入上述95项；真实已安装Keil构建和导出成功。见[构建日志](../outputs/reports/historical-logs/control-tuning-build-2026-10-12.log)与[最终报告](../outputs/reports/control-tuning-final-2026-10-12.md)。重新在主工程调试时按下文构建入口生成本机调试缓存。

## 历史：外部核心进展同步（2026-10-11）

已采用Autodrive.rar最终Q40外侧循迹/自动±130us状态，本机Wi-Fi、Keil/DAP及编辑器配置保留。实际本机全量0错误0警告（Code108136/RO13200/RW300/ZI104716），364引用均有效。完整73项回归仍15个旧参数断言失败、0错误，较合并前17失败无新增；具体通过项和限制见[同步记录](archive-sync.md)。固件已重新生成至outputs/firmware并核对实际下载缓存，未自动烧录。


## 历史基线：曾回退17:25版本（2026-10-08）

用户于本日后续明确要求回退，恢复car_before-remote-speed_20261008_193758.zip所保存的17:25交付状态：C15自动驾驶保留，手柄恢复10%开环，撤销后来的手柄闭环与提速。71项回归通过，Keil0错误0警告，三种固件哈希均与当时记录完全一致。下载前重新加载主工程，避免Keil内存中的后续工程状态覆盖已恢复文件。日志：[回退测试](../outputs/reports/historical-logs/rollback-1725-tests.log)、[回退构建](../outputs/reports/historical-logs/rollback-1725-build.log)。本次未自动烧录。


## 历史构建阶段：自动驾驶（2026-10-08）

主工程car/autocar.uvprojx新增10个引用，共364个，重新加载工程后Rebuild。当前C15自动驾驶，C12遥控允许/自动取消，S3停车；操作和调参看[自动驾驶指南](autonomous-driving.md)，旧舵机S2测试说明仅供历史追溯。

71项完整unittest与19项旧算法通过。默认模式实际Keil全量0错误0警告（Code105320/RO13172/RW300/ZI104684），关闭遥控隔离构建也0/0。实际下载缓存与outputs/firmware三种固件一致。入口仍为car/scripts/build.ps1；完整回归使用python -B -m unittest discover -s car/tests -p 'test_*.py'，旧test.ps1不能代替全套测试。日志见[本轮构建](../outputs/reports/historical-logs/autonomous-build.log)和[完整回归](../outputs/reports/historical-logs/autonomous-suite.log)。


工程入口：[car/autocar.uvprojx](../car/autocar.uvprojx)，目标名 `autocar`。本次构建对象是整理后的应用工程，不是逐一构建84个供应商例程。

## 本机已验证环境

| 项目 | 配置 |
| --- | --- |
| uVision / MDK | `D:\Keil_v5\UV4\UV4.exe`，MDK 5.39（2026-09-30重新安装后验证） |
| C 编译器 | `D:\Keil_v5\ARM\ARMCLANG\bin\armclang.exe`，Arm Compiler for Embedded 6.21 |
| 芯片设备包 | NXP.MIMXRT1064_DFP.19.0.0，根目录 `D:\Arm\Packs` |
| 工程 Device | MIMXRT1064DVL6B，Cortex-M7，硬件浮点 |
| 逐飞库 | V3.11.2，来源提交 `1223cdb` |
| 应用参考 | `参考/镜头组例程.zip`，其中库版本V3.5.5，仅迁入并适配应用层 |
| 算法测试 | 原生 Windows GCC（本机使用 Vivado 附带 MinGW-W64 6.2.0） |

初始化时以 MDK5.24a / Compiler6.7 编译通过。2026-09-30重新安装后，已使用 MDK5.39 / Compiler6.21 对逐飞V3.11.2原始模板和当前car工程完整副本分别原样全量编译，两者均0错误0警告，见 [新版兼容性报告](../outputs/reports/keil-compatibility-2026-09-30.md)。该次兼容性验证未改写主工程配置或替换固件；后续变更以最新日志为准。IAR例程保留在原始资料中，没有创建或验证 `car/` 的IAR工程。

## 文件职责

```text
car/
  autocar.uvprojx              Keil工程与文件分组
  autocar.uvoptx               对应调试配置
  src/main.c                  初始化、图像处理与诊断主循环
  src/isr.c                   10ms控制与独立1ms计时、SDK中断转发
  src/img_process.c           直方图、Otsu、二值化、基础边线
  src/pid.c                   位置式/增量式PID
  src/camera_debug.c          14:33版同步图传、后缀重试与重连（冻结）
  src/camera_display.c        IPS200 SPI原始灰度与网络/舵机状态
  src/servo_control.c         按键启动的舵机测试
  src/motor_control.c         C12双路电机有限时序与S3停止
  src/encoder_feedback.c      STEP/DIR采样、累计与轮速
  include/                    应用头文件与接口
  config/board_config.h       硬件引脚、功能开关、控制周期
  config/camera_debug_config.h 图传视图、Wi-Fi SPI与实际热点配置
  config/servo_config.h        接口1、供电记录、脉宽和按键
  config/motor_test_config.h   P8通道映射、测试PWM与时长
  config/encoder_config.h      STEP/DIR、PPR、齿轮比与轮径
  library/                    保持供应商布局的SDK/逐飞驱动/组件与许可
  mdk/scf/                    内存布局与链接配置
  mdk/ini/                    下载调试初始化脚本
  scripts/build.ps1            全量构建及最终产物导出
  scripts/test.ps1             原生C算法回归测试
  tests/test_algorithms.c      算法行为检查
```

库只保留一份工程内依赖，不依赖 `../SeekFree/` 的相对路径。初始化时上游文件逐字节复制，后续工程副本修正以日志为准；初始来源与校验值见 [依赖来源与验收摘要](../outputs/reports/project-initialization-2026-09-30.md#依赖来源与验收记录)。原来的空工程已备份到 [car_before-project-initialization_20260930_184639](../outputs/backups/car_before-project-initialization_20260930_184639/)。

## 构建命令

在 PowerShell 中以工作区根目录 `<项目根目录>` 为当前目录运行：

```powershell
.\car\scripts\build.ps1
```

其他电脑使用实际安装路径：

```powershell
.\car\scripts\build.ps1 -KeilPath 'C:\Keil_v5\UV4\UV4.exe'
```

脚本要求同一 Keil 安装下的 `ARM/ARMCLANG/bin/fromelf.exe` 存在，执行 uVision 全量重建，检查进程退出码、日志错误数和本轮产物时间。先在临时目录生成并检查完整 AXF、HEX、BIN，再复制到输出目录；导出工具缺失或导出失败会明确报错并保留上一套输出。脚本不会烧录、启动调试会话或控制硬件。

| 文件位置 | 内容 |
| --- | --- |
| `car/build/objects/` | 编译对象、依赖、中间AXF/HEX及构建辅助文件（持久缓存，不提交） |
| `car/build/listings/` | 汇编/链接中间清单 |
| `outputs/firmware/autocar.axf` | 带调试信息的最终ELF镜像 |
| `outputs/firmware/autocar.hex` | Intel HEX镜像，包含Flash地址信息 |
| `outputs/firmware/autocar.bin` | 原始二进制，Flash加载基址为0x70000000 |
| `car/build/listings/autocar.map` | 临时链接映射与内存占用，不自动导出到reports |
| `car/build/keil-build.log` | 最近一次构建日志；重建前存档已有日志 |

最终产物代表最近一次成功导出的构建。修改源文件后必须重新构建并检查成功结果；构建失败时不能把旧产物当成本轮成功结果。`car/build/`为持久构建缓存（不提交），可长期保留；若清理该缓存，需重新构建才能使用 Keil 的调试输出路径。

也可以在 uVision 中打开工程选择 `autocar` 并执行 Rebuild，但GUI编译仅生成 `car/build/` 下的中间产物；交付归档应使用上述脚本。源码目录与工程位置必须保持相对结构，迁移后先关闭旧源文件页签并重新构建。

## AXF文件无法加载

若下载提示`Flash Download failed - Could not load file '.\build\objects\autocar.axf'`，先核对最近一次Build Output和实际文件，不能只看`outputs/firmware/`已有固件。当前Keil下载读取的是工程OutputDirectory中的AXF。

2026-10-03本次实例：20:37构建因`main.c`第60行函数名误写为`get_hist_g-0ram`产生两个编译错误，日志为`Target not created`，下载路径没有AXF，而旧HEX仍残留。已将其恢复为`get_hist_gram`，直接运行主工程`car/scripts/build.ps1`，全量编译0错误0警告；AXF已在Keil实际路径生成并通过fromelf解析，与发布目录同名文件哈希一致。

处理顺序：先修复编译错误，再Rebuild，确认AXF生成后下载。构建失败后的旧HEX或发布文件不代表本轮主工程构建成功。本例未改输出路径、Flash算法、DAP参数或图传/舵机功能。

交付检查同时覆盖Keil实际下载目录和`outputs/firmware/`，不能仅用隔离构建副本的成功结果代替主工程下载文件检查。`car/build/`是主工程下载缓存；若清理此缓存，下载前必须重新构建。临时目录清理仍遵循任务边界，不删除其他任务的目录。

## 历史兼容处理与新版验证

1. 模板 `pCCUsed` 记录6.19，car工程记录6.7；重装后的两次实际构建均使用6.21。判断实际编译器版本以日志为准，不仅查看这一旧版本记录。
2. 初始化时旧MDK5.24a因未识别新版汇编选项而出现51项错误，曾通过 `uClangAs=1` 解决。新版MDK5.39可直接编译使用 `ClangAsOpt=1` 的原模板。本轮开始时现有car工程已使用 `ClangAsOpt=2` 且没有 `uClangAs`，也原样编译通过；不需要恢复旧版兼容项。
3. 工程选择真实 DVL6B 设备。逐飞附带的2018版 SDK 头文件只接受 `CPU_MIMXRT1064DVL6A` 等A系列选择宏，因此保留该宏以选择共用RT1064寄存器头；没有用它更改用户的真实芯片型号。供应商代码未因此被改写，硬件修订版差异需上板确认。
4. 沿用逐飞 Flash→SDRAM 启动/搬移布局，DTCM 448 KiB、ITCM 64 KiB。不要为消除链接错误随意改写启动文件或内存地址。

## 算法回归测试

```powershell
.\car\scripts\test.ps1
# 或指定任一可用的原生 Windows GCC
.\car\scripts\test.ps1 -Compiler 'C:\mingw64\bin\gcc.exe'
```

测试编译实际 `src/img_process.c` 和 `src/pid.c`，不使用另写的算法仿制品。19项检查覆盖跨帧直方图、高灰度区、二值化、Otsu常量/双色/空输入、黑帧/窄图边界、PID积分限幅与误差反转、输出饱和、非法采样周期。运行日志为 [algorithm-tests.log](../car/build/algorithm-tests.log)。

完整工程还必须通过 Keil 编译；原生算法测试不覆盖 MCU 外设寄存器、中断时序、DMA、实际摄像头帧和电机控制效果。

构建导出流程有四项行为检查：缺少导出工具、导出失败、三种固件正常发布，以及链接map仅保留在临时构建目录且既有报告不受影响。它运行真实 PowerShell 构建脚本，用原生小程序替代外部 Keil/fromelf 进程，以检查文件和退出状态，不能替代真实 Keil 编译：

```powershell
python -B .\car\tests\test_build_script.py
```

该检查需要 Python 3、PowerShell 和原生 Windows GCC；其他机器通过环境变量 `AUTOCAR_HOST_CC` 指定 GCC 可执行文件。最近的四项检查记录见 [folder-cleanup-tests.log](../outputs/reports/historical-logs/folder-cleanup-tests.log)；初始化阶段的三项记录仍保留在 [build-script-tests.log](../outputs/reports/historical-logs/build-script-tests.log)。测试临时目录在 `tmp/build-script-tests/`，任务结束统一清理。

## 首次上板

先阅读 [硬件与引脚](hardware-and-pins.md) 和 [逐飞助手调试指南](seekfree-assistant-debugging.md)。当前图像输出为普通IPS200 SPI屏 + Wi-Fi SPI TCP图传，188×120灰度及真实三条边线，心跳诊断和DAP图传已移除。网络参数已写入本机配置，含密码，不公开复制。先开电脑2.4GHz热点与助手TCP Server8080侦听，再编译下载并复位车端。

2026-10-03的CSI3.3V/5V_EXT灯灭问题经对照确认：DAP断开、仅电池带全部外设时灯正常，当前用此方式运行；具体DAP/时序影响待进一步核实。实际烧录由用户执行，自动化未操作芯片下载或验证屏幕/无线收图。当前电机输出关闭；舵机上电无PWM，由S2/S3控制参考测试，见舵机指南。

## Flash算法路径与下载错误

2026-10-01定位到 `Cannot Load Flash Device Description!` 的原因：旧模板在设备包根目录下引用 `arm/MIMXRT1064_QSPI_4KB_SEC.FLM`，但NXP.MIMXRT1064_DFP.19.0.0的实际路径是：

```text
D:\Arm\Packs\NXP\MIMXRT1064_DFP\19.0.0\devices\MIMXRT1064\arm\MIMXRT1064_QSPI_4KB_SEC.FLM
```

工程保留设备包宏以适应安装位置变化，正确的算法引用为：

```text
$$Device:MIMXRT1064DVL6B$devices\MIMXRT1064\arm\MIMXRT1064_QSPI_4KB_SEC.FLM
```

已修正 `autocar.uvoptx` 中CMSIS-DAP和两个备用驱动记录的三处算法路径，同时按官方PDSC修正 `autocar.uvprojx` 的设备头文件及外设描述XML路径。算法文件存在，ARM fromelf可解析其FlashDevice和编程/擦除入口，描述与工程参数一致：

| 项目 | 正确值 |
| --- | --- |
| 算法显示名称 | `MIMXRT106x 4mB Winbond QSPI Flash` |
| Flash起始地址 | `0x70000000` |
| Flash容量 | `0x00400000`，4 MiB |
| 算法工作RAM起始地址 | `0x20000000` |
| 算法工作RAM大小 | `0x00008000`，32 KiB |

Keil若保持打开，可能仍使用内存中的旧工程设置。重新加载工程后再下载；不要用旧配置覆盖已修正的磁盘文件。若界面仍保留旧条目，可在 **Options for Target → Utilities → Settings → Flash Download** 中移除旧算法，再通过 **Add** 选择上述4 MiB QSPI算法，并核对地址和RAM参数后保存。

配置修正时只验证了引用与算法文件结构，自动化工具未执行芯片擦除或下载。用户随后报告已烧录，目前继续排查串口零接收；烧录成功不等于图传可用。原工程配置已备份到 `outputs/backups/`，修改记录在 [2026-10-01.log](../logs/2026-10-01.log)。

## 报告保留策略

`outputs/reports/`保留经审核的重要验收与兼容性报告。链接map和机器校验清单按临时材料管理，必要溯源信息并入主报告；构建脚本只导出AXF/HEX/BIN，不再向reports复制map。

最新供电对照：用户确认仅电池供电、摄像头/屏幕/Wi-Fi全部接回后仍全亮。烧录后彻底断电并移除DAP排线，再以电池运行；详细依据见硬件文档。

2026-10-03当前交付：按用户要求，屏幕/Wi-Fi恢复14:32:41同步版本并冻结，舵机模块及PIT1的1ms计时保留。异步模块的3个工程引用已移除，工程现有343个文件引用，下载器/Flash设置不变。关闭并重新打开工程后Rebuild，避免Keil内存中的旧设置或旧AXF被再次使用。

本轮全量构建0错误0警告；Code=85208、RO=14004、RW=312、ZI=104216字节。日志见 [回退构建](../outputs/reports/historical-logs/display-wifi-rollback-build.log)、[回退回归](../outputs/reports/historical-logs/display-wifi-rollback-tests.log)、[算法测试](../outputs/reports/historical-logs/display-wifi-rollback-algorithms.log)。冻结范围与旧版同步等待行为统一见 [图传指南](seekfree-assistant-debugging.md#屏幕与wi-fi冻结约束)。舵机操作见 [舵机指南](servo-debugging.md)。

当前C13开关阶段：上电默认关闭Wi-Fi，C13/S4切换；复用已有源/头/配置，无新增工程引用。图传核心冻结不变，具体授权例外见 [开关说明](seekfree-assistant-debugging.md#c13-wi-fi开关)。本轮全量构建0错误0警告，Code=85776、RO=14040、RW=312、ZI=104224字节；日志 [wifi-key-switch-build.log](../outputs/reports/historical-logs/wifi-key-switch-build.log)。下载前Rebuild，实物按键/复位和帧率待验证。

历史舵机校准阶段（已结束）：直接使用主工程build.ps1构建，0错误0警告；Code=85704、RO=12584、RW=312、ZI=104232字节，日志 [servo-calibration-build.log](../outputs/reports/historical-logs/servo-calibration-build.log)。既有工程引用不变，Keil实际下载目录和outputs/firmware均更新为手动校准版。用户现已确认1480µs并恢复±60µs摆动，见 [当前控制与校准记录](servo-debugging.md)。

当前正式程序恢复阶段：用户确认中值1480µs后，已局部恢复原S2摆动/S3停止程序，摆幅±60µs（1420–1540µs），临时校准代码与测试撤回。实际主工程全量构建0错误0警告，Code=85776、RO=12620、RW=312、ZI=104224字节；日志 [servo-center-restore-build.log](../outputs/reports/historical-logs/servo-center-restore-build.log)。Keil实际AXF和发布固件同步更新，未自动烧录；扩大行程待用户上板观察。

2026-10-04电机/编码器阶段：新增6个源/头/配置引用，总计349个引用；需重新加载Keil工程。实际主工程全量0错误0警告，Code=88992、RO=12736、RW=312、ZI=104312字节。日志 [motor-encoder-build.log](../outputs/reports/historical-logs/motor-encoder-build.log)，使用步骤见 [电机编码器指南](motor-encoder-debugging.md)。已同步实际下载AXF和outputs/firmware，未自动烧录。

2026-10-07虚拟手柄阶段：新增remote_command、remote_control及配置，工程文件引用为354个，需要重新加载Keil工程。默认REMOTE_CONTROL_ENABLED=1，设0并重编译恢复原按键测试。原图传发送核心保留，接收及安全边界见 [遥控指南](virtual-remote-control.md)；最终全量回归/编译以 [virtual-remote-suite.log](../outputs/reports/historical-logs/virtual-remote-suite.log)、[virtual-remote-build.log](../outputs/reports/historical-logs/virtual-remote-build.log)为准。不会自动烧录或操作实车。

最终遥控构建：53项主机unittest通过，另19项算法检查通过；默认遥控模式Keil0错误0警告，Code=91864、RO=12952、RW=312、ZI=104412字节。独立测试模式REMOTE_CONTROL_ENABLED=0也完成隔离全量编译0错误0警告（未发布该测试镜像）。已同步实际Keil下载目录及outputs/firmware遥控固件，未进行硬件下载。详见上述日志和 [遥控操作指南](virtual-remote-control.md)。
