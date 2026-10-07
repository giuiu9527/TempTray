# Build TempTray with the C# compiler that ships with Windows (no Visual Studio needed).
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here
if (-not (Test-Path .\LibreHardwareMonitorLib.dll)) {
    Write-Error "Put LibreHardwareMonitorLib.dll (net472 build, e.g. 0.9.4) next to this script. See README."
    exit 1
}
$csc = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
& $csc /nologo /target:winexe /out:TempTray.exe /win32manifest:app.manifest `
    /r:LibreHardwareMonitorLib.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll TempTray.cs
if ($LASTEXITCODE -eq 0) { Write-Host "Built TempTray.exe" }
