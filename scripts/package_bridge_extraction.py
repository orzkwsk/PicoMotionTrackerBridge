"""Export the reviewed legacy base plus its include closure, without PICO runtime."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import zipfile
ROOT = Path(__file__).resolve().parents[1]
BASE = '22c7bd8cdbd9a52355b6ff182f0ddc462a998bd8'
AUDIT = '4c0321f02b56a8ce420ba40a988ca9c296f05f61'
REPO = 'https://github.com/orzkwsk/PicoMotionTrackerBridge'
def git(*args): return subprocess.check_output(['git',*args],cwd=ROOT)
def sha(data): return hashlib.sha256(data).hexdigest()

def main():
    assert not git('status','--porcelain').strip(), 'Commit source changes before final handoff packaging'
    rows = git('ls-tree','-r',BASE).decode().splitlines()
    blobs = {line.split('\t',1)[1]: line.split()[2] for line in rows}
    seeds = {n for n in blobs if n.startswith(('outputs/steamvr/','include/pico_ot_bridge/','src/bridge/','scripts/'))}
    seeds |= {n for n in blobs if n.startswith('tools/steamvr_')}
    seeds |= {n for n in blobs if n.startswith('docs/') and n.count('/')==1}
    seeds |= {n for n in blobs if n.startswith('Start_') or n.startswith(('LICENSE','NOTICE'))}
    seeds |= {'tools/pc_bridge_service.cpp','tools/bridge_receiver_cli.cpp','tools/monaka_smoke_publisher.cpp',
              'integrations/monaka/CMakeLists.txt','README.md','include/pico_ot_backend/types.hpp'}
    seeds |= {f'tests/{n}_tests.cpp' for n in ['clock_sync','protocol','publisher','receiver','receiver_c_api','udp_socket']}
    files = {}; edges = []; queue = sorted(seeds)
    while queue:
        name = queue.pop()
        if name in files: continue
        assert name in blobs, name
        data = git('cat-file','blob',blobs[name]); files[name] = data
        if not name.endswith(('.cpp','.hpp','.h')): continue
        for include in re.findall(r'^\s*#\s*include\s*"([^"]+)"',data.decode('utf-8'),re.M):
            candidates = [str(PurePosixPath(name).parent/include), 'include/'+include,
                          'outputs/steamvr/common/'+include, 'outputs/steamvr/driver/'+include]
            target = next((x for x in candidates if x in blobs),None)
            assert target, f'unresolved project include {name}: {include}'
            assert not target.startswith(('src/runtime/','src/internal/')), target
            edges.append({'from':name,'include':include,'to':target}); queue.append(target)
    original = git('cat-file','blob',blobs['CMakeLists.txt'])
    files['reference/CMakeLists.txt'] = original
    # Remove only backend-library and decoder-test targets from the standalone
    # migration harness. Preserve the original root CMake verbatim as provenance.
    body = original.decode().split('# Platform-neutral HMD<->PC bridge core.',1)[1]
    body = body.split('if(PICO_OT_BUILD_TESTS AND NOT ANDROID)',1)[0]
    standalone = '''cmake_minimum_required(VERSION 3.20)
project(PicoBridgeExtraction LANGUAGES C CXX)
option(PICO_OT_BUILD_STEAMVR_DRIVER "Build legacy driver using OpenVR v2.15.6" OFF)
option(PICO_OT_BUILD_TESTS "Build legacy transport tests" ON)
# Platform-neutral HMD<->PC bridge core.'''+body+'''
if(PICO_OT_BUILD_TESTS)
  enable_testing()
  foreach(name IN ITEMS clock_sync protocol publisher receiver udp_socket)
    add_executable(${name}_tests tests/${name}_tests.cpp)
    target_link_libraries(${name}_tests PRIVATE PicoOT::BridgePublisher)
    add_test(NAME ${name}_tests COMMAND ${name}_tests)
  endforeach()
endif()
'''
    generated = {'CMakeLists.txt':standalone.encode()}
    generated['migration/config-examples/output_route.txt'] = b'steamvr\n'
    generated['migration/config-examples/steamvr_alignment.json'] = b'{"version":1,"xMeters":0.0,"yMeters":0.0,"zMeters":0.0}\n'
    generated['migration/README.md'] = '''# Task 4 extraction handoff

This is a reviewed historical migration input, not a second active PICO backend.
Do not launch its legacy service together with Task 2's service: both would own
29765 and could publish duplicate trackers through old/new SteamVR drivers.

The root CMake is a generated, standalone build harness. The source repository's
complete original CMake is preserved at reference/CMakeLists.txt. No PICO private
runtime, Android app, ABI shim, vendor decoder or vendor library is included.
The transport/types closure is migration reference for replacing old consumers;
Task 4 must consume the Task 1 MTP/Observation kit instead of inheriting POTB.

Build without any sibling repository:
  cmake -S . -B build -DPICO_OT_BUILD_TESTS=ON
  cmake --build build --config Debug
  ctest --test-dir build -C Debug --output-on-failure
For Windows x64 driver/calibrator add -DPICO_OT_BUILD_STEAMVR_DRIVER=ON.
The original CMake fetches ValveSoftware/openvr tag v2.15.6. Supply the SDK from
that tag via FETCHCONTENT_SOURCE_DIR_OPENVR_SDK for an offline build. It is not
in this ZIP. Windows SDK/MSVC C++17/CMake >=3.20 are also external prerequisites.
GUI compilation uses .NET Framework 4.8 Developer Pack/WPF and the OS csc.exe.
The GUI script launches after compiling; build/registration do not constitute
hardware validation. Runtime SteamVR/vrpathreg and PICO HMD app installation
are external. start_pico_backend.ps1 is historical launcher reference only.

Task 4 replacement points:
- TrackerDevice::Update and DeviceProvider::receiver_: ReceivedTrackerState,
  PosePacket, POTB Receiver/ClockSync -> Task 1 MtpPose/MtpTrackerState adapter.
- PicoOutputAToOpenVrCandidate/world/mount transforms -> owned mapping and
  calibration profiles; do not apply both here and upstream.
- tools/pc_bridge_service.cpp routing/Utility fan-out -> common Bridge policy;
  replace POTB Publisher/ToTrackerState with the supplied common codec.
- service_contract.hpp, output_route_ipc.hpp, world_alignment_ipc.hpp, C# IPC
  declarations: version and migrate routing/alignment configuration together.
- integrations/monaka and receiver_c_api remain legacy until Task 5 confirms
  no active consumer needs them. Do not confuse this with the Backend C API.
- scripts/Start_* paths and process names require new Bridge artifact names;
  ensure_pc_runtime.ps1 must stop owning/starting the PICO HMD lifecycle.

The supplied config examples were generated from service_contract.hpp and
SaveAlignmentConfig in steamvr_control_wpf.cs. They are not a user's config.
Original locations: %LOCALAPPDATA%/PicoMotionTrackerBridge/output_route.txt and
steamvr_alignment.json. Read version 1 translation fields before importing;
never silently reuse transforms for an unrelated C1 space id/revision.

No root LICENSE/NOTICE was present at the reviewed base. Original file notices
are preserved verbatim; this export grants no new license. OpenVR is external
and its license must accompany redistributed SDK parts. The manifest explicitly
records the origin and SHA of every original file and labels generated files.

Historical docs may describe pre-SteamVR backend milestones. Current Task 2
retains the old driver/GUI only as Phase A compatibility. Phase B removal needs
Task 4 Direct regression and Task 5 MTP/C ABI retirement handoffs.
'''.encode()
    entries = []
    for name,data in sorted(files.items()):
        origin = 'CMakeLists.txt' if name=='reference/CMakeLists.txt' else name
        entries.append({'path':name,'source_repository':REPO,'source_path':origin,'source_base_sha':BASE,
                        'source_blob_sha':blobs[origin],'sha256':sha(data),'generated':False,
                        'license_notice_handling':'Original bytes/notices preserved; no root license grant at base.',
                        'destination_candidate':'MonakaBridge/'+origin,
                        'old_type_replacement_notes':'See migration/README.md; retain provenance, replace POTB with Task 1 codec.'})
    for name,data in sorted(generated.items()):
        entries.append({'path':name,'source_repository':REPO,'source_path':None,'source_base_sha':BASE,
                        'source_blob_sha':None,'sha256':sha(data),'generated':True,
                        'license_notice_handling':'Generated migration support; no original source license inferred.',
                        'destination_candidate':'MonakaBridge/'+name,
                        'old_type_replacement_notes':'Generated migration support, not a historical source blob.'})
    files.update(generated)
    manifest = {'repository':REPO,'audited_base_sha':AUDIT,'source_base_sha':BASE,
                'packager_source_commit':git('rev-parse','HEAD').decode().strip(),
                'license_status':'No root LICENSE/NOTICE at base; existing file notices preserved, no new grant.',
                'external_dependencies':{'openvr':{'repository':'https://github.com/ValveSoftware/openvr','tag':'v2.15.6','verified_revision':'0924064316de3effbcd1acf1e309182a2deb1c05','included':False},
                                         'windows':'x64 Windows SDK / MSVC C++17 / CMake >=3.20',
                                         'gui':'.NET Framework 4.8 Developer Pack (WPF), Windows csc.exe',
                                         'runtime':'SteamVR and separately installed PICO HMD app; not included'},
                'include_closure':edges,'files':entries}
    files['extraction-manifest.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    files['SHA256SUMS']=''.join(f'{sha(data)}  {name}\n' for name,data in sorted(files.items())).encode()
    dist=ROOT/'dist';dist.mkdir(exist_ok=True); archive=dist/'pico-bridge-extraction.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for name,data in sorted(files.items()):
            info=zipfile.ZipInfo(name,(2026,9,15,0,0,0)); info.compress_type=zipfile.ZIP_DEFLATED
            z.writestr(info,data)
    external={'file':archive.name,'sha256':sha(archive.read_bytes()),'source_base_sha':BASE,
              'packager_source_commit':manifest['packager_source_commit'],
              'manifest_sha256':sha(files['extraction-manifest.json'])}
    (dist/'pico-bridge-extraction.handoff.json').write_text(json.dumps(external,indent=2)+'\n')
    print(json.dumps(external,indent=2))
    print(f'{len(entries)} files, {len(edges)} resolved project include edges, no PICO runtime source')

if __name__=='__main__': main()
