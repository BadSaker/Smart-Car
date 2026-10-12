"""跨设备外部参数登记、初始化时序与源码保护。"""
import json,os,sys,tempfile,unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch,Mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import app
from parameters import defaults

class RegistryTests(unittest.TestCase):
    def setUp(self):
        parent=Path(os.environ.get('AUTOCAR_TEST_ROOT',Path(__file__).resolve().parents[2]/'tmp'))/'registry-tests'
        parent.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=parent)
        self.root=Path(self.temp.name)/'simulation';(self.root/'configs').mkdir(parents=True)
        self.dummy=SimpleNamespace(extra_presets={},parameter_selector=Mock())
    def tearDown(self):self.temp.cleanup()

    def test_stale_registry_is_safe_before_message_widget_exists(self):
        (self.root/'parameter_locations.local.json').write_text(json.dumps({'missing':'gone.json'}),encoding='utf-8')
        with patch('app.ROOT',self.root):app.App.update_presets(self.dummy)
        self.assertIn('missing',self.dummy.preset_warning)

    def test_external_same_name_does_not_replace_builtin(self):
        builtin=self.root/'configs/我的参数.json';builtin.write_text('{}',encoding='utf-8')
        external=self.root/'external.json';external.write_text('{}',encoding='utf-8')
        (self.root/'parameter_locations.local.json').write_text(json.dumps({'我的参数':'external.json'}),encoding='utf-8')
        with patch('app.ROOT',self.root):app.App.update_presets(self.dummy)
        self.assertEqual(self.dummy.preset_paths['我的参数'],builtin)
        self.assertEqual(self.dummy.preset_paths['我的参数 [外部]'],external)

    def test_save_cannot_write_into_car_source(self):
        target=self.root.parent/'car/config/source.json'
        self.dummy.collect=lambda:defaults();self.dummy.root=None
        self.dummy.parameter_name=Mock();self.dummy.parameter_name.get.return_value='默认参数'
        with patch('app.ROOT',self.root),patch('app.filedialog.asksaveasfilename',return_value=str(target)),patch('app.messagebox.showerror') as error:
            app.App.save_params(self.dummy)
        error.assert_called_once();self.assertFalse(target.exists())

    def test_save_external_keeps_source_label_and_relative_registry(self):
        target=self.root/'outside.json'
        self.dummy.collect=lambda:defaults();self.dummy.root=None;self.dummy.message=Mock()
        self.dummy.parameter_name=Mock();self.dummy.parameter_name.get.return_value='默认参数'
        self.dummy.update_presets=lambda:app.App.update_presets(self.dummy)
        with patch('app.ROOT',self.root),patch('app.filedialog.asksaveasfilename',return_value=str(target)),patch('app.messagebox.showerror') as error:
            app.App.save_params(self.dummy)
        error.assert_not_called();self.dummy.parameter_name.set.assert_called_with('outside [外部]')
        registry=json.loads((self.root/'parameter_locations.local.json').read_text(encoding='utf-8'))
        self.assertEqual(registry,{'outside':'outside.json'})
