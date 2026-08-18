#!/usr/bin/env python3
# Genere src/emoji_font.h : set d'emojis courants rendus en bitmaps 18x18
# RGB565 depuis Apple Color Emoji, PRE-MELANGES SUR BLANC (la pilule message
# du badge est blanche) + masque de presence par ligne (fond de pilule).
# Usage : python3 tools/gen_emoji.py  (depuis la racine du repo)
from PIL import Image, ImageFont, ImageDraw

EMOJIS = [
    0x1F44B, 0x1F600, 0x1F601, 0x1F602, 0x1F605, 0x1F609, 0x1F60D, 0x1F60E,
    0x1F642, 0x1F61C, 0x1F919, 0x1F44D, 0x1F64C, 0x1F44F, 0x1F4AA, 0x2764,
    0x1F525, 0x1F389, 0x2728, 0x26A1, 0x1F680, 0x1F308, 0x2615, 0x1F355,
    0x1F3AE, 0x1F916, 0x1F4BB, 0x1F440, 0x1F49C, 0x1F4A5,
]
S = 18  # taille finale en px

font = ImageFont.truetype('/System/Library/Fonts/Apple Color Emoji.ttc', 26)
entries = []
for cp in EMOJIS:
    im = Image.new('RGBA', (40, 40), (0, 0, 0, 0))
    ImageDraw.Draw(im).text((2, 2), chr(cp), font=font, embedded_color=True)
    bbox = im.getbbox()
    if not bbox:
        print('skip (non rendu) U+%04X' % cp)
        continue
    g = im.crop(bbox).resize((S, S), Image.LANCZOS)
    px, mask = [], []
    for y in range(S):
        row = 0
        for x in range(S):
            r, gg, b, a = g.getpixel((x, y))
            if a > 20:
                row |= 1 << x
            # pre-melange sur blanc
            r = (r * a + 255 * (255 - a)) // 255
            gg = (gg * a + 255 * (255 - a)) // 255
            b = (b * a + 255 * (255 - a)) // 255
            px.append(((r >> 3) << 11) | ((gg >> 2) << 5) | (b >> 3))
        mask.append(row)
    entries.append((cp, px, mask))

out = ['// Emojis bitmap 18x18 RGB565 pre-melanges sur BLANC — genere par',
       '// tools/gen_emoji.py depuis Apple Color Emoji. Utilise par emoji_text.h.',
       '#pragma once', '#include <stdint.h>', '',
       '#define EMJ_S %d' % S,
       '#define EMJ_N %d' % len(entries), '',
       'struct EmjGlyph { uint32_t cp; uint32_t mask[%d]; uint16_t px[%d]; };' % (S, S * S),
       '', 'static const EmjGlyph EMJ_GLYPHS[EMJ_N] = {']
for cp, px, mask in entries:
    out.append('  {0x%X, {%s},' % (cp, ','.join(str(m) for m in mask)))
    lines = []
    for i in range(0, len(px), 18):
        lines.append('    ' + ','.join(str(p) for p in px[i:i + 18]) + ',')
    out.append('   {\n' + '\n'.join(lines) + '\n   }},')
out.append('};')
open('src/emoji_font.h', 'w').write('\n'.join(out) + '\n')
print('%d emojis -> src/emoji_font.h' % len(entries))
