# 自动驾驶、视觉与闭环调参

## 批准范围与设计

2026-10-08用户批准：先调研本地规则/GitHub，再加入摄像头循迹、环岛状态机、双轮速度PID、完成一圈后的斑马线停车；C15改为自动驾驶，S3停车，保留虚拟手柄且互斥。M1左后轮/M2右后轮已确认，编码器前进为正仍是假设，配置与运行保护不能代替实测。舵机保持1480µs中位及已采用1420–1540µs范围。

相机帧由主循环分析；10ms PIT独立执行编码器速度闭环、启动/停止按键、视觉新鲜度保护和电机输出。舵机仍由主循环唯一写入，通过带代次的服务确认防止主循环停顿后沿用旧转向。屏幕与Wi-Fi同步发送核心保持冻结，只在main调整分析顺序、增加顶部自动驾驶状态入口。

视觉从图像下方与车道连通的白区追踪边界，以可配置透视宽度补单边。候选环岛不立即转向，只有单侧开口/拐点/对侧连续和多帧证据才推进入口、环内、出口状态；状态有距离和时间边界。缺乏可靠路径时停止，不用无限时间盲补线。

控制采用近远双前瞻像素误差、滤波微分及限幅转向；左右轮各自10ms速度PID，积分反饱和和速度/占空比斜坡。Pure Pursuit作为后续标定后的替换方向，目前缺地面投影和轴距标定，不把像素距离冒充米制曲率。

起点/终点按本地规则的斑马线处理：多帧近场确认第一次过线、离线滞回、最短时间/里程后确认第二次，再按可配置里程推进使全车越线并停车。默认推进距离只是参考，需要结合相机看到标记时到车尾的距离和实际制动距离标定；不能仅凭识别算法宣称满足1m内停车。

## 实施计划与验证

- [x] 视觉：真实灰度合成场景、直道旁圆环负例、弯道、十字、左右环岛状态转移、斑马线/横线区分、低对比/全黑/全白和超时。
- [x] PID：阶跃、积分反饱和、测量微分、滤波、非法值与复位。
- [x] 控制：C15消抖、S3/C12停车、互斥、失帧/失反馈、负计数、堵转、超速、时钟回绕、完成一圈后停车。
- [x] 集成：main/isr/工程引用、舵机输出、完整回归、Keil全量编译；不自动烧录或实车驱动。
- [x] 交付：记录参数与来源、更新导航、固件/备份/日志，清理任务临时目录。

本任务在已批准现有工程内执行；不创建Git仓库，不修改来源中的嵌套Git。修改前备份见outputs/backups。正式完成情况以文末验证记录与每日日志为准。

## 初步调研来源

- [CMU Pure Pursuit原始报告](https://publications.ri.cmu.edu/implementation-of-the-pure-pursuit-path-tracking-algorithm)：几何预瞄控制原理；此次不直接移植代码。
- [PythonRobotics](https://github.com/AtsushiSakai/PythonRobotics)：比较Stanley/LQR/MPC等实现思路；Python依赖与模型标定条件不直接适配本工程，不复制其代码。
- [OpenCV阈值官方说明](https://docs.opencv.org/4.x/d7/d4d/tutorial_py_thresholding.html)：Otsu与局部阈值的适用条件；只参考算法，不引入OpenCV库。
- 本地[竞赛规则](competition-rules.md)：当前v0.5；正式计时不得携带无线模块，调试图传不代表竞赛允许。

## 使用与调参

重新加载car/autocar.uvprojx并Rebuild后由用户烧录。断电移除DAP排线，再用电池上电。首轮架空驱动轮，核对方向与舵机，再低速试短直道、缓弯、十字和左右环岛。

- C15/S2：启动/停止自动驾驶，取代舵机摆动。先稳定释放再新按下；可信图像至少3帧，居中等待500ms后加速。
- C14/S3：停车并退出当前允许状态。C12在自动驾驶中同样取消；非自动模式维持遥控允许/解除。
- C13：Wi-Fi开关，上电OFF。遥控与自动互斥，遥控允许时不能用C15直接接管，先C12或S3解除，再新按C15；自动退出后遥控仍需C12+新Down。
- REMOTE_CONTROL_ENABLED=0仅恢复C12有限电机测试，C15仍是自动驾驶。旧舵机摆动函数不再由main调用。

| 调参入口 | 参数与初值 |
| --- | --- |
| car/config/autonomous_config.h | 直道180、弯道120、元素/终点100mm/s；PWM上限100‰=10% |
| 同上：速度PID | Kp0.18、Ki0.15、Kd0、前馈0.30；左右轮独立闭环，默认最大15%内外轮差速比例 |
| 同上：转向PID | Kp18、Ki0、Kd0.5；近/远权重0.65/0.35，像素右正、舵机左正 |
| 同上：符号 | M1左/M2右已确认；AUTO_LEFT/RIGHT_ENCODER_SIGN默认+1仍须向前转轮验证，若负改对应项为-1 |
| 同上：舵机 | 沿用1480µs中位与1420–1540µs范围；自动步长6µs/20ms，迟到不追赶 |
| car/config/vision_config.h | 中心93列，row30宽36px、row116宽122px；近86–108行、远45–70行，须按实际灰度图标定 |
| 同上：环岛 | 连续3帧，入口160mm、环内至少900mm、出口180mm；60s阶段保护与4m限距 |
| autonomous_config.h：终点 | 最短圈程5000mm、圈时8000ms；确认终点后继续500mm，均需按实车/赛道标定 |

Ki按秒积分、Kd按秒微分，不是已乘10ms的离散系数。先校准方向/透视，再调前馈与速度Kp，最后Ki；直道蛇形先减转向Kp/车速，再调整滤波。PWM低到不动时先看驱动使能、反馈及供电，不应关闭堵转保护。当前机械角度未证明足够通过环岛，不盲目扩大舵机脉宽。

## 模块与保护

main包装CSI完成回调，以真实完成时间标记图像；复制时发生换帧则丢弃。track_vision从灰度提供边线/中线/支持度及元素；autonomous_control通过短临界区把摘要交给10ms PIT，autonomous_drive负责状态机和双轮PID，control_pid为新PID实现。旧pid.c保留早期回归，不再作为自动驾驶控制参数入口。PIT按编码器采样→自动控制→motor_control输出的顺序执行；舵机仅主循环写入。

屏幕顶部AUTO显示状态、Q支持度、Z起点是否已确认、里程及左右速度/PWM。CAMERA表示帧龄超过150ms；LOST为连续3个无可靠路径的新帧（第1帧即撤PWM）；ENCODER为反馈30ms未更新；ENC SIGN为持续100ms反号；OVERSPEED为绝对速度超过900mm/s；STALL为足够PWM下轮速低于10mm/s持续1500ms；SERVO GAP为主循环100ms未确认舵机服务；ELEMENT为元素限距/超时；TIMEOUT/FIN TIME为总运行180s/终点推进10s；CONFIG为参数错误。消除原因后须重新按C15，恢复图像或网络不会自行启动。

普通支持度门限45；已确认且有可见证据、受限距/超时约束的十字和环岛阶段可用35。全黑远场不能因已处于十字而无条件补线。候选环岛仍沿当前路走，不因直道旁出现圆形就转向。

第一次近场斑马线连续3帧为出发；离线5帧且前进500mm后解锁返回标记。第二次同时达到最短圈时/圈程进入FINISHING，再推进参考500mm并锁存DONE。无效图像打断连续计数；仍在线中达到圈程门槛会继续重判。若从起点之后发车，首个标记将被当作出发，因此必须从规则要求的起点前发车。

500mm从相机近场确认位置算起，不能证明全车已经越线。需实测此时车尾到斑马线的距离、制动余量，验证全车过线且停在1m内。轮速采用64mm轮径/4352计数每轮圈，仍需滚动标定。

同步Wi-Fi/屏幕驱动保持冻结，长阻塞会触发控制保护停车。首轮赛道测试保持C13 OFF，静态调图可开Wi-Fi；正式计时按规则移除无线模块。PWM0可能制动而非高阻，舵机撤销信号不等于卸力；PIT保护依赖中断正常运行，不是原子硬件急停。停止发生在舵机调用中途时，本次service返回前撤销旧目标。

## 研究来源与验证边界

- [Otsu原论文](https://ieeexplore.ieee.org/document/4310076)：类间方差；本实现32级直方图分割后取类均值中点阈值。
- [SmartCarBaseCameraByXiayu](https://github.com/xiayu2020/SmartCarBaseCameraByXiayu)：GPL-3.0，对照八邻域、丢线、补线/环岛思路；本实现选受限扫描线与显式开口证据，没有复制代码。
- [TC264环岛实现](https://github.com/hao-yue-1/SmartCar/blob/main/Seekfree_TC264_Opensource_Library/CODE/ImageCircleIsland.c)：参考模块划分，未检测到明确许可，不迁移源码。
- [RT1064_Smartcar](https://github.com/FredBill1/RT1064_Smartcar)：GPL-2.0，含OpenART，与本车任务不匹配，未采用架构。
- [MathWorks PID](https://www.mathworks.com/help/simulink/slref/pidcontroller.html)、[抗饱和说明](https://www.mathworks.com/help/simulink/slref/anti-windup-control-using-a-pid-controller.html)：条件积分、滤波微分和前馈限幅。离散实现语义见control_pid.h。

完整回归：工程根目录运行python -B -m unittest discover -s car/tests -p 'test_*.py'。car/scripts/test.ps1仅覆盖旧19项算法，不能代替完整自动驾驶测试。car/scripts/build.ps1执行实际Keil全量构建并导出AXF/HEX/BIN。

当前没有实车录像/安装几何验收，也没有IMU航向反馈；红砖专用识别绕行未实现。合成灰度序列/主机联合测试不能证明真实赛道完赛，需实测直弯、环岛、十字、终点及实时耗时。本次不自动烧录或运行实车。

## 本轮交付验证（2026-10-08）

- 完整71项unittest通过，另旧算法19/19通过；真实灰度视觉177检查、控制器20339检查及联合256检查在完整回归内。
- 实际主工程Keil全量0错误0警告，Code105320、RO13172、RW300、ZI104684字节。REMOTE_CONTROL_ENABLED=0隔离全量构建也0错误0警告，仅默认遥控兼容版本发布到outputs/firmware。
- 364个工程文件引用存在；冻结camera_debug.c和519个库文件逐个SHA256与修改前一致。
- 实际下载缓存tmp/build-autocar/objects与outputs/firmware的AXF/HEX/BIN逐字节一致。AXF984424、HEX352508、BIN125312字节。
- [完整回归日志](../logs/autonomous-suite.log)、[主构建日志](../logs/autonomous-build.log)、[关闭遥控构建](../logs/autonomous-mode0-build.log)、[旧算法检查](../logs/autonomous-algorithms.log)。
- 修改前备份：[car_before-autonomous-driving_20261008_163804.zip](../outputs/backups/car_before-autonomous-driving_20261008_163804.zip)。本轮未自动烧录、上电或赛道运行。
