#!/usr/bin/env python3
"""Build-time metadata; final executable hash lives in a companion metadata file."""
import argparse
import datetime
import hashlib
import json
import pathlib
import subprocess

def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--binary', type=pathlib.Path)
    args = parser.parse_args()
    if args.binary:
        metadata = json.loads((args.output / 'pico-build-provenance.json').read_text())
        metadata['binary_sha256'] = hashlib.sha256(args.binary.read_bytes()).hexdigest()
        args.binary.with_suffix('.build-provenance.json').write_text(json.dumps(metadata, indent=2)+'\n', encoding='utf-8')
        return
    metadata = dict(component_name='PicoMotionTrackerBridge', version=(args.source/'VERSION').read_text().strip(),
                    git_commit=git(args.source, 'rev-parse', 'HEAD'),
                    git_branch_or_ref=git(args.source, 'rev-parse', '--abbrev-ref', 'HEAD'),
                    dirty_at_build=bool(git(args.source, 'status', '--porcelain', '--untracked-files=normal')),
                    build_timestamp_utc=datetime.datetime.now(datetime.timezone.utc).isoformat().replace('+00:00','Z'),
                    binary_sha256=None, build_system='CMake/C++17')
    args.output.mkdir(parents=True, exist_ok=True)
    encoded = json.dumps(metadata, ensure_ascii=True, separators=(',', ':'))
    (args.output/'pico_diagnostic_build.hpp').write_text('#pragma once\ninline constexpr char kPicoDiagnosticBuildJson[] = '+json.dumps(encoded)+';\n', encoding='utf-8')
    (args.output/'pico-build-provenance.json').write_text(json.dumps(metadata, indent=2)+'\n', encoding='utf-8')

if __name__ == '__main__':
    main()
