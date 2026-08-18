// Les mini-jeux du badge (Snake, Pong, et la fournee Three.js : Blaster,
// FPS Rush, Donut Catcher, Sphere Runner, Render Panic, Shader Simon).
// Inclus par main.cpp apres le menu ; utilise canvas, rgb565/hsv2rgb565,
// frand, drawBallSprite, dvdBlit/dvdSprites, prefs et les pins BTN_*.
#pragma once

// Appuis fournis par loop() a chaque frame (debounces / press-down)
static bool gBtnCenter = false;
static bool gBtnLeft = false, gBtnRight = false;

// ------------------------------------------------------------------- jeux

// Ecran de fin commun aux deux jeux
static void drawGameOver(const char *title, int score, int best)
{
  canvas->fillScreen(RGB565_BLACK);
  canvas->setTextSize(3);
  canvas->setTextColor(rgb565(240, 80, 70));
  canvas->setCursor(CX - (int)strlen(title) * 9, 100);
  canvas->print(title);
  char buf[24];
  snprintf(buf, sizeof(buf), "SCORE %d", score);
  canvas->setTextSize(3);
  canvas->setTextColor(RGB565_WHITE);
  canvas->setCursor(CX - (int)strlen(buf) * 9, 150);
  canvas->print(buf);
  snprintf(buf, sizeof(buf), "best %d", best);
  canvas->setTextSize(2);
  canvas->setTextColor(rgb565(150, 150, 150));
  canvas->setCursor(CX - (int)strlen(buf) * 6, 192);
  canvas->print(buf);
  canvas->setTextColor(rgb565(255, 213, 48));
  canvas->setCursor(CX - 78, 236);
  canvas->print("right: replay");
  canvas->setCursor(CX - 72, 262);
  canvas->print("center: menu");
}

// ---- Snake jouable : pilotage continu (maintenir gauche/droite pour tourner),
// pommes a manger, mort sur le bord ou sur son propre corps. ----

#define SNKG_TRAIL 420
static struct
{
  float x, y, ang, speed;
  int len, score;
  bool over;
  Vec2 trail[SNKG_TRAIL];
  int trailLen;
  float ax, ay; // pomme
  int best;
} snkg;

// Mini-visage pour les petites spheres (le visage standard, calibre pour des
// tetes de 54 px, devient un pate illisible a 16 px) : yeux fins + sourire 1 px.
static void drawMiniFace(float cx, float cy, float r)
{
  uint16_t ink = rgb565(39, 39, 39);
  int er = max(1, (int)(r * 0.11f));
  canvas->fillCircle((int)(cx - r * 0.34f), (int)(cy - r * 0.2f), er, ink);
  canvas->fillCircle((int)(cx + r * 0.34f), (int)(cy - r * 0.2f), er, ink);
  // sourire : parabole en 4 segments de 1 px (x2 rangees pour la lisibilite)
  int px = 0, py = 0;
  for (int i = -2; i <= 2; i++)
  {
    int xx = (int)(cx + i * r * 0.14f);
    int yy = (int)(cy + r * 0.18f + (2 * 2 - i * i) * r * 0.045f);
    if (i > -2)
    {
      canvas->drawLine(px, py, xx, yy, ink);
      canvas->drawLine(px, py + 1, xx, yy + 1, ink);
    }
    px = xx;
    py = yy;
  }
}

static void snakeGameSpawnApple()
{
  // dans l'arene, pas trop pres de la tete
  for (int tries = 0; tries < 20; tries++)
  {
    float a = frand(0, 2 * PI), r = frand(30, RADIUS - 40);
    float x = CX + cosf(a) * r, y = CY + sinf(a) * r;
    if (sqrtf((x - snkg.x) * (x - snkg.x) + (y - snkg.y) * (y - snkg.y)) > 70)
    {
      snkg.ax = x;
      snkg.ay = y;
      return;
    }
  }
}

static void snakeGameReset()
{
  snkg.x = CX;
  snkg.y = CY;
  snkg.ang = frand(0, 2 * PI);
  snkg.speed = 85;
  snkg.len = 4;
  snkg.score = 0;
  snkg.over = false;
  snkg.trail[0] = {snkg.x, snkg.y};
  snkg.trailLen = 1;
  snkg.best = prefs.getUShort("snakeBest", 0);
  snakeGameSpawnApple();
}

static void gameSnake(float dt)
{
  const float headR = 16, spacing = 21;
  const float Rmax = RADIUS - headR - 6;

  if (snkg.over)
  {
    drawGameOver("GAME OVER", snkg.score, snkg.best);
    return;
  }
  if (dt > 0.12f)
    dt = 0.12f;

  // pilotage : maintenir gauche/droite pour tourner (lecture directe des pins)
  if (digitalRead(BTN_PREV) == LOW)
    snkg.ang -= 3.6f * dt;
  if (digitalRead(BTN_NEXT) == LOW)
    snkg.ang += 3.6f * dt;

  snkg.x += cosf(snkg.ang) * snkg.speed * dt;
  snkg.y += sinf(snkg.ang) * snkg.speed * dt;

  // memorise la trajectoire
  if (snkg.trailLen < SNKG_TRAIL)
    snkg.trailLen++;
  memmove(&snkg.trail[1], &snkg.trail[0], (snkg.trailLen - 1) * sizeof(Vec2));
  snkg.trail[0] = {snkg.x, snkg.y};

  // corps : echantillonnage a pas constant le long de la trajectoire
  static Vec2 pts[128];
  int np = 1, ti = 0;
  pts[0] = {snkg.x, snkg.y};
  float need = spacing, acc = 0;
  while (np < snkg.len && ti < snkg.trailLen - 1)
  {
    Vec2 a = snkg.trail[ti], b = snkg.trail[ti + 1];
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

  // collisions : bord de l'arene / pomme / son propre corps
  float dc = sqrtf((snkg.x - CX) * (snkg.x - CX) + (snkg.y - CY) * (snkg.y - CY));
  bool dead = dc > Rmax;
  float da = sqrtf((snkg.x - snkg.ax) * (snkg.x - snkg.ax) + (snkg.y - snkg.ay) * (snkg.y - snkg.ay));
  if (da < headR + 9)
  {
    snkg.score++;
    snkg.len = min(snkg.len + 2, 120);
    snkg.speed = min(snkg.speed + 4.0f, 170.0f);
    snakeGameSpawnApple();
  }
  for (int i = 3; i < np && !dead; i++)
  {
    float dx = pts[i].x - snkg.x, dy = pts[i].y - snkg.y;
    if (dx * dx + dy * dy < headR * headR * 1.1f)
      dead = true;
  }
  if (dead)
  {
    snkg.over = true;
    if (snkg.score > snkg.best)
    {
      snkg.best = snkg.score;
      prefs.putUShort("snakeBest", (uint16_t)snkg.best);
    }
    return;
  }

  // rendu
  canvas->fillScreen(RGB565_BLACK);
  canvas->drawCircle(CX, CY, (int)(RADIUS - 3), rgb565(70, 70, 90)); // bord arene
  // pomme : rouge + feuille + reflet
  canvas->fillCircle((int)snkg.ax, (int)snkg.ay, 9, rgb565(225, 60, 50));
  canvas->fillRect((int)snkg.ax - 1, (int)snkg.ay - 14, 3, 6, rgb565(90, 180, 80));
  canvas->fillCircle((int)snkg.ax - 3, (int)snkg.ay - 3, 2, rgb565(255, 180, 170));
  // corps (queue -> tete) puis visage
  for (int i = np - 1; i >= 0; i--)
  {
    float sr = headR * (1.0f - 0.25f * i / max(1, snkg.len - 1));
    drawBallSprite((int)pts[i].x, (int)pts[i].y, sr);
  }
  drawMiniFace(pts[0].x, pts[0].y, headR);
  // score
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", snkg.score);
  canvas->setTextSize(2);
  canvas->setTextColor(rgb565(200, 200, 200));
  canvas->setCursor(CX - (int)strlen(buf) * 6, 26);
  canvas->print(buf);
}

// ---- Pong circulaire : raquette en arc qui orbite sur le bord (maintenir
// gauche/droite), balle qui accelere a chaque renvoi, 3 vies. ----

static struct
{
  float padAng, padPrev;
  float bx, by, vx, vy, speed;
  int lives, score, best;
  bool over, serving;
  uint32_t serveMs;
  int palIdx;     // palette de la balle (change a chaque renvoi, comme le DVD)
  uint32_t hitMs; // feedback visuel de la raquette a l'impact
} pong;

static float wrapPi(float a)
{
  while (a > PI)
    a -= 2 * PI;
  while (a < -PI)
    a += 2 * PI;
  return a;
}

static void pongServe()
{
  pong.bx = CX;
  pong.by = CY;
  pong.speed = 130;
  float a = frand(0, 2 * PI);
  pong.vx = cosf(a) * pong.speed;
  pong.vy = sinf(a) * pong.speed;
  pong.serving = true;
  pong.serveMs = millis();
}

static void pongReset()
{
  pong.padAng = PI / 2;
  pong.padPrev = pong.padAng;
  pong.lives = 3;
  pong.score = 0;
  pong.over = false;
  pong.best = prefs.getUShort("pongBest", 0);
  pong.palIdx = 0;
  pong.hitMs = 0;
  pongServe();
}

static void gamePong(float dt)
{
  const float RIM = RADIUS - 10;   // rayon de jeu
  const float PAD_HALF = 0.42f;    // demi-angle de la raquette
  const float ballR = 13; // reduit de 30% (etait 18)

  if (pong.over)
  {
    drawGameOver("GAME OVER", pong.score, pong.best);
    return;
  }
  if (dt > 0.12f)
    dt = 0.12f;

  // raquette : maintenir gauche/droite pour orbiter
  pong.padPrev = pong.padAng;
  if (digitalRead(BTN_PREV) == LOW)
    pong.padAng -= 4.2f * dt;
  if (digitalRead(BTN_NEXT) == LOW)
    pong.padAng += 4.2f * dt;
  float padVel = wrapPi(pong.padAng - pong.padPrev) / max(dt, 0.001f);

  // balle (apres le service)
  if (pong.serving && millis() - pong.serveMs > 900)
    pong.serving = false;
  if (!pong.serving)
  {
    pong.bx += pong.vx * dt;
    pong.by += pong.vy * dt;
    float dx = pong.bx - CX, dy = pong.by - CY;
    float d = sqrtf(dx * dx + dy * dy);
    if (d >= RIM - ballR)
    {
      float nx = dx / d, ny = dy / d;
      float ballAng = atan2f(dy, dx);
      if (fabsf(wrapPi(ballAng - pong.padAng)) <= PAD_HALF)
      {
        // renvoi : reflexion radiale + effet tangentiel de la raquette
        float dot = pong.vx * nx + pong.vy * ny;
        pong.vx -= 2 * dot * nx;
        pong.vy -= 2 * dot * ny;
        pong.vx += -ny * padVel * RIM * 0.22f;
        pong.vy += nx * padVel * RIM * 0.22f;
        pong.speed = min(pong.speed * 1.045f, 420.0f);
        float vn = sqrtf(pong.vx * pong.vx + pong.vy * pong.vy);
        pong.vx = pong.vx / vn * pong.speed;
        pong.vy = pong.vy / vn * pong.speed;
        pong.bx = CX + nx * (RIM - ballR - 1);
        pong.by = CY + ny * (RIM - ballR - 1);
        pong.score++;
        pong.palIdx = (pong.palIdx + 1) % DVD_NPAL; // nouvelle couleur de balle
        pong.hitMs = millis();                      // flash de la raquette
      }
      else
      {
        pong.lives--;
        if (pong.lives <= 0)
        {
          pong.over = true;
          if (pong.score > pong.best)
          {
            pong.best = pong.score;
            prefs.putUShort("pongBest", (uint16_t)pong.best);
          }
          return;
        }
        pongServe();
      }
    }
  }

  // rendu
  canvas->fillScreen(rgb565(6, 8, 18));
  canvas->drawCircle(CX, CY, (int)RIM + 4, rgb565(60, 65, 95)); // bord
  // raquette : arc plein a bouts arrondis. A l'impact : flash vers le blanc
  // + epaississement bref (decroit sur 180 ms).
  float hitK = 0;
  if (pong.hitMs)
  {
    uint32_t dh = millis() - pong.hitMs;
    if (dh < 180)
      hitK = 1.0f - dh / 180.0f;
  }
  float thick = 5 + 3 * hitK; // demi-epaisseur
  uint16_t pc = rgb565(255, 213 + (int)(42 * hitK), 48 + (int)(207 * hitK));
  int steps = (int)(2 * PAD_HALF * (RIM + thick)) + 2; // ~1 trait radial par pixel d'arc
  for (int i = 0; i <= steps; i++)
  {
    float a = pong.padAng - PAD_HALF + 2 * PAD_HALF * i / steps;
    float ca = cosf(a), sa = sinf(a);
    canvas->drawLine(CX + (int)(ca * (RIM - thick)), CY + (int)(sa * (RIM - thick)),
                     CX + (int)(ca * (RIM + thick)), CY + (int)(sa * (RIM + thick)), pc);
  }
  for (int s = -1; s <= 1; s += 2)
  {
    float a = pong.padAng + s * PAD_HALF;
    canvas->fillCircle(CX + (int)(cosf(a) * RIM), CY + (int)(sinf(a) * RIM), (int)thick, pc);
  }
  // balle : sphere dont la palette change a chaque renvoi (comme l'anim DVD) ;
  // clignote pendant le service
  bool blink = pong.serving && ((millis() / 150) % 2 == 0);
  if (!blink)
    dvdBlit(dvdSprites[pong.palIdx], (int)pong.bx, (int)pong.by, ballR, 255);
  // score + vies (coeurs)
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", pong.score);
  canvas->setTextSize(2);
  canvas->setTextColor(rgb565(200, 200, 200));
  canvas->setCursor(CX - (int)strlen(buf) * 6, 30);
  canvas->print(buf);
  for (int i = 0; i < pong.lives; i++)
  {
    int hx = CX - 24 + i * 24, hy = 60;
    uint16_t hc = rgb565(235, 70, 90);
    canvas->fillCircle(hx - 3, hy, 4, hc);
    canvas->fillCircle(hx + 3, hy, 4, hc);
    canvas->fillTriangle(hx - 7, hy + 2, hx + 7, hy + 2, hx, hy + 10, hc);
  }
}


// ==== Fournee Three.js ======================================================

// Petites etoiles de fond deterministes (Blaster / FPS Rush)
static void drawStars()
{
  for (int i = 0; i < 40; i++)
  {
    int x = (i * 97 + 31) % W, y = (i * 151 + 67) % H;
    canvas->drawPixel(x, y, rgb565(70, 70, 90));
  }
}

// Mini cube wireframe "MeshNormalMaterial" : deux carres decales + aretes
static void drawWireCube(int cx, int cy, float r, float rot, uint8_t hue)
{
  uint16_t c1 = hsv2rgb565(hue, 220, 255);
  uint16_t c2 = hsv2rgb565((uint8_t)(hue + 60), 220, 200);
  int xa[4], ya[4], xb[4], yb[4];
  for (int k = 0; k < 4; k++)
  {
    float a = rot + PI / 4 + k * PI / 2;
    xa[k] = cx + (int)(cosf(a) * r);
    ya[k] = cy + (int)(sinf(a) * r);
    xb[k] = cx + (int)(cosf(a + 0.5f) * r * 0.72f) + (int)(r * 0.25f);
    yb[k] = cy + (int)(sinf(a + 0.5f) * r * 0.72f) - (int)(r * 0.25f);
  }
  for (int k = 0; k < 4; k++)
  {
    int n = (k + 1) % 4;
    canvas->drawLine(xa[k], ya[k], xa[n], ya[n], c1);
    canvas->drawLine(xb[k], yb[k], xb[n], yb[n], c2);
    canvas->drawLine(xa[k], ya[k], xb[k], yb[k], c2);
  }
}

// Mini donut (torus de face) : anneau plein + trou
static void drawDonutSprite(int cx, int cy, float r, uint8_t hue, uint16_t bg)
{
  canvas->fillCircle(cx, cy, (int)r, hsv2rgb565(hue, 200, 255));
  canvas->fillCircle(cx, cy, (int)(r * 0.45f), bg);
  canvas->drawCircle(cx, cy, (int)r, hsv2rgb565((uint8_t)(hue + 40), 220, 160));
}

// ---- Sphere Runner v2 : le perso ROULE (de profil) sur la planete wireframe,
// pics/cubes a sauter, donuts a collecter en l'air, cube-soleil Three.js. ----

// Blit du sprite de sphere TOURNE : le perso roule visiblement
static void drawBallSpriteRot(int cx, int cy, float rf, float ang)
{
  int r = (int)rf;
  if (r < 2 || !ballSprite)
    return;
  uint16_t *fb = canvas->getFramebuffer();
  const float half = SPR / 2 - 1;
  float ca = cosf(ang), sa = sinf(ang);
  for (int dy = -r; dy <= r; dy++)
  {
    int yy = cy + dy;
    if (yy < 0 || yy >= H)
      continue;
    int span = (int)sqrtf((float)(r * r - dy * dy));
    uint16_t *drow = &fb[yy * W];
    int x0 = max(-span, -cx), x1 = min(span, W - 1 - cx);
    for (int dx = x0; dx <= x1; dx++)
    {
      int u = (int)((dx * ca - dy * sa) / r * half) + SPR / 2;
      int v = (int)((dx * sa + dy * ca) / r * half) + SPR / 2;
      drow[cx + dx] = ballSprite[v * SPR + u];
    }
  }
}

// Visage de PROFIL (regarde vers la droite, d'ou arrivent les obstacles) —
// fixe pendant que la texture roule dessous, facon cartoon.
static void drawProfileFace(float cx, float cy, float r)
{
  uint16_t ink = rgb565(39, 39, 39);
  canvas->fillCircle((int)(cx + r * 0.42f), (int)(cy - r * 0.24f), max(1, (int)(r * 0.14f)), ink);
  // petite bouche souriante sur le bord avant
  canvas->drawLine((int)(cx + r * 0.48f), (int)(cy + r * 0.28f),
                   (int)(cx + r * 0.74f), (int)(cy + r * 0.14f), ink);
  canvas->drawLine((int)(cx + r * 0.48f), (int)(cy + r * 0.29f),
                   (int)(cx + r * 0.74f), (int)(cy + r * 0.15f), ink);
}

static struct
{
  float h, vh, worldAng, speed, rollAng;
  struct { float ang; uint8_t type; bool on; } ob[5]; // 0 pic, 1 cube, 2 double pic
  struct { float ang; bool on; } dn[3];               // donuts a collecter
  struct { float x, y, vx, vy, life; uint16_t c; } dust[8];
  int score, best, milestone;
  bool over;
  uint32_t spawnMs, milestoneMs;
} rn;

static void runnerReset()
{
  memset(&rn, 0, sizeof(rn));
  rn.speed = 1.1f;
  rn.best = prefs.getUShort("runBest", 0);
  rn.spawnMs = millis();
}

static void runnerDust(float x, float y, uint16_t c)
{
  for (int i = 0; i < 8; i++)
    if (rn.dust[i].life <= 0)
    {
      rn.dust[i] = {x, y, frand(-70, -30), frand(-40, 10), 0.45f, c};
      return;
    }
}

static void gameRunner(float dt)
{
  const float RG = RADIUS - 12; // rayon du sol
  const float PANG = PI / 2;    // le perso est en bas de l'ecran
  if (rn.over)
  {
    drawGameOver("GAME OVER", rn.score, rn.best);
    return;
  }
  if (dt > 0.12f)
    dt = 0.12f;
  uint32_t ms = millis();

  // saut (bouton central)
  bool grounded = rn.h <= 0.01f;
  if (gBtnCenter && grounded)
  {
    rn.vh = 300;
    runnerDust(CX - 8, CY + RG - 18, rgb565(150, 150, 170));
  }
  rn.vh -= 900 * dt;
  rn.h += rn.vh * dt;
  if (rn.h < 0)
  {
    rn.h = 0;
    rn.vh = 0;
  }
  grounded = rn.h <= 0.01f;

  // le monde tourne, ca accelere ; la sphere roule (rotation de la texture)
  rn.speed = min(1.1f + rn.score * 0.010f, 2.6f);
  rn.worldAng += rn.speed * dt;
  if (grounded)
    rn.rollAng += rn.speed * 4.2f * dt;
  int sc = (int)(rn.worldAng * 8);
  if (sc / 100 > rn.milestone)
  {
    rn.milestone = sc / 100;
    rn.milestoneMs = ms; // flash du score aux paliers de 100
  }
  if (sc > rn.score)
    rn.score = sc;

  // spawn : obstacles (varies) et donuts a collecter
  if (ms - rn.spawnMs > (uint32_t)(1900 / rn.speed))
  {
    rn.spawnMs = ms;
    if (frand(0, 1) < 0.35f)
    {
      for (int i = 0; i < 3; i++)
        if (!rn.dn[i].on)
        {
          rn.dn[i].ang = PANG - rn.worldAng - frand(2.4f, 3.4f);
          rn.dn[i].on = true;
          break;
        }
    }
    else
      for (int i = 0; i < 5; i++)
        if (!rn.ob[i].on)
        {
          rn.ob[i].ang = PANG - rn.worldAng - frand(2.4f, 3.4f);
          float r = frand(0, 1);
          rn.ob[i].type = r < 0.5f ? 0 : (r < 0.8f ? 1 : 2);
          rn.ob[i].on = true;
          break;
        }
  }
  // obstacles : collision selon le type
  for (int i = 0; i < 5; i++)
  {
    if (!rn.ob[i].on)
      continue;
    float sa = wrapPi(rn.ob[i].ang + rn.worldAng - PANG);
    float halfW = rn.ob[i].type == 2 ? 0.18f : 0.11f;
    float minH = rn.ob[i].type == 1 ? 30.0f : 24.0f;
    if (sa > 1.6f)
      rn.ob[i].on = false;
    else if (fabsf(sa) < halfW && rn.h < minH)
    {
      rn.over = true;
      if (rn.score > rn.best)
      {
        rn.best = rn.score;
        prefs.putUShort("runBest", (uint16_t)rn.best);
      }
      return;
    }
  }
  // donuts : a attraper en sautant
  for (int i = 0; i < 3; i++)
  {
    if (!rn.dn[i].on)
      continue;
    float sa = wrapPi(rn.dn[i].ang + rn.worldAng - PANG);
    if (sa > 1.6f)
      rn.dn[i].on = false;
    else if (fabsf(sa) < 0.14f && rn.h > 16 && rn.h < 62)
    {
      rn.dn[i].on = false;
      rn.score += 5;
      for (int k = 0; k < 3; k++)
        runnerDust(CX, CY + RG - 50, rgb565(255, 213, 48)); // confettis dores
    }
  }
  // poussiere de roulement
  if (grounded && (ms % 3) == 0)
    runnerDust(CX - 10, CY + RG - 16, rgb565(110, 110, 130));
  for (int i = 0; i < 8; i++)
    if (rn.dust[i].life > 0)
    {
      rn.dust[i].life -= dt;
      rn.dust[i].x += rn.dust[i].vx * dt;
      rn.dust[i].y += rn.dust[i].vy * dt;
    }

  // ---- rendu ----
  canvas->fillScreen(rgb565(7, 7, 14));
  drawStars();
  // cube-soleil "MeshNormalMaterial" qui tourne lentement dans le ciel
  drawWireCube(CX, CY - 34, 16, ms / 1400.0f, (uint8_t)(ms / 60));
  // planete wireframe : double cercle + graduations qui defilent
  canvas->drawCircle(CX, CY, (int)RG, rgb565(110, 110, 140));
  canvas->drawCircle(CX, CY, (int)RG + 4, rgb565(60, 60, 85));
  for (int i = 0; i < 24; i++)
  {
    float a = i * 2 * PI / 24 + rn.worldAng;
    canvas->drawLine(CX + (int)(cosf(a) * (RG - 8)), CY + (int)(sinf(a) * (RG - 8)),
                     CX + (int)(cosf(a) * RG), CY + (int)(sinf(a) * RG), rgb565(80, 80, 105));
  }
  // obstacles
  for (int i = 0; i < 5; i++)
    if (rn.ob[i].on)
    {
      float a = rn.ob[i].ang + rn.worldAng;
      uint16_t c = hsv2rgb565((uint8_t)(i * 60 + 180), 220, 255);
      if (rn.ob[i].type == 1)
      {
        // cube pose sur le sol
        drawWireCube(CX + (int)(cosf(a) * (RG - 14)), CY + (int)(sinf(a) * (RG - 14)),
                     12, a, (uint8_t)(i * 60));
      }
      else
      {
        int n = rn.ob[i].type == 2 ? 2 : 1;
        for (int k = 0; k < n; k++)
        {
          float aa = a + (n == 2 ? (k == 0 ? -0.09f : 0.09f) : 0);
          int bx1 = CX + (int)(cosf(aa - 0.06f) * RG), by1 = CY + (int)(sinf(aa - 0.06f) * RG);
          int bx2 = CX + (int)(cosf(aa + 0.06f) * RG), by2 = CY + (int)(sinf(aa + 0.06f) * RG);
          int tx = CX + (int)(cosf(aa) * (RG - 28)), ty = CY + (int)(sinf(aa) * (RG - 28));
          canvas->drawLine(bx1, by1, tx, ty, c);
          canvas->drawLine(bx2, by2, tx, ty, c);
          canvas->drawLine(bx1, by1, bx2, by2, c);
        }
      }
    }
  // donuts flottants (teinte qui tourne)
  for (int i = 0; i < 3; i++)
    if (rn.dn[i].on)
    {
      float a = rn.dn[i].ang + rn.worldAng;
      drawDonutSprite(CX + (int)(cosf(a) * (RG - 38)), CY + (int)(sinf(a) * (RG - 38)),
                      10, (uint8_t)(ms / 30 + i * 60), rgb565(7, 7, 14));
    }
  // poussiere
  for (int i = 0; i < 8; i++)
    if (rn.dust[i].life > 0)
      canvas->fillRect((int)rn.dust[i].x, (int)rn.dust[i].y, 2, 2, rn.dust[i].c);
  // perso : sphere qui ROULE + visage de profil
  float pr = RG - 15 - rn.h;
  int px = CX + (int)(cosf(PANG) * pr), py = CY + (int)(sinf(PANG) * pr);
  drawBallSpriteRot(px, py, 14, rn.rollAng);
  drawProfileFace(px, py, 14);
  // score (flash dore aux paliers de 100)
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", rn.score);
  canvas->setTextSize(2);
  canvas->setTextColor(ms - rn.milestoneMs < 700 && rn.milestone > 0 ? rgb565(255, 213, 48)
                                                                     : rgb565(200, 200, 200));
  canvas->setCursor(CX - (int)strlen(buf) * 6, 26);
  canvas->print(buf);
}


// ---- Roundtris : Tetris circulaire — le puits est un anneau sans murs
// lateraux (ca boucle sur 360 degres), les pieces tombent du bord vers le
// centre, un anneau complet s'efface. Gauche/droite : orbiter ; centre :
// pivoter. La piece suivante et le score s'affichent dans le noyau. ----

#define TT_RINGS 10
#define TT_SECT 16
#define TT_R0 26.0f  // rayon du noyau (le "sol")
#define TT_DR 14.0f  // epaisseur d'un anneau

// tetrominos : offsets (dx = angulaire, dy = radial), pivote par (dx,dy)->(-dy,dx)
static const int8_t TT_SHAPES[7][4][2] = {
    {{-1, 0}, {0, 0}, {1, 0}, {2, 0}},  // I
    {{0, 0}, {1, 0}, {0, 1}, {1, 1}},   // O
    {{-1, 0}, {0, 0}, {1, 0}, {0, 1}},  // T
    {{-1, 0}, {0, 0}, {1, 0}, {-1, 1}}, // L
    {{-1, 0}, {0, 0}, {1, 0}, {1, 1}},  // J
    {{0, 0}, {1, 0}, {-1, 1}, {0, 1}},  // S
    {{-1, 0}, {0, 0}, {0, 1}, {1, 1}}}; // Z

static struct
{
  uint8_t grid[TT_RINGS][TT_SECT]; // 0 vide, sinon 1 + type (couleur)
  int type, rot, ring, sect;       // piece qui tombe
  int nextType;
  int score, best;
  bool over;
  uint32_t fallMs, moveMs;
} tt;

static uint16_t ttColor(uint8_t v)
{
  static const uint8_t hues[7] = {128, 42, 200, 21, 160, 85, 0}; // I O T L J S Z
  return hsv2rgb565(hues[(v - 1) % 7], 220, 235);
}

// cellules absolues de la piece courante (rot appliquee) ; renvoie false si collision
static bool ttCells(int ring, int sect, int rot, int out[4][2])
{
  for (int i = 0; i < 4; i++)
  {
    int dx = TT_SHAPES[tt.type][i][0], dy = TT_SHAPES[tt.type][i][1];
    for (int k = 0; k < rot; k++)
    {
      int t2 = dx;
      dx = -dy;
      dy = t2;
    }
    int r = ring + dy;
    int s = ((sect + dx) % TT_SECT + TT_SECT) % TT_SECT;
    if (r < 0 || r >= TT_RINGS || tt.grid[r][s])
      return false;
    out[i][0] = r;
    out[i][1] = s;
  }
  return true;
}

static void ttSpawn()
{
  tt.type = tt.nextType;
  tt.nextType = random(7);
  tt.rot = 0;
  tt.ring = TT_RINGS - 2;
  tt.sect = random(TT_SECT);
  int c[4][2];
  if (!ttCells(tt.ring, tt.sect, tt.rot, c))
  {
    tt.over = true;
    if (tt.score > tt.best)
    {
      tt.best = tt.score;
      prefs.putUShort("tetroBest", (uint16_t)tt.best);
    }
  }
}

static void tetroReset()
{
  memset(&tt, 0, sizeof(tt));
  tt.best = prefs.getUShort("tetroBest", 0);
  tt.nextType = random(7);
  ttSpawn();
  tt.fallMs = millis();
}

// dessine une cellule (quartier d'anneau) : 2 triangles + liseret sombre
static void ttDrawCell(int ring, int sect, uint16_t col)
{
  float a0 = sect * 2 * PI / TT_SECT - PI / 2;
  float a1 = a0 + 2 * PI / TT_SECT;
  float r0 = TT_R0 + ring * TT_DR + 1, r1 = TT_R0 + (ring + 1) * TT_DR - 1;
  int x00 = CX + (int)(cosf(a0) * r0), y00 = CY + (int)(sinf(a0) * r0);
  int x01 = CX + (int)(cosf(a1) * r0), y01 = CY + (int)(sinf(a1) * r0);
  int x10 = CX + (int)(cosf(a0) * r1), y10 = CY + (int)(sinf(a0) * r1);
  int x11 = CX + (int)(cosf(a1) * r1), y11 = CY + (int)(sinf(a1) * r1);
  canvas->fillTriangle(x00, y00, x01, y01, x11, y11, col);
  canvas->fillTriangle(x00, y00, x11, y11, x10, y10, col);
  canvas->drawLine(x00, y00, x10, y10, RGB565_BLACK);
  canvas->drawLine(x01, y01, x11, y11, RGB565_BLACK);
}

static void gameTetro(float dt)
{
  (void)dt;
  if (tt.over)
  {
    drawGameOver("GAME OVER", tt.score, tt.best);
    return;
  }
  uint32_t ms = millis();

  // deplacements angulaires : maintien avec repetition (300 ms de debounce
  // serait trop lent pour un tetris)
  if (ms - tt.moveMs > 150)
  {
    int d = 0;
    if (digitalRead(BTN_PREV) == LOW)
      d--;
    if (digitalRead(BTN_NEXT) == LOW)
      d++;
    if (d)
    {
      int ns = ((tt.sect + d) % TT_SECT + TT_SECT) % TT_SECT;
      int c[4][2];
      if (ttCells(tt.ring, ns, tt.rot, c))
        tt.sect = ns;
      tt.moveMs = ms;
    }
  }
  // rotation (centre)
  if (gBtnCenter)
  {
    int nr = (tt.rot + 1) % 4;
    int c[4][2];
    if (ttCells(tt.ring, tt.sect, nr, c))
      tt.rot = nr;
  }
  // chute (vers le centre), cadence qui accelere avec le score
  uint32_t interval = (uint32_t)max(280, 750 - tt.score * 4);
  if (ms - tt.fallMs > interval)
  {
    tt.fallMs = ms;
    int c[4][2];
    if (ttCells(tt.ring - 1, tt.sect, tt.rot, c))
      tt.ring--;
    else
    {
      // atterrissage : fige la piece, efface les anneaux complets
      if (!ttCells(tt.ring, tt.sect, tt.rot, c))
      {
        tt.over = true;
        return;
      }
      for (int i = 0; i < 4; i++)
        tt.grid[c[i][0]][c[i][1]] = 1 + tt.type;
      int cleared = 0;
      for (int r = 0; r < TT_RINGS; r++)
      {
        bool full = true;
        for (int s = 0; s < TT_SECT; s++)
          if (!tt.grid[r][s])
            full = false;
        if (full)
        {
          cleared++;
          for (int rr = r; rr < TT_RINGS - 1; rr++)
            memcpy(tt.grid[rr], tt.grid[rr + 1], TT_SECT);
          memset(tt.grid[TT_RINGS - 1], 0, TT_SECT);
          r--; // recheck le meme anneau (cascade)
        }
      }
      tt.score += 1 + 10 * cleared * cleared;
      ttSpawn();
    }
  }

  // ---- rendu ----
  canvas->fillScreen(rgb565(8, 8, 16));
  // guides : cercle exterieur + noyau
  canvas->drawCircle(CX, CY, (int)(TT_R0 + TT_RINGS * TT_DR) + 2, rgb565(55, 55, 80));
  canvas->fillCircle(CX, CY, (int)TT_R0 - 2, rgb565(16, 16, 30));
  canvas->drawCircle(CX, CY, (int)TT_R0 - 2, rgb565(55, 55, 80));
  // pile
  for (int r = 0; r < TT_RINGS; r++)
    for (int s = 0; s < TT_SECT; s++)
      if (tt.grid[r][s])
        ttDrawCell(r, s, ttColor(tt.grid[r][s]));
  // piece qui tombe
  int c[4][2];
  if (ttCells(tt.ring, tt.sect, tt.rot, c))
    for (int i = 0; i < 4; i++)
      ttDrawCell(c[i][0], c[i][1], ttColor(1 + tt.type));
  // noyau : score + piece suivante en mini
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", tt.score);
  canvas->setTextSize(2);
  canvas->setTextColor(rgb565(200, 200, 200));
  canvas->setCursor(CX - (int)strlen(buf) * 6, CY - 16);
  canvas->print(buf);
  for (int i = 0; i < 4; i++)
  {
    int dx = TT_SHAPES[tt.nextType][i][0], dy = TT_SHAPES[tt.nextType][i][1];
    canvas->fillRect(CX - 3 + dx * 5, CY + 8 - dy * 5, 4, 4, ttColor(1 + tt.nextType));
  }
}

#include "tama.h"

// ==== Dispatch commun =======================================================

static void gameReset(int gi)
{
  if (g_ballDirty) // avatar/buddy change : re-teinte le sprite de boule
  {
    g_ballDirty = false;
    initBallSprite();
  }
  switch (gi)
  {
  case 0: snakeGameReset(); break;
  case 1: pongReset(); break;
  case 2: runnerReset(); break;
  case 3: tetroReset(); break;
  case 4: tamaReset(); break;
  }
}

static void gameUpdate(int gi, float dt)
{
  switch (gi)
  {
  case 0: gameSnake(dt); break;
  case 1: gamePong(dt); break;
  case 2: gameRunner(dt); break;
  case 3: gameTetro(dt); break;
  case 4: gameTama(dt); break;
  }
}

static bool gameIsOver(int gi)
{
  switch (gi)
  {
  case 0: return snkg.over;
  case 1: return pong.over;
  case 2: return rn.over;
  case 3: return tt.over;
  case 4: return false; // le pet ne meurt pas
  }
  return false;
}

// jeux dont le bouton central est une ACTION de jeu (sortie = gauche+droite 0.8 s)
static bool gameCenterIsAction(int gi)
{
  return gi == 2 || gi == 3 || gi == 4; // Sphere Run : sauter ; Roundtris : pivoter ; Pet : agir
}
