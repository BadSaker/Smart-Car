#include "control_pid.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

/* 算法依据：MathWorks PID Controller的clamping及2DOF的测量微分。
 * https://www.mathworks.com/help/simulink/slref/pidcontroller.html
 * https://www.mathworks.com/help/simulink/slref/pidcontroller2dof.html
 * 离散周期、状态恢复和候选积分提交顺序是本模块明确规定的工程选择。 */

static uint8_t config_is_valid(const control_pid_config_t *config)
{
    return (uint8_t)(isfinite(config->kp) && isfinite(config->ki) &&
                     isfinite(config->kd) && isfinite(config->output_min) &&
                     isfinite(config->output_max) && isfinite(config->integral_limit) &&
                     isfinite(config->derivative_tau_s) &&
                     config->output_min < config->output_max &&
                     config->integral_limit >= 0.0f && config->derivative_tau_s >= 0.0f);
}

static void clear_state(control_pid_t *pid)
{
    pid->integral = 0.0f;
    pid->derivative = 0.0f;
    pid->previous_measurement = 0.0f;
    pid->has_previous_measurement = 0U;
    pid->valid = 0U;
}

static float clamp_output(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

uint8_t control_pid_init(control_pid_t *pid, const control_pid_config_t *config)
{
    control_pid_config_t copy;
    if (pid == NULL) return 0U;
    if (config == NULL)
    {
        memset(pid, 0, sizeof(*pid));
        return 0U;
    }
    /* 允许用自身配置重新初始化，先取副本再清状态。 */
    copy = *config;
    memset(pid, 0, sizeof(*pid));
    pid->config = copy;
    pid->initialized = config_is_valid(&copy);
    pid->valid = pid->initialized;
    return pid->valid;
}

void control_pid_reset(control_pid_t *pid)
{
    if (pid == NULL) return;
    clear_state(pid);
    pid->valid = (uint8_t)(pid->initialized && config_is_valid(&pid->config));
}

float control_pid_step(control_pid_t *pid, float setpoint, float measurement,
                       float feedforward, float dt_s)
{
    float error;
    float proportional;
    float integral_change;
    float integral_candidate;
    float derivative = 0.0f;
    float output;
    const control_pid_config_t *config;

    if (pid == NULL) return 0.0f;
    config = &pid->config;
    if (!pid->initialized || !config_is_valid(config) ||
        !isfinite(setpoint) || !isfinite(measurement) || !isfinite(feedforward) ||
        !isfinite(dt_s) || dt_s <= 0.0f || !isfinite(pid->integral) ||
        !isfinite(pid->derivative) || !isfinite(pid->previous_measurement))
    {
        clear_state(pid);
        return 0.0f;
    }

    error = setpoint - measurement;
    proportional = config->kp * error;
    integral_change = config->ki * error * dt_s;
    integral_candidate = pid->integral + integral_change;
    if (!isfinite(error) || !isfinite(proportional) ||
        !isfinite(integral_change) || !isfinite(integral_candidate))
    {
        clear_state(pid);
        return 0.0f;
    }
    integral_candidate = clamp_output(integral_candidate, -config->integral_limit,
                                      config->integral_limit);

    if (pid->has_previous_measurement && config->kd != 0.0f)
    {
        float measurement_change = measurement - pid->previous_measurement;
        float denominator = config->derivative_tau_s + dt_s;
        if (!isfinite(measurement_change) || !isfinite(denominator))
        {
            clear_state(pid);
            return 0.0f;
        }
        /* D[k] = tau/(tau+dt)*D[k-1] - kd/(tau+dt)*(y[k]-y[k-1])。
         * 设定值的跳变不会进入微分，复位后的首个样本不制造微分尖峰。 */
        derivative = (config->derivative_tau_s / denominator) * pid->derivative -
                     config->kd * (measurement_change / denominator);
    }
    output = proportional + integral_candidate + derivative + feedforward;
    if (!isfinite(derivative) || !isfinite(output))
    {
        clear_state(pid);
        return 0.0f;
    }

    /* 判断实际积分方向，可同时覆盖负增益、非对称限幅及前馈占用。 */
    if (!((output > config->output_max && integral_change > 0.0f) ||
          (output < config->output_min && integral_change < 0.0f)))
    {
        pid->integral = integral_candidate;
    }
    pid->derivative = derivative;
    pid->previous_measurement = measurement;
    pid->has_previous_measurement = 1U;
    pid->valid = 1U;
    return clamp_output(output, config->output_min, config->output_max);
}
