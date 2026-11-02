"""Compile current tray window test, invoking actual GUI Open without live IPC."""
from pathlib import Path
import argparse, os, subprocess
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--gui', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().with_name('tray_window_tests.cs')
csc = Path(os.environ['WINDIR']) / 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
exe = a.out.resolve() / 'tray_window_tests.exe'
def run(name, command):
    print(repr(command), flush=True)
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    (a.out / (name + '.log')).write_bytes(result.stdout)
    print(result.stdout.decode(errors='replace'))
    if result.returncode: raise SystemExit(result.returncode)
run('compile', [str(csc), '/nologo', '/target:exe', '/reference:System.Windows.Forms.dll', '/reference:System.Drawing.dll', '/out:' + str(exe), str(source)])
run('window', [str(exe), str(a.gui.resolve())])
