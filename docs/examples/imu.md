# IMU

本页以 V3.11.2 的真实 `main.c`、`isr.c` 及本分类 `libraries` 为准。每项均提供原工程入口；所有测试步骤是待上板执行的操作与预期，本次没有编译或运行这84个原例。主板版本未知时，以所用板的原理图、丝印和实际连通性核对引脚。

先阅读 [统一准备与迁入流程](README.md#prepare)。源码中的 `clock_init(SYSTEM_CLOCK_600M)` 保留；调用 `debug_init` 的例程默认 UART1、115200、MCU TX B12/RX B13。原例 `main.c` 不能与车辆工程的 `main` 同时加入，`isr.c` 也必须逐IRQ合并。

默认SPI4 10MHz，SCK C23/MOSI C22/MISO C21/CS C20；各模块头文件默认关闭软件IIC。IMU660RC另需INT2 D1，它采用SPI读取和EXTI通知，不是串口模块。正式规则对模块是否含MCU的要求须按实物核查，不能由驱动文件名推断。

<a id="e4_01"></a>

## E4_01 — ICM20602 原始数据

原目录：`E4_01_icm20602_demo`。用途：验证六轴加速度/角速度传感器连通性。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_01_icm20602_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_01_icm20602_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_01_icm20602_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_01_icm20602_demo/iar/rt1064.eww)。

**接线与配置：** 默认SPI4 10MHz：SCK C23、MOSI C22、MISO C21、CS C20；软件IIC分支默认关闭。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define LED1 (B9 )
#define PIT_CH (PIT_CH0 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_icm20602.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_device/zf_device_icm20602.h) · [zf_device_icm20602.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_device/zf_device_icm20602.c)。

**初始化、主循环与中断：** icm20602_init 失败反复打印并翻转B9；成功后PIT0每5ms调用get_acc/get_gyro，主循环每秒打印原始整数。

关键API：`gpio_init`、`icm20602_init`、`gpio_toggle_level`、`pit_ms_init`、`pit_handler`、`icm20602_get_acc`、`icm20602_get_gyro`。

中断入口：`PIT_IRQHandler` 清相应通道标志并调用本例处理函数；通道号改变时同步修改ISR。

**测试与预期：** 静置观察重力方向和陀螺零偏，缓慢倾斜/转动确认轴及符号。

**限制：** 原始量不是姿态角，必须按量程换算与零偏标定；正式规则要求IMU模块不含MCU，需核查实物。

**迁入项目：** 固定周期读取并时间戳，量程换算、静态校零后再做姿态解算；不用每秒日志作控制输入。

<a id="e4_02"></a>

## E4_02 — IMU660RA 原始数据

原目录：`E4_02_imu660ra_demo`。用途：验证六轴加速度/角速度传感器连通性。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_02_imu660ra_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_02_imu660ra_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_02_imu660ra_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_02_imu660ra_demo/iar/rt1064.eww)。

**接线与配置：** 默认SPI4 10MHz：SCK C23、MOSI C22、MISO C21、CS C20；软件IIC分支默认关闭。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define LED1 (B9 )
#define PIT_CH (PIT_CH0 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_imu660ra.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_device/zf_device_imu660ra.h) · [zf_device_imu660ra.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_device/zf_device_imu660ra.c)。

**初始化、主循环与中断：** imu660ra_init 失败反复打印并翻转B9；成功后PIT0每5ms调用get_acc/get_gyro，主循环每秒打印原始整数。

关键API：`gpio_init`、`imu660ra_init`、`gpio_toggle_level`、`pit_ms_init`、`pit_handler`、`imu660ra_get_acc`、`imu660ra_get_gyro`。

中断入口：`PIT_IRQHandler` 清相应通道标志并调用本例处理函数；通道号改变时同步修改ISR。

**测试与预期：** 静置观察重力方向和陀螺零偏，缓慢倾斜/转动确认轴及符号。

**限制：** 原始量不是姿态角，必须按量程换算与零偏标定；正式规则要求IMU模块不含MCU，需核查实物。

**迁入项目：** 固定周期读取并时间戳，量程换算、静态校零后再做姿态解算；不用每秒日志作控制输入。

<a id="e4_03"></a>

## E4_03 — IMU660RB 原始数据

原目录：`E4_03_imu660rb_demo`。用途：验证六轴加速度/角速度传感器连通性。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_03_imu660rb_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_03_imu660rb_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_03_imu660rb_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_03_imu660rb_demo/iar/rt1064.eww)。

**接线与配置：** 默认SPI4 10MHz：SCK C23、MOSI C22、MISO C21、CS C20；软件IIC分支默认关闭。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define LED1 (B9 )
#define PIT_CH (PIT_CH0 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_imu660rb.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_device/zf_device_imu660rb.h) · [zf_device_imu660rb.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_device/zf_device_imu660rb.c)。

**初始化、主循环与中断：** imu660rb_init 失败反复打印并翻转B9；成功后PIT0每5ms调用get_acc/get_gyro，主循环每秒打印原始整数。

关键API：`gpio_init`、`imu660rb_init`、`gpio_toggle_level`、`pit_ms_init`、`pit_handler`、`imu660rb_get_acc`、`imu660rb_get_gyro`。

中断入口：`PIT_IRQHandler` 清相应通道标志并调用本例处理函数；通道号改变时同步修改ISR。

**测试与预期：** 静置观察重力方向和陀螺零偏，缓慢倾斜/转动确认轴及符号。

**限制：** 原始量不是姿态角，必须按量程换算与零偏标定；正式规则要求IMU模块不含MCU，需核查实物。

**迁入项目：** 固定周期读取并时间戳，量程换算、静态校零后再做姿态解算；不用每秒日志作控制输入。

<a id="e4_04"></a>

## E4_04 — IMU660RC 四元数120Hz模式

原目录：`E4_04_imu660rc_demo`。用途：验证新版RC的数据及roll/pitch/yaw输出。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_04_imu660rc_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_04_imu660rc_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_04_imu660rc_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_04_imu660rc_demo/iar/rt1064.eww)。

**接线与配置：** SPI4 C23/C22/C21/CS C20，INT2 D1。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define LED1 (B9 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_imu660rc.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_device/zf_device_imu660rc.h) · [zf_device_imu660rc.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_device/zf_device_imu660rc.c)。

**初始化、主循环与中断：** imu660rc_init(IMU660RC_QUARTERNION_120HZ)，默认SPI4；初始化配置D1上升沿EXTI；GPIO3_Combined_0_15_IRQHandler 调 imu660rc_callback→get_quarternion；主循环每秒打印acc/gyro和欧拉角。

关键API：`gpio_init`、`imu660rc_init`、`gpio_toggle_level`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 核对D1 INT2接线，缓慢旋转，观察roll/pitch/yaw变化；无中断时不要仅凭init成功判断有效。

**限制：** 本例不使用5ms PIT；源码有四元数输出模式不能证明实物是否含MCU，参赛合规需核对模块构成。

**迁入项目：** 按实际模块选择原始数据或输出模式，合并GPIO3共享ISR，加入数据更新时间与校准。

<a id="e4_05"></a>

## E4_05 — IMU963RA 原始数据

原目录：`E4_05_imu963ra_demo`。用途：验证九轴加速度/角速度/磁场传感器连通性。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_05_imu963ra_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_05_imu963ra_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_05_imu963ra_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_05_imu963ra_demo/iar/rt1064.eww)。

**接线与配置：** 默认SPI4 10MHz：SCK C23、MOSI C22、MISO C21、CS C20；软件IIC分支默认关闭。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define LED1 (B9 )
#define PIT_CH (PIT_CH0 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_imu963ra.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_device/zf_device_imu963ra.h) · [zf_device_imu963ra.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/libraries/zf_device/zf_device_imu963ra.c)。

**初始化、主循环与中断：** imu963ra_init 失败反复打印并翻转B9；成功后PIT0每5ms调用get_acc/get_gyro/get_mag，主循环每秒打印原始整数。

关键API：`gpio_init`、`imu963ra_init`、`gpio_toggle_level`、`pit_ms_init`、`pit_handler`、`imu963ra_get_acc`、`imu963ra_get_gyro`、`imu963ra_get_mag`。

中断入口：`PIT_IRQHandler` 清相应通道标志并调用本例处理函数；通道号改变时同步修改ISR。

**测试与预期：** 静置观察重力方向和陀螺零偏，缓慢倾斜/转动确认轴及符号；磁场随朝向变化。

**限制：** 原始量不是姿态角，必须按量程换算与零偏标定；正式规则要求IMU模块不含MCU，需核查实物。

**迁入项目：** 固定周期读取并时间戳，量程换算、静态校零后再做姿态解算；不用每秒日志作控制输入。

