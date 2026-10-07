# 逐飞 RT1064 84 个真实例程使用索引

本索引按当前 `SeekFree/RT1064_Library/Example` 源文件逐项核对，库版本 V3.11.2：核心板17、主板3、编码器2、电机6、IMU5、显示17、无线11、GNSS/测距5、摄像头18，共84。目录编号以实际源码为准；旧Excel索引和2023说明书的编号/数量不作为当前事实。另附 [空模板](#template) 和 [镜头组综合参考包](camera-reference.md)，二者不计入84。

现车配置：C车模、MIMXRT1064DVL6B逐飞核心板、逐飞RT1064 V3.0主板、MT9V034总钻风、DRV8701、逐飞1024线增量编码器、普通IPS200 SPI和Wi-Fi SPI。摄像头显示与无线图传先看 [当前指南](../seekfree-assistant-debugging.md)；其他基础例程可按 [E8_08有线图传参考](camera.md#e8_08) → [E2_01正交编码器](encoder.md#e2_01) → [E3_04 DIR/PWM](motor.md#e3_04) → [E3_06舵机](motor.md#e3_06) 分别验证。1024线不等于最终计数/轮转，先核实A/B输出、倍频、减速比、方向。主板版本及端口连通性必须依据实物确认。

<a id="prepare"></a>

## 统一准备与迁入流程

1. 断电核对模块型号、供电、共地和信号针脚。示例的宏代表默认主板接线，使用不同主板版本时必须查相应原理图和丝印；不要按目录名或旧注释接线。
2. 打开本条目的 `mdk/rt1064.uvprojx` 或 `iar/rt1064.eww`；单独测试每个例程，不把多个main/同名IRQ一起加入。工程通过同分类上一级 `libraries` 引用驱动，不能只搬main而遗漏工程依赖。
3. 查看工程选定器件、链接文件、启动文件、Flash算法和库路径。Keil模板记录AC6.19；本机2026-09-30重装为MDK5.39/AC6.21，原始空模板和当前车辆工程副本均原样编译通过，见 [当前环境](../environment-and-build.md)。这不表示84个原例都已编译，也没有上板验证其测试预期。
4. 普通串口输出默认 `debug_init`：UART1，115200，MCU TX B12/RX B13，8位数据、无校验；USB串口RX接B12、TX接B13并共地。源码中旧A9/A10注释不能覆盖当前debug头文件。
5. 下载前按每项限制判断是否允许运行。E16涉及OCOTP/启动配置，本次仅审阅；E08自动擦写Flash，E12可格式化/覆盖SD文件，theme2含参数保存，不得自动运行。电机/舵机例先架空或解除连杆，并降低输出进行首测。
6. 按每项测试记录连通性、方向、有效标志、数值单位与失效行为。传感器能初始化不代表校准完成，日志打印周期不一定等于采样周期。
7. 迁入时保留车辆唯一main，把初始化放板级模块、采样放固定周期、图像复制放帧任务；IRQ按通道/引脚逐项合并，保留清标志和SDK分发。不要复制整套例程工程覆盖车辆工程。
8. 统一引脚和定时器资源：共享PWM模块频率、SPI总线片选、UART接收者、GPT/PIT使用者及DMA内存段需逐项核对。当前PWM标度 `PWM_DUTY_MAX=10000`，百分数乘100，而不是默认1000。

正式发车按本项目赛项规则移除无线模块；GPS不用于室内惯导组正式赛；IMU模块须不含MCU，是否符合必须核查实物。当前车辆采用IPS200 SPI和Wi-Fi SPI作调试，工程不依赖TFT180或IPS200 Pro。

## 84 项目录

| 分类 | 数量 | 使用页 |
|---|---:|---|
| 核心板基础例程 | 17 | [coreboard.md](coreboard.md) |
| 主板输入与检测 | 3 | [motherboard.md](motherboard.md) |
| 编码器 | 2 | [encoder.md](encoder.md) |
| 电机与舵机 | 6 | [motor.md](motor.md) |
| IMU | 5 | [imu.md](imu.md) |
| 显示器 | 17 | [display.md](display.md) |
| 无线通信 | 11 | [wireless.md](wireless.md) |
| GNSS 与测距 | 5 | [gnss-range.md](gnss-range.md) |
| 摄像头采集与传输 | 18 | [camera.md](camera.md) |

### 核心板基础例程

| 真实目录 | 使用条目 | 原始入口 |
|---|---|---|
| `E01_gpio_demo` | [GPIO 输入输出](coreboard.md#e01) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E01_gpio_demo/user/src/main.c) |
| `E02_uart_demo` | [UART 中断与 FIFO 回显](coreboard.md#e02) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E02_uart_demo/user/src/main.c) |
| `E03_adc_demo` | [ADC 精度与均值滤波](coreboard.md#e03) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E03_adc_demo/user/src/main.c) |
| `E04_pwm_demo` | [四路 PWM 扫占空比](coreboard.md#e04) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E04_pwm_demo/user/src/main.c) |
| `E05_pit_demo` | [PIT 周期中断](coreboard.md#e05) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E05_pit_demo/user/src/main.c) |
| `E06_exti_demo` | [EXTI 边沿触发](coreboard.md#e06) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E06_exti_demo/user/src/main.c) |
| `E07_encoder_demo` | [正交与脉冲方向编码器对照](coreboard.md#e07) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E07_encoder_demo/user/src/main.c) |
| `E08_flash_demo` | [Flash 联合缓冲区读写](coreboard.md#e08) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E08_flash_demo/user/src/main.c) |
| `E09_timer_demo` | [GPT 计时单位](coreboard.md#e09) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E09_timer_demo/user/src/main.c) |
| `E10_printf_debug_log_demo` | [printf、日志与断言](coreboard.md#e10) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E10_printf_debug_log_demo/user/src/main.c) |
| `E11_interrupt_priority_set_demo` | [PIT 中断优先级设置](coreboard.md#e11) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E11_interrupt_priority_set_demo/user/src/main.c) |
| `E12_fatfs_demo` | [SD 卡 FatFs](coreboard.md#e12) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E12_fatfs_demo/user/src/main.c) |
| `E13_cache_demo` | [数据缓存控制](coreboard.md#e13) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E13_cache_demo/user/src/main.c) |
| `E14_code_running_in_itcm_demo` | [ITCM 代码段](coreboard.md#e14) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E14_code_running_in_itcm_demo/user/src/main.c) |
| `E15_specify_variable_position_demo` | [变量内存段与对齐](coreboard.md#e15) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E15_specify_variable_position_demo/user/src/main.c) |
| `E16_burn_fuse_demo` | [OCOTP 配置示例（仅审阅）](coreboard.md#e16) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E16_burn_fuse_demo/user/src/main.c) |
| `E17_usb_cdc_demo` | [USB CDC 输出](coreboard.md#e17) | [main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E17_usb_cdc_demo/user/src/main.c) |

### 主板输入与检测

| 真实目录 | 使用条目 | 原始入口 |
|---|---|---|
| `E1_01_button_switch_buzzer_demo` | [按键、拨码、LED 与蜂鸣器](motherboard.md#e1_01) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_01_button_switch_buzzer_demo/user/src/main.c) |
| `E1_02_hall_stopline_detection_demo` | [霍尔停止线检测](motherboard.md#e1_02) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_02_hall_stopline_detection_demo/user/src/main.c) |
| `E1_03_opm4a_demo` | [OPM4A 四通道电感采样](motherboard.md#e1_03) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E1_motherboard/E1_03_opm4a_demo/user/src/main.c) |

### 编码器

| 真实目录 | 使用条目 | 原始入口 |
|---|---|---|
| `E2_01_encoder_quadrature_demo` | [四路A/B 正交编码器](encoder.md#e2_01) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E2_encoder/E2_01_encoder_quadrature_demo/user/src/main.c) |
| `E2_02_encoder_dir_demo` | [四路脉冲/方向编码器](encoder.md#e2_02) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E2_encoder/E2_02_encoder_dir_demo/user/src/main.c) |

### 电机与舵机

| 真实目录 | 使用条目 | 原始入口 |
|---|---|---|
| `E3_01_hip4082_single_motor_contro_demo` | [HIP4082 单电机双 PWM](motor.md#e3_01) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_01_hip4082_single_motor_contro_demo/user/src/main.c) |
| `E3_02_hip4082_double_motor_contro_demo` | [HIP4082 多路双 PWM](motor.md#e3_02) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_02_hip4082_double_motor_contro_demo/user/src/main.c) |
| `E3_03_drv8701e_single_motor_contro_demo` | [DRV8701E 单路 DIR/PWM](motor.md#e3_03) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_03_drv8701e_single_motor_contro_demo/user/src/main.c) |
| `E3_04_drv8701e_double_motor_contro_demo` | [DRV8701E 多路 DIR/PWM](motor.md#e3_04) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_04_drv8701e_double_motor_contro_demo/user/src/main.c) |
| `E3_05_bldc_contro_demo_360c_spin27` | [360C SPIN27 无刷驱动与反馈](motor.md#e3_05) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_05_bldc_contro_demo_360c_spin27/user/src/main.c) |
| `E3_06_servo_control_demo` | [舵机角度转 PWM](motor.md#e3_06) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E3_motor/E3_06_servo_control_demo/user/src/main.c) |

### IMU

| 真实目录 | 使用条目 | 原始入口 |
|---|---|---|
| `E4_01_icm20602_demo` | [ICM20602 原始数据](imu.md#e4_01) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_01_icm20602_demo/user/src/main.c) |
| `E4_02_imu660ra_demo` | [IMU660RA 原始数据](imu.md#e4_02) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_02_imu660ra_demo/user/src/main.c) |
| `E4_03_imu660rb_demo` | [IMU660RB 原始数据](imu.md#e4_03) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_03_imu660rb_demo/user/src/main.c) |
| `E4_04_imu660rc_demo` | [IMU660RC 四元数120Hz模式](imu.md#e4_04) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_04_imu660rc_demo/user/src/main.c) |
| `E4_05_imu963ra_demo` | [IMU963RA 原始数据](imu.md#e4_05) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E4_imu/E4_05_imu963ra_demo/user/src/main.c) |

### 显示器

| 真实目录 | 使用条目 | 原始入口 |
|---|---|---|
| `E5_01_oled_display_demo` | [OLED 基础显示](display.md#e5_01) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_01_oled_display_demo/user/src/main.c) |
| `E5_02_tft180_display_demo` | [TFT180 基础显示](display.md#e5_02) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_02_tft180_display_demo/user/src/main.c) |
| `E5_03_ips114_display_demo` | [IPS114 基础显示](display.md#e5_03) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_03_ips114_display_demo/user/src/main.c) |
| `E5_04_ips200_display_demo` | [IPS200 基础显示](display.md#e5_04) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_04_ips200_display_demo/user/src/main.c) |
| `E5_05_ips200pro_page` | [IPS200 Pro 页面](display.md#e5_05) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_05_ips200pro_page/user/src/main.c) |
| `E5_06_ips200pro_label` | [IPS200 Pro 文本标签](display.md#e5_06) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_06_ips200pro_label/user/src/main.c) |
| `E5_07_ips200pro_table` | [IPS200 Pro 表格](display.md#e5_07) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_07_ips200pro_table/user/src/main.c) |
| `E5_08_ips200pro_meter` | [IPS200 Pro 仪表](display.md#e5_08) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_08_ips200pro_meter/user/src/main.c) |
| `E5_09_ips200pro_clock` | [IPS200 Pro 时钟](display.md#e5_09) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_09_ips200pro_clock/user/src/main.c) |
| `E5_10_ips200pro_progress_bar` | [IPS200 Pro 进度条](display.md#e5_10) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_10_ips200pro_progress_bar/user/src/main.c) |
| `E5_11_ips200pro_calendar` | [IPS200 Pro 日历](display.md#e5_11) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_11_ips200pro_calendar/user/src/main.c) |
| `E5_12_ips200pro_waveform` | [IPS200 Pro 波形](display.md#e5_12) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_12_ips200pro_waveform/user/src/main.c) |
| `E5_13_ips200pro_camera_mt9v03x` | [IPS200 Pro 灰度图像控件](display.md#e5_13) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_13_ips200pro_camera_mt9v03x/user/src/main.c) |
| `E5_14_ips200pro_camera_scc8660` | [IPS200 Pro 彩色图像控件](display.md#e5_14) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_14_ips200pro_camera_scc8660/user/src/main.c) |
| `E5_15_ips200pro_container` | [IPS200 Pro 容器](display.md#e5_15) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_15_ips200pro_container/user/src/main.c) |
| `E5_16_ips200pro_theme1` | [IPS200 Pro 综合菜单主题1](display.md#e5_16) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_16_ips200pro_theme1/user/src/main.c) |
| `E5_17_ips200pro_theme2` | [IPS200 Pro 综合菜单主题2](display.md#e5_17) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E5_display/E5_17_ips200pro_theme2/user/src/main.c) |

### 无线通信

| 真实目录 | 使用条目 | 原始入口 |
|---|---|---|
| `E6_01_wireless_uart_demo` | [无线 UART 回显](wireless.md#e6_01) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_01_wireless_uart_demo/user/src/main.c) |
| `E6_02_bluetooth_ch9141_demo` | [CH9141 蓝牙回显](wireless.md#e6_02) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_02_bluetooth_ch9141_demo/user/src/main.c) |
| `E6_03_wifi_uart_udp_demo` | [UART Wi-Fi UDP](wireless.md#e6_03) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_03_wifi_uart_udp_demo/user/src/main.c) |
| `E6_04_wifi_uart_tcp_client_demo` | [UART Wi-Fi TCP 客户端](wireless.md#e6_04) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_04_wifi_uart_tcp_client_demo/user/src/main.c) |
| `E6_05_wifi_uart_tcp_server_demo` | [UART Wi-Fi TCP 服务端](wireless.md#e6_05) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_05_wifi_uart_tcp_server_demo/user/src/main.c) |
| `E6_06_wifi_spi_udp_demo` | [SPI Wi-Fi UDP](wireless.md#e6_06) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_06_wifi_spi_udp_demo/user/src/main.c) |
| `E6_07_wifi_spi_tcp_client_demo` | [SPI Wi-Fi TCP 客户端](wireless.md#e6_07) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_07_wifi_spi_tcp_client_demo/user/src/main.c) |
| `E6_08_wifi_spi_oscilloscope_demo` | [SPI Wi-Fi 四通道示波器](wireless.md#e6_08) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_08_wifi_spi_oscilloscope_demo/user/src/main.c) |
| `E6_09_wifi_spi_mt9v03x_demo` | [SPI Wi-Fi MT9V03X 图传](wireless.md#e6_09) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_09_wifi_spi_mt9v03x_demo/user/src/main.c) |
| `E6_10_wifi_spi_ov7725_demo` | [SPI Wi-Fi OV7725 图传](wireless.md#e6_10) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_10_wifi_spi_ov7725_demo/user/src/main.c) |
| `E6_11_wifi_spi_scc8660_demo` | [SPI Wi-Fi SCC8660 图传](wireless.md#e6_11) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E6_wireless/E6_11_wifi_spi_scc8660_demo/user/src/main.c) |

### GNSS 与测距

| 真实目录 | 使用条目 | 原始入口 |
|---|---|---|
| `E7_01_split_ultrasonic_module_demo` | [分体超声波串口测距](gnss-range.md#e7_01) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_01_split_ultrasonic_module_demo/user/src/main.c) |
| `E7_02_gps_tau1201_pc_mono_demo` | [GNSS 串口信息](gnss-range.md#e7_02) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_02_gps_tau1201_pc_mono_demo/user/src/main.c) |
| `E7_03_gps_tau1201_tft180_display_demo` | [GNSS TFT180 显示](gnss-range.md#e7_03) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_03_gps_tau1201_tft180_display_demo/user/src/main.c) |
| `E7_04_gps_tau1201_ips114_display_demo` | [GNSS IPS114 显示](gnss-range.md#e7_04) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_04_gps_tau1201_ips114_display_demo/user/src/main.c) |
| `E7_05_gps_tau1201_ips200_display_demo` | [GNSS IPS200 显示](gnss-range.md#e7_05) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E7_gnss_range/E7_05_gps_tau1201_ips200_display_demo/user/src/main.c) |

### 摄像头采集与传输

| 真实目录 | 使用条目 | 原始入口 |
|---|---|---|
| `E8_01_ov7725_seekfree_assistant_demo` | [OV7725 → debug UART1 逐飞助手](camera.md#e8_01) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_01_ov7725_seekfree_assistant_demo/user/src/main.c) |
| `E8_02_ov7725_wireless_uart_seekfree_assistant_demo` | [OV7725 → 无线UART 逐飞助手](camera.md#e8_02) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_02_ov7725_wireless_uart_seekfree_assistant_demo/user/src/main.c) |
| `E8_03_ov7725_bluetooth_ch9141_seekfree_assistant_demo` | [OV7725 → 蓝牙CH9141 逐飞助手](camera.md#e8_03) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_03_ov7725_bluetooth_ch9141_seekfree_assistant_demo/user/src/main.c) |
| `E8_04_ov7725_oled_display_demo` | [OV7725 → OLED](camera.md#e8_04) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_04_ov7725_oled_display_demo/user/src/main.c) |
| `E8_05_ov7725_tft180_display_demo` | [OV7725 → TFT180](camera.md#e8_05) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_05_ov7725_tft180_display_demo/user/src/main.c) |
| `E8_06_ov7725_ips114_display_demo` | [OV7725 → IPS114](camera.md#e8_06) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_06_ov7725_ips114_display_demo/user/src/main.c) |
| `E8_07_ov7725_ips200_display_demo` | [OV7725 → IPS200](camera.md#e8_07) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_07_ov7725_ips200_display_demo/user/src/main.c) |
| `E8_08_mt9v03x_seekfree_assistant_demo` | [MT9V03X → debug UART1 逐飞助手](camera.md#e8_08) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_08_mt9v03x_seekfree_assistant_demo/user/src/main.c) |
| `E8_09_mt9v03x_wireless_uart_seekfree_assistant_demo` | [MT9V03X → 无线UART 逐飞助手](camera.md#e8_09) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_09_mt9v03x_wireless_uart_seekfree_assistant_demo/user/src/main.c) |
| `E8_10_mt9v03x_bluetooth_ch9141_seekfree_assistant_demo` | [MT9V03X → 蓝牙CH9141 逐飞助手](camera.md#e8_10) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_10_mt9v03x_bluetooth_ch9141_seekfree_assistant_demo/user/src/main.c) |
| `E8_11_mt9v03x_oled_display_demo` | [MT9V03X → OLED](camera.md#e8_11) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_11_mt9v03x_oled_display_demo/user/src/main.c) |
| `E8_12_mt9v03x_tft180_display_demo` | [MT9V03X → TFT180](camera.md#e8_12) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_12_mt9v03x_tft180_display_demo/user/src/main.c) |
| `E8_13_mt9v03x_ips114_display_demo` | [MT9V03X → IPS114](camera.md#e8_13) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_13_mt9v03x_ips114_display_demo/user/src/main.c) |
| `E8_14_mt9v03x_ips200_display_demo` | [MT9V03X → IPS200](camera.md#e8_14) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_14_mt9v03x_ips200_display_demo/user/src/main.c) |
| `E8_15_scc8660_usbcdc_seekfree_assistant_demo` | [SCC8660 → USB CDC 逐飞助手](camera.md#e8_15) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_15_scc8660_usbcdc_seekfree_assistant_demo/user/src/main.c) |
| `E8_16_scc8660_tft180_display_demo` | [SCC8660 → TFT180](camera.md#e8_16) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_16_scc8660_tft180_display_demo/user/src/main.c) |
| `E8_17_scc8660_ips114_display_demo` | [SCC8660 → IPS114](camera.md#e8_17) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_17_scc8660_ips114_display_demo/user/src/main.c) |
| `E8_18_scc8660_ips200_display_demo` | [SCC8660 → IPS200](camera.md#e8_18) | [main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E8_camera/E8_18_scc8660_ips200_display_demo/user/src/main.c) |

<a id="template"></a>

## 空模板（不计入84例程）

入口：[空模板main.c](../../SeekFree/RT1064_Library/SeekFree_RT1064_Opensource_Library/project/user/src/main.c) · [空模板isr.c](../../SeekFree/RT1064_Library/SeekFree_RT1064_Opensource_Library/project/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/SeekFree_RT1064_Opensource_Library/project/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/SeekFree_RT1064_Opensource_Library/project/iar/rt1064.eww)。

空模板用于构建新的单入口工程；先保留时钟/调试和启动链接布局，再按已验证模块逐项迁入。中断模板里的相机/GNSS/无线等通用分发函数不代表该设备已初始化。库文件在模板自身libraries中；84例程每分类自带一份libraries，当前同名zf文件哈希一致，迁入应统一保留一套并追踪版本。

诊断前查阅 [已知问题](../known-issues.md)；镜头组旧参考包的图像与PID问题见 [综合参考说明](camera-reference.md)。

车辆实际构建、器件/编译器与功能开关记录见 [环境与构建](../environment-and-build.md)，实物接线核对见 [硬件与引脚](../hardware-and-pins.md)。车辆工程的测试结果不代表84个原例均已构建或已上板。
