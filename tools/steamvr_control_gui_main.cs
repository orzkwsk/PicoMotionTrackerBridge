using System;
using System.Windows;
namespace PicoOTBridge
{
    internal static class SteamVrControlGuiProgram
    {
        [STAThread]
        static int Main(string[] args)
        {
            try { using var mutex = new System.Threading.Mutex(true, @"Local\PicoOTBridge.Control.GUI.v1", out var created); if (!created) { MessageBox.Show("PICO Control is already open."); return 0; } SteamVrControlWpf.Run(args.Length > 0 ? args[0] : Environment.CurrentDirectory); return 0; }
            catch (Exception e) { MessageBox.Show(e.Message, "PICO Control startup failed"); return 1; }
        }
    }
}
