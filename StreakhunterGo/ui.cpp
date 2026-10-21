/* ============================================================
 *  ui.cpp (TAMA)
 *  ------------------------------------------------------------
 *  Sechs Seiten:
 *      PROFIL <- HOME -> QUEST -> ITEMS -> ABENTEUER -> ARENA
 *
 *  GESTEN:
 *  Der CST816 meldet die Fingeranzahl direkt (Register 0x02) - die .ino
 *  liest sie und liefert g_touchMaxDev (groesste Auslenkung waehrend des
 *  Drucks). Daran, nicht am Endpunkt, entscheidet sich Tipp oder Wisch.
 *  TP_INT taugt bei diesem Chip NICHT als Kontaktindikator (er pulst nur),
 *  im Gegensatz zum FT3168 der Watch.
 *
 *  Seitenwechsel ist ein HARTER Umschalter, kein Slide: bei 368x448
 *  Vollbild und einem 368x80-Teilpuffer waere eine Animation zaeh.
 * ============================================================ */
#include "ui.h"
#include <esp_random.h>   /* fuer die Huepf-Animation */
#include <Arduino.h>      /* millis() */
#include <lvgl.h>
#include "assets.h"
#include "game.h"
#include "streakhunter.h"
#include "sh_platform.h"
#include "vitals.h"
#include "items.h"
#include "adventure.h"
#include "equip.h"
#include "moves.h"
#include "loadout.h"
#include "adventure.h"

/* ============================================================
 *  DESIGN-TOKENS
 *  ------------------------------------------------------------
 *  Alle Masse und Farben stehen HIER. Wer eine Karte baut, greift auf diese
 *  Werte zu und erfindet keine eigenen - nur so bleibt die Oberflaeche aus
 *  einem Guss. Eine Zahl aendern heisst: sie aendert sich ueberall.
 * ============================================================ */

/* --- Farben --- */
#define COL_PAGE        lv_color_hex(0x000000)
#define COL_BOX         lv_color_hex(0x12111a)   /* Karten- und Popup-Grund */
#define COL_GOLD        lv_color_hex(0xecc40b)
#define COL_GOLD_BRIGHT lv_color_hex(0xf7d23a)
#define COL_TEXT        lv_color_hex(0xece8f3)
#define COL_DIM         lv_color_hex(0x8b86a0)
#define COL_TRACK       lv_color_hex(0x191722)   /* Balken-Hintergrund */
#define COL_BORDER      lv_color_hex(0x3b3850)   /* Rand im Ruhezustand */
#define COL_BUBBLE      lv_color_hex(0xf2ecdf)
#define COL_GOOD        lv_color_hex(0x4fbf6a)
#define COL_WARN        lv_color_hex(0xe0b23c)
#define COL_BAD         lv_color_hex(0xd8503f)

/* Quest-Kategorien - jede hat ihre Farbe, erkennbar ohne zu lesen. */
/* --- Beast-Typen ---
 * Ein sehr leichter Farbschleier ueber dem Podest, damit man den Typ auf einen
 * Blick sieht. Bewusst schwach: das Bild soll seine Stimmung behalten, der Typ
 * nur mitschwingen. */
#define COL_TYP_LIGHT   lv_color_hex(0xe8c85a)   /* RUGA   */
#define COL_TYP_FIRE    lv_color_hex(0xd85842)   /* PINTAN */
#define COL_TYP_SHADOW  lv_color_hex(0x9a6ad8)   /* FEARO, LURCO */
#define COL_TYP_WATER   lv_color_hex(0x4a92d8)   /* LEONA  */
#define COL_TYP_VENOM   lv_color_hex(0x2fb890)   /* GRELO - smaragd/tuerkis, nicht gelbstichig */
/* Typ-Schimmer: EIN Regler, Recolor auf dem Podest-Bild.
 *
 * Hier lag vorher zusaetzlich ein Vollbild-Objekt mit MULTIPLY-Blend darueber -
 * optisch schoener, aber es hat das Huepfen ruiniert: LVGL musste bei jeder
 * Bewegung des Beasts den ganzen betroffenen Bereich pixelweise in Software
 * nachmischen. Das war die Ursache des Ruckelns.
 *
 * Recolor kostet praktisch nichts, weil LVGL es BEIM ZEICHNEN des Bildes
 * anwendet und nicht als eigenen Durchgang darueber. Etwas hoeher eingestellt,
 * damit die Farbe den fehlenden Multiply-Anteil ausgleicht. Von 255. */
#define TYP_OPA         10

#define COL_KOERPER     lv_color_hex(0xe0674f)   /* warm, koerperlich */
#define COL_GEIST       lv_color_hex(0x5aa6e0)   /* kuehl, klar       */
#define COL_GEWOHNHEIT  lv_color_hex(0xb47ee0)   /* violett, stetig   */

/* --- Geometrie --- */
#define SAFE_X          26      /* runde Ecken (Radius ca. 60 px)      */
#define UI_CARD_W      316      /* = 368 - 2*SAFE_X. Die EINE Breite.  */
#define UI_CARD_H       68      /* Standardhoehe einer Zeilen-Karte    */
#define UI_GAP          12      /* Abstand zwischen Karten             */
#define UI_TOP          84      /* erste Karte unter dem Titel         */
#define UI_RADIUS       12      /* Karten                              */
#define UI_RADIUS_BOX   18      /* Popups                              */
#define UI_BORDER        2      /* Randstaerke, ueberall gleich        */
#define UI_PAD          14      /* Innenabstand in Karten              */
#define UI_TITLE_Y      32      /* Seitentitel                         */
#define UI_BAR_H        12      /* Balken                              */
#define POP_W          296      /* Popup-Breite, fuer alle drei gleich */

/* Zeile i einer Kartenliste. */
#define UI_ROW(i)      (UI_TOP + (i) * (UI_CARD_H + UI_GAP))

/* --- Hintergrund --- */
#define BG_DARKEN      LV_OPA_70   /* allgemeiner UI-Hintergrund */
#define ADV_DARKEN     LV_OPA_80   /* Persista-Karte: detailreicher, braucht mehr */

/* ============================================================
 *  HOME-Raster (eigene Buehne, folgt nicht dem Kartenraster)
 *
 *      28 ┌── Kopfzeile (Akku | Name | Uhr)
 *      58 └──
 *      72     LEVEL
 *     104     XP-Balken
 *     120     XP-Zahlen
 *     330     Fusspunkt des Beasts
 *     372     STREAK
 *     438     Seiten-Punkte
 * ============================================================ */
#define HEAD_Y          28
#define HEAD_H          30
#define LEVEL_Y         82      /* Level-Block etwas tiefer */
#define XPBAR_Y        114
#define XPBAR_W        210
#define XPBAR_H         10
#define XPTXT_Y        130

/* Beast-Sprite: 200x240-Leinwand, Beast sitzt UNTEN BUENDIG (RUGA fuellt nur
 * 78x116; nach oben waechst es bei der Evolution hinein). Der Fusspunkt ist
 * deshalb die Konstante - nur die Hoehe aendern. */
#define BEAST_FEET_Y   344      /* Fusspunkt tiefer */
#define BEAST_H        260      /* kleiner - entwickelte Beasts fuellen die
                                 * Leinwand staerker aus als RUGA */
#define BEAST_Y        (BEAST_FEET_Y - 224 - BEAST_H / 2)
#define HOP_PX           5
#define STREAK_Y       386      /* tiefer */
#define BUBBLE_ICON_H   22   /* kleiner - stand im Verhaeltnis zu gross */
#define DOT_Y          -10

/* --- Gesten ---
 * TAP_MAX deutlich unter SWIPE_MIN; was dazwischen liegt, wird verworfen. */
#define TAP_MAX         40
#define SWIPE_MIN       50
#define ARM_MS         250   /* Overlay reagiert erst nach dieser Ruhepause */
#define CARD_BLOCK_MS  450   /* Statuskarte: Sperre ab dem Oeffnen, siehe card_arm_tick */

extern int32_t g_swipeStartX, g_swipeStartY, g_swipeEndX, g_swipeEndY;
extern volatile bool     g_swipePending;
extern volatile int32_t  g_touchMaxDev;
extern volatile uint32_t g_touchLastMs;

static bool is_tap(void) { return g_touchMaxDev <= TAP_MAX; }

/* ---------- Seiten ---------- */
enum { PG_PROFIL = 0, PG_HOME, PG_QUEST, PG_ITEMS, PG_ADV, PG_ARENA, PG_COUNT };

static lv_obj_t *pages[PG_COUNT];
static lv_obj_t *dots[PG_COUNT];
static int       cur_page = PG_HOME;

/* ---------- Home ---------- */
static lv_obj_t *lb_batt, *lb_name, *lb_clock, *lb_level;
static lv_obj_t *bar_xp, *lb_xp, *im_beast, *lb_streak;
static lv_obj_t *bubble, *im_mood, *tail1, *tail2, *lb_hint;
static lv_obj_t *im_podest;   /* Startseiten-Hintergrund, faerbt sich nach Typ */
static lv_timer_t *hop_timer = NULL;

/* ---------- Statuskarte ---------- */
static lv_obj_t *card_root, *card_name, *card_lvl, *card_eq;

/* Loadout-Overlay (Deklaration frueh, weil ui_handle_swipe es nutzt). */
static lv_obj_t *lo_root, *lo_slot_btn[2], *lo_slot_lbl[2], *lo_slot_sub[2];
static lv_obj_t *lo_pick, *lo_pick_title;
static lv_obj_t *lo_pick_btn[MOVES_PER_TYPE], *lo_pick_lbl[MOVES_PER_TYPE];
static lv_obj_t *lo_pick_sub[MOVES_PER_TYPE], *lo_pick_lock[MOVES_PER_TYPE];
static bool      lo_armed = false;
static uint32_t  lo_openMs = 0;
static int       lo_editSlot = 0;      /* welcher Slot wird gerade belegt (1/2) */
static lv_obj_t *card_xpbar, *card_xp;
static lv_obj_t *card_val[4], *card_pct[4];
static bool      card_armed  = false;
static uint32_t  card_openMs = 0;    /* wann wurde die Karte geoeffnet? */

/* ---------- Item-Popup ---------- */
#define FOOD_VERB "verschlungen"
static lv_obj_t *pop_box;
static lv_obj_t *pop_root, *pop_icon, *pop_name, *pop_note, *pop_have;
static lv_obj_t *pop_btn, *pop_btn_lbl, *pop_msg;
static bool      pop_armed = false;
static int       pop_id    = -1;

/* ---------- Bestaetigungs-Popup (generisch) ---------- */
static lv_obj_t *cf_root, *cf_box, *cf_title, *cf_text, *cf_btn, *cf_btn_lbl;
static bool      cf_armed = false;
static void    (*cf_action)(int) = NULL;
static int       cf_arg = 0;
static bool      cf_done = false;            /* Aktion erledigt -> Knopf schliesst */
static uint32_t  cf_doneMs = 0;              /* Zeitpunkt des Umschaltens auf SCHLIESSEN */
static char      cf_success[96] = "";        /* Meldung nach der Aktion            */

/* ---------- Profil ---------- */
static lv_obj_t *vb[3], *vl[3], *pf_num[4], *pf_name[3];

/* ---------- Einstellungen (Wisch nach unten) ---------- */
static lv_obj_t *set_batt = NULL;
static lv_obj_t *set_root, *set_time, *set_slider;
static bool      set_armed = false;
static int       set_h = 0, set_m = 0;

/* ---------- Vorwaertsdeklarationen ---------- */
static void refresh_home(void);
static void refresh_profil(void);
static void refresh_quest(void);
static void refresh_items(void);
static void refresh_adv(void);
static void refresh_arena(void);
static void card_arm_tick(void);
static void card_open(void);
static void lo_arm_tick(void);
static void lo_open(void);
static void lo_close(void);
static void pop_arm_tick(void);
static void pop_close(void);
static void cf_arm_tick(void);
static void cf_close(void);
static void set_arm_tick(void);
static void set_close(void);
static void set_open(void);
static void set_show_batt(void);
static void cf_relayout(void);   /* Bestaetigungs-Popup neu vermessen */
static bool set_is_open(void);
static bool arena_busy(void);   /* laeuft gerade ein Kampf? */

/* ============================================================
 *  Bausteine — jede Seite benutzt DIESE, keine eigenen
 * ============================================================ */

/* Eine Karte im Standardmass. h = 0 nimmt UI_CARD_H. */
static lv_obj_t* ui_card(lv_obj_t *p, int y, int h) {
    lv_obj_t *c = lv_obj_create(p);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, UI_CARD_W, h ? h : UI_CARD_H);
    lv_obj_align(c, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(c, COL_BOX, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, UI_RADIUS, 0);
    lv_obj_set_style_border_color(c, COL_BORDER, 0);
    lv_obj_set_style_border_width(c, UI_BORDER, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

/* Ein Knopf ist eine Karte, die man antippen kann - gleicher Rand, gleiche
 * Breite, gleicher Radius. So wirkt nichts kleiner als sein Nachbar. */
static lv_obj_t* ui_button(lv_obj_t *p, int y, int h) {
    lv_obj_t *b = ui_card(p, y, h);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

static lv_obj_t* ui_label(lv_obj_t *p, const lv_font_t *f, lv_color_t c) {
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

static lv_obj_t* ui_title(lv_obj_t *p, const char *txt) {
    lv_obj_t *t = ui_label(p, &oxanium_22, COL_GOLD_BRIGHT);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, UI_TITLE_Y);
    lv_label_set_text(t, txt);
    return t;
}

static lv_obj_t* ui_bar(lv_obj_t *p, int w, int h, int y) {
    lv_obj_t *b = lv_bar_create(p);
    lv_obj_set_size(b, w, h);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(b, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(b, h / 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(b, COL_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(b, h / 2, LV_PART_INDICATOR);
    lv_bar_set_range(b, 0, 100);
    return b;
}

static lv_obj_t* dark_bg(lv_obj_t *p, const lv_image_dsc_t *img, lv_opa_t dark) {
    lv_obj_t *bg = lv_image_create(p);
    lv_image_set_src(bg, img);
    lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_image_recolor(bg, lv_color_black(), 0);
    lv_obj_set_style_image_recolor_opa(bg, dark, 0);
    return bg;
}

static void scale_to_height(lv_obj_t *img, const lv_image_dsc_t *d, int targetH) {
    lv_image_set_src(img, d);
    int nh = (int)d->header.h;
    lv_image_set_scale(img, nh > 0 ? (256 * targetH) / nh : 256);
}

static lv_color_t vital_color(int v) {
    if (v >= 60) return COL_GOOD;
    if (v >= 30) return COL_WARN;
    return COL_BAD;
}

static lv_color_t pct_color(int pct) {
    if (pct >= 100) return COL_GOOD;
    if (pct >=  92) return COL_WARN;
    return COL_BAD;
}

/* beastId: 0=RUGA 1=PINTAN 2=FEARO 3=LEONA 4=LURCO 5=GRELO */
static lv_color_t beast_type_color(int beastId) {
    switch (beastId) {
        case 0:  return COL_TYP_LIGHT;    /* Light  */
        case 1:  return COL_TYP_FIRE;     /* Fire   */
        case 2:  return COL_TYP_SHADOW;   /* Shadow */
        case 3:  return COL_TYP_WATER;    /* Water  */
        case 4:  return COL_TYP_SHADOW;   /* Shadow */
        case 5:  return COL_TYP_VENOM;    /* Venom  */
        default: return COL_TYP_LIGHT;
    }
}

static lv_color_t cat_color(int cat) {
    if (cat == SH_CAT_KOERPER) return COL_KOERPER;
    if (cat == SH_CAT_GEIST)   return COL_GEIST;
    return COL_GEWOHNHEIT;
}

/* Modaler Hintergrund + Box - gleiche Optik fuer alle drei Popups. */
static lv_obj_t* modal_box(lv_obj_t *scr, lv_obj_t **root, int h,
                           lv_event_cb_t onBgClick) {
    *root = lv_obj_create(scr);
    lv_obj_remove_style_all(*root);
    lv_obj_set_size(*root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(*root, COL_PAGE, 0);
    lv_obj_set_style_bg_opa(*root, LV_OPA_70, 0);
    lv_obj_remove_flag(*root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(*root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(*root, onBgClick, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(*root, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *box = lv_obj_create(*root);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, POP_W, h);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, COL_BOX, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, UI_RADIUS_BOX, 0);
    lv_obj_set_style_border_color(box, COL_GOLD, 0);
    lv_obj_set_style_border_width(box, UI_BORDER, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

/* ============================================================
 *  Seitenwechsel
 * ============================================================ */
static void show_page(int p) {
    if (p < 0 || p >= PG_COUNT) return;
    cur_page = p;

    for (int i = 0; i < PG_COUNT; i++) {
        if (i == p) lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(dots[i], i == p ? COL_GOLD_BRIGHT : COL_DIM, 0);
        lv_obj_set_style_bg_opa(dots[i], i == p ? LV_OPA_COVER : LV_OPA_40, 0);
    }
    if (p == PG_HOME)   refresh_home();
    if (p == PG_PROFIL) refresh_profil();
    if (p == PG_QUEST)  refresh_quest();
    if (p == PG_ITEMS)  refresh_items();
    if (p == PG_ADV)    refresh_adv();
    if (p == PG_ARENA)  refresh_arena();
}

void ui_handle_swipe(void) {
    card_arm_tick();
    pop_arm_tick();
    cf_arm_tick();
    set_arm_tick();
    lo_arm_tick();

    if (!g_swipePending) return;
    g_swipePending = false;

    /* Einstellungen offen: Wischen (egal wohin) schliesst sie. */
    /* Einstellungen: NUR ein echter Wisch schliesst - kein Tipp.
     * g_swipePending wird bei jedem Fingerabheben gesetzt, also auch beim
     * Tippen auf einen der Knoepfe. Genau das hat einen bisher staendig
     * rausgeworfen. Deshalb hier die Strecke pruefen, nicht nur das Flag. */
    if (set_is_open()) {
        int sdx = (int)(g_swipeEndX - g_swipeStartX);
        int sdy = (int)(g_swipeEndY - g_swipeStartY);
        int sadx = sdx < 0 ? -sdx : sdx;
        int sady = sdy < 0 ? -sdy : sdy;
        bool wisch = (sady >= SWIPE_MIN && sady > sadx * 2);   /* deutlich vertikal */
        if (set_armed && wisch) set_close();
        return;
    }

    /* Offene Overlays: Wischen schliesst sie, statt die Seite zu wechseln. */
    if (cf_root && !lv_obj_has_flag(cf_root, LV_OBJ_FLAG_HIDDEN)) {
        if (cf_armed) cf_close();
        return;
    }
    if (pop_root && !lv_obj_has_flag(pop_root, LV_OBJ_FLAG_HIDDEN)) {
        if (pop_armed) pop_close();
        return;
    }
    if (card_root && !lv_obj_has_flag(card_root, LV_OBJ_FLAG_HIDDEN)) {
        if (card_armed) lv_obj_add_flag(card_root, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (lo_root && !lv_obj_has_flag(lo_root, LV_OBJ_FLAG_HIDDEN)) {
        /* Auswahl offen -> zurueck zu den Slots; sonst Overlay zu. */
        if (lo_armed) {
            if (!lv_obj_has_flag(lo_pick, LV_OBJ_FLAG_HIDDEN))
                lv_obj_add_flag(lo_pick, LV_OBJ_FLAG_HIDDEN);
            else
                lo_close();
        }
        return;
    }

    int dx  = (int)(g_swipeEndX - g_swipeStartX);
    int dy  = (int)(g_swipeEndY - g_swipeStartY);
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;

    /* Eindeutig nach UNTEN -> Einstellungen. Im Arena-Kampf nicht, dort ist
     * jeder Tipp ein Schlag und ein Wisch waere ein Fehlgriff. */
    if (dy >= SWIPE_MIN && ady > adx * 2) {
        if (!arena_busy()) set_open();
        return;
    }

    /* Eindeutig nach OBEN -> Statuskarte. Nur auf der Home-Seite, wo das Beast
     * steht, und nur wenn eine Karte steckt. */
    if (dy <= -SWIPE_MIN && ady > adx * 2) {
        if (cur_page == PG_HOME && !arena_busy()) card_open();
        return;
    }

    if (adx < SWIPE_MIN || adx < ady * 2) return;   /* nicht eindeutig horizontal */
    show_page(dx < 0 ? cur_page + 1 : cur_page - 1);
}

/* ============================================================
 *  Statuskarte (Tippen aufs Beast) — nur Kampfwerte.
 *  Die Vitalwerte stehen im PROFIL; hier nur ihre WIRKUNG in Prozent.
 * ============================================================ */
static void card_close(lv_event_t *e) {
    LV_UNUSED(e);
    if (!card_armed) return;
    lv_obj_add_flag(card_root, LV_OBJ_FLAG_HIDDEN);
}

/* Overlays reagieren erst, wenn der Finger WIRKLICH weg war.
 * Der CST816 pulst beim Aufsetzen UND beim Abheben - ohne diese Ruhepause
 * verbucht ein frisch geoeffnetes Overlay das Abheben als eigenen Klick. */
static void card_arm_tick(void) {
    if (card_armed || !card_root) return;
    if (lv_obj_has_flag(card_root, LV_OBJ_FLAG_HIDDEN)) return;

    /* Zwei Bedingungen, nicht eine.
     *
     * Warum ausgerechnet diese Karte zickte: Ihre Tap-Flaeche (podest_tap,
     * 240x200, bildschirmmittig) liegt GENAU unter der Box. Bei den anderen
     * Overlays sitzt der Ausloeser - eine Kachel, eine Quest-Karte - woanders.
     * Der CST816 pulst beim Aufsetzen UND beim Abheben; der zweite Puls landete
     * hier also direkt auf dem frisch geoeffneten Overlay und verbuchte sich als
     * Schliess-Klick.
     *
     * Die Ruhepause allein reichte nicht, weil sie ab dem letzten Touch-Ereignis
     * zaehlt - und das lag beim Oeffnen schon in der Vergangenheit. Deshalb
     * zusaetzlich eine Sperre ab dem OEFFNEN, die unabhaengig vom Touch laeuft. */
    if (millis() - card_openMs  <  CARD_BLOCK_MS) return;
    if (millis() - g_touchLastMs >= ARM_MS) card_armed = true;
}

/* Auch aufrufbar, WAEHREND das Beast unterwegs ist - sonst saehe man in seiner
 * Abwesenheit nicht, ob Quests und Claim XP gebracht haben. */
static void card_open(void) {
    if (!game_has_beast()) return;
    /* Dieselbe Kette wie im Kampf: Grundwert -> Vitalwerte -> Ausruestung. */
    Stats eff = equip_apply(vit_effective_stats(game_player_stats()));

    lv_label_set_text(card_name, game_player_name());
    lv_obj_align(card_name, LV_ALIGN_TOP_MID, 0, 20);
    lv_label_set_text_fmt(card_lvl, "LEVEL %d", (int)g_state.level);
    lv_obj_align(card_lvl, LV_ALIGN_TOP_MID, 0, 52);

    /* XP: derselbe Wert wie frueher auf dem Homescreen. */
    int into = game_xp_into_level(), span = game_xp_span();
    lv_bar_set_value(card_xpbar, span > 0 ? (into * 100) / span : 0, LV_ANIM_OFF);
    lv_label_set_text_fmt(card_xp, "%d / %d XP", into, span);
    lv_obj_align(card_xp, LV_ALIGN_TOP_MID, 0, 102);

    const int val[4] = { eff.atk, eff.def, eff.spd, eff.hp };
    const int pct[4] = { vit_mult_pct(g_vit.bond),
                         vit_mult_pct(g_vit.satiation),
                         vit_mult_pct(g_vit.energy), 0 };

    for (int i = 0; i < 4; i++) {
        int y = 148 + i * 34;
        lv_label_set_text_fmt(card_val[i], "%d", val[i]);
        lv_obj_align(card_val[i], LV_ALIGN_TOP_LEFT, 104, y - 4);

        if (i == 3) {
            lv_label_set_text(card_pct[i], "");      /* HP haengt an keinem Vitalwert */
        } else {
            lv_label_set_text_fmt(card_pct[i], "%d%%", pct[i]);
            lv_obj_set_style_text_color(card_pct[i], pct_color(pct[i]), 0);
        }
        lv_obj_align(card_pct[i], LV_ALIGN_TOP_RIGHT, -28, y);
    }

    /* Ausruestung: sie veraendert genau diese Werte, also steht sie hier. */
    if (equip_active())
        lv_label_set_text_fmt(card_eq, "%s  +%d  ·  noch %d Kämpfe",
                              equip_name(), equip_bonus(), equip_fights_left());
    else
        lv_label_set_text(card_eq, "Nichts angelegt");
    lv_obj_align(card_eq, LV_ALIGN_BOTTOM_MID, 0, -14);

    card_armed  = false;
    card_openMs = millis();
    lv_obj_remove_flag(card_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(card_root);
}

static void build_card(lv_obj_t *scr) {
    lv_obj_t *box = modal_box(scr, &card_root, 350, card_close);

    card_name = ui_label(box, &oxanium_22, COL_GOLD_BRIGHT);
    card_lvl  = ui_label(box, &oxanium_16, COL_DIM);

    /* XP-Leiste - vom Homescreen hierher gewandert. */
    card_xpbar = ui_bar(box, POP_W - 2 * 34, 8, 84);
    lv_obj_set_style_bg_color(card_xpbar, COL_GOLD, LV_PART_INDICATOR);
    card_xp = ui_label(box, &oxanium_16, COL_DIM);
    lv_obj_set_style_text_align(card_xp, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *sep = lv_obj_create(box);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, POP_W - 2 * 34, 1);
    lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 128);
    lv_obj_set_style_bg_color(sep, COL_BORDER, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    static const char *KEY[4] = { "ATK", "DEF", "SPD", "HP" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *k = ui_label(box, &oxanium_16, COL_DIM);
        lv_obj_align(k, LV_ALIGN_TOP_LEFT, 28, 148 + i * 34);
        lv_label_set_text(k, KEY[i]);

        card_val[i] = ui_label(box, &oxanium_22, COL_TEXT);
        card_pct[i] = ui_label(box, &oxanium_16, COL_TEXT);
    }

    card_eq = ui_label(box, &oxanium_16, COL_GOLD_BRIGHT);
    lv_obj_set_style_text_align(card_eq, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(card_eq, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(card_eq, POP_W - 2 * 20);
    lv_obj_align(card_eq, LV_ALIGN_BOTTOM_MID, 0, -14);
}

/* ============================================================
 *  Bestaetigungs-Popup (generisch)
 *  Eine Quest abzuhaken ist unwiderruflich - immer erst nachfragen.
 * ============================================================ */
static void cf_close(void) {
    lv_obj_add_flag(cf_root, LV_OBJ_FLAG_HIDDEN);
    cf_action = NULL;
}

static void cf_bg_clicked(lv_event_t *e) {
    /* Nur schliessen, wenn der Hintergrund SELBST getroffen wurde. Ein Klick auf
     * den Knopf bubbelt zwar hierher, aber sein Target ist der Knopf - den
     * lassen wir cf_ok_clicked behandeln. Das war die Ursache fuer das sofortige
     * Schliessen. */
    if (lv_event_get_target(e) != cf_root) return;
    if (cf_armed) cf_close();
}

static void cf_ok_clicked(lv_event_t *e) {
    LV_UNUSED(e);
    if (!cf_armed || !is_tap()) return;

    /* Zweiter Klick (Knopf heisst jetzt SCHLIESSEN) -> nur zumachen.
     * Aber erst, wenn seit dem Umschalten eine kurze Sperre vergangen ist -
     * sonst schliesst derselbe Fingerkontakt das Popup sofort wieder. */
    if (cf_done) {
        if (millis() - cf_doneMs < ARM_MS) return;
        cf_close();
        return;
    }
    if (!cf_action) return;

    /* Ohne hinterlegten Erfolgstext (z.B. Abenteuer, wo der Screen sofort
     * wechselt) verhaelt es sich wie frueher: Aktion, dann zu. */
    if (cf_success[0] == '\0') {
        void (*fn)(int) = cf_action;
        int arg = cf_arg;
        cf_close();
        fn(arg);
        return;
    }

    cf_action(cf_arg);          /* Tag eintragen / Quest abhaken */
    cf_done = true;
    cf_doneMs = millis();       /* Sperre gegen den durchschlagenden Klick */
    cf_armed = false;           /* Knopf muss neu scharf werden (cf_arm_tick) */

    /* Erfolgsmeldung an die Stelle des Fragetexts, Knopf wird neutral. */
    lv_label_set_text(cf_text, cf_success);
    lv_obj_set_style_text_color(cf_text, COL_GOOD, 0);
    cf_relayout();              /* Box an die neue Textlaenge anpassen */

    lv_label_set_text(cf_btn_lbl, "SCHLIESSEN");
    lv_obj_set_style_bg_color(cf_btn, COL_BOX, 0);
    lv_obj_set_style_text_color(cf_btn_lbl, COL_TEXT, 0);
    lv_obj_center(cf_btn_lbl);
}

static void cf_arm_tick(void) {
    if (cf_armed || !cf_root) return;
    if (lv_obj_has_flag(cf_root, LV_OBJ_FLAG_HIDDEN)) return;
    if (millis() - g_touchLastMs >= ARM_MS) cf_armed = true;
}

/* accent faerbt Rand, Titel und Knopf - so traegt das Popup die Farbe der
 * Kategorie, aus der es aufgerufen wurde.
 *
 * Die Box waechst MIT DEM TEXT. Vorher war sie fest 220 hoch; der laengere
 * Abenteuer-Text lief deshalb unter den Knopf. Jetzt wird die Hoehe aus der
 * tatsaechlichen Texthoehe berechnet, und der Knopf sitzt immer darunter. */
#define CF_TEXT_Y   64      /* Oberkante des Textblocks           */
#define CF_BTN_H    50
#define CF_BOT_PAD  16      /* Luft unter dem Knopf               */
#define CF_TXT_GAP  24      /* Luft zwischen Text und Knopf       */

/* Popup an seinen aktuellen Text anpassen. Wird beim Oeffnen gebraucht UND
 * nachdem die Erfolgsmeldung den Fragetext ersetzt hat - sonst behaelt die Box
 * die Hoehe des alten Textes, und eine laengere gruene Meldung klebt am Knopf. */
static void cf_relayout(void) {
    /* Erst den Text setzen lassen, DANN seine Hoehe abfragen - sonst misst man
     * den alten Inhalt. */
    lv_obj_update_layout(cf_text);
    int th = lv_obj_get_height(cf_text);
    if (th < 24) th = 24;

    int h = CF_TEXT_Y + th + CF_TXT_GAP + CF_BTN_H + CF_BOT_PAD;
    if (h < 230) h = 230;          /* kurze Popups bleiben kompakt */
    if (h > 400) h = 400;

    lv_obj_set_height(cf_box, h);
    lv_obj_center(cf_box);
    lv_obj_align(cf_title, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_align(cf_text,  LV_ALIGN_TOP_MID, 0, CF_TEXT_Y);
}

static void cf_open(const char *title, const char *text, const char *btn,
                    lv_color_t accent, void (*action)(int), int arg,
                    const char *success) {
    cf_armed  = false;
    cf_action = action;
    cf_arg    = arg;
    cf_done   = false;
    snprintf(cf_success, sizeof(cf_success), "%s", success ? success : "");

    lv_obj_set_style_border_color(cf_box, accent, 0);
    lv_obj_set_style_text_color(cf_title, accent, 0);
    lv_obj_set_style_bg_color(cf_btn, accent, 0);

    lv_label_set_text(cf_title, title);
    lv_label_set_text(cf_text, text);
    lv_obj_set_style_text_color(cf_text, COL_TEXT, 0);   /* nach Erfolg wieder normal */
    lv_label_set_text(cf_btn_lbl, btn);
    lv_obj_set_style_text_color(cf_btn_lbl, lv_color_black(), 0);

    cf_relayout();

    lv_obj_remove_flag(cf_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(cf_root);
}

static void build_confirm(lv_obj_t *scr) {
    cf_box = modal_box(scr, &cf_root, 220, cf_bg_clicked);

    cf_title = ui_label(cf_box, &oxanium_22, COL_GOLD_BRIGHT);

    cf_text = ui_label(cf_box, &oxanium_16, COL_TEXT);
    lv_obj_set_style_text_align(cf_text, LV_TEXT_ALIGN_CENTER, 0);
    /* Mehrzeilige Meldungen klebten sonst aneinander. */
    lv_obj_set_style_text_line_space(cf_text, 7, 0);
    lv_label_set_long_mode(cf_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cf_text, POP_W - 2 * 28);

    cf_btn = lv_obj_create(cf_box);
    lv_obj_remove_style_all(cf_btn);
    lv_obj_set_size(cf_btn, POP_W - 2 * 28, CF_BTN_H);
    lv_obj_align(cf_btn, LV_ALIGN_BOTTOM_MID, 0, -CF_BOT_PAD);
    lv_obj_set_style_radius(cf_btn, UI_RADIUS, 0);
    lv_obj_set_style_bg_opa(cf_btn, LV_OPA_COVER, 0);
    lv_obj_remove_flag(cf_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cf_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cf_btn, cf_ok_clicked, LV_EVENT_CLICKED, NULL);

    cf_btn_lbl = ui_label(cf_btn, &oxanium_22, lv_color_black());
    lv_obj_center(cf_btn_lbl);
}

/* ============================================================
 *  HOME
 * ============================================================ */
/* --- Huepfen ---
 * Vorher: immer 5 px hoch, immer im selben Takt. Das wirkte wie ein Metronom,
 * nicht wie ein Lebewesen.
 *
 * Jetzt bekommt jeder Sprung eine eigene Hoehe, eine eigene Richtung und eine
 * eigene Dauer, und die Pausen dazwischen sind ungleich lang. Zwei Animationen
 * laufen parallel - eine fuer X, eine fuer Y - und weil beide mit playback
 * zurueckfedern, landet das Beast immer wieder an seinem Platz. Aus der
 * Ueberlagerung entsteht der Bogen: schraeg hoch, schraeg runter.
 *
 * Dazu zwei Sorten Bewegung im Wechsel: der richtige Sprung und das kleine
 * Wippen. Erst dieser Rhythmus laesst es lebendig aussehen - gleichfoermige
 * Zufallssprunge sind auch nur ein Metronom mit Streuung. */
/* Verschoben wird mit TRANSLATE, nicht mit lv_obj_align().
 *
 * Das ist der Grund, warum es vorher ruckelte: align() rechnet bei JEDEM Frame
 * das Layout neu und invalidiert den Elternbereich. translate dagegen ist eine
 * reine Zeichen-Verschiebung - das Layout bleibt unberuehrt, LVGL zeichnet nur
 * das Sprite an neuer Stelle. Deutlich billiger, und genau dafuer ist es da. */
static void hop_x_cb(void *o, int32_t v) {
    lv_obj_set_style_translate_x((lv_obj_t *)o, v, 0);
}
static void hop_y_cb(void *o, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)o, v, 0);
}

static void hop_cb(lv_timer_t *t) {
    if (cur_page != PG_HOME || !game_has_beast()) return;
    if (lv_obj_has_flag(im_beast, LV_OBJ_FLAG_HIDDEN)) return;

    /* Im Kollaps liegt das Beast am Boden. Es huepft nicht. */
    if (vit_is_collapsed()) {
        lv_anim_delete(im_beast, hop_x_cb);
        lv_anim_delete(im_beast, hop_y_cb);
        lv_obj_set_style_translate_x(im_beast, 0, 0);
        lv_obj_set_style_translate_y(im_beast, 0, 0);
        lv_timer_set_period(t, 1200);
        return;
    }

    /* Drei Sorten Bewegung. Der Rhythmus macht das Lebendige - gleichfoermige
     * Zufallssprunge waeren auch nur ein Metronom mit Streuung.
     *
     * Nach UNTEN heisst: in die Knie gehen. Das Beast steht auf einem Podest, es
     * kann nicht hineinsinken - ein kurzes Ducken sieht aus wie Anspannung, wie
     * das Ausholen vor einem Sprung. Deshalb faellt es flacher aus als das
     * Springen und federt langsamer zurueck. */
    int r = (int)(esp_random() % 100);
    int dy, dx, dur;

    if (r < 55) {                 /* Sprung nach oben, oft schraeg */
        dy  = -(7 + (int)(esp_random() % 11));        /*  -7 .. -17 */
        dx  = (int)(esp_random() % 23) - 11;          /* -11 .. +11 */
        dur = 160 + (int)(esp_random() % 110);
    } else if (r < 80) {          /* Ducken, auch schraeg nach unten */
        dy  = 3 + (int)(esp_random() % 6);            /*  +3 .. +8  */
        dx  = (int)(esp_random() % 17) - 8;           /*  -8 .. +8  */
        dur = 230 + (int)(esp_random() % 140);        /* traeger    */
    } else {                      /* nur wippen, kaum Hoehe */
        dy  = (int)(esp_random() % 7) - 3;            /*  -3 .. +3  */
        dx  = (int)(esp_random() % 13) - 6;           /*  -6 .. +6  */
        dur = 260 + (int)(esp_random() % 180);
    }

    lv_anim_t a;

    if (dy != 0) {
        lv_anim_init(&a);
        lv_anim_set_var(&a, im_beast);
        lv_anim_set_exec_cb(&a, hop_y_cb);
        lv_anim_set_values(&a, 0, dy);
        lv_anim_set_duration(&a, dur);
        lv_anim_set_playback_duration(&a, dur + 70);  /* zurueck dauert laenger */
        lv_anim_set_path_cb(&a, dy < 0 ? lv_anim_path_ease_out    /* Sprung: schnell weg */
                                       : lv_anim_path_ease_in_out); /* Ducken: weich     */
        lv_anim_start(&a);
    }

    if (dx != 0) {
        lv_anim_init(&a);
        lv_anim_set_var(&a, im_beast);
        lv_anim_set_exec_cb(&a, hop_x_cb);
        lv_anim_set_values(&a, 0, dx);
        lv_anim_set_duration(&a, dur);
        lv_anim_set_playback_duration(&a, dur + 70);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }

    /* Ungleiche Pausen, und ab und zu kommt gleich der naechste hinterher. */
    uint32_t pause = (r < 55) ? 700 + (esp_random() % 2300)
                              : 450 + (esp_random() % 1500);
    if ((esp_random() % 100) < 18) pause = 260 + (esp_random() % 220);

    lv_timer_set_period(t, pause);
}

static void beast_clicked(lv_event_t *e) {
    LV_UNUSED(e);
    /* Frueher oeffnete ein Tap hier die Statuskarte. Das kollidierte staendig
     * mit Wischgesten. Die Karte kommt jetzt per Wisch nach OBEN (siehe
     * ui_handle_swipe) - ein Tap aufs Beast tut bewusst nichts mehr. */
}

static void build_home(lv_obj_t *p) {
    /* Das Podest wird je nach Beast-Typ leicht eingefaerbt (siehe refresh_home).
     * Recolor auf dem Bild selbst ist billiger als ein zweites Objekt darueber
     * und faerbt Vordergrund wie Schatten gleichmaessig. */
    im_podest = lv_image_create(p);
    lv_image_set_src(im_podest, &img_podest);
    lv_obj_align(im_podest, LV_ALIGN_CENTER, 0, 0);


    im_beast = lv_image_create(p);
    lv_obj_align(im_beast, LV_ALIGN_CENTER, 0, BEAST_Y);
    lv_obj_add_flag(im_beast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(im_beast, beast_clicked, LV_EVENT_CLICKED, NULL);

    /* Antippzone fuers leere Podest: das Beast ist unterwegs, die Statuskarte
     * soll trotzdem aufgehen. */
    lv_obj_t *podest_tap = lv_obj_create(p);
    lv_obj_remove_style_all(podest_tap);
    lv_obj_set_size(podest_tap, 240, 200);
    lv_obj_align(podest_tap, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_opa(podest_tap, LV_OPA_0, 0);
    lv_obj_remove_flag(podest_tap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(podest_tap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(podest_tap, beast_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_move_background(podest_tap);

    bubble = lv_obj_create(p);
    lv_obj_remove_style_all(bubble);
    lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(bubble, COL_BUBBLE, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bubble, 10, 0);
    lv_obj_set_style_pad_all(bubble, 4, 0);        /* kleiner - Icon bleibt gleich */
    lv_obj_remove_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(bubble, LV_ALIGN_CENTER, 104, -40);   /* weiter rechts, raus aus dem Sprite */

    im_mood = lv_image_create(bubble);
    lv_image_set_src(im_mood, &mood_happy);
    lv_image_set_scale(im_mood, (256 * BUBBLE_ICON_H) / 48);
    lv_obj_center(im_mood);

    /* Zipfel: zwei versetzte Quadrate unten links (Pixel-Art-Schwanz) */
    tail1 = lv_obj_create(p);
    lv_obj_remove_style_all(tail1);
    lv_obj_set_size(tail1, 10, 10);
    lv_obj_set_style_bg_color(tail1, COL_BUBBLE, 0);
    lv_obj_set_style_bg_opa(tail1, LV_OPA_COVER, 0);

    tail2 = lv_obj_create(p);
    lv_obj_remove_style_all(tail2);
    lv_obj_set_size(tail2, 6, 6);
    lv_obj_set_style_bg_color(tail2, COL_BUBBLE, 0);
    lv_obj_set_style_bg_opa(tail2, LV_OPA_COVER, 0);

    /* Kopfzeile: Container mit drei Kindern -> garantiert auf einer Linie */
    lv_obj_t *head = lv_obj_create(p);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, UI_CARD_W, HEAD_H);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, HEAD_Y);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lb_batt = lv_label_create(head);        /* Standard-Font: hat die Symbole */
    lv_obj_set_style_text_color(lb_batt, COL_TEXT, 0);
    lv_label_set_text(lb_batt, "");

    lb_name  = ui_label(head, &oxanium_22, COL_GOLD_BRIGHT);
    lb_clock = ui_label(head, &oxanium_16, COL_TEXT);
    lv_label_set_text(lb_clock, "--:--");

    lb_level = ui_label(p, &oxanium_22, COL_TEXT);

    bar_xp = ui_bar(p, XPBAR_W, XPBAR_H, XPBAR_Y);
    lv_obj_set_style_bg_color(bar_xp, COL_GOLD, LV_PART_INDICATOR);

    lb_xp = ui_label(p, &oxanium_16, COL_TEXT);

    /* STREAK: gleiche Schrift und Farbe wie der Beast-Name oben. */
    lb_streak = ui_label(p, &oxanium_22, COL_GOLD_BRIGHT);

    lb_hint = ui_label(p, &oxanium_22, COL_TEXT);
    lv_obj_align(lb_hint, LV_ALIGN_CENTER, 0, 10);
    lv_label_set_text(lb_hint, "Karte einlegen");
    lv_obj_add_flag(lb_hint, LV_OBJ_FLAG_HIDDEN);

    if (!hop_timer) hop_timer = lv_timer_create(hop_cb, 1600, NULL);
}

static void refresh_home(void) {
    int h = 0, m = 0;
    shp_clock(&h, &m);
    lv_label_set_text_fmt(lb_clock, "%02d:%02d", h, m);
    lv_obj_align(lb_clock, LV_ALIGN_RIGHT_MID, 0, 0);

    int pct = shp_battery_percent();
    if (pct < 0) {
        lv_label_set_text(lb_batt, "");
    } else if (shp_is_charging()) {
        lv_label_set_text_fmt(lb_batt, LV_SYMBOL_CHARGE " %d%%", pct);
    } else {
        const char *sym = (pct > 80) ? LV_SYMBOL_BATTERY_FULL
                        : (pct > 60) ? LV_SYMBOL_BATTERY_3
                        : (pct > 40) ? LV_SYMBOL_BATTERY_2
                        : (pct > 15) ? LV_SYMBOL_BATTERY_1
                                     : LV_SYMBOL_BATTERY_EMPTY;
        lv_label_set_text_fmt(lb_batt, "%s %d%%", sym, pct);
    }
    lv_obj_align(lb_batt, LV_ALIGN_LEFT_MID, 0, 0);

    /* Typ-Schimmer: gilt auch, waehrend das Beast unterwegs ist - es bleibt ja
     * dasselbe Beast. Nur ohne Karte faellt er weg. */
    if (game_has_beast()) {
        lv_obj_set_style_image_recolor(im_podest, beast_type_color(g_state.beastId), 0);
        lv_obj_set_style_image_recolor_opa(im_podest, TYP_OPA, 0);
    } else {
        lv_obj_set_style_image_recolor_opa(im_podest, LV_OPA_0, 0);
    }

    if (game_has_beast()) {
        bool away = adv_is_away();

        /* Das Beast wird IMMER gezeichnet, solange eine Karte steckt - auch
         * unterwegs. Dann aber stark abgeblendet, damit man sieht: es ist da,
         * nur eben nicht hier. Darueber liegt der "Unterwegs"-Hinweis. */
        scale_to_height(im_beast, asset_beast(g_state.beastId, game_player_stage()), BEAST_H);
        lv_obj_align(im_beast, LV_ALIGN_CENTER, 0, BEAST_Y);
        lv_obj_set_style_image_opa(im_beast, away ? 60 : LV_OPA_COVER, 0);
        lv_obj_remove_flag(im_beast, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text(lb_name, game_player_name());
        lv_label_set_text_fmt(lb_level, "LEVEL: %d", (int)g_state.level);

        /* XP-Leiste und -Zahl leben jetzt in der Statuskarte, nicht mehr hier -
         * der Homescreen bleibt so ruhiger. */
        lv_obj_add_flag(bar_xp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lb_xp,  LV_OBJ_FLAG_HIDDEN);

        if (away) {
            /* Sprechblase weg, stattdessen der Countdown. */
            lv_obj_add_flag(bubble, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(tail1,  LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(tail2,  LV_OBJ_FLAG_HIDDEN);
            int left = adv_minutes_left();
            lv_label_set_text_fmt(lb_hint, "Unterwegs\nnoch %d:%02d Std.", left / 60, left % 60);
            lv_obj_set_style_text_align(lb_hint, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_remove_flag(lb_hint, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(bubble, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(tail1,  LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(tail2,  LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lb_hint,   LV_OBJ_FLAG_HIDDEN);

            const lv_image_dsc_t *mi;
            switch (vit_mood()) {
                case MOOD_HUNGRY: mi = &mood_hungry; break;
                case MOOD_TIRED:  mi = &mood_tired;  break;
                case MOOD_SAD:    mi = &mood_sad;    break;
                case MOOD_KO:     mi = &mood_ko;     break;
                default:          mi = &mood_happy;  break;
            }
            lv_image_set_src(im_mood, mi);
        }

        if (vit_is_collapsed()) {
            int m = vit_recover_minutes();
            lv_label_set_text_fmt(lb_streak, "BEWUSSTLOS  ·  noch %dh %02dm", m / 60, m % 60);
            lv_obj_set_style_text_color(lb_streak, COL_BAD, 0);
        } else {
            lv_label_set_text_fmt(lb_streak, "STREAK: %d", (int)g_state.streak);
            lv_obj_set_style_text_color(lb_streak, COL_GOLD_BRIGHT, 0);
        }
    } else {
        lv_obj_add_flag(im_beast, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bubble,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(tail1,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(tail2,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bar_xp,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lb_xp,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lb_hint, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text(lb_hint, "Karte einlegen");
        lv_label_set_text(lb_name, "KEIN BEAST");
        lv_label_set_text(lb_level, "");
        lv_label_set_text(lb_streak, "");
    }

    lv_obj_align(lb_name,   LV_ALIGN_CENTER,  0, 0);       /* im Kopfzeilen-Container */
    lv_obj_align(lb_level,  LV_ALIGN_TOP_MID, 0, LEVEL_Y);
    lv_obj_align(lb_xp,     LV_ALIGN_TOP_MID, 0, XPTXT_Y);
    lv_obj_align(lb_streak, LV_ALIGN_TOP_MID, 0, STREAK_Y);

    /* Die Blase ist LV_SIZE_CONTENT - der Zipfel muss ihr folgen. */
    lv_obj_align_to(tail1, bubble, LV_ALIGN_OUT_BOTTOM_LEFT,  9, -4);
    lv_obj_align_to(tail2, bubble, LV_ALIGN_OUT_BOTTOM_LEFT,  4,  5);
}

/* ============================================================
 *  PROFIL — die drei Vitalwerte plus Bilanz.
 *  Bewusst OHNE Kampfwerte: die stehen in der Statuskarte.
 * ============================================================ */
#define PF_ROW_H    66
#define PF_GRID_Y  300
#define PF_GRID_DY  54

static void build_profil(lv_obj_t *p) {
    dark_bg(p, &img_bg_ui, BG_DARKEN);
    ui_title(p, "PROFIL");

    /* Nur Name und Wert. Welchen Kampfwert das beeinflusst und um wie viel
     * Prozent, steht in der Statuskarte - hier waere es doppelt. */
    static const char *NAME[3] = { "Sättigung", "Energie", "Bindung" };

    for (int i = 0; i < 3; i++) {
        int y = UI_TOP + i * PF_ROW_H;

        pf_name[i] = ui_label(p, &oxanium_16, COL_TEXT);
        lv_obj_align(pf_name[i], LV_ALIGN_TOP_LEFT, SAFE_X, y);
        lv_label_set_text(pf_name[i], NAME[i]);

        vl[i] = ui_label(p, &oxanium_22, COL_TEXT);
        vb[i] = ui_bar(p, UI_CARD_W, UI_BAR_H, y + 28);
    }

    /* Bilanz als 2x2-Gitter: Zahl gross, Bezeichnung klein darunter. */
    static const char *CAP[4] = { "STREAK", "LÄNGSTE", "QUESTS", "SIEGE" };
    for (int i = 0; i < 4; i++) {
        int x = (i % 2 == 0) ? -UI_CARD_W / 4 : UI_CARD_W / 4;
        int y = PF_GRID_Y + (i / 2) * PF_GRID_DY;

        pf_num[i] = ui_label(p, &oxanium_22, COL_GOLD_BRIGHT);
        lv_obj_align(pf_num[i], LV_ALIGN_TOP_MID, x, y);
        lv_label_set_text(pf_num[i], "0");

        lv_obj_t *c = ui_label(p, &oxanium_16, COL_DIM);
        lv_obj_align(c, LV_ALIGN_TOP_MID, x, y + 26);
        lv_label_set_text(c, CAP[i]);
    }
}

static void refresh_profil(void) {
    const int v[3] = { (int)g_vit.satiation, (int)g_vit.energy, (int)g_vit.bond };

    /* Ohne Karte gehoeren die Vitalwerte niemandem - sie werden ausgegraut.
     * Die vier Zahlen unten bleiben normal: Streak, Quests und Siege gehoeren
     * dem GERAET, also dir. Die gelten auch ohne Beast. */
    bool has = game_has_beast();

    for (int i = 0; i < 3; i++) {
        lv_bar_set_value(vb[i], has ? v[i] : 0, LV_ANIM_ON);
        lv_obj_set_style_bg_color(vb[i], has ? vital_color(v[i]) : COL_BORDER,
                                  LV_PART_INDICATOR);

        if (has) {
            lv_label_set_text_fmt(vl[i], "%d", v[i]);
            lv_obj_set_style_text_color(vl[i], vital_color(v[i]), 0);
        } else {
            lv_label_set_text(vl[i], "-");
            lv_obj_set_style_text_color(vl[i], COL_DIM, 0);
        }
        lv_obj_align(vl[i], LV_ALIGN_TOP_RIGHT, -SAFE_X, UI_TOP + i * PF_ROW_H - 3);

        /* Auch die Beschriftung links verblasst - die Zeile ist als Ganzes tot. */
        lv_obj_set_style_text_color(pf_name[i], has ? COL_TEXT : COL_DIM, 0);
    }

    /* Streak, Quests und Siege gehoeren zum GERAET - sie gelten auch ohne Karte. */
    const int num[4] = { (int)g_state.streak, (int)g_state.maxStreak,
                         (int)g_state.questsCompleted, (int)g_state.arenaWins };
    for (int i = 0; i < 4; i++) {
        lv_label_set_text_fmt(pf_num[i], "%d", num[i]);
        lv_obj_align(pf_num[i], LV_ALIGN_TOP_MID,
                     (i % 2 == 0) ? -UI_CARD_W / 4 : UI_CARD_W / 4,
                     PF_GRID_Y + (i / 2) * PF_GRID_DY);
    }
}

/* ============================================================
 *  QUEST — Claim the Day und die drei Tagesquests
 *  ------------------------------------------------------------
 *  Vier gleich grosse Karten im selben Raster. Jede Quest traegt die Farbe
 *  ihrer Kategorie im Rand - man erkennt den Schwerpunkt, ohne zu lesen.
 *  Der Claim-Knopf hat dieselbe Breite und denselben Rand wie die Quests,
 *  damit er nicht kleiner WIRKT als sie.
 * ============================================================ */
static const char *CAT_NAME[SH_NUM_CATEGORIES] = { "KÖRPER", "GEIST", "GEWOHNHEIT" };

static lv_obj_t *q_claim, *q_claim_lbl, *q_claim_sub;
static lv_obj_t *q_card[SH_NUM_CATEGORIES], *q_cat[SH_NUM_CATEGORIES];
static lv_obj_t *q_txt[SH_NUM_CATEGORIES], *q_mark[SH_NUM_CATEGORIES];

static void do_claim(int unused) {
    LV_UNUSED(unused);
    game_claim();
    /* Falls der Claim ein Item gebracht hat, in die Erfolgsmeldung aufnehmen. */
    int loot = game_claim_loot();
    if (loot >= 0)
        snprintf(cf_success, sizeof(cf_success),
                 "Tag eingetragen.\n+50 XP  ·  %s erhalten", ITEMS[loot].name);
    refresh_quest();
}
static void do_quest(int cat) {
    game_quest_done(cat);
    int loot = game_quest_loot();
    if (loot >= 0)
        snprintf(cf_success, sizeof(cf_success),
                 "Aufgabe erledigt.\n+20 XP  ·  %s erhalten", ITEMS[loot].name);
    refresh_quest();
}

static void q_claim_clicked(lv_event_t *e) {
    LV_UNUSED(e);
    if (!is_tap() || g_state.claimedToday) return;
    cf_open("CLAIM THE DAY", "Tag als erledigt eintragen?\n\n+50 XP  ·  Streak +1",
            "EINTRAGEN", COL_GOLD, do_claim, 0,
            "Tag eingetragen.\n+50 XP  ·  Streak +1");
}

static void q_card_clicked(lv_event_t *e) {
    if (!is_tap()) return;
    int cat = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_state.questDone[cat]) return;

    char t[160];
    lv_snprintf(t, sizeof(t), "%s\n\n+20 XP", sh_quest_text(cat, g_state.questIdx[cat]));
    cf_open(CAT_NAME[cat], t, "ERLEDIGT", cat_color(cat), do_quest, cat,
            "Aufgabe erledigt.\n+20 XP");
}

static void build_quest(lv_obj_t *p) {
    dark_bg(p, &img_bg_ui, BG_DARKEN);
    ui_title(p, "QUEST");

    /* Zeile 0: Claim - dieselbe Karte wie die Quests, nur gefuellt. */
    q_claim = ui_button(p, UI_ROW(0), UI_CARD_H);
    lv_obj_add_event_cb(q_claim, q_claim_clicked, LV_EVENT_CLICKED, NULL);
    q_claim_lbl = ui_label(q_claim, &oxanium_22, lv_color_black());
    q_claim_sub = ui_label(q_claim, &oxanium_16, lv_color_black());

    /* Zeilen 1..3: die Quests */
    for (int i = 0; i < SH_NUM_CATEGORIES; i++) {
        q_card[i] = ui_button(p, UI_ROW(i + 1), UI_CARD_H);
        lv_obj_add_event_cb(q_card[i], q_card_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        q_cat[i] = ui_label(q_card[i], &oxanium_16, cat_color(i));
        lv_obj_align(q_cat[i], LV_ALIGN_TOP_LEFT, UI_PAD, 8);
        lv_label_set_text(q_cat[i], CAT_NAME[i]);

        q_txt[i] = ui_label(q_card[i], &oxanium_16, COL_TEXT);
        lv_label_set_long_mode(q_txt[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(q_txt[i], UI_CARD_W - 2 * UI_PAD - 36);
        lv_obj_align(q_txt[i], LV_ALIGN_TOP_LEFT, UI_PAD, 36);

        q_mark[i] = lv_label_create(q_card[i]);   /* Standard-Font: hat das Häkchen */
        lv_obj_set_style_text_color(q_mark[i], COL_GOOD, 0);
        lv_obj_align(q_mark[i], LV_ALIGN_RIGHT_MID, -UI_PAD, 0);
        lv_label_set_text(q_mark[i], "");
    }

}

static void refresh_quest(void) {
    bool done = g_state.claimedToday;
    lv_obj_set_style_bg_color(q_claim, done ? COL_BOX : COL_GOLD, 0);
    lv_obj_set_style_border_color(q_claim, done ? COL_BORDER : COL_GOLD_BRIGHT, 0);
    lv_label_set_text(q_claim_lbl, done ? "HEUTE ERLEDIGT" : "CLAIM THE DAY");
    lv_label_set_text(q_claim_sub, done ? "morgen wieder"  : "+50 XP  ·  Streak +1");
    lv_obj_set_style_text_color(q_claim_lbl, done ? COL_DIM : lv_color_black(), 0);
    lv_obj_set_style_text_color(q_claim_sub, done ? COL_DIM : lv_color_black(), 0);
    lv_obj_align(q_claim_lbl, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_align(q_claim_sub, LV_ALIGN_TOP_MID, 0, 38);

    for (int i = 0; i < SH_NUM_CATEGORIES; i++) {
        bool qd = g_state.questDone[i];
        lv_label_set_text(q_txt[i], sh_quest_text(i, g_state.questIdx[i]));
        lv_obj_align(q_txt[i], LV_ALIGN_TOP_LEFT, UI_PAD, 36);

        /* Erledigt -> gruener Rand. Offen -> Farbe der Kategorie. */
        lv_obj_set_style_border_color(q_card[i], qd ? COL_GOOD : cat_color(i), 0);
        lv_obj_set_style_text_color(q_txt[i], qd ? COL_DIM : COL_TEXT, 0);
        lv_label_set_text(q_mark[i], qd ? LV_SYMBOL_OK : "");
        lv_obj_align(q_mark[i], LV_ALIGN_RIGHT_MID, -UI_PAD, 0);
    }

}

/* ============================================================
 *  ITEMS — Rucksack
 *  Zwei Kacheln nebeneinander ergeben exakt UI_CARD_W (2*152 + 12 = 316),
 *  drei Reiter ebenso (3*100 + 2*8 = 316). Alles fluchtet.
 * ============================================================ */
#define ITM_W        152
#define ITM_H        104
#define ITM_TAB_W    100
#define ITM_TAB_H     48   /* hoeher, leichter zu treffen */
#define ITM_TAB_GAP    8
#define ITM_GRID_Y   144
#define ITM_ICON_H    44

static lv_obj_t *itm_tab[CAT_COUNT];
static lv_obj_t *itm_cell[4], *itm_icon[4], *itm_name[4], *itm_cnt[4];
static lv_obj_t *itm_equip;                  /* Zeile "angelegt: ..." */
static int       itm_cat = CAT_FOOD;

static int itm_id(int slot) { return itm_cat * 4 + slot; }

/* ---------- Item-Popup ---------- */
static void pop_close(void) {
    lv_obj_add_flag(pop_root, LV_OBJ_FLAG_HIDDEN);
    pop_id = -1;
}

static void pop_bg_clicked(lv_event_t *e) {
    if (lv_event_get_target(e) != pop_root) return;   /* nur echter Hintergrund-Tap */
    if (pop_armed) pop_close();
}

static void pop_arm_tick(void) {
    if (pop_armed || !pop_root) return;
    if (lv_obj_has_flag(pop_root, LV_OBJ_FLAG_HIDDEN)) return;
    if (millis() - g_touchLastMs >= ARM_MS) pop_armed = true;
}

/* Nach einer erfolgreichen Aktion bleibt das Popup OFFEN. Die Erfolgsmeldung
 * ersetzt die Wirkungszeile, und der Knopf wird zu SCHLIESSEN - einheitlich mit
 * Claim und Quests. So kann man die Rueckmeldung in Ruhe lesen. */
static bool pop_done = false;   /* Aktion erledigt -> Knopf schliesst nur noch */
static uint32_t pop_doneMs = 0; /* Zeitpunkt des Umschaltens auf SCHLIESSEN */

/* Das Item-Popup von oben nach unten neu vermessen.
 * Vorher standen alle Zeilen auf festen Y-Werten. Sobald ein Text zweizeilig
 * wurde - etwa "Im Rucksack: 2 / Haelt 5 Kaempfe" oder der Hinweis, dass der
 * Wert erst beim Anlegen faellt - lief er in den Knopf hinein. Jetzt bestimmt
 * jede Zeile die Position der naechsten, und die Box waechst mit. */
#define POP_NOTE_Y   114
#define POP_GAP       14
#define POP_BTN_H     48
#define POP_BOT_PAD   18

static void pop_relayout(void) {
    lv_obj_update_layout(pop_root);

    int y = POP_NOTE_Y;
    lv_obj_align(pop_note, LV_ALIGN_TOP_MID, 0, y);
    y += lv_obj_get_height(pop_note) + POP_GAP;

    /* Leere Zeilen sollen keine Luecke reissen. */
    bool haveTxt = (lv_label_get_text(pop_have)[0] != '\0');
    if (haveTxt) {
        lv_obj_remove_flag(pop_have, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(pop_have, LV_ALIGN_TOP_MID, 0, y);
        y += lv_obj_get_height(pop_have) + POP_GAP;
    } else {
        lv_obj_add_flag(pop_have, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_align(pop_btn, LV_ALIGN_TOP_MID, 0, y);
    y += POP_BTN_H + POP_GAP;

    bool msgTxt = (lv_label_get_text(pop_msg)[0] != '\0');
    if (msgTxt) {
        lv_obj_remove_flag(pop_msg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(pop_msg, LV_ALIGN_TOP_MID, 0, y);
        y += lv_obj_get_height(pop_msg);
    } else {
        lv_obj_add_flag(pop_msg, LV_OBJ_FLAG_HIDDEN);
    }
    y += POP_BOT_PAD;

    if (y < 300) y = 300;
    if (y > 436) y = 436;
    lv_obj_set_height(pop_box, y);
    lv_obj_center(pop_box);
}

static void pop_success(const char *text) {
    pop_done = true;
    pop_doneMs = millis();      /* Sperre gegen den durchschlagenden Klick */
    pop_armed = false;          /* Knopf muss neu scharf werden (pop_arm_tick) */

    lv_label_set_text(pop_note, text);
    lv_obj_set_style_text_color(pop_note, COL_GOOD, 0);

    /* Restbestand aktualisieren. */
    if (inv_count(pop_id) > 0)
        lv_label_set_text_fmt(pop_have, "Im Rucksack: %d", inv_count(pop_id));
    else
        lv_label_set_text(pop_have, "");

    /* Knopf -> SCHLIESSEN, neutral gefaerbt. */
    lv_label_set_text(pop_btn_lbl, "SCHLIESSEN");
    lv_obj_set_style_bg_color(pop_btn, COL_BOX, 0);
    lv_obj_set_style_border_color(pop_btn, COL_BORDER, 0);
    lv_obj_set_style_text_color(pop_btn_lbl, COL_TEXT, 0);
    lv_obj_center(pop_btn_lbl);

    lv_label_set_text(pop_msg, "");
    pop_relayout();
    refresh_items();
}

static void pop_use_clicked(lv_event_t *e) {
    LV_UNUSED(e);
    if (!pop_armed || !is_tap() || pop_id < 0) return;

    /* Aktion schon erledigt -> der Knopf heisst jetzt SCHLIESSEN. Aber erst
     * nach kurzer Sperre, sonst schliesst derselbe Fingerkontakt sofort. */
    if (pop_done) {
        if (millis() - pop_doneMs < ARM_MS) return;
        pop_close();
        return;
    }

    if (equip_item() == pop_id) return;        /* liegt schon an - nichts zu tun */
    const ItemDef *d = &ITEMS[pop_id];

    if (d->cat == CAT_EQUIP) {
        if (!game_has_beast()) {
            lv_label_set_text(pop_msg, "Keine Karte eingelegt.");
            lv_obj_set_style_text_color(pop_msg, COL_BAD, 0);
        } else if (equip_put_on(pop_id)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s angelegt.  +%d", d->name, equip_bonus());
            pop_success(buf);
            return;
        }
    } else if (d->cat == CAT_ITEM) {
        if (!game_has_beast()) {
            lv_label_set_text(pop_msg, "Keine Karte eingelegt.");
            lv_obj_set_style_text_color(pop_msg, COL_BAD, 0);
        } else if (pop_id == IT_SCHILD || pop_id == IT_FEDER) {
            lv_label_set_text(pop_msg, "Wirkt von selbst, wenn es nötig ist.");
            lv_obj_set_style_text_color(pop_msg, COL_DIM, 0);
        } else if (pop_id == IT_ROLLE) {
            if (inv_use(pop_id)) {
                game_add_xp(100);
                pop_success("Wissen entfaltet.  +100 XP");
                return;
            }
        } else if (pop_id == IT_ELIXIER) {
            if (inv_use(pop_id)) {
                vit_feed(d->sat, d->ene, d->bond);
                pop_success("Elixier getrunken.  alles +40");
                return;
            }
        }
    } else if (d->cat != CAT_FOOD) {
        lv_label_set_text(pop_msg, "Noch nicht verfügbar.");
        lv_obj_set_style_text_color(pop_msg, COL_WARN, 0);
    } else if (!game_has_beast()) {
        lv_label_set_text(pop_msg, "Keine Karte eingelegt.");
        lv_obj_set_style_text_color(pop_msg, COL_BAD, 0);
    } else if (g_vit.satiation >= 100) {
        lv_label_set_text(pop_msg, "Dein Beast ist schon satt.");
        lv_obj_set_style_text_color(pop_msg, COL_WARN, 0);
    } else if (inv_use(pop_id)) {
        vit_feed(d->sat, d->ene, d->bond + BOND_FEED);
        /* Die Erfolgsmeldung nennt, was passiert ist - z.B. "Beere verschlungen.
         * +15 Sättigung". Der Satz kommt aus dem Item selbst. */
        char buf[72];
        snprintf(buf, sizeof(buf), "%s %s.  +%d Sättigung", d->name, FOOD_VERB, d->sat);
        pop_success(buf);
        return;
    }
    pop_relayout();
}

static void pop_open(int id) {
    if (id < 0 || id >= IT_COUNT) return;

    /* Das angelegte Stueck liegt NICHT mehr im Rucksack (Anzahl 0), soll aber
     * trotzdem anklickbar bleiben - man will ja sehen, was man traegt. */
    bool getragen = (equip_item() == id);
    if (inv_count(id) == 0 && !getragen) return;

    pop_id    = id;
    pop_armed = false;
    const ItemDef *d = &ITEMS[id];

    scale_to_height(pop_icon, item_icon(id), 60);
    lv_obj_set_style_image_opa(pop_icon, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(pop_icon, LV_ALIGN_TOP_MID, 0, 18);

    lv_label_set_text(pop_name, d->name);
    lv_obj_align(pop_name, LV_ALIGN_TOP_MID, 0, 82);

    /* Wirkung. Bei Ausruestung steht hier die SPANNE - die Zahl faellt erst
     * beim Anlegen, und das muss man vorher wissen. */
    lv_label_set_text(pop_note, d->note);

    bool passiv   = (id == IT_SCHILD || id == IT_FEDER);
    bool getragen2 = (equip_item() == id);

    if (getragen2) {
        /* Angelegt: keine Spanne mehr, sondern der gewuerfelte Wert. */
        lv_label_set_text_fmt(pop_have, "Angelegt:  +%d\nNoch %d Kämpfe",
                              equip_bonus(), equip_fights_left());
        lv_obj_set_style_text_color(pop_have, COL_GOLD_BRIGHT, 0);
    } else if (d->cat == CAT_EQUIP) {
        lv_label_set_text_fmt(pop_have, "Im Rucksack: %d\nHält 5 Kämpfe", inv_count(id));
        lv_obj_set_style_text_color(pop_have, COL_DIM, 0);
    } else {
        lv_label_set_text_fmt(pop_have, "Im Rucksack: %d", inv_count(id));
        lv_obj_set_style_text_color(pop_have, COL_DIM, 0);
    }

    /* --- Knopf --- */
    const char *lbl;
    bool aktiv;
    if (getragen2)                { lbl = "ANGELEGT";         aktiv = false; }
    else if (passiv)              { lbl = "WIRKT VON SELBST"; aktiv = false; }
    else if (d->cat == CAT_EQUIP) { lbl = "ANLEGEN";          aktiv = true;  }
    else                          { lbl = "VERWENDEN";        aktiv = true;  }

    lv_label_set_text(pop_btn_lbl, lbl);
    lv_obj_set_style_bg_color(pop_btn, aktiv ? COL_GOLD : COL_BOX, 0);
    lv_obj_set_style_border_color(pop_btn, aktiv ? COL_GOLD_BRIGHT : COL_BORDER, 0);
    lv_obj_set_style_text_color(pop_btn_lbl, aktiv ? lv_color_black() : COL_DIM, 0);
    lv_obj_center(pop_btn_lbl);

    /* --- Fusszeile --- */
    if (getragen2)
        lv_label_set_text(pop_msg, "Zerfällt, wenn die Kämpfe aufgebraucht sind.");
    else if (passiv)
        lv_label_set_text(pop_msg, id == IT_SCHILD
            ? "Fängt einen Streak-Bruch ab und zerbricht dabei."
            : "Fängt einen Kollaps ab.");
    else if (d->cat == CAT_EQUIP && equip_active())
        lv_label_set_text_fmt(pop_msg, "Ersetzt: %s. Der Bonus wird neu gewürfelt.",
                              equip_name());
    else if (d->cat == CAT_EQUIP)
        lv_label_set_text(pop_msg, "Der genaue Wert wird beim Anlegen gewürfelt.");
    else
        lv_label_set_text(pop_msg, "");
    lv_obj_set_style_text_color(pop_msg, COL_DIM, 0);

    /* Alles von oben nach unten setzen und die Box passend hoch machen. */
    pop_relayout();

    pop_done = false;   /* frisch geoeffnet -> Knopf ist wieder die Aktion */

    lv_obj_remove_flag(pop_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(pop_root);
}

static void build_popup(lv_obj_t *scr) {
    lv_obj_t *box = modal_box(scr, &pop_root, 310, pop_bg_clicked);
    pop_box = box;

    pop_icon = lv_image_create(box);
    pop_name = ui_label(box, &oxanium_22, COL_GOLD_BRIGHT);
    pop_note = ui_label(box, &oxanium_16, COL_TEXT);
    /* Feste Breite + Umbruch, sonst laufen lange Erfolgstexte
     * ("Kraftnuss verschlungen. +25 Sättigung") ueber den Popup-Rand. */
    lv_obj_set_width(pop_note, POP_W - 2 * 24);
    lv_label_set_long_mode(pop_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(pop_note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(pop_note, 7, 0);

    pop_have = ui_label(box, &oxanium_16, COL_DIM);
    lv_obj_set_style_text_align(pop_have, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(pop_have, 7, 0);

    pop_btn = lv_obj_create(box);
    lv_obj_remove_style_all(pop_btn);
    lv_obj_set_size(pop_btn, POP_W - 2 * 28, 48);
    lv_obj_set_style_radius(pop_btn, UI_RADIUS, 0);
    lv_obj_set_style_bg_opa(pop_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pop_btn, UI_BORDER, 0);
    lv_obj_remove_flag(pop_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pop_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pop_btn, pop_use_clicked, LV_EVENT_CLICKED, NULL);

    pop_btn_lbl = ui_label(pop_btn, &oxanium_22, lv_color_black());
    lv_obj_center(pop_btn_lbl);

    /* Umbruch ist Pflicht: ohne feste Breite lief der Text aus der Box. */
    pop_msg = ui_label(box, &oxanium_16, COL_DIM);
    lv_label_set_long_mode(pop_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(pop_msg, POP_W - 2 * 24);
    lv_obj_set_style_text_align(pop_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(pop_msg, 7, 0);
    lv_label_set_text(pop_msg, "");
}

/* ---------- Raster ---------- */
static void itm_cell_clicked(lv_event_t *e) {
    if (!is_tap()) return;
    pop_open(itm_id((int)(intptr_t)lv_event_get_user_data(e)));
}

static void itm_tab_clicked(lv_event_t *e) {
    if (!is_tap()) return;
    itm_cat = (int)(intptr_t)lv_event_get_user_data(e);
    refresh_items();
}

static void build_items(lv_obj_t *p) {
    dark_bg(p, &img_bg_ui, BG_DARKEN);
    ui_title(p, "RUCKSACK");

    static const char *TAB[CAT_COUNT] = { "FUTTER", "EQUIP", "ITEMS" };
    for (int i = 0; i < CAT_COUNT; i++) {
        itm_tab[i] = lv_obj_create(p);
        lv_obj_remove_style_all(itm_tab[i]);
        lv_obj_set_size(itm_tab[i], ITM_TAB_W, ITM_TAB_H);
        lv_obj_align(itm_tab[i], LV_ALIGN_TOP_MID,
                     -(ITM_TAB_W + ITM_TAB_GAP) + i * (ITM_TAB_W + ITM_TAB_GAP), UI_TOP);
        lv_obj_set_style_radius(itm_tab[i], UI_RADIUS, 0);
        lv_obj_set_style_bg_opa(itm_tab[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(itm_tab[i], UI_BORDER, 0);
        lv_obj_remove_flag(itm_tab[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(itm_tab[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(itm_tab[i], itm_tab_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *l = ui_label(itm_tab[i], &oxanium_16, COL_DIM);
        lv_obj_center(l);
        lv_label_set_text(l, TAB[i]);
    }

    /* Was gerade angelegt ist - steht nur im EQUIP-Reiter. */
    itm_equip = ui_label(p, &oxanium_16, COL_GOLD_BRIGHT);
    lv_obj_set_style_text_align(itm_equip, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(itm_equip, LV_ALIGN_BOTTOM_MID, 0, -36);
    lv_label_set_text(itm_equip, "");

    for (int i = 0; i < 4; i++) {
        int x = (i % 2 == 0) ? -(ITM_W + UI_GAP) / 2 : (ITM_W + UI_GAP) / 2;
        int y = ITM_GRID_Y + (i / 2) * (ITM_H + UI_GAP);

        itm_cell[i] = lv_obj_create(p);
        lv_obj_remove_style_all(itm_cell[i]);
        lv_obj_set_size(itm_cell[i], ITM_W, ITM_H);
        lv_obj_align(itm_cell[i], LV_ALIGN_TOP_MID, x, y);
        lv_obj_set_style_bg_color(itm_cell[i], COL_BOX, 0);
        lv_obj_set_style_bg_opa(itm_cell[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(itm_cell[i], UI_RADIUS, 0);
        lv_obj_set_style_border_width(itm_cell[i], UI_BORDER, 0);
        lv_obj_remove_flag(itm_cell[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(itm_cell[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(itm_cell[i], itm_cell_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        itm_icon[i] = lv_image_create(itm_cell[i]);
        lv_obj_align(itm_icon[i], LV_ALIGN_TOP_MID, 0, 10);

        itm_name[i] = ui_label(itm_cell[i], &oxanium_16, COL_TEXT);
        lv_obj_align(itm_name[i], LV_ALIGN_BOTTOM_MID, 0, -8);

        itm_cnt[i] = ui_label(itm_cell[i], &oxanium_22, COL_GOLD_BRIGHT);
        lv_obj_align(itm_cnt[i], LV_ALIGN_TOP_RIGHT, -10, 8);
    }
}

static void refresh_items(void) {
    /* Ausruestungsstand nur im EQUIP-Reiter. Die Kachel traegt schon den
     * gruenen Rand und den Wert - hier nur noch die Haltbarkeit. */
    if (itm_cat == CAT_EQUIP && equip_active()) {
        lv_label_set_text_fmt(itm_equip, "%s  ·  noch %d Kämpfe",
                              equip_name(), equip_fights_left());
        lv_obj_align(itm_equip, LV_ALIGN_BOTTOM_MID, 0, -36);
        lv_obj_remove_flag(itm_equip, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(itm_equip, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < CAT_COUNT; i++) {
        bool on = (i == itm_cat);
        lv_obj_set_style_bg_color(itm_tab[i], on ? COL_GOLD : COL_BOX, 0);
        lv_obj_set_style_border_color(itm_tab[i], on ? COL_GOLD_BRIGHT : COL_BORDER, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(itm_tab[i], 0),
                                    on ? lv_color_black() : COL_DIM, 0);
    }

    for (int i = 0; i < 4; i++) {
        int id = itm_id(i);
        int n  = inv_count(id);
        bool getragen = (equip_item() == id);   /* liegt an -> Anzahl 0, trotzdem da */

        scale_to_height(itm_icon[i], item_icon(id), ITM_ICON_H);
        lv_obj_align(itm_icon[i], LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_image_opa(itm_icon[i],
                                   (n > 0 || getragen) ? LV_OPA_COVER : LV_OPA_30,
                                   LV_PART_MAIN);

        lv_label_set_text(itm_name[i], ITEMS[id].name);
        lv_obj_align(itm_name[i], LV_ALIGN_BOTTOM_MID, 0, -8);

        if (getragen) {
            /* Angelegt: gruener Rand und der gewuerfelte Wert statt der Anzahl. */
            lv_label_set_text_fmt(itm_cnt[i], "+%d", equip_bonus());
            lv_obj_set_style_text_color(itm_cnt[i], COL_GOOD, 0);
            lv_obj_remove_flag(itm_cnt[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_border_color(itm_cell[i], COL_GOOD, 0);
            lv_obj_set_style_text_color(itm_name[i], COL_TEXT, 0);
        } else if (n > 0) {
            lv_label_set_text_fmt(itm_cnt[i], "%d", n);
            lv_obj_set_style_text_color(itm_cnt[i], COL_GOLD_BRIGHT, 0);
            lv_obj_remove_flag(itm_cnt[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_border_color(itm_cell[i], COL_GOLD, 0);
            lv_obj_set_style_text_color(itm_name[i], COL_TEXT, 0);
        } else {
            lv_obj_add_flag(itm_cnt[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_border_color(itm_cell[i], COL_BORDER, 0);
            lv_obj_set_style_text_color(itm_name[i], COL_DIM, 0);
        }
        lv_obj_align(itm_cnt[i], LV_ALIGN_TOP_RIGHT, -10, 8);
    }
}

/* ============================================================
 *  ABENTEUER
 *  ------------------------------------------------------------
 *  Persista-Karte als Buehne, die Zugaenge als grosse Knoepfe im
 *  Kartenmass. Antippzonen auf den Portalen waeren huebscher, aber auf
 *  368x448 zu treffunsicher - dieselbe Falle wie die alten Icon-Kacheln.
 *
 *  Drei Zustaende: bereit / unterwegs (Countdown) / zurueck (Ergebnis).
 * ============================================================ */
#define ADV_BTN_H    72
/* Beide Knoepfe als Block vertikal zentriert. Der Block ist 2*72 + 12 = 156
 * hoch; bei 448 Bildhoehe beginnt er also bei (448-156)/2 = 146. */
#define ADV_BTN_Y   146
#define ADV_STAT_Y  (ADV_BTN_Y + 2 * (ADV_BTN_H + UI_GAP) + 8)

static lv_obj_t *adv_btn[2], *adv_sub[2], *adv_state, *adv_stats, *adv_stats_cap;
static lv_obj_t *adv_title;   /* Seitentitel - unterwegs blendet er aus */
static lv_obj_t *adv_wait, *adv_wait_txt, *adv_clock;
static lv_obj_t *adv_beast, *adv_beast_wrap, *adv_flav, *adv_pct, *adv_bar;

/* Wanderung ueber die Abenteuerkarte. Feste Zeilen von oben nach unten,
 * damit sich nichts ueberlappt - auch nicht, wenn das Beast im Bogen laeuft. */
#define ADV_BEAST_H   124   /* Hoehe des Sprites unterwegs             */
#define ADV_WRAP      140   /* Kantenlaenge des Traeger-Objekts        */
#define ADV_WALK_X    104   /* halbe Wegstrecke links/rechts der Mitte */
#define ADV_WALK_ARC   10   /* wie hoch der Bogen in der Mitte liegt   */
#define ADV_Y_TITLE    34
#define ADV_Y_WALK     78   /* Oberkante des Traegers                  */
#define ADV_Y_PCT     224
#define ADV_Y_BAR     256
#define ADV_Y_CLOCK   282
#define ADV_Y_FLAV    326

static void ar_float_start(lv_obj_t *wrap, int amp, int dur, int delay); /* aus der Arena */
static lv_obj_t *adv_res, *adv_res_title, *adv_res_txt, *adv_res_hint;
static lv_obj_t *adv_res_icon, *adv_res_reward;

static void do_adv(int kind) {
    if (adv_start(kind)) refresh_adv();
}

static void adv_clicked(lv_event_t *e) {
    if (!is_tap()) return;
    int kind = (int)(intptr_t)lv_event_get_user_data(e);
    if (adv_is_away() || adv_has_result()) return;
    if (vit_is_collapsed()) return;      /* wer am Boden liegt, geht nirgendwo hin */
    if ((int)g_vit.energy < adv_energy_cost(kind)) return;

    char t[140];
    lv_snprintf(t, sizeof(t), "%s losschicken?\n\n%d Std.  ·  %d Energie\n\nEs hungert unterwegs doppelt.",
                kind == ADV_EXPED ? "Expedition" : "Streifzug",
                kind == ADV_EXPED ? ADV_EXPED_H : ADV_ROAM_H,
                adv_energy_cost(kind));
    cf_open(kind == ADV_EXPED ? "EXPEDITION" : "STREIFZUG", t, "LOSSCHICKEN",
            COL_GOLD, do_adv, kind, NULL);   /* NULL: schliesst sofort, Screen wechselt */
}

/* Ergebnis abholen -> Beute wandert in den Rucksack. */
static void ar_start_boss(int which);   /* Boss-Kampf, definiert bei der Arena */

static void adv_res_tap(lv_event_t *e) {
    LV_UNUSED(e);
    if (!is_tap()) return;
    adv_collect();
    /* Ab Level 20 kann die Expedition in einer Boss-Begegnung muenden. Dann
     * geht es direkt aus dem Abenteuer in die Arena - der Hoehepunkt nach
     * acht Stunden Wartezeit. */
    uint8_t boss = adv_boss_pending();
    refresh_adv();
    if (boss) {
        show_page(PG_ARENA);
        ar_start_boss((int)boss);
    }
}

static void build_adv(lv_obj_t *p) {
    dark_bg(p, &img_bg_adv, ADV_DARKEN);
    adv_title = ui_title(p, "ABENTEUER");

    static const char *T[2] = { "STREIFZUG", "EXPEDITION" };
    static const char *S[2] = { "2 Std.  ·  20 Energie", "8 Std.  ·  45 Energie" };

    for (int i = 0; i < 2; i++) {
        adv_btn[i] = ui_button(p, ADV_BTN_Y + i * (ADV_BTN_H + UI_GAP), ADV_BTN_H);
        lv_obj_set_style_border_color(adv_btn[i], COL_GOLD, 0);
        lv_obj_add_event_cb(adv_btn[i], adv_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *t = ui_label(adv_btn[i], &oxanium_22, COL_GOLD_BRIGHT);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 12);
        lv_label_set_text(t, T[i]);

        adv_sub[i] = ui_label(adv_btn[i], &oxanium_16, COL_TEXT);
        lv_obj_align(adv_sub[i], LV_ALIGN_TOP_MID, 0, 42);
        lv_label_set_text(adv_sub[i], S[i]);
    }

    adv_state = ui_label(p, &oxanium_16, COL_TEXT);
    lv_obj_set_style_text_align(adv_state, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(adv_state, LV_ALIGN_TOP_MID, 0, ADV_STAT_Y);
    lv_label_set_text(adv_state, "");

    /* Erfolgsbilanz: wie oft ist das Beast schon zurueckgekommen?
     * Ueberschrift klein und still, die Zahlen darunter. */
    /* Die Ueberschrift muss mit den Zahlen zusammen verschwinden - sonst steht
     * "ZURÜCKGEKEHRT" da, waehrend das Beast noch unterwegs ist. */
    adv_stats_cap = ui_label(p, &oxanium_16, COL_DIM);
    lv_obj_align(adv_stats_cap, LV_ALIGN_BOTTOM_MID, 0, -58);
    lv_label_set_text(adv_stats_cap, "ZURÜCKGEKEHRT");

    adv_stats = ui_label(p, &oxanium_16, COL_TEXT);
    lv_obj_set_style_text_align(adv_stats, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(adv_stats, LV_ALIGN_BOTTOM_MID, 0, -34);
    lv_label_set_text(adv_stats, "");

    /* --- unterwegs: Countdown statt Knoepfe --- */
    adv_wait = lv_obj_create(p);
    lv_obj_remove_style_all(adv_wait);
    lv_obj_set_size(adv_wait, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(adv_wait, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(adv_wait, LV_OBJ_FLAG_HIDDEN);

    /* --- Aufbau von oben nach unten, feste Zeilen, keine Ueberschneidungen ---
     *   ADV_Y_TITLE  Titel
     *   ADV_Y_PCT    Prozentzahl
     *   ADV_Y_BAR    Fortschrittsbalken
     *   ADV_Y_CLOCK  Restzeit
     *   ADV_Y_FLAV   Sprechkarte mit der Bemerkung                      */
    adv_wait_txt = ui_label(adv_wait, &oxanium_22, COL_GOLD_BRIGHT);
    lv_obj_set_style_text_align(adv_wait_txt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(adv_wait_txt, LV_ALIGN_TOP_MID, 0, ADV_Y_TITLE);

    /* Das Beast wandert ueber die Karte: seine Position zeigt den Fortschritt.
     * Wie in der Arena steckt das Sprite in einem Wrapper, der schwebt - so
     * stoert die Schwebe-Animation die Positionierung nicht. */
    adv_beast_wrap = lv_obj_create(adv_wait);
    lv_obj_remove_style_all(adv_beast_wrap);
    lv_obj_set_size(adv_beast_wrap, ADV_WRAP, ADV_WRAP);
    lv_obj_align(adv_beast_wrap, LV_ALIGN_TOP_MID, 0, ADV_Y_WALK);
    lv_obj_remove_flag(adv_beast_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(adv_beast_wrap, LV_OBJ_FLAG_CLICKABLE);
    adv_beast = lv_image_create(adv_beast_wrap);
    lv_obj_center(adv_beast);

    adv_pct = ui_label(adv_wait, &oxanium_16, COL_DIM);
    lv_obj_set_style_text_align(adv_pct, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(adv_pct, LV_ALIGN_TOP_MID, 0, ADV_Y_PCT);

    adv_bar = lv_bar_create(adv_wait);
    lv_obj_remove_style_all(adv_bar);
    lv_obj_set_size(adv_bar, 240, 10);
    lv_obj_align(adv_bar, LV_ALIGN_TOP_MID, 0, ADV_Y_BAR);
    lv_bar_set_range(adv_bar, 0, 100);
    lv_obj_set_style_bg_color(adv_bar, COL_BOX, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(adv_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(adv_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_border_color(adv_bar, COL_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(adv_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(adv_bar, COL_GOLD, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(adv_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(adv_bar, 5, LV_PART_INDICATOR);

    adv_clock = ui_label(adv_wait, &oxanium_22, COL_TEXT);
    lv_obj_set_style_text_align(adv_clock, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(adv_clock, LV_ALIGN_TOP_MID, 0, ADV_Y_CLOCK);

    /* Die Bemerkung sitzt in einer eigenen Karte - dadurch wirkt sie wie eine
     * Nachricht von unterwegs und kann nichts ueberlappen. */
    lv_obj_t *fc = ui_card(adv_wait, ADV_Y_FLAV, 92);
    adv_flav = ui_label(fc, &oxanium_16, COL_TEXT);
    lv_obj_set_width(adv_flav, UI_CARD_W - 2 * 22);
    lv_label_set_long_mode(adv_flav, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(adv_flav, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(adv_flav);

    /* --- zurueck: Ergebnis --- */
    adv_res = lv_obj_create(p);
    lv_obj_remove_style_all(adv_res);
    lv_obj_set_size(adv_res, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(adv_res, COL_PAGE, 0);
    lv_obj_set_style_bg_opa(adv_res, LV_OPA_80, 0);
    lv_obj_remove_flag(adv_res, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(adv_res, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(adv_res, adv_res_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(adv_res, LV_OBJ_FLAG_HIDDEN);

    adv_res_title = ui_label(adv_res, &oxanium_22, COL_GOLD_BRIGHT);
    lv_obj_align(adv_res_title, LV_ALIGN_CENTER, 0, -108);

    /* Grosses Item-Icon in der Mitte - der Blickfang. */
    adv_res_icon = lv_image_create(adv_res);
    lv_obj_align(adv_res_icon, LV_ALIGN_CENTER, 0, -40);

    /* Die Belohnung gross und golden, wie ein Gewinn. */
    adv_res_reward = ui_label(adv_res, &oxanium_22, COL_GOLD_BRIGHT);
    lv_obj_set_style_text_align(adv_res_reward, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(adv_res_reward, LV_ALIGN_CENTER, 0, 26);

    adv_res_txt = ui_label(adv_res, &oxanium_16, COL_DIM);
    lv_obj_set_style_text_align(adv_res_txt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(adv_res_txt, LV_ALIGN_CENTER, 0, 58);

    adv_res_hint = ui_label(adv_res, &oxanium_16, COL_DIM);
    lv_obj_align(adv_res_hint, LV_ALIGN_CENTER, 0, 104);
    lv_label_set_text(adv_res_hint, "tippen zum Einsammeln");
}

static void refresh_adv(void) {
    bool away    = adv_is_away();
    bool result  = adv_has_result();

    /* Genau EINER der drei Zustaende ist sichtbar:
     *   bereit -> Knoepfe    unterwegs -> Countdown    zurueck -> Ergebnis */
    bool idle = !away && !result;
    for (int i = 0; i < 2; i++) {
        if (idle) lv_obj_remove_flag(adv_btn[i], LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(adv_btn[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (idle) { lv_obj_remove_flag(adv_state, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(adv_stats, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(adv_stats_cap, LV_OBJ_FLAG_HIDDEN); }
    else      { lv_obj_add_flag(adv_state, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(adv_stats, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(adv_stats_cap, LV_OBJ_FLAG_HIDDEN); }

    if (away) lv_obj_remove_flag(adv_wait, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(adv_wait, LV_OBJ_FLAG_HIDDEN);

    /* Unterwegs traegt der Warte-Screen seine eigene Ueberschrift ("AUF
     * EXPEDITION"). Der Seitentitel muss dann weg - sonst stehen zwei
     * Ueberschriften uebereinander, und genau das sah kaputt aus. */
    if (away) lv_obj_add_flag(adv_title, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_remove_flag(adv_title, LV_OBJ_FLAG_HIDDEN);

    if (result) {
        int kind = adv_outcome_kind();
        lv_label_set_text(adv_res_title, adv_outcome_title());

        /* Titelfarbe traegt die Stimmung: Schatz golden, Pech gedaempft. */
        lv_color_t tc = COL_GOLD_BRIGHT;
        if      (kind == ADV_TREASURE) tc = COL_GOLD_BRIGHT;
        else if (kind == ADV_MISHAP)   tc = COL_BAD;
        else if (kind == ADV_DISCOVER) tc = COL_GOOD;
        lv_obj_set_style_text_color(adv_res_title, tc, 0);

        /* Icon: bei Fund/Schatz das echte Item, sonst ein passendes Symbol. */
        if (adv_outcome_has_item()) {
            scale_to_height(adv_res_icon, item_icon(adv_outcome_item()), 72);
            lv_obj_remove_flag(adv_res_icon, LV_OBJ_FLAG_HIDDEN);
        } else if (kind == ADV_DISCOVER) {
            scale_to_height(adv_res_icon, &ic_adventure, 72);
            lv_obj_remove_flag(adv_res_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(adv_res_icon, LV_OBJ_FLAG_HIDDEN);
        }

        /* Belohnung gross, Beschreibung klein darunter. */
        switch (kind) {
            case ADV_FIND:
                lv_label_set_text_fmt(adv_res_reward, "%dx %s",
                                      adv_outcome_amount(), ITEMS[adv_outcome_item()].name);
                lv_label_set_text(adv_res_txt, "in den Rucksack gewandert");
                break;
            case ADV_TREASURE:
                lv_label_set_text_fmt(adv_res_reward, "%s", ITEMS[adv_outcome_item()].name);
                lv_label_set_text(adv_res_txt, "aus einer Truhe geborgen");
                break;
            case ADV_DISCOVER:
                lv_label_set_text_fmt(adv_res_reward, "+%d XP", ADV_DISCOVER_XP);
                lv_label_set_text(adv_res_txt, "etwas Neues entdeckt");
                break;
            case ADV_MISHAP:
                lv_label_set_text(adv_res_reward, "Nichts gefunden");
                lv_label_set_text(adv_res_txt, "erschöpft zurückgekehrt");
                break;
            default:
                lv_label_set_text(adv_res_reward, "Zurück");
                lv_label_set_text(adv_res_txt, "");
                break;
        }

        lv_obj_remove_flag(adv_res, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(adv_res);
    } else {
        lv_obj_add_flag(adv_res, LV_OBJ_FLAG_HIDDEN);
    }

    if (away) {
        int left = adv_minutes_left();
        int pct  = adv_progress_pct();

        /* Ueberschrift, Prozent, Balken und Restzeit stehen jeweils in ihrer
         * eigenen festen Zeile - nichts wird mehr zusammengequetscht. */
        lv_label_set_text(adv_wait_txt, g_adv.kind == ADV_EXPED ? "AUF EXPEDITION"
                                                                : "AUF STREIFZUG");
        lv_label_set_text_fmt(adv_pct, "%d %% der Strecke", pct);
        lv_bar_set_value(adv_bar, pct, LV_ANIM_OFF);
        lv_label_set_text_fmt(adv_clock, "noch %d:%02d Std.", left / 60, left % 60);
        lv_label_set_text_fmt(adv_flav, "\"%s\"", adv_flavor());

        /* Sprite passend zur aktuellen Entwicklungsstufe. */
        scale_to_height(adv_beast, asset_beast(g_state.beastId, game_player_stage()), ADV_BEAST_H);

        /* Das Beast laeuft auf der Weglinie: waagerecht nach Fortschritt, dazu
         * ein flacher Bogen, damit die Strecke nicht wie ein Lineal wirkt.
         * Ausrichtung wie beim Aufbau ueber TOP_MID, sonst landet es in einem
         * ganz anderen Band als die Linie. */
        int x    = -ADV_WALK_X + (2 * ADV_WALK_X * pct) / 100;
        int arc  = (pct * (100 - pct)) / 100;          /* 0..25, Maximum bei 50 % */
        int lift = (arc * ADV_WALK_ARC) / 25;
        lv_obj_align(adv_beast_wrap, LV_ALIGN_TOP_MID, x, ADV_Y_WALK - lift);

        /* Schweben nur einmal starten, sonst setzt es sich bei jedem Tick zurueck. */
        static bool floating = false;
        if (!floating) { ar_float_start(adv_beast_wrap, 4, 1800, 0); floating = true; }
        return;
    }

    /* Zu wenig Energie oder bewusstlos -> Knopf ausgraut. */
    for (int i = 0; i < 2; i++) {
        bool ok = game_has_beast() && !vit_is_collapsed() &&
                  (int)g_vit.energy >= adv_energy_cost(i);
        lv_obj_set_style_border_color(adv_btn[i], ok ? COL_GOLD : COL_BORDER, 0);
        lv_obj_set_style_text_color(adv_sub[i], ok ? COL_TEXT : COL_BAD, 0);
    }

    if (!game_has_beast()) lv_label_set_text(adv_state, "Keine Karte eingelegt.");
    else lv_label_set_text_fmt(adv_state, "Energie   %d", (int)g_vit.energy);
    lv_obj_align(adv_state, LV_ALIGN_TOP_MID, 0, ADV_STAT_Y);

    lv_label_set_text_fmt(adv_stats, "%d Streifzüge   ·   %d Expeditionen",
                          adv_done_count(ADV_ROAM), adv_done_count(ADV_EXPED));
    lv_obj_align(adv_stats, LV_ALIGN_BOTTOM_MID, 0, -34);
}

/* ============================================================
 *  ARENA — 1:1 portiert aus der Watch-Firmware
 *  ------------------------------------------------------------
 *  Ein Timing-Minispiel. Ein Zeiger laeuft ueber eine Leiste; wer nah an
 *  der Mitte tippt, trifft haerter:
 *      Abstand < 3 %  -> PERFEKT
 *      Abstand < 9 %  -> GUT
 *      sonst          -> SCHWACH
 *
 *  DREI Dinge machen den Kampf fluessig - ohne sie ruckelt es:
 *
 *  1. Der HP-Balken laeuft KONTROLLIERT (DRAIN_MS) und der Kampf geht erst
 *     weiter, wenn er sein Ziel erreicht hat (completed_cb). Alles ist
 *     streng verkettet, nichts laeuft parallel.
 *  2. Der Balken rechnet in PROMILLE (0..1000), nicht in Prozent - sonst
 *     ruckelt die Bewegung bei wenig HP in groben Stufen.
 *  3. Waehrend des Kampfes laeuft das Display mit ~60 fps statt der
 *     ueblichen 30. Der duenne Zeiger braucht das.
 *
 *  Anordnung ueber Kreuz, wie auf der Watch: Gegner rechts oben, seine
 *  Werte-Box links; Spieler links, seine Box rechts unten.
 *
 *  ZWEI BREMSEN aus dem Konzept: 10 Kaempfe am Tag UND 12 Energie je Kampf.
 * ============================================================ */
enum { AS_MENU = 0, AS_BATTLE, AS_RESULT };

#define COL_HPBOX     lv_color_hex(0x0d0a14)
#define COL_PANEL     lv_color_hex(0x0a0810)
#define COL_DMG_ENEMY lv_color_hex(0xff7a5c)
#define COL_FB_GUT    lv_color_hex(0x8fe093)
#define COL_FB_WEAK   lv_color_hex(0x9aa0b5)

/* Vollflaechige Move-Button-Farben je Kategorie. Bewusst so gewaehlt, dass die
 * Schrift (dunkel auf hell, hell auf dunkel) jeweils gut lesbar bleibt.
 *   Basic     - ruhiges Stahlblau  (haeufigster Zug, soll nicht schreien)
 *   Special   - kraeftiges Rotorange (Angriff)
 *   Buff      - Gruen               (Staerkung, passt zur Aura)
 *   Signature - Gold                (der maechtigste Zug)                     */
#define COL_MV_BASIC  lv_color_hex(0x3d5a80)   /* Stahlblau  -> helle Schrift */
#define COL_MV_SPEC   lv_color_hex(0xa83e2a)   /* Rotorange  -> helle Schrift */
#define COL_MV_BUFF   lv_color_hex(0x2f7548)   /* Gruen      -> helle Schrift */
#define COL_MV_SIG    lv_color_hex(0xe6b422)   /* Gold       -> dunkle Schrift */


/* --- Masse (aus der Watch, auf 368x448 heruntergerechnet) --- */
#define AR_BOX_W     176
#define AR_BOX_H      52
#define AR_BOX_M      30      /* Rand - haelt die Box aus der Display-Rundung */
#define AR_FOE_BOX_Y  62
#define AR_ME_BOX_Y  196
#define AR_PANEL_H   170
#define AR_FOE_H     118
#define AR_ME_H      150

/* --- Kampf-Timing: alles strikt nacheinander verkettet --- */
#define LAND_MS      140      /* Ausfall -> Treffer landet                      */
#define DRAIN_MS     620      /* HP-Balken laeuft kontrolliert runter           */
#define RESULT_PAUSE 420      /* Pause nach dem Balken -> Ergebnis              */
#define TURN_GAP     520      /* Pause zwischen den Zuegen                      */
#define OPENING_GAP  700      /* Vorlauf, wenn ein schnellerer Gegner eroeffnet */

/* Ausfall, Treffer-Blitz und Rueckstoss je Intensitaet */
static const int      LUNGE_DX[3]  = { 8, 15, 24 };
static const int      LUNGE_DY[3]  = { 6, 12, 19 };
static const int      LUNGE_DUR[3] = { 130, 150, 170 };
static const lv_opa_t HIT_OPA[3]   = { 95, 140, 190 };
static const int      HIT_AMP[3]   = { 4, 7, 11 };
static const int      HIT_DUR[3]   = { 240, 280, 360 };

static lv_obj_t *ar_menu, *ar_start, *ar_start_lbl, *ar_info, *ar_menu_title;
static lv_obj_t *ar_loadout_btn, *ar_loadout_lbl;
static lv_obj_t *ar_left, *ar_cost, *ar_left_cap, *ar_cost_cap;
static lv_obj_t *ar_battle, *ar_foe_spr, *ar_me_spr, *ar_foe_bar, *ar_me_bar;
static lv_obj_t *ar_foe_wrap, *ar_me_wrap;   /* Wrapper fuer die Float-Bewegung */
static lv_obj_t *ar_foe_name, *ar_foe_lv, *ar_me_name, *ar_me_lv;
static lv_obj_t *ar_fb;
static lv_obj_t *ar_move_btn[2], *ar_move_name[2], *ar_move_tag[2];
static lv_obj_t *ar_stamina;
static lv_obj_t *ar_result, *ar_r_big, *ar_r_rew, *ar_r_hint, *ar_r_icon, *ar_r_loot;

static int   ar_state = AS_MENU;
static bool  ar_busy  = false;

/* Fuer ui_handle_swipe: waehrend eines Kampfes duerfen die Einstellungen
 * nicht aufgehen - dort ist jeder Tipp ein Schlag. */
static bool arena_busy(void) { return ar_state != AS_MENU; }

static void ar_show_result(bool win);

/* Einmal-Timer: feuert nach ms und loescht sich selbst. */
static void once_cb(lv_timer_t *t) {
    void (*fn)(lv_timer_t *) = (void (*)(lv_timer_t *))lv_timer_get_user_data(t);
    lv_timer_delete(t);
    fn(NULL);
}
static void once(void (*fn)(lv_timer_t *), uint32_t ms) {
    lv_timer_t *t = lv_timer_create(once_cb, ms, (void *)fn);
    lv_timer_set_repeat_count(t, 1);
}

/* Hohe Bildrate NUR waehrend des Kampfes. Der duenne Zeiger braucht 60 fps;
 * dauerhaft waere es Akkuverschwendung. */
static void ar_fast_render(bool on) {
    lv_display_t *d = lv_display_get_default();
    if (!d) return;
    lv_timer_t *t = lv_display_get_refr_timer(d);
    if (t) lv_timer_set_period(t, on ? 16 : 33);
}

/* ---------- HP-Balken ---------- */
static lv_color_t hp_color(int v, int max) {
    int pct = (max > 0) ? (v * 100) / max : 0;
    return pct > 50 ? COL_GOOD : pct > 20 ? COL_WARN : COL_BAD;
}

/* Balken-Schritt: Wert (Promille) + Farbe */
static void ar_bar_exec(void *o, int32_t v) {
    lv_obj_t *bar = (lv_obj_t *)o;
    lv_bar_set_value(bar, v, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, hp_color((int)v, 1000), LV_PART_INDICATOR);
}

/* Balken kontrolliert auf den HP-Stand laufen lassen. 'done' feuert erst,
 * wenn er sein Ziel erreicht hat - daran haengt der ganze Kampf-Ablauf. */
static void ar_drain(lv_obj_t *bar, int hp, int maxhp, lv_anim_completed_cb_t done) {
    int to   = (maxhp > 0) ? (int)((long)hp * 1000 / maxhp) : 0;
    int from = lv_bar_get_value(bar);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, ar_bar_exec);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, DRAIN_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);   /* gleichmaessig = ruhig */
    lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
}

/* ---------- Ausfall, Treffer-Blitz, Schadenszahl (aus der alten Kette) ---------- */
static void ar_tx(void *o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t *)o, v, 0); }
static void ar_ty(void *o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t *)o, v, 0); }

/* Angreifer stoesst Richtung Ziel und federt zurueck. */
static void ar_lunge(lv_obj_t *spr, int dx, int dy, int dur) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, spr);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_duration(&a, dur);
    lv_anim_set_playback_duration(&a, dur);
    lv_anim_set_exec_cb(&a, ar_tx); lv_anim_set_values(&a, 0, dx); lv_anim_start(&a);
    lv_anim_set_exec_cb(&a, ar_ty); lv_anim_set_values(&a, 0, dy); lv_anim_start(&a);
}

static void ar_flashoff_cb(lv_timer_t *t) {
    lv_obj_t *spr = (lv_obj_t *)lv_timer_get_user_data(t);
    lv_obj_set_style_image_recolor_opa(spr, LV_OPA_0, 0);
    lv_timer_delete(t);
}

/* Getroffenes Beast: aufblitzen + Rueckstoss weg vom Angreifer. */
static void ar_hit_fx(lv_obj_t *spr, int tier, int recoilDir) {
    lv_obj_set_style_image_recolor(spr, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(spr, HIT_OPA[tier], 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, spr);
    lv_anim_set_exec_cb(&a, ar_tx);
    lv_anim_set_values(&a, 0, recoilDir * HIT_AMP[tier]);
    lv_anim_set_duration(&a, HIT_DUR[tier] / 2);
    lv_anim_set_playback_duration(&a, HIT_DUR[tier] / 2);
    lv_anim_start(&a);

    lv_timer_t *ft = lv_timer_create(ar_flashoff_cb, HIT_DUR[tier], spr);
    lv_timer_set_repeat_count(ft, 1);
}

/* Setzt die Recolor-Opazitaet eines Sprites (fuer Aufleucht-Animationen). */
static void ar_recolor_exec(void *o, int32_t v) {
    lv_obj_set_style_image_recolor_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
}

/* Volltreffer-Effekt: statt eines vollflaechigen Bildschirm-Blitzes (der auf
 * dem ESP32 ruckelt, weil die ganze Flaeche neu gezeichnet wird) leuchtet nur
 * das getroffene Sprite kurz kraeftig in Gold auf und wird staerker
 * zurueckgeschleudert. Nur das kleine Sprite wird neu gerendert -> fluessig. */
static void ar_crit_fx(lv_obj_t *spr, int recoilDir) {
    /* Kraeftiges goldenes Aufleuchten (heller als ein normaler Treffer). */
    lv_obj_set_style_image_recolor(spr, COL_GOLD_BRIGHT, 0);
    lv_obj_set_style_image_recolor_opa(spr, LV_OPA_COVER, 0);

    /* Heftiger, schneller Ruck - deutlich staerker als HIT_AMP[2]. */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, spr);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, ar_tx);
    lv_anim_set_values(&a, 0, recoilDir * 16);
    lv_anim_set_duration(&a, 90);
    lv_anim_set_playback_duration(&a, 210);
    lv_anim_start(&a);

    /* Aufleuchten sanft ausklingen lassen (Recolor-Opazitaet zurueck auf 0). */
    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, spr);
    lv_anim_set_exec_cb(&b, ar_recolor_exec);
    lv_anim_set_values(&b, LV_OPA_COVER, 0);
    lv_anim_set_duration(&b, 300);
    lv_anim_set_delay(&b, 60);
    lv_anim_start(&b);
}

static void ar_popup_del(lv_anim_t *a) { lv_obj_delete((lv_obj_t *)a->var); }

/* Schadenszahl, die nach oben wegschwebt. */
static void ar_popup(bool enemyTarget, int dmg, int tier, bool enemyAttacking) {
    lv_obj_t *l = lv_label_create(ar_battle);
    lv_obj_set_style_text_font(l, tier >= 2 ? &oxanium_22 : &oxanium_16, 0);
    lv_obj_set_style_text_color(l, enemyAttacking ? COL_DMG_ENEMY
                                : (tier >= 2 ? COL_GOLD_BRIGHT : lv_color_white()), 0);
    lv_label_set_text_fmt(l, "-%d", dmg);

    if (enemyTarget) lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -70, 48);
    else             lv_obj_align(l, LV_ALIGN_TOP_LEFT,   72, 178);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, l);
    lv_anim_set_exec_cb(&a, ar_ty);
    lv_anim_set_values(&a, 0, -42);
    lv_anim_set_duration(&a, 620);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, ar_popup_del);
    lv_anim_start(&a);
}

/* ============================================================
 *  Move-basierter Kampf
 *  ------------------------------------------------------------
 *  Statt Timing: zwei Buttons. Ein Klick spielt den Move, dann kontert der
 *  Gegner. Die schoenen Animationen (Ausfall, HP-Ablauf, Trefferzahl) bleiben -
 *  nur der Ausloeser und der Schaden kommen jetzt aus dem Move-System.
 * ============================================================ */

/* --- Mini-Animationen je Move-Typ --- */

/* Aura-Puls um ein Beast (Buff): kurzes farbiges Aufleuchten, das an- und
 * wieder abschwillt. Farbe passt zum Effekt (ATK rot, DEF blau, SPD gruen). */
static void ar_aura_off(lv_timer_t *t) {
    lv_obj_t *spr = (lv_obj_t *)lv_timer_get_user_data(t);
    lv_obj_set_style_image_recolor_opa(spr, LV_OPA_0, 0);
    lv_timer_delete(t);
}
static void ar_aura(lv_obj_t *spr, lv_color_t col) {
    lv_obj_set_style_image_recolor(spr, col, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, spr);
    lv_anim_set_exec_cb(&a, ar_recolor_exec);
    /* Kraeftiger und laenger als vorher (war 150/260) - sonst kaum sichtbar. */
    lv_anim_set_values(&a, 0, 210);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_playback_duration(&a, 460);
    lv_anim_set_repeat_count(&a, 1);   /* zweimal aufleuchten = deutlich als Buff erkennbar */
    lv_anim_start(&a);
    lv_timer_t *ft = lv_timer_create(ar_aura_off, 900, spr);
    lv_timer_set_repeat_count(ft, 1);
}

/* Buff-Bewegung: das Beast hebt kurz ab und senkt sich wieder - ein klares
 * "es staerkt sich"-Signal, ganz ohne die Angriffs-Lunge nach vorne. */
static void ar_bounce(lv_obj_t *spr) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, spr);
    lv_anim_set_exec_cb(&a, ar_ty);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_values(&a, 0, -22);          /* hoch */
    lv_anim_set_duration(&a, 220);
    lv_anim_set_playback_duration(&a, 300);  /* und wieder runter */
    lv_anim_start(&a);
}

/* Dauerhafte, sanfte Schwebe-Bewegung auf einem Wrapper-Objekt. Laeuft endlos
 * mit weichem ease-in-out und leichtem Versatz je Beast, damit die Arena auch
 * ohne Interaktion lebt. Der Wrapper floatet, das Sprite darin animiert
 * unabhaengig - so stoeren sich Float und Kampf-Animationen nicht. */
static void ar_float_start(lv_obj_t *wrap, int amp, int dur, int delay) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, wrap);
    lv_anim_set_exec_cb(&a, ar_ty);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_values(&a, 0, -amp);              /* sanft hoch ... */
    lv_anim_set_duration(&a, dur);
    lv_anim_set_playback_duration(&a, dur);       /* ... und zurueck */
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_delay(&a, delay);
    lv_anim_start(&a);
}

/* Kurzer heller Blitz ueber den ganzen Kampfbereich (Signature-Eskalation). */
/* Aura-Farbe nach Buff-Art. */
static lv_color_t ar_buff_color(const Move *m) {
    switch (m->effect) {
        case EF_BUFF_ATK: return COL_BAD;                 /* rot   */
        case EF_BUFF_DEF: return lv_color_hex(0x5b8dff);  /* blau  */
        case EF_BUFF_SPD: return COL_GOOD;                /* gruen */
        case EF_HEAL:     return COL_GOOD;
        default:          return COL_GOLD_BRIGHT;
    }
}

/* Fuer die Animation nehmen wir einen mittleren "Tier" (Ausfall-Heftigkeit),
 * der Schaden selbst kommt aus dem Move. Ein starker Treffer (Typvorteil oder
 * Signature) darf heftiger ausschlagen. */
static int ar_anim_tier(int dmg, int refHp) {
    if (refHp <= 0) return 1;
    int pct = dmg * 100 / refHp;
    return pct >= 22 ? 2 : pct >= 10 ? 1 : 0;
}

static int ar_lastDmg = 0, ar_lastTier = 1;

/* Statuszeile im Panel: was ist gerade passiert. */
static void ar_say(const char *txt, lv_color_t col) {
    lv_label_set_text(ar_fb, txt);
    lv_obj_set_style_text_color(ar_fb, col, 0);
}

/* Beschriftet die zwei Move-Buttons aus dem Spieler-Loadout. */
/* Fuellfarbe + Textfarbe fuer einen Move-Button je Kategorie. */
/* Fuellfarbe, Textfarbe und ein etwas dunklerer Border-Ton (gleiche Farbe,
 * abgedunkelt) fuer einen Move-Button je Kategorie - der Border gibt Tiefe. */
static void ar_move_colors(const Move *m, lv_color_t *bg, lv_color_t *txt, lv_color_t *bd) {
    switch (m->cat) {
        case CAT_SIGNATURE: *bg = COL_MV_SIG;   *txt = lv_color_black(); break;
        case CAT_BUFF:      *bg = COL_MV_BUFF;  *txt = COL_TEXT;         break;
        case CAT_SPECIAL:   *bg = COL_MV_SPEC;  *txt = COL_TEXT;         break;
        default:            *bg = COL_MV_BASIC; *txt = COL_TEXT;         break;
    }
    /* Border = gleiche Farbe, mit Schwarz abgedunkelt (mix ~65% Farbe). */
    *bd = lv_color_mix(lv_color_black(), *bg, 90);
}

static void ar_label_moves(void) {
    for (int i = 0; i < 2; i++) {
        int id = game_move_player(i + 1);
        const Move *m = move_get(id);

        /* Slot 2 gesperrt (nur Basis verfuegbar, noch kein Special ab Lvl 5)?
         * Dann neutraler, gedaempfter Button mit Hinweis. */
        if (i == 1 && game_slot2_locked()) {
            lv_label_set_text(ar_move_name[i], "Gesperrt");
            lv_label_set_text(ar_move_tag[i], "ab Level 5");
            lv_obj_set_style_bg_color(ar_move_btn[i], COL_BOX, 0);
            lv_obj_set_style_border_color(ar_move_btn[i], COL_BORDER, 0);
            lv_obj_set_style_text_color(ar_move_name[i], COL_DIM, 0);
            lv_obj_set_style_text_color(ar_move_tag[i],  COL_DIM, 0);
            continue;
        }

        lv_label_set_text(ar_move_name[i], m->name);
        /* Kurz-Tag: Schaden oder Buff-Art. */
        char tag[24];
        if (m->power > 0) snprintf(tag, sizeof(tag), "%d Schaden", m->power);
        else              snprintf(tag, sizeof(tag), "Buff");
        lv_label_set_text(ar_move_tag[i], tag);

        /* Vollflaechige Kategorie-Farbe + passende Textfarbe + dunklerer Border. */
        lv_color_t bg, txt, bd;
        ar_move_colors(m, &bg, &txt, &bd);
        lv_obj_set_style_bg_color(ar_move_btn[i], bg, 0);
        lv_obj_set_style_border_color(ar_move_btn[i], bd, 0);
        lv_obj_set_style_text_color(ar_move_name[i], txt, 0);
        lv_obj_set_style_text_color(ar_move_tag[i],  txt, 0);
    }
}

/* Stamina-Anzeige aktualisieren - nach jedem Zug aufrufen. */
static void ar_update_stamina(void) {
    if (ar_stamina)
        lv_label_set_text_fmt(ar_stamina, "Stamina  %d / %d",
                              game_arena_my_stamina(), STAMINA_MAX);
}

/* Buttons sperren/freigeben. Waehrend der Animation ist alles gesperrt (on=false).
 * Ist der Zug frei (on=true), haengt jeder Button zusaetzlich an seiner Stamina:
 * ein Move, den man sich nicht leisten kann, bleibt gedaempft und tot. */
static void ar_moves_enabled(bool on) {
    for (int i = 0; i < 2; i++) {
        bool playable = on && game_move_playable(i + 1);
        /* Vollflaechige Kategorie-Farbe bleibt (aus ar_label_moves) - hier nur
         * die Deckkraft: spielbar = satt, nicht spielbar = ausgegraut. */
        lv_obj_set_style_bg_opa(ar_move_btn[i], playable ? LV_OPA_COVER : LV_OPA_40, 0);
        if (playable) lv_obj_add_flag(ar_move_btn[i], LV_OBJ_FLAG_CLICKABLE);
        else          lv_obj_remove_flag(ar_move_btn[i], LV_OBJ_FLAG_CLICKABLE);
    }
    ar_update_stamina();
}

/* ---------- Kampf-Kette ---------- */
static void ar_win_cb(lv_timer_t *t)  { LV_UNUSED(t); ar_show_result(true);  }
static void ar_lose_cb(lv_timer_t *t) { LV_UNUSED(t); ar_show_result(false); }
static void ar_enemy_turn(lv_timer_t *t);
static void ar_round_start(lv_timer_t *t);

/* Nach dem Spieler-Schlag: HP abgelaufen -> Ausgang pruefen. */
static void ar_pdrain_done(lv_anim_t *a) {
    LV_UNUSED(a);
    if (game_arena_foe_hp() <= 0) once(ar_win_cb, RESULT_PAUSE);
    else                          once(ar_enemy_turn, TURN_GAP);
}

/* Spieler nutzt einen Slot. */
static void ar_player_move(int slot) {
    int heal = 0;
    int dmg  = game_arena_player_move(slot, &heal);
    const Move *m = move_get(game_move_player(slot));
    ar_update_stamina();   /* Abzug sofort sichtbar */

    if (m->power > 0) {
        bool crit = game_arena_was_crit();
        ar_lastTier = ar_anim_tier(dmg, game_arena_foe_max());
        /* Special und Signature schlagen sichtbar heftiger aus. */
        if (m->cat == CAT_SPECIAL   && ar_lastTier < 1) ar_lastTier = 1;
        if (m->cat == CAT_SIGNATURE) ar_lastTier = 2;
        if (crit) ar_lastTier = 2;   /* Volltreffer immer maximal wuchtig */

        ar_lunge(ar_me_spr, LUNGE_DX[ar_lastTier], -LUNGE_DY[ar_lastTier], LUNGE_DUR[ar_lastTier]);
        ar_popup(true, dmg, ar_lastTier, false);

        /* Volltreffer: das getroffene Gegner-Sprite leuchtet gold auf und wird
         * kraeftig zurueckgeschleudert (fluessig, kein Vollbild-Blitz).
         * Sonst der normale Treffer-Effekt. */
        if (crit) ar_crit_fx(ar_foe_spr, +1);
        else      ar_hit_fx(ar_foe_spr, ar_lastTier, +1);

        /* Meldung: Volltreffer hat Vorrang, dann Typvorteil, dann ein neu
         * gesetzter Status (Gift/Brand), sonst der Movename. */
        int st = game_arena_last_status();
        if (crit)                        ar_say("Volltreffer!", COL_GOLD_BRIGHT);
        else if (game_arena_type_edge()) ar_say("Sehr effektiv!", COL_GOLD_BRIGHT);
        else if (st == 2)                ar_say("Gegner vergiftet!", COL_GOOD);
        else if (st == 1)                ar_say("Gegner brennt!", COL_WARN);
        else                             ar_say(m->name, COL_TEXT);
        ar_drain(ar_foe_bar, game_arena_foe_hp(), game_arena_foe_max(), ar_pdrain_done);
    } else {
        /* Reiner Buff/Heal: KEINE Angriffs-Lunge nach vorne (das wirkt wie ein
         * Angriff und verwirrt). Stattdessen hebt das Beast kurz ab und leuchtet
         * in der Buff-Farbe auf. */
        ar_bounce(ar_me_spr);
        ar_aura(ar_me_spr, ar_buff_color(m));
        if (heal > 0) ar_say("Geheilt!", COL_GOOD);
        else          ar_say(m->name, COL_GOLD_BRIGHT);
        ar_drain(ar_me_bar, game_arena_my_hp(), game_arena_my_max(), NULL);
        once(ar_enemy_turn, TURN_GAP);
    }
}

/* --- Gegner --- */
static void ar_edrain_done(lv_anim_t *a) {
    LV_UNUSED(a);
    if (game_arena_my_hp() <= 0) once(ar_lose_cb, RESULT_PAUSE);
    else                         once(ar_round_start, TURN_GAP);
}
static void ar_e_land(lv_timer_t *t) {
    LV_UNUSED(t);
    int heal = 0, slot = 1;
    int dmg = game_arena_enemy_move(&heal, &slot);
    const Move *m = move_get(game_move_enemy(slot));

    if (m->power > 0) {
        bool crit = game_arena_was_crit();
        /* Special/Signature auch beim Gegner heftiger. */
        if (m->cat == CAT_SPECIAL   && ar_lastTier < 1) ar_lastTier = 1;
        if (m->cat == CAT_SIGNATURE) ar_lastTier = 2;
        if (crit) ar_lastTier = 2;

        ar_popup(false, dmg, ar_lastTier, true);
        /* Volltreffer des Gegners: mein Sprite leuchtet auf und wird
         * zurueckgeschleudert - kein Vollbild-Blitz. */
        if (crit) ar_crit_fx(ar_me_spr, -1);
        else      ar_hit_fx(ar_me_spr, ar_lastTier, -1);
        if (crit) ar_say("Volltreffer des Gegners!", COL_BAD);
        else      ar_say(m->name, COL_TEXT);
        ar_drain(ar_me_bar, game_arena_my_hp(), game_arena_my_max(), ar_edrain_done);
    } else {
        /* Gegner-Buff/Heal: ebenfalls nur abheben + Aura, keine Lunge. */
        ar_bounce(ar_foe_spr);
        ar_aura(ar_foe_spr, ar_buff_color(m));
        if (heal > 0) ar_say("Gegner heilt sich", COL_DIM);
        else          ar_say("Gegner rüstet auf", COL_DIM);
        ar_drain(ar_foe_bar, game_arena_foe_hp(), game_arena_foe_max(), NULL);
        once(ar_round_start, TURN_GAP);
    }
}
static void ar_enemy_turn(lv_timer_t *t) {
    LV_UNUSED(t);
    ar_lastTier = 1;
    ar_lunge(ar_foe_spr, -LUNGE_DX[1], LUNGE_DY[1], LUNGE_DUR[1]);
    once(ar_e_land, LAND_MS);
}

/* Rundenbeginn: Burn/Poison ticken, dann sind die Buttons wieder frei. */
static void ar_round_start(lv_timer_t *t) {
    LV_UNUSED(t);
    int pdot = 0, edot = 0;
    game_arena_tick(&pdot, &edot);

    /* Ticks als HP-Ablauf zeigen - mit fliegender Schadenszahl, damit klar
     * wird, dass Gift/Brand gerade wirken. */
    if (edot > 0) {
        ar_popup(true, edot, 0, false);
        ar_drain(ar_foe_bar, game_arena_foe_hp(), game_arena_foe_max(), NULL);
    }
    if (pdot > 0) {
        ar_popup(false, pdot, 0, true);
        ar_drain(ar_me_bar,  game_arena_my_hp(),  game_arena_my_max(),  NULL);
    }

    /* Konnte ein Tick den Kampf entscheiden? */
    if (game_arena_foe_hp() <= 0) { once(ar_win_cb,  RESULT_PAUSE); return; }
    if (game_arena_my_hp()  <= 0) { once(ar_lose_cb, RESULT_PAUSE); return; }

    /* Klare Meldung, welcher Status gerade zieht. */
    if (edot > 0 && game_arena_foe_poisoned())      ar_say("Gift wirkt", COL_GOOD);
    else if (edot > 0 && game_arena_foe_burning())  ar_say("Brand wirkt", COL_WARN);
    else if (pdot > 0 && game_arena_my_poisoned())  ar_say("Du bist vergiftet", COL_BAD);
    else if (pdot > 0 && game_arena_my_burning())   ar_say("Du brennst", COL_BAD);
    else if (pdot > 0 || edot > 0)                  ar_say("Statusschaden", COL_WARN);
    else                                            ar_say("Dein Zug", COL_DIM);

    ar_busy = false;
    ar_moves_enabled(true);
}

/* Klick auf einen Move-Button. */
static void ar_move_clicked(lv_event_t *e) {
    if (ar_state != AS_BATTLE || ar_busy) return;
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (!game_move_playable(slot)) return;   /* zu wenig Stamina / im Cooldown */

    ar_busy = true;               /* sperrt weitere Klicks bis Rundenende */
    ar_moves_enabled(false);
    ar_player_move(slot);
}

/* ============================================================
 *  Zustaende
 * ============================================================ */
static void ar_goto(int st) {
    ar_state = st;
    ar_fast_render(st == AS_BATTLE);   /* fluessige Ausfall-Animationen im Kampf */
    lv_obj_add_flag(ar_menu,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ar_battle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ar_result, LV_OBJ_FLAG_HIDDEN);

    if (st == AS_MENU)   lv_obj_remove_flag(ar_menu,   LV_OBJ_FLAG_HIDDEN);
    if (st == AS_BATTLE) lv_obj_remove_flag(ar_battle, LV_OBJ_FLAG_HIDDEN);
    if (st == AS_RESULT) lv_obj_remove_flag(ar_result, LV_OBJ_FLAG_HIDDEN);

    /* Schwebe-Bewegung nur im Kampf laufen lassen. Beim Verlassen anhalten und
     * die Wrapper zuruecksetzen, damit sie nicht im Off weiterlaufen. */
    if (st == AS_BATTLE) {
        ar_float_start(ar_me_wrap,  6, 1600, 0);      /* Spieler: ruhig       */
        ar_float_start(ar_foe_wrap, 5, 1900, 400);    /* Gegner: leicht anders */
    } else {
        lv_anim_delete(ar_me_wrap,  ar_ty);
        lv_anim_delete(ar_foe_wrap, ar_ty);
        lv_obj_set_style_translate_y(ar_me_wrap,  0, 0);
        lv_obj_set_style_translate_y(ar_foe_wrap, 0, 0);
    }

    /* Die Seiten-Punkte stoeren im Kampf - sie liegen genau im Panel. */
    for (int i = 0; i < PG_COUNT; i++) {
        if (st == AS_MENU) lv_obj_remove_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
        else               lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void ar_show_result(bool win) {

    game_arena_finish(win);

    ar_busy = false;
    lv_label_set_text(ar_r_big, win ? "SIEG!" : "NIEDERLAGE");
    lv_obj_set_style_text_color(ar_r_big, win ? COL_GOLD_BRIGHT : COL_BAD, 0);
    lv_label_set_text(ar_r_rew, win ? "+5 XP" : "kein XP");
    lv_obj_set_style_text_color(ar_r_rew, win ? COL_GOLD_BRIGHT : COL_FB_WEAK, 0);

    int loot = game_arena_loot();      /* nur bei Sieg, meistens -1 */

    /* Zwei Anordnungen. Mit Beute rueckt alles nach oben, damit das Icon Platz
     * bekommt und der Text nicht darin haengt - genau das war vorher der Fall. */
    if (loot >= 0) {
        scale_to_height(ar_r_icon, item_icon(loot), 56);
        lv_obj_align(ar_r_icon, LV_ALIGN_CENTER, 0, -4);
        lv_obj_remove_flag(ar_r_icon, LV_OBJ_FLAG_HIDDEN);

        lv_obj_align(ar_r_big, LV_ALIGN_CENTER, 0, -96);
        lv_obj_align(ar_r_rew, LV_ALIGN_CENTER, 0, -58);

        lv_label_set_text(ar_r_loot, ITEMS[loot].name);
        lv_obj_align(ar_r_loot, LV_ALIGN_CENTER, 0, 44);
        lv_obj_remove_flag(ar_r_loot, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text(ar_r_hint, "tippen zum Fortfahren");
        lv_obj_align(ar_r_hint, LV_ALIGN_CENTER, 0, 86);
    } else {
        lv_obj_add_flag(ar_r_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ar_r_loot, LV_OBJ_FLAG_HIDDEN);

        lv_obj_align(ar_r_big, LV_ALIGN_CENTER, 0, -40);
        lv_obj_align(ar_r_rew, LV_ALIGN_CENTER, 0,   0);

        /* Level-Up und Evolution zeigt jetzt das uebergreifende Popup (kommt
         * direkt nach diesem Screen), damit es ueberall gleich aussieht. Hier
         * nur noch der Beute-Hinweis. */
        lv_label_set_text_fmt(ar_r_hint, "noch %d mit Beute\n\ntippen zum Fortfahren",
                              game_arena_left());
        lv_obj_align(ar_r_hint, LV_ALIGN_CENTER, 0, 52);
    }

    ar_goto(AS_RESULT);
}

static void ar_result_tap(lv_event_t *e) {
    LV_UNUSED(e);
    if (!is_tap()) return;
    ar_goto(AS_MENU);
    refresh_arena();
}

/* Gemeinsamer Aufbau fuer JEDEN Kampf (normaler Gegner wie Boss). Erwartet,
 * dass game_arena_begin bzw. game_arena_begin_boss schon gelaufen ist. */
static void ar_enter_fight(void) {
    /* Beim Boss kommt das Sprite aus img_boss.c, sonst das Beast der Stufe. */
    if (game_arena_boss())
        scale_to_height(ar_foe_spr, asset_boss(game_arena_boss()), AR_FOE_H);
    else
        scale_to_height(ar_foe_spr, asset_beast(game_arena_enemy_id(), game_arena_enemy_stage()), AR_FOE_H);
    scale_to_height(ar_me_spr,  asset_beast(g_state.beastId, game_player_stage()), AR_ME_H);

    lv_label_set_text(ar_foe_name, game_arena_enemy_name());
    lv_label_set_text(ar_me_name,  game_player_name());
    lv_label_set_text_fmt(ar_foe_lv, "Lv %d", game_arena_level());
    lv_label_set_text_fmt(ar_me_lv,  "Lv %d", game_arena_level());

    lv_bar_set_value(ar_foe_bar, 1000, LV_ANIM_OFF);
    lv_bar_set_value(ar_me_bar,  1000, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ar_foe_bar, COL_GOOD, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ar_me_bar,  COL_GOOD, LV_PART_INDICATOR);
    lv_label_set_text(ar_fb, "");

    /* Reste aus dem letzten Kampf entfernen */
    lv_anim_delete(ar_foe_spr, NULL);
    lv_anim_delete(ar_me_spr,  NULL);
    lv_obj_set_style_translate_x(ar_foe_spr, 0, 0);
    lv_obj_set_style_translate_y(ar_foe_spr, 0, 0);
    lv_obj_set_style_translate_x(ar_me_spr,  0, 0);
    lv_obj_set_style_translate_y(ar_me_spr,  0, 0);
    lv_obj_set_style_image_recolor_opa(ar_foe_spr, LV_OPA_0, 0);
    lv_obj_set_style_image_recolor_opa(ar_me_spr,  LV_OPA_0, 0);

    /* Die zwei Move-Buttons aus dem Loadout beschriften. */
    ar_label_moves();

    /* Typ-Vorteil vorab anzeigen, damit man die Attacke bewusst waehlt. */
    if (game_arena_boss())           ar_say("BOSSKAMPF!", COL_GOLD_BRIGHT);
    else if (game_arena_type_edge()) ar_say("Typ-Vorteil!", COL_GOLD_BRIGHT);
    else                             ar_say("Dein Zug", COL_DIM);

    ar_goto(AS_BATTLE);

    if (game_arena_faster()) {   /* schnellerer Gegner eroeffnet */
        ar_busy = true;
        ar_moves_enabled(false);
        once(ar_enemy_turn, OPENING_GAP);
    } else {
        ar_busy = false;
        ar_moves_enabled(true);
    }
}

static void ar_start_battle(void) {
    /* Kaempfe sind unbegrenzt - nur die echten Blocker pruefen. */
    if (adv_is_away()) return;                       /* nicht da */
    if ((int)g_vit.energy < VIT_COST_FIGHT) return;
    if (!game_arena_begin()) return;
    vit_spend_energy(VIT_COST_FIGHT);   /* der Kampf kostet, nicht der Sieg */
    ar_enter_fight();
}

/* Boss-Kampf nach einer Expedition. Kostet bewusst KEINE Energie - das
 * Abenteuer hat schon genug gekostet, und der Boss ist ein Geschenk. */
static void ar_start_boss(int which) {
    if (!game_arena_begin_boss(which)) return;
    ar_enter_fight();
}

static void ar_start_clicked(lv_event_t *e) {
    LV_UNUSED(e);
    if (!is_tap()) return;
    if (vit_is_collapsed()) return;   /* grauer Knopf reicht nicht - der Klick muss auch ins Leere gehen */
    ar_start_battle();
}

static void ar_loadout_clicked(lv_event_t *e) {
    LV_UNUSED(e);
    if (!is_tap()) return;
    lo_open();
}

/* ============================================================
 *  Aufbau der Arena
 * ============================================================ */
/* Werte-Box: Name, Level, HP-Balken. */
static lv_obj_t* ar_hpbox(lv_obj_t *p, bool left, int y,
                          lv_obj_t **name, lv_obj_t **lvl, lv_obj_t **bar) {
    lv_obj_t *b = lv_obj_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, AR_BOX_W, AR_BOX_H);
    lv_obj_align(b, left ? LV_ALIGN_TOP_LEFT : LV_ALIGN_TOP_RIGHT,
                 left ? AR_BOX_M : -AR_BOX_M, y);
    lv_obj_set_style_bg_color(b, COL_HPBOX, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_90, 0);
    lv_obj_set_style_radius(b, UI_RADIUS, 0);
    lv_obj_set_style_border_color(b, COL_BORDER, 0);
    lv_obj_set_style_border_width(b, UI_BORDER, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);

    *name = ui_label(b, &oxanium_16, COL_TEXT);
    lv_obj_align(*name, LV_ALIGN_TOP_LEFT, 11, 5);

    *lvl = ui_label(b, &oxanium_16, COL_DIM);
    lv_obj_align(*lvl, LV_ALIGN_TOP_RIGHT, -11, 6);

    *bar = lv_bar_create(b);
    lv_obj_set_size(*bar, AR_BOX_W - 24, 9);
    lv_obj_align(*bar, LV_ALIGN_TOP_LEFT, 12, 32);
    lv_obj_set_style_bg_color(*bar, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(*bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(*bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(*bar, 4, LV_PART_INDICATOR);
    lv_bar_set_range(*bar, 0, 1000);      /* Promille -> feine Bewegung */
    lv_bar_set_value(*bar, 1000, LV_ANIM_OFF);
    return b;
}

/* ============================================================
 *  Loadout-Ansicht — die zwei Attacken belegen
 * ============================================================ */
static void lo_arm_tick(void) {
    if (lo_armed || !lo_root) return;
    if (lv_obj_has_flag(lo_root, LV_OBJ_FLAG_HIDDEN)) return;
    if (millis() - lo_openMs < 250) return;
    if (millis() - g_touchLastMs >= ARM_MS) lo_armed = true;
}

/* Kurzbeschreibung eines Moves fuer die Auswahl-Zeile. */
/* Zwei kurze Zeilen statt einer langen (die ragte aus dem Button):
 *   line1: Kategorie + Schaden ODER Buff-Wirkung
 *   line2: Effekt + Stamina */
static void lo_move_desc(int moveId, char *buf, int n) {
    const Move *m = move_get(moveId);
    const char *cat =
        m->cat == CAT_BASIC     ? "Basis" :
        m->cat == CAT_SIGNATURE ? "Signature" :
        m->cat == CAT_BUFF      ? "Buff" : "Special";

    /* Effekt sehr kurz halten. */
    char eff[20] = "";
    switch (m->effect) {
        case EF_BURN:      snprintf(eff, sizeof(eff), " Brand");    break;
        case EF_POISON:    snprintf(eff, sizeof(eff), " Gift");     break;
        case EF_BUFF_ATK:  snprintf(eff, sizeof(eff), " +%d ATK", m->magPct); break;
        case EF_BUFF_DEF:  snprintf(eff, sizeof(eff), " +%d DEF", m->magPct); break;
        case EF_BUFF_SPD:  snprintf(eff, sizeof(eff), " +%d SPD", m->magPct); break;
        case EF_HEAL:      snprintf(eff, sizeof(eff), " Heilung"); break;
        case EF_DEF_IGNORE:snprintf(eff, sizeof(eff), " DEF-");    break;
        default: break;
    }

    int st = move_stamina_cost(moveId);
    /* Eine knappe Zeile. Beispiel: "Special · 13 · Gift · 3 St" */
    if (m->power > 0)
        snprintf(buf, n, "%s · %d%s · %d St", cat, m->power, eff, st);
    else
        snprintf(buf, n, "%s ·%s · %d St", cat, eff[0] ? eff : " Wirkung", st);
}

static void lo_close(void) {
    lv_obj_add_flag(lo_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lo_pick, LV_OBJ_FLAG_HIDDEN);
}

/* Die zwei Slot-Karten mit dem aktuellen Loadout beschriften. */
static void lo_refresh_slots(void) {
    for (int i = 0; i < 2; i++) {
        int id = loadout_slot(i + 1);
        const Move *m = move_get(id);
        lv_label_set_text(lo_slot_lbl[i], m->name);
        char d[64]; lo_move_desc(id, d, sizeof(d));
        lv_label_set_text(lo_slot_sub[i], d);
    }
}

/* Auswahl-Liste fuer den gerade gewaehlten Slot fuellen. */
static void lo_open_pick(int slot) {
    lo_editSlot = slot;
    MoveType t = beast_type(g_state.beastId);
    int ids[MOVES_PER_TYPE];
    moves_of_type(t, ids);

    lv_label_set_text(lo_pick_title, "Slot 2 belegen");

    for (int i = 0; i < MOVES_PER_TYPE; i++) {
        int id = ids[i];
        const Move *m = move_get(id);
        bool unlocked = loadout_unlocked(id);

        if (unlocked) {
            /* Frei: normal, waehlbar. Beschreibung sichtbar, kein Schloss. */
            lv_label_set_text(lo_pick_lbl[i], m->name);
            char d[64]; lo_move_desc(id, d, sizeof(d));
            lv_label_set_text(lo_pick_sub[i], d);

            lv_obj_set_style_bg_color(lo_pick_btn[i], COL_BOX, 0);
            lv_obj_set_style_border_color(lo_pick_btn[i], COL_GOLD, 0);
            lv_obj_set_style_bg_opa(lo_pick_btn[i], LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(lo_pick_lbl[i], COL_GOLD_BRIGHT, 0);
            lv_obj_set_style_text_color(lo_pick_sub[i], COL_TEXT, 0);
            lv_obj_add_flag(lo_pick_lock[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(lo_pick_sub[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lo_pick_btn[i], LV_OBJ_FLAG_CLICKABLE);
        } else {
            /* Gesperrt: Name gedaempft, STATT der Beschreibung die kurze
             * Freischalt-Bedingung ("ab Level 15"). */
            lv_label_set_text(lo_pick_lbl[i], m->name);

            UnlockSlot us = m->cat == CAT_SIGNATURE ? UNLOCK_SIG :
                            m->cat == CAT_BUFF      ? UNLOCK_BUFF :
                            (id == (int)t*MOVES_PER_TYPE + 2) ? UNLOCK_SP2 : UNLOCK_SP1;
            char lk[32];
            snprintf(lk, sizeof(lk), "Gesperrt  ·  %s", loadout_unlock_hint(us));
            lv_label_set_text(lo_pick_lock[i], lk);

            lv_obj_set_style_bg_color(lo_pick_btn[i], COL_PANEL, 0);
            lv_obj_set_style_border_color(lo_pick_btn[i], COL_BORDER, 0);
            lv_obj_set_style_bg_opa(lo_pick_btn[i], LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(lo_pick_lbl[i], COL_DIM, 0);
            lv_obj_add_flag(lo_pick_sub[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(lo_pick_lock[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(lo_pick_btn[i], LV_OBJ_FLAG_CLICKABLE);
        }
    }
    lv_obj_remove_flag(lo_pick, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(lo_pick);
}

static void lo_slot_clicked(lv_event_t *e) {
    if (!lo_armed || !is_tap()) return;
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    lo_open_pick(slot);
    /* Sonst schlaegt derselbe Fingerkontakt auf die gerade geoeffnete
     * Auswahlliste durch und waehlt sofort ungewollt einen Move. lo_arm_tick
     * macht die Karten nach der naechsten Ruhepause wieder scharf. */
    lo_armed = false;
    lo_openMs = millis();
}

static void lo_pick_clicked(lv_event_t *e) {
    if (!lo_armed || !is_tap()) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    MoveType t = beast_type(g_state.beastId);
    int ids[MOVES_PER_TYPE];
    moves_of_type(t, ids);
    int id = ids[idx];

    bool ok = (lo_editSlot == 1) ? loadout_set_slot1(id) : loadout_set_slot2(id);
    if (ok) {
        lv_obj_add_flag(lo_pick, LV_OBJ_FLAG_HIDDEN);
        lo_refresh_slots();
        /* Rueckkehr zu den Slot-Karten: erneut entschaerfen, damit der
         * Auswahl-Klick nicht gleich eine Slot-Karte wieder oeffnet. */
        lo_armed = false;
        lo_openMs = millis();
    }
}

static void lo_bg_clicked(lv_event_t *e) {
    if (lv_event_get_target(e) != lo_root) return;
    if (lo_armed) lo_close();
}

static void lo_open(void) {
    if (!game_has_beast()) return;
    lo_armed  = false;
    lo_openMs = millis();
    lo_refresh_slots();
    lv_obj_add_flag(lo_pick, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lo_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(lo_root);
}

static void build_loadout(lv_obj_t *scr) {
    /* Vollflaechiges Overlay mit abgedunkeltem Hintergrund. */
    lo_root = lv_obj_create(scr);
    lv_obj_remove_style_all(lo_root);
    lv_obj_set_size(lo_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(lo_root, COL_PAGE, 0);
    lv_obj_set_style_bg_opa(lo_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(lo_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(lo_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lo_root, lo_bg_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(lo_root, LV_OBJ_FLAG_HIDDEN);

    ui_title(lo_root, "ATTACKEN");

    /* Zwei grosse Slot-Karten. Slot 1 ist FEST (Basis-Angriff), nur Slot 2
     * ist waehlbar. */
    static const char *SLOTNAME[2] = { "SLOT 1  ·  fester Basisangriff", "SLOT 2  ·  frei wählbar" };
    for (int i = 0; i < 2; i++) {
        lo_slot_btn[i] = ui_card(lo_root, 70 + i * 118, 104);

        /* Nur Slot 2 ist antippbar. */
        if (i == 1) {
            lv_obj_add_flag(lo_slot_btn[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(lo_slot_btn[i], lo_slot_clicked, LV_EVENT_CLICKED,
                                (void *)(intptr_t)(i + 1));
            lv_obj_set_style_border_color(lo_slot_btn[i], COL_GOLD, 0);
        } else {
            lv_obj_set_style_border_color(lo_slot_btn[i], COL_BORDER, 0);
        }

        lv_obj_t *cap = ui_label(lo_slot_btn[i], &oxanium_16, COL_DIM);
        lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 16, 12);
        lv_label_set_text(cap, SLOTNAME[i]);

        lo_slot_lbl[i] = ui_label(lo_slot_btn[i], &oxanium_22, COL_GOLD_BRIGHT);
        lv_obj_align(lo_slot_lbl[i], LV_ALIGN_TOP_LEFT, 16, 40);

        lo_slot_sub[i] = ui_label(lo_slot_btn[i], &oxanium_16, COL_TEXT);
        lv_obj_align(lo_slot_sub[i], LV_ALIGN_TOP_LEFT, 16, 72);
    }

    lv_obj_t *hint = ui_label(lo_root, &oxanium_16, COL_DIM);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint, UI_CARD_W);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 320);
    lv_label_set_text(hint, "Slot 2 tippen, um die Attacke zu wechseln");

    /* --- Auswahl-Liste (liegt ueber den Slot-Karten) --- */
    lo_pick = lv_obj_create(lo_root);
    lv_obj_remove_style_all(lo_pick);
    lv_obj_set_size(lo_pick, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(lo_pick, COL_PAGE, 0);
    lv_obj_set_style_bg_opa(lo_pick, LV_OPA_COVER, 0);
    lv_obj_remove_flag(lo_pick, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(lo_pick, LV_OBJ_FLAG_HIDDEN);

    lo_pick_title = ui_title(lo_pick, "Slot 2 belegen");

    /* Fuenf Move-Karten, hoeher (72) fuer Name + Beschreibung + Lock-Zeile. */
    for (int i = 0; i < MOVES_PER_TYPE; i++) {
        lo_pick_btn[i] = ui_card(lo_pick, 60 + i * 74, 66);
        lv_obj_add_event_cb(lo_pick_btn[i], lo_pick_clicked, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lo_pick_lbl[i] = ui_label(lo_pick_btn[i], &oxanium_22, COL_GOLD_BRIGHT);
        lv_obj_align(lo_pick_lbl[i], LV_ALIGN_TOP_LEFT, 14, 8);

        lo_pick_sub[i] = ui_label(lo_pick_btn[i], &oxanium_16, COL_TEXT);
        lv_obj_align(lo_pick_sub[i], LV_ALIGN_TOP_LEFT, 14, 38);

        /* Lock-Zeile an derselben Stelle wie die Beschreibung (die bei
         * gesperrten Karten ausgeblendet ist) - klar lesbar, keine Ueberlappung. */
        lo_pick_lock[i] = ui_label(lo_pick_btn[i], &oxanium_16, COL_WARN);
        lv_obj_align(lo_pick_lock[i], LV_ALIGN_TOP_LEFT, 14, 38);
    }
}

static void build_arena(lv_obj_t *p) {
    /* Zwei Hintergruende, je nach Zustand:
     *   Menue  -> die Hoehle MIT den lauernden Beasts. Sie macht Lust auf den
     *             Kampf und zeigt, wer da unten wartet.
     *   Kampf  -> die LEERE Hoehle. Zuschauer waeren dort nur Unruhe; es zaehlt
     *             jeder Blick auf den Zeiger.
     * Beide liegen IN ihrem Container, nicht auf der Seite - so schaltet sich
     * der Hintergrund automatisch mit dem Zustand um. */

    /* --- Menue --- */
    ar_menu = lv_obj_create(p);
    lv_obj_remove_style_all(ar_menu);
    lv_obj_set_size(ar_menu, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(ar_menu, LV_OBJ_FLAG_SCROLLABLE);

    dark_bg(ar_menu, &img_bg_arena, BG_DARKEN);   /* mit Beasts */

    ar_menu_title = ui_title(ar_menu, "ARENA");

    ar_start = ui_button(ar_menu, 120, UI_CARD_H + 6);
    lv_obj_add_event_cb(ar_start, ar_start_clicked, LV_EVENT_CLICKED, NULL);
    ar_start_lbl = ui_label(ar_start, &oxanium_22, lv_color_black());
    lv_obj_center(ar_start_lbl);

    /* Zweiter Knopf: die zwei Attacken belegen. */
    ar_loadout_btn = ui_button(ar_menu, 204, UI_CARD_H - 8);
    lv_obj_set_style_bg_color(ar_loadout_btn, COL_BOX, 0);
    lv_obj_set_style_border_color(ar_loadout_btn, COL_GOLD, 0);
    lv_obj_add_event_cb(ar_loadout_btn, ar_loadout_clicked, LV_EVENT_CLICKED, NULL);
    ar_loadout_lbl = ui_label(ar_loadout_btn, &oxanium_22, COL_GOLD_BRIGHT);
    lv_obj_center(ar_loadout_lbl);
    lv_label_set_text(ar_loadout_lbl, "ATTACKEN");

    ar_info = ui_label(ar_menu, &oxanium_16, COL_DIM);
    lv_obj_set_style_text_align(ar_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ar_info, LV_ALIGN_TOP_MID, 0, 292);
    lv_label_set_text(ar_info, "");

    /* Zwei Zahlen statt einer Textzeile: KÄMPFE HEUTE | ENERGIE JE KAMPF */
    ar_left = ui_label(ar_menu, &oxanium_22, COL_GOLD_BRIGHT);
    ar_cost = ui_label(ar_menu, &oxanium_22, COL_GOLD_BRIGHT);

    ar_left_cap = ui_label(ar_menu, &oxanium_16, COL_DIM);
    lv_obj_align(ar_left_cap, LV_ALIGN_TOP_MID, -70, 320);
    lv_label_set_text(ar_left_cap, "MIT BEUTE");

    ar_cost_cap = ui_label(ar_menu, &oxanium_16, COL_DIM);
    lv_obj_align(ar_cost_cap, LV_ALIGN_TOP_MID, 70, 320);
    lv_label_set_text(ar_cost_cap, "ENERGIE");

    /* --- Kampf: ueber Kreuz, wie auf der Watch --- */
    ar_battle = lv_obj_create(p);
    lv_obj_remove_style_all(ar_battle);
    lv_obj_set_size(ar_battle, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(ar_battle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ar_battle, LV_OBJ_FLAG_HIDDEN);

    dark_bg(ar_battle, &img_bg_kampf, BG_DARKEN);     /* leere Hoehle */

    /* Jedes Sprite steckt in einem transparenten Wrapper. Der WRAPPER floatet
     * dauerhaft sanft auf und ab (ar_float_*), das SPRITE darin macht die
     * Kampf-Animationen (Lunge, Treffer, Buff). Beide Bewegungen ueberlagern
     * sich additiv, ohne sich zu ueberschreiben. */
    /* Der Wrapper ist gross genug, dass kein Sprite abgeschnitten wird
     * (Sprites sind hoechstens ~150px hoch). Er hat keinen sichtbaren Stil,
     * dient nur als Traeger fuer die Schwebe-Bewegung. */
    ar_foe_wrap = lv_obj_create(ar_battle);
    lv_obj_remove_style_all(ar_foe_wrap);
    lv_obj_set_size(ar_foe_wrap, 170, 170);
    lv_obj_align(ar_foe_wrap, LV_ALIGN_CENTER, 98, -126);
    lv_obj_remove_flag(ar_foe_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ar_foe_wrap, LV_OBJ_FLAG_CLICKABLE);
    ar_foe_spr = lv_image_create(ar_foe_wrap);        /* Gegner rechts oben */
    lv_obj_center(ar_foe_spr);

    ar_me_wrap = lv_obj_create(ar_battle);
    lv_obj_remove_style_all(ar_me_wrap);
    lv_obj_set_size(ar_me_wrap, 170, 170);
    lv_obj_align(ar_me_wrap, LV_ALIGN_CENTER, -98, -40);
    lv_obj_remove_flag(ar_me_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ar_me_wrap, LV_OBJ_FLAG_CLICKABLE);
    ar_me_spr = lv_image_create(ar_me_wrap);          /* Spieler links      */
    lv_obj_center(ar_me_spr);

    ar_hpbox(ar_battle, true,  AR_FOE_BOX_Y, &ar_foe_name, &ar_foe_lv, &ar_foe_bar);
    ar_hpbox(ar_battle, false, AR_ME_BOX_Y,  &ar_me_name,  &ar_me_lv,  &ar_me_bar);
    lv_obj_set_style_text_color(ar_me_name, COL_GOLD_BRIGHT, 0);

    /* Panel unten: Rueckmeldung + Timing-Leiste */
    lv_obj_t *panel = lv_obj_create(ar_battle);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, LV_PCT(100), AR_PANEL_H);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    ar_fb = ui_label(panel, &oxanium_22, COL_TEXT);
    lv_obj_align(ar_fb, LV_ALIGN_TOP_MID, 0, 6);
    lv_label_set_text(ar_fb, "");

    /* Stamina-Stand, klein zwischen Statuszeile und Buttons. */
    ar_stamina = ui_label(panel, &oxanium_16, COL_GOLD);
    lv_obj_align(ar_stamina, LV_ALIGN_TOP_MID, 0, 34);
    lv_label_set_text(ar_stamina, "Stamina  6 / 10");

    /* Zwei Move-Buttons statt der Timing-Leiste. Gross, klar getrennt -
     * touch-sicher. Beschriftung kommt beim Kampfstart aus dem Loadout. */
    for (int i = 0; i < 2; i++) {
        ar_move_btn[i] = lv_obj_create(panel);
        lv_obj_remove_style_all(ar_move_btn[i]);
        lv_obj_set_size(ar_move_btn[i], 168, 66);
        lv_obj_align(ar_move_btn[i], LV_ALIGN_BOTTOM_MID,
                     i == 0 ? -88 : 88, -30);
        /* Vollflaechig gefuellt (Farbe kommt je Kategorie aus ar_label_moves),
         * mit dezentem dunklerem Border in derselben Farbe fuer Tiefe. */
        lv_obj_set_style_bg_color(ar_move_btn[i], COL_MV_BASIC, 0);
        lv_obj_set_style_bg_opa(ar_move_btn[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(ar_move_btn[i], UI_RADIUS, 0);
        lv_obj_set_style_border_width(ar_move_btn[i], 2, 0);
        lv_obj_remove_flag(ar_move_btn[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(ar_move_btn[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(ar_move_btn[i], ar_move_clicked, LV_EVENT_PRESSED,
                            (void *)(intptr_t)(i + 1));

        ar_move_name[i] = ui_label(ar_move_btn[i], &oxanium_16, COL_TEXT);
        lv_obj_align(ar_move_name[i], LV_ALIGN_TOP_MID, 0, 12);

        ar_move_tag[i] = ui_label(ar_move_btn[i], &oxanium_16, COL_TEXT);
        lv_obj_align(ar_move_tag[i], LV_ALIGN_BOTTOM_MID, 0, -12);
    }

    /* --- Ergebnis --- */
    ar_result = lv_obj_create(p);
    lv_obj_remove_style_all(ar_result);
    lv_obj_set_size(ar_result, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(ar_result, 0, 0);
    lv_obj_set_style_bg_color(ar_result, COL_PAGE, 0);
    lv_obj_set_style_bg_opa(ar_result, LV_OPA_COVER, 0);
    lv_obj_remove_flag(ar_result, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ar_result, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ar_result, ar_result_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(ar_result, LV_OBJ_FLAG_HIDDEN);

    ar_r_big  = ui_label(ar_result, &oxanium_22, COL_GOLD_BRIGHT);
    ar_r_rew  = ui_label(ar_result, &oxanium_22, COL_GOLD_BRIGHT);
    ar_r_hint = ui_label(ar_result, &oxanium_16, COL_DIM);
    lv_obj_set_style_text_align(ar_r_hint, LV_TEXT_ALIGN_CENTER, 0);

    ar_r_icon = lv_image_create(ar_result);
    lv_obj_add_flag(ar_r_icon, LV_OBJ_FLAG_HIDDEN);

    ar_r_loot = ui_label(ar_result, &oxanium_22, COL_TEXT);
    lv_obj_add_flag(ar_r_loot, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_arena(void) {
    if (ar_state != AS_MENU) return;

    int  left = game_arena_left();               /* nur noch fuer die Beute-Info */
    int  ene  = (int)g_vit.energy;
    bool has  = game_has_beast();
    bool away = adv_is_away();
    /* Kaempfe sind unbegrenzt (Training) - der Knopf haengt daher NICHT mehr an
     * der Kampfzahl, sondern nur an den echten Blockern: Karte da, nicht
     * unterwegs, nicht bewusstlos, genug Energie. */
    bool ok   = has && !away && !vit_is_collapsed() && ene >= VIT_COST_FIGHT;

    lv_label_set_text(ar_start_lbl, ok ? "KAMPF STARTEN" : "NICHT BEREIT");
    lv_obj_set_style_bg_color(ar_start, ok ? COL_GOLD : COL_BOX, 0);
    lv_obj_set_style_border_color(ar_start, ok ? COL_GOLD_BRIGHT : COL_BORDER, 0);
    lv_obj_set_style_text_color(ar_start_lbl, ok ? lv_color_black() : COL_DIM, 0);
    lv_obj_center(ar_start_lbl);

    if (!has)
        lv_label_set_text(ar_info, "Keine Karte eingelegt");
    else if (vit_is_collapsed())
        lv_label_set_text(ar_info, "Dein Beast ist bewusstlos");
    else if (away)
        lv_label_set_text(ar_info, "Dein Beast ist unterwegs");
    else if (ene < VIT_COST_FIGHT)
        lv_label_set_text_fmt(ar_info, "Zu müde   ·   %d Energie nötig", VIT_COST_FIGHT);
    else if (game_loot_fights_today() >= SH_BATTLES_PER_DAY)
        /* Kaempfen geht weiter (Level, Bindung) - nur Beute gibt es nicht mehr. */
        lv_label_set_text(ar_info, "Nur noch Training · keine Beute");
    else
        lv_label_set_text(ar_info, "");
    lv_obj_align(ar_info, LV_ALIGN_TOP_MID, 0, 292);

    /* Die grossen Zahlen und der Info-Text teilen sich die Zeile bei y=292.
     * Deshalb entweder ODER: steht ein Info-Text, verschwinden die Zahlen.
     * Genau hier lag der Ueberlapp - "Zu müde" erschien UEBER den Zahlen. */
    bool infoLeer = (lv_label_get_text(ar_info)[0] == '\0');
    bool zeigeZahlen = has && !away && infoLeer;

    lv_label_set_text_fmt(ar_left, "%d/%d", left, SH_BATTLES_PER_DAY);
    lv_label_set_text_fmt(ar_cost, "%d", VIT_COST_FIGHT);
    /* Ist die Tagesbeute aufgebraucht, die Zahl gedaempft - Kaempfe gehen
     * weiter (Info-Zeile erklaert das), aber es gibt nichts mehr zu holen. */
    lv_obj_set_style_text_color(ar_left, left > 0 ? COL_GOLD_BRIGHT : COL_DIM, 0);
    lv_obj_align(ar_left, LV_ALIGN_TOP_MID, -70, 292);
    lv_obj_align(ar_cost, LV_ALIGN_TOP_MID,  70, 292);

    for (int i = 0; i < 2; i++) {
        lv_obj_t *o = (i == 0) ? ar_left : ar_cost;
        if (zeigeZahlen) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else             lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    if (zeigeZahlen) {
        lv_obj_remove_flag(ar_left_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ar_cost_cap, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ar_left_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ar_cost_cap, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ============================================================
 *  EINSTELLUNGEN — Wisch nach unten
 *  ------------------------------------------------------------
 *  Uhrzeit und Helligkeit. Beides greift sofort; die Helligkeit wird aber
 *  erst beim LOSLASSEN gesichert - sonst schriebe jeder Pixel Reglerweg in
 *  den Flash.
 * ============================================================ */
static void set_close(void) {
    lv_obj_add_flag(set_root, LV_OBJ_FLAG_HIDDEN);
}

static bool set_is_open(void) {
    return set_root && !lv_obj_has_flag(set_root, LV_OBJ_FLAG_HIDDEN);
}

static void set_arm_tick(void) {
    if (set_armed || !set_is_open()) return;
    if (millis() - g_touchLastMs >= ARM_MS) set_armed = true;
}

/* KEIN Schliessen per Tipp. Die Knoepfe liegen dicht beieinander, und ein
 * Fehlgriff daneben hat einen staendig aus den Einstellungen geworfen.
 * Nur der Wisch schliesst - das ist eine bewusste Geste. */
static void set_bg_clicked(lv_event_t *e) {
    LV_UNUSED(e);
}

static void set_show_time(void) {
    lv_label_set_text_fmt(set_time, "%02d:%02d", set_h, set_m);
    lv_obj_align(set_time, LV_ALIGN_TOP_MID, 0, 118);   /* mittig in der Knopfreihe */
}

/* delta in Minuten, laeuft sauber ueber Mitternacht. */
static void set_bump(int delta) {
    int total = ((set_h * 60 + set_m + delta) % 1440 + 1440) % 1440;
    set_h = total / 60;
    set_m = total % 60;
    shp_set_clock(set_h, set_m);       /* sofort in die RTC */
    set_show_time();
}

static void set_time_clicked(lv_event_t *e) {
    if (!set_armed || !is_tap()) return;
    set_bump((int)(intptr_t)lv_event_get_user_data(e));
}

static void set_slider_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    int v = lv_slider_get_value(set_slider);

    shp_set_brightness((uint8_t)v);                 /* sofort sichtbar */

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        shp_commit_brightness();                    /* erst beim Loslassen sichern */
}

static void set_open(void) {
    shp_clock(&set_h, &set_m);          /* aktuelle Uhrzeit als Startwert */
    set_show_time();

    lv_slider_set_value(set_slider, shp_get_brightness(), LV_ANIM_OFF);

    set_armed = false;
    set_show_batt();                 /* Schlafbilanz frisch berechnen */
    lv_obj_remove_flag(set_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(set_root);
}

/* Ein +/- Knopf im Uhrzeit-Block. */
#define SET_BTN_W    56
#define SET_BTN_H    56
#define SET_ROW_Y   104          /* Oberkante der Knopfreihe */

static void set_step_btn(lv_obj_t *p, const char *txt, int x, int delta) {
    lv_obj_t *b = lv_obj_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, SET_BTN_W, SET_BTN_H);
    lv_obj_align(b, LV_ALIGN_TOP_MID, x, SET_ROW_Y);
    lv_obj_set_style_bg_color(b, COL_BOX, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, UI_RADIUS, 0);
    lv_obj_set_style_border_color(b, COL_BORDER, 0);
    lv_obj_set_style_border_width(b, UI_BORDER, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, set_time_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)delta);

    lv_obj_t *l = ui_label(b, &oxanium_22, COL_GOLD_BRIGHT);
    lv_obj_center(l);
    lv_label_set_text(l, txt);
}

static void build_settings(lv_obj_t *scr) {
    set_root = lv_obj_create(scr);
    lv_obj_remove_style_all(set_root);
    lv_obj_set_size(set_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(set_root, COL_PAGE, 0);
    lv_obj_set_style_bg_opa(set_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(set_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(set_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(set_root, set_bg_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(set_root, LV_OBJ_FLAG_HIDDEN);

    ui_title(set_root, "EINSTELLUNGEN");

    /* --- Uhrzeit ---
     * Vier Knoepfe und die Zeit auf EINER Linie, sauber verteilt:
     *
     *     [-h] [-m]   18:42   [+m] [+h]
     *
     * Die Minuten-Knoepfe liegen innen (feiner), die Stunden aussen. */
    lv_obj_t *h1 = ui_label(set_root, &oxanium_16, COL_DIM);
    lv_obj_align(h1, LV_ALIGN_TOP_MID, 0, 76);
    lv_label_set_text(h1, "UHRZEIT");

    set_time = ui_label(set_root, &oxanium_22, COL_TEXT);
    set_show_time();

    set_step_btn(set_root, "-",  -142, -60);   /* Stunde zurueck */
    set_step_btn(set_root, "-",   -80,  -1);   /* Minute zurueck */
    set_step_btn(set_root, "+",    80,  +1);   /* Minute vor     */
    set_step_btn(set_root, "+",   142, +60);   /* Stunde vor     */

    /* Beschriftung: je ein Label MITTIG unter seinem Knopf, statt einer Zeile
     * mit Leerzeichen - die sass nie richtig. */
    static const int   HX[4] = { -142, -80, 80, 142 };
    static const char *HL[4] = { "Std.", "Min.", "Min.", "Std." };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *hl = ui_label(set_root, &oxanium_16, COL_DIM);
        lv_obj_align(hl, LV_ALIGN_TOP_MID, HX[i], SET_ROW_Y + SET_BTN_H + 6);
        lv_label_set_text(hl, HL[i]);
    }

    /* --- Helligkeit --- */
    lv_obj_t *h2 = ui_label(set_root, &oxanium_16, COL_DIM);
    lv_obj_align(h2, LV_ALIGN_TOP_MID, 0, 222);
    lv_label_set_text(h2, "HELLIGKEIT");


    set_slider = lv_slider_create(set_root);
    lv_obj_set_size(set_slider, UI_CARD_W - 20, 28);   /* hoeher = leichter zu treffen */
    lv_obj_align(set_slider, LV_ALIGN_TOP_MID, 0, 296);
    lv_slider_set_range(set_slider, 60, 255);   /* nie ganz dunkel */
    lv_obj_set_style_bg_color(set_slider, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(set_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(set_slider, 14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(set_slider, COL_GOLD, LV_PART_INDICATOR);
    lv_obj_set_style_radius(set_slider, 14, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(set_slider, COL_GOLD_BRIGHT, LV_PART_KNOB);

    /* Der KNOB bekommt viel Padding - das vergroessert die Anfasszone weit ueber
     * die sichtbare Leiste hinaus. Genau daran scheiterte das Greifen: der
     * CST816 ist ungenau, und 18 px hohe Slider trifft man kaum. Mit dem Padding
     * ist die fuehlbare Flaeche rund 56 px hoch, auch wenn die Leiste schmal
     * aussieht. */
    lv_obj_set_style_pad_all(set_slider, 16, LV_PART_KNOB);

    /* Das ganze Objekt zusaetzlich mit erweiterter Klickflaeche versehen, damit
     * auch ein Griff knapp ober- oder unterhalb noch zaehlt. */
    lv_obj_set_ext_click_area(set_slider, 20);

    lv_obj_add_event_cb(set_slider, set_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(set_slider, set_slider_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(set_slider, set_slider_cb, LV_EVENT_PRESS_LOST, NULL);

    /* Schlafbilanz: was der letzte Deep Sleep wirklich gekostet hat. Bewusst
     * hier auf dem Display, nicht nur im Log - am USB-Kabel laedt der Akku und
     * jede Messung waere verfaelscht. */
    set_batt = ui_label(set_root, &oxanium_16, COL_DIM);
    lv_obj_set_width(set_batt, 320);
    lv_label_set_long_mode(set_batt, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(set_batt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(set_batt, LV_ALIGN_TOP_MID, 0, 344);

    lv_obj_t *cl = ui_label(set_root, &oxanium_16, COL_DIM);
    lv_obj_set_style_text_align(cl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(cl, LV_ALIGN_TOP_MID, 0, 386);
    lv_label_set_text(cl, "Nach oben wischen zum Schließen");
}

/* Bilanz aktualisieren - wird beim Oeffnen der Einstellungen aufgerufen. */
static void set_show_batt(void) {
    if (!set_batt) return;
    int mins = 0, drop = 0, cyc = 0; bool chg = false;
    if (!shp_sleep_report(&mins, &drop, &chg, &cyc)) {
        lv_label_set_text_fmt(set_batt, "Akku %d mV", shp_battery_mv());
        return;
    }
    if (chg) {
        lv_label_set_text(set_batt, "Am Kabel · Schlafmessung erst\nohne USB aussagekräftig");
        return;
    }
    if (mins < 30) {
        lv_label_set_text_fmt(set_batt, "Schlaf %d min · %d mV · %d Zyklen\nzu kurz für eine Aussage",
                              mins, drop, cyc);
        return;
    }
    /* mV pro Stunde, eine Nachkommastelle ohne float-Formatierung.
     * Die Zyklen zeigen, wie oft das Geraet zwischendurch aufgewacht ist. */
    int perH10 = (drop * 600) / mins;      /* = mV/h * 10 */
    lv_label_set_text_fmt(set_batt, "Schlaf %d:%02d h · %d mV · %d Zyklen\n= %d,%d mV pro Stunde",
                          mins / 60, mins % 60, drop, cyc, perH10 / 10, perH10 % 10);
}

/* ============================================================
 *  Aufbau
 * ============================================================ */
static void lu_build(lv_obj_t *scr);   /* Level-Up-Popup, definiert weiter unten */

void ui_init(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_PAGE, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < PG_COUNT; i++) {
        pages[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(pages[i]);
        lv_obj_set_size(pages[i], LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(pages[i], COL_PAGE, 0);
        lv_obj_set_style_bg_opa(pages[i], LV_OPA_COVER, 0);
        lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    build_profil(pages[PG_PROFIL]);
    build_home(pages[PG_HOME]);
    build_quest(pages[PG_QUEST]);
    build_items(pages[PG_ITEMS]);
    build_adv(pages[PG_ADV]);
    build_arena(pages[PG_ARENA]);
    build_loadout(pages[PG_ARENA]);

    for (int i = 0; i < PG_COUNT; i++) {
        dots[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(dots[i]);
        lv_obj_set_size(dots[i], 6, 6);
        lv_obj_set_style_radius(dots[i], 3, 0);
        lv_obj_set_style_bg_opa(dots[i], LV_OPA_40, 0);
        int gap = 14, total = (PG_COUNT - 1) * gap;
        lv_obj_align(dots[i], LV_ALIGN_BOTTOM_MID, -(total / 2) + i * gap, DOT_Y);
    }

    build_card(scr);
    build_popup(scr);
    build_confirm(scr);
    build_settings(scr);
    lu_build(scr);            /* uebergreifendes Level-Up/Evolutions-Popup */
    show_page(PG_HOME);
}

/* ============================================================
 *  Uebergreifendes Level-Up / Evolutions-Popup
 *  Wird von ui_tick gepollt und erscheint ueber ALLEM, egal auf
 *  welcher Seite man ist. Erst Level-Up, bei Evolution danach die
 *  Verwandlung.
 * ============================================================ */
static lv_obj_t *lu_root, *lu_box, *lu_spr, *lu_title, *lu_sub;
static int  lu_pendingEvo   = 0;   /* nach dem Level-Up-Popup noch Evo zeigen? */
static int  lu_pendingStage = 1;   /* Zielstufe der Evolution                  */
static int  lu_showingEvo   = 0;   /* zeigt das Popup gerade die Evolution?    */

static void lu_recolor_exec2(void *o, int32_t v) {
    lv_obj_set_style_image_recolor_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
}

/* Kurzes goldenes Aufleuchten + sanftes Anheben des Sprites. */
static void lu_sprite_flourish(void) {
    lv_obj_set_style_image_recolor(lu_spr, COL_GOLD_BRIGHT, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lu_spr);
    lv_anim_set_exec_cb(&a, lu_recolor_exec2);
    lv_anim_set_values(&a, 180, 0);
    lv_anim_set_duration(&a, 700);
    lv_anim_start(&a);
}

static void lu_show_levelup(int level);   /* fwd */
static void lu_show_evolution(int stage); /* fwd */

/* Klick aufs Popup: schliessen - und falls noch eine Evolution aussteht,
 * direkt das Evolutions-Popup nachschieben. */
static void lu_clicked(lv_event_t *e) {
    LV_UNUSED(e);
    if (!is_tap()) return;
    lv_obj_add_flag(lu_root, LV_OBJ_FLAG_HIDDEN);
    if (!lu_showingEvo && lu_pendingEvo) {
        lu_pendingEvo = 0;
        lu_show_evolution(lu_pendingStage);
    }
}

static void lu_build(lv_obj_t *scr) {
    lu_box = modal_box(scr, &lu_root, 330, lu_clicked);

    lu_title = ui_label(lu_box, &oxanium_22, COL_GOLD_BRIGHT);
    lv_obj_align(lu_title, LV_ALIGN_TOP_MID, 0, 26);

    lu_spr = lv_image_create(lu_box);
    lv_obj_align(lu_spr, LV_ALIGN_CENTER, 0, 6);

    lu_sub = ui_label(lu_box, &oxanium_16, COL_TEXT);
    lv_obj_set_width(lu_sub, POP_W - 2 * 24);
    lv_label_set_long_mode(lu_sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(lu_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lu_sub, LV_ALIGN_BOTTOM_MID, 0, -24);
}

static void lu_show_levelup(int level) {
    lu_showingEvo = 0;
    lv_label_set_text(lu_title, "LEVEL UP!");
    scale_to_height(lu_spr, asset_beast(g_state.beastId, game_player_stage()), 150);
    lv_obj_set_style_image_recolor_opa(lu_spr, LV_OPA_0, 0);
    lv_label_set_text_fmt(lu_sub, "%s erreicht Level %d", game_player_name(), level);
    lv_obj_move_foreground(lu_root);
    lv_obj_remove_flag(lu_root, LV_OBJ_FLAG_HIDDEN);
    lu_sprite_flourish();
    shp_vibrate(30);
}

static void lu_show_evolution(int stage) {
    lu_showingEvo = 1;
    lv_label_set_text(lu_title, "ENTWICKLUNG!");
    /* Das Sprite in der NEUEN Stufe zeigen. */
    scale_to_height(lu_spr, asset_beast(g_state.beastId, stage), 150);
    lv_obj_set_style_image_recolor_opa(lu_spr, LV_OPA_0, 0);
    lv_label_set_text_fmt(lu_sub, "%s entwickelt sich weiter!", game_player_name());
    lv_obj_move_foreground(lu_root);
    lv_obj_remove_flag(lu_root, LV_OBJ_FLAG_HIDDEN);
    lu_sprite_flourish();
    shp_vibrate(60);
}

/* Von ui_tick gepollt: steht ein Level-Up an und ist gerade kein Popup offen? */
static void lu_poll(void) {
    /* Nicht mitten in einem Kampf stoeren und nicht ueber ein offenes Popup. */
    if (lu_root && !lv_obj_has_flag(lu_root, LV_OBJ_FLAG_HIDDEN)) return;

    int level = 0, evolved = 0, stage = 1;
    if (game_take_levelup(&level, &evolved, &stage)) {
        lu_pendingEvo   = evolved;
        lu_pendingStage = stage;
        lu_show_levelup(level);
    }
}

void ui_tick(void) {
    lu_poll();   /* uebergreifendes Level-Up/Evolutions-Popup zuerst */
    if (cur_page == PG_HOME)   refresh_home();
    if (cur_page == PG_PROFIL) refresh_profil();
    if (cur_page == PG_QUEST)  refresh_quest();
    if (cur_page == PG_ITEMS)  refresh_items();
    if (cur_page == PG_ADV)    refresh_adv();
    if (cur_page == PG_ARENA)  refresh_arena();
}

void ui_card_changed(void) {
    show_page(cur_page);   /* frischt die aktuelle Seite auf */
}
