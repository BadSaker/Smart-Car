# 仿真项目内 Python 实施计划

用户已批准完整 Python 与依赖均部署在 simulation 内，并验证脱离 D 盘 Anaconda。

目标：simulation/python 包含完整 Windows x64 CPython、Tk/Tcl、NumPy、Pillow；启动器仅允许此环境。采用 Astral python-build-standalone 的固定发行包，记录 SHA-256；不复制 Anaconda，不修改系统设置和 car 算法。

执行：在当前获授权工作区连续实施，不提交 Git。采用 writing-plans、executing-plans、test-driven-development 和 verification-before-completion 流程；临时产物仅放 tmp/simulation-python，交付后清理。

- [x] 部署：核实发行包和 wheel 版本/哈希，新增 simulation/python-runtime.json、requirements.lock、setup_python.ps1；保留运行时许可证，初始化脚本可重复执行，已有不匹配环境不覆盖。
- [x] 启动：先写真实 PowerShell 启动回归，确认旧启动器会误用外部环境，再修改 launch.ps1、增加 runtime_check.py 和 run_python.ps1。取消缓存/注册表/PATH/SIMULATION_PYTHON 解释器发现，环境缺失明确失败；子进程清理 Python/Tcl 注入及外部 PATH，用户包禁用。
- [x] 检查：本地解释器、标准库、扩展、包及 Tcl 加载路径；旧缓存与污染变量；缺失环境不得回退；中文空格路径；Tk 窗口；全部 simulation/tests；现有 GUI 检查；真实 C 桥接编译加载。
- [x] 交付：更新 README、本地环境文档、忽略规则、日志和报告；保留历史证据但不再读取旧缓存；检查差异、清理本任务临时目录。

验证以命令退出码、实际模块/DLL加载路径和 GUI 结果为准，软件验证不代表实车验收。

