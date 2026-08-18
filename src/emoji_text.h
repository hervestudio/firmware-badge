// Rendu de texte MIXTE Dingos Medium + emojis bitmap (emoji_font.h) —
// PARTAGE firmware / emulateur. Le texte arrive en UTF-8 (webapp Setup) :
// les runs ASCII passent par mdPrint, les emojis connus sont dessines en
// bitmap 18x18 (pre-melanges sur blanc : a utiliser sur fond clair, la
// pilule message), le reste (variation selectors, ZWJ, emojis inconnus,
// accents non translitteres) est ignore silencieusement.
// Depend de : canvas, mdPrint/mdTextW, emoji_font.h.
#pragma once
#include "emoji_font.h"

static const EmjGlyph *emjFind(uint32_t cp)
{
  for (int i = 0; i < EMJ_N; i++)
    if (EMJ_GLYPHS[i].cp == cp)
      return &EMJ_GLYPHS[i];
  return nullptr;
}

// decode le codepoint UTF-8 a la position *s et avance *s
static uint32_t mxDecode(const char **s)
{
  const uint8_t *p = (const uint8_t *)*s;
  uint32_t cp = *p;
  int n = 0;
  if (cp >= 0xF0) { cp &= 7; n = 3; }
  else if (cp >= 0xE0) { cp &= 15; n = 2; }
  else if (cp >= 0xC0) { cp &= 31; n = 1; }
  (*s)++;
  while (n-- && ((uint8_t)**s & 0xC0) == 0x80)
    cp = (cp << 6) | ((uint8_t)*(*s)++ & 63);
  return cp;
}

#define MX_EMJ_ADV (EMJ_S + 2)

static int mxTextW(const char *s)
{
  char run[64];
  int w = 0, rl = 0;
  while (*s)
  {
    if ((uint8_t)*s < 0x80)
    {
      if (rl < 63)
        run[rl++] = *s;
      s++;
      continue;
    }
    run[rl] = 0;
    w += mdTextW(run);
    rl = 0;
    uint32_t cp = mxDecode(&s);
    if (emjFind(cp))
      w += MX_EMJ_ADV;
  }
  run[rl] = 0;
  return w + mdTextW(run);
}

// y = haut de la ligne Dingos Medium (les emojis sont centres dessus)
static void mxPrint(int x, int y, const char *s, uint16_t col)
{
  char run[64];
  int rl = 0;
  while (*s)
  {
    if ((uint8_t)*s < 0x80)
    {
      if (rl < 63)
        run[rl++] = *s;
      s++;
      continue;
    }
    run[rl] = 0;
    mdPrint(x, y, run, col);
    x += mdTextW(run);
    rl = 0;
    uint32_t cp = mxDecode(&s);
    const EmjGlyph *g = emjFind(cp);
    if (!g)
      continue;
    int ey = y + 7 - EMJ_S / 2; // centre vertical approx de la ligne
    for (int gy = 0; gy < EMJ_S; gy++)
      for (int gx = 0; gx < EMJ_S; gx++)
        if (g->mask[gy] & (1u << gx))
          canvas->drawPixel(x + gx, ey + gy, g->px[gy * EMJ_S + gx]);
    x += MX_EMJ_ADV;
  }
  run[rl] = 0;
  mdPrint(x, y, run, col);
}
