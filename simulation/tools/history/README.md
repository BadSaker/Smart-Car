# 历史复现工具

这些脚本保存2026-10-11至12日各功能交付时的检查，包含旧相机参数、旧DLL和性能备份依赖。它们不属于当前版本的验收入口，也不能直接用当前默认参数证明旧版本结果。复现前从对应备份恢复完整夹具，并检查脚本中的报告、参数与DLL路径。

当前入口为 `simulation/tests`、`../check_current_ui.py`、`../validate_control.py`。历史脚本会生成输出，只能在任务临时副本中执行。

2026-10-14归档后，旧`simulation/reports/`与`simulation/backups/`文件分别位于`outputs/reports/simulation-history-2026-10-14/`及`outputs/backups/simulation-history-2026-10-14/`。历史脚本保留原路径和版本假设；复现时在`tmp/<任务名>/workspace/simulation/`复制对应历史源码，恢复reports/backups布局，再运行该时期工具。不能把旧路径输出写回持久归档，也不能借旧工具修改当前主工程。当前启动器与界面诊断已使用runtime目录，旧工具如读取startup-timing等文件需恢复匹配版本，而非把新输出当作旧版本证据。
