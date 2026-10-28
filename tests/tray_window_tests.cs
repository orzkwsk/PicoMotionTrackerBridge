using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Threading;
using System.Windows.Forms;
class TrayWindowTests {
 [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr window,int command);
 [DllImport("user32.dll")] static extern bool IsIconic(IntPtr window);
 static void Check(bool value,string message){if(!value)throw new Exception(message);}
 static void StartFixture(string root,out Process p) {
  p=Process.Start(new ProcessStartInfo(Assembly.GetExecutingAssembly().Location,"fixture"){UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=root});
  var until=Stopwatch.StartNew();
  while(until.ElapsedMilliseconds<5000){p.Refresh();if(p.HasExited)throw new Exception("Fixture exited");if(p.MainWindowHandle!=IntPtr.Zero)return;Thread.Sleep(20);}
  throw new Exception("Fixture window unavailable");
 }
 static void Stop(Process p){if(p==null)return;try{if(!p.HasExited){p.CloseMainWindow();Check(p.WaitForExit(5000),"Owned fixture did not exit");}}finally{p.Dispose();}}
 static void Set(object value,string name,object field){value.GetType().GetField(name,BindingFlags.NonPublic|BindingFlags.Instance).SetValue(value,field);}
 [STAThread] static int Main(string[] args) {
  if(args[0]=="fixture"){using(var form=new Form{Text="SA05 synthetic window "+Guid.NewGuid().ToString("N"),Width=320,Height=120})using(var timer=new System.Windows.Forms.Timer{Interval=15000}){timer.Tick+=delegate{form.Close();};timer.Start();form.Shown+=delegate{form.WindowState=FormWindowState.Minimized;};Application.Run(form);}return 0;}
  Process owned=null,other=null;int failures=0;
  try{
   var product=Assembly.LoadFrom(args[0]);var trayType=product.GetType("PicoOTBridge.SteamVrControlTray",true);
   StartFixture(Path.GetDirectoryName(args[0]),out owned);StartFixture(Path.GetDirectoryName(args[0]),out other);
   Thread.Sleep(100);owned.Refresh();other.Refresh();var handle=owned.MainWindowHandle;var otherHandle=other.MainWindowHandle;
   Check(IsIconic(handle)&&IsIconic(otherHandle),"Fixture precondition not minimized");
   // Constructor creates production-named IPC; invoke only the actual Open callback.
   var tray=FormatterServices.GetUninitializedObject(trayType);
   using(var icon=new NotifyIcon()){
    Set(tray,"controlProcess",owned);Set(tray,"notifyIcon",icon);
    trayType.GetMethod("OpenControlWindow",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(tray,null);
    var until=Stopwatch.StartNew();while(IsIconic(handle)&&until.ElapsedMilliseconds<2000)Thread.Sleep(20);
    Check(!IsIconic(handle),"Open leaves existing owned GUI minimized");
    Check(IsIconic(otherHandle),"Open modified another same-executable process");
    Console.WriteLine("PASS actual product Open restores only retained GUI Process; other instance unchanged");
    ShowWindow(handle,6);Thread.Sleep(50);Set(tray,"exitPending",true);
    trayType.GetMethod("OpenControlWindow",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(tray,null);
    Check(IsIconic(handle),"Exit-pending Open touched GUI");
    Console.WriteLine("PASS Exit-pending Open refuses reactivation");
   }
   var focus=product.GetType("PicoOTBridge.OwnedControlWindow");
   if(focus!=null){
    var method=focus.GetMethod("TryActivate",BindingFlags.Static|BindingFlags.NonPublic);
    Check(!(bool)method.Invoke(null,new object[]{null}),"Null child accepted");
    using(var current=Process.GetCurrentProcess())Check(!(bool)method.Invoke(null,new object[]{current}),"Windowless process accepted");
    owned.CloseMainWindow();Check(owned.WaitForExit(5000),"fixture exit timeout");
    Check(!(bool)method.Invoke(null,new object[]{owned}),"Exited process accepted");
    Console.WriteLine("PASS null, windowless, exited process refused");
   }
  }catch(Exception e){Console.WriteLine("FAIL "+e);failures=1;}
  finally{try{Stop(owned);}catch(Exception e){Console.WriteLine("CLEANUP FAIL "+e);failures=1;}try{Stop(other);}catch(Exception e){Console.WriteLine("CLEANUP FAIL "+e);failures=1;}}
  Console.WriteLine("RESULT exit="+failures+"; synthetic windows only; no hardware/IPC/service");return failures;
 }
}
