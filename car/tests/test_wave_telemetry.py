"""真实波形队列、默认电机入口和采样集成；仅替换硬件边界。"""
from pathlib import Path
import subprocess
import unittest
import re
from test_motor_control import PLATFORM, COMPILER

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / 'car/build/wifi-wave-tests'

class WaveTelemetryTests(unittest.TestCase):
    def compile_run(self, name, sources, definitions=()):
        stub = SCRATCH / 'include'
        stub.mkdir(parents=True, exist_ok=True)
        config=(ROOT/'car/config/autonomous_config.h').read_text(encoding='utf-8')
        if name=='queue-motor1-left':
            config=re.sub(r'(#define AUTO_MOTOR1_IS_LEFT )\d', r'\g<1>1', config)
        (stub/'autonomous_config.h').write_text(config,encoding='utf-8')
        (stub/'platform.h').write_text(PLATFORM, encoding='ascii')
        for header in ('zf_driver_gpio.h', 'zf_driver_pwm.h', 'fsl_common.h'):
            (stub/header).write_text('#include "platform.h"\n', encoding='ascii')
        exe = SCRATCH / (name+'.exe')
        cmd = [COMPILER, '-std=c99', '-Wall', '-Wextra', '-Werror',
               '-I'+str(stub), '-I'+str(ROOT/'car/include'), '-I'+str(ROOT/'car/config')]
        cmd += ['-D'+d for d in definitions]
        cmd += [str(ROOT/s) for s in sources] + ['-o', str(exe)]
        result = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        print(result.stdout.strip())

    def test_c12_cannot_drive_or_stop_default_motor(self):
        self.compile_run('c12', ['car/tests/test_wave_motor.c', 'car/src/motor_control.c'])

    def test_queue_and_same_tick_channels(self):
        self.assertTrue((ROOT/'car/src/wave_telemetry.c').is_file(), '100Hz波形采样模块尚未实现')
        self.compile_run('queue', ['car/tests/test_wave_telemetry.c', 'car/src/wave_telemetry.c'])

    def test_alternate_mapping_motor_one_left(self):
        self.assertTrue((ROOT/'car/src/wave_telemetry.c').is_file(), '波形采样模块尚未实现')
        self.compile_run('queue-motor1-left', ['car/tests/test_wave_telemetry.c', 'car/src/wave_telemetry.c'], ['TEST_MOTOR1_LEFT=1'])

    def test_capture_follows_final_pwm_in_isr(self):
        text = (ROOT/'car/src/isr.c').read_text(encoding='utf-8')
        self.assertIn('wave_telemetry_capture(app_uptime_ms)', text)
        self.assertLess(text.index('motor_control_tick_10ms();'),
                        text.index('wave_telemetry_capture(app_uptime_ms)'))

if __name__ == '__main__':
    unittest.main(verbosity=2)
