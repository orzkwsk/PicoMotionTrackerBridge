"""Deterministic software-only fixtures; no sockets, HMD or runtime control."""
import json
import math
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
RUNNER = Path(sys.argv.pop(1)).resolve()
FIXTURES = json.loads((ROOT / 'tests/fixtures/admission/scenarios.json').read_text())

def replay(config, frames):
    text = '\n'.join(json.dumps(x, allow_nan=False) for x in [{'config': config}] + frames) + '\n'
    result = subprocess.run([str(RUNNER)], input=text, text=True, capture_output=True, check=True)
    return [json.loads(s) for s in result.stdout.splitlines()][1:]

def frame(t, valid=True, sequence=0, **extra):
    return dict(timestamp_ns=t * 1_000_000, raw_valid=valid, sequence=sequence,
                position=[1, 2, 3], quaternion=[0, 0, 0, 1], **extra)

class AdmissionReplayTests(unittest.TestCase):
    def test_historical_mask_capture(self):
        directory=ROOT/'tests/fixtures/admission'
        manifest=json.loads((directory/'capture_manifest.json').read_text())
        import hashlib
        raw=(directory/manifest['fixture']).read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),manifest['fixture_sha256'])
        frames=[json.loads(x) for x in raw.splitlines()]
        self.assertEqual(len(frames),139);self.assertFalse(manifest['natural_reacquire'])
        c=dict(enabled=True,reacquire_confirm_ms=20,reacquire_min_samples=3)
        rows=replay(c,frames);self.assertEqual(rows,replay(c,frames))
        self.assertEqual(len(rows),139)
        for r,f in zip(rows,frames):
            self.assertEqual(r['raw_position'],f['position']);self.assertEqual(r['raw_quaternion'],f['quaternion'])
            self.assertFalse(f['capture_metadata']['original_core_pose_submitted'])
            if not r['effective_valid']:
                self.assertIsNone(r['protocol']['position']);self.assertIsNone(r['protocol']['orientation'])
        self.assertEqual(rows[0]['admission_state'],'CANDIDATE')
        self.assertIn('ADMITTED',[r['admission_state'] for r in rows])
        c['enabled']=False
        for r,f in zip(replay(c,frames),frames):
            self.assertTrue(r['effective_valid']);self.assertEqual(r['protocol']['position'],f['position'])

    def test_scenarios(self):
        for case in FIXTURES:
            with self.subTest(case=case['name']):
                frames=[]
                for i, sample in enumerate(case['samples']):
                    sample=dict(sample)
                    f=frame(sample.pop('ms'), sample.pop('valid'), i)
                    f.update(sample); frames.append(f)
                rows=replay(case['config'],frames)
                self.assertEqual(rows,replay(case['config'],frames),'nondeterministic replay')
                self.assertEqual([r['admission_state'] for r in rows],case['states'])
                baseline=rows[0]['protocol']
                for r, f in zip(rows,frames):
                    v=r['protocol']
                    self.assertEqual(r['raw_position'],f['position'])
                    self.assertEqual(r['raw_quaternion'],f['quaternion'])
                    for key in ('capabilities','source_id','device_id','session_id','clock_id','coordinate_space'):
                        self.assertEqual(v[key],baseline[key],key)
                    self.assertEqual(v['validity'],dict(position=r['effective_valid'],orientation=r['effective_valid']))
                    self.assertEqual(v['modality'],'full' if r['effective_valid'] else 'none')
                    self.assertEqual(r['raw_valid'],f['raw_valid'])
                    if case['config']['enabled'] and not r['effective_valid']:
                        self.assertIsNone(v['position']); self.assertIsNone(v['orientation'])
                        self.assertIsNone(v.get('linear_velocity'))
                    for key in ('sample_dt_ms','position_step_m','quaternion_step_rad','linear_speed_mps','angular_speed_radps'):
                        self.assertTrue(r[key] is None or math.isfinite(r[key]),key)
                if case['name']=='q_sign_flip':
                    self.assertLess(max(r['quaternion_step_rad'] or 0 for r in rows),1e-7)
                if case['name']=='quaternion_jump_then_settle':
                    self.assertGreater(max(r['quaternion_step_rad'] or 0 for r in rows),3)
                if case['name']=='position_jump_then_settle':
                    self.assertGreater(max(r['position_step_m'] or 0 for r in rows),10)
                if case['name']=='duplicate_timestamp':
                    self.assertEqual([r['candidate_samples'] for r in rows],[1,1,2,3])
                if not case['config']['enabled']:
                    for r,f in zip(rows,frames):
                        self.assertEqual(r['effective_valid'],f['raw_valid'])
                        self.assertEqual(r['protocol']['position'],f['position'])

    def test_duplicate_sequence_and_device_isolation(self):
        c=dict(enabled=True,reacquire_confirm_ms=20,reacquire_min_samples=3)
        rows=replay(c,[frame(0,sequence=1),frame(10,sequence=1),frame(20,sequence=2),frame(30,sequence=3)])
        self.assertTrue(rows[1]['dropped']);self.assertEqual(rows[-1]['admission_state'],'ADMITTED')
        rows=replay(c,[frame(0,sequence=1),frame(10,sequence=1,device_id='second'),frame(20,sequence=2)])
        self.assertEqual([r['candidate_samples'] for r in rows],[1,1,2])

    def test_new_process_and_independent_session_reset(self):
        c=dict(enabled=True,reacquire_confirm_ms=0,reacquire_min_samples=2)
        f=frame(0,sequence=1)
        self.assertEqual(replay(c,[f])[0]['admission_state'],'CANDIDATE')
        other='00000000-0000-4000-8000-000000000003'
        rows=replay(c,[f,frame(10,sequence=2),frame(20,sequence=0,session_id=other)])
        self.assertEqual([r['admission_state'] for r in rows],['CANDIDATE','ADMITTED','CANDIDATE'])

    def test_invalid_config_and_input(self):
        for c in ({'enabled':1},{'reacquire_confirm_ms':-1},{'reacquire_min_samples':1.5},
                  {'reacquire_confirm_ms':9223372036854775807},{'reacquire_min_samples':18446744073709551615}, {'unknown':True}):
            with self.subTest(config=c), self.assertRaises(subprocess.CalledProcessError):replay(c,[])
        with self.assertRaises(subprocess.CalledProcessError):replay({},[frame(-1)])

if __name__=='__main__':unittest.main()
