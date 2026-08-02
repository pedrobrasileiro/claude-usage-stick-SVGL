#!/usr/bin/env bash
#
# Build / upload / monitor do Claude Usage Stick — Fikra ES3C28P (ESP32-S3).
#
# Uso:
#   ./build.sh                 # compila
#   ./build.sh upload          # compila + grava (porta padrão abaixo)
#   ./build.sh upload <porta>  # compila + grava na porta indicada
#   ./build.sh monitor <porta> # abre o serial monitor (115200)
#
# Pré-requisitos: arduino-cli 1.4.x, core esp32:esp32 3.3.8, libs GFX
# Library for Arduino 1.6.5 + lvgl 9.2.2 + ArduinoJson 7.2.0 (mesmas versões
# do claude_stick/ e claude_stick_cyd/).
#
# Lógica de negócio/UI (providers, estado, dashboard etc.) vem de
# firmware/libraries/lib_core/ (biblioteca Arduino compartilhada com
# claude_stick_cyd/ — ver firmware/README.md). --libraries abaixo aponta
# pra firmware/libraries/ pra arduino-cli achar essa lib local, sem
# escanear os sketches (evita conflito de headers com claude_stick/).
#
# PORT_DEFAULT é um placeholder — confirme a porta real (USB-C nativo do
# S3, geralmente /dev/cu.usbmodem*) após a primeira gravação.
set -euo pipefail

SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB_DIR="$(dirname "$SKETCH_DIR")/libraries"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=custom,CDCOnBoot=cdc,USBMode=hwcdc,FlashMode=qio"
PORT_DEFAULT="/dev/cu.usbmodem101"

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
      --libraries "$LIB_DIR" \
      --build-property "compiler.cpp.extra_flags=$LVFLAGS" \
      --build-property "compiler.c.extra_flags=$LVFLAGS" \
      "$SKETCH_DIR"
    ;;
  upload)
    echo "==> compilando + gravando em $port ($FQBN)"
    arduino-cli compile \
      --fqbn "$FQBN" \
      --libraries "$LIB_DIR" \
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
