"""使用行为确定的工具替身检验真实 PowerShell 构建与导出流程的边界。

工具替身用于替代外部 Keil/fromelf 进程，构建脚本逻辑保持原样。
它们模拟真实工具创建输出文件、记录状态并返回退出码。
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / 'tmp' / 'build-script-tests'
COMPILER = os.environ.get('AUTOCAR_HOST_CC', r'D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe')
SHELL = shutil.which('pwsh') or shutil.which('powershell')
FAKE_TOOL = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void put(const char *p, const char *v) {
    FILE *f = fopen(p, "wb");
    if (!f) exit(91);
    fputs(v, f); fclose(f);
}
int main(int argc, char **argv) {
    int i;
    if (argc > 1 && strcmp(argv[1], "--bin") == 0) {
        if (getenv("FAKE_EXPORT_FAILURE")) return 42;
        for (i = 1; i + 1 < argc; ++i)
            if (strcmp(argv[i], "--output") == 0) put(argv[i+1], "new-bin");
        return 0;
    }
    for (i = 1; i + 1 < argc; ++i)
        if (strcmp(argv[i], "-o") == 0)
            put(argv[i+1], "autocar.axf - 0 Error(s), 0 Warning(s).\n");
    put("..\\tmp\\build-autocar\\objects\\autocar.axf", "new-axf");
    put("..\\tmp\\build-autocar\\objects\\autocar.hex", "new-hex");
    put("..\\tmp\\build-autocar\\listings\\autocar.map", "test-linker-map");
    return 0;
}
'''


class BuildExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        source = SCRATCH / 'fake_tool.c'
        source.write_text(FAKE_TOOL, encoding='ascii')
        cls.tool = SCRATCH / 'fake_tool.exe'
        subprocess.run([COMPILER, str(source), '-o', str(cls.tool)], check=True)

    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix=self._testMethodName + '-', dir=SCRATCH))
        self.script = self.work / 'car/scripts/build.ps1'
        self.script.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / 'car/scripts/build.ps1', self.script)
        (self.work / 'car/autocar.uvprojx').write_text('test fixture', encoding='ascii')
        self.keil = self.work / 'tools/UV4/UV4.exe'
        self.keil.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(self.tool, self.keil)
        self.exporter = self.work / 'tools/ARM/ARMCLANG/bin/fromelf.exe'
        self.out = self.work / 'outputs/firmware'
        self.out.mkdir(parents=True, exist_ok=True)
        for ext in ('axf', 'hex', 'bin'):
            (self.out / ('autocar.' + ext)).write_text('old-' + ext, encoding='ascii')

    def run_build(self, export_failure=False):
        env = os.environ.copy()
        env.pop('FAKE_EXPORT_FAILURE', None)
        if export_failure:
            env['FAKE_EXPORT_FAILURE'] = '1'
        return subprocess.run([SHELL, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                               str(self.script), '-KeilPath', str(self.keil)],
                              cwd=self.work, env=env, capture_output=True, text=True,
                              encoding='utf-8', errors='replace')

    def add_exporter(self):
        self.exporter.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(self.tool, self.exporter)

    def assert_artifacts(self, prefix):
        for ext in ('axf', 'hex', 'bin'):
            self.assertEqual((self.out / ('autocar.' + ext)).read_text(), prefix + '-' + ext)

    def test_missing_exporter_fails_without_publishing(self):
        self.assertNotEqual(self.run_build().returncode, 0)
        self.assert_artifacts('old')

    def test_export_failure_preserves_previous_complete_set(self):
        self.add_exporter()
        self.assertNotEqual(self.run_build(export_failure=True).returncode, 0)
        self.assert_artifacts('old')

    def test_success_publishes_all_three_new_files(self):
        self.add_exporter()
        result = self.run_build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_artifacts('new')

    def test_linker_map_stays_temporary_and_preserves_reports(self):
        self.add_exporter()
        reports = self.work / 'outputs/reports'
        reports.mkdir(parents=True, exist_ok=True)
        important = reports / 'validation-2026-09-30.md'
        important.write_text('reviewed report', encoding='utf-8')
        result = self.run_build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.work / 'tmp/build-autocar/listings/autocar.map').read_text(),
                         'test-linker-map')
        self.assertEqual({p.name for p in reports.iterdir()}, {important.name})
        self.assertEqual(important.read_text(), 'reviewed report')


if __name__ == '__main__':
    unittest.main(verbosity=2)
