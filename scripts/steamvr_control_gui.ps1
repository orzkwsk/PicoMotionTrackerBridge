param()

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$GuiSource = Join-Path $RepoRoot "tools\steamvr_control_wpf.cs"
$GuiMainSource = Join-Path $RepoRoot "tools\steamvr_control_gui_main.cs"
$BuildDir = Join-Path $RepoRoot "build-gui"
$GuiExe = Join-Path $BuildDir "pico_ot_steamvr_control.exe"

foreach ($path in @($GuiSource, $GuiMainSource)) {
    if (-not (Test-Path $path)) {
        throw "SteamVR control GUI source was not found: $path"
    }
}

$cscCandidates = @(
    (Join-Path $env:WINDIR "Microsoft.NET\Framework64\v4.0.30319\csc.exe"),
    (Join-Path $env:WINDIR "Microsoft.NET\Framework\v4.0.30319\csc.exe")
)
$Csc = $cscCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Csc) {
    throw "C# compiler (csc.exe) was not found in the .NET Framework installation."
}

function Get-WpfReferenceSet {
    $referenceRoots = @()
    if (${env:ProgramFiles(x86)}) {
        $referenceRoots += (Join-Path ${env:ProgramFiles(x86)} "Reference Assemblies\Microsoft\Framework\.NETFramework")
    }
    if ($env:ProgramFiles) {
        $referenceRoots += (Join-Path $env:ProgramFiles "Reference Assemblies\Microsoft\Framework\.NETFramework")
    }

    foreach ($root in $referenceRoots | Select-Object -Unique) {
        if (-not (Test-Path $root)) {
            continue
        }

        $versionDirs = Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "v4*" } |
            Sort-Object Name -Descending

        foreach ($dir in $versionDirs) {
            $windowsBase = Join-Path $dir.FullName "WindowsBase.dll"
            $presentationCore = Join-Path $dir.FullName "PresentationCore.dll"
            $presentationFramework = Join-Path $dir.FullName "PresentationFramework.dll"
            $systemXaml = Join-Path $dir.FullName "System.Xaml.dll"
            if ((Test-Path $windowsBase) -and
                (Test-Path $presentationCore) -and
                (Test-Path $presentationFramework) -and
                (Test-Path $systemXaml)) {
                return @{
                    WindowsBase = $windowsBase
                    PresentationCore = $presentationCore
                    PresentationFramework = $presentationFramework
                    SystemXaml = $systemXaml
                    Source = $dir.FullName
                }
            }
        }
    }

    # Fallback for machines that have the runtime but not the Developer Pack
    # reference assemblies. WPF assemblies are split between the Framework
    # root and its WPF subdirectory on some .NET Framework installations.
    $frameworkRoots = @(
        (Join-Path $env:WINDIR "Microsoft.NET\Framework64\v4.0.30319"),
        (Join-Path $env:WINDIR "Microsoft.NET\Framework\v4.0.30319")
    )

    foreach ($root in $frameworkRoots) {
        if (-not (Test-Path $root)) {
            continue
        }

        $wpfDir = Join-Path $root "WPF"
        $windowsBaseCandidates = @(
            (Join-Path $root "WindowsBase.dll"),
            (Join-Path $wpfDir "WindowsBase.dll")
        )
        $presentationCoreCandidates = @(
            (Join-Path $root "PresentationCore.dll"),
            (Join-Path $wpfDir "PresentationCore.dll")
        )
        $presentationFrameworkCandidates = @(
            (Join-Path $root "PresentationFramework.dll"),
            (Join-Path $wpfDir "PresentationFramework.dll")
        )
        $systemXamlCandidates = @(
            (Join-Path $root "System.Xaml.dll"),
            (Join-Path $wpfDir "System.Xaml.dll")
        )

        $windowsBase = $windowsBaseCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
        $presentationCore = $presentationCoreCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
        $presentationFramework = $presentationFrameworkCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
        $systemXaml = $systemXamlCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

        if ($windowsBase -and $presentationCore -and $presentationFramework -and $systemXaml) {
            return @{
                WindowsBase = $windowsBase
                PresentationCore = $presentationCore
                PresentationFramework = $presentationFramework
                SystemXaml = $systemXaml
                Source = $root
            }
        }
    }

    throw "WPF .NET Framework reference assemblies were not found. Install the .NET Framework 4.8 Developer Pack (or Visual Studio .NET desktop development workload)."
}

$WpfRefs = Get-WpfReferenceSet

$needsBuild = -not (Test-Path $GuiExe)
if (-not $needsBuild) {
    $exeTime = (Get-Item $GuiExe).LastWriteTimeUtc
    foreach ($source in @($GuiSource, $GuiMainSource, $PSCommandPath)) {
        if ((Get-Item $source).LastWriteTimeUtc -gt $exeTime) {
            $needsBuild = $true
            break
        }
    }
}

if ($needsBuild) {
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

    Write-Host "WPF references: $($WpfRefs.Source)"

    $compilerArgs = @(
        "/nologo",
        "/target:winexe",
        "/optimize+",
        "/out:$GuiExe",
        "/reference:System.dll",
        "/reference:System.Core.dll",
        "/reference:$($WpfRefs.WindowsBase)",
        "/reference:$($WpfRefs.PresentationCore)",
        "/reference:$($WpfRefs.PresentationFramework)",
        "/reference:$($WpfRefs.SystemXaml)",
        $GuiSource,
        $GuiMainSource
    )

    & $Csc @compilerArgs

    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $GuiExe)) {
        throw "Failed to build WPF SteamVR control GUI executable."
    }
}

Write-Host "WPF GUI: $GuiExe"
Start-Process -FilePath $GuiExe -ArgumentList @($RepoRoot)
