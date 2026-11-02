using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace PicoOTBridge {
    // Internal software-test profile; production startup never selects it implicitly.
    internal sealed class ReceiverIsolationProfile : IDisposable {
        internal string FilePath, Id, Root;
        internal int OwnerPid;
        internal long OwnerCreated;
        internal int Ingress, Steam, Monaka, Utility, Observation;
        FileStream trayLease;
        [DllImport("iphlpapi.dll")] static extern uint GetExtendedUdpTable(IntPtr table, ref int size, bool order, int family, int kind, uint reserved);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool CreateDirectory(string path, IntPtr security);
        internal string Name(string suffix) { return @"Local\PicoOTBridge_Isolated_" + Id + "_" + suffix; }
        internal string OwnerRecord { get { return Id+"\n"+OwnerPid+":"+OwnerCreated+"\n"+Ingress+","+Steam+","+Monaka+","+Utility+","+Observation+"\n"; } }
        internal string Arguments { get { return " --isolated-profile \""+FilePath+"\""; } }
        internal static bool IsRequested(string[] args) { return args.Any(a => a.StartsWith("--isolat",StringComparison.OrdinalIgnoreCase)||a=="--test-owner"); }
        internal static ReceiverIsolationProfile Parse(string[] args) {
            string path=null;
            for(int i=0;i<args.Length;++i) {
                if(!args[i].StartsWith("--isolat",StringComparison.OrdinalIgnoreCase))continue;
                if(args[i]!="--isolated-profile" || path!=null || i+1>=args.Length || args[i+1].StartsWith("--"))throw new ArgumentException("Complete --isolated-profile path required");
                path=args[++i];
            }
            if(path==null) {if(args.Contains("--test-owner"))throw new ArgumentException("Test owner requires complete isolation profile");return null;}
            if(!Path.IsPathRooted(path) || new FileInfo(path).Length>8192)throw new ArgumentException("Absolute bounded isolation profile required");
            string json=File.ReadAllText(path,Encoding.UTF8);
            var values=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(json);
            var expected=new HashSet<string>(new[]{"version","instance_id","state_root","owner_pid","owner_created","ingress_port","steamvr_port","monaka_port","utility_port","observation_port"});
            var keys=Regex.Matches(json,"\"([^\"\\\\]*)\"\\s*:").Cast<Match>().Select(m=>m.Groups[1].Value).ToArray();
            if(values==null || keys.Length!=10 || keys.Distinct().Count()!=10 || !expected.SetEquals(keys) || !expected.SetEquals(values.Keys) || !(values["version"] is int) || (int)values["version"]!=1)
                throw new ArgumentException("Complete version1 isolation profile required");
            var p=new ReceiverIsolationProfile();p.FilePath=Path.GetFullPath(path);
            p.Id=values["instance_id"] as string;
            if(p.Id==null || !Regex.IsMatch(p.Id,@"\A[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\z"))throw new ArgumentException("Canonical instance UUID required");
            p.Root=values["state_root"] as string;
            if(p.Root==null || !Path.IsPathRooted(p.Root) || !string.Equals(Path.GetFullPath(p.Root),p.Root,StringComparison.OrdinalIgnoreCase) || Path.GetFileName(p.Root)!=p.Id || !Directory.Exists(p.Root) || (File.GetAttributes(p.Root)&FileAttributes.ReparsePoint)!=0)
                throw new ArgumentException("Dedicated canonical isolation root required");
            for(var ancestor=new DirectoryInfo(p.Root);ancestor!=null;ancestor=ancestor.Parent)if((ancestor.Attributes&FileAttributes.ReparsePoint)!=0)throw new ArgumentException("Isolation root/ancestor reparse is not allowed");
            var created=values["owner_created"] as string;
            if(!(values["owner_pid"] is int) || (int)values["owner_pid"]<=0 || created==null || !Regex.IsMatch(created,@"\A[1-9][0-9]{0,18}\z") || !long.TryParse(created,out p.OwnerCreated))throw new ArgumentException("Exact harness owner required");
            p.OwnerPid=(int)values["owner_pid"];
            var ports=new HashSet<int>();
            Func<string,int> port=key=>{if(!(values[key] is int))throw new ArgumentException("Integer isolation port required");int v=(int)values[key];if(v<1024||v>65535||(v>=29765&&v<=29768)||v==29810||!ports.Add(v))throw new ArgumentException("Distinct nonproduction ports required");return v;};
            p.Ingress=port("ingress_port");p.Steam=port("steamvr_port");p.Monaka=port("monaka_port");p.Utility=port("utility_port");p.Observation=port("observation_port");
            p.ValidateOwner(); return p;
        }
        static Process ExactOwner(int pid,long created) {
            var owner=Process.GetProcessById(pid);
            try {if(owner.Handle==IntPtr.Zero||owner.HasExited||owner.StartTime.ToUniversalTime().ToFileTimeUtc()!=created)throw new IOException("Isolation owner changed");return owner;}
            catch {owner.Dispose();throw;}
        }
        internal void ValidateOwner() {
            using(var owner=ExactOwner(OwnerPid,OwnerCreated)){}
            string lease=Path.Combine(Root,"owner.claim");
            using(var f=new FileStream(lease,FileMode.Open,FileAccess.Read,FileShare.ReadWrite))using(var r=new StreamReader(f,Encoding.UTF8))if(r.ReadToEnd()!=OwnerRecord)throw new IOException("Harness lease/profile mismatch");
            bool retained=false;
            try {using(var probe=new FileStream(lease,FileMode.Open,FileAccess.Write,FileShare.ReadWrite)){} }
            catch(IOException e){if((Marshal.GetHRForException(e)&65535)==32)retained=true;else throw;}
            if(!retained)throw new IOException("Harness lease not retained");
            int size=0;GetExtendedUdpTable(IntPtr.Zero,ref size,false,2,1,0);
            IntPtr data=Marshal.AllocHGlobal(size);
            try {
                if(GetExtendedUdpTable(data,ref size,false,2,1,0)!=0)throw new IOException("Endpoint owner census unavailable");
                var found=new HashSet<int>();int count=Marshal.ReadInt32(data);
                for(int i=0;i<count;++i){int offset=4+i*12;uint addr=unchecked((uint)Marshal.ReadInt32(data,offset));int raw=Marshal.ReadInt32(data,offset+4);int port=((raw&255)<<8)|((raw>>8)&255);int pid=Marshal.ReadInt32(data,offset+8);if(addr==0x0100007f&&pid==OwnerPid)found.Add(port);}
                if(new[]{Steam,Monaka,Utility,Observation}.Any(port=>!found.Contains(port)))throw new IOException("All output listeners must belong to exact harness owner");
            }finally{Marshal.FreeHGlobal(data);}
        }
        internal void ClaimTray() {
            ValidateOwner();
            trayLease=new FileStream(Path.Combine(Root,"tray.claim"),FileMode.CreateNew,FileAccess.ReadWrite,FileShare.Read);
            using(var current=Process.GetCurrentProcess()) {var bytes=Encoding.ASCII.GetBytes(current.Id+":"+current.StartTime.ToUniversalTime().ToFileTimeUtc()+"\n");trayLease.Write(bytes,0,bytes.Length);trayLease.Flush(true);}
        }
        static string ReadClaim(string path) {using(var f=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite))using(var reader=new StreamReader(f))return reader.ReadToEnd();}
        internal Process ValidateTray(string identity) {
            ValidateOwner();string[] pieces=(identity??"").Split(':');long created;int pid;
            if(pieces.Length!=2||!int.TryParse(pieces[0],out pid)||!long.TryParse(pieces[1],out created)||ReadClaim(Path.Combine(Root,"tray.claim")).Trim()!=identity)throw new IOException("Isolated tray identity mismatch");
            return ExactOwner(pid,created);
        }
        internal string CurrentTrayIdentity { get {using(var current=Process.GetCurrentProcess())return current.Id+":"+current.StartTime.ToUniversalTime().ToFileTimeUtc();} }
        public void Dispose(){if(trayLease!=null){trayLease.Dispose();trayLease=null;}}
    }
}
