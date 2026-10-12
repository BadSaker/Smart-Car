# 当前软件验收入口

当前入口：`check_current_ui.py --output <任务目录>`检查参数、保高标定和实际Tk窗口；`validate_control.py --config <预设> --output <任务目录> --directions AB BA`执行独立闭环；`snapshot.py --output <任务目录>`导出明确源码输入。

`check_camera_alignment.py`与`check_camera_ui.py`为相机专项，输出必须使用任务目录。实测图像默认位置为`../assets/measurements`。运行前检查其CLI参数及环境。

其余检查、性能剖析和旧GUI工具仅用于历史复现，入口见`history/README.md`。新验收从当前tests运行，不以历史脚本默认参数断言判断当前状态。
