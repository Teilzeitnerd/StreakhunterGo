/* ============================================================
 *  vitals.cpp — Saettigung, Energie, Bindung in Echtzeit
 * ============================================================ */
#include "vitals.h"
#include <Arduino.h>
#include <string.h>
#include "sh_platform.h"
#include "items.h"
#include "game.h"
#include "adventure.h"

#include "HWCDC.h"
extern HWCDC USBSerial;

Vitals g_vit;

#define VIT_MAGIC 0x54564131u   /* 'TVA1' */

/* Auf der Platte liegt ein Kopf mit Magic + Version davor, damit ein
 * spaeterer Formatwechsel erkannt wird statt Muell zu laden. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    Vitals   v;
} VitBlob;

static bool     s_dirty     = false;
static uint32_t s_lastSaveM = 0;

static bool     s_collapseEvent = false;   /* einmal abholbar */
static bool     s_featherEvent  = false;

/* ============================================================
 *  Speicherplatz pro Beast
 *  ------------------------------------------------------------
 *  Jedes Beast bekommt seinen eigenen NVS-Schluessel, gebildet aus Spezies und
 *  Geburtstag. Damit sind selbst zwei Karten derselben Spezies unterscheidbar,
 *  und keines erbt den Hunger des anderen.
 *
 *  NVS-Schluessel duerfen hoechstens 15 Zeichen haben: "v5_4294967295" = 13.
 * ============================================================ */
static char s_key[16] = "vitals";

static void make_key(void) {
    uint16_t sp; uint32_t bd;
    if (game_beast_uid(&sp, &bd)) snprintf(s_key, sizeof(s_key), "v%u_%lu",
                                           (unsigned)sp, (unsigned long)bd);
    else                          strcpy(s_key, "vitals");
}

/* ============================================================
 *  Laden / Speichern
 * ============================================================ */
static void vit_save(void) {
    VitBlob b;
    b.magic   = VIT_MAGIC;
    b.version = 2;
    b.v       = g_vit;
    shp_save_blob(s_key, &b, sizeof(b));
    s_dirty     = false;
    s_lastSaveM = shp_now_minutes();
}

static void vit_fresh(uint32_t now) {
    memset(&g_vit, 0, sizeof(g_vit));
    g_vit.satiation = 100;
    g_vit.energy    = 100;
    g_vit.bond      = 20;    /* Bindung muss man sich verdienen */
    g_vit.lastMin   = now;
}

/* thaw = true: die Karte wurde gerade eingelegt. Dann wird die Zeit, in der das
 * Beast im Regal lag, NICHT nachgeholt - genau das ist der Freeze. */
static void vit_load(bool thaw) {
    VitBlob b;
    uint32_t now = shp_now_minutes();

    if (shp_load_blob(s_key, &b, sizeof(b)) && b.magic == VIT_MAGIC && b.version == 2) {
        g_vit = b.v;
        if (thaw) g_vit.lastMin = now;      /* Pause nicht nachrechnen */
        USBSerial.printf("[VIT] %s geladen: Saettigung %u  Energie %u  Bindung %u%s\n",
                         s_key, (unsigned)g_vit.satiation, (unsigned)g_vit.energy,
                         (unsigned)g_vit.bond, thaw ? "  (aufgetaut)" : "");
    } else {
        vit_fresh(now);
        vit_save();
        USBSerial.printf("[VIT] %s: frische Vitalwerte angelegt.\n", s_key);
    }

    /* Sicherheitsnetz: Ist die Uhr zurueckgesprungen (RTC neu gestellt),
     * darf lastMin nicht in der Zukunft liegen. */
    if (g_vit.lastMin > now) g_vit.lastMin = now;

    s_lastSaveM = now;
    s_dirty     = false;
}

void vit_begin(void) {
    make_key();
    vit_load(false);    /* Boot: die verstrichene Zeit WIRD nachgerechnet */
}

/* --- Karte gezogen: einfrieren --- */
void vit_freeze(void) {
    vit_save();
    USBSerial.printf("[VIT] %s eingefroren (Saettigung %u).\n",
                     s_key, (unsigned)g_vit.satiation);
}

/* --- Karte eingelegt: altes sichern, neues auftauen --- */
void vit_bind(void) {
    vit_save();         /* noch unter dem ALTEN Schluessel */
    make_key();         /* auf das neue Beast umstellen    */
    vit_load(true);     /* auftauen: Pause wird nicht nachgeholt */
}

/* ============================================================
 *  Der Tick — hier vergeht die Zeit
 * ============================================================ */
void vit_tick(void) {
    uint32_t now = shp_now_minutes();
    if (now <= g_vit.lastMin) return;

    uint32_t mins = now - g_vit.lastMin;

    /* Ohne Karte ist das Beast in Stase: es hungert nicht, es altert nicht.
     * Das ist der Urlaubsmodus - die Karte ist die Pausentaste. Wir schieben
     * lastMin einfach mit, damit die Pause nicht spaeter nachgeholt wird. */
    if (!game_has_beast()) {
        g_vit.lastMin = now;
        return;
    }

    /* Ganze Stunden verarbeiten; der Rest bleibt fuer den naechsten Tick
     * stehen. So geht auch bei einem Neustart keine Minute verloren. */
    uint32_t hours = mins / 60;
    if (hours == 0) return;
    g_vit.lastMin += hours * 60;

    int hr = 0, mi = 0;
    shp_clock(&hr, &mi);
    bool night = (hr >= 22 || hr < 7);

    /* --- Saettigung --- */
    /* Auf Abenteuer zehrt es doppelt. Das ist der Preis der Expedition:
     * acht Stunden unterwegs kosten so viel wie sechzehn zu Hause. */
    int rateSat = adv_is_away() ? VIT_SAT_PER_H_ADV : VIT_SAT_PER_H;
    int satDrop = (int)hours * rateSat;
    int sat = (int)g_vit.satiation - satDrop;
    if (sat < 0) sat = 0;

    /* --- Bindung: leidet, wenn das Beast hungert --- */
    int bond = (int)g_vit.bond;
    if (g_vit.satiation < VIT_HUNGER_BOND_AT || sat < VIT_HUNGER_BOND_AT)
        bond -= (int)hours * VIT_BOND_DROP_H;
    if (bond < 0)   bond = 0;
    if (bond > 100) bond = 100;

    /* --- Energie --- */
    int rate = night ? VIT_ENE_PER_H_NIGHT : VIT_ENE_PER_H;
    int ene  = (int)g_vit.energy + (int)hours * rate;
    if (ene > 100) ene = 100;

    /* --- Hungertimer --- */
    if (sat == 0) {
        if (g_vit.hungerSinceMin == 0) {
            g_vit.hungerSinceMin = now;
            USBSerial.println("[VIT] Saettigung auf 0 - der Hungertimer laeuft.");
        }
    } else {
        g_vit.hungerSinceMin = 0;
    }

    g_vit.satiation = (uint8_t)sat;
    g_vit.energy    = (uint8_t)ene;
    g_vit.bond      = (uint8_t)bond;
    s_dirty = true;

    /* --- Kollaps ---
     * Lange genug bei Saettigung 0? Dann bricht das Beast zusammen. Es sei denn,
     * eine Phoenixfeder liegt im Rucksack: die verbrennt an seiner Stelle. */
    if (g_vit.hungerSinceMin != 0 && !vit_is_collapsed() &&
        (now - g_vit.hungerSinceMin) >= (uint32_t)VIT_COLLAPSE_H * 60) {

        if (inv_count(IT_FEDER) > 0) {
            inv_use(IT_FEDER);
            g_vit.satiation      = VIT_FEATHER_SAT;
            g_vit.hungerSinceMin = 0;
            s_featherEvent       = true;
            USBSerial.println("[VIT] Phoenixfeder verbrennt - der Kollaps ist abgewendet.");
        } else {
            g_vit.collapseUntil  = now + (uint32_t)VIT_RECOVER_H * 60;
            g_vit.satiation      = VIT_COLLAPSE_SAT;   /* nicht bei 0 aufwachen */
            g_vit.hungerSinceMin = 0;
            int b = (int)g_vit.bond - VIT_COLLAPSE_BOND;
            g_vit.bond = (uint8_t)(b < 0 ? 0 : b);
            s_collapseEvent = true;
            USBSerial.printf("[VIT] KOLLAPS. %d Stunden am Boden, Bindung -%d.\n",
                             VIT_RECOVER_H, VIT_COLLAPSE_BOND);
        }
        vit_save();
    }

    USBSerial.printf("[VIT] +%luh -> Saettigung %d  Energie %d  Bindung %d%s\n",
                     (unsigned long)hours, sat, ene, bond, night ? "  (Nacht)" : "");

    /* Hoechstens alle 30 Minuten in den Flash schreiben. */
    if (s_dirty && (now - s_lastSaveM) >= 30) vit_save();
}

void vit_flush(void) {
    if (s_dirty) vit_save();
}

/* ============================================================
 *  Aenderungen
 * ============================================================ */
static int clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

void vit_feed(int satPlus, int energyPlus, int bondPlus) {
    g_vit.satiation = (uint8_t)clamp100((int)g_vit.satiation + satPlus);
    g_vit.energy    = (uint8_t)clamp100((int)g_vit.energy    + energyPlus);
    g_vit.bond      = (uint8_t)clamp100((int)g_vit.bond      + bondPlus);
    if (g_vit.satiation > 0) g_vit.hungerSinceMin = 0;
    vit_save();
}

bool vit_spend_energy(int amount) {
    if ((int)g_vit.energy < amount) return false;
    g_vit.energy = (uint8_t)((int)g_vit.energy - amount);
    vit_save();
    return true;
}

void vit_add_bond(int delta) {
    int before = g_vit.bond;
    g_vit.bond = (uint8_t)clamp100(before + delta);
    if (g_vit.bond != before) {
        vit_save();      /* wird selten gerufen - direkt sichern ist billig */
        USBSerial.printf("[VIT] Bindung %d -> %d (%+d)\n", before, (int)g_vit.bond, delta);
    }
}

/* ============================================================
 *  Abfragen
 * ============================================================ */
bool vit_is_collapsed(void) {
    return g_vit.collapseUntil != 0 && shp_now_minutes() < g_vit.collapseUntil;
}

int vit_mood(void) {
    if (vit_is_collapsed())               return MOOD_KO;
    if (g_vit.satiation < 30)             return MOOD_HUNGRY;
    if (g_vit.energy    < 20)             return MOOD_TIRED;
    if (g_vit.bond      < 30)             return MOOD_SAD;
    return MOOD_HAPPY;
}

/* Wirkwert = Grundwert * (0,80 + 0,30 * Vital/100).
 * In Ganzzahlen: (80 + 30*v/100) / 100. */
int vit_mult_pct(int vital) {
    if (vital < 0)   vital = 0;
    if (vital > 100) vital = 100;
    return 80 + (30 * vital) / 100;
}

static int apply(int base, int vital) {
    int r = (base * vit_mult_pct(vital)) / 100;
    return r < 1 ? 1 : r;
}

Stats vit_effective_stats(Stats base) {
    Stats s = base;
    s.atk = apply(base.atk, g_vit.bond);        /* Bindung    -> ATK */
    s.def = apply(base.def, g_vit.satiation);   /* Saettigung -> DEF */
    s.spd = apply(base.spd, g_vit.energy);      /* Energie    -> SPD */
    s.hp  = base.hp;                            /* HP kommt allein aus dem Level */
    return s;
}

/* ============================================================
 *  Kollaps: Abfragen
 * ============================================================ */
int vit_recover_minutes(void) {
    if (!vit_is_collapsed()) return 0;
    uint32_t now = shp_now_minutes();
    return (g_vit.collapseUntil > now) ? (int)(g_vit.collapseUntil - now) : 0;
}

bool vit_take_collapse_event(void) {
    if (!s_collapseEvent) return false;
    s_collapseEvent = false;
    return true;
}

bool vit_take_feather_event(void) {
    if (!s_featherEvent) return false;
    s_featherEvent = false;
    return true;
}
