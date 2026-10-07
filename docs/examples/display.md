# 显示器

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

图像控件和主题菜单另需 [摄像头接线](camera.md)。主题1/2有额外 `user/src` 和 `user/inc` 文件，需一起审阅。

<a id="e5_01"></a>

## E5_01 — OLED 基础显示

原目录：`E5_01_oled_display_demo`。用途：验证128×64显示器的文本、图像和图形接口。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_01_oled_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_01_oled_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_01_oled_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_01_oled_display_demo/iar/rt1064.eww)。

**接线与配置：** 底层屏接SPI3：SCK B0、MOSI B1、RES/RST B2、DC C19、CS B3；彩屏背光C18。

接线/API依据：[zf_device_oled.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_oled.h) · [zf_device_oled.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_oled.c)。

**初始化、主循环与中断：** oled_init；先数字/汉字，再两种波形缩放、清屏和全屏亮；循环输出有符号/无符号/浮点、波形。

关键API：`oled_init`、`oled_clear`、`oled_show_string`、`oled_show_float`、`oled_show_int`、`oled_show_uint`、`oled_show_chinese`、`oled_show_wave`、`oled_full`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 观察logo/文字/波形和颜色阶段，确认方向、裁剪、字符位宽是否正确。

**限制：** 本例使用秒级阻塞展示；集成前核对屏幕型号和接口，当前实车为普通IPS200 SPI。

**迁入项目：** 有屏调试时将所需show接口放低频任务；现车改串口诊断。

<a id="e5_02"></a>

## E5_02 — TFT180 基础显示

原目录：`E5_02_tft180_display_demo`。用途：验证128×160显示器的文本、图像和图形接口。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_02_tft180_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_02_tft180_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_02_tft180_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_02_tft180_display_demo/iar/rt1064.eww)。

**接线与配置：** 底层屏接SPI3：SCK B0、MOSI B1、RES/RST B2、DC C19、CS B3；彩屏背光C18。

接线/API依据：[zf_device_tft180.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_tft180.h) · [zf_device_tft180.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_tft180.c)。

**初始化、主循环与中断：** tft180_init；竖屏、8×16字体、红前景；logo缩至120×40；循环输出有符号/无符号/浮点、波形、连线和红绿蓝白纯色。

关键API：`tft180_set_dir`、`tft180_set_font`、`tft180_set_color`、`tft180_init`、`tft180_clear`、`tft180_show_rgb565_image`、`tft180_full`、`tft180_show_string`、`tft180_show_chinese`、`tft180_show_float`、`tft180_show_int`、`tft180_show_uint`、`tft180_show_wave`、`tft180_draw_line`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 观察logo/文字/波形和颜色阶段，确认方向、裁剪、字符位宽是否正确。

**限制：** 本例使用秒级阻塞展示；集成前核对屏幕型号和接口，当前实车为普通IPS200 SPI。

**迁入项目：** 有屏调试时将所需show接口放低频任务；现车改串口诊断。

<a id="e5_03"></a>

## E5_03 — IPS114 基础显示

原目录：`E5_03_ips114_display_demo`。用途：验证240×135显示器的文本、图像和图形接口。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_03_ips114_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_03_ips114_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_03_ips114_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_03_ips114_display_demo/iar/rt1064.eww)。

**接线与配置：** 底层屏接SPI3：SCK B0、MOSI B1、RES/RST B2、DC C19、CS B3；彩屏背光C18。

接线/API依据：[zf_device_ips114.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips114.h) · [zf_device_ips114.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips114.c)。

**初始化、主循环与中断：** ips114_init；8×16字体、红前景；logo原尺寸240×80；循环输出有符号/无符号/浮点、波形、连线和红绿蓝白纯色。

关键API：`ips114_set_font`、`ips114_set_color`、`ips114_init`、`ips114_clear`、`ips114_show_rgb565_image`、`ips114_full`、`ips114_show_string`、`ips114_show_chinese`、`ips114_show_float`、`ips114_show_int`、`ips114_show_uint`、`ips114_show_wave`、`ips114_draw_line`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 观察logo/文字/波形和颜色阶段，确认方向、裁剪、字符位宽是否正确。

**限制：** 本例使用秒级阻塞展示；集成前核对屏幕型号和接口，当前实车为普通IPS200 SPI。

**迁入项目：** 有屏调试时将所需show接口放低频任务；现车改串口诊断。

<a id="e5_04"></a>

## E5_04 — IPS200 基础显示

原目录：`E5_04_ips200_display_demo`。用途：验证240×320显示器的文本、图像和图形接口。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_04_ips200_display_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_04_ips200_display_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_04_ips200_display_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_04_ips200_display_demo/iar/rt1064.eww)。

**接线与配置：** 底层屏接SPI3：SCK B0、MOSI B1、RES/RST B2、DC C19、CS B3；彩屏背光C18。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define IPS200_TYPE (IPS200_TYPE_SPI)
```

接线/API依据：[zf_device_ips200.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200.h) · [zf_device_ips200.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200.c)。

**初始化、主循环与中断：** ips200_init；SPI模式、竖屏、8×16字体；logo原尺寸240×80；循环输出有符号/无符号/浮点、波形、连线和红绿蓝白纯色。

关键API：`ips200_set_dir`、`ips200_set_font`、`ips200_set_color`、`ips200_init`、`ips200_clear`、`ips200_show_rgb565_image`、`ips200_full`、`ips200_show_string`、`ips200_show_chinese`、`ips200_show_float`、`ips200_show_int`、`ips200_show_uint`、`ips200_show_wave`、`ips200_draw_line`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 观察logo/文字/波形和颜色阶段，确认方向、裁剪、字符位宽是否正确。

**限制：** 本例使用秒级阻塞展示；集成前核对屏幕型号和接口，当前实车为普通IPS200 SPI。

**迁入项目：** 有屏调试时将所需show接口放低频任务；现车改串口诊断。

<a id="e5_05"></a>

## E5_05 — IPS200 Pro 页面

原目录：`E5_05_ips200pro_page`。用途：验证Pro屏的页面协议与控件生命周期。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_05_ips200pro_page/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_05_ips200pro_page/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_05_ips200pro_page/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_05_ips200pro_page/iar/rt1064.eww)。

**接线与配置：** IPS200 Pro SPI3 40MHz：SCK B0、MOSI B1、CS B3、RST B2、INT C19；图像例另需对应摄像头。

接线/API依据：[zf_device_ips200pro.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.h) · [zf_device_ips200pro.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.c)。

**初始化、主循环与中断：** 创建三个页面，修改第三页标题、隐藏/恢复页0、设置页面颜色；每秒循环切换页面。

关键API：`ips200pro_init`、`ips200pro_get_information`、`ips200pro_get_free_stack_size`、`ips200pro_get_time`、`ips200pro_set_default_font`、`ips200pro_set_direction`、`ips200pro_page_create`、`ips200pro_page_set_title_name`、`ips200pro_page_hidden`、`ips200pro_set_color`、`ips200pro_page_switch`、`ips200pro_set_backlight`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 观察三个背景色页面和标题依次出现。

**限制：** 需IPS200 Pro模块，普通IPS200不是同一API；现车为普通IPS200 SPI，本例Pro依赖不直接迁入。

**迁入项目：** 将页面ID与低频状态显示绑定，避免反复创建；若现车调试则采用串口替代。

<a id="e5_06"></a>

## E5_06 — IPS200 Pro 文本标签

原目录：`E5_06_ips200pro_label`。用途：验证Pro屏的文本标签协议与控件生命周期。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_06_ips200pro_label/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_06_ips200pro_label/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_06_ips200pro_label/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_06_ips200pro_label/iar/rt1064.eww)。

**接线与配置：** IPS200 Pro SPI3 40MHz：SCK B0、MOSI B1、CS B3、RST B2、INT C19；图像例另需对应摄像头。

接线/API依据：[zf_device_ips200pro.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.h) · [zf_device_ips200pro.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.c)。

**初始化、主循环与中断：** 创建四个label，演示printf格式化、GBK中文、多行/大字体及长文本。

关键API：`ips200pro_init`、`ips200pro_get_information`、`ips200pro_get_free_stack_size`、`ips200pro_get_time`、`ips200pro_set_default_font`、`ips200pro_set_direction`、`ips200pro_set_format`、`ips200pro_set_color`、`ips200pro_page_switch`、`ips200pro_label_create`、`ips200pro_label_printf`、`ips200pro_set_font`、`ips200pro_label_show_string`、`ips200pro_set_backlight`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 观察英文标签、Power=66/Speed=66.666、多行中文与长文本。

**限制：** 需IPS200 Pro模块，普通IPS200不是同一API；现车为普通IPS200 SPI，本例Pro依赖不直接迁入。

**迁入项目：** 只更新变化字段并保留ID，中文编码与set_format一致；若现车调试则采用串口替代。

<a id="e5_07"></a>

## E5_07 — IPS200 Pro 表格

原目录：`E5_07_ips200pro_table`。用途：验证Pro屏的表格协议与控件生命周期。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_07_ips200pro_table/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_07_ips200pro_table/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_07_ips200pro_table/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_07_ips200pro_table/iar/rt1064.eww)。

**接线与配置：** IPS200 Pro SPI3 40MHz：SCK B0、MOSI B1、CS B3、RST B2、INT C19；图像例另需对应摄像头。

接线/API依据：[zf_device_ips200pro.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.h) · [zf_device_ips200pro.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.c)。

**初始化、主循环与中断：** 创建两张4×3表，单元格格式化水果价格，设置第二表列宽60和颜色。

关键API：`ips200pro_init`、`ips200pro_get_information`、`ips200pro_get_free_stack_size`、`ips200pro_get_time`、`ips200pro_set_default_font`、`ips200pro_set_direction`、`ips200pro_set_format`、`ips200pro_set_color`、`ips200pro_page_switch`、`ips200pro_table_create`、`ips200pro_table_cell_printf`、`ips200pro_table_set_col_width`、`ips200pro_set_backlight`、`ips200pro_table_select`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 核对两张表的行列、单元格文字与边框。

**限制：** 需IPS200 Pro模块，普通IPS200不是同一API；现车为普通IPS200 SPI，本例Pro依赖不直接迁入。

**迁入项目：** 把传感器/标定值映射到单元格，使用同一表对象；若现车调试则采用串口替代。

<a id="e5_08"></a>

## E5_08 — IPS200 Pro 仪表

原目录：`E5_08_ips200pro_meter`。用途：验证Pro屏的仪表协议与控件生命周期。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_08_ips200pro_meter/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_08_ips200pro_meter/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_08_ips200pro_meter/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_08_ips200pro_meter/iar/rt1064.eww)。

**接线与配置：** IPS200 Pro SPI3 40MHz：SCK B0、MOSI B1、CS B3、RST B2、INT C19；图像例另需对应摄像头。

接线/API依据：[zf_device_ips200pro.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.h) · [zf_device_ips200pro.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.c)。

**初始化、主循环与中断：** 创建两个仪表，每20ms分别增减3和1，使数值在0~300/0~100往返。

关键API：`ips200pro_init`、`ips200pro_get_information`、`ips200pro_get_free_stack_size`、`ips200pro_get_time`、`ips200pro_set_default_font`、`ips200pro_set_direction`、`ips200pro_set_format`、`ips200pro_set_color`、`ips200pro_set_backlight`、`ips200pro_page_switch`、`ips200pro_meter_create`、`ips200pro_meter_set_value`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 指针或刻度值同步往返，颜色和范围正确。

**限制：** 需IPS200 Pro模块，普通IPS200不是同一API；现车为普通IPS200 SPI，本例Pro依赖不直接迁入。

**迁入项目：** 将真实速度缩放后写入meter，不保留人工扫描；若现车调试则采用串口替代。

<a id="e5_09"></a>

## E5_09 — IPS200 Pro 时钟

原目录：`E5_09_ips200pro_clock`。用途：验证Pro屏的时钟协议与控件生命周期。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_09_ips200pro_clock/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_09_ips200pro_clock/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_09_ips200pro_clock/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_09_ips200pro_clock/iar/rt1064.eww)。

**接线与配置：** IPS200 Pro SPI3 40MHz：SCK B0、MOSI B1、CS B3、RST B2、INT C19；图像例另需对应摄像头。

接线/API依据：[zf_device_ips200pro.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.h) · [zf_device_ips200pro.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.c)。

**初始化、主循环与中断：** 创建200尺寸模拟时钟，分别设时分秒针颜色，将时间设为15:30:00；主循环空转。

关键API：`ips200pro_init`、`ips200pro_get_information`、`ips200pro_get_free_stack_size`、`ips200pro_get_time`、`ips200pro_set_default_font`、`ips200pro_set_direction`、`ips200pro_set_format`、`ips200pro_set_color`、`ips200pro_set_backlight`、`ips200pro_page_switch`、`ips200pro_clock_create`、`ips200pro_set_time`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 显示15:30起始时钟并随模块计时变化。

**限制：** 需IPS200 Pro模块，普通IPS200不是同一API；现车为普通IPS200 SPI，本例Pro依赖不直接迁入。

**迁入项目：** 时钟是模块功能；按实际时间源校时，不能当控制定时器；若现车调试则采用串口替代。

<a id="e5_10"></a>

## E5_10 — IPS200 Pro 进度条

原目录：`E5_10_ips200pro_progress_bar`。用途：验证Pro屏的进度条协议与控件生命周期。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_10_ips200pro_progress_bar/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_10_ips200pro_progress_bar/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_10_ips200pro_progress_bar/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_10_ips200pro_progress_bar/iar/rt1064.eww)。

**接线与配置：** IPS200 Pro SPI3 40MHz：SCK B0、MOSI B1、CS B3、RST B2、INT C19；图像例另需对应摄像头。

接线/API依据：[zf_device_ips200pro.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.h) · [zf_device_ips200pro.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.c)。

**初始化、主循环与中断：** 创建五个横/竖进度条，每20ms更新区间端点，先递增再递减。

关键API：`ips200pro_init`、`ips200pro_get_information`、`ips200pro_get_free_stack_size`、`ips200pro_get_time`、`ips200pro_set_default_font`、`ips200pro_set_direction`、`ips200pro_set_format`、`ips200pro_set_color`、`ips200pro_set_backlight`、`ips200pro_page_switch`、`ips200pro_progress_bar_create`、`ips200pro_progress_bar_set_value`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 看到从0端增长、移动10宽区间和从100端变化等效果。

**限制：** 需IPS200 Pro模块，普通IPS200不是同一API；现车为普通IPS200 SPI，本例Pro依赖不直接迁入。

**迁入项目：** 按电量/任务进度映射到区间，明确两参数不是单一百分比；若现车调试则采用串口替代。

<a id="e5_11"></a>

## E5_11 — IPS200 Pro 日历

原目录：`E5_11_ips200pro_calendar`。用途：验证Pro屏的日历协议与控件生命周期。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_11_ips200pro_calendar/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_11_ips200pro_calendar/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_11_ips200pro_calendar/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_11_ips200pro_calendar/iar/rt1064.eww)。

**接线与配置：** IPS200 Pro SPI3 40MHz：SCK B0、MOSI B1、CS B3、RST B2、INT C19；图像例另需对应摄像头。

接线/API依据：[zf_device_ips200pro.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.h) · [zf_device_ips200pro.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.c)。

**初始化、主循环与中断：** 设日期2025-2-17；每2秒交替显示2030年2月英文与2025年2月中文日历。

关键API：`ips200pro_init`、`ips200pro_get_information`、`ips200pro_get_free_stack_size`、`ips200pro_get_time`、`ips200pro_set_default_font`、`ips200pro_set_direction`、`ips200pro_set_format`、`ips200pro_set_color`、`ips200pro_set_backlight`、`ips200pro_page_switch`、`ips200pro_calendar_create`、`ips200pro_set_date`、`ips200pro_get_date`、`ips200pro_calendar_display`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 年份/月份和语言交替，日期标识可见。

**限制：** 需IPS200 Pro模块，普通IPS200不是同一API；现车为普通IPS200 SPI，本例Pro依赖不直接迁入。

**迁入项目：** 日期与显示月份分开管理，移除演示年份切换；若现车调试则采用串口替代。

<a id="e5_12"></a>

## E5_12 — IPS200 Pro 波形

原目录：`E5_12_ips200pro_waveform`。用途：验证Pro屏的波形协议与控件生命周期。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_12_ips200pro_waveform/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_12_ips200pro_waveform/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_12_ips200pro_waveform/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_12_ips200pro_waveform/iar/rt1064.eww)。

**接线与配置：** IPS200 Pro SPI3 40MHz：SCK B0、MOSI B1、CS B3、RST B2、INT C19；图像例另需对应摄像头。

接线/API依据：[zf_device_ips200pro.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.h) · [zf_device_ips200pro.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.c)。

**初始化、主循环与中断：** 创建200×150波形；通道1每20ms追加单个正弦点，通道2批量200点；切换线型并清屏。

关键API：`ips200pro_init`、`ips200pro_get_information`、`ips200pro_get_free_stack_size`、`ips200pro_get_time`、`ips200pro_set_default_font`、`ips200pro_set_direction`、`ips200pro_set_format`、`ips200pro_set_color`、`ips200pro_set_backlight`、`ips200pro_page_switch`、`ips200pro_waveform_create`、`sin`、`ips200pro_waveform_add_value`、`ips200pro_waveform_line_type`、`ips200pro_waveform_clear`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 蓝/棕两路正弦出现，线型改变后清屏重绘。

**限制：** 需IPS200 Pro模块，普通IPS200不是同一API；现车为普通IPS200 SPI，本例Pro依赖不直接迁入。

**迁入项目：** 用环缓冲批量送实际采样值，限制刷新吞吐；若现车调试则采用串口替代。

<a id="e5_13"></a>

## E5_13 — IPS200 Pro 灰度图像控件

原目录：`E5_13_ips200pro_camera_mt9v03x`。用途：验证Pro屏的灰度图像控件协议与控件生命周期。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_13_ips200pro_camera_mt9v03x/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_13_ips200pro_camera_mt9v03x/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_13_ips200pro_camera_mt9v03x/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_13_ips200pro_camera_mt9v03x/iar/rt1064.eww)。

**接线与配置：** IPS200 Pro SPI3 40MHz：SCK B0、MOSI B1、CS B3、RST B2、INT C19；图像例另需对应摄像头。

接线/API依据：[zf_device_ips200pro.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.h) · [zf_device_ips200pro.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.c) · [zf_device_mt9v03x.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_mt9v03x.h) · [zf_device_mt9v03x.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_mt9v03x.c)。

**初始化、主循环与中断：** 初始化MT9V03X；每帧绘两条人工蓝/绿边界及移动红矩形，再以IMAGE_GRAYSCALE显示。

关键API：`ips200pro_init`、`ips200pro_get_information`、`ips200pro_get_free_stack_size`、`ips200pro_get_time`、`ips200pro_set_default_font`、`ips200pro_set_direction`、`ips200pro_set_format`、`ips200pro_set_color`、`ips200pro_set_backlight`、`ips200pro_page_switch`、`ips200pro_image_create`、`mt9v03x_init`、`ips200pro_image_draw_line`、`ips200pro_image_draw_rectangle`、`ips200pro_image_display`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 灰度实时图叠加移动线与矩形，遮挡镜头图像变化。

**限制：** 需IPS200 Pro模块，普通IPS200不是同一API；现车为普通IPS200 SPI，本例Pro依赖不直接迁入。

**迁入项目：** 人工边界不是循迹结果；迁入时换为算法边界并保留帧所有权；若现车调试则采用串口替代。

<a id="e5_14"></a>

## E5_14 — IPS200 Pro 彩色图像控件

原目录：`E5_14_ips200pro_camera_scc8660`。用途：验证Pro屏的彩色图像控件协议与控件生命周期。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_14_ips200pro_camera_scc8660/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_14_ips200pro_camera_scc8660/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_14_ips200pro_camera_scc8660/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_14_ips200pro_camera_scc8660/iar/rt1064.eww)。

**接线与配置：** IPS200 Pro SPI3 40MHz：SCK B0、MOSI B1、CS B3、RST B2、INT C19；图像例另需对应摄像头。

接线/API依据：[zf_device_ips200pro.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.h) · [zf_device_ips200pro.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.c) · [zf_device_scc8660.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_scc8660.h) · [zf_device_scc8660.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_scc8660.c)。

**初始化、主循环与中断：** 初始化SCC8660；每帧绘人工边界及移动红矩形，再以IMAGE_RGB565显示。

关键API：`ips200pro_init`、`ips200pro_get_information`、`ips200pro_get_free_stack_size`、`ips200pro_get_time`、`ips200pro_set_default_font`、`ips200pro_set_direction`、`ips200pro_set_format`、`ips200pro_set_color`、`ips200pro_set_backlight`、`ips200pro_page_switch`、`ips200pro_image_create`、`scc8660_init`、`ips200pro_image_draw_line`、`ips200pro_image_draw_rectangle`、`ips200pro_image_display`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 彩色实时图与叠加图形出现，红蓝颜色正确。

**限制：** 需IPS200 Pro模块，普通IPS200不是同一API；现车为普通IPS200 SPI，本例Pro依赖不直接迁入。

**迁入项目：** 按RGB565字节序传输，迁入算法真实边界而非演示曲线；若现车调试则采用串口替代。

<a id="e5_15"></a>

## E5_15 — IPS200 Pro 容器

原目录：`E5_15_ips200pro_container`。用途：验证Pro屏的容器协议与控件生命周期。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_15_ips200pro_container/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_15_ips200pro_container/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_15_ips200pro_container/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_15_ips200pro_container/iar/rt1064.eww)。

**接线与配置：** IPS200 Pro SPI3 40MHz：SCK B0、MOSI B1、CS B3、RST B2、INT C19；图像例另需对应摄像头。

接线/API依据：[zf_device_ips200pro.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.h) · [zf_device_ips200pro.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_ips200pro.c)。

**初始化、主循环与中断：** 创建两个140×30容器，分别圆角/直角；一标签绝对坐标，另一set_parent后相对坐标(0,5)。

关键API：`ips200pro_init`、`ips200pro_get_information`、`ips200pro_get_free_stack_size`、`ips200pro_get_time`、`ips200pro_set_default_font`、`ips200pro_set_direction`、`ips200pro_set_format`、`ips200pro_set_color`、`ips200pro_set_backlight`、`ips200pro_page_switch`、`ips200pro_container_create`、`ips200pro_container_radius`、`ips200pro_label_create`、`ips200pro_label_printf`、`ips200pro_set_parent`、`ips200pro_set_position`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 核对两个边框及各自文字位置。

**限制：** 需IPS200 Pro模块，普通IPS200不是同一API；现车为普通IPS200 SPI，本例Pro依赖不直接迁入。

**迁入项目：** 将成组控件归到父容器，明确相对坐标与ID生命周期；若现车调试则采用串口替代。

<a id="e5_16"></a>

## E5_16 — IPS200 Pro 综合菜单主题1

原目录：`E5_16_ips200pro_theme1`。用途：验证Pro屏的综合菜单主题1协议与控件生命周期。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_16_ips200pro_theme1/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_16_ips200pro_theme1/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_16_ips200pro_theme1/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_16_ips200pro_theme1/iar/rt1064.eww)。

**接线与配置：** IPS200 Pro SPI3 40MHz：SCK B0、MOSI B1、CS B3、RST B2、INT C19；图像例另需对应摄像头。

接线/API依据：[zf_device_mt9v03x.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_mt9v03x.h) · [zf_device_mt9v03x.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_mt9v03x.c) · [zf_device_key.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_key.h) · [zf_device_key.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/libraries/zf_device/zf_device_key.c)。

菜单附加源码：。

**初始化、主循环与中断：** 初始化MT9V03X、key_init(20)、user_menu_init；新帧触发user_menu_loop，持续cpu_usage_check。

关键API：`mt9v03x_init`、`key_init`、`user_menu_init`、`user_menu_loop`、`cpu_usage_check`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 接匹配屏/相机，按键切页面，看图像和菜单；核对user_menu_device内扫描调度。

**限制：** 需IPS200 Pro模块，普通IPS200不是同一API；现车为普通IPS200 SPI，本例Pro依赖不直接迁入。 菜单驱动依赖相机新帧，失去相机可能不更新；迁入时解耦帧与按键任务

**迁入项目：** 菜单驱动依赖相机新帧，失去相机可能不更新；迁入时解耦帧与按键任务；若现车调试则采用串口替代。

<a id="e5_17"></a>

## E5_17 — IPS200 Pro 综合菜单主题2

原目录：`E5_17_ips200pro_theme2`。用途：验证Pro屏的综合菜单主题2协议与控件生命周期。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_17_ips200pro_theme2/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_17_ips200pro_theme2/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_17_ips200pro_theme2/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_17_ips200pro_theme2/iar/rt1064.eww)。

**接线与配置：** IPS200 Pro SPI3 40MHz：SCK B0、MOSI B1、CS B3、RST B2、INT C19；图像例另需对应摄像头。

菜单附加源码：。

**初始化、主循环与中断：** ipspro_page_ctrl_init 后填page3八项文本；循环show_page1(angle/far/image_err/run)、key_loop，rand_num取模10000。

关键API：`ipspro_page_ctrl_init`、`ipspro_page_ctrl_show_page3`、`ipspro_page_ctrl_show_page1`、`ipspro_page_ctrl_key_loop`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 按键切换并检查设置页/文本页；只用专用测试参数区。

**限制：** 需IPS200 Pro模块，普通IPS200不是同一API；现车为普通IPS200 SPI，本例Pro依赖不直接迁入。 含Flash参数保存；迁入前核对存储区并改成显式保存事件，不能自动运行原例

**迁入项目：** 含Flash参数保存；迁入前核对存储区并改成显式保存事件，不能自动运行原例；若现车调试则采用串口替代。

