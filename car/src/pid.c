#include "pid.h"

// 电机PID初始化参数
float speed_KP = 50, speed_KI = 0,speed_KD = 0.0, speed_IMAX = 5000.0, speed_OUTMAX = 5000.0;
volatile float speed_target = 0.0f;
volatile float speed_real = 0.0f;
volatile float speed_pwm = 0.0f;

pid_param_t speed_pid_l;  // 电机PID
pid_param_t speed_pid_r;  // 电机PID

/*******************************************************************************
* 函 数 名         : My_Pid_Init
* 函数功能         : PID初始化	
* 输    入         : 无
* 输    出         : 无
*******************************************************************************/
void My_Pid_Init(void)
{
    Pid_Param_Init(&speed_pid_l, speed_KP, speed_KI,speed_KD, speed_IMAX, speed_OUTMAX);
    Pid_Param_Init(&speed_pid_r, speed_KP, speed_KI,speed_KD, speed_IMAX, speed_OUTMAX);
}


/*******************************************************************************
* 函 数 名         : Pid_Param_Init
* 函数功能         : PID参数初始化	
* 输    入         : imax:积分项最大值
* 输    出         : 无
*******************************************************************************/
void Pid_Param_Init(pid_param_t * pid, float kp, float ki, float kd, float imax, float outmax)
{
    pid->kp        = kp;
    pid->ki        = ki;
    pid->kd        = kd;
    pid->imax      = imax;
    pid->out_p     = 0;
    pid->out_i     = 0;
    pid->out_d     = 0;
    pid->out       = 0;
    pid->outmax    = outmax;
    pid->integrator= 0;
    pid->last_error= 0;
    pid->last_derivative   = 0;
    pid->last_t    = 0;
}

/*******************************************************************************
* 函 数 名         : PidLocCtrl
* 函数功能	   : 位置式PID控制
* 输    入         : pid：控制器状态；error：当前误差；t：采样周期
* 输    出         : 浮点型数值
*******************************************************************************/
float PidLocCtrl(pid_param_t * pid, float error, float t)
{
    if (t <= 0.0f)
    {
        return pid->out;
    }
    /* 累积误差 */
    pid->integrator += error;

    /* 误差限幅 */
    pid->integrator = constrain_float(pid->integrator, -pid->imax, pid->imax);


    pid->out_p = pid->kp * error;
    pid->out_i = pid->ki * t * pid->integrator;
    pid->out_d = pid->kd/t * (error - pid->last_error);

    pid->last_error = error;

    pid->out = pid->out_p + pid->out_i + pid->out_d;

    pid->out = constrain_float(pid->out, -pid->outmax, pid->outmax);

    return pid->out;
}


/*******************************************************************************
* 函 数 名         : PidIncCtrl
* 函数功能	   : 增量式PID控制
* 输    入         : pid：控制器状态；error：当前误差；t：采样周期
* 输    出         : 浮点型数值
*******************************************************************************/
float PidIncCtrl(pid_param_t * pid, float error, float t)
{
    if (t <= 0.0f)
    {
        return pid->out;
    }

    pid->out_p = pid->kp * (error - pid->last_error);
    pid->out_i = pid->ki * error * t ;
    pid->out_d = pid->kd/t * ((error - pid->last_error) - pid->last_derivative);

    pid->last_derivative = error - pid->last_error;
    pid->last_error = error;

    pid->out += pid->out_p + pid->out_i + pid->out_d;

    pid->out = constrain_float(pid->out, -pid->outmax, pid->outmax);
    return pid->out;
}

/*******************************************************************************
* 函 数 名         : constrain_float
* 函数功能         : 浮点型数限幅
* 输    入         : amt：待限幅数值；low：下限；high：上限
* 输    出         : 浮点型数值
*******************************************************************************/
float constrain_float(float amt, float low, float high)
{
    return ((amt)<(low)?(low):((amt)>(high)?(high):(amt)));
}

/*******************************************************************************
* 函 数 名         : constrain_short
* 函数功能         : 短整型数限幅
* 输    入         : amt：待限幅数值；low：下限；high：上限
* 输    出         : 短整型数值
*******************************************************************************/
short constrain_short(short amt, short low, short high)
{
    return ((amt)<(low)?(low):((amt)>(high)?(high):(amt)));
}
