// "A friend is here" reaction — SHARED firmware / emulator. When another
// badge is detected nearby (ESP-NOW on firmware, test export on the
// emulator), the Conf Buddy plays one of the three expressions ported from
// the speaker-badge-anims visualizer (Happy / Wow / Love triggers of
// screen-anims.js), picked at random, while a "<Name> 👋" pill slides
// down from the top of the screen.
//   HAPPY: arc ^ eyes + spinning stars beside them + face bounce
//   WOW  : stars spinning INSIDE the eyes + small O-shaped mouth
//   LOVE : red heart eyes + "heartbeat" rings (lub-dub)
//          + small hearts floating up
// (the full-screen sphere is blitted via precomputed tables: the JS
// scale/bounce is translated into face motion and effects.)
// Depends on: canvas, rgb565, RGB565_WHITE, CX/CY, millis, avStroke,
// mxPrint/mxTextW (emoji_text.h), sinf/cosf/expf.
#pragma once
#include "emoji_text.h"

static char socialReactName[24] = "";
static uint32_t socialReactUntil = 0; // millis() when the reaction ends
static uint8_t socialReactType = 0;   // 0 happy, 1 wow, 2 love

#define SOCIAL_REACT_MS 5000

// increments the encounter counter of the shared table (menu_ui.h) —
// NVS persistence is handled on the firmware side (social.h)
static void socialMetAdd(const char *name)
{
  for (int i = 0; i < metN; i++)
    if (strcmp(metNames[i], name) == 0)
    {
      metCounts[i]++;
      return;
    }
  if (metN < MET_MAX)
  {
    snprintf(metNames[metN], sizeof(metNames[0]), "%s", name);
    metCounts[metN] = 1;
    metN++;
  }
}

static void socialReactTrigger(const char *name, uint32_t now)
{
  static uint32_t seed = 0x2A5F17u;
  seed = seed * 1103515245u + now + 12345u;
  socialReactType = (uint8_t)((seed >> 16) % 3);
  snprintf(socialReactName, sizeof(socialReactName), "%s", name);
  socialReactUntil = now + SOCIAL_REACT_MS;
  socialMetAdd(name);
}

// ---- drawing helpers ---------------------------------------------------

// spinning 5-point star (stars of the Happy/Wow triggers)
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

// solid heart (Love eyes + particles)
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

// Love mode "pulse ring": born at the sphere's edge, expands while fading
// (ease-out), 3 concentric circles for thickness
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

// Replaces the avatar's face during the reaction (called at the top of
// drawIdleFaceLook). fr > 100 = only the full-screen buddy (RADIUS 180,
// down to ~120 with the Love heartbeat), not the previews at 78 and below
// (Settings, Setup, QR). Returns true if the expression was drawn.
static bool socialExprFace(float cx, float cy, float fr)
{
  uint32_t now = millis();
  if (!socialReactName[0] || now >= socialReactUntil || fr < 100)
    return false;
  float tA = (SOCIAL_REACT_MS - (int)(socialReactUntil - now)) / 1000.0f;
  float pop = tA < 0.3f ? tA / 0.3f : 1.0f; // element pop-in
  pop = 1 - (1 - pop) * (1 - pop);
  const uint16_t ink = rgb565(39, 39, 39);
  const uint16_t star = rgb565(255, 224, 102);
  const uint16_t red = rgb565(240, 50, 85);
  float ex = fr * 0.36f, ey = -fr * 0.19f, er = fr * 0.105f;
  float spin = tA * 3.2f;
  float osc = 1 + 0.22f * sinf(tA * 6.0f);

  switch (socialReactType)
  {
  case 0: // HAPPY — ^ arcs, stars beside them (the bounce is carried by
          // the whole sphere via g_sphereYOff, face included)
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
  case 1: // WOW — stars spinning in the eyes, small O-shaped mouth
  {
    canvas->fillCircle((int)(cx - ex), (int)(cy + ey), (int)er, ink);
    canvas->fillCircle((int)(cx + ex), (int)(cy + ey), (int)er, ink);
    float sz = er * 0.78f * pop * osc;
    socialStar(cx - ex, cy + ey - er * 0.15f, sz, spin, star);
    socialStar(cx + ex, cy + ey - er * 0.15f, sz, spin, star);
    canvas->fillCircle((int)cx, (int)(cy + fr * 0.13f), (int)(fr * 0.070f), ink);
    break;
  }
  default: // LOVE — heart eyes, lub-dub rings, small hearts floating up
  {
    // lub-dub beat (56 bpm like the JS): two rings per cycle
    const float T = 60.0f / 56.0f;
    float base = floorf(tA / T) * T;
    for (int b = -1; b <= 0; b++) // current + previous beat
    {
      float tb = base + b * T;
      if (tb < 0)
        continue;
      socialRing(tA - tb, 1.3f, fr);          // lub
      socialRing(tA - tb - 0.20f, 1.0f, fr);  // dub, shorter
    }
    socialHeart(cx - ex, cy + ey, er * 1.5f * pop, red);
    socialHeart(cx + ex, cy + ey, er * 1.5f * pop, red);
    avStroke(0, cx - fr * 0.14f, fr * 0.28f, cy + fr * 0.11f, fr * 0.045f,
             fr * 0.028f, ink);
    // small hearts floating up on the sides (regular spawn, 1.6 s life)
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

// "<Name> 👋" pill sliding down from the top (over the idle anim, before
// flush). Also updates, for the NEXT frame, the look freeze and the sphere
// bounce (g_lookFreeze / g_sphereYOff, read by animIdleRainbow): the
// sphere follows the reaction's motion just like in the visualizer.
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
    if (socialReactType == 0) // HAPPY: 5.5 Hz bounce (0.045 R)
      g_sphereYOff = (int)(-fabsf(sinf(tA * 5.5f * (float)PI)) * 8.0f * g_lookFreeze);
    else if (socialReactType == 1) // WOW: light 3 Hz bounce (0.03 R)
      g_sphereYOff = (int)(-fabsf(sinf(tA * 3.0f * (float)PI)) * 5.0f * g_lookFreeze);
    else // LOVE: the sphere beats with the heart (heartbeatScale from the
    {    // JS: damped lub-dub springs + micro-breathing, reduced base)
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
  // vertical position: ease-out on entry (0.35 s), ease-in on exit
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
  int y0 = (int)(-40 + k * (34 + 40)); // -40 (off screen) -> 34
  // circle chord at the pill's level (top edge is the most constrained)
  int dy = 181 - y0;
  if (dy > 179)
    dy = 179;
  int half = (int)sqrtf(180.0f * 180.0f - (float)dy * dy);
  if (half < 40)
    half = 40;
  int xmin = 181 - half + 4, xmax = 181 + half - 4;
  int L = CX - total / 2; // centered
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
