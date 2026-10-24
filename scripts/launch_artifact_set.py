"""Start the latest successful compatible local artifact set. No build/deploy/register."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,subprocess
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def inside(root,name):
    p=(root/name).resolve()
    if not p.is_relative_to(root.resolve()):raise ValueError('Artifact path escapes package')
    return p
def verify(package,component):
    m=json.loads((package/'artifact-manifest.json').read_text(encoding='utf-8'))
    if m['schema']!='monaka.hil-artifact-set.v1':raise ValueError('Unknown artifact schema')
    for name,pin in m['files_sha256'].items():
        if sha(inside(package,name))!=pin:raise ValueError('Artifact changed: '+name)
    for name in ['build','full_regression','revision','lifecycle']:
        r=json.loads(inside(package,m['receipts'][name]).read_text(encoding='utf-8'))
        if r['status']!='PASS' or r['compatibility_id']!=m['compatibility_id']:raise ValueError('Incompatible or untested artifact set: '+name)
    spec=m['launch'][component]
    for key in ['gui','service']:
        if spec[key] not in m['files_sha256']:raise ValueError('Unpinned executable: '+key)
    args=[str(inside(package,spec['gui'])),str(inside(package,spec['root'])),'--service-exe',str(inside(package,spec['service']))]+spec.get('arguments',[])
    for option,relative in spec.get('path_arguments',{}).items():
        if option!='--installation':raise ValueError('Unsupported artifact path argument')
        path=inside(package,relative)
        if not path.is_file():raise ValueError('Installation profile missing')
        args.extend([option,str(path)])
    if '--control-window-only' in args:raise ValueError('Normal entry must own tray/service lifetime')
    return args,m['compatibility_id']
def select(catalog,component):
    values=json.loads(catalog.read_text(encoding='utf-8'))
    entries=sorted(values['artifact_sets'],key=lambda x:x['build_utc'],reverse=True)
    # A broken newest set is surfaced, never silently bypassed for an older build.
    for entry in entries:
        if entry['status']=='PASS' and component in entry['components']:
            package=Path(entry['package']).resolve();verify(package,component);return package
    raise ValueError('No successful compatible local artifact set. Run the explicit build/package workflow.')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('component',choices=['pico','monaka']);p.add_argument('--package',type=Path);p.add_argument('--catalog',type=Path);p.add_argument('--check',action='store_true');p.add_argument('--tray-only',action='store_true');a=p.parse_args()
    catalog=a.catalog or Path(os.environ.get('MONAKA_ARTIFACT_CATALOG',Path(__file__).resolve().parents[1]/'build/artifact-sets.json'))
    package=a.package.resolve() if a.package else select(catalog,a.component);args,identity=verify(package,a.component)
    if a.tray_only:args.append('--tray-only')
    if a.check:print(json.dumps(dict(status='PASS',compatibility_id=identity,argv=args,process_started=False)));return
    logs=package/'launch-logs';logs.mkdir(exist_ok=True);stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    with (logs/(stamp+'.stdout.log')).open('xb') as out,(logs/(stamp+'.stderr.log')).open('xb') as err:
        child=subprocess.Popen(args,cwd=package,stdin=subprocess.DEVNULL,stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
    print(json.dumps(dict(status='TRAY_REQUESTED',pid=child.pid,compatibility_id=identity,readiness='Native tray verifies path, SHA, creation time and ownership before reuse/stop')))
if __name__=='__main__':main()
