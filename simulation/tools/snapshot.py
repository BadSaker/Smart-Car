"""导出明确的算法输入；资料提取与源码快照分开。"""
from pathlib import Path
import argparse, hashlib, json, shutil, sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from source_layout import source_root,source_files
def inventory():
    source=source_root()
    return {str(p.relative_to(source)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files()}
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();output=args.output.resolve();source=source_root()
    if output==source or source in output.parents:parser.error('输出不能位于car源码目录')
    output.mkdir(parents=True,exist_ok=True)
    for file in source_files():
        target=output/'source'/file.relative_to(source);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,target)
    shutil.copy2(ROOT/'native_bridge.c',output/'native_bridge.c')
    shutil.copy2(ROOT/'vendor/UPSTREAM_LICENSE',output/'UPSTREAM_LICENSE')
    hashes=inventory();hashes['native_bridge.c']=hashlib.sha256((ROOT/'native_bridge.c').read_bytes()).hexdigest()
    (output/'manifest.json').write_text(json.dumps(hashes,indent=2),encoding='utf-8')
    print(f'Exported {len(hashes)} algorithm inputs.')
    return True
if __name__=='__main__':
    import sys
    sys.exit(0 if main() else 1)
