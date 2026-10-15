# Plano de validação em equipamento real

Equipamento de referência: Windows 10/11 64 bits, Intel Core i5-10210U, 12 GB RAM, Intel UHD, câmeras USB, dois microfones USB, monitor comum. Registrar para cada teste: data, versão (`Configurações → Diagnóstico → Copiar relatório`), resultado (Aprovado/Reprovado), evidências (arquivo gravado, captura de tela, trecho do log).

Critério geral: **um teste só é aprovado com evidência da saída real** (arquivo reproduzido, stream recebido, áudio ouvido/medido) — nunca apenas porque a interface foi exibida.

| # | Cenário | Procedimento | Aprovado quando | Cobertura automática na bancada Linux |
|---|---|---|---|---|
| H-01 | Detectar dispositivos | Conectar 2 câmeras e 2 microfones USB; abrir; Adicionar → Câmera / Entrada → Microfone | Todos listados com nomes corretos; reabrir após trocar porta USB mantém as fontes associadas | t01 (microfones virtuais, tela); câmera ausente em t15 |
| H-02 | Duas câmeras simultâneas | Adicionar 2 câmeras | Duas miniaturas vivas (movimento), sem quadros congelados em 10 min; COMP < 1% | t03 (fontes de teste) |
| H-03 | Três+ câmeras | Adicionar a 3ª/4ª câmera (mesmo hub e hubs separados) | Miniaturas vivas; se a banda USB não suportar, cartão indica "Sem sinal" sem travar | — |
| H-04 | Captura de tela e janela | Tela inteira e Janela (PowerPoint); minimizar/restaurar a janela; desconectar 2º monitor | Imagem atualiza; janela minimizada mostra aviso; nenhum "espelho infinito" com a opção de ocultar ativa | t03 (pixel idêntico ao x11grab) |
| H-05 | Trocar fontes gravando | Gravar 5 min alternando corte/AUTO/composições | Arquivo contínuo, sem cortes de áudio, todas as trocas visíveis | t11 |
| H-06 | Dois microfones USB | Dois locutores falando | Dois canais com medidores independentes; PGM com ambos; trilhas 2 e 3 isoladas no MKV | t10, t11 (entradas virtuais) |
| H-07 | Volume e mute independentes | Mutar A, baixar B −20 dB | Mix do programa: A ausente, B 20 dB mais baixo; trilhas isoladas coerentes | t10 (razão 0,1000 medida) |
| H-08 | Sincronia gravada | Claquete diante da câmera (palmas); gravar; medir no editor | Som e imagem com diferença ≤ 1 quadro após ajuste; atraso de áudio de +200 ms e de vídeo de 200 ms deslocam ±1 quadro | t12 (cadeia e deslocamentos medidos; precisão limitada sem GPU) |
| H-09 | Lower third | Alt+1, cortar entre fontes, Alt+1 | Entra/sai animado, permanece através dos cortes, aparece no arquivo | t08 |
| H-10 | Transição real | AUTO 1 s entre duas câmeras gravando | Dissolve visível no arquivo, duração ~1 s | t05 |
| H-11 | Gravar e reproduzir | Gravar 30 min em MKV (Quick Sync) | Reproduz no VLC/Filmes; MP4 gerado se habilitado; uso de disco ≈ estimativa | t11, t20 |
| H-12 | Transmissão de teste | YouTube (transmissão não listada) ou servidor RTMP próprio; 30 min | Recebido na plataforma; quadros perdidos < 1%; encerrar pede confirmação | t13, t14 (servidor local) |
| H-13 | Desconectar câmera | Puxar o USB durante gravação; reconectar | Cartão "Desconectado"/"Sem sinal" em ≤ 3 s; gravação continua; "Reconectar" restabelece | t15 (câmera inexistente) |
| H-14 | Desconectar microfone | Puxar o USB do microfone B | Canal "DESCONECTADO"; microfone A segue; reconectar e "Reconectar" | t16 |
| H-15 | Pouca memória / CPU sobrecarregada | Rodar com outro programa pesado (ex.: navegador com vídeo 4K) | UI responsiva; COMP/COD sinalizam sobrecarga; nenhuma falha | t19 (CPU 95%+, UI ≤ 36 ms de atraso) |
| H-16 | Reinício e configurações | Configurar tudo, fechar, reabrir; também matar o processo | Tudo restaurado; aviso de sessão anterior após o kill | t21 + validação manual da UI |
| H-17 | Nenhum botão sem implementação | Percorrer todos os botões/menus | Cada controle executa ação real ou mostra motivo de indisponibilidade | Validação manual da UI (VALIDATION.md §3) |
| H-18 | Desempenho de referência | Canvas 1080p, saída 720p30, 2 câmeras, 2 mics, gravação Quick Sync + transmissão 4500 kb/s, 60 min | COMP < 1%, COD < 1%, CPU < 70%, RAM estável (sem crescimento contínuo) | — |
