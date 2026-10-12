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

- 仿真功能、共享源码与软件验收：[simulation/README.md](simulation/README.md)
- 多设备热点、本地工具配置：[docs/local-environment.md](docs/local-environment.md)

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

## 仿真与多设备协作

- `simulation/`用于合成188×120图像、C算法闭环、参数实验、原始帧回放、实测截图对照和停车证据采集。入口与当前检查见其README。
- 算法唯一维护位置为`car/src`、`car/include`及`car/config`；仿真生成参数覆盖头和DLL，不回写主工程。正式验收导出来源快照及SHA-256。
- 活动参数只读取`simulation/configs/*.json`；历史参数放入`configs/archive/`，不进入主选择列表。移动配置须同步检查脚本默认路径及文档引用，不改变历史JSON内容。
- 不重新维护`simulation/vendor`算法副本；其来源说明和许可证保留。默认基线、分档验证、参考控制器与用户参数的职责见仿真参数文档，不因当前内容相同而删除用户入口。
- 地图真值仅用于渲染、理想参考控制和评分；理想参考通过不计为视觉算法通过。仿真、Keil构建、烧录和实车结果分别记录。
- 热点凭据、本机工具路径、外部参数登记及缓存不提交Git。`configure-wifi.ps1 -CheckOnly`只读；生成配置不会启停热点或操作硬件。
- 当前检查入口为`simulation/tests`、`simulation/tools/check_current_ui.py`和`validate_control.py`；历史工具有独立版本依赖。
- 本次已授权多设备配置入口整理；屏幕与Wi-Fi同步连接/发送/重试逻辑仍遵守冻结约束，其他图传改造须另行审核。
