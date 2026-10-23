using System;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Threading;
using PcServiceLifecycle;

internal static class LifetimeTests {
    static int passed;
    static void Require(bool value,string name){if(!value)throw new Exception(name);Console.WriteLine("PASS "+name);++passed;}
    static string Scope(){return @"Local\PicoOTBridge_Test_"+Guid.NewGuid().ToString("N");}
    static Control Controller(string scope,string fixture,string root){return new Control(scope,fixture,Path.Combine(root,"logs"));}
    static int Main(string[] args) {
        try {
            string fixture=args[0],root=args[1];
            if(args.Length>2 && args[2]=="--owner-child") {
                string ownScope=Scope();
                var controller=Controller(ownScope,fixture,root);controller.EnsureRunning('"'+ownScope+'"');
                Console.WriteLine(Control.Read(ownScope).Pid);Console.Out.Flush();
                Environment.Exit(0);return 0;
            }
            bool visible=true;string error;
            var gate=new ExitGate();
            Require(!gate.TryExit(delegate{throw new IOException("stop failed");},delegate{visible=false;},out error)&&visible,"stop failure retains tray");
            Require(!gate.TryExit(delegate{throw new TimeoutException();},delegate{visible=false;},out error)&&visible,"stop timeout retains tray");
            Require(gate.TryExit(delegate{},delegate{visible=false;},out error)&&!visible,"confirmed stop permits tray exit");
            var scope=Scope();
            using(var control=Controller(scope,fixture,root)) {
                control.EnsureRunning('"'+scope+'"');
                var state=Control.Read(scope);Require(state!=null&&state.Ready,"native readiness acknowledged");
                var lifetime=Process.GetProcessById((int)state.Pid);
                control.EnsureRunning('"'+scope+'"');Require(Control.Read(scope).Pid==state.Pid,"same owned build is idempotent");
                Control.VerifyExistingTray(scope,fixture,Process.GetCurrentProcess().MainModule.FileName);Require(true,"ready same tray/service build verified before reopen");
                bool staleRejected=false;try{Control.VerifyExistingTray(scope,fixture,fixture);}catch(IOException){staleRejected=true;}Require(staleRejected,"different tray path/build rejected before reopen");
                staleRejected=false;try{Control.VerifyExistingTray(scope,Path.Combine(root,"wrong.exe"),Process.GetCurrentProcess().MainModule.FileName);}catch(IOException){staleRejected=true;}Require(staleRejected,"different service build rejected before reopen");
                using(var legacy=Controller(Scope(),fixture,root)) {
                    bool startRejected=false,stopRejected=false;
                    try{legacy.EnsureRunning("unused");}catch(IOException){startRejected=true;}
                    try{legacy.StopAndWait(100);}catch(IOException){stopRejected=true;}
                    Require(startRejected&&stopRejected&&!lifetime.HasExited,"legacy instance without matching lifecycle is neither duplicated nor stopped");
                }
                using(var window=Process.Start(new ProcessStartInfo("cmd.exe","/d /c exit 0"){UseShellExecute=false,CreateNoWindow=true}))window.WaitForExit();
                Require(!lifetime.HasExited&&Control.Read(scope)!=null,"closing separate window leaves service and controller alive");
                visible=true;
                bool confirmed=gate.TryExit(delegate{control.StopAndWait(1000);},delegate{visible=false;},out error);
                Require(confirmed&&!visible&&lifetime.HasExited,"native stop confirms process exit before tray removal"+(error==null?"":"; "+error));
                lifetime.Dispose();
            }
            scope=Scope();
            using(var control=Controller(scope,fixture,root)) {
                control.EnsureRunning('"'+scope+"\" --ignore-stop");
                using(var lifetime=Process.GetProcessById((int)Control.Read(scope).Pid)) {
                    visible=true;
                    Require(!gate.TryExit(delegate{control.StopAndWait(50);},delegate{visible=false;},out error)&&visible&&!lifetime.HasExited,"unresponsive service retains tray without kill");
                    Require(lifetime.WaitForExit(3000),"timeout fixture exits by its own deadline");
                }
            }
            scope=Scope();
            using(var control=Controller(scope,fixture,root)) {
                control.EnsureRunning('"'+scope+'"');
                using(var other=new Control(scope,Path.Combine(root,"wrong.exe"),Path.Combine(root,"logs"))) {
                    visible=true;
                    Require(!gate.TryExit(delegate{other.StopAndWait(100);},delegate{visible=false;},out error)&&visible,"wrong executable identity cannot stop service or hide tray");
                }
                using(var map=MemoryMappedFile.OpenExisting(scope,MemoryMappedFileRights.ReadWrite))
                using(var view=map.CreateViewAccessor(0,48,MemoryMappedFileAccess.ReadWrite)) {
                    long created=view.ReadInt64(24);view.Write(24,created+1);
                    visible=true;
                    Require(!gate.TryExit(delegate{control.StopAndWait(100);},delegate{visible=false;},out error)&&visible,"PID reuse/creation mismatch fails closed");
                    view.Write(24,created);
                }
                control.StopAndWait(1000);
            }
            scope=Scope();
            using(var control=Controller(scope,fixture,root)) {
                control.EnsureRunning('"'+scope+"\" --stop-error");visible=true;
                Require(!gate.TryExit(delegate{control.StopAndWait(1000);},delegate{visible=false;},out error)&&visible&&error.Contains("17"),"abnormal native exit retains tray with exit code");
            }
            scope=Scope();
            using(var control=Controller(scope,fixture,root)) {
                bool rejected=false;
                try{control.EnsureRunning('"'+scope+"\" --exit-error");}catch(IOException e){rejected=e.Message.Contains("17")&&e.Message.Contains("Logs:");}
                Require(rejected,"startup exit code and persistent logs retained");
            }
            var self=Process.GetCurrentProcess().MainModule.FileName;
            using(var abandoned=Process.Start(new ProcessStartInfo(self,'"'+fixture+"\" \""+root+"\" --owner-child"){
                UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true})) {
                string text=abandoned.StandardOutput.ReadToEnd();string stderr=abandoned.StandardError.ReadToEnd();
                abandoned.WaitForExit();if(abandoned.ExitCode!=0)throw new Exception(stderr);
                int pid=int.Parse(text.Trim());
                bool exited;
                try{using(var service=Process.GetProcessById(pid))exited=service.WaitForExit(3000);}catch(ArgumentException){exited=true;}
                Require(exited,"tray/controller death triggers native graceful cleanup");
            }
            Console.WriteLine("TOTAL "+passed);return 0;
        }catch(Exception e){Console.Error.WriteLine(e);return 1;}
    }
    // Kept separate from the normal test body to simulate a controller crash
    // without disposing the service or ever touching production IPC/ports.
}
