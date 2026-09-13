using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Threading;
using Forms = System.Windows.Forms;

namespace PicoOTBridge
{
    internal sealed class SteamVrControlTray : IDisposable
    {
        private const string OutputRouteUpdatedEventName = @"Local\PicoOTBridge_OutputRouteUpdated";
        private const string SteamVrDirectValue = "steamvr-direct";
        private const string ExternalValue = "monaka-external";

        private readonly string repoRoot;
        private readonly string configDirectory;
        private readonly string routePath;
        private readonly Forms.ApplicationContext context;
        private readonly Forms.NotifyIcon notifyIcon;
        private readonly Forms.ToolStripMenuItem statusItem;
        private readonly Forms.ToolStripMenuItem directItem;
        private readonly Forms.ToolStripMenuItem externalItem;
        private Process controlProcess;
        private bool disposed;

        private enum OutputRoute
        {
            SteamVrDirect,
            External
        }

        public SteamVrControlTray(string repoRoot)
        {
            this.repoRoot = repoRoot;
            configDirectory = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "PicoMotionTrackerBridge");
            routePath = Path.Combine(configDirectory, "output_route.txt");

            context = new Forms.ApplicationContext();
            var menu = new Forms.ContextMenuStrip();

            statusItem = new Forms.ToolStripMenuItem { Enabled = false };
            menu.Items.Add(statusItem);
            menu.Items.Add(new Forms.ToolStripSeparator());

            var routeMenu = new Forms.ToolStripMenuItem("Output route");
            directItem = new Forms.ToolStripMenuItem("SteamVR Direct");
            externalItem = new Forms.ToolStripMenuItem("MonakaVR / External");
            directItem.Click += delegate { SetRoute(OutputRoute.SteamVrDirect); };
            externalItem.Click += delegate { SetRoute(OutputRoute.External); };
            routeMenu.DropDownItems.Add(directItem);
            routeMenu.DropDownItems.Add(externalItem);
            menu.Items.Add(routeMenu);

            menu.Items.Add(new Forms.ToolStripSeparator());
            menu.Items.Add("Open SteamVR Control...", null, delegate { OpenControlWindow(); });
            menu.Items.Add(new Forms.ToolStripSeparator());
            menu.Items.Add("Exit", null, delegate { Exit(); });

            notifyIcon = new Forms.NotifyIcon
            {
                Icon = SystemIcons.Application,
                Text = "PICO Motion Tracker Bridge",
                ContextMenuStrip = menu,
                Visible = true
            };
            notifyIcon.DoubleClick += delegate { OpenControlWindow(); };
            menu.Opening += delegate { RefreshRouteUi(); };

            RefreshRouteUi();
        }

        public void Run()
        {
            OpenControlWindow();
            Forms.Application.Run(context);
        }

        private OutputRoute LoadRoute()
        {
            try
            {
                if (!File.Exists(routePath))
                {
                    return OutputRoute.SteamVrDirect;
                }

                string value = File.ReadAllText(routePath, Encoding.UTF8).Trim();
                if (string.Equals(value, ExternalValue, StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(value, "external", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(value, "monaka", StringComparison.OrdinalIgnoreCase))
                {
                    return OutputRoute.External;
                }
            }
            catch
            {
                // Preserve the backward-compatible direct route when the persisted
                // preference cannot be read.
            }

            return OutputRoute.SteamVrDirect;
        }

        private void SaveRoute(OutputRoute route)
        {
            Directory.CreateDirectory(configDirectory);
            string value = route == OutputRoute.SteamVrDirect
                ? SteamVrDirectValue
                : ExternalValue;
            File.WriteAllText(routePath, value + Environment.NewLine, new UTF8Encoding(false));
        }

        private void SetRoute(OutputRoute route)
        {
            try
            {
                SaveRoute(route);
                bool signaled = SignalDriverRouteUpdate();
                RefreshRouteUi();

                string routeName = RouteName(route);
                string detail = signaled
                    ? routeName + " requested."
                    : routeName + " saved; it will be used the next time the SteamVR driver starts.";
                notifyIcon.ShowBalloonTip(
                    2500,
                    "PICO Motion Tracker Bridge",
                    detail,
                    Forms.ToolTipIcon.Info);
            }
            catch (Exception ex)
            {
                Forms.MessageBox.Show(
                    ex.Message,
                    "Output route change failed",
                    Forms.MessageBoxButtons.OK,
                    Forms.MessageBoxIcon.Error);
            }
        }

        private static string RouteName(OutputRoute route)
        {
            return route == OutputRoute.SteamVrDirect
                ? "SteamVR Direct"
                : "MonakaVR / External";
        }

        private bool SignalDriverRouteUpdate()
        {
            try
            {
                using (var evt = EventWaitHandle.OpenExisting(OutputRouteUpdatedEventName))
                {
                    return evt.Set();
                }
            }
            catch (WaitHandleCannotBeOpenedException)
            {
                return false;
            }
        }

        private void RefreshRouteUi()
        {
            OutputRoute route = LoadRoute();
            directItem.Checked = route == OutputRoute.SteamVrDirect;
            externalItem.Checked = route == OutputRoute.External;
            statusItem.Text = "Route: " + RouteName(route);
        }

        private void OpenControlWindow()
        {
            try
            {
                if (controlProcess != null && !controlProcess.HasExited)
                {
                    notifyIcon.ShowBalloonTip(
                        1500,
                        "PICO Motion Tracker Bridge",
                        "SteamVR Control is already open.",
                        Forms.ToolTipIcon.Info);
                    return;
                }

                string executable = Process.GetCurrentProcess().MainModule.FileName;
                var psi = new ProcessStartInfo
                {
                    FileName = executable,
                    Arguments = Quote(repoRoot) + " --control-window-only",
                    UseShellExecute = true
                };
                controlProcess = Process.Start(psi);
            }
            catch (Exception ex)
            {
                Forms.MessageBox.Show(
                    ex.Message,
                    "SteamVR Control launch failed",
                    Forms.MessageBoxButtons.OK,
                    Forms.MessageBoxIcon.Error);
            }
        }

        private static string Quote(string value)
        {
            return "\"" + value.Replace("\"", string.Empty) + "\"";
        }

        private void Exit()
        {
            try
            {
                if (controlProcess != null && !controlProcess.HasExited)
                {
                    controlProcess.CloseMainWindow();
                }
            }
            catch
            {
            }

            context.ExitThread();
        }

        public void Dispose()
        {
            if (disposed)
            {
                return;
            }

            disposed = true;
            notifyIcon.Visible = false;
            notifyIcon.Dispose();
            context.Dispose();
            if (controlProcess != null)
            {
                controlProcess.Dispose();
                controlProcess = null;
            }
        }
    }
}
