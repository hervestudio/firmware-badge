// "QR Code" screen (Meet > QR Code) - SHARED firmware / emulator.
// Renders the QR of the URL configured via More > Setup: white background,
// ink #211C3B modules, lavender finder cores (sticker design), and the
// ANIMATED Conf Buddy centered in a white medallion (~10% of the area:
// ECC HIGH tolerates 30% damaged codewords, wide readability margin).
// Depends on: canvas, rgb565, RGB565_WHITE, W/H/CX/CY, qrcodegen,
// dvdGenSprite/dvdBlit, avatarDrawFace/avatarDrawExtras, PAL_RAINBOW/PAL_N,
// mdPrint/mdTextW, sinf/cosf/fmodf, free.
#pragma once
#include "qrcodegen.h"
#include "emoji_text.h" // card message pill: Dingos Medium + emojis

#define QR_URL_MAX 96
#define QR_NAME_MAX 24
static char qrUrl[QR_URL_MAX] = "https://threejs.paris";
static char qrName[QR_NAME_MAX] = "";
static char qrCompany[28] = "";
static char qrMsg[48] = ""; // may contain emojis (UTF-8, emoji_text.h)

// Badge SSID: unique per person - "badge-<Name>" once the name has been
// configured via Setup (sanitized for the SSID: alphanumeric and dashes),
// otherwise the generic name. Used by the Draw / Setup / OTA WiFi APs.
static const char *badgeSsid()
{
  static char ssid[33];
  char nm[21];
  int o = 0;
  for (int i = 0; qrName[i] && o < 20; i++)
  {
    char c = qrName[i];
    if (isalnum((unsigned char)c))
      nm[o++] = c;
    else if (c == ' ' || c == '-' || c == '_')
      nm[o++] = '-';
  }
  nm[o] = 0;
  if (!o)
    return "badge-threejs";
  snprintf(ssid, sizeof(ssid), "badge-%s", nm);
  return ssid;
}

// Small utility QR on a rounded white card, centered at (cx, cy) -
// used by the Setup/Draw waiting screens for the WiFi QR (camera scan
// -> the phone joins the AP -> the captive portal opens the page)
static void qrMiniDraw(int cx, int cy, int target, const char *text)
{
  static uint8_t mini[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
  uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
  if (!qrcodegen_encodeText(text, tmp, mini, qrcodegen_Ecc_MEDIUM, 1, 6,
                            qrcodegen_Mask_AUTO, true))
    return;
  int size = qrcodegen_getSize(mini);
  int scale = target / size;
  if (scale < 1)
    scale = 1;
  int px = size * scale, x0 = cx - px / 2, y0 = cy - px / 2;
  const int m = 8, rr = 8; // quiet zone + rounded corners
  canvas->fillRect(x0 - m, y0 - m - rr, px + 2 * m, px + 2 * m + 2 * rr,
                   RGB565_WHITE);
  canvas->fillRect(x0 - m - rr, y0 - m, px + 2 * m + 2 * rr, px + 2 * m,
                   RGB565_WHITE);
  canvas->fillCircle(x0 - m, y0 - m, rr, RGB565_WHITE);
  canvas->fillCircle(x0 + px + m, y0 - m, rr, RGB565_WHITE);
  canvas->fillCircle(x0 - m, y0 + px + m, rr, RGB565_WHITE);
  canvas->fillCircle(x0 + px + m, y0 + px + m, rr, RGB565_WHITE);
  const uint16_t ink = rgb565(0x21, 0x1C, 0x3B);
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++)
      if (qrcodegen_getModule(mini, x, y))
        canvas->fillRect(x0 + x * scale, y0 + y * scale, scale, scale, ink);
}

static uint8_t qrModules[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
static bool qrValid = false;
static uint16_t *qrSpr = nullptr; // buddy sphere sprite (dvdGenSprite)

// (Re)generates the QR and the buddy sprite - call on screen entry
// (the URL or buddy may have changed via Setup between two visits)
static void qrScreenPrepare()
{
  uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
  qrValid = qrcodegen_encodeText(qrUrl, tmp, qrModules, qrcodegen_Ecc_HIGH,
                                 1, 8, qrcodegen_Mask_AUTO, true);
  if (qrSpr)
    free(qrSpr);
  const AvatarDef &av = g_buddyCustom ? g_buddyCustomDef : AVATARS[g_avatarIdx];
  qrSpr = dvdGenSprite(PAL_RAINBOW, PAL_N, av.hue, av.sat);
}

static void qrScreenRelease()
{
  if (qrSpr)
  {
    free(qrSpr);
    qrSpr = nullptr;
  }
}

// Badge identity card (Figma design): animated buddy + white message pill
// overlapping the sphere + NAME in Dingos title + company in Bebas.
// Used by the Setup preview (and later the social screen). Also depends
// on mtPrint/mtTextW and bbPrint/bbTextW (include beforehand).
static void qrBuddyAnim(float cx, float cy, float fr, float t,
                        const uint16_t *spr);
static void badgeCardDraw(float t, const uint16_t *spr)
{
  canvas->fillScreen(RGB565_BLACK);
  qrBuddyAnim(CX, 150, 72.0f, t, spr);
  if (qrMsg[0])
  {
    // white pill hooked to the top right of the sphere, 2 px #3E3E3E
    // outline (stroke = slightly larger grey pill underneath), tight
    // horizontal padding (proportions of the Figma mockup).
    // "Never clipped by the round screen" guarantee: if the pill is too
    // wide for the circle's chord at y0=84, it steps down (the chord
    // widens toward the middle); remaining extreme case -> text elided
    // with "..".
    const int ph = 28, pad = 10;
    char msg[sizeof(qrMsg) + 2];
    snprintf(msg, sizeof(msg), "%s", qrMsg);
    int tw = mxTextW(msg), total = tw + 2 * pad; // visual pill width
    if (total < ph)
      total = ph;
    int y0 = 84, half = 0;
    for (;;)
    {
      int dy = 181 - y0; // the top edge is closest to the screen edge
      half = (int)sqrtf(180.0f * 180.0f - (float)dy * dy);
      if (total + 8 <= 2 * half || y0 >= 128)
        break;
      y0 += 8;
    }
    while (total + 8 > 2 * half && strlen(msg) > 3)
    {
      msg[strlen(msg) - 3] = 0; // drops one character (UTF-8 approx ok:
      strcat(msg, "..");        // cut generously, then re-measure)
      tw = mxTextW(msg);
      total = tw + 2 * pad;
    }
    int xmin = 181 - half + 4, xmax = 181 + half - 4;
    int L = CX + 44; // visual left edge of the pill
    if (L + total > xmax)
      L = xmax - total;
    if (L < xmin)
      L = xmin;
    const int r = ph / 2, cy = y0 + r;
    const int rx0 = L + r, rw = total - ph; // rect between the round ends
    const uint16_t stroke = rgb565(0x3E, 0x3E, 0x3E);
    canvas->fillRect(rx0, y0 - 2, rw, ph + 4, stroke);
    canvas->fillCircle(rx0, cy, r + 2, stroke);
    canvas->fillCircle(rx0 + rw, cy, r + 2, stroke);
    canvas->fillRect(rx0, y0, rw, ph, RGB565_WHITE);
    canvas->fillCircle(rx0, cy, r, RGB565_WHITE);
    canvas->fillCircle(rx0 + rw, cy, r, RGB565_WHITE);
    mxPrint(L + pad, y0 + 7, msg, rgb565(0x21, 0x1C, 0x3B));
  }
  char up[28];
  if (qrName[0])
  {
    int i = 0;
    for (; qrName[i] && i < 27; i++)
      up[i] = toupper((unsigned char)qrName[i]);
    up[i] = 0;
    mtPrint(CX - mtTextW(up) / 2, 252, up, RGB565_WHITE);
  }
  if (qrCompany[0])
  {
    int i = 0;
    for (; qrCompany[i] && i < 27; i++)
      up[i] = toupper((unsigned char)qrCompany[i]);
    up[i] = 0;
    bbPrint(CX - bbTextW(up) / 2, 286, up, rgb565(198, 196, 214));
  }
}

// Animated buddy (breathing, wandering gaze, blinks) - used by the QR
// screen (medallion) and the Setup mode live preview
static void qrBuddyAnim(float cx, float cy, float fr, float t,
                        const uint16_t *spr)
{
  float bob = sinf(t * 1.6f) * fr * 0.055f;
  float ph = fmodf(t, 3.7f); // brief blink every ~3.7 s
  float openness = 1.0f;
  if (ph > 3.45f)
  {
    openness = fabsf(ph - 3.575f) / 0.125f;
    if (openness > 1.0f)
      openness = 1.0f;
    if (openness < 0.15f)
      openness = 0.15f;
  }
  float theta = sinf(t * 0.55f) * 0.45f * 30.0f * (float)PI / 180.0f;
  float breathe = sinf(t * 1.8f) * fr * 0.011f;
  if (spr)
    dvdBlit(spr, (int)cx, (int)(cy + bob), fr, 255);
  avatarDrawFace(cx, cy + bob, fr, breathe, sinf(t * 0.8f) * fr * 0.055f,
                 cosf(theta), sinf(theta), openness, rgb565(39, 39, 39));
  avatarDrawExtras(cx, cy + bob, fr, breathe);
}

// One frame of the QR screen (t in seconds): full-screen INVERTED QR -
// black background, white modules, lavender finder cores (iOS and modern
// readers decode inverted QRs; verified by decoding screen captures).
// building = "under construction" QR (random modules filling in, while
// the URL is being typed in Setup).
static void qrScreenDraw(float t, bool building = false)
{
  const uint16_t lav = rgb565(0x9d, 0x97, 0xed);
  canvas->fillScreen(RGB565_BLACK);

  int size = (!building && qrValid) ? qrcodegen_getSize(qrModules) : 25;
  int scale = 232 / size;
  if (scale < 1)
    scale = 1;
  int px = size * scale, x0 = CX - px / 2, y0 = CY - px / 2;

  if (building)
  {
    // "under construction" placeholder: real finder patterns + random
    // modules appearing progressively (new draw each cycle)
    unsigned seed = (unsigned)(t / 2.6f) * 2654435761u + 12345u;
    float prog = fmodf(t, 2.6f) / 2.2f;
    for (int y = 0; y < size; y++)
      for (int x = 0; x < size; x++)
      {
        bool finder = (x < 7 && y < 7) || (x >= size - 7 && y < 7) ||
                      (x < 7 && y >= size - 7);
        if (finder)
        {
          int fx = x >= size - 7 ? x - (size - 7) : x, fy = y >= size - 7 ? y - (size - 7) : y;
          bool core = fx >= 2 && fx <= 4 && fy >= 2 && fy <= 4;
          if ((fx == 0 || fx == 6 || fy == 0 || fy == 6) || core)
            canvas->fillRect(x0 + x * scale, y0 + y * scale, scale, scale,
                             core ? lav : RGB565_WHITE);
          continue;
        }
        unsigned h = ((unsigned)(x * 73856093) ^ (unsigned)(y * 19349663) ^ seed);
        h = (h ^ (h >> 13)) * 2246822519u;
        h ^= h >> 16;
        if ((h >> 8 & 127) / 128.0f < prog && (h & 7) < 4)
          canvas->fillRect(x0 + x * scale, y0 + y * scale, scale, scale,
                           RGB565_WHITE);
      }
  }
  else if (qrValid)
  {
    for (int y = 0; y < size; y++)
      for (int x = 0; x < size; x++)
      {
        if (!qrcodegen_getModule(qrModules, x, y))
          continue;
        // 3x3 core of the three finder patterns in lavender, like the sticker
        bool core = (x >= 2 && x <= 4 && y >= 2 && y <= 4) ||
                    (x >= size - 5 && x <= size - 3 && y >= 2 && y <= 4) ||
                    (x >= 2 && x <= 4 && y >= size - 5 && y <= size - 3);
        canvas->fillRect(x0 + x * scale, y0 + y * scale, scale, scale,
                         core ? lav : RGB565_WHITE);
      }
  }
  else
  {
    canvas->setTextSize(2);
    canvas->setTextColor(RGB565_WHITE);
    canvas->setCursor(CX - 66, CY - 8);
    canvas->print("bad URL");
  }

  // black medallion (erases the modules) + animated buddy in the center
  canvas->fillCircle(CX, CY, 40, RGB565_BLACK);
  qrBuddyAnim(CX, CY, 34.0f, t, qrSpr);

  // speaker name (configured via Setup) below the QR
  if (qrName[0])
  {
    int tw = mdTextW(qrName);
    if (tw < 200)
      mdPrint(CX - tw / 2, 324, qrName, RGB565_WHITE);
  }
}
