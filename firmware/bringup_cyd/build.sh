#!/usr/bin/env bash
#
# Build / upload / monitor do bring-up CYD (ESP32-2432S028, ESP32 clássico).
#
# Uso:
#   ./build.sh                 # compila
#   ./build.sh upload          # compila + grava (porta padrão abaixo)
#   ./build.sh upload <porta>  # compila + grava na porta indicada
#   ./build.sh monitor <porta> # abre o serial monitor (115200)
#
# Pré-requisitos:
#   - arduino-cli, core esp32:esp32
#   - libs: GFX Library for Arduino, lvgl, XPT2046_Touchscreen
set -euo pipefail

SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FQBN="esp32:esp32:esp32:UploadSpeed=115200"
PORT_DEFAULT="/dev/cu.usbserial-10"

LVFLAGS="-DLV_CONF_INCLUDE_SIMPLE -I${SKETCH_DIR}"

cmd="${1:-build}"
port="${2:-$PORT_DEFAULT}"

case "$cmd" in
  monitor)
    exec arduino-cli monitor -p "$port" -c baudrate=115200
    ;;
  build)
    echo "==> compilando ($FQBN)"
    arduino-cli compile \
      --fqbn "$FQBN" \
      --build-property "compiler.cpp.extra_flags=$LVFLAGS" \
      --build-property "compiler.c.extra_flags=$LVFLAGS" \
      "$SKETCH_DIR"
    ;;
  upload)
    echo "==> compilando + gravando em $port ($FQBN)"
    arduino-cli compile \
      --fqbn "$FQBN" \
      --build-property "compiler.cpp.extra_flags=$LVFLAGS" \
      --build-property "compiler.c.extra_flags=$LVFLAGS" \
      --upload -p "$port" \
      "$SKETCH_DIR"
    ;;
  *)
    echo "comando desconhecido: $cmd (use: build | upload | monitor)" >&2
    exit 1
    ;;
esac
