# 编码器

本页以 V3.11.2 的真实 `main.c`、`isr.c` 及本分类 `libraries` 为准。每项均提供原工程入口；所有测试步骤是待上板执行的操作与预期，本次没有编译或运行这84个原例。主板版本未知时，以所用板的原理图、丝印和实际连通性核对引脚。

先阅读 [统一准备与迁入流程](README.md#prepare)。源码中的 `clock_init(SYSTEM_CLOCK_600M)` 保留；调用 `debug_init` 的例程默认 UART1、115200、MCU TX B12/RX B13。原例 `main.c` 不能与车辆工程的 `main` 同时加入，`isr.c` 也必须逐IRQ合并。

<a id="e2_01"></a>

## E2_01 — 四路A/B 正交编码器

原目录：`E2_01_encoder_quadrature_demo`。用途：读取主板四个编码器接口的100ms增量。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E2_encoder/E2_01_encoder_quadrature_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E2_encoder/E2_01_encoder_quadrature_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E2_encoder/E2_01_encoder_quadrature_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E2_encoder/E2_01_encoder_quadrature_demo/iar/rt1064.eww)。

**接线与配置：** 通道1 C0/C1；2 C2/C24；3 C3/C4；4 C5/C25。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define ENCODER_1 (QTIMER1_ENCODER1)
#define ENCODER_1_A (QTIMER1_ENCODER1_CH1_C0)
#define ENCODER_1_B (QTIMER1_ENCODER1_CH2_C1)
#define ENCODER_2 (QTIMER1_ENCODER2)
#define ENCODER_2_A (QTIMER1_ENCODER2_CH1_C2)
#define ENCODER_2_B (QTIMER1_ENCODER2_CH2_C24)
#define ENCODER_3 (QTIMER2_ENCODER1)
#define ENCODER_3_A (QTIMER2_ENCODER1_CH1_C3)
#define ENCODER_3_B (QTIMER2_ENCODER1_CH2_C4)
#define ENCODER_4 (QTIMER2_ENCODER2)
#define ENCODER_4_A (QTIMER2_ENCODER2_CH1_C5)
#define ENCODER_4_B (QTIMER2_ENCODER2_CH2_C25)
#define PIT_CH (PIT_CH0 )
```

**初始化、主循环与中断：** 四路 encoder_quad_init；PIT_CH0每100ms read+clear，主循环每100ms打印四个窗口计数。

关键API：`encoder_quad_init`、`pit_ms_init`、`pit_handler`、`encoder_get_count`、`encoder_clear_count`。

中断入口：`PIT_IRQHandler` 清相应通道标志并调用本例处理函数；通道号改变时同步修改ISR。

**测试与预期：** 手动正反转各轴，只有对应通道响应、符号反向，停转后为0。

**限制：** 1024线不能直接当轮端计数/转；需验证输出协议、倍频、传动比和正方向。

**迁入项目：** 现车优先以A/B正交验证；保留需要的两路并迁入固定周期速度采样。

<a id="e2_02"></a>

## E2_02 — 四路脉冲/方向编码器

原目录：`E2_02_encoder_dir_demo`。用途：读取主板四个编码器接口的100ms增量。

源码与工程：[main.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E2_encoder/E2_02_encoder_dir_demo/user/src/main.c) · [isr.c](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E2_encoder/E2_02_encoder_dir_demo/user/src/isr.c) · [Keil工程](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E2_encoder/E2_02_encoder_dir_demo/mdk/rt1064.uvprojx) · [IAR工作区](../../SeekFree/RT1064_Library/Example/Motherboard_Demo/E2_encoder/E2_02_encoder_dir_demo/iar/rt1064.eww)。

**接线与配置：** 通道1 C0/C1；2 C2/C24；3 C3/C4；4 C5/C25。

本例main中的配置宏（保持源码符号，以当前头文件展开为准）：

```c
#define ENCODER_1 (QTIMER1_ENCODER1)
#define ENCODER_1_LSB (QTIMER1_ENCODER1_CH1_C0)
#define ENCODER_1_DIR (QTIMER1_ENCODER1_CH2_C1)
#define ENCODER_2 (QTIMER1_ENCODER2)
#define ENCODER_2_LSB (QTIMER1_ENCODER2_CH1_C2)
#define ENCODER_2_DIR (QTIMER1_ENCODER2_CH2_C24)
#define ENCODER_3 (QTIMER2_ENCODER1)
#define ENCODER_3_LSB (QTIMER2_ENCODER1_CH1_C3)
#define ENCODER_3_DIR (QTIMER2_ENCODER1_CH2_C4)
#define ENCODER_4 (QTIMER2_ENCODER2)
#define ENCODER_4_LSB (QTIMER2_ENCODER2_CH1_C5)
#define ENCODER_4_DIR (QTIMER2_ENCODER2_CH2_C25)
#define PIT_CH (PIT_CH0 )
```

**初始化、主循环与中断：** 四路 encoder_dir_init；PIT_CH0每100ms read+clear，主循环每100ms打印四个窗口计数。

关键API：`encoder_dir_init`、`pit_ms_init`、`pit_handler`、`encoder_get_count`、`encoder_clear_count`。

中断入口：`PIT_IRQHandler` 清相应通道标志并调用本例处理函数；通道号改变时同步修改ISR。

**测试与预期：** 手动正反转各轴，只有对应通道响应、符号反向，停转后为0。

**限制：** 1024线不能直接当轮端计数/转；需验证输出协议、倍频、传动比和正方向。

**迁入项目：** 仅实物确认pulse/dir后使用；与A/B模式二选一，不能混用初始化。

