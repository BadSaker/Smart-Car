# 无线通信

当前工程的屏幕/Wi-Fi已按用户要求冻结在14:33版，见 [冻结约束](../seekfree-assistant-debugging.md#屏幕与wi-fi冻结约束)。下文“迁入项目”的非阻塞改造等内容是例程评估建议，不是当前工程状态或执行授权，不得据此重新改写已冻结模块。


本页以 V3.11.2 的真实 `main.c`、`isr.c` 及本分类 `libraries` 为准。每项均提供原工程入口；所有测试步骤是待上板执行的操作与预期，本次没有编译或运行这84个原例。主板版本未知时，以所用板的原理图、丝印和实际连通性核对引脚。

先阅读 [统一准备与迁入流程](README.md#prepare)。源码中的 `clock_init(SYSTEM_CLOCK_600M)` 保留；调用 `debug_init` 的例程默认 UART1、115200、MCU TX B12/RX B13。原例 `main.c` 不能与车辆工程的 `main` 同时加入，`isr.c` 也必须逐IRQ合并。

## 接线与网络设置

UART模块均使用UART8 115200：MCU TX D16→模块RX，MCU RX D17←模块TX；RTS D26。UART Wi-Fi额外RST D27。驱动宏以模块的TX/RX命名，不能按宏名把线接反。初始化设置 `wireless_module_uart_handler`，LPUART8 IRQ按非空指针分发。

SPI Wi-Fi默认SPI1 50MHz：SCK D12、MOSI D14、MISO D15、CS D13、INT B17、RST B16；与IPS200并口模式冲突。驱动默认目标IP `192.168.2.21`、端口`8086`、本地`6666`；UART Wi-Fi默认目标 `192.168.2.16:8086`、本地`5555`。这些是原例配置，测试前改成PC/AP实际地址，确认PC防火墙和监听协议。SSID/密码在main中配置，禁止把演示凭据当实际网络配置。

无线例程仅用于调试；依据本项目赛项规则，正式发车携带无线模块无效，应拆除模块并关闭相应功能。

<a id="e6_01"></a>

## E6_01 — 无线 UART 回显

原目录：`E6_01_wireless_uart_demo`。用途：验证逐飞无线串口模块收发与流控。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_01_wireless_uart_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_01_wireless_uart_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_01_wireless_uart_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_01_wireless_uart_demo/iar/rt1064.eww)。

**接线与配置：** UART8：模块TX→MCU RX D17，模块RX←MCU TX D16；RTS D26，115200。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define LED1 (B9 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_wireless_uart.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wireless_uart.h) · [zf_device_wireless_uart.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wireless_uart.c)。

**初始化、主循环与中断：** wireless_uart_init；失败B9每100ms闪；成功发欢迎串，主循环每50ms最多读32字节、回发并报告长度。

关键API：`gpio_init`、`wireless_uart_init`、`gpio_toggle_level`、`wireless_uart_send_byte`、`wireless_uart_send_string`、`wireless_uart_read_buffer`、`wireless_uart_send_buffer`、`func_uint_to_str`、`strlen`。

中断入口：UART8接收转交所选模块回调；UART1仅作debug，避免共用接收通道。

**测试与预期：** 配对接收端与终端发送abc，收到原内容和data len:3。

**限制：** 64字节驱动FIFO、32字节应用缓冲；仅调试，正式发车携带无线模块按规则无效。

**迁入项目：** 接收解析加入边界和超时，调试通信与控制状态隔离；正式车拆除模块。

<a id="e6_02"></a>

## E6_02 — CH9141 蓝牙回显

原目录：`E6_02_bluetooth_ch9141_demo`。用途：验证CH9141蓝牙串口链路。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_02_bluetooth_ch9141_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_02_bluetooth_ch9141_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_02_bluetooth_ch9141_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_02_bluetooth_ch9141_demo/iar/rt1064.eww)。

**接线与配置：** UART8 D16 TX/D17 RX、RTS D26，115200。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define LED1 (B9 )
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_driver/zf_driver_gpio.h) · [zf_device_bluetooth_ch9141.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_bluetooth_ch9141.h) · [zf_device_bluetooth_ch9141.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_bluetooth_ch9141.c)。

**初始化、主循环与中断：** bluetooth_ch9141_init失败闪灯，成功等1秒发欢迎串；每50ms读最多32字节、回发及长度。

关键API：`gpio_init`、`bluetooth_ch9141_init`、`gpio_toggle_level`、`bluetooth_ch9141_send_byte`、`bluetooth_ch9141_send_string`、`bluetooth_ch9141_read_buffer`、`bluetooth_ch9141_send_buffer`、`func_uint_to_str`、`strlen`。

中断入口：UART8接收转交所选模块回调；UART1仅作debug，避免共用接收通道。

**测试与预期：** 用匹配蓝牙终端连接并发送abc，看到回显和长度。

**限制：** 蓝牙连接/供电与波特率需匹配；仅调试，正式发车携带无线模块按规则无效。

**迁入项目：** 仅用作诊断接入，移除正式赛无线模块；保留UART8回调分发。

<a id="e6_03"></a>

## E6_03 — UART Wi-Fi UDP

原目录：`E6_03_wifi_uart_udp_demo`。用途：验证对应网络连接方式与应用数据。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_03_wifi_uart_udp_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_03_wifi_uart_udp_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_03_wifi_uart_udp_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_03_wifi_uart_udp_demo/iar/rt1064.eww)。

**接线与配置：** main中的SSID/密码需替换；UART类MCU TX D16/RX D17、RTS D26/RST D27；SPI类SCK D12、MOSI D14、MISO D15、CS D13、INT B17/RST B16。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define WIFI_SSID_TEST "SEEKFREE"
#define WIFI_PASSWORD_TEST "12345678 "
```

接线/API依据：[zf_device_wifi_uart.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_uart.h) · [zf_device_wifi_uart.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_uart.c)。

**初始化、主循环与中断：** STA连AP；AUTO_CONNECT非2时调用connect_udp_client；发送测试串；每100ms读+IPD缓冲并回发。

关键API：`wifi_uart_init`、`wifi_uart_connect_udp_client`、`wifi_uart_send_buffer`、`wifi_uart_read_buffer`、`strstr`。

中断入口：UART8接收转交所选模块回调；UART1仅作debug，避免共用接收通道。

**测试与预期：** PC开UDP端口，收到测试串后发送短文本，确认板端日志与回发。

**限制：** 密码字符串12345678后有空格；UDP建立失败分支仅if，非持续重试；无线仅调试，正式发车携带无效。

**迁入项目：** 保留实际传输API，改成非阻塞连接/断线处理与长度安全协议；正式赛移除模块。

<a id="e6_04"></a>

## E6_04 — UART Wi-Fi TCP 客户端

原目录：`E6_04_wifi_uart_tcp_client_demo`。用途：验证对应网络连接方式与应用数据。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_04_wifi_uart_tcp_client_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_04_wifi_uart_tcp_client_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_04_wifi_uart_tcp_client_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_04_wifi_uart_tcp_client_demo/iar/rt1064.eww)。

**接线与配置：** main中的SSID/密码需替换；UART类MCU TX D16/RX D17、RTS D26/RST D27；SPI类SCK D12、MOSI D14、MISO D15、CS D13、INT B17/RST B16。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define WIFI_SSID_TEST "SEEKFREE"
#define WIFI_PASSWORD_TEST "12345678"
```

接线/API依据：[zf_device_wifi_uart.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_uart.h) · [zf_device_wifi_uart.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_uart.c)。

**初始化、主循环与中断：** STA连AP；AUTO_CONNECT非1时循环connect_tcp_servers；发测试串，每100ms读+IPD并回发。

关键API：`wifi_uart_init`、`wifi_uart_connect_tcp_servers`、`wifi_uart_send_buffer`、`wifi_uart_read_buffer`、`strstr`。

中断入口：UART8接收转交所选模块回调；UART1仅作debug，避免共用接收通道。

**测试与预期：** 先在PC开TCP server，板端连接后应收测试串、回显短文本。

**限制：** 断线没有完整重连状态机；无线仅调试，正式发车携带无效。

**迁入项目：** 保留实际传输API，改成非阻塞连接/断线处理与长度安全协议；正式赛移除模块。

<a id="e6_05"></a>

## E6_05 — UART Wi-Fi TCP 服务端

原目录：`E6_05_wifi_uart_tcp_server_demo`。用途：验证对应网络连接方式与应用数据。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_05_wifi_uart_tcp_server_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_05_wifi_uart_tcp_server_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_05_wifi_uart_tcp_server_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_05_wifi_uart_tcp_server_demo/iar/rt1064.eww)。

**接线与配置：** main中的SSID/密码需替换；UART类MCU TX D16/RX D17、RTS D26/RST D27；SPI类SCK D12、MOSI D14、MISO D15、CS D13、INT B17/RST B16。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define WIFI_SSID_TEST "SEEKFREE"
#define WIFI_PASSWORD_TEST "12345678"
```

接线/API依据：[zf_device_wifi_uart.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_uart.h) · [zf_device_wifi_uart.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_uart.c)。

**初始化、主循环与中断：** STA连AP；AUTO_CONNECT非3时entry_tcp_servers；每100ms找+IPD的link_id并向对应客户端回发、查询连接IP。

关键API：`wifi_uart_init`、`wifi_uart_entry_tcp_servers`、`wifi_uart_read_buffer`、`strstr`、`wifi_uart_tcp_servers_send_buffer`、`wifi_uart_tcp_servers_check_link`、`strlen`。

中断入口：UART8接收转交所选模块回调；UART1仅作debug，避免共用接收通道。

**测试与预期：** PC客户端连板端IP和5555端口，发短文本得到对应连接回发。

**限制：** link_id取单个字符，+IPD串流完整性/缓冲终止需额外处理；无线仅调试，正式发车携带无效。

**迁入项目：** 保留实际传输API，改成非阻塞连接/断线处理与长度安全协议；正式赛移除模块。

<a id="e6_06"></a>

## E6_06 — SPI Wi-Fi UDP

原目录：`E6_06_wifi_spi_udp_demo`。用途：验证对应网络连接方式与应用数据。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_06_wifi_spi_udp_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_06_wifi_spi_udp_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_06_wifi_spi_udp_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_06_wifi_spi_udp_demo/iar/rt1064.eww)。

**接线与配置：** main中的SSID/密码需替换；UART类MCU TX D16/RX D17、RTS D26/RST D27；SPI类SCK D12、MOSI D14、MISO D15、CS D13、INT B17/RST B16。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define WIFI_SSID_TEST "SEEKFREE"
#define WIFI_PASSWORD_TEST "12345678"
```

接线/API依据：[zf_device_wifi_spi.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_spi.h) · [zf_device_wifi_spi.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_spi.c)。

**初始化、主循环与中断：** wifi_spi_init；AUTO_CONNECT=0时socket_connect UDP；发送后udp_send_now立即提交；每100ms读回并回发。

关键API：`wifi_spi_init`、`wifi_spi_socket_connect`、`wifi_spi_send_buffer`、`wifi_spi_udp_send_now`、`wifi_spi_read_buffer`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** PC在目标UDP端口接收测试串并回发，观察即时提交和板日志。

**限制：** UDP必须调用udp_send_now；网络配置需改为本地实际值；无线仅调试，正式发车携带无效。

**迁入项目：** 保留实际传输API，改成非阻塞连接/断线处理与长度安全协议；正式赛移除模块。

<a id="e6_07"></a>

## E6_07 — SPI Wi-Fi TCP 客户端

原目录：`E6_07_wifi_spi_tcp_client_demo`。用途：验证对应网络连接方式与应用数据。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_07_wifi_spi_tcp_client_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_07_wifi_spi_tcp_client_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_07_wifi_spi_tcp_client_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_07_wifi_spi_tcp_client_demo/iar/rt1064.eww)。

**接线与配置：** main中的SSID/密码需替换；UART类MCU TX D16/RX D17、RTS D26/RST D27；SPI类SCK D12、MOSI D14、MISO D15、CS D13、INT B17/RST B16。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define WIFI_SSID_TEST "SEEKFREE"
#define WIFI_PASSWORD_TEST "12345678"
```

接线/API依据：[zf_device_wifi_spi.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_spi.h) · [zf_device_wifi_spi.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_spi.c)。

**初始化、主循环与中断：** wifi_spi_init；AUTO_CONNECT=0时socket_connect TCP；每100ms读取256字节缓冲并回发。

关键API：`wifi_spi_init`、`wifi_spi_socket_connect`、`wifi_spi_send_buffer`、`wifi_spi_read_buffer`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** PC启动TCP server，板连接后收测试串，再发文本看回显。

**限制：** 网络读缓冲不保证天然是C字符串，二进制/满缓冲需按长度处理；无线仅调试，正式发车携带无效。

**迁入项目：** 保留实际传输API，改成非阻塞连接/断线处理与长度安全协议；正式赛移除模块。

<a id="e6_08"></a>

## E6_08 — SPI Wi-Fi 四通道示波器

原目录：`E6_08_wifi_spi_oscilloscope_demo`。用途：验证对应网络连接方式与应用数据。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_08_wifi_spi_oscilloscope_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_08_wifi_spi_oscilloscope_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_08_wifi_spi_oscilloscope_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_08_wifi_spi_oscilloscope_demo/iar/rt1064.eww)。

**接线与配置：** main中的SSID/密码需替换；UART类MCU TX D16/RX D17、RTS D26/RST D27；SPI类SCK D12、MOSI D14、MISO D15、CS D13、INT B17/RST B16。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define WIFI_SSID_TEST "SEEKFREE"
#define WIFI_PASSWORD_TEST "12345678"
```

接线/API依据：[zf_device_wifi_spi.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_spi.h) · [zf_device_wifi_spi.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_spi.c)。

**初始化、主循环与中断：** TCP连接后assistant_interface WIFI_SPI；每20ms四通道各增0.1/0.5/1/2，channel_num=4并oscilloscope_send，解析调参数据。

关键API：`wifi_spi_init`、`wifi_spi_socket_connect`、`seekfree_assistant_interface_init`、`seekfree_assistant_oscilloscope_send`、`seekfree_assistant_data_analysis`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 逐飞助手选择网络示波器，看到四条斜率不同的曲线。

**限制：** 发送的是人工增长数据，不是传感器；调参输入不能直接作用到未限幅驱动；无线仅调试，正式发车携带无效。

**迁入项目：** 保留实际传输API，改成非阻塞连接/断线处理与长度安全协议；正式赛移除模块。

<a id="e6_09"></a>

## E6_09 — SPI Wi-Fi MT9V03X 图传

原目录：`E6_09_wifi_spi_mt9v03x_demo`。用途：把188×120灰度图像发到逐飞助手。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_09_wifi_spi_mt9v03x_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_09_wifi_spi_mt9v03x_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_09_wifi_spi_mt9v03x_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_09_wifi_spi_mt9v03x_demo/iar/rt1064.eww)。

**接线与配置：** Wi-Fi SPI1 D12/D14/D15/CS D13、INT B17/RST B16；相机CSI接线见摄像头通用表。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define INCLUDE_BOUNDARY_TYPE 0
#define WIFI_SSID_TEST "SEEKFREE"
#define WIFI_PASSWORD_TEST "12345678"
#define BOUNDARY_NUM (MT9V03X_H * 3 / 2)
```

接线/API依据：[zf_device_wifi_spi.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_spi.h) · [zf_device_wifi_spi.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_spi.c) · [zf_device_mt9v03x.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_mt9v03x.h) · [zf_device_mt9v03x.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_mt9v03x.c)。

**初始化、主循环与中断：** 先建立Wi-Fi TCP，初始化相机和assistant WIFI_SPI；INCLUDE_BOUNDARY_TYPE默认0仅图像；每新帧清finish_flag、复制到image_copy、camera_send。

关键API：`elif`、`wifi_spi_init`、`wifi_spi_socket_connect`、`mt9v03x_init`、`seekfree_assistant_interface_init`、`seekfree_assistant_camera_information_config`、`seekfree_assistant_camera_boundary_config`、`seekfree_assistant_camera_send`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 助手选择匹配相机格式，应收到实时图；再分别试边界模式1~4核对X/Y/XY/仅边界。

**限制：** 人工边界不是识别结果；无线仅调试、正式携带无效；复制帧会增加RAM和吞吐。

**迁入项目：** 现车MT9V034采用MT9V03X链路，优先先用有线E8_08；保留帧复制以避免处理被DMA覆盖。

<a id="e6_10"></a>

## E6_10 — SPI Wi-Fi OV7725 图传

原目录：`E6_10_wifi_spi_ov7725_demo`。用途：把160×120、每行W/8字节的压缩二值图像发到逐飞助手。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_10_wifi_spi_ov7725_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_10_wifi_spi_ov7725_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_10_wifi_spi_ov7725_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_10_wifi_spi_ov7725_demo/iar/rt1064.eww)。

**接线与配置：** Wi-Fi SPI1 D12/D14/D15/CS D13、INT B17/RST B16；相机CSI接线见摄像头通用表。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define INCLUDE_BOUNDARY_TYPE 0
#define WIFI_SSID_TEST "SEEKFREE"
#define WIFI_PASSWORD_TEST "12345678"
#define BOUNDARY_NUM (OV7725_H * 3 / 2)
```

接线/API依据：[zf_device_wifi_spi.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_spi.h) · [zf_device_wifi_spi.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_spi.c) · [zf_device_ov7725.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_ov7725.h) · [zf_device_ov7725.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_ov7725.c)。

**初始化、主循环与中断：** 先建立Wi-Fi TCP，初始化相机和assistant WIFI_SPI；INCLUDE_BOUNDARY_TYPE默认0仅图像；每新帧清finish_flag、复制到image_copy、camera_send。

关键API：`elif`、`wifi_spi_init`、`wifi_spi_socket_connect`、`ov7725_init`、`seekfree_assistant_interface_init`、`seekfree_assistant_camera_information_config`、`seekfree_assistant_camera_boundary_config`、`seekfree_assistant_camera_send`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 助手选择匹配相机格式，应收到实时图；再分别试边界模式1~4核对X/Y/XY/仅边界。

**限制：** 人工边界不是识别结果；无线仅调试、正式携带无效；复制帧会增加RAM和吞吐。

**迁入项目：** 现车MT9V034采用MT9V03X链路，优先先用有线E8_08；保留帧复制以避免处理被DMA覆盖。

<a id="e6_11"></a>

## E6_11 — SPI Wi-Fi SCC8660 图传

原目录：`E6_11_wifi_spi_scc8660_demo`。用途：把160×120 RGB565图像发到逐飞助手。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_11_wifi_spi_scc8660_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_11_wifi_spi_scc8660_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_11_wifi_spi_scc8660_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_11_wifi_spi_scc8660_demo/iar/rt1064.eww)。

**接线与配置：** Wi-Fi SPI1 D12/D14/D15/CS D13、INT B17/RST B16；相机CSI接线见摄像头通用表。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define INCLUDE_BOUNDARY_TYPE 0
#define WIFI_SSID_TEST "SEEKFREE"
#define WIFI_PASSWORD_TEST "12345678"
#define BOUNDARY_NUM (SCC8660_H * 3 / 2)
```

接线/API依据：[zf_device_wifi_spi.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_spi.h) · [zf_device_wifi_spi.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_wifi_spi.c) · [zf_device_scc8660.h](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_scc8660.h) · [zf_device_scc8660.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/libraries/zf_device/zf_device_scc8660.c)。

**初始化、主循环与中断：** 先建立Wi-Fi TCP，初始化相机和assistant WIFI_SPI；INCLUDE_BOUNDARY_TYPE默认0仅图像；每新帧清finish_flag、复制到image_copy、camera_send。

关键API：`elif`、`wifi_spi_init`、`wifi_spi_socket_connect`、`scc8660_init`、`seekfree_assistant_interface_init`、`seekfree_assistant_camera_information_config`、`seekfree_assistant_camera_boundary_config`、`seekfree_assistant_camera_send`。

中断入口：CSI IRQ分发采集，UART5 IRQ分发相机配置；若用UART8无线则同时保留无线分发。

**测试与预期：** 助手选择匹配相机格式，应收到实时图；再分别试边界模式1~4核对X/Y/XY/仅边界。

**限制：** 人工边界不是识别结果；无线仅调试、正式携带无效；复制帧会增加RAM和吞吐。

**迁入项目：** 现车MT9V034采用MT9V03X链路，优先先用有线E8_08；保留帧复制以避免处理被DMA覆盖。

