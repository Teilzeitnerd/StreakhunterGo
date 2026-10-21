/* ============================================================
 *  loadout.cpp — die zwei Attacken pro Beast
 * ============================================================ */
#include "loadout.h"
#include <Arduino.h>
#include <string.h>
#include "sh_platform.h"
#include "streakhunter.h"
#include "adventure.h"
#include "game.h"

#include "HWCDC.h"
extern HWCDC USBSerial;

bool game_beast_uid(uint16_t *species, uint32_t *birthDay);

Loadout g_lo;

#define LO_MAGIC 0x4C4F4431u   /* 'LOD1' */

/* Freischalt-Schwellen - genau nach Kevs Vorgabe. */
#define UNLOCK_SP1_LEVEL    5
#define UNLOCK_BUFF_LEVEL  10
#define UNLOCK_SP2_LEVEL   15
#define UNLOCK_SIG_LEVEL   20

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  slot1;
    uint8_t  slot2;
} LoBlob;

static char s_key[16] = "loadout";

static void make_key(void) {
    uint16_t sp; uint32_t bd;
    if (game_beast_uid(&sp, &bd)) snprintf(s_key, sizeof(s_key), "l%u_%lu",
                                           (unsigned)sp, (unsigned long)bd);
    else                          strcpy(s_key, "loadout");
}

/* --- Freischaltung: live aus dem Spielstand, nichts gespeichert. --- */
bool loadout_unlock_done(UnlockSlot slot) {
    switch (slot) {
        case UNLOCK_SP1:  return g_state.level >= UNLOCK_SP1_LEVEL;
        case UNLOCK_BUFF: return g_state.level >= UNLOCK_BUFF_LEVEL;
        case UNLOCK_SP2:  return g_state.level >= UNLOCK_SP2_LEVEL;
        case UNLOCK_SIG:  return g_state.level >= UNLOCK_SIG_LEVEL;
        default:          return true;
    }
}

const char* loadout_unlock_hint(UnlockSlot slot) {
    static char buf[24];
    int lvl = slot == UNLOCK_SP1  ? UNLOCK_SP1_LEVEL  :
              slot == UNLOCK_BUFF ? UNLOCK_BUFF_LEVEL :
              slot == UNLOCK_SP2  ? UNLOCK_SP2_LEVEL  : UNLOCK_SIG_LEVEL;
    snprintf(buf, sizeof(buf), "ab Level %d", lvl);
    return buf;
}

bool loadout_unlocked(int moveId) {
    const Move *m = move_get(moveId);
    if (m->cat == CAT_BASIC) return true;

    /* Der Move muss zum Typ des Beasts gehoeren. */
    MoveType bt = beast_type(g_state.beastId);
    if ((MoveType)m->type != bt) return false;

    if (m->cat == CAT_SIGNATURE) return loadout_unlock_done(UNLOCK_SIG);
    if (m->cat == CAT_BUFF)      return loadout_unlock_done(UNLOCK_BUFF);

    /* Zwei Specials: der erste (SP1) ist der zweite Move im Typblock,
     * der zweite (SP2) der dritte. */
    int base = (int)bt * MOVES_PER_TYPE;
    if (moveId == base + 1) return loadout_unlock_done(UNLOCK_SP1);
    if (moveId == base + 2) return loadout_unlock_done(UNLOCK_SP2);
    return false;
}

/* --- Standard-Loadout: Basis + erster freigeschalteter Zweitslot. --- */
static void lo_default(void) {
    MoveType bt = beast_type(g_state.beastId);
    int base = (int)bt * MOVES_PER_TYPE;
    g_lo.slot1 = base;          /* Basis */
    g_lo.slot2 = base;          /* zur Not auch Basis - immer gueltig */

    /* Slot 2 mit dem ersten sinnvollen freigeschalteten Move fuellen. */
    int prefer[3] = { base + 1, base + 4, base + 3 };  /* SP1, Buff, Signature */
    for (int i = 0; i < 3; i++) {
        if (loadout_unlocked(prefer[i])) { g_lo.slot2 = prefer[i]; break; }
    }
}

static void lo_save(void) {
    LoBlob b = { LO_MAGIC, (uint8_t)g_lo.slot1, (uint8_t)g_lo.slot2 };
    shp_save_blob(s_key, &b, sizeof(b));
}

void loadout_bind(void) {
    make_key();
    LoBlob b;
    if (shp_load_blob(s_key, &b, sizeof(b)) && b.magic == LO_MAGIC) {
        g_lo.slot1 = b.slot1;
        g_lo.slot2 = b.slot2;
        if (!loadout_unlocked(g_lo.slot2)) lo_default();
    } else {
        lo_default();
    }
    /* Slot 1 ist IMMER der Basis-Angriff des eigenen Typs - fix, nicht
     * waehlbar. Egal was gespeichert war. */
    g_lo.slot1 = (int)beast_type(g_state.beastId) * MOVES_PER_TYPE;

    USBSerial.printf("[LO] %s + %s\n",
                     move_get(g_lo.slot1)->name, move_get(g_lo.slot2)->name);
}

void loadout_freeze(void) {
    lo_save();
    strcpy(s_key, "loadout");
}

void loadout_begin(void) {
    loadout_bind();
}

/* --- Setzen mit Slot-Regel. --- */
bool loadout_set_slot1(int moveId) {
    const Move *m = move_get(moveId);
    if (m->cat == CAT_BUFF) return false;       /* Slot 1 nur Angriff */
    if (!loadout_unlocked(moveId)) return false;
    g_lo.slot1 = moveId;
    lo_save();
    return true;
}

bool loadout_set_slot2(int moveId) {
    if (!loadout_unlocked(moveId)) return false;  /* Slot 2 frei: alles erlaubt */
    g_lo.slot2 = moveId;
    lo_save();
    return true;
}

int loadout_slot(int which) {
    return which == 1 ? g_lo.slot1 : g_lo.slot2;
}

/* --- Gegner-Loadout ---
 * Die Freischalt-Bedingungen sind dieselben wie beim Spieler (loadout_unlock_done
 * liest deinen Fortschritt). Der Gegner hat also genau die Slots frei, die auch
 * du frei haettest - fair und mitwachsend. Nur sein Typ ist ein anderer, und
 * Slot 2 wird gewuerfelt, damit er kein Spiegelbild ist. */
void loadout_for_enemy(int enemyBeastId, uint32_t *rng, int *slot1, int *slot2) {
    MoveType et = beast_type(enemyBeastId);
    int base = (int)et * MOVES_PER_TYPE;

    /* Welche Moves des Gegner-Typs sind frei? Basis immer. */
    bool sp1 = loadout_unlock_done(UNLOCK_SP1);
    bool buff= loadout_unlock_done(UNLOCK_BUFF);
    bool sp2 = loadout_unlock_done(UNLOCK_SP2);
    bool sig = loadout_unlock_done(UNLOCK_SIG);

    /* Slot 1: staerkster freigeschalteter Angriff.
     * Reihenfolge der Staerke: Signature > Special2 > Special1 > Basis. */
    if      (sig) *slot1 = base + 3;   /* Signature */
    else if (sp2) *slot1 = base + 2;   /* Special 2 */
    else if (sp1) *slot1 = base + 1;   /* Special 1 */
    else          *slot1 = base;       /* Basis     */

    /* Slot 2: zufaellig aus dem freigeschalteten Pool. Kandidaten sammeln
     * (Angriffe UND Buff), dann einen ziehen. Basis ist immer dabei, damit
     * die Liste nie leer ist. */
    int pool[MOVES_PER_TYPE];
    int n = 0;
    pool[n++] = base;                       /* Basis  (immer)          */
    if (sp1)  pool[n++] = base + 1;         /* Special 1               */
    if (sp2)  pool[n++] = base + 2;         /* Special 2               */
    if (sig)  pool[n++] = base + 3;         /* Signature               */
    if (buff) pool[n++] = base + 4;         /* Buff                    */

    *rng = (*rng) * 1103515245u + 12345u;
    int pick = (int)(((*rng) >> 16) % (uint32_t)n);
    *slot2 = pool[pick];

    /* Vermeidet den Sonderfall "zweimal exakt derselbe Move", solange es eine
     * Alternative gibt - sonst wirkt der Gegner arm. */
    if (*slot2 == *slot1 && n > 1) {
        *rng = (*rng) * 1103515245u + 12345u;
        int p2 = (int)(((*rng) >> 16) % (uint32_t)n);
        if (pool[p2] != *slot1) *slot2 = pool[p2];
    }
}
