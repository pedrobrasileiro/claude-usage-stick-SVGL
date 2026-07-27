# firmware/ — Claude Usage Stick (ESP32-S3 + LVGL)

Firmware para a tela touch **Guition JC4832W535** (AXS15231B QSPI, 480×320).

- **`claude_stick/`** — o projeto. Sketch arduino-cli completo: busca o uso do
  Claude (headers da `api.anthropic.com`), saúde dos modelos (`status.claude.com`),
  e renderiza tudo em LVGL 9 com navegação touch. Token OAuth guardado cifrado
  (AES-256-GCM + PIN). Veja o build em [`claude_stick/build.sh`](claude_stick/build.sh).
- **`bringup/`** — bring-up validado no hardware (cores certas, orientação
  USB-à-esquerda e touch alinhado). Referência conhecida-boa da config de
  display/touch; não é o app.
- **`REFERENCIA-HARDWARE-LVGL.md`** — pinos, libs testadas, pipeline de flush
  (rotação 270° CW na mão) e armadilhas (PSRAM OPI obrigatória, etc.).
- **`claude_stick_cyd/`** — fork pra **ESP32-2432S028** ("Cheap Yellow
  Display", ESP32 clássico sem PSRAM, 320×240). Touch **XPT2046** resistivo
  (chip U3, barramento SPI dedicado — pinos confirmados no manual do
  fabricante, `bringup_cyd/Manual_Pinagem_Foto.pdf`): navegação e ajustes
  nativos por toque, PIN pedido a cada boot. Botão físico BOOT e portal web
  (`claude-stick.local`) continuam como fallback (provisionamento inicial de
  WiFi/token e reconfiguração remota). Veja
  [`claude_stick_cyd/README.md`](claude_stick_cyd/README.md).
- **`bringup_cyd/`** — bring-up validado dessa placa (pinos do display e do
  touch XPT2046, sketch de calibração — toque nos 4 cantos e leia os valores
  no Serial).

Comece por [`../README.md`](../README.md) para a visão geral e o passo a passo de build.
