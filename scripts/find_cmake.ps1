function Find-InstalledCMake {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if (-not $root) { continue }
        $standalone = Join-Path $root 'CMake\bin\cmake.exe'
        if (Test-Path -LiteralPath $standalone -PathType Leaf) { return $standalone }
        $pattern = Join-Path $root 'Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        $candidate = Get-ChildItem -Path $pattern -File -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
        if ($candidate) { return $candidate.FullName }
    }
    throw 'CMake was not found. Add cmake.exe to PATH or install Visual Studio CMake support.'
}
