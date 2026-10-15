# Arquitetura do Marshalls Studio

## 1. Decisão de tecnologia

| Requisito | Escolha | Justificativa |
|---|---|---|
| Captura, composição, filtros, transições, codificação, gravação e transmissão | **libobs** (motor do OBS Studio), embarcado no processo | Infraestrutura madura e testada em produção para DirectShow/WASAPI/DXGI/WGC no Windows, V4L2/PulseAudio/X11 no Linux, FFmpeg, x264, Quick Sync/NVENC/AMF e RTMP/RTMPS. Reimplementar isso levaria anos. |
| Interface | **C++17 + Qt 6 Widgets** | Mesmo toolkit e ABI usados pelo OBS (o build do libobs no Windows já baixa o Qt 6 correspondente), widgets nativos, baixo consumo, integração direta com janelas nativas onde o libobs desenha. |
| Por que não Electron/web | — | O libobs desenha em janelas nativas (HWND/X11) e entrega áudio/vídeo em C; uma camada web exigiria ponte IPC de vídeo (cópias de quadros), dobraria o consumo de memória e complicaria a distribuição. Para o equipamento de referência (i5-10210U, Intel UHD) isso é custo sem benefício. |

Versões: código validado contra **libobs 30.2.3** (Ubuntu 24.04 backports). O script Windows compila exatamente essa tag por padrão (`-ObsVersion` permite outra). A versão upstream atual é 32.2.x; a migração deve ser feita revalidando a suíte de integração (ver "Riscos").

### Licença (importante para o negócio)
O libobs é **GPL-2.0-or-later**. Um executável que o incorpora só pode ser distribuído sob termos compatíveis com a GPL (código-fonte disponível para quem recebe o binário). Por isso o repositório inclui `LICENSE` (GPL-2.0). x264 também é GPL. Se a distribuição comercial fechada for requisito, a arquitetura teria de trocar o motor (ex.: GStreamer/FFmpeg próprio) — decisão do produto, não técnica.

## 2. Camadas

```
src/core    (QtCore/QtGui, sem libobs)  modelo de dados, persistência + migração,
                                         máquina de estados do programa, atalhos,
                                         validação de transmissão, layouts, segurança
                                         de áudio, logs com redação de segredos
src/engine  (libobs)                     Engine (fachada libobs), OutputManager,
                                         ThumbnailService, LowerThird (fonte libobs
                                         própria), RtmpService, ObsPlatform,
                                         Studio (camada de aplicação)
src/ui      (Qt Widgets)                 mesa de direção, grade, mixer, diálogos
src/app                                   main: logs, instância única, sentinela de
                                         falha, tradução, sinais, ciclo de vida
```

A interface **nunca** chama o libobs para ações do operador: tudo passa por `Studio`, a mesma API que os testes de integração usam. Assim, cada botão é exercitado pelo mesmo caminho testado.

## 3. Threads

| Thread | Dono | Função |
|---|---|---|
| UI (principal) | Qt | Interface, `Studio`, comandos ao libobs (rápidos) |
| `libobs: graphics` | libobs | Composição, render dos displays, miniaturas (`ThumbnailService` roda dentro do loop de render) |
| `video-io` / `audio-io` | libobs | Saída de vídeo/áudio, mixagem, encoders |
| Captura | plugins | DirectShow, WASAPI, V4L2, PulseAudio, FFmpeg (mídia) |
| `ms-device-watcher` | Marshalls | Enumeração periódica (3 s) de câmeras e dispositivos de áudio, com COM inicializado no Windows — enumerar DirectShow pode levar centenas de ms e nunca bloqueia a UI |
| Remux MKV→MP4 | Marshalls | `media_remux` em thread separada |

Callbacks do libobs (fim de transição, fim de mídia, sinais de saída) chegam em threads do libobs e são reenviados à UI com `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Níveis de áudio usam atômicos lidos pela UI a 30 Hz.

Regra de bloqueio: nunca segurar um mutex próprio enquanto se entra no contexto gráfico (`obs_enter_graphics`) — a thread de render segura o contexto e pode esperar o mesmo mutex (evitado no `ThumbnailService`).

## 4. Grafo de mídia

Canais de saída do libobs (renderizados de baixo para cima):

| Canal | Conteúdo |
|---|---|
| 0 | **Barramento de programa**: uma transição (cut/fade/slide/swipe) cuja fonte ativa é uma *cena solo* (uma fonte da grade ajustada ao canvas) ou uma *composição* |
| 1 | **Camada gráfica**: cena com lower thirds e logotipos — independe dos cortes |
| 2 | **Fade para preto**: transição fade entre uma cena vazia e um cartão preto, cobrindo tudo |
| 10–63 | Entradas de áudio independentes (microfones, áudio do computador) |

Consequências:
- Colocar uma fonte individual no ar **não altera** nenhuma composição: cada fonte tem sua cena solo própria; composições referenciam as mesmas fontes.
- Gráficos permanecem no ar durante cortes e transições e entram/saem com animação própria.
- FTB escurece toda a saída (inclusive gráficos) e volta, sem perder o estado do programa.

### Máquina de estados do programa (`ProgramController`)
Estado: selecionada (verde), no ar (vermelha), anterior, alvo pendente, transição em andamento, estado do FTB. Regras testadas: corte/transição rejeitados durante transição ou FTB em curso; "já está no ar" não faz nada; remover a fonte no ar deixa a saída em preto e avisa; "voltar à fonte anterior" ao fim de uma vinheta.

### Miniaturas
`ThumbnailService` renderiza cada fonte numa textura pequena (padrão 320×180, 10 qps, configuráveis) dentro do loop gráfico, distribui as fontes entre quadros (custo constante por quadro) e faz *readback* em pipeline (mapeia no passe seguinte, sem travar a GPU). O mesmo mecanismo captura a textura principal do programa — usado nos testes para verificar pixel a pixel o que vai ao ar.

### Monitor de programa
`obs_display` desenhando a textura principal em uma janela nativa filha (HWND/X11). No X11/EGL o libobs cria sua janela de swapchain com a geometria do pai no momento da criação e só a reconfigura se o tamanho solicitado mudar; o monitor cria o display com tamanho provisório e aplica o real em seguida (bug encontrado e corrigido durante a validação).

### Gráficos (lower thirds, tarjas, títulos)
`LowerThirdRenderer` rasteriza o modelo com Qt (tipografia, logo, cores) em RGBA pré-multiplicado. Uma fonte libobs própria (`marshalls_graphic`) envia a textura e executa a animação de entrada/permanência/saída **na thread de vídeo, quadro a quadro** (deslizar+fade, revelar, fade, corte), com shader próprio de opacidade. A pré-visualização do editor usa o mesmo rasterizador: o que se vê no editor é o que vai ao ar.

### Áudio
- Cada entrada tem: volume (fader com curva de broadcast), mute, atraso de sincronização, monitoramento (desligado / só monitorar / monitorar e enviar), filtros (ganho → supressão de ruído → gate → EQ 3 bandas → compressor → limitador, em ordem fixa), roteamento para o programa (trilha 1) e trilha isolada (trilhas 2–6).
- O mesmo dispositivo não pode ser capturado duas vezes; monitorar enquanto o áudio do computador é capturado gera aviso de risco de eco.
- Medidores por canal (pós-fader, pré-mute — o libobs mostra o sinal mesmo com mute; a UI acinzenta e marca "MUDO") e medidor **PGM** calculado do próprio mix de programa.
- **Correção de bug do libobs 30**: mudar o atraso de sincronização **a partir de 0** com áudio fluindo não tem efeito (o libobs continua emendando os pacotes). O motor aplica 1 ns e, no pacote seguinte, o valor-alvo. Validado medindo o deslocamento entre duas entradas com o mesmo sinal.

### Saídas
- Gravação: `ffmpeg_muxer` (MKV padrão; MP4/MOV opcionais), codificador escolhido (Quick Sync/NVENC/AMF/x264, automático = primeiro de hardware disponível), CRF/CBR/VBR, trilha 1 = programa + trilhas isoladas por microfone, verificação de espaço antes e durante (encerra abaixo do mínimo), remux opcional para MP4 ao final.
- Transmissão: serviço RTMP/RTMPS próprio (`marshalls_rtmp`) + `rtmp_output`, reconexão automática configurável, **atraso intencional** via `obs_output_set_delay` (somente transmissão, sobre dados codificados — memória ≈ bitrate × atraso), estatísticas (kb/s efetivo, quadros perdidos, congestionamento, atraso ativo).
- O plugin `rtmp-services` do OBS **não é carregado**: ele baixa catálogo remoto ao iniciar e travou (double free) ao descarregar na versão testada.

## 5. Plugins carregados (lista de permissão)
Somente plugins de núcleo; nunca os de frontend do OBS (que exigem a UI do OBS e abortam o processo):
`image-source, obs-ffmpeg, obs-filters, obs-outputs, obs-transitions, obs-x264` + Windows: `win-dshow, win-wasapi, win-capture, obs-qsv11, obs-nvenc, enc-amf, obs-text` + Linux: `linux-capture, linux-pulseaudio, linux-v4l2, linux-pipewire, text-freetype2`. Cada módulo é carregado uma única vez (o libobs também varre caminhos internos; carregar em dobro corrompia o estado do `linux-capture`).

## 6. Dispositivos
- Enumeração por propriedades dos próprios plugins; para câmeras/telas usa-se uma **instância ociosa** (alguns plugins desreferenciam a instância e travam quando consultados só pelo tipo). No Linux as câmeras são enumeradas diretamente por V4L2 (`VIDIOC_QUERYCAP`), com IDs **`/dev/v4l/by-id/...`** — estáveis entre reinícios e portas USB.
- Identificadores estáveis são persistidos junto com o nome; se o ID sumir e um dispositivo com o mesmo nome aparecer, a fonte é reassociada automaticamente.
- Saúde por fonte (consulta a cada 500 ms, sem bloquear): presença do dispositivo, chegada de quadros (filtro passivo `marshalls_frame_probe` conta quadros assíncronos), pacotes de áudio, estado da mídia. Estados: OK, Iniciando, Sem sinal, Desconectado, Erro, Indisponível. Botão "Reconectar" reaplica as configurações (no DirectShow, desativa/ativa).

## 7. Persistência e confiabilidade
- Perfis JSON versionados (`schema_version`), gravação atômica (`QSaveFile`), **nunca regrava arquivo idêntico**, mantém `.bak` da versão válida anterior, quarentena de arquivo corrompido com recuperação pelo `.bak`, migração passo a passo com cópia `.vN.bak` do original, cópia preservada ao abrir perfil de versão mais nova.
- Salvamento automático 800 ms após cada alteração.
- Chave de transmissão fora do perfil: DPAPI (usuário) no Windows; arquivo 0600 no Linux (sem criptografia — limitação documentada). Toda linha de log passa por um redator que remove a chave e parâmetros `key=`/`token=`.
- Logs rotativos (10 sessões, 32 MB máx. por sessão), libobs e Qt redirecionados.
- Sentinela de sessão: após encerramento inesperado o operador é avisado no próximo início. MKV interrompido permanece reproduzível (validado com `abort()` durante a gravação).
- Instância única por configuração (`QLockFile`).
- Linux: SIGTERM/SIGINT encerram pelo caminho normal (saídas finalizadas, configuração salva).

## 8. Riscos técnicos conhecidos
1. **Build Windows não executado neste ambiente** (Linux, sem MSVC/DirectX). Scripts e CI estão prontos; a primeira execução real pode exigir ajustes de compilação.
2. Plugins Windows (DirectShow/WASAPI/DXGI/WGC/Quick Sync) não foram exercitados aqui; o código só usa APIs públicas comuns ao Linux, mas o comportamento de reconexão de dispositivos depende do plugin.
3. Atualizar para libobs 31/32 pode alterar assinaturas (ex.: `obs_service_info`), exigindo revalidação.
4. Desempenho no i5-10210U depende do Quick Sync; x264 em 1080p tende a sobrecarregar esse processador.
