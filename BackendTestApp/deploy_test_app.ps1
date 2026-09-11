param(
    [string]$UnityVersion = "2022.3.22f1",
    [string]$Device = "",
    [switch]$SkipBuild,
    [switch]$NoLogcat
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$android = "C:\Program Files\Unity\Hub\Editor\$UnityVersion\Editor\Data\PlaybackEngines\AndroidPlayer"
$adb = Join-Path $android "SDK\platform-tools\adb.exe"
$apk = Join-Path $root "app\build\outputs\apk\debug\app-debug.apk"

if (-not (Test-Path $adb)) { throw "adb not found: $adb" }
if (-not $SkipBuild) {
    & (Join-Path $root "build_test_app.ps1") -UnityVersion $UnityVersion
}
if (-not (Test-Path $apk)) { throw "APK not found: $apk" }

function Invoke-AdbCaptured {
    param([string[]]$Arguments)
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = (& $adb @Arguments 2>&1 | Out-String).Trim()
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }
    [PSCustomObject]@{ Output = $output; ExitCode = $exitCode }
}

if ($Device -and $Device.Contains(':')) {
    $connect = Invoke-AdbCaptured @('connect', $Device)
    if ($connect.Output) { Write-Host $connect.Output }
    if ($connect.ExitCode -ne 0 -or $connect.Output -match '(?i)failed|unable|cannot|refused|timed out|no route') {
        throw "adb connect failed: $($connect.Output)"
    }
}

$adbPrefix = @()
if ($Device) { $adbPrefix += @('-s', $Device) }

$state = Invoke-AdbCaptured ($adbPrefix + @('get-state'))
if ($state.Output) { Write-Host "ADB state: $($state.Output)" }
if ($state.ExitCode -ne 0 -or $state.Output -notmatch 'device') {
    throw "ADB device is not ready. Use -Device <serial|ip:port> when needed."
}

$install = Invoke-AdbCaptured ($adbPrefix + @('install', '-r', '-t', $apk))
if ($install.Output) { Write-Host $install.Output }
if ($install.ExitCode -ne 0) { throw "adb install failed: $($install.Output)" }

$clear = Invoke-AdbCaptured ($adbPrefix + @('logcat', '-c'))
if ($clear.ExitCode -ne 0) { Write-Warning "logcat clear failed: $($clear.Output)" }

$stop = Invoke-AdbCaptured ($adbPrefix + @('shell', 'am', 'force-stop', 'com.orz.picootbackendtest'))
if ($stop.ExitCode -ne 0) { Write-Warning "force-stop failed: $($stop.Output)" }

$launch = Invoke-AdbCaptured ($adbPrefix + @('shell', 'am', 'start', '-n', 'com.orz.picootbackendtest/.MainActivity'))
if ($launch.Output) { Write-Host $launch.Output }
if ($launch.ExitCode -ne 0) { throw "Failed to launch test app: $($launch.Output)" }

Write-Host "Started com.orz.picootbackendtest/.MainActivity"
Write-Host "Log tag: PicoOTTest"

if (-not $NoLogcat) {
    & $adb @adbPrefix logcat -v threadtime -s 'PicoOTTest:I' '*:S'
}
