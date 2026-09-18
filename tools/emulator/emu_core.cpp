// Desktop test harness for the badge animations (snake + disco).
// The "PORT" block is written against the Arduino_GFX API (canvas->...)
// so it can later be copied as-is into main.cpp.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <algorithm>

#define W 360
#define H 360
#define CX 180
#define CY 180
#define RADIUS 180
#ifndef PI
#define PI 3.14159265358979f
#endif
using std::min;
using std::max;
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

// ---------------------------------------------------------------- mini canvas
#ifndef PROGMEM
#define PROGMEM
#endif
#include "glcdfont.h"

struct Canvas
{
  uint16_t fb[W * H];
  uint16_t *getFramebuffer() { return fb; }
  // --- text engine: same 5x7 font as the firmware (Adafruit glcdfont) ---
  int tcx = 0, tcy = 0, tsz = 1;
  uint16_t tcol = 0xFFFF;
  void setCursor(int x, int y) { tcx = x; tcy = y; }
  void setTextSize(int s) { tsz = s < 1 ? 1 : s; }
  void setTextColor(uint16_t c) { tcol = c; }
  void drawGlyph(int x, int y, unsigned char ch)
  {
    for (int col = 0; col < 5; col++)
    {
      unsigned char bits = font[(int)ch * 5 + col];
      for (int row = 0; row < 8; row++)
        if (bits & (1 << row))
          fillRect(x + col * tsz, y + row * tsz, tsz, tsz, tcol);
    }
  }
  void print(const char *s)
  {
    for (; *s; s++)
    {
      if (*s == '\n') { tcx = 0; tcy += 8 * tsz; continue; }
      drawGlyph(tcx, tcy, (unsigned char)*s);
      tcx += 6 * tsz;
    }
  }
  void printf(const char *fmt, ...)
  {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    print(buf);
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c)
  {
    fillRect(x + r, y, w - 2 * r, h, c);
    fillRect(x, y + r, w, h - 2 * r, c);
    fillCircle(x + r, y + r, r, c);
    fillCircle(x + w - 1 - r, y + r, r, c);
    fillCircle(x + r, y + h - 1 - r, r, c);
    fillCircle(x + w - 1 - r, y + h - 1 - r, r, c);
  }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c)
  {
    for (int i = x + r; i < x + w - r; i++) { drawPixel(i, y, c); drawPixel(i, y + h - 1, c); }
    for (int j = y + r; j < y + h - r; j++) { drawPixel(x, j, c); drawPixel(x + w - 1, j, c); }
    int cx0 = x + r, cy0 = y + r, cx1 = x + w - 1 - r, cy1 = y + h - 1 - r;
    for (float a = 0; a <= 1.5708f; a += 0.04f)
    {
      int dx = (int)(cosf(a) * r + 0.5f), dy = (int)(sinf(a) * r + 0.5f);
      drawPixel(cx1 + dx, cy0 - dy, c); drawPixel(cx0 - dx, cy0 - dy, c);
      drawPixel(cx1 + dx, cy1 + dy, c); drawPixel(cx0 - dx, cy1 + dy, c);
    }
  }
  void drawPixel(int x, int y, uint16_t c)
  {
    if (x >= 0 && x < W && y >= 0 && y < H)
      fb[y * W + x] = c;
  }
  void fillScreen(uint16_t c)
  {
    for (int i = 0; i < W * H; i++)
      fb[i] = c;
  }
  void fillRect(int x, int y, int w, int h, uint16_t c)
  {
    for (int j = y; j < y + h; j++)
      for (int i = x; i < x + w; i++)
        drawPixel(i, j, c);
  }
  void fillCircle(int cx, int cy, int r, uint16_t c)
  {
    for (int dy = -r; dy <= r; dy++)
      for (int dx = -r; dx <= r; dx++)
        if (dx * dx + dy * dy <= r * r)
          drawPixel(cx + dx, cy + dy, c);
  }
  void fillEllipse(int cx, int cy, int rx, int ry, uint16_t c)
  {
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    for (int dy = -ry; dy <= ry; dy++)
      for (int dx = -rx; dx <= rx; dx++)
      {
        float fx = (float)dx / rx, fy = (float)dy / ry;
        if (fx * fx + fy * fy <= 1.0f)
          drawPixel(cx + dx, cy + dy, c);
      }
  }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c)
  {
    int minx = min(x0, min(x1, x2)), maxx = max(x0, max(x1, x2));
    int miny = min(y0, min(y1, y2)), maxy = max(y0, max(y1, y2));
    long d = (long)(x1 - x0) * (y2 - y0) - (long)(x2 - x0) * (y1 - y0);
    if (d == 0)
      return;
    for (int y = miny; y <= maxy; y++)
      for (int x = minx; x <= maxx; x++)
      {
        long w0 = (long)(x1 - x0) * (y - y0) - (long)(y1 - y0) * (x - x0);
        long w1 = (long)(x2 - x1) * (y - y1) - (long)(y2 - y1) * (x - x1);
        long w2 = (long)(x0 - x2) * (y - y2) - (long)(y0 - y2) * (x - x2);
        bool neg = w0 <= 0 && w1 <= 0 && w2 <= 0, pos = w0 >= 0 && w1 >= 0 && w2 >= 0;
        if (neg || pos)
          drawPixel(x, y, c);
      }
  }
  void drawRect(int x, int y, int w, int h, uint16_t c)
  {
    for (int i = x; i < x + w; i++) { drawPixel(i, y, c); drawPixel(i, y + h - 1, c); }
    for (int j = y; j < y + h; j++) { drawPixel(x, j, c); drawPixel(x + w - 1, j, c); }
  }
  void drawCircle(int cx, int cy, int r, uint16_t c)
  {
    int steps = max(24, r * 6);
    for (int i = 0; i < steps; i++)
    {
      float a = i * 2 * 3.14159265f / steps;
      drawPixel(cx + (int)(cosf(a) * r), cy + (int)(sinf(a) * r), c);
    }
  }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c)
  {
    int steps = max(abs(x1 - x0), abs(y1 - y0));
    if (steps == 0)
    {
      drawPixel(x0, y0, c);
      return;
    }
    for (int i = 0; i <= steps; i++)
    {
      drawPixel(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, c);
    }
  }
};
static Canvas theCanvas;
static Canvas *canvas = &theCanvas;

static float frand(float lo, float hi)
{
  return lo + (hi - lo) * (float)(rand() % 10000) / 10000.0f;
}

#define RGB565_BLACK 0

// =====================================================================
// ==== PORT BEGIN (code meant to be copied into main.cpp) =============
// =====================================================================

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static uint16_t hsv2rgb565(uint8_t h, uint8_t s, uint8_t v)
{
  uint8_t region = h / 43;
  uint8_t rem = (h - region * 43) * 6;
  uint8_t p = (uint16_t)(v * (255 - s)) >> 8;
  uint8_t q = (uint16_t)(v * (255 - ((s * rem) >> 8))) >> 8;
  uint8_t t = (uint16_t)(v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
  uint8_t r, g, b;
  switch (region)
  {
  case 0: r = v; g = t; b = p; break;
  case 1: r = q; g = v; b = p; break;
  case 2: r = p; g = v; b = t; break;
  case 3: r = p; g = q; b = v; break;
  case 4: r = t; g = p; b = v; break;
  default: r = v; g = p; b = q; break;
  }
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

#include "../../src/avatars.h" // 40 avatars (table + face) -- before anims_extra.h

// ---- "Rainbow" sphere texture: Gaussian blend of the colored points of
// PAL_RAINBOW (screen-anims.js), vibrance + grain, precomputed at boot. ----

#define SPR 112 // sprite size (largest use: snake head, 108 px)
static uint16_t *ballSprite = nullptr;

// {px, py, r, g, b} -- color points on the unit disk
static const float PAL_RAINBOW[][5] = {
    {-0.65, -0.65, 250, 209, 228}, {-0.43, -0.65, 248, 229, 157}, {-0.22, -0.65, 244, 213, 121},
    {0.00, -0.65, 228, 170, 144}, {0.22, -0.65, 214, 135, 150}, {0.43, -0.65, 185, 100, 129},
    {-0.65, -0.43, 251, 225, 208}, {-0.43, -0.43, 250, 229, 177}, {-0.22, -0.43, 236, 197, 99},
    {0.00, -0.43, 217, 160, 69}, {0.22, -0.43, 202, 140, 54}, {0.43, -0.43, 166, 99, 48},
    {-0.65, -0.22, 249, 233, 179}, {-0.43, -0.22, 245, 219, 152}, {-0.22, -0.22, 226, 166, 96},
    {0.00, -0.22, 207, 132, 71}, {0.22, -0.22, 177, 113, 29}, {0.43, -0.22, 140, 75, 25},
    {-0.65, 0.00, 229, 212, 104}, {-0.43, 0.00, 221, 195, 96}, {-0.22, 0.00, 211, 125, 138},
    {0.00, 0.00, 187, 86, 127}, {0.22, 0.00, 142, 60, 43}, {0.43, 0.00, 109, 41, 46},
    {-0.65, 0.22, 206, 194, 82}, {-0.43, 0.22, 190, 178, 80}, {-0.22, 0.22, 186, 110, 159},
    {0.00, 0.22, 163, 63, 140}, {0.22, 0.22, 120, 39, 85}, {0.43, 0.22, 92, 27, 71},
    {-0.65, 0.43, 196, 171, 83}, {-0.43, 0.43, 161, 164, 76}, {-0.22, 0.43, 141, 128, 133},
    {0.00, 0.43, 135, 53, 139}, {0.22, 0.43, 102, 31, 101}, {0.43, 0.43, 97, 30, 107},
    {-0.65, 0.65, 178, 99, 142}, {-0.43, 0.65, 133, 136, 66}, {-0.22, 0.65, 100, 113, 121},
    {0.00, 0.65, 98, 55, 125}, {0.22, 0.65, 103, 35, 117}, {0.43, 0.65, 109, 36, 117},
    {0.850, 0.000, 93, 95, 61}, {0.736, 0.425, 77, 68, 119}, {0.425, 0.736, 117, 40, 132},
    {0.000, 0.850, 80, 76, 143}, {-0.425, 0.736, 123, 111, 58}, {-0.736, 0.425, 208, 145, 145},
    {-0.850, 0.000, 241, 181, 177}, {-0.736, -0.425, 251, 214, 202}, {-0.425, -0.736, 252, 234, 169},
    {0.000, -0.850, 232, 188, 213}, {0.425, -0.736, 192, 104, 162}, {0.736, -0.425, 103, 58, 52},
    {-0.797, -0.460, 250, 204, 211}, {-0.920, 0.000, 240, 166, 218},
    {0.000, -0.920, 233, 188, 223}, {0.460, -0.797, 184, 97, 168}};
#define PAL_N (sizeof(PAL_RAINBOW) / sizeof(PAL_RAINBOW[0]))

static void initBallSprite()
{
  if (!ballSprite) // regenerable on avatar change (g_ballDirty)
    ballSprite = (uint16_t *)malloc(SPR * SPR * sizeof(uint16_t));
  // colors transformed by the active avatar / custom buddy (same
  // transformation as the idle sphere): the snake, the DVD rainbow
  // palette and Sphere Run follow the badge's configured color
  const AvatarDef &avB = g_buddyCustom ? g_buddyCustomDef : AVATARS[g_avatarIdx];
  float PC[PAL_N][3];
  for (unsigned k = 0; k < PAL_N; k++)
  {
    if (avB.hue != 0 || avB.sat != 1.0f)
    {
      float h, s, l;
      rgb2hsl(PAL_RAINBOW[k][2], PAL_RAINBOW[k][3], PAL_RAINBOW[k][4], &h, &s, &l);
      h = fmodf(h + avB.hue / 360.0f + 1.0f, 1.0f);
      s = constrain(s * avB.sat, 0.0f, 1.0f);
      hsl2rgb(h, s, l, &PC[k][0], &PC[k][1], &PC[k][2]);
    }
    else
    {
      PC[k][0] = PAL_RAINBOW[k][2];
      PC[k][1] = PAL_RAINBOW[k][3];
      PC[k][2] = PAL_RAINBOW[k][4];
    }
  }

  const float sigma = 0.16f, sigma2 = 2 * sigma * sigma;
  const float satBoost = 1.75f, lumBoost = 1.12f, grainAmp = 40.0f;
  const float r = SPR / 2.0f - 1;
  // point visibility (rotation 0): sqrt(max(0, nz))
  float vis[PAL_N];
  for (unsigned k = 0; k < PAL_N; k++)
  {
    float nzsq = 1 - PAL_RAINBOW[k][0] * PAL_RAINBOW[k][0] - PAL_RAINBOW[k][1] * PAL_RAINBOW[k][1];
    vis[k] = sqrtf(nzsq > 0 ? sqrtf(nzsq) : 0.5f);
  }
  for (int y = 0; y < SPR; y++)
    for (int x = 0; x < SPR; x++)
    {
      float nx = (x - SPR / 2.0f) / r, ny = (y - SPR / 2.0f) / r;
      if (nx * nx + ny * ny > 1.0f)
      {
        ballSprite[y * SPR + x] = 0; // outside the disk (never read at blit)
        continue;
      }
      float tw = 0, pr = 0, pg = 0, pb = 0;
      for (unsigned k = 0; k < PAL_N; k++)
      {
        float ddx = nx - PAL_RAINBOW[k][0], ddy = ny - PAL_RAINBOW[k][1];
        float w = expf(-(ddx * ddx + ddy * ddy) / sigma2) * vis[k];
        tw += w;
        pr += PC[k][0] * w;
        pg += PC[k][1] * w;
        pb += PC[k][2] * w;
      }
      if (tw < 1e-6f)
      {
        ballSprite[y * SPR + x] = 0;
        continue;
      }
      pr /= tw;
      pg /= tw;
      pb /= tw;
      float lum = 0.299f * pr + 0.587f * pg + 0.114f * pb;
      pr = (lum + (pr - lum) * satBoost) * lumBoost;
      pg = (lum + (pg - lum) * satBoost) * lumBoost;
      pb = (lum + (pb - lum) * satBoost) * lumBoost;
      float grain = frand(-0.5f, 0.5f) * grainAmp;
      int R8 = (int)max(0.0f, min(255.0f, pr + grain));
      int G8 = (int)max(0.0f, min(255.0f, pg + grain));
      int B8 = (int)max(0.0f, min(255.0f, pb + grain));
      ballSprite[y * SPR + x] = rgb565(R8, G8, B8);
    }
}

// Blit of the sphere sprite, resized to radius r (nearest, disk mask)
static void drawBallSprite(int16_t cx, int16_t cy, float rf)
{
  int r = (int)rf;
  if (r < 2)
    return;
  uint16_t *fb = canvas->getFramebuffer();
  const int half = SPR / 2 - 1;
  for (int dy = -r; dy <= r; dy++)
  {
    int yy = cy + dy;
    if (yy < 0 || yy >= H)
      continue;
    int span = (int)sqrtf((float)(r * r - dy * dy));
    int sy = dy * half / r + SPR / 2;
    uint16_t *srow = &ballSprite[sy * SPR];
    uint16_t *drow = &fb[yy * W];
    int x0 = max(-span, -cx), x1 = min(span, W - 1 - cx);
    for (int dx = x0; dx <= x1; dx++)
      drow[cx + dx] = srow[dx * half / r + SPR / 2];
  }
}

// ---- The mascot's SVG mouth (MOUTH_SVG from screen-anims.js),
// rasterized at boot into a mask, then blitted to scale. ----

#define MOUTH_MW 142 // 71 x 2
#define MOUTH_MH 146 // 73 x 2
static uint8_t *mouthMask = nullptr;

static void bez3(float p0x, float p0y, float c1x, float c1y, float c2x, float c2y,
                 float p1x, float p1y, float s, float *ox, float *oy)
{
  float u = 1 - s;
  *ox = u * u * u * p0x + 3 * u * u * s * c1x + 3 * u * s * s * c2x + s * s * s * p1x;
  *oy = u * u * u * p0y + 3 * u * u * s * c1y + 3 * u * s * s * c2y + s * s * s * p1y;
}

static void mouthStampCircle(float cx, float cy, float r)
{
  for (int y = (int)(cy - r); y <= (int)(cy + r + 1); y++)
    for (int x = (int)(cx - r); x <= (int)(cx + r + 1); x++)
      if (x >= 0 && x < MOUTH_MW && y >= 0 && y < MOUTH_MH &&
          (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r)
        mouthMask[y * MOUTH_MW + x] = 1;
}

static void initMouthMask()
{
  mouthMask = (uint8_t *)calloc(MOUTH_MW * MOUTH_MH, 1);
  const float SC = 2.0f; // viewBox 71x73 -> 142x146
  // Outline of the filled path, tessellating the 4 cubics + segments
  float poly[200][2];
  int np = 0;
  const int SEG = 24;
  float x, y;
  // M 12.8717 29.0658
  // C 0.183 19.66, 6.834 -0.482, 22.627 -0.482
  for (int i = 0; i <= SEG; i++)
  {
    bez3(12.8717f, 29.0658f, 0.183f, 19.6626f, 6.83373f, -0.481934f, 22.6268f, -0.481934f, (float)i / SEG, &x, &y);
    poly[np][0] = x * SC;
    poly[np++][1] = y * SC;
  }
  // H 51.1003
  poly[np][0] = 51.1003f * SC;
  poly[np++][1] = -0.481934f * SC;
  // C 66.711 -0.482, 73.477 19.28, 61.142 28.848
  for (int i = 1; i <= SEG; i++)
  {
    bez3(51.1003f, -0.481934f, 66.7113f, -0.481934f, 73.4774f, 19.2799f, 61.1424f, 28.8482f, (float)i / SEG, &x, &y);
    poly[np][0] = x * SC;
    poly[np++][1] = y * SC;
  }
  // L 48.6013 38.5763 ; C 45.728 40.805, 42.195 42.015, 38.559 42.015
  poly[np][0] = 48.6013f * SC;
  poly[np++][1] = 38.5763f * SC;
  for (int i = 1; i <= SEG; i++)
  {
    bez3(48.6013f, 38.5763f, 45.7282f, 40.805f, 42.1954f, 42.0146f, 38.5592f, 42.0146f, (float)i / SEG, &x, &y);
    poly[np][0] = x * SC;
    poly[np++][1] = y * SC;
  }
  // H 35.7539 ; C 32.241 42.015, 28.821 40.886, 25.999 38.794 ; Z
  poly[np][0] = 35.7539f * SC;
  poly[np++][1] = 42.0146f * SC;
  for (int i = 1; i <= SEG; i++)
  {
    bez3(35.7539f, 42.0146f, 32.241f, 42.0146f, 28.8211f, 40.8855f, 25.9988f, 38.7939f, (float)i / SEG, &x, &y);
    poly[np][0] = x * SC;
    poly[np++][1] = y * SC;
  }
  // Even-odd scanline fill
  for (int yy = 0; yy < MOUTH_MH; yy++)
  {
    float fy = yy + 0.5f;
    float xs[32];
    int nxs = 0;
    for (int i = 0; i < np; i++)
    {
      float y0 = poly[i][1], y1 = poly[(i + 1) % np][1];
      if ((y0 <= fy && y1 > fy) || (y1 <= fy && y0 > fy))
        xs[nxs++] = poly[i][0] + (fy - y0) / (y1 - y0) * (poly[(i + 1) % np][0] - poly[i][0]);
    }
    for (int i = 0; i < nxs - 1; i++)
      for (int j = i + 1; j < nxs; j++)
        if (xs[j] < xs[i])
        {
          float tmp = xs[i];
          xs[i] = xs[j];
          xs[j] = tmp;
        }
    for (int i = 0; i + 1 < nxs; i += 2)
      for (int xx = (int)(xs[i] + 0.5f); xx < (int)(xs[i + 1] + 0.5f); xx++)
        if (xx >= 0 && xx < MOUTH_MW)
          mouthMask[yy * MOUTH_MW + xx] = 1;
  }
  // The two "commas": cubics traced as thick circles (stroke 12.29, round cap)
  const float SW = 12.2881f * SC / 2; // stroke radius
  for (int i = 0; i <= 32; i++)
  {
    float s = (float)i / 32;
    bez3(32.1099f, 34.3398f, 37.0394f, 44.1075f, 36.3352f, 71.3541f, 7.81445f, 65.1851f, s, &x, &y);
    mouthStampCircle(x * SC, y * SC, SW);
    bez3(38.8256f, 34.3398f, 33.8961f, 44.1075f, 34.6003f, 71.3541f, 63.1211f, 65.1851f, s, &x, &y);
    mouthStampCircle(x * SC, y * SC, SW);
  }
}

// Mouth blit: resized mask (nearest), solid color

// ---- Mouth of the "laugh" face (AF_RIRE): 0/1/2 mask (transparent/ink/
// white) tessellated from the SVG export Mouth_visage2.svg (viewBox 73x59,
// reference 2026-09-07 (Romain)) -- same technique as the muzzle.
#define LAUGH_MW 146 // 73 x 2
#define LAUGH_MH 118 // 59 x 2
static uint8_t *laughMask = nullptr;

static void laughFillPath(const float *pts, int ncub, uint8_t val)
{
  // pts: x0,y0 then ncub cubics (c1x,c1y,c2x,c2y,px,py), closed outline
  const float SC = 2.0f;
  float poly[220][2];
  int np = 0;
  const int SEG = 24;
  float cxp = pts[0], cyp = pts[1];
  poly[np][0] = cxp * SC;
  poly[np++][1] = cyp * SC;
  for (int c = 0; c < ncub; c++)
  {
    const float *q = &pts[2 + c * 6];
    for (int i = 1; i <= SEG; i++)
    {
      float x, y;
      bez3(cxp, cyp, q[0], q[1], q[2], q[3], q[4], q[5], (float)i / SEG, &x, &y);
      poly[np][0] = x * SC;
      poly[np++][1] = y * SC;
    }
    cxp = q[4];
    cyp = q[5];
  }
  for (int yy = 0; yy < LAUGH_MH; yy++)
  {
    float fy = yy + 0.5f;
    float xs[32];
    int nxs = 0;
    for (int i = 0; i < np; i++)
    {
      float y0 = poly[i][1], y1 = poly[(i + 1) % np][1];
      if ((y0 <= fy && y1 > fy) || (y1 <= fy && y0 > fy))
        xs[nxs++] = poly[i][0] + (fy - y0) / (y1 - y0) * (poly[(i + 1) % np][0] - poly[i][0]);
    }
    for (int i = 0; i < nxs - 1; i++)
      for (int j = i + 1; j < nxs; j++)
        if (xs[j] < xs[i])
        {
          float tmp = xs[i];
          xs[i] = xs[j];
          xs[j] = tmp;
        }
    for (int i = 0; i + 1 < nxs; i += 2)
      for (int xx = (int)(xs[i] + 0.5f); xx < (int)(xs[i + 1] + 0.5f); xx++)
        if (xx >= 0 && xx < LAUGH_MW)
          laughMask[yy * LAUGH_MW + xx] = val;
  }
}

static void initLaughMask()
{
  laughMask = (uint8_t *)calloc(LAUGH_MW * LAUGH_MH, 1);
  static const float BLACK[] = {34.7607f, 5.49146f,
      27.8109f, 5.50955f, 20.212f, 4.00743f, 13.9419f, 2.34413f,
      7.58008f, 0.656501f, 0.862811f, 5.53642f, 1.41466f, 12.0951f,
      3.45424f, 36.335f, 9.38839f, 61.0545f, 38.1324f, 58.2857f,
      67.812f, 55.4268f, 72.9704f, 33.2371f, 72.1404f, 9.20926f,
      71.9349f, 3.26263f, 65.812f, -0.567272f, 60.0948f, 1.08149f,
      52.7209f, 3.20802f, 42.9625f, 5.47012f, 34.7607f, 5.49146f};
  static const float WHITE[] = {38.2526f, 56.2221f,
      57.4991f, 54.998f, 65.3174f, 43.6002f, 66.9171f, 34.5579f,
      44.6629f, 27.8063f, 18.5998f, 32.2446f, 7.56836f, 35.5361f,
      11.2605f, 47.3106f, 19.006f, 57.4462f, 38.2526f, 56.2221f};
  laughFillPath(BLACK, 6, 1);
  laughFillPath(WHITE, 3, 2);
}

// Laugh blit: resized mask (nearest), ink + white
static void drawLaughImg(float cx, float cy, float wpx, float hpx, uint16_t ink)
{
  int iw = (int)wpx, ih = (int)hpx;
  if (iw < 2 || ih < 2 || !laughMask)
    return;
  int x0 = (int)(cx - iw / 2.0f), y0 = (int)(cy - ih / 2.0f);
  uint16_t wht = rgb565(255, 255, 255);
  // 2x2 supersampling of the mask -> ink/white coverage, AA blend
  for (int yy = 0; yy < ih; yy++)
  {
    int mya = (yy * 2) * LAUGH_MH / (ih * 2);
    int myb = (yy * 2 + 1) * LAUGH_MH / (ih * 2);
    for (int xx = 0; xx < iw; xx++)
    {
      int mxa = (xx * 2) * LAUGH_MW / (iw * 2);
      int mxb = (xx * 2 + 1) * LAUGH_MW / (iw * 2);
      uint8_t v0 = laughMask[mya * LAUGH_MW + mxa];
      uint8_t v1 = laughMask[mya * LAUGH_MW + mxb];
      uint8_t v2 = laughMask[myb * LAUGH_MW + mxa];
      uint8_t v3 = laughMask[myb * LAUGH_MW + mxb];
      int nInk = (v0 == 1) + (v1 == 1) + (v2 == 1) + (v3 == 1);
      int nWht = (v0 == 2) + (v1 == 2) + (v2 == 2) + (v3 == 2);
      if (!nInk && !nWht)
        continue;
      if (nWht)
        avBlend(x0 + xx, y0 + yy, wht, nWht / 4.0f);
      if (nInk)
        avBlend(x0 + xx, y0 + yy, ink, nInk / 4.0f);
    }
  }
}

static void drawMouth(float cx, float cy, float wpx, float hpx, uint16_t ink)
{
  int iw = (int)wpx, ih = (int)hpx;
  if (iw < 2 || ih < 2)
    return;
  int x0 = (int)(cx - iw / 2.0f), y0 = (int)(cy - ih / 2.0f);
  // 2x2 supersampling -> coverage, AA blend (see avBlend)
  for (int yy = 0; yy < ih; yy++)
  {
    int mya = (yy * 2) * MOUTH_MH / (ih * 2);
    int myb = (yy * 2 + 1) * MOUTH_MH / (ih * 2);
    for (int xx = 0; xx < iw; xx++)
    {
      int mxa = (xx * 2) * MOUTH_MW / (iw * 2);
      int mxb = (xx * 2 + 1) * MOUTH_MW / (iw * 2);
      int n = mouthMask[mya * MOUTH_MW + mxa] + mouthMask[mya * MOUTH_MW + mxb] +
              mouthMask[myb * MOUTH_MW + mxa] + mouthMask[myb * MOUTH_MW + mxb];
      if (n)
        avBlend(x0 + xx, y0 + yy, ink, n / 4.0f);
    }
  }
}

// original character's muzzle (AF_MUSEAU face), rendered by the platform
static void avatarPlatformMouth(float mx, float my, float mw, float mh, uint16_t ink)
{
  drawMouth(mx, my, mw, mh, ink);
}
// mouth of the "laugh" face (black + white SVG mask)
static void avatarPlatformLaugh(float mx, float my, float mw, float mh, uint16_t ink)
{
  drawLaughImg(mx, my, mw, mh, ink);
}

// ---- "Idle" face state: wandering gaze + blinks (port of
// getIdleState / pickNewLookTarget / drawFaceElements from screen-anims.js) ----

static struct
{
  float lookCX = 0, lookCY = 0, lookPX = 0, lookPY = 0;
  float lookStart = 0, lookDur = 0.5f, lookHold = 0.5f;
  float blinkStart = -1, blinkDur = 0.2f, nextBlink = 2.5f;
} idleSt;

static void pickNewLookTarget(float *tx, float *ty)
{
  float r = frand(0, 1);
  if (r < 0.30f)
  {
    *tx = frand(-0.15f, 0.15f);
    *ty = frand(-0.15f, 0.15f);
  }
  else if (r < 0.50f)
  {
    *tx = -(0.5f + frand(0, 0.5f));
    *ty = frand(-0.2f, 0.2f);
  }
  else if (r < 0.70f)
  {
    *tx = 0.5f + frand(0, 0.5f);
    *ty = frand(-0.2f, 0.2f);
  }
  else
  {
    *tx = frand(-0.7f, 0.7f);
    *ty = frand(-0.5f, 0.5f);
  }
}

// Returns lookX, lookY [-1..1] and openness [0..1]
static void getIdle(float t, float *lookX, float *lookY, float *openness)
{
  if (t >= idleSt.lookHold)
  {
    idleSt.lookPX = idleSt.lookCX;
    idleSt.lookPY = idleSt.lookCY;
    pickNewLookTarget(&idleSt.lookCX, &idleSt.lookCY);
    idleSt.lookStart = t;
    idleSt.lookDur = 0.4f + frand(0, 0.5f);
    idleSt.lookHold = t + idleSt.lookDur + 0.8f + frand(0, 2.2f);
  }
  float lt = (t - idleSt.lookStart) / idleSt.lookDur;
  lt = lt < 0 ? 0 : (lt > 1 ? 1 : lt);
  float e = lt < 0.5f ? 4 * lt * lt * lt : 1 - powf(-2 * lt + 2, 3) / 2; // easeInOutCubic
  float lx = idleSt.lookPX + (idleSt.lookCX - idleSt.lookPX) * e;
  float ly = idleSt.lookPY + (idleSt.lookCY - idleSt.lookPY) * e;
  lx += sinf(t * 1.4f) * 0.04f + sinf(t * 0.7f + 1.3f) * 0.025f;
  ly += sinf(t * 1.1f + 0.4f) * 0.03f + sinf(t * 0.5f + 2.1f) * 0.02f;
  *lookX = max(-1.0f, min(1.0f, lx));
  *lookY = max(-1.0f, min(1.0f, ly));

  *openness = 1;
  if (idleSt.blinkStart < 0 && t >= idleSt.nextBlink)
  {
    idleSt.blinkStart = t;
    idleSt.blinkDur = 0.14f + frand(0, 0.10f);
  }
  if (idleSt.blinkStart >= 0)
  {
    float bt = (t - idleSt.blinkStart) / idleSt.blinkDur;
    if (bt >= 1 || bt < 0) // bt < 0: local time jumped backwards
    {
      idleSt.blinkStart = -1;
      idleSt.nextBlink = t + (frand(0, 1) < 0.25f ? 0.15f : 1.8f + frand(0, 3.5f));
    }
    else
      *openness = bt < 0.4f ? 1 - bt / 0.4f : (bt - 0.4f) / 0.6f;
  }
}

// social reaction (social_ui.h, included by emu.cpp): replaces the face
// during the 5 s of a badge-to-badge encounter
static bool socialExprFace(float cx, float cy, float fr);

// Animated face projected onto a sphere of radius fr centered (cx, cy):
// round eyes following the gaze (lateral squish), blink, SVG mouth.
static void drawIdleFaceLook(float cx, float cy, float fr, float t,
                             float lookX, float lookY, float openness)
{
  if (socialExprFace(cx, cy, fr))
    return; // Happy/Wow/Love expression instead of the normal face
  uint16_t ink = rgb565(39, 39, 39); // #272727
  float breathe = sinf(t * 1.8f) * 0.5f;
  float theta = lookX * 30.0f * PI / 180.0f;
  // face of the displayed avatar (9 Figma designs): projection and
  // rendering entirely in avatars.h (shared firmware/emulator)
  avatarDrawFace(cx, cy, fr, breathe, lookY * fr * 0.18f, cosf(theta),
                 sinf(theta), openness, ink);
  avatarDrawExtras(cx, cy, fr, breathe);
}

static void drawIdleFace(float cx, float cy, float fr, float t)
{
  float lookX, lookY, openness;
  getIdle(t, &lookX, &lookY, &openness);
  drawIdleFaceLook(cx, cy, fr, t, lookX, lookY, openness);
}

// ------------------------------------------------- snake (trail of spheres)

#define SNAKE_N 13
#define TRAIL_MAX 160
struct Vec2
{
  float x, y;
};
static Vec2 snakeTrail[TRAIL_MAX];
static int snakeTrailLen = 0;
static float snakeX, snakeY, snakeAngle;
static bool snakeInit = false;

// Simple snake face (eyes + quadratic smile, faithful port of drawSnake)
static void drawSnakeFace(float cx, float cy, float r, float t)
{
  uint16_t ink = rgb565(39, 39, 39);
  float ex = r * 0.30f, ey = -r * 0.18f, er = r * 0.10f;
  canvas->fillCircle((int16_t)(cx - ex), (int16_t)(cy + ey), (int16_t)er, ink);
  canvas->fillCircle((int16_t)(cx + ex), (int16_t)(cy + ey), (int16_t)er, ink);
  float p0x = -r * 0.26f, p0y = r * 0.02f, pcx = 0, pcy = r * 0.26f;
  float p1x = r * 0.26f, p1y = r * 0.02f;
  int16_t dotR = max(2, (int)(r * 0.030f));
  for (int k = 0; k <= 16; k++)
  {
    float s = k / 16.0f, u = 1.0f - s;
    float x = u * u * p0x + 2 * u * s * pcx + s * s * p1x;
    float y = u * u * p0y + 2 * u * s * pcy + s * s * p1y;
    canvas->fillCircle((int16_t)(cx + x), (int16_t)(cy + y), dotR, ink);
  }
}

static void animSnake(float t, float dt)
{
  if (g_ballDirty) // avatar/buddy changed: re-tint the ball sprite
  {
    g_ballDirty = false;
    initBallSprite();
  }
  const float headR = RADIUS * 0.30f;
  const float Rmax = RADIUS - headR - 4;
  const float speed = RADIUS * 0.55f;

  if (!snakeInit)
  {
    snakeInit = true;
    snakeX = CX;
    snakeY = CY;
    snakeAngle = frand(0, 2 * PI);
    snakeTrail[0] = {snakeX, snakeY};
    snakeTrailLen = 1;
  }

  if (dt > 0.09f)
    dt = 0.09f;
  float turn = sinf(t * 0.8f) * 1.2f + sinf(t * 0.33f + 2.1f) * 0.7f;
  snakeAngle += turn * dt;
  float nx = snakeX + cosf(snakeAngle) * speed * dt;
  float ny = snakeY + sinf(snakeAngle) * speed * dt;
  float dxc = nx - CX, dyc = ny - CY;
  float d = sqrtf(dxc * dxc + dyc * dyc);
  if (d > Rmax)
  {
    float nX = dxc / d, nY = dyc / d;
    float vX = cosf(snakeAngle), vY = sinf(snakeAngle);
    float dot = vX * nX + vY * nY;
    snakeAngle = atan2f(vY - 2 * dot * nY, vX - 2 * dot * nX);
    nx = CX + nX * Rmax;
    ny = CY + nY * Rmax;
  }
  snakeX = nx;
  snakeY = ny;
  if (snakeTrailLen < TRAIL_MAX)
    snakeTrailLen++;
  memmove(&snakeTrail[1], &snakeTrail[0], (snakeTrailLen - 1) * sizeof(Vec2));
  snakeTrail[0] = {nx, ny};

  const float spacing = headR * 0.52f;
  Vec2 pts[SNAKE_N];
  pts[0] = {snakeX, snakeY};
  int np = 1, ti = 0;
  float need = spacing, acc = 0;
  while (np < SNAKE_N && ti < snakeTrailLen - 1)
  {
    Vec2 a = snakeTrail[ti], b = snakeTrail[ti + 1];
    float segLen = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
    if (acc + segLen >= need)
    {
      float f = (need - acc) / (segLen > 0 ? segLen : 1);
      pts[np++] = {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
      need += spacing;
    }
    else
    {
      acc += segLen;
      ti++;
    }
  }
  while (np < SNAKE_N)
  {
    pts[np] = pts[np - 1];
    np++;
  }

  canvas->fillScreen(RGB565_BLACK);
  for (int i = SNAKE_N - 1; i >= 0; i--)
  {
    float sr = headR * (1.0f - 0.25f * i / (SNAKE_N - 1));
    drawBallSprite((int16_t)pts[i].x, (int16_t)pts[i].y, sr);
  }
  // the face looks in the direction of movement (smoothed angle so
  // that bounces off the edges do not snap the gaze); blinking still
  // comes from getIdle
  {
    static float lkx = 0, lky = 0;
    float tx = cosf(snakeAngle), ty = sinf(snakeAngle) * 0.7f;
    lkx += (tx - lkx) * 0.18f;
    lky += (ty - lky) * 0.18f;
    float ix, iy, openness;
    getIdle(t, &ix, &iy, &openness);
    drawIdleFaceLook(pts[0].x, pts[0].y, headR, t, lkx, lky, openness);
  }
}

// ---------------------------------------------------- disco (mirror ball)

static const uint8_t DISCO_PALS[5][3] = {
    {158, 197, 240}, {255, 167, 254}, {255, 203, 138}, {159, 146, 243}, {128, 219, 188}};

static void animDisco(float t)
{
  const float Rb = RADIUS * 0.74f;
  const int NLAT = 15, NLON = 26;
  const float rot = t * 0.6f;
  const float Lx = -0.45f, Ly = -0.52f, Lz = 0.72f;

  canvas->fillScreen(rgb565(8, 6, 16)); // #080610

  // subtle halo behind the ball (before the lights so it does not hide them)
  canvas->fillCircle(CX, CY, (int16_t)(Rb * 1.12f), rgb565(20, 22, 42));

  // colored light dots sweeping the background (twinkling)
  for (int i = 0; i < 42; i++)
  {
    float a = i * 2.39996f + t * 0.35f;
    float rr = RADIUS * (0.55f + ((i * 53) % 100) / 100.0f * 0.55f);
    int16_t x = CX + (int16_t)(cosf(a) * rr);
    int16_t y = CY + (int16_t)(sinf(a * 1.27f + i) * rr);
    float tw = 0.5f + 0.5f * sinf(t * 4 + i * 1.7f);
    if (tw < 0.45f)
      continue;
    uint8_t hue = (uint8_t)(fmodf(i * 37 + t * 60.0f, 360.0f) * 255.0f / 360.0f);
    uint8_t val = (uint8_t)max(0.0f, min(255.0f, 90 + (tw - 0.45f) * 300.0f));
    int16_t s = (int16_t)(2 + tw * 6);
    canvas->fillRect(x - s / 2, y - s / 2, s, s, hsv2rgb565(hue, 200, val));
  }

  // facets: projected lat/long grid, front hemisphere only
  for (int i = 0; i < NLAT; i++)
  {
    float f0 = -PI / 2 + (float)i / NLAT * PI;
    float f1 = -PI / 2 + (float)(i + 1) / NLAT * PI;
    float fc = (f0 + f1) / 2;
    for (int j = 0; j < NLON; j++)
    {
      float l0 = (float)j / NLON * 2 * PI + rot;
      float l1 = (float)(j + 1) / NLON * 2 * PI + rot;
      float lc = (l0 + l1) / 2;
      float nx = cosf(fc) * sinf(lc), ny = sinf(fc), nz = cosf(fc) * cosf(lc);
      if (nz <= 0.04f)
        continue;
      float b = nx * Lx + ny * Ly + nz * Lz;
      if (b < 0)
        b = 0;
      int seed = i * 131 + j * 57;
      float tw = 0.5f + 0.5f * sinf(t * 3 + seed);
      const uint8_t *base = DISCO_PALS[(i * 7 + j * 3) % 5];
      float sf = 0.28f + b * 1.05f;
      float gm = (b > 0.55f && tw > 0.8f) ? 0.82f : 0;
      float fr = min(255.0f, base[0] * sf), fg = min(255.0f, base[1] * sf), fb2 = min(255.0f, base[2] * sf);
      uint8_t cr = (uint8_t)(fr + (255 - fr) * gm);
      uint8_t cg = (uint8_t)(fg + (255 - fg) * gm);
      uint8_t cb = (uint8_t)(fb2 + (255 - fb2) * gm);
      uint16_t col = rgb565(cr, cg, cb);
      uint16_t edge = rgb565(cr * 0.55f, cg * 0.55f, cb * 0.55f);

      const float P[4][2] = {{f0, l0}, {f0, l1}, {f1, l1}, {f1, l0}};
      int16_t sx[4], sy[4];
      for (int k = 0; k < 4; k++)
      {
        float x = cosf(P[k][0]) * sinf(P[k][1]);
        float y = sinf(P[k][0]);
        sx[k] = CX + (int16_t)(x * Rb);
        sy[k] = CY - (int16_t)(y * Rb);
      }
      canvas->fillTriangle(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2], col);
      canvas->fillTriangle(sx[0], sy[0], sx[2], sy[2], sx[3], sy[3], col);
      for (int k = 0; k < 4; k++)
        canvas->drawLine(sx[k], sy[k], sx[(k + 1) % 4], sy[(k + 1) % 4], edge);
    }
  }

  // animated character face, camera-facing, scaled to the ball
  drawIdleFace(CX, CY, Rb, t);
}

// =====================================================================
// ==== PORT END =======================================================
// =====================================================================

// New anims shared as-is with the firmware
#include "anims_extra.h"
// (tama.h comes in via games.h in the emulator)

static void savePPM(const char *path)
{
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++)
  {
    uint16_t c = theCanvas.fb[i];
    uint8_t px[3] = {(uint8_t)(((c >> 11) & 0x1F) << 3), (uint8_t)(((c >> 5) & 0x3F) << 2),
                     (uint8_t)((c & 0x1F) << 3)};
    fwrite(px, 1, 3, f);
  }
  fclose(f);
}

// (harness main removed -- see emu.cpp)
