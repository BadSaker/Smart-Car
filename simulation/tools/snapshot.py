"""Read-only provenance capture; all output stays beneath simulation."""
from pathlib import Path
import hashlib, json, shutil, zipfile, xml.etree.ElementTree as ET
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT.parent / 'Autodrive'
def inventory():
    return {str(p.relative_to(SOURCE)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(SOURCE.rglob('*')) if p.is_file()}
def main():
    for name in ('vendor/src', 'vendor/include', 'vendor/config', 'assets', 'reports', 'runs', 'build', 'configs', 'docs'):
        (ROOT/name).mkdir(parents=True, exist_ok=True)
    manifest = ROOT/'reports/source_manifest.json'
    if manifest.exists():
        old = json.loads(manifest.read_text(encoding='utf-8'))
        new = inventory()
        changes = [k for k in set(old)|set(new) if old.get(k)!=new.get(k)]
        result={'source_unchanged': not changes, 'file_count':len(new), 'changed': changes}
        (ROOT/'reports/source_integrity.json').write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf-8')
        print(json.dumps(result, ensure_ascii=False))
        return not changes
    manifest.write_text(json.dumps(inventory(), indent=2, ensure_ascii=False), encoding='utf-8')
    for category, names in {'src':['track_vision.c','autonomous_drive.c','control_pid.c'],
                            'include':['track_vision.h','autonomous_drive.h','control_pid.h'],
                            'config':['autonomous_config.h','vision_config.h','servo_config.h','encoder_config.h']}.items():
        for name in names: shutil.copyfile(SOURCE/'car'/category/name, ROOT/'vendor'/category/name)
    shutil.copyfile(SOURCE/'car/src/autonomous_control.c', ROOT/'vendor/autonomous_control_reference.c')
    license_file=ROOT.parent/'SeekFree/RT1064_Library/LICENSE'
    if license_file.exists(): shutil.copyfile(license_file,ROOT/'vendor/UPSTREAM_LICENSE')
    doc=ROOT.parent/'downloads/中期检查要求.docx'
    with zipfile.ZipFile(doc) as z:
        ns={'w':'http://schemas.openxmlformats.org/wordprocessingml/2006/main'}
        tree=ET.fromstring(z.read('word/document.xml'))
        text=[''.join(p.itertext()) for p in tree.findall('.//w:p',ns)]
        (ROOT/'assets/中期检查要求.txt').write_text('\n'.join(text),encoding='utf-8')
        for name in ('image1.png','image2.png'):
            (ROOT/'assets'/name).write_bytes(z.read('word/media/'+name))
    (ROOT/'reports/input_manifest.json').write_text(json.dumps({'document':str(doc),'sha256':hashlib.sha256(doc.read_bytes()).hexdigest()},indent=2,ensure_ascii=False),encoding='utf-8')
    print('Captured source hashes and immutable algorithm copies.')
    return True
if __name__=='__main__':
    import sys
    sys.exit(0 if main() else 1)
