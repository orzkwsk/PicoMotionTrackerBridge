param(
    [string]$Jdk = "C:\Program Files\Unity\Hub\Editor\2022.3.22f1\Editor\Data\PlaybackEngines\AndroidPlayer\OpenJDK"
)

$ErrorActionPreference = "Stop"
$javac = Join-Path $Jdk "bin\javac.exe"
$java = Join-Path $Jdk "bin\java.exe"
if (-not (Test-Path -LiteralPath $javac) -or -not (Test-Path -LiteralPath $java)) {
    throw "Java compiler/runtime not found under $Jdk. Supply -Jdk <JDK directory>."
}
$root = $PSScriptRoot
$classes = Join-Path $root "app\build\lifecycle-tests"
$sources = Join-Path $root "app\src\main\java\com\orz\picootbackendtest"
$test = Join-Path $root "tests\com\orz\picootbackendtest\BridgeLifecycleTest.java"
New-Item -ItemType Directory -Force -Path $classes | Out-Null
& $javac --release 11 -d $classes (Join-Path $sources "StartResult.java") (Join-Path $sources "BridgeLifecycle.java") $test
if ($LASTEXITCODE -ne 0) { throw "Lifecycle test compilation failed." }
& $java -cp $classes com.orz.picootbackendtest.BridgeLifecycleTest
if ($LASTEXITCODE -ne 0) { throw "Lifecycle tests failed." }
