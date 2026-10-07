#ifndef _PID_H
#define _PID_H

#include <stdint.h>

typedef struct
{
    float                kp;                    //比例系数
    float                ki;                    //积分系数
    float                kd;                    //微分系数
    float                imax;                  //积分限幅

    float                out_p;                 //比例项输出
    float                out_i;                 //积分项输出
    float                out_d;                 //微分项输出
    float                out;                   //PID输出
    float                outmax;                //输出限幅

    float                integrator;            //< 积分值
    float                last_error;            //< 上次误差
    float                last_derivative;       //< 上次误差与上上次误差之差
    unsigned long        last_t;                //< 上次时间
}pid_param_t;

extern pid_param_t speed_pid_l;  // 电机PID
extern pid_param_t speed_pid_r;  // 电机PID
extern float speed_KP, speed_KI,speed_KD, speed_IMAX, speed_OUTMAX;
extern volatile float speed_target;             // 每个控制周期的目标编码器计数
extern volatile float speed_real;               // 中断更新的实际编码器计数
extern volatile float speed_pwm;                // 中断更新的PWM输出，主循环可观察


void My_Pid_Init(void);
void Pid_Param_Init(pid_param_t * pid, float kp, float ki, float kd, float imax, float outmax);

float constrain_float(float amt, float low, float high);
short constrain_short(short amt, short low, short high);


float PidLocCtrl(pid_param_t * pid, float error, float t);
float PidIncCtrl(pid_param_t * pid, float error , float t);


#endif
