# Compilação, execução e instalador

## Windows 10/11 64 bits (plataforma alvo)

### Requisitos
- Visual Studio 2022 com a carga "Desenvolvimento para desktop com C++" (MSVC v143, Windows SDK 10)
- CMake ≥ 3.24 e Git no `PATH`
- PowerShell 5+ (incluído no Windows)
- NSIS 3 (somente para gerar o instalador): `choco install nsis` ou https://nsis.sourceforge.io
- ~15 GB livres; a primeira compilação do libobs leva 30–60 min

### Passo 1 — libobs e plugins (uma vez por versão)
```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows\build-obs.ps1 -ObsVersion 30.2.3
```
Clona `obsproject/obs-studio` na tag indicada, configura o preset oficial `windows-x64` (que baixa os binários pré-compilados de dependências — FFmpeg, x264, mbedTLS… — **e o Qt 6** em `deps\obs-studio\.deps`), desliga navegador, websocket, scripting, VLC, AJA, câmera virtual, WebRTC e atualizador, compila e instala em `deps\obs-install`.

### Passo 2 — aplicativo, testes e pacote
Em um "Developer PowerShell for VS 2022" (para o runtime MSVC ser copiado):
```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows\build.ps1 -Config RelWithDebInfo
```
- Compila contra `deps\obs-install` e o **mesmo Qt** baixado pelo OBS (ABI idêntica).
- Executa os testes que não dependem de hardware (`ctest -LE integration`).
- Monta `dist\MarshallsStudio` no layout que o libobs espera:
  `bin\64bit\MarshallsStudio.exe` (+ obs.dll, FFmpeg, Qt, runtime MSVC), `obs-plugins\64bit\` (apenas os plugins da lista de permissão), `data\libobs`, `data\obs-plugins\<plugin>`.

Executar: `dist\MarshallsStudio\bin\64bit\MarshallsStudio.exe`
Opções: `--profile "<nome>"`, `--portable` (configuração em `config\` ao lado do executável), `--verbose`.

### Passo 3 — instalador
```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows\make-installer.ps1
```
Gera `dist\MarshallsStudio-<versão>-Setup-x64.exe` (NSIS): instala em `Program Files\Marshalls Studio`, atalhos no menu Iniciar e na área de trabalho, desinstalador registrado. Exige Windows 10+ 64 bits. A desinstalação preserva configurações (`%APPDATA%\Marshalls\Marshalls Studio`) e gravações.

### CI
`.github/workflows/windows.yml` executa os três passos em `windows-2022`, guarda o libobs em cache e publica o instalador como artefato.

> Status honesto: os scripts e o workflow foram escritos e revisados, e as chamadas de API exclusivas do Windows (DPAPI, `SetWindowDisplayAffinity`, COM, `_wfopen`, recurso `.rc`) foram compiladas com MinGW; **o build MSVC completo ainda não foi executado** (o ambiente de desenvolvimento é Linux). A primeira execução no Windows/CI é o próximo passo de validação.

## Linux (desenvolvimento e bancada de testes)

Ubuntu 24.04:
```bash
sudo apt-get install -t noble-backports libobs-dev obs-studio
sudo apt-get install cmake ninja-build g++ qt6-base-dev qt6-base-dev-tools qt6-translations-l10n \
     libgl1-mesa-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/src/app/MarshallsStudio
```
Em sessões Wayland o aplicativo roda via XWayland (o libobs desenha em janelas X11).

### Testes
```bash
ctest --test-dir build -LE integration --output-on-failure      # unitários (sem hardware)

# integração com o motor real: Xvfb, PulseAudio com entradas virtuais, nginx-rtmp
sudo apt-get install xvfb x11-xserver-utils pulseaudio pulseaudio-utils nginx-light libnginx-mod-rtmp ffmpeg
bash scripts/linux/test-env.sh start
DISPLAY=:99 QTEST_DISABLE_STACK_DUMP=1 ./build/tests/engine/tst_engine
bash scripts/linux/test-env.sh stop
```
`MS_LOG_STDERR=1` mostra os logs do motor durante os testes.
