param(
    [string]$UnityVersion = "2022.3.22f1",
    [string]$Device = ""
)

$adb = "C:\Program Files\Unity\Hub\Editor\$UnityVersion\Editor\Data\PlaybackEngines\AndroidPlayer\SDK\platform-tools\adb.exe"
if (-not (Test-Path $adb)) { throw "adb not found: $adb" }

$adbPrefix = @()
if ($Device) { $adbPrefix += @('-s', $Device) }
& $adb @adbPrefix logcat -v threadtime -s 'PicoOTTest:I' '*:S'
