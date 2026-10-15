"""读取同一来源快照的编码器常量，复现固件整数换算；物理轮径由模型另行给定。"""
import re
from fractions import Fraction
from source_layout import source_root

class EncoderScale:

    def __init__(self):
        text = (source_root() / 'config/encoder_config.h').read_text(encoding='utf-8')

        def constant(name):
            values = re.findall('^#define\\s+' + name + '\\s+(\\d+)[Uu]?\\s*$', text, re.M)
            if len(values) != 1 or int(values[0]) <= 0:
                raise ValueError(f'编码器配置须为唯一正整数常量：{name}')
            return int(values[0])
        self.period_ms = constant('ENCODER_FEEDBACK_PERIOD_MS')
        self.numerator = constant('ENCODER_PULSES_PER_REV') * constant('ENCODER_STEP_EDGE_FACTOR') * constant('ENCODER_TO_WHEEL_RATIO_NUM')
        self.denominator = constant('ENCODER_TO_WHEEL_RATIO_DEN')
        self.circumference_um = constant('ENCODER_WHEEL_CIRCUMFERENCE_UM')
        self.counts_per_turn = Fraction(self.numerator, self.denominator)

    def speed_mm_s(self, delta):
        # 与固件一致：半单位远离零，不使用银行家舍入。
        counts_per_second = abs(int(delta)) * 1000 // self.period_ms
        numerator = counts_per_second * self.circumference_um * self.denominator
        denominator = self.numerator * 1000
        magnitude = (numerator + denominator // 2) // denominator
        return -magnitude if delta < 0 else magnitude

    def forward_step_um(self, left_delta, right_delta):
        # 与固件一致：平均两轮，有符号整数除法向零截断。
        counts = int(left_delta) + int(right_delta)
        magnitude = abs(counts) * self.circumference_um * self.denominator // (2 * self.numerator)
        return -magnitude if counts < 0 else magnitude
