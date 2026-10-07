# 主板输入与检测

本页以 V3.11.2 的真实 `main.c`、`isr.c` 及本分类 `libraries` 为准。每项均提供原工程入口；所有测试步骤是待上板执行的操作与预期，本次没有编译或运行这84个原例。主板版本未知时，以所用板的原理图、丝印和实际连通性核对引脚。

先阅读 [统一准备与迁入流程](README.md#prepare)。源码中的 `clock_init(SYSTEM_CLOCK_600M)` 保留；调用 `debug_init` 的例程默认 UART1、115200、MCU TX B12/RX B13。原例 `main.c` 不能与车辆工程的 `main` 同时加入，`isr.c` 也必须逐IRQ合并。

<a id="e1_01"></a>

## E1_01 — 按键、拨码、LED 与蜂鸣器

原目录：`E1_01_button_switch_buzzer_demo`。用途：验证主板人机输入和蜂鸣器。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_01_button_switch_buzzer_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_01_button_switch_buzzer_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_01_button_switch_buzzer_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_01_button_switch_buzzer_demo/iar/rt1064.eww)。

**接线与配置：** SWITCH C27/C26、LED B9、BEEP B11；KEY_LIST C15/C14/C13/C12。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define SWITCH1 (C27)
#define SWITCH2 (C26)
#define LED1 (B9)
#define BEEP (B11)
```

接线/API依据：[zf_device_key.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/libraries/zf_device/zf_device_key.h) · [zf_device_key.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/libraries/zf_device/zf_device_key.c) · [zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/libraries/zf_driver/zf_driver_gpio.h)。

**初始化、主循环与中断：** 上电两次短鸣闪灯；key_init(5)，每5ms key_scanner；短/长按均响约200ms；任一拨码低时LED按计数闪烁。

关键API：`key_init`、`gpio_init`、`gpio_set_level`、`gpio_get_level`、`key_scanner`、`key_get_state`、`key_clear_state`、`key_clear_all_state`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 依次拨动C27/C26并短按、长按四键，LED与鸣声应按上述变化。

**限制：** 扫描周期必须和 key_init 参数一致；阻塞会破坏按键时序。

**迁入项目：** 把scanner放5ms任务，键状态转换成启动/停止事件，蜂鸣器由状态机限时控制。

<a id="e1_02"></a>

## E1_02 — 霍尔停止线检测

原目录：`E1_02_hall_stopline_detection_demo`。用途：用数字霍尔与GPT测量两次触发间隔。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_02_hall_stopline_detection_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_02_hall_stopline_detection_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_02_hall_stopline_detection_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_02_hall_stopline_detection_demo/iar/rt1064.eww)。

**接线与配置：** 霍尔信号D4；LED B9；GPT_TIM_1。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define HALL_PIN (D4)
#define LED1 (B9)
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/libraries/zf_driver/zf_driver_gpio.h) · [zf_driver_timer.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/libraries/zf_driver/zf_driver_timer.h)。

**初始化、主循环与中断：** D4下拉输入；低电平首次开始GPT1毫秒计时，高电平停止读取并清零，LED低有效点亮，串口打印非零间隔。

关键API：`gpio_init`、`timer_init`、`gpio_get_level`、`timer_stop`、`timer_get`、`timer_clear`、`gpio_set_level`、`timer_start`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 用匹配霍尔模块和磁体触发，LED随D4高电平亮，串口输出Hall trigger毫秒。

**限制：** 没有直接电机停止代码，也没有锁存与消抖；不能当正式停车状态机。

**迁入项目：** 将可靠触发变成停车事件，增加保持、去抖和驱动零输出。

<a id="e1_03"></a>

## E1_03 — OPM4A 四通道电感采样

原目录：`E1_03_opm4a_demo`。用途：读取四路电感前端ADC输出。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_03_opm4a_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_03_opm4a_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_03_opm4a_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_03_opm4a_demo/iar/rt1064.eww)。

**接线与配置：** L1~4到B14/B15/B21/B23。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define CHANNEL_NUMBER (4)
#define ADC_CHANNEL1 (ADC1_CH3_B14)
#define ADC_CHANNEL2 (ADC1_CH4_B15)
#define ADC_CHANNEL3 (ADC1_CH10_B21)
#define ADC_CHANNEL4 (ADC1_CH12_B23)
```

接线/API依据：[zf_driver_adc.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/libraries/zf_driver/zf_driver_adc.h)。

**初始化、主循环与中断：** 四路均12位，每秒打印L1~L4原始值。

关键API：`adc_init`、`adc_convert`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 接OPM4A并移动到对应导线磁场，各路应随位置变化；静止时读数相对稳定。

**限制：** 仅ADC显示，没有差比和循迹控制；输入供电与模拟量范围按实物前端核对。

**迁入项目：** 定周期采样、减零点/归一化再提取横向偏差。

