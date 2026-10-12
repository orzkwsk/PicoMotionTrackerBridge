using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using PicoOTBridge;
using PicoOTBridge.Control;
internal static class Program
{
    [STAThread] static int Main()
    {
        var model=new ControlViewModel(new Fake());model.Refresh.Execute(null);
        var window=new SteamVrControlWpf.ControlWindow(model);
        var root=(FrameworkElement)window.Content;root.Measure(new Size(980,940));root.Arrange(new Rect(0,0,980,940));root.UpdateLayout();
        var image=new RenderTargetBitmap(980,940,96,96,PixelFormats.Pbgra32);image.Render(root);
        var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(image));Directory.CreateDirectory("build/ui-smoke");using(var file=File.Create("build/ui-smoke/pico-five-devices.png"))encoder.Save(file);
        if(model.Devices.Count!=5)throw new InvalidOperationException("Device binding failed.");
        model.DisposeAsync().AsTask().GetAwaiter().GetResult();
        Console.WriteLine("PASS offscreen WPF layout/binding with five devices. Interactive GUI smoke/HIL NOT RUN.");return 0;
    }
    sealed class Fake:IControlClient
    {
        public Task<ControlResponse> RequestAsync(ControlRequest request,CancellationToken token)=>Task.FromResult(new ControlResponse {Id=request.Id,Ok=true,Code="Ok",Backend="Running (mock)",Driver="Unavailable (mock)",Current=new(0.01,0.02,0.03),Saved=new(0,0,0),Output="SteamVR",RequestedOutput="steamvr",Message="Start SteamVR with the PICO driver to apply alignment.",Devices=Enumerable.Range(0,5).Select(i=>new DeviceStatus($"PICO-SERIAL-{i}","Connected snapshot",i==1?"Lost":i==2?"Stale":"Tracking",i is not (1 or 2),i is not (1 or 2),i==2?900:10)).ToArray()});
        public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
    }
}
