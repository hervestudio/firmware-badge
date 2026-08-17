// Sphere Pet : tamagotchi isometrique noir & blanc pixel-art — seul le perso
// (la sphere arc-en-ciel) est en couleur. Trois jauges (faim / humeur /
// energie), actions FEED / PLAY / SLEEP / CLEAN au bouton central, crottes a
// nettoyer, sieste qui eteint la piece. Stats persistees en NVS.
// Inclus par games.h (canvas, rgb565, hsv2rgb565, frand, drawBallSpriteRot,
// prefs, gBtn*). Compile aussi dans le harnais avec TAMA_HARNESS defini.
#pragma once

#ifdef TAMA_HARNESS
static bool gBtnCenter = false, gBtnLeft = false, gBtnRight = false;
// copie du blit tourne de games.h (le harnais n'inclut pas games.h)
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
    int x0 = -span < -cx ? -cx : -span, x1 = span > W - 1 - cx ? W - 1 - cx : span;
    for (int dx = x0; dx <= x1; dx++)
    {
      int u = (int)((dx * ca - dy * sa) / r * half) + SPR / 2;
      int v = (int)((dx * sa + dy * ca) / r * half) + SPR / 2;
      drow[cx + dx] = ballSprite[v * SPR + u];
    }
  }
}
#endif

#define TAMA_NPOOP 3
static struct
{
  float food, fun, energy; // jauges 0..100
  bool sleeping;
  float t;                        // horloge locale
  int sel;                        // action selectionnee (0..3)
  float eatT, playT, cleanT;      // anims d'action en cours (s restantes)
  float poopTimer;                // prochaine crotte apres un repas
  bool poop[TAMA_NPOOP];
  float bx, prevBx, rollAng;      // perso : offset + roulis pendant PLAY
  float saveT;
} tama;

static const int TAMA_POOP_XY[TAMA_NPOOP][2] = {{132, 212}, {184, 230}, {104, 194}};
#define TAMA_BALL_X 180
#define TAMA_BALL_Y 194
#define TAMA_BALL_R 22

static void tamaSave()
{
#ifndef TAMA_HARNESS
  prefs.putUShort("petFood", (uint16_t)tama.food);
  prefs.putUShort("petFun", (uint16_t)tama.fun);
  prefs.putUShort("petNrj", (uint16_t)tama.energy);
#endif
}

static void tamaReset()
{
  memset(&tama, 0, sizeof(tama));
  tama.food = tama.fun = tama.energy = 80;
#ifndef TAMA_HARNESS
  tama.food = prefs.getUShort("petFood", 80);
  tama.fun = prefs.getUShort("petFun", 80);
  tama.energy = prefs.getUShort("petNrj", 80);
#endif
}

// ------------------------------------------------------------ pixel helpers

// Glyphe "Z" en 3 traits (pas de moteur de texte : pixel-art oblige)
static void tamaZ(int x, int y, int s, uint16_t c)
{
  canvas->drawLine(x, y, x + s, y, c);
  canvas->drawLine(x + s, y, x, y + s, c);
  canvas->drawLine(x, y + s, x + s, y + s, c);
}

// Donut en fil de fer (blanc : seul le perso a droit a la couleur)
static void tamaDonutIcon(int x, int y, int r, uint16_t c)
{
  canvas->drawCircle(x, y, r, c);
  canvas->drawCircle(x, y, r > 4 ? r / 2 - 1 : 1, c);
  canvas->drawPixel(x - r / 2, y - r / 2, c);
  canvas->drawPixel(x + r / 2, y - r / 3, c);
  canvas->drawPixel(x, y + r - 2, c);
}

static void tamaHeart(int x, int y, int s, uint16_t c)
{
  canvas->fillCircle(x - s / 2, y, s / 2, c);
  canvas->fillCircle(x + s / 2, y, s / 2, c);
  canvas->fillTriangle(x - s, y + 1, x + s, y + 1, x, y + s + s / 2, c);
}

static void tamaPoopDraw(int x, int y, uint16_t c)
{
  canvas->drawCircle(x, y, 5, c);
  canvas->drawCircle(x, y - 5, 3, c);
  canvas->drawPixel(x + 1, y - 9, c);
  canvas->drawPixel(x + 2, y - 10, c);
  canvas->drawLine(x - 5, y + 4, x + 5, y + 4, c);
}

// ---------------------------------------------------------------- la piece

// Piece isometrique (2:1) facon pixel-art : coin de murs, fenetre a stores,
// meuble TV, plante, table basse, tapis sous le perso.
static void tamaRoom(uint16_t ink, uint16_t dim)
{
  // coins : A haut du coin, A' pied du coin, B/C extremites hautes des murs,
  // B'/C' pieds, D coin avant du sol
  const int Ax = 180, Ay = 52, Afy = 124;
  const int Bx = 66, By = 109, Bfy = 181;
  const int Cx2 = 294, Cy2 = 109, Cfy = 181;
  const int Dx = 180, Dy = 238;

  // murs
  canvas->drawLine(Ax, Ay, Bx, By, ink);
  canvas->drawLine(Ax, Ay, Cx2, Cy2, ink);
  canvas->drawLine(Ax, Ay, Ax, Afy, ink);
  canvas->drawLine(Bx, By, Bx, Bfy, ink);
  canvas->drawLine(Cx2, Cy2, Cx2, Cfy, ink);
  // plinthes (jonction mur/sol)
  canvas->drawLine(Ax, Afy, Bx, Bfy, dim);
  canvas->drawLine(Ax, Afy, Cx2, Cfy, dim);
  // sol
  canvas->drawLine(Bx, Bfy, Dx, Dy, ink);
  canvas->drawLine(Dx, Dy, Cx2, Cfy, ink);
  // rebord avant "plateforme flottante" (marches pixel du bas de la ref)
  canvas->drawLine(Bx, Bfy + 8, Dx, Dy + 8, dim);
  canvas->drawLine(Dx, Dy + 8, Cx2, Cfy + 8, dim);
  canvas->drawLine(Bx, Bfy, Bx, Bfy + 8, dim);
  canvas->drawLine(Dx, Dy, Dx, Dy + 8, dim);
  canvas->drawLine(Cx2, Cfy, Cx2, Cfy + 8, dim);

  // fenetre a stores sur le mur gauche
  const int W1x = 152, W1y = 74, W2x = 95, W2y = 103, wh = 32;
  canvas->drawLine(W1x, W1y, W2x, W2y, ink);
  canvas->drawLine(W2x, W2y, W2x, W2y + wh, ink);
  canvas->drawLine(W2x, W2y + wh, W1x, W1y + wh, ink);
  canvas->drawLine(W1x, W1y + wh, W1x, W1y, ink);
  for (int k = 1; k <= 4; k++) // stores
    canvas->drawLine(W1x - 2, W1y + k * 6, W2x + 2, W2y + k * 6, dim);

  // meuble TV contre le mur gauche (boite iso) + ecran
  canvas->drawLine(84, 144, 136, 118, ink);  // haut arriere
  canvas->drawLine(136, 118, 156, 128, ink); // haut droit
  canvas->drawLine(156, 128, 104, 154, ink); // haut avant
  canvas->drawLine(104, 154, 84, 144, ink);  // haut gauche
  canvas->drawLine(84, 144, 84, 168, ink);   // aretes verticales
  canvas->drawLine(104, 154, 104, 178, ink);
  canvas->drawLine(156, 128, 156, 152, ink);
  canvas->drawLine(84, 168, 104, 178, ink); // bas
  canvas->drawLine(104, 178, 156, 152, ink);
  canvas->drawLine(120, 146, 120, 168, dim); // porte du meuble
  // televiseur pose dessus
  canvas->drawLine(108, 116, 134, 103, ink);
  canvas->drawLine(134, 103, 134, 125, ink);
  canvas->drawLine(134, 125, 108, 138, ink);
  canvas->drawLine(108, 138, 108, 116, ink);
  canvas->drawLine(112, 119, 130, 110, dim); // reflets d'ecran
  canvas->drawLine(112, 125, 130, 116, dim);

  // plante en pot (mur droit)
  canvas->drawLine(240, 152, 252, 152, ink);
  canvas->drawLine(240, 152, 242, 164, ink);
  canvas->drawLine(252, 152, 250, 164, ink);
  canvas->drawLine(242, 164, 250, 164, ink);
  canvas->drawLine(246, 152, 246, 130, ink); // tige
  canvas->drawLine(246, 140, 237, 128, ink);
  canvas->drawLine(246, 136, 255, 126, ink);
  canvas->drawCircle(236, 124, 4, ink);
  canvas->drawCircle(256, 122, 4, ink);
  canvas->drawCircle(246, 116, 5, ink);

  // table basse (droite)
  canvas->drawLine(236, 186, 280, 208, ink);
  canvas->drawLine(280, 208, 236, 230, ink);
  canvas->drawLine(236, 230, 192, 208, ink);
  canvas->drawLine(192, 208, 236, 186, ink);
  canvas->drawLine(192, 208, 192, 220, ink);
  canvas->drawLine(236, 230, 236, 242, ink);
  canvas->drawLine(280, 208, 280, 220, ink);
  canvas->drawLine(192, 220, 236, 242, ink);
  canvas->drawLine(236, 242, 280, 220, ink);
  canvas->drawCircle(246, 203, 4, dim); // bol
  canvas->drawLine(220, 210, 230, 215, dim); // zine
  canvas->drawLine(230, 215, 236, 212, dim);

  // tapis sous le perso
  canvas->drawLine(120, 196, 172, 170, dim);
  canvas->drawLine(172, 170, 224, 196, dim);
  canvas->drawLine(224, 196, 172, 222, dim);
  canvas->drawLine(172, 222, 120, 196, dim);
}

// -------------------------------------------------------------------- HUD

static void tamaBar(int x, int y, float v, uint16_t ink)
{
  canvas->drawRect(x, y, 38, 8, ink);
  int w = (int)(v * 34 / 100);
  if (w > 0)
    canvas->fillRect(x + 2, y + 2, w, 4, ink);
}

static void tamaHud(uint16_t ink)
{
  // faim (donut) / humeur (coeur) / energie (Z)
  tamaDonutIcon(88, 34, 5, ink);
  tamaBar(98, 30, tama.food, ink);
  tamaHeart(160, 33, 4, ink);
  tamaBar(170, 30, tama.fun, ink);
  tamaZ(230, 30, 7, ink);
  tamaBar(242, 30, tama.energy, ink);
}

static void tamaActionBar(uint16_t ink)
{
  const int y = 292, bw = 36, bh = 32;
  for (int i = 0; i < 4; i++)
  {
    int x = CX - 87 + i * 44 - bw / 2;
    bool on = (i == tama.sel);
    if (on)
      canvas->fillRect(x, y, bw, bh, ink);
    else
      canvas->drawRect(x, y, bw, bh, ink);
    uint16_t ic = on ? RGB565_BLACK : ink;
    int cx = x + bw / 2, cy = y + bh / 2;
    switch (i)
    {
    case 0: // FEED : donut
      tamaDonutIcon(cx, cy, 7, ic);
      break;
    case 1: // PLAY : balle qui rebondit
      canvas->drawCircle(cx, cy - 2, 6, ic);
      canvas->drawLine(cx - 6, cy + 9, cx + 6, cy + 9, ic);
      canvas->drawPixel(cx - 8, cy + 5, ic);
      canvas->drawPixel(cx + 8, cy + 5, ic);
      break;
    case 2: // SLEEP : lune + Z
      canvas->drawCircle(cx - 3, cy, 7, ic);
      canvas->fillCircle(cx + 1, cy - 2, 6, on ? ink : RGB565_BLACK);
      tamaZ(cx + 5, cy - 8, 4, ic);
      break;
    case 3: // CLEAN : balai
      canvas->drawLine(cx + 7, cy - 10, cx - 2, cy + 4, ic);
      canvas->drawLine(cx - 2, cy + 4, cx - 8, cy + 10, ic);
      canvas->drawLine(cx - 5, cy + 3, cx - 2, cy + 10, ic);
      canvas->drawLine(cx - 8, cy + 5, cx - 1, cy + 10, ic);
      break;
    }
  }
}

// ------------------------------------------------------------------ perso

static void tamaFace(float cx, float cy, float r, int mood, bool closedEyes,
                     bool eating)
{
  uint16_t ink = rgb565(39, 39, 39);
  int ex0 = (int)(cx - r * 0.34f), ex1 = (int)(cx + r * 0.34f);
  int ey = (int)(cy - r * 0.2f);
  int er = (int)(r * 0.11f) < 1 ? 1 : (int)(r * 0.11f);
  if (closedEyes)
  {
    canvas->drawLine(ex0 - er, ey, ex0 + er, ey, ink);
    canvas->drawLine(ex1 - er, ey, ex1 + er, ey, ink);
  }
  else
  {
    canvas->fillCircle(ex0, ey, er, ink);
    canvas->fillCircle(ex1, ey, er, ink);
  }
  if (eating)
  {
    canvas->fillCircle((int)cx, (int)(cy + r * 0.28f), (int)(r * 0.18f), ink);
    return;
  }
  int px = 0, py = 0;
  for (int i = -2; i <= 2; i++) // bouche : sourire, ou parabole inversee si triste
  {
    int xx = (int)(cx + i * r * 0.14f);
    float arc = (2 * 2 - i * i) * r * 0.045f;
    int yy = mood > 0 ? (int)(cy + r * 0.18f + arc)
                      : (int)(cy + r * 0.30f - arc);
    if (mood == 0)
      yy = (int)(cy + r * 0.26f); // neutre : trait droit
    if (i > -2)
    {
      canvas->drawLine(px, py, xx, yy, ink);
      canvas->drawLine(px, py + 1, xx, yy + 1, ink);
    }
    px = xx;
    py = yy;
  }
}

// ------------------------------------------------------------------- jeu

static void tamaDoAction()
{
  if (tama.eatT > 0 || tama.playT > 0 || tama.cleanT > 0)
    return; // une anim a la fois
  if (tama.sleeping && tama.sel != 2)
    return; // il dort : seul SLEEP (reveil) repond
  switch (tama.sel)
  {
  case 0:
    if (tama.food < 97)
      tama.eatT = 1.8f;
    break;
  case 1:
    if (!tama.sleeping && tama.energy > 10)
      tama.playT = 2.4f;
    break;
  case 2:
    tama.sleeping = !tama.sleeping;
    break;
  case 3:
    tama.cleanT = 1.3f;
    break;
  }
}

static void gameTama(float dt)
{
  tama.t += dt;

  // ---- entrees
  if (gBtnLeft)
    tama.sel = (tama.sel + 3) % 4;
  if (gBtnRight)
    tama.sel = (tama.sel + 1) % 4;
  if (gBtnCenter)
    tamaDoAction();

  // ---- simulation
  int npoop = 0;
  for (int i = 0; i < TAMA_NPOOP; i++)
    npoop += tama.poop[i];
  tama.food -= dt * 100 / 480;                       // vide en ~8 min
  tama.fun -= dt * 100 / 360 * (npoop ? 2.2f : 1);   // ~6 min, x2 si sale
  if (tama.sleeping)
  {
    tama.energy += dt * 100 / 45; // sieste : plein en 45 s
    if (tama.energy >= 100)
    {
      tama.energy = 100;
      tama.sleeping = false; // reveil naturel
    }
  }
  else
    tama.energy -= dt * 100 / 720; // ~12 min
  tama.food = constrain(tama.food, 0.0f, 100.0f);
  tama.fun = constrain(tama.fun, 0.0f, 100.0f);
  tama.energy = constrain(tama.energy, 0.0f, 100.0f);

  if (tama.eatT > 0)
  {
    tama.eatT -= dt;
    if (tama.eatT <= 0)
    {
      tama.food = constrain(tama.food + 30, 0.0f, 100.0f);
      tama.fun = constrain(tama.fun + 4, 0.0f, 100.0f);
      tama.poopTimer = frand(25, 70);
      tamaSave();
    }
  }
  if (tama.playT > 0)
  {
    tama.playT -= dt;
    float pr = 1 - tama.playT / 2.4f;
    tama.prevBx = tama.bx;
    tama.bx = sinf(pr * 2 * PI * 2) * 54 * sinf(pr * PI);
    tama.rollAng += (tama.bx - tama.prevBx) / TAMA_BALL_R;
    if (tama.playT <= 0)
    {
      tama.bx = 0;
      tama.fun = constrain(tama.fun + 32, 0.0f, 100.0f);
      tama.energy = constrain(tama.energy - 8, 0.0f, 100.0f);
      tamaSave();
    }
  }
  if (tama.cleanT > 0)
  {
    tama.cleanT -= dt;
    float broomX = 84 + (1 - tama.cleanT / 1.3f) * 192;
    for (int i = 0; i < TAMA_NPOOP; i++)
      if (tama.poop[i] && TAMA_POOP_XY[i][0] < broomX)
        tama.poop[i] = false;
  }
  if (tama.poopTimer > 0)
  {
    tama.poopTimer -= dt;
    if (tama.poopTimer <= 0)
      for (int i = 0; i < TAMA_NPOOP; i++)
        if (!tama.poop[i])
        {
          tama.poop[i] = true;
          break;
        }
  }
  tama.saveT += dt;
  if (tama.saveT > 20)
  {
    tama.saveT = 0;
    tamaSave();
  }

  // ---- rendu
  float low = tama.food < tama.fun ? tama.food : tama.fun;
  if (tama.energy < low)
    low = tama.energy;
  int mood = low > 55 ? 1 : (low > 25 ? 0 : -1);
  uint16_t ink = tama.sleeping ? rgb565(104, 104, 104) : rgb565(214, 214, 214);
  uint16_t dim = tama.sleeping ? rgb565(48, 48, 48) : rgb565(96, 96, 96);

  canvas->fillScreen(RGB565_BLACK);
  tamaRoom(ink, dim);
  for (int i = 0; i < TAMA_NPOOP; i++)
    if (tama.poop[i])
      tamaPoopDraw(TAMA_POOP_XY[i][0], TAMA_POOP_XY[i][1], ink);

  // perso : ombre, sphere COLOREE (la seule du tableau), visage
  int bx = TAMA_BALL_X + (int)tama.bx, by = TAMA_BALL_Y;
  float squash = tama.playT > 0 ? fabsf(sinf((1 - tama.playT / 2.4f) * 2 * PI * 4)) * 3 : 0;
  canvas->fillEllipse(bx, by + TAMA_BALL_R - 2, 20, 6, rgb565(70, 70, 70));
  drawBallSpriteRot(bx, by + (int)squash, TAMA_BALL_R, tama.rollAng);
  bool blink = fmodf(tama.t, 3.7f) < 0.13f;
  tamaFace(bx, by + squash, TAMA_BALL_R, mood, tama.sleeping || blink,
           tama.eatT > 0 && fmodf(tama.t, 0.4f) < 0.2f);

  // ---- overlays d'action
  if (tama.eatT > 0) // donut qui retrecit a chaque bouchee
  {
    int dr = 2 + (int)(tama.eatT / 1.8f * 7);
    tamaDonutIcon(bx + TAMA_BALL_R + 14, by + 4, dr, ink);
  }
  if (tama.playT > 0) // petits coeurs qui montent
  {
    float pr = 1 - tama.playT / 2.4f;
    for (int i = 0; i < 3; i++)
    {
      float ph = pr * 1.6f - i * 0.33f;
      if (ph > 0 && ph < 1)
        tamaHeart(bx - 20 + i * 20, (int)(by - TAMA_BALL_R - 6 - ph * 26), 4, ink);
    }
  }
  if (tama.cleanT > 0) // balai qui balaie le sol
  {
    int broomX = 84 + (int)((1 - tama.cleanT / 1.3f) * 192);
    int sw = (int)(sinf(tama.t * 18) * 5);
    canvas->drawLine(broomX + 14, 158, broomX + sw, 208, ink);
    canvas->drawLine(broomX + sw, 208, broomX + sw - 7, 222, ink);
    canvas->drawLine(broomX + sw + 5, 209, broomX + sw + 1, 222, ink);
    canvas->drawLine(broomX + sw - 7, 222, broomX + sw + 4, 224, ink);
  }
  if (tama.sleeping) // Zzz flottants
  {
    float zf = fmodf(tama.t, 2.0f) / 2;
    tamaZ(bx + 26, by - TAMA_BALL_R - 8 - (int)(zf * 10), 5, ink);
    tamaZ(bx + 36, by - TAMA_BALL_R - 20 - (int)(zf * 8), 7, ink);
    tamaZ(bx + 48, by - TAMA_BALL_R - 34 - (int)(zf * 6), 9, dim);
  }
  if (low < 18 && fmodf(tama.t, 0.8f) < 0.4f) // alerte "!"
  {
    canvas->fillRect(bx - 2, by - TAMA_BALL_R - 30, 4, 14, rgb565(255, 255, 255));
    canvas->fillRect(bx - 2, by - TAMA_BALL_R - 12, 4, 4, rgb565(255, 255, 255));
  }

  tamaHud(ink);
  tamaActionBar(ink);
}
