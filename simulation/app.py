import json, math, os, time, traceback, re
import copy
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
# This GUI uses small matrix batches; worker-pool overhead can stall repainting.
os.environ['OPENBLAS_NUM_THREADS']='1'
os.environ['OMP_NUM_THREADS']='1'
import tkinter as tk
from tkinter import ttk,messagebox,filedialog,simpledialog
from PIL import Image,ImageTk
import numpy as np
from parameters import ROOT,defaults,validate,save,load
from engine import Simulation,STATES,FAULTS,ELEMENTS,REASONS
from render import map_image,scene_image,camera_image,chart_image
from comparison import Comparison
from calibration import ROWS, references, comparison as width_comparison, fit as fit_six_rows
from perturbation import PRESETS, KEYS as DISTURBANCE_KEYS

SAMPLING_OPTIONS={'历史最近邻':(1,'nearest'),'最近邻 2×2':(2,'nearest'),
    '最近邻 3×3':(3,'nearest'),'单点双线性':(1,'bilinear'),
    '平滑 2×2':(2,'bilinear'),'平滑 3×3':(3,'bilinear')}


def sampling_label(p):
    value=(p.get('camera_samples',1),p.get('camera_sampling','nearest'))
    return next(label for label,option in SAMPLING_OPTIONS.items() if option==value)

def desktop_dpi():
    if os.name=='nt':
        import ctypes
        try:ctypes.windll.shcore.SetProcessDpiAwareness(2)
        except Exception:pass

def window_handle(root):
    import ctypes
    api=ctypes.windll.user32.GetAncestor
    api.argtypes=[ctypes.c_void_p,ctypes.c_uint];api.restype=ctypes.c_void_p
    return api(root.winfo_id(),2)

def disable_window_transitions(root):
    if os.name!='nt':return False
    import ctypes
    disabled=ctypes.c_int(1);api=ctypes.windll.dwmapi.DwmSetWindowAttribute
    api.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint]
    return api(window_handle(root),3,ctypes.byref(disabled),ctypes.sizeof(disabled))==0

FIELDS={
 '车辆':[('length','整车总长 m〔记录〕'),('wheelbase','轴距 m'),('track','轮胎中心距 m'),('body_width','底板最大宽 m〔近似〕'),
 ('tire_width','轮胎宽 m'),('wheel_diameter','轮径 m'),('steer_max_deg','满幅前轮转角 °〔假设〕'),
 ('servo_center','舵机中位 µs'),('servo_range','舵机摆幅 ±µs'),('servo_step','命令步进 µs'),
 ('servo_interval','步进间隔 s'),('servo_tau','机械响应 s〔假设〕'),('motor_gain','电机增益 mm/s/‰〔假设〕'),
 ('motor_deadzone','起转死区 ‰〔假设〕'),('motor_tau','电机响应 s〔假设〕'),('coast_tau','滑行响应 s〔假设〕')],
 '车模':[('front_overhang','前轴至底板前缘 m'),('rear_overhang','后轴至底板后缘 m'),
 ('plate_height','底板底面离地 m〔近似〕'),('plate_thickness','底板厚 m〔近似〕'),('camera_mount_gap','镜头至竖杆间距 m〔近似〕')],
 '相机':[('height','镜头高度 m'),('camera_offset','相机在后轴前 m'),('pitch','向下俯角 °'),
 ('hfov','有效水平视场 °'),('fy_ratio','fy/fx 缩放比〔假设〕'),('cx','主点列 px'),('cy','主点行 px'),('k1','角点径向比例〔模型〕'),
 ('fps','相机帧率 Hz'),('gray_road','道路灰度 0–255'),('gray_edge','边线灰度 0–255'),
 ('gray_background','背景灰度 0–255'),('camera_lateral','镜头左偏 m'),('camera_yaw','镜头左偏航 °'),('camera_roll','镜头横滚 °')],
 '控制':[('cruise','直道目标 mm/s'),('curve_speed','弯道目标 mm/s'),('outer_speed','外侧目标 mm/s'),
 ('kp','转向 Kp'),('ki','转向 Ki'),('kd','转向 Kd'),('speed_kp','速度 Kp'),('speed_ki','速度 Ki'),
 ('min_q','普通Q门限'),('outer_q','外侧Q门限'),('outer_enabled','外侧补线 0/1'),
 ('width_top','第30行标定宽 px'),('width_bottom','第116行标定宽 px')],
 '场景':[('road_width','道路内沿宽 m'),('edge_width','黑边宽 m〔假设〕'),('radius','圆弧中心半径 m'),
 ('offset','初始左偏 m'),('heading_error','初始左偏航 °')],
 '扰动':[('disturbance_level','扰动等级 0–4'),('seed','随机种子 整数'),('seed_auto','重置更换种子 0/1'),
 ('start_jitter','起步横向幅值 m'),('heading_jitter','起步偏航幅值 °'),('brightness_jitter','亮度变化幅值 灰度'),('noise','灰度噪声 σ')]
}

# Display familiar physical units; JSON and engine retain SI units.
MM_FIELDS={'length','wheelbase','track','body_width','tire_width','wheel_diameter',
 'front_overhang','rear_overhang','plate_height','plate_thickness','height','camera_offset','camera_lateral','camera_mount_gap','start_jitter'}
MS_FIELDS={'servo_interval','servo_tau','motor_tau','coast_tau'}

def number_text(value):
    return f'{value:.10f}'.rstrip('0').rstrip('.') if value else '0'
def field_spec(key,label):
    label=re.sub(r'〔.*?〕','',label)
    if key=='hfov':return '镜头视场角','°',1
    if key=='outer_enabled':return '外侧补线','',1
    if key in ('kp','ki','kd','speed_kp','speed_ki'):return label,'',1
    name,_,unit=label.partition(' ')
    if key in MM_FIELDS:return name,'mm',1000
    if key in MS_FIELDS:return name,'ms',1000
    return name,unit,1

class App:
    def __init__(self,root):
        self.root=root;root.tk.call('tk','scaling',1.25)
        root.title('小车仿真实验室 · 完整赛道')
        width=min(1480,root.winfo_screenwidth()-60);height=min(930,root.winfo_screenheight()-90)
        root.geometry(f'{width}x{height}+25+35');root.minsize(1100,720)
        root.configure(bg='#0c1624');self.running=False;self.yaw=-55;self.elevation=48;self.zoom=1.;self.last_drag=None
        self.p=defaults();self.sim=Simulation(self.p);self.comparison=Comparison();self.vars={};self.photos={};self.last_clock=time.perf_counter();self.accumulator=0.
        self.fit_candidate=None;self._syncing=False;self.parameter_pages=[];self.extra_presets={};self.photo_back={};self._refresh_job=None
        self._window_hidden=not root.winfo_viewable();self._restore_job=None;self._restoring=False
        self.window_refresh_stats=[];self.window_refresh_details=[]
        self.replay_frames=[];self.replay_index=0;self.replay_clock=0.
        self._visual_dirty=True;self._last_render=0.;self._render_keys={}
        self._source_image=None;self.field_scales={};self.field_widgets={};self.display_values={};self.metric_vars={};self.details_visible=False;self.details_window=None
        style=ttk.Style(root);style.theme_use('clam')
        style.configure('.',font=('Microsoft YaHei UI',10),background='#142236',foreground='#dce6f2')
        style.configure('TFrame',background='#142236');style.configure('TLabel',background='#142236',foreground='#dce6f2')
        style.configure('TButton',padding=(10,7),background='#273d56',foreground='#eff6ff')
        style.map('TButton',background=[('active','#375a79')])
        style.configure('TEntry',fieldbackground='#0e1a2a',foreground='#f1f6fb',insertcolor='white')
        style.configure('Compact.TEntry',font=('Microsoft YaHei UI',9),padding=0)
        style.configure('Treeview',background='#0e1a2a',fieldbackground='#0e1a2a',foreground='#dce6f2',rowheight=24)
        style.configure('Treeview.Heading',background='#273d56',foreground='#eff6ff')
        style.map('Treeview',background=[('selected','#36536c')],foreground=[('selected','#ffffff')])
        style.configure('TCombobox',fieldbackground='#0e1a2a',foreground='#f1f6fb')
        style.map('TCombobox',fieldbackground=[('readonly','#0e1a2a')],foreground=[('readonly','#f1f6fb')])
        style.configure('TNotebook.Tab',padding=(12,7),background='#203247')
        # Replace Clam's expanding tab element with a flat border: selection changes color only.
        self.tab_images=[]
        for color in ('#203247','#36536c','#294359'):
            tab_image=tk.PhotoImage(master=root,width=2,height=2);tab_image.put(color,to=(0,0,2,2));self.tab_images.append(tab_image)
        style.element_create('FlatTab.background','image',self.tab_images[0],
            ('selected',self.tab_images[1]),('active',self.tab_images[2]),border=0,sticky='nswe')
        style.layout('TNotebook.Tab',[('FlatTab.background',{'sticky':'nswe','children':[
            ('Notebook.padding',{'sticky':'nswe','children':[('Notebook.label',{'sticky':'nswe'})]})]})])
        style.configure('TNotebook.Tab',padding=(12,8),borderwidth=0,relief='flat')
        style.map('TNotebook.Tab',background=[('selected','#36536c'),('!selected','#203247')],
                  padding=[('selected',(12,8)),('!selected',(12,8))],
                  expand=[('selected',(0,0,0,0)),('!selected',(0,0,0,0))],relief=[('pressed','flat'),('selected','flat')])
        style.configure('TCheckbutton',background='#142236',foreground='#dce6f2')
        style.configure('Compact.TNotebook.Tab',padding=(7,8))
        style.map('Compact.TNotebook.Tab',padding=[('selected',(7,8)),('!selected',(7,8))])
        banner=tk.Frame(root,bg='#0c1624');banner.pack(fill='x',padx=18,pady=(12,8))
        tk.Label(banner,text='小车仿真实验室',font=('Microsoft YaHei UI',22,'bold'),fg='#eff5fd',bg='#0c1624').pack(side='left')
        self.mode_note=tk.Label(banner,text='',fg='#f7be69',bg='#0c1624')
        bar=ttk.Frame(root);bar.pack(fill='x',padx=18,pady=(0,8))
        for text,method in [('▶ 启动 / 继续',self.start),('Ⅱ 暂停',self.pause),('■ 停车',self.stop),('应用并重置',self.reset)]:
            ttk.Button(bar,text=text,command=method).pack(side='left',padx=3,pady=5)
        self.speed=tk.StringVar(value='1×');ttk.Label(bar,text='速度').pack(side='left',padx=(12,3))
        ttk.Combobox(bar,textvariable=self.speed,values=['0.5×','1×','2×','4×','8×'],width=5,state='readonly').pack(side='left')
        ttk.Button(bar,text='导出实验',command=self.export).pack(side='right',padx=4)
        ttk.Button(bar,text='图像回放',command=self.replay).pack(side='right',padx=4)
        ttk.Button(bar,text='保存参数',command=self.save_params).pack(side='right',padx=4)
        outer=ttk.Panedwindow(root,orient='horizontal');outer.pack(fill='both',expand=True,padx=18)
        left=ttk.Frame(outer,width=320);middle=ttk.Frame(outer);right=ttk.Frame(outer,width=390)
        outer.add(left,weight=0);outer.add(middle,weight=1);outer.add(right,weight=0)
        ttk.Label(left,text='实验参数',font=('Microsoft YaHei UI',13,'bold')).pack(anchor='w',padx=12,pady=10)
        self.scenario=tk.StringVar(value='完整赛道');self.direction=tk.StringVar(value='A→B');self.controller=tk.StringVar(value='现有 C 算法')
        for title,var,vals in [('场景',self.scenario,['完整赛道','中期 A—B','直道诊断','圆弧诊断']),('方向',self.direction,['A→B','B→A']),
                               ('控制',self.controller,['现有 C 算法','理想几何参考'])]:
            row=ttk.Frame(left);row.pack(fill='x',padx=10,pady=3);ttk.Label(row,text=title,width=5).pack(side='left')
            ttk.Combobox(row,textvariable=var,values=vals,state='readonly',width=19).pack(side='right')
        row=ttk.Frame(left);row.pack(fill='x',padx=10,pady=3)
        ttk.Label(row,text='选择参数',width=8).pack(side='left')
        self.parameter_name=tk.StringVar(value='默认参数')
        self.parameter_selector=ttk.Combobox(row,textvariable=self.parameter_name,state='readonly',width=19)
        self.parameter_selector.pack(side='right');self.update_presets()
        self.parameter_selector.bind('<<ComboboxSelected>>',self.select_params)
        self.vars['show_ab']=tk.BooleanVar(value=True)
        ttk.Checkbutton(left,text='显示 A / B 标线',variable=self.vars['show_ab'],command=self.inject).pack(anchor='w',padx=10,pady=3)
        self.parameter_tabs=ttk.Notebook(left,style='Compact.TNotebook');self.parameter_tabs.pack(fill='both',expand=True,padx=8,pady=8)
        for title,fields in FIELDS.items():
            page=ttk.Frame(self.parameter_tabs);self.parameter_tabs.add(page,text=title)
            pane=ttk.Frame(page);pane.pack(fill='both',expand=True);cells=[]
            self.parameter_pages.append((pane,cells))
            for i,(key,label) in enumerate(fields):
                name,unit,scale=field_spec(key,label);self.field_scales[key]=scale
                cell=ttk.Frame(pane);cells.append(cell)
                ttk.Label(cell,text=name,font=('Microsoft YaHei UI',9),width=13).grid(row=0,column=0,sticky='w')
                v=tk.StringVar(value=number_text(self.p[key]*scale));self.vars[key]=v;self.display_values[key]=v.get()
                widget=ttk.Entry(cell,textvariable=v,width=10,style='Compact.TEntry',validate='key',validatecommand=(root.register(self.numeric_edit),'%P'))
                self.field_widgets[key]=widget;widget.grid(row=0,column=1,sticky='w',padx=(0,3))
                ttk.Label(cell,text=unit,font=('Microsoft YaHei UI',9),foreground='#91a9c2').grid(row=0,column=2,sticky='w')
            if title=='扰动':
                cell=ttk.Frame(pane);cells.append(cell);ttk.Label(cell,text='0关闭，1轻微，2中等\n3较强，4自定义',font=('Microsoft YaHei UI',8),foreground='#91a9c2').pack(anchor='w')
            if title=='车模':
                cell=ttk.Frame(pane);cells.append(cell);ttk.Label(cell,text='安装间距为外形近似\n不改变镜头投影位置',font=('Microsoft YaHei UI',8),foreground='#91a9c2').pack(anchor='w')
            self.layout_parameters(pane,cells)
        root.bind('<MouseWheel>',self.parameter_scroll,add='+')
        self.vars['disturbance_level'].trace_add('write',self.level_changed)
        for key in DISTURBANCE_KEYS:self.vars[key].trace_add('write',self.disturbance_changed)
        ttk.Label(left,text='修改后点击「应用并重置」',foreground='#a8bbcf',wraplength=285).pack(anchor='w',padx=12,pady=8)
        self.view_tabs=ttk.Notebook(middle,style='Compact.TNotebook');self.view_tabs.pack(fill='both',expand=True,padx=8,pady=8)
        self.views={}
        for name in ('2D 赛道','3D 场景','赛道原图','相机标定','C1 车模','实测对照'):
            pane=ttk.Frame(self.view_tabs);self.view_tabs.add(pane,text=name);self.views[name]=pane
        def viewport(parent):
            canvas=tk.Canvas(parent,width=200,height=200,bg='#101d2d',highlightthickness=0)
            canvas.bind('<Configure>',self.request_refresh)
            return canvas
        self.map_label=viewport(self.views['2D 赛道']);self.map_label.pack(fill='both',expand=True)
        self.scene_toolbar=ttk.Frame(self.views['3D 场景']);self.scene_toolbar.pack(side='top',fill='x',pady=4)
        self.follow=tk.BooleanVar(value=False)
        self.follow_button=ttk.Checkbutton(self.scene_toolbar,text='跟车视角',variable=self.follow,command=self.request_refresh)
        self.follow_button.pack(side='left',padx=8)
        self.scene_label=viewport(self.views['3D 场景']);self.scene_label.pack(fill='both',expand=True)
        self.scene_label.bind('<ButtonPress-1>',self.drag_start);self.scene_label.bind('<B1-Motion>',self.drag)
        self.scene_label.bind('<MouseWheel>',self.scroll)
        self.model_label=viewport(self.views['C1 车模']);self.model_label.pack(fill='both',expand=True)
        self.model_label.bind('<ButtonPress-1>',self.drag_start);self.model_label.bind('<B1-Motion>',self.drag)
        self.model_label.bind('<MouseWheel>',self.scroll)
        self.source_label=viewport(self.views['赛道原图']);self.source_label.pack(fill='both',expand=True)
        ttk.Label(self.views['赛道原图'],text='赛道原图：中期检查要求.docx',wraplength=580).pack(pady=8)
        cal_page=self.views['相机标定']
        self.calibration_canvas=tk.Canvas(cal_page,bg='#142236',highlightthickness=0)
        cal_scroll=ttk.Scrollbar(cal_page,orient='vertical',command=self.calibration_canvas.yview);cal_scroll.pack(side='right',fill='y')
        self.calibration_canvas.pack(side='left',fill='both',expand=True);self.calibration_canvas.configure(yscrollcommand=cal_scroll.set)
        cal=ttk.Frame(self.calibration_canvas);cal_window=self.calibration_canvas.create_window(0,0,window=cal,anchor='nw')
        cal.bind('<Configure>',lambda event:self.calibration_canvas.configure(scrollregion=self.calibration_canvas.bbox('all')))
        self.calibration_canvas.bind('<Configure>',lambda event:self.calibration_canvas.itemconfigure(cal_window,width=event.width))
        ttk.Label(cal,text='固定高度 · 六行宽度标定',font=('Microsoft YaHei UI',16,'bold')).pack(anchor='w',padx=16,pady=12)
        self.reference_vars=[];row=ttk.Frame(cal);row.pack(fill='x',padx=16)
        for index,y in enumerate(ROWS):
            ttk.Label(row,text=f'{y}行').grid(row=index//3,column=(index%3)*2,padx=4,pady=4)
            v=tk.StringVar(value=number_text(self.p['reference_widths'][index]));self.reference_vars.append(v)
            ttk.Entry(row,textvariable=v,width=7,validate='key',validatecommand=(root.register(self.numeric_edit),'%P')).grid(row=index//3,column=(index%3)*2+1,padx=4)
        ttk.Button(cal,text='应用参考宽度',command=self.apply_references).pack(anchor='w',padx=16,pady=6)
        self.width_table=ttk.Treeview(cal,columns=('row','measured','predicted','error'),show='headings',height=6)
        for key,title in zip(('row','measured','predicted','error'),('行号','参考 px','预测 px','误差 px')):
            self.width_table.heading(key,text=title);self.width_table.column(key,width=95,stretch=True)
        self.width_table.pack(fill='x',padx=16,pady=6)
        self.calibration_text=tk.StringVar();ttk.Label(cal,textvariable=self.calibration_text,wraplength=560,justify='left').pack(anchor='w',padx=20,pady=10)
        actions=ttk.Frame(cal);actions.pack(fill='x',padx=16,pady=6)
        ttk.Button(actions,text='整体误差最小',command=lambda:self.fit('average')).pack(side='left',padx=3)
        ttk.Button(actions,text='最大误差最小',command=lambda:self.fit('maximum')).pack(side='left',padx=3)
        ttk.Button(actions,text='应用标定结果',command=self.apply_fit).pack(side='left',padx=3)
        self.fit_text=tk.StringVar(value='保持当前镜头高度和道路内宽，拟合俯角与有效水平视场。')
        ttk.Label(cal,textvariable=self.fit_text,wraplength=520).pack(anchor='w',padx=16,pady=6)
        ttk.Button(cal,text='试用 20° / 水平120°假设',command=self.nominal).pack(anchor='w',padx=20,pady=5)
        sampling=ttk.Frame(cal);sampling.pack(fill='x',padx=20,pady=5)
        ttk.Label(sampling,text='成像采样').pack(side='left')
        self.sampling_quality=tk.StringVar(value=sampling_label(self.p))
        sampling_choice=ttk.Combobox(sampling,textvariable=self.sampling_quality,
            values=list(SAMPLING_OPTIONS),state='readonly',width=16)
        sampling_choice.pack(side='left',padx=8)
        ttk.Label(cal,text='镜头标称角120°：方向定义未确认，与有效水平视场分别记录。\n黑边宽度0表示不额外绘制黑边，外宽仍未实测。',foreground='#91a9c2',wraplength=540).pack(anchor='w',padx=20,pady=6)
        ttk.Label(cal,text='实际图像校准：在直道摆正车辆，记录地面距离及图像行位置。\n修改参数不能证明安装角度已被实测确认。',foreground='#f7be69',wraplength=540).pack(anchor='w',padx=20,pady=20)
        comparison_toolbar=ttk.Frame(self.views['实测对照']);comparison_toolbar.pack(fill='x')
        for n,(text,action) in enumerate([('导入帧/截图',self.load_measured),('添加标定点',self.add_calibration_point),
            ('导入点CSV',self.load_calibration_points),('设置参考行宽',self.set_reference_widths),
            ('清空点',self.clear_calibration_points),('导出对照',self.export_comparison),('查看点误差',self.show_point_errors)]):
            ttk.Button(comparison_toolbar,text=text,command=action).grid(row=n//3,column=n%3,padx=2,pady=2,sticky='ew')
        for column in range(3):comparison_toolbar.columnconfigure(column,weight=1)
        ttk.Label(self.views['实测对照'],text='地面点以后轴为原点：x向前、y向左（mm）。对照不自动修改参数。',wraplength=350,foreground='#91a9c2').pack(fill='x',padx=8,pady=4)
        self.comparison_label=viewport(self.views['实测对照']);self.comparison_label.pack(fill='both',expand=True)
        ttk.Label(right,text='摄像头画面',font=('Microsoft YaHei UI',13,'bold')).pack(anchor='w',padx=12,pady=10)
        self.camera_label=tk.Canvas(right,width=376,height=240,bg='#101d2d',highlightthickness=0);self.camera_label.pack(padx=8)
        self.camera_label.bind('<Configure>',lambda event:event.widget.coords('viewport-image',event.width/2,event.height/2))
        ttk.Label(right,text='青/橙：识别边线  绿：有效中线  紫：重建边线',foreground='#a8bbcf').pack(pady=4)
        self.run_state=tk.StringVar();ttk.Label(right,textvariable=self.run_state,font=('Microsoft YaHei UI',13,'bold')).pack(anchor='w',padx=12,pady=(10,4))
        metrics=ttk.Frame(right);metrics.pack(fill='x',padx=12,pady=3)
        self.metric_labels={}
        for i,(key,label) in enumerate([('time','运行时间'),('distance','行驶距离'),('speed','车速'),('steer','转向角'),('error','横向偏差'),('quality','路径可信度'),('element','赛道元素'),('fault','故障')]):
            ttk.Label(metrics,text=label,foreground='#91a9c2',width=12).grid(row=i,column=0,sticky='w',pady=0)
            value=tk.StringVar();self.metric_vars[key]=value
            metric_label=ttk.Label(metrics,textvariable=value);metric_label.grid(row=i,column=1,sticky='w',padx=8,pady=0);self.metric_labels[key]=metric_label
        self.status=tk.StringVar();self.details_button=ttk.Button(right,text='查看诊断详情',command=self.toggle_details)
        self.details_button.pack(anchor='w',padx=12,pady=4)
        self.chart_label=tk.Canvas(right,width=376,height=120,bg='#101d2d',highlightthickness=0);self.chart_label.pack(padx=8,pady=5)
        self.chart_label.bind('<Configure>',lambda event:event.widget.coords('viewport-image',event.width/2,event.height/2))
        injections=ttk.LabelFrame(right,text='故障模拟');injections.pack(fill='x',padx=10,pady=8)
        self.injection_widgets=[]
        for key,label in [('drop_camera','停止图像'),('drop_feedback','冻结反馈'),('blackout','全黑画面')]:
            v=tk.BooleanVar(value=False);self.vars[key]=v
            widget=ttk.Checkbutton(injections,text=label,variable=v,command=self.inject);self.injection_widgets.append(widget)
            widget.pack(side='left',padx=7,pady=5)
        self.message=tk.StringVar(value='已就绪：297 mm实测高度，俯角/有效视场为条件拟合。点击启动开始仿真。')
        ttk.Label(root,textvariable=self.message,foreground='#91a9c2').pack(fill='x',padx=22,pady=8)
        self.view_tabs.bind('<<NotebookTabChanged>>',self.request_refresh)
        self.transitions_disabled=disable_window_transitions(root)
        root.bind('<Map>',self.window_mapped,add='+');root.bind('<Unmap>',self.window_unmapped,add='+')
        root.protocol('WM_DELETE_WINDOW',self.close);self.request_refresh();root.after(16,self.loop)
    def window_unmapped(self,event):
        if event.widget!=self.root:return
        self._window_hidden=True;self.accumulator=0.;self.last_clock=time.perf_counter()
        if self._refresh_job is not None:self.root.after_cancel(self._refresh_job);self._refresh_job=None
    def window_mapped(self,event):
        if event.widget!=self.root:return
        self.transitions_disabled=disable_window_transitions(self.root)
        self.schedule_restore()
    def schedule_restore(self):
        if self._restore_job is None and not self._restoring:
            self._restore_job=self.root.after(0,self.restore_window)
    def restore_window(self):
        self._restore_job=None
        if not self.root.winfo_viewable() or self.root.state()=='iconic':return
        started=time.perf_counter();self._restoring=True;self._window_hidden=False
        try:
            # Restore must repaint even when paused frame/time cache keys did not change.
            layout_ms=(time.perf_counter()-started)*1000
            render_start=time.perf_counter()
            for key in ('map','scene','model','source','comparison','camera','chart'):self._render_keys.pop(key,None)
            self.refresh(force=False)
            render_ms=(time.perf_counter()-render_start)*1000
            paint_start=time.perf_counter();self.root.update_idletasks()
            paint_ms=(time.perf_counter()-paint_start)*1000
            if os.name=='nt':
                import ctypes
                hwnd=window_handle(self.root)
                ctypes.windll.user32.RedrawWindow.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint]
                # Repaint retained controls and canvas surfaces, without an erase/blank pass.
                # Queue repainting; synchronous Win32 dispatch would re-enter Tk outside
                # its event dispatcher. Tk will service the paint on the next pump.
                ctypes.windll.user32.RedrawWindow(hwnd,None,None,0x0001|0x0080|0x0400)
            self.window_refresh_stats.append((time.perf_counter()-started)*1000)
            self.window_refresh_details.append({'layout_ms':layout_ms,'render_ms':render_ms,'paint_ms':paint_ms})
        finally:
            self._restoring=False;self._visual_dirty=False;self.accumulator=0.
            self.last_clock=self._last_render=time.perf_counter()
    @staticmethod
    def numeric_edit(value):
        return re.fullmatch(r'[+-]?(?:\d*(?:\.\d*)?)',value) is not None
    def parameter_scroll(self,event):
        x,y=self.root.winfo_pointerxy()
        for canvas in [self.calibration_canvas]:
            if canvas.winfo_ismapped() and canvas.winfo_rootx()<=x<=canvas.winfo_rootx()+canvas.winfo_width() and canvas.winfo_rooty()<=y<=canvas.winfo_rooty()+canvas.winfo_height():
                canvas.yview_scroll(-1 if event.delta>0 else 1,'units');return
    def layout_parameters(self,pane,cells):
        pane.columnconfigure(0,weight=1)
        for index,cell in enumerate(cells):cell.grid(row=index,column=0,sticky='nw',padx=5,pady=1)
    def level_changed(self,*args):
        if self._syncing:return
        value=self.vars['disturbance_level'].get()
        try:level=float(value)
        except ValueError:return
        if level in PRESETS:
            self._syncing=True
            try:
                for key,amount in zip(DISTURBANCE_KEYS,PRESETS[level]):self.vars[key].set(number_text(amount*self.field_scales[key]))
            finally:self._syncing=False
    def disturbance_changed(self,*args):
        if not self._syncing and self.vars['disturbance_level'].get()!='4':
            self._syncing=True
            try:self.vars['disturbance_level'].set('4')
            finally:self._syncing=False
    def update_presets(self):
        registry=ROOT/'parameter_locations.json'
        if registry.exists():
            try:
                for name,path in json.loads(registry.read_text(encoding='utf-8')).items():
                    if Path(path).is_file():self.extra_presets[name]=Path(path)
            except (ValueError,OSError):pass
        self.preset_paths={path.stem:path for path in sorted((ROOT/'configs').glob('*.json'))}
        self.preset_paths.update(self.extra_presets)
        self.parameter_selector.configure(values=['默认参数',*self.preset_paths])
    def select_params(self,event=None):
        name=self.parameter_name.get();self.running=False
        try:
            p=defaults() if name=='默认参数' else load(self.preset_paths[name])
            self.set_fields(p);self.reset()
        except Exception as error:messagebox.showerror('选择参数失败',str(error))
    def collect(self):
        p=self.p.copy()
        for key,var in self.vars.items():
            if isinstance(var,tk.BooleanVar):p[key]=var.get()
            elif var.get()!=self.display_values.get(key):p[key]=float(var.get())/self.field_scales.get(key,1)
        p['scenario']={'完整赛道':'full','中期 A—B':'midterm','直道诊断':'straight','圆弧诊断':'circle'}[self.scenario.get()]
        p['direction']=self.direction.get();p['controller']='native' if self.controller.get()=='现有 C 算法' else 'reference'
        p['reference_widths']=[float(v.get()) for v in self.reference_vars]
        if hasattr(self,'sampling_quality'):
            p['camera_samples'],p['camera_sampling']=SAMPLING_OPTIONS[self.sampling_quality.get()]
        if abs(p['pitch']-p['fitted_pitch'])>1e-4 or abs(p['hfov']-p['fitted_hfov'])>1e-4:p['camera_mode']='manual'
        return validate(p)
    def set_fields(self,p):
        self.p=p;self._syncing=True
        self.fit_candidate=None
        if hasattr(self,'fit_text'):self.fit_text.set('保持当前镜头高度和道路内宽，拟合俯角与有效水平视场。')
        if hasattr(self,'sampling_quality'):
            self.sampling_quality.set(sampling_label(p))
        try:
            for key,var in self.vars.items():
                var.set(p[key] if isinstance(var,tk.BooleanVar) else number_text(p[key]*self.field_scales.get(key,1)))
                if not isinstance(var,tk.BooleanVar):self.display_values[key]=var.get()
            for v,width in zip(self.reference_vars,p['reference_widths']):v.set(number_text(width))
        finally:self._syncing=False
        self.scenario.set({'full':'完整赛道','midterm':'中期 A—B','straight':'直道诊断','circle':'圆弧诊断'}[p['scenario']])
        self.direction.set(p['direction']);self.controller.set('现有 C 算法' if p['controller']=='native' else '理想几何参考')
    def reset(self):
        self.running=False
        try:
            p=self.collect();new=Simulation(p);self.sim=new;self.set_fields(new.p);self.replay_frames=[];self.accumulator=0.
            self.comparison.widths=references(new.p);self.comparison.version+=1
            self.message.set('参数已应用。点击启动开始仿真。');self.refresh()
        except Exception as e:messagebox.showerror('参数 / 编译错误',str(e))
    def start(self):
        if self.sim.replay:
            self.running=True;self.last_clock=time.perf_counter();return
        if self.sim.data[0] in (4,5,6):
            if self.sim.stop_event is not None and not self.sim.stop_capture_complete:
                self.running=True;self.last_clock=time.perf_counter();self.message.set('继续记录停车后图像。');return
            self.message.set('当前已结束、停车或故障；请「应用并重置」后重新启动。');return
        self.sim.started=True;self.running=True;self.last_clock=time.perf_counter()
    def pause(self):self.running=False;self.message.set('已暂停，仿真时间冻结。')
    def stop(self):
        if self.sim.replay:self.pause();return
        self.sim.stop_requested=True;self.sim.tick();self.running=self.sim.p['controller']=='native';self.last_clock=time.perf_counter();self.refresh()
        self.message.set('已停车，正在记录停车后2秒图像。暂停可提前结束记录。' if self.running else '参考控制已停车。')
    def inject(self):
        for key in ('drop_camera','drop_feedback','blackout','show_ab'):self.sim.p[key]=self.vars[key].get()
        self.refresh()
    def fit(self,objective='maximum'):
        try:
            p=self.collect();self.fit_input=json.dumps(p,sort_keys=True);self.fit_candidate=fit_six_rows(p,objective)
            candidate=self.fit_candidate;_,rmse,maximum=width_comparison(candidate)
            self.fit_text.set(f"保持高度 {candidate['height']*1000:.2f} mm；候选俯角 {candidate['pitch']:.2f}°，有效水平视场 {candidate['hfov']:.2f}°\n均方根误差 {rmse:.2f} px，最大误差 {maximum:.2f} px。条件拟合，点击应用后生效。")
        except Exception as e:messagebox.showerror('拟合失败',str(e))
    def apply_fit(self):
        try:
            if self.fit_candidate is None:raise ValueError('请先计算标定候选。')
            if json.dumps(self.collect(),sort_keys=True)!=self.fit_input:raise ValueError('参数已变化，请重新计算标定候选。')
            self.set_fields(self.fit_candidate.copy());self.reset();self.fit_candidate=None
            self.fit_text.set('标定结果已应用。')
        except Exception as error:messagebox.showerror('应用标定失败',str(error))
    def apply_references(self):
        try:
            widths=self.collect()['reference_widths'];self.p['reference_widths']=widths.copy();self.sim.p['reference_widths']=widths.copy()
            self.comparison.widths=references(self.sim.p);self.comparison.version+=1;self.refresh()
        except Exception as error:messagebox.showerror('参考宽度错误',str(error))
    def nominal(self):
        p=self.collect();p.update(pitch=20.,hfov=120.,k1=0.,camera_mode='manual')
        self.set_fields(p);self.reset();self.message.set('已试用20°俯角/水平120°假设；高度保持，尚未实测确认。')
    def toggle_details(self):
        if self.details_window is not None:
            self.details_window.destroy();self.details_window=None;self.details_visible=False
        else:
            window=tk.Toplevel(self.root);window.title('诊断详情');window.geometry('440x330');window.transient(self.root)
            window.configure(bg='#142236');self.details_window=window;self.details_visible=True
            ttk.Label(window,textvariable=self.status,justify='left',wraplength=400).pack(anchor='w',padx=20,pady=20)
            ttk.Button(window,text='关闭',command=self.toggle_details).pack(anchor='e',padx=20,pady=10)
            window.protocol('WM_DELETE_WINDOW',self.toggle_details)
        self.details_button.configure(text='关闭诊断详情' if self.details_visible else '查看诊断详情')
    def save_params(self):
        try:
            p=self.collect();name=self.parameter_name.get()
            chosen=filedialog.asksaveasfilename(parent=self.root,title='保存参数',initialdir=str(ROOT/'configs'),
                initialfile=('我的参数' if name=='默认参数' else name)+'.json',defaultextension='.json',filetypes=[('JSON参数','*.json')])
            if not chosen:return
            path=Path(chosen)
            if path.resolve().is_relative_to((ROOT.parent/'Autodrive').resolve()):raise ValueError('Autodrive 为只读目录，请选择其他保存位置。')
            save(path,p);self.extra_presets[path.stem]=path
            (ROOT/'parameter_locations.json').write_text(json.dumps({name:str(path) for name,path in self.extra_presets.items()},ensure_ascii=False,indent=2),encoding='utf-8')
            self.update_presets();self.parameter_name.set(path.stem);self.message.set('已保存：'+str(path))
        except Exception as e:messagebox.showerror('保存失败',str(e))
    def export(self):
        folder=self.sim.export('replay' if self.sim.replay else self.sim.p['controller'])
        map_image(self.sim).save(folder/'map.png');scene_image(self.sim).save(folder/'scene.png')
        camera_image(self.sim).save(folder/'vision_overlay.png');chart_image(self.sim).save(folder/'chart.png')
        self.message.set('已导出参数、结果、CSV与截图：'+str(folder))
    def load_measured(self):
        path=filedialog.askopenfilename(title='选择188×120原始帧或完整逐飞助手截图',filetypes=[('图像','*.png *.bmp *.jpg *.pgm')])
        if not path:return
        try:
            self.comparison.load_image(path);self.running=False;self.refresh()
            self.message.set('已导入截图参考，紫色区域被彩线/文字遮挡；车辆姿态尚未对齐。' if self.comparison.source_kind=='screenshot' else '已导入实测帧，仿真已暂停，可添加地面标定点。')
        except Exception as error:messagebox.showerror('导入失败',str(error))
    def add_calibration_point(self):
        value=simpledialog.askstring('添加标定点','输入 x(mm), y(mm), 实测列u, 实测行v；地面以后轴为原点：')
        if value is None:return
        try:
            values=re.split(r'[,，\s]+',value.strip())
            if len(values)!=4:raise ValueError('需要四个数值：x、y、u、v。')
            self.comparison.add_point(*values);self.refresh()
        except Exception as error:messagebox.showerror('标定点错误',str(error))
    def load_calibration_points(self):
        path=filedialog.askopenfilename(title='CSV列：x_mm,y_mm,u_px,v_px',filetypes=[('CSV','*.csv')])
        if not path:return
        try:self.comparison.load_points(path);self.refresh()
        except Exception as error:messagebox.showerror('导入失败',str(error))
    def set_reference_widths(self):
        self.view_tabs.select(self.views['相机标定']);self.request_refresh()
    def clear_calibration_points(self):
        self.comparison.points=[];self.comparison.version+=1;self.refresh()
    def export_comparison(self):
        folder=ROOT/'runs'/('comparison-'+str(time.time_ns()))
        self.comparison.export(folder,self.sim.p,self.sim);self.message.set('已导出实测对照：'+str(folder))
    def show_point_errors(self):
        window=tk.Toplevel(self.root);window.title('标定点像素误差');window.geometry('680x350');window.transient(self.root)
        columns=('id','observed','predicted','error');table=ttk.Treeview(window,columns=columns,show='headings')
        for key,title in zip(columns,('编号','实测 (u,v)','预测 (u,v)','误差 (du,dv)')):
            table.heading(key,text=title);table.column(key,width=150)
        for point in self.comparison.result(self.sim.p)['points']:
            def pair(value):return '镜头后方' if value is None else f'({value[0]:.2f}, {value[1]:.2f})'
            table.insert('',tk.END,values=(point['id'],pair([point['u_px'],point['v_px']]),pair(point['predicted_px']),pair(point['error_px'])))
        table.pack(fill='both',expand=True,padx=10,pady=10)
    def replay(self):
        self.running=False
        paths=filedialog.askopenfilenames(title='选择188×120原始帧（按文件名排序回放）',filetypes=[('图像','*.png *.bmp *.jpg *.pgm')])
        if not paths:return
        self.running=False
        try:
            frames=[]
            for path in sorted(paths):
                with Image.open(path) as im:
                    if im.size!=(188,120):raise ValueError(f'{path} 尺寸不是188×120；不自动缩放原始帧。')
                    frames.append((path,np.asarray(im.convert('L')).copy()))
            self.sim=Simulation(self.collect());self.sim.replay=True;self.sim.history=[]
            self.replay_frames=frames;self.replay_index=0;self.replay_clock=0.;self.replay_next();self.refresh()
        except Exception as e:messagebox.showerror('回放失败',str(e))
    def replay_next(self):
        if self.replay_index>=len(self.replay_frames):self.running=False;self.message.set('图像回放完成。继续仿真请应用并重置。');return
        path,gray=self.replay_frames[self.replay_index];self.replay_index+=1
        s=self.sim;s.now=round((self.replay_index-1)*1000/s.p['fps']);s.image=gray
        s.native.vision(gray,s.now,0);s.data,s.rows=s.native.data()
        s.frame_id+=1;s.frame_time_ms=s.now;s.frame_pose=(s.x,s.y,s.h);s._new_frame=True;s.capture_frame();s.record()
        self.message.set(f'图像回放 {self.replay_index}/{len(self.replay_frames)}，{path}，无轮速/里程，元素距离转换未验证')
    def loop(self):
        try:
            if self.root.state()=='iconic' or not self.root.winfo_viewable():
                self._window_hidden=True;self.accumulator=0.;self.last_clock=time.perf_counter()
                self.root.after(100,self.loop);return
            if self._window_hidden:
                self.schedule_restore();self.root.after(8,self.loop);return
            clock=time.perf_counter();elapsed=min(.2,clock-self.last_clock);self.last_clock=clock
            if self.running:
                factor=float(self.speed.get().replace('×',''))
                if self.sim.replay:
                    self.replay_clock=min(.5,self.replay_clock+elapsed*factor)
                    period=1/self.sim.p['fps'];budget=time.perf_counter()+.006
                    while self.running and self.replay_clock>=period and time.perf_counter()<budget:
                        self.replay_clock-=period;self.replay_next()
                else:
                    self.accumulator=min(.5,self.accumulator+elapsed*factor)
                    # Bound each simulation batch so Tk can service input between batches.
                    budget=time.perf_counter()+.006
                    while self.accumulator>=.01 and time.perf_counter()<budget:
                        self.sim.tick();self.accumulator-=.01
                        if self.sim.completed and self.sim.stop_event is None:
                            self.running=False;self.message.set('到达终点，已冻结仿真供检查；这不是控制器自动停车。');break
                        if self.sim.p['controller']=='native' and self.sim.stop_event is not None:
                            if self.sim.stop_capture_complete:
                                self.running=False;self.message.set('停车证据与前后图像已记录，可导出实验。');break
                            self.message.set('控制器已停车，正在记录停车后2秒图像。')
            clock=time.perf_counter()
            pending=getattr(self,'_scene_future',None)
            if pending is not None and pending.done():
                if self.running and self.view_tabs.tab(self.view_tabs.select(),'text')=='3D 场景':
                    self._visual_dirty=True
                else:self._scene_future=None
            if self._visual_dirty or clock-self._last_render>=1/30:
                self.refresh(force=False);self._last_render=clock;self._visual_dirty=False
        except Exception:
            self.running=False;log=ROOT/'reports/ui_error.log';log.write_text(traceback.format_exc(),encoding='utf-8')
            self.message.set('运行异常，已暂停。详情：'+str(log))
        delay=1 if self.running and self.accumulator>=.01 else (8 if self.running else 25)
        self.root.after(delay,self.loop)
    def show(self,key,label,image):
        # Upload into an invisible back buffer, then swap the complete opaque frame.
        old=self.photos.get(key);photo=self.photo_back.get(key)
        if photo is not None and photo.width()==image.width and photo.height()==image.height:photo.paste(image)
        else:photo=ImageTk.PhotoImage(image)
        if isinstance(label,tk.Canvas):
            if label.find_withtag('viewport-image'):label.itemconfigure('viewport-image',image=photo)
            else:label.create_image(0,0,image=photo,tags='viewport-image',anchor='center')
        else:label.configure(image=photo)
        self.photos[key]=photo;self.photo_back[key]=old
        if isinstance(label,tk.Canvas):label.coords('viewport-image',label.winfo_width()/2,label.winfo_height()/2)
    def request_refresh(self,event=None):
        # Coalesce drag/follow/tab events; render the latest view once on the next UI pump.
        self._visual_dirty=True
        if self._window_hidden or self._restoring:return
        if self._refresh_job is None:self._refresh_job=self.root.after_idle(self.flush_refresh)
    def flush_refresh(self):
        self._refresh_job=None
        if self._window_hidden or not self.root.winfo_viewable():return
        started=time.perf_counter()
        self.refresh(force=False);self._last_render=started;self._visual_dirty=False
    def refresh(self,force=True):
        s=self.sim;tab=self.view_tabs.tab(self.view_tabs.select(),'text')
        def changed(name,key):
            if force or self._render_keys.get(name)!=key:
                self._render_keys[name]=key;return True
            return False
        if tab=='2D 赛道':
            size=(max(160,self.map_label.winfo_width()),max(160,self.map_label.winfo_height()))
            if changed('map',(id(s),s.now,size,s.p['show_ab'])):self.show('map',self.map_label,map_image(s,size))
        elif tab=='3D 场景':
            size=(max(160,self.scene_label.winfo_width()),max(160,self.scene_label.winfo_height()))
            key=(id(s),s.now,size,self.yaw,self.elevation,self.zoom,self.follow.get(),s.p['show_ab'])
            if self.running and not force and not self._restoring:
                self.scene_async(key,size)
            elif changed('scene',key):
                self.show('scene',self.scene_label,scene_image(s,size,self.yaw,self.elevation,self.zoom,self.follow.get()))
        elif tab=='赛道原图':
            size=(max(160,self.source_label.winfo_width()),max(160,self.source_label.winfo_height()))
            if changed('source',size):
                if self._source_image is None:
                    with Image.open(ROOT/'assets/image1.png') as source:self._source_image=source.convert('RGB')
                im=self._source_image.copy();im.thumbnail(size);self.show('source',self.source_label,im)
        elif tab=='C1 车模':
            size=(max(160,self.model_label.winfo_width()),max(160,self.model_label.winfo_height()))
            if changed('model',(id(s),s.now,size,self.yaw,self.elevation,self.zoom)):
                self.show('model',self.model_label,scene_image(s,size,self.yaw,self.elevation,self.zoom,model_only=True))
        elif tab=='实测对照':
            size=(max(160,self.comparison_label.winfo_width()),max(240,self.comparison_label.winfo_height()))
            if changed('comparison',(id(s),s.now,size,self.comparison.version)):
                self.show('comparison',self.comparison_label,self.comparison.image(s,size))
        if changed('camera',(id(s),s.frame_id)):self.show('camera',self.camera_label,camera_image(s))
        if changed('chart',(id(s),s.now//200)):self.show('chart',self.chart_label,chart_image(s,(376,120)))
        p=s.p
        if changed('calibration',(id(s),tuple(p['reference_widths']),p['height'],p['pitch'],p['hfov'],p['fy_ratio'],p['cy'],p['k1'],p['camera_yaw'],p['camera_roll'],p['road_width'])):
            rows,rmse,maximum=width_comparison(p)
            for item in self.width_table.get_children():self.width_table.delete(item)
            for row in rows:
                self.width_table.insert('',tk.END,values=(row['row'],f"{row['reference']:g}",'—' if row['predicted'] is None else f"{row['predicted']:.2f}",'—' if row['error'] is None else f"{row['error']:+.2f}"))
            errors='无有效地面投影' if rmse is None else f'均方根误差 {rmse:.2f} px，最大误差 {maximum:.2f} px'
            self.calibration_text.set(f"高度 {p['height']*1000:g} mm，俯角 {p['pitch']:.2f}°，有效水平视场 {p['hfov']:.2f}°\n{errors}\n条件：平地直道、已知主点和fy/fx；拟合值不代表实测角度。\n参考行宽与截图轮廓有差异，需结合实测对照判断。")
        if not changed('diagnostics',(id(s),s.now//100,int(s.data[0]),int(s.data[1]),s.p['show_ab'])):return
        data=s.data;_,error,progress=s.track.nearest(s.x,s.y,getattr(s,'route_progress',0))
        state='图像回放（无控制）' if s.replay else ('理想几何参考（读取真实路径）' if s.p['controller']=='reference' else STATES[int(data[0])])
        self.run_state.set('已达终点' if s.completed else state.replace('（读取真实路径）',''))
        path=s.path_diagnostic();self.metric_labels['quality'].configure(foreground='#58d3b2' if path['accepted'] else '#ff907f')
        metrics={'time':f'{s.now/1000:.1f} s','distance':f'{s.distance:.2f} m',
          'speed':f'{s.v*1000:.0f} mm/s','steer':f'{math.degrees(s.steer):+.1f}°',
          'error':f'{error*100:+.1f} cm',
          'quality':f"{path['confidence']}/{path['minimum']} {'有效' if path['accepted'] else '失效'}",
          'element':ELEMENTS[int(data[7])],'fault':('—' if s.replay or s.p['controller']=='reference' else FAULTS[int(data[1])])}
        for key,value in metrics.items():
            if self.metric_vars[key].get()!=value:self.metric_vars[key].set(value)
        evidence='停车触发证据' if s.stop_event is not None else '当前帧'
        live=s.path_diagnostic(False)
        self.status.set(f"{evidence}：{path['reason']}（帧{path['frame_id']}）\n"
          f"最新图像：{live['confidence']}/{live['minimum']} {'有效' if live['accepted'] else '失效'}（帧{live['frame_id']}）\n"
          f"{evidence}近 / 远误差：{path['near_px']:+d} / {path['far_px']:+d} px\n"
          f"外侧支持：{'有' if data[9] else '无'}；覆盖行：{int(data[18])} / {int(data[19])}\n"
          f"舵机脉宽：{s.pulse:.0f} µs\n"
          f"左 / 右轮速：{s.left_v*1000:.0f} / {s.right_v*1000:.0f} mm/s\n"
          f"左 / 右电机输出：{s.applied_duties[0]:.0f} / {s.applied_duties[1]:.0f} ‰\n"
          f"路径进度：{progress:.2f} m；出界轮数：{s.outside_wheels()}\n"
          f"C控制器诊断：{FAULTS[int(data[1])]}")
        self.mode_note.configure(text='理想几何参考' if p['controller']=='reference' else '现有 C 算法')
    def scene_async(self,key,size):
        # The worker only reads a detached drawing snapshot and creates a PIL image.
        # Simulation, Tk widgets and PhotoImage uploads stay on the UI thread.
        pending=getattr(self,'_scene_future',None)
        if pending is not None and pending.done():
            self._scene_future=None
            submitted=self._scene_key
            rendered=pending.result()
            if (submitted[0],submitted[2:])==(key[0],key[2:]):
                self.show('scene',self.scene_label,rendered)
                self._render_keys['scene']=submitted
        if getattr(self,'_scene_future',None) is None and self._render_keys.get('scene')!=key:
            if not hasattr(self,'_scene_executor'):
                self._scene_executor=ThreadPoolExecutor(max_workers=1,thread_name_prefix='scene-render')
            snapshot=copy.copy(self.sim)
            snapshot.p=self.sim.p.copy();snapshot.trajectory=list(self.sim.trajectory)
            snapshot.track=copy.copy(self.sim.track)
            self._scene_key=key
            self._scene_future=self._scene_executor.submit(scene_image,snapshot,size,self.yaw,self.elevation,self.zoom,self.follow.get())
    def drag_start(self,event):self.last_drag=(event.x,event.y)
    def drag(self,event):
        if self.last_drag:
            self.yaw+=(event.x-self.last_drag[0])*.4;self.elevation=float(np.clip(self.elevation+(event.y-self.last_drag[1])*.3,15,80))
            self.last_drag=(event.x,event.y);self.request_refresh()
    def scroll(self,event):self.zoom=float(np.clip(self.zoom*(1.1 if event.delta>0 else 1/1.1),.5,3));self.request_refresh()
    def close(self):
        self.running=False
        if hasattr(self,'_scene_executor'):self._scene_executor.shutdown(wait=False,cancel_futures=True)
        self.root.destroy()

def main():
    desktop_dpi()
    root=tk.Tk();root.withdraw()
    try:
        started=time.perf_counter()
        app=App(root);root.geometry('+10000+10000');root.deiconify();root.update()
        # Map and paint children outside the desktop before showing the complete
        # window. A fully transparent window can defer native child painting.
        app.refresh(force=True);root.update_idletasks()
        if app._restore_job is not None:root.after_cancel(app._restore_job);app._restore_job=None
        app._window_hidden=False;app.last_clock=time.perf_counter()
        timing={'ui_prepare_ms':(time.perf_counter()-started)*1000}
        if os.environ.get('SIMULATION_START_CLOCK'):timing['entry_to_prepared_frame_ms']=(time.perf_counter()-float(os.environ['SIMULATION_START_CLOCK']))*1000
        (ROOT/'reports/startup-timing.json').write_text(json.dumps(timing,indent=2),encoding='utf-8')
        root.geometry('+25+35');app.transitions_disabled=disable_window_transitions(root);root.mainloop()
    except Exception:
        log=ROOT/'reports/startup_error.log';log.write_text(traceback.format_exc(),encoding='utf-8')
        messagebox.showerror('启动失败','详情：'+str(log));root.destroy();raise
if __name__=='__main__':main()
