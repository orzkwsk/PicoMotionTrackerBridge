"""Verify provenance, closure and ZIP integrity; optionally build in an empty directory."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tempfile
import zipfile
ROOT=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--build',action='store_true');p.add_argument('--cmake',default='cmake')
    a=p.parse_args(); handoff=json.loads((ROOT/'dist/pico-bridge-extraction.handoff.json').read_bytes())
    archive=ROOT/'dist'/handoff['file'];assert sha(archive.read_bytes())==handoff['sha256']
    with zipfile.ZipFile(archive) as z:
        names=z.namelist();assert len(names)==len(set(names))
        assert all(not PurePosixPath(n).is_absolute() and '..' not in PurePosixPath(n).parts and ':' not in n and '\\' not in n for n in names)
        files={n:z.read(n) for n in names}
    manifest=json.loads(files['extraction-manifest.json'])
    assert sha(files['extraction-manifest.json'])==handoff['manifest_sha256']
    assert all(not n.startswith(('src/runtime/','src/internal/','BackendTestApp/')) for n in files)
    sums=dict(line.split('  ',1)[::-1] for line in files['SHA256SUMS'].decode().splitlines())
    assert set(sums)|{'SHA256SUMS'}==set(files)
    for n,h in sums.items():assert sha(files[n])==h,n
    for row in manifest['files']:
        data=files[row['path']];assert sha(data)==row['sha256']
        if not row['generated']:
            blob_hash=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
            assert blob_hash==row['source_blob_sha'],row['path']
            assert row['source_base_sha']==manifest['source_base_sha']
    actual_edges=set()
    for name,data in files.items():
        if not name.endswith(('.cpp','.hpp','.h')):continue
        for include in re.findall(r'^\s*#\s*include\s*"([^"]+)"',data.decode('utf-8'),re.M):
            candidates=[str(PurePosixPath(name).parent/include),'include/'+include,'outputs/steamvr/common/'+include,'outputs/steamvr/driver/'+include]
            target=next((n for n in candidates if n in files),None)
            assert target,(name,include)
            actual_edges.add((name,include,target))
    assert actual_edges=={(e['from'],e['include'],e['to']) for e in manifest['include_closure']}
    if a.build:
        with tempfile.TemporaryDirectory(prefix='pico-bridge-extraction-') as folder:
            root=Path(folder);assert root.resolve().parent==Path(tempfile.gettempdir()).resolve()
            for n,data in files.items():
                target=root/n;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
            for cmd in [[a.cmake,'-S','.', '-B','build','-DPICO_OT_BUILD_TESTS=ON'],
                        [a.cmake,'--build','build','--config','Debug'],
                        [str(Path(a.cmake).with_name('ctest.exe' if Path(a.cmake).suffix=='.exe' else 'ctest')),'--test-dir','build','-C','Debug','--output-on-failure']]:
                subprocess.run(cmd,cwd=root,check=True)
    report={'hash_and_provenance':'PASS','include_closure':'PASS','file_count':len(manifest['files']),
            'include_edges':len(actual_edges),'standalone_build':'PASS' if a.build else 'NOT RUN','zip_sha256':handoff['sha256']}
    (ROOT/'dist/extraction-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
