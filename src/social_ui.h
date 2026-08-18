// Reaction "un ami est la" — PARTAGE firmware / emulateur. Quand un autre
// badge est detecte a proximite (ESP-NOW cote firmware, export de test cote
// emulateur), une pilule glisse du haut de l'ecran par-dessus l'anim Conf
// Buddy : "<Nom> 👋", meme style que la pilule message de la carte.
// Depend de : canvas, rgb565, RGB565_WHITE, CX, mxPrint/mxTextW (emoji_text.h).
#pragma once
#include "emoji_text.h"

static char socialReactName[24] = "";
static uint32_t socialReactUntil = 0; // millis() de fin de la reaction

#define SOCIAL_REACT_MS 5000

static void socialReactTrigger(const char *name, uint32_t now)
{
  snprintf(socialReactName, sizeof(socialReactName), "%s", name);
  socialReactUntil = now + SOCIAL_REACT_MS;
}

// Dessine la reaction par-dessus la frame courante (a appeler juste avant le
// flush de l'anim idle). Glisse du haut a l'arrivee, remonte a la fin.
static void socialReactDraw(uint32_t now)
{
  if (!socialReactName[0] || now >= socialReactUntil)
    return;
  float tIn = (SOCIAL_REACT_MS - (int)(socialReactUntil - now)) / 1000.0f;
  float tOut = (socialReactUntil - now) / 1000.0f;
  // position verticale : ease-out a l'entree (0,35 s), ease-in a la sortie
  float k = 1.0f;
  if (tIn < 0.35f)
    k = tIn / 0.35f;
  else if (tOut < 0.35f)
    k = tOut / 0.35f;
  k = 1.0f - (1.0f - k) * (1.0f - k); // ease
  const int ph = 30, pad = 10;
  char msg[40];
  snprintf(msg, sizeof(msg), "%s \xF0\x9F\x91\x8B", socialReactName);
  int tw = mxTextW(msg), total = tw + 2 * pad;
  int y0 = (int)(-40 + k * (34 + 40)); // -40 (hors ecran) -> 34
  // corde du cercle au niveau de la pilule (bord haut le plus contraint)
  int dy = 181 - y0;
  if (dy > 179)
    dy = 179;
  int half = (int)sqrtf(180.0f * 180.0f - (float)dy * dy);
  if (half < 40)
    half = 40;
  int xmin = 181 - half + 4, xmax = 181 + half - 4;
  int L = CX - total / 2; // centree
  if (L + total > xmax)
    L = xmax - total;
  if (L < xmin)
    L = xmin;
  const int r = ph / 2, cy = y0 + r;
  const int rx0 = L + r, rw = total - ph;
  const uint16_t stroke = rgb565(0x3E, 0x3E, 0x3E);
  canvas->fillRect(rx0, y0 - 2, rw, ph + 4, stroke);
  canvas->fillCircle(rx0, cy, r + 2, stroke);
  canvas->fillCircle(rx0 + rw, cy, r + 2, stroke);
  canvas->fillRect(rx0, y0, rw, ph, RGB565_WHITE);
  canvas->fillCircle(rx0, cy, r, RGB565_WHITE);
  canvas->fillCircle(rx0 + rw, cy, r, RGB565_WHITE);
  mxPrint(L + pad, y0 + 8, msg, rgb565(0x21, 0x1C, 0x3B));
}
