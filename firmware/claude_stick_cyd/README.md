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
3. **A cada boot seguinte**: a tela mostra só "acesse
   claude-stick.local pra configurar" até você digitar o **PIN** nesse
   endereço. Sem PIN certo, sem dashboard — mesmo modelo de segurança do
   original (AES-256-GCM + PIN), só que o PIN é digitado no navegador em
   vez do teclado da tela.
4. **Ajustes** (brilho / intervalo / fuso / slideshow / idioma / trocar
   WiFi / trocar token / apagar tudo): clique longo no BOOT abre a tela
   "Ajustes" no display, que mostra a URL (`claude-stick.local/settings`)
   pra abrir no navegador e mexer nas opções.

Não existe captive portal automático — o SSID/IP aparecem na própria
tela do device em cada etapa.

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

## Layout ainda não ajustado pra 320×240

As tiles do dashboard foram desenhadas originalmente pra 480×320 (placa
S3). Os quatro contêineres externos (scrim, barra de referência,
tileview) já usam as macros `SCREEN_WIDTH`/`SCREEN_HEIGHT` e se ajustam
certo, mas o **conteúdo interno de cada tile** (posições de texto,
medidores, números) ainda tem números fixos pensados pra tela maior —
espera-se cortes/sobreposição em 320×240 até esse ajuste fino ser feito.
