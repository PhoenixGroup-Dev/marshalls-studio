# Componentes de terceiros

| Componente | Uso | Licença |
|---|---|---|
| libobs e plugins de núcleo do OBS Studio (obsproject/obs-studio) | Motor de captura, composição, codificação, gravação e transmissão | GPL-2.0-or-later |
| Qt 6 (Core, Gui, Widgets) | Interface e utilitários | LGPL-3.0 (linkagem dinâmica) |
| FFmpeg (via obs-deps) | Decodificação de mídia, muxer, AAC | LGPL-2.1+ (partes GPL conforme o build do obs-deps) |
| x264 (via obs-deps) | Codificação H.264 por software | GPL-2.0-or-later |
| mbedTLS (via obs-deps) | RTMPS | Apache-2.0 |
| librist / libsrt / zlib e demais dependências do obs-deps | Conforme o build do OBS | Licenças respectivas (ver `deps\obs-studio\.deps` e a documentação do obs-deps) |
| Mesa / drivers de GPU | Renderização (sistema) | — |

Como o executável incorpora componentes GPL, a distribuição do Marshalls Studio deve acompanhar o código-fonte correspondente (ou oferta escrita) nos termos da GPL-2.0 (`LICENSE`).
