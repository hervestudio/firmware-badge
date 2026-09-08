// UI menu du badge — PARTAGEE entre le firmware (main.cpp) et l'emulateur
// (tools/emulator/emu.cpp) : une seule source pour les deux rendus.
//
// - Menu principal "bulles" : 4 categories (Play/Watch/Meet/More) en cercles
//   pastel avec une petite physique (ressorts + collisions : les bulles se
//   poussent quand la selection grossit).
// - Sous-menus : liste par categorie avec defilement amorti + pilule de
//   selection ajustee au label (position/largeur/couleur animees).
//
// Depend de : canvas, rgb565, RGB565_BLACK/WHITE, W/H/CX/CY, millis(),
// mfPrint/mfTextW (Dingos ExtraBold), mdPrint (Dingos Medium), expf/sqrtf,
// constrain, snprintf.
#pragma once

// ------------------------------------------------------------- categories
enum : uint8_t { UIC_PLAY = 0, UIC_WATCH, UIC_MEET, UIC_MORE };
#define UI_NCATS 4
static const char *UI_CAT_NAMES[UI_NCATS] = {"Play", "Watch", "Meet", "More"};
static const uint16_t UI_PASTELS[UI_NCATS] = {
    rgb565(0xfb, 0xd9, 0x75), rgb565(0xfc, 0xa3, 0xf7),
    rgb565(0x9d, 0x97, 0xed), rgb565(0x7e, 0xdb, 0xb0)};

static const char *UI_PLAY_IT[] = {"Snake", "Pong", "Sphere Run", "Roundtris"};
// ("Sphere Pet" retire du menu — revue Romain 2026-08-29 ; le code du jeu
// (tama.h, UI_PET) reste en place, re-ajouter l'entree suffit a le retablir)
static const char *UI_WATCH_IT[] = {"Conf Buddy", "Snake", "Disco", "Globe",
                                    "Three Conf", "DVD", "Points", "Warp",
                                    "Solar System", "My Photo"};
// ("My Photo" n'apparait que si une photo a ete uploadee via Setup :
// g_hasPhoto est declare par chaque plateforme avant l'include)

// ---- rencontres (qui j'ai croise, combien de fois) : table partagee,
// alimentee par socialReactTrigger (social_ui.h), persistee en NVS "met2"
// cote firmware, affichee par l'ecran Meet > Encounters
// niveaux du reglage de proximite des rencontres (Settings > Proximity)
static const int8_t UI_PROX_LEVELS[4] = {-30, -55, -62, -70};
static const char *UI_PROX_NAMES[4] = {"Touch", "Close", "Normal", "Far"};

#define MET_MAX 40
static char metNames[MET_MAX][21];
static uint16_t metCounts[MET_MAX];
static int metN = 0;

// Leaderboard des jeux (Meet > Leaderboard) : meilleurs scores connus des
// badges croises, appris passivement via les beacons ESP-NOW (social.h,
// fusion par maximum), persistes en NVS "lb1"
#define LB_GAMES 4 // ordre FIGE du beacon : Snake, Pong, Sphere Run, Roundtris
static const char *LB_GAME_NAMES[LB_GAMES] = {"Snake", "Pong", "Sphere Run",
                                              "Roundtris"};
static char lbNames[MET_MAX][21];
static uint16_t lbScores[MET_MAX][LB_GAMES];
static int lbN = 0;
static bool lbDirty = false; // scores appris non encore persistes

// Fusion par MAXIMUM des scores annonces par un badge (beacon ESP-NOW cote
// firmware, rencontre simulee cote emulateur)
static void lbMerge(const char *name, const uint16_t *sc)
{
  if (!name[0])
    return;
  bool any = false;
  for (int g = 0; g < LB_GAMES; g++)
    if (sc[g])
      any = true;
  if (!any)
    return; // rien a apprendre (vieux firmware ou jamais joue)
  int idx = -1;
  for (int i = 0; i < lbN; i++)
    if (strncmp(lbNames[i], name, sizeof(lbNames[0]) - 1) == 0)
    {
      idx = i;
      break;
    }
  if (idx < 0)
  {
    if (lbN >= MET_MAX)
      return; // table pleine (40 = toute la serie, ne devrait pas arriver)
    idx = lbN++;
    snprintf(lbNames[idx], sizeof(lbNames[0]), "%s", name);
    memset(lbScores[idx], 0, sizeof(lbScores[0]));
  }
  for (int g = 0; g < LB_GAMES; g++)
    if (sc[g] > lbScores[idx][g])
    {
      lbScores[idx][g] = sc[g];
      lbDirty = true;
    }
}


// nombre d'entrees par categorie, "Back" compris (toujours en dernier)
static int uiListCount(int cat)
{
  switch (cat)
  {
  case UIC_PLAY: return 5;
  case UIC_WATCH: return g_hasPhoto ? 11 : 10;
  case UIC_MEET: return 5; // Schedule + QR Code + Encounters + Leaderboard + Back
  default: return 5; // More : Draw, Setup, Auto cycle, Settings, Back
                     // (OTA / Rotate / Batt deplaces dans Settings, sous PIN)
  }
}

static void uiListLabel(int cat, int i, bool autoCyc, char *buf, size_t n)
{
  if (i == uiListCount(cat) - 1)
  {
    snprintf(buf, n, "Back");
    return;
  }
  switch (cat)
  {
  case UIC_PLAY: snprintf(buf, n, "%s", UI_PLAY_IT[i]); break;
  case UIC_WATCH: snprintf(buf, n, "%s", UI_WATCH_IT[i]); break;
  case UIC_MEET:
    if (i == 0)
      snprintf(buf, n, "Schedule");
    else if (i == 1)
      snprintf(buf, n, "QR Code");
    else if (i == 2)
      snprintf(buf, n, "Encounters");
    else
      snprintf(buf, n, "Leaderboard");
    break;
  default:
    if (i == 0)
      snprintf(buf, n, "Draw (WiFi)");
    else if (i == 1)
      snprintf(buf, n, "Setup (WiFi)");
    else if (i == 2)
      snprintf(buf, n, "Auto cycle: %s", autoCyc ? "ON" : "OFF");
    else
      snprintf(buf, n, "Settings");
  }
}

// resolution d'une selection -> action a executer par l'appelant
enum UiAction : uint8_t { UIA_NONE, UIA_ANIM, UIA_GAME, UIA_DRAW, UIA_AUTO,
                          UIA_OTA, UIA_SCHED, UIA_ROT, UIA_SETTINGS, UIA_BACK,
                          UIA_SETUP, UIA_QR, UIA_MET, UIA_LB };
static UiAction uiResolve(int cat, int sel, int *arg)
{
  if (sel == uiListCount(cat) - 1)
    return UIA_BACK;
  switch (cat)
  {
  case UIC_PLAY: *arg = sel; return UIA_GAME;
  case UIC_WATCH: *arg = sel; return UIA_ANIM;    // slots ACTIVE 0..6
  case UIC_MEET:
    if (sel == 0)
      return UIA_SCHED;
    if (sel == 1)
      return UIA_QR; // QR code configure via More > Setup
    if (sel == 2)
      return UIA_MET; // qui j'ai croise, combien de fois
    return UIA_LB; // scores des jeux, les miens + ceux des badges croises
  default:
    if (sel == 0)
      return UIA_DRAW;
    if (sel == 1)
      return UIA_SETUP; // parcours de config sur telephone (nom/buddy/QR)
    if (sel == 2)
      return UIA_AUTO;
    return UIA_SETTINGS; // protege par code (avatar, proximite, OTA...)
  }
}

// ----------------------------------- rotation logicielle de l'ecran
// Certains modules ont la dalle collee legerement de travers sur le PCB :
// on compense en tournant l'image de quelques degres au moment du flush.
static int uiScreenRot = 0; // degres, -15..+15, persiste en NVS par l'appelant

// etale un 565 sur 32 bits (R|B en mot bas, G en mot haut) : permet le
// melange pondere des 3 canaux en une seule multiplication
static inline uint32_t uiSpread565(uint16_t c)
{
  return (c | ((uint32_t)c << 16)) & 0x07E0F81Fu;
}

static void uiRotateBlit(const uint16_t *src, uint16_t *dst, int deg)
{
  float a = deg * 3.14159265f / 180.0f;
  int32_t ca = (int32_t)(cosf(a) * 65536.0f), sa = (int32_t)(sinf(a) * 65536.0f);
  for (int y = 0; y < H; y++)
  {
    int32_t dy = y - CY;
    int32_t u = ((int32_t)CX << 16) - CX * ca + dy * sa;
    int32_t v = ((int32_t)CY << 16) + CX * sa + dy * ca;
    uint16_t *drow = &dst[y * W];
    for (int x = 0; x < W; x++, u += ca, v -= sa)
    {
      int ux = u >> 16, vy = v >> 16;
      if (ux < 0 || ux >= W - 1 || vy < 0 || vy >= H - 1)
      {
        drow[x] = 0;
        continue;
      }
      // "SHARP bilinear" : bilineaire a transition resserree (x2 autour du
      // demi-pixel) — anti-crenelage sans le flou du bilineaire plein : les
      // coeurs de pixels restent purs, seuls les bords melangent.
      const uint16_t *s = &src[vy * W + ux];
      int rfx = ((int)((u >> 11) & 31) - 16) * 2 + 16;
      int rfy = ((int)((v >> 11) & 31) - 16) * 2 + 16;
      uint32_t fx = rfx < 0 ? 0 : (rfx > 31 ? 31 : (uint32_t)rfx);
      uint32_t fy = rfy < 0 ? 0 : (rfy > 31 ? 31 : (uint32_t)rfy);
      uint32_t top = ((uiSpread565(s[0]) * (32 - fx) + uiSpread565(s[1]) * fx) >> 5) & 0x07E0F81Fu;
      uint32_t bot = ((uiSpread565(s[W]) * (32 - fx) + uiSpread565(s[W + 1]) * fx) >> 5) & 0x07E0F81Fu;
      uint32_t mix = ((top * (32 - fy) + bot * fy) >> 5) & 0x07E0F81Fu;
      drow[x] = (uint16_t)((mix | (mix >> 16)) & 0xFFFF);
    }
  }
}

// Petit menu des Settings (apres le code PIN) : Avatar / Proximity / Back
#define SETMENU_N 7 // Avatar, Proximity, Rotate, OTA, Batt, Batt log, Back
static void uiDrawSetMenu(int sel)
{
  canvas->fillScreen(RGB565_BLACK);
  mtPrint(CX - mtTextW("SETTINGS") / 2, 40, "SETTINGS", rgb565(0xfb, 0xd9, 0x75));
  // OTA / Rotate / Batt deplaces depuis More (revue Romain 2026-08-29) :
  // reserves a l'organisation, derriere le code PIN
  char batt[20];
  if (batMvRaw > 0)
    snprintf(batt, sizeof(batt), "Batt: %lu.%02luV", (unsigned long)(batMvRaw / 1000),
             (unsigned long)(batMvRaw % 1000 / 10));
  else
    snprintf(batt, sizeof(batt), "Batt: --");
  const char *IT[SETMENU_N] = {"Avatar", "Proximity", "Rotate screen",
                               "OTA flash mode", batt, "Batt log", "Back"};
  for (int i = 0; i < SETMENU_N; i++)
  {
    int y = 88 + i * 34;
    if (i == sel)
    {
      int w = mfTextW(IT[i]) + 36;
      canvas->fillRect(CX - w / 2 + 12, y - 6, w - 24, 32, rgb565(0xfb, 0xd9, 0x75));
      canvas->fillCircle(CX - w / 2 + 12, y + 10, 16, rgb565(0xfb, 0xd9, 0x75));
      canvas->fillCircle(CX + w / 2 - 12, y + 10, 16, rgb565(0xfb, 0xd9, 0x75));
      mfPrint(CX - mfTextW(IT[i]) / 2, y, IT[i], RGB565_BLACK);
    }
    else
      mfPrint(CX - mfTextW(IT[i]) / 2, y, IT[i],
              i == 4 ? rgb565(130, 130, 130) : RGB565_WHITE);
  }
}

// Etape photo du Setup, cote badge, tant qu'aucune photo n'est recue :
// emplacement en pointilles + invitation — montre qu'on est bien passe a
// l'etape 3 (revue Romain 2026-09-07)
static void uiDrawPhotoPlaceholder()
{
  canvas->fillScreen(RGB565_BLACK);
  uint16_t dim = rgb565(110, 110, 110);
  for (int k = 0; k < 64; k += 2) // cercle pointille
  {
    float a = k * (2 * (float)PI / 64);
    canvas->fillCircle((int)(CX + cosf(a) * 118), (int)(CY + sinf(a) * 118),
                       3, dim);
  }
  bbPrint(CX - bbTextW("YOUR PHOTO") / 2, 150, "YOUR PHOTO",
          rgb565(0x9d, 0x97, 0xed));
  mdPrint(CX - mdTextW("choose it on your phone") / 2, 186,
          "choose it on your phone", rgb565(150, 160, 150));
  mdPrint(CX - mdTextW("(optional)") / 2, 214, "(optional)",
          rgb565(110, 110, 110));
}

// Ecran Settings > Batt log : courbe de decharge enregistree pendant que le
// badge tourne. Ordonnee = %, abscisse = temps ecoule. Pente %/h calculee
// entre le premier et le dernier echantillon -> projection d'autonomie
// pleine->vide. gauche = remise a zero, centre = retour.
static void uiDrawBlog(uint32_t now, int curPct, uint32_t curMv)
{
  canvas->fillScreen(RGB565_BLACK);
  mfPrint(CX - mfTextW("BATT LOG") / 2, 44, "BATT LOG",
          rgb565(0x9d, 0x97, 0xed));
  const int gx0 = 64, gx1 = 296, gy0 = 92, gy1 = 232;
  // grille : 0 / 50 / 100 %
  for (int p = 0; p <= 100; p += 50)
  {
    int y = gy1 - (gy1 - gy0) * p / 100;
    for (int x = gx0; x < gx1; x += 4)
      canvas->drawPixel(x, y, rgb565(46, 50, 46));
  }
  char buf[28];
  if (blogN < 2)
  {
    mdPrint(CX - mdTextW("recording...") / 2, 150, "recording...",
            rgb565(150, 160, 150));
    snprintf(buf, sizeof(buf), "1 point / %lu min",
             (unsigned long)(blogIvlMs / 60000));
    mdPrint(CX - mdTextW(buf) / 2, 176, buf, rgb565(130, 130, 130));
  }
  else
  {
    for (int i = 1; i < blogN; i++)
    {
      int xa = gx0 + (gx1 - gx0) * (i - 1) / (blogN - 1);
      int xb = gx0 + (gx1 - gx0) * i / (blogN - 1);
      int ya = gy1 - (gy1 - gy0) * blogPct[i - 1] / 100;
      int yb = gy1 - (gy1 - gy0) * blogPct[i] / 100;
      canvas->drawLine(xa, ya, xb, yb, rgb565(0xfb, 0xd9, 0x75));
      canvas->drawLine(xa, ya + 1, xb, yb + 1, rgb565(0xfb, 0xd9, 0x75));
    }
  }
  // stats : duree couverte, etat courant, pente et projection
  uint32_t spanMin = blogN > 1 ? (uint32_t)(blogN - 1) * blogIvlMs / 60000 : 0;
  snprintf(buf, sizeof(buf), "%luh%02lu  %d%%  %lu.%02luV",
           (unsigned long)(spanMin / 60), (unsigned long)(spanMin % 60),
           curPct < 0 ? 0 : curPct, (unsigned long)(curMv / 1000),
           (unsigned long)(curMv % 1000 / 10));
  mfPrint(CX - mfTextW(buf) / 2, 246, buf, RGB565_WHITE);
  if (blogN > 5 && blogPct[0] > blogPct[blogN - 1])
  {
    float hours = (blogN - 1) * (blogIvlMs / 1000.0f) / 3600.0f;
    float rate = (blogPct[0] - blogPct[blogN - 1]) / hours; // %/h
    if (rate > 0.5f)
    {
      snprintf(buf, sizeof(buf), "-%d.%d%%/h  full in %d.%dh",
               (int)rate, (int)(rate * 10) % 10, (int)(100 / rate),
               (int)(1000 / rate) % 10);
      bbPrint(CX - bbTextW(buf) / 2, 276, buf, rgb565(0xfb, 0xd9, 0x75));
    }
  }
  mdPrint(CX - mdTextW("left: reset   center: back") / 2, 308,
          "left: reset   center: back", rgb565(130, 130, 130));
}

// Ecran Settings > Batt : CALIBRATION de la jauge par badge. Le pont
// 100k/100k reel a une tolerance de +/-5 % (150 mV d'ecart mesures sur un
// badge, revue Romain 2026-08-29) : gauche/droite ajustent un facteur
// multiplicatif (NVS "vcal", pour-mille) jusqu'a ce que la tension affichee
// = le multimetre sur B+/B-, centre = sauver.
static void uiDrawVcal(uint32_t mv, int cal)
{
  canvas->fillScreen(RGB565_BLACK);
  mtPrint(CX - mtTextW("BATT CAL") / 2, 40, "BATT CAL", rgb565(0xfb, 0xd9, 0x75));
  char t[16];
  if (mv > 0)
    snprintf(t, sizeof(t), "%lu.%02luV", (unsigned long)(mv / 1000),
             (unsigned long)(mv % 1000 / 10));
  else
    snprintf(t, sizeof(t), "--");
  mtPrint(CX - mtTextW(t) / 2, 140, t, RGB565_WHITE);
  snprintf(t, sizeof(t), "%+d.%d%%", (cal - 1000) / 10, abs(cal - 1000) % 10);
  bbPrint(CX - bbTextW(t) / 2, 196, t, rgb565(0x9d, 0x97, 0xed));
  mdPrint(CX - mdTextW("match the multimeter") / 2, 246,
          "match the multimeter", rgb565(130, 130, 130));
  mdPrint(CX - mdTextW("on the battery (B+/B-)") / 2, 272,
          "on the battery (B+/B-)", rgb565(130, 130, 130));
  mdPrint(CX - mdTextW("center: save") / 2, 306, "center: save",
          rgb565(130, 130, 130));
}

// Reglage de proximite des rencontres, avec jauge LIVE du badge le plus
// proche (la radio ecoute en mode sonde pendant cet ecran) : la zone au-dela
// du seuil est celle qui declenche.
static void uiDrawProx(int level, float liveRssi)
{
  canvas->fillScreen(RGB565_BLACK);
  mtPrint(CX - mtTextW("PROXIMITY") / 2, 30, "PROXIMITY", rgb565(0x9d, 0x97, 0xed));
  mtPrint(CX - mtTextW(UI_PROX_NAMES[level]) / 2, 108, UI_PROX_NAMES[level],
          RGB565_WHITE);
  char db[16];
  snprintf(db, sizeof(db), "%d dBm", (int)UI_PROX_LEVELS[level]);
  bbPrint(CX - bbTextW(db) / 2, 150, db, rgb565(150, 160, 150));
  // jauge : -85 (loin) a -25 (colle) ; repere = seuil ; barre = signal live
  const int gx0 = 62, gx1 = 298, gy = 210, gh = 16;
  auto rssiToX = [&](float r) {
    float u = (r + 85.0f) / 60.0f;
    u = u < 0 ? 0 : (u > 1 ? 1 : u);
    return (int)(gx0 + u * (gx1 - gx0));
  };
  canvas->fillRect(gx0, gy, gx1 - gx0, gh, rgb565(34, 38, 34));
  int tx = rssiToX(UI_PROX_LEVELS[level]);
  // zone de declenchement (a droite du seuil) legerement teintee
  canvas->fillRect(tx, gy, gx1 - tx, gh, rgb565(46, 58, 46));
  if (liveRssi > -95)
  {
    bool trig = liveRssi > UI_PROX_LEVELS[level];
    canvas->fillRect(gx0, gy + 3, rssiToX(liveRssi) - gx0, gh - 6,
                     trig ? rgb565(0x7e, 0xdb, 0xb0) : rgb565(0xfb, 0xd9, 0x75));
  }
  canvas->fillRect(tx - 1, gy - 5, 3, gh + 10, RGB565_WHITE); // repere seuil
  if (liveRssi > -95)
  {
    char rs[16];
    snprintf(rs, sizeof(rs), "badge at %d dBm", (int)liveRssi);
    mdPrint(CX - mdTextW(rs) / 2, 244, rs, RGB565_WHITE);
  }
  else
    mdPrint(CX - mdTextW("no badge nearby") / 2, 244, "no badge nearby",
            rgb565(130, 130, 130));
  mdPrint(CX - mdTextW("< > adjust    center: save") / 2, 296,
          "< > adjust    center: save", rgb565(130, 130, 130));
}

// Ecran Meet > Encounters : qui j'ai croise, combien de fois (tri par
// nombre de rencontres decroissant). prev/next = defilement, centre = retour.
#define MET_ROWS 6
static void uiDrawMet(int scroll)
{
  canvas->fillScreen(RGB565_BLACK);
  // titre en Dingos menu (plus etroit) et descendu : en mtPrint a y=26 il
  // debordait de la zone ronde visible (revue Romain 2026-08-29)
  mfPrint(CX - mfTextW("ENCOUNTERS") / 2, 46, "ENCOUNTERS",
          rgb565(0x9d, 0x97, 0xed));
  if (metN == 0)
  {
    mfPrint(CX - mfTextW("No one met yet") / 2, 160, "No one met yet",
            RGB565_WHITE);
    mdPrint(CX - mdTextW("badges say hi nearby") / 2, 196,
            "badges say hi nearby", rgb565(130, 130, 130));
    mdPrint(CX - mdTextW("center: back") / 2, 300, "center: back",
            rgb565(130, 130, 130));
    return;
  }
  // tri par compte decroissant (indices, insertion — n <= 40)
  uint8_t ord[MET_MAX];
  for (int i = 0; i < metN; i++)
    ord[i] = (uint8_t)i;
  for (int i = 1; i < metN; i++)
  {
    uint8_t k = ord[i];
    int j = i - 1;
    while (j >= 0 && metCounts[ord[j]] < metCounts[k])
    {
      ord[j + 1] = ord[j];
      j--;
    }
    ord[j + 1] = k;
  }
  char buf[12];
  for (int r = 0; r < MET_ROWS && scroll + r < metN; r++)
  {
    int i = ord[scroll + r];
    int y = 86 + r * 34;
    mfPrint(64, y, metNames[i], RGB565_WHITE);
    snprintf(buf, sizeof(buf), "x%u", (unsigned)metCounts[i]);
    mfPrint(296 - mfTextW(buf), y, buf, rgb565(0xfb, 0xd9, 0x75));
  }
  // indicateurs de defilement
  if (scroll > 0)
    mdPrint(CX - mdTextW("^") / 2, 76, "^", rgb565(130, 130, 130));
  if (scroll + MET_ROWS < metN)
    mdPrint(CX - mdTextW("v") / 2, 288, "v", rgb565(130, 130, 130));
  mdPrint(CX - mdTextW("center: back") / 2, 314, "center: back",
          rgb565(130, 130, 130));
}

// Ecran Meet > Leaderboard : un jeu a la fois, classement des badges croises
// + soi ("You", surligne). prev/next = jeu suivant/precedent, centre = retour.
// Toujours 6 lignes max ; si "You" sort du top 6, il remplace la 6e ligne
// avec son vrai rang.
#define LB_ROWS 6
static void uiDrawLB(int game, const uint16_t *mine)
{
  canvas->fillScreen(RGB565_BLACK);
  // titre en Dingos menu et descendu (meme raison que ENCOUNTERS)
  mfPrint(CX - mfTextW("LEADERBOARD") / 2, 46, "LEADERBOARD",
          rgb565(0x9d, 0x97, 0xed));
  char sub[24];
  snprintf(sub, sizeof(sub), "< %s >", LB_GAME_NAMES[game]);
  bbPrint(CX - bbTextW(sub) / 2, 82, sub, rgb565(0xfb, 0xd9, 0x75));
  // participants : badges croises avec un score non nul + soi (sentinelle
  // MET_MAX). Tri decroissant par score du jeu affiche (n <= 41, insertion).
  auto sc = [&](uint8_t i) -> uint16_t {
    return i == MET_MAX ? mine[game] : lbScores[i][game];
  };
  uint8_t ord[MET_MAX + 1];
  int n = 0;
  for (int i = 0; i < lbN; i++)
    if (lbScores[i][game] > 0)
      ord[n++] = (uint8_t)i;
  ord[n++] = MET_MAX;
  for (int i = 1; i < n; i++)
  {
    uint8_t k = ord[i];
    int j = i - 1;
    while (j >= 0 && (sc(ord[j]) < sc(k) ||
                      (sc(ord[j]) == sc(k) && ord[j] == MET_MAX)))
    {
      ord[j + 1] = ord[j]; // a egalite, "You" passe apres (fair-play)
      j--;
    }
    ord[j + 1] = k;
  }
  if (n == 1 && mine[game] == 0)
  {
    mfPrint(CX - mfTextW("No scores yet") / 2, 170, "No scores yet",
            RGB565_WHITE);
    mdPrint(CX - mdTextW("play & meet badges") / 2, 206,
            "play & meet badges", rgb565(130, 130, 130));
    mdPrint(CX - mdTextW("center: back") / 2, 300, "center: back",
            rgb565(130, 130, 130));
    return;
  }
  int selfRank = 0;
  while (ord[selfRank] != MET_MAX)
    selfRank++;
  char buf[16];
  for (int r = 0; r < LB_ROWS && r < n; r++)
  {
    // derniere ligne visible : "You" avec son vrai rang s'il est plus bas
    int rank = (r == LB_ROWS - 1 && selfRank >= LB_ROWS) ? selfRank : r;
    uint8_t i = ord[rank];
    bool self = (i == MET_MAX);
    int y = 112 + r * 32;
    uint16_t col = self ? rgb565(0xfb, 0xd9, 0x75) : RGB565_WHITE;
    snprintf(buf, sizeof(buf), "%d.", rank + 1);
    mfPrint(52, y, buf, rgb565(130, 130, 130));
    mfPrint(88, y, self ? "You" : lbNames[i], col);
    snprintf(buf, sizeof(buf), "%u", (unsigned)sc(i));
    mfPrint(308 - mfTextW(buf), y, buf, col);
  }
  mdPrint(CX - mdTextW("center: back") / 2, 314, "center: back",
          rgb565(130, 130, 130));
}

// Ecran de calibration : aligner la barre d'horizon jaune avec l'horizontale
// physique du badge (gauche/droite = -1/+1 degre, centre = sauver et sortir)
static void uiDrawRotate(int deg)
{
  canvas->fillScreen(RGB565_BLACK);
  uint16_t grid = rgb565(60, 80, 66);
  canvas->drawCircle(CX, CY, 150, grid);
  canvas->drawCircle(CX, CY, 100, grid);
  for (int y = 30; y < H - 30; y += 3)
    canvas->drawPixel(CX, y, grid);
  // barre d'HORIZON : elle doit etre parfaitement horizontale a l'oeil
  canvas->fillRect(30, CY - 2, W - 60, 4, rgb565(0xfb, 0xd9, 0x75));
  bbPrint(180 - bbTextW("SCREEN TILT") / 2, 74, "SCREEN TILT", RGB565_WHITE);
  char t[8];
  snprintf(t, sizeof(t), "%+d", deg);
  mtPrint(180 - mtTextW(t) / 2, 210, t, RGB565_WHITE);
  bbPrint(180 - bbTextW("DEG") / 2, 248, "DEG", rgb565(150, 160, 150));
  mdPrint(180 - mdTextW("center: save") / 2, 296, "center: save",
          rgb565(130, 130, 130));
}

// ------------------------------- Settings : code d'acces + choix d'avatar
// Les Settings (avatar/personne du badge) sont proteges par un code a 5
// chiffres : gauche/droite = chiffre -/+, centre = valider et passer au
// suivant. Mauvais code = retour au menu.
#define UI_PIN_LEN 5
static const uint8_t UI_PIN_CODE[UI_PIN_LEN] = {0, 0, 0, 0, 0}; // provisoire (etait 39193)

static void uiDrawPin(const uint8_t *digits, int pos, bool error)
{
  canvas->fillScreen(RGB565_BLACK);
  uint16_t accent = rgb565(0xfb, 0xd9, 0x75);
  bbPrint(180 - bbTextW("SETTINGS") / 2, 78, "SETTINGS", RGB565_WHITE);
  mdPrint(180 - mdTextW("enter code") / 2, 112, "enter code",
          rgb565(130, 140, 130));
  const int bw = 42, bh = 56, gap = 9;
  const int x0 = CX - (UI_PIN_LEN * bw + (UI_PIN_LEN - 1) * gap) / 2;
  const int by = 150;
  for (int i = 0; i < UI_PIN_LEN; i++)
  {
    int bx = x0 + i * (bw + gap);
    uint16_t frame = (i == pos) ? accent : rgb565(70, 82, 72);
    canvas->fillRoundRect(bx, by, bw, bh, 9, frame);
    canvas->fillRoundRect(bx + 2, by + 2, bw - 4, bh - 4, 7, RGB565_BLACK);
    if (i < pos || i == pos) // chiffres deja saisis + chiffre en cours
    {
      char d[2] = {(char)('0' + digits[i]), 0};
      mtPrint(bx + bw / 2 - mtTextW(d) / 2, by + 13, d,
              (i == pos) ? accent : RGB565_WHITE);
    }
  }
  if (error)
    mdPrint(180 - mdTextW("wrong code") / 2, 232, "wrong code",
            rgb565(240, 80, 77));
  mdPrint(180 - mdTextW("center: next") / 2, 276, "center: next",
          rgb565(130, 130, 130));
}

// Cadre de l'ecran de choix d'avatar : la plateforme dessine la sphere (et le
// visage) PAR-DESSUS, centree en (CX, CY - 26), rayon ~78.
static void uiDrawAvatarFrame(int idx, int total, const char *name)
{
  canvas->fillScreen(RGB565_BLACK);
  uint16_t accent = rgb565(0xfb, 0xd9, 0x75);
  bbPrint(180 - bbTextW("WHO AM I ?") / 2, 58, "WHO AM I ?", RGB565_WHITE);
  mfPrint(26, CY - 40, "<", rgb565(120, 130, 120));
  mfPrint(334 - mfTextW(">"), CY - 40, ">", rgb565(120, 130, 120));
  mfPrint(180 - mfTextW(name) / 2, 258, name, accent);
  char cnt[16];
  snprintf(cnt, sizeof(cnt), "%d / %d", idx + 1, total);
  mdPrint(180 - mdTextW(cnt) / 2, 292, cnt, rgb565(130, 140, 130));
  mdPrint(180 - mdTextW("center: save") / 2, 318, "center: save",
          rgb565(110, 110, 110));
}

// -------------------------------------------------------- tete batterie
static void uiBatteryHeader(int pct, bool charging)
{
  char pctTxt[8];
  if (pct >= 0)
    snprintf(pctTxt, sizeof(pctTxt), "%d%%", pct);
  else
    snprintf(pctTxt, sizeof(pctTxt), "--%%");
  // centrage PARFAIT du groupe icone (44 px avec la tetine) + espace + texte
  const int bx = 180 - (44 + 10 + mdTextW(pctTxt)) / 2, by = 44;
  uint16_t frame = rgb565(210, 210, 210);
  canvas->drawRoundRect(bx, by, 40, 22, 4, frame);
  canvas->fillRect(bx + 40, by + 6, 4, 10, frame);
  if (pct >= 0)
  {
    uint16_t fill = pct > 50 ? rgb565(80, 220, 120)
                             : (pct > 20 ? rgb565(255, 190, 60) : rgb565(240, 80, 70));
    int w = 34 * pct / 100;
    if (w > 0)
      canvas->fillRect(bx + 3, by + 3, w, 16, fill);
  }
  mdPrint(bx + 54, by + 5, pctTxt, RGB565_WHITE);
  if (charging)
  {
    uint16_t yl = rgb565(255, 213, 48);
    canvas->fillTriangle(bx - 14, by - 2, bx - 22, by + 13, bx - 13, by + 11, yl);
    canvas->fillTriangle(bx - 15, by + 9, bx - 12, by + 24, bx - 6, by + 8, yl);
  }
}

// ------------------------------------------- menu principal a bulles
struct UiBubble
{
  float x, y, vx, vy, r;
};
static UiBubble uiBub[UI_NCATS];
static int uiHomeFocus = 0;
// ancrages et rayons de base (composes d'apres la maquette : Play a gauche,
// Watch en haut a droite, Meet en bas au centre, More en bas a droite)
static const float UI_BUB_HOME[UI_NCATS][2] = {
    {116, 184}, {246, 146}, {184, 264}, {283, 246}};
static const float UI_BUB_R[UI_NCATS] = {72, 76, 66, 52};

static void uiHomeReset()
{
  for (int i = 0; i < UI_NCATS; i++)
  {
    uiBub[i].x = UI_BUB_HOME[i][0];
    uiBub[i].y = UI_BUB_HOME[i][1];
    uiBub[i].vx = uiBub[i].vy = 0;
    uiBub[i].r = UI_BUB_R[i] * (i == uiHomeFocus ? 1.26f : 0.80f);
  }
}

static void uiHomeNav(int dir) { uiHomeFocus = (uiHomeFocus + dir + UI_NCATS) % UI_NCATS; }

static bool uiDrawHome(float dt, int batPct, bool batCharging)
{
  if (dt > 0.25f)
    dt = 0.05f;
  canvas->fillScreen(RGB565_BLACK);

  // Physique en SOUS-PAS FIXES de 16 ms : a ~12 fps le badge recevait des pas
  // de ~90 ms — le rayon grossissait plus vite que le solveur de collisions ne
  // separait les bulles (morsures). En sous-echantillonnant, l'ESP simule
  // exactement comme l'emulateur a 60 fps, quel que soit son framerate.
  float rem = dt;
  while (rem > 0.0001f)
  {
    float h = rem > 0.016f ? 0.016f : rem;
    rem -= h;

    // cibles de rayon (la focus grossit) + ressort vers l'ancrage ; l'ancrage
    // est CLAMPE aux murs selon le rayon courant, sinon le ressort et le mur
    // se battent en permanence (vibration + bulle qui ecrase sa voisine)
    for (int i = 0; i < UI_NCATS; i++)
    {
      float tr = UI_BUB_R[i] * (i == uiHomeFocus ? 1.26f : 0.80f);
      uiBub[i].r += (tr - uiBub[i].r) * (1 - expf(-h * 10.0f));
      float hx = UI_BUB_HOME[i][0], hy = UI_BUB_HOME[i][1];
      float minY = 82.0f + uiBub[i].r;
      if (hy < minY)
        hy = minY;
      float hdx = hx - CX, hdy = hy - CY;
      float hd = sqrtf(hdx * hdx + hdy * hdy);
      float hmax = 176.0f - uiBub[i].r;
      if (hd > hmax && hd > 1)
      {
        hx = CX + hdx / hd * hmax;
        hy = CY + hdy / hd * hmax;
      }
      uiBub[i].vx += (hx - uiBub[i].x) * 26.0f * h;
      uiBub[i].vy += (hy - uiBub[i].y) * 26.0f * h;
    }
    // collisions : la bulle qui grossit POUSSE ses voisines
    for (int a = 0; a < UI_NCATS; a++)
      for (int b = a + 1; b < UI_NCATS; b++)
      {
        float dx = uiBub[b].x - uiBub[a].x, dy = uiBub[b].y - uiBub[a].y;
        float d2 = dx * dx + dy * dy;
        float mind = uiBub[a].r + uiBub[b].r + 8.0f;
        if (d2 < mind * mind && d2 > 1.0f)
        {
          float d = sqrtf(d2), ov = mind - d;
          float nx = dx / d, ny = dy / d;
          uiBub[a].x -= nx * ov * 0.25f;
          uiBub[a].y -= ny * ov * 0.25f;
          uiBub[b].x += nx * ov * 0.25f;
          uiBub[b].y += ny * ov * 0.25f;
          uiBub[a].vx -= nx * ov * 4.0f;
          uiBub[a].vy -= ny * ov * 4.0f;
          uiBub[b].vx += nx * ov * 4.0f;
          uiBub[b].vy += ny * ov * 4.0f;
        }
      }
    // integration + amortissement + murs (ecran rond, bandeau batterie)
    for (int i = 0; i < UI_NCATS; i++)
    {
      float damp = expf(-h * 4.0f);
      uiBub[i].vx *= damp;
      uiBub[i].vy *= damp;
      uiBub[i].x += uiBub[i].vx * h;
      uiBub[i].y += uiBub[i].vy * h;
      float ddx = uiBub[i].x - CX, ddy = uiBub[i].y - CY;
      float dd = sqrtf(ddx * ddx + ddy * ddy);
      float maxd = 176.0f - uiBub[i].r;
      if (dd > maxd && dd > 1)
      {
        uiBub[i].x = CX + ddx / dd * maxd;
        uiBub[i].y = CY + ddy / dd * maxd;
        // annule la composante de vitesse SORTANTE (sinon ca vibre au mur)
        float dot = (uiBub[i].vx * ddx + uiBub[i].vy * ddy) / dd;
        if (dot > 0)
        {
          uiBub[i].vx -= ddx / dd * dot;
          uiBub[i].vy -= ddy / dd * dot;
        }
      }
      if (uiBub[i].y - uiBub[i].r < 82)
      {
        uiBub[i].y = 82 + uiBub[i].r;
        if (uiBub[i].vy < 0)
          uiBub[i].vy = 0;
      }
    }
  }
  // dessin : les non-focus en contour, la focus en dernier, pleine
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < UI_NCATS; i++)
    {
      if ((pass == 1) != (i == uiHomeFocus))
        continue;
      int x = (int)(uiBub[i].x + 0.5f), y = (int)(uiBub[i].y + 0.5f);
      int r = (int)(uiBub[i].r + 0.5f);
      uint16_t col = UI_PASTELS[i];
      uint16_t tcol;
      const char *nm = UI_CAT_NAMES[i];
      if (i == uiHomeFocus)
      {
        canvas->fillCircle(x, y, r, col);
        tcol = RGB565_BLACK;
        // au survol : pastille noire avec fleche "play" a droite du label
        int tw = mfTextW(nm), ir = 13, gap = 8;
        int x0 = x - (tw + gap + 2 * ir) / 2;
        mfPrint(x0, y - 8, nm, tcol);
        int icx = x0 + tw + gap + ir;
        canvas->fillCircle(icx, y, ir, RGB565_BLACK);
        canvas->fillTriangle(icx - 3, y - 6, icx - 3, y + 6, icx + 7, y, col);
      }
      else
      {
        canvas->drawCircle(x, y, r, col); // contour ~3 px
        canvas->drawCircle(x, y, r - 1, col);
        canvas->drawCircle(x, y, r - 2, col);
        tcol = col;
        mfPrint(x - mfTextW(nm) / 2, y - 8, nm, tcol);
      }
    }

  uiBatteryHeader(batPct, batCharging);

  // encore en mouvement ? (permet a l'appelant de sauter le flush au repos)
  float act = 0;
  for (int i = 0; i < UI_NCATS; i++)
  {
    float tr = UI_BUB_R[i] * (i == uiHomeFocus ? 1.26f : 0.80f);
    act += fabsf(tr - uiBub[i].r) + fabsf(uiBub[i].vx) + fabsf(uiBub[i].vy);
  }
  return act > 0.8f;
}

// --------------------------------------------------- ecran Schedule
// Design Figma "Internal - Three.js Conference" : pastille DAY en haut,
// type d'event + horaire en Bebas Neue, titre en Dingos ExtraBold 30,
// pastille NOW, fleches gauche/droite en bas. Donnees d'exemple en dur —
// remplacees par le vrai programme quand Romain le fournit.
struct UiEvent
{
  uint8_t day;
  const char *type, *time, *l1, *l2;
};

// Horloge de conf, alimentee par la plateforme (main.cpp : RTC synchronisee
// via la webapp Draw ; emulateur : heure du navigateur ou parametre d'URL).
// uiNowMin < 0 = heure inconnue -> aucune pastille NOW.
static int uiNowDay = 0;  // 1 ou 2 pendant la conf, 0 sinon
static int uiNowMin = -1; // minutes locales du jour (0..1439)

static bool uiEventIsNow(const UiEvent &e)
{
  if (uiNowMin < 0 || uiNowDay != e.day)
    return false;
  int h1, m1, h2, m2;
  if (sscanf(e.time, "%d:%d - %d:%d", &h1, &m1, &h2, &m2) != 4)
    return false; // jalon sans plage ("18:30") : pas de NOW
  int s = h1 * 60 + m1, en = h2 * 60 + m2;
  if (en < s)
    en += 24 * 60; // plage qui passe minuit
  return uiNowMin >= s && uiNowMin < en;
}
// Programme officiel — source : Google Sheet "Three.js Conf Paris"
// (onglets Day 1/Day 2, releve du 2026-09-08). Lignes coupees pour 2 x
// Dingos 30, entrees de logistique interne omises.
static const UiEvent UI_EVENTS[] = {
    // ---- Day 1 · jeudi 10
    {1, "INFO", "09:00 - 10:00", "Doors open", "& coffee"},
    {1, "TALK", "10:00 - 10:05", "David Ronai", "opening talk"},
    {1, "SPEAKER", "10:05 - 10:35", "Vicente", "Lucendo"},
    {1, "SPEAKER", "10:35 - 10:50", "Kim Boutin", ""},
    {1, "SPEAKER", "10:50 - 11:05", "Robin Payot", "Zelda TSL"},
    {1, "BREAK", "11:05 - 11:45", "Break &", "exchange"},
    {1, "PANEL", "11:10 - 11:40", "Code AI", "workflow"},
    {1, "SPEAKER", "11:45 - 11:55", "Cassie Evans", "GSAP news"},
    {1, "SPEAKER", "11:55 - 12:25", "Damien", "Mortini"},
    {1, "LUNCH", "12:30 - 14:00", "Lunch", "book a resto"},
    {1, "SPEAKER", "14:00 - 14:20", "Celia Lopez", ""},
    {1, "SPEAKER", "14:20 - 14:40", "Thomas &", "Natalia"},
    {1, "SPEAKER", "14:40 - 14:55", "Lovis Odin", "fal.ai"},
    {1, "BREAK", "15:00 - 15:40", "Break &", "panels"},
    {1, "PANEL", "15:10 - 15:35", "Roast my", "folio"},
    {1, "LIGHTNING", "15:40 - 16:00", "Lightning", "talks x10"},
    {1, "SPEAKER", "16:00 - 16:15", "Romain &", "Julie"},
    {1, "KEYNOTE", "16:15 - 16:45", "Mr.doob", "last talk"},
    {1, "TALK", "16:45 - 16:50", "Closing", "Day 1"},
    {1, "PARTY", "17:00 - 18:30", "Toast &", "networking"},
    {1, "PANEL", "17:15 - 17:45", "Generative", "art"},
    {1, "INFO", "18:30", "Venue", "closes"},
    // ---- Day 2 · vendredi 11
    {2, "BREAKFAST", "08:00 - 09:00", "VIP with", "speakers"},
    {2, "INFO", "09:00 - 09:30", "Doors", "open"},
    {2, "TALK", "09:25 - 09:30", "David Ronai", "quick intro"},
    {2, "SPEAKER", "09:30 - 09:45", "Ponpon", "Mania"},
    {2, "SPEAKER", "09:45 - 10:05", "Dennis, Kris", "Poimandres"},
    {2, "SPEAKER", "10:05 - 10:20", "Daria", "interactive"},
    {2, "SPEAKER", "10:20 - 10:35", "Sean", "Miris"},
    {2, "BREAK", "10:40 - 11:10", "Break &", "AI 3D panel"},
    {2, "SPEAKER", "11:15 - 11:30", "Misha", "EdClub"},
    {2, "SPEAKER", "11:30 - 11:55", "Anderson", "Mancini"},
    {2, "SPEAKER", "11:55 - 12:20", "Renaud", "perf tricks"},
    {2, "LUNCH", "12:20 - 14:00", "Lunch", "not included"},
    {2, "SPEAKER", "14:00 - 14:10", "Cassandre", "Legay"},
    {2, "SPEAKER", "14:10 - 14:30", "Sunag", "TSL"},
    {2, "SPEAKER", "14:30 - 15:00", "Edan Kwan", "Lusion"},
    {2, "BREAK", "15:00 - 15:40", "Break &", "4D Gaussian"},
    {2, "LIGHTNING", "15:40 - 16:00", "Lightning", "talks x10"},
    {2, "SPEAKER", "16:00 - 16:20", "Merci Michel", "announcement"},
    {2, "KEYNOTE", "16:20 - 16:50", "Bruno Simon", "last keynote"},
    {2, "TALK", "16:50 - 17:00", "Closing", "talk"},
    {2, "PARTY", "17:30 - 18:15", "Networking", ""},
    {2, "INFO", "18:30", "Venue", "closes"},
    {2, "PARTY", "19:00 - 1:00", "After party", "at Fluctuart"},
};
#define UI_NEVENTS ((int)(sizeof(UI_EVENTS) / sizeof(UI_EVENTS[0])))

static bool uiDrawSchedule(int idx)
{
  canvas->fillScreen(RGB565_BLACK);
  const UiEvent &e = UI_EVENTS[idx];

  // pastille DAY (bleu jour 1, rose jour 2), texte navy ExtraBold
  uint16_t dayCol = (e.day == 1) ? rgb565(0xa5, 0xc9, 0xf1) : rgb565(0xfc, 0xa3, 0xf7);
  char dayTxt[8];
  snprintf(dayTxt, sizeof(dayTxt), "DAY %d", e.day);
  int dw = mfTextW(dayTxt) + 44;
  canvas->fillRoundRect(180 - dw / 2, 26, dw, 44, 22, dayCol);
  mfPrint(180 - mfTextW(dayTxt) / 2, 40, dayTxt, rgb565(0x1d, 0x24, 0x40));

  // type (gauche) + horaire (droite) en Bebas Neue
  bbPrint(66, 128, e.type, RGB565_WHITE);
  bbPrint(312 - bbTextW(e.time), 128, e.time, RGB565_WHITE);

  // titre sur 1-2 lignes en ExtraBold 30
  mtPrint(66, 162, e.l1, RGB565_WHITE);
  if (e.l2 && e.l2[0])
    mtPrint(66, 162 + 36, e.l2, RGB565_WHITE);

  // pastille NOW (uniquement si l'heure est connue et dans la plage)
  if (uiEventIsNow(e))
  {
    int nw = bbTextW("NOW") + 30;
    canvas->fillRoundRect(66, 246, nw, 32, 16, rgb565(0x9d, 0x97, 0xed));
    bbPrint(66 + 15, 250, "NOW", RGB565_WHITE);
  }

  // fleches de navigation en bas
  int cyb = 320;
  canvas->fillCircle(160, cyb, 15, rgb565(45, 45, 45));
  canvas->fillTriangle(165, cyb - 6, 165, cyb + 6, 154, cyb, rgb565(130, 130, 130));
  canvas->fillCircle(200, cyb, 15, RGB565_WHITE);
  canvas->fillTriangle(196, cyb - 6, 196, cyb + 6, 207, cyb, RGB565_BLACK);
  return false; // ecran statique : l'appelant ne flush que sur changement
}

// ------------------------------------ liste par categorie (scroll anime)
#define UI_LIST_VISIBLE 6

static bool uiDrawList(int cat, int menuSel, bool autoCyc, int batPct,
                       bool batCharging)
{
  canvas->fillScreen(RGB565_BLACK);
  int count = uiListCount(cat);

  static float scrollY = -1e9f, pillY = 0, pillW = 0, pcR = 0, pcG = 0, pcB = 0;
  static uint32_t lastMs = 0;
  static int lastCat = -1;
  uint32_t nowMs = millis();
  float adt = (nowMs - lastMs) / 1000.0f;
  lastMs = nowMs;
  int maxFirst = count - UI_LIST_VISIBLE;
  if (maxFirst < 0)
    maxFirst = 0;
  int first = constrain(menuSel - 2, 0, maxFirst);
  float targetScroll = first * 34.0f;
  char selLabel[32];
  uiListLabel(cat, menuSel, autoCyc, selLabel, sizeof(selLabel));
  bool selIsBack = (menuSel == count - 1);
  // "Back" : pilule BLANCHE (pas pastel) + place pour la fleche retour
  float targetW = mfTextW(selLabel) + 30.0f + (selIsBack ? 18.0f : 0.0f);
  uint16_t sc = selIsBack ? rgb565(255, 255, 255) : UI_PASTELS[menuSel % 4];
  float scR = (float)(((sc >> 11) & 31) << 3), scG = (float)(((sc >> 5) & 63) << 2),
        scB = (float)((sc & 31) << 3);
  if (adt > 0.25f || scrollY < -1e8f || cat != lastCat)
  {
    scrollY = targetScroll;
    pillY = 105 + menuSel * 34 - scrollY;
    pillW = targetW;
    pcR = scR;
    pcG = scG;
    pcB = scB;
    adt = 0;
    lastCat = cat;
  }
  float k = 1 - expf(-adt * 14.0f);
  scrollY += (targetScroll - scrollY) * k;
  float pTY = 105 + menuSel * 34 - scrollY;
  pillY += (pTY - pillY) * k;
  pillW += (targetW - pillW) * k;
  pcR += (scR - pcR) * k;
  pcG += (scG - pcG) * k;
  pcB += (scB - pcB) * k;

  canvas->fillRoundRect((int)(180 - pillW / 2 + 0.5f), (int)(pillY - 7 + 0.5f),
                        (int)(pillW + 0.5f), 30, 8,
                        rgb565((int)pcR, (int)pcG, (int)pcB));
  for (int i = 0; i < count; i++)
  {
    float y = 105 + i * 34 - scrollY;
    if (y < 58 || y > 336)
      continue;
    char label[32];
    uiListLabel(cat, i, autoCyc, label, sizeof(label));
    uint16_t tc = (i == menuSel) ? RGB565_BLACK : rgb565(175, 175, 175);
    if (i == menuSel && i == count - 1)
    {
      // "Back" survole : petite fleche retour a gauche du mot
      int tw = mfTextW(label);
      int x0 = 180 - (tw + 18) / 2;
      int yy = (int)(y + 0.5f);
      canvas->fillTriangle(x0, yy + 8, x0 + 7, yy + 2, x0 + 7, yy + 14, tc);
      canvas->fillRect(x0 + 7, yy + 6, 6, 4, tc);
      mfPrint(x0 + 18, yy, label, tc);
    }
    else
      mfPrint(180 - mfTextW(label) / 2, (int)(y + 0.5f), label, tc);
  }

  canvas->fillRect(0, 0, W, 96, RGB565_BLACK);
  canvas->fillRect(0, 330, W, H - 330, RGB565_BLACK);
  uiBatteryHeader(batPct, batCharging);

  uint16_t arrow = rgb565(120, 120, 120);
  if (first > 0)
    canvas->fillTriangle(174, 88, 186, 88, 180, 80, arrow);
  if (first + UI_LIST_VISIBLE < count)
    canvas->fillTriangle(174, 314, 186, 314, 180, 322, arrow);

  return (fabsf(targetScroll - scrollY) + fabsf(pTY - pillY) +
          fabsf(targetW - pillW) + fabsf(scR - pcR) + fabsf(scG - pcG) +
          fabsf(scB - pcB)) > 0.7f;
}
