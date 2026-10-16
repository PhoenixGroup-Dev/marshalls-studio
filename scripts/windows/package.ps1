<#
  Assembles a relocatable install tree in dist\MarshallsStudio that mirrors the
  OBS layout expected by libobs:
     bin\64bit\MarshallsStudio.exe, obs.dll, FFmpeg/x264 DLLs, Qt runtime
     obs-plugins\64bit\<allowed core plugins>.dll
     data\libobs, data\obs-plugins\<plugin>
  Only the plugins the application loads are shipped (no OBS frontend plugins).
#>
param(
    [string]$Config = "RelWithDebInfo",
    [string]$Build,
    [string]$QtDir
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$Prefix = Join-Path $Root "deps\obs-install"
$Dist = Join-Path $Root "dist\MarshallsStudio"
$Bin = Join-Path $Dist "bin\64bit"

$Plugins = @("image-source", "obs-ffmpeg", "obs-filters", "obs-outputs", "obs-transitions", "obs-x264",
             "win-dshow", "win-wasapi", "win-capture", "obs-qsv11", "obs-nvenc", "enc-amf", "obs-text")

if (Test-Path $Dist) { Remove-Item -Recurse -Force $Dist }
New-Item -ItemType Directory -Force -Path $Bin, "$Dist\obs-plugins\64bit", "$Dist\data" | Out-Null

# libobs runtime (DLLs and helper executables such as obs-ffmpeg-mux.exe)
Get-ChildItem "$Prefix\bin\64bit" -File | Where-Object {
    $_.Name -notmatch '^obs64\.(exe|pdb)$' -and $_.Name -notmatch '^Qt6' -and $_.Name -notmatch 'frontend'
} | Copy-Item -Destination $Bin

# Prebuilt dependency DLLs (FFmpeg, x264, librist, srt, mbedTLS, ...) that libobs
# and its plugins load; cmake --install does not copy all of them.
$DotDeps = Join-Path $Root "deps\obs-studio\.deps"
$ObsDeps = Get-ChildItem $DotDeps -Directory -Filter "obs-deps-20*-x64" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($ObsDeps -and (Test-Path "$($ObsDeps.FullName)\bin")) {
    Get-ChildItem "$($ObsDeps.FullName)\bin" -Filter "*.dll" | Where-Object { -not (Test-Path (Join-Path $Bin $_.Name)) } |
        Copy-Item -Destination $Bin
}

Copy-Item "$Build\src\app\$Config\MarshallsStudio.exe" $Bin
if (Test-Path "$Build\src\app\$Config\MarshallsStudio.pdb") { Copy-Item "$Build\src\app\$Config\MarshallsStudio.pdb" $Bin }

# Plugins and their data
foreach ($p in $Plugins) {
    $dll = "$Prefix\obs-plugins\64bit\$p.dll"
    if (Test-Path $dll) {
        Copy-Item $dll "$Dist\obs-plugins\64bit\"
        if (Test-Path "$Prefix\data\obs-plugins\$p") {
            Copy-Item -Recurse "$Prefix\data\obs-plugins\$p" "$Dist\data\obs-plugins\$p"
        }
    } else {
        Write-Warning "Plugin $p não encontrado (ignorado)"
    }
}
Copy-Item -Recurse "$Prefix\data\libobs" "$Dist\data\libobs"

# Qt runtime: windeployqt when available, otherwise copy the modules used by
# the app (the Qt bundled with obs-deps does not ship windeployqt).
$windeployqt = "$QtDir\bin\windeployqt.exe"
if (Test-Path $windeployqt) {
    & $windeployqt --release --no-system-d3d-compiler --no-opengl-sw "$Bin\MarshallsStudio.exe"
    if ($LASTEXITCODE -ne 0) { throw "windeployqt falhou" }
} else {
    foreach ($m in @("Qt6Core", "Qt6Gui", "Qt6Widgets", "Qt6Network", "Qt6Svg")) {
        if (Test-Path "$QtDir\bin\$m.dll") { Copy-Item "$QtDir\bin\$m.dll" $Bin }
    }
    foreach ($dir in @("platforms", "styles", "imageformats", "iconengines")) {
        if (Test-Path "$QtDir\plugins\$dir") {
            New-Item -ItemType Directory -Force -Path "$Bin\$dir" | Out-Null
            Get-ChildItem "$QtDir\plugins\$dir" -Filter "*.dll" | Where-Object { $_.Name -notmatch "d\.dll$" } |
                Copy-Item -Destination "$Bin\$dir"
        }
    }
    if (-not (Test-Path "$Bin\platforms\qwindows.dll")) { throw "qwindows.dll não encontrado em $QtDir\plugins\platforms" }
    New-Item -ItemType Directory -Force -Path "$Bin\translations" | Out-Null
    Get-ChildItem "$QtDir\translations" -Filter "qtbase_pt_BR.qm" -ErrorAction SilentlyContinue | Copy-Item -Destination "$Bin\translations"
}

# MSVC runtime (app-local deployment)
if ($env:VCToolsRedistDir) {
    $crt = Get-ChildItem "$env:VCToolsRedistDir\x64" -Directory -Filter "Microsoft.VC*.CRT" | Select-Object -First 1
    if ($crt) { Copy-Item "$($crt.FullName)\*.dll" $Bin }
}

# Verify that every DLL imported by the executable, libobs and the plugins is
# shipped or provided by Windows (a missing one stops the app before main()).
$dumpbin = (Get-Command dumpbin.exe -ErrorAction SilentlyContinue).Source
if ($dumpbin) {
    $shipped = @{}
    Get-ChildItem $Dist -Recurse -Include *.dll, *.exe | ForEach-Object { $shipped[$_.Name.ToLower()] = $true }
    $missing = @()
    $binaries = @(Get-ChildItem $Bin -Include *.exe, *.dll -Recurse) + @(Get-ChildItem "$Dist\obs-plugins\64bit" -Filter *.dll)
    foreach ($b in $binaries) {
        $deps = & $dumpbin /nologo /dependents $b.FullName | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim().ToLower() }
        foreach ($d in $deps) {
            if ($shipped.ContainsKey($d) -or $d -like "api-ms-*" -or $d -like "ext-ms-*" -or (Test-Path "$env:WINDIR\System32\$d")) { continue }
            $missing += "$($b.Name) -> $d"
        }
    }
    if ($missing.Count -gt 0) { throw "DLLs ausentes no pacote: $($missing -join '; ')" }
    Write-Host "Verificação de dependências: todas as DLLs importadas estão presentes."
} else {
    Write-Warning "dumpbin não encontrado: verificação de dependências não executada"
}

Copy-Item "$Root\LICENSE" "$Dist\LICENSE.txt"
Copy-Item "$Root\docs\THIRD_PARTY.md" "$Dist\THIRD_PARTY.txt"
Write-Host "Distribuição montada em $Dist"
