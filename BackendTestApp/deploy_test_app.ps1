param(
    [string]$UnityVersion = "2022.3.22f1",
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

& $adb devices
& $adb install -r $apk
if ($LASTEXITCODE -ne 0) { throw "adb install failed" }

& $adb logcat -c
& $adb shell am force-stop com.orz.picootbackendtest
& $adb shell am start -n com.orz.picootbackendtest/.MainActivity
if ($LASTEXITCODE -ne 0) { throw "Failed to launch test app" }

Write-Host "Started com.orz.picootbackendtest/.MainActivity"
Write-Host "Log tag: PicoOTTest"

if (-not $NoLogcat) {
    & $adb logcat -v threadtime -s 'PicoOTTest:I' '*:S'
}
