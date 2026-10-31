"""Build the fixed PC receiver and GUI. Never start the HMD, service or driver."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def find_cmake():
    found = shutil.which('cmake.exe') or shutil.which('cmake')
    if found:
        return found
    for key in ('ProgramFiles', 'ProgramFiles(x86)'):
        if not os.environ.get(key):
            continue
        folder = Path(os.environ[key])
        candidates = [folder / 'CMake/bin/cmake.exe']
        candidates += sorted(folder.glob('Microsoft Visual Studio/*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'), reverse=True)
        for candidate in candidates:
            if candidate.is_file():
                return str(candidate)
    raise FileNotFoundError('CMake was not found. Add CMake to PATH or install Visual Studio CMake support.')


def main():
    cmake = find_cmake()
    build = ROOT / 'Build/runtime'
    env = os.environ.copy()
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    env['MSBUILDDISABLENODEREUSE'] = '1'
    for key in list(env):
        if key.lower() == 'path' and key != 'PATH':
            env['PATH'] = env.pop(key)
    powershell = shutil.which('powershell.exe')
    if not powershell:
        raise FileNotFoundError('Windows PowerShell is required to compile the existing WPF GUI.')
    commands = [
        [cmake, '-S', str(ROOT), '-B', str(build), '-A', 'x64',
         '-DPICO_OT_BUILD_TESTS=OFF', '-DPICO_OT_BUILD_STEAMVR_DRIVER=OFF',
         '-DPython3_EXECUTABLE=' + sys.executable],
        [cmake, '--build', str(build), '--config', 'Release', '--parallel', '2', '--target', 'pico_ot_bridge_service'],
        [powershell, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
         str(ROOT / 'scripts/steamvr_control_gui.ps1'), '-BuildOnly',
         '-OutputDirectory', str(build / 'control/Release')],
    ]
    for command in commands:
        print(subprocess.list2cmdline(command), flush=True)
        result = subprocess.run(command, cwd=ROOT, env=env)
        if result.returncode:
            return result.returncode
    print('Built repo/Build/runtime. Current-user state and running products were not changed.')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except OSError as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
