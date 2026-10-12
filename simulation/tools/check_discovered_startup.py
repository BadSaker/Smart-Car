"""Integration check launched through the interpreter discovery entry point."""
import sys,json,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import app
Original=app.App
class TestedApp(Original):
    def __init__(self,root):
        super().__init__(root);root.after(100,self.check_ready)
    def check_ready(self):
        if self.root.winfo_rootx()>=1000:
            self.root.after(100,self.check_ready);return
        assert self.root.winfo_viewable()
        assert self.map_label.winfo_width()>200 and 'camera' in self.photos
        (app.ROOT/'reports/discovered-startup-check.json').write_text(json.dumps({
            'passed':True,'executable':sys.executable,'window_ready':True,
            'camera_size':[self.photos['camera'].width(),self.photos['camera'].height()],
            'algorithm_version':self.sim.native.version},indent=2),encoding='utf-8')
        self.close()
app.App=TestedApp
app.main()
