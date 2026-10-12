using System;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using PicoOTBridge.Control;

namespace PicoOTBridge
{
    public static class SteamVrControlWpf
    {
        public static void Run(string repoRoot)
        {
            var application = new Application();
            application.Run(new ControlWindow(new ControlViewModel(new PipeControlClient())));
        }
        public sealed class ControlWindow : Window
        {
            readonly ControlViewModel model;
            TextBox xBox = null!, yBox = null!, zBox = null!, statusBox = null!;
            ComboBox trackerCombo = null!, referenceCombo = null!;
            bool closing;
            public ControlWindow(ControlViewModel model)
            {
                this.model = model; DataContext = model;
                Title = "PICO OT SteamVR Control"; Width = 980; Height = 940; MinWidth = 740; MinHeight = 650;
                WindowStartupLocation = WindowStartupLocation.CenterScreen;
                Content = BuildUi();
                xBox.SetBinding(TextBox.TextProperty, new Binding("X") { Mode = BindingMode.TwoWay, UpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged });
                yBox.SetBinding(TextBox.TextProperty, new Binding("Y") { Mode = BindingMode.TwoWay, UpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged });
                zBox.SetBinding(TextBox.TextProperty, new Binding("Z") { Mode = BindingMode.TwoWay, UpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged });
                trackerCombo.SetBinding(ItemsControl.ItemsSourceProperty, new Binding("Devices"));
                trackerCombo.DisplayMemberPath = "Serial"; trackerCombo.SelectedValuePath = "Serial";
                trackerCombo.SetBinding(System.Windows.Controls.Primitives.Selector.SelectedValueProperty, new Binding("SelectedSerial") { Mode = BindingMode.TwoWay });
                referenceCombo.SelectionChanged += delegate { model.Reference = referenceCombo.SelectedIndex == 1 ? "left" : referenceCombo.SelectedIndex == 2 ? "hmd" : "right"; };
                Loaded += delegate { model.StartPolling(); };
                Closing += async delegate (object? sender, System.ComponentModel.CancelEventArgs e) { if (closing) return; e.Cancel = true; IsEnabled = false; await model.DisposeAsync(); closing = true; _ = Dispatcher.BeginInvoke(new Action(Close)); };
            }
            private FrameworkElement BuildUi()
            {
                var panel = new StackPanel { Margin = new Thickness(16) };
                var health = new TextBlock { TextWrapping = TextWrapping.Wrap, FontWeight = FontWeights.SemiBold, Margin = new Thickness(0, 0, 0, 12) };
                health.SetBinding(TextBlock.TextProperty, new Binding("Health")); panel.Children.Add(health);
                var actions = new WrapPanel();
                foreach (var pair in new[] { ("Refresh status", model.Refresh), ("Start PC receiver", model.StartBackend), ("Stop PC receiver", model.StopBackend) }) actions.Children.Add(new Button { Content = pair.Item1, Command = pair.Item2, Padding = new Thickness(10, 6, 10, 6), Margin = new Thickness(0, 0, 8, 8) });
                actions.Children.Add(new Button { Content = "Cancel operation", Command = model.Cancel, Padding = new Thickness(10, 6, 10, 6) });
                var copy = new Button { Content = "Copy diagnostics", Margin = new Thickness(8, 0, 0, 8) }; copy.Click += delegate { Clipboard.SetText(model.Diagnostics()); }; actions.Children.Add(copy);
                var logs = new Button { Content = "Open log folder", Margin = new Thickness(8, 0, 0, 8) }; logs.Click += delegate { var path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PicoMotionTrackerBridge", "logs"); Directory.CreateDirectory(path); Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }; actions.Children.Add(logs);
                panel.Children.Add(actions);
                var devices = new DataGrid { IsReadOnly = true, AutoGenerateColumns = false, CanUserAddRows = false, MinHeight = 150, Margin = new Thickness(0, 4, 0, 12) };
                foreach (var pair in new[] { ("Serial / stable ID", "Serial"), ("Connection", "Connection"), ("Tracking", "Tracking"), ("Position", "Position"), ("Orientation", "Orientation"), ("Last update", "LastUpdate"), ("Battery", "Battery") }) devices.Columns.Add(new DataGridTextColumn { Header = pair.Item1, Binding = new Binding(pair.Item2) });
                devices.SetBinding(ItemsControl.ItemsSourceProperty, new Binding("Devices")); panel.Children.Add(devices);
                panel.Children.Add(new TextBlock { Text = "Connection is the backend's reported snapshot; pose age shows stale input independently. New HMD device discovery requires stopping and starting the HMD backend. PC receiver restart does not rescan the vendor runtime.", TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 12) });
                panel.Children.Add(BuildTranslationSection());
                var dirty = new TextBlock { Text = "Unsaved changes — click Save to keep this alignment.", FontWeight = FontWeights.SemiBold, Margin = new Thickness(8) };
                dirty.SetBinding(VisibilityProperty, new Binding("Unsaved") { Converter = new BooleanToVisibilityConverter() }); panel.Children.Add(dirty);
                panel.Children.Add(BuildCalibrationSection());
                var advanced = new StackPanel(); var route = new ComboBox { ItemsSource = new[] { "steamvr", "monaka", "both", "disabled" }, Width = 160, HorizontalAlignment = HorizontalAlignment.Left }; route.SetBinding(System.Windows.Controls.Primitives.Selector.SelectedItemProperty, new Binding("Route") { Mode = BindingMode.TwoWay }); advanced.Children.Add(route); advanced.Children.Add(new Button { Content = "Save output route", Command = model.SetRoute, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0, 8, 0, 8) });
                panel.Children.Add(new Expander { Header = "Advanced: output route", Content = advanced, Margin = new Thickness(0, 8, 0, 8) });
                panel.Children.Add(new Expander { Header = "Diagnostics / Research: temporary driver zero controls", Content = BuildDiagnosticsSection(), Margin = new Thickness(0, 8, 0, 8) });
                panel.Children.Add(BuildStatusSection()); return new ScrollViewer { Content = panel, Background = SystemColors.WindowBrush, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
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
                foreach (var nudge in new[] { xMinus, xPlus, yMinus, yPlus, zMinus, zPlus }) nudge.SetBinding(IsEnabledProperty, new Binding("CanApply"));
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

                xMinus.Click += async delegate { await model.NudgeAsync("X", -1, (Keyboard.Modifiers & ModifierKeys.Shift) != 0 ? 0.010 : 0.001); };
                xPlus.Click += async delegate { await model.NudgeAsync("X", 1, (Keyboard.Modifiers & ModifierKeys.Shift) != 0 ? 0.010 : 0.001); };
                yMinus.Click += async delegate { await model.NudgeAsync("Y", -1, (Keyboard.Modifiers & ModifierKeys.Shift) != 0 ? 0.010 : 0.001); };
                yPlus.Click += async delegate { await model.NudgeAsync("Y", 1, (Keyboard.Modifiers & ModifierKeys.Shift) != 0 ? 0.010 : 0.001); };
                zMinus.Click += async delegate { await model.NudgeAsync("Z", -1, (Keyboard.Modifiers & ModifierKeys.Shift) != 0 ? 0.010 : 0.001); };
                zPlus.Click += async delegate { await model.NudgeAsync("Z", 1, (Keyboard.Modifiers & ModifierKeys.Shift) != 0 ? 0.010 : 0.001); };

                readButton.Command = model.Read;
                applyButton.Command = model.Apply;
                resetButton.Command = model.Reset;
                saveButton.Command = model.Save;
                loadApplyButton.Command = model.Load;

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

                refreshButton.Command = model.Refresh;
                calibrateButton.Command = model.Calibrate;

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

                orientationZero.Click += async delegate { await model.DiagnosticAsync("orientation_zero"); };
                orientationClear.Click += async delegate { await model.DiagnosticAsync("orientation_clear"); };
                positionZero.Click += async delegate { await model.DiagnosticAsync("position_zero"); };
                positionClear.Click += async delegate { await model.DiagnosticAsync("position_clear"); };

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
                statusBox.SetBinding(TextBox.TextProperty, new Binding("Message") { Mode = BindingMode.OneWay });
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

        }
    }
}
