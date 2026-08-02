# firmware/ — Claude Usage Stick (ESP32-S3 + LVGL)

Três variantes de placa, três sketches. Qual usar depende do hardware físico
que você tem em mãos:

| Placa | Pasta | Display | Touch | PSRAM |
|---|---|---|---|---|
| Guition JC4832W535 | [`claude_stick/`](claude_stick/) | QSPI AXS15231B, 480×320 | AXS15231B (I2C, integrado ao display) | Sim (OPI) |
| "Cheap Yellow Display" ESP32-2432S028 | [`claude_stick_cyd/`](claude_stick_cyd/) | SPI ILI9341, 320×240 | XPT2046 resistivo (SPI dedicado) | Não |
| Fikra ES3C28P | [`claude_stick_fikra/`](claude_stick_fikra/) | SPI ILI9341V, 240×320 (rotacionado p/ 320×240) | FT6336G capacitivo (I2C 0x38) | Sim (OPI) |

- **`claude_stick/`** — o projeto original. Sketch arduino-cli monolítico
  (não usa `libraries/lib_core/`): busca o uso do Claude (headers da
  `api.anthropic.com`), saúde dos modelos (`status.claude.com`), e renderiza
  tudo em LVGL 9 com navegação touch. Token OAuth guardado cifrado
  (AES-256-GCM + PIN). Veja o build em [`claude_stick/build.sh`](claude_stick/build.sh).
- **`bringup/`** — bring-up validado no hardware (cores certas, orientação
  USB-à-esquerda e touch alinhado). Referência conhecida-boa da config de
  display/touch; não é o app.
- **`REFERENCIA-HARDWARE-LVGL.md`** — pinos, libs testadas, pipeline de flush
  (rotação 270° CW na mão) e armadilhas (PSRAM OPI obrigatória, etc.).
- **`libraries/lib_core/`** — biblioteca Arduino (formato 1.5, `src/`) com a lógica
  de negócio e UI **compartilhada** entre `claude_stick_cyd/` e
  `claude_stick_fikra/`: providers (Claude API + scraping OpenCode Go),
  máquina de estado (`state_app`/`state_security`/`state_dashboard`),
  telas LVGL (`ui_screens`/`ui_dashboard`/`ui_helpers`), história/heatmap,
  segurança (PIN + lockout), portal de provisionamento e servidor web de
  ajustes. Cada sketch fornece seu próprio `config.h` (pinos/constantes de
  placa), `touch.h` (driver de toque) e `lv_conf.h` (perfil de memória do
  LVGL) — resolvidos via include path do sketch, mesmo mecanismo já usado
  pelo `lv_conf.h` (`-DLV_CONF_INCLUDE_SIMPLE -I$SKETCH_DIR`); os
  `build.sh` de ambos os sketches passam `--libraries` apontando pra
  `firmware/` pra o `arduino-cli` achar essa lib local. **Placa principal
  (`claude_stick/`) não usa essa lib** — continua monolítica, por decisão
  explícita (retrofitá-la é um projeto à parte, bem maior).
- **`claude_stick_cyd/`** — sketch fino pra **ESP32-2432S028** ("Cheap
  Yellow Display", ESP32 clássico sem PSRAM, 320×240). Touch **XPT2046**
  resistivo (chip U3, barramento SPI dedicado — pinos confirmados no manual
  do fabricante, `bringup_cyd/Manual_Pinagem_Foto.pdf`): navegação e
  ajustes nativos por toque, PIN pedido a cada boot. Botão físico BOOT e
  portal web (`claude-stick.local`) continuam como fallback (provisionamento
  inicial de WiFi/token e reconfiguração remota). Lógica/UI vem de
  `libraries/lib_core/`. Veja [`claude_stick_cyd/README.md`](claude_stick_cyd/README.md).
- **`bringup_cyd/`** — bring-up validado dessa placa (pinos do display e do
  touch XPT2046, sketch de calibração — toque nos 4 cantos e leia os valores
  no Serial).
- **`claude_stick_fikra/`** — sketch fino pra **Fikra ES3C28P** (ESP32-S3,
  módulo de display 2,8" da Fikra Creative Studio, fabricante ShenZhen
  QDtech/LCDWIKI — Manual Técnico Fikra 2.0). Display ILI9341V SPI 240×320
  (rotacionado p/ landscape 320×240, mesma orientação de UI do CYD), touch
  capacitivo **FT6336G** via I2C (endereço 0x38), 8MB PSRAM OPI + 16MB
  flash. Mesma UI/lógica do CYD (vem de `libraries/lib_core/`), só troca `config.h`
  (pinos), `touch.h` (driver I2C em vez de SPI resistivo) e o FQBN no
  `build.sh` (perfil ESP32-S3 com PSRAM, igual ao `claude_stick/`). Bring-up
  físico validado: WiFi, provisionamento, touch e dashboard funcionando de
  ponta a ponta (`TFT_ROTATION 1` + mapeamento de touch em `touch.h`
  confirmados corretos na primeira gravação).
  Além do dashboard, expõe uma tela extra em Ajustes — **LED RGB**
  (`rgb_led.h/.cpp` + `ui_rgb.h/.cpp`, hardware exclusivo dessa placa,
  WS2812 no GPIO42): cores fixas (R/G/B/branco/preto), modo alternando e
  desligar. Ver `g_hasBoardExtra`/`ST_EXTRA` em `state_app.h` (`libraries/lib_core/`)
  — hook genérico pra qualquer placa expor uma tela própria em Ajustes sem
  poluir a lib compartilhada com hardware específico.
  **Nota de bring-up (resolvida):** o painel ILI9341V dessa placa "lava"
  (deixa mais claro) qualquer cinza bem escuro — preto puro (`0x000000`)
  renderiza correto, mas o `C_BG` original do app (`0x0F0F12`, cinza-chumbo)
  saía acinzentado/azulado. Tentativas de `invertDisplay`, `SPI_FREQ` e
  VCOM/gamma customizados (comandos `0xC5`/`0x26`) não mudaram nada — é
  característica do painel, não bug de init. Fix: `C_BG` virou preto puro
  e `C_SURFACE`/`C_SURFACE2`/`C_MUTED`/`C_FAINT` foram recalibrados
  (cards mais escuros, texto secundário mais claro) em `ui_helpers.h`
  (`libraries/lib_core/` — compartilhado, também vale pro CYD).

Comece por [`../README.md`](../README.md) para a visão geral e o passo a passo de build.
