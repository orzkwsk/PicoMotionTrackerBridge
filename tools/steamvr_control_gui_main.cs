using System;
using System.IO;
using System.Windows.Forms;

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
                SteamVrControlGui.Run(repoRoot);
                return 0;
            }
            catch (Exception ex)
            {
                MessageBox.Show(
                    ex.ToString(),
                    "PICO OT SteamVR Control",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
                return 1;
            }
        }

        private static string ResolveRepoRoot(string[] args)
        {
            if (args != null && args.Length > 0 && !string.IsNullOrWhiteSpace(args[0]))
            {
                return Path.GetFullPath(args[0]);
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
            if (File.Exists(Path.Combine(currentDirectory, "tools", "steamvr_control_gui.cs")))
            {
                return currentDirectory;
            }

            throw new InvalidOperationException(
                "Repository root could not be determined. Launch through scripts\\steamvr_control_gui.ps1 or pass the repository path as the first argument.");
        }
    }
}
