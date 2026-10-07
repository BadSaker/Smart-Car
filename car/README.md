# Autocar RT1064 工程

打开 [autocar.uvprojx](autocar.uvprojx)，目标`autocar`。当前默认虚拟手柄遥控：C13联网、C12本地允许、助手Down确认，再用W/S行驶和A/D转向；见 [遥控指南](../docs/virtual-remote-control.md)。原按键测试需REMOTE_CONTROL_ENABLED=0重编译。

本工程用于C车模、逐飞MIMXRT1064DVL6B与RT1064 V3.0主板。当前上电默认关闭Wi-Fi，MT9V034图像先显示到普通IPS200 SPI屏；按C13/S4后开启Wi-Fi SPI并向逐飞助手1.2.7传图，再按关闭；电机上电关闭，C12按配置选择遥控允许或独立测试；舵机默认无PWM，中值1480µs，S3撤销信号；遥控与S2摆动测试互斥。

网络参数位于 [camera_debug_config.h](config/camera_debug_config.h)，已经配置本机2.4GHz热点及电脑目标192.168.137.1:8080。该文件含本机热点密码，不要贴入日志或公开报告。心跳诊断/DAP图传模式已删除；DAP用于SWD下载。电源对照确认仅电池带全部外设时各灯正常，当前按此方式运行。

在工作区根目录执行：

```powershell
.\car\scripts\build.ps1
.\car\scripts\test.ps1
```

- [构建与调试说明](../docs/environment-and-build.md)
- [C12电机与编码器测试](../docs/motor-encoder-debugging.md)
- [硬件与引脚](../docs/hardware-and-pins.md)
- [逐飞助手图传使用说明](../docs/seekfree-assistant-debugging.md)
- [已知问题与验证边界](../docs/known-issues.md)
- [全部例程的使用说明](../docs/examples/README.md)
- [最终固件](../outputs/firmware/)

`src/` 为应用实现，`include/` 为接口，`config/` 为硬件配置，`library/` 为保留来源和许可的SDK与逐飞驱动。图像/PID来源于本地摄像头组参考工程并作了回归修复；中断框架和底层库来源于V3.11.2。尚未实现完整赛道元素处理和自动停车。

最新供电对照：用户确认仅电池供电、摄像头/屏幕/Wi-Fi全部接回后仍全亮。烧录后彻底断电并移除DAP排线，再以电池运行；详细依据见硬件文档。

屏幕与Wi-Fi已按用户要求回退并冻结在14:32:41（用户称14:33）版，使用同步图传、有限后缀重试和自动重连。仅增加本次明确授权的C13软件开关，默认关闭；其他变更仍须新的明确授权，具体边界见 [冻结约束](../docs/seekfree-assistant-debugging.md#屏幕与wi-fi冻结约束)。异步模块及后续发送预算已移除；首次联网和重连时主循环可能等待，屏幕及舵机按键响应会延后。

S-U400中值已按用户确认设为1480µs，摆幅±60µs，当前屏幕显示遥控状态/脉宽，独立测试模式恢复S状态提示；临时校准和C12调节已撤回。详情见 [舵机指南](../docs/servo-debugging.md)：6.0V、P16/C30、1480±60µs测试窗口；S3撤销信号不保证S-U400卸力。

电机采用P8官方通道1=C8/C9、通道2=C6/C7，实际左右对应需单轮验证；编码器按STEP/DIR上升沿×1自由运行采样。用户确认轮1圈=编码器4.25圈，因此4352计数/轮圈，轮径64mm。在独立测试模式下，C12顺序测试M1/停/M2/停/双路后自动结束；当前遥控模式C12用于本地允许，S3停止，图像及Wi-Fi底部状态保留。
