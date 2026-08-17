#!/usr/bin/env python3
"""Rasterise une police TTF/OTF en header C bitmap 1 bpp pour le badge.

Usage: font2header.py font.otf taille_px sortie.h [PREFIX]

Qualite : chaque glyphe est rendu a 4x la taille puis sous-echantillonne
(seuil 50 %) — rasterisation stable, hauteurs coherentes entre lettres.
Metriques : l'avance (chasse) et le bearing gauche REELS de la police sont
conserves -> l'approche entre lettres est celle dessinee par le fondeur.
Genere PREFIX_H, PREFIX_GLYPHS[95] (w, xoff, adv, off), PREFIX_BITS[] et
les fonctions {prefix}TextW() / {prefix}Print() (dessin via canvas->).
"""
import sys

from PIL import Image, ImageDraw, ImageFont

path, size, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
prefix = sys.argv[4] if len(sys.argv) > 4 else "MF"
fn = prefix.lower()

SS = 4  # facteur de sur-echantillonnage
font = ImageFont.truetype(path, size * SS)
asc4, desc4 = font.getmetrics()
H = (asc4 + desc4 + SS - 1) // SS  # cellule cible (arrondi haut)

glyphs, bits = [], bytearray()
for code in range(32, 127):
    ch = chr(code)
    adv = int(round(font.getlength(ch) / SS))
    # rendu 4x large (marges pour les debordements type 'f' ou italiques)
    W4 = int(font.getlength(ch)) + 8 * SS
    img = Image.new("L", (W4, H * SS), 0)
    ImageDraw.Draw(img).text((4 * SS, 0), ch, font=font, fill=255)
    img = img.resize((W4 // SS, H), Image.BILINEAR).point(lambda v: 255 if v >= 128 else 0)
    bbox = img.getbbox()
    if bbox is None:  # espace
        glyphs.append((0, 0, adv, 0))
        continue
    x0, _, x1, _ = bbox
    w = x1 - x0
    xoff = x0 - 4  # bearing gauche reel (origine du stylo a 4 px dans l'image)
    off = len(bits)
    stride = (w + 7) // 8
    px = img.load()
    for y in range(H):
        for bx in range(stride):
            b = 0
            for i in range(8):
                x = x0 + bx * 8 + i
                if x < x1 and px[x, y]:
                    b |= 0x80 >> i
            bits.append(b)
    glyphs.append((w, xoff, adv, off))

with open(out, "w") as f:
    f.write(f"// Genere par tools/font2header.py — {path.split('/')[-1]} @ {size}px (SS{SS}).\n")
    f.write("// Proportionnelle 1 bpp, avance/bearing de la police conserves.\n")
    f.write("#pragma once\n#include <stdint.h>\n\n")
    f.write(f"#define {prefix}_H {H}\n\n")
    f.write(f"struct {prefix}Glyph {{ uint8_t w; int8_t xoff; uint8_t adv; uint16_t off; }};\n")
    f.write(f"static const {prefix}Glyph {prefix}_GLYPHS[95] = {{\n")
    for w, xoff, adv, off in glyphs:
        f.write(f"  {{{w}, {xoff}, {adv}, {off}}},\n")
    f.write("};\n\n")
    f.write(f"static const uint8_t {prefix}_BITS[{len(bits)}] = {{\n")
    for i in range(0, len(bits), 24):
        f.write("  " + ",".join(str(b) for b in bits[i:i + 24]) + ",\n")
    f.write("};\n\n")
    f.write(f"""static int {fn}TextW(const char *s)
{{
  int w = 0;
  for (; *s; s++)
    if (*s >= 32 && *s < 127)
      w += {prefix}_GLYPHS[*s - 32].adv;
  return w;
}}

static void {fn}Print(int x, int y, const char *s, uint16_t col)
{{
  for (; *s; s++)
  {{
    if (*s < 32 || *s >= 127)
      continue;
    const {prefix}Glyph &g = {prefix}_GLYPHS[*s - 32];
    int stride = (g.w + 7) / 8;
    for (int gy = 0; gy < {prefix}_H; gy++)
      for (int gx = 0; gx < g.w; gx++)
        if ({prefix}_BITS[g.off + gy * stride + gx / 8] & (0x80 >> (gx & 7)))
          canvas->drawPixel(x + g.xoff + gx, y + gy, col);
    x += g.adv;
  }}
}}
""")
print(f"wrote {out}: cellule H={H}, {len(bits)} octets")
