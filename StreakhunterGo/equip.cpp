/* ============================================================
 *  equip.cpp — Ausruestung
 * ============================================================ */
#include "equip.h"
#include <Arduino.h>
#include <string.h>
#include <esp_random.h>
#include "sh_platform.h"
#include "items.h"

#include "HWCDC.h"
extern HWCDC USBSerial;

EquipState g_eq;

#define EQ_MAGIC 0x45515031u   /* 'EQP1' */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  item;
    uint8_t  bonus;
    uint8_t  fightsLeft;
} EqBlob;

static void eq_save(void) {
    EqBlob b;
    b.magic      = EQ_MAGIC;
    b.item       = g_eq.item;
    b.bonus      = g_eq.bonus;
    b.fightsLeft = g_eq.fightsLeft;
    shp_save_blob("equip", &b, sizeof(b));
}

static void eq_clear(void) {
    g_eq.item       = EQ_NONE;
    g_eq.bonus      = 0;
    g_eq.fightsLeft = 0;
}

void equip_begin(void) {
    EqBlob b;
    if (shp_load_blob("equip", &b, sizeof(b)) && b.magic == EQ_MAGIC) {
        g_eq.item       = b.item;
        g_eq.bonus      = b.bonus;
        g_eq.fightsLeft = b.fightsLeft;
        /* Sicherheitsnetz: abgelaufenes oder unsinniges Stueck raeumen wir weg. */
        if (g_eq.fightsLeft == 0 || g_eq.item < IT_AMULETT || g_eq.item > IT_RING)
            eq_clear();
    } else {
        eq_clear();
    }

    if (equip_active())
        USBSerial.printf("[EQ] %s angelegt (+%d, noch %d Kaempfe)\n",
                         equip_name(), (int)g_eq.bonus, (int)g_eq.fightsLeft);
    else
        USBSerial.println("[EQ] nichts angelegt.");
}

bool equip_active(void)    { return g_eq.item != EQ_NONE && g_eq.fightsLeft > 0; }
int  equip_item(void)      { return equip_active() ? (int)g_eq.item : -1; }
int  equip_bonus(void)     { return equip_active() ? (int)g_eq.bonus : 0; }
int  equip_fights_left(void) { return equip_active() ? (int)g_eq.fightsLeft : 0; }

const char* equip_name(void) {
    if (!equip_active()) return "-";
    return ITEMS[g_eq.item].name;
}

bool equip_put_on(int itemId) {
    if (itemId < IT_AMULETT || itemId > IT_RING) return false;
    if (inv_count(itemId) <= 0) return false;
    if (!inv_use(itemId)) return false;          /* verbraucht das Stueck */

    /* Der Wurf. Der Runenring streut schmaler, wirkt dafuer auf alles. */
    int roll = (itemId == IT_RING)
             ? 1 + (int)(esp_random() % 5)       /* 1..5  */
             : 3 + (int)(esp_random() % 10);     /* 3..12 */

    g_eq.item       = (uint8_t)itemId;
    g_eq.bonus      = (uint8_t)roll;
    g_eq.fightsLeft = EQ_FIGHTS;
    eq_save();

    USBSerial.printf("[EQ] %s angelegt: +%d fuer %d Kaempfe.\n",
                     ITEMS[itemId].name, roll, EQ_FIGHTS);
    return true;
}

void equip_after_fight(void) {
    if (!equip_active()) return;

    g_eq.fightsLeft--;
    if (g_eq.fightsLeft == 0) {
        USBSerial.printf("[EQ] %s ist zerfallen.\n", ITEMS[g_eq.item].name);
        eq_clear();
    } else {
        USBSerial.printf("[EQ] %s: noch %d Kaempfe.\n",
                         ITEMS[g_eq.item].name, (int)g_eq.fightsLeft);
    }
    eq_save();
}

Stats equip_apply(Stats s) {
    if (!equip_active()) return s;
    int b = g_eq.bonus;

    switch (g_eq.item) {
        case IT_AMULETT:    s.atk += b; break;
        case IT_PANZER:     s.def += b; break;
        case IT_FEDERN:     s.spd += b; break;
        case IT_RING:  s.atk += b; s.def += b; s.spd += b; break;
        default: break;
    }
    return s;
}
