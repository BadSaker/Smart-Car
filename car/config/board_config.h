#ifndef AUTOCAR_BOARD_CONFIG_H
#define AUTOCAR_BOARD_CONFIG_H

/* C车模、逐飞RT1064 V3.0、MT9V034、普通IPS200 SPI和Wi-Fi SPI。
 * 电机上电不输出，C12测试由motor_test_config.h管理；舵机1由servo_config.h管理。
 * 编码器接线、方向以及实际机械中位仍需按实物核对。 */
#ifndef APP_ENABLE_IPS200
#define APP_ENABLE_IPS200          (1)
#endif
#define APP_IPS200_DIRECTION       IPS200_CROSSWISE
#define APP_CONTROL_PERIOD_MS     (10)
#define APP_SYSTEM_TICK_MS        (1U)
#if APP_SYSTEM_TICK_MS != 1U
#error "异步SPI保护间隔要求1ms时间基准"
#endif
#define APP_CONTROL_PERIOD_S      ((float)APP_CONTROL_PERIOD_MS / 1000.0f)



/* 编码器STEP/DIR及计数/传动参数统一在encoder_config.h。
 * 双驱通道编号和参考方向电平统一在motor_test_config.h。
 * 不再沿用未经实测的左右电机别名或编码器符号翻转。 */

/* 舵机接口1的信号、脉宽和按键测试参数统一在servo_config.h维护。
 * 不再使用旧参考程序的角度到0.5–2.5ms全行程换算。 */

#endif /* AUTOCAR_BOARD_CONFIG_H */
