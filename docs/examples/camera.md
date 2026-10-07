# 摄像头采集与传输

本页以 V3.11.2 的真实 `main.c`、`isr.c` 及本分类 `libraries` 为准。每项均提供原工程入口；所有测试步骤是待上板执行的操作与预期，本次没有编译或运行这84个原例。主板版本未知时，以所用板的原理图、丝印和实际连通性核对引脚。

先阅读 [统一准备与迁入流程](README.md#prepare)。源码中的 `clock_init(SYSTEM_CLOCK_600M)` 保留；调用 `debug_init` 的例程默认 UART1、115200、MCU TX B12/RX B13。原例 `main.c` 不能与车辆工程的 `main` 同时加入，`isr.c` 也必须逐IRQ合并。

## 屏幕接线依据

按本分类 `libraries/zf_device` 的头文件和驱动实现接线；下表为默认配置，不能把多个屏同时接到同一接口后全部初始化。

| 模块 | 总线与关键引脚 | 注意 |
|---|---|---|
| OLED | SPI3 SCK B0、MOSI B1、RES B2、DC C19、CS B3 | 128×64；页坐标与像素坐标要区分 |
| TFT180 | 同上，背光 C18，40MHz | 128×160；横屏160×128 |
| IPS114 | 同上，RST B2，背光 C18，60MHz | 240×135，方向与坐标以当前驱动为准 |
| IPS200 SPI | SPI3 B0/B1、RST B2、DC C19、CS B3、背光 C18，60MHz | 是否SPI由例程的IPS200_TYPE决定 |
| IPS200 PARALLEL8 | RD B0、WR B1、RS B2、RST C19、CS B3、BL C18；D0~D7 B16/B17/B18/B19/D12/D13/D14/D15 | E8_07使用；与Wi-Fi SPI引脚冲突 |
| IPS200 Pro | SPI3 SCK B0、MOSI B1、CS B3、RST B2、INT C19，40MHz | Pro为独立协议和控件模块，不能用普通IPS200替换 |

当前实车采用普通IPS200 SPI屏和Wi-Fi SPI；本页例程保持原始接口说明，集成方式见 [调试指南](../seekfree-assistant-debugging.md)。

## 摄像头接线与数据约定

当前现车是逐飞 MT9V034 总钻风，按 MT9V03X 驱动验证。默认 CSI 并口相机使用 PCLK B20、VSYNC B22；采集数据映射由 `zf_driver_csi.c` 的 IOMUX 配置固定为 GPIO_AD_B1_08~15 的 CSI_DATA09~02，接线顺序须对照主板相机插座及相机定义，不要把CSI_DATA编号直接当模块D0~D7重新布线。配置口 UART5、9600：相机TX→MCU RX C29，相机RX←MCU TX C28；部分驱动支持IIC配置路径，接口由初始化实现识别/选择。

| 相机 | 当前默认帧 | 帧缓冲和完成标志 |
|---|---|---|
| OV7725 小钻风 | 160×120二值，每8像素1字节 | `ov7725_image_binary`、`ov7725_finish_flag`；IMAGE_SIZE=W×H/8 |
| MT9V03X 总钻风 | 188×120、8位灰度，默认50fps | `mt9v03x_image`、`mt9v03x_finish_flag`；IMAGE_SIZE=W×H |
| SCC8660 凌瞳 | 160×120、16位RGB565，默认50fps | `scc8660_image`、`scc8660_finish_flag`；IMAGE_SIZE=W×H×2；默认格式含字节交换 |

`CSI_IRQHandler` 必须调用 `CSI_DriverIRQHandler`；配置UART5 IRQ必须保留非空 `camera_uart_handler` 分发。FLEXIO是另一套引脚与API，不能把FLEXIO头文件的配置口/数据口混入这些CSI例程。图像算法应先复制完成帧到独立缓冲，避免DMA更新时读到半帧；完成标志的清除时机按所选例程/驱动处理。

图传边界模式：0仅图像、1按行X边界、2按列Y边界、3 XY点列、4仅边界不发图。演示边界均是公式生成的假数据，不表示识别了赛道。

<a id="e8_01"></a>

## E8_01 — OV7725 → debug UART1 逐飞助手

原目录：`E8_01_ov7725_seekfree_assistant_demo`。用途：在PC查看压缩二值160×120帧与示例边界。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_01_ov7725_seekfree_assistant_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_01_ov7725_seekfree_assistant_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_01_ov7725_seekfree_assistant_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_01_ov7725_seekfree_assistant_demo/iar/rt1064.eww)。

**接线与配置：** debug UART1接线见下方驱动头文件及通用表。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define INCLUDE_BOUNDARY_TYPE 3
#define BOUNDARY_NUM (OV7725_H * 3 / 2)
#define LED1 (B9 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_ov7725.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ov7725.h) · [zf_device_ov7725.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ov7725.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化debug UART1传输与assistant interface，反复初始化相机；INCLUDE_BOUNDARY_TYPE默认3发送XY人工边界，0为仅图像、1 X、2 Y、4仅边界；新帧清标志、复制帧并camera_send。

关键API：`seekfree_assistant_interface_init`、`gpio_init`、`ov7725_init`、`gpio_toggle_level`、`seekfree_assistant_camera_information_config`、`elif`、`seekfree_assistant_camera_boundary_config`、`seekfree_assistant_camera_send`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 助手选择debug UART1连接与OV7725格式，遮挡/移动相机检查实时图；默认有三条人工边界，改0后无边界。

**限制：** 边界为演示公式，不是寻线；UART带宽限制帧率。

**迁入项目：** 保留对应相机帧格式与传输初始化，合并CSI/UART中断；处理帧使用独立快照。

<a id="e8_02"></a>

## E8_02 — OV7725 → 无线UART 逐飞助手

原目录：`E8_02_ov7725_wireless_uart_seekfree_assistant_demo`。用途：在PC查看压缩二值160×120帧与示例边界。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_02_ov7725_wireless_uart_seekfree_assistant_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_02_ov7725_wireless_uart_seekfree_assistant_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_02_ov7725_wireless_uart_seekfree_assistant_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_02_ov7725_wireless_uart_seekfree_assistant_demo/iar/rt1064.eww)。

**接线与配置：** 无线UART接线见下方驱动头文件及通用表。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define INCLUDE_BOUNDARY_TYPE 3
#define BOUNDARY_NUM (OV7725_H * 3 / 2)
#define LED1 (B9 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_wireless_uart.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_wireless_uart.h) · [zf_device_wireless_uart.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_wireless_uart.c) · [zf_device_ov7725.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ov7725.h) · [zf_device_ov7725.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ov7725.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化无线UART传输与assistant interface，反复初始化相机；INCLUDE_BOUNDARY_TYPE默认3发送XY人工边界，0为仅图像、1 X、2 Y、4仅边界；新帧清标志、复制帧并camera_send。

关键API：`gpio_init`、`wireless_uart_init`、`gpio_toggle_level`、`seekfree_assistant_interface_init`、`ov7725_init`、`seekfree_assistant_camera_information_config`、`elif`、`seekfree_assistant_camera_boundary_config`、`seekfree_assistant_camera_send`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 助手选择无线UART连接与OV7725格式，遮挡/移动相机检查实时图；默认有三条人工边界，改0后无边界。

**限制：** 边界为演示公式，不是寻线；UART带宽限制帧率。无线仅调试，正式发车携带无效。

**迁入项目：** 保留对应相机帧格式与传输初始化，合并CSI/UART中断；处理帧使用独立快照。

<a id="e8_03"></a>

## E8_03 — OV7725 → 蓝牙CH9141 逐飞助手

原目录：`E8_03_ov7725_bluetooth_ch9141_seekfree_assistant_demo`。用途：在PC查看压缩二值160×120帧与示例边界。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_03_ov7725_bluetooth_ch9141_seekfree_assistant_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_03_ov7725_bluetooth_ch9141_seekfree_assistant_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_03_ov7725_bluetooth_ch9141_seekfree_assistant_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_03_ov7725_bluetooth_ch9141_seekfree_assistant_demo/iar/rt1064.eww)。

**接线与配置：** 蓝牙CH9141接线见下方驱动头文件及通用表。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define INCLUDE_BOUNDARY_TYPE 3
#define BOUNDARY_NUM (OV7725_H * 3 / 2)
#define LED1 (B9 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_bluetooth_ch9141.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_bluetooth_ch9141.h) · [zf_device_bluetooth_ch9141.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_bluetooth_ch9141.c) · [zf_device_ov7725.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ov7725.h) · [zf_device_ov7725.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ov7725.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化蓝牙CH9141传输与assistant interface，反复初始化相机；INCLUDE_BOUNDARY_TYPE默认3发送XY人工边界，0为仅图像、1 X、2 Y、4仅边界；新帧清标志、复制帧并camera_send。

关键API：`gpio_init`、`bluetooth_ch9141_init`、`gpio_toggle_level`、`seekfree_assistant_interface_init`、`ov7725_init`、`seekfree_assistant_camera_information_config`、`elif`、`seekfree_assistant_camera_boundary_config`、`seekfree_assistant_camera_send`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 助手选择蓝牙CH9141连接与OV7725格式，遮挡/移动相机检查实时图；默认有三条人工边界，改0后无边界。

**限制：** 边界为演示公式，不是寻线；UART带宽限制帧率。无线仅调试，正式发车携带无效。

**迁入项目：** 保留对应相机帧格式与传输初始化，合并CSI/UART中断；处理帧使用独立快照。

<a id="e8_04"></a>

## E8_04 — OV7725 → OLED

原目录：`E8_04_ov7725_oled_display_demo`。用途：把压缩二值160×120帧显示到本地OLED。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_04_ov7725_oled_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_04_ov7725_oled_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_04_ov7725_oled_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_04_ov7725_oled_display_demo/iar/rt1064.eww)。

**接线与配置：** OLED按通用SPI屏接线。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define LED1 (B9 )
```

接线/API依据：[zf_device_oled.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_oled.h) · [zf_device_oled.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_oled.c) · [zf_device_ov7725.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ov7725.h) · [zf_device_ov7725.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ov7725.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化屏幕，循环重试ov7725_init并显示初始化状态；finish_flag到来时调用 `oled_displayimage7725((const uint8 *)ov7725_image_binary)`，随后清标志。

关键API：`oled_init`、`oled_show_string`、`ov7725_init`、`oled_displayimage7725`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 接对应相机和屏，看到初始化成功及随遮挡变化的实时图；确认裁剪、方向和颜色。

**限制：** 屏幕型号与接口必须匹配；当前选择普通IPS200 SPI。

**迁入项目：** 抽取相机初始化与完成标志处理；现车用E8_08有线图传/串口替代显示；RGB565保持字节序。

<a id="e8_05"></a>

## E8_05 — OV7725 → TFT180

原目录：`E8_05_ov7725_tft180_display_demo`。用途：把压缩二值160×120帧显示到本地TFT180。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_05_ov7725_tft180_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_05_ov7725_tft180_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_05_ov7725_tft180_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_05_ov7725_tft180_display_demo/iar/rt1064.eww)。

**接线与配置：** TFT180按通用SPI屏接线。

接线/API依据：[zf_device_tft180.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_tft180.h) · [zf_device_tft180.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_tft180.c) · [zf_device_ov7725.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ov7725.h) · [zf_device_ov7725.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ov7725.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化屏幕，循环重试ov7725_init并显示初始化状态；finish_flag到来时调用 `tft180_displayimage7725((const uint8 *)ov7725_image_binary, 160, 128)`，随后清标志。

关键API：`tft180_set_dir`、`tft180_init`、`tft180_show_string`、`ov7725_init`、`tft180_displayimage7725`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 接对应相机和屏，看到初始化成功及随遮挡变化的实时图；确认裁剪、方向和颜色。

**限制：** 屏幕型号与接口必须匹配；当前选择普通IPS200 SPI。

**迁入项目：** 抽取相机初始化与完成标志处理；现车用E8_08有线图传/串口替代显示；RGB565保持字节序。

<a id="e8_06"></a>

## E8_06 — OV7725 → IPS114

原目录：`E8_06_ov7725_ips114_display_demo`。用途：把压缩二值160×120帧显示到本地IPS114。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_06_ov7725_ips114_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_06_ov7725_ips114_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_06_ov7725_ips114_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_06_ov7725_ips114_display_demo/iar/rt1064.eww)。

**接线与配置：** IPS114按通用SPI屏接线。

接线/API依据：[zf_device_ips114.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ips114.h) · [zf_device_ips114.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ips114.c) · [zf_device_ov7725.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ov7725.h) · [zf_device_ov7725.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ov7725.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化屏幕，循环重试ov7725_init并显示初始化状态；finish_flag到来时调用 `ips114_displayimage7725((const uint8 *)ov7725_image_binary, 240, 135)`，随后清标志。

关键API：`ips114_init`、`ips114_show_string`、`ov7725_init`、`ips114_displayimage7725`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 接对应相机和屏，看到初始化成功及随遮挡变化的实时图；确认裁剪、方向和颜色。

**限制：** 屏幕型号与接口必须匹配；当前选择普通IPS200 SPI。

**迁入项目：** 抽取相机初始化与完成标志处理；现车用E8_08有线图传/串口替代显示；RGB565保持字节序。

<a id="e8_07"></a>

## E8_07 — OV7725 → IPS200

原目录：`E8_07_ov7725_ips200_display_demo`。用途：把压缩二值160×120帧显示到本地IPS200。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_07_ov7725_ips200_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_07_ov7725_ips200_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_07_ov7725_ips200_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_07_ov7725_ips200_display_demo/iar/rt1064.eww)。

**接线与配置：** IPS200 PARALLEL8接线见通用表。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define IPS200_TYPE (IPS200_TYPE_PARALLEL8)
```

接线/API依据：[zf_device_ips200.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ips200.h) · [zf_device_ips200.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ips200.c) · [zf_device_ov7725.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ov7725.h) · [zf_device_ov7725.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ov7725.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化屏幕，循环重试ov7725_init并显示初始化状态；finish_flag到来时调用 `ips200_displayimage7725((const uint8 *)ov7725_image_binary, 240, 180)`，随后清标志。

关键API：`ips200_init`、`ips200_show_string`、`ov7725_init`、`ips200_displayimage7725`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 接对应相机和屏，看到初始化成功及随遮挡变化的实时图；确认裁剪、方向和颜色。

**限制：** 屏幕型号与接口必须匹配；当前选择普通IPS200 SPI。 本例IPS200用PARALLEL8而非SPI。

**迁入项目：** 抽取相机初始化与完成标志处理；现车用E8_08有线图传/串口替代显示；RGB565保持字节序。

<a id="e8_08"></a>

## E8_08 — MT9V03X → debug UART1 逐飞助手

原目录：`E8_08_mt9v03x_seekfree_assistant_demo`。用途：在PC查看灰度188×120帧与示例边界。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_08_mt9v03x_seekfree_assistant_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_08_mt9v03x_seekfree_assistant_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_08_mt9v03x_seekfree_assistant_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_08_mt9v03x_seekfree_assistant_demo/iar/rt1064.eww)。

**接线与配置：** debug UART1接线见下方驱动头文件及通用表。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define INCLUDE_BOUNDARY_TYPE 3
#define BOUNDARY_NUM (MT9V03X_H * 3 / 2)
#define LED1 (B9 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_mt9v03x.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_mt9v03x.h) · [zf_device_mt9v03x.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_mt9v03x.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化debug UART1传输与assistant interface，反复初始化相机；INCLUDE_BOUNDARY_TYPE默认3发送XY人工边界，0为仅图像、1 X、2 Y、4仅边界；新帧清标志、复制帧并camera_send。

关键API：`seekfree_assistant_interface_init`、`gpio_init`、`mt9v03x_init`、`gpio_toggle_level`、`seekfree_assistant_camera_information_config`、`elif`、`seekfree_assistant_camera_boundary_config`、`seekfree_assistant_camera_send`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 助手选择debug UART1连接与MT9V03X格式，遮挡/移动相机检查实时图；默认有三条人工边界，改0后无边界。

**限制：** 边界为演示公式，不是寻线；UART带宽限制帧率。

**迁入项目：** 本例保留为有线图传参考；当前MT9V034采用IPS200 SPI本地显示和Wi-Fi SPI图传，采集与协议可复用。

<a id="e8_09"></a>

## E8_09 — MT9V03X → 无线UART 逐飞助手

原目录：`E8_09_mt9v03x_wireless_uart_seekfree_assistant_demo`。用途：在PC查看灰度188×120帧与示例边界。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_09_mt9v03x_wireless_uart_seekfree_assistant_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_09_mt9v03x_wireless_uart_seekfree_assistant_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_09_mt9v03x_wireless_uart_seekfree_assistant_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_09_mt9v03x_wireless_uart_seekfree_assistant_demo/iar/rt1064.eww)。

**接线与配置：** 无线UART接线见下方驱动头文件及通用表。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define INCLUDE_BOUNDARY_TYPE 3
#define BOUNDARY_NUM (MT9V03X_H * 3 / 2)
#define LED1 (B9 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_wireless_uart.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_wireless_uart.h) · [zf_device_wireless_uart.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_wireless_uart.c) · [zf_device_mt9v03x.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_mt9v03x.h) · [zf_device_mt9v03x.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_mt9v03x.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化无线UART传输与assistant interface，反复初始化相机；INCLUDE_BOUNDARY_TYPE默认3发送XY人工边界，0为仅图像、1 X、2 Y、4仅边界；新帧清标志、复制帧并camera_send。

关键API：`gpio_init`、`wireless_uart_init`、`gpio_toggle_level`、`seekfree_assistant_interface_init`、`mt9v03x_init`、`seekfree_assistant_camera_information_config`、`elif`、`seekfree_assistant_camera_boundary_config`、`seekfree_assistant_camera_send`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 助手选择无线UART连接与MT9V03X格式，遮挡/移动相机检查实时图；默认有三条人工边界，改0后无边界。

**限制：** 边界为演示公式，不是寻线；UART带宽限制帧率。无线仅调试，正式发车携带无效。

**迁入项目：** 保留对应相机帧格式与传输初始化，合并CSI/UART中断；处理帧使用独立快照。

<a id="e8_10"></a>

## E8_10 — MT9V03X → 蓝牙CH9141 逐飞助手

原目录：`E8_10_mt9v03x_bluetooth_ch9141_seekfree_assistant_demo`。用途：在PC查看灰度188×120帧与示例边界。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_10_mt9v03x_bluetooth_ch9141_seekfree_assistant_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_10_mt9v03x_bluetooth_ch9141_seekfree_assistant_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_10_mt9v03x_bluetooth_ch9141_seekfree_assistant_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_10_mt9v03x_bluetooth_ch9141_seekfree_assistant_demo/iar/rt1064.eww)。

**接线与配置：** 蓝牙CH9141接线见下方驱动头文件及通用表。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define INCLUDE_BOUNDARY_TYPE 3
#define BOUNDARY_NUM (MT9V03X_H * 3 / 2)
#define LED1 (B9 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_bluetooth_ch9141.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_bluetooth_ch9141.h) · [zf_device_bluetooth_ch9141.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_bluetooth_ch9141.c) · [zf_device_mt9v03x.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_mt9v03x.h) · [zf_device_mt9v03x.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_mt9v03x.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化蓝牙CH9141传输与assistant interface，反复初始化相机；INCLUDE_BOUNDARY_TYPE默认3发送XY人工边界，0为仅图像、1 X、2 Y、4仅边界；新帧清标志、复制帧并camera_send。

关键API：`gpio_init`、`bluetooth_ch9141_init`、`gpio_toggle_level`、`seekfree_assistant_interface_init`、`mt9v03x_init`、`seekfree_assistant_camera_information_config`、`elif`、`seekfree_assistant_camera_boundary_config`、`seekfree_assistant_camera_send`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 助手选择蓝牙CH9141连接与MT9V03X格式，遮挡/移动相机检查实时图；默认有三条人工边界，改0后无边界。

**限制：** 边界为演示公式，不是寻线；UART带宽限制帧率。无线仅调试，正式发车携带无效。

**迁入项目：** 保留对应相机帧格式与传输初始化，合并CSI/UART中断；处理帧使用独立快照。

<a id="e8_11"></a>

## E8_11 — MT9V03X → OLED

原目录：`E8_11_mt9v03x_oled_display_demo`。用途：把灰度188×120帧显示到本地OLED。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_11_mt9v03x_oled_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_11_mt9v03x_oled_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_11_mt9v03x_oled_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_11_mt9v03x_oled_display_demo/iar/rt1064.eww)。

**接线与配置：** OLED按通用SPI屏接线。

接线/API依据：[zf_device_oled.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_oled.h) · [zf_device_oled.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_oled.c) · [zf_device_mt9v03x.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_mt9v03x.h) · [zf_device_mt9v03x.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_mt9v03x.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化屏幕，循环重试mt9v03x_init并显示初始化状态；finish_flag到来时调用 `oled_displayimage03x((const uint8 *)mt9v03x_image, 64)`，随后清标志。

关键API：`oled_init`、`oled_show_string`、`mt9v03x_init`、`oled_displayimage03x`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 接对应相机和屏，看到初始化成功及随遮挡变化的实时图；确认裁剪、方向和颜色。

**限制：** 屏幕型号与接口必须匹配；当前选择普通IPS200 SPI。 OLED灰度显示参数64为阈值，画面只有黑白。

**迁入项目：** 抽取相机初始化与完成标志处理；现车用E8_08有线图传/串口替代显示；RGB565保持字节序。

<a id="e8_12"></a>

## E8_12 — MT9V03X → TFT180

原目录：`E8_12_mt9v03x_tft180_display_demo`。用途：把灰度188×120帧显示到本地TFT180。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_12_mt9v03x_tft180_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_12_mt9v03x_tft180_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_12_mt9v03x_tft180_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_12_mt9v03x_tft180_display_demo/iar/rt1064.eww)。

**接线与配置：** TFT180按通用SPI屏接线。

接线/API依据：[zf_device_tft180.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_tft180.h) · [zf_device_tft180.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_tft180.c) · [zf_device_mt9v03x.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_mt9v03x.h) · [zf_device_mt9v03x.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_mt9v03x.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化屏幕，循环重试mt9v03x_init并显示初始化状态；finish_flag到来时调用 `tft180_displayimage03x((const uint8 *)mt9v03x_image, 160, 128)`，随后清标志。

关键API：`tft180_set_dir`、`tft180_init`、`tft180_show_string`、`mt9v03x_init`、`tft180_displayimage03x`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 接对应相机和屏，看到初始化成功及随遮挡变化的实时图；确认裁剪、方向和颜色。

**限制：** 屏幕型号与接口必须匹配；当前选择普通IPS200 SPI。

**迁入项目：** 抽取相机初始化与完成标志处理；现车用E8_08有线图传/串口替代显示；RGB565保持字节序。

<a id="e8_13"></a>

## E8_13 — MT9V03X → IPS114

原目录：`E8_13_mt9v03x_ips114_display_demo`。用途：把灰度188×120帧显示到本地IPS114。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_13_mt9v03x_ips114_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_13_mt9v03x_ips114_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_13_mt9v03x_ips114_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_13_mt9v03x_ips114_display_demo/iar/rt1064.eww)。

**接线与配置：** IPS114按通用SPI屏接线。

接线/API依据：[zf_device_ips114.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ips114.h) · [zf_device_ips114.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ips114.c) · [zf_device_mt9v03x.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_mt9v03x.h) · [zf_device_mt9v03x.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_mt9v03x.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化屏幕，循环重试mt9v03x_init并显示初始化状态；finish_flag到来时调用 `ips114_displayimage03x((const uint8 *)mt9v03x_image, 240, 135)`，随后清标志。

关键API：`ips114_init`、`ips114_show_string`、`mt9v03x_init`、`ips114_displayimage03x`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 接对应相机和屏，看到初始化成功及随遮挡变化的实时图；确认裁剪、方向和颜色。

**限制：** 屏幕型号与接口必须匹配；当前选择普通IPS200 SPI。

**迁入项目：** 抽取相机初始化与完成标志处理；现车用E8_08有线图传/串口替代显示；RGB565保持字节序。

<a id="e8_14"></a>

## E8_14 — MT9V03X → IPS200

原目录：`E8_14_mt9v03x_ips200_display_demo`。用途：把灰度188×120帧显示到本地IPS200。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_14_mt9v03x_ips200_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_14_mt9v03x_ips200_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_14_mt9v03x_ips200_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_14_mt9v03x_ips200_display_demo/iar/rt1064.eww)。

**接线与配置：** IPS200按通用SPI屏接线。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define IPS200_TYPE (IPS200_TYPE_SPI)
```

接线/API依据：[zf_device_ips200.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ips200.h) · [zf_device_ips200.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ips200.c) · [zf_device_mt9v03x.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_mt9v03x.h) · [zf_device_mt9v03x.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_mt9v03x.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化屏幕，循环重试mt9v03x_init并显示初始化状态；finish_flag到来时调用 `ips200_displayimage03x((const uint8 *)mt9v03x_image, 240, 180)`，随后清标志。

关键API：`ips200_init`、`ips200_show_string`、`mt9v03x_init`、`ips200_displayimage03x`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 接对应相机和屏，看到初始化成功及随遮挡变化的实时图；确认裁剪、方向和颜色。

**限制：** 屏幕型号与接口必须匹配；当前选择普通IPS200 SPI。

**迁入项目：** 抽取相机初始化与完成标志处理；现车用E8_08有线图传/串口替代显示；RGB565保持字节序。

<a id="e8_15"></a>

## E8_15 — SCC8660 → USB CDC 逐飞助手

原目录：`E8_15_scc8660_usbcdc_seekfree_assistant_demo`。用途：在PC查看RGB565彩色160×120帧与示例边界。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_15_scc8660_usbcdc_seekfree_assistant_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_15_scc8660_usbcdc_seekfree_assistant_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_15_scc8660_usbcdc_seekfree_assistant_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_15_scc8660_usbcdc_seekfree_assistant_demo/iar/rt1064.eww)。

**接线与配置：** USB CDC接线见下方驱动头文件及通用表。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define INCLUDE_BOUNDARY_TYPE 3
#define BOUNDARY_NUM (SCC8660_H * 3 / 2)
#define LED1 (B9 )
```

接线/API依据：[zf_driver_usb_cdc.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_usb_cdc.h) · [zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_scc8660.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_scc8660.h) · [zf_device_scc8660.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_scc8660.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化USB CDC传输与assistant interface，反复初始化相机；INCLUDE_BOUNDARY_TYPE默认3发送XY人工边界，0为仅图像、1 X、2 Y、4仅边界；新帧清标志、复制帧并camera_send。

关键API：`usb_cdc_init`、`seekfree_assistant_interface_init`、`gpio_init`、`scc8660_init`、`gpio_toggle_level`、`seekfree_assistant_camera_information_config`、`elif`、`seekfree_assistant_camera_boundary_config`、`seekfree_assistant_camera_send`、`seekfree_assistant_transfer`、`usb_cdc_write_buffer`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 助手选择USB CDC连接与SCC8660格式，遮挡/移动相机检查实时图；默认有三条人工边界，改0后无边界。

**限制：** 边界为演示公式，不是寻线；UART带宽限制帧率。

**迁入项目：** 保留对应相机帧格式与传输初始化，合并CSI/UART中断；处理帧使用独立快照。

<a id="e8_16"></a>

## E8_16 — SCC8660 → TFT180

原目录：`E8_16_scc8660_tft180_display_demo`。用途：把RGB565彩色160×120帧显示到本地TFT180。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_16_scc8660_tft180_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_16_scc8660_tft180_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_16_scc8660_tft180_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_16_scc8660_tft180_display_demo/iar/rt1064.eww)。

**接线与配置：** TFT180按通用SPI屏接线。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define LED1 (B9 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_tft180.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_tft180.h) · [zf_device_tft180.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_tft180.c) · [zf_device_scc8660.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_scc8660.h) · [zf_device_scc8660.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_scc8660.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化屏幕，循环重试scc8660_init并显示初始化状态；finish_flag到来时调用 `tft180_displayimage8660((const uint16 *)scc8660_image, 160, 128)`，随后清标志。

关键API：`gpio_init`、`tft180_set_dir`、`tft180_init`、`tft180_show_string`、`scc8660_init`、`gpio_toggle_level`、`tft180_displayimage8660`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 接对应相机和屏，看到初始化成功及随遮挡变化的实时图；确认裁剪、方向和颜色。

**限制：** 屏幕型号与接口必须匹配；当前选择普通IPS200 SPI。

**迁入项目：** 抽取相机初始化与完成标志处理；现车用E8_08有线图传/串口替代显示；RGB565保持字节序。

<a id="e8_17"></a>

## E8_17 — SCC8660 → IPS114

原目录：`E8_17_scc8660_ips114_display_demo`。用途：把RGB565彩色160×120帧显示到本地IPS114。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_17_scc8660_ips114_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_17_scc8660_ips114_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_17_scc8660_ips114_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_17_scc8660_ips114_display_demo/iar/rt1064.eww)。

**接线与配置：** IPS114按通用SPI屏接线。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define LED1 (B9 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_ips114.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ips114.h) · [zf_device_ips114.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ips114.c) · [zf_device_scc8660.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_scc8660.h) · [zf_device_scc8660.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_scc8660.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化屏幕，循环重试scc8660_init并显示初始化状态；finish_flag到来时调用 `ips114_displayimage8660((const uint16 *)scc8660_image, 240, 135)`，随后清标志。

关键API：`gpio_init`、`ips114_init`、`ips114_show_string`、`scc8660_init`、`gpio_toggle_level`、`ips114_displayimage8660`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 接对应相机和屏，看到初始化成功及随遮挡变化的实时图；确认裁剪、方向和颜色。

**限制：** 屏幕型号与接口必须匹配；当前选择普通IPS200 SPI。

**迁入项目：** 抽取相机初始化与完成标志处理；现车用E8_08有线图传/串口替代显示；RGB565保持字节序。

<a id="e8_18"></a>

## E8_18 — SCC8660 → IPS200

原目录：`E8_18_scc8660_ips200_display_demo`。用途：把RGB565彩色160×120帧显示到本地IPS200。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_18_scc8660_ips200_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_18_scc8660_ips200_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_18_scc8660_ips200_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_18_scc8660_ips200_display_demo/iar/rt1064.eww)。

**接线与配置：** IPS200按通用SPI屏接线。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define IPS200_TYPE (IPS200_TYPE_SPI)
#define LED1 (B9 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_ips200.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ips200.h) · [zf_device_ips200.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_ips200.c) · [zf_device_scc8660.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_scc8660.h) · [zf_device_scc8660.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_device/zf_device_scc8660.c) · [CSI引脚与DMA实现](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/libraries/zf_driver/zf_driver_csi.c)。

**初始化、主循环与中断：** 初始化屏幕，循环重试scc8660_init并显示初始化状态；finish_flag到来时调用 `ips200_displayimage8660((const uint16 *)scc8660_image, 320, 240)`，随后清标志。

关键API：`gpio_init`、`ips200_set_dir`、`ips200_init`、`ips200_show_string`、`scc8660_init`、`gpio_toggle_level`、`ips200_displayimage8660`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 接对应相机和屏，看到初始化成功及随遮挡变化的实时图；确认裁剪、方向和颜色。

**限制：** 屏幕型号与接口必须匹配；当前选择普通IPS200 SPI。

**迁入项目：** 抽取相机初始化与完成标志处理；现车用E8_08有线图传/串口替代显示；RGB565保持字节序。

