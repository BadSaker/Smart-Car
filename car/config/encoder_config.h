#ifndef AUTOCAR_ENCODER_CONFIG_H
#define AUTOCAR_ENCODER_CONFIG_H

#include "zf_driver_encoder.h"

/* 用户确认：左侧接ENC1，右侧接ENC2；QTMR硬件按STEP上升沿和DIR电平计数。 */
#define ENCODER_LEFT_CHANNEL         QTIMER1_ENCODER1
#define ENCODER_LEFT_STEP_PIN        QTIMER1_ENCODER1_CH1_C0
#define ENCODER_LEFT_DIRECTION_PIN   QTIMER1_ENCODER1_CH2_C1
#define ENCODER_RIGHT_CHANNEL        QTIMER1_ENCODER2
#define ENCODER_RIGHT_STEP_PIN       QTIMER1_ENCODER2_CH1_C2
#define ENCODER_RIGHT_DIRECTION_PIN  QTIMER1_ENCODER2_CH2_C24

#define ENCODER_FEEDBACK_PERIOD_MS   10U
#define ENCODER_PULSES_PER_REV       1024U
#define ENCODER_STEP_EDGE_FACTOR     1U

/* 用户确认：车轮转1圈，啮合齿轮驱动编码器转4.25圈。
 * 此处为编码器:车轮转数比，不是电机:车轮减速比；两者不能相互代用。
 * 默认每轮圈计数=1024*1*425/100=4352。允许主机测试以1:1覆盖该比例。 */
#ifndef ENCODER_TO_WHEEL_RATIO_NUM
#define ENCODER_TO_WHEEL_RATIO_NUM   425U
#endif
#ifndef ENCODER_TO_WHEEL_RATIO_DEN
#define ENCODER_TO_WHEEL_RATIO_DEN   100U
#endif

/* 用户确认轮直径64mm；周长为pi*64mm取整到微米，仍需实车滚动标定。 */
#define ENCODER_WHEEL_DIAMETER_UM    64000U
#define ENCODER_WHEEL_CIRCUMFERENCE_UM 201062U

#if ENCODER_FEEDBACK_PERIOD_MS != 10U
#error "encoder_feedback_tick_10ms requires a 10ms period"
#endif
#if ENCODER_TO_WHEEL_RATIO_NUM == 0U || ENCODER_TO_WHEEL_RATIO_DEN == 0U
#error "encoder to wheel ratio must be positive"
#endif

#endif /* AUTOCAR_ENCODER_CONFIG_H */
