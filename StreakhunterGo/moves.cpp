#include "moves.h"

/* ============================================================
 *  Die Attacken-Tabelle
 *  ------------------------------------------------------------
 *  Pro Typ genau 5, in fester Reihenfolge:
 *    Basis (0.7x)  Special1  Special2  Signature  Buff
 *
 *  Ausgewaehlt aus der grossen Referenzliste - je die, die klar
 *  einen Charakter haben, ohne Sonderregeln, die wir nicht bauen.
 *
 *  power:   fester Grundschaden. Basic 8, Special 11-13, Signature 14-16, 0=Buff
 *  magPct:  Buff-Hoehe / Heal-% / DEF-Ignore-%   (bei Burn/Poison egal)
 *  rounds:  Effektdauer; 0 = fuer den Rest des Kampfes
 * ============================================================ */
static const Move MOVES[MV_COUNT] = {
    /* ---------- FLAME ---------- */
    { "Glutschlag",   TYPE_FLAME, CAT_BASIC,      8, EF_NONE,       0, 0 },
    { "Sengfeuer", TYPE_FLAME, CAT_SPECIAL,   11, EF_BURN,       0, 2 },
    { "Flammenlanze",    TYPE_FLAME, CAT_SPECIAL,   13, EF_DEF_IGNORE,20, 0 },
    { "Infernosturz",  TYPE_FLAME, CAT_SIGNATURE, 15, EF_BURN,       0, 3 },
    { "Hitzekern",      TYPE_FLAME, CAT_BUFF,        0, EF_BUFF_ATK,  20, 0 },

    /* ---------- VENOM ---------- */
    { "Giftzahn",    TYPE_VENOM, CAT_BASIC,      8, EF_NONE,       0, 0 },
    { "Giftwolke",    TYPE_VENOM, CAT_SPECIAL,   11, EF_POISON,     0, 3 },
    { "Giftpfeil",     TYPE_VENOM, CAT_SPECIAL,   13, EF_DEF_IGNORE,20, 0 },
    { "Säurestoß",     TYPE_VENOM, CAT_SIGNATURE, 15, EF_POISON,     0, 3 },
    { "Giftschild",    TYPE_VENOM, CAT_BUFF,        0, EF_BUFF_DEF,  25, 0 },

    /* ---------- SHADOW ---------- */
    { "Schattenhieb",  TYPE_SHADOW,CAT_BASIC,      8, EF_NONE,       0, 0 },
    { "Dunkelpuls",     TYPE_SHADOW,CAT_SPECIAL,   13, EF_DEF_IGNORE,20, 0 },
    { "Leerenpuls",     TYPE_SHADOW,CAT_SPECIAL,   11, EF_HEAL,      10, 0 },
    { "Albtraumschlag", TYPE_SHADOW,CAT_SIGNATURE, 16, EF_NONE,       0, 0 },
    { "Schattenschleier",    TYPE_SHADOW,CAT_BUFF,        0, EF_BUFF_SPD,  30, 0 },

    /* ---------- LIGHT ---------- */
    { "Lichthieb",   TYPE_LIGHT, CAT_BASIC,      8, EF_NONE,       0, 0 },
    { "Sonnenstrahl",        TYPE_LIGHT, CAT_SPECIAL,   11, EF_HEAL,      10, 0 },
    { "Heiliger Hieb",    TYPE_LIGHT, CAT_SPECIAL,   12, EF_BUFF_DEF,  25, 2 },
    { "Strahlenstoß",  TYPE_LIGHT, CAT_SIGNATURE, 15, EF_HEAL,      10, 0 },
    { "Inneres Licht",    TYPE_LIGHT, CAT_BUFF,        0, EF_BUFF_DEF,  25, 0 },

    /* ---------- WATER ---------- */
    { "Wasserstrahl",      TYPE_WATER, CAT_BASIC,      8, EF_NONE,       0, 0 },
    { "Tiefenströmung",   TYPE_WATER, CAT_SPECIAL,   11, EF_HEAL,      10, 0 },
    { "Sog",        TYPE_WATER, CAT_SPECIAL,   13, EF_DEF_IGNORE,20, 0 },
    { "Flutwucht",    TYPE_WATER, CAT_SIGNATURE, 15, EF_NONE,       0, 0 },
    { "Stilles Wasser",   TYPE_WATER, CAT_BUFF,        0, EF_HEAL,      15, 0 },
};

const Move* move_get(int id) {
    if (id < 0 || id >= MV_COUNT) return &MOVES[0];
    return &MOVES[id];
}

/* --- Typen-Matchup: 5er-Kreis, jeder schlaegt die naechsten zwei.
 *   FLAME -> VENOM -> SHADOW -> LIGHT -> WATER -> (FLAME)
 * Jeder Typ hat damit genau 2 Staerken und 2 Schwaechen, kein Paar ist
 * wechselseitig stark. --- */
bool type_advantage(MoveType a, MoveType d) {
    int diff = ((int)d - (int)a + TYPE_COUNT) % TYPE_COUNT;
    return diff == 1 || diff == 2;
}

int move_stamina_cost(int moveId) {
    const Move *m = move_get(moveId);
    switch (m->cat) {
        case CAT_BASIC:     return 0;
        case CAT_BUFF:      return 2;
        case CAT_SPECIAL:   return 3;
        case CAT_SIGNATURE: return 6;
        default:            return 0;
    }
}

int moves_of_type(MoveType t, int out_ids[MOVES_PER_TYPE]) {
    int base = (int)t * MOVES_PER_TYPE;
    for (int i = 0; i < MOVES_PER_TYPE; i++) out_ids[i] = base + i;
    return MOVES_PER_TYPE;
}

/* Freischalt-Slot -> Move-ID. Reihenfolge in jedem Typ-Block:
 *   +0 Basis  +1 Special1  +2 Special2  +3 Signature  +4 Buff        */
int move_for_unlock(MoveType t, UnlockSlot slot) {
    int base = (int)t * MOVES_PER_TYPE;
    switch (slot) {
        case UNLOCK_SP1:  return base + 1;   /* Special 1  */
        case UNLOCK_BUFF: return base + 4;   /* Buff       */
        case UNLOCK_SP2:  return base + 2;   /* Special 2  */
        case UNLOCK_SIG:  return base + 3;   /* Signature  */
        default:          return base;       /* Basis      */
    }
}
