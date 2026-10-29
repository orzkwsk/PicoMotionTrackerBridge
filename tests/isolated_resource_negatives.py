"""TEST ONLY: product service failure paths, all own resources/process handles."""
from pathlib import Path
import argparse, ctypes as c, ctypes.wintypes as w, json, os, socket, subprocess, sys, time
from isolated_process_e2e import Fixture,K,creation,require,checked_run,state,lease
K.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD];K.WaitForSingleObject.restype=w.DWORD
K.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];K.OpenProcess.restype=w.HANDLE
K.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)];K.QueryFullProcessImageNameW.restype=w.BOOL
K.TerminateProcess.argtypes=[w.HANDLE,w.UINT];K.TerminateProcess.restype=w.BOOL

def ingress_free(f):
    probe=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);probe.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
    try:probe.bind(('127.0.0.1',f.profile['ingress_port']))
    finally:probe.close()
def failed_service(f,name,held_lifecycle=False):
    checked_run(f.argv,f.root/(name+'.json'),False)
    if not held_lifecycle:require(state(f.scope) is None,'partial lifecycle leak '+name)
    require(not any(f.collect(.03)),'failure emitted common output '+name)

def worker(service,root):
    root.mkdir(parents=True,exist_ok=False);f=Fixture(root,service,'OWNER-DEATH');f.start()
    (root/'ready.json').write_text(json.dumps({'pid':f.proc.pid,'created':creation(w.HANDLE(int(f.proc._handle))),'profile':f.profile,'scope':f.scope}),encoding='utf-8')
    while True:time.sleep(.1)

def main():
    p=argparse.ArgumentParser();p.add_argument('--service',type=Path,required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--owner-worker',action='store_true');args=p.parse_args();root=args.work.resolve();service=args.service.resolve()
    if args.owner_worker:return worker(service,root)
    root.mkdir(parents=True,exist_ok=False);results=[]
    def case(name,operation):
        f=Fixture(root,service,name);holders=[]
        try:
            operation(f,holders);results.append({'case':name,'result':'PASS','root':str(f.root)})
        except Exception as error:results.append({'case':name,'result':'FAIL','error':repr(error),'root':str(f.root)});raise
        finally:
            for h in holders:
                if isinstance(h,socket.socket):h.close()
                else:K.CloseHandle(h)
            f.close();ingress_free(f)
    def occupied(f,held):
        s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1);s.bind(('127.0.0.1',f.profile['ingress_port']));held.append(s);failed_service(f,'occupied-ingress')
    def named(f,held,suffix,kind):
        name='Local\\PicoOTBridge_Isolated_'+f.id+'_'+suffix
        if kind=='mutex':h=K.CreateMutexW(None,False,name)
        elif kind=='event':h=K.CreateEventW(None,True,False,name)
        else:h=K.CreateFileMappingW(w.HANDLE(-1),None,4,0,48,name)
        require(h,'own collision resource');held.append(h)
        view=None
        if kind=='map':
            view=K.MapViewOfFile(h,2,0,0,48);require(view,'own collision view');c.memmove(view,b'R5_OWN_UNCHANGED',16)
        try:
            failed_service(f,'existing-'+suffix,suffix=='Lifecycle')
            if view:require(c.string_at(view,16)==b'R5_OWN_UNCHANGED','existing map overwritten')
            if kind=='event':require(K.WaitForSingleObject(h,0)==258,'existing route event signaled')
        finally:
            if view:K.UnmapViewOfFile(view)
    def no_output(f,held):
        f.sockets[0].close();failed_service(f,'missing-listener');require(not(f.root/'service.claim').exists(),'resource side effect before owner census')
    def unleased(f,held):
        K.CloseHandle(f.leases.pop(0));failed_service(f,'released-lease');require(not(f.root/'service.claim').exists(),'resource side effect before lease rejection')
    def bad_config(f,held):
        (f.root/'observation-provider.conf').write_text('MALFORMED\n');failed_service(f,'invalid-observation-config')
        require(all(n==0 for n in f.counts),'legacy output continued after Observation init failure')
    def bad_diagnostic(f,held):
        (f.root/'diagnostic.jsonl').mkdir();failed_service(f,'diagnostic-write-failure')
    def stale(f,held):
        f.start();f.stop();failed_service(f,'consumed-root-restart')
    def concurrent(f,held):
        logs=[open(f.root/('race-'+str(i)+'.log'),'w') for i in range(2)];children=[]
        try:
            for log in logs:children.append(subprocess.Popen([str(a) for a in f.argv],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW))
            end=time.monotonic()+6
            while time.monotonic()<end:
                statuses=[child.poll() for child in children];alive=[child for child in children if child.poll() is None];value=state(f.scope)
                if len(alive)==1 and any(code is not None and code!=0 for code in statuses) and value and value[7]==1:
                    f.proc=alive[0];require(value[3]==f.proc.pid and value[5]==creation(w.HANDLE(int(f.proc._handle))),'race winner exact child');f.stop();return
                time.sleep(.02)
            raise RuntimeError('concurrent start failed ownership contract')
        finally:
            for child in children:
                if child.poll() is None:child.terminate();child.wait(timeout=5)
            for log in logs:log.close()
    try:
        case('occupied-ingress',occupied)
        case('existing-instance',lambda f,h:named(f,h,'Instance','mutex'))
        case('existing-route',lambda f,h:named(f,h,'RouteUpdated','event'))
        case('existing-status',lambda f,h:named(f,h,'Status','map'))
        case('existing-lifecycle',lambda f,h:named(f,h,'Lifecycle','map'))
        case('missing-listener',no_output);case('released-lease',unleased)
        case('bad-observation-config',bad_config);case('bad-diagnostic',bad_diagnostic)
        case('stale-root',stale);case('concurrent-start',concurrent)
        sentinel=Fixture(root,service,'SURVIVOR');sentinel.start();owner=None;child_handle=None
        try:
            workerroot=root/'owner-death';log=open(root/'owner-worker.log','w')
            owner=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'--service',str(service),'--work',str(workerroot),'--owner-worker'],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
            end=time.monotonic()+8
            while not(workerroot/'ready.json').exists():
                require(owner.poll() is None and time.monotonic()<end,'owned worker readiness');time.sleep(.02)
            ready=json.loads((workerroot/'ready.json').read_text());child_handle=K.OpenProcess(0x100000|0x1000|1,False,ready['pid']);require(child_handle,'retain exact child observer')
            require(creation(child_handle)==ready['created'],'child creation identity')
            path=c.create_unicode_buffer(32768);size=w.DWORD(len(path));require(K.QueryFullProcessImageNameW(child_handle,0,path,c.byref(size)) and Path(path.value).resolve()==service,'child executable identity')
            owner.terminate();owner.wait(timeout=5)
            require(K.WaitForSingleObject(child_handle,6000)==0,'owner-death child exit confirmation')
            require(state(ready['scope']) is None,'owner-death lifecycle residue')
            s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1);s.bind(('127.0.0.1',ready['profile']['ingress_port']));s.close()
            require(sentinel.proc.poll() is None and state(sentinel.scope)[3]==sentinel.proc.pid,'unrelated owned instance affected')
            sentinel.stop();results.append({'case':'owner-death-sibling-preserved','result':'PASS','service_identity':ready})
        finally:
            if owner and owner.poll() is None:owner.terminate();owner.wait(timeout=5)
            if child_handle:
                if K.WaitForSingleObject(child_handle,0)!=0:K.TerminateProcess(child_handle,96);K.WaitForSingleObject(child_handle,5000)
                K.CloseHandle(child_handle)
            sentinel.close()
            if 'log' in locals():log.close()
    finally:
        (root/'RESULT.json').write_text(json.dumps({'phase':'OFFLINE_ONLY','software':'FAIL' if sys.exc_info()[0] else 'PASS_SCOPED_NEGATIVES','physical_hil':'NOT_RUN','cutover':'NOT_AUTHORIZED','cases':results},indent=2),encoding='utf-8')
    print(json.dumps(results,indent=2))
if __name__=='__main__':main()
