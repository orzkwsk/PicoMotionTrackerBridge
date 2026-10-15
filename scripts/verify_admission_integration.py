"""Offline admission -> actual P1/P1V libraries -> unchanged Core file replay.

Explicit fixture mapping uses the P1 MTP fixture and its real codec. No live
Bridge, receiver endpoint, device, Core state command, or production transform.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

# Pinned external source/artifact directories are read-only inputs.
sys.dont_write_bytecode = True

def read(p):return json.loads(p.read_text(encoding='utf-8'))
def save(p,x):p.write_text(json.dumps(x,indent=2,allow_nan=False)+'\n',encoding='utf-8');return p
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def invoke(*args):return subprocess.check_output([str(x) for x in args],text=True).strip()
def check(condition,message):
    if not condition:raise RuntimeError(message)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace',type=Path,required=True)
    parser.add_argument('--runner',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();root=args.workspace.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    mv=root/'development/MonakaVRInputProtocol';mb=root/'development/MonakaBridgeInputProtocol'
    heads={str(p.relative_to(root)):invoke('git','-C',p,'rev-parse','HEAD') for p in (mv,mb)}
    check(list(heads.values())==['6dfa595fa308fea64880fb5f501073feed9a726a','3ea3194479cfa1a04b0fcbcf04916186efb4f9d3'],'P1 source pin mismatch')
    project=mv/'build/native/Release/mvip_capabilities_runner.exe'
    normalize=mb/'build/native/Release/mbip_capabilities_runner.exe'
    encode=mb/'build/native/Release/mvip_boundary_encoder.exe'
    fixtures=mb/'tests/fixtures/p1'
    frames=[dict(timestamp_ns=ms*1_000_000,sequence=i,raw_valid=not 820<=ms<=1000,
        position=[1,2,3],quaternion=[0,0,0,1],source_id='backend-A',device_id='device-01')
        for i,ms in enumerate(range(0,2020,20))]
    config=dict(enabled=True,reacquire_confirm_ms=60,reacquire_min_samples=4)
    payload=''.join(json.dumps(j)+'\n' for j in [dict(config=config)]+frames)
    result=subprocess.run([str(args.runner.resolve())],input=payload,text=True,capture_output=True,check=True)
    (out/'admission.jsonl').write_text(result.stdout,encoding='utf-8')
    rows=[json.loads(s) for s in result.stdout.splitlines()][1:]
    observation_binding=read(fixtures/'observation-contract.binding.json')
    observation_binding['session']=rows[0]['session_id']
    ob=save(out/'observation.binding.json',observation_binding)
    template=read(fixtures/'legacy-bridge-second-derived-pose.json')
    mtp_binding=read(fixtures/'legacy-bridge-contract.binding.json')
    mtp_binding['declaration']=rows[0]['capabilities'];mtp_binding['session']=rows[0]['session_id']
    tb=save(out/'tracking.binding.json',mtp_binding)
    normalized=[];channel_rows=[]
    for i,row in enumerate(rows):
        v=row['protocol'];path=save(out/f'observation-{i:03}.json',v)
        projection=json.loads(invoke(project,path));n=json.loads(invoke(normalize,ob,path,'STRICT'))
        for facts in (projection['channels'],n['channels']):
            for key in ('position','orientation'):
                check(facts[key]['supported'] and facts[key]['valid']==row['effective_valid'],'capability/effective contract failed')
        check(n['wire']==v,'normalizer mutated observation')
        # Fixture adapter only: all pose validity and numeric values originate in
        # production ApplyPicoAdmission output. Coordinates are synthetic identity.
        m=copy.deepcopy(template)
        for key in ('version','source_id','session_id','clock_id','sequence','timestamp_ns','sent_at_ns',
                    'timestamp_kind','position','orientation','validity','tracking_state','modality','capabilities'):
            m[key]=v[key]
        m['coordinate_space']=dict(id='pico-offline-core-space',convention='rh_y_up_neg_z_forward',revision=42)
        m['mapping_revision']=17
        m['confidence']=dict(position=1.0 if row['effective_valid'] else 0.0,orientation=.5 if row['effective_valid'] else 0.0)
        m['input'].update(source_id=v['source_id'],device_id=v['device_id'],session_id=v['session_id'],
            sequence=v['sequence'],orientation_evidence=v['orientation_evidence'])
        m['linear_velocity']=v['linear_velocity']['value'] if v.get('linear_velocity') else None
        m['angular_velocity']=None;m['linear_acceleration']=None
        mp=save(out/f'mtp-{i:03}.json',m)
        encoded=invoke(encode,mp);check(not encoded.startswith('ERROR:'),'P1 MTP codec rejected fixture mapping')
        n=json.loads(invoke(normalize,tb,mp,'STRICT'))
        check(n['wire']==m,'normalizer mutated MTP')
        check(n['channels']['position']['supported'] and n['channels']['position']['valid']==row['effective_valid'],'Core-facing P fact failed')
        normalized.append(n['wire']);channel_rows.append(dict(sequence=i,admission=row['admission_state'],channels=n['channels']))
    save(out/'protocol-channel-results.json',channel_rows)
    package=root/'hil/phase2c3r-angular-recovery-20261015/candidate-final-package'
    jar=package/'lib/core.jar'
    check(sha(jar)=='ef37474a804ea0c3705fe556d21c97861fc4c1fd8feb1b622c42130c8f061863','Core artifact pin mismatch')
    core_config=read(root/'hil/phase2c3r-angular-recovery-20261015/core.hil.config.json')
    p=normalized[0];core_config['space']=p['coordinate_space']
    core_config['mainSourceId']='mtp:'+''.join(f'{len(p[k])}:{p[k]}' for k in ('publisher_id','source_id','tracker_id'))
    cp=save(out/'core.config.json',core_config)
    spec=importlib.util.spec_from_file_location('offline_core_control',package/'hil-control.py')
    control=importlib.util.module_from_spec(spec);spec.loader.exec_module(control)
    runs=[]
    for repeat in range(2):
        session=out/f'core-replay-{repeat}';results=[];started=False
        try:
            control.start(session,cp,False);started=True
            for i,(row,m) in enumerate(zip(rows,normalized)):
                t=frames[i]['timestamp_ns'];sample=dict(sequence=i,sampleAtNanos=t,sourceEpoch='synthetic-offline-v1',
                    calibrationEpoch='synthetic-offline-body-v1',commonWorldEpoch=None,valid=True,quaternionXyzw=[0,0,0,1])
                f=dict(timestampNanos=t,sequence=i,mainMtp=m,mainReceivedAtNanos=t,
                    imu=dict(sample,sourceId=core_config['imuSourceId']),
                    hmd=dict(sample,sourceId=core_config['hmdSourceId'],positionMeters=[0,1.6,0]))
                r=control.send(session,'input',frame=f)
                check(r['raw6dofValid']==row['effective_valid'] and r['sixDofValid']==row['effective_valid'],'Core admitted CANDIDATE or rejected ADMITTED')
                if row['admission_state']=='CANDIDATE':
                    check(r['raw6dofPose']==dict(positionMeters=None,quaternionXyzw=None),'candidate raw pose entered Core')
                results.append({key:r[key] for key in ('state','nativeContinuityState','transitionReason','sixDofValid',
                    'raw6dofValid','dwellElapsedNanos','blendProgress','finalOutputPose')})
            save(out/f'core-results-{repeat}.json',results);runs.append(results)
        finally:
            if started:control.send(session,'shutdown')
    check(runs[0]==runs[1],'Core replay nondeterministic')
    reacquire=next(i for i,r in enumerate(rows) if frames[i]['timestamp_ns']>1_000_000_000 and r['effective_valid'])
    check(runs[0][reacquire]['state']=='RECOVERY_DWELL','ADMITTED did not enter existing RECOVERY_DWELL')
    check(runs[0][-1]['state']=='FULL_6DOF','Core did not restore FULL')
    summary=dict(result='PASS',software_only=True,fixture_mapping_only=True,live_bridge_tested=False,
        protocol_heads=heads,protocol_binary_sha256={p.name:sha(p) for p in (project,normalize,encode)},
        observation_projections=len(rows),observation_normalizations=len(rows),mtp_codec_encodes=len(rows),mtp_normalizations=len(rows),
        core_source_commit='c73805da1a64d0b161775decd60e5632ea295a0f',core_jar_sha256=sha(jar),core_replay_frames_per_run=len(rows),
        core_replay_runs=2,core_deterministic=True,candidate_frames=sum(r['admission_state']=='CANDIDATE' for r in rows),
        reacquire_admission_frame=reacquire,core_state_at_admission=runs[0][reacquire]['state'],
        core_states=list(dict.fromkeys(r['state'] for r in runs[0])),core_state_commands_used=[],
        tuning_changed=False,hardware='NOT RUN',core_full_suite='NOT RUN')
    save(out/'summary.json',summary);print(json.dumps(summary))
if __name__=='__main__':main()
