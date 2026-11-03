"""Windows sandbox workaround: create test directories with inherited ACLs, not mode 0700."""
from pathlib import Path
import sys,tempfile,uuid,unittest
root=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(root/'car/tests'))
def inherited_directory(suffix=None,prefix=None,dir=None):
    p=Path(dir or tempfile.gettempdir())/((prefix or 'tmp')+uuid.uuid4().hex+(suffix or ''))
    p.mkdir(mode=0o777)
    return str(p)
tempfile.mkdtemp=inherited_directory
suite=(unittest.defaultTestLoader.discover(str(root/'car/tests')) if sys.argv[1:]==['--discover'] else unittest.defaultTestLoader.loadTestsFromNames(sys.argv[1:] or ['test_build_script']))
result=unittest.TextTestRunner(verbosity=2).run(suite)
raise SystemExit(not result.wasSuccessful())
