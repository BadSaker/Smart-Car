"""在界面异常处理器建立前记录启动错误；诊断仅写本机运行目录。"""
from pathlib import Path
import traceback
import ctypes
import os,time
ROOT=Path(__file__).resolve().parent
from runtime_paths import RUNTIME, runtime_file
def main():
    try:
        os.environ['SIMULATION_START_CLOCK']=str(time.perf_counter())
        RUNTIME.mkdir(parents=True,exist_ok=True)
        (runtime_file('launcher-pid.txt')).write_text(str(os.getpid()),encoding='ascii')
        import sys
        (runtime_file('launcher.log')).write_text('Started simulator. PID='+str(os.getpid())+'; Python='+sys.executable,encoding='utf-8')
        from app import main as run_app
        run_app()
    except Exception:
        log=runtime_file('startup_error.log')
        log.parent.mkdir(parents=True,exist_ok=True)
        log.write_text(traceback.format_exc(),encoding='utf-8')
        ctypes.windll.user32.MessageBoxW(None,'仿真启动失败。详细原因已保存到：\n'+str(log),'启动失败',0x10)
        raise
if __name__=='__main__':main()
