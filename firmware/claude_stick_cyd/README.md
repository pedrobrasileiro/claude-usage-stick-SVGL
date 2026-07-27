# firmware/claude_stick_cyd/ — Claude Usage Stick no ESP32-2432S028 (CYD)

Fork do [`../claude_stick`](../claude_stick) (original, Guition JC4832W535
480×320 touch) adaptado pra placa **ESP32-2432S028** — "Cheap Yellow
Display" (CYD): ESP32 clássico WROOM-32, **sem PSRAM**, tela ILI9341
**320×240** e touch **XPT2046** resistivo.

O touch dessa unidade tinha sido dado como inoperante num bring-up
anterior (varredura SPI incompleta + varredura I2C exaustiva, sem
resultado). O manual/schematic oficial do fabricante
([`../bringup_cyd/Manual_Pinagem_Foto.pdf`](../bringup_cyd/Manual_Pinagem_Foto.pdf))
revelou a pinagem real do chip XPT2046 (U3) — um barramento SPI
**separado** do display, nunca testado corretamente antes — e com os
pinos certos o touch respondeu normalmente. Navegação e ajustes hoje são
nativos por toque, com **botão físico (BOOT)** e **portal web**
continuando como fallback.

## O que muda em relação ao original

| | Original (`claude_stick`) | CYD (`claude_stick_cyd`) |
|---|---|---|
| Chip | ESP32-S3, 8 MB PSRAM | ESP32 clássico, **sem PSRAM** |
| Tela | 480×320 AXS15231B QSPI | 320×240 ILI9341 SPI |
| Touch | Capacitivo (AXS15231B, I2C) | **Resistivo (XPT2046, SPI dedicado)** |
| Navegação | Toque na tela | Toque na tela (BOOT físico como fallback) |
| PIN | Teclado na tela, a cada boot | Teclado na tela, a cada boot |
| WiFi / token (1º uso) | Teclado na tela (LVGL) | Formulário no navegador (fallback web) |

Todo o resto — leitura de uso via headers da API, sondagem de saúde dos
modelos, histórico/heatmap, cifra do token — é o mesmo código, só
recompilado pra essa placa.

## Navegação por touch

- **Swipe** nos tiles do dashboard (Agora → Modelos → Janela 5h → Ritmo
  por hora).
- **Engrenagem** no canto superior direito do dashboard abre **Ajustes**
  (pede PIN se a sessão de 5 min já expirou).
- Botão **Voltar** no topo das telas de Ajustes e Sobre.
- Cada linha de Ajustes é um botão: toque avança pro próximo valor
  (brilho, intervalo, fuso, slideshow, ritmo por hora/heatmap) ou abre a
  ação (atualizar agora, reconfigurar WiFi, trocar token, sobre, apagar
  tudo — este último exige dois toques de confirmação).
- **BOOT físico (GPIO0)** continua funcionando em paralelo como fallback:
  clique curto avança tile / volta pro dashboard; clique longo abre
  Ajustes (mesmo gate de PIN) / Sobre.

## PIN a cada boot

Como no original, o PIN é pedido **toda vez que a placa liga** (teclado
numérico touch) pra decifrar o token — nunca fica salvo em claro na NVS.
A mesma tela de PIN também gateia o acesso a Ajustes, com sessão de 5
minutos, lockout progressivo e apaga-tudo em 10 tentativas erradas.

## Provisionamento (WiFi + token): ainda pelo navegador

Configurar WiFi e trocar o token continuam sendo feitos por um
**formulário HTML** servido pelo próprio device — tela pequena (320×240)
+ touch resistivo tornam um teclado alfanumérico tocado mais chato pra
digitar SSID/senha/token, e é uma ação rara (1x no provisionamento,
raramente depois):

1. **Primeiro uso** (sem WiFi salvo): a placa sobe um **ponto de acesso**
   próprio, `ClaudeStick-Setup` (sem senha). Conecte seu celular/notebook
   nessa rede e abra `http://192.168.4.1`. O formulário pede rede WiFi de
   casa (com senha), o token OAuth do Claude e um PIN de 4 dígitos.
2. **Depois de configurado**: a placa conecta na sua WiFi normal (modo
   STA) e o mesmo formulário fica disponível em
   **`http://claude-stick.local`** (mDNS) — ou pelo IP mostrado na tela.
3. **Reconfigurar WiFi / trocar token**: pela tela de Ajustes (toque) ou
   por `claude-stick.local/settings` no navegador — os dois caem no mesmo
   fluxo de provisionamento web.

Não existe captive portal automático — o SSID/IP aparecem na própria
tela do device em cada etapa.

O portal web (`/settings`) também serve como fallback completo de
ajustes (mesmas ações da tela touch, em forma de formulário) — útil se o
touch falhar ou descalibrar em campo — e mantém os endpoints `/window` e
`/tokens` usados pelo bridge `tools/token_bridge.py`.

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
| Tela | **ILI9341**, SPI, 320×240 — `DC=2 CS=15 SCLK=14 MOSI=13 MISO=12 BL=21` |
| Touch | **XPT2046** resistivo, SPI dedicado (HSPI) — `CLK=25 CS=33 MOSI=32 MISO=39 IRQ=36` |
| Botão | **BOOT / GPIO0**, ativo em LOW (pull-up de fábrica) — fallback |

Pinagem do touch confirmada no manual/schematic oficial do fabricante:
[`../bringup_cyd/Manual_Pinagem_Foto.pdf`](../bringup_cyd/Manual_Pinagem_Foto.pdf).
Bring-up de referência (pinos/cores/calibração validados) em
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
