using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Text;
using System.Threading;
using Forms = System.Windows.Forms;

namespace PicoOTBridge
{
    internal sealed class SteamVrControlTray : IDisposable
    {
        private const string OutputRouteUpdatedEventName = @"Local\PicoOTBridge_OutputRouteUpdated";
        private const string ServiceStatusMappingName = @"Local\PicoOTBridge_ServiceStatus";
        private const uint ServiceStatusMagic = 0x53544250u;
        private const uint ServiceStatusVersion = 1u;
        private const long ServiceStatusSize = 40;

        private const string SteamVrValue = "steamvr";
        private const string MonakaValue = "monaka";
        private const string BothValue = "both";
        private const string DisabledValue = "disabled";

        private readonly string repoRoot;
        private readonly string configDirectory;
        private readonly string routePath;
        private readonly Forms.ApplicationContext context;
        private readonly Forms.NotifyIcon notifyIcon;
        private readonly Forms.Timer iconRefreshTimer;
        private Icon trayIcon;
        private bool lightTaskbar;
        private Size trayIconSize;
        private readonly Forms.ToolStripMenuItem statusItem;
        private readonly Forms.ToolStripMenuItem steamVrItem;
        private readonly Forms.ToolStripMenuItem monakaItem;
        private readonly Forms.ToolStripMenuItem bothItem;
        private readonly Forms.ToolStripMenuItem disabledItem;
        private Process controlProcess;
        private readonly PcServiceLifecycle.Control serviceControl;
        private readonly Forms.Control dispatcher = new Forms.Control();
        private readonly Forms.ToolStripMenuItem exitItem;
        private readonly EventWaitHandle openControlRequest;
        private readonly bool showWindow;
        private bool exitPending;
        private string lifecycleIssue;
        private bool disposed;

        private enum OutputRoute
        {
            SteamVr = 0,
            Monaka = 1,
            Both = 2,
            Disabled = 3
        }

        public SteamVrControlTray(string repoRoot, string serviceExecutable, bool openWindow)
        {
            this.repoRoot = repoRoot;
            showWindow=openWindow;
            dispatcher.CreateControl();
            serviceControl=new PcServiceLifecycle.Control(@"Local\PicoOTBridge_ServiceLifecycle",serviceExecutable,
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"PicoMotionTrackerBridge","service-logs"));
            openControlRequest=new EventWaitHandle(false,EventResetMode.AutoReset,@"Local\PicoOTBridge_OpenControl");
            configDirectory = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "PicoMotionTrackerBridge");
            routePath = Path.Combine(configDirectory, "output_route.txt");

            context = new Forms.ApplicationContext();
            var menu = new Forms.ContextMenuStrip();

            statusItem = new Forms.ToolStripMenuItem { Enabled = false };
            menu.Items.Add(statusItem);
            menu.Items.Add(new Forms.ToolStripSeparator());

            var routeMenu = new Forms.ToolStripMenuItem("Output");
            steamVrItem = new Forms.ToolStripMenuItem("SteamVR");
            monakaItem = new Forms.ToolStripMenuItem("MonakaVR");
            bothItem = new Forms.ToolStripMenuItem("Both");
            disabledItem = new Forms.ToolStripMenuItem("Disabled");
            steamVrItem.Click += delegate { SetRoute(OutputRoute.SteamVr); };
            monakaItem.Click += delegate { SetRoute(OutputRoute.Monaka); };
            bothItem.Click += delegate { SetRoute(OutputRoute.Both); };
            disabledItem.Click += delegate { SetRoute(OutputRoute.Disabled); };
            routeMenu.DropDownItems.Add(steamVrItem);
            routeMenu.DropDownItems.Add(monakaItem);
            routeMenu.DropDownItems.Add(bothItem);
            routeMenu.DropDownItems.Add(new Forms.ToolStripSeparator());
            routeMenu.DropDownItems.Add(disabledItem);
            menu.Items.Add(routeMenu);

            menu.Items.Add(new Forms.ToolStripSeparator());
            menu.Items.Add("Open SteamVR Control...", null, delegate { OpenControlWindow(); });
            menu.Items.Add(new Forms.ToolStripSeparator());
            exitItem=new Forms.ToolStripMenuItem("Exit");
            exitItem.Click+=delegate { Exit(); };
            menu.Items.Add(exitItem);

            lightTaskbar = AppIcon.IsLightTaskbar();
            trayIconSize = Forms.SystemInformation.SmallIconSize;
            trayIcon = AppIcon.LoadTrayIcon(lightTaskbar, trayIconSize);
            notifyIcon = new Forms.NotifyIcon
            {
                Icon = trayIcon,
                Text = "PICO Motion Tracker Bridge",
                ContextMenuStrip = menu,
                Visible = true
            };
            notifyIcon.DoubleClick += delegate { OpenControlWindow(); };
            menu.Opening += delegate { RefreshRouteUi(); };

            // Run on the existing UI thread; theme changes need no service restart.
            iconRefreshTimer = new Forms.Timer { Interval = 2000 };
            iconRefreshTimer.Tick += delegate {
                RefreshTrayIcon();RefreshRouteUi();
                if(openControlRequest.WaitOne(0))OpenControlWindow();
            };
            iconRefreshTimer.Start();

            RefreshRouteUi();
        }

        private void RefreshTrayIcon()
        {
            bool currentLightTaskbar = AppIcon.IsLightTaskbar();
            Size currentSize = Forms.SystemInformation.SmallIconSize;
            if (currentLightTaskbar == lightTaskbar && currentSize == trayIconSize)
            {
                return;
            }

            Icon nextIcon = AppIcon.LoadTrayIcon(currentLightTaskbar, currentSize);
            Icon previousIcon = trayIcon;
            notifyIcon.Icon = nextIcon;
            trayIcon = nextIcon;
            lightTaskbar = currentLightTaskbar;
            trayIconSize = currentSize;
            previousIcon.Dispose();
        }

        public void Run()
        {
            ThreadPool.QueueUserWorkItem(delegate {
                string error=null;
                try {serviceControl.EnsureRunning("");}catch(Exception e){error=e.Message;}
                try {dispatcher.BeginInvoke((Action)delegate {
                    lifecycleIssue=error;RefreshRouteUi();
                    if(error!=null)notifyIcon.ShowBalloonTip(5000,"PICO Receiver",error,Forms.ToolTipIcon.Error);
                    else if(showWindow)OpenControlWindow();
                });}catch(InvalidOperationException){}
            });
            Forms.Application.Run(context);
        }

        private OutputRoute LoadRequestedRoute()
        {
            try
            {
                if (!File.Exists(routePath))
                {
                    return OutputRoute.SteamVr;
                }

                string value = File.ReadAllText(routePath, Encoding.UTF8).Trim();
                if (string.Equals(value, MonakaValue, StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(value, "monaka-external", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(value, "external", StringComparison.OrdinalIgnoreCase))
                {
                    return OutputRoute.Monaka;
                }
                if (string.Equals(value, BothValue, StringComparison.OrdinalIgnoreCase))
                {
                    return OutputRoute.Both;
                }
                if (string.Equals(value, DisabledValue, StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(value, "off", StringComparison.OrdinalIgnoreCase))
                {
                    return OutputRoute.Disabled;
                }
            }
            catch
            {
            }

            return OutputRoute.SteamVr;
        }

        private void SaveRoute(OutputRoute route)
        {
            Directory.CreateDirectory(configDirectory);
            string value;
            switch (route)
            {
                case OutputRoute.Monaka:
                    value = MonakaValue;
                    break;
                case OutputRoute.Both:
                    value = BothValue;
                    break;
                case OutputRoute.Disabled:
                    value = DisabledValue;
                    break;
                default:
                    value = SteamVrValue;
                    break;
            }
            File.WriteAllText(routePath, value + Environment.NewLine, new UTF8Encoding(false));
        }

        private void SetRoute(OutputRoute route)
        {
            try
            {
                SaveRoute(route);
                bool signaled = SignalServiceRouteUpdate();
                RefreshRouteUi();

                string detail = signaled
                    ? RouteName(route) + " requested."
                    : RouteName(route) + " saved; it will be applied when the PC Bridge Service starts.";
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
            switch (route)
            {
                case OutputRoute.Monaka:
                    return "MonakaVR";
                case OutputRoute.Both:
                    return "Both";
                case OutputRoute.Disabled:
                    return "Disabled";
                default:
                    return "SteamVR";
            }
        }

        private bool SignalServiceRouteUpdate()
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

        private bool TryReadServiceStatus(out OutputRoute route, out uint trackerCount)
        {
            route = OutputRoute.SteamVr;
            trackerCount = 0;
            try
            {
                using (var mapping = MemoryMappedFile.OpenExisting(
                    ServiceStatusMappingName,
                    MemoryMappedFileRights.Read))
                using (var view = mapping.CreateViewAccessor(
                    0,
                    ServiceStatusSize,
                    MemoryMappedFileAccess.Read))
                {
                    uint magic = view.ReadUInt32(0);
                    uint version = view.ReadUInt32(4);
                    if (magic != ServiceStatusMagic || version != ServiceStatusVersion)
                    {
                        return false;
                    }

                    uint routeRaw = view.ReadUInt32(16);
                    if (routeRaw > (uint)OutputRoute.Disabled)
                    {
                        return false;
                    }
                    route = (OutputRoute)routeRaw;
                    trackerCount = view.ReadUInt32(20);
                    return true;
                }
            }
            catch (FileNotFoundException)
            {
                return false;
            }
        }

        private void RefreshRouteUi()
        {
            if(exitPending) {statusItem.Text="Stopping PICO Receiver; tray retained until exit confirmed";return;}
            if(lifecycleIssue!=null) {statusItem.Text="Receiver control: "+lifecycleIssue;return;}
            OutputRoute requested = LoadRequestedRoute();
            steamVrItem.Checked = requested == OutputRoute.SteamVr;
            monakaItem.Checked = requested == OutputRoute.Monaka;
            bothItem.Checked = requested == OutputRoute.Both;
            disabledItem.Checked = requested == OutputRoute.Disabled;

            OutputRoute applied;
            uint trackerCount;
            if (TryReadServiceStatus(out applied, out trackerCount))
            {
                if (applied == requested)
                {
                    statusItem.Text = "Output: " + RouteName(applied) +
                                      " | " + trackerCount.ToString() + " tracker(s)";
                }
                else
                {
                    statusItem.Text = "Requested: " + RouteName(requested) +
                                      " | Applied: " + RouteName(applied);
                }
            }
            else
            {
                statusItem.Text = "Bridge Service: offline | requested: " + RouteName(requested);
            }
        }

        private void OpenControlWindow()
        {
            if(exitPending)return;
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
            if(exitPending)return;
            exitPending=true;exitItem.Enabled=false;RefreshRouteUi();
            ThreadPool.QueueUserWorkItem(delegate {
                string error;
                new PcServiceLifecycle.ExitGate().TryExit(
                    delegate {
                        if(controlProcess!=null&&!controlProcess.HasExited) {
                            controlProcess.CloseMainWindow();
                            if(!controlProcess.WaitForExit(1000))throw new IOException("Close Bridge Control first; receiver and tray retained");
                        }
                        serviceControl.StopAndWait(5000);
                    },
                    delegate {dispatcher.BeginInvoke((Action)delegate {context.ExitThread();});},out error);
                if(error!=null)try{dispatcher.BeginInvoke((Action)delegate{ExitFailed(error);});}catch(InvalidOperationException){}
            });
        }
        private void ExitFailed(string error) {
            exitPending=false;exitItem.Enabled=true;lifecycleIssue=error;RefreshRouteUi();
            notifyIcon.ShowBalloonTip(5000,"PICO Receiver stop failed",error,Forms.ToolTipIcon.Error);
        }

        public void Dispose()
        {
            if (disposed)
            {
                return;
            }

            disposed = true;
            iconRefreshTimer.Stop();
            iconRefreshTimer.Dispose();
            openControlRequest.Dispose();
            serviceControl.Dispose();
            dispatcher.Dispose();
            notifyIcon.Visible = false;
            notifyIcon.Dispose();
            trayIcon.Dispose();
            context.Dispose();
            if (controlProcess != null)
            {
                controlProcess.Dispose();
                controlProcess = null;
            }
        }
    }
}
