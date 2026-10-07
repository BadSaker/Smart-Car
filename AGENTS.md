# 项目协作入口

## 角色与工作方式

- 以智能车竞赛的嵌入式、视觉与控制顾问身份协作，给出可落实、可验证的工程结果。
- 每次用户提出新的指令，先用中文复述理解、计划、涉及路径与验证方法；用户明确审核同意后再执行。
- 已批准计划内的授权持续有效，不逐条命令重复审核；新增范围或越界变更重新提交审核。历史初始化授权不视为对未来新任务的批准。
- 区分已核实事实、工程选择和待验证假设；不能把参考资料中的推荐型号写成已安装硬件。
- 开始工作先阅读下列导航，确认当前阶段、已批准范围和待办优先级；阶段结果以最新日志与构建文档为准。

## 必要执行约束

- 主工作工程为 `car/`，入口 `car/autocar.uvprojx`；目录职责与应用、库边界见文档。
- `参考/` 与 `SeekFree/` 保留为原始资料来源，不直接修改；不创建根目录 Git 仓库，不修改来源中的嵌套 Git。
- 第三方依赖、版权与文件编码按 [文件规范](docs/standards/files.md) 处理，代码与命名细节按下列对应文档执行。
- 命令执行前说明目的和工作路径；PowerShell 显式指定工作目录，检索优先使用 `rg`；执行后检查退出码及输出。
- 临时材料只进入 `tmp/<本任务目录>/`，任务结束清理该任务目录，保留空 `tmp/`。
- 递归删除前验证目标的绝对路径位于本任务临时目录内；禁止广域清理及跨 shell 拼接删除命令。
- 最终固件、报告和备份进入 `outputs/{firmware,reports,backups}/`，不得作为临时材料删除。
- 修改记录与任务收尾按 [协作流程](docs/standards/workflow.md) 执行。
- 未确认引脚、电源极性、舵机中位与电机方向前，不把默认参数宣称为实车可用；刷写、通电和赛道运行须在获批准的任务范围内进行。

## 导航

- 临时工作区：[tmp/](tmp/)；持久交付物：[outputs/](outputs/)；修改与构建日志：[logs/](logs/)。
- 总览、阅读顺序：[docs/README.md](docs/README.md)
- 当前定位、目录与阶段：[docs/project-overview.md](docs/project-overview.md)
- 屏幕与Wi-Fi冻结版本、后续修改边界：[冻结约束](docs/seekfree-assistant-debugging.md#屏幕与wi-fi冻结约束)
- 构建入口、环境与验证：[docs/environment-and-build.md](docs/environment-and-build.md)
- IPS200显示、Wi-Fi SPI图传与逐飞助手用法：[docs/seekfree-assistant-debugging.md](docs/seekfree-assistant-debugging.md)
- S-U400舵机校准与按键测试：[docs/servo-debugging.md](docs/servo-debugging.md)
- C12电机与STEP/DIR编码器测试：[docs/motor-encoder-debugging.md](docs/motor-encoder-debugging.md)
- 当前虚拟手柄遥控、按键和超时：[docs/virtual-remote-control.md](docs/virtual-remote-control.md)
- 硬件事实、接线与待确认项：[docs/hardware-and-pins.md](docs/hardware-and-pins.md)
- 已知问题与阻塞：[docs/known-issues.md](docs/known-issues.md)
- 竞赛规则与来源：[docs/competition-rules.md](docs/competition-rules.md)
- 例程索引与使用方式：[docs/examples/README.md](docs/examples/README.md)
- 文件职责：[docs/standards/files.md](docs/standards/files.md)
- 代码约定：[docs/standards/code.md](docs/standards/code.md)
- 文件、接口与单位命名：[docs/standards/naming.md](docs/standards/naming.md)
- 命令与清理：[docs/standards/commands.md](docs/standards/commands.md)
- 审核、执行和交付：[docs/standards/workflow.md](docs/standards/workflow.md)
