/* ============================================================
 * Streakhunter Watch — Kern-Spiellogik (board-unabhaengig, portables C)
 * ------------------------------------------------------------
 * Keine Arduino-/LVGL-Abhaengigkeiten. Reines C99, testbar mit gcc.
 * Wird spaeter 1:1 in die Firmware uebernommen; die UI/Firmware ruft
 * nur diese Funktionen auf und liest/schreibt SaveState.
 * ============================================================ */
#ifndef STREAKHUNTER_H
#define STREAKHUNTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ---------- Konstanten / Design-Werte (an einer Stelle justierbar) ---------- */
#define SH_SAVE_VERSION      4
#define SH_MAX_LEVEL         30
#define SH_NUM_BEASTS        6
#define SH_NUM_CATEGORIES    3      /* Koerper / Geist / Gewohnheit            */

#define SH_EVO1_LEVEL        10     /* Stufe 1 -> 2                            */
#define SH_EVO2_LEVEL        20     /* Stufe 2 -> 3                            */
#define SH_EVO1_BONUS        15     /* einmaliger Statbonus bei Evo 1          */
#define SH_EVO2_BONUS        20     /* einmaliger Statbonus bei Evo 2          */

/* XP-Quellen */
#define SH_XP_CLAIM          50
#define SH_XP_QUEST          20
#define SH_XP_PERFECT_DAY    50
#define SH_XP_STREAK_MILE    100
/* Groesster Abstand, der noch als echte Abwesenheit gilt. Alles darueber ist
 * ein Uhrenfehler und bricht die Streak nicht. */
#define SH_MAX_PLAUSIBLE_GAP 60

#define SH_STREAK_MILE_EVERY 7
#define SH_STEPS_PER_CHUNK   2000   /* (alt, ungenutzt seit Aktivitaets-Modell) */
#define SH_XP_PER_CHUNK      10     /* (alt, ungenutzt)                        */
#define SH_STEP_CAP          40000  /* (alt, ungenutzt)                        */
#define SH_STEP_GOAL         5600   /* Ziel fuer "Perfect Day" (= 80% Aktiv.)  */
#define SH_ACTIVITY_FULL     7000   /* Schritte = 100% Aktivitaet              */

/* Arena */
#define SH_ARENA_WIN_XP      5
#define SH_BATTLES_PER_DAY   10     /* 10 Kämpfe/Tag -> max 50 XP, Counter sinnvoll */
#define SH_DMG_SCALE_NUM     38     /* Schadensskala 0.38 als 38/100          */
#define SH_DMG_SCALE_DEN     100
#define SH_SPD_DIVISOR       220    /* Extra-Treffer-Chance = dSPD / Divisor   */
#define SH_SPD_EXTRA_CAP_N   45     /* Obergrenze 0.45 als 45/100              */

/* Wochen-Endgegner (Boss) */
#define SH_BOSS_MIN_LEVEL    20     /* Feature erst ab voll entwickeltem Beast */
#define SH_QUEST_THRESHOLD   12     /* >=12 Wochen-Quests -> SOLVAR, sonst LUNRAK */
#define SH_BOSS_ATTEMPTS     5      /* Versuche am Boss-Tag (Montag)           */
#define SH_MEDAL_BRONZE      1      /* ab 1 Sieg                               */
#define SH_MEDAL_SILVER      3      /* ab 3 Siegen                             */
#define SH_MEDAL_GOLD        5      /* ab 5 Siegen                             */
enum { SH_BOSS_NONE = 0, SH_BOSS_LUNRAK = 1, SH_BOSS_SOLVAR = 2 };

/* Kategorien */
enum { SH_CAT_KOERPER = 0, SH_CAT_GEIST = 1, SH_CAT_GEWOHNHEIT = 2 };

/* Beast-IDs (Reihenfolge wie Starter-Auswahl) */
enum { SH_RUGA=0, SH_PINTAN=1, SH_FEARO=2, SH_LEONA=3, SH_LURCO=4, SH_GRELO=5 };

/* ---------- Statblock ---------- */
typedef struct {
    int atk;
    int def;
    int spd;
    int hp;
} Stats;

/* ---------- Persistenter Speicherzustand (geht in den Flash) ---------- */
typedef struct {
    uint8_t  version;             /* SH_SAVE_VERSION                         */
    uint8_t  onboarded;           /* 0/1                                     */
    uint8_t  beastId;             /* 0..5                                    */
    uint8_t  level;               /* 1..30                                   */
    uint32_t xp;                  /* Gesamt-XP (kumuliert)                   */

    uint16_t streak;              /* aktuelle Streak                         */
    uint16_t maxStreak;           /* laengste Streak                         */
    uint32_t activeSinceDay;      /* Tag-Index des Starts ("aktiv seit")     */
    uint32_t lastDay;             /* zuletzt verarbeiteter Tag-Index         */

    uint8_t  claimedToday;        /* 0/1                                     */
    uint8_t  questDone[SH_NUM_CATEGORIES];  /* je Kategorie erledigt 0/1     */
    uint8_t  questIdx[SH_NUM_CATEGORIES];   /* gezogener Quest-Index heute   */
    uint8_t  perfectAwardedToday; /* Perfect-Day-Bonus heute schon vergeben  */

    uint32_t stepsToday;          /* Schritte heute                          */
    uint32_t lifeXp;              /* Lifetime-XP gesamt (geraeteweit, alle Karten) */
    uint16_t questsCompleted;     /* Lifetime erledigte Quests               */
    uint8_t  battlesToday;        /* heute genutzte Kaempfe                  */
    uint16_t arenaWins;           /* Lifetime Arena-Siege                    */

    /* --- Wochen-Endgegner --- */
    uint16_t questsThisWeek;      /* Daily Quests in dieser Woche            */
    uint32_t lastWeek;            /* zuletzt verarbeitete Wocheneinheit      */
    uint16_t lunrakWins;          /* LUNRAK besiegt (gesamt)                 */
    uint16_t solvarWins;          /* SOLVAR besiegt (gesamt)                 */
    uint8_t  bossToday;           /* heute verfuegbarer Boss (0/1/2)         */
    uint8_t  bossAttemptsLeft;    /* verbleibende Versuche heute             */
    uint8_t  bossDefeatedThisWeek;/* diese Woche bereits besiegt?            */

    uint16_t stepHist[7];         /* Schritte der letzten 7 abgeschl. Tage    */
} SaveState;

/* ---------- Ereignisse (Rueckgabe von add_xp, fuer UI-Feiermomente) ---------- */
typedef struct {
    int leveledUp;     /* Anzahl Level-Aufstiege in diesem add_xp            */
    int newLevel;      /* Level danach                                       */
    int evolved;       /* 1, wenn dabei eine Evolutionsstufe ueberschritten  */
    int newStage;      /* Stufe danach (1..3)                                */
} XpResult;

/* ============================================================
 *  API
 * ============================================================ */

/* Speicher initialisieren (Werkszustand, vor Onboarding) */
void sh_init(SaveState *s, uint32_t today);

/* Onboarding abschliessen: Starter waehlen */
void sh_choose_starter(SaveState *s, uint8_t beastId, uint32_t today);

/* --- XP / Level / Evolution --- */
int       sh_level_for_xp(uint32_t xp);          /* Level aus Gesamt-XP      */
uint32_t  sh_xp_floor(int level);                /* XP-Schwelle des Levels   */
uint32_t  sh_xp_into_level(uint32_t xp);         /* XP innerhalb des Levels  */
uint32_t  sh_xp_span(int level);                 /* XP-Breite des Levels     */
int       sh_stage_for_level(int level);         /* 1 / 2 / 3                */
XpResult  sh_add_xp(SaveState *s, uint32_t amount);

/* --- Aktivitaet (Schritte -> Prozent -> XP) --- */
int       sh_activity_pct(uint32_t steps);       /* 0..100 (100% = 10.000)   */
int       sh_activity_xp(int pct);               /* 0/10/20/30/40 XP pro Tag */

/* --- Statwerte (deterministisch aus beastId+level reproduzierbar) --- */
Stats     sh_compute_stats(uint8_t beastId, int level);

/* --- Tagesablauf --- */
/* Beim Boot und bei jedem Datums-Tick aufrufen. Erkennt Tageswechsel,
 * schreibt Step-XP gut, bricht ggf. die Streak, zieht neue Quests.
 * Rueckgabe: 1 falls ein Tageswechsel verarbeitet wurde.            */
int  sh_process_day_change(SaveState *s, uint32_t today);

/* Claim the Day: Streak-Treiber. Gibt vergebene XP zurueck (0 wenn schon). */
uint32_t sh_claim_day(SaveState *s);

/* Eine Daily Quest abhaken. Gibt vergebene XP zurueck (0 wenn schon/ungueltig). */
uint32_t sh_complete_quest(SaveState *s, int category);

/* Schritte fuer heute setzen (vom IMU/Pedometer). Reine Buchhaltung;
 * die XP-Gutschrift passiert beim Tageswechsel.                      */
void sh_set_steps(SaveState *s, uint32_t steps);

/* --- Quests --- */
void        sh_roll_quests(uint32_t day, uint8_t outIdx[SH_NUM_CATEGORIES]);
const char* sh_quest_text(int category, int idx);
int         sh_quest_count(int category);

/* --- Kampf --- */
/* Spieler-Trefferstufe aus der Timing-Leiste: Multiplikator. */
float sh_player_tier_mult(int tier);     /* 0=schwach,1=gut,2=perfekt        */
/* Gegner-Trefferstufe abhaengig vom Level (hoeher = besser). rng: 0..32767  */
float sh_enemy_tier_mult(int enemyLevel, uint32_t *rng);
/* Schaden berechnen. */
int   sh_damage(int atk, int def, float mult, uint32_t *rng);
/* Schnelligkeit: wer schlaegt zuerst? (1 = A) */
int   sh_first_attacker(int spdA, int spdB);
/* Chance auf einen sofortigen Extra-Treffer des Angreifers (0..0.45). */
float sh_extra_hit_chance(int spdAttacker, int spdDefender);
/* Einen gewonnenen Kampf verbuchen (XP + Zaehler). Gibt XP zurueck.         */
uint32_t sh_register_arena_win(SaveState *s);
void     sh_register_arena_loss(SaveState *s);   /* Slot verbrauchen, kein XP */
/* Kampf erlaubt? (Slots heute frei)                                         */
int   sh_can_battle(const SaveState *s);
/* Zufaelligen Gegner ziehen (rotiert). Gegner erscheint auf SPIELER-Level
 * und in der dazu passenden Entwicklungsstufe (sh_stage_for_level).          */
uint8_t sh_roll_enemy(uint32_t *rng);

/* Hilfsname fuer Beast in gegebener Stufe (1..3) */
const char* sh_beast_name(uint8_t beastId, int stage);

/* --- Wochen-Endgegner --- */
int         sh_boss_available(const SaveState *s);   /* 0 keiner / 1 LUNRAK / 2 SOLVAR */
int         sh_boss_attempts_left(const SaveState *s);
Stats       sh_boss_stats(int which, int level);     /* hochskalierter Archetyp        */
uint32_t    sh_register_boss_win(SaveState *s);      /* 1 wenn verbucht                */
void        sh_register_boss_loss(SaveState *s);     /* Versuch verbraucht             */
int         sh_medal(int wins);                      /* 0 keine /1 Bronze /2 Silber /3 Gold */
const char* sh_boss_name(int which);

#ifdef __cplusplus
}
#endif

#endif /* STREAKHUNTER_H */
