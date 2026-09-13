using System;
using System.IO;
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

                using (var tray = new SteamVrControlTray(repoRoot))
                {
                    tray.Run();
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
