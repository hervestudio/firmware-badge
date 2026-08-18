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

#define QR_URL_MAX 96
#define QR_NAME_MAX 24
static char qrUrl[QR_URL_MAX] = "https://threejs.paris";
static char qrName[QR_NAME_MAX] = "";

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

// Une frame de l'ecran QR (t en secondes) : QR statique + buddy anime
// (respiration, regard qui se promene, clignements)
static void qrScreenDraw(float t)
{
  const uint16_t ink = rgb565(0x21, 0x1C, 0x3B);
  const uint16_t lav = rgb565(0x9d, 0x97, 0xed);
  canvas->fillScreen(RGB565_WHITE);

  if (qrValid)
  {
    int size = qrcodegen_getSize(qrModules);
    int scale = 240 / size;
    if (scale < 1)
      scale = 1;
    int px = size * scale, x0 = CX - px / 2, y0 = CY - px / 2;
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
                         core ? lav : ink);
      }
  }
  else
  {
    canvas->setTextSize(2);
    canvas->setTextColor(ink);
    canvas->setCursor(CX - 90, CY - 8);
    canvas->print("URL invalide");
  }

  // medaillon blanc + buddy anime au centre
  float bob = sinf(t * 1.6f) * 2.0f;
  canvas->fillCircle(CX, CY, 42, RGB565_WHITE);
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
  float breathe = sinf(t * 1.8f) * 0.4f;
  if (qrSpr)
    dvdBlit(qrSpr, CX, (int)(CY + bob), 36.0f, 255);
  avatarDrawFace(CX, CY + bob, 36.0f, breathe, sinf(t * 0.8f) * 2.0f,
                 cosf(theta), sinf(theta), openness, rgb565(39, 39, 39));
  avatarDrawExtras(CX, CY + bob, 36.0f, breathe);

  // nom du speaker (configure via Setup) sous le QR, si la place le permet
  if (qrName[0])
  {
    int tw = mdTextW(qrName);
    if (tw < 200)
      mdPrint(CX - tw / 2, 322, qrName, ink);
  }
}
