// Live drawing mode: the badge creates a Wi-Fi AP (same credentials as
// OTA), serves the drawing webapp (draw_page.h, gzipped) at
// http://192.168.4.1 and receives strokes over binary WebSocket (port 81).
// Each segment is 12 bytes LE: x0,y0,x1,y1 (int16), 565 color (uint16),
// radius (uint8), reserved (uint8). One-byte message 0x01 = clear screen.
// Assumed defined before inclusion: canvas, W/H, rgb565, Serial0,
// OTA_SSID/OTA_PASS.
#pragma once
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "draw_page.h"

static WebServer *drawHttp = nullptr;
static WebSocketsServer *drawWs = nullptr;
static bool drawDirty = false;   // framebuffer changed since the last flush
static bool drawStarted = false; // first client connected -> black canvas
static uint8_t drawClients = 0;
// bounding box of the changes since the last flush (partial flush:
// one stroke = a few ms of SPI instead of ~52 ms for the full screen)
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

// ---- animated brushes: the points of these strokes are stored and
// repainted continuously (glitter, iridescence...). Brush 0 (plain) stays
// "stamp and forget": zero cost, minimal latency.
enum DrawBrush : uint8_t { BR_PLAIN = 0, BR_GLITTER, BR_IRIS, BR_NEON, BR_FIRE };

struct DrawPt
{
  int16_t x, y;
  uint16_t col; // chosen color (glitter/neon)
  uint8_t r, brush;
  uint16_t seed; // per-point random phase
};
#define DRAW_MAXPTS 4096
static DrawPt *drawPts = nullptr;
static int drawNPts = 0, drawPtHead = 0; // ring: past the cap, overwrites old ones

// Paints an animated point at time t (called at stamp AND every anim tick)
static void drawPtPaint(const DrawPt &p, float t)
{
  switch (p.brush)
  {
  case BR_GLITTER:
  {
    // base: darkened color; on top: twinkling glitter
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
    // iridescent sheet: hue driven by position + time (fluid wave)
    uint8_t hue = (uint8_t)((int)(p.x * 0.55f + p.y * 0.35f + t * 70) & 255);
    canvas->fillCircle(p.x, p.y, p.r, hsv2rgb565(hue, 230, 255));
    break;
  }
  case BR_NEON:
  {
    // brightness pulse, phase-shifted per point for a wave effect
    float pulse = 0.62f + 0.38f * sinf(t * 3.2f + (p.seed & 63) * 0.1f);
    uint16_t r5 = (p.col >> 11) & 31, g6 = (p.col >> 5) & 63, b5 = p.col & 31;
    canvas->fillCircle(p.x, p.y, p.r,
                       (((uint16_t)(r5 * pulse) << 11) |
                        ((uint16_t)(g6 * pulse) << 5) | (uint16_t)(b5 * pulse)));
    break;
  }
  case BR_FIRE:
  {
    // embers: warm colors picked at random, constant crackling
    static const uint16_t FIRE_COLS[5] = {
        0xF800 /*red*/, 0xFB20 /*orange*/, 0xFE60 /*yellow-orange*/,
        0xFFE0 /*yellow*/, 0x9800 /*dark red*/};
    canvas->fillCircle(p.x, p.y, p.r, FIRE_COLS[rand() % 5]);
    if (p.r > 3) // brighter dancing core
      canvas->fillCircle(p.x + rand() % 3 - 1, p.y + rand() % 3 - 1, p.r / 3,
                         FIRE_COLS[1 + rand() % 3]);
    break;
  }
  default:
    canvas->fillCircle(p.x, p.y, p.r, p.col);
  }
}

// Thick stroke: stamps discs along the segment (no native thick line in
// Arduino_GFX), step of r/2 for a solid look without needless overhead
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
      // animated points recorded more sparsely (r instead of r/2):
      // overlap stays sufficient and it saves ring capacity
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
  // eraser (black, plain brush): forgets the covered animated points
  if (brush == BR_PLAIN && col == RGB565_BLACK && drawNPts)
  {
    int rr = r + 2;
    for (int i = 0; i < drawNPts; i++)
    {
      DrawPt &p = drawPts[i];
      if (!p.r)
        continue;
      // point-segment distance approximated by both endpoints + midpoint
      int mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
      int d0x = p.x - x0, d0y = p.y - y0, d1x = p.x - x1, d1y = p.y - y1;
      int dmx = p.x - mx, dmy = p.y - my;
      int lim = (rr + p.r) * (rr + p.r);
      if (d0x * d0x + d0y * d0y < lim || d1x * d1x + d1y * d1y < lim ||
          dmx * dmx + dmy * dmy < lim)
        p.r = 0; // point disabled
    }
  }
  drawRectGrow(min(x0, x1) - r - 1, min(y0, y1) - r - 1,
               max(x0, x1) + r + 1, max(y0, y1) + r + 1);
}

// Animation tick: repaints the animated points ~12x/s (in-progress strokes
// are still flushed immediately between two ticks)
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

// Flushes only the modified area (the screen keeps the rest unchanged);
// falls back to a full flush if the area covers more than a third of it
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
  if (dmafOk)
  {
    // partial window via the DMA driver (blocking: minimal latency and
    // the framebuffer stays free for the next stroke)
    dmafFlush(x0, y0, w, h, fb + y0 * W + x0, W, true);
    return;
  }
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
      drawStarted = true; // clears the info screen, make way for the canvas
      canvas->fillScreen(RGB565_BLACK);
      drawRectGrow(0, 0, W - 1, H - 1);
    }
    Serial0.printf("draw: client %u connected\n", num);
    break;
  case WStype_DISCONNECTED:
    if (drawClients)
      drawClients--;
    Serial0.printf("draw: client %u left\n", num);
    break;
  case WStype_TEXT:
    // "T<local epoch in seconds>" sent by the webapp on connection:
    // syncs the RTC (LOCAL time stored as-is, decoded with gmtime)
    if (len > 1 && payload[0] == 'T')
    {
      struct timeval tv = {(time_t)strtoul((const char *)payload + 1, nullptr, 10), 0};
      settimeofday(&tv, nullptr);
      Serial0.println("draw: time synced by the phone");
    }
    break;
  case WStype_BIN:
    if (len == 1 && payload[0] == 1)
    {
      canvas->fillScreen(RGB565_BLACK);
      drawNPts = drawPtHead = 0; // forgets all animated points
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
  // any other URL (iOS captive portal detection...) redirects to the page
  drawHttp->onNotFound([]() {
    drawHttp->sendHeader("Location", "http://192.168.4.1/");
    drawHttp->send(302, "text/plain", "");
  });
  drawHttp->begin();
  badgeDnsStart(); // captive portal: the page opens by itself on connect
  drawWs = new WebSocketsServer(81);
  drawWs->onEvent(drawWsEvent);
  drawWs->begin();
  drawStarted = drawDirty = false;
  drawClients = 0;
  drawRectReset();
  if (!drawPts)
    drawPts = (DrawPt *)ps_malloc(DRAW_MAXPTS * sizeof(DrawPt));
  drawNPts = drawPtHead = 0;
  Serial0.printf("DRAW MODE: AP %s / %s, http://%s\n", badgeSsid(), OTA_PASS,
                 WiFi.softAPIP().toString().c_str());
}

static void drawModeLoop()
{
  badgeDnsLoop();
  drawHttp->handleClient();
  drawWs->loop();
}

static void drawModeExit()
{
  badgeDnsStop();
  drawWs->close();
  delete drawWs;
  drawWs = nullptr;
  drawHttp->stop();
  delete drawHttp;
  drawHttp = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial0.println("draw: done, WiFi off");
}
