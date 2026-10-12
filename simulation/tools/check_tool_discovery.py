"""Check bitness selection, cached DLL independence and fresh compilation."""
import sys,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from parameters import ROOT,defaults
import toolchain,native
report={}
compiler=toolchain.find_compiler()
report['compiler']=str(compiler)
original=native.find_compiler
def unavailable():raise AssertionError('A cached DLL must not look for a compiler')
try:
    native.find_compiler=unavailable
    cached=native.Native(defaults())
    report['cached_dll_without_compiler']=cached.version
finally:native.find_compiler=original
p=defaults();p['kp']=28.01234567
built=native.Native(p)
report['new_parameters_compile']=built.version
assert (ROOT/'build'/built.version/'compiler.json').is_file()
assert built.data()[0].shape==(22,)
(ROOT/'reports/tool-discovery-check.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
