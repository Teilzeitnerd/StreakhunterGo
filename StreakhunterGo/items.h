/* ============================================================
 *  items — Rucksack: Futter, Equipment, Items
 *  ------------------------------------------------------------
 *  Zwoelf Items, drei Kategorien. Bewusst schlank: jedes Item hat einen
 *  klaren Zweck, keine Dubletten.
 *
 *  FUTTER      fuellt Saettigung (und damit DEF)
 *  EQUIPMENT   Random Roll auf ATK/DEF/SPD, haelt 5 Kaempfe, EIN Slot
 *  ITEMS       Einmal-Effekte
 *
 *  Speicherort: NVS-Blob "inv" auf dem Geraet. Der Umzug auf die Karte
 *  (/tama.shv) kommt, wenn das Beast zwischen Watch und Tama wandern soll.
 * ============================================================ */
#ifndef ITEMS_H
#define ITEMS_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* Reihenfolge NICHT aendern - sie ist der Index im gespeicherten Inventar. */
enum {
    /* --- Futter --- */
    IT_BEERE = 0,
    IT_FLEISCH,
    IT_KRAFTNUSS,
    IT_FESTMAHL,
    /* --- Equipment --- */
    IT_AMULETT,
    IT_PANZER,
    IT_FEDERN,
    IT_RING,
    /* --- Items --- */
    IT_SCHILD,
    IT_ROLLE,
    IT_FEDER,
    IT_ELIXIER,
    IT_COUNT
};

enum { CAT_FOOD = 0, CAT_EQUIP, CAT_ITEM, CAT_COUNT };

#define INV_MAX_STACK 9    /* je Item hoechstens so viele */

typedef struct {
    const char *name;
    uint8_t     cat;
    int8_t      sat;       /* Saettigung  */
    int8_t      ene;       /* Energie     */
    int8_t      bond;      /* Bindung     */
    const char *note;      /* kurze Wirkungsbeschreibung fuer die UI */
} ItemDef;

extern const ItemDef ITEMS[IT_COUNT];
extern uint8_t g_inv[IT_COUNT];

/* Icon zum Item. Fuer noch fehlende Grafiken liefert es einen Platzhalter -
 * dann steht im Namen trotzdem, worum es geht. */
const lv_image_dsc_t* item_icon(int id);
bool item_has_icon(int id);

void inv_begin(void);                 /* in setup() nach shp_init()          */
int  inv_add(int id, int n);          /* liefert die tatsaechlich zugefuegte Zahl */
bool inv_use(int id);                 /* benutzt EIN Stueck; false = keins da */
int  inv_count(int id);
int  inv_total(void);

#endif /* ITEMS_H */
