/* ============================================================
 *  assets.h (TAMA) — Grafik-Manifest (LVGL v9)
 *  ------------------------------------------------------------
 *  Bewusst abgespeckt gegenueber der Watch: nur die 18 Beast-Sprites.
 *  Sie sind aufloesungsunabhaengig und ziehen 1:1 um.
 *
 *  Noch NICHT drin:
 *    - oxanium_clock : die grossen Uhr-Ziffern braucht das Tama nicht.
 *
 *  Beast-Reihenfolge (beastId): 0=RUGA 1=PINTAN 2=FEARO
 *                               3=LEONA 4=LURCO 5=GRELO
 *  Stufen 1..3 (z.B. RUGA -> MARUGA -> JAMARUGA).
 * ============================================================ */
#ifndef ASSETS_H
#define ASSETS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_image_dsc_t img_ruga_1,   img_ruga_2,   img_ruga_3;
extern const lv_image_dsc_t img_pintan_1, img_pintan_2, img_pintan_3;
extern const lv_image_dsc_t img_fearo_1,  img_fearo_2,  img_fearo_3;
extern const lv_image_dsc_t img_leona_1,  img_leona_2,  img_leona_3;
extern const lv_image_dsc_t img_lurco_1,  img_lurco_2,  img_lurco_3;
extern const lv_image_dsc_t img_grelo_1,  img_grelo_2,  img_grelo_3;

/* --- Bosse (img_boss.c) --- */
extern const lv_image_dsc_t img_lunrak;   /* Schattenwolf */
extern const lv_image_dsc_t img_solvar;   /* Lichthirsch  */

/* --- Hintergruende (fuer 368x448 neu exportiert) --- */
extern const lv_image_dsc_t img_podest;     /* Home: Podest vor Burg/Bergen  */
extern const lv_image_dsc_t img_bg_kampf;   /* Arena: Hoehle, LEER (im Kampf) */
extern const lv_image_dsc_t img_bg_arena;   /* Arena: Hoehle mit Beasts (Menue)*/
extern const lv_image_dsc_t img_bg_ui;      /* allgemeiner UI-Hintergrund    */
extern const lv_image_dsc_t img_bg_adv;     /* Abenteuer: Persista-Karte     */

/* --- Icons (RGB565A8, mit Alphakanal) --- */
extern const lv_image_dsc_t ic_food, ic_adventure, ic_bag;                  /* 96x96 Kacheln  */
extern const lv_image_dsc_t mood_happy, mood_hungry, mood_tired,
                            mood_sad, mood_ko;                              /* 48x48 Blase    */
extern const lv_image_dsc_t it_beere, it_fleisch, it_kraftnuss, it_festmahl; /* 64x64 Futter  */
extern const lv_image_dsc_t eq_amulett, eq_panzer, eq_federn, eq_ring;      /* 64x64 Ausruest.*/
extern const lv_image_dsc_t it_schild, it_rolle, it_feder, it_elixier;      /* 64x64 Items    */

/* --- Schrift (Oxanium) --- */
extern const lv_font_t oxanium_16;
extern const lv_font_t oxanium_22;

#ifdef __cplusplus
}
#endif

/* Nachschlag: Sprite fuer (beastId, stufe 1..3). */
static inline const lv_image_dsc_t* asset_beast(int beastId, int stage) {
    static const lv_image_dsc_t* T[6][3] = {
        { &img_ruga_1,   &img_ruga_2,   &img_ruga_3   },
        { &img_pintan_1, &img_pintan_2, &img_pintan_3 },
        { &img_fearo_1,  &img_fearo_2,  &img_fearo_3  },
        { &img_leona_1,  &img_leona_2,  &img_leona_3  },
        { &img_lurco_1,  &img_lurco_2,  &img_lurco_3  },
        { &img_grelo_1,  &img_grelo_2,  &img_grelo_3  },
    };
    if (beastId < 0 || beastId > 5) beastId = 0;
    if (stage < 1) stage = 1;
    if (stage > 3) stage = 3;
    return T[beastId][stage - 1];
}

/* Nachschlag: Boss-Sprite (1 = LUNRAK, 2 = SOLVAR). */
static inline const lv_image_dsc_t* asset_boss(int which) {
    return (which == 2) ? &img_solvar : &img_lunrak;
}

#endif /* ASSETS_H */
