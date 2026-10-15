<#
  Generates the Windows installer from dist\MarshallsStudio with NSIS.
  Requirements: NSIS 3.x (makensis in PATH or default install folder).
#>
param([string]$Version = "")
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Version) {
    $Version = (Select-String -Path "$Root\CMakeLists.txt" -Pattern 'project\(MarshallsStudio VERSION ([0-9.]+)').Matches[0].Groups[1].Value
}
$makensis = (Get-Command makensis -ErrorAction SilentlyContinue).Source
if (-not $makensis) { $makensis = "${env:ProgramFiles(x86)}\NSIS\makensis.exe" }
if (-not (Test-Path $makensis)) { throw "makensis não encontrado. Instale o NSIS 3." }
New-Item -ItemType Directory -Force -Path "$Root\dist" | Out-Null
& $makensis "/DVERSION=$Version" "/DSRCDIR=$Root\dist\MarshallsStudio" "/DOUTFILE=$Root\dist\MarshallsStudio-$Version-Setup-x64.exe" "$Root\installer\windows\marshalls.nsi"
if ($LASTEXITCODE -ne 0) { throw "makensis falhou" }
