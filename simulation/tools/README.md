# 当前软件验收入口

当前入口：`check_current_ui.py --output <任务目录>`检查参数、保高标定和实际Tk窗口；`validate_control.py --config <预设> --output <任务目录> --directions AB BA`执行独立闭环；`snapshot.py --output <任务目录>`导出明确源码输入。

`probe_reality_gap.py --config <预设> --output <新任务子目录>`用于执行边界诊断，不替代正常验收。它编译当前C算法和真实`servo_control.c`，仅替换GPIO/PWM硬件边界。`--hardware-servo`使用该舵机代码的整数步进和名义PWM；`--service-ms 40`只改变服务间隔，`--frame-period-ms 40`只改变图像提交周期，`--main-loop-ms 40`同时改变两者（简化有效间隔，不是板上循环耗时测量）。`--pause-ms 150 --inject-ms 10000`暂停图像提交与服务；`--image-delay-ms 160 --frame-age-aware`把延迟解释为CSI完成后的处理积压，按固件帧龄拒绝旧帧。参数都是受控假设，不能写成实测。

诊断输出含10ms遥测、消费帧/渲染帧区分、原始参数、编译输入和SHA-256。进程退出0表示实验正常执行，车辆是否通过看`pass_run`；短观察窗的`time_limit`不是整圈通过。PWM为驱动接口名义脉宽，未模拟计数器二次量化、50Hz重载、IRQ抢占或侧滑。故障即停止采集，不包含完整故障后2秒窗口。具体发现见[2026-10-28差异报告](../../outputs/reports/reality-gap-2026-10-28.md)。

统一通过项目内环境运行，命令见[仿真说明](../README.md)。`check_independent_python.py`应通过`launch.ps1 -EntryPoint tools/check_independent_python.py`启动，短暂打开并关闭真实GUI，核对Python/Tk/Tcl和Windows已加载模块来源；结果写入运行目录的`independent-python-check.json`。

`check_camera_alignment.py`与`check_camera_ui.py`为相机专项，输出必须使用任务目录。实测图像默认位置为`../assets/measurements`。运行前检查其CLI参数及环境。

其余检查、性能剖析和旧GUI工具仅用于历史复现，入口见`history/README.md`。新验收从当前tests运行，不以历史脚本默认参数断言判断当前状态。
