#!/usr/bin/env python3
"""
gen_opencode_logo.py — rasteriza o favicon oficial do OpenCode
(assets/brand/opencode-icon.svg) em imagens LVGL ARGB8888.

Gera firmware/claude_stick_cyd/opencode_logo.h com:
  img_oc_sm       — ícone pequeno para o header (h=26)
  img_oc_big      — ícone para telas de loading/about (h=90)
  img_oc_xl       — ícone para overlay de moment (w=176)

Requisitos: rsvg-convert (brew install librsvg) e Pillow.
"""
import os
import subprocess
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON = os.path.join(ROOT, "assets", "brand", "opencode-icon.svg")
OUT = os.path.join(ROOT, "firmware", "claude_stick_cyd", "opencode_logo.h")

# Cores do favicon OpenCode
BG = "#131010"       # fundo (quase preto)
FRAME = "#FFFFFF"    # bracket externo
ACCENT = "#5A5858"   # bloco interno cinza


def render(svg: str, *, height: int | None = None, width: int | None = None) -> Image.Image:
    """rsvg-convert -> PIL RGBA (fundo transparente)."""
    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as tf:
        png = tf.name
    cmd = ["rsvg-convert", svg, "-o", png]
    if height:
        cmd += ["-h", str(height)]
    if width:
        cmd += ["-w", str(width)]
    subprocess.run(cmd, check=True)
    im = Image.open(png).convert("RGBA")
    os.unlink(png)
    return im


def to_c(im: Image.Image, name: str) -> str:
    """PIL RGBA -> lv_image_dsc_t ARGB8888 (bytes B,G,R,A little-endian)."""
    w, h = im.size
    px = im.tobytes()
    data = bytearray()
    for i in range(0, len(px), 4):
        r, g, b, a = px[i], px[i + 1], px[i + 2], px[i + 3]
        data += bytes((b, g, r, a))
    rows = []
    for i in range(0, len(data), 20):
        rows.append(",".join(str(v) for v in data[i:i + 20]))
    body = ",\n  ".join(rows)
    return (
        f"static const uint8_t {name}_map[] = {{\n  {body}\n}};\n"
        f"static const lv_image_dsc_t {name} = {{\n"
        f"  {{ LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_ARGB8888, 0, {w}, {h}, {w * 4}, 0 }},\n"
        f"  sizeof({name}_map), {name}_map\n"
        f"}};\n"
    )


def main() -> None:
    parts = ["// GERADO por tools/gen_opencode_logo.py — nao editar na mao.",
             "// Icone oficial do OpenCode (favicon 512x512, 3 cores).",
             "#pragma once", "#include <lvgl.h>", ""]

    # SM: header (height=26, proporcional)
    sm = render(ICON, height=26)
    parts.append(to_c(sm, "img_oc_sm"))
    print(f"oc_sm: {sm.size}")

    # Header wordmark: mesmo ícone um pouco maior ou gerar texto
    # Como o ícone é quadrado, o "wordmark" é o ícone com altura 26 tbm
    # (o header usa ícone + wordmark lado a lado; pra OpenCode usamos só o ícone maior)
    wm = render(ICON, height=44)
    parts.append(to_c(wm, "img_oc_wordmark"))
    print(f"oc_wordmark: {wm.size}")

    # BIG: loading/about (height=144, proporcional)
    big = render(ICON, height=144)
    parts.append(to_c(big, "img_oc_big"))
    print(f"oc_big: {big.size}")

    # XL: moment overlay (width=176, proporcional)
    xl = render(ICON, width=176)
    parts.append(to_c(xl, "img_oc_xl"))
    print(f"oc_xl: {xl.size}")

    with open(OUT, "w") as f:
        f.write("\n".join(parts))
    print(f"gerado: {OUT} ({os.path.getsize(OUT) // 1024} KB)")


if __name__ == "__main__":
    main()
