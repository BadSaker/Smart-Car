"""Download and verify the standalone x64 GCC release, then unpack locally."""
import hashlib,json,time,urllib.request,zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
release=json.loads((root/'downloads/release.json').read_text(encoding='utf-8'))
asset=next(a for a in release['assets'] if a['name'].endswith('.zip') and 'x86_64' in a['name'])
archive=root/'downloads'/asset['name']
expected=asset['digest'].split(':',1)[1]
digest=hashlib.sha256();total=0;last=time.monotonic()
with urllib.request.urlopen(asset['browser_download_url'],timeout=40) as response,archive.open('wb') as target:
    while chunk:=response.read(1024*1024):
        target.write(chunk);digest.update(chunk);total+=len(chunk)
        if time.monotonic()-last>10:
            print(f'Downloaded {total//1048576}/{asset["size"]//1048576} MiB',flush=True);last=time.monotonic()
assert total==asset['size'],'Download size mismatch'
assert digest.hexdigest()==expected,'SHA256 mismatch'
print('Release SHA256 verified; extracting toolchain.',flush=True)
with zipfile.ZipFile(archive) as package:
    for info in package.infolist():
        destination=(root/info.filename).resolve()
        destination.relative_to(root.resolve())
        assert info.filename.startswith('mingw64/'),info.filename
    package.extractall(root)
(root/'deployment.json').write_text(json.dumps({
    'source':release['html_url'],'archive_url':asset['browser_download_url'],
    'sha256':expected,'archive_bytes':total,'gcc_version':'14.3.0',
    'mingw_w64_version':'12.0.0','target':'x86_64-w64-mingw32','runtime':'UCRT',
    'thread_model':'POSIX'},indent=2),encoding='utf-8')
print('Standalone GCC extracted.',flush=True)
