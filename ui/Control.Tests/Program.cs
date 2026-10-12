using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using PicoOTBridge.Control;
using PicoOTBridge.ControlService;
static void Check(bool value, string reason) { if (!value) throw new Exception(reason); }
var data = Path.Combine(Environment.CurrentDirectory, "build", "control-test-data", Guid.NewGuid().ToString("N")); Directory.CreateDirectory(data);
var driver = new FakeDriver(); await using var owner = new ControlOwner(Environment.CurrentDirectory, data, driver, () => new BackendSnapshot("Running", "SteamVR", [new("serial-1", "Connected snapshot", "Tracking", true, true, 10)]));
var request = new ControlRequest(1, "test", "get_alignment");
Check((await owner.ExecuteAsync(request, CancellationToken.None)).Current == driver.Value, "driver alignment read");
await owner.ExecuteAsync(request with { Operation = "set_alignment", Alignment = new(1, 2, 3) }, CancellationToken.None); Check(driver.Value == new Alignment(1, 2, 3), "alignment applied");
await owner.ExecuteAsync(request with { Operation = "save_alignment", Alignment = new(1, 2, 3) }, CancellationToken.None);
driver.Value = new(9, 9, 9); await owner.ExecuteAsync(request with { Operation = "reload_alignment" }, CancellationToken.None); Check(driver.Value == new Alignment(1, 2, 3), "atomic save and load");
driver.Value=new(0,0,0);using(var pollingToken=new CancellationTokenSource()){var polling=owner.PollAsync(pollingToken.Token);Check(driver.Value==new Alignment(1,2,3),"desired alignment reapplied after driver restart");pollingToken.Cancel();await polling;}
var original = File.ReadAllText(Path.Combine(data, "steamvr_alignment.json"));
try { await owner.ExecuteAsync(request with { Operation = "save_alignment", Alignment = new(double.NaN, 0, 0) }, CancellationToken.None); throw new Exception("invalid save accepted"); } catch (InvalidDataException) { }
Check(File.ReadAllText(Path.Combine(data, "steamvr_alignment.json")) == original, "invalid persistence preserves prior file");
await owner.ExecuteAsync(request with { Operation = "set_route", Route = "disabled" }, CancellationToken.None); Check(File.ReadAllText(Path.Combine(data, "output_route.txt")).Trim() == "disabled", "route persistence owner");
var blocked = Path.Combine(data, "blocked"); Directory.CreateDirectory(Path.Combine(blocked, "steamvr_alignment.json")); await using (var broken = new ControlOwner(Environment.CurrentDirectory, blocked, driver, () => new("Unavailable", "Unknown", [])))
{
    try { await broken.ExecuteAsync(request with { Operation = "save_alignment", Alignment = new(0, 0, 0) }, CancellationToken.None); throw new Exception("persistence failure accepted"); } catch (IOException) { }
}
// Real Windows pipe framing/current-user transport with a hardware-free owner.
string pipeName = "PicoOTBridge.Tests." + Guid.NewGuid().ToString("N");
async Task Serve() { using var pipe = new NamedPipeServerStream(pipeName, PipeDirection.InOut, 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly); using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(10)); await pipe.WaitForConnectionAsync(deadline.Token); await PicoOTBridge.ControlService.Program.ServeAsync(pipe, owner, deadline.Token); }
await using (var client = new PipeControlClient(pipeName))
{
    var server = Serve(); var result = await client.RequestAsync(new(1, "status", "get_status"), CancellationToken.None); await server; Check(result.Version == 1 && result.Id == "status" && result.Devices.Length == 1, "real pipe response correlation/status");
    foreach (var frame in new[] { "{", "{\"version\":1,\"version\":1,\"id\":\"x\",\"operation\":\"get_status\"}", "{\"version\":2,\"id\":\"x\",\"operation\":\"get_status\"}", new string('x', 4097) })
    {
        server = Serve(); using var raw = new NamedPipeClientStream(".", pipeName, PipeDirection.InOut, PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly); await raw.ConnectAsync(); try { await raw.WriteAsync(Encoding.UTF8.GetBytes(frame + "\n")); var response = await ControlJson.ReadFrameAsync(raw, 16384, CancellationToken.None); Check(JsonDocument.Parse(response).RootElement.GetProperty("ok").GetBoolean() == false, "invalid/version/duplicate/oversize rejected"); } catch (IOException) when (frame.Length > 4096) { /* bounded oversized frame may close while writer is still sending */ }
        await server;
    }
    server = Serve(); using (var raw = new NamedPipeClientStream(".", pipeName, PipeDirection.InOut, PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly)) { await raw.ConnectAsync(); await raw.WriteAsync("{"u8.ToArray()); }
    await server; // disconnected partial frame does not retain work
    server = Serve(); await client.RequestAsync(new(1, "reconnect", "get_status"), CancellationToken.None); await server;
}
// Actual fixed native status ABI, read through the production service adapter.
var mappingName=@"Local\PicoOTBridge.StatusTests."+Guid.NewGuid().ToString("N");
using(var mapping=System.IO.MemoryMappedFiles.MemoryMappedFile.CreateNew(mappingName,280))using(var view=mapping.CreateViewAccessor()) {
    view.Write(0,0x43544F50u);view.Write(4,1u);view.Write(8,2L);view.Write(16,(ulong)Environment.TickCount64);view.Write(28,0u);view.Write(32,5u);
    for(int i=0;i<5;i++){var serial=Encoding.UTF8.GetBytes($"native-{i}");view.WriteArray(40+i*48,serial,0,serial.Length);view.Write(40+i*48+32,i==3?0u:15u);view.Write(40+i*48+40,i==2?900L:i==3?-1L:10L);}
    var snapshot=BackendStatus.Read(mappingName);Check(snapshot.State=="Running"&&snapshot.Devices.Length==5,"native status ABI/five devices");Check(snapshot.Devices[2].Tracking=="Stale"&&snapshot.Devices[2].PositionValid==false,"native stale invalidation");Check(snapshot.Devices[3].PositionValid==null&&snapshot.Devices[3].AgeMs==null,"native unknown is not zero");
    view.Write(4,99u);Check(BackendStatus.Read(mappingName).State=="Unsupported status version","unknown native version rejected");view.Write(4,1u);view.Write(32,6u);Check(BackendStatus.Read(mappingName).State=="Invalid status","native bounds checked");view.Write(32,5u);view.Write(8,3L);Check(BackendStatus.Read(mappingName).State=="Status updating","seqlock rejects partial publication");view.Write(8,4L);view.Write(16,0UL);Check(BackendStatus.Read(mappingName).State=="Status stale","dead backend heartbeat rejected");
}
var fake = new FakeClient(); await using (var model = new ControlViewModel(fake))
{
    model.Refresh.Execute(null); await fake.Wait(); Check(model.Devices.Count == 5, "five devices"); Check(model.Devices.All(d => d.Battery == "Unavailable"), "unknown battery");
    model.SelectedSerial="serial-0";model.Refresh.Execute(null);await fake.Wait();Check(model.SelectedSerial=="serial-0","selection preserved on poll");
    model.X = "2"; Check(model.Unsaved, "unsaved state"); model.Save.Execute(null); await fake.Wait(); Check(!model.Unsaved, "saved state");
    fake.Pending = new(TaskCreationOptions.RunContinuationsAsynchronously); model.Apply.Execute(null); model.Apply.Execute(null); Check(model.Busy && fake.ApplyCount == 1, "duplicate guard");
    model.Cancel.Execute(null); await fake.Wait(); Check(!model.Busy && model.Message.Contains("cancelled"), "cancel operation");
    fake.Error = new TimeoutException("Service timeout; refresh"); model.Refresh.Execute(null); await fake.Wait(); Check(model.Message.Contains("timeout"), "timeout display");
    fake.Error = new IOException("Service disconnected; reconnect"); model.Refresh.Execute(null); await fake.Wait(); Check(model.Message.Contains("reconnect"), "disconnect display");
    fake.Error = new InvalidDataException("Invalid response/version"); model.Refresh.Execute(null); await fake.Wait(); Check(model.Message.Contains("Invalid"), "invalid response display");
    model.Refresh.Execute(null); await fake.Wait();
    fake.Error = new IOException("Persistence failed"); model.Save.Execute(null); await fake.Wait(); Check(model.Message.Contains("Persistence"), "persistence failure display");
}
Check(fake.Disposed, "shutdown awaits disposal");
var shutting = new FakeClient(); var shutdownModel = new ControlViewModel(shutting); shutting.Pending = new(TaskCreationOptions.RunContinuationsAsynchronously); shutdownModel.Refresh.Execute(null); await shutdownModel.DisposeAsync(); Check(!shutdownModel.Busy && shutting.Disposed, "shutdown during operation");
Console.WriteLine("PASS persistence/validation/failure, real current-user pipe/framing/reconnect, five-device states, duplicate/cancel/timeout/disconnect/invalid/shutdown");
sealed class FakeDriver : IDriverControl { public Alignment Value = new(0, 0, 0); public Alignment Read() => Value; public void Apply(Alignment a) => Value = a; public void Signal(string operation) { } }
sealed class FakeClient : IControlClient
{
    public Exception? Error; public TaskCompletionSource<ControlResponse>? Pending; public int ApplyCount; public bool Disposed; Alignment saved = new(0, 0, 0);
    public async Task<ControlResponse> RequestAsync(ControlRequest request, CancellationToken token) { if (request.Operation == "set_alignment") ApplyCount++; if (Error is { } error) { Error = null; throw error; } if (Pending is { } pending) { try { return await pending.Task.WaitAsync(token); } finally { Pending = null; } } if (request.Operation == "save_alignment") saved = request.Alignment!; return new() { Id = request.Id, Ok = true, Code = "Ok", Backend = "Running", Driver = "Available", Current = new(0, 0, 0), Saved = saved, Devices = Enumerable.Range(0, 5).Select(i => new DeviceStatus($"serial-{i}", "Connected snapshot", i == 1 ? "Lost" : i == 2 ? "Stale" : "Tracking", i != 1, i != 1, i == 2 ? 900 : 10)).ToArray() }; }
    public async Task Wait() => await Task.Delay(40); public ValueTask DisposeAsync() { Disposed = true; return ValueTask.CompletedTask; }
}
