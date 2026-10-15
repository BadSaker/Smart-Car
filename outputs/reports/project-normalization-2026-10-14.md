# 项目规范整理与行为等价验收 — 2026-10-14

本次在用户批准“全部执行”后，完成协作入口、当前文档、仿真历史材料与运行输出、局部代码可读性整理。以2026-10-14运动稳定性交付为基线，未改控制参数、算法公式、状态机条件、公共接口或硬件接线。未提交Git，未烧录、通电、操作热点或车辆。

## 基线与修改归属

只读审查读取并计算了20,468个非Git内部文件的SHA-256，约1.93GB，无读取失败；217份选定自维护文本可按UTF-8读取。365个Keil文件引用存在。10月14日交付证据中167份final-source与修改前工作区逐字节一致，三份发布固件与该报告一致。

本机历史覆盖65份相关会话记录、49个会话ID（含续接和辅助会话），检查392条用户消息、683条助手公开消息。记录中存在显式读取AGENTS.md，父目录误选不能证明所有旧修改均违规。其他设备或应用的开发者聊天未获取；其工作以项目文件、日志和交付证据核对。历史授权不扩展到新任务。

| 历史内容 | 本轮处理 |
| --- | --- |
| 多设备热点入口与wifi目录迁移 | 保留有效实现，修正冻结说明的旧路径；真实配置与备份保持本机隔离 |
| 共享C算法与源码签名 | 保留car为唯一维护来源，不恢复vendor副本 |
| 独立GCC与工具发现 | 保留项目工具链与许可证，诊断迁至runtime |
| Tk版本及Qt/GPU回退 | 保留当前Tk实现与既有回退备份，不恢复已撤销优化 |
| 10月13日旧模型/旧速度结果 | 当前文档明确标历史，不改写原报告或历史参数 |
| 10月14日运动修复 | 保留控制行为、参数和实车限制；仅局部排版及说明整理 |
| 无法独立核实的旧授权与实车身份 | 不补写成已确认，不据此恢复或删除材料 |

## 已完成的整理

AGENTS.md继续为规则正文，CLAUDE.md只导入共享规则。明确Autodrive项目根、历史授权日期及冻结边界；增加只读结构检查入口。当前导航指向最新交付，环境文档维护构建、自动驾驶指南维护参数使用、硬件文档维护实测事实、仿真控制文档维护模型分档，避免在导航复制当前参数。

147份旧simulation/reports文件迁入[历史报告目录](simulation-history-2026-10-14/)，15份旧simulation/backups文件迁入[历史备份目录](../backups/simulation-history_20261014_183830/)。162份原文件均保持文件名、内容和SHA-256；原目录保留README索引。历史正文、JSON中的旧路径不改写，相对路径按迁移前位置解释；旧工具在独立任务工作区恢复完整匹配布局再复现。

启动器、启动包装、界面异常/计时和编译器发现诊断统一写simulation/runtime，可用SIMULATION_RUNTIME_ROOT指定任务目录。启动器只读兼容旧reports及归档位置的解释器缓存；旧缓存内容不改写。日常实验导出继续使用runs；正式验收中间材料进入本任务tmp，正式报告进入outputs。

三个Python模块（dynamics、encoder、source_layout）展开压缩语句并补充单位/约束说明；AST完全一致。track_vision仅展开阈值统计的多语句同行，C词法序列和预处理行保持一致。未执行全工程自动格式化、代码拆分或文件批量改名。

## 验证结果

| 验证 | 结果 |
| --- | --- |
| 工程完整unittest | 146/146通过；包含构建工具替身与Wi-Fi布局检查 |
| 仿真完整unittest | 55/55通过 |
| GUI | 11活动预设、参数往返、固定高度拟合、1100×720/1480×930、2D/3D及运行后摄像头渲染通过 |
| 入口兼容 | 7项通过：显式解释器、新缓存、旧缓存、归档缓存、失效缓存回退、正确项目根、错误父目录拒绝；在中文/空格目录执行 |
| 行为等价 | AB/BA各1600个10ms周期，共3200周期；36列控制/姿态/轮速数据、77列视觉诊断、12列运动诊断、全部视觉行和160帧图像完全一致；另30组实测夹具/全黑/全白/随机原始帧输入完全一致 |
| Keil隔离全量构建 | ARM Compiler6.21，0错误0警告；Code138936、RO13296、RW300、ZI110260，和原交付相同 |
| 可烧录字节 | 隔离HEX、BIN与现有发布文件逐字节一致；AXF因源码位置/调试信息变化不同，不宣称AXF相同 |
| 保护与迁移 | 945份受保护对象哈希不变，162份迁移原文件哈希一致 |

首次受限执行的回归因临时目录与假工具启动WinError5失败，失败日志保留。使用新的正常执行身份副本重跑全量后通过，没有放宽断言或修改系统ACL。首次GUI与仿真回归并行争用同一隔离DLL缓存，随后串行重跑通过，没有为此修改产品缓存逻辑。等价验证脚本初次误用不存在的启动方法，改为现有started接口后完成；只修改任务验证脚本。

完整工程和仿真回归均在本任务复制的工程内运行，避免旧测试固定tmp路径写入主工作区。Keil在独立build-workspace内运行。未重复完整赛道验收：本轮不改变行为，采用AST/词法、固定输入对照、现有回归与可烧录字节一致性证明；原运动修复整圈结果及实车限制继续以[原交付](motion-stability-2026-10-14.md)为准。

当前及非聊天文档51份文件链接可达；另检查保留的历史聊天，17处旧绝对路径格式链接早已失效，原聊天字节保持不变，不通过改写原话伪造历史。工程引用仍为365份全部存在。

## 保护与交付

冻结main/isr/camera_debug/camera_display及头文件、car/config全部参数、library、MDK、工程/下载设置、wifi配置、原始参考、历史及活动JSON、原始测量素材、原发布三份固件及tmp/build-autocar均纳入保护清单。945份对象保持SHA-256。源码图像尺寸、C13逻辑、SPI参数、同步连接/发送/重试/重连未改。

- 修改前备份：car_before-project-normalization_20261014_183451.zip（已于 2026-10-15 精简中删除），SHA-256 `7105064dbe46ba692e419af08ee90be38d53f4a71917df3108d7a540b6947d57`；本机私有，Git忽略。
- 验证证据：[project-normalization-evidence-2026-10-14.zip](project-normalization-evidence-2026-10-14.zip)，78项，逐项SHA-256和ZIP CRC通过，包SHA-256 `d41a5c6d0b8228859b321ef2e10c61c8e2b1200616db6434d2839c1d8bde3c2a`。含迁移清单、保护摘要、源码快照、对照数据、验证脚本、成功/失败日志及GUI截图；本机私有，Git忽略。
- 原发布及实际下载固件保持原字节；隔离验证固件另名保存，未替代正式发布。
- 隔离验证固件：[autocar_normalization_check_20261014_184702.axf](../firmware/autocar_normalization_check_20261014_184702.axf)，SHA-256 `748dc9eb1deedf599bc65dca0bc90f5ea029d93f1acb6db43ba06d62a2af6868`。
- 隔离验证固件：[autocar_normalization_check_20261014_184702.hex](../firmware/autocar_normalization_check_20261014_184702.hex)，SHA-256 `147566201a6314e832d6258d034f71f062734952a833b88dc0eb158caf375373`。
- 隔离验证固件：[autocar_normalization_check_20261014_184702.bin](../firmware/autocar_normalization_check_20261014_184702.bin)，SHA-256 `d6b4a3098ac216fcd91aba681f2fb95e9302fbcc8f9bffad21dda52661a5d6cf`。

## 精确修改范围

除下列文件、上述162份迁移和新增交付物外，不执行其他整理。迁移逐文件旧/新路径与哈希位于证据包verification/final-verification.json。

- `.gitignore`
- `AGENTS.md`
- `car/scripts/check_project_layout.ps1`
- `car/src/track_vision.c`
- `docs/README.md`
- `docs/autonomous-driving.md`
- `docs/environment-and-build.md`
- `docs/known-issues.md`
- `docs/local-environment.md`
- `docs/project-overview.md`
- `docs/seekfree-assistant-debugging.md`
- `docs/standards/files.md`
- `docs/standards/workflow.md`
- `outputs/backups/simulation-history_20261014_183830/README.md`
- `outputs/reports/simulation-history-2026-10-14/README.md`
- `simulation/README.md`
- `simulation/app.py`
- `simulation/backups/README.md`
- `simulation/docs/control-tuning.md`
- `simulation/dynamics.py`
- `simulation/encoder.py`
- `simulation/launch.ps1`
- `simulation/reports/README.md`
- `simulation/runtime_paths.py`
- `simulation/source_layout.py`
- `simulation/start_app.py`
- `simulation/toolchain.py`
- `simulation/tools/history/README.md`
- `simulation/tools/启动交互检测.cmd`
- `simulation/启动仿真.cmd`

## 使用与后续边界

从Autodrive根运行 `car/scripts/check_project_layout.ps1`；从其他目录调用时显式传 `-ProjectRoot <Autodrive目录>`。脚本只读核对入口、365个工程引用和11份共享算法文件，不证明协作规则已被阅读。

现有tmp旧测试目录、field-robustness材料、figs及下载缓存均保留，属于其他任务或仍有用途，不在本次删除范围。只清理tmp/project-normalization-20261014；新持久备份、归档、报告、验证固件不作为临时材料清理。后续若清理旧材料，须先列精确目标、保留理由及证据归档情况。

工作区已有Git变更和未跟踪交付未重置、未暂存、未提交；不创建仓库或改写历史。本机凭据、解释器/工具路径诊断、验证固件和私有证据保持隔离；本轮没有外发或发布。历史固件仍可能包含凭据，Git忽略不能清除旧历史。

实车PWM—轮速、舵机中间段/响应、滚动周长、相机标定、真实OVERSPEED原因与现场元素表现仍待验证。控制参数或行为调整、冻结模块修改、刷写及实车操作必须按新的具体范围批准。

收尾已完成：持久证据包逐项SHA-256及CRC复核通过，备份CRC和原文件SHA-256复核通过；仅删除本任务临时目录（4731个条目），tmp根及23个既有任务目录保留。清理后再次核对945份保护对象、162份迁移和证据包最终源码快照，全部一致。
