using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;

namespace PicoOTBridge
{
    public static class SteamVrControlWpf
    {
        public static void Run(string repoRoot)
        {
            var application = new Application();
            var window = new ControlWindow(repoRoot);
            application.Run(window);
        }

        private sealed class AlignmentState
        {
            public ulong Generation;
            public double X;
            public double Y;
            public double Z;
        }

        private sealed class ControlWindow : Window
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
            private TextBlock unsavedText;
            private AlignmentState savedAlignment;

            public ControlWindow(string repoRoot)
            {
                this.repoRoot = repoRoot;
                configDirectory = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                    "PicoMotionTrackerBridge");
                configPath = Path.Combine(configDirectory, "steamvr_alignment.json");

                Title = "OT Tracker Receiver";
                Width = 740;
                Height = 700;
                MinWidth = 700;
                MinHeight = 640;
                WindowStartupLocation = WindowStartupLocation.CenterScreen;

                Content = BuildUi();
                LoadSavedValuesOnStartup();
                TryRefreshTrackersOnStartup();
            }

            private FrameworkElement BuildUi()
            {
                var root = new Grid { Margin = new Thickness(16) };
                root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1.0, GridUnitType.Star) });

                FrameworkElement translation = BuildTranslationSection();
                Grid.SetRow(translation, 0);
                root.Children.Add(translation);

                unsavedText = new TextBlock
                {
                    Text = "Unsaved changes - current alignment is not saved. Click Save to keep it.",
                    FontWeight = FontWeights.SemiBold,
                    Visibility = Visibility.Collapsed,
                    Margin = new Thickness(8, 6, 8, 6)
                };
                Grid.SetRow(unsavedText, 1);
                root.Children.Add(unsavedText);

                FrameworkElement calibration = BuildCalibrationSection();
                Grid.SetRow(calibration, 2);
                root.Children.Add(calibration);

                FrameworkElement diagnostics = BuildDiagnosticsSection();
                Grid.SetRow(diagnostics, 3);
                root.Children.Add(diagnostics);

                FrameworkElement status = BuildStatusSection();
                Grid.SetRow(status, 4);
                root.Children.Add(status);

                xBox.TextChanged += delegate { UpdateUnsavedIndicator(); };
                yBox.TextChanged += delegate { UpdateUnsavedIndicator(); };
                zBox.TextChanged += delegate { UpdateUnsavedIndicator(); };

                return root;
            }

            private FrameworkElement BuildTranslationSection()
            {
                var group = new GroupBox
                {
                    Header = "World Translation",
                    Margin = new Thickness(0, 0, 0, 6),
                    Padding = new Thickness(10)
                };

                var panel = new StackPanel();
                panel.Children.Add(new TextBlock
                {
                    Text = "Values are in meters. Increment buttons move 1 mm; hold Shift for 10 mm.",
                    Margin = new Thickness(0, 0, 0, 8)
                });

                var grid = new Grid();
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(28) });
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(160) });
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(28) });
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(86) });
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(86) });
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1.0, GridUnitType.Star) });

                Button xMinus;
                Button xPlus;
                Button yMinus;
                Button yPlus;
                Button zMinus;
                Button zPlus;
                xBox = AddAxisRow(grid, 0, "X", out xMinus, out xPlus);
                yBox = AddAxisRow(grid, 1, "Y", out yMinus, out yPlus);
                zBox = AddAxisRow(grid, 2, "Z", out zMinus, out zPlus);
                panel.Children.Add(grid);

                var actions = new WrapPanel { Margin = new Thickness(0, 10, 0, 0) };
                Button readButton = MakeButton("Read current", 105);
                Button applyButton = MakeButton("Apply", 85);
                Button resetButton = MakeButton("Reset", 85);
                Button saveButton = MakeButton("Save", 85);
                Button loadApplyButton = MakeButton("Load + Apply", 120);
                actions.Children.Add(readButton);
                actions.Children.Add(applyButton);
                actions.Children.Add(resetButton);
                actions.Children.Add(saveButton);
                actions.Children.Add(loadApplyButton);
                panel.Children.Add(actions);

                xMinus.Click += delegate { RunUiAction("Adjust failed", delegate { NudgeAxis(xBox, "X", -1); }); };
                xPlus.Click += delegate { RunUiAction("Adjust failed", delegate { NudgeAxis(xBox, "X", 1); }); };
                yMinus.Click += delegate { RunUiAction("Adjust failed", delegate { NudgeAxis(yBox, "Y", -1); }); };
                yPlus.Click += delegate { RunUiAction("Adjust failed", delegate { NudgeAxis(yBox, "Y", 1); }); };
                zMinus.Click += delegate { RunUiAction("Adjust failed", delegate { NudgeAxis(zBox, "Z", -1); }); };
                zPlus.Click += delegate { RunUiAction("Adjust failed", delegate { NudgeAxis(zBox, "Z", 1); }); };

                readButton.Click += delegate { RunUiAction("Read failed", ReadCurrent); };
                applyButton.Click += delegate { RunUiAction("Apply failed", ApplyFields); };
                resetButton.Click += delegate { RunUiAction("Reset failed", ResetTranslation); };
                saveButton.Click += delegate { RunUiAction("Save failed", SaveFields); };
                loadApplyButton.Click += delegate { RunUiAction("Load failed", LoadAndApply); };

                group.Content = panel;
                return group;
            }

            private TextBox AddAxisRow(Grid grid, int row, string axis, out Button minusButton, out Button plusButton)
            {
                grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

                var label = new TextBlock
                {
                    Text = axis,
                    VerticalAlignment = VerticalAlignment.Center,
                    Margin = new Thickness(0, 4, 6, 4)
                };
                Grid.SetRow(label, row);
                Grid.SetColumn(label, 0);
                grid.Children.Add(label);

                var box = new TextBox
                {
                    Margin = new Thickness(0, 3, 6, 3),
                    VerticalContentAlignment = VerticalAlignment.Center
                };
                Grid.SetRow(box, row);
                Grid.SetColumn(box, 1);
                grid.Children.Add(box);

                var unit = new TextBlock
                {
                    Text = "m",
                    VerticalAlignment = VerticalAlignment.Center,
                    Margin = new Thickness(0, 4, 6, 4)
                };
                Grid.SetRow(unit, row);
                Grid.SetColumn(unit, 2);
                grid.Children.Add(unit);

                minusButton = MakeButton("-1 mm", 78);
                plusButton = MakeButton("+1 mm", 78);
                Grid.SetRow(minusButton, row);
                Grid.SetColumn(minusButton, 3);
                Grid.SetRow(plusButton, row);
                Grid.SetColumn(plusButton, 4);
                grid.Children.Add(minusButton);
                grid.Children.Add(plusButton);

                return box;
            }

            private FrameworkElement BuildCalibrationSection()
            {
                var group = new GroupBox
                {
                    Header = "Calibration",
                    Margin = new Thickness(0, 6, 0, 6),
                    Padding = new Thickness(10)
                };

                var grid = new Grid();
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(100) });
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1.0, GridUnitType.Star) });
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(110) });
                grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

                AddGridLabel(grid, 0, "PICO tracker");
                trackerCombo = new ComboBox
                {
                    Margin = new Thickness(0, 3, 8, 3),
                    MinWidth = 280
                };
                Grid.SetRow(trackerCombo, 0);
                Grid.SetColumn(trackerCombo, 1);
                grid.Children.Add(trackerCombo);
                Button refreshButton = MakeButton("Refresh", 95);
                Grid.SetRow(refreshButton, 0);
                Grid.SetColumn(refreshButton, 2);
                grid.Children.Add(refreshButton);

                AddGridLabel(grid, 1, "Reference");
                referenceCombo = new ComboBox { Margin = new Thickness(0, 3, 8, 3) };
                referenceCombo.Items.Add("Right Controller");
                referenceCombo.Items.Add("Left Controller");
                referenceCombo.Items.Add("HMD (optional)");
                referenceCombo.SelectedIndex = 0;
                Grid.SetRow(referenceCombo, 1);
                Grid.SetColumn(referenceCombo, 1);
                grid.Children.Add(referenceCombo);

                Button calibrateButton = MakeButton("Measure + Apply", 140);
                calibrateButton.HorizontalAlignment = HorizontalAlignment.Left;
                Grid.SetRow(calibrateButton, 2);
                Grid.SetColumn(calibrateButton, 1);
                grid.Children.Add(calibrateButton);

                refreshButton.Click += delegate { RunUiAction("Tracker refresh failed", RefreshTrackers); };
                calibrateButton.Click += delegate { RunUiAction("Calibration failed", Calibrate); };

                group.Content = grid;
                return group;
            }

            private FrameworkElement BuildDiagnosticsSection()
            {
                var group = new GroupBox
                {
                    Header = "Diagnostics",
                    Margin = new Thickness(0, 6, 0, 6),
                    Padding = new Thickness(10)
                };

                var grid = new Grid();
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(120) });
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(150) });
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(150) });
                grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

                var zeroHeader = new TextBlock
                {
                    Text = "Zero",
                    FontWeight = FontWeights.SemiBold,
                    HorizontalAlignment = HorizontalAlignment.Center,
                    Margin = new Thickness(4)
                };
                Grid.SetRow(zeroHeader, 0);
                Grid.SetColumn(zeroHeader, 1);
                grid.Children.Add(zeroHeader);

                var clearHeader = new TextBlock
                {
                    Text = "Clear",
                    FontWeight = FontWeights.SemiBold,
                    HorizontalAlignment = HorizontalAlignment.Center,
                    Margin = new Thickness(4)
                };
                Grid.SetRow(clearHeader, 0);
                Grid.SetColumn(clearHeader, 2);
                grid.Children.Add(clearHeader);

                AddGridLabel(grid, 1, "Orientation");
                AddGridLabel(grid, 2, "Position");

                Button orientationZero = MakeButton("Zero", 130);
                Button orientationClear = MakeButton("Clear", 130);
                Button positionZero = MakeButton("Zero", 130);
                Button positionClear = MakeButton("Clear", 130);

                Grid.SetRow(orientationZero, 1);
                Grid.SetColumn(orientationZero, 1);
                Grid.SetRow(orientationClear, 1);
                Grid.SetColumn(orientationClear, 2);
                Grid.SetRow(positionZero, 2);
                Grid.SetColumn(positionZero, 1);
                Grid.SetRow(positionClear, 2);
                Grid.SetColumn(positionClear, 2);
                grid.Children.Add(orientationZero);
                grid.Children.Add(orientationClear);
                grid.Children.Add(positionZero);
                grid.Children.Add(positionClear);

                orientationZero.Click += delegate { RunUiAction("Control failed", delegate { SendControlEvent(OrientationZeroEventName); SetStatus("Orientation Zero requested."); }); };
                orientationClear.Click += delegate { RunUiAction("Control failed", delegate { SendControlEvent(OrientationClearEventName); SetStatus("Orientation Zero cleared."); }); };
                positionZero.Click += delegate { RunUiAction("Control failed", delegate { SendControlEvent(PositionZeroEventName); SetStatus("Position Zero requested."); }); };
                positionClear.Click += delegate { RunUiAction("Control failed", delegate { SendControlEvent(PositionClearEventName); SetStatus("Position Zero cleared."); }); };

                group.Content = grid;
                return group;
            }

            private FrameworkElement BuildStatusSection()
            {
                var group = new GroupBox
                {
                    Header = "Status",
                    Margin = new Thickness(0, 6, 0, 0),
                    Padding = new Thickness(8)
                };

                statusBox = new TextBox
                {
                    IsReadOnly = true,
                    AcceptsReturn = true,
                    TextWrapping = TextWrapping.Wrap,
                    VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                    MinHeight = 100
                };
                group.Content = statusBox;
                return group;
            }

            private static Button MakeButton(string text, double width)
            {
                return new Button
                {
                    Content = text,
                    Width = width,
                    MinHeight = 28,
                    Margin = new Thickness(3)
                };
            }

            private static void AddGridLabel(Grid grid, int row, string text)
            {
                var label = new TextBlock
                {
                    Text = text,
                    VerticalAlignment = VerticalAlignment.Center,
                    Margin = new Thickness(0, 5, 8, 5)
                };
                Grid.SetRow(label, row);
                Grid.SetColumn(label, 0);
                grid.Children.Add(label);
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
                    MessageBox.Show(this, ex.Message, title, MessageBoxButton.OK, MessageBoxImage.Error);
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
                if (unsavedText == null)
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

                unsavedText.Visibility = dirty ? Visibility.Visible : Visibility.Collapsed;
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
                double stepMeters = (Keyboard.Modifiers & ModifierKeys.Shift) == ModifierKeys.Shift ? 0.010 : 0.001;
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
                Dispatcher.Invoke(DispatcherPriority.Background, new Action(delegate { }));

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
                if (statusBox != null)
                {
                    statusBox.Text = text;
                }
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
