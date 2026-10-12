"""主机回归编译器：显式环境变量优先，随后复用仿真工具发现。"""
from pathlib import Path
import json
import os
import shutil

def find_compiler():
    explicit=os.environ.get('AUTOCAR_HOST_CC')
    if explicit:
        path=Path(os.path.expandvars(explicit.strip('"')))
        if not path.is_file():raise RuntimeError('AUTOCAR_HOST_CC指定的文件不存在')
        return str(path)
    root=Path(__file__).resolve().parents[2]
    settings=root/'car/scripts/tool_paths.local.json'
    if settings.is_file():
        value=json.loads(settings.read_text(encoding='utf-8')).get('Gcc')
        if value:
            path=Path(os.path.expandvars(value))
            if not path.is_file():raise RuntimeError('本地Gcc配置文件不存在')
            return str(path)
    local=root/'simulation/toolchain/mingw64/bin/gcc.exe'
    if local.is_file():return str(local)
    found=shutil.which('gcc')
    if found:return found
    for drive in 'CDEFGHIJKLMNOPQRSTUVWXYZ':
        for pattern in ('Xilinx/Vivado/*/tps/mingw/*/win64.o/nt/bin/gcc.exe','msys64/ucrt64/bin/gcc.exe'):
            for path in sorted(Path(drive+':/').glob(pattern),reverse=True):
                if path.is_file():return str(path)
    raise RuntimeError('未找到Windows GCC；设置AUTOCAR_HOST_CC')
