// 40 avatars — variations du perso "idle rainbow", un par badge (speaker ou
// crew de threejs.paris). L'avatar actif est choisi dans More > Settings
// (code 39193) et persiste en NVS ("avatar") : il colore la sphere du perso
// (rotation de teinte + saturation de PAL_RAINBOW dans irGenFrame) et choisit
// son VISAGE parmi les 9 designs Figma (node 4195-8272).
//
// Fichier partage firmware / emulateur. A inclure AVANT anims_extra.h
// (irGenFrame et animIdleRainbow lisent g_avatarIdx / irDirtyMask) et apres
// la declaration de canvas. Le museau (visage 0) est rendu par la plateforme
// (drawMouthImg cote firmware, drawMouth cote emulateur) via le wrapper
// avatarPlatformMouth defini dans chaque TU.
#pragma once

// definis plus loin dans anims_extra.h (utilises par irGenFrame pour la
// transformation de teinte de l'avatar)
static void rgb2hsl(float r, float g, float b, float *h, float *s, float *l);
static void hsl2rgb(float h, float s, float l, float *r, float *g, float *b);
// museau du perso original, rendu par la plateforme (defini apres l'include)
static void avatarPlatformMouth(float mx, float my, float mw, float mh, uint16_t ink);
// bouche du visage "rire" (masque SVG noir + blanc), idem par plateforme
static void avatarPlatformLaugh(float mx, float my, float mw, float mh, uint16_t ink);

// Les 9 visages du Figma 4195-8272, de gauche a droite. Geometrie extraite
// des metadonnees Figma (frames yeux/bouche), echelle calee sur l'ecart des
// yeux du perso original : 1 px Figma = 0.005712 * fr.
enum AvatarFace : uint8_t
{
  AF_MUSEAU = 0,   // museau + moustache (le perso original)
  AF_RIRE,         // grande bouche ouverte joyeuse, bas blanc
  AF_SOURIRE,      // petit sourire fin
  AF_VAGUE,        // petite bouche ondulee
  AF_CHAT,         // yeux ovales + bouche de chat (omega)
  AF_LUNETTES,     // lunettes de soleil (bandeau + verres) + rictus
  AF_FERMES,       // grands yeux fermes contents ^^ + petit sourire
  AF_MOUSTACHE,    // grande moustache ondulee
  AF_ETOILES,      // yeux etoiles a 8 branches relies par une barre
};

struct AvatarDef
{
  const char *name; // personne (protege par le code, sert aussi au social)
  int16_t hue;      // rotation de teinte (deg) appliquee a PAL_RAINBOW
  float sat;        // multiplicateur de saturation
  uint8_t face;     // AvatarFace
  uint8_t extra;    // 0 rien, 1 joues roses, 2 etincelle
};

// 22 speakers du schedule + crew. Teintes reparties sur la roue, visages
// varies pour que deux badges voisins ne se ressemblent pas.
static const AvatarDef AVATARS[] = {
    {"David", 0, 1.00f, AF_MUSEAU, 0},      // le perso original
    {"Daniel", -30, 1.00f, AF_RIRE, 0},     // sunset
    {"Kim", 40, 1.05f, AF_CHAT, 1},         // bubblegum + joues
    {"Robin", 90, 1.10f, AF_LUNETTES, 0},   // acid
    {"Cassie", 130, 1.00f, AF_SOURIRE, 0},
    {"Vincente", 180, 1.05f, AF_VAGUE, 0},
    {"Celia", 220, 1.00f, AF_FERMES, 1},
    {"Thomas", 260, 1.05f, AF_MOUSTACHE, 0},
    {"Natalia", 300, 1.00f, AF_RIRE, 1},
    {"Herve", 330, 1.05f, AF_LUNETTES, 0},
    {"Mr.doob", 0, 0.35f, AF_ETOILES, 0},   // quasi N&B, yeux etoiles
    {"Ponpom", 20, 1.25f, AF_CHAT, 1},
    {"Dennis", 60, 1.05f, AF_VAGUE, 0},
    {"Miris", 110, 1.00f, AF_FERMES, 2},
    {"Daria", 150, 1.05f, AF_SOURIRE, 0},
    {"Sunag", 200, 1.00f, AF_MOUSTACHE, 0},
    {"Renaud", 240, 1.05f, AF_MUSEAU, 2},
    {"Anderson", 280, 1.00f, AF_LUNETTES, 0},
    {"Edan", 320, 1.10f, AF_RIRE, 0},
    {"Merci Michel", -60, 1.05f, AF_FERMES, 1},
    {"Cassandre", 70, 1.00f, AF_SOURIRE, 1},
    {"Bruno", 335, 1.15f, AF_RIRE, 1},      // love
    {"Crew 01", 15, 1.00f, AF_VAGUE, 0},
    {"Crew 02", 45, 1.05f, AF_CHAT, 0},
    {"Crew 03", 80, 1.00f, AF_SOURIRE, 0},
    {"Crew 04", 100, 1.10f, AF_MUSEAU, 1},
    {"Crew 05", 140, 1.00f, AF_ETOILES, 0},
    {"Crew 06", 165, 1.05f, AF_FERMES, 0},
    {"Crew 07", 190, 1.00f, AF_RIRE, 1},
    {"Crew 08", 210, 1.10f, AF_LUNETTES, 0},
    {"Crew 09", 235, 1.00f, AF_MOUSTACHE, 2},
    {"Crew 10", 255, 1.05f, AF_CHAT, 0},
    {"Crew 11", 275, 1.00f, AF_VAGUE, 1},
    {"Crew 12", 295, 1.10f, AF_SOURIRE, 0},
    {"Crew 13", 315, 1.00f, AF_MUSEAU, 0},
    {"Crew 14", 345, 1.05f, AF_FERMES, 2},
    {"Crew 15", -15, 1.10f, AF_RIRE, 1},
    {"Crew 16", -45, 1.00f, AF_LUNETTES, 0},
    {"Crew 17", 55, 1.20f, AF_CHAT, 0},
    {"Crew 18", 120, 0.65f, AF_ETOILES, 2}, // pastel doux
};
#define AVATAR_N ((int)(sizeof(AVATARS) / sizeof(AVATARS[0])))

static uint8_t g_avatarIdx = 0;     // avatar SAUVE (NVS) : colore la sphere
static uint8_t g_avatarFaceIdx = 0; // avatar AFFICHE par le visage (= sauve,
                                    // sauf pendant la preview des Settings)
static bool g_ballDirty = false; // ballSprite (snake/DVD/jeux) a regenerer
static uint32_t irDirtyMask = 0; // bit i = frame de rotation idle i a
                                 // regenerer (avatar/buddy change) ; les
                                 // frames AFFICHEES sont refaites en priorite
// pilotes par la reaction sociale (social_ui.h) pendant une rencontre :
// gel de la rotation de la sphere (les triggers du visualiseur figent le
// regard) et rebond vertical du blit (bounce Happy/Wow)
static float g_lookFreeze = 0.0f;  // 0 = libre, 1 = regard gele
static int g_sphereYOff = 0;       // decalage vertical de la sphere (px)
static float g_sphereScale = 1.0f; // scale de la sphere (battement Love)

// ---- buddy CUSTOM (parcours Setup sur telephone) : quand actif, il remplace
// l'avatar de la table pour la couleur de la sphere ET le visage. Persiste en
// NVS (bcust/bhue/bsat/bface) ; choisir un avatar dans Settings le desactive.
static bool g_buddyCustom = false;
static AvatarDef g_buddyCustomDef = {"Custom", 0, 1.00f, AF_MUSEAU, 0};
static int g_faceForce = -1; // >=0 : force un avatar de la table (preview
                             // Settings, meme si le custom est actif)
static inline const AvatarDef &avatarCurrent()
{
  if (g_faceForce >= 0)
    return AVATARS[g_faceForce];
  return g_buddyCustom ? g_buddyCustomDef : AVATARS[g_avatarFaceIdx];
}

// ---- rendu du visage --------------------------------------------------

// contexte de projection sphere (rempli par avatarDrawFace, utilise par les
// helpers) : rotation du regard autour de Y
static float avCosT = 1, avSinT = 0, avCx = 0, avCy = 0, avFr = 1;
static float avBreathe = 0, avYShift = 0;

// projette un point (nx, ny) du disque unite : x ecran, squish lateral,
// visibilite (z tourne)
static void avProject(float nx, float ny, float *sx, float *scale, float *vis)
{
  float nzsq = 1 - nx * nx - ny * ny;
  float nz = nzsq > 0 ? sqrtf(nzsq) : 0.1f;
  float nxr = nx * avCosT + nz * avSinT;
  float nzr = -nx * avSinT + nz * avCosT;
  *sx = avCx + nxr * avFr + avBreathe * 0.6f;
  *scale = nzr > 0.3f ? nzr : 0.3f;
  *vis = nzr;
}
static float avY(float ny) // y ecran d'un point du visage
{
  return avCy + ny * avFr + avBreathe * 0.4f + avYShift;
}

// ---- anticrenelage (revue Romain 2026-09-07 : visages creneles) ----
// melange un pixel avec l'encre selon une couverture 0..1 (lecture du
// framebuffer : API canvas-> des deux plateformes)
static inline void avBlend(int x, int y, uint16_t ink, float a)
{
  if (x < 0 || x >= W || y < 0 || y >= H || a <= 0.003f)
    return;
  uint16_t *fb = canvas->getFramebuffer();
  if (a >= 0.997f)
  {
    fb[y * W + x] = ink;
    return;
  }
  uint16_t d = fb[y * W + x];
  int dr = (d >> 11) & 31, dg = (d >> 5) & 63, db = d & 31;
  int ir = (ink >> 11) & 31, ig = (ink >> 5) & 63, ib = ink & 31;
  int r = dr + (int)((ir - dr) * a);
  int g = dg + (int)((ig - dg) * a);
  int b = db + (int)((ib - db) * a);
  fb[y * W + x] = (uint16_t)((r << 11) | (g << 5) | b);
}

// ellipse pleine anticrenelee (bord adouci sur ~1 px)
static void avFillEllipseAA(float cx, float cy, float rx, float ry, uint16_t ink)
{
  if (rx < 0.5f || ry < 0.5f)
    return;
  float rm = rx < ry ? rx : ry;
  int x0 = (int)(cx - rx - 1), x1 = (int)(cx + rx + 1);
  int y0 = (int)(cy - ry - 1), y1 = (int)(cy + ry + 1);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
    {
      float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
      float d = sqrtf(dx * dx + dy * dy);
      float a = (1 - d) * rm + 0.5f;
      if (a <= 0)
        continue;
      avBlend(x, y, ink, a > 1 ? 1 : a);
    }
}

// trace epais le long d'une courbe : serie de disques (pas d'arc natif).
// mode : 0 = arc doux vers le bas (sourire), 1 = onde sin 1.5 periode,
// 2 = omega chat (2 bosses vers le bas), 3 = arc vers le haut (oeil ferme),
// 4 = rictus incline (monte a droite)
static void avStroke(int mode, float x0, float w, float yBase, float amp,
                     float r, uint16_t ink)
{
  int n = (int)(w / (r > 1 ? r * 0.5f : 1)) + 4;
  for (int i = 0; i <= n; i++)
  {
    float u = (float)i / n, y = yBase;
    switch (mode)
    {
    case 0: y = yBase + amp * (0.25f - (u - 0.5f) * (u - 0.5f)) * 4.0f; break;
    case 1: y = yBase + amp * sinf(u * 3.0f * (float)PI); break;
    case 2: y = yBase + amp * fabsf(sinf(u * 2.0f * (float)PI)); break; // 2 bosses
    case 3: y = yBase - amp * sinf(u * (float)PI); break;
    case 4: y = yBase - amp * u + amp * 0.5f * (0.25f - (u - 0.5f) * (u - 0.5f)) * 4.0f; break;
    }
    avFillEllipseAA(x0 + u * w, y, r, r, ink);
  }
}

// etoile a 8 branches (yeux AF_ETOILES) : disque central + 8 lobes
static void avStar(float cx, float cy, float R, uint16_t ink)
{
  avFillEllipseAA(cx, cy, R * 0.72f, R * 0.72f, ink);
  for (int k = 0; k < 8; k++)
  {
    float a = k * (float)PI / 4.0f;
    avFillEllipseAA(cx + cosf(a) * R * 0.68f, cy + sinf(a) * R * 0.68f,
                    R * 0.36f, R * 0.36f, ink);
  }
}

// Visage complet de l'avatar affiche, projete sur la sphere (cx, cy, fr).
// breathe/yShift : micro-mouvements de l'idle ; cosT/sinT : rotation du
// regard ; openness : clignement (1 = ouvert).
static void avatarDrawFace(float cx, float cy, float fr, float breathe,
                           float yShift, float cosT, float sinT,
                           float openness, uint16_t ink)
{
  const AvatarDef &av = avatarCurrent();
  avCx = cx; avCy = cy; avFr = fr;
  avCosT = cosT; avSinT = sinT;
  avBreathe = breathe; avYShift = yShift;

  float exl, exr, mxx, scl, scr, scm, visl, visr, vism;

  switch (av.face)
  {
  // Geometrie extraite du board Figma 4197-8511 (visages poses sur spheres
  // 310 px -> cotes en unites de rayon, symetrisees).
  // ------------------------------------------------ museau (perso original)
  case AF_MUSEAU:
  {
    avProject(-0.381f, -0.210f, &exl, &scl, &visl);
    avProject(0.381f, -0.210f, &exr, &scr, &visr);
    avProject(0, 0.074f, &mxx, &scm, &vism);
    float er = fr * 0.099f;
    float ryf = er * openness; if (ryf < 1) ryf = 1;
    if (visl > 0)
      avFillEllipseAA(exl, avY(-0.210f), er * scl, ryf, ink);
    if (visr > 0)
      avFillEllipseAA(exr, avY(-0.210f), er * scr, ryf, ink);
    avatarPlatformMouth(mxx, avY(0.074f), fr * 0.316f * scm, fr * 0.342f, ink);
    break;
  }
  // ------------------------------------------- rire : grande bouche ouverte
  case AF_RIRE:
  {
    // recale sur le rendu de reference (image Romain 2026-09-07) : bouche
    // CARREE-ARRONDIE (plus un ellipse), blanc en bol qui demarre juste
    // au-dessus du centre, lisere noir conserve en bas et sur les cotes
    // echelle/position : 2e reference Romain 2026-09-07 (bouche agrandie,
    // yeux recales)
    avProject(-0.465f, -0.215f, &exl, &scl, &visl);
    avProject(0.465f, -0.215f, &exr, &scr, &visr);
    avProject(0, 0.085f, &mxx, &scm, &vism);
    float er = fr * 0.095f;
    float ryf = er * openness; if (ryf < 1) ryf = 1;
    if (visl > 0)
      avFillEllipseAA(exl, avY(-0.255f), er * scl, ryf, ink);
    if (visr > 0)
      avFillEllipseAA(exr, avY(-0.255f), er * scr, ryf, ink);
    // bouche = masque tessele de l'export SVG Mouth_visage2.svg (73x59),
    // rendu par la plateforme — voir initLaughMask (ratio du SVG conserve)
    avatarPlatformLaugh(mxx, avY(0.085f), fr * 0.485f * scm, fr * 0.392f, ink);
    break;
  }
  // ------------------------------------------------------ petit sourire fin
  case AF_SOURIRE:
  {
    avProject(-0.431f, -0.213f, &exl, &scl, &visl);
    avProject(0.431f, -0.213f, &exr, &scr, &visr);
    avProject(0, -0.032f, &mxx, &scm, &vism);
    float er = fr * 0.084f;
    float ryf = er * openness; if (ryf < 1) ryf = 1;
    if (visl > 0)
      avFillEllipseAA(exl, avY(-0.213f), er * scl, ryf, ink);
    if (visr > 0)
      avFillEllipseAA(exr, avY(-0.213f), er * scr, ryf, ink);
    float w = fr * 0.380f * scm;
    avStroke(0, mxx - w / 2, w, avY(-0.038f) - fr * 0.010f, fr * 0.025f,
             fr * 0.026f, ink);
    break;
  }
  // -------------------------------------------------------- petite vague
  case AF_VAGUE:
  {
    avProject(-0.431f, -0.194f, &exl, &scl, &visl);
    avProject(0.431f, -0.194f, &exr, &scr, &visr);
    avProject(0, 0.035f, &mxx, &scm, &vism);
    float er = fr * 0.103f;
    float ryf = er * openness; if (ryf < 1) ryf = 1;
    if (visl > 0)
      avFillEllipseAA(exl, avY(-0.194f), er * scl, ryf, ink);
    if (visr > 0)
      avFillEllipseAA(exr, avY(-0.194f), er * scr, ryf, ink);
    float w = fr * 0.440f * scm;
    avStroke(1, mxx - w / 2, w, avY(0.032f), fr * 0.018f, fr * 0.028f, ink);
    break;
  }
  // -------------------------------------- chat : yeux ovales + omega large
  case AF_CHAT:
  {
    avProject(-0.316f, -0.204f, &exl, &scl, &visl);
    avProject(0.316f, -0.204f, &exr, &scr, &visr);
    avProject(0, 0.150f, &mxx, &scm, &vism);
    float rx = fr * 0.075f, ryv = fr * 0.094f;
    float ryf = ryv * openness; if (ryf < 1) ryf = 1;
    if (visl > 0)
      avFillEllipseAA(exl, avY(-0.204f), rx * scl, ryf, ink);
    if (visr > 0)
      avFillEllipseAA(exr, avY(-0.204f), rx * scr, ryf, ink);
    float w = fr * 0.580f * scm, amp = fr * 0.105f;
    avStroke(2, mxx - w / 2, w, avY(0.100f), amp, fr * 0.035f, ink);
    break;
  }
  // ---------------------------- lunettes de soleil : bandeau + verres + rictus
  case AF_LUNETTES:
  {
    avProject(-0.200f, -0.213f, &exl, &scl, &visl);
    avProject(0.200f, -0.213f, &exr, &scr, &visr);
    avProject(0.184f, 0.058f, &mxx, &scm, &vism);
    float barTop = avY(-0.297f), barH = fr * 0.065f;
    float lensHalf = fr * 0.174f, lensDrop = fr * 0.103f;
    float xL = exl - lensHalf * scl, xR = exr + lensHalf * scr;
    canvas->fillRoundRect((int)xL, (int)barTop, (int)(xR - xL), (int)barH,
                          (int)(barH * 0.3f), ink);
    for (int s = 0; s < 2; s++)
    {
      float c = s ? exr : exl, sc = s ? scr : scl;
      float hw = lensHalf * sc;
      canvas->fillRect((int)(c - hw), (int)(barTop + barH - 1), (int)(2 * hw),
                       (int)(lensDrop * 0.5f), ink);
      avFillEllipseAA(c, barTop + barH + lensDrop * 0.5f, hw,
                      lensDrop * 0.5f, ink);
    }
    float w = fr * 0.330f * scm;
    avStroke(4, mxx - w / 2, w, avY(0.058f) + fr * 0.045f, fr * 0.090f,
             fr * 0.030f, ink);
    break;
  }
  // ------------------------------ grands yeux fermes ^^ + petit sourire
  case AF_FERMES:
  {
    avProject(-0.271f, -0.184f, &exl, &scl, &visl);
    avProject(0.271f, -0.184f, &exr, &scr, &visr);
    avProject(0.090f, 0.178f, &mxx, &scm, &vism);
    float aw = fr * 0.297f, amp = fr * 0.050f, base = avY(-0.159f);
    avStroke(3, exl - aw * scl / 2, aw * scl, base, amp, fr * 0.030f, ink);
    avStroke(3, exr - aw * scr / 2, aw * scr, base, amp, fr * 0.030f, ink);
    float w = fr * 0.323f * scm;
    avStroke(0, mxx - w / 2, w, avY(0.178f) - fr * 0.012f, fr * 0.043f,
             fr * 0.027f, ink);
    break;
  }
  // ------------------------------------------------- grande moustache ondulee
  case AF_MOUSTACHE:
  {
    avProject(-0.342f, -0.168f, &exl, &scl, &visl);
    avProject(0.342f, -0.168f, &exr, &scr, &visr);
    avProject(0, 0.103f, &mxx, &scm, &vism);
    float er = fr * 0.084f;
    float ryf = er * openness; if (ryf < 1) ryf = 1;
    if (visl > 0)
      avFillEllipseAA(exl, avY(-0.168f), er * scl, ryf, ink);
    if (visr > 0)
      avFillEllipseAA(exr, avY(-0.168f), er * scr, ryf, ink);
    float w = fr * 0.826f * scm;
    avStroke(1, mxx - w / 2, w, avY(0.103f), fr * 0.033f, fr * 0.035f, ink);
    break;
  }
  // --------------------------- yeux etoiles a 8 branches relies par une barre
  case AF_ETOILES:
  {
    avProject(-0.248f, -0.129f, &exl, &scl, &visl);
    avProject(0.248f, -0.129f, &exr, &scr, &visr);
    avProject(0.052f, 0.168f, &mxx, &scm, &vism);
    float ey = avY(-0.129f), R = fr * 0.142f;
    canvas->fillRect((int)exl, (int)(ey - fr * 0.035f), (int)(exr - exl),
                     (int)(fr * 0.070f), ink); // barre de liaison
    avStar(exl, ey, R * (0.7f + 0.3f * scl), ink);
    avStar(exr, ey, R * (0.7f + 0.3f * scr), ink);
    float w = fr * 0.374f * scm;
    avStroke(0, mxx - w / 2, w, avY(0.168f) - fr * 0.010f, fr * 0.035f,
             fr * 0.027f, ink);
    break;
  }
  }
}

// Extras dessines apres le visage. Les JOUES suivent la projection sphere
// (regard + yShift), comme les yeux/bouche — corrige 2026-08-17 (video
// Romain : elles restaient fixes pendant l'anim). L'ETINCELLE reste fixe a
// l'ecran, coherente avec les highlights cuits de la sphere.
// (Reutilise le contexte av* rempli par avatarDrawFace juste avant.)
static void avatarDrawExtras(float cx, float cy, float fr, float breathe)
{
  const AvatarDef &av = avatarCurrent();
  if (av.extra == 1) // joues roses sous les yeux
  {
    uint16_t blush = rgb565(246, 148, 168);
    float sxl, sxr, scl, scr, visl, visr;
    avProject(-0.40f, -0.055f, &sxl, &scl, &visl);
    avProject(0.40f, -0.055f, &sxr, &scr, &visr);
    float by = avY(-0.055f);
    if (visl > 0)
      canvas->fillEllipse((int)sxl, (int)by, (int)(fr * 0.085f * scl),
                          (int)(fr * 0.05f), blush);
    if (visr > 0)
      canvas->fillEllipse((int)sxr, (int)by, (int)(fr * 0.085f * scr),
                          (int)(fr * 0.05f), blush);
  }
  else if (av.extra == 2) // etincelle haut-droite (fixe a l'ecran)
  {
    uint16_t w = rgb565(255, 252, 240);
    int sx = (int)(cx + fr * 0.47f), sy = (int)(cy - fr * 0.47f), s = (int)(fr * 0.075f);
    canvas->fillRect(sx - 1, sy - s, 3, 2 * s + 1, w);
    canvas->fillRect(sx - s, sy - 1, 2 * s + 1, 3, w);
  }
  (void)breathe;
}
