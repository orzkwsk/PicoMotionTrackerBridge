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
$javaExe = Join-Path $jdk "bin\java.exe"
$gradleRoot = Join-Path $android "Tools\gradle"
$gradleLib = Join-Path $gradleRoot "lib"
$gradlePlugins = Join-Path $gradleLib "plugins"
$gradleClasspath = "$gradleLib\*;$gradlePlugins\*"

foreach ($path in @($sdk, $ndk, $jdk, $javaExe, $gradleLib, $gradlePlugins)) {
    if (-not (Test-Path $path)) { throw "Required Unity Android tool not found: $path" }
}

function Resolve-Exe {
    param([string]$Name, [string[]]$Fallbacks)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($candidate in $Fallbacks) {
        if (Test-Path $candidate) { return $candidate }
    }
    throw "$Name not found. Install/enable Visual Studio CMake tools or add it to PATH."
}

$cmake = Resolve-Exe "cmake.exe" @(
    "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
$ninja = Resolve-Exe "ninja.exe" @(
    "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
)

$gradleLauncher = Get-ChildItem -Path $gradleLib -Filter "gradle-launcher-*.jar" -File -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $gradleLauncher) { throw "Unity Gradle launcher not found under: $gradleLib" }

$env:JAVA_HOME = $jdk
$env:ANDROID_HOME = $sdk
$env:ANDROID_SDK_ROOT = $sdk
$env:ANDROID_NDK_HOME = $ndk

$sdkProp = $sdk.Replace('\', '/')
$ndkProp = $ndk.Replace('\', '/')
@("sdk.dir=$sdkProp", "ndk.dir=$ndkProp") | Set-Content -Encoding ASCII (Join-Path $root "local.properties")

$nativeBuild = Join-Path $root "native-build"
$nativeSource = Join-Path $root "app\src\main\cpp"
$toolchain = Join-Path $ndk "build\cmake\android.toolchain.cmake"

Write-Host "CMake:  $cmake"
Write-Host "Ninja:  $ninja"
Write-Host "SDK:    $sdk"
Write-Host "NDK:    $ndk"
Write-Host "JDK:    $jdk"
Write-Host "Gradle: $gradleRoot ($($gradleLauncher.Name))"
Write-Host ""
Write-Host "Building native arm64 libraries..."

& $cmake -S $nativeSource -B $nativeBuild -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DANDROID_ABI=arm64-v8a" `
    "-DANDROID_PLATFORM=android-29" `
    "-DPICO_OT_BUILD_TESTS=OFF" `
    "-DPICO_OT_BUILD_SHARED=ON" `
    "-DCMAKE_BUILD_TYPE=Debug"
if ($LASTEXITCODE -ne 0) { throw "Native CMake configure failed with exit code $LASTEXITCODE" }

& $cmake --build $nativeBuild
if ($LASTEXITCODE -ne 0) { throw "Native CMake build failed with exit code $LASTEXITCODE" }

$backendSo = Get-ChildItem $nativeBuild -Recurse -Filter "libpico_ot_backend.so" -File | Select-Object -First 1
$testSo = Get-ChildItem $nativeBuild -Recurse -Filter "libpico_ot_backend_test.so" -File | Select-Object -First 1
if (-not $backendSo) { throw "libpico_ot_backend.so was not produced" }
if (-not $testSo) { throw "libpico_ot_backend_test.so was not produced" }

$jniLibDir = Join-Path $root "app\src\main\jniLibs\arm64-v8a"
New-Item -ItemType Directory -Force -Path $jniLibDir | Out-Null
Copy-Item -Force $backendSo.FullName (Join-Path $jniLibDir "libpico_ot_backend.so")
Copy-Item -Force $testSo.FullName (Join-Path $jniLibDir "libpico_ot_backend_test.so")

Write-Host ""
Write-Host "Building Android APK..."
Push-Location $root
try {
    & $javaExe `
        -classpath $gradleClasspath `
        org.gradle.launcher.GradleMain `
        --no-daemon `
        :app:assembleDebug
    if ($LASTEXITCODE -ne 0) { throw "Gradle build failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

$apk = Join-Path $root "app\build\outputs\apk\debug\app-debug.apk"
if (-not (Test-Path $apk)) { throw "APK was not produced: $apk" }
Write-Host ""
Write-Host "APK: $apk"
