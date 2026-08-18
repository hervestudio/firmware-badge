// Animations "Three Globe" et "Three Conf" (vintage) portees de
// screen-anims.js. Fichier partage entre main.cpp et le harnais de test
// desktop : n'utiliser que l'API canvas->, rgb565/hsv2rgb565, frand,
// W/H/CX/CY/RADIUS.
#pragma once
#include <string.h>
#include "tc_logo.h"
#include "speaker_photo.h"
#include "speaker_photo3.h"

// ---------------------------------------------------- helpers CRT / couleurs

// Assombrit une ligne du framebuffer (~x0.81), pour les scanlines CRT
static void dimRow(int y, int x0, int x1)
{
  uint16_t *fb = canvas->getFramebuffer();
  uint16_t *row = &fb[y * W];
  for (int x = x0; x < x1; x++)
  {
    uint16_t c = row[x];
    uint16_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    row[x] = ((r * 13 >> 4) << 11) | ((g * 13 >> 4) << 5) | (b * 13 >> 4);
  }
}

// Gradient horizontal ecran du globe : magenta -> cyan -> jaune
static uint16_t globeGradient(int x)
{
  const int GR = (int)(RADIUS * 0.92f);
  float u = (float)(x - (CX - GR)) / (2 * GR);
  u = u < 0 ? 0 : (u > 1 ? 1 : u);
  int r, g, b;
  if (u < 0.5f)
  {
    float f = u * 2;
    r = 255 + (int)((70 - 255) * f);
    g = 90 + (int)((215 - 90) * f);
    b = 210 + (int)((245 - 210) * f);
  }
  else
  {
    float f = (u - 0.5f) * 2;
    r = 70 + (int)((250 - 70) * f);
    g = 215 + (int)((220 - 215) * f);
    b = 245 + (int)((95 - 245) * f);
  }
  return rgb565(r, g, b);
}

// ------------------------------------- logo TG (globe) : pretraitement + rendu

// Version du logo pour le globe : sous-titre "CONFERENCE" retire, contre-formes
// fermees marquees 3 (navy opaque), seul le fond exterieur reste transparent.
static int8_t *tgLogo = nullptr;

static void initTgLogo()
{
  const int n = TC_W * TC_H;
  tgLogo = (int8_t *)malloc(n);
  for (int i = 0; i < n; i++)
  {
    int8_t v = TC_DATA[i] - '0';
    if (v == 1 && i / TC_W >= 66)
      v = 0; // retire "CONFERENCE"
    tgLogo[i] = v;
  }
  // flood fill du fond exterieur depuis les bords
  uint8_t *ext = (uint8_t *)calloc(n, 1);
  int *stack = (int *)malloc(n * sizeof(int));
  int sp = 0;
  for (int x = 0; x < TC_W; x++)
  {
    stack[sp++] = x;
    stack[sp++] = x + (TC_H - 1) * TC_W;
  }
  for (int y = 0; y < TC_H; y++)
  {
    stack[sp++] = y * TC_W;
    stack[sp++] = TC_W - 1 + y * TC_W;
  }
  while (sp > 0)
  {
    int i = stack[--sp];
    if (ext[i] || tgLogo[i] != 0)
      continue;
    ext[i] = 1;
    int x = i % TC_W, y = i / TC_W;
    if (x > 0)
      stack[sp++] = i - 1;
    if (x < TC_W - 1)
      stack[sp++] = i + 1;
    if (y > 0)
      stack[sp++] = i - TC_W;
    if (y < TC_H - 1)
      stack[sp++] = i + TC_W;
  }
  for (int i = 0; i < n; i++)
    if (tgLogo[i] == 0 && !ext[i])
      tgLogo[i] = 3; // trou ferme -> navy
  free(stack);
  free(ext);
}

// Peint le logo TG par runs verticaux : cols[v] = couleur (0 -> transparent)
static void tgPaint(float offx, float offy, const uint16_t cols[4], const bool skip[4])
{
  const float scale = (2.0f * RADIUS * 0.90f) / TC_W;
  const float gw = TC_W * scale, gh = TC_H * scale;
  const float x0 = CX - gw / 2, y0 = CY - gh / 2;
  for (int gx = 0; gx < TC_W; gx++)
  {
    int cx0 = (int)(x0 + gx * scale + offx + 0.5f);
    int cw = max(1, (int)(x0 + (gx + 1) * scale + offx + 0.5f) - cx0);
    int run = 0, runStart = 0;
    for (int gy = 0; gy <= TC_H; gy++)
    {
      int v = gy < TC_H ? tgLogo[gy * TC_W + gx] : -1;
      if (v == run)
        continue;
      if (run > 0 && !skip[run])
      {
        int ry = (int)(y0 + runStart * scale + offy + 0.5f);
        int rh = max(1, (int)(y0 + gy * scale + offy + 0.5f) - ry);
        canvas->fillRect(cx0, ry, cw, rh, cols[run]);
      }
      run = v;
      runStart = gy;
    }
  }
}

// -------------------------------------------------------------- three globe

// Globe filaire retro (meridiens + paralleles) en degrade magenta/cyan/jaune
// fixe a l'ecran, logo THREE CONF .JS net par-dessus, scanlines CRT.
static void animGlobe(float t)
{
  const uint16_t NAVY = rgb565(10, 7, 38); // #0a0726

  canvas->fillScreen(rgb565(22, 20, 58)); // #16143a
  // lueur violette douce derriere le globe (degrade approxime)
  canvas->fillCircle(CX, (int)(CY * 0.94f), (int)(RADIUS * 0.75f), rgb565(34, 27, 66));
  canvas->fillCircle(CX, (int)(CY * 0.92f), (int)(RADIUS * 0.45f), rgb565(45, 33, 75));

  const float GR = RADIUS * 0.92f, tilt = 0.30f;
  const float ct = cosf(tilt), st = sinf(tilt);
  const float rotY = t * 0.4f;

  // trace une polyligne sur la sphere : halo dim decale +/-1px puis trait net
  // P(phi, lam) -> ecran
  auto project = [&](float phi, float lam, int *sx, int *sy) {
    float cphi = cosf(phi);
    float X = cphi * cosf(lam), Y = sinf(phi), Z = cphi * sinf(lam);
    *sx = CX + (int)(X * GR);
    *sy = CY - (int)((Y * ct - Z * st) * GR);
  };
  auto strokeSeg = [&](int x0, int y0, int x1, int y1) {
    int mx = (x0 + x1) / 2;
    uint16_t col = globeGradient(mx);
    uint16_t r = (col >> 11) & 31, g = (col >> 5) & 63, b = col & 31;
    uint16_t dim = ((r * 5 >> 4) << 11) | ((g * 5 >> 4) << 5) | (b * 5 >> 4);
    canvas->drawLine(x0, y0 - 1, x1, y1 - 1, dim);
    canvas->drawLine(x0, y0 + 1, x1, y1 + 1, dim);
    canvas->drawLine(x0, y0, x1, y1, col);
  };
  // 16 meridiens (26 segments)
  for (int j = 0; j < 16; j++)
  {
    float lam = (float)j / 16 * 2 * PI + rotY;
    int px = 0, py = 0;
    for (int k = 0; k <= 26; k++)
    {
      int sx, sy;
      project(-PI / 2 + (float)k / 26 * PI, lam, &sx, &sy);
      if (k > 0)
        strokeSeg(px, py, sx, sy);
      px = sx;
      py = sy;
    }
  }
  // 8 paralleles (52 segments)
  for (int i = 1; i < 9; i++)
  {
    float phi = -PI / 2 + (float)i / 9 * PI;
    int px = 0, py = 0;
    for (int k = 0; k <= 52; k++)
    {
      int sx, sy;
      project(phi, (float)k / 52 * 2 * PI + rotY, &sx, &sy);
      if (k > 0)
        strokeSeg(px, py, sx, sy);
      px = sx;
      py = sy;
    }
  }

  // logo net et plat par-dessus : 4 passes de contour navy puis vraies couleurs
  const uint16_t PINK = rgb565(0xfc, 0xa3, 0xf7), YELL = rgb565(0xfb, 0xd9, 0x75);
  const uint16_t outline[4] = {0, NAVY, NAVY, NAVY};
  const uint16_t colors[4] = {0, PINK, YELL, NAVY};
  const bool skipNone[4] = {true, false, false, false};
  float o = max(2.0f, RADIUS * 0.009f);
  tgPaint(-o, 0, outline, skipNone);
  tgPaint(o, 0, outline, skipNone);
  tgPaint(0, -o, outline, skipNone);
  tgPaint(0, o, outline, skipNone);
  tgPaint(0, 0, colors, skipNone);

  // scanlines CRT (1 ligne sur 3)
  for (int y = 0; y < H; y += 3)
    dimRow(y, 0, W);
}

// -------------------------------------------------------------- three conf

// Logo vintage : intro colonnes qui glissent de la gauche, lettres en
// arc-en-ciel anime, fond a trame de points, scanlines + bob vertical.
static void animThreeConf(float t, int yOff = 0)
{
  // yOff : decalage vertical optionnel (le splash de boot remonte le logo de
  // 20 px pour laisser respirer le loader ; l'anim du menu reste centree).
  const uint16_t BG = rgb565(0x0c, 0x14, 0x0e);
  const float cell = 1.5f; // echelle du logo (1.0 = taille native 185x80)
  const int gw = (int)(TC_W * cell), gh = (int)(TC_H * cell);
  const int x0 = CX - gw / 2;
  const int y0base = CY - gh / 2 + yOff;

  canvas->fillScreen(BG);
  // trame de points discrete
  uint16_t dotC = rgb565(19, 31, 22);
  for (int yy = 0; yy < H; yy += 8)
    for (int xx = 0; xx < W; xx += 8)
      canvas->fillRect(xx, yy, 2, 2, dotC);

  const float T_IN = 1.4f, SLIDE = 16, SLIDE_DIST = 46;
  const float front = (t / T_IN) * (TC_W + SLIDE);
  const int bob = (t > T_IN) ? (int)(sinf((t - T_IN) * 2.0f) * 2 + (sinf((t - T_IN) * 2.0f) > 0 ? 0.5f : -0.5f)) : 0;
  const int y0 = y0base + bob;

  for (int gx = 0; gx < TC_W; gx++)
  {
    if (gx > front)
      break;
    float fp = min(1.0f, max(0.0f, (front - gx) / SLIDE)); // 0 (arrive) -> 1 (cale)
    int dx = (int)(-(1 - fp) * (1 - fp) * SLIDE_DIST);
    int colX = x0 + (int)(gx * cell) + dx;
    int colW = max(1, x0 + (int)((gx + 1) * cell) - (x0 + (int)(gx * cell)));
    float alpha = 0.25f + 0.75f * fp;
    // arc-en-ciel anime sur les lettres (roses ET badge .JS)
    uint8_t hue = (uint8_t)(fmodf(gx * 2.4f + t * 130.0f, 360.0f) * 255.0f / 360.0f);
    uint16_t pinkC = hsv2rgb565(hue, 130, (uint8_t)(246 * alpha));
    uint16_t whiteC = rgb565((int)(255 * alpha), (int)(255 * alpha), (int)(255 * alpha));
    int run = 0, runStart = 0;
    for (int gy = 0; gy <= TC_H; gy++)
    {
      int v = gy < TC_H ? TC_DATA[gy * TC_W + gx] - '0' : -1;
      if (v == run)
        continue;
      if (run > 0)
      {
        int ry = y0 + (int)(runStart * cell);
        int rh = max(1, y0 + (int)(gy * cell) - ry);
        canvas->fillRect(colX, ry, colW, rh, (run == 1 || run == 2) ? pinkC : whiteC);
      }
      run = v;
      runStart = gy;
    }
  }

  // trait lumineux au front d'arrivee
  if (t < T_IN)
  {
    int fx = x0 + (int)(min((float)TC_W, front) * cell);
    canvas->fillRect(fx - 1, y0, 3, gh, rgb565(220, 220, 220));
  }

  // scanlines CRT (1 ligne sur 3)
  for (int y = 0; y < H; y += 3)
    dimRow(y, 0, W);
}

// ------------------------------------------------------------ idle rainbow

// Le perso plein ecran (drawCharacter 'rainbow') : texture sphere qui tourne
// avec le regard (11 frames -30..+30 deg, blend des 2 plus proches), vignette
// de bord + highlights fixes cuits dans les frames, grain ecran, visage idle.
#define IR_SPR 112
#define IR_FRAMES 11
#define IR_MAXROT 30.0f
#define IR_SIGMA2 (2 * 0.16f * 0.16f)
#define IR_CUTOFF2 (0.55f * 0.55f) // au-dela, poids gaussien negligeable
static uint16_t *irFrames[IR_FRAMES];
static uint16_t *irScratch = nullptr;
// tables de reechantillonnage bilineaire demi-resolution -> texture
static uint16_t irTIdx[W / 2];
static uint8_t irTFrac[W / 2];
static int8_t irNoise[256];
static float irExpLUT[130]; // expf(-d2/sigma2) tabule -> generation ~8x plus rapide

// Overscan identique au JS : la sphere est un peu plus grande que l'ecran
#define IR_SB (RADIUS * 0.13f)
#define IR_DRAWN (W + 2 * IR_SB)

static void irInit()
{
  for (int i = 0; i < IR_FRAMES; i++)
    irFrames[i] = (uint16_t *)malloc(IR_SPR * IR_SPR * sizeof(uint16_t));
  irScratch = (uint16_t *)malloc(IR_SPR * IR_SPR * sizeof(uint16_t));
  // le rendu se fait en demi-resolution (blocs 2x2) : echantillon au centre
  for (int x = 0; x < W / 2; x++)
  {
    float tf = (2 * x + 0.5f + IR_SB) * IR_SPR / IR_DRAWN - 0.5f;
    if (tf < 0)
      tf = 0;
    if (tf > IR_SPR - 2)
      tf = IR_SPR - 2;
    irTIdx[x] = (uint16_t)tf;
    irTFrac[x] = (uint8_t)((tf - (int)tf) * 16);
  }
  for (int i = 0; i < 256; i++)
    irNoise[i] = (int8_t)frand(-1.3f, 1.3f); // grain epars : +-1 dans ~25% des cas
  for (int i = 0; i < 130; i++)
    irExpLUT[i] = expf(-(i * (IR_CUTOFF2 / 128.0f)) / IR_SIGMA2);
}

// Genere une frame de rotation : blend gaussien des points tournes autour de Y,
// vibrance, puis vignette de bord + highlights (fixes a l'ecran) cuits dedans.
// Les couleurs passent par la transformation de l'AVATAR actif (teinte +
// saturation, voir avatars.h) : chaque badge a sa sphere.
static void irGenFrame(int fi)
{
  const float rotDeg = -IR_MAXROT + fi * (2 * IR_MAXROT / (IR_FRAMES - 1));
  const float theta = rotDeg * PI / 180.0f;
  const float cosT = cosf(theta), sinT = sinf(theta);
  const float satBoost = 1.75f, lumBoost = 1.12f;
  const float d2toLut = 128.0f / IR_CUTOFF2;
  const float rTex = IR_SPR * 229.0f / 466.0f; // rayon boule dans la frame (ratio JS)

  // couleurs de la palette transformees par l'avatar actif (ou le buddy
  // custom configure via Setup, qui prend le pas sur la table)
  const AvatarDef &av = g_buddyCustom ? g_buddyCustomDef : AVATARS[g_avatarIdx];
  float PC[PAL_N][3];
  for (unsigned k = 0; k < PAL_N; k++)
  {
    if (av.hue != 0 || av.sat != 1.0f)
    {
      float h, s, l;
      rgb2hsl(PAL_RAINBOW[k][2], PAL_RAINBOW[k][3], PAL_RAINBOW[k][4], &h, &s, &l);
      h = fmodf(h + av.hue / 360.0f + 1.0f, 1.0f);
      s = constrain(s * av.sat, 0.0f, 1.0f);
      hsl2rgb(h, s, l, &PC[k][0], &PC[k][1], &PC[k][2]);
    }
    else
    {
      PC[k][0] = PAL_RAINBOW[k][2];
      PC[k][1] = PAL_RAINBOW[k][3];
      PC[k][2] = PAL_RAINBOW[k][4];
    }
  }

  // points de la palette tournes autour de l'axe Y
  float rpx[PAL_N], rpy[PAL_N], rvis[PAL_N];
  for (unsigned k = 0; k < PAL_N; k++)
  {
    float nx = PAL_RAINBOW[k][0], ny = PAL_RAINBOW[k][1];
    float nzsq = 1 - nx * nx - ny * ny;
    float nz = nzsq > 0 ? sqrtf(nzsq) : 0;
    float nxr = nx * cosT + nz * sinT;
    float nzr = -nx * sinT + nz * cosT;
    rpx[k] = nxr;
    rpy[k] = ny;
    float vis = nzsq > 0 ? max(0.0f, nzr) : 0.5f;
    rvis[k] = sqrtf(vis);
  }

  uint16_t *dst = irFrames[fi];
  for (int y = 0; y < IR_SPR; y++)
    for (int x = 0; x < IR_SPR; x++)
    {
      float nx = (x - IR_SPR / 2.0f) / rTex, ny = (y - IR_SPR / 2.0f) / rTex;
      float pr = 0, pg = 0, pb = 0;
      if (nx * nx + ny * ny <= 1.0f)
      {
        float tw = 0;
        for (unsigned k = 0; k < PAL_N; k++)
        {
          float ddx = nx - rpx[k], ddy = ny - rpy[k];
          float d2 = ddx * ddx + ddy * ddy;
          if (d2 > IR_CUTOFF2)
            continue;
          float w = irExpLUT[(int)(d2 * d2toLut)] * rvis[k];
          tw += w;
          pr += PC[k][0] * w;
          pg += PC[k][1] * w;
          pb += PC[k][2] * w;
        }
        if (tw > 1e-6f)
        {
          pr /= tw;
          pg /= tw;
          pb /= tw;
          float lum = 0.299f * pr + 0.587f * pg + 0.114f * pb;
          pr = (lum + (pr - lum) * satBoost) * lumBoost;
          pg = (lum + (pg - lum) * satBoost) * lumBoost;
          pb = (lum + (pb - lum) * satBoost) * lumBoost;
        }
      }
      // position ecran du texel (fixe) -> vignette + highlights cuits
      float sx = -IR_SB + (x + 0.5f) * IR_DRAWN / IR_SPR;
      float sy = -IR_SB + (y + 0.5f) * IR_DRAWN / IR_SPR;
      float dxc = sx - CX, dyc = sy - CY;
      float rr = sqrtf(dxc * dxc + dyc * dyc) / RADIUS;
      // rimShade prononce : rgba(12,7,20) 0 -> 0.22@0.60 -> 0.70@0.88 -> 1.0@1.0
      float a;
      if (rr < 0.60f)
        a = rr / 0.60f * 0.22f;
      else if (rr < 0.88f)
        a = 0.22f + (rr - 0.60f) / 0.28f * 0.48f;
      else
        a = min(1.0f, 0.70f + (rr - 0.88f) / 0.12f * 0.30f);
      pr = pr * (1 - a) + 12 * a;
      pg = pg * (1 - a) + 7 * a;
      pb = pb * (1 - a) + 20 * a;
      // highlight principal haut-gauche (blanc dore, additif)
      float d1x = sx - (CX - RADIUS * 0.35f), d1y = sy - (CY - RADIUS * 0.45f);
      float u1 = sqrtf(d1x * d1x + d1y * d1y) / (RADIUS * 0.42f);
      if (u1 < 1)
      {
        float ha;
        if (u1 < 0.3f)
          ha = 0.55f - u1 / 0.3f * 0.25f;
        else if (u1 < 0.6f)
          ha = 0.30f - (u1 - 0.3f) / 0.3f * 0.20f;
        else
          ha = 0.10f * (1 - (u1 - 0.6f) / 0.4f);
        pr += 255 * ha;
        pg += 244 * ha;
        pb += 205 * ha;
      }
      // highlight secondaire haut-droite (rose tendre, additif)
      float d2x = sx - (CX + RADIUS * 0.55f), d2y = sy - (CY - RADIUS * 0.20f);
      float u2 = sqrtf(d2x * d2x + d2y * d2y) / (RADIUS * 0.25f);
      if (u2 < 1)
      {
        float ha = 0.30f * (1 - u2);
        pr += 255 * ha;
        pg += 220 * ha;
        pb += 230 * ha;
      }
      int R8 = (int)max(0.0f, min(255.0f, pr));
      int G8 = (int)max(0.0f, min(255.0f, pg));
      int B8 = (int)max(0.0f, min(255.0f, pb));
      dst[y * IR_SPR + x] = rgb565(R8, G8, B8);
    }
}

static void animIdleRainbow(float t)
{
  float lookX, lookY, openness;
  getIdle(t, &lookX, &lookY, &openness);

  // rencontre sociale : la sphere suit l'expression — regard gele en douceur
  // (equivalent des lookFreeze des triggers du visualiseur)
  lookX *= (1.0f - g_lookFreeze);
  lookY *= (1.0f - g_lookFreeze);

  // choix des 2 frames de rotation + blend (comme getSphereFramesBlended)
  float fidx = (lookX * IR_MAXROT + IR_MAXROT) / (2 * IR_MAXROT / (IR_FRAMES - 1));
  int lo = (int)fidx;
  if (lo < 0)
    lo = 0;
  if (lo > IR_FRAMES - 2)
    lo = IR_FRAMES - 2;
  // regeneration apres changement d'avatar/buddy : les frames AFFICHEES
  // (lo, lo+1) sont refaites immediatement — sinon on voyait l'ancienne
  // sphere un instant — puis une frame de fond par appel pour le reste
  if (irDirtyMask)
  {
    irDirtyMask &= (1u << IR_FRAMES) - 1;
    bool shown = false;
    if (irDirtyMask & (1u << lo))
    {
      irGenFrame(lo);
      irDirtyMask &= ~(1u << lo);
      shown = true;
    }
    if (irDirtyMask & (1u << (lo + 1)))
    {
      irGenFrame(lo + 1);
      irDirtyMask &= ~(1u << (lo + 1));
      shown = true;
    }
    if (!shown)
      for (int i = 0; i < IR_FRAMES; i++)
        if (irDirtyMask & (1u << i))
        {
          irGenFrame(i);
          irDirtyMask &= ~(1u << i);
          break;
        }
  }

  int alpha16 = (int)((fidx - lo) * 16);
  const uint16_t *texA = irFrames[lo], *texB = irFrames[lo + 1];
  const uint16_t *tex;
  if (alpha16 <= 0)
    tex = texA;
  else if (alpha16 >= 16)
    tex = texB;
  else
  {
    for (int i = 0; i < IR_SPR * IR_SPR; i++)
    {
      uint16_t a = texA[i], b = texB[i];
      uint16_t r = (((a >> 11) & 31) * (16 - alpha16) + ((b >> 11) & 31) * alpha16) >> 4;
      uint16_t g = (((a >> 5) & 63) * (16 - alpha16) + ((b >> 5) & 63) * alpha16) >> 4;
      uint16_t bl = ((a & 31) * (16 - alpha16) + (b & 31) * alpha16) >> 4;
      irScratch[i] = (r << 11) | (g << 5) | bl;
    }
    tex = irScratch;
  }

  // upscale bilineaire en demi-resolution (chaque echantillon remplit un bloc
  // 2x2) + grain discret : ~2x plus rapide, invisible sur ces degrades doux.
  // g_sphereYOff : rebond vertical de la sphere pendant une reaction sociale
  // (bandes decouvertes remises a noir).
  // g_sphereScale : battement de coeur du mode Love — tables d'echantillonnage
  // regenerees pour la frame (180 entrees, cout negligeable), pixels hors
  // texture -> noir (la sphere retrecit proprement sur fond noir)
  const float ss = g_sphereScale;
  const bool scaled = ss < 0.999f || ss > 1.001f;
  static uint16_t sIdx[W / 2];
  static uint8_t sFrac[W / 2];
  static uint8_t sOut[W / 2];
  if (scaled)
    for (int i = 0; i < W / 2; i++)
    {
      float p = 180.0f + (2 * i + 0.5f - 180.0f) / ss;
      float tf = (p + IR_SB) * IR_SPR / IR_DRAWN - 0.5f;
      if (tf < 0 || tf > IR_SPR - 2)
      {
        sOut[i] = 1;
        sIdx[i] = 0;
        sFrac[i] = 0;
      }
      else
      {
        sOut[i] = 0;
        sIdx[i] = (uint16_t)tf;
        sFrac[i] = (uint8_t)((tf - (int)tf) * 16);
      }
    }
  const uint16_t *TIdx = scaled ? sIdx : irTIdx;
  const uint8_t *TFrac = scaled ? sFrac : irTFrac;
  uint16_t *fb = canvas->getFramebuffer();
  const int yOff = g_sphereYOff;
  if (yOff > 0)
    memset(fb, 0, (size_t)yOff * W * sizeof(uint16_t));
  else if (yOff < 0)
    memset(&fb[(H + yOff) * W], 0, (size_t)(-yOff) * W * sizeof(uint16_t));
  for (int y2 = 0; y2 < H / 2; y2++)
  {
    int dy = y2 * 2 + yOff;
    uint16_t *d0 = (dy >= 0 && dy < H) ? &fb[dy * W] : nullptr;
    uint16_t *d1 = (dy + 1 >= 0 && dy + 1 < H) ? &fb[(dy + 1) * W] : nullptr;
    if (!d0 && !d1)
      continue;
    if (scaled && sOut[y2]) // ligne hors sphere retrecie -> noir
    {
      if (d0)
        memset(d0, 0, W * sizeof(uint16_t));
      if (d1)
        memset(d1, 0, W * sizeof(uint16_t));
      continue;
    }
    const uint16_t *rowA = &tex[TIdx[y2] * IR_SPR];
    const uint16_t *rowB = rowA + IR_SPR;
    int fy = TFrac[y2];
    for (int x2 = 0; x2 < W / 2; x2++)
    {
      if (scaled && sOut[x2]) // colonne hors sphere -> noir
      {
        int xx = x2 * 2;
        if (d0)
          d0[xx] = d0[xx + 1] = 0;
        if (d1)
          d1[xx] = d1[xx + 1] = 0;
        continue;
      }
      int tx = TIdx[x2], fx = TFrac[x2];
      uint16_t c00 = rowA[tx], c10 = rowA[tx + 1], c01 = rowB[tx], c11 = rowB[tx + 1];
      int w11 = fx * fy, w10 = fx * (16 - fy), w01 = (16 - fx) * fy, w00 = (16 - fx) * (16 - fy);
      int r = (((c00 >> 11) & 31) * w00 + ((c10 >> 11) & 31) * w10 + ((c01 >> 11) & 31) * w01 + ((c11 >> 11) & 31) * w11) >> 8;
      int g = (((c00 >> 5) & 63) * w00 + ((c10 >> 5) & 63) * w10 + ((c01 >> 5) & 63) * w01 + ((c11 >> 5) & 63) * w11) >> 8;
      int b = ((c00 & 31) * w00 + (c10 & 31) * w10 + (c01 & 31) * w01 + (c11 & 31) * w11) >> 8;
      // grain tres subtil : canal vert uniquement (pas le plus fin du RGB565)
      int gr = irNoise[(x2 * 7 + y2 * 131) & 255];
      g += gr;
      uint16_t c = (uint16_t)(((r < 0 ? 0 : (r > 31 ? 31 : r)) << 11) |
                              ((g < 0 ? 0 : (g > 63 ? 63 : g)) << 5) |
                              (b < 0 ? 0 : (b > 31 ? 31 : b)));
      int xx = x2 * 2;
      if (d0)
      {
        d0[xx] = c;
        d0[xx + 1] = c;
      }
      if (d1)
      {
        d1[xx] = c;
        d1[xx + 1] = c;
      }
    }
  }

  drawIdleFaceLook(CX, CY + yOff, RADIUS * ss, t, lookX, lookY, openness);
}

// ------------------------------------------------------------------- dvd

// Sphere-perso qui rebondit facon ecran de veille DVD : trainee, etincelles au
// bord, et changement de palette a chaque rebond (rainbow, sunset, acid,
// bubblegum, love — VORTEX_PALS du JS). Les 5 sprites sont pre-generes au boot
// par le meme blend gaussien que la sphere du snake.

// Palette "Love" — rose romantique custom (PAL_LOVE de screen-anims.js)
static const float PAL_LOVE[][5] = {
    {-0.65, -0.65, 255, 215, 230}, {-0.43, -0.65, 255, 220, 220}, {-0.22, -0.65, 255, 200, 210},
    {0.00, -0.65, 255, 180, 200}, {0.22, -0.65, 240, 160, 190}, {0.43, -0.65, 210, 130, 170},
    {-0.65, -0.43, 255, 230, 240}, {-0.43, -0.43, 255, 220, 235}, {-0.22, -0.43, 255, 190, 215},
    {0.00, -0.43, 250, 150, 200}, {0.22, -0.43, 235, 110, 170}, {0.43, -0.43, 200, 80, 140},
    {-0.65, -0.22, 255, 235, 245}, {-0.43, -0.22, 255, 225, 230}, {-0.22, -0.22, 255, 180, 200},
    {0.00, -0.22, 245, 130, 180}, {0.22, -0.22, 215, 90, 145}, {0.43, -0.22, 175, 60, 120},
    {-0.65, 0.00, 255, 195, 210}, {-0.43, 0.00, 250, 175, 200}, {-0.22, 0.00, 240, 140, 180},
    {0.00, 0.00, 220, 100, 160}, {0.22, 0.00, 175, 60, 120}, {0.43, 0.00, 130, 40, 90},
    {-0.65, 0.22, 230, 150, 175}, {-0.43, 0.22, 215, 130, 165}, {-0.22, 0.22, 195, 100, 155},
    {0.00, 0.22, 165, 60, 130}, {0.22, 0.22, 130, 35, 95}, {0.43, 0.22, 95, 25, 75},
    {-0.65, 0.43, 210, 130, 165}, {-0.43, 0.43, 185, 105, 155}, {-0.22, 0.43, 165, 80, 140},
    {0.00, 0.43, 135, 45, 110}, {0.22, 0.43, 105, 30, 90}, {0.43, 0.43, 90, 25, 85},
    {-0.65, 0.65, 175, 95, 145}, {-0.43, 0.65, 155, 75, 130}, {-0.22, 0.65, 130, 55, 115},
    {0.00, 0.65, 110, 40, 95}, {0.22, 0.65, 95, 30, 90}, {0.43, 0.65, 90, 25, 90},
    {0.850, 0.000, 150, 60, 110}, {0.736, 0.425, 130, 50, 110}, {0.425, 0.736, 120, 40, 105},
    {0.000, 0.850, 95, 35, 90}, {-0.425, 0.736, 130, 50, 100}, {-0.736, 0.425, 220, 130, 170},
    {-0.850, 0.000, 250, 180, 200}, {-0.736, -0.425, 255, 215, 220}, {-0.425, -0.736, 255, 220, 215},
    {0.000, -0.850, 245, 175, 195}, {0.425, -0.736, 215, 100, 160}, {0.736, -0.425, 165, 65, 120},
    {-0.797, 0.460, 245, 175, 195}, {-0.920, 0.000, 255, 200, 215},
    {-0.797, -0.460, 255, 230, 230}, {0.000, -0.920, 250, 195, 210}};

#define DVD_NPAL 5
static uint16_t *dvdSprites[DVD_NPAL]; // [0] = pointe sur ballSprite (rainbow)

// rgb <-> hsl pour les variantes decalees en teinte (port de shiftHue du JS)
static void rgb2hsl(float r, float g, float b, float *h, float *s, float *l)
{
  r /= 255; g /= 255; b /= 255;
  float mx = max(r, max(g, b)), mn = min(r, min(g, b));
  *l = (mx + mn) / 2;
  if (mx == mn) { *h = *s = 0; return; }
  float d = mx - mn;
  *s = *l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
  if (mx == r) *h = (g - b) / d + (g < b ? 6 : 0);
  else if (mx == g) *h = (b - r) / d + 2;
  else *h = (r - g) / d + 4;
  *h /= 6;
}

static float hue2rgb1(float p, float q, float t)
{
  if (t < 0) t += 1;
  if (t > 1) t -= 1;
  if (t < 1.0f / 6) return p + (q - p) * 6 * t;
  if (t < 0.5f) return q;
  if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
  return p;
}

static void hsl2rgb(float h, float s, float l, float *r, float *g, float *b)
{
  if (s == 0) { *r = *g = *b = l * 255; return; }
  float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
  float p = 2 * l - q;
  *r = hue2rgb1(p, q, h + 1.0f / 3) * 255;
  *g = hue2rgb1(p, q, h) * 255;
  *b = hue2rgb1(p, q, h - 1.0f / 3) * 255;
}

// Genere un sprite de sphere depuis une table de points {px,py,r,g,b},
// avec option de decalage de teinte (comme shiftHue du JS). Meme rendu que
// initBallSprite : blend gaussien + vibrance + grain, via la LUT d'expf.
static uint16_t *dvdGenSprite(const float (*pts)[5], int npts, float hueDeg, float satMul)
{
  uint16_t *dst = (uint16_t *)malloc(SPR * SPR * sizeof(uint16_t));
  const float satBoost = 1.75f, lumBoost = 1.12f, grainAmp = 40.0f;
  const float r = SPR / 2.0f - 1;
  const float d2toLut = 128.0f / IR_CUTOFF2;
  // palette transformee + visibilite
  float P[64][5], vis[64];
  for (int k = 0; k < npts; k++)
  {
    P[k][0] = pts[k][0];
    P[k][1] = pts[k][1];
    if (hueDeg != 0 || satMul != 1.0f)
    {
      float h, s, l;
      rgb2hsl(pts[k][2], pts[k][3], pts[k][4], &h, &s, &l);
      h = fmodf(h + hueDeg / 360.0f + 1.0f, 1.0f);
      s = constrain(s * satMul, 0.0f, 1.0f);
      hsl2rgb(h, s, l, &P[k][2], &P[k][3], &P[k][4]);
    }
    else
    {
      P[k][2] = pts[k][2];
      P[k][3] = pts[k][3];
      P[k][4] = pts[k][4];
    }
    float nzsq = 1 - P[k][0] * P[k][0] - P[k][1] * P[k][1];
    vis[k] = sqrtf(nzsq > 0 ? sqrtf(nzsq) : 0.5f);
  }
  for (int y = 0; y < SPR; y++)
    for (int x = 0; x < SPR; x++)
    {
      float nx = (x - SPR / 2.0f) / r, ny = (y - SPR / 2.0f) / r;
      if (nx * nx + ny * ny > 1.0f)
      {
        dst[y * SPR + x] = 0;
        continue;
      }
      float tw = 0, pr = 0, pg = 0, pb = 0;
      for (int k = 0; k < npts; k++)
      {
        float ddx = nx - P[k][0], ddy = ny - P[k][1];
        float d2 = ddx * ddx + ddy * ddy;
        if (d2 > IR_CUTOFF2)
          continue;
        float w = irExpLUT[(int)(d2 * d2toLut)] * vis[k];
        tw += w;
        pr += P[k][2] * w;
        pg += P[k][3] * w;
        pb += P[k][4] * w;
      }
      if (tw > 1e-6f)
      {
        pr /= tw;
        pg /= tw;
        pb /= tw;
        float lum = 0.299f * pr + 0.587f * pg + 0.114f * pb;
        pr = (lum + (pr - lum) * satBoost) * lumBoost;
        pg = (lum + (pg - lum) * satBoost) * lumBoost;
        pb = (lum + (pb - lum) * satBoost) * lumBoost;
      }
      float grain = frand(-0.5f, 0.5f) * grainAmp;
      int R8 = (int)max(0.0f, min(255.0f, pr + grain));
      int G8 = (int)max(0.0f, min(255.0f, pg + grain));
      int B8 = (int)max(0.0f, min(255.0f, pb + grain));
      dst[y * SPR + x] = rgb565(R8, G8, B8);
    }
  return dst;
}

static void dvdInitSprites()
{
  dvdSprites[0] = ballSprite; // rainbow : reutilise le sprite du snake
  dvdSprites[1] = dvdGenSprite(PAL_RAINBOW, PAL_N, -30, 1.0f);   // sunset
  dvdSprites[2] = dvdGenSprite(PAL_RAINBOW, PAL_N, 90, 1.1f);    // acid
  dvdSprites[3] = dvdGenSprite(PAL_RAINBOW, PAL_N, 40, 1.05f);   // bubblegum
  dvdSprites[4] = dvdGenSprite(PAL_LOVE, sizeof(PAL_LOVE) / sizeof(PAL_LOVE[0]), 0, 1.0f); // love
}

// Blit du sprite avec attenuation de luminosite (trainee) — masque disque
static void dvdBlit(const uint16_t *spr, int cx, int cy, float rf, uint8_t bright)
{
  int r = (int)rf;
  if (r < 2 || !spr)
    return;
  uint16_t *fb = canvas->getFramebuffer();
  const int half = SPR / 2 - 1;
  for (int dy = -r; dy <= r; dy++)
  {
    int yy = cy + dy;
    if (yy < 0 || yy >= H)
      continue;
    int span = (int)sqrtf((float)(r * r - dy * dy));
    const uint16_t *srow = &spr[(dy * half / r + SPR / 2) * SPR];
    uint16_t *drow = &fb[yy * W];
    int x0 = max(-span, -cx), x1 = min(span, W - 1 - cx);
    for (int dx = x0; dx <= x1; dx++)
    {
      uint16_t c = srow[dx * half / r + SPR / 2];
      if (bright < 255)
      {
        uint16_t cr = (((c >> 11) & 31) * bright) >> 8;
        uint16_t cg = (((c >> 5) & 63) * bright) >> 8;
        uint16_t cb = ((c & 31) * bright) >> 8;
        c = (cr << 11) | (cg << 5) | cb;
      }
      drow[cx + dx] = c;
    }
  }
}

static bool dvdInit = false;
static float dvdX, dvdY, dvdVx, dvdVy;
static int dvdPal = 0;
static struct { float x, y, life; } dvdSparks[6];

static void animDvd(float t, float dt)
{
  const float ballR = RADIUS * 0.2f;
  const float maxR = RADIUS - ballR - 2;
  if (dt > 0.09f)
    dt = 0.09f;

  if (!dvdInit)
  {
    dvdInit = true;
    dvdX = CX;
    dvdY = CY;
    float a = frand(0, 2 * PI), sp = RADIUS * 0.55f;
    dvdVx = cosf(a) * sp;
    dvdVy = sinf(a) * sp;
    for (int i = 0; i < 6; i++)
      dvdSparks[i].life = 0;
  }

  // deplacement + rebond billard sur le bord du disque
  dvdX += dvdVx * dt;
  dvdY += dvdVy * dt;
  float dx = dvdX - CX, dy = dvdY - CY;
  float d = sqrtf(dx * dx + dy * dy);
  if (d > maxR)
  {
    float nx = dx / d, ny = dy / d;
    float dot = dvdVx * nx + dvdVy * ny;
    dvdVx -= 2 * dot * nx;
    dvdVy -= 2 * dot * ny;
    dvdX = CX + nx * maxR;
    dvdY = CY + ny * maxR;
    dvdPal = (dvdPal + 1) % DVD_NPAL; // change de palette a chaque rebond
    for (int i = 0; i < 6; i++)
      if (dvdSparks[i].life <= 0)
      {
        dvdSparks[i] = {CX + nx * RADIUS, CY + ny * RADIUS, 1.0f};
        break;
      }
  }

  canvas->fillScreen(rgb565(10, 8, 20)); // #0a0814

  // trainee : fantomes attenues derriere la sphere, le long de la vitesse
  float vn = sqrtf(dvdVx * dvdVx + dvdVy * dvdVy);
  float ux = dvdVx / vn, uy = dvdVy / vn;
  for (int i = 5; i >= 1; i--)
  {
    float gx = dvdX - ux * i * ballR * 0.14f;
    float gy = dvdY - uy * i * ballR * 0.14f;
    dvdBlit(dvdSprites[dvdPal], (int)gx, (int)gy, ballR * (1 - 0.04f * i),
            (uint8_t)(56 - i * 8));
  }
  // sphere + visage anime
  dvdBlit(dvdSprites[dvdPal], (int)dvdX, (int)dvdY, ballR, 255);
  drawIdleFace(dvdX, dvdY, ballR, t);

  // etincelles : anneaux blancs qui s'etendent et s'eteignent
  for (int i = 0; i < 6; i++)
  {
    if (dvdSparks[i].life <= 0)
      continue;
    dvdSparks[i].life -= dt * 2.2f;
    if (dvdSparks[i].life <= 0)
      continue;
    uint8_t g = (uint8_t)(255 * dvdSparks[i].life);
    int rr = (int)((1 - dvdSparks[i].life) * 34 + 4);
    canvas->drawCircle((int)dvdSparks[i].x, (int)dvdSparks[i].y, rr, rgb565(g, g, g));
  }
}

// ---------------------------------------------------------------- points

// Nuage de 300 points 3D qui tourne et morphe sphere -> cube -> tore,
// profondeur = taille + eclat ("GPGPU particles").
#define PTS_N 300
static float ptsShapes[3][PTS_N][3];

static void initPoints()
{
  for (int i = 0; i < PTS_N; i++)
  {
    float yy = 1 - (float)i / (PTS_N - 1) * 2;
    float rr = sqrtf(max(0.0f, 1 - yy * yy));
    float th = i * 2.39996f;
    ptsShapes[0][i][0] = cosf(th) * rr;
    ptsShapes[0][i][1] = yy;
    ptsShapes[0][i][2] = sinf(th) * rr;
    int f = i % 6;
    float a = frand(-1, 1), b = frand(-1, 1);
    float cp[3];
    if (f == 0) { cp[0] = 1; cp[1] = a; cp[2] = b; }
    else if (f == 1) { cp[0] = -1; cp[1] = a; cp[2] = b; }
    else if (f == 2) { cp[0] = a; cp[1] = 1; cp[2] = b; }
    else if (f == 3) { cp[0] = a; cp[1] = -1; cp[2] = b; }
    else if (f == 4) { cp[0] = a; cp[1] = b; cp[2] = 1; }
    else { cp[0] = a; cp[1] = b; cp[2] = -1; }
    for (int k = 0; k < 3; k++)
      ptsShapes[1][i][k] = cp[k] * 0.82f;
    float u = (float)i / PTS_N * 2 * PI * 7, v = i * 2.39996f, cv = cosf(v);
    ptsShapes[2][i][0] = (0.7f + 0.3f * cv) * cosf(u);
    ptsShapes[2][i][1] = (0.7f + 0.3f * cv) * sinf(u);
    ptsShapes[2][i][2] = 0.3f * sinf(v);
  }
}

static int ptsOrder[PTS_N];
static float ptsZc[PTS_N];
static int ptsCompare(const void *a, const void *b)
{
  float za = ptsZc[*(const int *)a], zb = ptsZc[*(const int *)b];
  return za < zb ? 1 : (za > zb ? -1 : 0); // loin -> proche
}

static void animPoints(float t)
{
  canvas->fillScreen(rgb565(7, 6, 17)); // #070611
  const float period = 3.4f;
  float ph = t / period;
  int idx = (int)ph % 3, nxt = (idx + 1) % 3;
  float f = ph - (int)ph;
  f = f < 0.65f ? 0 : (f - 0.65f) / 0.35f;
  f = f * f * (3 - 2 * f); // smoothstep
  float ax = t * 0.4f + 0.3f, ay = t * 0.55f;
  float cax = cosf(ax), sax = sinf(ax), cay = cosf(ay), say = sinf(ay);

  static float sx[PTS_N], sy[PTS_N], sc[PTS_N];
  for (int i = 0; i < PTS_N; i++)
  {
    float x = ptsShapes[idx][i][0] + (ptsShapes[nxt][i][0] - ptsShapes[idx][i][0]) * f;
    float y = ptsShapes[idx][i][1] + (ptsShapes[nxt][i][1] - ptsShapes[idx][i][1]) * f;
    float z = ptsShapes[idx][i][2] + (ptsShapes[nxt][i][2] - ptsShapes[idx][i][2]) * f;
    float y1 = y * cax - z * sax, z1 = y * sax + z * cax;
    float x2 = x * cay + z1 * say, z2 = -x * say + z1 * cay;
    float zc = z2 + 3.2f;
    float s = RADIUS * 2.6f / zc;
    sx[i] = CX + x2 * s;
    sy[i] = CY - y1 * s;
    sc[i] = s;
    ptsZc[i] = zc;
    ptsOrder[i] = i;
  }
  qsort(ptsOrder, PTS_N, sizeof(int), ptsCompare);
  for (int k = 0; k < PTS_N; k++)
  {
    int i = ptsOrder[k];
    int sz = (int)constrain(sc[i] * 0.02f, 1.0f, 11.0f);
    float al = constrain((4.7f - ptsZc[i]) / 2.7f, 0.18f, 1.0f);
    uint8_t hue = (uint8_t)(fmodf(i * 3.7f, 360.0f) * 255.0f / 360.0f);
    canvas->fillCircle((int)sx[i], (int)sy[i], sz, hsv2rgb565(hue, 217, (uint8_t)(al * 255)));
  }
}

// ------------------------------------------------------- photo du speaker

// Avatar plein ecran : copie directe du tableau flash vers le framebuffer.
static void animPhoto(float)
{
  memcpy(canvas->getFramebuffer(), SPEAKER_PHOTO, (size_t)W * H * 2);
}

// Deuxieme avatar (test photo couleur), statique comme animPhoto.
static void animPhoto3(float)
{
  memcpy(canvas->getFramebuffer(), SPEAKER_PHOTO3, (size_t)W * H * 2);
}

// Variante CRT : zoom respirant, barre de balayage lumineuse, scanlines.
static void animPhoto2(float t)
{
  uint16_t *fb = canvas->getFramebuffer();

  // zoom 1.00..1.05 (vers l'interieur uniquement : jamais d'echantillon hors
  // image), reechantillonnage nearest en virgule fixe 16.16
  float s = 1.025f + 0.025f * sinf(t * 0.9f);
  uint32_t inv = (uint32_t)(65536.0f / s);
  for (int y = 0; y < H; y++)
  {
    uint32_t v = (uint32_t)((CY << 16) + (int32_t)((int64_t)(y - CY) * (int32_t)inv));
    const uint16_t *src = &SPEAKER_PHOTO[(v >> 16) * W];
    uint16_t *row = &fb[y * W];
    uint32_t u = (uint32_t)((CX << 16) - (int32_t)((int64_t)CX * (int32_t)inv));
    for (int x = 0; x < W; x++, u += inv)
      row[x] = src[u >> 16];
  }

  // barre de balayage CRT : bande claire qui descend (periode ~8.7 s)
  float yb = fmodf(t * 55.0f, (float)(H + 120)) - 60.0f;
  for (int dyy = -18; dyy <= 18; dyy++)
  {
    int y = (int)yb + dyy;
    if (y < 0 || y >= H)
      continue;
    int g16 = 16 + (int)(6.0f * (1.0f - (float)abs(dyy) / 18.0f)); // gain x1..x1.37
    uint16_t *row = &fb[y * W];
    for (int x = 0; x < W; x++)
    {
      uint16_t c = row[x];
      int r = (((c >> 11) & 31) * g16) >> 4;
      int g = (((c >> 5) & 63) * g16) >> 4;
      int b = ((c & 31) * g16) >> 4;
      row[x] = ((r > 31 ? 31 : r) << 11) | ((g > 63 ? 63 : g) << 5) | (b > 31 ? 31 : b);
    }
  }

  // scanlines (1 ligne sur 3)
  for (int y = 0; y < H; y += 3)
    dimRow(y, 0, W);
}
