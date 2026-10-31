param(
    [string]$UnityVersion = "2022.3.22f1",
    [string]$Device = ""
)

$fromPath = Get-Command adb.exe -ErrorAction SilentlyContinue
$adb = if ($fromPath) { $fromPath.Source } else { $null }
if (-not $adb) {
    $candidates = @()
    foreach ($sdkRoot in @($env:ANDROID_SDK_ROOT, $env:ANDROID_HOME)) {
        if ($sdkRoot) { $candidates += Join-Path $sdkRoot 'platform-tools\adb.exe' }
    }
    if ($env:ProgramFiles) {
        $candidates += Join-Path $env:ProgramFiles "Unity\Hub\Editor\$UnityVersion\Editor\Data\PlaybackEngines\AndroidPlayer\SDK\platform-tools\adb.exe"
    }
    $adb = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $adb) { throw 'adb.exe was not found. Set ANDROID_SDK_ROOT/ANDROID_HOME or add Android platform-tools to PATH.' }

$adbPrefix = @()
if ($Device) { $adbPrefix += @('-s', $Device) }
& $adb @adbPrefix logcat -v threadtime -s 'PicoOTTest:I' '*:S'
