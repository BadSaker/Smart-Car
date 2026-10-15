# 小车仿真实验室

本地Tk工具提供合成188×120摄像头、真实C算法闭环、2D/3D赛道与C1车模、参数实验、原始帧回放、逐飞截图对照和停车证据导出。算法唯一来源为主工程`../car`；仿真桥接与模型留在本目录。

## 启动

双击`启动仿真.cmd`。启动器寻找64位Python、Tk、NumPy和Pillow，不自动安装依赖。显式解释器使用`SIMULATION_PYTHON`，编译器使用`SIMULATION_CC`。依赖见requirements.txt；本轮验证Python3.14.6、NumPy2.4.6、Pillow12.3.0。

Windows x64 GCC可通过`toolchain/downloads/fetch_gcc.py`下载经SHA-256校验的独立发行包，或指定现有安装。工具链二进制不提交Git，发行标识和许可证保留；已有匹配DLL可以在无编译器时加载。首次运行新C参数需要编译器。

当前版本采用2026-10-14运动稳定性软件交付；当前分档与复现方法见[控制说明](docs/control-tuning.md)，软件验收及实车限制见[交付报告](../outputs/reports/motion-stability-2026-10-14.md)。选择参数后应用并重置，再启动；仿真参数不会回写固件。

倍速只改变现实时间中的推进率，控制仍每10ms、摄像头按原仿真时间采样。工具栏显示实际倍率，高倍速减少绘图并保留待计算时间；暂停/重置清除积压。默认双线性采样使用与NumPy参考逐像素一致的C渲染，采样数、相机与动力模型保持。验收见[本轮报告](../outputs/reports/element-speed-2026-10-13.md)，真实事件循环检查入口tools/check_speed_ui.py。

镜头实测高度297mm、后轴前80mm，道路内宽500mm。当前31.46896度俯角、89.66951度有效水平视场为条件拟合；120度标称镜头定义未知。默认无扰动、2×2像素积分、双线性采样。前轮端点已实测；物理相机、PWM—速度、舵机中间段及轮胎动力学尚未完成实测标定。

## 维护与验证

| 主题 | 入口 |
| --- | --- |
| 架构、坐标、车模与动力边界 | [architecture](docs/architecture.md) |
| 参数、预设、扰动和界面 | [parameters-and-ui](docs/parameters-and-ui.md) |
| 相机条件标定及数据冲突 | [camera-alignment](docs/camera-alignment.md) |
| 停车、回放和实测对照 | [diagnostics](docs/diagnostics.md) |
| 当前控制档位与双向验收 | [control-tuning](docs/control-tuning.md) |
| 历史文档与聊天来源 | [archive](docs/archive/README.md) |
| 多设备热点和工具配置 | [local-environment](../docs/local-environment.md) |

从Autodrive根目录，用当前机器已确认的Python运行：

```powershell
python -B -m unittest discover -s simulation/tests -p 'test_*.py' -v
python -B simulation/tools/check_current_ui.py --output tmp/任务名/ui
python -B simulation/tools/validate_control.py --config simulation/configs/中速验证_300.json --output tmp/任务名/control --directions AB BA
python -B simulation/tools/snapshot.py --output tmp/任务名/source
```

可用`AUTOCAR_TEST_ROOT`指定测试临时根目录，避免沿用其他执行身份创建的临时材料。GUI检查会打开短时窗口；测试可能生成DLL缓存。正式验收报告进入`outputs/reports`，过程输出只进入任务目录。历史检查见`tools/history`，不属于当前验收入口。

启动日志、解释器发现缓存和编译器发现诊断统一进入本机`runtime/`；`SIMULATION_RUNTIME_ROOT`可指定本任务运行目录。启动器兼容只读旧位置的解释器缓存，通过探测后写新位置。原`reports/`的147个文件及`backups/`的15个文件已保留原字节迁入outputs，索引见[历史报告](reports/README.md)、[历史备份](backups/README.md)。日常导出仍使用`runs/`，不作为正式验收报告。协作与迁移验证见[规范整理报告](../outputs/reports/project-normalization-2026-10-14.md)。

源码签名包含三个算法C文件、对应头文件、五个配置头（含encoder_config.h、vehicle_geometry.h和servo_config.h），共11份共享文件、桥接、C参数、编译选项及可用编译器的文件SHA-256；新构建保存实际编译器身份和来源清单。验收脚本复制明确输入到独立工作目录，防止运行过程中源文件变化影响实验。

2026-10-13现场优化：速度输入已移除500mm/s上限，接受可由C float表示的正有限值；PWM可配置1–1000‰，单轮超速保护独立配置。编码器按用户核实的68/30齿比读取主工程配置。当时默认800的旧模型结果仅作历史记录，当前版本以2026-10-14运动修复报告为准；历史实施记录见[实施状态](../outputs/reports/field-robustness-plan-2026-10-13.md)。

## 验收边界

地图真值用于合成图像、理想参考控制和评分，不给native控制器输入地图身份。参考路线约30.021m，含近似闭合段；终点是仿真位置标记。中央270度环道仍可能输出CURVE。理想参考控制成功、仿真整圈、Keil编译、烧录及实车完赛分别记录。

回放仅接受188×120原始帧，没有同步里程；逐飞完整截图导入用于实测对照，彩线和文字遮挡区无法恢复。导入不自动改变车姿。停车证据前5秒／后2秒的采集完整性必须检查。

原八张实拍图持久保存在`assets/measurements`；原始文件保持字节一致。历史相机高度、角度和默认速度见归档，不作为当前配置来源。
