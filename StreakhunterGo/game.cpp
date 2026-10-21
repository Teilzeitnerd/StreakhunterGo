/* ============================================================
 *  game.cpp — Spielsteuerung
 * ============================================================ */
#include "game.h"
#include "vitals.h"
#include "equip.h"
#include "items.h"
#include "adventure.h"
#include "moves.h"
#include "loadout.h"
#include "sh_platform.h"
#include "sh_sd.h"      /* Beast-Karte (Etappe 3) */
#include "HWCDC.h"
#include <Arduino.h>   /* fuer millis() */
#include <esp_random.h>
#include <string.h>

extern HWCDC USBSerial;

SaveState g_state;
static uint32_t s_lastDay = 0xFFFFFFFF;   /* erzwingt ersten Tagescheck */
static uint32_t s_rng     = 0x1234abcd;   /* Zufall fuer Gegnerwahl       */
static int      s_dayLevelUp    = 0;      /* Tageswechsel hat Level-Up ausgeloest */static int      s_streakSaved   = 0;      /* Streak-Schild hat die Streak gerettet */

static void gate_load(void);              /* Beute-Sperre, Definition weiter unten */
static int      s_dayLevelUpEvo = 0;      /* dabei auch entwickelt?              */

/* --- Uebergreifender Level-Up/Evolutions-Event ---------------------------
 * Jede XP-Quelle (Quest, Claim, Abenteuer, Kampf, Tageswechsel) meldet einen
 * Level-Aufstieg hierher. Die UI pollt das im Haupt-Loop und zeigt dann - egal
 * wo man gerade ist - ein Level-Up-Popup, ggf. gefolgt von einem
 * Evolutions-Popup. So gehen Aufstiege nicht mehr "still" unter. */
static int      s_luPending  = 0;    /* es steht ein ungezeigter Level-Up an   */
static int      s_luLevel    = 0;    /* das erreichte Level                    */
static int      s_luEvolved  = 0;    /* wurde dabei eine Evolution ueberschritten? */
static int      s_luStage    = 1;    /* Stufe nach der Evolution (1..3)        */

/* Eine XpResult verbuchen - merkt sich den hoechsten erreichten Level-Up und
 * ob eine Evolution dabei war. Mehrere Aufstiege vor dem naechsten Poll werden
 * zum juengsten zusammengefasst (das ist der, der zaehlt). */
static void note_levelup(XpResult r) {
    if (r.leveledUp > 0) {
        s_luPending = 1;
        s_luLevel   = r.newLevel;
        if (r.evolved) { s_luEvolved = 1; s_luStage = r.newStage; }
    }
}

/* Von der UI im Loop gepollt. Liefert 1, wenn ein ungezeigter Level-Up ansteht,
 * und fuellt level/evolved/stage. Setzt den Event zurueck (nur einmal). */
int game_take_levelup(int *level, int *evolved, int *stage) {
    if (!s_luPending) return 0;
    if (level)   *level   = s_luLevel;
    if (evolved) *evolved = s_luEvolved;
    if (stage)   *stage   = s_luStage;
    s_luPending = 0;
    s_luEvolved = 0;
    s_luStage   = 1;
    return 1;
}


/* --- Beast-Quelle: Karte oder gar kein Beast (nur noch Karten-Beasts) ----- */
enum { BEAST_NONE = 0, BEAST_CARD = 1 };

/* --- TAMA: Die Aktivitaets-Historie (Schritte -> Tages-%) ist ersatzlos
 *  entfallen. Der Tamagotchi hat keinen Schrittzaehler. Die neue XP-Quelle
 *  kommt in Schritt 6 (Pflege-Mechaniken). --- */
static uint8_t  s_beastSource  = BEAST_NONE;
static uint32_t s_cardBirthDay = 0;       /* "Geburtstag" der aktiven Karte    */

/* Karte -> aktives Beast (in g_state). */
static void beast_from_card(const ShBeastCard *c) {
    g_state.beastId = (uint8_t)c->species;
    g_state.level   = (uint8_t)c->level;
    g_state.xp      = c->xp;
    s_cardBirthDay  = c->birthDay;
}
/* aktives Beast (g_state) -> Karte. */
static void beast_to_card(ShBeastCard *c) {
    memset(c, 0, sizeof(*c));
    c->species  = g_state.beastId;
    c->stage    = (uint16_t)sh_stage_for_level(g_state.level);
    c->level    = g_state.level;
    c->xp       = g_state.xp;
    c->birthDay = s_cardBirthDay ? s_cardBirthDay : g_state.activeSinceDay;
    const char *nm = sh_beast_name(g_state.beastId, sh_stage_for_level(g_state.level));
    strncpy(c->name, nm, SHB_NAME_LEN - 1);
}

/* Ohne aktives Karten-Beast gibt es keine XP/Level-Ups: Level/XP nach einem
 * XP-vergebenden Kern-Aufruf wieder auf den vorherigen Stand einfrieren. Die
 * Geraete-Zaehler (Streak, Claims, Quests, Schritte) bleiben unberuehrt. */
static inline void freeze_beast_if_none(uint8_t lv, uint32_t xp, uint32_t life) {
    if (s_beastSource != BEAST_CARD) { g_state.level = lv; g_state.xp = xp; g_state.lifeXp = life; }
}

/* Speichern: Geraet immer ins NVS. Beast nur, wenn eine Karte aktiv ist
 * (dann auf die Karte). Ohne Karte gibt es kein Beast zu sichern. */
static void save(void) {
    shp_save_device(&g_state);
    if (s_beastSource == BEAST_CARD) {
        ShBeastCard c;
        beast_to_card(&c);
        shsd_write_beast(&c);
        shsd_end();                 /* Karte danach wieder freigeben */
    }
}

/* Ist gerade ein Karten-Beast aktiv? (UI: Beast/Arena zeigen oder nicht) */
bool game_has_beast(void) { return s_beastSource == BEAST_CARD; }

/* Nach Werksreset: beast-los starten (nicht die Karte ueberschreiben). */
void game_force_no_beast(void) { s_beastSource = BEAST_NONE; }

/* Spezies + Geburtstag identifizieren ein Beast eindeutig - auch zwei Karten
 * mit derselben Spezies bleiben unterscheidbar. */
bool game_beast_uid(uint16_t *species, uint32_t *birthDay) {
    if (s_beastSource != BEAST_CARD) return false;
    if (species)  *species  = (uint16_t)g_state.beastId;
    if (birthDay) *birthDay = s_cardBirthDay ? s_cardBirthDay : g_state.activeSinceDay;
    return true;
}

/* Hot-Swap: Karte zur Laufzeit pruefen und die Beast-Quelle umschalten.
 * Rueckgabe: 0 = keine Aenderung, 1 = Karte eingesteckt, 2 = Karte entfernt. */
int game_poll_card(void) {
    ShBeastCard c;
    bool present = (shsd_probe(&c) == 1);     /* nur "Karte mit gueltigem Beast" zaehlt */

    if (present && s_beastSource != BEAST_CARD) {
        /* Karte rein: das alte Beast wird gesichert, das neue aufgetaut. Die
         * Reihenfolge ist wichtig - beast_from_card() setzt die Kennung, an der
         * vit_bind() und adv_bind() ihren Speicherplatz erkennen. */
        beast_from_card(&c);
        s_beastSource = BEAST_CARD;
        vit_bind();
        adv_bind();
        loadout_bind();
        return 1;
    }

    if (!present && s_beastSource == BEAST_CARD) {
        /* Karte raus: alles einfrieren, SOLANGE die Kennung noch gilt. */
        vit_freeze();
        adv_freeze();
        loadout_freeze();
        s_beastSource = BEAST_NONE;
        return 2;
    }
    return 0;
}

/* ============================================================
 *  Kalender-Historie (Ampel pro Tag), separater NVS-Blob.
 *  Status: 0=rot (nichts/zu wenig), 1=gelb (geclaimt + >=1 Quest),
 *          2=gruen (geclaimt + alle Quests). Ringpuffer ueber die
 *          letzten Tage -> deckt die Monatsansicht ab.
 * ============================================================ */
#define CAL_DAYS 45
typedef struct {
    uint32_t day[CAL_DAYS];   /* Tag-Index (0xFFFFFFFF = leer) */
    uint8_t  stat[CAL_DAYS];  /* 0/1/2                          */
    uint16_t head;            /* naechste Schreibposition       */
} CalHist;
static CalHist s_cal;

static uint8_t cal_status(int claimed, int questsDone) {
    if (claimed && questsDone >= SH_NUM_CATEGORIES) return 2;   /* gruen */
    if (claimed && questsDone >= 1)                 return 1;   /* gelb  */
    return 0;                                                    /* rot   */
}
static void cal_load(void) {
    if (!shp_load_blob("calhist", &s_cal, sizeof(s_cal))) {
        for (int i = 0; i < CAL_DAYS; i++) { s_cal.day[i] = 0xFFFFFFFF; s_cal.stat[i] = 0; }
        s_cal.head = 0;
    }
}
static void cal_record(uint32_t dayIdx, uint8_t st) {
    for (int i = 0; i < CAL_DAYS; i++)          /* vorhandenen Tag aktualisieren */
        if (s_cal.day[i] == dayIdx) { s_cal.stat[i] = st; shp_save_blob("calhist", &s_cal, sizeof(s_cal)); return; }
    s_cal.day[s_cal.head]  = dayIdx;            /* sonst neu in den Ring         */
    s_cal.stat[s_cal.head] = st;
    s_cal.head = (uint16_t)((s_cal.head + 1) % CAL_DAYS);
    shp_save_blob("calhist", &s_cal, sizeof(s_cal));
}
/* Ampelstatus eines vergangenen Tages: 0/1/2, oder -1 wenn unbekannt. */
int game_cal_status(uint32_t dayIdx) {
    for (int i = 0; i < CAL_DAYS; i++)
        if (s_cal.day[i] == dayIdx) return (int)s_cal.stat[i];
    return -1;
}
/* Live-Status des heutigen Tages aus dem aktuellen Spielstand. */
int game_cal_today_status(void) {
    int q = 0;
    for (int c = 0; c < SH_NUM_CATEGORIES; c++) q += g_state.questDone[c];
    return (int)cal_status(g_state.claimedToday, q);
}

/* --- Status-Effekt einer Kampfseite (Burn/Poison ticken, Buffs modifizieren) --- */
typedef struct {
    uint8_t burnRounds;    /* >0: brennt noch so viele Runden           */
    uint8_t poisonRounds;  /* >0: vergiftet                             */
    /* Buffs als Prozent-Aufschlag auf die Grundwerte (100 = neutral).
     * "battle" = bis Kampfende, wird ueber buffRounds=255 markiert. */
    uint8_t atkPct, defPct, spdPct;
    uint8_t atkR, defR, spdR;   /* Restrunden je Buff (255 = ganzer Kampf) */
} FxState;

/* --- Zustand des laufenden, interaktiven Arena-Kampfes --- */
typedef struct {
    uint8_t enemyId;
    int     lvl;
    Stats   me, foe;
    int     myHp, foeHp;
    float   foeMult;   /* gemerkte Gegner-Intensitaet fuer den aktuellen Zug */

    int     mySlot1, mySlot2;     /* Move-IDs des Spielers  */
    int     foeSlot1, foeSlot2;   /* Move-IDs des Gegners   */
    FxState myFx, foeFx;          /* aktive Effekte je Seite */

    int     myStam, foeStam;      /* Stamina 0..10                          */
    int     mySigCd, foeSigCd;    /* Restzuege, die die Signature sperrt     */
    bool    foeSupportLast;       /* letzter Gegnerzug war Buff/Heal (Anti-Spam) */
} ArenaState;
static ArenaState s_ar;
static int s_bossMode = 0;   /* 0 = normaler Gegner, sonst SH_BOSS_LUNRAK/SOLVAR */
static int s_lastCrit = 0;   /* war der letzte Treffer ein Volltreffer? (fuer die UI) */
static int s_lastStatus = 0; /* 0=nichts, 1=Brand gesetzt, 2=Gift gesetzt (fuer die UI) */

/* Eigener 0..1-Zufall fuer die Extra-Treffer-Wuerfe (nutzt denselben Seed). */
static float arena_rand01(void) {
    s_rng = s_rng * 1103515245u + 12345u;
    return (float)((s_rng >> 16) & 0x7fffu) / 32768.0f;
}

bool game_begin(void) {
    gate_load();      /* Beute-Sperre: Tageszaehler des GERAETS laden */
    if (!shp_load(&g_state)) {
        sh_init(&g_state, shp_day_index());   /* frischer Spielstand */
        save();
    }
    /* Selbstheilung: ein alter Spielstand (vor der RTC) hatte activeSinceDay
       in anderen Einheiten -> unsinnige "Aktiv seit"-Werte. Korrigieren. */
    uint32_t today = shp_day_index();
    if (g_state.activeSinceDay == 0 || g_state.activeSinceDay > today) {
        g_state.activeSinceDay = today;
        save();
    }
    s_lastDay = g_state.lastDay;
    cal_load();                         /* Kalender-Historie laden */

    /* --- Beast nur noch von der Karte ---
     * Liegt eine gueltige Karte an, wird ihr Beast aktiv; sonst ist die Uhr
     * beast-los (Habit-Tracking laeuft weiter, aber ohne Beast/XP/Arena). */
    s_beastSource = BEAST_NONE;
    if (g_state.onboarded) {
        ShBeastCard c;
        if (shsd_read_beast(&c)) {
            beast_from_card(&c);
            s_beastSource = BEAST_CARD;
            USBSerial.printf("[BEAST] Karte aktiv: Spezies=%u Lv=%u XP=%lu\n",
                             (unsigned)g_state.beastId, (unsigned)g_state.level,
                             (unsigned long)g_state.xp);
        } else if (shsd_begin() && g_state.beastId < SH_NUM_BEASTS && g_state.level > 0) {
            /* Karte steckt, ist aber nicht lesbar (Datei fehlt/korrupt, z.B. weil
               der Akku beim Schreiben leer wurde) UND im Spielstand liegt noch ein
               gueltiger Beast -> daraus wiederherstellen und die Karte neu schreiben. */
            s_beastSource = BEAST_CARD;
            ShBeastCard heal;
            beast_to_card(&heal);
            shsd_write_beast(&heal);
            USBSerial.printf("[BEAST] Karte war korrupt -> aus Spielstand wiederhergestellt + geheilt: Spezies=%u Lv=%u\n",
                             (unsigned)g_state.beastId, (unsigned)g_state.level);
        } else {
            USBSerial.println("[BEAST] keine Karte -> beast-los");
        }
        shsd_end();
    }
    return g_state.onboarded != 0;
}

/* Level-Up durch Tageswechsel (Schritt-XP) abholen. Liefert einmalig 1,
 * setzt *evolved auf 1, wenn dabei eine Entwicklung passierte. */
int game_take_daychange_levelup(int *evolved) {
    int r = s_dayLevelUp;
    if (evolved) *evolved = s_dayLevelUpEvo;
    s_dayLevelUp = 0;
    return r;
}

void game_tick(void) {
    /* OHNE laufende Echtzeituhr gibt es keinen verlaesslichen Tagesbegriff.
     * shp_day_index() faellt dann auf millis() zurueck und liefert nach jedem
     * Neustart 0 - der Tagesanker wuerde auf 0 gesetzt, und beim naechsten
     * Start mit funktionierender Uhr saehe es aus wie tausende verpasste Tage.
     * Genau so geht eine Streak verloren, ohne dass jemand etwas versaeumt hat.
     * Deshalb: keine Uhr, kein Tageswechsel. */
    if (!shp_rtc_ok()) return;

    uint32_t today = shp_day_index();
    bool urgent    = false;   /* sofort speichern (Tageswechsel) */

    /* Tages-/Wochenwechsel */
    if (today != s_lastDay) {
        /* Ampelstatus des ABGESCHLOSSENEN Tages festhalten (vor dem Reset). */
        {
            int q = 0;
            for (int c = 0; c < SH_NUM_CATEGORIES; c++) q += g_state.questDone[c];
            cal_record(g_state.lastDay, cal_status(g_state.claimedToday, q));
        }
        int lvlBefore   = g_state.level;
        uint32_t xpBefore = g_state.xp;
        uint32_t lifeBefore = g_state.lifeXp;
        int stageBefore = sh_stage_for_level(g_state.level);
        uint32_t streakBefore = g_state.streak;

        sh_process_day_change(&g_state, today);
        freeze_beast_if_none((uint8_t)lvlBefore, xpBefore, lifeBefore);   /* ohne Karte keine XP */

        /* --- Streak-Schild ---
         * Es wirkt PASSIV. Wer einen Tag vergisst, verliert die Streak - es sei
         * denn, ein Schild liegt im Rucksack. Dann faengt es den Bruch ab und
         * zerbricht dabei. Man kann es nicht "benutzen", nur besitzen; genau
         * darin liegt der Wert: es ist eine Versicherung, kein Werkzeug. */
        if (streakBefore > 0 && g_state.streak == 0 && inv_count(IT_SCHILD) > 0) {
            inv_use(IT_SCHILD);
            g_state.streak = streakBefore;      /* gerettet, nicht erhoeht */
            s_streakSaved  = 1;
            USBSerial.printf("[ITEM] Streak-Schild zerbricht - Streak %lu gerettet.\n",
                             (unsigned long)streakBefore);
        }
        if (g_state.level > lvlBefore) {
            s_dayLevelUp    = 1;
            s_dayLevelUpEvo = (sh_stage_for_level(g_state.level) > stageBefore) ? 1 : 0;
            /* Auch beim Tageswechsel das uebergreifende Popup ausloesen. */
            XpResult r;
            r.leveledUp = g_state.level - lvlBefore;
            r.newLevel  = g_state.level;
            r.newStage  = sh_stage_for_level(g_state.level);
            r.evolved   = s_dayLevelUpEvo;
            note_levelup(r);
        }
        s_lastDay = today;
        urgent = true;
    }
    /* TAMA: kein Schritt-Sync mehr (kein Schrittzaehler). stepsToday bleibt 0,
     * damit vergibt sh_process_day_change auch keine Aktivitaets-XP. */
    if (urgent) save();
}

void game_choose_starter(uint8_t beastId) {
    sh_choose_starter(&g_state, beastId, shp_day_index());
    save();
}

/* Onboarding abschliessen ohne residenten Beast (Beasts kommen nur von Karten).
 * Setzt onboarded=1 und frischt die Tagesfelder; das Beast bleibt vestigial. */
void game_finish_onboarding(void) {
    sh_choose_starter(&g_state, g_state.beastId, shp_day_index());
    save();
}

/* Loot, den der letzte Claim / die letzte Quest gebracht hat. Die UI holt es
 * sich ab, um es in der Erfolgsmeldung zu nennen (-1 = nichts). */
static int s_claimLoot = -1;
static int s_questLoot = -1;
int game_claim_loot(void) { int v = s_claimLoot; s_claimLoot = -1; return v; }
int game_quest_loot(void) { int v = s_questLoot; s_questLoot = -1; return v; }

void game_claim(void) {
    uint8_t lv = g_state.level; uint32_t xp = g_state.xp, life = g_state.lifeXp;
    int levelBefore = g_state.level;
    int stageBefore = sh_stage_for_level(g_state.level);
    if (sh_claim_day(&g_state) > 0) {
        freeze_beast_if_none(lv, xp, life);        /* ohne Karte keine XP */
        vit_add_bond(BOND_CLAIM);                  /* du kuemmerst dich */

        /* Level-Up/Evolution uebergreifend melden. */
        if (g_state.level > levelBefore) {
            XpResult r;
            r.leveledUp = g_state.level - levelBefore;
            r.newLevel  = g_state.level;
            r.newStage  = sh_stage_for_level(g_state.level);
            r.evolved   = (r.newStage > stageBefore) ? 1 : 0;
            note_levelup(r);
        }

        /* Claim-Belohnung: ein zufaelliges Equip oder Item in den Rucksack.
         * Gleichverteilt ueber die acht Nicht-Futter-Gegenstaende. */
        static const int pool[] = { IT_AMULETT, IT_PANZER, IT_FEDERN, IT_RING,
                                    IT_SCHILD, IT_ROLLE, IT_FEDER, IT_ELIXIER };
        int pick = pool[esp_random() % (sizeof(pool) / sizeof(pool[0]))];
        if (inv_add(pick, 1) > 0) s_claimLoot = pick;

        shp_vibrate(40); save();
    }
}

/* XP von aussen zufuehren (Abenteuer-Entdeckung). Geht durch dieselbe Tuer
 * wie Claim und Quest - also auch durch die Karten-Sperre. */
/* Hat das Schild beim letzten Tageswechsel die Streak gerettet? Die UI holt
 * sich das einmal ab und quittiert es damit. */
bool game_streak_saved(void) {
    if (!s_streakSaved) return false;
    s_streakSaved = 0;
    return true;
}

void game_add_xp(uint32_t amount) {
    uint8_t lv = g_state.level; uint32_t xp = g_state.xp, life = g_state.lifeXp;
    XpResult r = sh_add_xp(&g_state, amount);
    note_levelup(r);                           /* uebergreifendes Popup ausloesen */
    freeze_beast_if_none(lv, xp, life);        /* ohne Karte keine XP */
    save();
}

void game_quest_done(int category) {
    uint8_t lv = g_state.level; uint32_t xp = g_state.xp, life = g_state.lifeXp;
    int levelBefore = g_state.level;
    int stageBefore = sh_stage_for_level(g_state.level);
    if (sh_complete_quest(&g_state, category) > 0) {
        freeze_beast_if_none(lv, xp, life);        /* ohne Karte keine XP */
        vit_add_bond(BOND_QUEST);

        /* Level-Up/Evolution, die dabei entstand, uebergreifend melden. */
        if (g_state.level > levelBefore) {
            XpResult r;
            r.leveledUp = g_state.level - levelBefore;
            r.newLevel  = g_state.level;
            r.newStage  = sh_stage_for_level(g_state.level);
            r.evolved   = (r.newStage > stageBefore) ? 1 : 0;
            note_levelup(r);
        }

        /* Quest-Belohnung: gewichtetes Futter.
         * 50 % Beere, 25 % Fleisch, 12,5 % Kraftnuss, 5 % Festmahl.
         * Rest (7,5 %) = kein Drop. Skala 0..999 (Promille). */
        uint32_t r = esp_random() % 1000;
        int pick = -1;
        if      (r < 500) pick = IT_BEERE;      /* 50,0 % */
        else if (r < 750) pick = IT_FLEISCH;    /* 25,0 % */
        else if (r < 875) pick = IT_KRAFTNUSS;  /* 12,5 % */
        else if (r < 925) pick = IT_FESTMAHL;   /*  5,0 % */
        if (pick >= 0 && inv_add(pick, 1) > 0) s_questLoot = pick;

        shp_vibrate(30); save();
    }
}


/* ===== Interaktive Arena ====================================================
 * Die UI ruft begin() einmal pro Kampf, dann pro Tipp player_strike(),
 * danach enemy_strike(), und am Ende finish(win). So kann die UI jeden
 * Schlag einzeln animieren. */
/* Kaempfe gehoeren zum BEAST (in g_vit), nicht zum Geraet: ein frisches Beast
 * bringt frische Kraft mit. Gegen das Farmen steht die Beute-Sperre unten. */
int game_arena_left(void) {
    uint32_t today = shp_day_index();
    if (g_vit.fightsDay != today) {          /* neuer Tag -> Zaehler zurueck */
        g_vit.fightsDay    = today;
        g_vit.fightsToday  = 0;
    }
    int l = SH_BATTLES_PER_DAY - (int)g_vit.fightsToday;
    return l < 0 ? 0 : l;
}

/* Kampflaenge-Regler: skaliert NUR die HP beider Seiten gleich hoch.
 * Aendert nicht, wer gewinnt (Schaden bleibt gleich) - nur die Dauer.
 * 100 = wie Rohwerte, 280 = ~2.8x laengere Kaempfe. */
/* HP-Pool NUR im interaktiven Arena-Kampf strecken. Aendert KEINE Balance:
 * Beast-Stats, Schadensskala und Tier-Multiplikatoren bleiben unberuehrt,
 * jeder HP-Wert skaliert gleich -> alle Verhaeltnisse identisch, nur mehr
 * Runden (kein One-Shot mehr). Einzige Stellschraube fuer die Kampflaenge. */
/* --- Kampf-Schaden: Level-Skalierung im Pokemon-Stil ---
 * Der feste Move-Schaden (power) waechst mit dem Level, damit Attacken auf
 * hohem Level ordentlich reinhauen und die Kampfdauer nicht ausartet.
 * REF = Level, auf das geeicht wird (dort aendert sich nichts).
 * BLEND = wie stark der Level durchschlaegt (0.5 = Treffer steigen sanft von
 *   ~3 auf ~5 ueber 30 Level; 1.0 = konstante Dauer). */
#define ARENA_LVL_REF            4
#define ARENA_DMG_LEVEL_BLEND    0.5f
#define ARENA_DEF_POWER          0.72f  /* DEF-Einfluss (< 1 = Tanks weniger stark) */
#define ARENA_TYPE_MULT          1.25f  /* Schaden bei Typvorteil                  */
#define ARENA_DMG_SPREAD         8      /* Schadensstreuung in % (+/-)             */
/* Volltreffer: SPD-abhaengige Chance auf x-fachen Schaden. */
#define ARENA_CRIT_BASE          0.08f  /* Grundchance bei Referenz-SPD            */
#define ARENA_CRIT_SPD_REF       50     /* SPD, ab der der Bonus zaehlt            */
#define ARENA_CRIT_PER_SPD       0.0045f/* + Chance je SPD-Punkt darueber          */
#define ARENA_CRIT_MIN           0.02f  /* Untergrenze (auch lahme Beasts)         */
#define ARENA_CRIT_CAP           0.45f  /* Obergrenze                              */
#define ARENA_CRIT_MULT          2.0f   /* Volltreffer-Schaden                     */
#define ARENA_AI_BLUNDER_PCT     12     /* Chance, dass die Gegner-KI suboptimal spielt */
/* Der Spieler kaempft mit Vitalwert-Bonus (bis x1,10 auf ATK/DEF/SPD) UND
 * Ausruestung (+3..+12). Der Gegner bekam bisher rohe Basiswerte - daher war er
 * ab mittleren Leveln chancenlos. Dieser Faktor gibt ihm einen vergleichbaren
 * "auch gut trainiert"-Bonus, damit Kaempfe wieder auf Augenhoehe stattfinden. */
#define ARENA_FOE_TRAINED        1.12f

/* ============================================================
 *  Move-basierter Kampf
 * ============================================================ */

/* Effektive Werte einer Seite = Grundwert x aktive Buffs (in %). */
static int fx_atk(const Stats *base, const FxState *fx) {
    return base->atk * (fx->atkR ? fx->atkPct : 100) / 100;
}
static int fx_def(const Stats *base, const FxState *fx) {
    return base->def * (fx->defR ? fx->defPct : 100) / 100;
}
static int fx_spd(const Stats *base, const FxState *fx) {
    return base->spd * (fx->spdR ? fx->spdPct : 100) / 100;
}

/* Buff setzen: pct und Dauer (255 = ganzer Kampf). */
static void fx_set_buff(FxState *fx, int effect, int pct, int rounds) {
    uint8_t r = (rounds <= 0) ? 255 : (uint8_t)rounds;
    switch (effect) {
        case EF_BUFF_ATK: fx->atkPct = pct; fx->atkR = r; break;
        case EF_BUFF_DEF: fx->defPct = pct; fx->defR = r; break;
        case EF_BUFF_SPD: fx->spdPct = pct; fx->spdR = r; break;
    }
}

/* Runden-Tick fuer EINE Seite: DoT abziehen, Buff-Dauer runterzaehlen.
 * Liefert den DoT-Schaden (fuer die UI-Anzeige). maxHp fuer die %-Rechnung. */
static int fx_tick(FxState *fx, int *hp, int maxHp) {
    int dot = 0;
    if (fx->burnRounds)   { dot += maxHp * 5 / 100; fx->burnRounds--; }   /* -5%/R */
    if (fx->poisonRounds) { dot += maxHp * 6 / 100; fx->poisonRounds--; } /* -6%/R */
    if (dot > 0) { *hp -= dot; if (*hp < 0) *hp = 0; }
    if (fx->atkR && fx->atkR != 255) fx->atkR--;
    if (fx->defR && fx->defR != 255) fx->defR--;
    if (fx->spdR && fx->spdR != 255) fx->spdR--;
    return dot;
}

/* Einen Move anwenden. attacker/defender = Stats+Fx+HP+Typ.
 * Liefert den zugefuegten Schaden (0 bei reinem Buff/Heal).
 * healOut: falls der Move den Angreifer heilt, steht die Menge hier. */
static int apply_move(int moveId,
                      const Stats *atkBase, FxState *atkFx, int *atkHp, int atkMaxHp,
                      MoveType atkType,
                      const Stats *defBase, FxState *defFx, int *defHp,
                      MoveType defType,
                      int *healOut) {
    const Move *mv = move_get(moveId);
    *healOut = 0;

    /* Reiner Buff / Heal (kein Schaden). */
    if (mv->power == 0) {
        if (mv->effect == EF_HEAL) {
            int h = atkMaxHp * mv->magPct / 100;
            *atkHp += h; if (*atkHp > atkMaxHp) *atkHp = atkMaxHp;
            *healOut = h;
        } else {
            fx_set_buff(atkFx, mv->effect, mv->magPct, mv->rounds);
        }
        return 0;
    }

    /* Schaden aus FESTEM Grundwert, moduliert durch das ATK/DEF-Verhaeltnis.
     *   Faktor = atk / (atk + def) * 2
     * Bei gleichen Werten = 1.0 (voller power). Mehr ATK -> mehr, mehr DEF ->
     * weniger. So bleibt der Wert nah am festen Grundschaden, aber Pflege
     * (Vitalwerte -> Stats) und Buffs wirken sich spuerbar aus. */
    int atk = fx_atk(atkBase, atkFx);
    int def = fx_def(defBase, defFx);

    /* DEF-Ignore: ein Teil der Verteidigung zaehlt nicht. */
    if (mv->effect == EF_DEF_IGNORE) def = def * (100 - mv->magPct) / 100;
    if (def < 1) def = 1;

    /* DEF-Einfluss gedaempft (ARENA_DEF_POWER < 1). Reine Tank-Beasts mit sehr
     * hoher DEF (z.B. Zelva) waren sonst fast uneinnehmbar, wodurch manche
     * Matchups praktisch entschieden waren. Der Daempfer holt sie naeher an
     * den Rest heran, ohne DEF wirkungslos zu machen. */
    float defEff = (float)def * ARENA_DEF_POWER;
    if (defEff < 1.0f) defEff = 1.0f;

    float factor = (float)atk / ((float)atk + defEff) * 2.0f;
    float dmgf   = (float)mv->power * factor;

    /* Level-Skalierung im Pokemon-Stil: der Schaden waechst mit dem Level,
     * unabhaengig vom ATK/DEF-Verhaeltnis (das sich bei gleich-leveligen
     * Gegnern ausgleicht). Ohne das wuerden Kaempfe auf hohem Level ewig
     * dauern, weil nur die HP absolut steigt.
     *
     * lvlf   = Pokemon-Levelterm (2*Level/5 + 2)
     * BLEND  = wie stark der Level durchschlaegt (1.0 = voll konstante Dauer,
     *          0.5 = Treffer steigen dezent von ~3 auf ~5 ueber 30 Level).
     * Geeicht auf Level 4 (ARENA_LVL_REF), damit sich Low-Level nicht aendert. */
    {
        float lvlf    = 2.0f * s_ar.lvl / 5.0f + 2.0f;
        float lvlfRef = 2.0f * ARENA_LVL_REF / 5.0f + 2.0f;
        float eff     = lvlfRef + (lvlf - lvlfRef) * ARENA_DMG_LEVEL_BLEND;
        dmgf *= eff / lvlfRef;   /* bei Level == REF exakt 1.0 */
    }

    if (type_advantage(atkType, defType)) dmgf *= ARENA_TYPE_MULT;  /* super-effektiv */

    /* --- Volltreffer ---
     * Mit einer SPD-abhaengigen Chance landet ein Volltreffer (x ARENA_CRIT_MULT).
     * Schnelle Beasts treffen oefter voll - so bekommen die flinken Glaskanonen
     * echte Durchschlagskraft gegen langsame Verteidigungsbollwerke, ohne die
     * Grundbalance zu verwuerfeln. */
    s_lastCrit = 0;
    {
        int spd = fx_spd(atkBase, atkFx);
        float chance = ARENA_CRIT_BASE + (spd - ARENA_CRIT_SPD_REF) * ARENA_CRIT_PER_SPD;
        if (chance < ARENA_CRIT_MIN) chance = ARENA_CRIT_MIN;
        if (chance > ARENA_CRIT_CAP) chance = ARENA_CRIT_CAP;
        if (arena_rand01() < chance) {
            dmgf *= ARENA_CRIT_MULT;
            s_lastCrit = 1;
        }
    }

    /* Leichte Schadensstreuung (+/- ARENA_DMG_SPREAD %), damit Kaempfe nicht
     * voellig vorhersehbar sind - mal sitzt ein Treffer besser, mal schlechter.
     * Bewusst klein gehalten, damit es Wuerze gibt, aber nicht das Ergebnis
     * verwuerfelt. */
    {
        int span = ARENA_DMG_SPREAD * 2 + 1;          /* z.B. -8..+8 = 17 Werte */
        int off  = (int)(arena_rand01() * span) - ARENA_DMG_SPREAD;
        dmgf *= (1.0f + off / 100.0f);
    }

    int dmg = (int)(dmgf + 0.5f);
    if (dmg < 1) dmg = 1;
    *defHp -= dmg; if (*defHp < 0) *defHp = 0;

    /* Sekundaereffekt am Ziel. */
    s_lastStatus = 0;
    switch (mv->effect) {
        case EF_BURN:   if (*defHp > 0) { defFx->burnRounds   = mv->rounds ? mv->rounds : 2; s_lastStatus = 1; } break;
        case EF_POISON: if (*defHp > 0) { defFx->poisonRounds = mv->rounds ? mv->rounds : 3; s_lastStatus = 2; } break;
        case EF_HEAL: {   /* Angriff MIT Heilung (z.B. Leerenpuls, Sonnenstrahl) */
            int h = atkMaxHp * mv->magPct / 100;
            *atkHp += h; if (*atkHp > atkMaxHp) *atkHp = atkMaxHp;
            *healOut = h;
        } break;
        case EF_BUFF_ATK: case EF_BUFF_DEF: case EF_BUFF_SPD:
            fx_set_buff(atkFx, mv->effect, mv->magPct, mv->rounds);
            break;
        default: break;
    }
    return dmg;
}

bool game_arena_begin(void) {
    s_bossMode = 0;              /* normaler Arena-Gegner */
    /* Kaempfe sind pro Beast unbegrenzt (fuer Level und Bindung) - nur die
     * BEUTE ist geraeteweit auf SH_BATTLES_PER_DAY begrenzt (siehe gate_*).
     * Deshalb hier KEINE sh_can_battle-Sperre mehr: die wuerde den Start
     * blockieren, obwohl der Button (game_arena_left) noch gruenes Licht gibt -
     * genau der Widerspruch, der den Startknopf tot wirken liess. */
    s_ar.enemyId = sh_roll_enemy(&s_rng);
    s_ar.lvl     = g_state.level;
    /* Die Vitalwerte greifen HIER - sonst waere die ganze Pflege folgenlos.
     * Satt/ausgeruht/verbunden = bis +10%, verwahrlost = bis -20%. */
    s_ar.me      = equip_apply(vit_effective_stats(sh_compute_stats(g_state.beastId, s_ar.lvl)));
    s_ar.foe     = sh_compute_stats(s_ar.enemyId, s_ar.lvl);
    /* Gegner-Ausgleich: der Spieler bringt Pflege- und Ausruestungsboni mit,
     * der Gegner sonst nur rohe Werte. */
    s_ar.foe.atk = (int)(s_ar.foe.atk * ARENA_FOE_TRAINED);
    s_ar.foe.def = (int)(s_ar.foe.def * ARENA_FOE_TRAINED);
    s_ar.foe.spd = (int)(s_ar.foe.spd * ARENA_FOE_TRAINED);
    s_ar.foe.hp  = (int)(s_ar.foe.hp  * ARENA_FOE_TRAINED);
    /* Kampf-HP werden NORMIERT statt aus den Roh-HP skaliert. Mit festen
     * Schadenswerten (Basic 8 ... Signature 16) wuerden linear steigende Roh-HP
     * die Kaempfe auf hohem Level endlos machen. Ein level-unabhaengiger Rahmen
     * haelt jeden Kampf bei ~5-6 Runden - egal ob Level 5 oder 30. */
    /* --- Kampf-HP ---
     * Echte HP aus sh_compute_stats. Die Kampfdauer wird ueber die
     * Level-Skalierung des Schadens (ARENA_DMG_LEVEL_BLEND) geregelt, nicht
     * ueber die HP. */
    s_ar.myHp    = s_ar.me.hp;
    s_ar.foeHp   = s_ar.foe.hp;

    /* Loadouts setzen. Spieler: seine zwei Slots. Gegner: gleiche
     * Freischalt-Voraussetzungen, Slot 2 zufaellig (kein Spiegelbild). */
    s_ar.mySlot1 = loadout_slot(1);
    s_ar.mySlot2 = loadout_slot(2);
    loadout_for_enemy(s_ar.enemyId, &s_rng, &s_ar.foeSlot1, &s_ar.foeSlot2);

    memset(&s_ar.myFx,  0, sizeof(FxState));
    memset(&s_ar.foeFx, 0, sizeof(FxState));

    /* Stamina: beide starten mit 6 von 10, keine Signature-Sperre. */
    s_ar.myStam  = STAMINA_START;
    s_ar.foeStam = STAMINA_START;
    s_ar.mySigCd = 0;
    s_ar.foeSigCd = 0;
    s_ar.foeSupportLast = false;
    return true;
}

/* 1 = Gegner ist STRIKT schneller und hat damit den Erstschlag.
 * Bei Gleichstand beginnt der Spieler (sh_first_attacker gibt dann A=Spieler). */
int game_arena_enemy_first(void) {
    return sh_first_attacker(s_ar.me.spd, s_ar.foe.spd) == 0 ? 1 : 0;
}

/* Spieler trifft (tier 0=schwach,1=gut,2=perfekt). Inkl. evtl. Extra-Treffer
 * bei SPD-Vorteil. Liefert den insgesamt zugefuegten Schaden. */
int game_arena_player_strike(int tier) {
    float m = sh_player_tier_mult(tier);
    int d = sh_damage(s_ar.me.atk, s_ar.foe.def, m, &s_rng);
    s_ar.foeHp -= d;
    if (s_ar.foeHp > 0 && arena_rand01() < sh_extra_hit_chance(s_ar.me.spd, s_ar.foe.spd)) {
        int d2 = sh_damage(s_ar.me.atk, s_ar.foe.def, m, &s_rng);
        s_ar.foeHp -= d2;
        d += d2;
    }
    if (s_ar.foeHp < 0) s_ar.foeHp = 0;
    return d;
}

/* Gegner kontert (zufaellige Staerke abhaengig vom Level). Inkl. Extra-Treffer. */
int game_arena_enemy_strike(void) {
    float em = sh_enemy_tier_mult(s_ar.lvl, &s_rng);
    int d = sh_damage(s_ar.foe.atk, s_ar.me.def, em, &s_rng);
    s_ar.myHp -= d;
    if (s_ar.myHp > 0 && arena_rand01() < sh_extra_hit_chance(s_ar.foe.spd, s_ar.me.spd)) {
        int d2 = sh_damage(s_ar.foe.atk, s_ar.me.def, em, &s_rng);
        s_ar.myHp -= d2;
        d += d2;
    }
    if (s_ar.myHp < 0) s_ar.myHp = 0;
    return d;
}

/* ===== Einzelschlag-API: die UI steuert Timing, Doppel-Hits und Animationen.
 * Jeder Aufruf wendet GENAU EINEN Treffer an und liefert dessen Schaden. ===== */

/* Ein Spieler-Treffer (tier 0/1/2). */
int game_arena_player_hit(int tier) {
    float m = sh_player_tier_mult(tier);
    int d = sh_damage(s_ar.me.atk, s_ar.foe.def, m, &s_rng);
    s_ar.foeHp -= d;
    if (s_ar.foeHp < 0) s_ar.foeHp = 0;
    return d;
}
/* Bekommt der Spieler einen Extra-Treffer (nur bei SPD-Vorteil)? */
bool game_arena_player_extra(void) {
    return arena_rand01() < sh_extra_hit_chance(s_ar.me.spd, s_ar.foe.spd);
}
/* Gegner-Zug einleiten: Intensitaet wuerfeln + merken. Liefert tier (0/1/2). */
int game_arena_enemy_roll(void) {
    float em = sh_enemy_tier_mult(s_ar.lvl, &s_rng);
    s_ar.foeMult = em;
    return (em >= 1.5f) ? 2 : (em >= 1.0f) ? 1 : 0;
}
/* Ein Gegner-Treffer mit der gemerkten Intensitaet. */
int game_arena_enemy_hit(void) {
    int d = sh_damage(s_ar.foe.atk, s_ar.me.def, s_ar.foeMult, &s_rng);
    s_ar.myHp -= d;
    if (s_ar.myHp < 0) s_ar.myHp = 0;
    return d;
}
/* Bekommt der Gegner einen Extra-Treffer? */
bool game_arena_enemy_extra(void) {
    return arena_rand01() < sh_extra_hit_chance(s_ar.foe.spd, s_ar.me.spd);
}

/* ============================================================
 *  Arena-Beute
 *  ------------------------------------------------------------
 *  NUR bei Sieg, und meistens gibt es nichts. Die Arena ist der Ort, an dem
 *  Items herkommen - aber sie darf keine Maschine sein, sonst lohnt sich das
 *  Abenteuer nicht mehr. Wertvolles bleibt selten:
 *
 *      45 %  nichts
 *      30 %  Futter (Beere oder Fleisch)
 *      12 %  Kraftnuss
 *       8 %  Ausruestung
 *       5 %  ein echtes Item (Schild oder Rolle)
 *
 *  Phoenixfeder und Elixier gibt es hier NICHT - die sind zu stark. Sie
 *  bleiben dem Abenteuer vorbehalten.
 * ============================================================ */
static int s_lastLoot = -1;

/* ============================================================
 *  Beute-Sperre — der Riegel gegen das Farmen
 *  ------------------------------------------------------------
 *  Die Kaempfe zaehlen PRO BEAST: sechs Karten waeren 60 Kaempfe am Tag. Das
 *  ist fuer Bindung und Level auch in Ordnung - es ist echte Arbeit, und jedes
 *  Beast verdient sich sein eigenes Wachstum.
 *
 *  Aber BEUTE gibt es nur fuer die ersten SH_BATTLES_PER_DAY Kaempfe des Tages,
 *  geraeteweit gezaehlt. Wer danach weiterkaempft, levelt und bindet - farmt
 *  aber keine Items mehr. Genau die Bremse, die es braucht.
 * ============================================================ */
typedef struct __attribute__((packed)) {
    uint32_t day;
    uint16_t fights;      /* Kaempfe HEUTE auf diesem Geraet, ueber alle Beasts */
} LootGate;

static LootGate s_gate = { 0, 0 };

static void gate_load(void) {
    if (!shp_load_blob("lootgate", &s_gate, sizeof(s_gate))) { s_gate.day = 0; s_gate.fights = 0; }
}
static void gate_save(void) { shp_save_blob("lootgate", &s_gate, sizeof(s_gate)); }

/* Zaehlt den Kampf und sagt, ob es dafuer noch Beute gibt. */
static bool gate_allows_loot(void) {
    uint32_t today = shp_day_index();
    if (s_gate.day != today) { s_gate.day = today; s_gate.fights = 0; }

    s_gate.fights++;
    gate_save();

    bool ok = (s_gate.fights <= SH_BATTLES_PER_DAY);
    if (!ok)
        USBSerial.printf("[ARENA] Kampf %u heute - keine Beute mehr (Grenze %d).\n",
                         (unsigned)s_gate.fights, SH_BATTLES_PER_DAY);
    return ok;
}

int game_loot_fights_today(void) {
    if (s_gate.day != shp_day_index()) return 0;
    return (int)s_gate.fights;
}

int game_arena_loot(void) {
    int l = s_lastLoot;
    s_lastLoot = -1;      /* nur einmal abholbar */
    return l;
}

static void roll_loot(void) {
    int r = (int)(esp_random() % 100);

    /* Zielverteilung: 50 % nichts, 35 % Futter, 15 % Equip/Item.
     * Futter (35): Beere/Fleisch haeufig, Kraftnuss seltener.
     * Equip/Item (15): 10 % Equipment, 5 % Verbrauchs-Item. */
    if      (r < 50) s_lastLoot = -1;                                    /* 50 % nichts    */
    else if (r < 73) s_lastLoot = (esp_random() % 2) ? IT_BEERE : IT_FLEISCH; /* 23 % B/F  */
    else if (r < 85) s_lastLoot = IT_KRAFTNUSS;                         /* 12 % Kraftnuss */
    else if (r < 95) s_lastLoot = IT_AMULETT + (int)(esp_random() % 4); /* 10 % Equipment */
    else             s_lastLoot = (esp_random() % 2) ? IT_SCHILD : IT_ROLLE; /*  5 % Item */

    if (s_lastLoot >= 0) {
        inv_add(s_lastLoot, 1);
        USBSerial.printf("[ARENA] Beute: %s\n", ITEMS[s_lastLoot].name);
    }
}

void game_arena_finish(bool win) {
    /* Der Kampf zaehlt fuer DIESES Beast ... */
    uint32_t today = shp_day_index();
    if (g_vit.fightsDay != today) { g_vit.fightsDay = today; g_vit.fightsToday = 0; }
    if (g_vit.fightsToday < 255) g_vit.fightsToday++;

    /* ... und einmal geraeteweit, fuer die Beute-Sperre. */
    bool lootAllowed = gate_allows_loot();

    if (win) {
        int levelBefore = g_state.level;
        int stageBefore = sh_stage_for_level(g_state.level);
        sh_register_arena_win(&g_state);
        vit_add_bond(BOND_ARENA_WIN);       /* Bindung gibt es IMMER - das ist Zuwendung */
        if (s_bossMode) {
            /* Boss besiegt: garantiert ein Equipment oder Item, dazu Bonus-XP.
             * Das ist der seltenste Moment im Spiel und darf sich lohnen. */
            static const int pool[] = { IT_AMULETT, IT_PANZER, IT_FEDERN, IT_RING,
                                        IT_SCHILD, IT_ROLLE, IT_FEDER, IT_ELIXIER };
            s_lastLoot = pool[esp_random() % (sizeof(pool) / sizeof(pool[0]))];
            inv_add(s_lastLoot, 1);
            sh_add_xp(&g_state, BOSS_WIN_XP);
        }
        else if (lootAllowed) roll_loot();  /* Beute nur innerhalb der Tagesgrenze */
        else                  s_lastLoot = -1;
        /* Level-Up/Evolution aus dem Kampf uebergreifend melden - das Popup
         * erscheint nach dem Ergebnis-Screen, genau wie ueberall sonst. */
        if (g_state.level > levelBefore) {
            XpResult r;
            r.leveledUp = g_state.level - levelBefore;
            r.newLevel  = g_state.level;
            r.newStage  = sh_stage_for_level(g_state.level);
            r.evolved   = (r.newStage > stageBefore) ? 1 : 0;
            note_levelup(r);
        }
        shp_vibrate(40);
    } else {
        sh_register_arena_loss(&g_state);
        vit_add_bond(-BOND_ARENA_LOSS);     /* eine Niederlage nagt an der Bindung */
        s_lastLoot = -1;
    }
    equip_after_fight();      /* Ausruestung nutzt sich ab - auch bei Niederlage */
    vit_flush();              /* fightsToday sichern */
    save();
}

int     game_arena_foe_hp(void)      { return s_ar.foeHp; }
int     game_arena_foe_max(void)     { return s_ar.foe.hp; }
int     game_arena_my_hp(void)       { return s_ar.myHp; }
int     game_arena_my_max(void)      { return s_ar.me.hp; }
uint8_t game_arena_enemy_id(void)    { return s_ar.enemyId; }

/* --- Boss-Kampf ---
 * Laeuft komplett ueber die normale Arena (Moves, Stamina, Volltreffer,
 * Animationen) - nur Werte, Name und Sprite kommen vom Boss. Der enemyId wird
 * bewusst auf ein typgleiches Beast gesetzt: so stimmen Typvorteil und die
 * Zugauswahl automatisch, ohne Sonderweg im Kampfcode.
 *   LUNRAK = Schatten (wie FEARO),  SOLVAR = Licht (wie RUGA). */
bool game_arena_begin_boss(int which) {
    if (which != SH_BOSS_LUNRAK && which != SH_BOSS_SOLVAR) return false;
    if (!game_arena_begin()) return false;         /* Grundaufbau wiederverwenden */
    s_bossMode   = which;
    s_ar.enemyId = (which == SH_BOSS_LUNRAK) ? SH_FEARO : SH_RUGA;
    s_ar.foe     = sh_boss_stats(which, s_ar.lvl);
    s_ar.foeHp   = s_ar.foe.hp;
    loadout_for_enemy(s_ar.enemyId, &s_rng, &s_ar.foeSlot1, &s_ar.foeSlot2);
    return true;
}

/* 0 = kein Boss, sonst SH_BOSS_LUNRAK / SH_BOSS_SOLVAR. */
int game_arena_boss(void) { return s_bossMode; }

int     game_arena_enemy_stage(void) { return sh_stage_for_level(s_ar.lvl); }
int     game_arena_level(void)       { return s_ar.lvl; }
const char* game_arena_enemy_name(void) {
    if (s_bossMode) return sh_boss_name(s_bossMode);
    return sh_beast_name(s_ar.enemyId, sh_stage_for_level(s_ar.lvl));
}

/* ===== Move-Kampf-API fuer die UI ===== */

/* Welche Moves hat wer? (Move-IDs; -> move_get(id)->name fuer die Buttons.) */
int game_move_player(int slot) { return slot == 1 ? s_ar.mySlot1 : s_ar.mySlot2; }
int game_move_enemy(int slot)  { return slot == 1 ? s_ar.foeSlot1 : s_ar.foeSlot2; }

/* Stamina-Staende fuer die UI-Anzeige. */
int game_arena_my_stamina(void)  { return s_ar.myStam; }
int game_arena_foe_stamina(void) { return s_ar.foeStam; }

/* Kann der Spieler diesen Slot gerade spielen?
 * Genug Stamina UND (falls Signature) kein Cooldown. */
/* Slot 2 ist gesperrt, solange nichts ausser dem Basis-Angriff freigeschaltet
 * ist (dann steht dort derselbe Move wie in Slot 1). Erst ab dem ersten
 * freigeschalteten Special (Level 5) wird er nutzbar. */
bool game_slot2_locked(void) {
    return s_ar.mySlot2 == s_ar.mySlot1;
}

bool game_move_playable(int slot) {
    int id = (slot == 1) ? s_ar.mySlot1 : s_ar.mySlot2;

    if (slot == 2 && game_slot2_locked()) return false;

    const Move *m = move_get(id);
    if (s_ar.myStam < move_stamina_cost(id)) return false;
    if (m->cat == CAT_SIGNATURE && s_ar.mySigCd > 0) return false;
    return true;
}

/* Nach einem eigenen Zug: Signature-Cooldown eine Stufe abbauen.
 * (Wird am Ende JEDES eigenen Zuges aufgerufen, nicht pro Runde.) */
static void stam_after_own_turn(int *sigCd) {
    if (*sigCd > 0) (*sigCd)--;
}

/* Spieler nutzt einen seiner Slots (1 oder 2). Liefert den Schaden;
 * *heal = wie viel sich der Spieler dabei geheilt hat.
 * Stamina wurde per game_move_playable vorab geprueft. */
int game_arena_player_move(int slot, int *heal) {
    int id = (slot == 1) ? s_ar.mySlot1 : s_ar.mySlot2;
    const Move *m = move_get(id);

    /* Cooldown zuerst abbauen (dieser Zug zaehlt fuer eine evtl. laufende Sperre). */
    stam_after_own_turn(&s_ar.mySigCd);

    /* Kosten abziehen. */
    s_ar.myStam -= move_stamina_cost(id);
    if (s_ar.myStam < 0) s_ar.myStam = 0;

    int dmg = apply_move(id,
                         &s_ar.me,  &s_ar.myFx,  &s_ar.myHp,  s_ar.me.hp,
                         beast_type(g_state.beastId),
                         &s_ar.foe, &s_ar.foeFx, &s_ar.foeHp,
                         beast_type(s_ar.enemyId), heal);

    /* Basic trifft -> Stamina zurueck. */
    if (m->cat == CAT_BASIC && dmg > 0) {
        s_ar.myStam += STAMINA_BASIC_GAIN;
        if (s_ar.myStam > STAMINA_MAX) s_ar.myStam = STAMINA_MAX;
    }
    /* Signature -> Cooldown setzen. */
    if (m->cat == CAT_SIGNATURE) s_ar.mySigCd = SIGNATURE_COOLDOWN;
    return dmg;
}

/* Gegner waehlt einen Zug (KI, die Stamina und Cooldown respektiert). */
int game_arena_enemy_move(int *heal, int *usedSlot) {
    int s1 = s_ar.foeSlot1, s2 = s_ar.foeSlot2;
    const Move *m1 = move_get(s1);
    const Move *m2 = move_get(s2);

    /* Welche Slots sind bezahlbar und nicht im Cooldown? */
    bool can1 = s_ar.foeStam >= move_stamina_cost(s1) &&
                !(m1->cat == CAT_SIGNATURE && s_ar.foeSigCd > 0);
    bool can2 = s_ar.foeStam >= move_stamina_cost(s2) &&
                !(m2->cat == CAT_SIGNATURE && s_ar.foeSigCd > 0);

    /* Der Basic ist immer bezahlbar (Kosten 0) - Rueckfall. */
    int basicSlot = (m1->cat == CAT_BASIC) ? 1 : (m2->cat == CAT_BASIC) ? 2 : 1;

    /* Ist ein Slot ein Nicht-Angriff (reiner Buff/Heal)? Der darf NUR unter
     * strengen Bedingungen gespielt werden, sonst spammt die KI ihn (genau der
     * Bug mit dem endlosen Heal-Gegner). */
    bool s2_support = (m2->power == 0);
    bool s1_support = (m1->power == 0);   /* Slot 1 ist eigentlich immer Angriff */

    int hpPct = s_ar.foe.hp > 0 ? s_ar.foeHp * 100 / s_ar.foe.hp : 0;

    /* Darf der Gegner einen Support-Move (Buff/Heal) sinnvoll spielen?
     *  - Heal: nur wenn HP unter 55 % (sonst verschwendet)
     *  - Buff: nur wenn der Buff nicht schon aktiv ist
     * Und generell hoechstens gelegentlich, nie zwei Runden am Stueck. */
    bool supportUseful = false;
    if (s2_support && can2 && !s_ar.foeSupportLast) {
        const Move *ms = m2;
        if (ms->effect == EF_HEAL) {
            supportUseful = (hpPct < 55);
        } else {  /* Buff */
            bool active = (ms->effect == EF_BUFF_ATK && s_ar.foeFx.atkR) ||
                          (ms->effect == EF_BUFF_DEF && s_ar.foeFx.defR) ||
                          (ms->effect == EF_BUFF_SPD && s_ar.foeFx.spdR);
            supportUseful = !active;
        }
    }

    int choice;

    /* --- KI-Patzer ---
     * Mit einer Chance spielt der Gegner NICHT optimal, sondern faellt auf den
     * simplen Basic-Angriff zurueck. Das macht Kaempfe weniger berechenbar und
     * gibt dem Spieler auch bei Statnachteil eine echte Chance - ein Gegner,
     * der immer perfekt spielt, macht jedes Matchup zur reinen Rechenaufgabe. */
    s_rng = s_rng * 1103515245u + 12345u;
    bool blunder = ((s_rng >> 16) % 100) < ARENA_AI_BLUNDER_PCT;

    if (blunder) {
        choice = basicSlot;             /* Patzer: nur der Basis-Angriff */
        s_ar.foeSupportLast = false;
    } else if (supportUseful) {
        choice = 2;                     /* sinnvoller Support-Zug */
    } else {
        /* Sonst ein ANGRIFF. Bevorzugt der staerkere bezahlbare Angriffsslot. */
        bool atk1ok = can1 && !s1_support;
        bool atk2ok = can2 && !s2_support;
        if (atk1ok && atk2ok) {
            /* beide Angriffe moeglich: meist den Hauptangriff, ab und zu den
             * zweiten - fuer etwas Abwechslung. */
            s_rng = s_rng * 1103515245u + 12345u;
            choice = (((s_rng >> 16) % 100) < 35) ? 2 : 1;
        } else if (atk1ok) {
            choice = 1;
        } else if (atk2ok) {
            choice = 2;
        } else {
            choice = basicSlot;         /* nichts anderes bezahlbar -> Basic */
        }
    }

    int id = (choice == 1) ? s1 : s2;
    const Move *m = move_get(id);

    /* Merken, ob dieser Zug ein Support war - verhindert zwei am Stueck. */
    s_ar.foeSupportLast = (m->power == 0);

    stam_after_own_turn(&s_ar.foeSigCd);
    s_ar.foeStam -= move_stamina_cost(id);
    if (s_ar.foeStam < 0) s_ar.foeStam = 0;

    *usedSlot = choice;
    int dmg = apply_move(id,
                         &s_ar.foe, &s_ar.foeFx, &s_ar.foeHp, s_ar.foe.hp,
                         beast_type(s_ar.enemyId),
                         &s_ar.me,  &s_ar.myFx,  &s_ar.myHp,
                         beast_type(g_state.beastId), heal);

    if (m->cat == CAT_BASIC && dmg > 0) {
        s_ar.foeStam += STAMINA_BASIC_GAIN;
        if (s_ar.foeStam > STAMINA_MAX) s_ar.foeStam = STAMINA_MAX;
    }
    if (m->cat == CAT_SIGNATURE) s_ar.foeSigCd = SIGNATURE_COOLDOWN;
    return dmg;
}

/* Rundenbeginn: Status ticken (Burn/Poison), Buff-Dauer runter, Stamina regen.
 * Liefert den DoT-Schaden je Seite, damit die UI ihn anzeigen kann. */
void game_arena_tick(int *playerDot, int *enemyDot) {
    *playerDot = fx_tick(&s_ar.myFx,  &s_ar.myHp,  s_ar.me.hp);
    *enemyDot  = fx_tick(&s_ar.foeFx, &s_ar.foeHp, s_ar.foe.hp);

    /* +2 Stamina pro voller Runde, gedeckelt bei 10. */
    s_ar.myStam  += STAMINA_REGEN;  if (s_ar.myStam  > STAMINA_MAX) s_ar.myStam  = STAMINA_MAX;
    s_ar.foeStam += STAMINA_REGEN;  if (s_ar.foeStam > STAMINA_MAX) s_ar.foeStam = STAMINA_MAX;
}

/* Wer eroeffnet? Hoechste effektive SPD (buff-modifiziert). */
int game_arena_faster(void) {
    int mySpd  = fx_spd(&s_ar.me,  &s_ar.myFx);
    int foeSpd = fx_spd(&s_ar.foe, &s_ar.foeFx);
    return (foeSpd > mySpd) ? 1 : 0;   /* 1 = Gegner zuerst */
}

/* Hat der Spieler gegen den Gegner Typ-Vorteil? (Fuer einen UI-Hinweis.) */
bool game_arena_type_edge(void) {
    return type_advantage(beast_type(g_state.beastId), beast_type(s_ar.enemyId));
}

/* War der zuletzt berechnete Treffer ein Volltreffer? Die UI holt es sich direkt
 * nach game_arena_player_move / _enemy_move ab. */
bool game_arena_was_crit(void) { return s_lastCrit != 0; }

/* Hat der letzte Move einen Status gesetzt? 0=nein, 1=Brand, 2=Gift. */
int game_arena_last_status(void) { return s_lastStatus; }

/* Laufen beim jeweiligen Beast gerade Status-Effekte? Fuer Dauer-Symbole. */
bool game_arena_foe_burning(void)  { return s_ar.foeFx.burnRounds   > 0; }
bool game_arena_foe_poisoned(void) { return s_ar.foeFx.poisonRounds > 0; }
bool game_arena_my_burning(void)   { return s_ar.myFx.burnRounds    > 0; }
bool game_arena_my_poisoned(void)  { return s_ar.myFx.poisonRounds  > 0; }


/* --- Abfragen --- */
Stats game_player_stats(void) { return sh_compute_stats(g_state.beastId, g_state.level); }
int   game_xp_into_level(void) { return (int)sh_xp_into_level(g_state.xp); }
int   game_xp_span(void)       { return (int)sh_xp_span(g_state.level); }
int   game_player_stage(void)  { return sh_stage_for_level(g_state.level); }
const char* game_player_name(void) {
    return sh_beast_name(g_state.beastId, sh_stage_for_level(g_state.level));
}
