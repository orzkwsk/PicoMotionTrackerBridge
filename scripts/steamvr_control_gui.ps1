[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release',[switch]$BuildOnly)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
Push-Location $taskRoot
try {
    & (Join-Path $PSScriptRoot "build_pc_bridge_service.ps1") -Config $Configuration | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "PC receiver build failed." }
    foreach ($taskProject in @('Control.Service','Control.Wpf')) {
        & dotnet build "ui/$taskProject/$taskProject.csproj" -c $Configuration --disable-build-servers -p:UseSharedCompilation=false -m:1
        if ($LASTEXITCODE -ne 0) { throw 'Control build failed. Install the .NET 8 SDK with Windows desktop support.' }
    }
    if (-not $BuildOnly) {
        $taskService=Join-Path $taskRoot "ui/Control.Service/bin/$Configuration/net8.0-windows/Control.Service.exe"
        $taskGui=Join-Path $taskRoot "ui/Control.Wpf/bin/$Configuration/net8.0-windows/Control.Wpf.exe"
        Start-Process -FilePath $taskService -ArgumentList ('"'+$taskRoot+'"') -WorkingDirectory $taskRoot -WindowStyle Hidden
        Start-Process -FilePath $taskGui -ArgumentList ('"'+$taskRoot+'"') -WorkingDirectory $taskRoot
    }
} finally { Pop-Location }
