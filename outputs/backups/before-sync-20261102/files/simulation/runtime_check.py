"""Validate real imports and Tcl against the pinned, project-local runtime."""
import importlib
import importlib.metadata
import json
from pathlib import Path
import site
import struct
import sys

ROOT = Path(__file__).resolve().parent


def check():
    local = (ROOT / 'python').resolve()
    manifest = json.loads((ROOT / 'python-runtime.json').read_text(encoding='utf-8'))

    def require_local(value):
        path = Path(value).resolve()
        if not path.is_relative_to(local):
            raise RuntimeError(f'External Python dependency: {path}')
        return str(path)

    executable = require_local(sys.executable)
    require_local(sys.base_prefix)
    if sys.version.split()[0] != manifest['version'] or struct.calcsize('P') != 8:
        raise RuntimeError('Python version/architecture differs from python-runtime.json')
    if site.ENABLE_USER_SITE:
        raise RuntimeError('User site-packages must be disabled')
    modules = {}
    for name in ('encodings', 'ctypes', 'tkinter', '_tkinter', 'numpy', 'numpy._core._multiarray_umath', 'PIL', 'PIL._imaging', 'PIL.ImageTk'):
        modules[name] = require_local(importlib.import_module(name).__file__)
    versions = {name: importlib.metadata.version(name) for name in manifest['packages']}
    if versions != manifest['packages']:
        raise RuntimeError(f'Dependency versions differ: {versions}')
    import tkinter
    tcl = tkinter.Tcl()
    tcl_library = require_local(tcl.eval('info library'))
    return dict(executable=executable, version=sys.version.split()[0], bits=64,
                versions=versions, modules=modules, tcl_library=tcl_library,
                tcl_version=tcl.eval('info patchlevel'), user_site_enabled=site.ENABLE_USER_SITE,
                sys_path=sys.path)


if __name__ == '__main__':
    print(json.dumps(check(), ensure_ascii=True, indent=2))
