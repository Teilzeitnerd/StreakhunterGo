/* ============================================================
 *  equip — Ausruestung
 *  ------------------------------------------------------------
 *  EIN Slot. Wer etwas anlegt, wirft das Vorherige weg. Das haelt die
 *  Entscheidung scharf: kein Optimieren an vier Slots, sondern eine Frage -
 *  was brauche ich jetzt?
 *
 *  DER WURF PASSIERT BEIM ANLEGEN, nicht beim Finden.
 *  Zwei Amulette im Rucksack sind gleich; erst wenn man eines anlegt, faellt
 *  die Zahl. Das ist wichtig gegen einen Exploit: Wuerfelte man beim Finden,
 *  koennte man so lange an- und ablegen, bis das Maximum steht. Deshalb gibt
 *  es auch KEIN Ablegen - einmal angelegt, laeuft es ab.
 *
 *      Amulett       ATK  +3..+12
 *      Panzerplatte  DEF  +3..+12
 *      Sprungfedern  SPD  +3..+12
 *      Runenring     alle +1..+5     (breiter, dafuer flacher)
 *
 *  HALTBARKEIT: 5 Kaempfe. Danach zerfaellt es.
 *
 *  Die Boni sind FLACH und kommen NACH den Vitalwerten obendrauf:
 *      Grundwert -> Vitalwerte (Prozent) -> Ausruestung (flach)
 *  Ein gepflegtes Beast mit Ausruestung ist stark. Ein verwahrlostes mit
 *  Ausruestung ist nur weniger schwach.
 *
 *  Speicherort: NVS-Blob "equip".
 * ============================================================ */
#ifndef EQUIP_H
#define EQUIP_H

#include <stdint.h>
#include <stdbool.h>
#include "streakhunter.h"   /* Stats */

#define EQ_FIGHTS      5    /* Haltbarkeit in Kaempfen */
#define EQ_NONE      255    /* nichts angelegt         */

typedef struct {
    uint8_t item;        /* IT_AMULETT..IT_RING, oder EQ_NONE */
    uint8_t bonus;       /* der gewuerfelte Wert                   */
    uint8_t fightsLeft;  /* 0 = zerfallen                          */
} EquipState;

extern EquipState g_eq;

void equip_begin(void);                 /* in setup(), nach inv_begin() */

bool equip_active(void);                /* ist etwas angelegt?          */
int  equip_item(void);                  /* Item-ID oder -1              */
int  equip_bonus(void);                 /* gewuerfelter Wert            */
int  equip_fights_left(void);

/* Anlegen. Verbraucht ein Stueck aus dem Rucksack und wuerfelt den Bonus.
 * Ein bereits angelegtes Stueck wird dabei verworfen.
 * false = nicht im Rucksack oder kein Ausruestungsgegenstand. */
bool equip_put_on(int itemId);

/* Nach jedem Kampf aufrufen. Zaehlt die Haltbarkeit herunter. */
void equip_after_fight(void);

/* Legt die Boni auf die Kampfwerte. Nach vit_effective_stats() aufrufen. */
Stats equip_apply(Stats s);

const char* equip_name(void);           /* Name des angelegten Stuecks  */

#endif /* EQUIP_H */
