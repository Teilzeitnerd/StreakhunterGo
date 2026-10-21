#ifndef MOVES_H
#define MOVES_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 *  Attacken und Typen fuer das Kampfsystem
 *  ------------------------------------------------------------
 *  Bewusst schlank gehalten (siehe Plan):
 *    - 5 Typen (kein Wind - kein Beast traegt ihn)
 *    - 2 Status-Effekte: Burn und Poison (Schaden ueber Zeit)
 *    - Buffs auf einen Wert, rundenbasiert
 *    - pro Typ: 1 Basis + 2 Special + 1 Signature + 1 Buff = 5 Moves
 *    - jedes Beast ruestet GENAU 2 aus: Slot 1 Angriff, Slot 2 frei
 * ============================================================ */

/* --- Typen. Reihenfolge = Kreis fuer die Matchup-Tabelle. --- */
typedef enum {
    TYPE_FLAME = 0,
    TYPE_VENOM,
    TYPE_SHADOW,
    TYPE_LIGHT,
    TYPE_WATER,
    TYPE_COUNT
} MoveType;

/* --- Beast -> Typ. Aus der Zuordnung im Spiel (RUGA=Light usw.). --- */
static inline MoveType beast_type(int beastId) {
    switch (beastId) {
        case 0: return TYPE_LIGHT;    /* RUGA   */
        case 1: return TYPE_FLAME;    /* PINTAN */
        case 2: return TYPE_SHADOW;   /* FEARO  */
        case 3: return TYPE_WATER;    /* LEONA  */
        case 4: return TYPE_SHADOW;   /* LURCO  */
        case 5: return TYPE_VENOM;    /* GRELO  */
        default: return TYPE_LIGHT;
    }
}

/* --- Kategorie eines Moves. Bestimmt Slot-Regeln und Freischaltung. --- */
typedef enum {
    CAT_BASIC = 0,    /* immer verfuegbar, nur Slot 1                     */
    CAT_SPECIAL,      /* Angriff mit Effekt, Slot 1 oder 2               */
    CAT_SIGNATURE,    /* staerkster Angriff, Slot 1 oder 2               */
    CAT_BUFF          /* kein Schaden, NUR Slot 2                        */
} MoveCat;

/* --- Was ein Move zusaetzlich bewirkt. Genau EIN Effekt pro Move. --- */
typedef enum {
    EF_NONE = 0,
    EF_BURN,          /* Gegner: Schaden ueber Zeit                      */
    EF_POISON,        /* Gegner: Schaden ueber Zeit, laenger             */
    EF_BUFF_ATK,      /* selbst: ATK hoch                                */
    EF_BUFF_DEF,      /* selbst: DEF hoch                                */
    EF_BUFF_SPD,      /* selbst: SPD hoch                                */
    EF_HEAL,          /* selbst: HP zurueck                              */
    EF_DEF_IGNORE     /* Angriff ignoriert einen Teil der Gegner-DEF     */
} MoveEffect;

/* --- Ein Move. Kompakt, damit die Tabelle klein bleibt. --- */
typedef struct {
    const char *name;
    uint8_t     type;       /* MoveType                                  */
    uint8_t     cat;        /* MoveCat                                   */
    uint8_t     power;      /* fester Grundschaden (0 = reiner Buff/Heal)     */
    uint8_t     effect;     /* MoveEffect                                */
    uint8_t     magPct;     /* Effekt-Staerke in % (Buff-Hoehe, Heal-%,  */
                            /*   DEF-Ignore-%). Bei Burn/Poison ungenutzt */
    uint8_t     rounds;     /* Dauer des Effekts in Runden (0 = battle)  */
} Move;

/* --- Move-IDs. Reihenfolge: pro Typ 5 Moves am Stueck. --- */
enum {
    /* FLAME */
    MV_FL_BASIC = 0, MV_FL_SP1, MV_FL_SP2, MV_FL_SIG, MV_FL_BUFF,
    /* VENOM */
    MV_VE_BASIC, MV_VE_SP1, MV_VE_SP2, MV_VE_SIG, MV_VE_BUFF,
    /* SHADOW */
    MV_SH_BASIC, MV_SH_SP1, MV_SH_SP2, MV_SH_SIG, MV_SH_BUFF,
    /* LIGHT */
    MV_LI_BASIC, MV_LI_SP1, MV_LI_SP2, MV_LI_SIG, MV_LI_BUFF,
    /* WATER */
    MV_WA_BASIC, MV_WA_SP1, MV_WA_SP2, MV_WA_SIG, MV_WA_BUFF,
    MV_COUNT
};

#define MOVES_PER_TYPE 5

/* Zugriff und Abfragen (Definition in moves.cpp). */
const Move* move_get(int id);
bool        type_advantage(MoveType attacker, MoveType defender);  /* true = x1.4 */

/* --- Stamina --- */
#define STAMINA_MAX      10
#define STAMINA_START     6
#define STAMINA_REGEN     2    /* pro voller Runde, beide Kaempfer            */
#define STAMINA_BASIC_GAIN 1   /* Basic-Treffer gibt Stamina zurueck          */
#define SIGNATURE_COOLDOWN 1   /* eigene Zuege, die die Signature blockiert   */

/* Stamina-Kosten eines Moves (aus seiner Kategorie). */
int move_stamina_cost(int moveId);

/* Alle Move-IDs eines Typs (5 Stueck) - fuer die Loadout-Auswahl. */
int         moves_of_type(MoveType t, int out_ids[MOVES_PER_TYPE]);

/* Die vier freischaltbaren Slots eines Typs, in Freischalt-Reihenfolge:
 *   [0] Special 1   [1] Buff   [2] Special 2   [3] Signature
 * (Basis ist immer frei und nicht in dieser Liste.) */
typedef enum {
    UNLOCK_SP1 = 0,   /* Level 5                    */
    UNLOCK_BUFF,      /* 15 Arena-Siege             */
    UNLOCK_SP2,       /* 5 Expeditionen + 5 Streifzuege */
    UNLOCK_SIG,       /* Level 20                   */
    UNLOCK_COUNT
} UnlockSlot;

int move_for_unlock(MoveType t, UnlockSlot slot);

#endif
