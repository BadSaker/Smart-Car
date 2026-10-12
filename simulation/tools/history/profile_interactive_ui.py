"""Launch the real UI and record event-loop/render timing during manual clicks."""
import sys, time, json
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import app

rows=[]
original=app.App
class ProfileApp(original):
    def __init__(self, root):
        super().__init__(root)
        self.probe_last=time.perf_counter()
        root.after(16,self.probe)
    def mark(self, kind, elapsed):
        rows.append({'kind':kind,'ms':elapsed*1000,'clock':round(time.perf_counter(),4),
                     'view':self.view_tabs.tab(self.view_tabs.select(),'text'),
                     'running':self.running,'sim_ms':self.sim.now})
    def probe(self):
        now=time.perf_counter()
        self.mark('heartbeat',now-self.probe_last);self.probe_last=now
        if not hasattr(self,'last_write') or now-self.last_write>2:
            self.last_write=now
            (app.ROOT/'reports/interactive-ui-profile.json').write_text(json.dumps(rows[-30000:]),encoding='utf-8')
        self.root.after(16,self.probe)
    def refresh(self,*args,**kwargs):
        start=time.perf_counter()
        try:return super().refresh(*args,**kwargs)
        finally:self.mark('refresh',time.perf_counter()-start)
    def loop(self):
        start=time.perf_counter()
        try:return super().loop()
        finally:self.mark('loop',time.perf_counter()-start)
app.App=ProfileApp
app.main()
