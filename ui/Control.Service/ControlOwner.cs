using System.Diagnostics;
using System.Globalization;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using PicoOTBridge.Control;

namespace PicoOTBridge.ControlService;

public interface IDriverControl
{
    Alignment Read(); void Apply(Alignment value); void Signal(string operation);
}
public sealed class DriverControl : IDriverControl
{
    const string Mapping = @"Local\PicoOTBridge_WorldAlignment";
    static MemoryMappedViewAccessor Open(out MemoryMappedFile mapping)
    {
        mapping = MemoryMappedFile.OpenExisting(Mapping, MemoryMappedFileRights.ReadWrite);
        var view = mapping.CreateViewAccessor(0, 40, MemoryMappedFileAccess.ReadWrite);
        if (view.ReadUInt32(0) != 0x504F5441 || view.ReadUInt32(4) != 1) { view.Dispose(); mapping.Dispose(); throw new InvalidDataException("SteamVR alignment IPC version mismatch."); }
        return view;
    }
    public Alignment Read() { using var view = Open(out var mapping); using (mapping) { var a = new Alignment(view.ReadDouble(16), view.ReadDouble(24), view.ReadDouble(32)); a.Validate(); return a; } }
    public void Apply(Alignment value)
    {
        value.Validate(); using var view = Open(out var mapping); using (mapping) { var generation = view.ReadUInt64(8); if (generation == ulong.MaxValue) throw new InvalidDataException("Driver alignment generation exhausted. Restart SteamVR."); view.Write(16, value.XMeters); view.Write(24, value.YMeters); view.Write(32, value.ZMeters); view.Write(8, generation + 1); view.Flush(); }
        Event(@"Local\PicoOTBridge_WorldAlignmentUpdated");
    }
    static void Event(string name) { using var signal = EventWaitHandle.OpenExisting(name); if (!signal.Set()) throw new IOException("Driver control event could not be signalled."); }
    public void Signal(string operation) => Event(operation switch { "orientation_zero" => @"Local\PicoOTBridge_OrientationZero", "orientation_clear" => @"Local\PicoOTBridge_OrientationClear", "position_zero" => @"Local\PicoOTBridge_PositionZero", "position_clear" => @"Local\PicoOTBridge_PositionClear", _ => throw new InvalidDataException("Unsupported diagnostic operation.") });
}
public sealed record BackendSnapshot(string State, string Output, DeviceStatus[] Devices);
public static class BackendStatus
{
    [DllImport("kernel32.dll")] static extern ulong GetTickCount64();
    public static BackendSnapshot Read(string mappingName=@"Local\PicoOTBridge_ControlStatus.v1")
    {
        try
        {
            using var map = MemoryMappedFile.OpenExisting(mappingName, MemoryMappedFileRights.Read);
            using var view = map.CreateViewAccessor(0, 280, MemoryMappedFileAccess.Read);
            for (int attempt = 0; attempt < 3; attempt++)
            {
                long before = view.ReadInt64(8); if ((before & 1) != 0) continue;
                if (view.ReadUInt32(0) != 0x43544F50 || view.ReadUInt32(4) != 1) return new("Unsupported status version", "Unknown", []);
                var uptime = view.ReadUInt64(16); uint route = view.ReadUInt32(28), count = view.ReadUInt32(32); if (count > 5) return new("Invalid status", "Unknown", []);
                var now = GetTickCount64(); if (now < uptime || now - uptime > 3000) return new("Status stale", "Unknown", []);
                var devices = new List<DeviceStatus>();
                for (int i = 0; i < count; i++)
                {
                    long offset = 40 + i * 48; var bytes = new byte[32]; view.ReadArray(offset, bytes, 0, 32); int end = Array.IndexOf(bytes, (byte)0); if (end < 1) return new("Invalid serial status", "Unknown", []);
                    string serial = new UTF8Encoding(false, true).GetString(bytes, 0, end); var flags = view.ReadUInt32(offset + 32); long age = view.ReadInt64(offset + 40);
                    double? ageMs = age >= 0 ? (double)age + (now - uptime) : null;
                    bool? p = (flags & 2) == 0 ? null : ageMs <= 500 && (flags & 4) != 0;
                    bool? o = (flags & 2) == 0 ? null : ageMs <= 500 && (flags & 8) != 0;
                    var tracking = ageMs is null ? "Unknown" : ageMs > 500 ? "Stale" : p == true && o == true ? "Tracking" : "Lost";
                    devices.Add(new(serial, (flags & 1) != 0 ? "Connected snapshot" : "Disconnected snapshot", tracking, p, o, ageMs));
                }
                Thread.MemoryBarrier(); if (before != view.ReadInt64(8)) continue;
                return new("Running", route switch { 0 => "SteamVR", 1 => "MonakaVR", 2 => "Both", 3 => "Disabled", _ => "Unknown" }, devices.ToArray());
            }
            return new("Status updating", "Unknown", []);
        }
        catch (FileNotFoundException) { return new("Unavailable", "Unknown", []); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or DecoderFallbackException) { return new("Status unavailable", "Unknown", []); }
    }
}
public sealed class ControlOwner : IAsyncDisposable
{
    readonly string root, data, alignmentPath; readonly IDriverControl driver; readonly Func<BackendSnapshot> backend;
    readonly SemaphoreSlim mutation = new(1, 1); readonly object sync = new();
    Alignment? desired, saved; string lastError = ""; ControlResponse cached = new();
    string? activeId; CancellationTokenSource? active;
    public ControlOwner(string root, string? dataDirectory = null, IDriverControl? driver = null, Func<BackendSnapshot>? backend = null)
    {
        this.root = root; data = dataDirectory ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PicoMotionTrackerBridge");
        alignmentPath = Path.Combine(data, "steamvr_alignment.json"); this.driver = driver ?? new DriverControl(); this.backend = backend ?? (()=>BackendStatus.Read());
        Directory.CreateDirectory(data);
        try { saved = Load(); desired = saved; } catch (Exception e) { lastError = "Saved alignment could not be loaded. Check diagnostics; existing file was preserved. " + e.GetType().Name; }
    }
    Alignment? Load()
    {
        if (!File.Exists(alignmentPath)) return null; string text = File.ReadAllText(alignmentPath); ControlJson.ValidateObject(text);
        using var document = JsonDocument.Parse(text); if (document.RootElement.GetProperty("version").GetInt32() != 1) throw new InvalidDataException("Unsupported alignment version.");
        var value = JsonSerializer.Deserialize<Alignment>(text, ControlJson.Options) ?? throw new InvalidDataException("Invalid saved alignment.");
        foreach (var key in new[] { "xMeters", "yMeters", "zMeters" }) if (!document.RootElement.TryGetProperty(key, out _)) throw new InvalidDataException("Incomplete saved alignment.");
        value.Validate(); return value;
    }
    public static void AtomicWrite(string path, string value)
    {
        var directory = Path.GetDirectoryName(path)!; Directory.CreateDirectory(directory); string temp = Path.Combine(directory, $".{Path.GetFileName(path)}.{Guid.NewGuid():N}.tmp");
        try { using (var stream = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None, 4096, FileOptions.WriteThrough)) { var bytes = Encoding.UTF8.GetBytes(value); stream.Write(bytes); stream.Flush(true); } if (File.Exists(path)) File.Replace(temp, path, null); else File.Move(temp, path); }
        finally { if (File.Exists(temp)) File.Delete(temp); }
    }
    public async Task PollAsync(CancellationToken token)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(1));
        try { do { if (await mutation.WaitAsync(0, token)) { try { Refresh(); } catch (Exception e) { lastError = "Status refresh failed: " + e.GetType().Name; await LogAsync(e); } finally { mutation.Release(); } } } while (await timer.WaitForNextTickAsync(token)); } catch (OperationCanceledException) { }
    }
    void Refresh()
    {
        string availability = "Unavailable";
        try { var current = driver.Read(); availability = "Available"; if (desired is { } target && target != current) driver.Apply(target); if (desired is null) desired = current; }
        catch (FileNotFoundException) { availability = "Unavailable: start SteamVR with the PICO driver"; }
        catch (Exception e) { availability = "Unavailable"; lastError = e.GetType().Name + ": check the SteamVR driver and refresh."; }
        var state = backend(); var requested = File.Exists(Path.Combine(data, "output_route.txt")) ? File.ReadAllText(Path.Combine(data, "output_route.txt")).Trim() : "steamvr";
        var value = new ControlResponse { Ok = true, Code = "Ok", Backend = state.State, Driver = availability, Devices = state.Devices, Output = state.Output, RequestedOutput = requested, Current = desired, Saved = saved, LastError = lastError, Message = lastError };
        lock (sync) cached = value;
    }
    ControlResponse Snapshot(ControlRequest request, string? message = null) { lock (sync) return cached with { Id = request.Id, Ok = true, Code = "Ok", Current = desired, Saved = saved, Message = message ?? cached.Message }; }
    public async Task<ControlResponse> ExecuteAsync(ControlRequest request, CancellationToken token)
    {
        if (request.Operation == "get_status") return Snapshot(request);
        if (request.Operation == "cancel_operation") { lock (sync) { if (request.TargetId != activeId || active is null) throw new InvalidOperationException("Operation is already complete or belongs to another request."); active.Cancel(); } return Snapshot(request, "Cancellation requested."); }
        if (!await mutation.WaitAsync(0, token)) throw new InvalidOperationException("Busy: another control operation is running.");
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(token); cancellation.CancelAfter(TimeSpan.FromSeconds(25));
        lock (sync) { activeId = request.Id; active = cancellation; }
        try
        {
            string message;
            switch (request.Operation)
            {
                case "get_alignment": desired = driver.Read(); message = "Read current driver alignment."; break;
                case "set_alignment": var value = request.Alignment ?? throw new InvalidDataException("Alignment is required."); value.Validate(); driver.Apply(value); desired = value; message = "Alignment applied; Save to retain it."; break;
                case "reset_alignment": var neutral = new Alignment(0, 0, 0); driver.Apply(neutral); desired = neutral; message = "Alignment reset; Save to retain it."; break;
                case "save_alignment": var toSave = request.Alignment ?? throw new InvalidDataException("Alignment is required."); toSave.Validate(); AtomicWrite(alignmentPath, JsonSerializer.Serialize(new { version = 1, xMeters = toSave.XMeters, yMeters = toSave.YMeters, zMeters = toSave.ZMeters }, ControlJson.Options)); saved = toSave; message = "Alignment saved."; break;
                case "reload_alignment": var loaded = Load() ?? throw new InvalidOperationException("No saved alignment. Apply and Save an alignment first."); driver.Apply(loaded); saved = desired = loaded; message = "Saved alignment loaded and applied."; break;
                case "calibrate_translation":
                    if (string.IsNullOrWhiteSpace(request.Tracker) || request.Tracker.Length > 96 || request.Tracker.Any(char.IsControl) || request.Reference is not ("left" or "right" or "hmd")) throw new InvalidDataException("Select a tracker and left/right/HMD reference.");
                    driver.Signal("position_clear"); await Task.Delay(100, cancellation.Token);
                    var result = await CalibrateAsync(request.Tracker, request.Reference, cancellation.Token);
                    cancellation.Token.ThrowIfCancellationRequested(); driver.Apply(result); desired = result; message = "Measured alignment applied; verify it, then Save. It has not been saved."; break;
                case "set_route": if (request.Route is not ("steamvr" or "monaka" or "both" or "disabled")) throw new InvalidDataException("Unsupported output route."); AtomicWrite(Path.Combine(data, "output_route.txt"), request.Route + "\n"); try { using var updated = EventWaitHandle.OpenExisting(@"Local\PicoOTBridge_OutputRouteUpdated"); updated.Set(); } catch (WaitHandleCannotBeOpenedException) { } message = "Output route saved; applies when the PC receiver is running."; break;
                case "start_backend": StartBackend(); await WaitBackendAsync(true, cancellation.Token); message = "PC receiver start requested. Refresh status; HMD backend must also be running."; break;
                case "stop_backend": using (var stop = EventWaitHandle.OpenExisting(@"Local\PicoOTBridge_ServiceStop.v1")) stop.Set(); await WaitBackendAsync(false, cancellation.Token); message = "PC receiver stop requested. The HMD backend was not restarted."; break;
                case "orientation_zero": case "orientation_clear": case "position_zero": case "position_clear": driver.Signal(request.Operation); message = "Diagnostic driver control requested."; break;
                default: throw new InvalidDataException("Unsupported operation. Check frontend and service versions.");
            }
            lastError = ""; Refresh(); return Snapshot(request, message);
        }
        catch (Exception e) { lastError = e.GetType().Name + ": control operation failed. Check logs and retry after resolving the reported condition."; await LogAsync(e); throw; }
        finally { lock (sync) { active = null; activeId = null; } mutation.Release(); }
    }
    string Find(string executable)
    {
        var configuration=typeof(ControlOwner).Assembly.GetCustomAttributes(typeof(System.Reflection.AssemblyConfigurationAttribute),false).OfType<System.Reflection.AssemblyConfigurationAttribute>().FirstOrDefault()?.Configuration??"Release";
        foreach (var directory in new[] { $"build-bridge-host/{configuration}", $"build/runtime/{configuration}", $"build/ui-baseline/{configuration}", $"build-steamvr/{configuration}", "build/runtime/Release", "build-bridge-host/Release", "build-steamvr/Release", "build-steamvr/Debug", "build-steamvr" }) { var path = Path.Combine(root, directory, executable); if (File.Exists(path)) return path; }
        throw new FileNotFoundException(executable + " is missing. Build the PC receiver or SteamVR driver with the documented script.");
    }
    void StartBackend()
    {
        if (backend().State == "Running") return;
        var start = new ProcessStartInfo(Find("pico_ot_bridge_service.exe")) { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = root, RedirectStandardOutput = true, RedirectStandardError = true };
        var process = Process.Start(start) ?? throw new IOException("PC receiver could not start.");
        _ = Task.Run(async () => { try { await Task.WhenAll(DrainLogAsync(process.StandardOutput, "backend"), DrainLogAsync(process.StandardError, "backend-errors")); await process.WaitForExitAsync(); } finally { process.Dispose(); } });
    }
    async Task WaitBackendAsync(bool running, CancellationToken token)
    {
        var deadline = Stopwatch.StartNew();
        while ((backend().State == "Running") != running)
        {
            if (deadline.Elapsed > TimeSpan.FromSeconds(3)) throw new IOException("PC receiver did not reach the requested state. Open logs; an older or unavailable instance may own ingress. Check the existing launcher, then retry.");
            await Task.Delay(50, token);
        }
    }
    async Task DrainLogAsync(StreamReader reader, string name)
    {
        string logs = Path.Combine(data, "logs"); Directory.CreateDirectory(logs); await using var log = new StreamWriter(Path.Combine(logs, $"{name}-{Guid.NewGuid():N}.log"));
        while (await reader.ReadLineAsync() is { } line) { await log.WriteLineAsync(line); await log.FlushAsync(); }
    }
    async Task<Alignment> CalibrateAsync(string serial, string reference, CancellationToken token)
    {
        var start = new ProcessStartInfo(Find("pico_ot_steamvr_calibrator.exe")) { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (var argument in new[] { "--tracker", serial, "--reference", reference, "--measure-only" }) start.ArgumentList.Add(argument);
        using var process = Process.Start(start) ?? throw new IOException("Calibrator could not start.");
        var stdout = process.StandardOutput.ReadToEndAsync(token); var stderr = process.StandardError.ReadToEndAsync(token);
        try
        {
            await process.WaitForExitAsync(token); var output = await stdout; var errors = await stderr; if (process.ExitCode != 0) throw new InvalidOperationException("Calibration failed. Keep tracker and reference together, check tracking, then retry. Details are in the log."); if (output.Length > 65536 || errors.Length > 65536) throw new InvalidDataException("Unexpected calibrator output size.");
            var match = Regex.Match(output, @"(?m)^CALIBRATION_RESULT\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s*$", RegexOptions.CultureInvariant); if (!match.Success) throw new InvalidDataException("Calibration result is unavailable. Check tracking and retry.");
            var value = new Alignment(double.Parse(match.Groups[1].Value, CultureInfo.InvariantCulture), double.Parse(match.Groups[2].Value, CultureInfo.InvariantCulture), double.Parse(match.Groups[3].Value, CultureInfo.InvariantCulture)); value.Validate(); return value;
        }
        catch { if (!process.HasExited) process.Kill(true); await process.WaitForExitAsync(); try { await Task.WhenAll(stdout, stderr); } catch (Exception) { } throw; }
    }
    async Task LogAsync(Exception error) { try { string logs = Path.Combine(data, "logs"); Directory.CreateDirectory(logs); await File.AppendAllTextAsync(Path.Combine(logs, "control.log"), $"{DateTime.UtcNow:o} {error}\n"); } catch (IOException) { } }
    public ValueTask DisposeAsync() { lock (sync) active?.Cancel(); mutation.Dispose(); return ValueTask.CompletedTask; }
}
