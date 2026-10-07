#include "zf_common_headfile.h"
#include "board_config.h"
#include "camera_display.h"
#include "servo_control.h"
#include "motor_control.h"
#include "encoder_feedback.h"
#include "remote_control.h"
#include "remote_control_config.h"

#if APP_ENABLE_IPS200
static int previous_network_status = -1;
static void display_line(uint16 y, const char *text)
{
    char line[40];
    /* 每字8像素，39个字符可放入320像素横屏。 */
    snprintf(line, sizeof(line), "%-39.39s", text);
    ips200_show_string(0, y, line);
}

#if REMOTE_CONTROL_ENABLED
static uint8_t display_remote_status(void)
{
    motor_control_status_t motor;
    remote_control_status_t remote;
    const servo_control_status_t *servo = servo_control_get_status();
    char text[40];
    motor_control_get_status(&motor);
    if (motor.state == MOTOR_TEST_CONFIG_ERROR) return 0;
    remote_control_get_status(&remote);
    if (!motor.remote_link) display_line(0, "RC:WAIT WiFi C13:ON");
    else if (!motor.remote_armed) display_line(0, "RC:OFF C12:ARM S3:STOP");
    else if (remote.draining) display_line(0, "RC:DRAIN old input; wait");
    else if (motor.remote_wait_neutral) display_line(0, "RC:WAIT Down=STOP to enable");
    else
    {
        snprintf(text, sizeof(text), "RC:ON T:%d/1000 RX:%lu", (int)motor.applied_permille,
                 (unsigned long)remote.received_commands);
        display_line(0, text);
    }
    snprintf(text, sizeof(text), "Steer:%uus S3:STOP", (unsigned)servo->current_pulse_us);
    display_line(16, text);
    return 1;
}
#endif

static char motor_phase_code(motor_test_state_t state)
{
    switch (state)
    {
        case MOTOR_TEST_CHANNEL1: return '1';
        case MOTOR_TEST_CHANNEL2: return '2';
        case MOTOR_TEST_BOTH: return 'B';
        case MOTOR_TEST_GAP1:
        case MOTOR_TEST_GAP2: return 'P';
        case MOTOR_TEST_COMPLETE: return 'D';
        case MOTOR_TEST_STOPPED: return 'S';
        default: return '-';
    }
}

static void display_encoder_speed(uint16 y, char side,
                                  const encoder_channel_feedback_t *channel, char phase)
{
    char text[40];
    /* 先扩展再取绝对值，兼容负的零点几rpm及INT32_MIN诊断值。 */
    uint32_t magnitude = (uint32_t)(channel->wheel_rpm_x10 < 0 ?
                          -(int64_t)channel->wheel_rpm_x10 : channel->wheel_rpm_x10);
    snprintf(text, sizeof(text), "%c %s%lu.%lurpm %ldmm/s %c", side,
             channel->wheel_rpm_x10 < 0 ? "-" : "",
             (unsigned long)(magnitude / 10U), (unsigned long)(magnitude % 10U),
             (long)channel->speed_mm_s, phase);
    display_line(y, text);
}

static uint8_t display_motor_feedback(void)
{
    motor_control_status_t motor;
    encoder_feedback_snapshot_t feedback;
    char text[40], phase;
    motor_control_get_status(&motor);
    if (motor.state == MOTOR_TEST_CONFIG_ERROR)
    {
        display_line(0, "MOTOR CONFIG ERROR");
        display_line(16, "Check motor_test_config.h");
        return 1;
    }
    if (!motor.telemetry_visible) return 0;
    encoder_feedback_get_snapshot(&feedback);
    phase = motor_phase_code(motor.state);
    /* 100个10ms样本为1秒；仅切换原顶部两行，不覆盖摄像头区域。 */
    if ((feedback.sample_sequence / (1000U / APP_CONTROL_PERIOD_MS)) & 1U)
    {
        display_encoder_speed(0, 'L', &feedback.left, phase);
        display_encoder_speed(16, 'R', &feedback.right, phase);
    }
    else
    {
        snprintf(text, sizeof(text), "L%s d:%d c:%ld n:%ld",
                 feedback.left.total_saturated ? "!" : "", (int)feedback.left.delta_counts,
                 (long)feedback.left.counts_per_second, (long)feedback.left.total_counts);
        display_line(0, text);
        snprintf(text, sizeof(text), "R%s d:%d c:%ld n:%ld %c",
                 feedback.right.total_saturated ? "!" : "", (int)feedback.right.delta_counts,
                 (long)feedback.right.counts_per_second, (long)feedback.right.total_counts, phase);
        display_line(16, text);
    }
    return 1;
}
#endif

void camera_display_init(void)
{
#if APP_ENABLE_IPS200
    /* 驱动在初始化时写入MADCTL，必须先设置横屏方向。 */
    ips200_set_dir(APP_IPS200_DIRECTION);
    ips200_init(IPS200_TYPE_SPI);
    ips200_set_font(IPS200_8X16_FONT);
    ips200_set_color(RGB565_WHITE, RGB565_BLACK);
    ips200_clear();
    previous_network_status = -1;
    display_line(0, "MT9V034 GRAY 188x120");
    camera_display_message("Camera initializing...");
    display_line(224, "Wi-Fi not started");
#endif
}

void camera_display_message(const char *text)
{
#if APP_ENABLE_IPS200
    if (text != NULL) display_line(16, text);
#else
    (void)text;
#endif
}

void camera_display_network_status(camera_debug_status_t status)
{
#if APP_ENABLE_IPS200
    const char *text;
    char details[40];
    if ((int)status == previous_network_status) return;
    previous_network_status = (int)status;
    switch (status)
    {
        case CAMERA_DEBUG_DISABLED:
#if CAMERA_DEBUG_ENABLED
                                        text = "Wi-Fi OFF; C13:ON"; break;
#else
                                        text = "Wi-Fi output disabled"; break;
#endif
        case CAMERA_DEBUG_READY:         text = "Wi-Fi ON; TCP ready; C13:OFF"; break;
        case CAMERA_DEBUG_CONFIG_ERROR:  text = "Wi-Fi: check network config"; break;
        case CAMERA_DEBUG_INIT_ERROR:    text = "Wi-Fi failed; waiting to reconnect"; break;
        case CAMERA_DEBUG_CONNECT_ERROR: text = "TCP failed; waiting to reconnect"; break;
        case CAMERA_DEBUG_INITIALIZING:  text = "Wi-Fi ON; initializing/reconnecting"; break;
        case CAMERA_DEBUG_CONNECTING:    text = "Wi-Fi ON; TCP connecting..."; break;
        case CAMERA_DEBUG_SEND_ERROR:
        {
            const char *part;
            switch (camera_debug_last_error_stage)
            {
                case CAMERA_DEBUG_STAGE_IMAGE_HEADER:    part = "head"; break;
                case CAMERA_DEBUG_STAGE_IMAGE:           part = "image"; break;
                case CAMERA_DEBUG_STAGE_BOUNDARY_HEADER: part = "b-head"; break;
                case CAMERA_DEBUG_STAGE_LEFT:            part = "left"; break;
                case CAMERA_DEBUG_STAGE_MIDDLE:          part = "middle"; break;
                case CAMERA_DEBUG_STAGE_RIGHT:           part = "right"; break;
                default:                                part = "unknown"; break;
            }
            snprintf(details, sizeof(details), "TX %s rem:%lu; reconnect", part,
                     (unsigned long)camera_debug_last_remaining);
            text = details;
        }break;
        default:                        text = "Wi-Fi output disabled"; break;
    }
    display_line(224, text);
#else
    (void)status;
#endif
}

void camera_display_statistics(void)
{
#if APP_ENABLE_IPS200
    char text[40];
#if REMOTE_CONTROL_ENABLED
    if (display_remote_status()) return;
#endif
    if (display_motor_feedback()) return;
    /* 三个uint32计数达到最大值时仍不超过39字符。 */
    snprintf(text, sizeof(text), "TX:%lu E:%lu R:%lu",
             (unsigned long)camera_debug_frames_sent,
             (unsigned long)camera_debug_error_count,
             (unsigned long)camera_debug_reconnect_count);
    display_line(0, text);
    {
        const servo_control_status_t *servo = servo_control_get_status();
        const char *state;
        switch (servo->state)
        {
            case SERVO_CONTROL_SUPPLY_ERROR: state = "POWER"; break;
            case SERVO_CONTROL_CENTER: state = "CENTER"; break;
            case SERVO_CONTROL_TO_LOW:
            case SERVO_CONTROL_HOLD_LOW: state = "LOW"; break;
            case SERVO_CONTROL_TO_HIGH:
            case SERVO_CONTROL_HOLD_HIGH: state = "HIGH"; break;
            case SERVO_CONTROL_RETURN_CENTER: state = "RETURN"; break;
            case SERVO_CONTROL_COMPLETE: state = "DONE"; break;
            case SERVO_CONTROL_STOPPED: state = "STOP"; break;
            default: state = "IDLE"; break;
        }
        snprintf(text, sizeof(text), "S:%s %u us S2:run S3:stop", state,
                 (unsigned)servo->current_pulse_us);
        display_line(16, text);
    }
#endif
}

void camera_display_frame(const uint8_t *gray_image)
{
#if APP_ENABLE_IPS200
    if (gray_image != NULL)
        ips200_show_gray_image(19, 40, gray_image, MT9V03X_W, MT9V03X_H, 282, 180, 0);
#else
    (void)gray_image;
#endif
}
