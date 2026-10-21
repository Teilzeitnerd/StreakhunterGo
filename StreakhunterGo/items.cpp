/* ============================================================
 *  items.cpp — Rucksack
 * ============================================================ */
#include "items.h"
#include <Arduino.h>
#include <string.h>
#include "sh_platform.h"
#include "vitals.h"
#include "assets.h"

#include "HWCDC.h"
extern HWCDC USBSerial;

uint8_t g_inv[IT_COUNT];

/* Werte aus dem Konzept. Wer hier etwas dreht, dreht am Balancing -
 * die Zahlen haengen an der Saettigungsrate (-3/h) und der 33-Stunden-Kurve. */
const ItemDef ITEMS[IT_COUNT] = {
    /* name           cat        sat  ene bond  note                       */
    { "Beere",       CAT_FOOD,   15,   0,   0, "+15 Sättigung"             },
    { "Fleisch",     CAT_FOOD,   35,   0,   0, "+35 Sättigung"             },
    { "Kraftnuss",   CAT_FOOD,   25,  20,   0, "+25 Sättigung, +20 Energie"},
    { "Festmahl",    CAT_FOOD,   70,   0,   5, "+70 Sättigung, +5 Bindung" },

    { "Amulett",     CAT_EQUIP,   0,   0,   0, "ATK +3 bis +12"        },
    { "Panzerplatte",CAT_EQUIP,   0,   0,   0, "DEF +3 bis +12"   },
    { "Sprungfedern",CAT_EQUIP,   0,   0,   0, "SPD +3 bis +12"          },
    { "Runenring",   CAT_EQUIP,   0,   0,   0, "Alle Werte +1 bis +5"      },

    { "Streak-Schild",CAT_ITEM,   0,   0,   0, "rettet die Streak einmal"  },
    { "XP-Rolle",    CAT_ITEM,    0,   0,   0, "+100 XP"                   },
    { "Phönixfeder", CAT_ITEM,    0,   0,   0, "fängt einen Kollaps ab"    },
    { "Vital-Elixier",CAT_ITEM,  40,  40,  40, "alles +40"                 },
};

/* --- Icons --- Alle zwoelf sind jetzt da, keine Platzhalter mehr. */
static const lv_image_dsc_t* ICON[IT_COUNT] = {
    &it_beere,   &it_fleisch, &it_kraftnuss, &it_festmahl,
    &eq_amulett, &eq_panzer,  &eq_federn,    &eq_ring,
    &it_schild,  &it_rolle,   &it_feder,     &it_elixier,
};

static const bool HAS_ICON[IT_COUNT] = {
    true, true, true, true,
    true, true, true, true,
    true, true, true, true,
};

const lv_image_dsc_t* item_icon(int id) {
    if (id < 0 || id >= IT_COUNT) id = 0;
    return ICON[id];
}

bool item_has_icon(int id) {
    if (id < 0 || id >= IT_COUNT) return false;
    return HAS_ICON[id];
}

/* ============================================================
 *  Persistenz
 * ============================================================ */
#define INV_MAGIC 0x494E5631u   /* 'INV1' */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  inv[IT_COUNT];
} InvBlob;

static void inv_save(void) {
    InvBlob b;
    b.magic = INV_MAGIC;
    memcpy(b.inv, g_inv, IT_COUNT);
    shp_save_blob("inv", &b, sizeof(b));
}

void inv_begin(void) {
    InvBlob b;
    if (shp_load_blob("inv", &b, sizeof(b)) && b.magic == INV_MAGIC) {
        memcpy(g_inv, b.inv, IT_COUNT);
        USBSerial.printf("[INV] geladen: %d Gegenstände\n", inv_total());
        return;
    }

    /* Starterpaket. Ohne Futterquelle (Arena und Abenteuer geben noch nichts
     * her) wuerde das Beast sonst zwangslaeufig verhungern. */
    memset(g_inv, 0, sizeof(g_inv));
    g_inv[IT_BEERE]     = 5;
    g_inv[IT_FLEISCH]   = 3;
    g_inv[IT_KRAFTNUSS] = 1;
    inv_save();
    USBSerial.println("[INV] Starterpaket angelegt (5 Beeren, 3 Fleisch, 1 Kraftnuss).");
}

/* ============================================================
 *  Zugriff
 * ============================================================ */
int inv_count(int id) {
    if (id < 0 || id >= IT_COUNT) return 0;
    return g_inv[id];
}

int inv_total(void) {
    int n = 0;
    for (int i = 0; i < IT_COUNT; i++) n += g_inv[i];
    return n;
}

int inv_add(int id, int n) {
    if (id < 0 || id >= IT_COUNT || n <= 0) return 0;
    int room = INV_MAX_STACK - g_inv[id];
    if (room <= 0) return 0;                 /* Stapel voll -> Beute verfaellt */
    if (n > room) n = room;
    g_inv[id] += (uint8_t)n;
    inv_save();
    return n;
}

bool inv_use(int id) {
    if (id < 0 || id >= IT_COUNT || g_inv[id] == 0) return false;
    g_inv[id]--;
    inv_save();
    return true;
}
