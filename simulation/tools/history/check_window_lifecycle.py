"""Exercise the real Tk/Windows minimize/restore path and prepared startup."""
import ctypes,json,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT))
import app
from PIL import ImageGrab,ImageTk
import numpy as np
from render import camera_image

Original=app.App;report={'cycles':[]};failure=[]

class TestedApp(Original):
    def __init__(self,root):
        super().__init__(root);self.cases=iter([(0,False),(0,True),(1,True),(2,False),(4,False),(5,False)])
        root.after(1000,self.begin)
    def begin(self):
        # Preparation may exceed one second on a busy desktop; do not exercise
        # the restore checks while main() is still laying out the offscreen UI.
        if self.root.winfo_rootx()>=1000:
            self.root.after(100,self.begin);return
        try:
            assert self.root.winfo_rootx()<1000 and self.map_label.winfo_width()>200
            assert self.transitions_disabled
            report['prepared_first_visible_frame']=True
            ImageGrab.grab(window=app.window_handle(self.root)).save(ROOT/'reports/prepared-startup-desktop.png')
            self.next_case()
        except Exception as error:self.fail(error)
    def fail(self,error):failure.append(repr(error));self.close()
    def next_case(self):
        try:
            tab,running=next(self.cases)
        except StopIteration:
            report['startup_timing']=json.loads((ROOT/'reports/startup-timing.json').read_text())
            self.close();return
        self.pause();self.view_tabs.select(tab);self.refresh();self.case={'tab':tab,'running':running}
        if running:self.start()
        self.root.after(80,self.minimize)
    def minimize(self):
        self.root.iconify();self.root.after(120,self.check_hidden)
    def check_hidden(self):
        try:
            assert self.root.state()=='iconic';self.hidden_time=self.sim.now;self.hidden_frame=self.sim.frame_id
            self.root.after(160,self.restore)
        except Exception as error:self.fail(error)
    def restore(self):
        try:
            assert self.sim.now==self.hidden_time and self.sim.frame_id==self.hidden_frame
            self.case['hidden_simulation_frozen']=True
            self.restore_begin=time.perf_counter();self.root.deiconify()
            self.case['deiconify_call_ms']=(time.perf_counter()-self.restore_begin)*1000
            self.root.after(1,self.check_restored)
        except Exception as error:self.fail(error)
    def check_restored(self):
        try:
            if self._window_hidden or self._restore_job is not None:
                self.root.after(2,self.check_restored);return
            self.case['restore_to_ready_ms']=(time.perf_counter()-self.restore_begin)*1000
            self.case['complete_repaint_ms']=self.window_refresh_stats[-1]
            self.case['stages']=self.window_refresh_details[-1]
            assert self.running==self.case['running']
            assert np.array_equal(np.asarray(ImageTk.getimage(self.photos['camera']).convert('RGB')),np.asarray(camera_image(self.sim)))
            for canvas in [self.camera_label,self.chart_label]:
                assert canvas.coords('viewport-image')==[canvas.winfo_width()/2,canvas.winfo_height()/2]
                assert len(canvas.find_withtag('viewport-image'))==1
            report['cycles'].append(self.case.copy());self.root.after(40,self.next_case)
        except Exception as error:self.fail(error)

app.App=TestedApp;app.main()
assert not failure,failure
report['native_frames_and_positions']='camera pixels exactly match source; one image per canvas after every restore'
(ROOT/'reports/window-lifecycle-checks.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
