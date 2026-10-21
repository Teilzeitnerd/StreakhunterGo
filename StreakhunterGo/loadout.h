#ifndef LOADOUT_H
#define LOADOUT_H

#include <stdint.h>
#include <stdbool.h>
#include "moves.h"

/* ============================================================
 *  loadout.h — die zwei ausgeruesteten Attacken eines Beasts
 *  ------------------------------------------------------------
 *  Slot 1: immer ein Angriff (Basic/Special/Signature)
 *  Slot 2: frei (Angriff ODER Buff)
 *
 *  Pro Beast gespeichert (NVS-Blob mit Beast-Schluessel). Welche Moves
 *  freigeschaltet sind, wird LIVE aus Level, Siegen und Abenteuern
 *  berechnet - nichts extra zu speichern.
 * ============================================================ */

typedef struct {
    int slot1;   /* Move-ID, immer Angriff        */
    int slot2;   /* Move-ID, Angriff oder Buff    */
} Loadout;

extern Loadout g_lo;

/* Laden/Binden beim Kartenwechsel (wie vitals/adventure). */
void loadout_begin(void);
void loadout_bind(void);
void loadout_freeze(void);

/* Ist dieser Move fuer das aktuelle Beast freigeschaltet?
 * Basis immer; der Rest nach den Bedingungen (siehe loadout.cpp). */
bool loadout_unlocked(int moveId);

/* Setzen (mit Slot-Regel-Pruefung). Gibt false zurueck, wenn die Regel
 * verletzt wuerde (z.B. Buff in Slot 1) oder der Move nicht frei ist. */
bool loadout_set_slot1(int moveId);
bool loadout_set_slot2(int moveId);

/* Bequemer Zugriff fuers Kampf-/UI-Modul. */
int  loadout_slot(int which);   /* which = 1 oder 2 -> Move-ID */

/* Wie weit ist die Freischaltung? Fuer die UI-Anzeige. */
bool loadout_unlock_done(UnlockSlot slot);   /* Bedingung erfuellt? */

/* Loadout fuer einen GEGNER bestimmen. Gleiche Freischalt-Voraussetzungen wie
 * der Spieler (haengen an deinem Fortschritt), aber:
 *   Slot 1: staerkster freigeschalteter ANGRIFF seines Typs
 *   Slot 2: ZUFAELLIG aus seinem freigeschalteten Pool (Angriff ODER Buff)
 * rng: Zeiger auf denselben Kampf-Zufall (wird fortgeschrieben). */
void loadout_for_enemy(int enemyBeastId, uint32_t *rng, int *slot1, int *slot2);
const char* loadout_unlock_hint(UnlockSlot slot);  /* "Level 5", "15 Siege" ... */

#endif
