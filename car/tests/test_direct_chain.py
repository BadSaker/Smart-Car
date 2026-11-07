"""Regression for immediate PID drive and sparse visual observation."""
from pathlib import Path
import subprocess,unittest
from host_toolchain import find_compiler
ROOT=Path(__file__).resolve().parents[2]
class DirectChainTests(unittest.TestCase):
 def test_direct_controller_and_observation(self):
  tmp=ROOT/'tmp/vision-pid-20261106/direct';tmp.mkdir(parents=True,exist_ok=True)
  for name,sources,args in [('pid',['autonomous_drive','control_pid'],[[],['steering']]),('vision',['track_vision'],[[]]),('geometry_restore',['autonomous_drive','control_pid'],[[]])]:
   exe=tmp/(name+'.exe');cmd=[find_compiler(),'-std=c99','-Wall','-Wextra','-Werror','-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),str(ROOT/('car/tests/'+('test_geometry_restore.c' if name=='geometry_restore' else 'test_direct_'+name+'.c')))]
   cmd += [str(ROOT/('car/src/'+n+'.c')) for n in sources]+['-lm','-o',str(exe)]
   r=subprocess.run(cmd,capture_output=True,text=True,errors='replace');self.assertEqual(r.returncode,0,r.stderr)
   for a in args:
    r=subprocess.run([str(exe)]+a,capture_output=True,text=True,errors='replace');self.assertEqual(r.returncode,0,r.stdout+r.stderr)
