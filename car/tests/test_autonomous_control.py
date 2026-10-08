"""真实自动驾驶适配、状态机、PID、电机和舵机的联合测试，替换硬件边界。"""
from pathlib import Path
import subprocess,unittest,os
ROOT=Path(__file__).resolve().parents[2]
SCRATCH=ROOT/"tmp/autodrive/integration-tests"
COMPILER=os.environ.get("AUTOCAR_HOST_CC",r"D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe")
PLATFORM="""#ifndef AUTO_PLATFORM_H
#define AUTO_PLATFORM_H
#include <stdint.h>
typedef enum { C0,C1,C2,C6,C7,C8,C9,C12,C14,C15,C24,C30 } gpio_pin_enum;
typedef enum {GPI,GPO} gpio_dir_enum;
typedef enum {PWM2_MODULE0_CHA_C6,PWM2_MODULE1_CHA_C8,PWM4_MODULE2_CHA_C30} pwm_channel_enum;
#define GPIO_LOW 0U
#define GPIO_HIGH 1U
#define GPI_PULL_UP 22U
#define GPO_PUSH_PULL 33U
#define PWM_DUTY_MAX 10000U
void gpio_init(gpio_pin_enum,gpio_dir_enum,uint8_t,uint32_t);
uint8_t gpio_get_level(gpio_pin_enum);
void gpio_set_level(gpio_pin_enum,uint8_t);
void pwm_init(pwm_channel_enum,uint32_t,uint32_t);
void pwm_set_duty(pwm_channel_enum,uint32_t);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
#endif
"""
class AutonomousControlTests(unittest.TestCase):
    def test_hardware_boundary_integration(self):
        source=ROOT/"car/src/autonomous_control.c"
        self.assertTrue(source.exists(),"自动驾驶硬件适配尚未实现")
        stub=SCRATCH/"include";stub.mkdir(parents=True,exist_ok=True)
        (stub/"platform.h").write_text(PLATFORM,encoding="ascii")
        for name in ("zf_driver_gpio.h","zf_driver_pwm.h","zf_driver_encoder.h","fsl_common.h"):
            (stub/name).write_text('#include "platform.h"\n',encoding="ascii")
        exe=SCRATCH/"integrated.exe"
        names=("autonomous_control","autonomous_drive","control_pid","motor_control","servo_control","track_vision")
        cmd=[COMPILER,"-std=c99","-Wall","-Wextra","-Werror","-O2","-I"+str(stub),"-I"+str(ROOT/"car/include"),"-I"+str(ROOT/"car/config"),str(ROOT/"car/tests/test_autonomous_control.c")]
        cmd += [str(ROOT/"car/src"/(name+".c")) for name in names]+["-lm","-o",str(exe)]
        r=subprocess.run(cmd,capture_output=True,text=True,encoding="utf8",errors="replace");self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        r=subprocess.run([str(exe)],capture_output=True,text=True,encoding="utf8",errors="replace");self.assertEqual(r.returncode,0,r.stdout+r.stderr);print(r.stdout.strip())
        cmd[cmd.index(str(ROOT/"car/tests/test_autonomous_control.c"))]=str(ROOT/"car/tests/test_servo_service_race.c")
        r=subprocess.run(cmd,capture_output=True,text=True,encoding="utf8",errors="replace");self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        for scenario in ("stop","start"):
            r=subprocess.run([str(exe),scenario],capture_output=True,text=True,encoding="utf8",errors="replace");self.assertEqual(r.returncode,0,r.stdout+r.stderr);print(r.stdout.strip())
if __name__=="__main__":unittest.main(verbosity=2)
