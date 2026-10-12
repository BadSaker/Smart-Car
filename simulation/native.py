"""Compile only simulation-owned copies, and expose the original C algorithms."""
import ctypes as C, hashlib, json, os, re, subprocess
from pathlib import Path
from parameters import ROOT
from toolchain import find_compiler
MACROS={'cruise':'AUTO_CRUISE_SPEED_MM_S','curve_speed':'AUTO_CURVE_SPEED_MM_S',
 'outer_speed':'AUTO_CURVE_OUTER_SPEED_MM_S','kp':'AUTO_STEERING_KP','ki':'AUTO_STEERING_KI',
 'kd':'AUTO_STEERING_KD','speed_kp':'AUTO_SPEED_KP','speed_ki':'AUTO_SPEED_KI',
 'min_q':'AUTO_MIN_CONFIDENCE','outer_q':'AUTO_CURVE_OUTER_MIN_CONFIDENCE',
 'width_top':'VISION_WIDTH_TOP_PX','width_bottom':'VISION_WIDTH_BOTTOM_PX',
 'outer_enabled':'VISION_CURVE_OUTER_ENABLED'}
INTS={'min_q','outer_q','width_top','width_bottom','outer_enabled'}
COMPILE_OPTIONS=['-std=c99','-shared','-O2','-Wall','-Wextra','-Werror','-static-libgcc']
class Native:
    def __init__(self,p):
        signature={k:p[k] for k in MACROS}
        inputs=[ROOT/'native_bridge.c',*sorted((ROOT/'vendor/src').glob('*.c')),
                *sorted((ROOT/'vendor/include').glob('*.h')),*sorted((ROOT/'vendor/config').glob('*.h'))]
        h=hashlib.sha256(json.dumps(signature,sort_keys=True).encode())
        for f in inputs:h.update(f.read_bytes())
        # The portable toolchain manifest identifies the verified distribution.
        # It remains readable without launching GCC when a matching DLL exists.
        identity=ROOT/'toolchain/deployment.json'
        if identity.is_file():
            h.update(identity.read_bytes())
            h.update(json.dumps(COMPILE_OPTIONS).encode())
        self.version=h.hexdigest()[:16];build=ROOT/'build'/self.version;build.mkdir(parents=True,exist_ok=True)
        dll=build/'algorithms.dll'
        compiler=None
        if not dll.exists():
            compiler=find_compiler()
            for name in ('autonomous_config.h','vision_config.h'):
                text=(ROOT/'vendor/config'/name).read_text(encoding='utf-8')
                for key,macro in MACROS.items():
                    value=f'{int(p[key])}U' if key in INTS else f'{float(p[key]):.8f}f'
                    text=re.sub(r'(^#define\s+'+macro+r'\s+)[^\r\n]+',lambda m:m[1]+value,text,flags=re.M)
                (build/name).write_text(text,encoding='utf-8')
            cmd=[str(compiler),*COMPILE_OPTIONS,
                 '-I'+str(build),'-I'+str(ROOT/'vendor/include'),str(ROOT/'native_bridge.c')]
            cmd += [str(ROOT/'vendor/src'/n) for n in ('track_vision.c','autonomous_drive.c','control_pid.c')]
            cmd += ['-lm','-o',str(dll)]
            r=subprocess.run(cmd,capture_output=True,text=True,encoding='utf-8',errors='replace',creationflags=0x08000000)
            (build/'compile.log').write_text(r.stdout+r.stderr,encoding='utf-8')
            (build/'parameters.json').write_text(json.dumps(signature,indent=2),encoding='utf-8')
            compiler_info={'path':str(compiler),'options':COMPILE_OPTIONS}
            if identity.is_file():compiler_info['distribution']=json.loads(identity.read_text(encoding='utf-8'))
            (build/'compiler.json').write_text(json.dumps(compiler_info,indent=2),encoding='utf-8')
            if r.returncode:raise RuntimeError('C算法编译失败：'+r.stderr)
        # Loading a cached DLL does not require a compiler installation.
        self.dll_dirs=[]
        if hasattr(os,'add_dll_directory'):
            for directory in [dll.parent,ROOT/'toolchain/mingw64/bin',*([compiler.parent] if compiler else [])]:
                if directory.is_dir():self.dll_dirs.append(os.add_dll_directory(str(directory)))
        self.lib=C.CDLL(str(dll));self.out=(C.c_float*22)();self.rows=(C.c_uint8*480)()
        self.lib.sim_reset.argtypes=[];self.lib.sim_reset.restype=None
        self.lib.sim_vision.argtypes=[C.POINTER(C.c_uint8),C.c_uint32,C.c_uint32];self.lib.sim_vision.restype=None
        self.lib.sim_tick.argtypes=[C.c_uint32,*([C.c_int]*6)];self.lib.sim_tick.restype=None
        self.lib.sim_data.argtypes=[C.POINTER(C.c_float),C.POINTER(C.c_uint8)];self.lib.sim_data.restype=None
        self.lib.sim_reset()
    def vision(self,image,now,distance):
        import numpy as np
        image=np.ascontiguousarray(image,dtype=np.uint8)
        if image.shape!=(120,188):raise ValueError('原始灰度帧必须为188×120')
        self.lib.sim_vision(image.ctypes.data_as(C.POINTER(C.c_uint8)),now,distance)
    def tick(self,now,fresh,start,stop,left,right,step):
        self.lib.sim_tick(now,int(fresh),int(start),int(stop),int(left),int(right),int(step))
    def data(self):
        import numpy as np
        self.lib.sim_data(self.out,self.rows)
        return np.array(self.out,dtype=float),np.array(self.rows,dtype=np.uint8).reshape(4,120)
