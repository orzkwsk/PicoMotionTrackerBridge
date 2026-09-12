using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;
using System.Drawing;

namespace PicoOTBridge
{
    public static class SteamVrControlGui
    {
        public static void Run(string repoRoot)
        {
            Application.EnableVisualStyles();
            using (var form = new ControlForm(repoRoot))
            {
                Application.Run(form);
            }
        }

        private sealed class AlignmentState
        {
            public ulong Generation;
            public double X;
            public double Y;
            public double Z;
        }

        private sealed class ControlForm : Form
        {
            private const string MappingName = @"Local\PicoOTBridge_WorldAlignment";
            private const string AlignmentUpdatedEventName = @"Local\PicoOTBridge_WorldAlignmentUpdated";
            private const string OrientationZeroEventName = @"Local\PicoOTBridge_OrientationZero";
            private const string OrientationClearEventName = @"Local\PicoOTBridge_OrientationClear";
            private const string PositionZeroEventName = @"Local\PicoOTBridge_PositionZero";
            private const string PositionClearEventName = @"Local\PicoOTBridge_PositionClear";
            private const uint AlignmentMagic = 0x504F5441u;
            private const uint AlignmentVersion = 1u;
            private const int AlignmentStateSize = 40;

            private readonly string repoRoot;
            private readonly string configDirectory;
            private readonly string configPath;

            private TextBox xBox;
            private TextBox yBox;
            private TextBox zBox;
            private TextBox serialBox;
            private ComboBox controllerCombo;
            private TextBox statusBox;

            public ControlForm(string repoRoot)
            {
                this.repoRoot = repoRoot;
                configDirectory = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                    "PicoMotionTrackerBridge");
                configPath = Path.Combine(configDirectory, "steamvr_alignment.json");

                Text = "PICO OT SteamVR Control";
                ClientSize = new Size(560, 485);
                StartPosition = FormStartPosition.CenterScreen;
                FormBorderStyle = FormBorderStyle.FixedDialog;
                MaximizeBox = false;
                Font = new Font("Segoe UI", 9.0f);

                BuildUi();
                LoadSavedValuesOnStartup();
            }

            private void BuildUi()
            {
                AddLabel("World translation (meters)", 20, 18, 220);
                AddLabel("X", 25, 52, 25);
                AddLabel("Y", 25, 82, 25);
                AddLabel("Z", 25, 112, 25);
                xBox = AddTextBox(55, 49, 150);
                yBox = AddTextBox(55, 79, 150);
                zBox = AddTextBox(55, 109, 150);

                var readButton = AddButton("Read current", 230, 48, 100);
                var applyButton = AddButton("Apply", 340, 48, 90);
                var resetButton = AddButton("Reset", 440, 48, 90);
                var saveButton = AddButton("Save", 230, 88, 100);
                var loadApplyButton = AddButton("Load + Apply", 340, 88, 190);

                AddLabel("Calibration", 20, 160, 120);
                AddLabel("Tracker serial", 25, 194, 90);
                serialBox = AddTextBox(120, 191, 260);
                AddLabel("Controller", 25, 226, 90);
                controllerCombo = new ComboBox();
                controllerCombo.Location = new Point(120, 223);
                controllerCombo.Size = new Size(120, 23);
                controllerCombo.DropDownStyle = ComboBoxStyle.DropDownList;
                controllerCombo.Items.Add("Right");
                controllerCombo.Items.Add("Left");
                controllerCombo.SelectedIndex = 0;
                Controls.Add(controllerCombo);
                var calibrateButton = AddButton("Calibrate", 260, 219, 120);

                AddLabel("Diagnostics", 20, 275, 120);
                var orientationZeroButton = AddButton("Orientation Zero", 25, 307, 120);
                var orientationClearButton = AddButton("Orientation Clear", 155, 307, 120);
                var positionZeroButton = AddButton("Position Zero", 285, 307, 120);
                var positionClearButton = AddButton("Position Clear", 415, 307, 120);

                AddLabel("Status", 20, 355, 100);
                statusBox = new TextBox();
                statusBox.Location = new Point(20, 380);
                statusBox.Size = new Size(515, 80);
                statusBox.Multiline = true;
                statusBox.ReadOnly = true;
                statusBox.ScrollBars = ScrollBars.Vertical;
                Controls.Add(statusBox);

                readButton.Click += delegate { RunUiAction("Read failed", ReadCurrent); };
                applyButton.Click += delegate { RunUiAction("Apply failed", ApplyFields); };
                resetButton.Click += delegate { RunUiAction("Reset failed", ResetTranslation); };
                saveButton.Click += delegate { RunUiAction("Save failed", SaveFields); };
                loadApplyButton.Click += delegate { RunUiAction("Load failed", LoadAndApply); };
                calibrateButton.Click += delegate { RunUiAction("Calibration failed", Calibrate); };
                orientationZeroButton.Click += delegate { RunUiAction("Control failed", delegate { SendControlEvent(OrientationZeroEventName); SetStatus("Orientation Zero requested."); }); };
                orientationClearButton.Click += delegate { RunUiAction("Control failed", delegate { SendControlEvent(OrientationClearEventName); SetStatus("Orientation Zero cleared."); }); };
                positionZeroButton.Click += delegate { RunUiAction("Control failed", delegate { SendControlEvent(PositionZeroEventName); SetStatus("Position Zero requested."); }); };
                positionClearButton.Click += delegate { RunUiAction("Control failed", delegate { SendControlEvent(PositionClearEventName); SetStatus("Position Zero cleared."); }); };
            }

            private Label AddLabel(string text, int x, int y, int width)
            {
                var control = new Label();
                control.Text = text;
                control.Location = new Point(x, y);
                control.Size = new Size(width, 22);
                Controls.Add(control);
                return control;
            }

            private TextBox AddTextBox(int x, int y, int width)
            {
                var control = new TextBox();
                control.Location = new Point(x, y);
                control.Size = new Size(width, 23);
                Controls.Add(control);
                return control;
            }

            private Button AddButton(string text, int x, int y, int width)
            {
                var control = new Button();
                control.Text = text;
                control.Location = new Point(x, y);
                control.Size = new Size(width, 30);
                Controls.Add(control);
                return control;
            }

            private void RunUiAction(string title, Action action)
            {
                try
                {
                    action();
                }
                catch (Exception ex)
                {
                    SetStatus(ex.Message);
                    MessageBox.Show(this, ex.Message, title, MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }

            private static string FormatNumber(double value)
            {
                return value.ToString("0.000000", CultureInfo.InvariantCulture);
            }

            private static double ParseNumber(string text, string name)
            {
                double value;
                if (!double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value))
                {
                    throw new InvalidOperationException(name + " must be a number using '.' as the decimal separator.");
                }
                return value;
            }

            private AlignmentState ReadAlignmentState()
            {
                try
                {
                    using (var mapping = MemoryMappedFile.OpenExisting(MappingName, MemoryMappedFileRights.ReadWrite))
                    using (var view = mapping.CreateViewAccessor(0, AlignmentStateSize, MemoryMappedFileAccess.ReadWrite))
                    {
                        uint magic = view.ReadUInt32(0);
                        uint version = view.ReadUInt32(4);
                        if (magic != AlignmentMagic || version != AlignmentVersion)
                        {
                            throw new InvalidOperationException("SteamVR world-alignment shared-state ABI mismatch.");
                        }

                        return new AlignmentState
                        {
                            Generation = view.ReadUInt64(8),
                            X = view.ReadDouble(16),
                            Y = view.ReadDouble(24),
                            Z = view.ReadDouble(32)
                        };
                    }
                }
                catch (FileNotFoundException)
                {
                    throw new InvalidOperationException("SteamVR alignment IPC was not found. Start SteamVR with the PICO OT bridge driver first.");
                }
            }

            private void WriteAlignmentTranslation(double x, double y, double z)
            {
                try
                {
                    using (var mapping = MemoryMappedFile.OpenExisting(MappingName, MemoryMappedFileRights.ReadWrite))
                    using (var view = mapping.CreateViewAccessor(0, AlignmentStateSize, MemoryMappedFileAccess.ReadWrite))
                    {
                        uint magic = view.ReadUInt32(0);
                        uint version = view.ReadUInt32(4);
                        if (magic != AlignmentMagic || version != AlignmentVersion)
                        {
                            throw new InvalidOperationException("SteamVR world-alignment shared-state ABI mismatch.");
                        }

                        ulong generation = view.ReadUInt64(8);
                        view.Write(16, x);
                        view.Write(24, y);
                        view.Write(32, z);
                        view.Write(8, generation + 1UL);
                        view.Flush();
                    }

                    using (var updatedEvent = EventWaitHandle.OpenExisting(AlignmentUpdatedEventName))
                    {
                        if (!updatedEvent.Set())
                        {
                            throw new InvalidOperationException("Failed to signal the SteamVR alignment update event.");
                        }
                    }
                }
                catch (FileNotFoundException)
                {
                    throw new InvalidOperationException("SteamVR alignment IPC was not found. Start SteamVR with the PICO OT bridge driver first.");
                }
                catch (WaitHandleCannotBeOpenedException)
                {
                    throw new InvalidOperationException("SteamVR alignment update event was not found. Start SteamVR with the PICO OT bridge driver first.");
                }
            }

            private static void SendControlEvent(string name)
            {
                try
                {
                    using (var evt = EventWaitHandle.OpenExisting(name))
                    {
                        if (!evt.Set())
                        {
                            throw new InvalidOperationException("Failed to signal control event: " + name);
                        }
                    }
                }
                catch (WaitHandleCannotBeOpenedException)
                {
                    throw new InvalidOperationException("SteamVR control event was not found. Start SteamVR with the PICO OT bridge driver first.");
                }
            }

            private void ReadCurrent()
            {
                AlignmentState state = ReadAlignmentState();
                PutStateInFields(state);
                SetStatus("Read current translation. generation=" + state.Generation.ToString(CultureInfo.InvariantCulture));
            }

            private void ApplyFields()
            {
                double x = ParseNumber(xBox.Text, "X");
                double y = ParseNumber(yBox.Text, "Y");
                double z = ParseNumber(zBox.Text, "Z");
                WriteAlignmentTranslation(x, y, z);
                SetStatus("Applied translation: X=" + FormatNumber(x) + ", Y=" + FormatNumber(y) + ", Z=" + FormatNumber(z) + " m");
            }

            private void ResetTranslation()
            {
                WriteAlignmentTranslation(0.0, 0.0, 0.0);
                PutStateInFields(new AlignmentState { X = 0.0, Y = 0.0, Z = 0.0 });
                SetStatus("World translation reset to zero.");
            }

            private void SaveFields()
            {
                double x = ParseNumber(xBox.Text, "X");
                double y = ParseNumber(yBox.Text, "Y");
                double z = ParseNumber(zBox.Text, "Z");
                SaveAlignmentConfig(x, y, z);
                SetStatus("Saved translation to " + configPath);
            }

            private void LoadAndApply()
            {
                AlignmentState state = LoadAlignmentConfig();
                if (state == null)
                {
                    throw new InvalidOperationException("No saved alignment exists at " + configPath);
                }
                PutStateInFields(state);
                WriteAlignmentTranslation(state.X, state.Y, state.Z);
                SetStatus("Loaded and applied saved translation from " + configPath);
            }

            private void Calibrate()
            {
                string serial = serialBox.Text.Trim();
                if (serial.Length == 0)
                {
                    throw new InvalidOperationException("Enter the PICO tracker serial to use as the calibration reference.");
                }

                string calibrator = FindCalibrator();
                if (calibrator == null)
                {
                    throw new InvalidOperationException("pico_ot_steamvr_calibrator.exe was not found. Run scripts\\build_steamvr_driver.ps1 first.");
                }

                SendControlEvent(PositionClearEventName);
                Thread.Sleep(100);

                string role = controllerCombo.SelectedItem.ToString().ToLowerInvariant();
                var psi = new ProcessStartInfo();
                psi.FileName = calibrator;
                psi.Arguments = "--tracker \"" + serial.Replace("\"", "") + "\" --controller " + role;
                psi.UseShellExecute = false;
                psi.RedirectStandardOutput = true;
                psi.RedirectStandardError = true;
                psi.CreateNoWindow = true;

                SetStatus("Calibrating... keep the tracker and controller reference points together and still.");
                Refresh();

                string stdout;
                string stderr;
                int exitCode;
                using (var process = Process.Start(psi))
                {
                    stdout = process.StandardOutput.ReadToEnd();
                    stderr = process.StandardError.ReadToEnd();
                    process.WaitForExit();
                    exitCode = process.ExitCode;
                }

                if (exitCode != 0)
                {
                    throw new InvalidOperationException("Calibration failed (exit " + exitCode.ToString(CultureInfo.InvariantCulture) + ").\r\n" + stdout + "\r\n" + stderr);
                }

                AlignmentState state = ReadAlignmentState();
                PutStateInFields(state);
                SaveAlignmentConfig(state.X, state.Y, state.Z);
                SetStatus(stdout.Trim() + "\r\nSaved resulting translation.");
            }

            private string FindCalibrator()
            {
                string buildRoot = Path.Combine(repoRoot, "build-steamvr");
                if (!Directory.Exists(buildRoot))
                {
                    return null;
                }
                string[] files = Directory.GetFiles(buildRoot, "pico_ot_steamvr_calibrator.exe", SearchOption.AllDirectories);
                return files.Length > 0 ? files[0] : null;
            }

            private void PutStateInFields(AlignmentState state)
            {
                xBox.Text = FormatNumber(state.X);
                yBox.Text = FormatNumber(state.Y);
                zBox.Text = FormatNumber(state.Z);
            }

            private void SetStatus(string text)
            {
                statusBox.Text = text;
            }

            private void SaveAlignmentConfig(double x, double y, double z)
            {
                Directory.CreateDirectory(configDirectory);
                string json = "{\r\n" +
                    "  \"version\": 1,\r\n" +
                    "  \"xMeters\": " + x.ToString("R", CultureInfo.InvariantCulture) + ",\r\n" +
                    "  \"yMeters\": " + y.ToString("R", CultureInfo.InvariantCulture) + ",\r\n" +
                    "  \"zMeters\": " + z.ToString("R", CultureInfo.InvariantCulture) + "\r\n" +
                    "}\r\n";
                File.WriteAllText(configPath, json, new UTF8Encoding(false));
            }

            private AlignmentState LoadAlignmentConfig()
            {
                if (!File.Exists(configPath))
                {
                    return null;
                }

                string json = File.ReadAllText(configPath, Encoding.UTF8);
                Match versionMatch = Regex.Match(json, "\\\"version\\\"\\s*:\\s*(\\d+)", RegexOptions.CultureInvariant);
                Match xMatch = Regex.Match(json, "\\\"xMeters\\\"\\s*:\\s*([-+0-9.eE]+)", RegexOptions.CultureInvariant);
                Match yMatch = Regex.Match(json, "\\\"yMeters\\\"\\s*:\\s*([-+0-9.eE]+)", RegexOptions.CultureInvariant);
                Match zMatch = Regex.Match(json, "\\\"zMeters\\\"\\s*:\\s*([-+0-9.eE]+)", RegexOptions.CultureInvariant);
                if (!versionMatch.Success || !xMatch.Success || !yMatch.Success || !zMatch.Success)
                {
                    throw new InvalidOperationException("Saved alignment file is malformed: " + configPath);
                }
                if (versionMatch.Groups[1].Value != "1")
                {
                    throw new InvalidOperationException("Unsupported saved alignment version: " + versionMatch.Groups[1].Value);
                }

                return new AlignmentState
                {
                    X = double.Parse(xMatch.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture),
                    Y = double.Parse(yMatch.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture),
                    Z = double.Parse(zMatch.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture)
                };
            }

            private void LoadSavedValuesOnStartup()
            {
                try
                {
                    AlignmentState saved = LoadAlignmentConfig();
                    if (saved != null)
                    {
                        PutStateInFields(saved);
                        SetStatus("Loaded saved values into the fields. Click 'Load + Apply' to use them.");
                    }
                    else
                    {
                        PutStateInFields(new AlignmentState { X = 0.0, Y = 0.0, Z = 0.0 });
                        SetStatus("No saved alignment yet.");
                    }
                }
                catch (Exception ex)
                {
                    PutStateInFields(new AlignmentState { X = 0.0, Y = 0.0, Z = 0.0 });
                    SetStatus("Saved alignment could not be read: " + ex.Message);
                }
            }
        }
    }
}
