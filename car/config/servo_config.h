#ifndef AUTOCAR_SERVO_CONFIG_H
#define AUTOCAR_SERVO_CONFIG_H

/* RT1064 V3.0：P16/舵机接口1；S2启动，S3停止，按键按下接地。
 * 依据主板手册PDF第12、27页；实际插头极性仍须按板上丝印核对。 */
#define SERVO_PWM_CHANNEL             PWM4_MODULE2_CHA_C30
#define SERVO_SIGNAL_PIN              C30
#define SERVO_START_KEY_PIN           C15
#define SERVO_STOP_KEY_PIN            C14
#define SERVO_KEY_ACTIVE_LEVEL        GPIO_LOW

/* 用户于2026-10-03确认已调到6.0V；这是配置记录，不是软件测量。
 * Futaba官方产品目录列出S-U400供电6.0–7.4V，配置不符时拒绝启动。 */
#ifndef SERVO_SUPPLY_MV
#define SERVO_SUPPLY_MV               (6000U)
#endif
#define SERVO_SUPPLY_MIN_MV           (6000U)
#define SERVO_SUPPLY_MAX_MV           (7400U)

/* 用户于2026-10-03通过校准确认本车中值为1480us。
 * 按已批准方案将摆幅从±40us增至±60us，端点1420/1540us，机械极限仍需观察。
 * 本车已确认1420us右转、1540us左转；不沿用0.5–2.5ms全行程角度公式。 */
#define SERVO_PWM_FREQUENCY_HZ        (50U)
#define SERVO_REFERENCE_CENTER_US     (1480U)
#define SERVO_TEST_EXCURSION_US       (60U)
#define SERVO_TEST_MIN_PULSE_US       (SERVO_REFERENCE_CENTER_US - SERVO_TEST_EXCURSION_US)
#define SERVO_TEST_MAX_PULSE_US       (SERVO_REFERENCE_CENTER_US + SERVO_TEST_EXCURSION_US)
#ifndef SERVO_TEST_STEP_US
#define SERVO_TEST_STEP_US            (2U)
#endif
#define SERVO_TEST_STEP_INTERVAL_MS   (20U)
#define SERVO_TEST_CENTER_HOLD_MS     (500U)
#define SERVO_TEST_ENDPOINT_HOLD_MS   (300U)
#define SERVO_KEY_DEBOUNCE_MS         (30U)

#if SERVO_REFERENCE_CENTER_US <= SERVO_TEST_EXCURSION_US
#error "Servo reference pulse must stay positive"
#endif
#if SERVO_TEST_MAX_PULSE_US >= (1000000U / SERVO_PWM_FREQUENCY_HZ)
#error "Servo pulse must be shorter than one PWM period"
#endif
#if SERVO_TEST_STEP_US == 0 || SERVO_TEST_STEP_US > SERVO_TEST_EXCURSION_US
#error "Servo test step must be positive and within the test excursion"
#endif

#endif /* AUTOCAR_SERVO_CONFIG_H */
