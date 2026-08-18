// Emulateur navigateur du badge threejs.paris (WebAssembly).
//
// Le rendu utilise les VRAIES sources du firmware : src/anims_extra.h (toutes
// les anims + photos speakers) et src/games.h + src/tama.h (les 5 jeux) sont
// compiles tels quels — ce que tu vois ici est ce que tu verras sur l'ESP32.
// Le menu, le splash de boot et l'extinction CRT sont des COPIES de main.cpp
// (a resynchroniser si tu modifies ces sections-la du firmware).
//
// Build : ./build.sh (emcc). Sortie : emu.js (wasm embarque) + index.html.
#define EMU_BUILD 1
#include "emu_core.cpp" // Canvas (+ police glcdfont du firmware) + anims

#include <emscripten.h>
#include <map>
#include <string>

// ------------------------------------------------------------ stubs Arduino
static double emuNowMs = 0;
static uint32_t millis() { return (uint32_t)emuNowMs; }

#define BTN_NEXT 19
#define BTN_PREV 20
#define BTN_AUTO 21
#define LOW 0
static bool emuHeldPrev = false, emuHeldNext = false, emuHeldCtr = false;
static int digitalRead(int pin)
{
  if (pin == BTN_PREV) return emuHeldPrev ? 0 : 1;
  if (pin == BTN_NEXT) return emuHeldNext ? 0 : 1;
  if (pin == BTN_AUTO) return emuHeldCtr ? 0 : 1;
  return 1;
}

// (Vec2 est deja defini par emu_core.cpp)
static long random(long n) { return rand() % n; }

// remise a zero du visage idle (copie de main.cpp)
static void resetIdle()
{
  idleSt.lookCX = idleSt.lookCY = idleSt.lookPX = idleSt.lookPY = 0;
  idleSt.lookStart = 0;
  idleSt.lookDur = 0.5f;
  idleSt.lookHold = 0.5f;
  idleSt.blinkStart = -1;
  idleSt.nextBlink = 2.5f;
}

// NVS simule (records des jeux — persiste tant que l'onglet est ouvert)
struct Preferences
{
  std::map<std::string, uint16_t> m;
  void begin(const char *, bool) {}
  uint16_t getUShort(const char *k, uint16_t d)
  {
    auto it = m.find(k);
    return it == m.end() ? d : it->second;
  }
  void putUShort(const char *k, uint16_t v) { m[k] = v; }
};
static Preferences prefs;

#ifndef RGB565_WHITE
#define RGB565_WHITE 0xFFFF
#endif

// ------------------------------------------------- les jeux du firmware
#include "menu_font.h" // Dingos ExtraBold pour le menu
#include "menu_font_med.h" // Dingos Medium (pourcentage batterie)
#include "menu_font_bebas.h"
#include "menu_font_title.h"
#include "games.h"

// ---------------------------------------------- menu / UI (copie de main.cpp)
#define ANIM_DURATION_MS 15000
static const uint8_t ACTIVE[] = {8, 4, 5, 6, 7, 9, 10, 11, 12, 13};
static const int NACTIVE = (int)sizeof(ACTIVE);
// (tables du menu : voir menu_ui.h, partage avec le firmware)

enum UiMode : uint8_t { UI_ANIM, UI_MENU, UI_HOME, UI_SCHED, UI_ROT, UI_DRAW,
                        UI_SNAKE, UI_PONG, UI_RUN, UI_TETRIS, UI_PET, UI_FLASH, UI_OFF,
                        UI_PIN, UI_SET, UI_SETUP, UI_QR };
static UiMode uiMode = UI_ANIM;
static int menuSel = 0, slot = 0, menuCat = 0, schedIdx = 0;

// etat des Settings (code + choix d'avatar) — copie de main.cpp
static uint8_t pinDigits[5];
static int pinPos = 0;
static bool pinRedraw = true;
static double pinErrorUntil = 0;
static int setSel = 0, setShown = -1;
static uint16_t *setSpr = nullptr;
static bool autoCycle = false;
static uint32_t slotStartMs = 0, animStartMs = 0;
static int lastAnim = -1;
static int batPct = 76; // jauge simulee (pas d'ADC dans le navigateur)
static uint32_t batMvRaw = 3780; // tension simulee
static bool batCharging = false;

#include "menu_ui.h" // menu bulles + listes (partage avec le firmware)
#include "qr_screen.h" // ecran Meet > QR Code (partage avec le firmware)

// splash de boot (copie de main.cpp)
static void drawBootLoader(float p, float t)
{
  const int NB = 12, bw = 14, bh = 14, gap = 4;
  const int totW = NB * bw + (NB - 1) * gap;
  const int x0 = CX - totW / 2, y = 276;
  const uint16_t pink = rgb565(0xfc, 0xa3, 0xf7);
  const uint16_t dimFrame = rgb565(70, 110, 80);
  static const char *PHRASES[] = {
      "npm install three", "compiling shaders", "baking the donut",
      "spinning the cube", "computing normals", "draw calls--",
      "camera.lookAt(you)", "scene.add(badge)", "60 fps, promise",
      "reticulating splines", "webgl: ok", "dispose()ing bugs"};
  const int NPHRASES = sizeof(PHRASES) / sizeof(PHRASES[0]);
  const char *txt = PHRASES[((int)(t / 1.0f)) % NPHRASES];
  canvas->setTextSize(2);
  canvas->setTextColor(rgb565(0xfb, 0xd9, 0x75));
  canvas->setCursor(CX - (int)strlen(txt) * 6, y - 26);
  canvas->print(txt);
  int filled = (int)(p * NB + 0.5f);
  for (int i = 0; i < NB; i++)
  {
    int x = x0 + i * (bw + gap);
    if (i < filled)
      canvas->fillRect(x, y, bw, bh, pink);
    else
      canvas->drawRect(x, y, bw, bh, dimFrame);
  }
  for (int yy = y - 28; yy < y + bh + 2; yy++)
    if (yy % 3 == 0)
      dimRow(yy, 40, 320);
}

static void drawDrawWait()
{
  canvas->fillScreen(RGB565_BLACK);
  canvas->setTextColor(rgb565(0xfc, 0xa3, 0xf7));
  canvas->setTextSize(3);
  canvas->setCursor(90, 85);
  canvas->print("DRAW MODE");
  canvas->setTextSize(2);
  canvas->setTextColor(RGB565_WHITE);
  canvas->setCursor(70, 135);
  canvas->print("WiFi badge-threejs");
  canvas->setCursor(70, 160);
  canvas->print("Pass threejs2026");
  canvas->setTextColor(rgb565(255, 213, 48));
  canvas->setCursor(70, 190);
  canvas->print("http://192.168.4.1");
  canvas->setTextColor(rgb565(130, 130, 130));
  canvas->setCursor(CX - 96, 240);
  canvas->print("center: exit");
}

static void drawFlashScreen()
{
  canvas->fillScreen(RGB565_BLACK);
  canvas->setTextColor(rgb565(255, 213, 48));
  canvas->setTextSize(3);
  canvas->setCursor(90, 90);
  canvas->print("FLASH MODE");
  canvas->setTextSize(2);
  canvas->setTextColor(RGB565_WHITE);
  canvas->setCursor(70, 140);
  canvas->print("WiFi badge-threejs");
  canvas->setCursor(70, 165);
  canvas->print("Pass threejs2026");
  canvas->setCursor(70, 190);
  canvas->print("pio run -e ota -t upload");
  canvas->setTextColor(rgb565(130, 130, 130));
  canvas->setCursor(CX - 102, 240);
  canvas->print("center: exit");
}

// Ecran du mode Setup — sur le vrai badge, le telephone se connecte en WiFi
// et configure nom / buddy / URL du QR ; l'emulateur affiche les infos.
static void drawSetupScreen()
{
  canvas->fillScreen(RGB565_BLACK);
  canvas->setTextColor(rgb565(0xfb, 0xd9, 0x75));
  canvas->setTextSize(3);
  canvas->setCursor(CX - 90, 40);
  canvas->print("SETUP");
  canvas->setTextSize(2);
  canvas->setTextColor(RGB565_WHITE);
  canvas->setCursor(70, 120);
  canvas->print("WiFi badge-threejs");
  canvas->setCursor(70, 150);
  canvas->print("Pass threejs2026");
  canvas->setTextColor(rgb565(255, 213, 48));
  canvas->setCursor(70, 185);
  canvas->print("http://192.168.4.1");
  canvas->setTextColor(rgb565(130, 130, 130));
  canvas->setCursor(CX - 96, 250);
  canvas->print("center: exit");
}

// Extinction CRT de main.cpp, refaite en machine a etats (pas de boucle
// bloquante dans le navigateur) — memes constantes, meme rendu.
static uint16_t *offSnap = nullptr;
static double offT0 = 0;
static bool offDone = false;
static void powerOffStart()
{
  uint16_t *fb = canvas->getFramebuffer();
  if (!offSnap)
    offSnap = (uint16_t *)malloc(W * H * sizeof(uint16_t));
  memcpy(offSnap, fb, W * H * sizeof(uint16_t));
  offT0 = emuNowMs;
  offDone = false;
  uiMode = UI_OFF;
}
static void powerOffFrame()
{
  uint16_t *fb = canvas->getFramebuffer();
  double el = emuNowMs - offT0;
  if (el < 550) // phase 1 : contraction verticale + flash blanc
  {
    float p = (float)(el / 550.0);
    float e = p * p;
    int newH = (int)(H * (1 - e) + 6 * e);
    int off = (H - newH) / 2;
    memset(fb, 0, W * H * 2);
    for (int dy = 0; dy < newH; dy++)
      memcpy(&fb[(off + dy) * W], &offSnap[(int)((float)dy * H / newH) * W], W * 2);
    if (p > 0.85f)
    {
      uint8_t a = (uint8_t)((p - 0.85f) / 0.15f * 153);
      for (int i = (off - 2) * W; i < (off + newH + 2) * W; i++)
      {
        if (i < 0 || i >= W * H)
          continue;
        uint16_t c = fb[i];
        uint16_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        r += ((31 - r) * a) >> 8;
        g += ((63 - g) * a) >> 8;
        b += ((31 - b) * a) >> 8;
        fb[i] = (r << 11) | (g << 5) | b;
      }
    }
  }
  else if (el < 1150) // phase 2 : point chaud qui se dissipe
  {
    float fade = 1 - (float)((el - 550) / 600.0);
    canvas->fillScreen(RGB565_BLACK);
    canvas->fillCircle(CX, CY, (int)(40 * fade),
                       rgb565((int)(90 * fade), (int)(85 * fade), (int)(78 * fade)));
    canvas->fillCircle(CX, CY, (int)(8 * fade),
                       rgb565((int)(230 * fade), (int)(216 * fade), (int)(198 * fade)));
  }
  else
  {
    canvas->fillScreen(RGB565_BLACK);
    offDone = true; // ecran eteint — reveil par le bouton central
  }
}

// -------------------------------------------------------------- boot + boucle
static double bootT0 = -1;

extern "C" {

static uint16_t *emuRotBuf = nullptr;
EMSCRIPTEN_KEEPALIVE uint16_t *emu_fb()
{
  if (uiScreenRot == 0)
    return canvas->getFramebuffer();
  if (!emuRotBuf)
    emuRotBuf = (uint16_t *)malloc((size_t)W * H * 2);
  uiRotateBlit(canvas->getFramebuffer(), emuRotBuf, uiScreenRot);
  return emuRotBuf;
}

// horloge de conf simulee (jour 1/2 + minutes) — voir index.html
EMSCRIPTEN_KEEPALIVE void emu_set_now(int day, int minutes)
{
  uiNowDay = day;
  uiNowMin = minutes;
}

EMSCRIPTEN_KEEPALIVE void emu_init()
{
  srand(42);
  initBallSprite();
  initMouthMask();
  initTgLogo();
  irInit();
  for (int i = 0; i < IR_FRAMES; i++)
    irGenFrame(i);
  dvdInitSprites();
  initPoints();
  bootT0 = -1;
  uiMode = UI_ANIM;
  slot = 0;
}

// dtMs : temps ecoule depuis le dernier frame ; held : bit0 prev (haut),
// bit1 next (bas), bit2 centre.

// ---- Draw mode FONCTIONNEL (port de src/draw_mode.h sans le WiFi) ----
// Le "telephone" est rendu par la page (index.html) : elle envoie les
// segments via emu_draw_seg(), exactement comme la webapp reelle en WS.
enum DrawBrush : uint8_t { BR_PLAIN = 0, BR_GLITTER, BR_IRIS, BR_NEON, BR_FIRE };
struct DrawPt
{
  int16_t x, y;
  uint16_t col;
  uint8_t r, brush;
  uint16_t seed;
};
#define DRAW_MAXPTS 4096
static DrawPt *drawPts = nullptr;
static int drawNPts = 0, drawPtHead = 0;

static void drawPtPaint(const DrawPt &p, float t)
{
  switch (p.brush)
  {
  case BR_GLITTER:
  {
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
    uint8_t hue = (uint8_t)((int)(p.x * 0.55f + p.y * 0.35f + t * 70) & 255);
    canvas->fillCircle(p.x, p.y, p.r, hsv2rgb565(hue, 230, 255));
    break;
  }
  case BR_NEON:
  {
    float pulse = 0.62f + 0.38f * sinf(t * 3.2f + (p.seed & 63) * 0.1f);
    uint16_t r5 = (p.col >> 11) & 31, g6 = (p.col >> 5) & 63, b5 = p.col & 31;
    canvas->fillCircle(p.x, p.y, p.r,
                       (((uint16_t)(r5 * pulse) << 11) |
                        ((uint16_t)(g6 * pulse) << 5) | (uint16_t)(b5 * pulse)));
    break;
  }
  case BR_FIRE:
  {
    static const uint16_t FIRE_COLS[5] = {0xF800, 0xFB20, 0xFE60, 0xFFE0, 0x9800};
    canvas->fillCircle(p.x, p.y, p.r, FIRE_COLS[rand() % 5]);
    if (p.r > 3)
      canvas->fillCircle(p.x + rand() % 3 - 1, p.y + rand() % 3 - 1, p.r / 3,
                         FIRE_COLS[1 + rand() % 3]);
    break;
  }
  default:
    canvas->fillCircle(p.x, p.y, p.r, p.col);
  }
}

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
    else if (i % 2 == 0 || steps < 2)
    {
      DrawPt p = {(int16_t)x, (int16_t)y, col, (uint8_t)r, (uint8_t)brush,
                  (uint16_t)rand()};
      drawPtPaint(p, t);
      drawPts[drawPtHead] = p;
      drawPtHead = (drawPtHead + 1) % DRAW_MAXPTS;
      if (drawNPts < DRAW_MAXPTS)
        drawNPts++;
    }
  }
  if (brush == BR_PLAIN && col == RGB565_BLACK && drawNPts)
  {
    int rr = r + 2;
    for (int i = 0; i < drawNPts; i++)
    {
      DrawPt &p = drawPts[i];
      if (!p.r)
        continue;
      int mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
      int d0x = p.x - x0, d0y = p.y - y0, d1x = p.x - x1, d1y = p.y - y1;
      int dmx = p.x - mx, dmy = p.y - my;
      int lim = (rr + p.r) * (rr + p.r);
      if (d0x * d0x + d0y * d0y < lim || d1x * d1x + d1y * d1y < lim ||
          dmx * dmx + dmy * dmy < lim)
        p.r = 0;
    }
  }
}

static void drawEmuEnter()
{
  if (!drawPts)
    drawPts = (DrawPt *)malloc(DRAW_MAXPTS * sizeof(DrawPt));
  drawNPts = 0;
  drawPtHead = 0;
  canvas->fillScreen(RGB565_BLACK);
}

static void drawEmuTick()
{
  // repeint les points animes (paillettes, neon, iris, feu)
  float t = millis() / 1000.0f;
  for (int i = 0; i < drawNPts; i++)
    if (drawPts[i].r)
      drawPtPaint(drawPts[i], t);
}

extern "C"
{
  EMSCRIPTEN_KEEPALIVE int emu_mode() { return (int)uiMode; }
  EMSCRIPTEN_KEEPALIVE void emu_draw_seg(int x0, int y0, int x1, int y1,
                                         int col, int r, int brush)
  {
    if (uiMode != UI_DRAW || !drawPts)
      return;
    if (brush > BR_FIRE || brush < 0)
      brush = BR_PLAIN;
    drawStamp(x0, y0, x1, y1, (uint16_t)col, r < 1 ? 1 : (r > 30 ? 30 : r),
              (uint8_t)brush);
  }
  EMSCRIPTEN_KEEPALIVE void emu_draw_clear()
  {
    if (uiMode != UI_DRAW)
      return;
    drawNPts = 0;
    drawPtHead = 0;
    canvas->fillScreen(RGB565_BLACK);
  }
}

EMSCRIPTEN_KEEPALIVE void emu_frame(float dtMs, int held)
{
  if (dtMs > 100)
    dtMs = 100; // onglet en arriere-plan : pas de saut de temps geant
  emuNowMs += dtMs;
  uint32_t now = millis();
  float dt = dtMs / 1000.0f;

  // ---- boutons : fronts + appui long (meme semantique que le firmware)
  static bool pPrev = false, pNext = false, pCtr = false;
  static double ctrDownAt = 0;
  static bool ctrConsumed = true;
  emuHeldPrev = held & 1;
  emuHeldNext = held & 2;
  emuHeldCtr = held & 4;
  bool navPrev = emuHeldPrev && !pPrev;
  bool navNext = emuHeldNext && !pNext;
  bool centerDown = emuHeldCtr && !pCtr;
  if (centerDown)
  {
    ctrDownAt = emuNowMs;
    ctrConsumed = false;
  }
  bool autoShort = (!emuHeldCtr && pCtr && !ctrConsumed &&
                    emuNowMs - ctrDownAt < 2000);
  bool centerLong = (emuHeldCtr && !ctrConsumed && emuNowMs - ctrDownAt > 2000);
  pPrev = emuHeldPrev;
  pNext = emuHeldNext;
  pCtr = emuHeldCtr;

  // ---- splash de boot : Three Conf + loader pendant 4 s
  if (bootT0 < 0)
    bootT0 = emuNowMs;
  if (emuNowMs - bootT0 < 4000)
  {
    float ts = (float)(emuNowMs - bootT0) / 1000.0f;
    animThreeConf(ts, -20);
    drawBootLoader(ts / 4.0f, ts);
    return;
  }

  // ---- extinction / reveil
  if (uiMode == UI_OFF)
  {
    if (offDone && centerDown)
    {
      ctrConsumed = true;
      bootT0 = emuNowMs; // reboot : splash puis idle rainbow
      uiMode = UI_ANIM;
      slot = 0;
      slotStartMs = now;
      lastAnim = -1;
    }
    else
      powerOffFrame();
    return;
  }
  if (centerLong && uiMode != UI_FLASH)
  {
    ctrConsumed = true;
    powerOffStart();
    return;
  }

  if (uiMode == UI_HOME)
  {
    if (navNext)
      uiHomeNav(1);
    if (navPrev)
      uiHomeNav(-1);
    if (autoShort)
    {
      menuCat = uiHomeFocus;
      menuSel = 0;
      uiMode = UI_MENU;
    }
    if (uiMode == UI_HOME)
      uiDrawHome(dt, batPct, batCharging);
    else if (uiMode == UI_SCHED)
      uiDrawSchedule(schedIdx);
    else
      uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
    return;
  }

  if (uiMode == UI_ROT)
  {
    if (navPrev && uiScreenRot > -15)
      uiScreenRot--;
    if (navNext && uiScreenRot < 15)
      uiScreenRot++;
    if (autoShort)
    {
      prefs.putUShort("rotDeg", (uint16_t)(uiScreenRot + 128)); // persiste (session)
      uiMode = UI_MENU;
      uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
      return;
    }
    uiDrawRotate(uiScreenRot);
    return;
  }

  if (uiMode == UI_PIN)
  {
    // code d'acces des Settings — copie de main.cpp
    if (navPrev)
      pinDigits[pinPos] = (pinDigits[pinPos] + 9) % 10;
    if (navNext)
      pinDigits[pinPos] = (pinDigits[pinPos] + 1) % 10;
    if (autoShort && pinErrorUntil <= 0)
    {
      if (pinPos < UI_PIN_LEN - 1)
        pinPos++;
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
          uiMode = UI_SET;
          return;
        }
        pinErrorUntil = now + 900; // "wrong code" puis retour au menu
      }
    }
    if (pinErrorUntil > 0)
    {
      uiDrawPin(pinDigits, pinPos, true);
      if (now >= pinErrorUntil)
      {
        pinErrorUntil = 0;
        uiMode = UI_MENU;
      }
      return;
    }
    uiDrawPin(pinDigits, pinPos, false);
    return;
  }

  if (uiMode == UI_SET)
  {
    // choix de l'avatar/personne — copie de main.cpp
    if (navPrev)
      setSel = (setSel + AVATAR_N - 1) % AVATAR_N;
    if (navNext)
      setSel = (setSel + 1) % AVATAR_N;
    if (autoShort)
    {
      prefs.putUShort("avatar", (uint16_t)setSel);
      g_avatarIdx = (uint8_t)setSel;
      g_avatarFaceIdx = g_avatarIdx;
      irDirtyFrom = 0;
      if (setSpr)
      {
        free(setSpr);
        setSpr = nullptr;
      }
      setShown = -1;
      uiMode = UI_MENU;
      uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
      return;
    }
    if (setShown != setSel)
    {
      setShown = setSel;
      if (setSpr)
        free(setSpr);
      const AvatarDef &av = AVATARS[setSel];
      setSpr = dvdGenSprite(PAL_RAINBOW, PAL_N, av.hue, av.sat);
    }
    g_avatarFaceIdx = (uint8_t)setSel;
    uiDrawAvatarFrame(setSel, AVATAR_N, AVATARS[setSel].name);
    dvdBlit(setSpr, CX, CY - 26, 78, 255);
    drawIdleFaceLook(CX, CY - 26, 78, 0, 0, 0, 1.0f);
    return;
  }

  if (uiMode == UI_SCHED)
  {
    if (navNext)
      schedIdx = (schedIdx + 1) % UI_NEVENTS;
    if (navPrev)
      schedIdx = (schedIdx + UI_NEVENTS - 1) % UI_NEVENTS;
    if (autoShort)
      uiMode = UI_MENU; // retour a la liste Meet
    if (uiMode == UI_SCHED)
      uiDrawSchedule(schedIdx);
    else
      uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
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
        uiMode = UI_DRAW; // dessin fonctionnel : la page affiche le telephone
        drawEmuEnter();
        break;
      case UIA_AUTO:
        autoCycle = !autoCycle;
        break;
      case UIA_OTA:
        uiMode = UI_FLASH;
        break;
      case UIA_SCHED:
        uiMode = UI_SCHED;
        break;
      case UIA_ROT:
        uiMode = UI_ROT;
        break;
      case UIA_SETTINGS:
        memset(pinDigits, 0, sizeof(pinDigits));
        pinPos = 0;
        pinRedraw = true;
        pinErrorUntil = 0;
        uiMode = UI_PIN;
        break;
      case UIA_SETUP:
        uiMode = UI_SETUP;
        break;
      case UIA_QR:
        qrScreenPrepare();
        uiMode = UI_QR;
        break;
      case UIA_BACK:
        uiMode = UI_HOME;
        break;
      default:
        break;
      }
    }
    if (uiMode == UI_MENU)
      uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
    else if (uiMode == UI_HOME)
      uiDrawHome(dt, batPct, batCharging);
    else if (uiMode == UI_DRAW)
      ; // ecran de dessin deja efface par drawEmuEnter()
    else if (uiMode == UI_FLASH)
      drawFlashScreen();
    else if (uiMode == UI_SETUP)
      drawSetupScreen();
    else if (uiMode == UI_QR)
      qrScreenDraw((float)(emuNowMs / 1000.0));
    return;
  }

  if (uiMode == UI_DRAW || uiMode == UI_FLASH || uiMode == UI_SETUP)
  {
    if (uiMode == UI_DRAW)
      drawEmuTick();
    else if (uiMode == UI_FLASH)
      drawFlashScreen();
    else
      drawSetupScreen();
    if (autoShort)
      uiMode = UI_MENU;
    return;
  }

  if (uiMode == UI_QR)
  {
    qrScreenDraw((float)(emuNowMs / 1000.0));
    if (autoShort)
    {
      qrScreenRelease();
      uiMode = UI_MENU;
    }
    return;
  }

  if (uiMode >= UI_SNAKE && uiMode <= UI_PET)
  {
    int gi = uiMode - UI_SNAKE;
    bool over = gameIsOver(gi);
    static double bothHold = 0;
    if (emuHeldPrev && emuHeldNext)
    {
      if (!bothHold)
        bothHold = emuNowMs;
    }
    else
      bothHold = 0;
    bool quit = (bothHold && emuNowMs - bothHold > 800);
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
      uiMode = UI_MENU;
      uiDrawList(menuCat, menuSel, autoCycle, batPct, batCharging);
      return;
    }
    if (over && navNext)
    {
      gameReset(gi);
      navNext = navPrev = false;
      gBtnCenter = false;
    }
    gBtnLeft = navPrev;
    gBtnRight = navNext;
    gameUpdate(gi, dt);
    gBtnCenter = gBtnLeft = gBtnRight = false;
    return;
  }

  // ---- mode animations
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
    uiMode = UI_HOME;
    uiHomeReset();
    uiDrawHome(dt, batPct, batCharging);
    return;
  }
  if (autoCycle && now - slotStartMs >= ANIM_DURATION_MS)
  {
    slot = (slot + 1) % NACTIVE;
    slotStartMs = now;
  }
  int anim = ACTIVE[slot];
  if (anim != lastAnim)
  {
    lastAnim = anim;
    animStartMs = now;
    resetIdle();
  }
  float t = (now - animStartMs) / 1000.0f;
  switch (anim)
  {
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
  }
}

} // extern "C"
