# simulation 独立 Python 交付与验证

2026-10-28完成用户批准的项目内完整Python部署。日常仍双击`simulation/启动仿真.cmd`，固定启动`simulation/python/pythonw.exe`。本轮未修改car源码、系统PATH、注册表或D盘Anaconda，未提交Git，未进行实车验证。

## 部署

运行时为CPython 3.13.18 Windows x64（约199 MiB），来自[Astral python-build-standalone 20261026](https://github.com/astral-sh/python-build-standalone/releases/tag/20261026)，SHA-256为`ebe90cc1dce2dd2d7afa1f0aa9b6c269c7c0174aeabdc6a801381602b7283424`。Tk/Tcl随包部署，Tcl 8.6.15；NumPy 2.5.0、Pillow 12.3.0来自PyPI固定Windows cp313 wheel。来源、文件名和哈希保存在[发行清单](../../simulation/python-runtime.json)，包哈希锁定在[requirements.lock](../../simulation/requirements.lock)。原始许可证随运行时及各包保留。

`setup_python.ps1`负责部署/检查；完整下载缓存保留在`simulation/runtime/downloads`，可离线重新部署。运行时和缓存不提交Git。已有不同版本或不完整的运行时不自动覆盖；需先人工移走后重新初始化。

`launch.ps1`不再读取历史解释器缓存、注册表或系统Python；`SIMULATION_PYTHON`不再控制解释器。启动及命令行包装`run_python.ps1`清理子进程Python/Conda/Tcl污染变量，设置项目内Tk/Tcl、受控PATH，使用`-E -s -B -X utf8`排除外部Python环境变量和用户包目录。`runtime_check.py`真实导入库并核对版本及路径。初始化额外禁用全部pip配置和外部pip缓存，避免外部target/prefix改变安装位置。

## 验证结果

| 验证 | 结果与证据 |
| --- | --- |
| 完整仿真测试 | **59/59通过**，含4项新增启动/部署回归；[测试记录](simulation-python-2026-10-28/tests.log) |
| 外部环境隔离 | 旧缓存和SIMULATION_PYTHON指向Anaconda，PYTHONHOME/PYTHONPATH/PYTHONUSERBASE/TCL_LIBRARY/TK_LIBRARY指向无效外部位置时，本地检查仍通过 |
| 缺失环境 | 项目内Python不存在时明确失败，不能回退外部解释器；旧启动器在同一回归下曾错误选中Anaconda |
| pip配置隔离 | 外部pip.ini强制virtualenv时先复现失败；设置PIP_CONFIG_FILE=nul后通过 |
| 真实GUI | 预设、标定、参数往返、大小窗口、3D/相机渲染通过；151帧，行驶约0.3291m；[结果](simulation-python-2026-10-28/gui-result.json)、[截图](simulation-python-2026-10-28/camera-after-ticks.png) |
| pythonw与DLL | 通过正式启动器打开/关闭真实App；Python、Tk/Tcl、NumPy、Pillow模块及运行时DLL均来自simulation/python；已加载模块无Anaconda；[模块清单](simulation-python-2026-10-28/independent-python-check.json)、[启动日志](simulation-python-2026-10-28/launcher.log) |
| 中文空格路径 | 复制运行时至本任务中文空格目录，按新位置启动检查通过；[证据](simulation-python-2026-10-28/relocated-check.json) |
| 重复初始化 | 已有环境重复执行成功，版本一致、pip check通过；[记录](simulation-python-2026-10-28/setup-repeat.log) |
| 新C桥接编译 | 使用独特临时参数生成新DLL，不复用旧DLL；项目内GCC编译零诊断并加载成功；[证据](simulation-python-2026-10-28/native-check.json) |

全部软件验证通过受控启动环境完成，未通过卸载或重命名用户Anaconda来验证。受限沙箱中Tcl初始化失败，正常桌面权限下同一环境及GUI通过；该限制不属于缺失依赖。历史报告中的D盘路径保留为原始证据，现启动器不会读取它们。

## 使用

在Autodrive根目录：

```powershell
# 重建或补齐固定环境（首次需要联网，完整缓存可离线）
powershell -NoProfile -ExecutionPolicy Bypass -File simulation/setup_python.ps1
# 仅检查
powershell -NoProfile -ExecutionPolicy Bypass -File simulation/setup_python.ps1 -CheckOnly
# 运行全部仿真回归
powershell -NoProfile -ExecutionPolicy Bypass -File simulation/run_python.ps1 -m unittest discover -s simulation/tests -p 'test_*.py' -v
# 短暂打开并自动关闭GUI，输出模块来源至simulation/runtime
powershell -NoProfile -ExecutionPolicy Bypass -File simulation/launch.ps1 -EntryPoint tools/check_independent_python.py
```

命令行入口使用独立PowerShell进程，避免清理环境变量影响交互会话。GCC按原工具链机制工作，Python运行时不替代GCC。换机器首次克隆须运行初始化脚本，不能仅依赖Git忽略的本地二进制。
