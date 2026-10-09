import hashlib
import json
import pathlib
import subprocess
import sys

exe = pathlib.Path(sys.argv[1])
root = pathlib.Path(sys.argv[2])
metadata = json.loads(subprocess.check_output([str(exe), '--version', '--json'], text=True))
head = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
assert metadata['git_commit'] == head, 'build embeds a stale Git SHA'
assert isinstance(metadata['dirty_at_build'], bool)
assert metadata['component_name'] == 'PicoMotionTrackerBridge'
sidecar = json.loads(exe.with_suffix('.build-provenance.json').read_text())
assert sidecar['binary_sha256'] == hashlib.sha256(exe.read_bytes()).hexdigest()
for key, value in metadata.items():
    if key != 'binary_sha256':
        assert sidecar[key] == value, key
assert metadata['binary_sha256'] is None, 'self-referential binary hashes must not be fabricated'
print('PASS exact embedded SHA and executable SHA256 companion mapping; dirty_at_build=' + str(metadata['dirty_at_build']))
