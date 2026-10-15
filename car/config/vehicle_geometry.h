#ifndef AUTOCAR_VEHICLE_GEOMETRY_H
#define AUTOCAR_VEHICLE_GEOMETRY_H

/* 当前车模参数；190mm轴距沿用工程记录，180mm轮胎外宽沿用C1模型。
 * 2026-10-14用户实测前轮相对直行端点tan=21.5/31，左右基本相同。
 * 对应1330/1630us，中位1480us；中间位置暂按线性脉宽映射，仍待多点核对。 */
#define VEHICLE_WHEELBASE_MM          190.0f
#define VEHICLE_REAR_TRACK_MM        153.0f
#define VEHICLE_HALF_TIRE_OUTER_MM    90.0f
#define VEHICLE_MAX_STEER_RAD         (34.7431801583f * 0.017453292519943295f)

#endif
