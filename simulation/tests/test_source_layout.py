"""共享源码身份、参数覆盖和无编译器缓存加载。"""
import json,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import native
from parameters import defaults
from source_layout import source_files

class SourceLayoutTests(unittest.TestCase):
    def test_main_project_is_the_only_default_source(self):
        self.assertEqual(len(source_files()),11)
        for path in source_files():self.assertIn('car',path.parts)

    def test_cached_dll_loads_without_compiler_and_records_inputs(self):
        p=defaults();instance=native.Native(p)
        info=json.loads((native.ROOT/'build'/instance.version/'source.json').read_text())
        self.assertEqual(len(info),11)
        with patch('native.candidates',return_value=iter(())),patch.dict('os.environ',{},clear=True):
            cached=native.Native(p)
        self.assertEqual(cached.version,instance.version)

    def test_parameter_change_changes_dll_identity(self):
        p=defaults();first=native.Native(p);p['max_duty']=79
        second=native.Native(p);self.assertNotEqual(first.version,second.version)
        header=(native.ROOT/'build'/second.version/'autonomous_config.h').read_text(encoding='utf-8')
        self.assertIn('#define AUTO_MAX_DUTY_PERMILLE 79U',header)
