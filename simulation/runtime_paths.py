"""本机启动诊断与工具发现缓存的位置；正式验收输出由调用方显式指定。"""
import os
from pathlib import Path


RUNTIME = Path(os.environ.get('SIMULATION_RUNTIME_ROOT', Path(__file__).resolve().parent / 'runtime')).resolve()


def runtime_file(name):
    """创建运行目录并返回文件位置，不读取或移动历史报告。"""
    RUNTIME.mkdir(parents=True, exist_ok=True)
    return RUNTIME / name
