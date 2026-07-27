# firmware/claude_stick_cyd/ — Claude Usage Stick no ESP32-2432S028 (CYD)

Fork do [`../claude_stick`](../claude_stick) (original, Guition JC4832W535
480×320 touch) adaptado pra placa **ESP32-2432S028** — "Cheap Yellow
Display" (CYD): ESP32 clássico WROOM-32, **sem PSRAM**, tela ILI9341
**320×240** e, nesta unidade específica, **touch inoperante** (testado
exaustivamente no bring-up, sem resposta).

Sem touch, a UX foi redesenhada em volta do que a placa tem de sobra: um
**botão físico (BOOT)** e uma **rede WiFi**.

## O que muda em relação ao original

| | Original (`claude_stick`) | CYD (`claude_stick_cyd`) |
|---|---|---|
| Chip | ESP32-S3, 8 MB PSRAM | ESP32 clássico, **sem PSRAM** |
| Tela | 480×320 AXS15231B QSPI | 320×240 ILI9341 SPI |
| Touch | Capacitivo, navegação por swipe | **Nenhum** |
| Navegação | Toque na tela | **Botão BOOT (GPIO0)** |
| PIN / token / WiFi | Teclado na tela (LVGL) | **Formulário no navegador** |

Todo o resto — leitura de uso via headers da API, sondagem de saúde dos
modelos, histórico/heatmap, cifra do token — é o mesmo código, só
recompilado pra essa placa.

## Navegação (só o botão BOOT)

- **Clique curto** no dashboard (`ST_MAIN`): avança pra próxima tile
  (Agora → Modelos → Janela 5h → Ritmo por hora → volta pra Agora).
- **Clique longo**: `ST_MAIN` → **Ajustes** → **Sobre** → volta pro
  dashboard. Um clique longo dentro de Ajustes/Sobre também volta direto
  pro dashboard.

Sem gestos, sem toques na tela — é só isso.

## Provisionamento e ajustes: tudo pelo navegador

Como não há teclado touch, PIN, token, WiFi e todos os ajustes (brilho,
intervalo de poll, fuso horário, slideshow, heatmap, idioma) são feitos
por um **formulário HTML simples** servido pelo próprio device:

1. **Primeiro uso** (sem WiFi salvo): a placa sobe um **ponto de acesso**
   próprio, `ClaudeStick-Setup` (sem senha). Conecte seu celular/notebook
   nessa rede e abra `http://192.168.4.1`. O formulário pede rede WiFi de
   casa (com senha), o token OAuth do Claude e um PIN de 4 dígitos.
2. **Depois de configurado**: a placa conecta na sua WiFi normal (modo
   STA) e o mesmo formulário fica disponível em
   **`http://claude-stick.local`** (mDNS) — ou pelo IP mostrado na tela.
3. **A cada boot seguinte**: liga e vai direto pro dashboard, sem pedir
   PIN — a placa guarda o PIN na NVS do device e decifra o token
   sozinha (mesma cifra AES-256-GCM do original, só que a chave "mora"
   no device em vez de ser digitada toda vez). Trade-off: quem tiver
   acesso físico ao device (ou um dump da flash) consegue ler esse PIN
   salvo — mesmo risco que os outros ajustes já gravados em claro hoje
   (brilho, fuso etc.).
4. **Ajustes** (brilho / intervalo / fuso / slideshow / idioma / trocar
   WiFi / trocar token / apagar tudo): clique longo no BOOT abre a tela
   "Ajustes" no display, que mostra a URL (`claude-stick.local/settings`)
   pra abrir no navegador. Essa página **pede o PIN** antes de liberar
   qualquer ajuste (sessão de 5 minutos) — mesmo lockout progressivo e
   apaga-tudo em 10 tentativas erradas do desbloqueio de antes.

Não existe captive portal automático — o SSID/IP aparecem na própria
tela do device em cada etapa.

### Como trocar o token depois

1. No device, dá um **clique longo no BOOT** até a tela mostrar
   **"Ajustes"** (curto muda de tile no dashboard; longo abre Ajustes →
   Sobre → volta pro dashboard).
2. A tela de Ajustes mostra a URL `claude-stick.local/settings` — abre
   essa página no navegador (celular/notebook na mesma rede WiFi) e
   digita o **PIN** quando pedido.
3. Clica em **"Trocar token"**. O formulário volta a pedir o **token
   OAuth novo** + um **PIN** (pode ser o mesmo de antes ou um novo — os
   dois campos de confirmação valem aqui também).
4. Ao salvar, o device valida o token na hora (chamada real à API) antes
   de aceitar, e volta pro dashboard já com o token novo.

O mesmo caminho (`/settings`) serve pra **reconfigurar WiFi** e
**apagar tudo** (com confirmação via JavaScript no navegador, já que não
tem tela de toque pra confirmar duas vezes).

### Os botões de `/settings` são cíclicos, não formulários

Cada clique nesses botões **avança pro próximo valor da lista** (não abre
um campo pra digitar) — clica de novo até chegar no valor que quer:

| Botão | Ciclo |
|---|---|
| Trocar intervalo | 30s → 1min → 2min → 5min → 30s → ... |
| Trocar slideshow | desligado → 5s → 10s → 15s → 30s → desligado → ... |
| Trocar fuso | GMT-3 → -4 → -5 → -6 → -7 → -8 → -2 → -1 → 0 → +1 → +2 → +3 → GMT-3 → ... |
| Trocar brilho | baixo → médio → alto → baixo → ... |
| Trocar idioma | Português → English → Português → ... |

O valor atual de cada um aparece na própria página (ex.: "Atualizar a
cada: **60s**"), assim dá pra saber onde parar. O modo do heatmap
("Ritmo por hora": hoje/7d/30d/tudo) é o único que **não** é cíclico — é
um `<select>` com "Aplicar".

**Atualizar agora**, **Reconfigurar WiFi**, **Trocar token** e **Apagar
tudo** são ações de um clique só (não cíclicas).

## Por que sem PSRAM importa (heap apertado)

Sem PSRAM, o LVGL usa `LV_DISPLAY_RENDER_MODE_PARTIAL` com buffer pequeno
em RAM comum (~20 linhas), e sobram só uns **89 KB livres** na hora do
handshake TLS. Isso já mordeu a gente uma vez: um bundle de CA com muitos
certificados ocupa heap permanente e disputa espaço com as contas de
bignum do handshake, causando falhas aleatórias de alocação
(`BIGNUM - Memory allocation failed`) bem no meio da conexão HTTPS. Por
isso `certs.cpp` aqui é enxuto (confia direto nas intermediárias TLS
atuais, não nas raízes — ver comentário no arquivo) e o tamanho do
fragmento TLS foi reduzido (`mbedtls_ssl_conf_max_frag_len`, patch no
core instalado, ver `firmware/README.md`).

## Hardware

| | |
|---|---|
| Placa | **ESP32-2432S028** ("Cheap Yellow Display" / CYD) |
| Chip | ESP32 clássico WROOM-32, **sem PSRAM** |
| Tela | **ILI9341**, SPI, 320×240 |
| Pinos | `DC=2 CS=15 SCLK=14 MOSI=13 MISO=12 BL=21` (compartilhando o mesmo `SPIClass` que o touch, que não é usado aqui) |
| Botão | **BOOT / GPIO0**, ativo em LOW (pull-up de fábrica) |
| Touch | Presente no hardware, **não usado** (inoperante nesta unidade) |

Bring-up de referência (pinos/cores validados) em
[`../bringup_cyd/`](../bringup_cyd/).

## Build & flash

```bash
cd firmware/claude_stick_cyd
./build.sh                 # compila
./build.sh upload          # compila + grava (porta padrão /dev/cu.usbserial-10)
./build.sh upload <porta>
./build.sh monitor <porta>
```

FQBN: `esp32:esp32:esp32:PartitionScheme=huge_app,UploadSpeed=115200`
(upload a 921600 falha nesse conversor serial — 115200 é o confiável).

Mesmas libs do original: **GFX Library for Arduino** 1.6.5 · **lvgl**
9.2.2 · **ArduinoJson** 7.2.0.

`./build.sh upload` grava na flash normalmente — depois de gravado, o
device roda sozinho na tomada/powerbank, sem precisar do cabo USB ligado
no computador. Só volta a precisar do `arduino-cli` se for atualizar o
firmware de novo.

## Layout

As 4 tiles do dashboard (Agora, Modelos, Janela de 5h, Ritmo por hora) e
o popup de "momento" (animação ao cruzar 25/50/70/100%) já foram
redimensionados pra 320×240 — o layout original era pensado pra 480×320
(placa S3) e cortava conteúdo nessa tela menor. O popup de momento, que
no original tinha mascote e texto lado a lado, foi empilhado
verticalmente (mascote em cima, texto embaixo) porque não cabe os dois
lado a lado em 320px de largura.
