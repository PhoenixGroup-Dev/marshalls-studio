<#
  Builds and installs libobs + OBS core plugins (no browser/websocket/scripting)
  from the official obs-studio sources into deps\obs-install.

  The OBS CMake preset "windows-x64" downloads the matching prebuilt
  obs-deps (FFmpeg, x264, mbedTLS, ...) and Qt6 into deps\obs-studio\.deps;
  Marshalls Studio is later built against that same Qt.

  Requirements: Visual Studio 2022 (Desktop C++), CMake >= 3.24, Git.
  Usage:  powershell -ExecutionPolicy Bypass -File scripts\windows\build-obs.ps1 [-ObsVersion 30.2.3]
#>
param(
    [string]$ObsVersion = "30.2.3",
    [string]$Config = "RelWithDebInfo"
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Deps = Join-Path $Root "deps"
$Src = Join-Path $Deps "obs-studio"
$Prefix = Join-Path $Deps "obs-install"
New-Item -ItemType Directory -Force -Path $Deps | Out-Null

if (-not (Test-Path $Src)) {
    git clone --depth 1 --branch $ObsVersion --recursive --shallow-submodules https://github.com/obsproject/obs-studio.git $Src
    if ($LASTEXITCODE -ne 0) { throw "git clone falhou" }
}

Push-Location $Src
try {
    cmake --preset windows-x64 `
        -DENABLE_BROWSER=OFF -DENABLE_WEBSOCKET=OFF -DENABLE_SCRIPTING=OFF `
        -DENABLE_VLC=OFF -DENABLE_AJA=OFF -DENABLE_VIRTUALCAM=OFF -DENABLE_WEBRTC=OFF `
        -DENABLE_NEW_MPEGTS_OUTPUT=OFF -DENABLE_UPDATER=OFF
    if ($LASTEXITCODE -ne 0) { throw "configuração do OBS falhou" }
    cmake --build build_x64 --config $Config --parallel
    if ($LASTEXITCODE -ne 0) { throw "compilação do OBS falhou" }
    cmake --install build_x64 --prefix $Prefix --config $Config
    if ($LASTEXITCODE -ne 0) { throw "instalação do OBS falhou" }
} finally {
    Pop-Location
}
Write-Host "libobs $ObsVersion instalado em $Prefix"
