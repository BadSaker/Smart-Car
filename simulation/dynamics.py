"""两轮转动惯性、有限接地与底盘纵向耦合；不用地图修正编码器读数。

质量按底盘归一化，wheel_inertia_ratio为每轮等效转动质量/底盘质量。
轮胎纵向力随滑移增长并受附着限制；隐式步进处理接地刚性。
转向仍用自行车运动学，侧滑和横摆惯性未标定，不能视作完整实车模型。
"""
import math

class WheelDynamics:

    def __init__(self, p):
        self.p = p
        self.wheel = [0.0, 0.0]
        self.speed = 0.0
        self.contact_force = [0.0, 0.0]
        self.slip = [0.0, 0.0]

    # 输入为左右PWM千分比、舵角弧度、步长秒；更新纵向状态和轮速。
    def step(self, duties, steer, dt):
        p = self.p
        if not all((math.isfinite(float(x)) for x in (*duties, steer, dt))) or dt <= 0:
            raise ValueError('动力学输入必须有限且步长为正')
        mass = p.get('wheel_inertia_ratio', 0.04)
        stiffness = p.get('tire_stiffness', 60.0)
        ratio = math.tan(steer) * p['track'] / (2 * p['wheelbase'])
        factors = [1 - ratio, 1 + ratio]
        if min(factors) <= 0:
            raise ValueError('当前前进模型要求转弯半径大于半轮距')
        limits = [p.get('traction_left', 0.9) * 9.81 / 2, p.get('traction_right', 0.9) * 9.81 / 2]
        corner = p.get('curve_load', 0.0) * abs(math.tan(steer)) * self.speed * self.speed / 1000 / 2
        loads = [p.get('wheel_load_left', 0.0) / 1000 + corner, p.get('wheel_load_right', 0.0) / 1000 + corner]
        coefficients = []
        sources = []
        for k, duty in enumerate(duties):
            tau = p['motor_tau'] if duty > p['motor_deadzone'] else p['coast_tau']
            coefficient = (1 + 2 * mass) / (2 * tau)
            free = max(0, min(1000, float(duty)) - p['motor_deadzone']) * p['motor_gain'] / 1000
            coefficients.append(mass / dt + coefficient)
            # 阻力不能在静止且无驱动时凭空把车轮倒推。
            sources.append(max(0, mass / dt * self.wheel[k] + coefficient * free - loads[k]))
        fixed = [None, None]
        for _ in range(3):
            offsets = []
            slopes = []
            for k in (0, 1):
                if fixed[k] is None:
                    offsets.append(stiffness * sources[k] / (coefficients[k] + stiffness))
                    slopes.append(stiffness * coefficients[k] * factors[k] / (coefficients[k] + stiffness))
                else:
                    offsets.append(fixed[k])
                    slopes.append(0.0)
            speed = max(0, (self.speed / dt + sum(offsets)) / (1 / dt + sum(slopes)))
            forces = [offsets[k] - slopes[k] * speed for k in (0, 1)]
            changed = False
            for k in (0, 1):
                if fixed[k] is None and abs(forces[k]) > limits[k]:
                    fixed[k] = math.copysign(limits[k], forces[k])
                    changed = True
            if not changed:
                break
        self.speed = speed
        self.contact_force = forces
        self.wheel = [max(0, (sources[k] - forces[k]) / coefficients[k]) for k in (0, 1)]
        self.slip = [self.wheel[k] - factors[k] * speed for k in (0, 1)]
