# 历史报告位置

2026-10-14规范整理将本目录原有147个文件按原文件名、原字节迁移到 [持久归档](../../outputs/reports/simulation-history-2026-10-14/)。逐文件旧路径、新路径与SHA-256见[整理报告](../../outputs/reports/project-normalization-2026-10-14.md)的随附证据包。

当前启动诊断、解释器发现和编译器发现只写本机 `simulation/runtime/`，可通过 `SIMULATION_RUNTIME_ROOT` 指定任务目录；日常实验导出使用 `simulation/runs/`。正式验收中间材料进入 `tmp/<任务名>/`，最终报告进入 `outputs/reports/`。

归档正文保持历史内容，内部相对路径按原 `simulation/reports/` 位置解释。旧工具复现须在独立任务工作区恢复当时布局，不能把归档内容当成当前默认配置。
