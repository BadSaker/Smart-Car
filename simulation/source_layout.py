"""算法唯一来源与显式实验快照；不修改主工程。"""
from pathlib import Path
import os
SOURCE_NAMES = {
    'src': ('track_vision.c', 'autonomous_drive.c', 'control_pid.c'),
    'include': ('track_vision.h', 'autonomous_drive.h', 'control_pid.h'),
    'config': (
        'autonomous_config.h', 'vision_config.h', 'encoder_config.h',
        'vehicle_geometry.h', 'servo_config.h',
    ),
}


# 显式实验来源仍须包含全部11份文件，不能回退到vendor算法副本。
def source_root():
    root = Path(os.environ.get('SIMULATION_SOURCE_ROOT', Path(__file__).resolve().parents[1] / 'car')).resolve()
    for category, names in SOURCE_NAMES.items():
        for name in names:
            if not (root / category / name).is_file():
                raise RuntimeError(f'算法来源缺少文件：{category}/{name}')
    return root

def source_files():
    root = source_root()
    return [root / category / name for category, names in SOURCE_NAMES.items() for name in names]
