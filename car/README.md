# Autocar RT1064 工程

打开 [autocar.uvprojx](autocar.uvprojx)，目标autocar；本轮新增模块后需要重新加载工程并Rebuild。

当前C15/S2启动或停止自动驾驶，C14/S3停车；C12用于虚拟手柄本地允许，自动行驶中按C12则取消。C13切换Wi-Fi，上电OFF。电机/舵机上电不产生运动命令，舵机中位1480µs。当前C15不再执行舵机摆动，遥控与自动模式互斥。

- [自动驾驶使用、视觉/PID与终点标定](../docs/autonomous-driving.md)
- [自动驾驶调参](config/autonomous_config.h)、[视觉调参](config/vision_config.h)
- [构建和完整回归](../docs/environment-and-build.md)
- [虚拟手柄](../docs/virtual-remote-control.md)、[硬件接线](../docs/hardware-and-pins.md)
- [图传冻结说明](../docs/seekfree-assistant-debugging.md)、[已知限制](../docs/known-issues.md)
- [最终固件](../outputs/firmware/)

在工作区根目录执行：

```powershell
.\car\scripts\build.ps1
python -B -m unittest discover -s car/tests -p 'test_*.py'
```

旧test.ps1仅验证早期19项图像/PID算法。完整测试和编译通过不等于实车完赛；首次按指南架空核对方向，再C13 OFF低速试车。烧录后断电移除DAP排线，用已确认的电池供电。

src为应用、include为接口、config为调参/接线、library为保留来源许可的驱动。自动驾驶采用新的control_pid/track_vision模块；M1左/M2右已确认，编码器+1符号仍待实测。红砖专用避障未实现，透视宽度、环岛/终点参数仍须实车验证。

camera_debug_config.h含本机热点密码，不贴入日志或公开报告。屏幕/Wi-Fi保持14:33冻结同步驱动；长阻塞会触发控制保护停车，不在本轮改写网络底层。
