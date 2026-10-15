# Limitações conhecidas e pendências

Classificação honesta do estado atual. "Validado" significa executado e medido nesta bancada (Linux, sem GPU, sem câmeras/microfones físicos — ver `VALIDATION.md`).

## Pendências que bloqueiam o uso operacional no Windows
| Item | Situação | Próximo passo |
|---|---|---|
| Build MSVC + pacote + instalador | Scripts e CI escritos; chamadas Win32 compiladas com MinGW; **build completo não executado** | Rodar `.github/workflows/windows.yml` ou `scripts/windows/*.ps1` numa máquina Windows e corrigir eventuais erros de compilação |
| Plugins Windows (DirectShow, WASAPI, captura de monitor/janela DXGI/WGC, Quick Sync/NVENC/AMF) | Código implementado via API pública do libobs; **não exercitados** | Plano `HARDWARE_TEST_PLAN.md` |
| Testes com câmeras e microfones físicos, GPU Intel UHD | Não disponíveis na bancada | Plano `HARDWARE_TEST_PLAN.md` |

## Parcialmente implementado
| Recurso | O que existe | O que falta |
|---|---|---|
| Monitoramento de áudio | Por canal (desligado/só monitorar/monitorar+enviar) e escolha do dispositivo de saída | **Volume mestre de monitoramento independente**: o libobs aplica o fader do canal também ao monitoramento e não expõe um nível de monitor separado. Exigiria um barramento de monitor próprio (trilha dedicada + saída de áudio própria) |
| Captura de janela | Windows: fonte `window_capture` (exclui a própria janela da lista) | Linux: o plugin `xcomposite` exige suporte EGL a pixmaps X11 indisponível nesta bancada — a opção aparece como "Indisponível" com o motivo |
| Prevenção de recursão na captura de tela | Windows 10 2004+: `WDA_EXCLUDEFROMCAPTURE` | Outros sistemas: orientação ao operador |
| Precisão do atraso de vídeo | Aplicado e medido (115–200 ms para 200 ms configurados) | Na bancada sem GPU o compositor perde quadros e o filtro (baseado em quadros) deriva; precisão de ±1 quadro deve ser confirmada com GPU (teste H-08) |
| Sincronia A/V da cadeia | Medida em −7 a −56 ms entre execuções na bancada (renderização por software) | Meta ≤ 1 quadro em hardware real (H-08) |
| Apresentações | Sequência de imagens (slides exportados) com troca manual, ou captura da janela do software de slides (Windows) | Integração direta com PowerPoint/PDF |
| Chave de transmissão no Linux | Arquivo 0600 fora do perfil | Sem criptografia (no Windows: DPAPI) |
| Plataformas de transmissão | RTMP/RTMPS com presets YouTube/Twitch/Facebook/personalizado; RTMP validado contra servidor local | RTMPS e plataformas reais não testados; SRT/WHIP não expostos |
| Recuperação após falha | Sentinela + aviso; MKV interrompido reproduzível (validado) | Não há remux automático de MKV interrompido para MP4 |

## Não implementado
- Atalhos globais (com o aplicativo fora de foco).
- Saída de vídeo para monitor externo em tela cheia (projetor) e multiview em janela separada.
- Barramento de pré-visualização separado (por decisão do produto a operação é grade → programa; a composição pode ser editada fora do ar no editor).
- Áudio por fonte de vídeo de dispositivos de captura com áudio embutido foi implementado no motor, mas sem teste com placa de captura real.
- Interface em outros idiomas além do português.
- macOS.

## Observações sobre o libobs (comportamentos encontrados e contornados)
1. `linux-v4l2` e `xshm` travam quando consultados sem instância → enumeração por instância ociosa / V4L2 direto.
2. `rtmp-services` travou ao descarregar (double free) e faz requisições de rede ao iniciar → substituído por serviço RTMP próprio.
3. Carregamento duplicado de módulos (caminhos internos do libobs) corrompia `linux-capture` → deduplicação por nome.
4. Mudança de sync offset a partir de 0 é ignorada com áudio contínuo → aplicação em dois passos.
5. Swapchain X11/EGL criado com o tamanho antigo da janela → tamanho provisório + redimensionamento.
6. O medidor de canal do libobs é pré-mute.
