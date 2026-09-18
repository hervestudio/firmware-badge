// Setup mode (More > Setup): badge configuration flow FROM THE PHONE.
// The badge creates the same Wi-Fi AP as Draw/OTA, serves the 3-step
// webapp (setup_page.h, gzipped) at http://192.168.4.1 and receives the
// settings over text WebSocket (port 81):
//   badge -> phone: J{"name":..,"url":..,"hue":H,"sat":S*100,"face":F,"cust":0/1}
//   phone -> badge: N<name>  |  A<hue>,<sat100>,<face>  |  R (back to
//                   the table avatar)  |  U<url>
// Each message is SAVED to NVS immediately and the badge shows a live
// preview of the buddy. The QR itself is viewed in Meet > QR Code.
// Assumes defined before inclusion: canvas, W/H/CX/CY, rgb565, Serial0,
// OTA_SSID/OTA_PASS, prefs, qrUrl/qrName (qr_screen.h), dvdGenSprite/dvdBlit,
// avatarDrawFace/avatarDrawExtras, g_buddyCustom/g_buddyCustomDef,
// irDirtyMask, mdPrint/mdTextW.
#pragma once
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiUdp.h>
#include "setup_page.h"

// Captive DNS shared by Draw/Setup: every A query is answered with the
// badge's IP -> the captive-portal probes (iOS/Android) trigger the
// AUTOMATIC page opening on WiFi connection, and a hand-typed
// http://badge.local resolves too (via this DNS). HOMEMADE responder,
// synchronous, polled in our loop: the core 3.x DNSServer is asynchronous
// (AsyncUDP, replies from the lwIP task) and, combined with mDNS, made
// the badge reboot when the portal sheet opened.
static WiFiUDP *badgeDnsUdp = nullptr;
static void badgeDnsStart()
{
  badgeDnsUdp = new WiFiUDP();
  badgeDnsUdp->begin(53);
}
static void badgeDnsLoop()
{
  if (!badgeDnsUdp)
    return;
  for (int k = 0; k < 8; k++) // drains the portal's burst of probes
  {
    int len = badgeDnsUdp->parsePacket();
    if (len <= 0)
      return;
    uint8_t buf[512];
    len = badgeDnsUdp->read(buf, sizeof(buf));
    if (len < 17 || (buf[2] & 0x80)) // too short or already a response
      continue;
    // QTYPE of the first question: skip the name (labels until 0)
    int q = 12;
    while (q < len && buf[q])
      q += buf[q] + 1;
    if (q + 5 > len)
      continue;
    uint16_t qtype = (buf[q + 1] << 8) | buf[q + 2];
    // response header: QR=1 AA=1 RA=1, 1 question, ANCOUNT per type
    bool answerA = qtype == 1 || qtype == 255; // A or ANY
    buf[2] = 0x84;
    buf[3] = 0x80;
    buf[4] = 0;
    buf[5] = 1; // QDCOUNT forced to 1 (only the 1st question is copied)
    buf[6] = 0;
    buf[7] = answerA ? 1 : 0; // ANCOUNT (AAAA & co: NOERROR, no answer)
    buf[8] = buf[9] = buf[10] = buf[11] = 0; // NSCOUNT/ARCOUNT
    int out = q + 5; // end of the question section
    if (answerA)
    {
      const IPAddress ip = WiFi.softAPIP();
      const uint8_t ans[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4,
                             ip[0], ip[1], ip[2], ip[3]};
      memcpy(buf + out, ans, sizeof(ans));
      out += sizeof(ans);
    }
    badgeDnsUdp->beginPacket(badgeDnsUdp->remoteIP(), badgeDnsUdp->remotePort());
    badgeDnsUdp->write(buf, out);
    badgeDnsUdp->endPacket();
  }
}
static void badgeDnsStop()
{
  if (badgeDnsUdp)
  {
    badgeDnsUdp->stop();
    delete badgeDnsUdp;
    badgeDnsUdp = nullptr;
  }
}

static WebServer *setupHttp = nullptr;
static WebSocketsServer *setupWs = nullptr;
static bool setupRedraw = false; // state changed -> repaint the preview
static uint8_t setupClients = 0;
static uint16_t *setupSpr = nullptr; // buddy preview sprite (cached)
static uint8_t setupStep = 0;        // step shown on the phone (0..3)
static File setupPhotoFile;          // photo upload in progress (LittleFS)
static uint32_t setupPhotoLeft = 0;  // remaining expected bytes
static bool setupBuilding = false;   // URL being typed -> "wip" QR
static bool setupQrDirty = true;     // URL changed -> re-encode the QR

// Connection info screen (while no phone is connected yet): WiFi QR to
// scan with the camera (joins the AP, then the portal opens the page) +
// plain-text credentials and fallback IP
static void setupDrawScreen()
{
  canvas->fillScreen(RGB565_BLACK);
  canvas->setTextColor(rgb565(0xfb, 0xd9, 0x75));
  canvas->setTextSize(3);
  canvas->setCursor(CX - 45, 24);
  canvas->print("SETUP");
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

// Live preview (phone connected), animated frame by frame: steps 1-3 =
// identity card (buddy + message + name + company); step 4 (QR,
// setupStep 3 since the photo step was added) = the live QR, "under
// construction" version while the URL is being typed
static void setupDrawLive(float t)
{
  // photo step (setupStep 2): PROGRESS during the transfer, then
  // full-screen preview of the received photo (review 2026-09-07 (Romain))
  if (setupStep == 2 && setupPhotoFile && setupPhotoLeft)
  {
    canvas->fillScreen(RGB565_BLACK);
    uint32_t total = (uint32_t)W * H * 2;
    int pct = (int)((total - setupPhotoLeft) * 100 / total);
    bbPrint(CX - bbTextW("RECEIVING PHOTO") / 2, 140, "RECEIVING PHOTO",
            rgb565(0x9d, 0x97, 0xed));
    const int bx = 80, bw = 200, by = 180, bh = 14;
    canvas->drawRoundRect(bx, by, bw, bh, 7, rgb565(90, 90, 90));
    canvas->fillRoundRect(bx + 2, by + 2, (bw - 4) * pct / 100, bh - 4, 5,
                          rgb565(0xfb, 0xd9, 0x75));
    return;
  }
  if (setupStep == 2)
  {
    if (g_hasPhoto && g_myPhoto)
      memcpy(canvas->getFramebuffer(), g_myPhoto, (size_t)W * H * 2);
    else
      uiDrawPhotoPlaceholder(); // dotted outline + invite (no photo yet)
    return;
  }
  if (setupStep == 3)
  {
    if (setupQrDirty) // also (re)generates the medallion buddy sprite
    {
      qrScreenPrepare();
      setupQrDirty = false;
    }
    qrScreenDraw(t, setupBuilding);
    return;
  }
  // sprite regenerated only when the parameters change
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
  // effective color/face: the custom one when active, otherwise the table
  // avatar (Settings) - the phone sliders thus reflect the chosen avatar
  // and the custom starts from those values
  const AvatarDef &av = g_buddyCustom ? g_buddyCustomDef : AVATARS[g_avatarIdx];
  char msg[330];
  snprintf(msg, sizeof(msg),
           "J{\"name\":\"%s\",\"comp\":\"%s\",\"msg\":\"%s\",\"url\":\"%s\","
           "\"hue\":%d,\"sat\":%d,\"face\":%d,\"cust\":%d}",
           qrName, qrCompany, qrMsg, qrUrl, ((av.hue % 360) + 360) % 360,
           (int)(av.sat * 100 + 0.5f), (int)av.face, g_buddyCustom ? 1 : 0);
  setupWs->sendTXT(num, msg);
}

// copies a received string, filtering quotes (state JSON) and control
// chars; keeps UTF-8 as is (message emojis)
static void setupCopyStr(char *dst, size_t cap, const uint8_t *src, size_t len)
{
  size_t o = 0;
  for (size_t i = 0; i < len && o + 1 < cap; i++)
    if (src[i] >= 32 && src[i] != '"' && src[i] != '\\')
      dst[o++] = (char)src[i];
  dst[o] = 0;
}

// name/company variant: transliterates Latin accents to ASCII
// (the badge's Dingos/Bebas fonts cover 32..126)
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
    else if (c == 0xC3 && i + 1 < len) // UTF-8 Latin-1 Supplement
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
    Serial0.printf("setup: client %u connected\n", num);
    break;
  case WStype_DISCONNECTED:
    if (setupClients)
      setupClients--;
    setupRedraw = true;
    Serial0.printf("setup: client %u gone\n", num);
    break;
  case WStype_TEXT:
    if (!len)
      break;
    switch (payload[0])
    {
    case 'N': // speaker name
      setupCopyAscii(qrName, sizeof(qrName), payload + 1, len - 1);
      prefs.putString("bname", qrName);
      Serial0.printf("setup: name = \"%s\"\n", qrName);
      break;
    case 'C': // company
      setupCopyAscii(qrCompany, sizeof(qrCompany), payload + 1, len - 1);
      prefs.putString("bcomp", qrCompany);
      Serial0.printf("setup: company = \"%s\"\n", qrCompany);
      break;
    case 'M': // message (pill, emojis welcome)
      setupCopyStr(qrMsg, sizeof(qrMsg), payload + 1, len - 1);
      prefs.putString("bmsg", qrMsg);
      Serial0.printf("setup: message = \"%s\"\n", qrMsg);
      break;
    case 'U': // QR code URL (validated phone-side)
      setupCopyStr(qrUrl, sizeof(qrUrl), payload + 1, len - 1);
      if (!qrUrl[0])
        snprintf(qrUrl, sizeof(qrUrl), "https://threejs.paris");
      prefs.putString("qrurl", qrUrl);
      setupBuilding = false;
      setupQrDirty = true;
      Serial0.printf("setup: url = \"%s\"\n", qrUrl);
      break;
    case 'B': // URL being typed / incomplete -> QR under construction
      setupBuilding = true;
      break;
    case 'S': // step shown on the phone (0..3)
      if (len >= 2)
        setupStep = (uint8_t)((payload[1] - '0') % 4);
      break;
    case 'P': // photo upload start: "P<bytes>" (raw RGB565 360x360,
              // cropped phone-side), followed by acknowledged binary frames
    {
      uint32_t n = strtoul((const char *)payload + 1, nullptr, 10);
      if (n != (uint32_t)W * H * 2 || !LittleFS.begin(true))
      {
        setupWs->sendTXT(num, "PERR");
        break;
      }
      if (setupPhotoFile)
        setupPhotoFile.close();
      setupPhotoFile = LittleFS.open("/photo.tmp", "w");
      setupPhotoLeft = setupPhotoFile ? n : 0;
      if (!setupPhotoFile)
        setupWs->sendTXT(num, "PERR");
      else
      {
        setupWs->sendTXT(num, "PACK"); // ready for the first frame
        Serial0.printf("setup: photo upload (%lu bytes)\n", (unsigned long)n);
      }
      break;
    }
    case 'X': // photo deletion
      if (LittleFS.begin(true))
        LittleFS.remove("/photo.565");
      g_hasPhoto = false;
      setupWs->sendTXT(num, "XOK");
      Serial0.println("setup: photo deleted");
      break;
    case 'A': // custom buddy: hue,sat100,face
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
        irDirtyMask = 0xFFFFFFFFu; // all idle frames to redo
      g_ballDirty = true;    // + the ball sprite (snake/DVD/games)
        setupQrDirty = true; // the QR medallion sprite too
      }
      break;
    }
    case 'R': // back to the table avatar (Settings)
      g_buddyCustom = false;
      prefs.putUChar("bcust", 0);
      irDirtyMask = 0xFFFFFFFFu; // all idle frames to redo
      g_ballDirty = true;    // + the ball sprite (snake/DVD/games)
      setupQrDirty = true; // QR medallion sprite to regenerate
      setupSendState(num); // resyncs the phone's sliders/face
      Serial0.println("setup: back to the table avatar");
      break;
    default:
      break;
    }
    setupRedraw = true;
    break;
  case WStype_BIN: // photo upload frames, acknowledged one by one
    if (setupPhotoFile && setupPhotoLeft)
    {
      size_t take = len > setupPhotoLeft ? setupPhotoLeft : len;
      if (setupPhotoFile.write(payload, take) != take)
      {
        setupPhotoFile.close();
        setupPhotoLeft = 0;
        setupWs->sendTXT(num, "PERR");
        break;
      }
      setupPhotoLeft -= take;
      if (setupPhotoLeft == 0)
      {
        setupPhotoFile.close();
        LittleFS.remove("/photo.565");
        LittleFS.rename("/photo.tmp", "/photo.565");
        g_hasPhoto = false; // myPhotoLoad rearms it if the file is valid
        myPhotoLoad();
        setupWs->sendTXT(num, g_hasPhoto ? "POK" : "PERR");
        Serial0.println("setup: photo received");
      }
      else
        setupWs->sendTXT(num, "PACK");
    }
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
  badgeDnsStart(); // captive portal: the page opens by itself on connect
  setupWs = new WebSocketsServer(81);
  setupWs->onEvent(setupWsEvent);
  setupWs->begin();
  setupClients = 0;
  setupRedraw = false;
  setupStep = 0;
  setupBuilding = false;
  setupQrDirty = true;
  Serial0.printf("SETUP MODE: AP %s / %s, http://%s\n", badgeSsid(), OTA_PASS,
                 WiFi.softAPIP().toString().c_str());
}

static void setupModeLoop()
{
  badgeDnsLoop();
  setupHttp->handleClient();
  setupWs->loop();
}

static void setupModeExit()
{
  badgeDnsStop();
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
  Serial0.println("setup: done, WiFi off");
}
