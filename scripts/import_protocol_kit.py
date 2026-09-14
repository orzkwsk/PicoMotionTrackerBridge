"""Import the actual Task 1 handoff, or verify the already vendored kit."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

ROOT = Path(__file__).resolve().parents[1]
C1 = '3a76435c8b25b3975028f9a83c4dc3d1e3848806c9608d885f5bba74a1198ed3'
SOURCE = '5f41586b51bd84bb1cea344879b5d6325fc2c47d'
SCHEMA = '04f2d6c831c68a65edfbfb66ff8838d1c9d78535'
def sha(b): return hashlib.sha256(b).hexdigest()
def verify(files, manifest):
    assert manifest['source_commit'] == SOURCE and manifest['schema_commit'] == SCHEMA
    assert manifest['contract_c1_sha256'] == C1
    assert sha(files['protocol.lock.json']) == manifest['protocol_lock_sha256']
    lock = json.loads(files['protocol.lock.json'])
    assert lock['source_commit'] == SOURCE and lock['schema_commit'] == SCHEMA
    assert lock['source_tree_clean'] and lock['wire_version'] == {'major': 1, 'minor': 0}
    for name, digest in lock['files_sha256'].items(): assert sha(files[name]) == digest, name
    sums = dict(line.split('  ', 1)[::-1] for line in files['SHA256SUMS'].decode().splitlines())
    assert set(sums) | {'SHA256SUMS'} == set(files)
    for name, digest in sums.items(): assert sha(files[name]) == digest, name
    assert sha(files['docs/C1.md']) == C1
    text = (ROOT/'docs/codex/task2/02_common_contract_and_report.md').read_text(encoding='utf-8')
    contract = text.split('<!-- BEGIN COMMON C1 -->\n', 1)[1].split('<!-- END COMMON C1 -->', 1)[0]
    assert sha(contract.encode()) == C1, 'Task 2 C1 differs from delivered kit'
    return lock

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--zip', type=Path); p.add_argument('--manifest', type=Path)
    p.add_argument('--verify', action='store_true'); a = p.parse_args()
    dest = ROOT/'third_party/monaka-protocol'; dep = ROOT/'dependencies'
    if a.verify:
        manifest = json.loads((dep/'monaka-protocol.handoff.json').read_bytes())
        blob = (dep/'artifacts/monaka-protocol-kit-v1.0.zip').read_bytes()
        assert sha(blob) == manifest['sha256']
        files = {x.relative_to(dest).as_posix(): x.read_bytes() for x in dest.rglob('*') if x.is_file()}
        verify(files, manifest)
        assert (dep/'monaka-protocol.lock.json').read_bytes() == files['protocol.lock.json']
    else:
        assert a.zip and a.manifest, 'Actual ZIP and manifest paths are required'
        assert not dest.exists(), 'Refuse to overwrite an existing kit'
        blob = a.zip.read_bytes(); manifest = json.loads(a.manifest.read_bytes())
        assert sha(blob) == manifest['sha256'], 'ZIP SHA256 mismatch'
        with zipfile.ZipFile(a.zip) as z:
            names = z.namelist()
            assert len(names) == len(set(names))
            for name in names:
                path = PurePosixPath(name)
                assert not path.is_absolute() and '..' not in path.parts and ':' not in name and '\\' not in name
            files = {name: z.read(name) for name in names}
        verify(files, manifest)
        for name, data in files.items():
            target = dest/name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
        (dep/'artifacts').mkdir(parents=True, exist_ok=True)
        (dep/'artifacts/monaka-protocol-kit-v1.0.zip').write_bytes(blob)
        (dep/'monaka-protocol.handoff.json').write_bytes(a.manifest.read_bytes())
        (dep/'monaka-protocol.lock.json').write_bytes(files['protocol.lock.json'])
    print('PASS Task 1 ZIP, source/schema commits, C1, lock and all included file hashes: '+manifest['sha256'])

if __name__ == '__main__': main()
