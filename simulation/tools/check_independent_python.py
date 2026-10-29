"""Exercise the real GUI through launch.ps1 and record loaded Windows modules."""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import sys
import traceback

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from runtime_paths import runtime_file
from runtime_check import check


def loaded_modules():
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    psapi.EnumProcessModules.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE), wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    psapi.GetModuleFileNameExW.argtypes = [wintypes.HANDLE, wintypes.HMODULE, wintypes.LPWSTR, wintypes.DWORD]
    process = kernel.GetCurrentProcess()
    handles = (wintypes.HMODULE * 2048)()
    needed = wintypes.DWORD()
    if not psapi.EnumProcessModules(process, handles, ctypes.sizeof(handles), ctypes.byref(needed)):
        raise ctypes.WinError(ctypes.get_last_error())
    if needed.value > ctypes.sizeof(handles):
        raise RuntimeError('Loaded module buffer too small')
    paths = []
    for handle in handles[:needed.value // ctypes.sizeof(wintypes.HMODULE)]:
        buffer = ctypes.create_unicode_buffer(32768)
        if not psapi.GetModuleFileNameExW(process, handle, buffer, len(buffer)):
            raise ctypes.WinError(ctypes.get_last_error())
        paths.append(buffer.value)
    return paths


def main():
    result = {'passed': False}
    app = None
    try:
        result.update(check())
        import tkinter as tk
        from app import App, desktop_dpi
        desktop_dpi()
        root = tk.Tk()
        app = App(root)
        root.update()
        assert root.winfo_viewable()
        result['tk_library'] = root.tk.eval('set tk_library')
        assert Path(result['tk_library']).resolve().is_relative_to(ROOT / 'python')
        result['algorithm_version'] = app.sim.native.version
        result['loaded_modules'] = loaded_modules()
        forbidden = [p for p in result['loaded_modules'] if 'anaconda' in p.lower() or 'miniconda' in p.lower()]
        assert not forbidden, forbidden
        runtime_dlls = [p for p in result['loaded_modules'] if Path(p).name.lower().startswith(('python3', 'tcl86', 'tk86'))]
        assert len(runtime_dlls) >= 3, runtime_dlls
        assert all(Path(p).resolve().is_relative_to(ROOT / 'python') for p in runtime_dlls), runtime_dlls
        result['passed'] = True
    except Exception:
        result['error'] = traceback.format_exc()
        raise
    finally:
        if app is not None:
            app.close()
        runtime_file('independent-python-check.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
