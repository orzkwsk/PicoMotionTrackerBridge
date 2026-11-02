using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using PcServiceLifecycle;
class IsolatedControlTests {
    static void Check(bool v,string s){if(!v)throw new Exception(s);Console.WriteLine("PASS "+s);}
    static void Until(Func<bool> check){var end=Stopwatch.StartNew();while(!check()){if(end.ElapsedMilliseconds>5000)throw new Exception("fixture timeout");Thread.Sleep(5);}}
    static string Scope(){return @"Local\PicoOTBridge_Test_"+Guid.NewGuid().ToString("N");}
    static int Main(string[] args){try{
        string fixture=Path.GetFullPath(args[0]),root=Path.GetFullPath(args[1]);Directory.CreateDirectory(root);
        string scope=Scope();
        using(var control=new Control(scope,fixture,Path.Combine(root,"stop-first"),true)) {
            control.StopAndWait(500);bool refused=false;
            try{control.EnsureRunning(" --isolated-profile \""+scope+"\"");}catch(IOException){refused=true;}
            Check(refused&&Control.Read(scope)==null&&!Directory.Exists(Path.Combine(root,"stop-first")),"Exit before queued start denies later spawn and file side effects");
        }
        scope=Scope();
        using(var control=new Control(scope,fixture,Path.Combine(root,"racing"),true)) {
            var start=Task.Run(()=>control.EnsureRunning(" --isolated-profile \""+scope+"\" --ready-delay 500"));
            Until(()=>Control.Read(scope)!=null);
            var initial=Control.Read(scope);using(var child=Process.GetProcessById((int)initial.Pid)) {
                var stop=Task.Run(()=>control.StopAndWait(2000));Thread.Sleep(75);
                Check(!stop.IsCompleted&&!child.HasExited,"Exit during startup waits for retained startup operation");
                Task.WaitAll(start,stop);Check(child.WaitForExit(2000)&&child.HasExited,"serialized startup then owned stop confirms exact child exit");
                Check(Control.Read(scope)==null,"serialized Exit leaves no lifecycle mapping");
            }
        }
        scope=Scope();
        using(var control=new Control(scope,fixture,Path.Combine(root,"lost-map"),true)) {
            control.EnsureRunning(" --isolated-profile \""+scope+"\" --drop-map 200");var initial=Control.Read(scope);
            using(var child=Process.GetProcessById((int)initial.Pid)) {
                Until(()=>Control.Read(scope)==null);bool refused=false;
                try{control.StopAndWait(100);}catch(IOException){refused=true;}
                Check(refused&&!child.HasExited,"missing lifecycle with retained live child refuses clean exit");
                Check(child.WaitForExit(3000),"unresponsive fixture exits by its own bounded deadline");control.StopAndWait(500);
                Check(child.HasExited,"missing-map retry succeeds only after exact child exit");
            }
        }
        return 0;
    }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
