/* ============================================================
 *  vitals — Saettigung, Energie, Bindung
 *  ------------------------------------------------------------
 *  Der Tamagotchi-Kern. Laeuft ueber die ECHTE Zeit (RTC), nicht ueber
 *  millis(): das Beast hungert auch, waehrend das Geraet im Standby liegt.
 *
 *  Jeder Vitalwert ist an genau EINEN Kampfwert gekoppelt:
 *
 *      Saettigung -> DEF     (satt = Reserven, haelt was aus)
 *      Energie    -> SPD     (muede = langsam)
 *      Bindung    -> ATK     (es kaempft fuer dich)
 *
 *      Wirkwert = Grundwert * (0,80 + 0,30 * Vital/100)
 *
 *  Also -20 % bei 0 und +10 % bei 100. Dezent, aber im Kampf spuerbar.
 *
 *  SPEICHERORT (vorlaeufig): NVS-Blob "vitals" auf dem Geraet.
 *  Laut Konzept sollen die Werte spaeter mit dem Beast auf die Karte
 *  wandern (/tama.shv). Das kommt zusammen mit dem Inventar - dann
 *  brauchen wir die Datei ohnehin und schreiben sie in einem Rutsch.
 * ============================================================ */
#ifndef VITALS_H
#define VITALS_H

#include <stdint.h>
#include "streakhunter.h"

/* --- Design-Werte, alle an einer Stelle --- */
#define VIT_SAT_PER_H       3    /* Saettigung sinkt pro Stunde            */
#define VIT_SAT_PER_H_ADV   6    /* ... auf Abenteuer doppelt so schnell   */
#define VIT_ENE_PER_H       8    /* Energie steigt pro Stunde              */
#define VIT_ENE_PER_H_NIGHT 12   /* ... nachts (22-7 Uhr) schneller        */
#define VIT_BOND_DROP_H     1    /* Bindung sinkt pro Stunde bei Hunger    */
#define VIT_HUNGER_BOND_AT  30   /* ... naemlich unter dieser Saettigung   */

#define VIT_COST_FIGHT      8    /* Energie pro Arena-Kampf (= Recovery/h)  */
#define VIT_COST_ROAM       20   /* Energie: Streifzug (2 h)               */
#define VIT_COST_EXPED      45   /* Energie: Expedition (8 h)              */

/* ============================================================
 *  Kollaps
 *  ------------------------------------------------------------
 *  Bleibt die Saettigung VIT_COLLAPSE_H Stunden auf 0, bricht das Beast
 *  zusammen. Das ist KEIN Tod - Level und XP bleiben unberuehrt. Es ist eine
 *  Ohnmacht: Das Beast liegt VIT_RECOVER_H Stunden am Boden, kann nicht kaempfen
 *  und nicht auf Abenteuer, und die Bindung nimmt Schaden.
 *
 *  Tod waere die falsche Strafe. Ein Beast, in das man Wochen gesteckt hat, darf
 *  nicht an einem vergessenen Wochenende sterben - das macht das Spiel zu einer
 *  Pflicht, und Pflichten legt man weg. Eine Ohnmacht dagegen tut weh, ohne zu
 *  zerstoeren: Man verliert Zeit und Zuneigung, nicht das Beast.
 *
 *  Die PHOENIXFEDER faengt genau das ab. Liegt sie im Rucksack, verbrennt sie
 *  statt des Beasts: kein Kollaps, Saettigung zurueck auf VIT_FEATHER_SAT.
 * ============================================================ */
#define VIT_COLLAPSE_H      36   /* Stunden bei Saettigung 0 -> Kollaps    */
#define VIT_RECOVER_H       10   /* so lange liegt es danach am Boden      */
#define VIT_COLLAPSE_BOND   25   /* Bindung, die der Kollaps kostet        */
#define VIT_COLLAPSE_SAT    25   /* Saettigung nach dem Aufwachen          */
#define VIT_FEATHER_SAT     50   /* Saettigung, wenn die Feder rettet      */

/* --- Bindung STEIGT durch Pflege ---
 * Sie war lange nur eine Einbahnstrasse nach unten: sie fiel bei Hunger, aber
 * nichts liess sie wieder steigen. Das Beast blieb auf dem Startwert 20
 * haengen. Diese Quellen schliessen die Luecke - alle haengen daran, dass man
 * sich kuemmert, keine an Glueck. */
#define BOND_FEED        2   /* Fuettern                                    */
#define BOND_CLAIM       4   /* Claim the Day - der staerkste Einzelbeitrag  */
#define BOND_QUEST       2   /* je erledigter Tagesquest (3x = +6)           */
#define BOND_ARENA_WIN   1   /* Arena-Sieg - bewusst der kleinste Bonus:     */
#define BOND_ARENA_LOSS  1   /* Arena-Niederlage kostet Bindung (Enttaeuschung) */
                             /* die Arena soll das Leben nicht ersetzen      */
#define BOND_ADV_GOOD    4   /* Abenteuer ohne Pech                          */

/* Stimmungen (fuer die Sprechblase) */
enum { MOOD_HAPPY = 0, MOOD_HUNGRY, MOOD_TIRED, MOOD_SAD, MOOD_AWAY, MOOD_KO };

typedef struct {
    uint8_t  satiation;      /* 0..100 */
    uint8_t  energy;         /* 0..100 */
    uint8_t  bond;           /* 0..100 */
    uint32_t lastMin;        /* Minuten seit 1970: letzter Tick            */
    uint32_t hungerSinceMin; /* seit wann Saettigung 0 (0 = kein Hunger)   */
    uint32_t collapseUntil;  /* Erholung nach Kollaps (Minuten seit 1970)  */

    /* Kaempfe: gehoeren zum BEAST, nicht zum Geraet. Ein frisches Beast bringt
     * frische Kraft mit. Gegen das Farmen steht die Beute-Sperre (siehe game.cpp). */
    uint8_t  fightsToday;
    uint32_t fightsDay;      /* Tagesindex, zu dem fightsToday gehoert     */
} Vitals;

extern Vitals g_vit;

/* In setup() nach shp_init() UND game_begin() aufrufen - es braucht die
 * Beast-Kennung, um den richtigen Speicherplatz zu waehlen. */
void vit_begin(void);

/* ============================================================
 *  Beast-Wechsel
 *  ------------------------------------------------------------
 *  Jedes Beast hat seinen EIGENEN Speicherplatz im NVS, benannt nach Spezies
 *  und Geburtstag. Wird die Karte gezogen, friert sein Zustand ein: Hunger,
 *  Energie, Bindung und ein laufendes Abenteuer stehen still, bis die Karte
 *  wieder eingelegt wird. Man kann also Beasts wechseln, ohne dass eines im
 *  Regal verhungert.
 * ============================================================ */
void vit_bind(void);     /* Karte eingelegt: altes sichern, neues laden */
void vit_freeze(void);   /* Karte gezogen: Zustand einfrieren           */

/* Regelmaessig in loop() aufrufen (1x/Sek reicht). Rechnet die verstrichene
 * ECHTE Zeit ab und speichert bei Bedarf. */
void vit_tick(void);

/* --- Aenderungen (speichern selbst) --- */
void vit_feed(int satPlus, int energyPlus, int bondPlus);  /* Futter/Item   */
bool vit_spend_energy(int amount);   /* false = zu wenig Energie            */
void vit_add_bond(int delta);

/* Ungesicherte Aenderungen sofort in den Flash. Vor dem Deep Sleep aufrufen. */
void vit_flush(void);

/* --- Abfragen --- */
bool vit_is_collapsed(void);    /* liegt das Beast gerade am Boden?          */
int  vit_recover_minutes(void); /* wie lange noch? 0 = wieder auf den Beinen  */

/* Kollaps bzw. Rettung durch die Feder - einmal abholbar. Die Oberflaeche holt
 * das Ereignis ab, zeigt es an und quittiert es damit. */
bool vit_take_collapse_event(void);
bool vit_take_feather_event(void);
int  vit_mood(void);                 /* MOOD_* fuer die Sprechblase         */

/* Kampfwerte MIT Vitalwert-Einfluss. Das ist die Funktion, die die UI und
 * die Arena benutzen sollen - nicht sh_compute_stats() direkt. */
Stats vit_effective_stats(Stats base);

/* Prozentwert des Multiplikators (80..110), fuer die Anzeige. */
int  vit_mult_pct(int vital);

#endif /* VITALS_H */
