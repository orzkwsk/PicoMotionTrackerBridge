param(
    [string]$UnityVersion = "2022.3.22f1"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$unity = "C:\Program Files\Unity\Hub\Editor\$UnityVersion\Editor"
$android = Join-Path $unity "Data\PlaybackEngines\AndroidPlayer"
$sdk = Join-Path $android "SDK"
$ndk = Join-Path $android "NDK"
$jdk = Join-Path $android "OpenJDK"
$gradle = Join-Path $android "Tools\gradle\bin\gradle.bat"

foreach ($path in @($sdk, $ndk, $jdk, $gradle)) {
    if (-not (Test-Path $path)) { throw "Required Unity Android tool not found: $path" }
}

$env:JAVA_HOME = $jdk
$env:ANDROID_HOME = $sdk
$env:ANDROID_SDK_ROOT = $sdk
$env:ANDROID_NDK_HOME = $ndk

$sdkProp = $sdk.Replace('\\', '/')
$ndkProp = $ndk.Replace('\\', '/')
@("sdk.dir=$sdkProp", "ndk.dir=$ndkProp") | Set-Content -Encoding ASCII (Join-Path $root "local.properties")

Push-Location $root
try {
    & $gradle --no-daemon :app:assembleDebug
    if ($LASTEXITCODE -ne 0) { throw "Gradle build failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

$apk = Join-Path $root "app\build\outputs\apk\debug\app-debug.apk"
if (-not (Test-Path $apk)) { throw "APK was not produced: $apk" }
Write-Host "APK: $apk"
