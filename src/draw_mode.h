// Mode dessin live : le badge cree un AP Wi-Fi (memes identifiants que l'OTA),
// sert la webapp de dessin (draw_page.h, gzippee) sur http://192.168.4.1 et
// recoit les traits en WebSocket binaire (port 81). Chaque segment fait
// 12 octets LE : x0,y0,x1,y1 (int16), couleur 565 (uint16), rayon (uint8),
// reserve (uint8). Message d'un octet 0x01 = effacer l'ecran.
// Suppose definis avant inclusion : canvas, W/H, rgb565, Serial0,
// OTA_SSID/OTA_PASS.
#pragma once
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "draw_page.h"

static WebServer *drawHttp = nullptr;
static WebSocketsServer *drawWs = nullptr;
static bool drawDirty = false;   // le framebuffer a change depuis le dernier flush
static bool drawStarted = false; // premier client connecte -> toile noire
static uint8_t drawClients = 0;
// rectangle englobant des modifs depuis le dernier flush (flush partiel :
// un trait = quelques ms de SPI au lieu des ~52 ms du plein ecran)
static int drawMinX, drawMinY, drawMaxX, drawMaxY;

static void drawRectReset()
{
  drawMinX = drawMinY = 32767;
  drawMaxX = drawMaxY = -1;
}

static void drawRectGrow(int x0, int y0, int x1, int y1)
{
  if (x0 < drawMinX) drawMinX = x0;
  if (y0 < drawMinY) drawMinY = y0;
  if (x1 > drawMaxX) drawMaxX = x1;
  if (y1 > drawMaxY) drawMaxY = y1;
  drawDirty = true;
}

// ---- pinceaux animes : les points de ces traits sont memorises et repeints
// en continu (paillettes, irisation...). Le pinceau 0 (plain) reste "stampe
// et oublie" : cout zero, latence minimale.
enum DrawBrush : uint8_t { BR_PLAIN = 0, BR_GLITTER, BR_IRIS, BR_NEON, BR_FIRE };

struct DrawPt
{
  int16_t x, y;
  uint16_t col; // couleur choisie (glitter/neon)
  uint8_t r, brush;
  uint16_t seed; // phase aleatoire propre au point
};
#define DRAW_MAXPTS 4096
static DrawPt *drawPts = nullptr;
static int drawNPts = 0, drawPtHead = 0; // ring : au-dela du cap, ecrase les anciens

// Peint un point anime a l'instant t (appele au stamp ET a chaque tick d'anim)
static void drawPtPaint(const DrawPt &p, float t)
{
  switch (p.brush)
  {
  case BR_GLITTER:
  {
    // fond : couleur assombrie ; dessus : paillettes qui scintillent
    uint16_t r5 = (p.col >> 11) & 31, g6 = (p.col >> 5) & 63, b5 = p.col & 31;
    uint16_t base = ((r5 * 9 >> 4) << 11) | ((g6 * 9 >> 4) << 5) | (b5 * 9 >> 4);
    canvas->fillCircle(p.x, p.y, p.r, base);
    int n = 1 + p.r / 3;
    for (int i = 0; i < n; i++)
    {
      int dx = rand() % (2 * p.r + 1) - p.r, dy = rand() % (2 * p.r + 1) - p.r;
      if (dx * dx + dy * dy > p.r * p.r)
        continue;
      uint16_t sc = (rand() & 3) ? RGB565_WHITE : rgb565(255, 240, 180);
      canvas->drawPixel(p.x + dx, p.y + dy, sc);
      if (p.r > 4 && (rand() & 1))
        canvas->drawPixel(p.x + dx + 1, p.y + dy, sc);
    }
    break;
  }
  case BR_IRIS:
  {
    // nappe irisee : teinte fonction de la position + du temps (vague fluide)
    uint8_t hue = (uint8_t)((int)(p.x * 0.55f + p.y * 0.35f + t * 70) & 255);
    canvas->fillCircle(p.x, p.y, p.r, hsv2rgb565(hue, 230, 255));
    break;
  }
  case BR_NEON:
  {
    // pulsation de luminosite, dephasee par point pour un effet de vague
    float pulse = 0.62f + 0.38f * sinf(t * 3.2f + (p.seed & 63) * 0.1f);
    uint16_t r5 = (p.col >> 11) & 31, g6 = (p.col >> 5) & 63, b5 = p.col & 31;
    canvas->fillCircle(p.x, p.y, p.r,
                       (((uint16_t)(r5 * pulse) << 11) |
                        ((uint16_t)(g6 * pulse) << 5) | (uint16_t)(b5 * pulse)));
    break;
  }
  case BR_FIRE:
  {
    // braises : couleurs chaudes tirees au hasard, crepitement permanent
    static const uint16_t FIRE_COLS[5] = {
        0xF800 /*rouge*/, 0xFB20 /*orange*/, 0xFE60 /*jaune-orange*/,
        0xFFE0 /*jaune*/, 0x9800 /*rouge sombre*/};
    canvas->fillCircle(p.x, p.y, p.r, FIRE_COLS[rand() % 5]);
    if (p.r > 3) // coeur plus clair qui danse
      canvas->fillCircle(p.x + rand() % 3 - 1, p.y + rand() % 3 - 1, p.r / 3,
                         FIRE_COLS[1 + rand() % 3]);
    break;
  }
  default:
    canvas->fillCircle(p.x, p.y, p.r, p.col);
  }
}

// Trait epais : estampe des disques le long du segment (pas de trait epais
// natif dans Arduino_GFX), pas de r/2 pour un rendu plein sans surcout inutile
static void drawStamp(int x0, int y0, int x1, int y1, uint16_t col, int r,
                      uint8_t brush)
{
  int dx = x1 - x0, dy = y1 - y0;
  float t = millis() / 1000.0f;
  int steps = (int)(sqrtf((float)(dx * dx + dy * dy)) / (r > 2 ? r / 2 : 1)) + 1;
  for (int i = 0; i <= steps; i++)
  {
    int x = x0 + dx * i / steps, y = y0 + dy * i / steps;
    if (brush == BR_PLAIN)
      canvas->fillCircle(x, y, r, col);
    else
    {
      // points animes enregistres plus espaces (r au lieu de r/2) : le
      // chevauchement reste suffisant et ca economise le ring
      if (i % 2 == 0 || steps < 2)
      {
        DrawPt p = {(int16_t)x, (int16_t)y, col, (uint8_t)r, brush,
                    (uint16_t)rand()};
        drawPtPaint(p, t);
        drawPts[drawPtHead] = p;
        drawPtHead = (drawPtHead + 1) % DRAW_MAXPTS;
        if (drawNPts < DRAW_MAXPTS)
          drawNPts++;
      }
    }
  }
  // gomme (noir, pinceau plain) : oublie les points animes recouverts
  if (brush == BR_PLAIN && col == RGB565_BLACK && drawNPts)
  {
    int rr = r + 2;
    for (int i = 0; i < drawNPts; i++)
    {
      DrawPt &p = drawPts[i];
      if (!p.r)
        continue;
      // distance point-segment approximee par les deux extremites + milieu
      int mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
      int d0x = p.x - x0, d0y = p.y - y0, d1x = p.x - x1, d1y = p.y - y1;
      int dmx = p.x - mx, dmy = p.y - my;
      int lim = (rr + p.r) * (rr + p.r);
      if (d0x * d0x + d0y * d0y < lim || d1x * d1x + d1y * d1y < lim ||
          dmx * dmx + dmy * dmy < lim)
        p.r = 0; // point desactive
    }
  }
  drawRectGrow(min(x0, x1) - r - 1, min(y0, y1) - r - 1,
               max(x0, x1) + r + 1, max(y0, y1) + r + 1);
}

// Tick d'animation : repeint les points animes ~12x/s (les traits en cours
// restent flushes immediatement entre deux ticks)
static void drawAnimTick(uint32_t now)
{
  static uint32_t lastTick = 0;
  if (!drawNPts || now - lastTick < 80)
    return;
  lastTick = now;
  float t = now / 1000.0f;
  for (int i = 0; i < drawNPts; i++)
  {
    const DrawPt &p = drawPts[i];
    if (!p.r)
      continue;
    drawPtPaint(p, t);
    drawRectGrow(p.x - p.r - 1, p.y - p.r - 1, p.x + p.r + 1, p.y + p.r + 1);
  }
}

// Flush de la seule zone modifiee (l'ecran garde le reste inchange) ;
// bascule en flush complet si la zone couvre plus du tiers de l'ecran
static void drawFlushDirty()
{
  int x0 = drawMinX < 0 ? 0 : drawMinX, y0 = drawMinY < 0 ? 0 : drawMinY;
  int x1 = drawMaxX > W - 1 ? W - 1 : drawMaxX, y1 = drawMaxY > H - 1 ? H - 1 : drawMaxY;
  drawDirty = false;
  drawRectReset();
  if (x1 < x0 || y1 < y0)
    return;
  int w = x1 - x0 + 1, h = y1 - y0 + 1;
  if ((uint32_t)w * h > (uint32_t)W * H / 3)
  {
    canvas->flush();
    return;
  }
  uint16_t *fb = canvas->getFramebuffer();
  Arduino_TFT *tft = (Arduino_TFT *)panel;
  tft->startWrite();
  tft->writeAddrWindow(x0, y0, w, h);
  for (int y = y0; y <= y1; y++)
    tft->writePixels(fb + y * W + x0, w);
  tft->endWrite();
}

static void drawWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len)
{
  switch (type)
  {
  case WStype_CONNECTED:
    drawClients++;
    if (!drawStarted)
    {
      drawStarted = true; // efface l'ecran d'infos, place a la toile
      canvas->fillScreen(RGB565_BLACK);
      drawRectGrow(0, 0, W - 1, H - 1);
    }
    Serial0.printf("draw : client %u connecte\n", num);
    break;
  case WStype_DISCONNECTED:
    if (drawClients)
      drawClients--;
    Serial0.printf("draw : client %u parti\n", num);
    break;
  case WStype_TEXT:
    // "T<epoch local en secondes>" envoye par la webapp a la connexion :
    // synchronise la RTC (heure LOCALE stockee telle quelle, decodee en gmtime)
    if (len > 1 && payload[0] == 'T')
    {
      struct timeval tv = {(time_t)strtoul((const char *)payload + 1, nullptr, 10), 0};
      settimeofday(&tv, nullptr);
      Serial0.println("draw : heure synchronisee par le telephone");
    }
    break;
  case WStype_BIN:
    if (len == 1 && payload[0] == 1)
    {
      canvas->fillScreen(RGB565_BLACK);
      drawNPts = drawPtHead = 0; // oublie tous les points animes
      drawRectGrow(0, 0, W - 1, H - 1);
      break;
    }
    for (size_t off = 0; off + 12 <= len; off += 12)
    {
      int16_t x0, y0, x1, y1;
      uint16_t col;
      memcpy(&x0, payload + off, 2);
      memcpy(&y0, payload + off + 2, 2);
      memcpy(&x1, payload + off + 4, 2);
      memcpy(&y1, payload + off + 6, 2);
      memcpy(&col, payload + off + 8, 2);
      int r = payload[off + 10];
      uint8_t brush = payload[off + 11];
      if (brush > BR_FIRE)
        brush = BR_PLAIN;
      drawStamp(x0, y0, x1, y1, col, r < 1 ? 1 : (r > 30 ? 30 : r), brush);
    }
    break;
  default:
    break;
  }
}

static void drawModeEnter()
{
  WiFi.mode(WIFI_AP);
  WiFi.softAP(badgeSsid(), OTA_PASS);
  drawHttp = new WebServer(80);
  drawHttp->on("/", []() {
    drawHttp->sendHeader("Content-Encoding", "gzip");
    drawHttp->send_P(200, "text/html", (PGM_P)DRAW_PAGE_GZ, DRAW_PAGE_LEN);
  });
  // toute autre URL (detection de portail captif iOS...) renvoie vers la page
  drawHttp->onNotFound([]() {
    drawHttp->sendHeader("Location", "http://192.168.4.1/");
    drawHttp->send(302, "text/plain", "");
  });
  drawHttp->begin();
  drawWs = new WebSocketsServer(81);
  drawWs->onEvent(drawWsEvent);
  drawWs->begin();
  drawStarted = drawDirty = false;
  drawClients = 0;
  drawRectReset();
  if (!drawPts)
    drawPts = (DrawPt *)ps_malloc(DRAW_MAXPTS * sizeof(DrawPt));
  drawNPts = drawPtHead = 0;
  Serial0.printf("MODE DRAW : AP %s / %s, http://%s\n", badgeSsid(), OTA_PASS,
                 WiFi.softAPIP().toString().c_str());
}

static void drawModeLoop()
{
  drawHttp->handleClient();
  drawWs->loop();
}

static void drawModeExit()
{
  drawWs->close();
  delete drawWs;
  drawWs = nullptr;
  drawHttp->stop();
  delete drawHttp;
  drawHttp = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial0.println("draw : fin, WiFi coupe");
}
