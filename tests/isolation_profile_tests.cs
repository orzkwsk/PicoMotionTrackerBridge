using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Web.Script.Serialization;
using PicoOTBridge;
class IsolationProfileTests {
    static void Check(bool value,string text){if(!value)throw new Exception(text);}
    static void Reject(Action operation,string text){bool failed=false;try{operation();}catch{failed=true;}Check(failed,text);}
    static int Main(string[] args) {try{
        string id=Guid.NewGuid().ToString(),root=Path.Combine(Path.GetFullPath(args[0]),id);Directory.CreateDirectory(root);
        var sockets=new List<Socket>();
        try {
            for(int i=0;i<5;++i){var s=new Socket(AddressFamily.InterNetwork,SocketType.Dgram,ProtocolType.Udp);s.ExclusiveAddressUse=true;s.Bind(new IPEndPoint(IPAddress.Loopback,0));sockets.Add(s);}
            using(var owner=Process.GetCurrentProcess()) {
                var values=new Dictionary<string,object>{{"version",1},{"instance_id",id},{"state_root",root},{"owner_pid",owner.Id},{"owner_created",owner.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()}};
                string[] names={"ingress_port","steamvr_port","monaka_port","utility_port","observation_port"};for(int i=0;i<5;++i)values[names[i]]=((IPEndPoint)sockets[i].LocalEndPoint).Port;
                string path=Path.Combine(Path.GetDirectoryName(root),id+".json");var json=new JavaScriptSerializer();File.WriteAllText(path,json.Serialize(values));
                string record=id+"\n"+owner.Id+":"+values["owner_created"]+"\n"+values[names[0]]+","+values[names[1]]+","+values[names[2]]+","+values[names[3]]+","+values[names[4]]+"\n";
                var lease=new FileStream(Path.Combine(root,"owner.claim"),FileMode.CreateNew,FileAccess.ReadWrite,FileShare.Read);var bytes=Encoding.UTF8.GetBytes(record);lease.Write(bytes,0,bytes.Length);lease.Flush(true);
                using(var parsed=ReceiverIsolationProfile.Parse(new[]{"--isolated-profile",path})) {parsed.ValidateOwner();parsed.ClaimTray();Reject(()=>parsed.ClaimTray(),"duplicate/stale tray root accepted");}
                lease.Dispose();Reject(()=>ReceiverIsolationProfile.Parse(new[]{"--isolated-profile",path}),"released owner lease accepted");
                foreach(string key in new List<string>(values.Keys)){var missing=new Dictionary<string,object>(values);missing.Remove(key);File.WriteAllText(path,json.Serialize(missing));Reject(()=>ReceiverIsolationProfile.Parse(new[]{"--isolated-profile",path}),"missing key accepted: "+key);}
                File.WriteAllText(path,"{\"version\":1,\"version\":1}");Reject(()=>ReceiverIsolationProfile.Parse(new[]{"--isolated-profile",path}),"duplicate key accepted");
                Reject(()=>ReceiverIsolationProfile.Parse(new[]{"--isolated-profile"}),"missing profile path accepted");Reject(()=>ReceiverIsolationProfile.Parse(new[]{"--isolated-unknown",path}),"unknown isolation option accepted");
            }
        }finally{foreach(var s in sockets)s.Dispose();}
        Console.WriteLine("PASS GUI complete profile, exact listeners, leased ownership, stale tray, released lease, partial/duplicate/missing/unknown negatives");return 0;
    }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
