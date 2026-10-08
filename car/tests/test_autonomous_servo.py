"""验证自动转向所有权、实际PWM范围与迟到限速。"""
from pathlib import Path
import subprocess, unittest
from test_remote_servo import PLATFORM, COMPILER
ROOT=Path(__file__).resolve().parents[2]
SCRATCH=ROOT/"tmp/autodrive/servo-tests"
class AutonomousServoTests(unittest.TestCase):
    def test_owner_and_rate(self):
        source=ROOT/"car/src/servo_control.c"
        self.assertIn("servo_control_autonomous_apply",source.read_text(encoding="utf8"),"自动舵机接口尚未实现")
        stub=SCRATCH/"include";stub.mkdir(parents=True,exist_ok=True)
        (stub/"platform.h").write_text(PLATFORM,encoding="ascii")
        for name in ("zf_driver_gpio.h","zf_driver_pwm.h"):(stub/name).write_text('#include "platform.h"\n',encoding="ascii")
        exe=SCRATCH/"servo.exe"
        args=[COMPILER,"-std=c99","-Wall","-Wextra","-Werror","-I"+str(stub),"-I"+str(ROOT/"car/include"),"-I"+str(ROOT/"car/config"),str(ROOT/"car/tests/test_autonomous_servo.c"),str(source),"-o",str(exe)]
        r=subprocess.run(args,capture_output=True,text=True,encoding="utf8",errors="replace");self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        r=subprocess.run([str(exe)],capture_output=True,text=True,encoding="utf8",errors="replace");self.assertEqual(r.returncode,0,r.stdout+r.stderr);print(r.stdout.strip())
if __name__=="__main__":unittest.main(verbosity=2)
