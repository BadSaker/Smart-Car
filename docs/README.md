# 文档导航

本项目把逐飞 RT1064 资料、竞赛要求和智能车应用工程整理在同一工作区。日常开发入口是 [car/autocar.uvprojx](../car/autocar.uvprojx)。参考资料中的器件型号、例程的默认引脚和推荐参数不代表当前实车配置。

## 接手阅读顺序

1. 阅读 [AGENTS.md](../AGENTS.md)，了解计划审核和执行边界。
2. 阅读 [项目概况](project-overview.md)，了解目录职责与当前阶段；先核对 [屏幕与Wi-Fi冻结约束](seekfree-assistant-debugging.md#屏幕与wi-fi冻结约束)，不得沿用已撤销的异步优化计划。
3. 阅读 [环境与构建](environment-and-build.md)、[已知问题](known-issues.md)，确认可复现的构建方式和未解决事项。
4. 需要接线、移植外设或调车时，阅读 [硬件与引脚](hardware-and-pins.md)。
5. 设计车辆功能前阅读 [竞赛规则](competition-rules.md)；查找库用法时进入 [例程导航](examples/README.md)。
6. 检查 [logs](../logs/) 中最近的 `.log`，再决定本次工作的优先级。

当前阶段：C15自动驾驶与视觉/PID接入，运行前阅读 [自动驾驶指南](autonomous-driving.md)；最新构建与实车边界以指南和日志为准。

## 标准与资料

| 内容 | 入口 | 何时使用 |
| --- | --- | --- |
| 文件组织与持久产物 | [files.md](standards/files.md) | 新增、移动文件或整理产物 |
| 应用代码约定 | [code.md](standards/code.md) | 实现、移植、审查代码 |
| 名称与单位 | [naming.md](standards/naming.md) | 新增接口、参数、文件 |
| 摄像头可视化调试 | [seekfree-assistant-debugging.md](seekfree-assistant-debugging.md) | IPS200 SPI显示、Wi-Fi SPI图传与逐飞助手1.2.7配置 |
| S-U400舵机校准与测试 | [servo-debugging.md](servo-debugging.md) | 已确认1480µs中值，正式按键摆动1420–1540µs及校准历史 |
| 当前虚拟手柄遥控 | [virtual-remote-control.md](virtual-remote-control.md) | Wi-Fi文本手柄、C12本地允许、Down确认及有限时驱动 |
| 电机与编码器测试 | [motor-encoder-debugging.md](motor-encoder-debugging.md) | C12有限测试、STEP×1、4352计数/轮圈及64mm轮速换算 |
| PowerShell 执行与清理 | [commands.md](standards/commands.md) | 检索、构建、验证、清理 |
| 审核与交付 | [workflow.md](standards/workflow.md) | 开始新任务、变更范围、交付 |
| 原始竞赛与硬件资料 | [参考](../参考/) | 核对规则、宣讲、原理图 |
| 原始逐飞开源库与例程 | [SeekFree](../SeekFree/) | 对照库实现、说明书、例程 |
| 最终产物 | [outputs](../outputs/) | 查找固件、报告、备份 |

## 信息维护

- 已核实内容要给出本地来源、版本或验证结果。推断、推荐和待确认项要明确标识。
- 规则发生更新时，先核对主办方的新资料，再同步规则摘要和受影响的设计；不以宣讲建议替代正式规则。
- 环境路径、编译器版本、命令和构建结果由环境文档维护；本页只负责导航，避免复制后失效。
- 硬件尚未确认的项目集中在硬件文档；代码使用某个默认值时应记录其依据和适用范围。
- 任务过程记录进入每日日志，长期有用的结论归入相应文档，最终可复用结果保存在 `outputs/`。


当前资料路径检查（2026-10-08）：工作区未找到SeekFree/原始目录，指向该目录的历史例程索引不能本地打开；car/library副本仍完整可构建，参考/仍存在。本次没有删除或恢复该目录；新增自动驾驶入口与工程引用有效。
