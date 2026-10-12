"""Validate the project-local GCC against the previous default algorithm DLL."""
import ctypes as C,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
import app  # Keep the same small-array CPU library settings as the simulator.
import numpy as np
import native
from engine import Simulation
from parameters import ROOT,defaults
from toolchain import find_compiler
compiler=find_compiler()
assert compiler==ROOT/'toolchain/mingw64/bin/gcc.exe'
old=C.CDLL(str(ROOT/'build/b09bee4813ed377a/algorithms.dll'))
report={'compiler':str(compiler),'cases':[]}
for scenario,direction,steps in [('full','A→B',7000),('full','B→A',7000),('straight','A→B',1000),('circle','A→B',1000)]:
    p=defaults();p.update(scenario=scenario,direction=direction)
    sim=Simulation(p);n=sim.native
    assert old._handle!=n.lib._handle
    for name in ('sim_reset','sim_vision','sim_tick','sim_data'):
        getattr(old,name).argtypes=getattr(n.lib,name).argtypes
        getattr(old,name).restype=getattr(n.lib,name).restype
    old.sim_reset()
    output=(C.c_float*22)();rows=(C.c_uint8*480)()
    old.sim_vision(sim.image.ctypes.data_as(C.POINTER(C.c_uint8)),sim.now,round(sim.distance*1000))
    newvision=n.vision;newtick=n.tick;newdata=n.data
    state={'samples':0,'max_float_difference':0.}
    def vision(image,now,distance):
        newvision(image,now,distance)
        old.sim_vision(image.ctypes.data_as(C.POINTER(C.c_uint8)),now,distance)
    def tick(*args):newtick(*args);old.sim_tick(*args)
    def data():
        current,current_rows=newdata();old.sim_data(output,rows)
        previous=np.asarray(output,dtype=float);previous_rows=np.asarray(rows,dtype=np.uint8).reshape(4,120)
        assert np.array_equal(current_rows,previous_rows),'Vision row mismatch'
        error=float(np.max(np.abs(current-previous)))
        state['max_float_difference']=max(state['max_float_difference'],error)
        assert np.array_equal(current,previous),(scenario,direction,sim.now,error)
        state['samples']+=1
        return current,current_rows
    n.vision=vision;n.tick=tick;n.data=data;data();sim.started=True
    for _ in range(steps):sim.tick()
    report['cases'].append({'scenario':scenario,'direction':direction,'steps':steps,**state})
original=native.find_compiler
try:
    def unavailable():raise AssertionError('Cached DLL unexpectedly requested GCC')
    native.find_compiler=unavailable
    cached=native.Native(defaults());report['cached_load_without_compiler']=cached.version
finally:native.find_compiler=original
p=defaults();p['kp']=28.02345678
fresh=native.Native(p);info=json.loads((ROOT/'build'/fresh.version/'compiler.json').read_text())
assert Path(info['path'])==compiler
report['new_parameters_compile']=fresh.version
report['passed']=True
(ROOT/'reports/independent-gcc-check.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False,indent=2))
