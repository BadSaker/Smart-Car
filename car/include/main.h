#ifndef AUTOCAR_MAIN_H
#define AUTOCAR_MAIN_H

#include "zf_common_headfile.h"
#include "board_config.h"
#include "img_process.h"
#include "pid.h"
#include "isr.h"
#include "camera_debug.h"
#include "camera_display.h"
#include "servo_control.h"
#include "motor_control.h"
#include "encoder_feedback.h"
#include "remote_control.h"
#include "remote_control_config.h"

extern volatile uint32_t app_uptime_ms;

#endif
