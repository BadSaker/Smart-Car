# 电机与舵机

本页以 V3.11.2 的真实 `main.c`、`isr.c` 及本分类 `libraries` 为准。每项均提供原工程入口；所有测试步骤是待上板执行的操作与预期，本次没有编译或运行这84个原例。主板版本未知时，以所用板的原理图、丝印和实际连通性核对引脚。

先阅读 [统一准备与迁入流程](README.md#prepare)。源码中的 `clock_init(SYSTEM_CLOCK_600M)` 保留；调用 `debug_init` 的例程默认 UART1、115200、MCU TX B12/RX B13。原例 `main.c` 不能与车辆工程的 `main` 同时加入，`isr.c` 也必须逐IRQ合并。

<a id="e3_01"></a>

## E3_01 — HIP4082 单电机双 PWM

原目录：`E3_01_hip4082_single_motor_contro_demo`。用途：验证双PWM桥的正反转输入。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_01_hip4082_single_motor_contro_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_01_hip4082_single_motor_contro_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_01_hip4082_single_motor_contro_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_01_hip4082_single_motor_contro_demo/iar/rt1064.eww)。

**接线与配置：** MOTOR_PWM1=PWM2_MODULE3_CHA_D2，PWM2=CHB_D3。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define MAX_DUTY (50 )
#define PWM_CH1 (PWM2_MODULE3_CHA_D2)
#define PWM_CH2 (PWM2_MODULE3_CHB_D3)
```

接线/API依据：[zf_driver_pwm.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/libraries/zf_driver/zf_driver_pwm.h)。

**初始化、主循环与中断：** D2/D3两PWM，17kHz；每50ms占空比百分数逐步在-50~50扫动，正负决定哪路PWM有效、另一路为0。

关键API：`pwm_init`、`pwm_set_duty`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 车轮架空、低功率检查后，电机应渐进正反转；示波器确认双输入不会同时有PWM。

**限制：** 为HIP4082接口，不符合DIR/PWM式DRV8701直接接法。

**迁入项目：** 若实物双PWM桥，封装符号输出与换向死区；现车DRV8701应选E3_03/04。

<a id="e3_02"></a>

## E3_02 — HIP4082 多路双 PWM

原目录：`E3_02_hip4082_double_motor_contro_demo`。用途：演示四组双PWM驱动共同扫动。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_02_hip4082_double_motor_contro_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_02_hip4082_double_motor_contro_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_02_hip4082_double_motor_contro_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_02_hip4082_double_motor_contro_demo/iar/rt1064.eww)。

**接线与配置：** 四组具体PWM和引脚见本例main宏表；与DRV8701 DIR/PWM不可互换。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define MAX_DUTY (50)
#define MOTOR1_PWM1 (PWM2_MODULE1_CHB_C9)
#define MOTOR1_PWM2 (PWM2_MODULE1_CHA_C8)
#define MOTOR2_PWM1 (PWM2_MODULE0_CHB_C7)
#define MOTOR2_PWM2 (PWM2_MODULE0_CHA_C6)
#define MOTOR3_PWM1 (PWM2_MODULE3_CHB_D3)
#define MOTOR3_PWM2 (PWM2_MODULE3_CHA_D2)
#define MOTOR4_PWM1 (PWM2_MODULE2_CHB_C11)
#define MOTOR4_PWM2 (PWM2_MODULE2_CHA_C10)
```

接线/API依据：[zf_driver_pwm.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/libraries/zf_driver/zf_driver_pwm.h)。

**初始化、主循环与中断：** 四电机各两PWM均17kHz，主循环同一duty在±50%扫动，同桥另一PWM置0。

关键API：`pwm_init`、`pwm_set_duty`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 架空逐路核对，再观察四路同步渐进换向。

**限制：** 目录double而实际定义MOTOR1~4；不是四轮独立闭环。

**迁入项目：** 按实际使用通道封装输出，禁止直接迁入扫占空比循环。

<a id="e3_03"></a>

## E3_03 — DRV8701E 单路 DIR/PWM

原目录：`E3_03_drv8701e_single_motor_contro_demo`。用途：验证DIR/PWM式直流电机。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_03_drv8701e_single_motor_contro_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_03_drv8701e_single_motor_contro_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_03_drv8701e_single_motor_contro_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_03_drv8701e_single_motor_contro_demo/iar/rt1064.eww)。

**接线与配置：** DIR D2，PWM2_MODULE3_CHB_D3。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define MAX_DUTY (50 )
#define DIR (D2)
#define PWM (PWM2_MODULE3_CHB_D3)
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/libraries/zf_driver/zf_driver_gpio.h) · [zf_driver_pwm.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/libraries/zf_driver/zf_driver_pwm.h)。

**初始化、主循环与中断：** D2方向、D3 PWM，17kHz；每50ms在±50%扫动，符号映射GPIO高/低和绝对占空比。

关键API：`gpio_init`、`pwm_init`、`gpio_set_level`、`pwm_set_duty`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 车轮架空并先降低MAX_DUTY，检查正反转及符号映射；不要落地直接扫动。

**限制：** DRV8701实物接口需确认是否与E版输入一致，输出是开环扫描。

**迁入项目：** 迁入有符号 motor_set 接口与限幅；上电0输出，受启动状态控制。

<a id="e3_04"></a>

## E3_04 — DRV8701E 多路 DIR/PWM

原目录：`E3_04_drv8701e_double_motor_contro_demo`。用途：验证主板四组DIR/PWM接口，现车电机接线主要参考。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_04_drv8701e_double_motor_contro_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_04_drv8701e_double_motor_contro_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_04_drv8701e_double_motor_contro_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_04_drv8701e_double_motor_contro_demo/iar/rt1064.eww)。

**接线与配置：** 1 DIR C9/PWM C8；2 DIR C7/PWM C6；3 DIR D2/PWM D3；4 DIR C10/PWM C11。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define MAX_DUTY (50 )
#define MOTOR1_DIR (C9 )
#define MOTOR1_PWM (PWM2_MODULE1_CHA_C8)
#define MOTOR2_DIR (C7 )
#define MOTOR2_PWM (PWM2_MODULE0_CHA_C6)
#define MOTOR3_DIR (D2 )
#define MOTOR3_PWM (PWM2_MODULE3_CHB_D3)
#define MOTOR4_DIR (C10 )
#define MOTOR4_PWM (PWM2_MODULE2_CHB_C11)
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/libraries/zf_driver/zf_driver_gpio.h) · [zf_driver_pwm.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/libraries/zf_driver/zf_driver_pwm.h)。

**初始化、主循环与中断：** 四路17kHzPWM和方向GPIO初始化；共用duty每50ms在±50%扫动，方向同步。

关键API：`gpio_init`、`pwm_init`、`gpio_set_level`、`pwm_set_duty`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 架空逐路确认主板接口，再确认左右轮正方向；正式接线可能只用其中两组。

**限制：** 目录double但实际四组；主板版本未知，必须核对接口针脚与实际DRV8701模块。

**迁入项目：** 抽取所需左右通道，替换扫动为闭环输出，PWM初值0并添加启动/停止门控。

<a id="e3_05"></a>

## E3_05 — 360C SPIN27 无刷驱动与反馈

原目录：`E3_05_bldc_contro_demo_360c_spin27`。用途：演示指定无刷控制器的DIR/PWM和方向式反馈。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_05_bldc_contro_demo_360c_spin27/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_05_bldc_contro_demo_360c_spin27/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_05_bldc_contro_demo_360c_spin27/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_05_bldc_contro_demo_360c_spin27/iar/rt1064.eww)。

**接线与配置：** 1 PWM C9/DIR C8/反馈C0 C1；2 PWM C7/DIR C6/反馈C2 C24。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define MAX_DUTY (30 )
#define BLDC_MOTOR1_PWM (PWM2_MODULE1_CHB_C9)
#define BLDC_MOTOR1_DIR (C8 )
#define BLDC_MOTOR1_ENCODER_TIM (QTIMER1_ENCODER1)
#define BLDC_MOTOR1_ENCODER_A (QTIMER1_ENCODER1_CH1_C0)
#define BLDC_MOTOR1_ENCODER_B (QTIMER1_ENCODER1_CH2_C1)
#define BLDC_MOTOR2_PWM (PWM2_MODULE0_CHB_C7)
#define BLDC_MOTOR2_DIR (C6 )
#define BLDC_MOTOR2_ENCODER_TIM (QTIMER1_ENCODER2)
#define BLDC_MOTOR2_ENCODER_A (QTIMER1_ENCODER2_CH1_C2)
#define BLDC_MOTOR2_ENCODER_B (QTIMER1_ENCODER2_CH2_C24)
#define PIT_CH (PIT_CH0 )
```

接线/API依据：[zf_driver_pwm.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/libraries/zf_driver/zf_driver_pwm.h) · [zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/libraries/zf_driver/zf_driver_gpio.h)。

**初始化、主循环与中断：** 两路PWM1kHz；encoder_dir_init；PIT0每100ms在±30%扫动并读清编码器，主循环每300ms打印。

关键API：`pwm_init`、`gpio_init`、`encoder_dir_init`、`pit_ms_init`、`pit_handler`、`pwm_set_duty`、`gpio_set_level`、`encoder_get_count`、`encoder_clear_count`。

中断入口：`PIT_IRQHandler` 清相应通道标志并调用本例处理函数；通道号改变时同步修改ISR。

**测试与预期：** 仅匹配360C SPIN27控制器，架空检查输出方向和100ms反馈。

**限制：** 不是裸三相无刷换相例程；不能用于DRV8701直流桥；编码器调用是dir模式。

**迁入项目：** 只作为对应控制器外部接口参考，保留具体反馈协议与频率。

<a id="e3_06"></a>

## E3_06 — 舵机角度转 PWM

原目录：`E3_06_servo_control_demo`。用途：演示三路50Hz舵机在75°~105°往返。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_06_servo_control_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_06_servo_control_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_06_servo_control_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_06_servo_control_demo/iar/rt1064.eww)。

**接线与配置：** C30=PWM4_MODULE2_CHA；D0/D1=PWM1_MODULE3 A/B；50Hz，允许宏50~300Hz。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define SERVO_MOTOR1_PWM (PWM4_MODULE2_CHA_C30)
#define SERVO_MOTOR2_PWM (PWM1_MODULE3_CHA_D0)
#define SERVO_MOTOR3_PWM (PWM1_MODULE3_CHB_D1)
#define SERVO_MOTOR_FREQ (50 )
#define SERVO_MOTOR_L_MAX (75 )
#define SERVO_MOTOR_R_MAX (105)
#define SERVO_MOTOR_DUTY(x) ((float)PWM_DUTY_MAX/(1000.0/(float)SERVO_MOTOR_FREQ)*(0.5+(float)(x)/90.0))
```

接线/API依据：[zf_driver_pwm.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/libraries/zf_driver/zf_driver_pwm.h)。

**初始化、主循环与中断：** SERVO_MOTOR_DUTY 将角度换脉宽，三路同角度，每50ms加减1°；默认90°起始。

关键API：`pwm_init`、`pwm_set_duty`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 先拆转向连杆或抬车，确认中位与两侧限位，示波器90°约1.5ms/20ms。

**限制：** 实际舵机中位/机械限位需单独标定；同PWM模块频率冲突；75/105不可直接作为现车安全极限。

**迁入项目：** 现车优先C30单舵机，封装中位、方向、限幅，启动前保持已标定中位。

