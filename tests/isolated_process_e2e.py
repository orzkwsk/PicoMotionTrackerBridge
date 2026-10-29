"""TEST ONLY. Own loopback listeners/files/exact Popen handles; never deploy.

The parent harness retains all four exclusive output sockets until its exact
service handle signals. Ingress allocation is a hint released before startup;
the service must obtain exclusive bind itself or reject, never claim success
from the hint. No production object name/endpoint is opened or connected.
"""
from pathlib import Path
import argparse, ctypes as c, ctypes.wintypes as w, hashlib, json, os, select, socket, struct, subprocess, time, uuid

K=c.WinDLL('kernel32',use_last_error=True)
K.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,c.c_void_p,w.DWORD,w.DWORD,w.HANDLE];K.CreateFileW.restype=w.HANDLE
K.WriteFile.argtypes=[w.HANDLE,c.c_void_p,w.DWORD,c.POINTER(w.DWORD),c.c_void_p];K.WriteFile.restype=w.BOOL
K.FlushFileBuffers.argtypes=[w.HANDLE];K.CloseHandle.argtypes=[w.HANDLE]
K.GetCurrentProcess.restype=w.HANDLE
K.GetProcessTimes.argtypes=[w.HANDLE,c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME)]
K.OpenFileMappingW.argtypes=[w.DWORD,w.BOOL,w.LPCWSTR];K.OpenFileMappingW.restype=w.HANDLE
K.MapViewOfFile.argtypes=[w.HANDLE,w.DWORD,w.DWORD,w.DWORD,c.c_size_t];K.MapViewOfFile.restype=c.c_void_p
K.UnmapViewOfFile.argtypes=[c.c_void_p]
K.OpenEventW.argtypes=[w.DWORD,w.BOOL,w.LPCWSTR];K.OpenEventW.restype=w.HANDLE
K.SetEvent.argtypes=[w.HANDLE];K.CreateEventW.argtypes=[c.c_void_p,w.BOOL,w.BOOL,w.LPCWSTR];K.CreateEventW.restype=w.HANDLE
K.CreateFileMappingW.argtypes=[w.HANDLE,c.c_void_p,w.DWORD,w.DWORD,w.DWORD,w.LPCWSTR];K.CreateFileMappingW.restype=w.HANDLE
K.CreateMutexW.argtypes=[c.c_void_p,w.BOOL,w.LPCWSTR];K.CreateMutexW.restype=w.HANDLE

def require(ok,text):
    if not ok: raise RuntimeError(text)
def creation(handle):
    values=[w.FILETIME() for _ in range(4)]
    require(K.GetProcessTimes(handle,*[c.byref(v) for v in values]),'exact process creation time')
    return (values[0].dwHighDateTime<<32)|values[0].dwLowDateTime
def lease(path,record):
    h=K.CreateFileW(str(path),0xC0000000,1,None,1,0x80,None)
    require(h not in (None,c.c_void_p(-1).value),'fresh retained lease '+str(path))
    data=record.encode('ascii');done=w.DWORD()
    require(K.WriteFile(h,data,len(data),c.byref(done),None) and done.value==len(data) and K.FlushFileBuffers(h),'lease persistence')
    return h
def listener():
    s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1);s.bind(('127.0.0.1',0));s.setblocking(False)
    require(s.getsockname()[1] not in (29765,29766,29767,29768,29810),'nonproduction ephemeral port');return s
def state(name):
    h=K.OpenFileMappingW(4,False,name)
    if not h:return None
    try:
        v=K.MapViewOfFile(h,4,0,0,48)
        if not v:return None
        try:return struct.unpack('<IIQIIQQII',c.string_at(v,48))
        finally:K.UnmapViewOfFile(v)
    finally:K.CloseHandle(h)
def checked_run(argv,log,expect_success=True):
    result=subprocess.run([str(a) for a in argv],capture_output=True,text=True,timeout=15,creationflags=subprocess.CREATE_NO_WINDOW)
    log.write_text(json.dumps({'argv':[str(a) for a in argv],'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr},indent=2),encoding='utf-8')
    require((result.returncode==0)==expect_success,'command expected '+str(expect_success)+': '+str(log))
    return result

class Fixture:
    def __init__(self,parent,service,label):
        self.service=service;self.label=label;self.id=str(uuid.uuid4());self.root=parent/self.id;self.root.mkdir();self.sockets=[listener() for _ in range(4)];self.input=listener()
        reservation=listener();ingress=reservation.getsockname()[1];reservation.close()
        self.owner_created=creation(K.GetCurrentProcess());self.identity=f'{os.getpid()}:{self.owner_created}'
        self.profile={'version':1,'instance_id':self.id,'state_root':str(self.root),'owner_pid':os.getpid(),'owner_created':str(self.owner_created),'ingress_port':ingress}
        for name,s in zip(('steamvr_port','monaka_port','utility_port','observation_port'),self.sockets):self.profile[name]=s.getsockname()[1]
        self.file=parent/(self.id+'.json');self.file.write_text(json.dumps(self.profile),encoding='utf-8')
        ports=','.join(str(self.profile[k]) for k in ('ingress_port','steamvr_port','monaka_port','utility_port','observation_port'))
        self.leases=[lease(self.root/'owner.claim',f'{self.id}\n{self.identity}\n{ports}\n'),lease(self.root/'tray.claim',self.identity+'\n')]
        (self.root/'output_route.txt').write_text('both\n',encoding='ascii');self.proc=None;self.logs=[];self.messages=[];self.counts=[0]*4
    @property
    def scope(self):return 'Local\\PicoOTBridge_Isolated_'+self.id+'_Lifecycle'
    @property
    def argv(self):return [self.service,'--isolated-profile',self.file,'--tray-owner',self.identity]
    def start(self):
        for suffix in ('stdout','stderr'):self.logs.append(open(self.root/(suffix+'.log'),'w',encoding='utf-8'))
        self.proc=subprocess.Popen([str(a) for a in self.argv],stdout=self.logs[-2],stderr=self.logs[-1],creationflags=subprocess.CREATE_NO_WINDOW)
        end=time.monotonic()+8
        while time.monotonic()<end:
            require(self.proc.poll() is None,'service exited before ready '+str(self.root))
            value=state(self.scope)
            if value and value[0]==0x314c4350 and value[2] and not value[2]&1 and value[3]==self.proc.pid and value[5]==creation(w.HANDLE(int(self.proc._handle))) and value[6]==self.owner_created and value[7]==1:return
            time.sleep(.02)
        raise RuntimeError('ready timeout '+str(self.root))
    def declare(self,session):
        lines=(self.root/'observation-provider.conf').read_text().splitlines();self.source=lines[1];self.space=lines[2]
        (self.root/'observation-provider.conf.space').write_text('\n'.join(('pico-source-space-declaration-v1',self.source,self.space,'r5-fake-generation',str(session),'known'))+'\n',encoding='ascii')
    def send(self,sender,sequence,mode='full',session=101):
        require(self.proc.poll() is None,'exact receiver exited')
        packet=self.root/f'packet-{session}-{sequence}-{mode}.bin'
        checked_run([sender,self.file,'R5-FAKE-'+self.label,session,sequence,mode,self.proc.pid,packet],self.root/f'send-{session}-{sequence}-{mode}.json')
        self.input.sendto(packet.read_bytes(),('127.0.0.1',self.profile['ingress_port']))
    def collect(self,seconds=.25):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            ready,_,_=select.select([s for s in self.sockets if s.fileno()>=0],[],[],min(.03,max(0,end-time.monotonic())))
            for s in ready:
                data,_=s.recvfrom(65535);i=self.sockets.index(s);self.counts[i]+=1
                if i==3:self.messages.append(json.loads(data))
        return self.messages
    def stop(self):
        if self.proc and self.proc.poll() is None:
            checked_run([self.service,'--isolated-profile',self.file,'--stop','--tray-owner',self.identity],self.root/'owned-stop.json')
            require(self.proc.wait(timeout=7)==0,'graceful exact service exit')
    def close(self):
        if self.proc and self.proc.poll() is None:
            # Exact Popen handle belongs only to this test; never executable-name discovery.
            self.proc.terminate();self.proc.wait(timeout=5)
        if self.proc:
            require(state(self.scope) is None,'lifecycle handle residue')
            probe=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);probe.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1);probe.bind(('127.0.0.1',self.profile['ingress_port']));probe.close()
        for f in self.logs:f.close()
        for h in self.leases:K.CloseHandle(h)
        for s in self.sockets:s.close()
        self.input.close()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--service',type=Path,required=True);parser.add_argument('--sender',type=Path,required=True);parser.add_argument('--consumer',type=Path,required=True);parser.add_argument('--work',type=Path,required=True);args=parser.parse_args()
    root=args.work.resolve();root.mkdir(parents=True,exist_ok=False);fixtures=[];out={'phase':'OFFLINE_ONLY','physical_hil':'NOT_RUN','cutover':'NOT_AUTHORIZED','origin':'FAKE_POTB','cases':[]}
    try:
        for label in ('A','B'):
            f=Fixture(root,args.service.resolve(),label);fixtures.append(f);f.start();f.declare(101);f.send(args.sender.resolve(),1);f.collect()
        for f in fixtures:
            f.collect();require(any(m.get('device_id')=='R5-FAKE-'+f.label and m.get('type')=='pose' for m in f.messages),'actual adapter observation')
            require(all(m.get('device_id','R5-FAKE-'+f.label)=='R5-FAKE-'+f.label for m in f.messages),'cross-talk')
            require(all(n>0 for n in f.counts),'all three legacy outputs and common observation reached own listeners')
        out['cases'].append({'id':'P-02','result':'PASS','instances':[f.profile for f in fixtures],'counts':[f.counts for f in fixtures]})
        f=fixtures[0];f.send(args.sender.resolve(),2,'none');f.collect();require(any(m.get('type')=='pose' and m.get('modality')=='none' for m in f.messages),'NONE observation absent')
        before=len(f.messages);f.send(args.sender.resolve(),2,'full');f.send(args.sender.resolve(),3,'malformed');f.collect(.1)
        require(not any(m.get('type')=='pose' and m.get('modality')=='full' for m in f.messages[before:]),'duplicate/malformed revived FULL')
        f.send(args.sender.resolve(),4);f.collect();require(any(m.get('type')=='pose' and m.get('modality')=='full' for m in f.messages[before:]),'valid packet did not recover')
        out['cases'].append({'id':'P-06','result':'PASS','negative':'NONE, duplicate sequence, malformed packet; valid recovery'})
        checked_run(f.argv,f.root/'duplicate-instance.json',False)
        invalid=dict(f.profile);invalid.pop('utility_port');bad=root/'partial.json';bad.write_text(json.dumps(invalid));checked_run([args.service,'--isolated-profile',bad,'--stop'],root/'partial-stop.json',False)
        checked_run([args.service,'--isolated-profile','--stop'],root/'missing-profile-stop.json',False)
        out['cases'].append({'id':'P-04','result':'PASS','negative':'duplicate consumed root; partial/missing profile with stop rejects'})
        for f in fixtures:
            f.stop();wire=f.root/'observations.jsonl';wire.write_text(''.join(json.dumps(m,separators=(',',':'))+'\n' for m in f.messages),encoding='utf-8');checked_run([args.consumer,wire],f.root/'common-decode.json')
        out['cases'].append({'id':'P-05-graceful','result':'PASS','negative':'exact owned stop and port/lifecycle cleanup checked in finally; fresh-profile rerun only'})
        out['software']='PASS_SCOPED_SERVICE_E2E'
    except Exception as error:
        out['software']='FAIL';out['error']=repr(error);raise
    finally:
        cleanup=[]
        for f in fixtures:
            try:f.close();cleanup.append({'id':f.id,'result':'PASS'})
            except Exception as error:cleanup.append({'id':f.id,'result':'FAIL','error':repr(error)});out['software']='FAIL'
        out['cleanup']=cleanup
        out['binaries']=[{'path':str(p.resolve()),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in (args.service,args.sender,args.consumer)]
        (root/'RESULT.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
    require(out['software']!='FAIL','cleanup failed');print(json.dumps(out,indent=2))
if __name__=='__main__':main()
