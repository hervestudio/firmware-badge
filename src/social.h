// Rencontres entre badges (ESP-NOW) : quand le Conf Buddy est affiche, le
// badge diffuse un beacon broadcast (~1 Hz, canal 1) avec son identite et
// ecoute ceux des autres. Un badge recu avec un RSSI fort (= a 1-3 m) et pas
// vu recemment declenche la reaction du buddy (social_ui.h) et s'ajoute au
// journal des rencontres en NVS ("met", pour le futur ecran social).
//
// La radio n'est active QUE pendant l'anim Conf Buddy (socialStart/Stop
// pilotes par la boucle) : pas d'impact batterie dans les menus/jeux, et
// aucune interference avec les AP WiFi de Draw/Setup/OTA.
// Suppose definis avant inclusion : qrName, AVATARS/g_avatarIdx,
// g_buddyCustom/g_buddyCustomDef, prefs, Serial0, social_ui.h (via include).
#pragma once
#include <esp_now.h>
#include <esp_wifi.h>
#include "social_ui.h"

#define SOCIAL_CHANNEL 1
#define SOCIAL_BEACON_MS 1000
// Seuil de proximite REGLABLE (Settings > Proximity, NVS "prox") : 4 niveaux
// de Touch (badges quasi colles) a Far (~5 m). -62 = "Normal" par defaut.
static int8_t socialRssiNear = -62;  // valeur active (UI_PROX_LEVELS)
static bool socialProbeOnly = false; // ecran Proximity : ecoute sans reagir
#define SOCIAL_FRESH_MS 2500   // beacon "encore la"
#define SOCIAL_COOLDOWN_MS 60000    // par badge croise
#define SOCIAL_GLOBAL_MS 25000      // entre deux reactions, tous badges
#define SOCIAL_MAXPEERS 40          // toute la serie sans eviction

struct __attribute__((packed)) SocialBeacon
{
  uint32_t magic; // 'TJC1'
  uint8_t avatar;
  uint8_t face;
  uint8_t cust;
  uint8_t sat100;
  int16_t hue;
  char name[20];
  // meilleurs scores (Meet > Leaderboard), ordre LB_GAME_NAMES : Snake,
  // Pong, Sphere Run, Roundtris. Champ AJOUTE en fin de paquet : les vieux
  // firmwares (paquet court) restent acceptes, scores a 0.
  uint16_t scores[LB_GAMES];
};
#define SOCIAL_MAGIC 0x314A4354u // "TCJ1" little-endian
#define SOCIAL_BEACON_V1_LEN offsetof(SocialBeacon, scores)

struct SocialPeer
{
  uint8_t mac[6];
  char name[24];
  uint8_t avatar;
  float rssi; // EMA
  uint32_t lastSeen;
  uint32_t lastReact;
  uint16_t scores[LB_GAMES]; // derniers scores annonces (0 si vieux firmware)
};

static SocialPeer socialPeers[SOCIAL_MAXPEERS];
static int socialNPeers = 0;
static bool socialOn = false;
static uint32_t socialNextBeacon = 0;
static uint16_t socialMyBest[LB_GAMES]; // mes records, caches a socialStart
static portMUX_TYPE socialMux = portMUX_INITIALIZER_UNLOCKED;
static const uint8_t SOCIAL_BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---- Leaderboard : persistance NVS des tables lb* (fusion lbMerge dans
// menu_ui.h, partagee avec l'emulateur). "lb1" : "nom\tsnake\tpong\trun\ttetris\n".
static void lbSave()
{
  String m;
  char line[64];
  for (int i = 0; i < lbN; i++)
  {
    snprintf(line, sizeof(line), "%s\t%u\t%u\t%u\t%u\n", lbNames[i],
             lbScores[i][0], lbScores[i][1], lbScores[i][2], lbScores[i][3]);
    m += line;
  }
  prefs.putString("lb1", m);
  lbDirty = false;
}

static void lbLoad()
{
  String m = prefs.getString("lb1", "");
  lbN = 0;
  int pos = 0;
  while (pos < (int)m.length() && lbN < MET_MAX)
  {
    int nl = m.indexOf('\n', pos);
    if (nl < 0)
      break;
    int tab = m.indexOf('\t', pos);
    if (tab > pos && tab < nl)
    {
      snprintf(lbNames[lbN], sizeof(lbNames[0]), "%s",
               m.substring(pos, tab).c_str());
      int p2 = tab + 1;
      for (int g = 0; g < LB_GAMES; g++)
      {
        lbScores[lbN][g] = (uint16_t)m.substring(p2, nl).toInt();
        int t2 = m.indexOf('\t', p2);
        if (t2 < 0 || t2 > nl)
          break;
        p2 = t2 + 1;
      }
      lbN++;
    }
    pos = nl + 1;
  }
}

// Callback ESP-NOW (tache WiFi) : met a jour la table sous spinlock, court.
static void socialRecvCb(const esp_now_recv_info *info, const uint8_t *data,
                         int len)
{
  if (len < (int)SOCIAL_BEACON_V1_LEN)
    return;
  const SocialBeacon *b = (const SocialBeacon *)data;
  if (b->magic != SOCIAL_MAGIC)
    return;
  bool hasScores = len >= (int)sizeof(SocialBeacon);
  int8_t rssi = info->rx_ctrl ? info->rx_ctrl->rssi : -100;
  uint32_t now = millis();
  portENTER_CRITICAL(&socialMux);
  int idx = -1, oldest = 0;
  for (int i = 0; i < socialNPeers; i++)
  {
    if (memcmp(socialPeers[i].mac, info->src_addr, 6) == 0)
    {
      idx = i;
      break;
    }
    if (socialPeers[i].lastSeen < socialPeers[oldest].lastSeen)
      oldest = i;
  }
  if (idx < 0)
  {
    idx = socialNPeers < SOCIAL_MAXPEERS ? socialNPeers++ : oldest;
    memcpy(socialPeers[idx].mac, info->src_addr, 6);
    socialPeers[idx].rssi = rssi;
    socialPeers[idx].lastReact = 0;
  }
  SocialPeer &p = socialPeers[idx];
  p.rssi = p.rssi * 0.6f + rssi * 0.4f; // lisse le bruit du RSSI
  p.lastSeen = now;
  p.avatar = b->avatar;
  memcpy(p.name, b->name, sizeof(b->name));
  p.name[sizeof(b->name)] = 0;
  if (hasScores)
    memcpy(p.scores, b->scores, sizeof(p.scores));
  else
    memset(p.scores, 0, sizeof(p.scores));
  portEXIT_CRITICAL(&socialMux);
}

static void socialStart()
{
  if (socialOn)
    return;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  // puissance d'emission bridee : (1) portee courte voulue pour la detection
  // de proximite, (2) reduit le pic de courant radio qui peut faire chuter
  // le rail d'alim (reset POWERON observe sur certaines cartes)
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  esp_wifi_set_channel(SOCIAL_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK)
  {
    Serial0.println("social : esp_now_init KO");
    WiFi.mode(WIFI_OFF);
    return;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, SOCIAL_BCAST, 6);
  peer.channel = SOCIAL_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&peer);
  esp_now_register_recv_cb(socialRecvCb);
  socialNPeers = 0;
  socialNextBeacon = 0;
  // scores annonces dans le beacon : relus a chaque allumage de la radio
  // (une partie jouee entre-temps est donc prise en compte au retour idle)
  socialMyBest[0] = prefs.getUShort("snakeBest", 0);
  socialMyBest[1] = prefs.getUShort("pongBest", 0);
  socialMyBest[2] = prefs.getUShort("runBest", 0);
  socialMyBest[3] = prefs.getUShort("tetroBest", 0);
  socialOn = true;
  Serial0.println("social : radio ON (Conf Buddy)");
}

static void socialStop()
{
  if (!socialOn)
    return;
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  socialOn = false;
  if (lbDirty)
    lbSave(); // derniers scores appris pendant cette session radio
  // arret PROPRE : la radio n'a pas tue le badge -> desarme le marqueur du
  // briseur de boucle (sinon, quitter l'idle dans les 8 s laissait le
  // marqueur arme et le boot suivant bloquait le social a tort)
  prefs.putUChar("socboot", 0);
  Serial0.println("social : radio OFF");
}

// Persistance des compteurs de rencontres (table partagee metNames/metCounts
// de menu_ui.h) en NVS "met2" : lignes "nom\tcompte\n"
static void socialMetSave()
{
  String m;
  for (int i = 0; i < metN; i++)
  {
    m += metNames[i];
    m += '\t';
    m += String(metCounts[i]);
    m += '\n';
  }
  prefs.putString("met2", m);
}

static void socialMetLoad()
{
  String m = prefs.getString("met2", "");
  metN = 0;
  int pos = 0;
  while (pos < (int)m.length() && metN < MET_MAX)
  {
    int nl = m.indexOf('\n', pos);
    if (nl < 0)
      break;
    int tab = m.indexOf('\t', pos);
    if (tab > pos && tab < nl)
    {
      snprintf(metNames[metN], sizeof(metNames[0]), "%s",
               m.substring(pos, tab).c_str());
      metCounts[metN] = (uint16_t)m.substring(tab + 1, nl).toInt();
      metN++;
    }
    pos = nl + 1;
  }
}

// RSSI lisse du pair le plus recent/fort (jauge live de l'ecran Proximity) ;
// retourne -100 si personne d'entendu depuis 3 s
static float socialNearestRssi()
{
  float best = -100;
  uint32_t now = millis();
  portENTER_CRITICAL(&socialMux);
  for (int i = 0; i < socialNPeers; i++)
    if (now - socialPeers[i].lastSeen < 3000 && socialPeers[i].rssi > best)
      best = socialPeers[i].rssi;
  portEXIT_CRITICAL(&socialMux);
  return best;
}

// A appeler chaque frame quand la radio est active : beacon periodique +
// detection de rencontre (RSSI fort, cooldown par badge)
static void socialLoop(uint32_t now)
{
  if (!socialOn)
    return;
  if (now >= socialNextBeacon)
  {
    socialNextBeacon = now + SOCIAL_BEACON_MS + (esp_random() & 255);
    SocialBeacon b = {};
    b.magic = SOCIAL_MAGIC;
    b.avatar = g_avatarIdx;
    const AvatarDef &av = g_buddyCustom ? g_buddyCustomDef : AVATARS[g_avatarIdx];
    b.face = av.face;
    b.cust = g_buddyCustom ? 1 : 0;
    b.sat100 = (uint8_t)(av.sat * 100 + 0.5f);
    b.hue = av.hue;
    snprintf(b.name, sizeof(b.name), "%s",
             qrName[0] ? qrName : AVATARS[g_avatarIdx].name);
    memcpy(b.scores, socialMyBest, sizeof(b.scores));
    esp_now_send(SOCIAL_BCAST, (const uint8_t *)&b, sizeof(b));
  }
  // diagnostic : pairs entendus + RSSI lisse (toutes les 3 s)
  static uint32_t socialLogMs = 0;
  if (now - socialLogMs > 3000)
  {
    socialLogMs = now;
    // copie sous verrou, impression HORS section critique (jamais d'UART
    // avec les interruptions coupees)
    SocialPeer snap[SOCIAL_MAXPEERS];
    int nsnap;
    portENTER_CRITICAL(&socialMux);
    nsnap = socialNPeers;
    memcpy(snap, socialPeers, sizeof(SocialPeer) * nsnap);
    portEXIT_CRITICAL(&socialMux);
    for (int i = 0; i < nsnap; i++)
      if (now - snap[i].lastSeen < 5000)
      {
        Serial0.printf("social : \"%s\" rssi %.0f (seuil %d) vu il y a %lu ms\n",
                       snap[i].name, snap[i].rssi, (int)socialRssiNear,
                       (unsigned long)(now - snap[i].lastSeen));
        // leaderboard : fusionne les scores annonces (tache principale,
        // jamais dans le callback WiFi — la NVS reste hors section critique)
        lbMerge(snap[i].name, snap[i].scores);
      }
    static uint32_t lbSaveMs = 0;
    if (lbDirty && now - lbSaveMs > 30000)
    {
      lbSaveMs = now;
      lbSave();
      Serial0.println("social : leaderboard sauve (NVS)");
    }
  }

  // rencontre : parmi les pairs frais/proches/hors cooldown, on salue LE
  // PLUS PROCHE (meilleur RSSI), au plus une reaction toutes les 25 s — dans
  // une grappe de badges, le buddy salue calmement au lieu d'enchainer
  if (socialProbeOnly) // ecran Proximity : ecoute/emet mais ne reagit pas
    return;
  if (now < socialReactUntil)
    return;
  static uint32_t socialLastReact = 0;
  if (socialLastReact && now - socialLastReact < SOCIAL_GLOBAL_MS)
    return;
  char reactName[24] = "";
  float bestRssi = -1000;
  int best = -1;
  portENTER_CRITICAL(&socialMux);
  for (int i = 0; i < socialNPeers; i++)
  {
    SocialPeer &p = socialPeers[i];
    if (now - p.lastSeen < SOCIAL_FRESH_MS && p.rssi > socialRssiNear &&
        (p.lastReact == 0 || now - p.lastReact > SOCIAL_COOLDOWN_MS) &&
        p.rssi > bestRssi)
    {
      bestRssi = p.rssi;
      best = i;
    }
  }
  if (best >= 0)
  {
    socialPeers[best].lastReact = now;
    snprintf(reactName, sizeof(reactName), "%s",
             socialPeers[best].name[0]
                 ? socialPeers[best].name
                 : AVATARS[socialPeers[best].avatar % AVATAR_N].name);
  }
  portEXIT_CRITICAL(&socialMux);
  if (reactName[0])
  {
    socialLastReact = now;
    socialReactTrigger(reactName, now);
    socialMetSave();
    Serial0.printf("social : rencontre avec \"%s\" (rssi %.0f)\n", reactName,
                   bestRssi);
  }
}
