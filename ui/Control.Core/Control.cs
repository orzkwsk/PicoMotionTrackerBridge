using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO.Pipes;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Windows.Input;

namespace PicoOTBridge.Control;

public sealed record Alignment(double XMeters, double YMeters, double ZMeters)
{
    public void Validate() { if (!double.IsFinite(XMeters) || !double.IsFinite(YMeters) || !double.IsFinite(ZMeters) || Math.Abs(XMeters) > 1000 || Math.Abs(YMeters) > 1000 || Math.Abs(ZMeters) > 1000) throw new InvalidDataException("Alignment must be finite and within 1000 metres."); }
}
public sealed record DeviceStatus(string Serial, string Connection, string Tracking, bool? PositionValid, bool? OrientationValid, double? AgeMs)
{
    public string Position => PositionValid is null ? "Unknown" : PositionValid.Value ? "Valid" : "Invalid";
    public string Orientation => OrientationValid is null ? "Unknown" : OrientationValid.Value ? "Valid" : "Invalid";
    public string LastUpdate => AgeMs is null ? "Not available" : $"{AgeMs:F0} ms ago";
    public string Battery => "Unavailable";
}
public sealed record ControlRequest(int Version, string Id, string Operation, Alignment? Alignment = null, string? Tracker = null, string? Reference = null, string? Route = null, string? TargetId = null);
public sealed record ControlResponse
{
    public int Version { get; init; } = 1; public string Id { get; init; } = ""; public bool Ok { get; init; }
    public string Code { get; init; } = "Unknown"; public string Message { get; init; } = "";
    public string Backend { get; init; } = "Unknown"; public string Driver { get; init; } = "Unknown";
    public string Output { get; init; } = "Unknown"; public string RequestedOutput { get; init; } = "Unknown";
    public string LastError { get; init; } = ""; public Alignment? Current { get; init; }
    public Alignment? Saved { get; init; }
    public DeviceStatus[] Devices { get; init; } = [];
}
public interface IControlClient : IAsyncDisposable { Task<ControlResponse> RequestAsync(ControlRequest request, CancellationToken token); }
public static class ControlJson
{
    public static readonly JsonSerializerOptions Options = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
    public static async Task<string> ReadFrameAsync(Stream stream, int maximum, CancellationToken token)
    {
        var bytes = new List<byte>(); var one = new byte[1];
        while (true)
        {
            if (await stream.ReadAsync(one, token) == 0) throw new IOException("Control service disconnected. Start the control service and refresh.");
            if (one[0] == 10) break;
            if (bytes.Count >= maximum) throw new InvalidDataException("Control frame exceeds its size limit.");
            bytes.Add(one[0]);
        }
        return new System.Text.UTF8Encoding(false, true).GetString(bytes.ToArray());
    }
    public static void ValidateObject(string text)
    {
        using var doc = JsonDocument.Parse(text, new JsonDocumentOptions { MaxDepth = 8 });
        static void Unique(JsonElement element)
        {
            if (element.ValueKind == JsonValueKind.Object) { var names = new HashSet<string>(); foreach (var field in element.EnumerateObject()) { if (!names.Add(field.Name)) throw new InvalidDataException("Duplicate control field."); Unique(field.Value); } }
            else if (element.ValueKind == JsonValueKind.Array) foreach (var item in element.EnumerateArray()) Unique(item);
        }
        if (doc.RootElement.ValueKind != JsonValueKind.Object) throw new InvalidDataException("Control message must be an object."); Unique(doc.RootElement);
    }
}
public sealed class PipeControlClient(string? endpoint = null) : IControlClient
{
    public const string PipeName = "PicoOTBridge.Control.v1";
    public static string SessionPipeName=>PipeName+".Session."+System.Diagnostics.Process.GetCurrentProcess().SessionId;
    readonly string pipeName=endpoint??SessionPipeName;
    public async Task<ControlResponse> RequestAsync(ControlRequest request, CancellationToken token)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token); timeout.CancelAfter(request.Operation == "calibrate_translation" ? TimeSpan.FromSeconds(30) : TimeSpan.FromSeconds(6));
        try
        {
            using var pipe = new NamedPipeClientStream(".", pipeName, PipeDirection.InOut, PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
            await pipe.ConnectAsync(timeout.Token);
            var bytes = System.Text.Encoding.UTF8.GetBytes(JsonSerializer.Serialize(request, ControlJson.Options) + "\n");
            if (bytes.Length > 4096) throw new InvalidDataException("Control request is too large.");
            await pipe.WriteAsync(bytes, timeout.Token); await pipe.FlushAsync(timeout.Token);
            var frame = await ControlJson.ReadFrameAsync(pipe, 16384, timeout.Token); ControlJson.ValidateObject(frame);
            using var document=JsonDocument.Parse(frame);
            if(!document.RootElement.TryGetProperty("version",out var version)||version.ValueKind!=JsonValueKind.Number||version.GetInt32()!=1)throw new InvalidDataException("Missing or unsupported control version.");
            var result = JsonSerializer.Deserialize<ControlResponse>(frame, ControlJson.Options) ?? throw new InvalidDataException("Invalid control response.");
            if (result.Version != 1 || result.Id != request.Id || result.Devices is null || result.Devices.Length > 5 || result.Devices.Any(d => d.AgeMs is < 0 || d.Serial is null)) throw new InvalidDataException("Invalid control response/version.");
            result.Current?.Validate(); result.Saved?.Validate();
            if (!result.Ok) throw new InvalidOperationException($"{result.Code}: {result.Message}");
            return result;
        }
        catch (OperationCanceledException)
        {
            if (request.Operation == "calibrate_translation")
            {
                try { await CancelAsync(request.Id); } catch (Exception) { }
            }
            if (token.IsCancellationRequested) throw;
            throw new TimeoutException("Control service did not respond. Start or restart the control service, then refresh status.");
        }
    }
    async Task CancelAsync(string id)
    {
        using var token = new CancellationTokenSource(TimeSpan.FromSeconds(2));
        await RequestAsync(new(1, Guid.NewGuid().ToString("N"), "cancel_operation", TargetId: id), token.Token);
    }
    public ValueTask DisposeAsync() => ValueTask.CompletedTask;
}
public sealed class AsyncCommand(Func<Task> execute, Func<bool> enabled) : ICommand
{
    public event EventHandler? CanExecuteChanged;
    public bool CanExecute(object? parameter) => enabled();
    public async void Execute(object? parameter) { if (enabled()) await execute(); }
    public void Refresh() => CanExecuteChanged?.Invoke(this, EventArgs.Empty);
}
public sealed class ControlViewModel : INotifyPropertyChanged, IAsyncDisposable
{
    readonly IControlClient client; readonly CancellationTokenSource lifetime = new();
    readonly List<AsyncCommand> commands = []; Task? polling; CancellationTokenSource? operation;
    bool busy, closing, initialized; string x = "0.000000", y = "0.000000", z = "0.000000";
    public bool Busy { get => busy; private set { busy = value; Changed(); UpdateCommands(); } }
    public string X { get => x; set { x = value; Changed(); Changed(nameof(Unsaved)); } }
    public string Y { get => y; set { y = value; Changed(); Changed(nameof(Unsaved)); } }
    public string Z { get => z; set { z = value; Changed(); Changed(nameof(Unsaved)); } }
    string? selectedSerial;
    public string? SelectedSerial { get => selectedSerial; set { selectedSerial = value; Changed(); UpdateCommands(); } }
    public string Reference { get; set; } = "right";
    public string Route { get; set; } = "steamvr";
    public ControlResponse Status { get; private set; } = new();
    public string Message { get; private set; } = "Start the control service to read backend status.";
    public string Health => $"Control service: {ServiceState} | PC receiver: {Status.Backend} | SteamVR driver: {Status.Driver}\nOutput: {Status.Output} | Requested: {Status.RequestedOutput} | Devices: {Devices.Count}";
    public bool DriverAvailable => Status.Driver == "Available";
    public bool CanApply => DriverAvailable && !Busy;
    public string ServiceState { get; private set; } = "Unavailable";
    public ObservableCollection<DeviceStatus> Devices { get; } = [];
    public bool Unsaved { get { try { var a = Fields(); return Status.Saved is not { } saved || Math.Abs(a.XMeters - saved.XMeters) > 5e-7 || Math.Abs(a.YMeters - saved.YMeters) > 5e-7 || Math.Abs(a.ZMeters - saved.ZMeters) > 5e-7; } catch (Exception) { return true; } } }
    public AsyncCommand Read { get; }
    public AsyncCommand Apply { get; }
    public AsyncCommand Reset { get; }
    public AsyncCommand Save { get; }
    public AsyncCommand Load { get; }
    public AsyncCommand Calibrate { get; }
    public AsyncCommand Refresh { get; }
    public AsyncCommand StartBackend { get; }
    public AsyncCommand StopBackend { get; }
    public AsyncCommand SetRoute { get; }
    public ICommand Cancel { get; }
    public event PropertyChangedEventHandler? PropertyChanged;
    public ControlViewModel(IControlClient client)
    {
        this.client = client;
        Read = Command(async () => { var status = await Request("get_alignment"); if (status.Current is { } a) Put(a); }, () => DriverAvailable);
        Apply = Command(async () => await Request("set_alignment", Fields()), () => DriverAvailable);
        Reset = Command(async () => { var response = await Request("reset_alignment"); if (response.Current is { } a) Put(a); }, () => DriverAvailable);
        Save = Command(async () => await Request("save_alignment", Fields()), () => ServiceState == "Running");
        Load = Command(async () => { var response = await Request("reload_alignment"); if (response.Current is { } a) Put(a); }, () => DriverAvailable);
        Calibrate = Command(async () => { if (string.IsNullOrWhiteSpace(SelectedSerial)) throw new InvalidOperationException("Select a connected tracker, then refresh status if needed."); var response = await Request("calibrate_translation", tracker: SelectedSerial, reference: Reference); if (response.Current is { } a) Put(a); }, () => DriverAvailable && !string.IsNullOrEmpty(SelectedSerial));
        Refresh = Command(async () => await Request("get_status"));
        StartBackend = Command(async () => await Request("start_backend"));
        StopBackend = Command(async () => await Request("stop_backend"));
        SetRoute = Command(async () => await Request("set_route", route: Route));
        Cancel = new CancelCommand(() => operation?.Cancel(), () => Busy);
    }
    AsyncCommand Command(Func<Task> action, Func<bool>? enabled = null) { var cmd = new AsyncCommand(() => RunAsync(action), () => !Busy && !closing && (enabled?.Invoke() ?? true)); commands.Add(cmd); return cmd; }
    public async Task RunAsync(Func<Task> action)
    {
        if (Busy || closing) return; Busy = true; operation = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
        try { await action(); }
        catch (OperationCanceledException) { Message = "Operation cancelled. Refresh to verify current state."; Changed(nameof(Message)); }
        catch (Exception e) { Message = e.Message; ServiceState = "Unavailable or operation failed"; Changed(nameof(Message)); Changed(nameof(Health)); }
        finally { operation.Dispose(); operation = null; Busy = false; }
    }
    async Task<ControlResponse> Request(string name, Alignment? alignment = null, string? tracker = null, string? reference = null, string? route = null)
    {
        var response = await client.RequestAsync(new(1, Guid.NewGuid().ToString("N"), name, alignment, tracker, reference, route), operation!.Token);
        if (!initialized) { if ((response.Current ?? response.Saved) is { } starting) Put(starting); initialized = true; }
        Status = response; ServiceState = "Running"; var selection = SelectedSerial; Devices.Clear(); foreach (var device in response.Devices) Devices.Add(device); SelectedSerial = Devices.Any(d => d.Serial == selection) ? selection : null;
        Message = response.Message; Changed(nameof(Status)); Changed(nameof(Message)); Changed(nameof(Health)); Changed(nameof(Unsaved)); return response;
    }
    Alignment Fields()
    {
        static double Parse(string value) { if (!double.TryParse(value, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var parsed)) throw new InvalidOperationException("Enter numeric X, Y and Z in metres."); return parsed; }
        var result = new Alignment(Parse(X), Parse(Y), Parse(Z)); result.Validate(); return result;
    }
    void Put(Alignment value) { X = value.XMeters.ToString("F6", System.Globalization.CultureInfo.InvariantCulture); Y = value.YMeters.ToString("F6", System.Globalization.CultureInfo.InvariantCulture); Z = value.ZMeters.ToString("F6", System.Globalization.CultureInfo.InvariantCulture); }
    public Task NudgeAsync(string axis, int direction, double step) => RunAsync(async () =>
    {
        var a = Fields(); a = axis switch { "X" => a with { XMeters = a.XMeters + direction * step }, "Y" => a with { YMeters = a.YMeters + direction * step }, "Z" => a with { ZMeters = a.ZMeters + direction * step }, _ => throw new InvalidOperationException("Unknown axis") };
        a.Validate(); Put(a); await Request("set_alignment", a);
    });
    public Task DiagnosticAsync(string command) => RunAsync(async () => await Request(command));
    public void StartPolling() { polling ??= PollAsync(); }
    async Task PollAsync()
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(1));
        try { do { if (!Busy) await RunAsync(async () => await Request("get_status")); } while (await timer.WaitForNextTickAsync(lifetime.Token)); } catch (OperationCanceledException) { }
    }
    public string Diagnostics() => $"PICO Control {typeof(ControlViewModel).Assembly.GetName().Version}\nOS: {Environment.OSVersion.VersionString}\n{Health}\nBattery: Unavailable\nLast message: {Message.Replace(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "<user>")}\nHIL: NOT RUN";
    void Changed([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new(name));
    void UpdateCommands() { Changed(nameof(CanApply)); foreach (var cmd in commands) cmd.Refresh(); if (Cancel is CancelCommand cancel) cancel.Refresh(); }
    public async ValueTask DisposeAsync() { closing = true; lifetime.Cancel(); operation?.Cancel(); if (polling is not null) await polling; while (Busy) await Task.Delay(10); await client.DisposeAsync(); lifetime.Dispose(); }
    sealed class CancelCommand(Action execute, Func<bool> enabled) : ICommand { public event EventHandler? CanExecuteChanged; public bool CanExecute(object? p) => enabled(); public void Execute(object? p) { if (enabled()) execute(); } public void Refresh() => CanExecuteChanged?.Invoke(this, EventArgs.Empty); }
}
