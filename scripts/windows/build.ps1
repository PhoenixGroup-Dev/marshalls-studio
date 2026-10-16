<#
  Builds Marshalls Studio against deps\obs-install (see build-obs.ps1), runs
  the hardware-independent tests and assembles dist\MarshallsStudio.

  Usage:  powershell -ExecutionPolicy Bypass -File scripts\windows\build.ps1 [-Config RelWithDebInfo] [-SkipTests]
#>
param(
    [string]$Config = "RelWithDebInfo",
    [switch]$SkipTests
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Deps = Join-Path $Root "deps"
$Prefix = Join-Path $Deps "obs-install"
$DotDeps = Join-Path $Deps "obs-studio\.deps"
$Build = Join-Path $Root "build-win"

$LibobsConfig = Get-ChildItem $Prefix -Recurse -Filter "libobsConfig.cmake" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $LibobsConfig) {
    $listing = (Get-ChildItem $Prefix -Recurse -Depth 2 -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName.Substring($Prefix.Length) }) -join " | "
    throw "libobsConfig.cmake não encontrado em $Prefix (execute build-obs.ps1). Conteúdo: $listing"
}
Write-Host "libobs: $($LibobsConfig.FullName)"
$Qt = Get-ChildItem $DotDeps -Directory -Filter "obs-deps-qt6-*-x64" | Select-Object -First 1
$ObsDeps = Get-ChildItem $DotDeps -Directory -Filter "obs-deps-20*-x64" | Select-Object -First 1
if (-not $Qt) { throw "Qt6 do obs-deps não encontrado em $DotDeps" }

$PrefixPath = "$Prefix;$Prefix\cmake;$($Qt.FullName);$($ObsDeps.FullName)"
cmake -S $Root -B $Build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$PrefixPath" -DMS_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "configuração falhou" }
cmake --build $Build --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "compilação falhou" }

if (-not $SkipTests) {
    $env:PATH = "$($Qt.FullName)\bin;$env:PATH"
    ctest --test-dir $Build -C $Config -LE integration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "testes falharam" }
}

& "$PSScriptRoot\package.ps1" -Config $Config -Build $Build -QtDir $Qt.FullName
