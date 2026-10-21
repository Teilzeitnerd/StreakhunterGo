/* ============================================================
 *  game — Spielsteuerung: verbindet Gehirn (streakhunter) mit
 *  Hardware (sh_platform). Haelt den Spielstand, kuemmert sich
 *  um Tages-/Wochenwechsel und speichert automatisch.
 *  Die UI ruft nur diese Funktionen auf.
 * ============================================================ */
#ifndef GAME_H
#define GAME_H

#include "streakhunter.h"

/* Aktueller Spielstand (von der UI lesbar). */
extern SaveState g_state;

/* In setup() nach shp_init() aufrufen: laedt Stand oder bereitet
 * Onboarding vor. Gibt true zurueck, wenn schon ein Beast gewaehlt ist. */
bool game_begin(void);

/* Regelmaessig in loop() aufrufen: prueft Tageswechsel (RTC),
 * synchronisiert Schritte (IMU) und speichert bei Aenderungen. */
void game_tick(void);
int  game_take_daychange_levelup(int *evolved);  /* 1 einmalig, wenn Tageswechsel (Step-XP) ein Level-Up brachte */
int  game_cal_status(uint32_t dayIndex);          /* Ampel eines Tages: 0=rot 1=gelb 2=gruen, -1 unbekannt */
int  game_cal_today_status(void);                 /* Ampel des heutigen Tages (live) */

/* --- Aktionen (jede speichert danach automatisch) --- */
void game_choose_starter(uint8_t beastId);   /* Onboarding (alt, ungenutzt) */
void game_finish_onboarding(void);           /* Onboarding abschliessen (kein Starter) */
bool game_has_beast(void);                   /* aktives Karten-Beast vorhanden? */
void game_force_no_beast(void);              /* nach Werksreset: beast-los */

/* TAMA: game_act_hist() / SH_ACT_WEEKS entfallen (kein Schrittzaehler). */

int  game_poll_card(void);                   /* Hot-Swap: 0=keine Aenderung, 1=ein, 2=aus */
void game_claim(void);                        /* taeglicher Claim */
void game_quest_done(int category);           /* SH_CAT_* */
int  game_claim_loot(void);                   /* Item-ID vom letzten Claim (-1 = keins) */
int  game_quest_loot(void);                   /* Item-ID von letzter Quest (-1 = keins) */
void game_add_xp(uint32_t amount);            /* z.B. Abenteuer-Bonus */

/* Uebergreifendes Level-Up/Evolution-Event. Die UI pollt das im Loop und zeigt
 * dann - egal wo man ist - ein Level-Up-Popup, bei evolved zusaetzlich ein
 * Evolutions-Popup. Liefert 1, wenn ein Event anstand (dann sind level/evolved/
 * stage gesetzt), sonst 0. Einmalig (setzt sich beim Abholen zurueck). */
int  game_take_levelup(int *level, int *evolved, int *stage);
bool game_streak_saved(void);                 /* Streak-Schild hat gegriffen (einmal abholbar) */
int  game_arena_loot(void);                   /* Beute des letzten Kampfes, -1 = nichts */

/* Eindeutige Kennung des aktiven Beasts: Spezies + Geburtstag.
 * false = keine Karte. Damit bekommt jedes Beast seinen eigenen NVS-Schluessel. */
bool game_beast_uid(uint16_t *species, uint32_t *birthDay);

/* Kaempfe heute auf diesem GERAET (ueber alle Beasts). Ab SH_BATTLES_PER_DAY
 * gibt es keine Beute mehr - Level und Bindung aber schon. */
int  game_loot_fights_today(void);

/* Boss-Kampf ueber die normale Arena. which = SH_BOSS_LUNRAK / SH_BOSS_SOLVAR.
 * Sieg gibt garantiert ein Equip/Item plus BOSS_WIN_XP. */
bool game_arena_begin_boss(int which);
int  game_arena_boss(void);                   /* 0 = kein Boss, sonst SH_BOSS_* */
#define BOSS_WIN_XP 60

/* --- Interaktive Arena (die UI steuert die Runden Tipp fuer Tipp) --- */
int  game_arena_left(void);              /* verbleibende Kaempfe heute (0..10)     */
bool game_arena_begin(void);             /* neuen Gegner wuerfeln, HP voll setzen  */
int  game_arena_enemy_first(void);       /* 1 = schnellerer Gegner hat Erstschlag  */
int  game_arena_player_strike(int tier); /* Spieler schlaegt zu (tier 0/1/2)->Dmg */
int  game_arena_enemy_strike(void);      /* Gegner kontert -> Schaden              */
int  game_arena_player_hit(int tier);    /* EIN Spieler-Treffer -> Schaden         */
bool game_arena_player_extra(void);      /* Extra-Treffer Spieler (SPD-Vorteil)?   */
int  game_arena_enemy_roll(void);        /* Gegner-Intensitaet wuerfeln -> tier    */
int  game_arena_enemy_hit(void);         /* EIN Gegner-Treffer -> Schaden          */
bool game_arena_enemy_extra(void);       /* Extra-Treffer Gegner?                  */
void game_arena_finish(bool win);        /* Ergebnis verbuchen + speichern         */
int  game_arena_foe_hp(void);            /* aktuelle Gegner-HP                     */
int  game_arena_foe_max(void);           /* Gegner-Max-HP                          */
int  game_arena_my_hp(void);             /* aktuelle Spieler-HP                    */
int  game_arena_my_max(void);            /* Spieler-Max-HP                         */
uint8_t game_arena_enemy_id(void);       /* Beast-ID des Gegners                   */
int  game_arena_enemy_stage(void);       /* Entwicklungsstufe des Gegners (1..3)   */
int  game_arena_level(void);             /* Level des Kampfes                      */
const char* game_arena_enemy_name(void); /* Gegnername in passender Stufe          */

/* --- Move-Kampf-API --- */
int  game_move_player(int slot);          /* Move-ID des Spieler-Slots (1/2)        */
bool game_slot2_locked(void);             /* true = Slot 2 noch nicht freigeschaltet */
int  game_move_enemy(int slot);           /* Move-ID des Gegner-Slots (1/2)         */
bool game_move_playable(int slot);        /* genug Stamina + kein Cooldown?         */
int  game_arena_my_stamina(void);         /* Stamina Spieler 0..10                  */
int  game_arena_foe_stamina(void);        /* Stamina Gegner 0..10                   */
int  game_arena_player_move(int slot, int *heal);    /* Spieler nutzt Slot -> Schaden */
int  game_arena_enemy_move(int *heal, int *usedSlot);/* Gegner-KI zieht -> Schaden     */
void game_arena_tick(int *playerDot, int *enemyDot); /* Rundenstart: Burn/Poison-Tick */
int  game_arena_faster(void);             /* 1 = Gegner ist schneller               */
bool game_arena_type_edge(void);          /* hat Spieler Typ-Vorteil?               */
bool game_arena_was_crit(void);           /* war der letzte Treffer ein Volltreffer? */
int  game_arena_last_status(void);        /* 0=nichts, 1=Brand, 2=Gift gesetzt       */
bool game_arena_foe_burning(void);
bool game_arena_foe_poisoned(void);
bool game_arena_my_burning(void);
bool game_arena_my_poisoned(void);

/* --- Abfragen fuer die UI --- */
Stats game_player_stats(void);
int   game_xp_into_level(void);               /* XP im aktuellen Level */
int   game_xp_span(void);                     /* XP-Spanne dieses Levels */
int   game_player_stage(void);                /* 1..3 */
const char* game_player_name(void);           /* Name in aktueller Stufe */

#endif /* GAME_H */
