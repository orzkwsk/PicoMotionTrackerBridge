"""Write Task 2's required final fields using committed source and real artifacts."""
import hashlib
import json
from pathlib import Path
import subprocess

if __name__ == "__main__":
    raise SystemExit("Historical wire-v1 report is retired at current v2 HEAD. Use python scripts/release_v2.py; see docs/release-v2.md. Historical supplied artifacts remain immutable provenance.")

ROOT=Path(__file__).resolve().parents[1]
BASE='22c7bd8cdbd9a52355b6ff182f0ddc462a998bd8'
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT,text=True).strip()
def read(path):return json.loads((ROOT/path).read_bytes())
def sha(path):return hashlib.sha256((ROOT/path).read_bytes()).hexdigest()
def reason(path):
    if path.startswith(('third_party/','dependencies/')):return 'Pin the delivered Task 1 bytes, source/schema provenance and artifact hashes; no codec reimplementation.'
    if 'observation' in path:return 'PC Observation conversion, independent clock/session/streams, persistent identity, nonblocking publication or mock coverage.'
    if path.startswith(('include/pico_ot_bridge/','src/bridge/')):return 'Opt-in single-owner bind and retired POTB session guard; preserve legacy defaults and wire ABI.'
    if path=='tools/pc_bridge_service.cpp':return 'Add C1 publication to the existing ingress owner while preserving all old outputs.'
    if path.startswith('scripts/'):return 'Verify/import real artifacts, rebuild changed provider sources, or produce reproducible extraction/provenance reports.'
    if path.startswith('docs/'):return 'Record baseline/contract decisions, compatibility limits, toolchains and actual verification.'
    return 'Build, CI and exact-byte vendoring support for Phase A.'
def main():
    assert not git('status','--porcelain'), 'Report only after a clean source commit'
    head=git('rev-parse','HEAD'); kit=read('dependencies/monaka-protocol.handoff.json')
    extraction=read('dist/pico-bridge-extraction.handoff.json'); check=read('dist/extraction-verification.json')
    assert extraction['packager_source_commit']==head and check['zip_sha256']==extraction['sha256']
    assert sha('dependencies/artifacts/monaka-protocol-kit-v1.0.zip')==kit['sha256']
    report={
      'repository':'orzkwsk/PicoMotionTrackerBridge',
      'audited_base_branch':'refactor/pc-bridge-service-fanout','audited_base_sha':'4c0321f02b56a8ce420ba40a988ca9c296f05f61',
      'actual_base_branch':'origin/refactor/pc-bridge-service-fanout','actual_base_sha':BASE,
      'base_change_reason':'Reviewed fast-forward of five documentation files only. Started work branch from required remote base, not initial main. Desktop environment; Cloud work exception not applicable.',
      'work_branch':git('branch','--show-current'),'HEAD_SHA':head,
      'protocol_version':'C1 candidate wire 1.0 / master reconciliation pending',
      'schema_commit':kit['schema_commit'],'protocol_kit_sha256':kit['sha256'],'contract_c1_sha256':kit['contract_c1_sha256'],
      'changed_files':[{'file':n,'reason':reason(n)} for n in git('diff','--name-only',BASE,'HEAD').splitlines()],
      'build_result':[
        {'command':'cmake -S . -B build -DPICO_OT_BUILD_TESTS=ON; cmake --build build --config Debug','environment':'Windows 11 x64 / MSVC 19.51.36257.0 / CMake 4.3.1-msvc1','result':'PASS'},
        {'command':'scripts/build_steamvr_driver.ps1 -Config Debug','environment':'Windows x64 / OpenVR v2.15.6 (0924064316de3effbcd1acf1e309182a2deb1c05)','result':'PASS (driver/calibrator and host targets)'},
        {'command':'BackendTestApp/build_test_app.ps1','environment':'Unity 2022.3.22f1 / NDK 23.1.7779620 / Clang 12.0.8 / Gradle 7.2 / AGP 7.1.2','result':'PASS (arm64 native libraries and APK)'},
        {'command':'cmake -S integrations/monaka -B build/capi; cmake --build build/capi --config Debug','environment':'Windows x64 / MSVC','result':'PASS'},
        {'command':'python scripts/verify_bridge_extraction.py --build --cmake <installed cmake>','environment':'Fresh temporary workspace with extracted bundle only','result':check['standalone_build']},
        {'command':'Linux host build','environment':'CI definition supplied; no local Linux/WSL','result':'NOT RUN'}],
      'unit_test_result':{'result':'PASS','host':'ctest --test-dir build -C Debug --output-on-failure: 8/8',
                          'driver_configuration':'ctest --test-dir build-steamvr -C Debug --output-on-failure: 8/8',
                          'legacy_c_api':'ctest --test-dir build/capi -C Debug --output-on-failure: 1/1'},
      'mock_or_cross_language_result':{'result':'PASS','description':'POTB fixtures -> Receiver -> PicoObservationAdapter/UDP publisher -> actual Task 1 C++ decoder; independent validity, metadata-only continuation, resync age, sequence wrap, retired sessions, eight devices, bounded cache, duplicate bind rejection and C1 consumer stopped with legacy progress.',
                                      'jvm_cross_language':'NOT RERUN in Task 2; original Task 1 74 fixtures/44 directions are provenance only.', 'extraction':check},
      'hardware_validation':'NOT RUN: five physical trackers, occlusion/recovery, power-off, HMD/PC restart, Utility coexistence and new-Bridge Direct/MTP comparison. No devices were deployed to or drivers registered.',
      'compatibility_status':'Phase A retains 29765 ingress ownership, 29766/29767/29768 fan-out, GUI/driver/calibration and Monaka receiver C API. C1 29810 uses a separate loopback socket. Private runtime/ABI/Build-ID guards and Android application sources are unchanged. No new protocol on old ports.',
      'phase_or_DoD_status':'Phase A complete / legacy removal pending' if check['standalone_build']=='PASS' else 'Phase A implementation prepared / extraction build verification pending',
      'unresolved_issues':['Reference master not provided; C1 remains candidate.','Hardware and Linux host execution NOT RUN.','Existing discovery/allocation, startup presence, charging/battery, query-vs-sensor and firmware limitations remain.','Phase B requires Task 4/5 replacement and config migration handoffs.'],
      'followup_required_in_other_repos':'Task 4 imports this extraction bundle and the same Task 1 kit, then supplies Bridge HEAD/hash and Direct regression. Task 5 supplies consumer HEAD/hash and C ABI retirement evidence. Do not start Phase B or remove old paths before those gates.',
      'responsibility_summary':'Added PICO PC observation ownership only. Generic output/calibration/GUI/driver remains compatibility in Phase A; migration copies with closure/provenance are handed to Task 4, not yet removed from active tree.',
      'handoff_artifacts':[
        {'file':'dist/pico-bridge-extraction.zip','sha256':extraction['sha256'],'original_commit':BASE,'packager_commit':head},
        {'file':'dist/pico-bridge-extraction.handoff.json','sha256':sha('dist/pico-bridge-extraction.handoff.json'),'original_commit':BASE,'packager_commit':head},
        {'file':'dependencies/artifacts/monaka-protocol-kit-v1.0.zip','sha256':kit['sha256'],'original_commit':kit['source_commit']},
        {'file':'dependencies/monaka-protocol.lock.json','sha256':sha('dependencies/monaka-protocol.lock.json'),'original_commit':kit['source_commit']},
        {'file':'BackendTestApp/app/build/outputs/apk/debug/app-debug.apk','sha256':sha('BackendTestApp/app/build/outputs/apk/debug/app-debug.apk'),'original_commit':head}],
      'working_tree_clean':True,'history_rewrite':False,'push_performed':False,
      'execution_notes':'Sandbox compiler/environment attempts failed or stalled; approved retries passed. A transient automatic-review usage-limit rejection was resolved by a later reviewed retry. Existing CMake/NDK/Gradle deprecation and read-only SDK metadata warnings remain.'
    }
    (ROOT/'dist/task2-final-report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print('Wrote dist/task2-final-report.json; '+report['phase_or_DoD_status'])
if __name__=='__main__':main()
