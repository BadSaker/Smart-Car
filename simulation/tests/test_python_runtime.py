"""Launcher must never fall back to an external Python or user packages."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHELL = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'


@unittest.skipUnless(os.name == 'nt', 'Windows launcher')
class PythonRuntimeTests(unittest.TestCase):
    def invoke(self, root, runtime, **extra):
        env = dict(os.environ, SIMULATION_RUNTIME_ROOT=str(runtime))
        env.update(extra)
        return subprocess.run([str(SHELL), '-NoProfile', '-ExecutionPolicy', 'Bypass',
                               '-File', str(root / 'launch.ps1'), '-CheckOnly'],
                              env=env, capture_output=True, text=True, encoding='utf-8',
                              errors='replace', timeout=45)

    def test_missing_local_python_does_not_use_cached_external_interpreter(self):
        with tempfile.TemporaryDirectory(prefix='simulation-launch-') as folder:
            root = Path(folder) / '中文 空格'
            root.mkdir()
            for name in ('launch.ps1', 'run_python.ps1', 'python_environment.ps1', 'runtime_check.py', 'python-runtime.json'):
                if (ROOT / name).exists():
                    shutil.copy2(ROOT / name, root / name)
            runtime = root / 'runtime'
            runtime.mkdir()
            # The test runner is usable but is external to this simulated project.
            import sys
            (runtime / 'launcher-runtime.json').write_text(json.dumps({'executable': sys.executable}))
            result = self.invoke(root, runtime, SIMULATION_PYTHON=sys.executable)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn('setup_python.ps1', (runtime / 'launcher.log').read_text(encoding='utf-8-sig'))

    def test_local_runtime_ignores_cached_path_and_environment_injection(self):
        with tempfile.TemporaryDirectory(prefix='simulation-isolation-') as folder:
            runtime = Path(folder)
            (runtime / 'launcher-runtime.json').write_text(json.dumps({'executable': 'D:/anaconda3/python.exe'}))
            result = self.invoke(ROOT, runtime, SIMULATION_PYTHON='D:/anaconda3/python.exe',
                                 PYTHONHOME='Z:/missing-python', PYTHONPATH='Z:/foreign-packages',
                                 PYTHONUSERBASE='Z:/foreign-user', TCL_LIBRARY='Z:/foreign-tcl',
                                 TK_LIBRARY='Z:/foreign-tk')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            info = json.loads((runtime / 'launcher-runtime.json').read_text(encoding='utf-8-sig'))
            self.assertEqual(Path(info['executable']).resolve(), (ROOT / 'python/python.exe').resolve())
            self.assertFalse(info['user_site_enabled'])
            for name, path in info['modules'].items():
                self.assertTrue(Path(path).resolve().is_relative_to(ROOT / 'python'), (name, path))

    def test_command_wrapper_preserves_python_switches_and_exit_code(self):
        result = subprocess.run([str(SHELL), '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                 '-File', str(ROOT / 'run_python.ps1'), '-c', 'import sys; sys.exit(7)'],
                                capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 7, result.stderr)

    def test_setup_ignores_external_pip_configuration(self):
        with tempfile.TemporaryDirectory(prefix='simulation-pip-') as folder:
            config = Path(folder) / 'pip.ini'
            config.write_text('[global]\nrequire-virtualenv = true\n')
            env = dict(os.environ, PIP_CONFIG_FILE=str(config), SIMULATION_RUNTIME_ROOT=folder)
            result = subprocess.run([str(SHELL), '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                     '-File', str(ROOT / 'setup_python.ps1')], env=env,
                                    capture_output=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
