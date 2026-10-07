# GNSS 与测距

本页以 V3.11.2 的真实 `main.c`、`isr.c` 及本分类 `libraries` 为准。每项均提供原工程入口；所有测试步骤是待上板执行的操作与预期，本次没有编译或运行这84个原例。主板版本未知时，以所用板的原理图、丝印和实际连通性核对引脚。

先阅读 [统一准备与迁入流程](README.md#prepare)。源码中的 `clock_init(SYSTEM_CLOCK_600M)` 保留；调用 `debug_init` 的例程默认 UART1、115200、MCU TX B12/RX B13。原例 `main.c` 不能与车辆工程的 `main` 同时加入，`isr.c` 也必须逐IRQ合并。

<a id="e7_01"></a>

## E7_01 — 分体超声波串口测距

原目录：`E7_01_split_ultrasonic_module_demo`。用途：解析A5起始的三字节飞行时间包。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_01_split_ultrasonic_module_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_01_split_ultrasonic_module_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_01_split_ultrasonic_module_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_01_split_ultrasonic_module_demo/iar/rt1064.eww)。

**接线与配置：** 模块RX→MCU TX C16，模块TX→MCU RX C17；B9 EN（与LED复用）。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define SPLIT_ULTRASONIC_UART (UART_4)
#define SPLIT_ULTRASONIC_BAUD (115200)
#define SPLIT_ULTRASONIC_TX (UART4_RX_C17)
#define SPLIT_ULTRASONIC_RX (UART4_TX_C16)
#define SPLIT_ULTRASONIC_EN (B9 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_driver/zf_driver_gpio.h) · [zf_driver_uart.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_driver/zf_driver_uart.h)。

**初始化、主循环与中断：** UART4 115200，B9 EN低初始化再拉高；LPUART4调uart_handler逐字节同步，A5+高/低字节得到ranging_counter；主循环每秒打印us。

关键API：`uart_handler`、`uart_query_byte`、`gpio_init`、`uart_init`、`uart_rx_interrupt`、`gpio_set_level`。

中断入口：`LPUART4_IRQHandler` 分发测距 `uart_handler` 或GNSS `gnss_uart_callback`，二者不能同时抢读UART4。

**测试与预期：** 移动反射平面，确认Ranging counter us随距离变化；先查模块输出协议。

**限制：** 输出是微秒计数而非厘米；没有校验和、温补或超时失效。宏TX/RX以模块命名，uart_init参数反向使用。

**迁入项目：** 按时差协议换距离并记录有效时间，错误帧/失联置无效；不可用于替代指定赛项限制。

<a id="e7_02"></a>

## E7_02 — GNSS 串口信息

原目录：`E7_02_gps_tau1201_pc_mono_demo`。用途：解析GN42A定位报文并显示时间、状态、经纬度、速度、方向、卫星数、高度。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_02_gps_tau1201_pc_mono_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_02_gps_tau1201_pc_mono_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_02_gps_tau1201_pc_mono_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_02_gps_tau1201_pc_mono_demo/iar/rt1064.eww)。

**接线与配置：** UART4 MCU TX C16/RX C17；B9/B10仅初始化为输出高。

接线/API依据：[zf_device_gnss.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_device/zf_device_gnss.h) · [zf_device_gnss.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_device/zf_device_gnss.c) · [zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_driver/zf_driver_gpio.h)。

**初始化、主循环与中断：** gnss_init(GN42A)；UART4 ISR调用gnss_uart_callback；主循环每秒在gnss_flag后data_parse成功才打印。

关键API：`gnss_init`、`gpio_init`、`gnss_data_parse`。

中断入口：`LPUART4_IRQHandler` 分发测距 `uart_handler` 或GNSS `gnss_uart_callback`，二者不能同时抢读UART4。

**测试与预期：** 在允许的室外调试条件等待定位，看gnss.state与卫星数变化，串口应打印各字段。

**限制：** 目录保留tau1201，实际API选择GN42A；当前gnss.c注明GN42A与TAU1201为同设备，两种初始化均为115200；室内无法保证定位，GPS不用于室内惯导组正式赛。

**迁入项目：** 仅学习报文/有效性解析；现车不引入GNSS依赖，室外独立项目需加入超时和state判断。

<a id="e7_03"></a>

## E7_03 — GNSS TFT180 显示

原目录：`E7_03_gps_tau1201_tft180_display_demo`。用途：解析GN42A定位报文并显示时间、状态、经纬度、速度、方向、卫星数、高度。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_03_gps_tau1201_tft180_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_03_gps_tau1201_tft180_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_03_gps_tau1201_tft180_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_03_gps_tau1201_tft180_display_demo/iar/rt1064.eww)。

**接线与配置：** UART4 MCU TX C16/RX C17；TFT180按对应SPI屏接线，IPS200本例SPI。

接线/API依据：[zf_device_gnss.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_device/zf_device_gnss.h) · [zf_device_gnss.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_device/zf_device_gnss.c) · [zf_device_tft180.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_device/zf_device_tft180.h) · [zf_device_tft180.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_device/zf_device_tft180.c)。

**初始化、主循环与中断：** gnss_init(GN42A)后初始化tft180；UART4 ISR调用gnss_uart_callback；主循环每秒在gnss_flag后data_parse成功才刷新屏幕。

关键API：`gnss_init`、`tft180_init`、`gnss_data_parse`、`tft180_show_uint`、`tft180_show_float`。

中断入口：`LPUART4_IRQHandler` 分发测距 `uart_handler` 或GNSS `gnss_uart_callback`，二者不能同时抢读UART4。

**测试与预期：** 在允许的室外调试条件等待定位，看gnss.state与卫星数变化，屏幕日期和数据字段应刷新。

**限制：** 目录保留tau1201，实际API选择GN42A；当前gnss.c注明GN42A与TAU1201为同设备，两种初始化均为115200；室内无法保证定位，GPS不用于室内惯导组正式赛。现车已配普通IPS200 SPI，仍需按本例使用的屏幕型号核对API。

**迁入项目：** 仅学习报文/有效性解析；现车不引入GNSS依赖，室外独立项目需加入超时和state判断。

<a id="e7_04"></a>

## E7_04 — GNSS IPS114 显示

原目录：`E7_04_gps_tau1201_ips114_display_demo`。用途：解析GN42A定位报文并显示时间、状态、经纬度、速度、方向、卫星数、高度。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_04_gps_tau1201_ips114_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_04_gps_tau1201_ips114_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_04_gps_tau1201_ips114_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_04_gps_tau1201_ips114_display_demo/iar/rt1064.eww)。

**接线与配置：** UART4 MCU TX C16/RX C17；IPS114按对应SPI屏接线，IPS200本例SPI。

接线/API依据：[zf_device_gnss.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_device/zf_device_gnss.h) · [zf_device_gnss.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_device/zf_device_gnss.c) · [zf_device_ips114.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_device/zf_device_ips114.h) · [zf_device_ips114.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_device/zf_device_ips114.c)。

**初始化、主循环与中断：** gnss_init(GN42A)后初始化ips114；UART4 ISR调用gnss_uart_callback；主循环每秒在gnss_flag后data_parse成功才刷新屏幕。

关键API：`gnss_init`、`ips114_init`、`gnss_data_parse`、`ips114_show_uint`、`ips114_show_float`。

中断入口：`LPUART4_IRQHandler` 分发测距 `uart_handler` 或GNSS `gnss_uart_callback`，二者不能同时抢读UART4。

**测试与预期：** 在允许的室外调试条件等待定位，看gnss.state与卫星数变化，屏幕日期和数据字段应刷新。

**限制：** 目录保留tau1201，实际API选择GN42A；当前gnss.c注明GN42A与TAU1201为同设备，两种初始化均为115200；室内无法保证定位，GPS不用于室内惯导组正式赛。现车已配普通IPS200 SPI，仍需按本例使用的屏幕型号核对API。

**迁入项目：** 仅学习报文/有效性解析；现车不引入GNSS依赖，室外独立项目需加入超时和state判断。

<a id="e7_05"></a>

## E7_05 — GNSS IPS200 显示

原目录：`E7_05_gps_tau1201_ips200_display_demo`。用途：解析GN42A定位报文并显示时间、状态、经纬度、速度、方向、卫星数、高度。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_05_gps_tau1201_ips200_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_05_gps_tau1201_ips200_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_05_gps_tau1201_ips200_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_05_gps_tau1201_ips200_display_demo/iar/rt1064.eww)。

**接线与配置：** UART4 MCU TX C16/RX C17；IPS200按对应SPI屏接线，IPS200本例SPI。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define IPS200_TYPE (IPS200_TYPE_SPI)
```

接线/API依据：[zf_device_ips200.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_device/zf_device_ips200.h) · [zf_device_ips200.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_device/zf_device_ips200.c) · [zf_device_gnss.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_device/zf_device_gnss.h) · [zf_device_gnss.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/libraries/zf_device/zf_device_gnss.c)。

**初始化、主循环与中断：** 先初始化ips200，gnss_init(GN42A)；UART4 ISR调用gnss_uart_callback；主循环每秒在gnss_flag后data_parse成功才刷新屏幕。

关键API：`ips200_init`、`ips200_show_string`、`gnss_init`、`ips200_clear`、`gnss_data_parse`、`ips200_show_uint`、`ips200_show_float`。

中断入口：`LPUART4_IRQHandler` 分发测距 `uart_handler` 或GNSS `gnss_uart_callback`，二者不能同时抢读UART4。

**测试与预期：** 在允许的室外调试条件等待定位，看gnss.state与卫星数变化，屏幕日期和数据字段应刷新。

**限制：** 目录保留tau1201，实际API选择GN42A；当前gnss.c注明GN42A与TAU1201为同设备，两种初始化均为115200；室内无法保证定位，GPS不用于室内惯导组正式赛。现车已配普通IPS200 SPI，仍需按本例使用的屏幕型号核对API。

**迁入项目：** 仅学习报文/有效性解析；现车不引入GNSS依赖，室外独立项目需加入超时和state判断。

