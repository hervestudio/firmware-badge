// Reaction "un ami est la" — PARTAGE firmware / emulateur. Quand un autre
// badge est detecte a proximite (ESP-NOW cote firmware, export de test cote
// emulateur), le Conf Buddy joue une des trois expressions portees du
// visualiseur speaker-badge-anims (triggers Happy / Wow / Love de
// screen-anims.js), tiree au hasard, pendant qu'une pilule "<Nom> 👋"
// glisse du haut de l'ecran.
//   HAPPY : yeux en arcs ^ + etoiles qui tournent a cote + rebond du visage
//   WOW   : etoiles qui tournent DANS les yeux + petite bouche en O
//   LOVE  : yeux en coeur rouges + anneaux "battement de coeur" (lub-dub)
//           + petits coeurs qui montent
// (la sphere plein ecran est blittee par tables precalculees : le
// scale/bounce du JS est traduit en mouvement du visage et en effets.)
// Depend de : canvas, rgb565, RGB565_WHITE, CX/CY, millis, avStroke,
// mxPrint/mxTextW (emoji_text.h), sinf/cosf/expf.
#pragma once
#include "emoji_text.h"

static char socialReactName[24] = "";
static uint32_t socialReactUntil = 0; // millis() de fin de la reaction
static uint8_t socialReactType = 0;   // 0 happy, 1 wow, 2 love

#define SOCIAL_REACT_MS 5000

static void socialReactTrigger(const char *name, uint32_t now)
{
  static uint32_t seed = 0x2A5F17u;
  seed = seed * 1103515245u + now + 12345u;
  socialReactType = (uint8_t)((seed >> 16) % 3);
  snprintf(socialReactName, sizeof(socialReactName), "%s", name);
  socialReactUntil = now + SOCIAL_REACT_MS;
}

// ---- helpers de dessin -------------------------------------------------

// etoile 5 branches qui tourne (etoiles des triggers Happy/Wow)
static void socialStar(float cx, float cy, float R, float ang, uint16_t col)
{
  canvas->fillCircle((int)cx, (int)cy, (int)(R * 0.45f), col);
  for (int k = 0; k < 5; k++)
  {
    float a = ang + k * 2.0f * (float)PI / 5.0f;
    canvas->fillCircle((int)(cx + cosf(a) * R * 0.62f),
                       (int)(cy + sinf(a) * R * 0.62f), (int)(R * 0.32f), col);
  }
}

// coeur plein (yeux Love + particules)
static void socialHeart(float cx, float cy, float s, uint16_t col)
{
  canvas->fillCircle((int)(cx - s * 0.35f), (int)(cy - s * 0.22f),
                     (int)(s * 0.42f), col);
  canvas->fillCircle((int)(cx + s * 0.35f), (int)(cy - s * 0.22f),
                     (int)(s * 0.42f), col);
  canvas->fillTriangle((int)(cx - s * 0.70f), (int)(cy - s * 0.02f),
                       (int)(cx + s * 0.70f), (int)(cy - s * 0.02f),
                       (int)cx, (int)(cy + s * 0.80f), col);
}

// anneau "pulse ring" du mode Love : nait au bord de la sphere et s'etend en
// palissant (ease-out), 3 cercles concentriques pour l'epaisseur
static void socialRing(float age, float life, float fr)
{
  if (age <= 0 || age >= life)
    return;
  float p = age / life, e = 1 - (1 - p) * (1 - p);
  int rad = (int)(fr * 0.95f + (255 - fr * 0.95f) * e);
  float fade = 1 - p;
  uint16_t col = rgb565((uint8_t)(255 * fade), (uint8_t)(110 * fade),
                        (uint8_t)(175 * fade));
  for (int k = -1; k <= 1; k++)
    canvas->drawCircle(CX, CY, rad + k, col);
}

// Remplace le visage de l'avatar pendant la reaction (appele en tete de
// drawIdleFaceLook). fr > 100 = seulement le buddy plein ecran (RADIUS 180,
// jusqu'a ~120 avec le battement Love), pas les previews a 78 et moins
// (Settings, Setup, QR). Retourne true si l'expression a dessine.
static bool socialExprFace(float cx, float cy, float fr)
{
  uint32_t now = millis();
  if (!socialReactName[0] || now >= socialReactUntil || fr < 100)
    return false;
  float tA = (SOCIAL_REACT_MS - (int)(socialReactUntil - now)) / 1000.0f;
  float pop = tA < 0.3f ? tA / 0.3f : 1.0f; // pop-in des elements
  pop = 1 - (1 - pop) * (1 - pop);
  const uint16_t ink = rgb565(39, 39, 39);
  const uint16_t star = rgb565(255, 224, 102);
  const uint16_t red = rgb565(240, 50, 85);
  float ex = fr * 0.36f, ey = -fr * 0.19f, er = fr * 0.105f;
  float spin = tA * 3.2f;
  float osc = 1 + 0.22f * sinf(tA * 6.0f);

  switch (socialReactType)
  {
  case 0: // HAPPY — arcs ^, etoiles a cote (le rebond est porte par la
          // sphere entiere via g_sphereYOff, visage compris)
  {
    avStroke(3, cx - ex - er * 1.1f, er * 2.2f, cy + ey + er * 0.35f,
             er * 0.85f, er * 0.30f, ink);
    avStroke(3, cx + ex - er * 1.1f, er * 2.2f, cy + ey + er * 0.35f,
             er * 0.85f, er * 0.30f, ink);
    avStroke(0, cx - fr * 0.16f, fr * 0.32f, cy + fr * 0.11f, fr * 0.05f,
             fr * 0.030f, ink);
    float sz = er * 0.85f * pop * osc;
    socialStar(cx - ex - er * 1.6f, cy + ey - er * 0.9f, sz, spin, star);
    socialStar(cx + ex + er * 1.6f, cy + ey - er * 0.9f, sz, -spin, star);
    break;
  }
  case 1: // WOW — etoiles qui tournent dans les yeux, petite bouche en O
  {
    canvas->fillCircle((int)(cx - ex), (int)(cy + ey), (int)er, ink);
    canvas->fillCircle((int)(cx + ex), (int)(cy + ey), (int)er, ink);
    float sz = er * 0.78f * pop * osc;
    socialStar(cx - ex, cy + ey - er * 0.15f, sz, spin, star);
    socialStar(cx + ex, cy + ey - er * 0.15f, sz, spin, star);
    canvas->fillCircle((int)cx, (int)(cy + fr * 0.13f), (int)(fr * 0.070f), ink);
    break;
  }
  default: // LOVE — yeux coeur, lub-dub d'anneaux, petits coeurs qui montent
  {
    // battement lub-dub (56 bpm comme le JS) : deux anneaux par cycle
    const float T = 60.0f / 56.0f;
    float base = floorf(tA / T) * T;
    for (int b = -1; b <= 0; b++) // battement courant + precedent
    {
      float tb = base + b * T;
      if (tb < 0)
        continue;
      socialRing(tA - tb, 1.3f, fr);          // lub
      socialRing(tA - tb - 0.20f, 1.0f, fr);  // dub, plus court
    }
    socialHeart(cx - ex, cy + ey, er * 1.5f * pop, red);
    socialHeart(cx + ex, cy + ey, er * 1.5f * pop, red);
    avStroke(0, cx - fr * 0.14f, fr * 0.28f, cy + fr * 0.11f, fr * 0.045f,
             fr * 0.028f, ink);
    // petits coeurs qui montent sur les cotes (spawn regulier, vie 1,6 s)
    for (int i = 0; i < 8; i++)
    {
      float born = i * 0.55f;
      float age = tA - born;
      if (age <= 0 || age >= 1.6f)
        continue;
      unsigned h = (unsigned)(i * 2654435761u);
      float side = (i & 1) ? 1.0f : -1.0f;
      float x = cx + side * (fr * 0.55f + (h & 31)) + sinf(age * 4 + i) * 8;
      float y = cy + fr * 0.25f - age * 65.0f;
      float fade = age < 1.1f ? 1.0f : (1.6f - age) / 0.5f;
      uint16_t c = rgb565((uint8_t)(255 * fade), (uint8_t)(90 * fade),
                          (uint8_t)(150 * fade));
      socialHeart(x, y, 7 + (h >> 5 & 7), c);
    }
    break;
  }
  }
  return true;
}

// Pilule "<Nom> 👋" qui glisse du haut (par-dessus l'anim idle, avant flush).
// Met aussi a jour, pour la frame SUIVANTE, le gel de rotation et le rebond
// de la sphere (g_lookFreeze / g_sphereYOff, lus par animIdleRainbow) : la
// sphere suit le mouvement de la reaction comme dans le visualiseur.
static void socialReactDraw(uint32_t now)
{
  bool active = socialReactName[0] && now < socialReactUntil;
  g_lookFreeze += ((active ? 1.0f : 0.0f) - g_lookFreeze) * 0.22f;
  if (g_lookFreeze < 0.01f)
    g_lookFreeze = 0;
  g_sphereYOff = 0;
  g_sphereScale = 1.0f;
  if (active)
  {
    float tA = (SOCIAL_REACT_MS - (int)(socialReactUntil - now)) / 1000.0f;
    if (socialReactType == 0) // HAPPY : bounce 5,5 Hz (0.045 R)
      g_sphereYOff = (int)(-fabsf(sinf(tA * 5.5f * (float)PI)) * 8.0f * g_lookFreeze);
    else if (socialReactType == 1) // WOW : bounce leger 3 Hz (0.03 R)
      g_sphereYOff = (int)(-fabsf(sinf(tA * 3.0f * (float)PI)) * 5.0f * g_lookFreeze);
    else // LOVE : la sphere bat au rythme du coeur (heartbeatScale du JS :
    {    // ressorts lub-dub amortis + micro-respiration, base reduite)
      const float T = 60.0f / 56.0f;
      float x = fmodf(tA, T);
      float lub = expf(-5.0f * x) * sinf(15.0f * x);
      float dub = x > 0.17f ? expf(-6.0f * (x - 0.17f)) * sinf(16.0f * (x - 0.17f))
                            : 0.0f;
      float hb = 1.0f + 0.36f * lub + 0.22f * dub + 0.025f * sinf(tA * 1.5f);
      g_sphereScale = 1.0f + (0.80f * hb - 1.0f) * g_lookFreeze;
    }
  }
  if (!active)
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
