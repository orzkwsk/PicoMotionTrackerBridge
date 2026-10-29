using System;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Threading;
using System.Security.Cryptography;

namespace PcServiceLifecycle {
    internal sealed class State {
        internal uint Pid, OwnerPid;
        internal long Created, OwnerCreated;
        internal bool Ready;
    }
    internal sealed class Control : IDisposable {
        readonly string name, executable, logs, executableSha;
        readonly bool isolated;
        readonly object lifecycleGate = new object();
        bool stopRequested;
        Process child;
        StreamWriter stdout, stderr;
        internal Control(string mappingName, string serviceExecutable, string logDirectory, bool isolated = false) {
            this.isolated=isolated;
            name=mappingName; executable=Path.GetFullPath(serviceExecutable); logs=logDirectory;
            executableSha=File.Exists(executable)?Hash(executable):null;
        }
        internal static string Hash(string path) {
            using(var stream=File.OpenRead(path))using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","").ToLowerInvariant();
        }
        internal static void VerifyExistingTray(string mappingName,string expectedService,string expectedController) {
            var state=Read(mappingName);
            if(state==null || !state.Ready)throw new IOException("Existing tray has no ready service identity");
            using(var control=new Control(mappingName,expectedService,Path.GetTempPath()))
            using(var service=control.Verified(state))
            using(var owner=Process.GetProcessById(checked((int)state.OwnerPid))) {
                if(owner.Handle==IntPtr.Zero || owner.StartTime.ToUniversalTime().ToFileTimeUtc()!=state.OwnerCreated ||
                   !string.Equals(Path.GetFullPath(owner.MainModule.FileName),Path.GetFullPath(expectedController),StringComparison.OrdinalIgnoreCase) ||
                   Hash(owner.MainModule.FileName)!=Hash(expectedController) || owner.HasExited || service.HasExited)
                    throw new IOException("Existing tray path/build differs; no reopen request sent");
            }
        }
        internal static State Read(string name) {
            try {
                using(var map=MemoryMappedFile.OpenExisting(name,MemoryMappedFileRights.Read))
                using(var view=map.CreateViewAccessor(0,48,MemoryMappedFileAccess.Read)) {
                    long first=view.ReadInt64(8); Thread.MemoryBarrier();
                    if(view.ReadUInt32(0)!=0x314c4350 || view.ReadUInt32(4)!=1 || first==0 || (first&1)!=0)
                        throw new IOException("Service lifetime identity is incomplete");
                    var state=new State {Pid=view.ReadUInt32(16),OwnerPid=view.ReadUInt32(20),
                        Created=view.ReadInt64(24),OwnerCreated=view.ReadInt64(32),Ready=view.ReadUInt32(40)==1};
                    Thread.MemoryBarrier();
                    if(first!=view.ReadInt64(8))throw new IOException("Service lifetime identity changed");
                    return state;
                }
            } catch(FileNotFoundException) { return null; }
        }
        Process Verified(State state) {
            var process=Process.GetProcessById(checked((int)state.Pid));
            try {
                // Retain the handle before signalling: .NET Framework cannot
                // retrieve ExitCode from an already-gone GetProcessById target.
                if(process.Handle==IntPtr.Zero)throw new IOException("Service process handle unavailable");
                if(process.StartTime.ToUniversalTime().ToFileTimeUtc()!=state.Created ||
                    !string.Equals(Path.GetFullPath(process.MainModule.FileName),executable,StringComparison.OrdinalIgnoreCase) ||
                    executableSha==null || Hash(process.MainModule.FileName)!=executableSha)
                    throw new IOException("Service identity/path differs; no stop request sent");
                return process;
            } catch {process.Dispose();throw;}
        }
        static void Log(StreamWriter writer,string value) {
            if(writer==null || value==null)return;
            try {lock(writer) { if(writer.BaseStream.Length<1024*1024) { writer.WriteLine(value);writer.Flush(); } }}
            catch(IOException){}catch(ObjectDisposedException){}
        }
        bool AnyUnmanagedProcess() {
            var processes=Process.GetProcessesByName(Path.GetFileNameWithoutExtension(executable));
            try { return processes.Length!=0; }
            finally {foreach(var process in processes)process.Dispose();}
        }
        void RequireOwned(State state) {
            using(var current=Process.GetCurrentProcess())
                if(state.OwnerPid!=(uint)current.Id || state.OwnerCreated!=current.StartTime.ToUniversalTime().ToFileTimeUtc())
                    throw new IOException("Receiver belongs to another controller or a headless session; it will not be stopped or replaced");
        }
        internal void EnsureRunning(string additionalArguments) {
            if(!isolated) {EnsureRunningCore(additionalArguments);return;}
            lock(lifecycleGate) {
                if(stopRequested)throw new IOException("Isolated receiver stop already requested; no later spawn allowed");
                EnsureRunningCore(additionalArguments);
            }
        }
        void EnsureRunningCore(string additionalArguments) {
            if(isolated&&!additionalArguments.StartsWith(" --isolated-profile ",StringComparison.Ordinal))throw new IOException("Isolated service profile missing");
            var existing=Read(name);
            if(existing!=null) {
                if(isolated&&child==null)throw new IOException("Isolated service cannot adopt an existing process");
                using(var process=Verified(existing)) {
                    RequireOwned(existing);
                    if(existing.Ready && !process.HasExited)return;
                }
                throw new IOException("Existing service is not ready");
            }
            if(!isolated&&AnyUnmanagedProcess())throw new IOException("An existing/legacy service has no compatible lifetime controller. No duplicate was started");
            if(!File.Exists(executable))throw new FileNotFoundException("Selected service executable is missing",executable);
            Directory.CreateDirectory(logs);
            string stamp=DateTime.UtcNow.ToString("yyyyMMddTHHmmssfffffffZ");
            stdout=new StreamWriter(Path.Combine(logs,stamp+".stdout.log"));
            stderr=new StreamWriter(Path.Combine(logs,stamp+".stderr.log"));
            using(var current=Process.GetCurrentProcess()) {
                string owner=current.Id+":"+current.StartTime.ToUniversalTime().ToFileTimeUtc();
                child=new Process {StartInfo=new ProcessStartInfo(executable,additionalArguments+" --tray-owner "+owner) {
                    UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true,
                    WorkingDirectory=Path.GetDirectoryName(executable)}};
                child.OutputDataReceived+=delegate(object sender,DataReceivedEventArgs e){Log(stdout,e.Data);};
                child.ErrorDataReceived+=delegate(object sender,DataReceivedEventArgs e){Log(stderr,e.Data);};
                if(!child.Start())throw new IOException("Service launch failed");
                child.BeginOutputReadLine();child.BeginErrorReadLine();
            }
            var deadline=Stopwatch.StartNew();
            while(deadline.ElapsedMilliseconds<5000) {
                if(child.HasExited) { child.WaitForExit();throw new IOException("Service exited with code "+child.ExitCode+". Logs: "+logs); }
                var state=Read(name);
                if(state!=null) {
                    using(var process=Verified(state)) { RequireOwned(state);if(state.Ready)return; }
                }
                Thread.Sleep(20);
            }
            throw new IOException("Service startup confirmation timed out. The tray remains available for stop/retry. Logs: "+logs);
        }
        internal void StopAndWait(int timeoutMs) {
            if(!isolated) {StopAndWaitCore(timeoutMs);return;}
            lock(lifecycleGate) {
                stopRequested=true;
                StopAndWaitCore(timeoutMs);
            }
        }
        void StopAndWaitCore(int timeoutMs) {
            var state=Read(name);
            if(state==null) {
                if(isolated) {
                    if(child!=null&&!child.HasExited)throw new IOException("Owned isolated child is alive without confirmed lifecycle; tray retained");
                    if(child!=null)child.WaitForExit();
                    return;
                }
                if(AnyUnmanagedProcess())throw new IOException("Service identity unavailable; tray retained, no force kill");
                return;
            }
            Process verified;
            try {verified=Verified(state);}
            catch(ArgumentException) {if(isolated) {if(child!=null&&!child.HasExited)throw new IOException("Owned child exit unconfirmed; tray retained");if(child!=null)child.WaitForExit();return;}if(!AnyUnmanagedProcess())return;throw;}
            using(var process=verified) {
                RequireOwned(state);
                if(isolated&&(child==null||child.Id!=process.Id||child.StartTime!=process.StartTime))throw new IOException("Isolated stop requires retained exact child handle");
                using(var stop=EventWaitHandle.OpenExisting(name+"_Stop_"+state.Pid+"_"+state.Created))stop.Set();
                if(!process.WaitForExit(timeoutMs))throw new IOException("Service did not stop in time. Tray retained; no force kill");
                if(process.ExitCode!=0)throw new IOException("Service exited with code "+process.ExitCode+". Tray retained; inspect logs: "+logs);
            }
        }
        public void Dispose() {
            // Disposing an exited tray cannot kill any process. Owner death is
            // detected by the native service and follows its normal cleanup path.
            if(child!=null) {
                if(child.HasExited)child.WaitForExit();
                child.Dispose();child=null;
            }
            if(stdout!=null) {lock(stdout)stdout.Dispose();stdout=null;}
            if(stderr!=null) {lock(stderr)stderr.Dispose();stderr=null;}
        }
    }
    internal sealed class ExitGate {
        internal bool TryExit(Action stopAndConfirm,Action exit,out string error) {
            try {stopAndConfirm();exit();error=null;return true;}
            catch(Exception e) {error=e.Message;return false;}
        }
    }
}
