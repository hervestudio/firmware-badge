// threejs.paris badge - animations on a GC9B72 2.1" 360x360 screen (4-wire
// SPI), ESP32-S3.
// Rendered via framebuffer (Arduino_Canvas, ~253 KB in PSRAM) then full
// flush() over SPI: no flicker, everything is drawn off-screen.
//
// 4 Three.js-style animations, cycled every 15 s:
//   0. 3D wireframe cube
//   1. starfield (flight through the stars)
//   2. plasma (sine LUT + palette, computed at half resolution)
//   3. torus as a point cloud
//
// SPI throughput caps the framerate: one 360x360x16bit flush = ~2 Mbits.
//   20 MHz -> ~9 fps max; 40 MHz -> ~19 fps max.
// 20 MHz is what the lib deems reliable on short wires. Try 40000000;
// drop back to 20000000 / 10000000 if you see noise or artifacts.

#include <Arduino_GFX_Library.h>
#include <Arduino_GC9B72.h>
#include <math.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
// included here (before the #define W/H...): WebSockets pulls in mbedtls,
// which uses identifiers named W - the macros would break them
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <time.h>
#include <sys/time.h>
#include <driver/rtc_io.h>
#include <driver/gpio.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <esp_mac.h> // eFuse MAC (random buddy draw) // speaker photo uploaded via Setup (spiffs partition)

// ---- Wiring (module: GND VCC SCL SDA RST DC CS BL SDO TE) ----
// VCC -> 3V3   GND -> GND
// SDO: not connected (lands opposite GPIO 46, left empty)
// RIBBON mapping (review 2026-08-14): the GPIOs are chosen so the 10-wire
// rainbow ribbon lands in the EXACT order of the devkit header, rows
// 13..20 contiguous, zero crossings (black TE=3, [46 empty], gray BL=9,
// purple CS=10, blue DC=11, green RST=12, yellow MOSI=13, orange SCLK=14;
// brown GND 2 rows lower, red 3V3 goes up alone to the top of the header).
// Badges wired BEFORE 2026-08-14 (flying wires): old mapping
// SCLK 12 / MOSI 11 / DC 13 / RST 14 -> redo those 4 wires on the ESP side,
// OR flash with the "proto" env (pio run -e proto -t upload) which keeps
// the old wiring - used for Romain's first prototype.
#ifdef PROTO_V1_WIRING
#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_DC 13
#define TFT_RST 14
#else
#define TFT_SCLK 14 // <- SCL  (orange)
#define TFT_MOSI 13 // <- SDA  (jaune)
#define TFT_DC 11   // <- DC   (bleu)
#define TFT_RST 12  // <- RST  (vert)
#endif
#define TFT_CS 10   // <- CS   (violet)
#define TFT_TE 3    // <- TE (pulse at each scan start, TEON enabled by the driver)
#define TFT_BL 9        // <- BL (backlight): driven by GPIO so it can be cut.
                        // GPIO 9 = pin NEXT TO the 10-14 screen block on the
                        // devkit header (contiguous ribbon wiring). The first
                        // badges were wired on GPIO 4 (top of the header): both
                        // pins are driven in parallel, no rewiring needed.
#define TFT_BL_LEGACY 4 // <- BL of the first badges (left floating on new ones)
                    //    at power-off - never wire it straight to 3V3 anymore!

// Navigation buttons (between GPIO and GND, internal pull-up, active LOW).
// 19/20/21: top-left corner of the devkit, next to a GND - short wiring.
// NB: 19/20 = D-/D+ of native USB, free because ARDUINO_USB_CDC_ON_BOOT is
// disabled (platformio.ini); flash and logs go through the CH343 bridge.
#define BTN_NEXT 19 // next / move down in the menu
#define BTN_PREV 20 // previous / move up in the menu; held at BOOT -> OTA flash mode
#define BTN_AUTO 21 // short press: open menu / select; long press 2 s: power off
#define BTN_BOOT 0  // board BOOT button: also "next" (handy for testing)
#define PIN_RGB 48  // devkit WS2812 RGB LED: never used, but its floating
                    // data pin can make it "latch" a color that stays lit
                    // (glow visible through the panel, even with the badge
                    // off - review 2026-09-05 (Romain)). We turn it off
                    // explicitly at boot and before deep sleep.

// Battery gauge (menu): 100k/100k divider B+ -> GPIO5 -> GND, plus charge
// detection from the TP4056's VBUS via 100k/100k -> GPIO6.
// Firmware is tolerant: without these wires the menu shows "--%", no bolt.
#ifdef PROTO_V1_WIRING
#define PIN_VBAT 5 // first proto: battery divider on 5/6 (tolerant if absent)
#define PIN_VBUS 6
#else
#define PIN_VBAT 1 // battery divider (was GPIO 5 - 1/2 simplify the wiring)
#define PIN_VBUS 2 // charge detection (was GPIO 6)
#endif

// OTA flash mode (PREV button held at power-on): the badge creates its own
// Wi-Fi access point and waits for the upload (pio run -e ota -t upload).
#define OTA_SSID "badge-threejs"
#define OTA_PASS "threejs2026"
static bool otaMode = false;

#define SPI_FREQ 80000000 // 80 MHz: flush ~26 ms instead of ~52 (validated on
                          // short soldered ribbon; go back to 40 MHz if
                          // artifacts appear on flying wires)

#define W 360
#define H 360
#define CX 180
#define CY 180

#define ANIM_COUNT 17
#define ANIM_DURATION_MS 15000
#define RADIUS 180 // usable radius of the round screen

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED /*MISO*/);
Arduino_GFX *panel = new Arduino_GC9B72(bus, TFT_RST, 0 /*rotation*/, false /*IPS*/, W, H);
Arduino_Canvas *canvas = new Arduino_Canvas(W, H, panel);

#include "dma_flush.h" // async SPI3+DMA flush (replaces canvas->flush)
static bool dmafOk = false;

// -------------------------------------------------------------------- helpers

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

static float frand(float lo, float hi)
{
  return lo + (hi - lo) * (float)random(10000) / 10000.0f;
}

// TE pulse counter (diagnostic): lets us check that the signal actually
// arrives and measure the panel's scan frequency.
static volatile uint32_t teCount = 0;
static void IRAM_ATTR teIsr() { teCount++; }

// Button presses captured by interrupt: the loop only runs at ~10 Hz, a
// brief press would be missed by polling. Debouncing is done in loop().
static volatile bool btnNextFlag = false;
static volatile bool btnPrevFlag = false;
static void IRAM_ATTR btnNextIsr() { btnNextFlag = true; }
static void IRAM_ATTR btnPrevIsr() { btnPrevFlag = true; }

// Center button: a SHORT press is detected on RELEASE (30-600 ms), so it
// does not trigger at the start of a long press (power off). The long press
// is monitored by polling in loop() via autoPressMs.
static volatile uint32_t autoPressMs = 0;
static volatile bool btnAutoShort = false;
static void IRAM_ATTR btnAutoIsr()
{
  uint32_t ms = millis();
  if (digitalRead(BTN_AUTO) == LOW)
    autoPressMs = ms;
  else
  {
    if (autoPressMs && ms - autoPressMs >= 30 && ms - autoPressMs < 600)
      btnAutoShort = true;
    autoPressMs = 0;
  }
}

// Waits for the next rising edge of TE so the flush starts at the beginning
// of a scan: the tearing point becomes fixed instead of scrolling.
// If no TE pulse is seen for 2 s (wire unplugged/bad contact), switch to
// bypass: no more waiting, until the signal comes back.
static bool teAlive = true;
static void waitTE()
{
  if (!teAlive)
    return;
  uint32_t t0 = millis();
  while (digitalRead(TFT_TE) == HIGH)
    if (millis() - t0 > 30)
      return;
  while (digitalRead(TFT_TE) == LOW)
    if (millis() - t0 > 30)
      return;
}

static void powerOff(); // defined after the animations

// ------------------------------------------------------------- 0. 3D cube

static void animCube(float t)
{
  static const float V[8][3] = {
      {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
      {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
  static const uint8_t E[12][2] = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0},
      {4, 5}, {5, 6}, {6, 7}, {7, 4},
      {0, 4}, {1, 5}, {2, 6}, {3, 7}};

  canvas->fillScreen(RGB565_BLACK);

  float ax = t * 0.9f, ay = t * 1.3f, az = t * 0.4f;
  float cax = cosf(ax), sax = sinf(ax);
  float cay = cosf(ay), say = sinf(ay);
  float caz = cosf(az), saz = sinf(az);

  int16_t px[8], py[8];
  for (int i = 0; i < 8; i++)
  {
    float x = V[i][0], y = V[i][1], z = V[i][2];
    // rotation X then Y then Z
    float y1 = y * cax - z * sax, z1 = y * sax + z * cax;
    float x2 = x * cay + z1 * say, z2 = -x * say + z1 * cay;
    float x3 = x2 * caz - y1 * saz, y3 = x2 * saz + y1 * caz;
    float s = 190.0f / (z2 + 3.4f);
    px[i] = CX + (int16_t)(x3 * s);
    py[i] = CY + (int16_t)(y3 * s);
  }

  for (int i = 0; i < 12; i++)
  {
    uint16_t c = hsv2rgb565((uint8_t)(t * 30.0f + i * 12), 255, 255);
    canvas->drawLine(px[E[i][0]], py[E[i][0]], px[E[i][1]], py[E[i][1]], c);
  }
  for (int i = 0; i < 8; i++)
    canvas->fillCircle(px[i], py[i], 3, RGB565_WHITE);
}

// ---------------------------------------------------------- 1. starfield

#define NSTARS 260
struct Star
{
  float x, y, z;
};
static Star stars[NSTARS];

static void resetStar(Star &s)
{
  s.x = frand(-1.0f, 1.0f);
  s.y = frand(-1.0f, 1.0f);
  s.z = frand(0.3f, 1.0f);
}

static void animStars(float dt)
{
  canvas->fillScreen(RGB565_BLACK);
  for (int i = 0; i < NSTARS; i++)
  {
    Star &s = stars[i];
    s.z -= dt * 0.45f;
    if (s.z <= 0.06f)
      resetStar(s);
    int16_t sx = CX + (int16_t)(s.x / s.z * 200.0f);
    int16_t sy = CY + (int16_t)(s.y / s.z * 200.0f);
    if (sx < 0 || sx >= W || sy < 0 || sy >= H)
      continue;
    uint8_t v = (uint8_t)constrain(280.0f * (1.05f - s.z), 40.0f, 255.0f);
    uint16_t c = hsv2rgb565(150, 60, v); // bluish white
    if (s.z < 0.35f)
      canvas->fillRect(sx, sy, 2, 2, c);
    else
      canvas->drawPixel(sx, sy, c);
  }
}

// ------------------------------------------------------------- 2. plasma

static uint8_t sinLUT[256];
static uint16_t palLUT[256];
static uint8_t radLUT[180 * 180];

static void initPlasma()
{
  for (int i = 0; i < 256; i++)
  {
    sinLUT[i] = (uint8_t)(128.0f + 127.0f * sinf(i * 2.0f * PI / 256.0f));
    palLUT[i] = hsv2rgb565((uint8_t)i, 255, 255);
  }
  for (int y = 0; y < 180; y++)
    for (int x = 0; x < 180; x++)
    {
      float dx = x - 90.0f, dy = y - 90.0f;
      radLUT[y * 180 + x] = (uint8_t)((uint32_t)(sqrtf(dx * dx + dy * dy) * 3.0f) & 255);
    }
}

static void animPlasma(float t)
{
  // computed at 180x180, each value fills a 2x2 block of the framebuffer
  // Deliberately slow motion: the flush (~52 ms) crosses the panel scan
  // (60 Hz) 3x; if two consecutive frames are close, the crossing points
  // (tearing) become invisible.
  uint16_t *fb = canvas->getFramebuffer();
  uint16_t ti = (uint16_t)(t * 18.0f);
  for (int y = 0; y < 180; y++)
  {
    uint16_t *row0 = fb + (y * 2) * W;
    uint16_t *row1 = row0 + W;
    const uint8_t *rad = &radLUT[y * 180];
    uint8_t sy = sinLUT[(uint8_t)(y * 2 + (ti >> 1))];
    for (int x = 0; x < 180; x++)
    {
      uint16_t v = sinLUT[(uint8_t)(x * 3 + ti)] + sy + sinLUT[(uint8_t)(rad[x] + ti)];
      uint16_t c = palLUT[(uint8_t)(v / 3 + (ti >> 1))];
      int xx = x * 2;
      row0[xx] = c;
      row0[xx + 1] = c;
      row1[xx] = c;
      row1[xx + 1] = c;
    }
  }
}

// -------------------------------------------------------------- 3. torus

#define TOR_RINGS 26
#define TOR_SEGS 15
#define TOR_PTS (TOR_RINGS * TOR_SEGS)
static float torus[TOR_PTS][3];

static void initTorus()
{
  const float R = 1.0f, r = 0.42f;
  int n = 0;
  for (int i = 0; i < TOR_RINGS; i++)
  {
    float u = i * 2.0f * PI / TOR_RINGS;
    for (int j = 0; j < TOR_SEGS; j++)
    {
      float v = j * 2.0f * PI / TOR_SEGS;
      torus[n][0] = (R + r * cosf(v)) * cosf(u);
      torus[n][1] = (R + r * cosf(v)) * sinf(u);
      torus[n][2] = r * sinf(v);
      n++;
    }
  }
}

static void animTorus(float t)
{
  canvas->fillScreen(RGB565_BLACK);
  float ax = t * 0.8f, ay = t * 1.1f;
  float cax = cosf(ax), sax = sinf(ax);
  float cay = cosf(ay), say = sinf(ay);
  for (int i = 0; i < TOR_PTS; i++)
  {
    float x = torus[i][0], y = torus[i][1], z = torus[i][2];
    float y1 = y * cax - z * sax, z1 = y * sax + z * cax;
    float x2 = x * cay + z1 * say, z2 = -x * say + z1 * cay;
    float s = 165.0f / (z2 + 2.6f);
    int16_t sx = CX + (int16_t)(x2 * s);
    int16_t sy = CY + (int16_t)(y1 * s);
    // hue from the position on the ring, brightness from depth
    uint8_t hue = (uint8_t)(i * 255 / TOR_PTS + (uint8_t)(t * 25.0f));
    uint8_t val = (uint8_t)constrain(190.0f - z2 * 90.0f, 70.0f, 255.0f);
    uint16_t c = hsv2rgb565(hue, 230, val);
    if (z2 < 0.0f)
      canvas->fillCircle(sx, sy, 2, c);
    else
      canvas->drawPixel(sx, sy, c);
  }
}

// ---- Ports from speaker-badge-anims (screen-anims.js) ----

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

#include "avatars.h" // 40 avatars (table + face) - before anims_extra.h

// ---- "Rainbow" sphere texture: Gaussian blend of the colored points of
// PAL_RAINBOW (screen-anims.js), vibrance + grain, precomputed at boot. ----

#define SPR 112 // sprite size (biggest use: snake head, 108 px)
static uint16_t *ballSprite = nullptr;

// {px, py, r, g, b} - color points on the unit disk
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
  if (!ballSprite) // regenerated on avatar change (g_ballDirty)
    ballSprite = (uint16_t *)malloc(SPR * SPR * sizeof(uint16_t));
  // colors transformed by the active avatar / custom buddy (same transform
  // as the idle sphere): the snake, the DVD rainbow palette and Sphere Run
  // follow the badge's configured color
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
static void drawBallSprite(int cx, int cy, float rf)
{
  int r = (int)rf;
  if (r < 2 || !ballSprite)
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

// ---- Mascot SVG mouth (MOUTH_SVG from screen-anims.js), rasterized at
// boot into a mask, then blitted to scale. ----

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
  float poly[200][2];
  int np = 0;
  const int SEG = 24;
  float x, y;
  for (int i = 0; i <= SEG; i++)
  {
    bez3(12.8717f, 29.0658f, 0.183f, 19.6626f, 6.83373f, -0.481934f, 22.6268f, -0.481934f, (float)i / SEG, &x, &y);
    poly[np][0] = x * SC;
    poly[np++][1] = y * SC;
  }
  poly[np][0] = 51.1003f * SC;
  poly[np++][1] = -0.481934f * SC;
  for (int i = 1; i <= SEG; i++)
  {
    bez3(51.1003f, -0.481934f, 66.7113f, -0.481934f, 73.4774f, 19.2799f, 61.1424f, 28.8482f, (float)i / SEG, &x, &y);
    poly[np][0] = x * SC;
    poly[np++][1] = y * SC;
  }
  poly[np][0] = 48.6013f * SC;
  poly[np++][1] = 38.5763f * SC;
  for (int i = 1; i <= SEG; i++)
  {
    bez3(48.6013f, 38.5763f, 45.7282f, 40.805f, 42.1954f, 42.0146f, 38.5592f, 42.0146f, (float)i / SEG, &x, &y);
    poly[np][0] = x * SC;
    poly[np++][1] = y * SC;
  }
  poly[np][0] = 35.7539f * SC;
  poly[np++][1] = 42.0146f * SC;
  for (int i = 1; i <= SEG; i++)
  {
    bez3(35.7539f, 42.0146f, 32.241f, 42.0146f, 28.8211f, 40.8855f, 25.9988f, 38.7939f, (float)i / SEG, &x, &y);
    poly[np][0] = x * SC;
    poly[np++][1] = y * SC;
  }
  // even-odd scanline fill of the outline
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
  // the two "commas": cubics drawn as thick circles (stroke 12.29, round cap)
  const float SW = 12.2881f * SC / 2;
  for (int i = 0; i <= 32; i++)
  {
    float s = (float)i / 32;
    bez3(32.1099f, 34.3398f, 37.0394f, 44.1075f, 36.3352f, 71.3541f, 7.81445f, 65.1851f, s, &x, &y);
    mouthStampCircle(x * SC, y * SC, SW);
    bez3(38.8256f, 34.3398f, 33.8961f, 44.1075f, 34.6003f, 71.3541f, 63.1211f, 65.1851f, s, &x, &y);
    mouthStampCircle(x * SC, y * SC, SW);
  }
}


// ---- Mouth of the "laugh" face (AF_RIRE): 0/1/2 mask (transparent/ink/
// white) tessellated from the SVG export Mouth_visage2.svg (viewBox 73x59,
// reference 2026-09-07 (Romain)) - same technique as the muzzle.
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

// Mouth blit: resized mask (nearest), solid color
static void drawMouthImg(float cx, float cy, float wpx, float hpx, uint16_t ink)
{
  int iw = (int)wpx, ih = (int)hpx;
  if (iw < 2 || ih < 2 || !mouthMask)
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

// ---- Face "idle" state: wandering gaze + blinks (port of getIdleState /
// pickNewLookTarget / drawFaceElements from screen-anims.js) ----

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

// Animated face projected onto a sphere of radius fr centered at (cx, cy):
// round eyes following the gaze (lateral squish), blink, SVG mouth.
// "Look" variant: the gaze is supplied by the caller (shared with the
// Idle Rainbow texture rotation).
// muzzle of the original character (AF_MUSEAU face), drawn by the platform
static void avatarPlatformMouth(float mx, float my, float mw, float mh, uint16_t ink)
{
  drawMouthImg(mx, my, mw, mh, ink);
}
// mouth of the "laugh" face, same principle (black + white SVG mask)
static void avatarPlatformLaugh(float mx, float my, float mw, float mh, uint16_t ink)
{
  drawLaughImg(mx, my, mw, mh, ink);
}

// social reaction (social_ui.h, included below): replaces the face during
// the 5 s of a badge-to-badge encounter
static bool socialExprFace(float cx, float cy, float fr);

static void drawIdleFaceLook(float cx, float cy, float fr, float t,
                             float lookX, float lookY, float openness)
{
  if (socialExprFace(cx, cy, fr))
    return; // Happy/Wow/Love expression instead of the normal face
  uint16_t ink = rgb565(39, 39, 39); // #272727
  float breathe = sinf(t * 1.8f) * 0.5f;
  float theta = lookX * 30.0f * PI / 180.0f;
  // face of the displayed avatar (9 Figma designs): projection and render
  // entirely in avatars.h (shared firmware/emulator)
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

// Character face (eyes + smile, periodic blink), proportions from the JS.
static void drawFace(float cx, float cy, float r, float t)
{
  uint16_t ink = rgb565(39, 39, 39); // #272727
  float ex = r * 0.30f, ey = -r * 0.18f, er = r * 0.10f;
  bool blink = fmodf(t, 3.2f) > 3.05f;
  if (blink)
  {
    int16_t h = max(2, (int)(r * 0.04f));
    canvas->fillRect((int16_t)(cx - ex - er), (int16_t)(cy + ey - h / 2), (int16_t)(2 * er), h, ink);
    canvas->fillRect((int16_t)(cx + ex - er), (int16_t)(cy + ey - h / 2), (int16_t)(2 * er), h, ink);
  }
  else
  {
    canvas->fillCircle((int16_t)(cx - ex), (int16_t)(cy + ey), (int16_t)er, ink);
    canvas->fillCircle((int16_t)(cx + ex), (int16_t)(cy + ey), (int16_t)er, ink);
  }
  // smile: quadratic bezier (-0.26r,0.02r) -> (0,0.26r) -> (0.26r,0.02r),
  // drawn as round dots (equivalent to a thick round-capped stroke)
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

// --------------------------------------------------- 4. snake (sphere trail)

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

  // smooth sinusoidal turning + billiard bounce on the disk edge
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

  // body sampled at constant spacing along the trail
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
  // tail -> head (head drawn on top), slight taper toward the tail
  for (int i = SNAKE_N - 1; i >= 0; i--)
  {
    float sr = headR * (1.0f - 0.25f * i / (SNAKE_N - 1));
    drawBallSprite((int)pts[i].x, (int)pts[i].y, sr);
  }
  // the face looks in the direction of travel (smoothed angle so bounces
  // on the edges do not snap the gaze); blinking still comes from getIdle
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

// ---------------------------------------------------- 5. disco (mirror ball)

static const uint8_t DISCO_PALS[5][3] = {
    {158, 197, 240}, {255, 167, 254}, {255, 203, 138}, {159, 146, 243}, {128, 219, 188}};

static void animDisco(float t)
{
  const float Rb = RADIUS * 0.74f;
  const int NLAT = 15, NLON = 26;
  const float rot = t * 0.6f;
  const float Lx = -0.45f, Ly = -0.52f, Lz = 0.72f; // light top-left-front

  canvas->fillScreen(rgb565(8, 6, 16)); // #080610

  // subtle halo behind the ball (before the lights so it does not cover them)
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
    uint8_t val = (uint8_t)constrain(90 + (tw - 0.45f) * 300.0f, 0.0f, 255.0f);
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
        continue; // back face
      float b = nx * Lx + ny * Ly + nz * Lz;
      if (b < 0)
        b = 0;
      int seed = i * 131 + j * 57;
      float tw = 0.5f + 0.5f * sinf(t * 3 + seed);
      const uint8_t *base = DISCO_PALS[(i * 7 + j * 3) % 5];
      float sf = 0.28f + b * 1.05f;
      float gm = (b > 0.55f && tw > 0.8f) ? 0.82f : 0; // glint -> toward white
      float fr = min(255.0f, base[0] * sf), fg = min(255.0f, base[1] * sf), fb = min(255.0f, base[2] * sf);
      uint8_t cr = (uint8_t)(fr + (255 - fr) * gm);
      uint8_t cg = (uint8_t)(fg + (255 - fg) * gm);
      uint8_t cb = (uint8_t)(fb + (255 - fb) * gm);
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

  // animated character face, facing the camera, scaled to the ball
  drawIdleFace(CX, CY, Rb, t);
}

// Reset of the idle face state (on animation change, local time restarts at
// 0: without a reset, the gaze/blink timers would be wrong)
static void resetIdle()
{
  idleSt.lookCX = idleSt.lookCY = idleSt.lookPX = idleSt.lookPY = 0;
  idleSt.lookStart = 0;
  idleSt.lookDur = 0.5f;
  idleSt.lookHold = 0.5f;
  idleSt.blinkStart = -1;
  idleSt.nextBlink = 2.5f;
}

// ---------------------- 6. three globe / 7. three conf (shared file) ----
#include "anims_extra.h"
#include "menu_font.h" // Dingos ExtraBold for the menu
#include "menu_font_med.h" // Dingos Medium (battery percentage)
#include "menu_font_bebas.h" // Bebas Neue (Schedule event types)
#include "menu_font_title.h" // Dingos ExtraBold 30 (Schedule titles)

// ------------------------------------------------------------------- menu

// Active anims (the others stay available in the code) + display names.
// Speaker photos (11..13) removed along with the Meet entries (review
// 2026-08-29).
static const uint8_t ACTIVE[] = {8, 4, 5, 6, 7, 9, 10, 14, 15, 16};
// (16 = My Photo: hidden entry, slot skipped while g_hasPhoto is false)
static const int NACTIVE = (int)sizeof(ACTIVE);
// (the menu tables live in menu_ui.h, shared with the emulator)

// OTA mode request from the menu: survives a software restart (RTC RAM)
#define OTA_MAGIC 0x07A07A17
RTC_NOINIT_ATTR uint32_t otaRequest;

// UI state: animations / menu / games
enum UiMode : uint8_t { UI_ANIM, UI_MENU, UI_HOME, UI_SCHED, UI_ROT, UI_DRAW, UI_SNAKE, UI_PONG, UI_RUN, UI_TETRIS, UI_PET, UI_PIN, UI_SET, UI_SETUP, UI_QR, UI_MET, UI_SETMENU, UI_PROX, UI_LB, UI_VCAL, UI_BLOG };
static UiMode uiMode = UI_ANIM;

// ---- Settings state (access code + avatar choice, see menu_ui.h) ----
static uint8_t pinDigits[5]; // = UI_PIN_LEN (menu_ui.h, included below)
static int pinPos = 0;
static bool pinError = false, pinRedraw = true;
static int setSel = 0, setShown = -1;   // avatar being picked / displayed
static uint16_t *setSpr = nullptr;      // preview sprite (dvdGenSprite)
static int metScroll = 0, metShown = -1; // Encounters screen (Meet)
static int lbGame = 0, lbShown = -1;     // Leaderboard screen (Meet)
static uint16_t lbMine[4];               // my records (LB_GAMES, declared
                                         // further down in menu_ui.h)
static int setMenuSel = 0, setMenuShown = -1; // Settings menu
static int proxLevel = 2;                     // proximity setting (Normal)
static int menuSel = 0;
static int menuCat = 0; // category of the displayed list (UIC_*)
static int schedIdx = 0; // event shown in the Schedule
static bool autoCycle = false;
static Preferences prefs; // game records, persisted in flash (NVS)

// Battery gauge: smoothed divider reading (x2), approximated LiPo curve.
static int batPct = -1; // -1: no divider bridge present
static int16_t vbatCal = 1000; // bridge calibration, per-mille (NVS "vcal")
static uint32_t batMvRaw = 0; // smoothed voltage (mV), for the More menu info
static bool batCharging = false;

// ---- Runtime logger (Settings > Batt log): battery level samples taken
// while the badge runs, to MEASURE the real on-device discharge
// (review 2026-09-01 (Romain)). When the buffer is full, decimate by 2
// and double the interval (the covered window doubles).
#define BLOG_MAX 240
static uint8_t blogPct[BLOG_MAX];
static uint16_t blogMv[BLOG_MAX];
static int blogN = 0;
static uint32_t blogIvlMs = 120000; // 2 min to start -> 8 h window
static uint32_t blogT0 = 0;         // millis() of the first sample

static void blogPush(uint8_t pct, uint16_t mv, uint32_t now)
{
  if (blogN == 0)
    blogT0 = now;
  if (blogN >= BLOG_MAX) // full: decimate by 2, double the interval
  {
    for (int i = 0; i < BLOG_MAX / 2; i++)
    {
      blogPct[i] = blogPct[i * 2];
      blogMv[i] = blogMv[i * 2];
    }
    blogN = BLOG_MAX / 2;
    blogIvlMs *= 2;
  }
  blogPct[blogN] = pct;
  blogMv[blogN] = mv;
  blogN++;
}

static void updateBattery(uint32_t now)
{
  static uint32_t lastRead = 0;
  static float ema = 0;
  if (now - lastRead < 500)
    return;
  lastRead = now;
  // ANALOG read of the charging detection (0.7 V threshold; while charging
  // the divider gives ~1.2-2.5 V, unplugged ~0 V). FLUSH first: the ADC
  // sampler is shared between channels and keeps the charge of the battery
  // pin (~1.7 V) -- with a 100k divider, a single reading would inherit that
  // residue and light a phantom bolt (seen in test on 2026-08-06).
  analogReadMilliVolts(PIN_VBUS);
  analogReadMilliVolts(PIN_VBUS);
  uint32_t vbus = 0;
  for (int i = 0; i < 4; i++)
    vbus += analogReadMilliVolts(PIN_VBUS);
  batCharging = (vbus / 4) > 700;
  // Burst of 12 readings, min and max discarded: the ESP32 ADC is noisy and
  // the 100k/100k divider is high impedance (~50k) -- a single reading
  // wanders by tens of mV, i.e. several % on the LiPo curve.
  uint32_t sum = 0, lo = UINT32_MAX, hi = 0;
  for (int i = 0; i < 12; i++)
  {
    uint32_t s = analogReadMilliVolts(PIN_VBAT);
    sum += s;
    if (s < lo)
      lo = s;
    if (s > hi)
      hi = s;
  }
  uint32_t mv = (sum - lo - hi) / 10 * 2; // 100k/100k divider
  // TAPERING charge compensation: ~200 mV at 1 A (CC phase, up to ~3.9 V),
  // then the current drops in CV phase -> the real overvoltage melts away
  // too. A fixed offset would over-correct the end of charge (the % looked
  // stuck below 100 %).
  if (batCharging && mv > 200)
  {
    uint32_t off = mv < 3900 ? 200 : (mv >= 4150 ? 40 : 200 - (mv - 3900) * 160 / 250);
    mv -= off;
  }
  if (mv < 2500)
  {
    batPct = -1; // no sensor wired (or battery out of range)
    ema = 0;
    batMvRaw = 0;
    return;
  }
  // SLOW smoothing (~10 s time constant at 2 readings/s): the voltage dips
  // under load (anims, WiFi) must not make the gauge plunge.
  // The EMA runs on the RAW voltage; the per-badge calibration (100k divider
  // tolerance, Settings > Batt screen) is applied afterwards -- that way the
  // calibration screen reacts instantly to the setting.
  ema = (ema == 0) ? mv : ema * 0.95f + mv * 0.05f;
  batMvRaw = (uint32_t)(ema * vbatCal / 1000.0f);
  static const struct { uint16_t mv; uint8_t pct; } C[] = {
      {3300, 0}, {3500, 10}, {3600, 20}, {3700, 40}, {3800, 60},
      {3900, 75}, {4000, 88}, {4100, 96}, {4200, 100}};
  float v = ema * vbatCal / 1000.0f; // curve on the CALIBRATED voltage
  int pct = 100;
  if (v <= C[0].mv)
    pct = 0;
  else
    for (int i = 1; i < 9; i++)
      if (v <= C[i].mv)
      {
        pct = C[i - 1].pct + (int)((v - C[i - 1].mv) * (C[i].pct - C[i - 1].pct) / (C[i].mv - C[i - 1].mv));
        break;
      }
  // End of charge: compensated voltage high (>=4.06 V) SUSTAINED 15 min
  // while charging -> full. The instantaneous threshold jumped to 100 % as
  // soon as a 90 % battery was plugged in (the voltage leaps to 4.2 V in CV
  // phase long before the real end -- without a current measurement, only
  // duration discriminates).
  static uint32_t fullSince = 0;
  if (batCharging && batMvRaw >= 4060)
  {
    if (!fullSince)
      fullSince = now;
    else if (now - fullSince > 15UL * 60 * 1000)
      pct = 100;
  }
  else
    fullSince = 0;

  // display hysteresis: the % only moves by 1 point per reading (2/s) --
  // no more 40 -> 20 -> 38 jumps, the gauge slides gently to the measure.
  // WHILE CHARGING, the rise is further limited to ~1 %/min: that is the max
  // physical rate (2000 mAh at 1 A) -- the charge voltage always
  // overestimates the level (60 -> 74 % jump seen when plugging in), time
  // does not lie.
  static uint32_t lastChargeUp = 0;
  if (batPct < 0)
    batPct = pct;
  else if (pct > batPct)
  {
    if (!batCharging || now - lastChargeUp >= 60000)
    {
      batPct++;
      lastChargeUp = now;
    }
  }
  else if (pct < batPct)
    batPct--;

  // runtime logger (Settings > Batt log): one sample every blogIvlMs while
  // the gauge is valid and we are discharging (points taken while charging
  // would skew the slope)
  static uint32_t blogLast = 0;
  if (batPct >= 0 && !batCharging &&
      (blogLast == 0 || now - blogLast >= blogIvlMs))
  {
    blogLast = now;
    blogPush((uint8_t)batPct, (uint16_t)batMvRaw, now);
  }
}

// ---- Speaker photo uploaded via Setup (Watch > My Photo) ----
// Raw RGB565 360x360 in /photo.565 (LittleFS, spiffs partition 3.4 MB),
// cropped/downscaled ON THE PHONE SIDE, loaded here into PSRAM at boot.
static uint16_t *g_myPhoto = nullptr;
static bool g_hasPhoto = false; // drives the menu entry (menu_ui.h)

static void myPhotoLoad()
{
  if (!LittleFS.begin(true))
  {
    Serial0.println("photo: LittleFS unavailable");
    return;
  }
  File f = LittleFS.open("/photo.565", "r");
  if (!f)
    return;
  if (f.size() != (size_t)W * H * 2)
  {
    f.close();
    return;
  }
  if (!g_myPhoto)
    g_myPhoto = (uint16_t *)ps_malloc((size_t)W * H * 2);
  if (g_myPhoto && f.read((uint8_t *)g_myPhoto, (size_t)W * H * 2) == (size_t)W * H * 2)
  {
    g_hasPhoto = true;
    Serial0.println("photo: loaded (Watch > My Photo)");
  }
  f.close();
}

static void animMyPhoto(float)
{
  if (g_hasPhoto && g_myPhoto)
    memcpy(canvas->getFramebuffer(), g_myPhoto, (size_t)W * H * 2);
  else
    canvas->fillScreen(RGB565_BLACK);
}

#include "menu_ui.h" // bubble menu + lists (shared firmware/emulator)

// Flush with optional software rotation (compensates crooked panels).
// The rotation (~4 ms) only applies if an angle is set.
static uint16_t *rotBuf = nullptr;
static void badgeFlush()
{
  const uint16_t *src = canvas->getFramebuffer();
  if (uiScreenRot != 0)
  {
    if (!rotBuf)
      rotBuf = (uint16_t *)ps_malloc((size_t)W * H * 2);
    if (rotBuf)
    {
      uiRotateBlit(canvas->getFramebuffer(), rotBuf, uiScreenRot);
      src = rotBuf;
    }
  }
  if (dmafOk)
  {
    // async: the end of the transfer runs on DMA during the next render
    dmafFlush(0, 0, W, H, src, W, false);
    return;
  }
  // fallback: original blocking Arduino_GFX path
  if (src == rotBuf)
    panel->draw16bitRGBBitmap(0, 0, rotBuf, W, H);
  else
    canvas->flush();
}

// ---------------------------------------------------------------- games
#include "games.h"
#include "qr_screen.h"  // Meet > QR Code screen (shared with the emulator) --
                        // provides badgeSsid(), used by draw/setup/OTA
#include "setup_mode.h" // phone config flow (More > Setup) -- provides the
                        // captive DNS badgeDns*, used by Draw
#include "draw_mode.h"
#include "social.h"     // badge-to-badge encounters (ESP-NOW, Conf Buddy)

// Draw mode waiting screen: connection info while nobody has joined
// (cleared by draw_mode.h on the first WebSocket connection)
static void drawDrawWait()
{
  canvas->fillScreen(RGB565_BLACK);
  canvas->setTextColor(rgb565(0xfc, 0xa3, 0xf7));
  canvas->setTextSize(3);
  canvas->setCursor(CX - 81, 24);
  canvas->print("DRAW MODE");
  char wifiQr[80];
  snprintf(wifiQr, sizeof(wifiQr), "WIFI:T:WPA;S:%s;P:%s;;", badgeSsid(),
           OTA_PASS);
  qrMiniDraw(CX, 148, 140, wifiQr);
  canvas->setTextSize(2);
  canvas->setTextColor(RGB565_WHITE);
  char line[40];
  snprintf(line, sizeof(line), "WiFi %s", badgeSsid());
  canvas->setCursor(CX - (int)strlen(line) * 6, 248);
  canvas->print(line);
  snprintf(line, sizeof(line), "Pass %s", OTA_PASS);
  canvas->setCursor(CX - (int)strlen(line) * 6, 270);
  canvas->print(line);
  canvas->setTextSize(1);
  canvas->setTextColor(rgb565(255, 213, 48));
  canvas->setCursor(CX - 87, 296);
  canvas->print("scan or join: page opens");
  canvas->setCursor(CX - 87, 310);
  canvas->print("or http://192.168.4.1");
  canvas->setTextColor(rgb565(130, 130, 130));
  canvas->setCursor(CX - 42, 326);
  canvas->print("center: exit");
}

// "Software" power off with the repo's "power down" animation (old CRT TV
// style): the character squashes vertically into a line with a white flash,
// then a central hot spot fades away. Then: display asleep (display off +
// sleep in), backlight cut and latched, ESP32 deep sleep, wake on BTN_AUTO
// press (RTC GPIO). Residual draw: TP4056 boost + devkit regulator (~a few
// mA) -- fine for a badge that is recharged often.
static void powerOff()
{
  Serial0.println("power off (deep sleep) -- wake with the AUTO button");

  // Source frame: whatever is ON SCREEN at power-off time (last rendered
  // frame -- running animation or menu), like a real TV being switched off.
  uint16_t *fb = canvas->getFramebuffer();
  uint16_t *snap = (uint16_t *)malloc(W * H * sizeof(uint16_t));
  if (snap)
  {
    memcpy(snap, fb, W * H * sizeof(uint16_t));
    // Phase 1: accelerated vertical contraction down to a 6 px line,
    // white flash at the end.
    uint32_t t0 = millis();
    while (true)
    {
      float p = (millis() - t0) / 550.0f;
      if (p >= 1)
        break;
      float e = p * p;
      int newH = (int)(H * (1 - e) + 6 * e);
      int off = (H - newH) / 2;
      memset(fb, 0, W * H * 2);
      for (int dy = 0; dy < newH; dy++)
        memcpy(&fb[(off + dy) * W], &snap[(int)((float)dy * H / newH) * W], W * 2);
      if (p > 0.85f)
      {
        uint8_t a = (uint8_t)((p - 0.85f) / 0.15f * 153);
        for (int i = (off - 2) * W; i < (off + newH + 2) * W; i++)
        {
          uint16_t c = fb[i];
          uint16_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
          r += ((31 - r) * a) >> 8;
          g += ((63 - g) * a) >> 8;
          b += ((31 - b) * a) >> 8;
          fb[i] = (r << 11) | (g << 5) | b;
        }
      }
      badgeFlush();
    }
    free(snap);
    // Phase 2: central hot spot fading away (halo + core)
    t0 = millis();
    while (true)
    {
      float el = (millis() - t0) / 1000.0f;
      if (el > 0.6f)
        break;
      float fade = 1 - el / 0.6f;
      canvas->fillScreen(RGB565_BLACK);
      canvas->fillCircle(CX, CY, (int)(40 * fade),
                         rgb565((int)(90 * fade), (int)(85 * fade), (int)(78 * fade)));
      canvas->fillCircle(CX, CY, (int)(8 * fade),
                         rgb565((int)(230 * fade), (int)(216 * fade), (int)(198 * fade)));
      badgeFlush();
    }
  }
  canvas->fillScreen(RGB565_BLACK);
  badgeFlush();

  if (dmafOk) // display traffic goes through SPI3 since dmafInit()
  {
    dmafCmdBlocking(0x28); // display off
    delay(20);
    dmafCmdBlocking(0x10); // sleep in
  }
  else
  {
    bus->sendCommand(0x28);
    delay(20);
    bus->sendCommand(0x10);
  }
  delay(120);
  // Cut the backlight and LATCH the low level during deep sleep (without
  // hold, the pin would float and the backlight could turn back on).
  digitalWrite(TFT_BL, LOW);
  gpio_hold_en((gpio_num_t)TFT_BL);
  digitalWrite(TFT_BL_LEGACY, LOW);
  gpio_hold_en((gpio_num_t)TFT_BL_LEGACY);
  neopixelWrite(PIN_RGB, 0, 0, 0); // devkit WS2812: explicit black
  pinMode(PIN_RGB, OUTPUT);
  digitalWrite(PIN_RGB, LOW);
  gpio_hold_en((gpio_num_t)PIN_RGB); // and data latched low during sleep
  gpio_deep_sleep_hold_en();
  // Wait for the button RELEASE: the long press is still in progress at this
  // point, and the ext0 wakeup triggers on a low level -- without this wait
  // the badge goes to sleep and wakes up immediately.
  while (digitalRead(BTN_AUTO) == LOW)
    delay(10);
  delay(100); // release debounce
  rtc_gpio_pullup_en((gpio_num_t)BTN_AUTO);
  rtc_gpio_pulldown_dis((gpio_num_t)BTN_AUTO);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_AUTO, 0);
  // (Wake on USB plug-in was tried then dropped -- review 2026-09-07
  // (Romain), ext1 level limitation and no time to make it reliable before
  // the production run: wake by center button only, the TP4056 LED acts as
  // the charge indicator while the badge is off.)
  esp_deep_sleep_start();
}

// Retro loader of the boot splash: "LOADING..." + segmented block bar, drawn
// ON TOP of the Three Conf anim (at startup only), then scanlined over to
// blend into the CRT look. p = progress 0..1.
static void drawBootLoader(float p, float t)
{
  const int NB = 12, bw = 14, bh = 14, gap = 4;
  const int totW = NB * bw + (NB - 1) * gap;
  const int x0 = CX - totW / 2, y = 276;
  const uint16_t pink = rgb565(0xfc, 0xa3, 0xf7);
  const uint16_t dimFrame = rgb565(70, 110, 80);

  // Rotating loading phrases -- Three.js dev humour
  static const char *PHRASES[] = {
      "npm install three",     "compiling shaders",   "baking the donut",
      "spinning the cube",     "computing normals",   "draw calls--",
      "camera.lookAt(you)",    "scene.add(badge)",    "60 fps, promise",
      "reticulating splines",  "webgl: ok",           "dispose()ing bugs"};
  const int NPHRASES = sizeof(PHRASES) / sizeof(PHRASES[0]);
  const char *txt = PHRASES[((int)(t / 1.0f)) % NPHRASES];
  canvas->setTextSize(2);
  canvas->setTextColor(rgb565(0xfb, 0xd9, 0x75));
  canvas->setCursor(CX - (int)strlen(txt) * 6, y - 26);
  canvas->print(txt);

  // segmented blocks
  int filled = (int)(p * NB + 0.5f);
  for (int i = 0; i < NB; i++)
  {
    int x = x0 + i * (bw + gap);
    if (i < filled)
      canvas->fillRect(x, y, bw, bh, pink);
    else
      canvas->drawRect(x, y, bw, bh, dimFrame);
  }

  // local scanlines to blend the loader into the CRT mood
  for (int yy = y - 28; yy < y + bh + 2; yy++)
    if (yy % 3 == 0)
      dimRow(yy, 40, 320);
}

// Texture generation during the boot anim. Returns false once everything is
// generated. Runs on CORE 0 (bootGenTask) while the anim runs on core 1:
// none of these functions touch the shared canvas, and doing one step per
// frame blocked the current frame (freeze visible at the start of the splash
// on the real badge).
static bool bootGenStep(int s)
{
  switch (s)
  {
  case 0: initPlasma(); return true;
  case 1: initTorus(); return true;
  case 2: initBallSprite(); return true;
  case 3: initMouthMask(); initLaughMask(); return true;
  case 4: initTgLogo(); return true;
  case 5: irInit(); return true;
  default:
    if (s - 6 < IR_FRAMES) { irGenFrame(s - 6); return true; }
    if (s - 6 == IR_FRAMES) { dvdInitSprites(); return true; }
    if (s - 6 == IR_FRAMES + 1)
    {
      initPoints();
      for (int i = 0; i < NSTARS; i++)
        resetStar(stars[i]);
      return true;
    }
    return false;
  }
}

static volatile bool bootGenDone = false;
static void bootGenTask(void *)
{
  uint32_t t0 = millis();
  int s = 0;
  while (bootGenStep(s))
    s++;
  Serial0.printf("texture generation: %lu ms (during the splash)\n",
                 (unsigned long)(millis() - t0));
  bootGenDone = true;
  vTaskDelete(nullptr);
}

// ------------------------------------------------------------------ loop

void setup()
{
  Serial0.begin(115200); // UART0 -> CH343 bridge: logs on /dev/cu.usbmodem*
  Serial0.println("=== Badge threejs.paris - animations GC9B72 ===");
  prefs.begin("badge", false); // game records (NVS)
  // FLEET WIPE (review 2026-09-08 (Romain)): WiFi OTA cannot erase the
  // flash, so the firmware does it -- on the FIRST boot of a new
  // "generation", the NVS (identity, scores, calibration...) and the photo
  // are erased. Increment RESET_GEN to trigger a new wipe of the whole
  // fleet on the next flash.
#define RESET_GEN 2 // gen 2: fleet wipe of 2026-09-09
  if (prefs.getUShort("fwgen", 0) != RESET_GEN)
  {
    prefs.clear();
    if (LittleFS.begin(true))
      LittleFS.remove("/photo.565");
    prefs.putUShort("fwgen", RESET_GEN);
    Serial0.println("fleet: memory wiped (new generation)");
  }
  uiScreenRot = (int)(int8_t)prefs.getChar("rotDeg", 0); // calibrated rotation
  vbatCal = prefs.getShort("vcal", 1000); // per-badge battery gauge calibration

  // CHARGE GUARD (review 2026-08-30 (Romain)): critical cell + charger
  // plugged in -> every full boot (anim, PSRAM, backlight) collapsed into a
  // brownout and looped, the display blinked and the charge current went
  // into the retries. Here we wait, CPU throttled and display off (the
  // TP4056 LED acts as the indicator), for the cell to come back up before
  // starting for real. Unplugged -> we attempt the normal boot.
  {
    analogReadMilliVolts(PIN_VBUS); // flush the shared sampler
    bool onUsb = analogReadMilliVolts(PIN_VBUS) > 700;
    uint32_t mv = 0;
    for (int i = 0; i < 4; i++)
      mv += analogReadMilliVolts(PIN_VBAT);
    mv = mv / 4 * 2 * (uint32_t)vbatCal / 1000;
    if (onUsb && mv > 2500 && mv < 3400) // 2500 = no divider (bare proto)
    {
      Serial0.printf("critical battery, charging (%lu mV): waiting before boot\n",
                     (unsigned long)mv);
      while (true)
      {
        delay(2000);
        uint32_t s2 = 0;
        for (int i = 0; i < 4; i++)
          s2 += analogReadMilliVolts(PIN_VBAT);
        s2 = s2 / 4 * 2 * (uint32_t)vbatCal / 1000;
        if (s2 >= 3550) // ~3.4 V real under charge: safe boot
          break;
        analogReadMilliVolts(PIN_VBUS);
        if (analogReadMilliVolts(PIN_VBUS) < 700)
          break; // unplugged by the user: give it a try
      }
      Serial0.println("charge ok: boot");
    }
  }
  g_avatarIdx = prefs.getUChar("avatar", 0) % AVATAR_N;  // badge avatar/person
  g_avatarFaceIdx = g_avatarIdx;
  // custom buddy + name + QR URL (More > Setup flow, on the phone)
  g_buddyCustom = prefs.getUChar("bcust", 0) != 0;
  g_buddyCustomDef.hue = prefs.getShort("bhue", 0);
  g_buddyCustomDef.sat = prefs.getUChar("bsat", 100) / 100.0f;
  g_buddyCustomDef.face = prefs.getUChar("bface", 0) % 9;
  prefs.getString("bname", qrName, sizeof(qrName));
  // RANDOM per-badge buddy while no profile is configured (review 2026-09-08
  // (Romain)): no name (Setup/Settings write "bname") and no custom buddy ->
  // hue + face drawn once and PERSISTED ("rhue"/"rface", stable across
  // boots), applied as a NOT-saved custom ("bcust" stays 0) -- the real
  // profile, when it arrives, takes over as is.
  if (!qrName[0] && !g_buddyCustom)
  {
    uint16_t rh = prefs.getUShort("rhue", 0xFFFF);
    uint8_t rf;
    if (rh == 0xFFFF)
    {
      // early at boot, esp_random() lacks entropy (radio off): we mix in the
      // eFuse MAC, unique per chip -- two badges cannot draw the same buddy
      uint8_t mac[6] = {0};
      esp_efuse_mac_get_default(mac);
      uint32_t mix = esp_random() ^ ((uint32_t)mac[5] << 16) ^
                     ((uint32_t)mac[4] << 8) ^ mac[3] ^ ((uint32_t)mac[2] << 24);
      rh = (uint16_t)(mix % 360);
      rf = (uint8_t)((mix >> 9) % 9);
      prefs.putUShort("rhue", rh);
      prefs.putUChar("rface", rf);
      Serial0.printf("random buddy: hue %u, face %u\n", rh, rf);
    }
    else
      rf = prefs.getUChar("rface", 0) % 9;
    g_buddyCustom = true;
    g_buddyCustomDef.hue = (int16_t)rh;
    g_buddyCustomDef.sat = 1.0f;
    g_buddyCustomDef.face = rf;
  }
  socialMetLoad(); // encounter counters (Meet > Encounters screen)
  lbLoad();        // scores learned from other badges (Meet > Leaderboard)
  myPhotoLoad();   // photo uploaded via Setup (Watch > My Photo)
  socialRssiNear = (int8_t)prefs.getChar("prox", -62); // proximity thr (Normal)
  prefs.getString("bcomp", qrCompany, sizeof(qrCompany));
  prefs.getString("bmsg", qrMsg, sizeof(qrMsg));
  if (prefs.getString("qrurl", qrUrl, sizeof(qrUrl)) == 0 || !qrUrl[0])
    snprintf(qrUrl, sizeof(qrUrl), "https://threejs.paris");

  // PREV button -- or BOOT, handy while the buttons are not wired -- pressed
  // during the boot anim -> OTA flash mode (the window is watched inside the
  // anim loop, no more dead time before the display turns on).
  // NB: BOOT held DURING the reset = ROM bootloader (GPIO 0 strapping); so
  // it must be pressed just AFTER the reset.
  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_BOOT, INPUT_PULLUP);
  // ...or requested from the menu "Mode Flash OTA" entry (RTC RAM flag)
  if (otaRequest == OTA_MAGIC)
    otaMode = true;
  otaRequest = 0;

  // Backlight: release any hold left by the previous deep sleep, then turn on
  // (GPIO 9 = ribbon badges, GPIO 4 = first badges -- driven in parallel,
  // the unwired pin simply stays floating)
  gpio_hold_dis((gpio_num_t)TFT_BL);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  gpio_hold_dis((gpio_num_t)TFT_BL_LEGACY);
  pinMode(TFT_BL_LEGACY, OUTPUT);
  digitalWrite(TFT_BL_LEGACY, HIGH);
  gpio_hold_dis((gpio_num_t)PIN_RGB);
  neopixelWrite(PIN_RGB, 0, 0, 0); // WS2812 off from boot (floating data
  pinMode(PIN_RGB, OUTPUT);        // = random colour possible)
  digitalWrite(PIN_RGB, LOW);

  pinMode(TFT_TE, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(TFT_TE), teIsr, RISING);

  pinMode(BTN_NEXT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_NEXT), btnNextIsr, FALLING);
  pinMode(BTN_PREV, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_PREV), btnPrevIsr, FALLING);
  pinMode(BTN_AUTO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_AUTO), btnAutoIsr, CHANGE);
  pinMode(PIN_VBUS, INPUT_PULLDOWN); // charge detection (LOW if not wired)
  pinMode(BTN_BOOT, INPUT_PULLUP);
  // (BOOT is no longer attached to an interrupt: it is polled in loop() with
  // 3 functions depending on the press duration -- see the "BOOT button"
  // block)

  if (!canvas->begin(SPI_FREQ))
  {
    Serial0.println("ERROR: canvas->begin() failed (framebuffer or display)");
    while (true)
      delay(1000);
  }
  // switch display traffic to SPI3+DMA (panel init stays Arduino_GFX)
  dmafOk = dmafInit();
  Serial0.printf("free PSRAM: %u bytes\n", (unsigned)ESP.getFreePsram());


  // Startup sequence: "Three Conf" anim (logo + loader) for 4 s -- doubles as
  // a visual check of the SPI link. Texture generation runs IN PARALLEL on
  // core 0 (the anim stays smooth on core 1), and PREV/BOOT pressed during
  // the anim switches to OTA flash mode.
  // NB: task deliberately on CORE 1 (the anim's core) -- pinned to core 0,
  // PSRAM generation concurrent with rendering caused TG1WDT resets on some
  // boards (marginal power rail); on the same core the scheduler interleaves
  // (generation ~5 s, still during the splash)
  xTaskCreatePinnedToCore(bootGenTask, "bootgen", 16384, nullptr, 1, nullptr, 1);
  if (!otaMode)
  {
    uint32_t t0 = millis();
    while (millis() - t0 < 4000 && !otaMode)
    {
      float ts = (millis() - t0) / 1000.0f;
      animThreeConf(ts, -20); // logo raised to make room for the loader
      drawBootLoader(ts / 4.0f, ts);
      waitTE();
      badgeFlush();
      // OTA window: 1.5 s (beyond that, a press during boot is ignored)
      if (ts < 1.5f &&
          (digitalRead(BTN_PREV) == LOW || digitalRead(BTN_BOOT) == LOW))
        otaMode = true;
    }
  }

  if (otaMode)
  {
    // Standalone access point + OTA server; the rest of setup (animations)
    // is skipped, loop() will only do ArduinoOTA.handle().
    WiFi.mode(WIFI_AP);
    WiFi.softAP(badgeSsid(), OTA_PASS);
    ArduinoOTA.onStart([]() { Serial0.println("OTA: start"); });
    ArduinoOTA.onProgress([](unsigned int prog, unsigned int total) {
      static int lastPct = -1;
      int pct = prog / (total / 100);
      if (pct / 5 != lastPct / 5) // refresh the display every 5 %
      {
        lastPct = pct;
        canvas->fillRect(80, 220, 200, 14, rgb565(40, 40, 40));
        canvas->fillRect(80, 220, 2 * pct, 14, rgb565(255, 213, 48));
        badgeFlush();
        Serial0.printf("OTA: %d %%\n", pct);
      }
    });
    ArduinoOTA.onEnd([]() { Serial0.println("OTA: OK, restarting"); });
    ArduinoOTA.onError([](ota_error_t e) { Serial0.printf("OTA: error %u\n", e); });
    ArduinoOTA.begin();

    canvas->fillScreen(RGB565_BLACK);
    canvas->setTextColor(rgb565(255, 213, 48));
    canvas->setTextSize(3);
    canvas->setCursor(90, 90);
    canvas->print("FLASH MODE");
    canvas->setTextSize(2);
    canvas->setTextColor(RGB565_WHITE);
    canvas->setCursor(70, 140);
    canvas->printf("WiFi %s", badgeSsid());
    canvas->setCursor(70, 165);
    canvas->printf("Pass %s", OTA_PASS);
    canvas->setCursor(70, 190);
    canvas->print("pio run -e ota -t upload");
    canvas->setTextColor(rgb565(130, 130, 130));
    canvas->setCursor(CX - 102, 240);
    canvas->print("hold center: exit");
    badgeFlush();
    Serial0.printf("OTA FLASH MODE: AP %s / %s, IP %s\n", badgeSsid(), OTA_PASS,
                   WiFi.softAPIP().toString().c_str());
    return;
  }

  // Wait for the generation to finish (core 0) -- in practice it ends well
  // before the 4 s of the splash
  while (!bootGenDone)
    delay(5);

  Serial0.println("setup done");
}

void loop()
{
  if (otaMode)
  {
    ArduinoOTA.handle();
    // Leaving flash mode WITHOUT flashing (closed case, no physical reset):
    // 2 s long press on the center button -> normal restart.
    static uint32_t exitHold = 0;
    if (digitalRead(BTN_AUTO) == LOW)
    {
      if (!exitHold)
        exitHold = millis();
      else if (millis() - exitHold > 2000)
        esp_restart();
    }
    else
      exitHold = 0;
    delay(5);
    return;
  }

  static uint32_t lastMs = millis();
  static uint32_t fpsCount = 0, fpsMark = millis();
  static int lastAnim = -1;

  uint32_t now = millis();
  float dt = (now - lastMs) / 1000.0f;
  lastMs = now;

  static uint32_t animStartMs = 0;
  static int slot = 0;
  static uint32_t lastBtnMs = 0;
  static uint32_t slotStartMs = 0;

  // Badge-to-badge encounters: the ESP-NOW radio is only active while the
  // Conf Buddy is on screen (it shuts down as soon as we enter the menu, so
  // always BEFORE the Draw/Setup/OTA WiFi APs).
  // Production-run safeguard (crash loop breaker): an NVS marker is armed
  // just before turning the radio on and disarmed after 8 s of operation.
  // If a boot finds the marker armed, the previous session died at radio
  // startup (brownout/POR on a marginal supply) -> social radio disabled for
  // THIS session, the badge stays usable. The marker is cleared: on the next
  // power cycle, we try once more.
  static uint32_t socialArmMs = 0;
  static const bool socialBlocked = [] {
    if (esp_reset_reason() == ESP_RST_BROWNOUT || prefs.getUChar("socboot", 0))
    {
      prefs.putUChar("socboot", 0);
      Serial0.println("social: disabled (crash at previous radio startup "
                      "-- check the power supply)");
      return true;
    }
    return false;
  }();
  {
    // ESP-NOW detection requires an IDENTITY (review 2026-09-08 (Romain)):
    // unconfigured badge (no name via Setup/Settings) = silent radio -- no
    // anonymous "who is nearby". The Proximity screen (organiser tool, probe
    // mode) stays active for diagnostics.
    bool wantSession = (((uiMode == UI_ANIM && ACTIVE[slot] == 8) && qrName[0]) ||
                        uiMode == UI_PROX) &&
                       !socialBlocked;
    socialProbeOnly = (uiMode == UI_PROX);
    // DUTY CYCLING of the listening during Conf Buddy (see SOCIAL_DUTY_* in
    // social.h); Proximity stays continuous. The "socboot" loop breaker is
    // only armed on the FIRST power-up of the power cycle: once the radio
    // has proven itself for 8 s, the duty-cycle restarts do not re-arm it
    // (otherwise two NVS writes per 12 s period).
    static uint32_t dutyAnchor = 0;
    static bool socialProven = false;
    bool wantSocial = wantSession;
    if (wantSession && !socialProbeOnly)
    {
      if (!dutyAnchor)
        dutyAnchor = now ? now : 1;
      wantSocial = ((now - dutyAnchor) % SOCIAL_DUTY_PERIOD) < SOCIAL_DUTY_ON;
    }
    else if (!wantSession)
      dutyAnchor = 0;
    if (wantSocial != socialOn)
    {
      if (wantSocial)
      {
        if (!socialProven)
        {
          prefs.putUChar("socboot", 1); // armed: dying here blocks at boot
          socialArmMs = now ? now : 1;
        }
        socialStart();
      }
      else
        socialStop();
    }
    if (socialOn && socialArmMs && now - socialArmMs > 8000)
    {
      prefs.putUChar("socboot", 0); // 8 s stable: the radio works on this board
      socialArmMs = 0;
      socialProven = true;
    }
    if (socialOn)
      socialLoop(now);
  }

  // ---- BOOT button alone = full navigation (handy on the bench, without
  // wired buttons): short = next * held >= 0.5 s = center button (the games
  // "jump" fires when crossing the threshold, the menu validation on
  // release) * held >= 2 s = power off, like the long center press.
  // No effect on the real buttons, which stay priority.
  {
    static uint32_t bootDownAt = 0;
    static bool bootCenterFired = false, bootOffFired = false;
    bool down = digitalRead(BTN_BOOT) == LOW;
    if (down && !bootDownAt)
    {
      bootDownAt = now;
      bootCenterFired = bootOffFired = false;
    }
    if (down && bootDownAt && !bootCenterFired && now - bootDownAt >= 500)
    {
      bootCenterFired = true;
      autoPressMs = now; // synthetic "center press" (games jump/action)
    }
    if (down && bootDownAt && !bootOffFired && now - bootDownAt >= 2000 &&
        now > 4000)
    {
      bootOffFired = true;
      powerOff(); // long center = power off
    }
    if (!down && bootDownAt)
    {
      uint32_t held = now - bootDownAt;
      bootDownAt = 0;
      if (held < 500)
        btnNextFlag = true; // short: next (historical behaviour)
      else if (held < 2000)
        btnAutoShort = true; // long: short center (menu / validate)
    }
  }

  // ---- buttons: left/right (300 ms debounce) + short/long center ----
  bool navNext = false, navPrev = false;
  if ((btnNextFlag || btnPrevFlag) && now - lastBtnMs > 300)
  {
    lastBtnMs = now;
    navNext = btnNextFlag;
    navPrev = !navNext && btnPrevFlag;
  }
  btnNextFlag = btnPrevFlag = false;

  // LONG press (2 s) on the center: power off (ignored for 4 s after boot)
  if (now > 4000 && autoPressMs && now - autoPressMs > 2000 && digitalRead(BTN_AUTO) == LOW)
    powerOff();
  bool autoShort = btnAutoShort;
  btnAutoShort = false;

  updateBattery(now);

  // Conference clock: splits the RTC time (synced by the Draw webapp) into
  // conference day + minutes. Time never synced -> NOW hidden.
  {
    static uint32_t clockLast = 0;
    if (now - clockLast >= 10000 || clockLast == 0)
    {
      clockLast = now;
      time_t t = time(nullptr);
      if (t < 1750000000) // before mid-2025: RTC never set
      {
        uiNowMin = -1;
        uiNowDay = 0;
      }
      else
      {
        struct tm tmv;
        gmtime_r(&t, &tmv); // the stored time is already LOCAL time
        uiNowDay = (tmv.tm_year == 126 && tmv.tm_mon == 8)
                       ? (tmv.tm_mday == 10 ? 1 : (tmv.tm_mday == 11 ? 2 : 0))
                       : 0;
        uiNowMin = tmv.tm_hour * 60 + tmv.tm_min;
      }
    }
  }

  // Battery protection: below 3.20 V sustained for 10 s (not charging),
  // clean power off -- better than waiting for the abrupt PCM cutoff near
  // 2.5 V, which wears the LiPo. (Checked 2026-08-07: gauge accurate to
  // within 10 mV.)
  // Threshold RAISED from 3.02 to 3.20 V (review 2026-08-15, after a proto
  // cell died from deep discharge): we sacrifice ~2 min of runtime to leave
  // ~8-10 % of real reserve -- the boost standby (~0.3 mA, never cut) eats
  // that reserve AFTER power off, and a doubled reserve gives weeks of
  // margin before the dangerous zone (<2.5 V).
  static uint32_t lowSince = 0;
  if (batPct == 0 && batMvRaw > 0 && batMvRaw < 3200 && !batCharging)
  {
    if (!lowSince)
      lowSince = now;
    else if (now - lowSince > 10000)
    {
      Serial0.println("critical battery (<3.20 V): protective power off");
      powerOff();
    }
  }
  else
    lowSince = 0;

  if (uiMode == UI_HOME)
  {
    // main bubble menu: up/down = category, center = enter
    if (navNext)
      uiHomeNav(1);
    if (navPrev)
      uiHomeNav(-1);
    if (autoShort)
    {
      menuCat = uiHomeFocus;
      menuSel = 0;
      uiMode = UI_MENU;
      Serial0.printf("menu: category %s\n", UI_CAT_NAMES[menuCat]);
    }
    bool moving = (uiMode == UI_HOME)
                      ? uiDrawHome(dt, batPct, batCharging)
                      : (uiMode == UI_SCHED
                             ? uiDrawSchedule(schedIdx)
                             : uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging));
    // no TE wait on the menus (tearing barely visible) and flush only if
    // something changed: max responsiveness, SPI bus idle otherwise
    static int homeLastPct = -999;
    static bool homeLastChg = false, homeFirst = true;
    if (moving || navNext || navPrev || autoShort || homeFirst ||
        batPct != homeLastPct || batCharging != homeLastChg)
    {
      badgeFlush();
      fpsCount++;
    }
    homeFirst = (uiMode != UI_HOME);
    homeLastPct = batPct;
    homeLastChg = batCharging;
    return;
  }

  if (uiMode == UI_ROT)
  {
    // left/right: -1/+1 degree; center: save to NVS and back to the menu
    if (navPrev && uiScreenRot > -15)
      uiScreenRot--;
    if (navNext && uiScreenRot < 15)
      uiScreenRot++;
    static int rotShown = -99;
    if (autoShort)
    {
      prefs.putChar("rotDeg", (int8_t)uiScreenRot);
      Serial0.printf("screen rotation saved: %+d deg\n", uiScreenRot);
      rotShown = -99;
      setMenuShown = -1; // Rotate lives in Settings: back to the submenu
      uiMode = UI_SETMENU;
      uiDrawSetMenu(setMenuSel);
      waitTE();
      badgeFlush();
      fpsCount++;
      return;
    }
    if (uiScreenRot != rotShown)
    {
      rotShown = uiScreenRot;
      uiDrawRotate(uiScreenRot);
      badgeFlush();
      fpsCount++;
    }
    return;
  }

  if (uiMode == UI_PIN)
  {
    // Settings access code: left/right = digit -/+, center = validate the
    // digit; after the 5th, correct code -> avatar choice, wrong code ->
    // error flash then back to the menu
    if (navPrev)
    {
      pinDigits[pinPos] = (pinDigits[pinPos] + 9) % 10;
      pinRedraw = true;
    }
    if (navNext)
    {
      pinDigits[pinPos] = (pinDigits[pinPos] + 1) % 10;
      pinRedraw = true;
    }
    if (autoShort)
    {
      if (pinPos < UI_PIN_LEN - 1)
      {
        pinPos++;
        pinRedraw = true;
      }
      else
      {
        bool ok = true;
        for (int i = 0; i < UI_PIN_LEN; i++)
          if (pinDigits[i] != UI_PIN_CODE[i])
            ok = false;
        if (ok)
        {
          setSel = g_avatarIdx;
          setShown = -1;
          setMenuSel = 0;
          setMenuShown = -1;
          uiMode = UI_SETMENU;
        }
        else
        {
          uiDrawPin(pinDigits, pinPos, true); // "wrong code"
          badgeFlush();
          delay(900);
          uiMode = UI_MENU;
          uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
          badgeFlush();
          fpsCount++;
        }
        return;
      }
    }
    if (uiMode == UI_PIN && pinRedraw)
    {
      pinRedraw = false;
      uiDrawPin(pinDigits, pinPos, false);
      badgeFlush();
      fpsCount++;
    }
    return;
  }

  if (uiMode == UI_SETMENU)
  {
    if (navNext)
      setMenuSel = (setMenuSel + 1) % SETMENU_N;
    if (navPrev)
      setMenuSel = (setMenuSel + SETMENU_N - 1) % SETMENU_N;
    if (autoShort)
    {
      if (setMenuSel == 0) // Avatar
      {
        setShown = -1;
        uiMode = UI_SET;
      }
      else if (setMenuSel == 1) // Proximity
      {
        proxLevel = 2;
        for (int i = 0; i < 4; i++)
          if (UI_PROX_LEVELS[i] == socialRssiNear)
            proxLevel = i;
        uiMode = UI_PROX; // the radio switches to probe mode (see loop)
      }
      else if (setMenuSel == 2) // Rotate screen
      {
        uiMode = UI_ROT;
      }
      else if (setMenuSel == 3) // OTA flash mode
      {
        Serial0.println("settings: restarting into OTA flash mode");
        otaRequest = OTA_MAGIC;
        delay(50);
        esp_restart();
      }
      else if (setMenuSel == 4) // Batt: gauge calibration screen
      {
        uiMode = UI_VCAL;
      }
      else if (setMenuSel == 5) // Batt log: recorded discharge curve
      {
        uiMode = UI_BLOG;
      }
      else
      {
        uiMode = UI_MENU;
        uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
        waitTE();
        badgeFlush();
        fpsCount++;
        return;
      }
      setMenuShown = -1;
      return;
    }
    if (setMenuShown != setMenuSel)
    {
      setMenuShown = setMenuSel;
      uiDrawSetMenu(setMenuSel);
      waitTE();
      badgeFlush();
      fpsCount++;
    }
    return;
  }

  if (uiMode == UI_PROX)
  {
    // proximity setting: live gauge of the nearest badge (radio in probe
    // mode), left/right = level, center = save. WRAPS at the ends:
    // essential with the BOOT button alone (no "prev")
    if (navNext)
      proxLevel = (proxLevel + 1) % 4;
    if (navPrev)
      proxLevel = (proxLevel + 3) % 4;
    if (autoShort)
    {
      socialRssiNear = UI_PROX_LEVELS[proxLevel];
      prefs.putChar("prox", socialRssiNear);
      Serial0.printf("proximity saved: %s (%d dBm)\n",
                     UI_PROX_NAMES[proxLevel], (int)socialRssiNear);
      setMenuShown = -1;
      uiMode = UI_SETMENU;
      uiDrawSetMenu(setMenuSel);
      waitTE();
      badgeFlush();
      fpsCount++;
      return;
    }
    uiDrawProx(proxLevel, socialOn ? socialNearestRssi() : -100.0f);
    waitTE();
    badgeFlush();
    fpsCount++;
    return;
  }

  if (uiMode == UI_BLOG)
  {
    // runtime curve: left = reset the log, center = back; redrawn ~1x/s
    // (the curve changes slowly)
    static uint32_t blogDrawMs = 0;
    if (navPrev)
    {
      blogN = 0;
      blogIvlMs = 120000;
      blogDrawMs = 0;
    }
    if (autoShort)
    {
      setMenuShown = -1;
      uiMode = UI_SETMENU;
      uiDrawSetMenu(setMenuSel);
      waitTE();
      badgeFlush();
      fpsCount++;
      return;
    }
    if (now - blogDrawMs > 1000)
    {
      blogDrawMs = now;
      uiDrawBlog(now, batPct, batMvRaw);
      waitTE();
      badgeFlush();
      fpsCount++;
    }
    return;
  }

  if (uiMode == UI_VCAL)
  {
    // gauge calibration: left/right = -/+0.3 % on the divider factor, the
    // display follows live; center = save to NVS and back
    if (navNext && vbatCal < 1100)
      vbatCal += 3;
    if (navPrev && vbatCal > 900)
      vbatCal -= 3;
    if (autoShort)
    {
      prefs.putShort("vcal", vbatCal);
      Serial0.printf("gauge calibration saved: %d/1000\n", (int)vbatCal);
      setMenuShown = -1;
      uiMode = UI_SETMENU;
      uiDrawSetMenu(setMenuSel);
      waitTE();
      badgeFlush();
      fpsCount++;
      return;
    }
    uiDrawVcal(batMvRaw, vbatCal);
    waitTE();
    badgeFlush();
    fpsCount++;
    return;
  }

  if (uiMode == UI_SET)
  {
    // badge avatar/person choice: left/right = previous/next (sphere + face
    // preview), center = save to NVS and exit
    if (navPrev)
      setSel = (setSel + AVATAR_N - 1) % AVATAR_N;
    if (navNext)
      setSel = (setSel + 1) % AVATAR_N;
    if (autoShort)
    {
      prefs.putUChar("avatar", (uint8_t)setSel);
      g_avatarIdx = (uint8_t)setSel;
      g_avatarFaceIdx = g_avatarIdx;
      g_faceForce = -1;
      if (g_buddyCustom) // picking a table avatar disables the custom one
      {
        g_buddyCustom = false;
        prefs.putUChar("bcust", 0);
      }
      // the chosen avatar becomes the badge identity: name pre-filled in
      // Setup (editable afterwards) and badge-<Name> SSID immediately
      snprintf(qrName, sizeof(qrName), "%s", AVATARS[setSel].name);
      prefs.putString("bname", qrName);
      snprintf(qrCompany, sizeof(qrCompany), "%s", AVATARS[setSel].comp);
      prefs.putString("bcomp", qrCompany);
      irDirtyMask = 0xFFFFFFFFu; // all idle frames must be redone
      g_ballDirty = true;    // + the ball sprite (snake/DVD/games)
      if (setSpr)
      {
        free(setSpr);
        setSpr = nullptr;
      }
      Serial0.printf("avatar saved: %d (%s)\n", setSel, AVATARS[setSel].name);
      setShown = -1;
      setMenuShown = -1;
      uiMode = UI_SETMENU;
      uiDrawSetMenu(setMenuSel);
      badgeFlush();
      fpsCount++;
      return;
    }
    if (setShown != setSel)
    {
      setShown = setSel;
      const AvatarDef &av = AVATARS[setSel];
      if (setSpr)
        free(setSpr);
      setSpr = dvdGenSprite(PAL_RAINBOW, PAL_N, av.hue, av.sat);
      uiDrawAvatarFrame(setSel, AVATAR_N, av.name);
      dvdBlit(setSpr, CX, CY - 26, 78, 255);
      g_avatarFaceIdx = (uint8_t)setSel; // the face follows the preview
      g_faceForce = setSel; // ...even if a custom buddy is active
      drawIdleFaceLook(CX, CY - 26, 78, 0, 0, 0, 1.0f);
      badgeFlush();
      fpsCount++;
    }
    return;
  }

  if (uiMode == UI_SCHED)
  {
    // schedule: left/right = previous/next event, center = back
    if (navNext)
      schedIdx = (schedIdx + 1) % UI_NEVENTS;
    if (navPrev)
      schedIdx = (schedIdx + UI_NEVENTS - 1) % UI_NEVENTS;
    if (autoShort)
      uiMode = UI_MENU; // back to the Meet list
    static int schedLast = -1;
    static bool schedLastNow = false;
    bool nowFlag = uiEventIsNow(UI_EVENTS[schedIdx]);
    if (uiMode != UI_SCHED)
    {
      uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
      badgeFlush();
      fpsCount++;
      schedLast = -1;
    }
    else if (schedIdx != schedLast || nowFlag != schedLastNow)
    {
      uiDrawSchedule(schedIdx);
      badgeFlush();
      fpsCount++;
      schedLast = schedIdx;
      schedLastNow = nowFlag;
    }
    return;
  }

  if (uiMode == UI_MENU)
  {
    int count = uiListCount(menuCat);
    if (navNext)
      menuSel = (menuSel + 1) % count;
    if (navPrev)
      menuSel = (menuSel + count - 1) % count;
    if (autoShort)
    {
      int arg = 0;
      switch (uiResolve(menuCat, menuSel, &arg))
      {
      case UIA_ANIM:
        slot = arg;
        slotStartMs = now;
        uiMode = UI_ANIM;
        break;
      case UIA_GAME:
        gameReset(arg);
        uiMode = (UiMode)(UI_SNAKE + arg);
        break;
      case UIA_DRAW:
        drawModeEnter();
        drawDrawWait();
        waitTE();
        badgeFlush();
        uiMode = UI_DRAW;
        fpsCount++;
        return; // without this return, the menu redraws over the info screen
      case UIA_SETUP:
        setupModeEnter();
        setupDrawScreen();
        waitTE();
        badgeFlush();
        uiMode = UI_SETUP;
        fpsCount++;
        return;
      case UIA_QR:
        qrScreenPrepare();
        uiMode = UI_QR;
        break;
      case UIA_MET:
        metScroll = 0;
        metShown = -1;
        uiMode = UI_MET;
        break;
      case UIA_LB:
        lbGame = 0;
        lbShown = -1;
        lbMine[0] = prefs.getUShort("snakeBest", 0);
        lbMine[1] = prefs.getUShort("pongBest", 0);
        lbMine[2] = prefs.getUShort("runBest", 0);
        lbMine[3] = prefs.getUShort("tetroBest", 0);
        uiMode = UI_LB;
        break;
      case UIA_AUTO:
        autoCycle = !autoCycle; // toggle without leaving
        break;
      case UIA_SCHED:
        uiMode = UI_SCHED; // the schedule (Meet category entry)
        break;
      case UIA_ROT:
        uiMode = UI_ROT; // screen rotation calibration
        break;
      case UIA_SETTINGS:
        memset(pinDigits, 0, sizeof(pinDigits));
        pinPos = 0;
        pinError = false;
        pinRedraw = true;
        uiMode = UI_PIN; // code-protected settings (badge avatar)
        break;
      case UIA_OTA:
        Serial0.println("menu: restarting into OTA flash mode");
        otaRequest = OTA_MAGIC;
        delay(50);
        esp_restart();
        break;
      case UIA_BACK:
        uiMode = UI_HOME;
        break;
      default:
        break;
      }
    }
    bool moving = (uiMode == UI_MENU)
                      ? uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging)
                      : uiDrawHome(dt, batPct, batCharging);
    static int listLastPct = -999;
    static bool listLastChg = false, listFirst = true;
    if (moving || navNext || navPrev || autoShort || listFirst ||
        batPct != listLastPct || batCharging != listLastChg)
    {
      badgeFlush();
      fpsCount++;
    }
    listFirst = (uiMode != UI_MENU);
    listLastPct = batPct;
    listLastChg = batCharging;
    return;
  }

  if (uiMode == UI_SETUP)
  {
    setupModeLoop();
    if (autoShort) // center: back to the menu, WiFi off
    {
      setupModeExit();
      uiMode = UI_MENU;
      uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
      waitTE();
      badgeFlush();
      fpsCount++;
      return;
    }
    if (setupClients)
    {
      // phone connected: continuous animated preview (buddy or live QR)
      setupDrawLive(now / 1000.0f);
      waitTE();
      badgeFlush();
      fpsCount++;
    }
    else if (setupRedraw)
    {
      setupRedraw = false;
      setupDrawScreen();
      waitTE();
      badgeFlush();
      fpsCount++;
    }
    else
      delay(2); // let the WiFi breathe
    return;
  }

  if (uiMode == UI_MET)
  {
    // encounters list: prev/next = scroll, center = back
    if (navNext && metScroll + MET_ROWS < metN)
      metScroll++;
    if (navPrev && metScroll > 0)
      metScroll--;
    if (autoShort)
    {
      uiMode = UI_MENU;
      uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
      waitTE();
      badgeFlush();
      fpsCount++;
      return;
    }
    if (metShown != metScroll)
    {
      metShown = metScroll;
      uiDrawMet(metScroll);
      waitTE();
      badgeFlush();
      fpsCount++;
    }
    return;
  }

  if (uiMode == UI_LB)
  {
    // leaderboard: prev/next = previous/next game (wraps, BOOT-friendly),
    // center = back
    if (navNext)
      lbGame = (lbGame + 1) % LB_GAMES;
    if (navPrev)
      lbGame = (lbGame + LB_GAMES - 1) % LB_GAMES;
    if (autoShort)
    {
      uiMode = UI_MENU;
      uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
      waitTE();
      badgeFlush();
      fpsCount++;
      return;
    }
    if (lbShown != lbGame)
    {
      lbShown = lbGame;
      uiDrawLB(lbGame, lbMine);
      waitTE();
      badgeFlush();
      fpsCount++;
    }
    return;
  }

  if (uiMode == UI_QR)
  {
    // static QR + animated buddy in the center; center = back to the menu
    if (autoShort)
    {
      qrScreenRelease();
      uiMode = UI_MENU;
      uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
      waitTE();
      badgeFlush();
      fpsCount++;
      return;
    }
    qrScreenDraw(now / 1000.0f);
    waitTE();
    badgeFlush();
    fpsCount++;
    return;
  }

  if (uiMode == UI_DRAW)
  {
    drawModeLoop();
    if (autoShort) // center: back to the menu, WiFi off
    {
      drawModeExit();
      uiMode = UI_MENU;
      uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
      waitTE();
      badgeFlush();
      fpsCount++;
      return;
    }
    drawAnimTick(now); // keeps the animated brushes alive (glitter, iris...)
    if (drawDirty)
    {
      // partial flush of the dirty area, no TE wait: minimal latency
      drawFlushDirty();
      fpsCount++;
    }
    else
      delay(2); // nothing to display: let the WiFi breathe
    return;
  }

  if (uiMode >= UI_SNAKE)
  {
    int gi = uiMode - UI_SNAKE;
    bool over = gameIsOver(gi);
    // "press-down" detection of the center button (responsive, shoot/jump)
    static uint32_t lastAutoPressSeen = 0;
    bool centerDown = false;
    uint32_t ap = autoPressMs;
    if (ap && ap != lastAutoPressSeen)
    {
      lastAutoPressSeen = ap;
      centerDown = true;
    }
    // universal exit: left + right held for 0.8 s
    static uint32_t bothHold = 0;
    if (digitalRead(BTN_PREV) == LOW && digitalRead(BTN_NEXT) == LOW)
    {
      if (!bothHold)
        bothHold = now;
    }
    else
      bothHold = 0;
    bool quit = (bothHold && now - bothHold > 800);
    // center: game action OR back to menu depending on the game (always
    // menu if game over)
    if (gameCenterIsAction(gi) && !over)
      gBtnCenter = centerDown;
    else
    {
      gBtnCenter = false;
      if (autoShort)
        quit = true;
    }
    if (quit)
    {
      bothHold = 0;
      uiMode = UI_MENU;
      uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
      waitTE();
      badgeFlush();
      fpsCount++;
      return;
    }
    if (over && navNext)
    {
      gameReset(gi); // replay -- and swallow the press so the game misses it
      navNext = navPrev = false;
      gBtnCenter = false;
    }
    gBtnLeft = navPrev;
    gBtnRight = navNext;
    gameUpdate(gi, dt);
    gBtnCenter = gBtnLeft = gBtnRight = false;
    waitTE();
    badgeFlush();
    fpsCount++;
    return;
  }

  // ---- animations mode ----
  if (navNext)
  {
    slot = (slot + 1) % NACTIVE;
    if (ACTIVE[slot] == 16 && !g_hasPhoto)
      slot = (slot + 1) % NACTIVE;
    slotStartMs = now;
  }
  if (navPrev)
  {
    slot = (slot + NACTIVE - 1) % NACTIVE;
    if (ACTIVE[slot] == 16 && !g_hasPhoto)
      slot = (slot + NACTIVE - 1) % NACTIVE;
    slotStartMs = now;
  }
  if (autoShort)
  {
    uiMode = UI_HOME; // main bubble menu
    uiHomeReset();
    Serial0.println("menu: opening");
  }
  if (autoCycle && now - slotStartMs >= ANIM_DURATION_MS)
  {
    slot = (slot + 1) % NACTIVE;
    if (ACTIVE[slot] == 16 && !g_hasPhoto)
      slot = (slot + 1) % NACTIVE;
    slotStartMs = now;
  }
  if (uiMode == UI_HOME)
  {
    uiDrawHome(dt, batPct, batCharging);
    waitTE();
    badgeFlush();
    fpsCount++;
    return;
  }

  int anim = ACTIVE[slot];
  if (anim != lastAnim)
  {
    lastAnim = anim;
    animStartMs = now;
    resetIdle();
    static const char *names[ANIM_COUNT] = {"cube", "starfield", "plasma", "torus",
                                            "snake", "disco", "globe", "threeconf",
                                            "idlerainbow", "dvd", "points", "photo",
                                            "photo2", "photo3", "warp", "solar",
                                            "myphoto"};
    Serial0.printf("animation: %s\n", names[anim]);
  }
  float t = (now - animStartMs) / 1000.0f; // time local to the animation

  switch (anim)
  {
  case 0: animCube(t); break;
  case 1: animStars(dt); break;
  case 2: animPlasma(t); break;
  case 3: animTorus(t); break;
  case 4: animSnake(t, dt); break;
  case 5: animDisco(t); break;
  case 6: animGlobe(t); break;
  case 7: animThreeConf(t); break;
  case 8: animIdleRainbow(t); break;
  case 9: animDvd(t, dt); break;
  case 10: animPoints(t); break;
  case 11: animPhoto(t); break;
  case 12: animPhoto2(t); break;
  case 13: animPhoto3(t); break;
  case 14: animWarp(t, dt); break;
  case 15: animSolar(t, dt); break;
  case 16: animMyPhoto(t); break;
  }
  if (anim == 8) // Conf Buddy: "a friend is here" reaction over the anim
    socialReactDraw(now);
  waitTE();
  badgeFlush();

  fpsCount++;
  if (now - fpsMark >= 2000)
  {
    uint32_t te = teCount;
    teCount = 0;
    teAlive = (te > 0);
    float fps = fpsCount * 1000.0f / (now - fpsMark);
    float teHz = te * 1000.0f / (now - fpsMark);
    Serial0.printf("%.1f fps | TE %.1f Hz%s\n", fps, teHz, teAlive ? "" : " (bypass)");
    fpsCount = 0;
    fpsMark = now;
  }
}
