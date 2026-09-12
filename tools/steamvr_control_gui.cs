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
            private const double AlignmentCompareEpsilon = 0.0000005;

            private readonly string repoRoot;
            private readonly string configDirectory;
            private readonly string configPath;

            private TextBox xBox;
            private TextBox yBox;
            private TextBox zBox;
            private ComboBox trackerCombo;
            private ComboBox referenceCombo;
            private TextBox statusBox;
            private Label unsavedLabel;
            private AlignmentState savedAlignment;

            public ControlForm(string repoRoot)
            {
                this.repoRoot = repoRoot;
                configDirectory = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                    "PicoMotionTrackerBridge");
                configPath = Path.Combine(configDirectory, "steamvr_alignment.json");

                Text = "PICO OT SteamVR Control";
                ClientSize = new Size(650, 555);
                StartPosition = FormStartPosition.CenterScreen;
                FormBorderStyle = FormBorderStyle.FixedDialog;
                MaximizeBox = false;
                Font = new Font("Segoe UI", 9.0f);

                BuildUi();
                LoadSavedValuesOnStartup();
                TryRefreshTrackersOnStartup();
            }

            private void BuildUi()
            {
                AddLabel("World translation (m)", 20, 18, 220);
                AddLabel("Increment: 1 mm   (Shift + click = 10 mm)", 260, 18, 300);

                AddLabel("X", 25, 54, 25);
                AddLabel("Y", 25, 88, 25);
                AddLabel("Z", 25, 122, 25);
                xBox = AddTextBox(55, 51, 145);
                yBox = AddTextBox(55, 85, 145);
                zBox = AddTextBox(55, 119, 145);
                AddLabel("m", 207, 54, 20);
                AddLabel("m", 207, 88, 20);
                AddLabel("m", 207, 122, 20);

                var xMinusButton = AddButton("-1 mm", 240, 48, 80);
                var xPlusButton = AddButton("+1 mm", 326, 48, 80);
                var yMinusButton = AddButton("-1 mm", 240, 82, 80);
                var yPlusButton = AddButton("+1 mm", 326, 82, 80);
                var zMinusButton = AddButton("-1 mm", 240, 116, 80);
                var zPlusButton = AddButton("+1 mm", 326, 116, 80);

                var readButton = AddButton("Read current", 425, 48, 100);
                var applyButton = AddButton("Apply", 535, 48, 90);
                var resetButton = AddButton("Reset", 425, 84, 95);
                var saveButton = AddButton("Save", 530, 84, 95);
                var loadApplyButton = AddButton("Load + Apply", 425, 120, 200);

                unsavedLabel = AddLabel(
                    "Unsaved changes - current alignment is not saved. Click Save to keep it.",
                    20,
                    154,
                    605);
                unsavedLabel.AutoSize = false;
                unsavedLabel.Visible = false;
                unsavedLabel.Font = new Font(Font, FontStyle.Bold);

                AddLabel("Calibration", 20, 184, 120);
                AddLabel("PICO tracker", 25, 217, 90);
                trackerCombo = new ComboBox();
                trackerCombo.Location = new Point(120, 214);
                trackerCombo.Size = new Size(310, 23);
                trackerCombo.DropDownStyle = ComboBoxStyle.DropDownList;
                Controls.Add(trackerCombo);
                var refreshTrackersButton = AddButton("Refresh", 440, 211, 85);

                AddLabel("Reference", 25, 251, 90);
                referenceCombo = new ComboBox();
                referenceCombo.Location = new Point(120, 248);
                referenceCombo.Size = new Size(205, 23);
                referenceCombo.DropDownStyle = ComboBoxStyle.DropDownList;
                referenceCombo.Items.Add("Right Controller");
                referenceCombo.Items.Add("Left Controller");
                referenceCombo.Items.Add("HMD (optional)");
                referenceCombo.SelectedIndex = 0;
                Controls.Add(referenceCombo);
                var calibrateButton = AddButton("Measure + Apply", 340, 244, 185);

                AddLabel("Diagnostics", 20, 306, 120);
                var orientationZeroButton = AddButton("Orientation Zero", 25, 338, 135);
                var orientationClearButton = AddButton("Orientation Clear", 170, 338, 135);
                var positionZeroButton = AddButton("Position Zero", 315, 338, 135);
                var positionClearButton = AddButton("Position Clear", 460, 338, 135);

                AddLabel("Status", 20, 390, 100);
                statusBox = new TextBox();
                statusBox.Location = new Point(20, 415);
                statusBox.Size = new Size(605, 110);
                statusBox.Multiline = true;
                statusBox.ReadOnly = true;
                statusBox.ScrollBars = ScrollBars.Vertical;
                Controls.Add(statusBox);

                xBox.TextChanged += delegate { UpdateUnsavedIndicator(); };
                yBox.TextChanged += delegate { UpdateUnsavedIndicator(); };
                zBox.TextChanged += delegate { UpdateUnsavedIndicator(); };

                xMinusButton.Click += delegate { RunUiAction("Adjust failed", delegate { NudgeAxis(xBox, "X", -1); }); };
                xPlusButton.Click += delegate { RunUiAction("Adjust failed", delegate { NudgeAxis(xBox, "X", 1); }); };
                yMinusButton.Click += delegate { RunUiAction("Adjust failed", delegate { NudgeAxis(yBox, "Y", -1); }); };
                yPlusButton.Click += delegate { RunUiAction("Adjust failed", delegate { NudgeAxis(yBox, "Y", 1); }); };
                zMinusButton.Click += delegate { RunUiAction("Adjust failed", delegate { NudgeAxis(zBox, "Z", -1); }); };
                zPlusButton.Click += delegate { RunUiAction("Adjust failed", delegate { NudgeAxis(zBox, "Z", 1); }); };

                readButton.Click += delegate { RunUiAction("Read failed", ReadCurrent); };
                applyButton.Click += delegate { RunUiAction("Apply failed", ApplyFields); };
                resetButton.Click += delegate { RunUiAction("Reset failed", ResetTranslation); };
                saveButton.Click += delegate { RunUiAction("Save failed", SaveFields); };
                loadApplyButton.Click += delegate { RunUiAction("Load failed", LoadAndApply); };
                refreshTrackersButton.Click += delegate { RunUiAction("Tracker refresh failed", RefreshTrackers); };
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

            private AlignmentState ReadFieldsState()
            {
                return new AlignmentState
                {
                    X = ParseNumber(xBox.Text, "X"),
                    Y = ParseNumber(yBox.Text, "Y"),
                    Z = ParseNumber(zBox.Text, "Z")
                };
            }

            private static bool NearlyEqual(double a, double b)
            {
                return Math.Abs(a - b) <= AlignmentCompareEpsilon;
            }

            private void UpdateUnsavedIndicator()
            {
                if (unsavedLabel == null)
                {
                    return;
                }

                bool dirty;
                try
                {
                    AlignmentState current = ReadFieldsState();
                    if (savedAlignment == null)
                    {
                        dirty = !NearlyEqual(current.X, 0.0) ||
                                !NearlyEqual(current.Y, 0.0) ||
                                !NearlyEqual(current.Z, 0.0);
                    }
                    else
                    {
                        dirty = !NearlyEqual(current.X, savedAlignment.X) ||
                                !NearlyEqual(current.Y, savedAlignment.Y) ||
                                !NearlyEqual(current.Z, savedAlignment.Z);
                    }
                }
                catch
                {
                    dirty = true;
                }

                unsavedLabel.Visible = dirty;
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

            private void NudgeAxis(TextBox box, string axisName, int direction)
            {
                double stepMeters = (ModifierKeys & Keys.Shift) == Keys.Shift ? 0.010 : 0.001;
                double current = ParseNumber(box.Text, axisName);
                box.Text = FormatNumber(current + direction * stepMeters);
                ApplyFields();
            }

            private void ReadCurrent()
            {
                AlignmentState state = ReadAlignmentState();
                PutStateInFields(state);
                UpdateUnsavedIndicator();
                SetStatus("Read current translation. generation=" + state.Generation.ToString(CultureInfo.InvariantCulture));
            }

            private void ApplyFields()
            {
                AlignmentState current = ReadFieldsState();
                WriteAlignmentTranslation(current.X, current.Y, current.Z);
                UpdateUnsavedIndicator();
                SetStatus("Applied translation: X=" + FormatNumber(current.X) + ", Y=" + FormatNumber(current.Y) + ", Z=" + FormatNumber(current.Z) + " m");
            }

            private void ResetTranslation()
            {
                WriteAlignmentTranslation(0.0, 0.0, 0.0);
                PutStateInFields(new AlignmentState { X = 0.0, Y = 0.0, Z = 0.0 });
                UpdateUnsavedIndicator();
                SetStatus("World translation reset to zero.");
            }

            private void SaveFields()
            {
                AlignmentState current = ReadFieldsState();
                SaveAlignmentConfig(current.X, current.Y, current.Z);
                savedAlignment = new AlignmentState { X = current.X, Y = current.Y, Z = current.Z };
                UpdateUnsavedIndicator();
                SetStatus("Saved translation to " + configPath);
            }

            private void LoadAndApply()
            {
                AlignmentState state = LoadAlignmentConfig();
                if (state == null)
                {
                    throw new InvalidOperationException("No saved alignment exists at " + configPath);
                }
                savedAlignment = new AlignmentState { X = state.X, Y = state.Y, Z = state.Z };
                PutStateInFields(state);
                WriteAlignmentTranslation(state.X, state.Y, state.Z);
                UpdateUnsavedIndicator();
                SetStatus("Loaded and applied saved translation from " + configPath);
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

            private string RunCalibrator(string arguments, out string stderr, out int exitCode)
            {
                string calibrator = FindCalibrator();
                if (calibrator == null)
                {
                    throw new InvalidOperationException("pico_ot_steamvr_calibrator.exe was not found. Run scripts\\build_steamvr_driver.ps1 first.");
                }

                var psi = new ProcessStartInfo();
                psi.FileName = calibrator;
                psi.Arguments = arguments;
                psi.UseShellExecute = false;
                psi.RedirectStandardOutput = true;
                psi.RedirectStandardError = true;
                psi.CreateNoWindow = true;

                string stdout;
                using (var process = Process.Start(psi))
                {
                    stdout = process.StandardOutput.ReadToEnd();
                    stderr = process.StandardError.ReadToEnd();
                    process.WaitForExit();
                    exitCode = process.ExitCode;
                }
                return stdout;
            }

            private void TryRefreshTrackersOnStartup()
            {
                try
                {
                    RefreshTrackers();
                }
                catch (Exception ex)
                {
                    SetStatus("Tracker list not available yet: " + ex.Message);
                }
            }

            private void RefreshTrackers()
            {
                string previous = trackerCombo.SelectedItem as string;
                string stderr;
                int exitCode;
                string stdout = RunCalibrator("--list-trackers", out stderr, out exitCode);
                if (exitCode != 0)
                {
                    throw new InvalidOperationException("Tracker enumeration failed (exit " + exitCode.ToString(CultureInfo.InvariantCulture) + ").\r\n" + stdout + "\r\n" + stderr);
                }

                trackerCombo.Items.Clear();
                string[] lines = stdout.Replace("\r", "").Split(new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries);
                for (int i = 0; i < lines.Length; ++i)
                {
                    string serial = lines[i].Trim();
                    if (serial.Length > 0)
                    {
                        trackerCombo.Items.Add(serial);
                    }
                }

                if (trackerCombo.Items.Count == 0)
                {
                    SetStatus("No PICO bridge trackers were found in SteamVR.");
                    return;
                }

                int previousIndex = previous == null ? -1 : trackerCombo.Items.IndexOf(previous);
                trackerCombo.SelectedIndex = previousIndex >= 0 ? previousIndex : 0;
                SetStatus("Found " + trackerCombo.Items.Count.ToString(CultureInfo.InvariantCulture) + " PICO bridge tracker(s).");
            }

            private string SelectedReferenceArgument()
            {
                if (referenceCombo.SelectedIndex == 1)
                {
                    return "left";
                }
                if (referenceCombo.SelectedIndex == 2)
                {
                    return "hmd";
                }
                return "right";
            }

            private static AlignmentState ParseCalibrationResult(string stdout)
            {
                Match match = Regex.Match(
                    stdout,
                    @"(?m)^CALIBRATION_RESULT\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s*$",
                    RegexOptions.CultureInvariant);
                if (!match.Success)
                {
                    throw new InvalidOperationException("Calibration result was not found in calibrator output.");
                }

                return new AlignmentState
                {
                    X = double.Parse(match.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture),
                    Y = double.Parse(match.Groups[2].Value, NumberStyles.Float, CultureInfo.InvariantCulture),
                    Z = double.Parse(match.Groups[3].Value, NumberStyles.Float, CultureInfo.InvariantCulture)
                };
            }

            private void Calibrate()
            {
                string serial = trackerCombo.SelectedItem as string;
                if (string.IsNullOrWhiteSpace(serial))
                {
                    throw new InvalidOperationException("Select a PICO tracker. Use Refresh if the list is empty.");
                }

                SendControlEvent(PositionClearEventName);
                Thread.Sleep(100);

                string reference = SelectedReferenceArgument();
                SetStatus("Measuring alignment against " + referenceCombo.SelectedItem + "... keep both reference points together and still.");
                Refresh();

                string stderr;
                int exitCode;
                string stdout = RunCalibrator(
                    "--tracker \"" + serial.Replace("\"", "") + "\" --reference " + reference + " --measure-only",
                    out stderr,
                    out exitCode);
                if (exitCode != 0)
                {
                    throw new InvalidOperationException("Calibration measurement failed (exit " + exitCode.ToString(CultureInfo.InvariantCulture) + ").\r\n" + stdout + "\r\n" + stderr);
                }

                AlignmentState suggested = ParseCalibrationResult(stdout);
                PutStateInFields(suggested);
                WriteAlignmentTranslation(suggested.X, suggested.Y, suggested.Z);
                UpdateUnsavedIndicator();
                SetStatus(
                    "Measured alignment was applied to SteamVR and loaded into X/Y/Z. It has NOT been saved.\r\n" +
                    "Verify the result, fine-tune if needed, then click Save to keep it.");
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
                        savedAlignment = new AlignmentState { X = saved.X, Y = saved.Y, Z = saved.Z };
                        PutStateInFields(saved);
                        UpdateUnsavedIndicator();
                        SetStatus("Loaded saved values into the fields. Click 'Load + Apply' to use them.");
                    }
                    else
                    {
                        savedAlignment = null;
                        PutStateInFields(new AlignmentState { X = 0.0, Y = 0.0, Z = 0.0 });
                        UpdateUnsavedIndicator();
                        SetStatus("No saved alignment yet.");
                    }
                }
                catch (Exception ex)
                {
                    savedAlignment = null;
                    PutStateInFields(new AlignmentState { X = 0.0, Y = 0.0, Z = 0.0 });
                    UpdateUnsavedIndicator();
                    SetStatus("Saved alignment could not be read: " + ex.Message);
                }
            }
        }
    }
}
