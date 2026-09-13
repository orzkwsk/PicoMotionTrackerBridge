param(
    [string]$UnityVersion = "2022.3.22f1",
    [string]$Device = ""
)

$ErrorActionPreference = "Stop"

function Find-Adb {
    $fromPath = Get-Command adb.exe -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    $unityAdb = "C:\Program Files\Unity\Hub\Editor\$UnityVersion\Editor\Data\PlaybackEngines\AndroidPlayer\SDK\platform-tools\adb.exe"
    if (Test-Path $unityAdb) {
        return $unityAdb
    }

    throw "adb.exe was not found. Install Android platform-tools or Unity $UnityVersion Android Build Support."
}

$adb = Find-Adb
$adbPrefix = @()
if ($Device) {
    if ($Device.Contains(':')) {
        & $adb connect $Device | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "adb connect failed: $Device"
        }
    }
    $adbPrefix = @('-s', $Device)
}

$state = (& $adb @adbPrefix get-state 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $state -notmatch 'device') {
    throw "ADB device is not ready. Connect the PICO first. Current state: $state"
}

$package = 'com.orz.picootbackendtest'
$activity = "$package/.MainActivity"
$packagePath = (& $adb @adbPrefix shell pm path $package 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $packagePath -notmatch '^package:') {
    throw "PICO BackendTestApp is not installed. Run BackendTestApp\deploy_test_app.ps1 once first."
}

& $adb @adbPrefix shell am force-stop $package | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Failed to stop existing PICO BackendTestApp process."
}

$launch = (& $adb @adbPrefix shell am start -n $activity 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Failed to launch $activity`n$launch"
}

Write-Host "PICO Backend started."
Write-Host $launch
