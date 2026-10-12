"""Locate a Windows x64 GCC without binding to a username or drive letter."""
import os, shutil, subprocess, json
from pathlib import Path
from functools import lru_cache
from parameters import ROOT

def fixed_drives():
    if os.name!='nt':return []
    import ctypes
    api=ctypes.windll.kernel32
    bits=api.GetLogicalDrives()
    return [Path(f'{chr(65+i)}:/') for i in range(26)
            if bits&(1<<i) and api.GetDriveTypeW(f'{chr(65+i)}:\\')==3]

def candidates():
    explicit=os.environ.get('SIMULATION_CC')
    if explicit:yield Path(os.path.expandvars(explicit.strip('"')))
    for relative in ('toolchain/mingw64/bin/gcc.exe','toolchain/bin/gcc.exe','toolchain/MinGW/bin/gcc.exe'):
        yield ROOT/relative
    found=shutil.which('gcc')
    if found:yield Path(found)
    # Bounded checks of known toolchain layouts, never a recursive disk scan.
    for drive in fixed_drives():
        for relative in ('msys64/ucrt64/bin/gcc.exe','msys64/mingw64/bin/gcc.exe','mingw64/bin/gcc.exe','MinGW/bin/gcc.exe'):
            yield drive/relative
    # Existing installations remain a last-resort source, without fixed drives
    # or versions. A local independent compiler takes precedence.
    for drive in fixed_drives():
        for pattern in ('Xilinx/Vivado/*/tps/mingw/*/win64.o/nt/bin/gcc.exe',
                        'AMD/Vivado/*/tps/mingw/*/win64.o/nt/bin/gcc.exe'):
            yield from sorted(drive.glob(pattern),reverse=True)

@lru_cache(maxsize=1)
def find_compiler():
    seen=set();attempts=[]
    explicit=os.environ.get('SIMULATION_CC')
    if explicit and not Path(os.path.expandvars(explicit.strip('"'))).is_file():
        raise RuntimeError('SIMULATION_CC 指定的编译器文件不存在。')
    for candidate in candidates():
        key=str(candidate).casefold()
        if key in seen or not candidate.is_file():continue
        seen.add(key)
        try:
            result=subprocess.run([str(candidate),'-dumpmachine'],capture_output=True,text=True,
                                  timeout=5,creationflags=0x08000000 if os.name=='nt' else 0)
            target=result.stdout.strip().lower()
            if result.returncode or 'x86_64' not in target or not any(x in target for x in ('mingw','windows','win32')):
                raise RuntimeError(f'需要 Windows x64 GCC，当前目标为 {target or "未知"}')
            attempts.append({'path':str(candidate),'accepted':True,'target':target})
            (ROOT/'reports').mkdir(exist_ok=True)
            (ROOT/'reports/compiler-discovery.json').write_text(json.dumps(attempts,ensure_ascii=False,indent=2),encoding='utf-8')
            return candidate.resolve()
        except (OSError,subprocess.SubprocessError,RuntimeError) as error:
            attempts.append({'path':str(candidate),'accepted':False,'reason':str(error)})
            if os.environ.get('SIMULATION_CC') and key==str(Path(os.path.expandvars(os.environ['SIMULATION_CC'].strip('"')))).casefold():
                raise RuntimeError(f'指定的 SIMULATION_CC 无法使用：{error}') from error
    (ROOT/'reports').mkdir(exist_ok=True)
    (ROOT/'reports/compiler-discovery.json').write_text(json.dumps(attempts,ensure_ascii=False,indent=2),encoding='utf-8')
    raise RuntimeError('未找到可用的64位GCC。请在 simulation/toolchain/mingw64 放置完整工具链，或设置 SIMULATION_CC。已有匹配DLL时无需编译器。')
