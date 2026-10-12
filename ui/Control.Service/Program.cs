using System.Diagnostics;
using System.IO.Pipes;
using System.Text.Json;
using PicoOTBridge.Control;

namespace PicoOTBridge.ControlService;

internal static class Program
{
    static async Task<int> Main(string[] args)
    {
        string root = args.Length > 0 ? Path.GetFullPath(args[0]) : Environment.CurrentDirectory;
        using var instance = new Mutex(true, @"Local\PicoOTBridge.Control.Service.v1", out var created); if (!created) return 0;
        using var lifetime = new CancellationTokenSource(); Console.CancelKeyPress += (_, e) => { e.Cancel = true; lifetime.Cancel(); };
        await using var owner = new ControlOwner(root);
        var status = owner.PollAsync(lifetime.Token);
        var clients = new HashSet<Task>();
        try
        {
            while (!lifetime.IsCancellationRequested)
            {
                // Fixed concurrency and short framing deadlines bound slow clients.
                if (clients.Count >= 8) { var done = await Task.WhenAny(clients); clients.Remove(done); await done; }
                var pipe = new NamedPipeServerStream(PipeControlClient.SessionPipeName, PipeDirection.InOut, 8, PipeTransmissionMode.Byte, PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly, 4096, 16384);
                try { await pipe.WaitForConnectionAsync(lifetime.Token); } catch { pipe.Dispose(); throw; }
                var client = ServeAsync(pipe, owner, lifetime.Token); clients.Add(client); clients.RemoveWhere(t => t.IsCompletedSuccessfully);
            }
        }
        catch (OperationCanceledException) { }
        finally { lifetime.Cancel(); await Task.WhenAll(clients); await status; }
        return 0;
    }
    internal static async Task ServeAsync(NamedPipeServerStream pipe, ControlOwner owner, CancellationToken lifetime)
    {
        using (pipe) using (var deadline = CancellationTokenSource.CreateLinkedTokenSource(lifetime))
        {
            deadline.CancelAfter(TimeSpan.FromSeconds(5)); string id = "";
            try
            {
                string frame = await ControlJson.ReadFrameAsync(pipe, 4096, deadline.Token); ControlJson.ValidateObject(frame);
                var request = JsonSerializer.Deserialize<ControlRequest>(frame, ControlJson.Options) ?? throw new InvalidDataException("Invalid request."); id = request.Id;
                if (request.Version != 1 || string.IsNullOrEmpty(request.Id) || request.Id.Length > 64 || string.IsNullOrEmpty(request.Operation)) throw new InvalidDataException("Unsupported request/version.");
                deadline.CancelAfter(TimeSpan.FromSeconds(30));
                var response = await owner.ExecuteAsync(request, deadline.Token);
                var bytes = System.Text.Encoding.UTF8.GetBytes(JsonSerializer.Serialize(response, ControlJson.Options) + "\n");
                await pipe.WriteAsync(bytes, deadline.Token); await pipe.FlushAsync(deadline.Token);
            }
            catch (Exception error) when (error is not OutOfMemoryException)
            {
                try { using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(1)); var response = new ControlResponse { Id = id, Ok = false, Code = error is InvalidDataException or JsonException ? "InvalidRequest" : error is OperationCanceledException ? "Cancelled" : "OperationFailed", Message = error is JsonException ? "Invalid control JSON." : error.Message }; var bytes = System.Text.Encoding.UTF8.GetBytes(JsonSerializer.Serialize(response, ControlJson.Options) + "\n"); await pipe.WriteAsync(bytes, timeout.Token); } catch (Exception) { }
            }
        }
    }
}
