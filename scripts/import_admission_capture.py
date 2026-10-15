"""Extract the historical 139 capture-only frames; never operate a device.

Input: 2C-3H fixed-root-controlled-02/root-observations.jsonl.
Output is real canonical Bridge-mapped PICO pose, NOT native-space or natural
SLAM reacquire evidence. The original mask remains an external test condition.
"""
import argparse
import hashlib
import json
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'tests/fixtures/admission')
    args=parser.parse_args();rows=[];total=0;epoch=None
    with args.source.open(encoding='utf-8') as f:
        for line in f:
            r=json.loads(line);total+=1;raw=r['rawPicoCaptureOnly']
            if r['coreMainPoseSubmitted'] or not raw['usableForComparison']:continue
            if r['coreDiagnosticRawPose']!={'positionMeters':None,'quaternionXyzw':None}:
                raise ValueError('historical mask unexpectedly fed raw pose to Core')
            if epoch is None:epoch=raw['receivedAtNanos']
            rows.append(dict(timestamp_ns=raw['receivedAtNanos']-epoch,sequence=raw['nativeMtpSequence'],
                raw_valid=all(raw['sourceReportedValidity'].values()),position=raw['positionMeters'],
                quaternion=raw['quaternionXyzw'],coordinate_space=raw['coordinateSpace'],
                capture_metadata=dict(original_frame_sequence=r['sequence'],raw_wire_sha256=r['rawWireSha256'],
                    received_at_ns=raw['receivedAtNanos'],sample_at_ns=raw['sampleAtNanos'],
                    original_core_pose_submitted=False)))
    if total!=977 or len(rows)!=139:raise ValueError(f'unexpected capture inventory: {total}/{len(rows)}')
    args.output.mkdir(parents=True,exist_ok=True)
    dest=args.output/'mask_time_139.jsonl'
    dest.write_text(''.join(json.dumps(r,separators=(',',':'),allow_nan=False)+'\n' for r in rows),encoding='utf-8')
    manifest=dict(classification='REAL MASK-TIME CAPTURE / NOT NATURAL REACQUIRE EVIDENCE',
        source_run='Phase 2C-3H / fixed-root-controlled-02',source_relative_path='hil/phase2c3h-root-fallback-20261015-005804-f52dc350/fixed-root-controlled-02/root-observations.jsonl',
        source_sha256=hashlib.sha256(args.source.read_bytes()).hexdigest(),source_frames=total,
        extracted_frames=len(rows),fixture=dest.name,fixture_sha256=hashlib.sha256(dest.read_bytes()).hexdigest(),
        original_core_pose_feedthrough_frames=0,coordinate_semantics='Existing Bridge canonical mapping; no new transform applied',
        native_tracking_state_available=False,natural_reacquire=False)
    (args.output/'capture_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(manifest))
if __name__=='__main__':main()
