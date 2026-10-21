/* ============================================================
 * Streakhunter Watch — Kern-Spiellogik (Implementierung)
 * ============================================================ */
#include "streakhunter.h"
#include <string.h>

/* ---------- Level-XP-Tabelle (kumuliert). xp[L] = noetige Gesamt-XP fuer Level L ---------- */
static const uint32_t LEVEL_XP[SH_MAX_LEVEL + 1] = {
    0, 0, 40, 100, 180, 290, 420, 570, 740, 930, 1150,
    1390, 1650, 1930, 2230, 2560, 2910, 3280, 3670, 4080, 4520,
    4980, 5460, 5960, 6480, 7030, 7600, 8190, 8800, 9430, 10090
};

/* ---------- Beast-Basiswerte (ATK/DEF/SPD/HP) ---------- */
static const Stats BASE[SH_NUM_BEASTS] = {
    /* RUGA   */ { 50, 40, 42, 25 },
    /* PINTAN */ { 46, 40, 58, 25 },
    /* FEARO  */ { 46, 34, 64, 24 },
    /* LEONA  */ { 42, 50, 38, 26 },
    /* LURCO  */ { 50, 37, 57, 24 },
    /* GRELO  */ { 53, 36, 53, 25 },
};

/* Identitaets-Gewichte fuer die Punktverteilung pro Level + HP-Spanne.
 * Die Spitzen wurden leicht zur Mitte gezogen (frueher z.B. LEONA DEF-W 5,
 * GRELO ATK-W 5), damit kein Beast strukturell chancenlos ist - die Identitaet
 * bleibt (LEONA defensiv, FEARO schnell), nur weniger extrem. */
static const int   ATK_W[SH_NUM_BEASTS] = { 3, 3, 3, 3, 4, 4 };
static const int   DEF_W[SH_NUM_BEASTS] = { 3, 3, 3, 4, 3, 3 };
static const int   SPD_W[SH_NUM_BEASTS] = { 3, 4, 4, 3, 3, 3 };
static const int   HP_MIN[SH_NUM_BEASTS]= { 4, 4, 4, 4, 4, 4 };
static const int   HP_MAX[SH_NUM_BEASTS]= { 5, 5, 5, 5, 5, 5 };

/* ---------- Namen je Stufe ---------- */
static const char* NAMES[SH_NUM_BEASTS][3] = {
    { "RUGA",   "MARUGA", "JAMARUGA" },
    { "PINTAN", "PINSON", "PINTOR"   },
    { "FEARO",  "NERIMA", "MORTEA"   },
    { "LEONA",  "ZELVA",  "VELIKANA" },
    { "LURCO",  "NOCTRA", "RAVORN"   },
    { "GRELO",  "VEXAR",  "PLAGION"  },
};

/* ---------- 70 Daily Quests (finale, gesperrte Liste 24/23/23) ---------- */
static const char* Q_KOERPER[24] = {
    "10 Liegestütze","20 Kniebeugen","60 Sekunden Plank","15 Sit-ups",
    "30 Hampelmänner","5 Minuten dehnen","1 Minute Wandsitzen","10 Ausfallschritte je Bein",
    "10 Burpees","20 Wadenheben","Treppe zweimal hoch","5 Minuten spazieren",
    "10 Armkreisen je Seite","8 langsame Liegestütze","12 Sprungkniebeugen","1 Minute joggen",
    "10 Beinheben","5 Minuten Yoga","20 Sekunden Balance halten","10 Kniehebungen",
    "1 Minute tanzen","Schultern lockern","Nacken dehnen","Rücken strecken"
};
static const char* Q_GEIST[23] = {
    "5 Minuten meditieren","10 tiefe Atemzüge","Dankbarkeit notieren","5 Minuten handyfrei",
    "5 Seiten lesen","2 Minuten Stille","Tagesziel festlegen","3 Minuten Bodyscan",
    "Etwas Neues lernen","5 Minuten journaln","Dich selbst loben","In die Ferne schauen",
    "Erfolg visualisieren","Lieblingslied bewusst hören","Positiven Gedanken notieren","Augen entspannen",
    "Schöne Erinnerung notieren","1 Minute lächeln","Morgen kurz planen","Sorge loslassen",
    "Positive Gedanken machen","1 Minute Atemübungen","Tag reflektieren"
};
static const char* Q_GEWOHNHEIT[23] = {
    "Ein Glas Wasser","5 Minuten Frischluft","Bett machen","Eine Frucht essen",
    "Schreibtisch aufräumen","Zähne gründlich putzen","Tee trinken","Fünf Dinge wegräumen",
    "10 Minuten früher schlafen","Vitamine nehmen","Ohne Bildschirm essen","Freund kontaktieren",
    "Müll rausbringen","Pflanze gießen","5 Minuten lüften","Süßigkeit auslassen",
    "Wasserflasche auffüllen","5 Minuten Sonne","30 Sekunden kalt duschen","Treppe statt Aufzug",
    "Gesunden Snack wählen","Kleidung vorbereiten","Zimmer aufräumen"
};

/* ============================================================
 *  Kleine deterministische RNG (LCG) — 0..32767
 * ============================================================ */
static uint32_t sh_rand(uint32_t *st) {
    *st = (*st) * 1103515245u + 12345u;
    return (*st >> 16) & 0x7FFF;
}

/* ============================================================
 *  XP / Level / Evolution
 * ============================================================ */
int sh_level_for_xp(uint32_t xp) {
    int lvl = 1;
    for (int L = 1; L <= SH_MAX_LEVEL; L++) {
        if (xp >= LEVEL_XP[L]) lvl = L; else break;
    }
    return lvl;
}
uint32_t sh_xp_floor(int level) {
    if (level < 1) level = 1;
    if (level > SH_MAX_LEVEL) level = SH_MAX_LEVEL;
    return LEVEL_XP[level];
}
uint32_t sh_xp_into_level(uint32_t xp) {
    int L = sh_level_for_xp(xp);
    return xp - LEVEL_XP[L];
}
uint32_t sh_xp_span(int level) {
    if (level >= SH_MAX_LEVEL) return 0;       /* max -> keine weitere Stufe */
    return LEVEL_XP[level + 1] - LEVEL_XP[level];
}
int sh_stage_for_level(int level) {
    if (level < SH_EVO1_LEVEL) return 1;
    if (level < SH_EVO2_LEVEL) return 2;
    return 3;
}
XpResult sh_add_xp(SaveState *s, uint32_t amount) {
    XpResult r = { 0, 0, 0, 0 };
    int oldLevel = s->level;
    int oldStage = sh_stage_for_level(oldLevel);
    s->xp     += amount;
    s->lifeXp += amount;          /* geraeteweite Gesamt-XP (ueber alle Karten) */
    int nl = sh_level_for_xp(s->xp);
    if (nl > SH_MAX_LEVEL) nl = SH_MAX_LEVEL;
    s->level    = (uint8_t)nl;
    r.newLevel  = nl;
    r.newStage  = sh_stage_for_level(nl);
    r.leveledUp = nl - oldLevel;
    r.evolved   = (r.newStage > oldStage) ? 1 : 0;
    return r;
}

/* Schritte -> Aktivitaet in Prozent (0..100, 100% = SH_ACTIVITY_FULL). */
int sh_activity_pct(uint32_t steps) {
    if (steps >= SH_ACTIVITY_FULL) return 100;
    return (int)(steps * 100u / SH_ACTIVITY_FULL);
}
/* Aktivitaets-Prozent -> Tages-XP. 0% gibt nichts (kein Gratis-XP). */
int sh_activity_xp(int pct) {
    if (pct <= 0)  return 0;
    if (pct <= 25) return 10;
    if (pct <= 50) return 20;
    if (pct <= 75) return 30;
    return 40;
}

/* ============================================================
 *  Statwerte: deterministisch aus beastId + level reproduzierbar.
 *  Gleiches Beast waechst immer identisch -> kein gespeicherter RNG-State.
 * ============================================================ */
static void apply_evo(Stats *st, int bonus, int aw, int dw, int sw) {
    int wsum = aw + dw + sw;
    int ba = bonus * aw / wsum;
    int bd = bonus * dw / wsum;
    int bs = bonus - ba - bd;
    st->atk += ba; st->def += bd; st->spd += bs;
}
Stats sh_compute_stats(uint8_t beastId, int level) {
    if (beastId >= SH_NUM_BEASTS) beastId = 0;
    if (level < 1) level = 1;
    if (level > SH_MAX_LEVEL) level = SH_MAX_LEVEL;

    Stats st = BASE[beastId];
    int aw = ATK_W[beastId], dw = DEF_W[beastId], sw = SPD_W[beastId];
    int wsum = aw + dw + sw;
    uint32_t rng = 0x9E3779B9u ^ ((uint32_t)beastId * 2654435761u + 7u);

    for (int L = 2; L <= level; L++) {
        int pts = 4 + (int)(sh_rand(&rng) % 3);     /* 4..6 Punkte */
        for (int p = 0; p < pts; p++) {
            int r = (int)(sh_rand(&rng) % wsum);
            if (r < aw)            st.atk++;
            else if (r < aw + dw)  st.def++;
            else                   st.spd++;
        }
        st.hp += HP_MIN[beastId] + (int)(sh_rand(&rng) % (HP_MAX[beastId] - HP_MIN[beastId] + 1));
        if (L == SH_EVO1_LEVEL) apply_evo(&st, SH_EVO1_BONUS, aw, dw, sw);
        if (L == SH_EVO2_LEVEL) apply_evo(&st, SH_EVO2_BONUS, aw, dw, sw);
    }
    return st;
}

/* ============================================================
 *  Quests
 * ============================================================ */
int sh_quest_count(int category) {
    switch (category) {
        case SH_CAT_KOERPER:     return 24;
        case SH_CAT_GEIST:       return 23;
        case SH_CAT_GEWOHNHEIT:  return 23;
        default:                 return 0;
    }
}
const char* sh_quest_text(int category, int idx) {
    int n = sh_quest_count(category);
    if (idx < 0 || idx >= n) return "";
    switch (category) {
        case SH_CAT_KOERPER:    return Q_KOERPER[idx];
        case SH_CAT_GEIST:      return Q_GEIST[idx];
        case SH_CAT_GEWOHNHEIT: return Q_GEWOHNHEIT[idx];
        default:                return "";
    }
}
/* Deterministisch pro Tag: gleicher Tag -> gleiche Quests (ueber Reboots stabil) */
void sh_roll_quests(uint32_t day, uint8_t outIdx[SH_NUM_CATEGORIES]) {
    for (int c = 0; c < SH_NUM_CATEGORIES; c++) {
        uint32_t h = (day + 1u) * 2654435761u ^ ((uint32_t)(c + 1) * 40503u);
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        int n = sh_quest_count(c);
        outIdx[c] = (uint8_t)(h % (uint32_t)n);
    }
}

/* ============================================================
 *  Perfect Day Pruefung (Claim + alle 3 Quests + Step-Ziel)
 * ============================================================ */
static uint32_t check_perfect_day(SaveState *s) {
    if (s->perfectAwardedToday) return 0;
    if (!s->claimedToday) return 0;
    for (int c = 0; c < SH_NUM_CATEGORIES; c++)
        if (!s->questDone[c]) return 0;
    if (s->stepsToday < SH_STEP_GOAL) return 0;
    s->perfectAwardedToday = 1;
    sh_add_xp(s, SH_XP_PERFECT_DAY);
    return SH_XP_PERFECT_DAY;
}

/* ============================================================
 *  Setup
 * ============================================================ */
void sh_init(SaveState *s, uint32_t today) {
    memset(s, 0, sizeof(*s));
    s->version        = SH_SAVE_VERSION;
    s->onboarded      = 0;
    s->beastId        = SH_RUGA;
    s->level          = 1;
    s->xp             = 0;
    s->activeSinceDay = today;
    s->lastDay        = today;
    s->lastWeek       = today / 7;
    sh_roll_quests(today, s->questIdx);
}
void sh_choose_starter(SaveState *s, uint8_t beastId, uint32_t today) {
    if (beastId >= SH_NUM_BEASTS) beastId = SH_RUGA;
    s->beastId        = beastId;
    s->onboarded      = 1;
    s->activeSinceDay = today;
    s->lastDay        = today;
    s->lastWeek       = today / 7;
    sh_roll_quests(today, s->questIdx);
}

/* ============================================================
 *  Tagesablauf
 * ============================================================ */
uint32_t sh_claim_day(SaveState *s) {
    if (s->claimedToday) return 0;
    s->claimedToday = 1;
    s->streak++;
    if (s->streak > s->maxStreak) s->maxStreak = s->streak;

    uint32_t gained = SH_XP_CLAIM;
    sh_add_xp(s, SH_XP_CLAIM);
    if (s->streak % SH_STREAK_MILE_EVERY == 0) {
        sh_add_xp(s, SH_XP_STREAK_MILE);
        gained += SH_XP_STREAK_MILE;
    }
    gained += check_perfect_day(s);
    return gained;
}
uint32_t sh_complete_quest(SaveState *s, int category) {
    if (category < 0 || category >= SH_NUM_CATEGORIES) return 0;
    if (s->questDone[category]) return 0;
    s->questDone[category] = 1;
    s->questsCompleted++;
    s->questsThisWeek++;
    uint32_t gained = SH_XP_QUEST;
    sh_add_xp(s, SH_XP_QUEST);
    gained += check_perfect_day(s);
    return gained;
}
void sh_set_steps(SaveState *s, uint32_t steps) {
    s->stepsToday = steps;
    check_perfect_day(s);
}
int sh_process_day_change(SaveState *s, uint32_t today) {
    /* Selbstheilung gegen RUECKWAERTS-Sprung: Stand die RTC kurz auf einem falschen
       (spaeteren) Datum und wurde dann korrigiert, liegen lastDay/lastWeek in der
       Zukunft -> der normale Wechsel unten wuerde NIE greifen (today <= lastDay),
       Claim bliebe gesperrt, Kaempfe/Aktivitaet eingefroren. Anker auf heute
       zuruecksetzen und den aktuellen Tag frisch machen. */
    if (today < s->lastDay) {
        s->lastDay  = today;
        s->lastWeek = today / 7;
        if (s->activeSinceDay > today) s->activeSinceDay = today;
        s->claimedToday        = 0;
        s->perfectAwardedToday = 0;
        s->battlesToday        = 0;
        for (int c = 0; c < SH_NUM_CATEGORIES; c++) s->questDone[c] = 0;
        sh_roll_quests(today, s->questIdx);
        return 1;
    }

    if (today <= s->lastDay) return 0;            /* gleicher Tag / kein Wechsel */
    uint32_t daysPassed = today - s->lastDay;

    /* Unplausibler VORWAERTS-Sprung (Uhr war weg oder stand falsch): das ist
       kein verpasster Tag, sondern eine Zeitkorrektur. Nur neu verankern, die
       Streak bleibt. Ohne diese Bremse loescht ein einziger Uhrenaussetzer
       eine ueber Wochen aufgebaute Streak. */
    if (daysPassed > SH_MAX_PLAUSIBLE_GAP) {
        s->lastDay  = today;
        s->lastWeek = today / 7;
        if (s->activeSinceDay > today) s->activeSinceDay = today;
        s->claimedToday        = 0;
        s->perfectAwardedToday = 0;
        s->battlesToday        = 0;
        for (int c = 0; c < SH_NUM_CATEGORIES; c++) s->questDone[c] = 0;
        sh_roll_quests(today, s->questIdx);
        return 1;
    }

    /* 1) Aktivitaets-XP fuer den ABGESCHLOSSENEN Tag gutschreiben.
       0% -> 0, 1-25% -> 10, 26-50% -> 20, 51-75% -> 30, 76-100% -> 40 XP. */
    int ax = sh_activity_xp(sh_activity_pct(s->stepsToday));
    if (ax) sh_add_xp(s, (uint32_t)ax);

    /* 1b) Schritt-Historie fortschreiben (juengster Eintrag = der abgeschl. Tag).
       Uebersprungene Tage (Geraet aus) zaehlen als 0. */
    {
        uint16_t ended = s->stepsToday > 65535u ? 65535u : (uint16_t)s->stepsToday;
        for (int i = 0; i < 6; i++) s->stepHist[i] = s->stepHist[i + 1];
        s->stepHist[6] = ended;
        for (uint32_t d = 1; d < daysPassed && d < 8; d++) {
            for (int i = 0; i < 6; i++) s->stepHist[i] = s->stepHist[i + 1];
            s->stepHist[6] = 0;
        }
    }

    /* 2) Streak-Bruch: abgeschlossener Tag nicht geclaimt ODER Tage uebersprungen */
    if (!s->claimedToday || daysPassed > 1) s->streak = 0;

    /* 3) Tagesfelder zuruecksetzen */
    s->claimedToday        = 0;
    s->perfectAwardedToday = 0;
    s->stepsToday          = 0;
    s->battlesToday        = 0;
    for (int c = 0; c < SH_NUM_CATEGORIES; c++) s->questDone[c] = 0;

    /* 4) Neue Quests des aktuellen Tags ziehen */
    sh_roll_quests(today, s->questIdx);
    s->lastDay = today;

    /* 5) Wochenwechsel: Boss fuer die ABGESCHLOSSENE Woche bestimmen */
    uint32_t newWeek = today / 7;
    if (newWeek > s->lastWeek) {
        if (s->level >= SH_BOSS_MIN_LEVEL) {
            s->bossToday = (s->questsThisWeek >= SH_QUEST_THRESHOLD)
                           ? SH_BOSS_SOLVAR : SH_BOSS_LUNRAK;
            s->bossAttemptsLeft = SH_BOSS_ATTEMPTS;
        } else {
            s->bossToday = SH_BOSS_NONE;   /* Feature erst ab Lv 20 */
            s->bossAttemptsLeft = 0;
        }
        s->bossDefeatedThisWeek = 0;
        s->questsThisWeek = 0;             /* Zaehler fuer neue Woche       */
        s->lastWeek       = newWeek;
    } else {
        s->bossToday = SH_BOSS_NONE;       /* Boss nur am ersten Wochentag  */
        s->bossAttemptsLeft = 0;
    }
    return 1;
}

/* ============================================================
 *  Kampf
 * ============================================================ */
float sh_player_tier_mult(int tier) {
    if (tier <= 0) return 0.5f;   /* schwach */
    if (tier == 1) return 1.0f;   /* gut     */
    return 1.7f;                  /* perfekt */
}
float sh_enemy_tier_mult(int enemyLevel, uint32_t *rng) {
    float weak   = 0.30f - enemyLevel * 0.01f;  if (weak   < 0.05f) weak   = 0.05f;
    float strong = 0.15f + enemyLevel * 0.012f; if (strong > 0.45f) strong = 0.45f;
    float r = (float)(sh_rand(rng) % 1000) / 1000.0f;
    if (r < weak)            return 0.5f;        /* schwach */
    if (r < 1.0f - strong)   return 1.0f;        /* gut     */
    return 1.6f;                                 /* stark   */
}
int sh_damage(int atk, int def, float mult, uint32_t *rng) {
    /* Verteidigung mit abnehmendem Nutzen statt hartem Halbieren:
       base = ATK * 100 / (100 + DEF)  -> hoher Angriff zaehlt, DEF saettigt */
    float base = (float)atk * 100.0f / (100.0f + (float)def);
    float var  = 0.85f + (float)(sh_rand(rng) % 300) / 1000.0f;  /* 0.85..1.15 */
    float dmg  = base * mult * var * SH_DMG_SCALE_NUM / SH_DMG_SCALE_DEN;
    int out = (int)(dmg + 0.5f);
    return out < 1 ? 1 : out;
}
int sh_first_attacker(int spdA, int spdB) {
    return spdA >= spdB ? 1 : 0;
}
float sh_extra_hit_chance(int spdAttacker, int spdDefender) {
    if (spdAttacker <= spdDefender) return 0.0f;
    float p = (float)(spdAttacker - spdDefender) / (float)SH_SPD_DIVISOR;
    float cap = (float)SH_SPD_EXTRA_CAP_N / 100.0f;
    return p > cap ? cap : p;
}
int sh_can_battle(const SaveState *s) {
    return s->battlesToday < SH_BATTLES_PER_DAY;
}
uint8_t sh_roll_enemy(uint32_t *rng) {
    /* zufaelliges der 6 Beasts; Stufe ergibt sich aus dem Spieler-Level
       ueber sh_stage_for_level(level) -> nie Baby-Form bei hohem Level */
    return (uint8_t)(sh_rand(rng) % SH_NUM_BEASTS);
}
uint32_t sh_register_arena_win(SaveState *s) {
    if (!sh_can_battle(s)) return 0;
    s->battlesToday++;
    s->arenaWins++;
    sh_add_xp(s, SH_ARENA_WIN_XP);
    return SH_ARENA_WIN_XP;
}
void sh_register_arena_loss(SaveState *s) {
    /* verlorener Kampf verbraucht einen der 10 Tagesslots, gibt aber kein XP */
    if (sh_can_battle(s)) s->battlesToday++;
}

const char* sh_beast_name(uint8_t beastId, int stage) {
    if (beastId >= SH_NUM_BEASTS) beastId = 0;
    if (stage < 1) stage = 1;
    if (stage > 3) stage = 3;
    return NAMES[beastId][stage - 1];
}

/* ============================================================
 *  Wochen-Endgegner (Boss)
 * ============================================================ */
int sh_boss_available(const SaveState *s) { return s->bossToday; }
int sh_boss_attempts_left(const SaveState *s) { return s->bossAttemptsLeft; }

/* Werte = hochskalierter Archetyp auf Spieler-Level. Erscheint nur ab Lv20. */
Stats sh_boss_stats(int which, int level) {
    if (which == SH_BOSS_SOLVAR) {
        /* SOLVAR: ausgewogener leichter Tank (RUGA-Basis, defensiv getuned) */
        Stats b = sh_compute_stats(SH_RUGA, level);
        Stats o = { (int)(b.atk*1.10f), (int)(b.def*1.15f),
                    (int)(b.spd*1.05f), (int)(b.hp*1.15f) };
        return o;
    }
    /* LUNRAK: Glaskanone (PLAGION/GRELO-Basis, hochgeschraubt + schneller) */
    Stats b = sh_compute_stats(SH_GRELO, level);
    Stats o = { (int)(b.atk*1.10f), (int)(b.def*1.08f),
                (int)(b.spd*1.06f), (int)(b.hp*1.10f) };
    return o;
}
int sh_medal(int wins) {
    if (wins >= SH_MEDAL_GOLD)   return 3;
    if (wins >= SH_MEDAL_SILVER) return 2;
    if (wins >= SH_MEDAL_BRONZE) return 1;
    return 0;
}
uint32_t sh_register_boss_win(SaveState *s) {
    if (s->bossToday == SH_BOSS_NONE || s->bossAttemptsLeft == 0) return 0;
    if (s->bossToday == SH_BOSS_LUNRAK) s->lunrakWins++; else s->solvarWins++;
    s->bossDefeatedThisWeek = 1;
    s->bossAttemptsLeft = 0;               /* besiegt -> Woche erledigt      */
    return 1;
}
void sh_register_boss_loss(SaveState *s) {
    if (s->bossAttemptsLeft > 0) s->bossAttemptsLeft--;
    if (s->bossAttemptsLeft == 0) s->bossToday = SH_BOSS_NONE;  /* verschwindet */
}
const char* sh_boss_name(int which) {
    if (which == SH_BOSS_LUNRAK) return "LUNRAK";
    if (which == SH_BOSS_SOLVAR) return "SOLVAR";
    return "";
}
