// Mode Setup (More > Setup) : parcours de configuration du badge DEPUIS LE
// TELEPHONE. Le badge cree le meme AP Wi-Fi que Draw/OTA, sert la webapp
// 3 etapes (setup_page.h, gzippee) sur http://192.168.4.1 et recoit les
// reglages en WebSocket texte (port 81) :
//   badge -> tel  : J{"name":..,"url":..,"hue":H,"sat":S*100,"face":F,"cust":0/1}
//   tel  -> badge : N<nom>  |  A<hue>,<sat100>,<face>  |  R (revenir a
//                   l'avatar de la table)  |  U<url>
// Chaque message est SAUVE en NVS immediatement et le badge affiche une
// preview live du buddy. Le QR lui-meme se regarde dans Meet > QR Code.
// Suppose definis avant inclusion : canvas, W/H/CX/CY, rgb565, Serial0,
// OTA_SSID/OTA_PASS, prefs, qrUrl/qrName (qr_screen.h), dvdGenSprite/dvdBlit,
// avatarDrawFace/avatarDrawExtras, g_buddyCustom/g_buddyCustomDef,
// irDirtyFrom, mdPrint/mdTextW.
#pragma once
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "setup_page.h"

static WebServer *setupHttp = nullptr;
static WebSocketsServer *setupWs = nullptr;
static bool setupRedraw = false; // l'etat a change -> repeindre la preview
static uint8_t setupClients = 0;
static uint16_t *setupSpr = nullptr; // sprite de preview du buddy (cache)
static uint8_t setupStep = 0;        // etape affichee sur le telephone (0..2)
static bool setupBuilding = false;   // URL en cours de saisie -> QR "chantier"
static bool setupQrDirty = true;     // l'URL a change -> re-encoder le QR

// Ecran d'infos de connexion (tant que le telephone n'est pas la)
static void setupDrawScreen()
{
  canvas->fillScreen(RGB565_BLACK);
  canvas->setTextColor(rgb565(0xfb, 0xd9, 0x75));
  canvas->setTextSize(3);
  canvas->setCursor(CX - 90, 40);
  canvas->print("SETUP");
  canvas->setTextSize(2);
  canvas->setTextColor(RGB565_WHITE);
  canvas->setCursor(70, 120);
  canvas->printf("WiFi %s", badgeSsid());
  canvas->setCursor(70, 150);
  canvas->printf("Pass %s", OTA_PASS);
  canvas->setTextColor(rgb565(255, 213, 48));
  canvas->setCursor(70, 185);
  canvas->print("http://192.168.4.1");
  canvas->setTextColor(rgb565(130, 130, 130));
  canvas->setCursor(CX - 96, 250);
  canvas->print("center: exit");
}

// Preview live (telephone connecte), animee frame par frame : etapes 1-2 =
// carte d'identite (buddy + message + nom + entreprise) ; etape 3 (QR) =
// le QR en direct, version "en construction" pendant la saisie de l'URL
static void setupDrawLive(float t)
{
  if (setupStep == 2)
  {
    if (setupQrDirty) // (re)genere aussi le sprite du buddy du medaillon
    {
      qrScreenPrepare();
      setupQrDirty = false;
    }
    qrScreenDraw(t, setupBuilding);
    return;
  }
  // sprite regenere seulement quand les parametres changent
  static int lastHue = -1000, lastSat = -1, lastCust = -1, lastAv = -1;
  const AvatarDef &av = g_buddyCustom ? g_buddyCustomDef : AVATARS[g_avatarIdx];
  int sat100 = (int)(av.sat * 100 + 0.5f);
  if (!setupSpr || av.hue != lastHue || sat100 != lastSat ||
      (int)g_buddyCustom != lastCust || (int)g_avatarIdx != lastAv)
  {
    if (setupSpr)
      free(setupSpr);
    setupSpr = dvdGenSprite(PAL_RAINBOW, PAL_N, av.hue, av.sat);
    lastHue = av.hue;
    lastSat = sat100;
    lastCust = (int)g_buddyCustom;
    lastAv = (int)g_avatarIdx;
  }
  badgeCardDraw(t, setupSpr);
}

static void setupSendState(uint8_t num)
{
  char msg[330];
  snprintf(msg, sizeof(msg),
           "J{\"name\":\"%s\",\"comp\":\"%s\",\"msg\":\"%s\",\"url\":\"%s\","
           "\"hue\":%d,\"sat\":%d,\"face\":%d,\"cust\":%d}",
           qrName, qrCompany, qrMsg, qrUrl, (int)g_buddyCustomDef.hue,
           (int)(g_buddyCustomDef.sat * 100 + 0.5f),
           (int)g_buddyCustomDef.face, g_buddyCustom ? 1 : 0);
  setupWs->sendTXT(num, msg);
}

// copie une chaine recue en filtrant les guillemets (JSON d'etat) et les
// controles ; garde l'UTF-8 tel quel (emojis du message)
static void setupCopyStr(char *dst, size_t cap, const uint8_t *src, size_t len)
{
  size_t o = 0;
  for (size_t i = 0; i < len && o + 1 < cap; i++)
    if (src[i] >= 32 && src[i] != '"' && src[i] != '\\')
      dst[o++] = (char)src[i];
  dst[o] = 0;
}

// variante nom/entreprise : translittere les accents latins vers l'ASCII
// (les polices Dingos/Bebas du badge couvrent 32..126)
static void setupCopyAscii(char *dst, size_t cap, const uint8_t *src, size_t len)
{
  static const char *FOLD = // Latin-1 0xC0..0xFF
      "AAAAAAECEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaeceeeeiiiionooooo/ouuuuypy";
  size_t o = 0;
  for (size_t i = 0; i < len && o + 1 < cap; i++)
  {
    uint8_t c = src[i];
    if (c >= 32 && c < 127 && c != '"' && c != '\\')
      dst[o++] = (char)c;
    else if (c == 0xC3 && i + 1 < len) // UTF-8 Latin-1 supplement
    {
      uint8_t d = 0xC0 + (src[++i] & 63);
      if (d >= 0xC0)
        dst[o++] = FOLD[d - 0xC0];
    }
  }
  dst[o] = 0;
}

static void setupWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len)
{
  switch (type)
  {
  case WStype_CONNECTED:
    setupClients++;
    setupSendState(num);
    setupRedraw = true;
    Serial0.printf("setup : client %u connecte\n", num);
    break;
  case WStype_DISCONNECTED:
    if (setupClients)
      setupClients--;
    setupRedraw = true;
    Serial0.printf("setup : client %u parti\n", num);
    break;
  case WStype_TEXT:
    if (!len)
      break;
    switch (payload[0])
    {
    case 'N': // nom du speaker
      setupCopyAscii(qrName, sizeof(qrName), payload + 1, len - 1);
      prefs.putString("bname", qrName);
      Serial0.printf("setup : nom = \"%s\"\n", qrName);
      break;
    case 'C': // entreprise
      setupCopyAscii(qrCompany, sizeof(qrCompany), payload + 1, len - 1);
      prefs.putString("bcomp", qrCompany);
      Serial0.printf("setup : entreprise = \"%s\"\n", qrCompany);
      break;
    case 'M': // message (pilule, emojis bienvenus)
      setupCopyStr(qrMsg, sizeof(qrMsg), payload + 1, len - 1);
      prefs.putString("bmsg", qrMsg);
      Serial0.printf("setup : message = \"%s\"\n", qrMsg);
      break;
    case 'U': // URL du QR code (validee cote telephone)
      setupCopyStr(qrUrl, sizeof(qrUrl), payload + 1, len - 1);
      if (!qrUrl[0])
        snprintf(qrUrl, sizeof(qrUrl), "https://threejs.paris");
      prefs.putString("qrurl", qrUrl);
      setupBuilding = false;
      setupQrDirty = true;
      Serial0.printf("setup : url = \"%s\"\n", qrUrl);
      break;
    case 'B': // URL en cours de saisie / incomplete -> QR en construction
      setupBuilding = true;
      break;
    case 'S': // etape affichee sur le telephone (0..2)
      if (len >= 2)
        setupStep = (uint8_t)((payload[1] - '0') % 3);
      break;
    case 'A': // buddy custom : hue,sat100,face
    {
      int h = 0, s = 100, f = 0;
      if (sscanf((const char *)payload + 1, "%d,%d,%d", &h, &s, &f) == 3)
      {
        g_buddyCustomDef.hue = (int16_t)(((h % 360) + 360) % 360);
        g_buddyCustomDef.sat = constrain(s, 20, 150) / 100.0f;
        g_buddyCustomDef.face = (uint8_t)constrain(f, 0, 8);
        g_buddyCustom = true;
        prefs.putUChar("bcust", 1);
        prefs.putShort("bhue", g_buddyCustomDef.hue);
        prefs.putUChar("bsat", (uint8_t)constrain(s, 20, 150));
        prefs.putUChar("bface", g_buddyCustomDef.face);
        irDirtyFrom = 0; // la sphere idle se regenerera progressivement
      }
      break;
    }
    case 'R': // retour a l'avatar de la table (Settings)
      g_buddyCustom = false;
      prefs.putUChar("bcust", 0);
      irDirtyFrom = 0;
      Serial0.println("setup : retour a l'avatar de la table");
      break;
    default:
      break;
    }
    setupRedraw = true;
    break;
  default:
    break;
  }
}

static void setupModeEnter()
{
  WiFi.mode(WIFI_AP);
  WiFi.softAP(badgeSsid(), OTA_PASS);
  setupHttp = new WebServer(80);
  setupHttp->on("/", []() {
    setupHttp->sendHeader("Content-Encoding", "gzip");
    setupHttp->send_P(200, "text/html", (PGM_P)SETUP_PAGE_GZ, SETUP_PAGE_LEN);
  });
  setupHttp->onNotFound([]() {
    setupHttp->sendHeader("Location", "http://192.168.4.1/");
    setupHttp->send(302, "text/plain", "");
  });
  setupHttp->begin();
  setupWs = new WebSocketsServer(81);
  setupWs->onEvent(setupWsEvent);
  setupWs->begin();
  setupClients = 0;
  setupRedraw = false;
  setupStep = 0;
  setupBuilding = false;
  setupQrDirty = true;
  Serial0.printf("MODE SETUP : AP %s / %s, http://%s\n", badgeSsid(), OTA_PASS,
                 WiFi.softAPIP().toString().c_str());
}

static void setupModeLoop()
{
  setupHttp->handleClient();
  setupWs->loop();
}

static void setupModeExit()
{
  setupWs->close();
  delete setupWs;
  setupWs = nullptr;
  setupHttp->stop();
  delete setupHttp;
  setupHttp = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  if (setupSpr)
  {
    free(setupSpr);
    setupSpr = nullptr;
  }
  Serial0.println("setup : fin, WiFi coupe");
}
