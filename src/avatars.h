// 40 avatars - variations of the "idle rainbow" character, one per badge
// (threejs.paris speaker or crew). The active avatar is picked in More >
// Settings (code 39193) and persists in NVS ("avatar"): it colors the
// character's sphere (hue rotation + saturation of PAL_RAINBOW in
// irGenFrame) and picks its FACE among the 9 Figma designs (node 4195-8272).
//
// File shared firmware / emulator. Include BEFORE anims_extra.h
// (irGenFrame and animIdleRainbow read g_avatarIdx / irDirtyMask) and after
// the canvas declaration. The muzzle (face 0) is rendered by the platform
// (drawMouthImg on firmware, drawMouth on the emulator) via the
// avatarPlatformMouth wrapper defined in each TU.
#pragma once

// defined later in anims_extra.h (used by irGenFrame for the avatar's
// hue transformation)
static void rgb2hsl(float r, float g, float b, float *h, float *s, float *l);
static void hsl2rgb(float h, float s, float l, float *r, float *g, float *b);
// original character's muzzle, platform-rendered (defined after the include)
static void avatarPlatformMouth(float mx, float my, float mw, float mh, uint16_t ink);
// "laugh" face mouth (black + white SVG mask), also per platform
static void avatarPlatformLaugh(float mx, float my, float mw, float mh, uint16_t ink);

// The 9 faces from Figma 4195-8272, left to right. Geometry extracted
// from the Figma metadata (eye/mouth frames), scale calibrated on the
// original character's eye spacing: 1 Figma px = 0.005712 * fr.
enum AvatarFace : uint8_t
{
  AF_MUSEAU = 0,   // muzzle + whiskers (the original character)
  AF_RIRE,         // big happy open mouth, white lower part
  AF_SOURIRE,      // small thin smile
  AF_VAGUE,        // small wavy mouth
  AF_CHAT,         // oval eyes + cat mouth (omega)
  AF_LUNETTES,     // sunglasses (bar + lenses) + smirk
  AF_FERMES,       // big happy closed eyes ^^ + small smile
  AF_MOUSTACHE,    // big wavy mustache
  AF_ETOILES,      // 8-pointed star eyes linked by a bar
};

struct AvatarDef
{
  const char *name; // person (PIN-protected, also used by the social layer)
  const char *comp; // company (prefills "bcomp" like the name, QR card)
  int16_t hue;      // hue rotation (deg) applied to PAL_RAINBOW
  float sat;        // saturation multiplier
  uint8_t face;     // AvatarFace
  uint8_t extra;    // 0 none, 1 pink cheeks, 2 sparkle
};

// 39 people from "stickers-badges (5).json" (snapshot 2026-09-08):
// n = name, c = company. Hues spread over the wheel (83 deg steps),
// varied faces so that two neighboring badges do not look alike.
static const AvatarDef AVATARS[] = {
    {"Makio64", "", 0, 1.00f, AF_MUSEAU, 0},
    {"Kim", "", 83, 1.05f, AF_LUNETTES, 0},
    {"Robin", "TSL Zelda", 166, 1.10f, AF_MUSEAU, 1},
    {"Cassie", "GSAP", 249, 1.00f, AF_CHAT, 2},
    {"Vicente", "Abeto", 332, 1.05f, AF_ETOILES, 0},
    {"Celia", "", 55, 1.10f, AF_VAGUE, 0},
    {"Thomas", "Google", 138, 1.00f, AF_MOUSTACHE, 1},
    {"Julie", "Herve Studio", 221, 1.05f, AF_SOURIRE, 0},
    {"Romain", "Herve Studio", 304, 1.10f, AF_FERMES, 0},
    {"Mr.doob", "three.js", 27, 1.00f, AF_RIRE, 0},
    {"Justine", "Ponpon Mania", 110, 1.05f, AF_LUNETTES, 1},
    {"Patrick", "Ponpon Mania", 193, 1.10f, AF_MUSEAU, 0},
    {"Sean", "Miris", 276, 1.00f, AF_CHAT, 0},
    {"Daria", "", 359, 1.05f, AF_ETOILES, 0},
    {"Sunag", "TSL creator", 82, 1.10f, AF_VAGUE, 1},
    {"Renaud", "utsubo", 165, 1.00f, AF_MOUSTACHE, 0},
    {"Anderson", "Neotix", 248, 1.05f, AF_SOURIRE, 0},
    {"Edan", "Lusion", 331, 1.10f, AF_FERMES, 2},
    {"Antoine", "Merci Michel", 54, 1.00f, AF_RIRE, 1},
    {"Cassandre", "Moment Factory", 137, 1.05f, AF_LUNETTES, 0},
    {"Bruno", "Three.Js Journey", 220, 1.10f, AF_MUSEAU, 0},
    {"Dennis", "pmndrs", 303, 1.00f, AF_CHAT, 0},
    {"Natalia", "Google", 26, 1.05f, AF_ETOILES, 1},
    {"Kris", "pmndrs", 109, 1.10f, AF_VAGUE, 0},
    {"Lovis", "fal.ai", 192, 1.00f, AF_MOUSTACHE, 2},
    {"Misha", "edclub", 275, 1.05f, AF_SOURIRE, 0},
    {"Misaki", "bonobo", 358, 1.10f, AF_FERMES, 1},
    {"Umut", "fal.ai", 81, 1.00f, AF_RIRE, 0},
    {"Bryan", "miris", 164, 1.05f, AF_LUNETTES, 0},
    {"Marc", "Vercel", 247, 1.10f, AF_MUSEAU, 0},
    {"Spline", "", 330, 1.00f, AF_CHAT, 1},
    {"Stijn", "flux", 53, 1.05f, AF_ETOILES, 2},
    {"Tanya", "shopify", 136, 1.10f, AF_VAGUE, 0},
    {"Daniel", "shopify", 219, 1.00f, AF_MOUSTACHE, 0},
    {"Francisco", "joyco", 302, 1.05f, AF_SOURIRE, 1},
    {"Zubin", "edclub", 25, 1.10f, AF_FERMES, 0},
    {"Mike", "edclub", 108, 1.00f, AF_RIRE, 0},
    {"Arnaud", "shopify", 191, 1.05f, AF_LUNETTES, 0},
    {"Damian", "shopify", 274, 1.10f, AF_MUSEAU, 1},
};
#define AVATAR_N ((int)(sizeof(AVATARS) / sizeof(AVATARS[0])))

static uint8_t g_avatarIdx = 0;     // SAVED avatar (NVS): colors the sphere
static uint8_t g_avatarFaceIdx = 0; // avatar SHOWN by the face (= saved,
                                    // except during the Settings preview)
static bool g_ballDirty = false; // ballSprite (snake/DVD/games) to regen
static uint32_t irDirtyMask = 0; // bit i = idle rotation frame i to
                                 // regenerate (avatar/buddy changed); the
                                 // DISPLAYED frames are redone first
// driven by the social reaction (social_ui.h) during an encounter:
// sphere rotation freeze (the visualizer triggers freeze the gaze) and
// vertical bounce of the blit (Happy/Wow bounce)
static float g_lookFreeze = 0.0f;  // 0 = free, 1 = gaze frozen
static int g_sphereYOff = 0;       // vertical offset of the sphere (px)
static float g_sphereScale = 1.0f; // sphere scale (Love heartbeat)

// ---- CUSTOM buddy (phone Setup flow): when active, it replaces the table
// avatar for both the sphere color AND the face. Persists in NVS
// (bcust/bhue/bsat/bface); picking an avatar in Settings disables it.
static bool g_buddyCustom = false;
static AvatarDef g_buddyCustomDef = {"Custom", "", 0, 1.00f, AF_MUSEAU, 0};
static int g_faceForce = -1; // >=0: forces a table avatar (Settings
                             // preview, even when custom is active)
static inline const AvatarDef &avatarCurrent()
{
  if (g_faceForce >= 0)
    return AVATARS[g_faceForce];
  return g_buddyCustom ? g_buddyCustomDef : AVATARS[g_avatarFaceIdx];
}

// ---- face rendering ----------------------------------------------------

// sphere projection context (filled by avatarDrawFace, used by the
// helpers): gaze rotation around Y
static float avCosT = 1, avSinT = 0, avCx = 0, avCy = 0, avFr = 1;
static float avBreathe = 0, avYShift = 0;

// projects a point (nx, ny) of the unit disc: screen x, lateral squish,
// visibility (rotated z)
static void avProject(float nx, float ny, float *sx, float *scale, float *vis)
{
  float nzsq = 1 - nx * nx - ny * ny;
  float nz = nzsq > 0 ? sqrtf(nzsq) : 0.1f;
  float nxr = nx * avCosT + nz * avSinT;
  float nzr = -nx * avSinT + nz * avCosT;
  *sx = avCx + nxr * avFr + avBreathe * 0.6f;
  *scale = nzr > 0.3f ? nzr : 0.3f;
  *vis = nzr;
}
static float avY(float ny) // screen y of a face point
{
  return avCy + ny * avFr + avBreathe * 0.4f + avYShift;
}

// ---- anti-aliasing (review 2026-09-07 (Romain): jagged faces) ----
// blends a pixel with the ink for a 0..1 coverage (framebuffer read:
// canvas-> API on both platforms)
static inline void avBlend(int x, int y, uint16_t ink, float a)
{
  if (x < 0 || x >= W || y < 0 || y >= H || a <= 0.003f)
    return;
  uint16_t *fb = canvas->getFramebuffer();
  if (a >= 0.997f)
  {
    fb[y * W + x] = ink;
    return;
  }
  uint16_t d = fb[y * W + x];
  int dr = (d >> 11) & 31, dg = (d >> 5) & 63, db = d & 31;
  int ir = (ink >> 11) & 31, ig = (ink >> 5) & 63, ib = ink & 31;
  int r = dr + (int)((ir - dr) * a);
  int g = dg + (int)((ig - dg) * a);
  int b = db + (int)((ib - db) * a);
  fb[y * W + x] = (uint16_t)((r << 11) | (g << 5) | b);
}

// anti-aliased filled ellipse (edge softened over ~1 px)
static void avFillEllipseAA(float cx, float cy, float rx, float ry, uint16_t ink)
{
  if (rx < 0.5f || ry < 0.5f)
    return;
  float rm = rx < ry ? rx : ry;
  int x0 = (int)(cx - rx - 1), x1 = (int)(cx + rx + 1);
  int y0 = (int)(cy - ry - 1), y1 = (int)(cy + ry + 1);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
    {
      float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
      float d = sqrtf(dx * dx + dy * dy);
      float a = (1 - d) * rm + 0.5f;
      if (a <= 0)
        continue;
      avBlend(x, y, ink, a > 1 ? 1 : a);
    }
}

// thick stroke along a curve: series of discs (no native arc).
// mode: 0 = soft downward arc (smile), 1 = 1.5-period sine wave,
// 2 = cat omega (2 downward bumps), 3 = upward arc (closed eye),
// 4 = tilted smirk (rises to the right)
static void avStroke(int mode, float x0, float w, float yBase, float amp,
                     float r, uint16_t ink)
{
  int n = (int)(w / (r > 1 ? r * 0.5f : 1)) + 4;
  for (int i = 0; i <= n; i++)
  {
    float u = (float)i / n, y = yBase;
    switch (mode)
    {
    case 0: y = yBase + amp * (0.25f - (u - 0.5f) * (u - 0.5f)) * 4.0f; break;
    case 1: y = yBase + amp * sinf(u * 3.0f * (float)PI); break;
    case 2: y = yBase + amp * fabsf(sinf(u * 2.0f * (float)PI)); break; // 2 bumps
    case 3: y = yBase - amp * sinf(u * (float)PI); break;
    case 4: y = yBase - amp * u + amp * 0.5f * (0.25f - (u - 0.5f) * (u - 0.5f)) * 4.0f; break;
    }
    avFillEllipseAA(x0 + u * w, y, r, r, ink);
  }
}

// 8-pointed star (AF_ETOILES eyes): central disc + 8 lobes
static void avStar(float cx, float cy, float R, uint16_t ink)
{
  avFillEllipseAA(cx, cy, R * 0.72f, R * 0.72f, ink);
  for (int k = 0; k < 8; k++)
  {
    float a = k * (float)PI / 4.0f;
    avFillEllipseAA(cx + cosf(a) * R * 0.68f, cy + sinf(a) * R * 0.68f,
                    R * 0.36f, R * 0.36f, ink);
  }
}

// Full face of the displayed avatar, projected on the sphere (cx, cy, fr).
// breathe/yShift: idle micro-movements; cosT/sinT: gaze rotation;
// openness: blink (1 = open).
static void avatarDrawFace(float cx, float cy, float fr, float breathe,
                           float yShift, float cosT, float sinT,
                           float openness, uint16_t ink)
{
  const AvatarDef &av = avatarCurrent();
  avCx = cx; avCy = cy; avFr = fr;
  avCosT = cosT; avSinT = sinT;
  avBreathe = breathe; avYShift = yShift;

  float exl, exr, mxx, scl, scr, scm, visl, visr, vism;

  switch (av.face)
  {
  // Geometry extracted from Figma board 4197-8511 (faces laid on 310 px
  // spheres -> dimensions in radius units, symmetrized).
  // -------------------------------------------- muzzle (original character)
  case AF_MUSEAU:
  {
    avProject(-0.381f, -0.210f, &exl, &scl, &visl);
    avProject(0.381f, -0.210f, &exr, &scr, &visr);
    avProject(0, 0.074f, &mxx, &scm, &vism);
    float er = fr * 0.099f;
    float ryf = er * openness; if (ryf < 1) ryf = 1;
    if (visl > 0)
      avFillEllipseAA(exl, avY(-0.210f), er * scl, ryf, ink);
    if (visr > 0)
      avFillEllipseAA(exr, avY(-0.210f), er * scr, ryf, ink);
    avatarPlatformMouth(mxx, avY(0.074f), fr * 0.316f * scm, fr * 0.342f, ink);
    break;
  }
  // ---------------------------------------------- laugh: big open mouth
  case AF_RIRE:
  {
    // realigned on the reference render (image Romain 2026-09-07): mouth
    // is ROUNDED-SQUARE (no longer an ellipse), white bowl starting just
    // above the center, black edging kept at the bottom and on the sides
    // scale/position: 2nd reference Romain 2026-09-07 (mouth enlarged,
    // eyes realigned)
    avProject(-0.465f, -0.215f, &exl, &scl, &visl);
    avProject(0.465f, -0.215f, &exr, &scr, &visr);
    avProject(0, 0.085f, &mxx, &scm, &vism);
    float er = fr * 0.095f;
    float ryf = er * openness; if (ryf < 1) ryf = 1;
    if (visl > 0)
      avFillEllipseAA(exl, avY(-0.255f), er * scl, ryf, ink);
    if (visr > 0)
      avFillEllipseAA(exr, avY(-0.255f), er * scr, ryf, ink);
    // mouth = tessellated mask from the SVG export Mouth_visage2.svg
    // (73x59), platform-rendered - see initLaughMask (SVG ratio kept)
    avatarPlatformLaugh(mxx, avY(0.085f), fr * 0.485f * scm, fr * 0.392f, ink);
    break;
  }
  // ------------------------------------------------------ small thin smile
  case AF_SOURIRE:
  {
    avProject(-0.431f, -0.213f, &exl, &scl, &visl);
    avProject(0.431f, -0.213f, &exr, &scr, &visr);
    avProject(0, -0.032f, &mxx, &scm, &vism);
    float er = fr * 0.084f;
    float ryf = er * openness; if (ryf < 1) ryf = 1;
    if (visl > 0)
      avFillEllipseAA(exl, avY(-0.213f), er * scl, ryf, ink);
    if (visr > 0)
      avFillEllipseAA(exr, avY(-0.213f), er * scr, ryf, ink);
    float w = fr * 0.380f * scm;
    avStroke(0, mxx - w / 2, w, avY(-0.038f) - fr * 0.010f, fr * 0.025f,
             fr * 0.026f, ink);
    break;
  }
  // -------------------------------------------------------- small wave
  case AF_VAGUE:
  {
    avProject(-0.431f, -0.194f, &exl, &scl, &visl);
    avProject(0.431f, -0.194f, &exr, &scr, &visr);
    avProject(0, 0.035f, &mxx, &scm, &vism);
    float er = fr * 0.103f;
    float ryf = er * openness; if (ryf < 1) ryf = 1;
    if (visl > 0)
      avFillEllipseAA(exl, avY(-0.194f), er * scl, ryf, ink);
    if (visr > 0)
      avFillEllipseAA(exr, avY(-0.194f), er * scr, ryf, ink);
    float w = fr * 0.440f * scm;
    avStroke(1, mxx - w / 2, w, avY(0.032f), fr * 0.018f, fr * 0.028f, ink);
    break;
  }
  // --------------------------------------- cat: oval eyes + wide omega
  case AF_CHAT:
  {
    avProject(-0.316f, -0.204f, &exl, &scl, &visl);
    avProject(0.316f, -0.204f, &exr, &scr, &visr);
    avProject(0, 0.150f, &mxx, &scm, &vism);
    float rx = fr * 0.075f, ryv = fr * 0.094f;
    float ryf = ryv * openness; if (ryf < 1) ryf = 1;
    if (visl > 0)
      avFillEllipseAA(exl, avY(-0.204f), rx * scl, ryf, ink);
    if (visr > 0)
      avFillEllipseAA(exr, avY(-0.204f), rx * scr, ryf, ink);
    float w = fr * 0.580f * scm, amp = fr * 0.105f;
    avStroke(2, mxx - w / 2, w, avY(0.100f), amp, fr * 0.035f, ink);
    break;
  }
  // ------------------------------- sunglasses: bar + lenses + smirk
  case AF_LUNETTES:
  {
    avProject(-0.200f, -0.213f, &exl, &scl, &visl);
    avProject(0.200f, -0.213f, &exr, &scr, &visr);
    avProject(0.184f, 0.058f, &mxx, &scm, &vism);
    float barTop = avY(-0.297f), barH = fr * 0.065f;
    float lensHalf = fr * 0.174f, lensDrop = fr * 0.103f;
    float xL = exl - lensHalf * scl, xR = exr + lensHalf * scr;
    canvas->fillRoundRect((int)xL, (int)barTop, (int)(xR - xL), (int)barH,
                          (int)(barH * 0.3f), ink);
    for (int s = 0; s < 2; s++)
    {
      float c = s ? exr : exl, sc = s ? scr : scl;
      float hw = lensHalf * sc;
      canvas->fillRect((int)(c - hw), (int)(barTop + barH - 1), (int)(2 * hw),
                       (int)(lensDrop * 0.5f), ink);
      avFillEllipseAA(c, barTop + barH + lensDrop * 0.5f, hw,
                      lensDrop * 0.5f, ink);
    }
    float w = fr * 0.330f * scm;
    avStroke(4, mxx - w / 2, w, avY(0.058f) + fr * 0.045f, fr * 0.090f,
             fr * 0.030f, ink);
    break;
  }
  // ------------------------------ big closed eyes ^^ + small smile
  case AF_FERMES:
  {
    avProject(-0.271f, -0.184f, &exl, &scl, &visl);
    avProject(0.271f, -0.184f, &exr, &scr, &visr);
    avProject(0.090f, 0.178f, &mxx, &scm, &vism);
    float aw = fr * 0.297f, amp = fr * 0.050f, base = avY(-0.159f);
    avStroke(3, exl - aw * scl / 2, aw * scl, base, amp, fr * 0.030f, ink);
    avStroke(3, exr - aw * scr / 2, aw * scr, base, amp, fr * 0.030f, ink);
    float w = fr * 0.323f * scm;
    avStroke(0, mxx - w / 2, w, avY(0.178f) - fr * 0.012f, fr * 0.043f,
             fr * 0.027f, ink);
    break;
  }
  // ------------------------------------------------- big wavy mustache
  case AF_MOUSTACHE:
  {
    avProject(-0.342f, -0.168f, &exl, &scl, &visl);
    avProject(0.342f, -0.168f, &exr, &scr, &visr);
    avProject(0, 0.103f, &mxx, &scm, &vism);
    float er = fr * 0.084f;
    float ryf = er * openness; if (ryf < 1) ryf = 1;
    if (visl > 0)
      avFillEllipseAA(exl, avY(-0.168f), er * scl, ryf, ink);
    if (visr > 0)
      avFillEllipseAA(exr, avY(-0.168f), er * scr, ryf, ink);
    float w = fr * 0.826f * scm;
    avStroke(1, mxx - w / 2, w, avY(0.103f), fr * 0.033f, fr * 0.035f, ink);
    break;
  }
  // --------------------------- 8-pointed star eyes linked by a bar
  case AF_ETOILES:
  {
    avProject(-0.248f, -0.129f, &exl, &scl, &visl);
    avProject(0.248f, -0.129f, &exr, &scr, &visr);
    avProject(0.052f, 0.168f, &mxx, &scm, &vism);
    float ey = avY(-0.129f), R = fr * 0.142f;
    canvas->fillRect((int)exl, (int)(ey - fr * 0.035f), (int)(exr - exl),
                     (int)(fr * 0.070f), ink); // linking bar
    avStar(exl, ey, R * (0.7f + 0.3f * scl), ink);
    avStar(exr, ey, R * (0.7f + 0.3f * scr), ink);
    float w = fr * 0.374f * scm;
    avStroke(0, mxx - w / 2, w, avY(0.168f) - fr * 0.010f, fr * 0.035f,
             fr * 0.027f, ink);
    break;
  }
  }
}

// Extras drawn after the face. The CHEEKS follow the sphere projection
// (gaze + yShift), like the eyes/mouth - fixed 2026-08-17 (video from
// Romain: they stayed static during the anim). The SPARKLE stays fixed
// on screen, consistent with the sphere's baked highlights.
// (Reuses the av* context filled by avatarDrawFace just before.)
static void avatarDrawExtras(float cx, float cy, float fr, float breathe)
{
  const AvatarDef &av = avatarCurrent();
  if (av.extra == 1) // pink cheeks under the eyes
  {
    uint16_t blush = rgb565(246, 148, 168);
    float sxl, sxr, scl, scr, visl, visr;
    avProject(-0.40f, -0.055f, &sxl, &scl, &visl);
    avProject(0.40f, -0.055f, &sxr, &scr, &visr);
    float by = avY(-0.055f);
    if (visl > 0)
      canvas->fillEllipse((int)sxl, (int)by, (int)(fr * 0.085f * scl),
                          (int)(fr * 0.05f), blush);
    if (visr > 0)
      canvas->fillEllipse((int)sxr, (int)by, (int)(fr * 0.085f * scr),
                          (int)(fr * 0.05f), blush);
  }
  else if (av.extra == 2) // top-right sparkle (fixed on screen)
  {
    uint16_t w = rgb565(255, 252, 240);
    int sx = (int)(cx + fr * 0.47f), sy = (int)(cy - fr * 0.47f), s = (int)(fr * 0.075f);
    canvas->fillRect(sx - 1, sy - s, 3, 2 * s + 1, w);
    canvas->fillRect(sx - s, sy - 1, 2 * s + 1, 3, w);
  }
  (void)breathe;
}
