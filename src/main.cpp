// Badge threejs.paris - animations sur ecran GC9B72 2.1" 360x360 (SPI 4 fils) ESP32-S3.
// Rendu via framebuffer (Arduino_Canvas, ~253 Ko en PSRAM) puis flush() complet en SPI :
// pas de scintillement, tout est dessine hors ecran.
//
// 4 animations style Three.js, cyclees toutes les 15 s :
//   0. cube wireframe 3D
//   1. starfield (vol a travers les etoiles)
//   2. plasma (LUT sinus + palette, calcule en demi-resolution)
//   3. tore en nuage de points
//
// Le debit SPI limite le framerate : un flush 360x360x16bits = ~2 Mbits.
//   20 MHz -> ~9 fps max ; 40 MHz -> ~19 fps max.
// 20 MHz est annonce fiable par la lib sur fils courts. Essaie 40000000 ;
// redescends a 20000000 / 10000000 si tu vois du bruit ou des artefacts.

#include <Arduino_GFX_Library.h>
#include <Arduino_GC9B72.h>
#include <math.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
// inclus ici (avant les #define W/H...) : WebSockets tire mbedtls, qui
// utilise des identifiants nommes W — les macros les casseraient
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <time.h>
#include <sys/time.h>
#include <driver/rtc_io.h>
#include <driver/gpio.h>
#include <Preferences.h>

// ---- Cablage (module : GND VCC SCL SDA RST DC CS BL SDO TE) ----
// VCC -> 3V3   GND -> GND
// SDO : non connecte (tombe en face de GPIO 46, laisse vide)
// Mapping NAPPE (revue 2026-08-14) : les GPIO sont choisis pour que la nappe
// arc-en-ciel 10 fils tombe dans l'ordre EXACT du peigne du devkit, lignes
// 13..20 contigues, zero croisement (noir TE=3, [46 vide], gris BL=9,
// violet CS=10, bleu DC=11, vert RST=12, jaune MOSI=13, orange SCLK=14 ;
// marron GND 2 lignes plus bas, rouge 3V3 remonte seul en haut du peigne).
// Badges cables AVANT le 2026-08-14 (fils volants) : ancien mapping
// SCLK 12 / MOSI 11 / DC 13 / RST 14 -> reprendre ces 4 fils cote ESP,
// OU flasher avec l'env "proto" (pio run -e proto -t upload) qui garde
// l'ancien cablage — utilise pour le premier prototype de Romain.
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
#define TFT_TE 3    // <- TE (impulsion a chaque debut de balayage, TEON active par le driver)
#define TFT_BL 9        // <- BL (retroeclairage) : pilote par GPIO pour pouvoir le couper.
                        // GPIO 9 = broche VOISINE du bloc ecran 10-14 sur le peigne du
                        // devkit (cablage nappe contigu). Les premiers badges etaient
                        // cables sur GPIO 4 (haut du peigne) : les deux broches sont
                        // pilotees en parallele, aucun recablage necessaire.
#define TFT_BL_LEGACY 4 // <- BL des premiers badges (laisse en l'air sur les nouveaux)
                    //    a l'extinction — ne plus le cabler en direct sur le 3V3 !

// Boutons de navigation (entre GPIO et GND, pull-up interne, actifs LOW).
// 19/20/21 : coin haut-gauche du devkit, a cote d'un GND — cablage court.
// NB : 19/20 = D-/D+ de l'USB natif, libres car ARDUINO_USB_CDC_ON_BOOT est
// desactive (platformio.ini) ; flash et logs passent par le pont CH343.
#define BTN_NEXT 19 // suivante / descendre dans le menu
#define BTN_PREV 20 // precedente / monter dans le menu ; maintenu au BOOT -> mode flash OTA
#define BTN_AUTO 21 // appui court : ouvre le menu / selectionne ; appui long 2 s : extinction
#define BTN_BOOT 0  // bouton BOOT de la carte : aussi "suivante" (pratique en test)

// Jauge batterie (menu) : pont diviseur 100k/100k B+ -> GPIO5 -> GND, et
// detection de charge par le VBUS du TP4056 via 100k/100k -> GPIO6.
// Firmware tolerant : sans ces fils, le menu affiche "--%" sans eclair.
#ifdef PROTO_V1_WIRING
#define PIN_VBAT 5 // premier proto : pont batterie sur 5/6 (tolerant si absent)
#define PIN_VBUS 6
#else
#define PIN_VBAT 1 // pont batterie (etait GPIO 5 — 1/2 simplifient le cablage)
#define PIN_VBUS 2 // detection charge (etait GPIO 6)
#endif

// Mode flash OTA (bouton PREV maintenu a l'allumage) : le badge cree son
// propre point d'acces Wi-Fi et attend le televersement (pio run -e ota -t upload).
#define OTA_SSID "badge-threejs"
#define OTA_PASS "threejs2026"
static bool otaMode = false;

#define SPI_FREQ 80000000 // 80 MHz : flush ~26 ms au lieu de ~52 (valide sur
                          // nappe courte soudee ; repasser a 40 MHz si
                          // artefacts sur fils volants)

#define W 360
#define H 360
#define CX 180
#define CY 180

#define ANIM_COUNT 16
#define ANIM_DURATION_MS 15000
#define RADIUS 180 // rayon utile de l'ecran rond

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED /*MISO*/);
Arduino_GFX *panel = new Arduino_GC9B72(bus, TFT_RST, 0 /*rotation*/, false /*IPS*/, W, H);
Arduino_Canvas *canvas = new Arduino_Canvas(W, H, panel);

#include "dma_flush.h" // flush asynchrone SPI3+DMA (remplace canvas->flush)
static bool dmafOk = false;

// ---------------------------------------------------------------- utilitaires

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

// Compteur d'impulsions TE (diagnostic) : permet de verifier que le signal
// arrive bien et de mesurer la frequence de balayage du panneau.
static volatile uint32_t teCount = 0;
static void IRAM_ATTR teIsr() { teCount++; }

// Appuis boutons captures par interruption : la boucle ne tourne qu'a ~10 Hz,
// un appui bref serait rate en polling. L'anti-rebond est fait dans loop().
static volatile bool btnNextFlag = false;
static volatile bool btnPrevFlag = false;
static void IRAM_ATTR btnNextIsr() { btnNextFlag = true; }
static void IRAM_ATTR btnPrevIsr() { btnPrevFlag = true; }

// Bouton central : l'appui COURT est detecte au RELACHEMENT (30-600 ms), pour
// ne pas se declencher au debut d'un appui long (extinction). L'appui long est
// surveille par polling dans loop() via autoPressMs.
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

// Attend le prochain front montant de TE pour demarrer le flush en debut de
// balayage : le point de dechirure devient fixe au lieu de defiler.
// Si aucune impulsion TE n'est vue pendant 2 s (fil debranche/faux contact),
// on passe en bypass : plus d'attente, jusqu'au retour du signal.
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

static void powerOff(); // definie apres les animations

// ------------------------------------------------------------- 0. cube 3D

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
    // rotation X puis Y puis Z
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
    uint16_t c = hsv2rgb565(150, 60, v); // blanc bleute
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
  // calcule en 180x180, chaque valeur remplit un bloc 2x2 du framebuffer
  // Mouvement volontairement lent : le flush (~52 ms) croise 3x le balayage
  // du panneau (60 Hz) ; si deux frames consecutives sont proches, les points
  // de croisement (tearing) deviennent invisibles.
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

// --------------------------------------------------------------- 3. tore

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
    // teinte selon la position sur l'anneau, luminosite selon la profondeur
    uint8_t hue = (uint8_t)(i * 255 / TOR_PTS + (uint8_t)(t * 25.0f));
    uint8_t val = (uint8_t)constrain(190.0f - z2 * 90.0f, 70.0f, 255.0f);
    uint16_t c = hsv2rgb565(hue, 230, val);
    if (z2 < 0.0f)
      canvas->fillCircle(sx, sy, 2, c);
    else
      canvas->drawPixel(sx, sy, c);
  }
}

// ---- Portages depuis speaker-badge-anims (screen-anims.js) ----

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

#include "avatars.h" // 40 avatars (table + visage) — avant anims_extra.h

// ---- Texture de sphere "rainbow" : blend gaussien des points colores de
// PAL_RAINBOW (screen-anims.js), vibrance + grain, pre-calculee au boot. ----

#define SPR 112 // taille du sprite (le plus gros usage : tete du snake, 108 px)
static uint16_t *ballSprite = nullptr;

// {px, py, r, g, b} — points de couleur sur le disque unite
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
  if (!ballSprite) // regenerable au changement d'avatar (g_ballDirty)
    ballSprite = (uint16_t *)malloc(SPR * SPR * sizeof(uint16_t));
  // couleurs transformees par l'avatar actif / le buddy custom (meme
  // transformation que la sphere idle) : le snake, la palette rainbow de la
  // DVD et Sphere Run suivent la couleur configuree du badge
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
        ballSprite[y * SPR + x] = 0; // hors disque (jamais lu au blit)
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

// Blit du sprite de sphere, redimensionne au rayon r (nearest, masque disque)
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

// ---- Bouche SVG de la mascotte (MOUTH_SVG de screen-anims.js), rasterisee
// au boot dans un masque, puis blittee a l'echelle. ----

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
  // remplissage scanline pair-impair du contour
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
  // les deux "virgules" : cubiques tracees en cercles epais (stroke 12.29, round cap)
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

// Blit de la bouche : masque redimensionne (nearest), couleur unie
static void drawMouthImg(float cx, float cy, float wpx, float hpx, uint16_t ink)
{
  int iw = (int)wpx, ih = (int)hpx;
  if (iw < 2 || ih < 2 || !mouthMask)
    return;
  int x0 = (int)(cx - iw / 2.0f), y0 = (int)(cy - ih / 2.0f);
  for (int yy = 0; yy < ih; yy++)
  {
    int my = yy * MOUTH_MH / ih;
    for (int xx = 0; xx < iw; xx++)
    {
      if (mouthMask[my * MOUTH_MW + xx * MOUTH_MW / iw])
        canvas->drawPixel(x0 + xx, y0 + yy, ink);
    }
  }
}

// ---- Etat "idle" du visage : regard vagabond + clignements (port de
// getIdleState / pickNewLookTarget / drawFaceElements de screen-anims.js) ----

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

// Renvoie lookX, lookY [-1..1] et openness [0..1]
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
    if (bt >= 1 || bt < 0) // bt < 0 : le temps local est reparti en arriere
    {
      idleSt.blinkStart = -1;
      idleSt.nextBlink = t + (frand(0, 1) < 0.25f ? 0.15f : 1.8f + frand(0, 3.5f));
    }
    else
      *openness = bt < 0.4f ? 1 - bt / 0.4f : (bt - 0.4f) / 0.6f;
  }
}

// Visage anime projete sur une sphere de rayon fr centree (cx, cy) :
// yeux ronds qui suivent le regard (squish lateral), clignement, bouche SVG.
// Variante "Look" : le regard est fourni par l'appelant (partage avec la
// rotation de texture d'Idle Rainbow).
// museau du perso original (visage AF_MUSEAU), rendu par la plateforme
static void avatarPlatformMouth(float mx, float my, float mw, float mh, uint16_t ink)
{
  drawMouthImg(mx, my, mw, mh, ink);
}

// reaction sociale (social_ui.h, inclus plus bas) : remplace le visage
// pendant les 5 s d'une rencontre entre badges
static bool socialExprFace(float cx, float cy, float fr);

static void drawIdleFaceLook(float cx, float cy, float fr, float t,
                             float lookX, float lookY, float openness)
{
  if (socialExprFace(cx, cy, fr))
    return; // expression Happy/Wow/Love a la place du visage normal
  uint16_t ink = rgb565(39, 39, 39); // #272727
  float breathe = sinf(t * 1.8f) * 0.5f;
  float theta = lookX * 30.0f * PI / 180.0f;
  // visage de l'avatar affiche (9 designs Figma) : projection et rendu
  // entierement dans avatars.h (partage firmware/emulateur)
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

// Visage du perso (yeux + sourire, clignement periodique), proportions du JS.
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
  // sourire : bezier quadratique (-0.26r,0.02r) -> (0,0.26r) -> (0.26r,0.02r),
  // trace en pastilles rondes (equivalent trait epais a bouts ronds)
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

// ------------------------------------------------- 4. snake (trail de spheres)

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
  if (g_ballDirty) // avatar/buddy change : re-teinte le sprite de boule
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

  // virage sinusoidal lisse + rebond billard sur le bord du disque
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

  // echantillonnage du corps a pas constant le long de la trainee
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
  // queue -> tete (la tete passe au-dessus), leger fuselage vers la queue
  for (int i = SNAKE_N - 1; i >= 0; i--)
  {
    float sr = headR * (1.0f - 0.25f * i / (SNAKE_N - 1));
    drawBallSprite((int)pts[i].x, (int)pts[i].y, sr);
  }
  // le visage regarde dans la direction du deplacement (angle lisse pour
  // que le rebond sur les bords ne fasse pas claquer le regard) ; le
  // clignement vient toujours de getIdle
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

// ------------------------------------------------- 5. disco (boule a facettes)

static const uint8_t DISCO_PALS[5][3] = {
    {158, 197, 240}, {255, 167, 254}, {255, 203, 138}, {159, 146, 243}, {128, 219, 188}};

static void animDisco(float t)
{
  const float Rb = RADIUS * 0.74f;
  const int NLAT = 15, NLON = 26;
  const float rot = t * 0.6f;
  const float Lx = -0.45f, Ly = -0.52f, Lz = 0.72f; // lumiere haut-gauche-avant

  canvas->fillScreen(rgb565(8, 6, 16)); // #080610

  // halo discret derriere la boule (avant les lumieres pour ne pas les couvrir)
  canvas->fillCircle(CX, CY, (int16_t)(Rb * 1.12f), rgb565(20, 22, 42));

  // points de lumiere colores qui balayent le fond (scintillants)
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

  // facettes : grille lat/long projetee, hemisphere avant uniquement
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
        continue; // face arriere
      float b = nx * Lx + ny * Ly + nz * Lz;
      if (b < 0)
        b = 0;
      int seed = i * 131 + j * 57;
      float tw = 0.5f + 0.5f * sinf(t * 3 + seed);
      const uint8_t *base = DISCO_PALS[(i * 7 + j * 3) % 5];
      float sf = 0.28f + b * 1.05f;
      float gm = (b > 0.55f && tw > 0.8f) ? 0.82f : 0; // glint -> vers le blanc
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

  // visage anime du perso, face camera, a l'echelle de la boule
  drawIdleFace(CX, CY, Rb, t);
}

// Remise a zero de l'etat du visage idle (au changement d'animation, le temps
// local repart de 0 : sans reset, les timers de regard/clignement seraient faux)
static void resetIdle()
{
  idleSt.lookCX = idleSt.lookCY = idleSt.lookPX = idleSt.lookPY = 0;
  idleSt.lookStart = 0;
  idleSt.lookDur = 0.5f;
  idleSt.lookHold = 0.5f;
  idleSt.blinkStart = -1;
  idleSt.nextBlink = 2.5f;
}

// ---------------------- 6. three globe / 7. three conf (fichier partage) ----
#include "anims_extra.h"
#include "menu_font.h" // Dingos ExtraBold pour le menu
#include "menu_font_med.h" // Dingos Medium (pourcentage batterie)
#include "menu_font_bebas.h" // Bebas Neue (types d'events du Schedule)
#include "menu_font_title.h" // Dingos ExtraBold 30 (titres du Schedule)

// ------------------------------------------------------------------- menu

// Anims actives (les autres restent dispo dans le code) + noms affiches.
// Photos speaker (11..13) retirees avec les entrees Meet (revue 2026-08-29).
static const uint8_t ACTIVE[] = {8, 4, 5, 6, 7, 9, 10, 14, 15};
static const int NACTIVE = (int)sizeof(ACTIVE);
// (les tables du menu vivent dans menu_ui.h, partage avec l'emulateur)

// Demande de mode OTA depuis le menu : survit au redemarrage logiciel (RTC RAM)
#define OTA_MAGIC 0x07A07A17
RTC_NOINIT_ATTR uint32_t otaRequest;

// Etat de l'interface : animations / menu / jeux
enum UiMode : uint8_t { UI_ANIM, UI_MENU, UI_HOME, UI_SCHED, UI_ROT, UI_DRAW, UI_SNAKE, UI_PONG, UI_RUN, UI_TETRIS, UI_PET, UI_PIN, UI_SET, UI_SETUP, UI_QR, UI_MET, UI_SETMENU, UI_PROX, UI_LB, UI_VCAL };
static UiMode uiMode = UI_ANIM;

// ---- etat des Settings (code d'acces + choix d'avatar, voir menu_ui.h) ----
static uint8_t pinDigits[5]; // = UI_PIN_LEN (menu_ui.h, inclus plus bas)
static int pinPos = 0;
static bool pinError = false, pinRedraw = true;
static int setSel = 0, setShown = -1;   // avatar en cours de choix / affiche
static uint16_t *setSpr = nullptr;      // sprite de preview (dvdGenSprite)
static int metScroll = 0, metShown = -1; // ecran Encounters (Meet)
static int lbGame = 0, lbShown = -1;     // ecran Leaderboard (Meet)
static uint16_t lbMine[4];               // mes records (LB_GAMES, declare
                                         // plus bas dans menu_ui.h)
static int setMenuSel = 0, setMenuShown = -1; // menu Settings
static int proxLevel = 2;                     // reglage proximite (Normal)
static int menuSel = 0;
static int menuCat = 0; // categorie de la liste affichee (UIC_*)
static int schedIdx = 0; // event affiche dans le Schedule
static bool autoCycle = false;
static Preferences prefs; // records des jeux, persistants en flash (NVS)

// Jauge batterie : lecture du pont diviseur (x2) lissee, courbe LiPo approchee.
static int batPct = -1; // -1 : pont diviseur absent
static int16_t vbatCal = 1000; // calibration du pont, pour-mille (NVS "vcal")
static uint32_t batMvRaw = 0; // tension lissee (mV), pour l'info du menu More
static bool batCharging = false;

static void updateBattery(uint32_t now)
{
  static uint32_t lastRead = 0;
  static float ema = 0;
  if (now - lastRead < 500)
    return;
  lastRead = now;
  // Lecture ANALOGIQUE de la detection de charge (seuil 0.7 V ; en charge le
  // pont donne ~1.2-2.5 V, non branche ~0 V). PURGE d'abord : l'echantillonneur
  // ADC est partage entre les canaux et garde la charge de la broche batterie
  // (~1.7 V) — avec un pont 100k, une lecture isolee heriterait de ce residu
  // et allumerait un eclair fantome (vu en test le 2026-08-06).
  analogReadMilliVolts(PIN_VBUS);
  analogReadMilliVolts(PIN_VBUS);
  uint32_t vbus = 0;
  for (int i = 0; i < 4; i++)
    vbus += analogReadMilliVolts(PIN_VBUS);
  batCharging = (vbus / 4) > 700;
  // Rafale de 12 lectures, min et max ecartes : l'ADC de l'ESP32 est bruyant
  // et le pont 100k/100k est haute impedance (~50k) — une lecture isolee
  // danse de plusieurs dizaines de mV, soit plusieurs % sur la courbe LiPo.
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
  uint32_t mv = (sum - lo - hi) / 10 * 2; // pont 100k/100k
  // Compensation de charge DEGRESSIVE : ~200 mV sous 1 A (phase CC, jusqu'a
  // ~3.9 V), puis le courant decroit en phase CV -> la surtension reelle
  // fond aussi. Un forfait fixe sur-corrigerait la fin de charge (le %
  // semblait bloque sous 100 %).
  if (batCharging && mv > 200)
  {
    uint32_t off = mv < 3900 ? 200 : (mv >= 4150 ? 40 : 200 - (mv - 3900) * 160 / 250);
    mv -= off;
  }
  if (mv < 2500)
  {
    batPct = -1; // pas de capteur cable (ou batterie hors plage)
    ema = 0;
    batMvRaw = 0;
    return;
  }
  // lissage LENT (~10 s de constante de temps a 2 lectures/s) : les creux de
  // tension sous charge (anims, WiFi) ne doivent pas faire plonger la jauge.
  // L'EMA porte sur la tension BRUTE ; la calibration par badge (tolerance
  // des ponts 100k, ecran Settings > Batt) s'applique apres — l'ecran de
  // calibration repond ainsi instantanement au reglage.
  ema = (ema == 0) ? mv : ema * 0.95f + mv * 0.05f;
  batMvRaw = (uint32_t)(ema * vbatCal / 1000.0f);
  static const struct { uint16_t mv; uint8_t pct; } C[] = {
      {3300, 0}, {3500, 10}, {3600, 20}, {3700, 40}, {3800, 60},
      {3900, 75}, {4000, 88}, {4100, 96}, {4200, 100}};
  float v = ema * vbatCal / 1000.0f; // courbe sur la tension CALIBREE
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
  // Fin de charge : tension compensee haute (>=4.06 V) SOUTENUE 15 min en
  // charge -> pleine. Le seuil instantane sautait a 100 % des le branchement
  // d'une batterie a 90 % (la tension bondit a 4.2 V en phase CV bien avant
  // la fin reelle — sans mesure de courant, seule la duree discrimine).
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

  // hysteresis d'affichage : le % ne bouge que d'1 point par lecture (2/s) —
  // fini les sauts 40 -> 20 -> 38, la jauge glisse doucement vers la mesure.
  // EN CHARGE, la montee est de plus limitee a ~1 %/min : c'est le rythme
  // physique max (2000 mAh a 1 A) — la tension de charge surestime toujours
  // le niveau (saut 60 -> 74 % observe au branchement), le temps ne ment pas.
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
}

#include "menu_ui.h" // menu bulles + listes (partage firmware/emulateur)

// Flush avec rotation logicielle optionnelle (compense les dalles de travers).
// La rotation (~4 ms) ne s'applique que si un angle est regle.
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
    // asynchrone : la fin du transfert part en DMA pendant le rendu suivant
    dmafFlush(0, 0, W, H, src, W, false);
    return;
  }
  // secours : chemin Arduino_GFX bloquant d'origine
  if (src == rotBuf)
    panel->draw16bitRGBBitmap(0, 0, rotBuf, W, H);
  else
    canvas->flush();
}

// ----------------------------------------------------------------- jeux
#include "games.h"
#include "qr_screen.h"  // ecran Meet > QR Code (partage avec l'emulateur) —
                        // fournit badgeSsid(), utilise par draw/setup/OTA
#include "setup_mode.h" // parcours de config sur telephone (More > Setup) —
                        // fournit le DNS captif badgeDns*, utilise par Draw
#include "draw_mode.h"
#include "social.h"     // rencontres entre badges (ESP-NOW, Conf Buddy)

// Ecran d'attente du mode dessin : infos de connexion tant que personne
// n'a rejoint (efface par draw_mode.h a la premiere connexion WebSocket)
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

// Extinction "logicielle" avec l'animation "power down" du repo (facon vieille
// TV CRT) : le perso se compresse verticalement en une ligne avec un flash
// blanc, puis un point chaud central se dissipe. Ensuite : ecran en veille
// (display off + sleep in), retroeclairage coupe et verrouille, deep sleep de
// l'ESP32, reveil par appui sur BTN_AUTO (GPIO RTC). Conso residuelle : boost
// TP4056 + regulateur du devkit (~qq mA) — OK pour un badge recharge souvent.
static void powerOff()
{
  Serial0.println("extinction (deep sleep) — reveil par le bouton AUTO");

  // Frame source : ce qui est A L'ECRAN au moment de l'extinction (dernier
  // frame rendu — animation en cours ou menu), comme une vraie TV qu'on coupe.
  uint16_t *fb = canvas->getFramebuffer();
  uint16_t *snap = (uint16_t *)malloc(W * H * sizeof(uint16_t));
  if (snap)
  {
    memcpy(snap, fb, W * H * sizeof(uint16_t));
    // Phase 1 : contraction verticale acceleree vers une ligne de 6 px,
    // flash blanc sur la fin.
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
    // Phase 2 : point chaud central qui se dissipe (halo + coeur)
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

  if (dmafOk) // le trafic ecran passe par SPI3 depuis dmafInit()
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
  // Coupe le retroeclairage et VERROUILLE l'etat bas pendant le deep sleep
  // (sans hold, la broche flotterait et le retroeclairage pourrait se rallumer).
  digitalWrite(TFT_BL, LOW);
  gpio_hold_en((gpio_num_t)TFT_BL);
  digitalWrite(TFT_BL_LEGACY, LOW);
  gpio_hold_en((gpio_num_t)TFT_BL_LEGACY);
  gpio_deep_sleep_hold_en();
  // Attendre le RELACHEMENT du bouton : l'appui long est encore en cours a cet
  // instant, et le reveil ext0 se declenche sur niveau bas — sans cette attente
  // le badge se rendort et se reveille immediatement.
  while (digitalRead(BTN_AUTO) == LOW)
    delay(10);
  delay(100); // anti-rebond du relachement
  rtc_gpio_pullup_en((gpio_num_t)BTN_AUTO);
  rtc_gpio_pulldown_dis((gpio_num_t)BTN_AUTO);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_AUTO, 0);
  // Reveil AUSSI au branchement USB (revue Romain 2026-08-30 : badge coupe
  // par la protection batterie -> "rien ne se passe" au branchement, alors
  // qu'il charge en silence). Le VBUS arrive divise par 2 sur GPIO2 (RTC) :
  // ext1 ANY_HIGH ~1.65 V au branchement. ARME SEULEMENT si le VBUS est
  // absent a l'extinction — sinon une extinction manuelle pendant la charge
  // se reveillerait aussitot.
  analogReadMilliVolts(PIN_VBUS); // purge du residu d'echantillonneur ADC
  if (analogReadMilliVolts(PIN_VBUS) < 700)
  {
    rtc_gpio_pullup_dis((gpio_num_t)PIN_VBUS);
    rtc_gpio_pulldown_en((gpio_num_t)PIN_VBUS);
    esp_sleep_enable_ext1_wakeup(1ULL << PIN_VBUS, ESP_EXT1_WAKEUP_ANY_HIGH);
  }
  esp_deep_sleep_start();
}

// Loader retro du splash de boot : "LOADING..." + barre a blocs segmentes,
// dessine PAR-DESSUS l'anim Three Conf (uniquement au demarrage), puis passe
// aux scanlines pour se fondre dans le look CRT. p = progression 0..1.
static void drawBootLoader(float p, float t)
{
  const int NB = 12, bw = 14, bh = 14, gap = 4;
  const int totW = NB * bw + (NB - 1) * gap;
  const int x0 = CX - totW / 2, y = 276;
  const uint16_t pink = rgb565(0xfc, 0xa3, 0xf7);
  const uint16_t dimFrame = rgb565(70, 110, 80);

  // Phrases de chargement qui tournent — humour de dev Three.js
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

  // blocs segmentes
  int filled = (int)(p * NB + 0.5f);
  for (int i = 0; i < NB; i++)
  {
    int x = x0 + i * (bw + gap);
    if (i < filled)
      canvas->fillRect(x, y, bw, bh, pink);
    else
      canvas->drawRect(x, y, bw, bh, dimFrame);
  }

  // scanlines locales pour fondre le loader dans l'ambiance CRT
  for (int yy = y - 28; yy < y + bh + 2; yy++)
    if (yy % 3 == 0)
      dimRow(yy, 40, 320);
}

// Generation des textures pendant l'anim de boot. Retourne false quand tout
// est genere. Execute sur LE COEUR 0 (bootGenTask) pendant que l'anim tourne
// sur le coeur 1 : aucune de ces fonctions ne touche au canvas partage, et
// une etape par frame bloquait la frame en cours (freeze visible en debut
// de splash sur le vrai badge).
static bool bootGenStep(int s)
{
  switch (s)
  {
  case 0: initPlasma(); return true;
  case 1: initTorus(); return true;
  case 2: initBallSprite(); return true;
  case 3: initMouthMask(); return true;
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
  Serial0.printf("generation textures : %lu ms (pendant le splash)\n",
                 (unsigned long)(millis() - t0));
  bootGenDone = true;
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------- boucle

void setup()
{
  Serial0.begin(115200); // UART0 -> pont CH343 : logs visibles sur /dev/cu.usbmodem*
  Serial0.println("=== Badge threejs.paris - animations GC9B72 ===");
  prefs.begin("badge", false); // records des jeux (NVS)
  uiScreenRot = (int)(int8_t)prefs.getChar("rotDeg", 0); // rotation ecran calibree
  vbatCal = prefs.getShort("vcal", 1000); // calibration jauge batterie par badge

  // GARDE DE CHARGE (revue Romain 2026-08-30) : cellule critique + chargeur
  // branche -> chaque boot complet (anim, PSRAM, retroeclairage) s'effondrait
  // en brownout et bouclait, l'ecran clignotait et le courant de charge
  // partait dans les tentatives. Ici on attend, CPU au ralenti et ecran
  // eteint (la LED du TP4056 sert de temoin), que la cellule remonte avant
  // de demarrer pour de bon. Debranchement -> on tente le boot normal.
  {
    analogReadMilliVolts(PIN_VBUS); // purge de l'echantillonneur partage
    bool onUsb = analogReadMilliVolts(PIN_VBUS) > 700;
    uint32_t mv = 0;
    for (int i = 0; i < 4; i++)
      mv += analogReadMilliVolts(PIN_VBAT);
    mv = mv / 4 * 2 * (uint32_t)vbatCal / 1000;
    if (onUsb && mv > 2500 && mv < 3400) // 2500 = pont absent (proto nu)
    {
      Serial0.printf("batterie critique en charge (%lu mV) : attente avant boot\n",
                     (unsigned long)mv);
      while (true)
      {
        delay(2000);
        uint32_t s2 = 0;
        for (int i = 0; i < 4; i++)
          s2 += analogReadMilliVolts(PIN_VBAT);
        s2 = s2 / 4 * 2 * (uint32_t)vbatCal / 1000;
        if (s2 >= 3550) // ~3.4 V reels sous charge : boot serein
          break;
        analogReadMilliVolts(PIN_VBUS);
        if (analogReadMilliVolts(PIN_VBUS) < 700)
          break; // debranche par l'utilisateur : on tente
      }
      Serial0.println("charge ok : boot");
    }
  }
  g_avatarIdx = prefs.getUChar("avatar", 0) % AVATAR_N;  // avatar/personne du badge
  g_avatarFaceIdx = g_avatarIdx;
  // buddy custom + nom + URL du QR (parcours More > Setup, sur telephone)
  g_buddyCustom = prefs.getUChar("bcust", 0) != 0;
  g_buddyCustomDef.hue = prefs.getShort("bhue", 0);
  g_buddyCustomDef.sat = prefs.getUChar("bsat", 100) / 100.0f;
  g_buddyCustomDef.face = prefs.getUChar("bface", 0) % 9;
  prefs.getString("bname", qrName, sizeof(qrName));
  socialMetLoad(); // compteurs de rencontres (ecran Meet > Encounters)
  lbLoad();        // scores appris des autres badges (Meet > Leaderboard)
  socialRssiNear = (int8_t)prefs.getChar("prox", -62); // seuil de proximite (= Normal)
  prefs.getString("bcomp", qrCompany, sizeof(qrCompany));
  prefs.getString("bmsg", qrMsg, sizeof(qrMsg));
  if (prefs.getString("qrurl", qrUrl, sizeof(qrUrl)) == 0 || !qrUrl[0])
    snprintf(qrUrl, sizeof(qrUrl), "https://threejs.paris");

  // Bouton PREV — ou BOOT, pratique tant que les boutons ne sont pas cables —
  // presse pendant l'anim de boot -> mode flash OTA (la fenetre est surveillee
  // dans la boucle d'anim, plus de temps mort avant l'allumage de l'ecran).
  // NB : BOOT maintenu PENDANT le reset = bootloader ROM (strapping GPIO 0) ;
  // il faut donc appuyer juste APRES le reset.
  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_BOOT, INPUT_PULLUP);
  // ...ou demande depuis l'entree "Mode Flash OTA" du menu (drapeau RTC RAM)
  if (otaRequest == OTA_MAGIC)
    otaMode = true;
  otaRequest = 0;

  // Retroeclairage : libere un eventuel hold du deep sleep precedent puis allume
  // (GPIO 9 = badges nappe, GPIO 4 = premiers badges — pilotes en parallele,
  // la broche non cablee reste simplement en l'air)
  gpio_hold_dis((gpio_num_t)TFT_BL);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  gpio_hold_dis((gpio_num_t)TFT_BL_LEGACY);
  pinMode(TFT_BL_LEGACY, OUTPUT);
  digitalWrite(TFT_BL_LEGACY, HIGH);

  pinMode(TFT_TE, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(TFT_TE), teIsr, RISING);

  pinMode(BTN_NEXT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_NEXT), btnNextIsr, FALLING);
  pinMode(BTN_PREV, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_PREV), btnPrevIsr, FALLING);
  pinMode(BTN_AUTO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_AUTO), btnAutoIsr, CHANGE);
  pinMode(PIN_VBUS, INPUT_PULLDOWN); // detection de charge (LOW si non cable)
  pinMode(BTN_BOOT, INPUT_PULLUP);
  // (BOOT n'est plus attache a une interruption : il est scrute dans loop()
  // avec 3 fonctions selon la duree d'appui — voir le bloc "bouton BOOT")

  if (!canvas->begin(SPI_FREQ))
  {
    Serial0.println("ERREUR : canvas->begin() a echoue (framebuffer ou ecran)");
    while (true)
      delay(1000);
  }
  // bascule le trafic ecran sur SPI3+DMA (l'init du panneau reste Arduino_GFX)
  dmafOk = dmafInit();
  Serial0.printf("PSRAM libre : %u octets\n", (unsigned)ESP.getFreePsram());

  // Sequence de demarrage : anim "Three Conf" (logo + loader) pendant 4 s —
  // fait aussi office de verification visuelle de la liaison SPI. La
  // generation des textures tourne EN PARALLELE sur le coeur 0 (l'anim reste
  // fluide sur le coeur 1), et PREV/BOOT presse pendant l'anim bascule en
  // mode flash OTA.
  // NB : tache volontairement sur le COEUR 1 (celui de l'anim) — epinglee au
  // coeur 0, la generation PSRAM concurrente du rendu provoquait des resets
  // TG1WDT sur certaines cartes (rail d'alim marginal) ; sur le meme coeur,
  // l'ordonnanceur entrelace (generation ~5 s, toujours pendant le splash)
  xTaskCreatePinnedToCore(bootGenTask, "bootgen", 16384, nullptr, 1, nullptr, 1);
  if (!otaMode)
  {
    uint32_t t0 = millis();
    while (millis() - t0 < 4000 && !otaMode)
    {
      float ts = (millis() - t0) / 1000.0f;
      animThreeConf(ts, -20); // logo remonte pour laisser la place au loader
      drawBootLoader(ts / 4.0f, ts);
      waitTE();
      badgeFlush();
      // fenetre OTA : 1,5 s (au-dela, un appui pendant le boot est ignore)
      if (ts < 1.5f &&
          (digitalRead(BTN_PREV) == LOW || digitalRead(BTN_BOOT) == LOW))
        otaMode = true;
    }
  }

  if (otaMode)
  {
    // Point d'acces autonome + serveur OTA ; le reste du setup (animations)
    // est saute, loop() ne fera que ArduinoOTA.handle().
    WiFi.mode(WIFI_AP);
    WiFi.softAP(badgeSsid(), OTA_PASS);
    ArduinoOTA.onStart([]() { Serial0.println("OTA : debut"); });
    ArduinoOTA.onProgress([](unsigned int prog, unsigned int total) {
      static int lastPct = -1;
      int pct = prog / (total / 100);
      if (pct / 5 != lastPct / 5) // rafraichit l'ecran tous les 5 %
      {
        lastPct = pct;
        canvas->fillRect(80, 220, 200, 14, rgb565(40, 40, 40));
        canvas->fillRect(80, 220, 2 * pct, 14, rgb565(255, 213, 48));
        badgeFlush();
        Serial0.printf("OTA : %d %%\n", pct);
      }
    });
    ArduinoOTA.onEnd([]() { Serial0.println("OTA : OK, redemarrage"); });
    ArduinoOTA.onError([](ota_error_t e) { Serial0.printf("OTA : erreur %u\n", e); });
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
    Serial0.printf("MODE FLASH OTA : AP %s / %s, IP %s\n", badgeSsid(), OTA_PASS,
                   WiFi.softAPIP().toString().c_str());
    return;
  }

  // Attend la fin de la generation (coeur 0) — en pratique elle se termine
  // bien avant les 4 s du splash
  while (!bootGenDone)
    delay(5);

  Serial0.println("setup done");
}

void loop()
{
  if (otaMode)
  {
    ArduinoOTA.handle();
    // Sortie du mode flash SANS flasher (boitier ferme, pas de reset physique) :
    // appui long 2 s sur le bouton central -> redemarrage normal.
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

  // Rencontres entre badges : la radio ESP-NOW n'est active que quand le
  // Conf Buddy est a l'ecran (elle se coupe des qu'on entre dans le menu,
  // donc toujours AVANT les AP WiFi de Draw/Setup/OTA).
  // Garde-fou serie (briseur de boucle de crash) : un marqueur NVS est arme
  // juste avant d'allumer la radio et desarme apres 8 s de fonctionnement.
  // Si un boot trouve le marqueur arme, la session precedente est morte au
  // demarrage radio (brownout/POR sur alim marginale) -> radio sociale
  // coupee pour CETTE session, le badge reste utilisable. Le marqueur est
  // efface : au prochain cycle d'alimentation, on retente une fois.
  static uint32_t socialArmMs = 0;
  static const bool socialBlocked = [] {
    if (esp_reset_reason() == ESP_RST_BROWNOUT || prefs.getUChar("socboot", 0))
    {
      prefs.putUChar("socboot", 0);
      Serial0.println("social : desactive (crash au demarrage radio "
                      "precedent — alimentation a verifier)");
      return true;
    }
    return false;
  }();
  {
    bool wantSocial = ((uiMode == UI_ANIM && ACTIVE[slot] == 8) ||
                       uiMode == UI_PROX) &&
                      !socialBlocked;
    socialProbeOnly = (uiMode == UI_PROX);
    if (wantSocial != socialOn)
    {
      if (wantSocial)
      {
        prefs.putUChar("socboot", 1); // arme : si on meurt ici, bloque au boot
        socialStart();
        socialArmMs = now ? now : 1;
      }
      else
        socialStop();
    }
    if (socialOn && socialArmMs && now - socialArmMs > 8000)
    {
      prefs.putUChar("socboot", 0); // 8 s stables : la radio passe sur cette carte
      socialArmMs = 0;
    }
    if (socialOn)
      socialLoop(now);
  }

  // ---- bouton BOOT seul = navigation complete (pratique au banc, sans
  // boutons cables) : court = suivant · maintenu >= 0,5 s = bouton central
  // (le "saut" des jeux part au franchissement du seuil, la validation menu
  // au relachement) · maintenu >= 2 s = extinction, comme le central long.
  // Sans effet sur les vrais boutons, qui restent prioritaires.
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
      autoPressMs = now; // "press central" synthetique (saut/action des jeux)
    }
    if (down && bootDownAt && !bootOffFired && now - bootDownAt >= 2000 &&
        now > 4000)
    {
      bootOffFired = true;
      powerOff(); // central long = extinction
    }
    if (!down && bootDownAt)
    {
      uint32_t held = now - bootDownAt;
      bootDownAt = 0;
      if (held < 500)
        btnNextFlag = true; // court : suivant (comportement historique)
      else if (held < 2000)
        btnAutoShort = true; // long : central court (menu / valider)
    }
  }

  // ---- boutons : gauche/droite (anti-rebond 300 ms) + central court/long ----
  bool navNext = false, navPrev = false;
  if ((btnNextFlag || btnPrevFlag) && now - lastBtnMs > 300)
  {
    lastBtnMs = now;
    navNext = btnNextFlag;
    navPrev = !navNext && btnPrevFlag;
  }
  btnNextFlag = btnPrevFlag = false;

  // appui LONG (2 s) sur le central : extinction (ignore les 4 s apres boot)
  if (now > 4000 && autoPressMs && now - autoPressMs > 2000 && digitalRead(BTN_AUTO) == LOW)
    powerOff();
  bool autoShort = btnAutoShort;
  btnAutoShort = false;

  updateBattery(now);

  // Horloge de conf : decompose l'heure RTC (synchronisee par la webapp
  // Draw) en jour de conf + minutes. Heure jamais synchronisee -> NOW cache.
  {
    static uint32_t clockLast = 0;
    if (now - clockLast >= 10000 || clockLast == 0)
    {
      clockLast = now;
      time_t t = time(nullptr);
      if (t < 1750000000) // avant mi-2025 : RTC jamais reglee
      {
        uiNowMin = -1;
        uiNowDay = 0;
      }
      else
      {
        struct tm tmv;
        gmtime_r(&t, &tmv); // l'heure stockee est deja l'heure LOCALE
        uiNowDay = (tmv.tm_year == 126 && tmv.tm_mon == 8)
                       ? (tmv.tm_mday == 10 ? 1 : (tmv.tm_mday == 11 ? 2 : 0))
                       : 0;
        uiNowMin = tmv.tm_hour * 60 + tmv.tm_min;
      }
    }
  }

  // Protection batterie : sous 3.20 V soutenus 10 s (hors charge), extinction
  // propre — mieux que d'attendre la coupure brutale du PCM vers 2.5 V, qui
  // use la LiPo. (Verifie 2026-08-07 : jauge juste a 10 mV pres.)
  // Seuil RELEVE de 3.02 a 3.20 V (revue 2026-08-15, apres la mort d'une
  // cellule de proto en decharge profonde) : on sacrifie ~2 min d'autonomie
  // pour laisser ~8-10 % de reserve reelle — la veille du boost (~0.3 mA,
  // jamais coupee) mange cette reserve APRES l'extinction, et une reserve
  // double donne des semaines de marge avant la zone dangereuse (<2.5 V).
  static uint32_t lowSince = 0;
  if (batPct == 0 && batMvRaw > 0 && batMvRaw < 3200 && !batCharging)
  {
    if (!lowSince)
      lowSince = now;
    else if (now - lowSince > 10000)
    {
      Serial0.println("batterie critique (<3.20 V) : extinction de protection");
      powerOff();
    }
  }
  else
    lowSince = 0;

  if (uiMode == UI_HOME)
  {
    // menu principal a bulles : haut/bas = categorie, central = entrer
    if (navNext)
      uiHomeNav(1);
    if (navPrev)
      uiHomeNav(-1);
    if (autoShort)
    {
      menuCat = uiHomeFocus;
      menuSel = 0;
      uiMode = UI_MENU;
      Serial0.printf("menu : categorie %s\n", UI_CAT_NAMES[menuCat]);
    }
    bool moving = (uiMode == UI_HOME)
                      ? uiDrawHome(dt, batPct, batCharging)
                      : (uiMode == UI_SCHED
                             ? uiDrawSchedule(schedIdx)
                             : uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging));
    // pas d'attente TE sur les menus (tearing peu visible) et flush seulement
    // si quelque chose a change : reactivite maximale, bus SPI au repos sinon
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
    // gauche/droite : -1/+1 degre ; centre : sauve en NVS et retour au menu
    if (navPrev && uiScreenRot > -15)
      uiScreenRot--;
    if (navNext && uiScreenRot < 15)
      uiScreenRot++;
    static int rotShown = -99;
    if (autoShort)
    {
      prefs.putChar("rotDeg", (int8_t)uiScreenRot);
      Serial0.printf("rotation ecran sauvee : %+d deg\n", uiScreenRot);
      rotShown = -99;
      setMenuShown = -1; // Rotate vit dans Settings : retour au sous-menu
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
    // code d'acces des Settings : gauche/droite = chiffre -/+, centre =
    // valider le chiffre ; apres le 5e, bon code -> choix d'avatar, mauvais
    // code -> flash d'erreur puis retour au menu
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
        uiMode = UI_PROX; // la radio passe en mode sonde (voir loop)
      }
      else if (setMenuSel == 2) // Rotate screen
      {
        uiMode = UI_ROT;
      }
      else if (setMenuSel == 3) // OTA flash mode
      {
        Serial0.println("settings : redemarrage en mode flash OTA");
        otaRequest = OTA_MAGIC;
        delay(50);
        esp_restart();
      }
      else if (setMenuSel == 4) // Batt : ecran de calibration de la jauge
      {
        uiMode = UI_VCAL;
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
    // reglage de proximite : jauge live du badge le plus proche (radio en
    // mode sonde), gauche/droite = niveau, centre = sauver. BOUCLE aux
    // extremites : indispensable avec le seul bouton BOOT (pas de "prev")
    if (navNext)
      proxLevel = (proxLevel + 1) % 4;
    if (navPrev)
      proxLevel = (proxLevel + 3) % 4;
    if (autoShort)
    {
      socialRssiNear = UI_PROX_LEVELS[proxLevel];
      prefs.putChar("prox", socialRssiNear);
      Serial0.printf("proximite sauvee : %s (%d dBm)\n",
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

  if (uiMode == UI_VCAL)
  {
    // calibration de la jauge : gauche/droite = -/+0.3 % sur le facteur du
    // pont, l'ecran suit en direct ; centre = sauver en NVS et retour
    if (navNext && vbatCal < 1100)
      vbatCal += 3;
    if (navPrev && vbatCal > 900)
      vbatCal -= 3;
    if (autoShort)
    {
      prefs.putShort("vcal", vbatCal);
      Serial0.printf("calibration jauge sauvee : %d/1000\n", (int)vbatCal);
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
    // choix de l'avatar/personne du badge : gauche/droite = precedent/
    // suivant (preview sphere + visage), centre = sauver en NVS et sortir
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
      if (g_buddyCustom) // choisir un avatar de la table desactive le custom
      {
        g_buddyCustom = false;
        prefs.putUChar("bcust", 0);
      }
      // l'avatar choisi devient l'identite du badge : nom pre-rempli dans
      // Setup (modifiable ensuite) et SSID badge-<Nom> immediats
      snprintf(qrName, sizeof(qrName), "%s", AVATARS[setSel].name);
      prefs.putString("bname", qrName);
      irDirtyMask = 0xFFFFFFFFu; // toutes les frames idle a refaire
      g_ballDirty = true;    // + le sprite de boule (snake/DVD/jeux)
      if (setSpr)
      {
        free(setSpr);
        setSpr = nullptr;
      }
      Serial0.printf("avatar sauve : %d (%s)\n", setSel, AVATARS[setSel].name);
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
      g_avatarFaceIdx = (uint8_t)setSel; // le visage suit la preview
      g_faceForce = setSel; // ...meme si un buddy custom est actif
      drawIdleFaceLook(CX, CY - 26, 78, 0, 0, 0, 1.0f);
      badgeFlush();
      fpsCount++;
    }
    return;
  }

  if (uiMode == UI_SCHED)
  {
    // programme : gauche/droite = event precedent/suivant, central = retour
    if (navNext)
      schedIdx = (schedIdx + 1) % UI_NEVENTS;
    if (navPrev)
      schedIdx = (schedIdx + UI_NEVENTS - 1) % UI_NEVENTS;
    if (autoShort)
      uiMode = UI_MENU; // retour a la liste Meet
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
        return; // sans ce return, le menu se redessine par-dessus l'ecran d'infos
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
        autoCycle = !autoCycle; // bascule sans sortir
        break;
      case UIA_SCHED:
        uiMode = UI_SCHED; // le programme (entree de la categorie Meet)
        break;
      case UIA_ROT:
        uiMode = UI_ROT; // calibration de la rotation ecran
        break;
      case UIA_SETTINGS:
        memset(pinDigits, 0, sizeof(pinDigits));
        pinPos = 0;
        pinError = false;
        pinRedraw = true;
        uiMode = UI_PIN; // settings proteges par code (avatar du badge)
        break;
      case UIA_OTA:
        Serial0.println("menu : redemarrage en mode flash OTA");
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
    if (autoShort) // central : retour menu, WiFi coupe
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
      // telephone connecte : preview animee en continu (buddy ou QR live)
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
      delay(2); // laisse respirer le WiFi
    return;
  }

  if (uiMode == UI_MET)
  {
    // liste des rencontres : prev/next = defilement, central = retour
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
    // leaderboard : prev/next = jeu precedent/suivant (boucle, BOOT-friendly),
    // central = retour
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
    // QR statique + buddy anime au centre ; central = retour menu
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
    if (autoShort) // central : retour menu, WiFi coupe
    {
      drawModeExit();
      uiMode = UI_MENU;
      uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
      waitTE();
      badgeFlush();
      fpsCount++;
      return;
    }
    drawAnimTick(now); // fait vivre les pinceaux animes (glitter, iris...)
    if (drawDirty)
    {
      // flush partiel de la zone modifiee, sans attente TE : latence minimale
      drawFlushDirty();
      fpsCount++;
    }
    else
      delay(2); // rien a afficher : laisse respirer le WiFi
    return;
  }

  if (uiMode >= UI_SNAKE)
  {
    int gi = uiMode - UI_SNAKE;
    bool over = gameIsOver(gi);
    // detection "press-down" du bouton central (reactif, pour tirer/sauter)
    static uint32_t lastAutoPressSeen = 0;
    bool centerDown = false;
    uint32_t ap = autoPressMs;
    if (ap && ap != lastAutoPressSeen)
    {
      lastAutoPressSeen = ap;
      centerDown = true;
    }
    // sortie universelle : gauche + droite maintenus 0.8 s
    static uint32_t bothHold = 0;
    if (digitalRead(BTN_PREV) == LOW && digitalRead(BTN_NEXT) == LOW)
    {
      if (!bothHold)
        bothHold = now;
    }
    else
      bothHold = 0;
    bool quit = (bothHold && now - bothHold > 800);
    // central : action de jeu OU retour menu selon le jeu (toujours menu si game over)
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
      gameReset(gi); // rejouer — et on avale l'appui pour ne pas le passer au jeu
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

  // ---- mode animations ----
  if (navNext)
  {
    slot = (slot + 1) % NACTIVE;
    slotStartMs = now;
  }
  if (navPrev)
  {
    slot = (slot + NACTIVE - 1) % NACTIVE;
    slotStartMs = now;
  }
  if (autoShort)
  {
    uiMode = UI_HOME; // menu principal a bulles
    uiHomeReset();
    Serial0.println("menu : ouverture");
  }
  if (autoCycle && now - slotStartMs >= ANIM_DURATION_MS)
  {
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
                                            "photo2", "photo3", "warp", "solar"};
    Serial0.printf("animation : %s\n", names[anim]);
  }
  float t = (now - animStartMs) / 1000.0f; // temps local a l'animation

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
  }
  if (anim == 8) // Conf Buddy : reaction "un ami est la" par-dessus l'anim
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
