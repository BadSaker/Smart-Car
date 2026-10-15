"""读取主工程算法，参数覆盖与DLL仅写入独立缓存。"""
import ctypes as C, hashlib, json, os, re, subprocess, shutil
from pathlib import Path
from parameters import ROOT
from toolchain import find_compiler,candidates
from source_layout import source_root,source_files
MACROS={'cruise':'AUTO_CRUISE_SPEED_MM_S','curve_speed':'AUTO_CURVE_SPEED_MM_S',
 'element_speed':'AUTO_ELEMENT_SPEED_MM_S','max_duty':'AUTO_MAX_DUTY_PERMILLE','speed_guard':'AUTO_MAX_MEASURED_SPEED_MM_S',
 'outer_speed':'AUTO_CURVE_OUTER_SPEED_MM_S','kp':'AUTO_STEERING_KP','ki':'AUTO_STEERING_KI',
 'kd':'AUTO_STEERING_KD','speed_kp':'AUTO_SPEED_KP','speed_ki':'AUTO_SPEED_KI',
 'min_q':'AUTO_MIN_CONFIDENCE','outer_q':'AUTO_CURVE_OUTER_MIN_CONFIDENCE',
 'width_top':'VISION_WIDTH_TOP_PX','width_bottom':'VISION_WIDTH_BOTTOM_PX',
 'outer_enabled':'VISION_CURVE_OUTER_ENABLED'}
MACROS['servo_step']='AUTO_SERVO_STEP_US'
MACROS.update(servo_center='SERVO_REFERENCE_CENTER_US',servo_range='SERVO_AUTONOMOUS_EXCURSION_US',
 speed_ff='AUTO_SPEED_FEEDFORWARD',speed_tracking='AUTO_SPEED_TRACKING_GAIN',accel='AUTO_ACCEL_MM_S2',decel='AUTO_DECEL_MM_S2',
 steering_rate='AUTO_STEERING_RATE_PERMILLE_S',steering_accel='AUTO_STEERING_ACCEL_PERMILLE_S2',lateral_accel='AUTO_LATERAL_ACCEL_MM_S2')
INTS={'min_q','outer_q','width_top','width_bottom','outer_enabled','max_duty','speed_guard','servo_step','servo_center','servo_range'}
MOTION_KEYS=('requested_steering_permille','steering_rate_permille_s','speed_limit_mm_s','left_target_mm_s','right_target_mm_s',
 'left_filtered_mm_s','right_filtered_mm_s','left_pid_request','right_pid_request','left_integral','right_integral','maximum_turn_ratio')
FEATURE_KEYS=('ring_side','raw_near_px','raw_far_px','left_open_rows','right_open_rows',
 'joint_open_rows','left_fork','right_fork','left_open_bottom','right_open_bottom',
 'near_supported','outer_near_rows','outer_far_rows','outer_reject','element_bridged',
 'ring_left_reject','ring_right_reject','left_island','right_island','ring_entry_supported','fault_element',
 'element_entry_ms','element_entry_mm','element_elapsed_ms','element_traveled_mm','recovery_frames','exit_evidence',
 'limit_reason','fault_entry_ms','fault_entry_mm','fault_elapsed_ms','fault_traveled_mm','history_count')+tuple(
 f'transition_{k}_{field}' for k in range(8) for field in ('ms','mm','from','to'))+(
 'observation_confidence','preview_first_row','preview_last_row','geometry_valid',
 'geometry_right_mm','geometry_forward_mm','geometry_fit_rms_mm','geometry_model',
 'geometry_points','geometry_visible_samples','geometry_inferred_samples','geometry_reject')
FLOAT_FEATURES={'geometry_right_mm','geometry_forward_mm','geometry_fit_rms_mm'}
COMPILE_OPTIONS=['-std=c99','-shared','-O2','-Wall','-Wextra','-Werror','-static-libgcc']
class Native:
    def __init__(self,p):
        signature={k:p[k] for k in MACROS}
        source=source_root()
        inputs=[ROOT/'native_bridge.c',*source_files()]
        h=hashlib.sha256(json.dumps(signature,sort_keys=True).encode())
        h.update(json.dumps(COMPILE_OPTIONS).encode())
        h.update(b'relative-build-v1')
        for f in inputs:
            h.update(f.name.encode());h.update(b'\0');h.update(f.read_bytes())
        # The portable toolchain manifest identifies the verified distribution.
        # It remains readable without launching GCC when a matching DLL exists.
        identity=ROOT/'toolchain/deployment.json'
        if identity.is_file():
            h.update(identity.read_bytes())
        request=h.hexdigest()
        request_hash=h.copy()
        candidate=next((p for p in candidates() if p.is_file()),None)
        explicit=os.environ.get('SIMULATION_CC')
        if explicit and not Path(os.path.expandvars(explicit.strip('"'))).is_file():raise RuntimeError('SIMULATION_CC指定的文件不存在')
        compiler_hash=hashlib.sha256(candidate.read_bytes()).hexdigest() if candidate else None
        index=ROOT/'build/index'/f'{request}.json'
        if compiler_hash:
            h.update(compiler_hash.encode());self.version=h.hexdigest()[:16]
        elif index.is_file():self.version=json.loads(index.read_text(encoding='utf-8'))['version']
        else:raise RuntimeError('无匹配缓存；需要Windows x64 GCC')
        build=ROOT/'build'/self.version;build.mkdir(parents=True,exist_ok=True)
        dll=build/'algorithms.dll'
        compiler=None
        if not dll.exists():
            compiler=find_compiler()
            actual_hash=hashlib.sha256(compiler.read_bytes()).hexdigest()
            if actual_hash!=compiler_hash:
                h=request_hash.copy();h.update(actual_hash.encode())
                self.version=h.hexdigest()[:16];build=ROOT/'build'/self.version
                build.mkdir(parents=True,exist_ok=True);dll=build/'algorithms.dll'
            for name in ('autonomous_config.h','vision_config.h','servo_config.h'):
                text=(source/'config'/name).read_text(encoding='utf-8')
                for key,macro in MACROS.items():
                    owner='vision_config.h' if key.startswith('width_') or key=='outer_enabled' else (
                        'servo_config.h' if key in ('servo_center','servo_range') else 'autonomous_config.h')
                    if owner!=name:continue
                    value=str(int(p[key])) if key=='speed_guard' else (f'{int(p[key])}U' if key in INTS else f'{float(p[key]):.9e}f')
                    text,count=re.subn(r'(^#define\s+'+macro+r'\s+)[^\r\n]+',lambda m:m[1]+value,text,flags=re.M)
                    if count!=1:raise RuntimeError(f'配置宏必须唯一：{macro}，实际匹配{count}')
                (build/name).write_text(text,encoding='utf-8')
            # GCC/ld命令只传ASCII相对文件名，支持含中文的项目工作目录。
            shutil.copy2(ROOT/'native_bridge.c',build/'native_bridge.c')
            for file in source_files():
                target=build/'source'/file.relative_to(source)
                target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,target)
            cmd=[str(compiler),*COMPILE_OPTIONS,'-I.','-Isource/include','-Isource/config','native_bridge.c']
            cmd += ['source/src/'+n for n in ('track_vision.c','autonomous_drive.c','control_pid.c')]
            cmd += ['-lm','-o','algorithms.pending.dll']
            r=subprocess.run(cmd,cwd=build,capture_output=True,text=True,encoding='utf-8',errors='replace',creationflags=0x08000000)
            (build/'compile.log').write_text(r.stdout+r.stderr,encoding='utf-8')
            (build/'parameters.json').write_text(json.dumps(signature,indent=2),encoding='utf-8')
            compiler_info={'path':str(compiler),'sha256':hashlib.sha256(compiler.read_bytes()).hexdigest(),'options':COMPILE_OPTIONS}
            if identity.is_file() and compiler.resolve()==(ROOT/'toolchain/mingw64/bin/gcc.exe').resolve():compiler_info['distribution']=json.loads(identity.read_text(encoding='utf-8'))
            (build/'compiler.json').write_text(json.dumps(compiler_info,indent=2),encoding='utf-8')
            if r.returncode:raise RuntimeError('C算法编译失败：'+r.stderr)
            (build/'algorithms.pending.dll').replace(dll)
            (build/'source.json').write_text(json.dumps({str(f.relative_to(source)):hashlib.sha256(f.read_bytes()).hexdigest() for f in source_files()},indent=2),encoding='utf-8')
        index.parent.mkdir(parents=True,exist_ok=True)
        index.write_text(json.dumps({'version':self.version}),encoding='utf-8')
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
        self.feature_values=(C.c_float*len(FEATURE_KEYS))();self.features={key:0 for key in FEATURE_KEYS}
        self.lib.sim_features.argtypes=[C.POINTER(C.c_float)];self.lib.sim_features.restype=None
        self.motion_values=(C.c_float*len(MOTION_KEYS))();self.motion={key:0. for key in MOTION_KEYS}
        self.lib.sim_motion.argtypes=[C.POINTER(C.c_float)];self.lib.sim_motion.restype=None
        self.lib.sim_camera.argtypes=[C.POINTER(C.c_uint8),C.c_uint,C.c_uint,C.POINTER(C.c_double),
            C.POINTER(C.c_double),C.POINTER(C.c_uint8),C.c_uint,C.POINTER(C.c_double),C.POINTER(C.c_uint8)]
        self.lib.sim_camera.restype=None
        self.lib.sim_reset()
    def camera(self,raster,rays,x,y,c,s,p,minimum,cell):
        import numpy as np
        if getattr(self,'_camera_rays_source',None) is not rays:
            self._camera_rays_source=rays
            self._camera_rays=(np.ascontiguousarray(rays[0],dtype=np.float64),
                np.ascontiguousarray(rays[1],dtype=np.float64),np.ascontiguousarray(rays[2],dtype=np.uint8))
        if getattr(self,'_camera_raster_id',None)!=id(raster):
            self._camera_raster_id=id(raster);self._camera_raster=np.ascontiguousarray(raster,dtype=np.uint8)
        longitudinal,right,valid=self._camera_rays
        values=(C.c_double*10)(x,y,c,s,p['camera_offset'],p['camera_lateral'],minimum[0],minimum[1],cell,p['gray_background'])
        image=np.empty((120,188),dtype=np.uint8)
        self.lib.sim_camera(self._camera_raster.ctypes.data_as(C.POINTER(C.c_uint8)),raster.shape[1],raster.shape[0],
            longitudinal.ctypes.data_as(C.POINTER(C.c_double)),right.ctypes.data_as(C.POINTER(C.c_double)),
            valid.ctypes.data_as(C.POINTER(C.c_uint8)),longitudinal.size//(188*120),values,
            image.ctypes.data_as(C.POINTER(C.c_uint8)))
        return image
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
        self.lib.sim_features(self.feature_values)
        self.lib.sim_motion(self.motion_values)
        self.motion=dict(zip(MOTION_KEYS,map(float,self.motion_values)))
        self.features={key:float(value) if key in FLOAT_FEATURES else int(value) for key,value in zip(FEATURE_KEYS,self.feature_values)}
        return np.array(self.out,dtype=float),np.array(self.rows,dtype=np.uint8).reshape(4,120)
