# Relatório de validação

Data: 2026-10-15 · Versão 0.1.0 · libobs 30.2.3 · Qt 6.4.2

## Bancada
Ubuntu 24.04 em contêiner, 4 vCPU, 15 GB RAM, **sem GPU** (OpenGL 4.5 por Mesa llvmpipe), Xvfb 1920×1080, sem câmeras nem microfones físicos. Substitutos reais usados:
- **Microfones:** entradas PulseAudio criadas a partir de tons tocados em tempo real (`ffmpeg -re`) — A = 440 Hz, B = 880 Hz; C e D recebem os mesmos bipes do mesmo gerador (para medir atraso relativo).
- **Câmeras:** arquivos de vídeo, cores sólidas e captura da tela X11; uma câmera V4L2 configurada para um dispositivo inexistente (cenário de desconexão).
- **Plataforma de transmissão:** servidor nginx-rtmp local que grava cada stream recebido.

Tudo o que não pôde ser exercitado aqui está listado em `HARDWARE_TEST_PLAN.md` e `LIMITATIONS.md`.

## 1. Testes unitários — `tests/core/tst_core.cpp`
**30 aprovados, 0 reprovados.** Cobrem: ida e volta do modelo JSON; saneamento de valores inválidos; remoção de itens de cena órfãos; persistência que não regrava arquivo idêntico; recuperação de perfil corrompido pelo `.bak` com quarentena; migração v1→v2 com cópia do original; preservação de perfil de versão mais nova; renomear/remover perfil; detecção de dispositivo duplicado; nomes únicos; máquina de estados do programa (corte, transição, rejeição durante transição/FTB, "já está no ar", ciclo do FTB, remoção do que está no ar, voltar ao anterior); atalhos (padrões sem conflito, conflito desativa ambos, combinações proibidas, ida e volta); redação de segredos; rotação de logs e chave ausente do log; armazenamento de segredo; estimativa de disco; validação de transmissão; layouts dentro do canvas; avisos de eco/duplicação; curva do fader monotônica e inversível.

## 2. Integração com o motor real — `tests/engine/tst_engine.cpp`
**23 aprovados, 0 reprovados** (execução completa em 99 s). Cada asserção inspeciona a saída real.

| Teste | Requisito | Verificação e resultado medido |
|---|---|---|
| t01 | Detectar dispositivos | Microfones `ms_mic_a…d` listados; 1 tela; enumeração de câmeras concluída (0 na bancada) |
| t02 | Adicionar fontes | 7 fontes OK; tela 1920 px de largura; captura duplicada do mesmo microfone **recusada** com mensagem |
| t03 | Miniaturas reais, captura de tela | Miniaturas com as cores corretas; janela real pintada no X: **x11grab rgb(42,85,153) = miniatura do motor rgb(42,85,153)**; miniatura de vídeo continua atualizando; 256×144 |
| t04 | Troca altera a saída | Pixel central do programa muda para a cor da fonte após CORTE; repetir no que já está no ar é recusado |
| t05 | Transição real | Fade de 1,5 s: pixel no meio = **rgb(98,137,32)** (mistura vermelho/verde, progresso 0,39); comando concorrente recusado; fonte final correta |
| t06 | Fade para preto | Saída preta (≤12) e retorno à fonte |
| t07 | Composição não destrói fontes | Lado a lado: metade esquerda vermelha, direita verde; miniatura da fonte individual intacta; edição no ar aplicada atomicamente |
| t08 | Lower third na saída | Cor de destaque presente na região do gráfico; permanece após corte; sai ao remover; sai sozinho após a duração |
| t09 | Logotipo | Aparece e é removido por "Limpar tudo" |
| t10 | Dois microfones independentes | Mix do programa (Goertzel): 440 Hz = 0,0312 e 880 Hz = 0,0312; **mute A → 440 Hz = 1,9e-16**, 880 Hz intacto; **fader B −20 dB → razão 0,0999997**; cadeia de filtros na ordem ganho→supressão→gate→EQ→compressor→limitador |
| t11 | Gravar trocando fontes | MKV de 9,3 s gravado com cortes, transição, composição e gráfico; **1 vídeo + 3 áudios** (programa + 2 isolados); decodifica sem erros; volumes −24,1/−27,1/−27,1 dB |
| t12 | Sincronia | Cadeia A/V (clipe com flash+bipe): **−31,6 ms** (CI aceita ≤ 3 quadros sem GPU); atraso de vídeo 200 ms: deslocamento **142,7 ms** medido (CI: 100–300 ms); atraso de áudio por entrada +200 ms: deslocamento **178,7 ms** entre duas entradas com o mesmo sinal (tolerância ±25 ms) |
| t13 | Transmissão real | 1365 kb/s, 0 de 153 quadros perdidos; servidor recebeu FLV de 5,8 s com H.264+AAC; **chave ausente de todos os logs e do perfil** |
| t14 | Atraso da transmissão | Atraso ativo = 3 s; primeiros dados após 4,1 s |
| t15 | Câmera ausente | "Desconectado — Dispositivo 'Webcam USB' não encontrado"; UI nunca parou mais de **21 ms**; no ar mostra preto; remover a fonte no ar → programa vazio |
| t16 | Microfone desconectado | Fonte removida do sistema → "Desconectado"; o outro microfone segue OK; ao voltar + Reconectar → sinal −24 dB; UI ≤ 25 ms |
| t17 | Mídia inválida | Arquivo corrompido → "Não foi possível decodificar…"; inexistente → "Arquivo não encontrado" |
| t18 | Falta de disco | Início recusado abaixo do mínimo; durante a gravação, ao cruzar o limite a gravação **encerra sozinha** com aviso e o arquivo parcial decodifica sem erro |
| t19 | CPU sobrecarregada | CPU 93,5%, compositor perdendo 61% dos quadros (sinalizado), trocas e gravação continuam; UI ≤ 49 ms |
| t20 | Queda abrupta | Processo filho morto com `abort()` durante a gravação: **MKV de 3,8 s reproduzível** |
| t21 | Reinício | Motor desligado e religado no mesmo processo: perfil, fontes, cenas, gráficos, logo, transição, chave, volume (−12 dB) e mute restaurados; composição volta ao ar corretamente |

### Falhas encontradas pelos testes e corrigidas
1. `linux-v4l2` e `xshm` travavam quando consultados sem instância → enumeração por instância ociosa / V4L2 direto com IDs `/dev/v4l/by-id`.
2. Instância Xlib usada fora da thread que a criou → crash no `XCloseDisplay` → enumeração de telas na thread da UI.
3. Plugins carregados duas vezes → deduplicação.
4. `rtmp-services` com double free ao descarregar → serviço RTMP próprio.
5. Sync offset de áudio ignorado ao sair de 0 (libobs) → aplicação em dois passos; validado em t12.
6. Diretório de gravação do servidor de teste sem permissão para o usuário do nginx (ambiente).
7. Gerador de áudio de teste com pacotes de 286 ms causava mix silencioso (artefato do teste, substituído por entradas em tempo real).

## 3. Validação manual da interface (aplicativo real sob Xvfb)
Perfil de demonstração com 2 clipes, captura de tela, vinheta, imagem, câmera ausente, 2 microfones, composição, 3 identificações e 1 logo. Operado por mouse e teclado via `xdotool`, verificado por capturas da tela (`docs/img/`).

| Ação | Resultado |
|---|---|
| Abrir | Miniaturas ao vivo, câmera ausente marcada "DESCONECTADO" com o motivo, medidores ativos |
| Clique → Enter | Cartão verde (PRÓXIMO) → vermelho (NO AR); monitor mostra a fonte |
| Clique na composição → Espaço | Cartão amarelo "ENTRANDO", barra de progresso, aviso "comandos bloqueados", depois NO AR |
| Alt+1/2/3, botão do logo | Identificações e logo aparecem no monitor de programa (renderização do motor) |
| GRAVAR + TRANSMITIR | Cronômetros, 4,4 Mb/s, 0 perdidos, moldura vermelha; MKV de 39 s com 1 vídeo + 3 áudios; servidor recebeu streams de 43 s e 30 s |
| PARAR / ENCERRAR | Confirmação com "Cancelar" como padrão; encerra corretamente |
| Editor de identificação, editor de composição, configurações, menu de contexto, remover fonte | Funcionais; remoção pede confirmação e persiste no perfil |
| Matar o processo | Próximo início exibe aviso de sessão anterior |
| SIGTERM | Encerramento normal, configuração salva, **0 alocações vazadas** no libobs |

### Defeitos de interface encontrados e corrigidos nesta validação
1. Monitor de programa preto (display criado antes da exposição) → criação no primeiro *paint*.
2. Monitor cortado à direita (swapchain X11 criado com tamanho antigo) → tamanho provisório + resize.
3. **Crash** ao fechar diálogo aberto pelo menu de contexto (o cartão era destruído com seu handler na pilha) → reconstruções adiadas, `deleteLater` e diálogos enfileirados (grade, mesa de áudio, gráficos).
4. Botões da mesa de áudio sem ícone; rótulo "SEM SINAL" oscilando; pré-visualização do lower third recortada; contador "0 MB" durante a gravação; botões padrão do Qt em inglês.

## 4. O que **não** foi validado
- Compilação MSVC, pacote e instalador Windows (scripts e CI prontos; trechos Win32 compilados com MinGW).
- Qualquer dispositivo físico (câmeras USB, microfones USB, placa de captura), GPU real, Quick Sync/NVENC/AMF.
- RTMPS e plataformas reais (YouTube/Twitch/Facebook).
- Desempenho no equipamento de referência (i5-10210U).
Esses itens estão no `HARDWARE_TEST_PLAN.md`.

## Como reproduzir
```bash
bash scripts/linux/test-env.sh start
ctest --test-dir build -LE integration --output-on-failure
DISPLAY=:99 QTEST_DISABLE_STACK_DUMP=1 ./build/tests/engine/tst_engine
```
