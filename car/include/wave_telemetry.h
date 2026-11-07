#ifndef AUTOCAR_WAVE_TELEMETRY_H
#define AUTOCAR_WAVE_TELEMETRY_H
#include <stdint.h>
#define WAVE_TELEMETRY_CAPACITY 200U
#define WAVE_TELEMETRY_CHANNELS 8U
/* 主循环：新模式/新TCP会话reset；启用后下一次PIT为相对时间零点。
 * 停用丢弃缓存，丢弃数量计入dropped；reset清计数。均保留PRIMASK。 */
void wave_telemetry_reset(void);
void wave_telemetry_set_enabled(uint8_t enabled);
/* 仅10ms PIT在本拍motor_control_tick_10ms之后调用。不联网、不写PWM。
 * 通道：左/右最终PWM(千分比)、左/右轮速(mm/s)、左/右目标(mm/s)、
 * 会话相对时间(ms)、state+100*fault。队满丢新点，时间戳保留缺口。 */
void wave_telemetry_capture(uint32_t now_ms);
/* 主循环取一组；短临界区只复制32字节。 */
uint8_t wave_telemetry_pop(float data[WAVE_TELEMETRY_CHANNELS]);
uint32_t wave_telemetry_dropped(void);
uint16_t wave_telemetry_queued(void);
#endif
