using System;
using System.IO;
using System.Threading;
using System.Windows;

namespace PicoOTBridge
{
    internal static class SteamVrControlGuiProgram
    {
        [STAThread]
        private static int Main(string[] args)
        {
            try
            {
                string repoRoot = ResolveRepoRoot(args);
                if (HasSwitch(args, "--control-window-only"))
                {
                    SteamVrControlWpf.Run(repoRoot);
                    return 0;
                }

                bool created;
                using (var single = new Mutex(true,@"Local\PicoOTBridge_ServiceTray",out created))
                {
                    bool owned=created;
                    if(!owned)try{owned=single.WaitOne(0);}catch(AbandonedMutexException){owned=true;}
                    if(!owned) {
                        PcServiceLifecycle.Control.VerifyExistingTray(@"Local\PicoOTBridge_ServiceLifecycle",Option(args,"--service-exe"),System.Diagnostics.Process.GetCurrentProcess().MainModule.FileName);
                        if(!HasSwitch(args,"--tray-only"))using(var request=EventWaitHandle.OpenExisting(@"Local\PicoOTBridge_OpenControl"))request.Set();
                        return 0;
                    }
                    try {
                        using (var tray = new SteamVrControlTray(repoRoot, Option(args,"--service-exe"), !HasSwitch(args,"--tray-only")))
                            tray.Run();
                    } finally {single.ReleaseMutex();}
                }
                return 0;
            }
            catch (Exception ex)
            {
                MessageBox.Show(
                    ex.ToString(),
                    "PICO OT SteamVR Control",
                    MessageBoxButton.OK,
                    MessageBoxImage.Error);
                return 1;
            }
        }

        private static string Option(string[] args,string name) {
            for(int i=0;i+1<args.Length;++i)if(string.Equals(args[i],name,StringComparison.OrdinalIgnoreCase))return Path.GetFullPath(args[i+1]);
            throw new ArgumentException("Missing "+name+"; launch through the reviewed startup script");
        }

        private static bool HasSwitch(string[] args, string value)
        {
            if (args == null)
            {
                return false;
            }

            for (int i = 0; i < args.Length; ++i)
            {
                if (string.Equals(args[i], value, StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
            }
            return false;
        }

        private static string ResolveRepoRoot(string[] args)
        {
            if (args != null)
            {
                for (int i = 0; i < args.Length; ++i)
                {
                    string arg = args[i];
                    if (!string.IsNullOrWhiteSpace(arg) && !arg.StartsWith("--", StringComparison.Ordinal))
                    {
                        return Path.GetFullPath(arg);
                    }
                }
            }

            string baseDirectory = AppDomain.CurrentDomain.BaseDirectory;
            DirectoryInfo buildDirectory = Directory.GetParent(
                baseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            if (buildDirectory != null &&
                string.Equals(buildDirectory.Name, "build-gui", StringComparison.OrdinalIgnoreCase) &&
                buildDirectory.Parent != null)
            {
                return buildDirectory.Parent.FullName;
            }

            string currentDirectory = Environment.CurrentDirectory;
            if (File.Exists(Path.Combine(currentDirectory, "tools", "steamvr_control_wpf.cs")))
            {
                return currentDirectory;
            }

            throw new InvalidOperationException(
                "Repository root could not be determined. Launch through scripts\\steamvr_control_gui.ps1 or pass the repository path as the first argument.");
        }
    }
}
