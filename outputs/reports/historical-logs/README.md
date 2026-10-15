# 历史日志归档

`logs/` 已收敛为每日一份 `YYYY-MM-DD.log`。本目录保存此前被文档引用的单次构建/测试/调试日志（原 `logs/*.log`），按原文件名保留，供历史记录追溯。

引用该目录的文档已同步更新路径（`logs/<name>` → `outputs/reports/historical-logs/<name>`）。当前构建/测试日志不再写入 `logs/`，而由 `car/scripts/build.ps1`、`test.ps1` 写入 `car/build/`（不提交）。未被任何文档引用的一次性过程日志已在此次整理中删除。
