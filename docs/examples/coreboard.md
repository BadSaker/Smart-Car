# 核心板基础例程

本页以 V3.11.2 的真实 `main.c`、`isr.c` 及本分类 `libraries` 为准。每项均提供原工程入口；所有测试步骤是待上板执行的操作与预期，本次没有编译或运行这84个原例。主板版本未知时，以所用板的原理图、丝印和实际连通性核对引脚。

先阅读 [统一准备与迁入流程](README.md#prepare)。源码中的 `clock_init(SYSTEM_CLOCK_600M)` 保留；调用 `debug_init` 的例程默认 UART1、115200、MCU TX B12/RX B13。原例 `main.c` 不能与车辆工程的 `main` 同时加入，`isr.c` 也必须逐IRQ合并。

<a id="e01"></a>

## E01 — GPIO 输入输出

原目录：`E01_gpio_demo`。用途：理解推挽输出、上拉输入及读写电平。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E01_gpio_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E01_gpio_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E01_gpio_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E01_gpio_demo/iar/rt1064.eww)。

**接线与配置：** B9 输出；C4、D1 上拉输入。

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/zf_driver/zf_driver_gpio.h)。

**初始化、主循环与中断：** B9 依次置高、置低、翻转，每步等待100ms；读取 C4 到 gpio_status。D1 已初始化但主循环未读取。

关键API：`gpio_init`、`gpio_set_level`、`gpio_toggle_level`、`gpio_get_level`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 用示波器测 B9，调试器看 gpio_status；将 C4 从上拉高拉到地，变量应从1变0。

**限制：** B9 为板载 LED；D1 后续可能与 IMU660RC INT2 或 PWM 冲突。

**迁入项目：** 把 gpio_init 放入板级初始化，将输入读取和输出动作拆成非阻塞函数。

<a id="e02"></a>

## E02 — UART 中断与 FIFO 回显

原目录：`E02_uart_demo`。用途：验证 UART1 接收中断到64字节 FIFO 的传递。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E02_uart_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E02_uart_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E02_uart_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E02_uart_demo/iar/rt1064.eww)。

**接线与配置：** UART1 MCU TX=B12、RX=B13；接USB串口 RX/TX，共地。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define UART_INDEX (DEBUG_UART_INDEX )
#define UART_BAUDRATE (DEBUG_UART_BAUDRATE)
#define UART_TX_PIN (DEBUG_UART_TX_PIN )
#define UART_RX_PIN (DEBUG_UART_RX_PIN )
#define UART_PRIORITY (LPUART1_IRQn)
```

接线/API依据：[zf_driver_uart.h](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/zf_driver/zf_driver_uart.h)。

**初始化、主循环与中断：** fifo_init 后 uart_init、uart_rx_interrupt；LPUART1 中调用 uart_rx_interrupt_handler，uart_query_byte 读一字节并入 FIFO；主循环每10ms取出并回显。

关键API：`fifo_init`、`uart_init`、`uart_rx_interrupt`、`interrupt_set_priority`、`uart_write_string`、`uart_write_byte`、`fifo_used`、`fifo_read_buffer`、`uart_write_buffer`、`uart_rx_interrupt_handler`、`uart_query_byte`、`fifo_write_buffer`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 串口终端115200收到 UART Text.；发送 abc，应收到 UART get data:abc。

**限制：** 宏指向当前 debug 配置；旧注释的 A9/A10 无效。FIFO 仅64字节，突发数据需溢出处理。

**迁入项目：** 合并既有 LPUART1 ISR 分发及 FIFO；不要同时让 debug 与自定义回调抢读同一 UART。

<a id="e03"></a>

## E03 — ADC 精度与均值滤波

原目录：`E03_adc_demo`。用途：比较四路ADC不同分辨率和10次均值结果。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E03_adc_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E03_adc_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E03_adc_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E03_adc_demo/iar/rt1064.eww)。

**接线与配置：** B14/B15/B21/B23 分别 ADC1_CH3/4/10/12。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define CHANNEL_NUMBER (4)
#define ADC_CHANNEL1 (ADC1_CH3_B14)
#define ADC_CHANNEL2 (ADC1_CH4_B15)
#define ADC_CHANNEL3 (ADC1_CH10_B21)
#define ADC_CHANNEL4 (ADC1_CH12_B23)
```

接线/API依据：[zf_driver_adc.h](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/zf_driver/zf_driver_adc.h)。

**初始化、主循环与中断：** 通道1/2为12位，3为10位，4为8位；每秒交替打印原始与10次均值。

关键API：`adc_init`、`adc_convert`、`adc_mean_filter_convert`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 接稳定的合法模拟电压到四路输入，串口原始与均值接近；满量程分别为4095、4095、1023、255。

**限制：** 不能把不同位宽读数直接比较；外部模拟电压必须符合主板 ADC 输入范围。

**迁入项目：** 统一正式采样的分辨率，并在固定周期采样，按前端分压标定电压。

<a id="e04"></a>

## E04 — 四路 PWM 扫占空比

原目录：`E04_pwm_demo`。用途：验证17kHz PWM 输出与占空比标度。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E04_pwm_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E04_pwm_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E04_pwm_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E04_pwm_demo/iar/rt1064.eww)。

**接线与配置：** D0/D1=PWM1_MODULE3 A/B；D2/D3=PWM2_MODULE3 A/B。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define CHANNEL_NUMBER (4)
#define PWM_CH1 (PWM1_MODULE3_CHA_D0)
#define PWM_CH2 (PWM1_MODULE3_CHB_D1)
#define PWM_CH3 (PWM2_MODULE3_CHA_D2)
#define PWM_CH4 (PWM2_MODULE3_CHB_D3)
```

接线/API依据：[zf_driver_pwm.h](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/zf_driver/zf_driver_pwm.h)。

**初始化、主循环与中断：** 四路初始化0；逐通道将 duty 从0扫到 PWM_DUTY_MAX/2 再降至0，每步100us。

关键API：`pwm_init`、`pwm_set_duty`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 不接电机时示波器看 D0~D3，应测得17kHz、0~50%周期扫动。

**限制：** PWM_DUTY_MAX 当前为10000，50%为5000；同一模块频率共享，不能独立改频率。

**迁入项目：** 只迁入 pwm_init/pwm_set_duty，替换扫描循环为经过限幅的控制输出。

<a id="e05"></a>

## E05 — PIT 周期中断

原目录：`E05_pit_demo`。用途：用中断置标志、主循环消费标志演示周期任务。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E05_pit_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E05_pit_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E05_pit_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E05_pit_demo/iar/rt1064.eww)。

**接线与配置：** LED B9；无外接传感器。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define PIT_CH (PIT_CH0 )
#define PIT_PRIORITY (PIT_IRQn)
#define LED1 (B9)
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/zf_driver/zf_driver_gpio.h)。

**初始化、主循环与中断：** PIT_CH0 周期1000ms、priority0；ISR 清标志后 pit_handler 设置 pit_state；主循环翻转 B9 并清状态。

关键API：`gpio_init`、`pit_ms_init`、`interrupt_set_priority`、`gpio_toggle_level`、`pit_handler`。

中断入口：`PIT_IRQHandler` 清相应通道标志并调用本例处理函数；通道号改变时同步修改ISR。

**测试与预期：** B9 每秒翻转一次；断点观察 pit_handler 每秒进入。

**限制：** 单一状态位不能累计遗漏次数，长阻塞会丢任务。

**迁入项目：** 以计数或时间戳管理调度；合并 PIT_IRQHandler 内通道分发。

<a id="e06"></a>

## E06 — EXTI 边沿触发

原目录：`E06_exti_demo`。用途：比较上升、下降和双边沿触发。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E06_exti_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E06_exti_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E06_exti_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E06_exti_demo/iar/rt1064.eww)。

**接线与配置：** C15/C14/C13/C12 为 KEY1~4，B9 LED。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define LED1 (B9 )
#define KEY1 (C15)
#define KEY2 (C14)
#define KEY3 (C13)
#define KEY4 (C12)
#define KEY_EXTI (GPIO2_Combined_16_31_IRQn)
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/zf_driver/zf_driver_gpio.h) · [zf_driver_exti.h](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/zf_driver/zf_driver_exti.h)。

**初始化、主循环与中断：** C15上升、C14下降、C13/C12双边沿；共享 GPIO2_Combined_16_31_IRQn；回调判断和清各引脚标志，主循环按键序号闪1~4次。

关键API：`gpio_init`、`exti_init`、`interrupt_set_priority`、`gpio_toggle_level`、`key1_exti_handler`、`exti_flag_get`、`exti_flag_clear`、`key2_exti_handler`、`key3_exti_handler`、`key4_exti_handler`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 按/松四键，观察 B9 对应1/2/3/4次闪烁；双边沿可在按下和释放均触发。

**限制：** 主循环闪灯阻塞，机械键未专门消抖；共享 IRQ 必须逐引脚分发。

**迁入项目：** 只复用 EXTI 配置和标志清除，事件进入状态机处理。

<a id="e07"></a>

## E07 — 正交与脉冲方向编码器对照

原目录：`E07_encoder_demo`。用途：同时验证两种编码器协议。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E07_encoder_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E07_encoder_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E07_encoder_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E07_encoder_demo/iar/rt1064.eww)。

**接线与配置：** 正交 C0/C1；脉冲/方向 C3/C25。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define PIT_CH (PIT_CH0 )
#define PIT_PRIORITY (PIT_IRQn)
#define ENCODER_QUADDEC (QTIMER1_ENCODER1)
#define ENCODER_QUADDEC_A (QTIMER1_ENCODER1_CH1_C0)
#define ENCODER_QUADDEC_B (QTIMER1_ENCODER1_CH2_C1)
#define ENCODER_DIR (QTIMER2_ENCODER1)
#define ENCODER_DIR_PULSE (QTIMER2_ENCODER1_CH1_C3)
#define ENCODER_DIR_DIR (QTIMER2_ENCODER1_CH2_C25)
```

**初始化、主循环与中断：** QTIMER1_ENCODER1 用 quad；QTIMER2_ENCODER1 用 dir；PIT0每100ms读取并清零，主循环每500ms打印最近窗口值。

关键API：`encoder_quad_init`、`encoder_dir_init`、`pit_ms_init`、`interrupt_set_priority`、`pit_handler`、`encoder_get_count`、`encoder_clear_count`。

中断入口：`PIT_IRQHandler` 清相应通道标志并调用本例处理函数；通道号改变时同步修改ISR。

**测试与预期：** 分别输入 A/B 或 pulse/dir，正反转计数符号应改变，停止后窗口值回0。

**限制：** 打印不是500ms累计；输入模式不能仅按线数推定。

**迁入项目：** 选择实物输出模式，统一符号，并把窗口时间写入速度单位。

<a id="e08"></a>

## E08 — Flash 联合缓冲区读写

原目录：`E08_flash_demo`。用途：学习Flash页读写与不同类型联合体解释。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E08_flash_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E08_flash_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E08_flash_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E08_flash_demo/iar/rt1064.eww)。

**接线与配置：** 内部Flash；FLASH_SECTION_INDEX=127、FLASH_PAGE_INDEX=FLASH_PAGE_3。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define FLASH_SECTION_INDEX (127)
#define FLASH_PAGE_INDEX (FLASH_PAGE_3)
```

接线/API依据：[zf_driver_flash.h](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/zf_driver/zf_driver_flash.h)。

**初始化、主循环与中断：** flash_init；检查 sector127/page3，已有内容即擦除；读旧内容，填各类型样值、写页、清RAM缓存后再读回。

关键API：`flash_data_buffer_printf`、`flash_init`、`flash_check`、`flash_erase_page`、`flash_read_page_to_buffer`、`flash_buffer_clear`、`flash_write_page_from_buffer`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 仅用明确可擦的测试页；串口最终读回3.1415926、各整数边界值。

**限制：** 会自动擦写已有页，不能自动运行；必须确认页与程序/参数区不重叠。联合体同一槽不能同时保存多种值。

**迁入项目：** 参数存储独立分区，加入版本、长度、校验及写入频率控制。

<a id="e09"></a>

## E09 — GPT 计时单位

原目录：`E09_timer_demo`。用途：测量时钟计数、微秒和毫秒三种模式。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E09_timer_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E09_timer_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E09_timer_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E09_timer_demo/iar/rt1064.eww)。

**接线与配置：** 无外设；GPT_TIM_1。

接线/API依据：[zf_driver_timer.h](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/zf_driver/zf_driver_timer.h)。

**初始化、主循环与中断：** GPT_TIM_1 依次以 IPG/2、us、ms模式计时540us、65ms、1000ms；每轮 start/stop/get/clear。

关键API：`timer_init`、`timer_start`、`timer_stop`、`timer_get`、`timer_clear`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 串口 us结果约65000、ms约1000；时钟模式结果由实际IPG时钟换算。

**限制：** 计时包含函数开销；GPT资源不能与霍尔等重复初始化。

**迁入项目：** 用于性能测量或超时，保留统一计时器所有权。

<a id="e10"></a>

## E10 — printf、日志与断言

原目录：`E10_printf_debug_log_demo`。用途：演示日志条件、断言开关和串口环缓冲。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E10_printf_debug_log_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E10_printf_debug_log_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E10_printf_debug_log_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E10_printf_debug_log_demo/iar/rt1064.eww)。

**接线与配置：** debug UART1 B12/B13、115200。

**初始化、主循环与中断：** zf_log 条件分别1/0；暂禁断言后调用 zf_assert(0)，再恢复；每ms增计数，每1000次打印秒数，约20秒条件失败；可选读取 debug 环缓冲。

关键API：`zf_log`、`debug_assert_disable`、`zf_assert`、`debug_assert_enable`、`debug_read_ring_buffer`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 终端应有条件日志、Time秒数；计数超过20000会触发启用的断言行为。

**限制：** 这不是长期运行测试，断言失败可能停住；DEBUG_UART_USE_INTERRUPT 控制接收功能。

**迁入项目：** 保留开发断言和限速日志，正式运行避免在实时ISR内 printf。

<a id="e11"></a>

## E11 — PIT 中断优先级设置

原目录：`E11_interrupt_priority_set_demo`。用途：演示修改 NVIC 的 PIT 优先级。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E11_interrupt_priority_set_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E11_interrupt_priority_set_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E11_interrupt_priority_set_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E11_interrupt_priority_set_demo/iar/rt1064.eww)。

**接线与配置：** LED B9；C31宏没有实际外部中断初始化。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define LED1 (B9 )
#define PIT_CH (PIT_CH0 )
#define PIT_PRIORITY (PIT_IRQn)
#define KEY (C31 )
#define KEY_EXTI (GPIO2_Combined_16_31_IRQn)
```

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/zf_driver/zf_driver_gpio.h)。

**初始化、主循环与中断：** PIT_CH0每200ms，interrupt_set_priority(PIT_IRQn,1)；pit_exti_handler 翻转 B9；主循环空转。

关键API：`gpio_init`、`pit_ms_init`、`interrupt_set_priority`、`pit_exti_handler`、`gpio_toggle_level`。

中断入口：`PIT_IRQHandler` 清相应通道标志并调用本例处理函数；通道号改变时同步修改ISR。

**测试与预期：** B9每200ms翻转；调试器核对 PIT IRQ优先级为1。

**限制：** KEY=C31和 KEY_EXTI 仅定义，没有 exti_init；不能据此声称演示抢占。

**迁入项目：** 按控制/采集任务设计 IRQ优先级，再用独立测量确认延迟。

<a id="e12"></a>

## E12 — SD 卡 FatFs

原目录：`E12_fatfs_demo`。用途：演示挂载、目录创建、512字节写读比较。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E12_fatfs_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E12_fatfs_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E12_fatfs_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E12_fatfs_demo/iar/rt1064.eww)。

**接线与配置：** 核心板SD卡槽；SDIO与SDK sdmmc配置和DTCM对齐缓冲。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define BUFFER_SIZE (512U)
```

接线/API依据：[zf_driver_sdio.h](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/zf_driver/zf_driver_sdio.h)。

**初始化、主循环与中断：** sdio_init 与 f_mount；FF_USE_MKFS开启时先 f_mkfs；创建 /dir_1/dir_2 与 /dir_1/f_1.dat，写512字节、seek回0、读回memcmp；串口 q退出后 close。

关键API：`sdio_init`、`f_mount`、`f_chdrive`、`f_mkfs`、`f_mkdir`、`f_open`、`f_opendir`、`f_readdir`、`f_write`、`f_lseek`、`f_read`、`memcmp`、`getchar`、`putchar`、`f_close`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 只用无重要内容的测试卡；终端比较一致，q结束后在PC可见目录和文件。

**限制：** 可能格式化整卡，FA_CREATE_ALWAYS也覆盖同名文件，不能自动运行；DMA缓冲对齐和内存段需保留。

**迁入项目：** 改成受控日志服务，禁用默认格式化，加入错误恢复与写入节流。

<a id="e13"></a>

## E13 — 数据缓存控制

原目录：`E13_cache_demo`。用途：演示 DCache 的启用、禁用及清理失效接口。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E13_cache_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E13_cache_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E13_cache_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E13_cache_demo/iar/rt1064.eww)。

**接线与配置：** test[50] RAM数组，无外接硬件。

**初始化、主循环与中断：** 依次 DisableDCache、EnableDCache、CleanInvalidateDCache，随后调用按范围API。

关键API：`L1CACHE_DisableDCache`、`L1CACHE_EnableDCache`、`L1CACHE_CleanInvalidateDCache`、`L1CACHE_CleanInvalidateDCacheByRange`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 以源码审阅与调试器核对缓存状态；修正地址后，用DMA写入与CPU读取对照验证一致性。

**限制：** 原例将 (uint32)test[0] 当地址，实际为首字节值，不是数组地址；应使用数组指针并满足缓存行对齐。不能直接复制该行。

**迁入项目：** 按DMA缓冲内存区选择noncache或正确 clean/invalidate，参见已知问题。

<a id="e14"></a>

## E14 — ITCM 代码段

原目录：`E14_code_running_in_itcm_demo`。用途：用函数段属性将执行代码置于ITCM。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E14_code_running_in_itcm_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E14_code_running_in_itcm_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E14_code_running_in_itcm_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E14_code_running_in_itcm_demo/iar/rt1064.eww)。

**接线与配置：** B9 LED；ITCM链接段。

接线/API依据：[zf_driver_gpio.h](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/zf_driver/zf_driver_gpio.h)。

**初始化、主循环与中断：** test(long) 以 AT_ITCM_SECTION_INIT 标注；主循环翻转 B9并执行 test(9999999)。

关键API：`test`、`gpio_init`、`gpio_toggle_level`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 查map/反汇编确认test落在ITCM；LED可见翻转，但不作为准确基准。

**限制：** 空延时循环可能被优化；段属性依赖链接布局和启动复制。

**迁入项目：** 仅把需要低延迟的热点函数放ITCM，保留链接段和启动逻辑。

<a id="e15"></a>

## E15 — 变量内存段与对齐

原目录：`E15_specify_variable_position_demo`。用途：比较默认、DTCM、OCRAM、SDRAM及noncache变量布局。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E15_specify_variable_position_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E15_specify_variable_position_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E15_specify_variable_position_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E15_specify_variable_position_demo/iar/rt1064.eww)。

**接线与配置：** 无外接模块；需要当前板的SDRAM及链接脚本匹配。

**初始化、主循环与中断：** a/b默认段，c~j分别用段/4字节对齐宏；主循环递增c~j及局部x。

关键API：本例以段属性/链接配置为主。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 用map和调试器看地址、4字节对齐及变量变化；优化后局部x可能不可见。

**限制：** SDRAM段依赖外部内存初始化；不可把TCM当任意DMA可访问RAM。

**迁入项目：** 为采集图像和DMA描述符选正确段，复制段声明与匹配的链接配置。

<a id="e16"></a>

## E16 — OCOTP 配置示例（仅审阅）

原目录：`E16_burn_fuse_demo`。用途：理解启动配置区访问；本次禁止自动运行。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E16_burn_fuse_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E16_burn_fuse_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E16_burn_fuse_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E16_burn_fuse_demo/iar/rt1064.eww)。

**接线与配置：** 内部OCOTP；EXAMPLE_OCOTP_FUSE_MAP_ADDRESS=0x06、WRITE_VALUE=0x18。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define EXAMPLE_OCOTP_FREQ_HZ (CLOCK_GetFreq(kCLOCK_IpgClk))
#define EXAMPLE_OCOTP_FUSE_MAP_ADDRESS 0x06
#define EXAMPLE_OCOTP_FUSE_WRITE_VALUE 0x18
```

**初始化、主循环与中断：** 读取OCOTP版本和锁寄存器，检查BOOT_CFG锁，调用 OCOTP_WriteFuseShadowRegister 地址0x06值0x18，然后停住。

关键API：`CLOCK_GetFreq`、`OCOTP_Init`、`OCOTP_GetVersion`、`OCOTP_ReadFuseShadowRegister`、`OCOTP_WriteFuseShadowRegister`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 只做代码、map与SDK接口审阅；不以烧录或运行验证。

**限制：** 这是不可逆eFuse编程，不能自动运行。虽然API名含Shadow，但SDK实现写入OCOTP解锁键和DATA并重载熔丝影子寄存器，并非仅改易失RAM；禁止在未知板执行。

SDK依据：[fsl_ocotp.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/sdk/drives/fsl_ocotp.c) · [fsl_ocotp.h](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/sdk/drives/fsl_ocotp.h)。

**迁入项目：** 不迁入车辆日常固件；如需生产配置，应单独按芯片手册和已批准流程处理。

<a id="e17"></a>

## E17 — USB CDC 输出

原目录：`E17_usb_cdc_demo`。用途：验证USB虚拟串口输出。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E17_usb_cdc_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E17_usb_cdc_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E17_usb_cdc_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/E17_usb_cdc_demo/iar/rt1064.eww)。

**接线与配置：** USB数据连接，默认 debug UART仅辅助。

接线/API依据：[zf_driver_usb_cdc.h](../../SeekFree/RT1064_Library/Example/Coreboard_Demo/libraries/zf_driver/zf_driver_usb_cdc.h)。

**初始化、主循环与中断：** usb_cdc_init 后每500ms usb_cdc_write_string 发送固定测试字符串。

关键API：`usb_cdc_init`、`usb_cdc_write_string`。

中断：见链接isr.c；除上述已启用设备外，模板中列出的其他IRQ不会因存在函数而自动工作。

**测试与预期：** 核心板USB数据口连接PC，应枚举串口，每半秒出现 Seekfree USB CDC Test。

**限制：** 数据口和供电/调试口需分清；是否枚举依赖线材、时钟和主机。

**迁入项目：** 复用CDC组件、IRQ及初始化，限制发送对控制周期的阻塞。

