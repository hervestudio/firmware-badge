// Ecran "QR Code" (Meet > QR Code) — PARTAGE firmware / emulateur.
// Rend le QR de l'URL configuree via More > Setup : fond blanc, modules
// encre #211C3B, coeurs des mires lavande (design du sticker), et le Conf
// Buddy ANIME au centre dans un medaillon blanc (~10 % de la surface :
// l'ECC HIGH tolere 30 % de codewords abimes, large marge de lecture).
// Depend de : canvas, rgb565, RGB565_WHITE, W/H/CX/CY, qrcodegen,
// dvdGenSprite/dvdBlit, avatarDrawFace/avatarDrawExtras, PAL_RAINBOW/PAL_N,
// mdPrint/mdTextW, sinf/cosf/fmodf, free.
#pragma once
#include "qrcodegen.h"
#include "emoji_text.h" // pilule message de la carte : Dingos Medium + emojis

#define QR_URL_MAX 96
#define QR_NAME_MAX 24
static char qrUrl[QR_URL_MAX] = "https://threejs.paris";
static char qrName[QR_NAME_MAX] = "";
static char qrCompany[28] = "";
static char qrMsg[48] = ""; // peut contenir des emojis (UTF-8, emoji_text.h)

static uint8_t qrModules[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
static bool qrValid = false;
static uint16_t *qrSpr = nullptr; // sprite sphere du buddy (dvdGenSprite)

// (Re)genere le QR et le sprite du buddy — a appeler a l'entree de l'ecran
// (l'URL ou le buddy ont pu changer via Setup entre deux visites)
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

// Carte d'identite du badge (design Figma) : buddy anime + pilule message
// blanche chevauchant la sphere + NOM en Dingos titre + entreprise en Bebas.
// Utilisee par la preview du Setup (et plus tard l'ecran social). Depend en
// plus de mtPrint/mtTextW et bbPrint/bbTextW (a inclure avant).
static void qrBuddyAnim(float cx, float cy, float fr, float t,
                        const uint16_t *spr);
static void badgeCardDraw(float t, const uint16_t *spr)
{
  canvas->fillScreen(RGB565_BLACK);
  qrBuddyAnim(CX, 150, 60.0f, t, spr);
  if (qrMsg[0])
  {
    // pilule blanche accrochee en haut a droite de la sphere
    int tw = mxTextW(qrMsg), ph = 30, pw = tw + 26;
    int x0 = CX + 14, y0 = 84;
    if (x0 + pw > 344)
      x0 = 344 - pw;
    canvas->fillRect(x0, y0, pw, ph, RGB565_WHITE);
    canvas->fillCircle(x0, y0 + ph / 2, ph / 2, RGB565_WHITE);
    canvas->fillCircle(x0 + pw, y0 + ph / 2, ph / 2, RGB565_WHITE);
    mxPrint(x0 + 13, y0 + 8, qrMsg, rgb565(0x21, 0x1C, 0x3B));
  }
  char up[28];
  if (qrName[0])
  {
    int i = 0;
    for (; qrName[i] && i < 27; i++)
      up[i] = toupper((unsigned char)qrName[i]);
    up[i] = 0;
    mtPrint(CX - mtTextW(up) / 2, 238, up, RGB565_WHITE);
  }
  if (qrCompany[0])
  {
    int i = 0;
    for (; qrCompany[i] && i < 27; i++)
      up[i] = toupper((unsigned char)qrCompany[i]);
    up[i] = 0;
    bbPrint(CX - bbTextW(up) / 2, 284, up, rgb565(198, 196, 214));
  }
}

// Buddy anime (respiration, regard qui se promene, clignements) — utilise
// par l'ecran QR (medaillon) et la preview live du mode Setup
static void qrBuddyAnim(float cx, float cy, float fr, float t,
                        const uint16_t *spr)
{
  float bob = sinf(t * 1.6f) * fr * 0.055f;
  float ph = fmodf(t, 3.7f); // clignement bref toutes les ~3,7 s
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

// Une frame de l'ecran QR (t en secondes) : QR INVERSE plein ecran — fond
// noir, modules blancs, coeurs des mires lavande (iOS et les lecteurs
// modernes lisent les QR inverses ; verifie au decodage sur captures).
// building = QR "en construction" (modules aleatoires qui se remplissent,
// pendant la saisie de l'URL dans Setup).
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
    // placeholder "en construction" : mires reelles + modules aleatoires qui
    // apparaissent progressivement (nouveau tirage a chaque cycle)
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
        // coeur 3x3 des trois mires en lavande, comme sur le sticker
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

  // medaillon noir (efface les modules) + buddy anime au centre
  canvas->fillCircle(CX, CY, 40, RGB565_BLACK);
  qrBuddyAnim(CX, CY, 34.0f, t, qrSpr);

  // nom du speaker (configure via Setup) sous le QR
  if (qrName[0])
  {
    int tw = mdTextW(qrName);
    if (tw < 200)
      mdPrint(CX - tw / 2, 324, qrName, RGB565_WHITE);
  }
}
