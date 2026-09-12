param()

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$RepoRoot = Split-Path -Parent $PSScriptRoot
$MappingName = "Local\PicoOTBridge_WorldAlignment"
$AlignmentUpdatedEventName = "Local\PicoOTBridge_WorldAlignmentUpdated"
$OrientationZeroEventName = "Local\PicoOTBridge_OrientationZero"
$OrientationClearEventName = "Local\PicoOTBridge_OrientationClear"
$PositionZeroEventName = "Local\PicoOTBridge_PositionZero"
$PositionClearEventName = "Local\PicoOTBridge_PositionClear"

$AlignmentMagic = [uint32]0x504F5441
$AlignmentVersion = [uint32]1
$AlignmentStateSize = 40

$ConfigDirectory = Join-Path $env:LOCALAPPDATA "PicoMotionTrackerBridge"
$ConfigPath = Join-Path $ConfigDirectory "steamvr_alignment.json"

function Format-Number([double]$Value) {
    return $Value.ToString("0.000000", [System.Globalization.CultureInfo]::InvariantCulture)
}

function Parse-Number([string]$Text, [string]$Name) {
    try {
        return [double]::Parse(
            $Text,
            [System.Globalization.NumberStyles]::Float,
            [System.Globalization.CultureInfo]::InvariantCulture)
    }
    catch {
        throw "$Name must be a number using '.' as the decimal separator."
    }
}

function Get-AlignmentState {
    $mapping = $null
    $view = $null
    try {
        $mapping = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting(
            $MappingName,
            [System.IO.MemoryMappedFiles.MemoryMappedFileRights]::ReadWrite)
        $view = $mapping.CreateViewAccessor(
            0,
            $AlignmentStateSize,
            [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite)

        $magic = $view.ReadUInt32(0)
        $version = $view.ReadUInt32(4)
        if ($magic -ne $AlignmentMagic -or $version -ne $AlignmentVersion) {
            throw "SteamVR world-alignment shared-state ABI mismatch."
        }

        return [pscustomobject]@{
            Generation = $view.ReadUInt64(8)
            X = $view.ReadDouble(16)
            Y = $view.ReadDouble(24)
            Z = $view.ReadDouble(32)
        }
    }
    catch [System.IO.FileNotFoundException] {
        throw "SteamVR alignment IPC was not found. Start SteamVR with the PICO OT bridge driver first."
    }
    finally {
        if ($view) { $view.Dispose() }
        if ($mapping) { $mapping.Dispose() }
    }
}

function Set-AlignmentTranslation([double]$X, [double]$Y, [double]$Z) {
    $mapping = $null
    $view = $null
    $updatedEvent = $null
    try {
        $mapping = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting(
            $MappingName,
            [System.IO.MemoryMappedFiles.MemoryMappedFileRights]::ReadWrite)
        $view = $mapping.CreateViewAccessor(
            0,
            $AlignmentStateSize,
            [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite)

        $magic = $view.ReadUInt32(0)
        $version = $view.ReadUInt32(4)
        if ($magic -ne $AlignmentMagic -or $version -ne $AlignmentVersion) {
            throw "SteamVR world-alignment shared-state ABI mismatch."
        }

        $generation = $view.ReadUInt64(8)
        $view.Write(16, [double]$X)
        $view.Write(24, [double]$Y)
        $view.Write(32, [double]$Z)
        $view.Write(8, [uint64]($generation + 1))
        $view.Flush()

        $updatedEvent = [System.Threading.EventWaitHandle]::OpenExisting($AlignmentUpdatedEventName)
        if (-not $updatedEvent.Set()) {
            throw "Failed to signal the SteamVR alignment update event."
        }
    }
    catch [System.IO.FileNotFoundException] {
        throw "SteamVR alignment IPC was not found. Start SteamVR with the PICO OT bridge driver first."
    }
    finally {
        if ($updatedEvent) { $updatedEvent.Dispose() }
        if ($view) { $view.Dispose() }
        if ($mapping) { $mapping.Dispose() }
    }
}

function Send-ControlEvent([string]$Name) {
    $event = $null
    try {
        $event = [System.Threading.EventWaitHandle]::OpenExisting($Name)
        if (-not $event.Set()) {
            throw "Failed to signal control event: $Name"
        }
    }
    catch [System.Threading.WaitHandleCannotBeOpenedException] {
        throw "SteamVR control event was not found. Start SteamVR with the PICO OT bridge driver first."
    }
    finally {
        if ($event) { $event.Dispose() }
    }
}

function Save-AlignmentConfig([double]$X, [double]$Y, [double]$Z) {
    New-Item -ItemType Directory -Path $ConfigDirectory -Force | Out-Null
    [pscustomobject]@{
        version = 1
        xMeters = $X
        yMeters = $Y
        zMeters = $Z
    } | ConvertTo-Json | Set-Content -Path $ConfigPath -Encoding UTF8
}

function Load-AlignmentConfig {
    if (-not (Test-Path $ConfigPath)) {
        return $null
    }
    $config = Get-Content -Raw -Path $ConfigPath | ConvertFrom-Json
    if ($config.version -ne 1) {
        throw "Unsupported saved alignment version: $($config.version)"
    }
    return [pscustomobject]@{
        X = [double]$config.xMeters
        Y = [double]$config.yMeters
        Z = [double]$config.zMeters
    }
}

function Find-Calibrator {
    $buildRoot = Join-Path $RepoRoot "build-steamvr"
    if (-not (Test-Path $buildRoot)) {
        return $null
    }
    return Get-ChildItem -Path $buildRoot -Filter "pico_ot_steamvr_calibrator.exe" -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}

$form = New-Object System.Windows.Forms.Form
$form.Text = "PICO OT SteamVR Control"
$form.ClientSize = New-Object System.Drawing.Size(560, 485)
$form.StartPosition = "CenterScreen"
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox = $false

$font = New-Object System.Drawing.Font("Segoe UI", 9)
$form.Font = $font

function Add-Label([string]$Text, [int]$X, [int]$Y, [int]$Width = 120) {
    $control = New-Object System.Windows.Forms.Label
    $control.Text = $Text
    $control.Location = New-Object System.Drawing.Point($X, $Y)
    $control.Size = New-Object System.Drawing.Size($Width, 22)
    $form.Controls.Add($control)
    return $control
}

function Add-TextBox([int]$X, [int]$Y, [int]$Width = 120) {
    $control = New-Object System.Windows.Forms.TextBox
    $control.Location = New-Object System.Drawing.Point($X, $Y)
    $control.Size = New-Object System.Drawing.Size($Width, 23)
    $form.Controls.Add($control)
    return $control
}

function Add-Button([string]$Text, [int]$X, [int]$Y, [int]$Width = 95) {
    $control = New-Object System.Windows.Forms.Button
    $control.Text = $Text
    $control.Location = New-Object System.Drawing.Point($X, $Y)
    $control.Size = New-Object System.Drawing.Size($Width, 30)
    $form.Controls.Add($control)
    return $control
}

Add-Label "World translation (meters)" 20 18 220 | Out-Null
Add-Label "X" 25 52 25 | Out-Null
Add-Label "Y" 25 82 25 | Out-Null
Add-Label "Z" 25 112 25 | Out-Null
$xBox = Add-TextBox 55 49 150
$yBox = Add-TextBox 55 79 150
$zBox = Add-TextBox 55 109 150

$readButton = Add-Button "Read current" 230 48 100
$applyButton = Add-Button "Apply" 340 48 90
$resetButton = Add-Button "Reset" 440 48 90
$saveButton = Add-Button "Save" 230 88 100
$loadApplyButton = Add-Button "Load + Apply" 340 88 190

Add-Label "Calibration" 20 160 120 | Out-Null
Add-Label "Tracker serial" 25 194 90 | Out-Null
$serialBox = Add-TextBox 120 191 260
Add-Label "Controller" 25 226 90 | Out-Null
$controllerCombo = New-Object System.Windows.Forms.ComboBox
$controllerCombo.Location = New-Object System.Drawing.Point(120, 223)
$controllerCombo.Size = New-Object System.Drawing.Size(120, 23)
$controllerCombo.DropDownStyle = "DropDownList"
[void]$controllerCombo.Items.Add("Right")
[void]$controllerCombo.Items.Add("Left")
$controllerCombo.SelectedIndex = 0
$form.Controls.Add($controllerCombo)
$calibrateButton = Add-Button "Calibrate" 260 219 120

Add-Label "Diagnostics" 20 275 120 | Out-Null
$orientationZeroButton = Add-Button "Orientation Zero" 25 307 120
$orientationClearButton = Add-Button "Orientation Clear" 155 307 120
$positionZeroButton = Add-Button "Position Zero" 285 307 120
$positionClearButton = Add-Button "Position Clear" 415 307 120

Add-Label "Status" 20 355 100 | Out-Null
$statusBox = New-Object System.Windows.Forms.TextBox
$statusBox.Location = New-Object System.Drawing.Point(20, 380)
$statusBox.Size = New-Object System.Drawing.Size(515, 80)
$statusBox.Multiline = $true
$statusBox.ReadOnly = $true
$statusBox.ScrollBars = "Vertical"
$form.Controls.Add($statusBox)

function Set-Status([string]$Text) {
    $statusBox.Text = $Text
}

function Put-State-In-Fields($State) {
    $xBox.Text = Format-Number $State.X
    $yBox.Text = Format-Number $State.Y
    $zBox.Text = Format-Number $State.Z
}

function Get-Fields-AsTranslation {
    return [pscustomobject]@{
        X = Parse-Number $xBox.Text "X"
        Y = Parse-Number $yBox.Text "Y"
        Z = Parse-Number $zBox.Text "Z"
    }
}

$readButton.Add_Click({
    try {
        $state = Get-AlignmentState
        Put-State-In-Fields $state
        Set-Status "Read current translation. generation=$($state.Generation)"
    }
    catch {
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Read failed", "OK", "Error") | Out-Null
    }
})

$applyButton.Add_Click({
    try {
        $t = Get-Fields-AsTranslation
        Set-AlignmentTranslation $t.X $t.Y $t.Z
        Set-Status "Applied translation: X=$(Format-Number $t.X), Y=$(Format-Number $t.Y), Z=$(Format-Number $t.Z) m"
    }
    catch {
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Apply failed", "OK", "Error") | Out-Null
    }
})

$resetButton.Add_Click({
    try {
        Set-AlignmentTranslation 0.0 0.0 0.0
        Put-State-In-Fields ([pscustomobject]@{ X = 0.0; Y = 0.0; Z = 0.0 })
        Set-Status "World translation reset to zero."
    }
    catch {
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Reset failed", "OK", "Error") | Out-Null
    }
})

$saveButton.Add_Click({
    try {
        $t = Get-Fields-AsTranslation
        Save-AlignmentConfig $t.X $t.Y $t.Z
        Set-Status "Saved translation to $ConfigPath"
    }
    catch {
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Save failed", "OK", "Error") | Out-Null
    }
})

$loadApplyButton.Add_Click({
    try {
        $t = Load-AlignmentConfig
        if ($null -eq $t) {
            throw "No saved alignment exists at $ConfigPath"
        }
        Put-State-In-Fields $t
        Set-AlignmentTranslation $t.X $t.Y $t.Z
        Set-Status "Loaded and applied saved translation from $ConfigPath"
    }
    catch {
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Load failed", "OK", "Error") | Out-Null
    }
})

$calibrateButton.Add_Click({
    try {
        $serial = $serialBox.Text.Trim()
        if ([string]::IsNullOrWhiteSpace($serial)) {
            throw "Enter the PICO tracker serial to use as the calibration reference."
        }

        $calibrator = Find-Calibrator
        if (-not $calibrator) {
            throw "pico_ot_steamvr_calibrator.exe was not found. Run scripts\build_steamvr_driver.ps1 first."
        }

        # Per-tracker Position Zero changes the reported absolute position, so it
        # must be disabled before world-space calibration.
        Send-ControlEvent $PositionClearEventName
        Start-Sleep -Milliseconds 100

        $role = $controllerCombo.SelectedItem.ToString().ToLowerInvariant()
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $calibrator
        $psi.Arguments = "--tracker `"$serial`" --controller $role"
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.CreateNoWindow = $true

        Set-Status "Calibrating... keep the tracker and controller reference points together and still."
        $form.Refresh()

        $process = [System.Diagnostics.Process]::Start($psi)
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) {
            throw "Calibration failed (exit $($process.ExitCode)).`r`n$stdout`r`n$stderr"
        }

        $state = Get-AlignmentState
        Put-State-In-Fields $state
        Save-AlignmentConfig $state.X $state.Y $state.Z
        Set-Status (($stdout.Trim()) + "`r`nSaved resulting translation.")
    }
    catch {
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Calibration failed", "OK", "Error") | Out-Null
        Set-Status $_.Exception.Message
    }
})

$orientationZeroButton.Add_Click({
    try { Send-ControlEvent $OrientationZeroEventName; Set-Status "Orientation Zero requested." }
    catch { [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Control failed", "OK", "Error") | Out-Null }
})
$orientationClearButton.Add_Click({
    try { Send-ControlEvent $OrientationClearEventName; Set-Status "Orientation Zero cleared." }
    catch { [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Control failed", "OK", "Error") | Out-Null }
})
$positionZeroButton.Add_Click({
    try { Send-ControlEvent $PositionZeroEventName; Set-Status "Position Zero requested." }
    catch { [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Control failed", "OK", "Error") | Out-Null }
})
$positionClearButton.Add_Click({
    try { Send-ControlEvent $PositionClearEventName; Set-Status "Position Zero cleared." }
    catch { [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Control failed", "OK", "Error") | Out-Null }
})

# Load persisted values into the edit fields on startup, but do not silently
# modify the active SteamVR alignment. The user must explicitly Apply or use
# Load + Apply.
try {
    $saved = Load-AlignmentConfig
    if ($saved) {
        Put-State-In-Fields $saved
        Set-Status "Loaded saved values into the fields. Click 'Load + Apply' to use them."
    }
    else {
        Put-State-In-Fields ([pscustomobject]@{ X = 0.0; Y = 0.0; Z = 0.0 })
        Set-Status "No saved alignment yet."
    }
}
catch {
    Put-State-In-Fields ([pscustomobject]@{ X = 0.0; Y = 0.0; Z = 0.0 })
    Set-Status "Saved alignment could not be read: $($_.Exception.Message)"
}

[void]$form.ShowDialog()
