/* ============================================================
 *  adventure.cpp
 * ============================================================ */
#include "adventure.h"
#include <Arduino.h>
#include <string.h>
#include "sh_platform.h"
#include "vitals.h"
#include "items.h"
#include "game.h"

#include "HWCDC.h"
extern HWCDC USBSerial;

Adventure g_adv;

#define ADV_MAGIC 0x41445631u   /* 'ADV1' */

typedef struct __attribute__((packed)) {
    uint32_t  magic;
    Adventure a;
    uint8_t   frozen;      /* 1 = eingefroren, remainMin gilt statt endMin */
    uint32_t  remainMin;   /* Restlaufzeit beim Einfrieren                 */
} AdvBlob;

/* Ein Schluessel pro Beast - wie bei den Vitalwerten. Ohne Karte "adv". */
static char s_key[16] = "adv";

static void make_key(void) {
    uint16_t sp; uint32_t bd;
    if (game_beast_uid(&sp, &bd)) snprintf(s_key, sizeof(s_key), "a%u_%lu",
                                           (unsigned)sp, (unsigned long)bd);
    else                          strcpy(s_key, "adv");
}

static char s_text[96];

/* Erfolgszaehler. Eigener Blob, damit ein Formatwechsel kein laufendes
 * Abenteuer zerstoert. */
static uint16_t s_done[2] = { 0, 0 };

static void stats_save(void) { shp_save_blob("advstat", s_done, sizeof(s_done)); }
static void stats_load(void) { if (!shp_load_blob("advstat", s_done, sizeof(s_done))) { s_done[0] = s_done[1] = 0; } }

int adv_done_count(int kind) { return (kind >= 0 && kind < 2) ? s_done[kind] : 0; }

static void adv_save(void) {
    AdvBlob b;
    b.magic     = ADV_MAGIC;
    b.a         = g_adv;
    b.frozen    = 0;
    b.remainMin = 0;
    shp_save_blob(s_key, &b, sizeof(b));
}

/* Einfrieren: Restzeit sichern statt der Endzeit. */
static void adv_save_frozen(void) {
    AdvBlob b;
    b.magic  = ADV_MAGIC;
    b.a      = g_adv;
    b.frozen = 1;

    uint32_t now = shp_now_minutes();
    b.remainMin  = (g_adv.active && g_adv.endMin > now) ? (g_adv.endMin - now) : 0;

    shp_save_blob(s_key, &b, sizeof(b));
    if (g_adv.active)
        USBSerial.printf("[ADV] %s eingefroren: noch %lu Min.\n",
                         s_key, (unsigned long)b.remainMin);
}

/* Laden. thaw = die Karte wurde gerade eingelegt: aus der Restzeit wird wieder
 * eine Endzeit, gerechnet ab JETZT. */
static void adv_load(bool thaw) {
    AdvBlob b;
    uint32_t now = shp_now_minutes();

    if (shp_load_blob(s_key, &b, sizeof(b)) && b.magic == ADV_MAGIC) {
        g_adv = b.a;
        if (b.frozen && g_adv.active) {
            g_adv.endMin   = now + b.remainMin;      /* Uhr laeuft wieder */
            g_adv.startMin = now;
            if (thaw)
                USBSerial.printf("[ADV] %s aufgetaut: noch %lu Min.\n",
                                 s_key, (unsigned long)b.remainMin);
            adv_save();
        }
    } else {
        memset(&g_adv, 0, sizeof(g_adv));
        adv_save();
    }
}

void adv_freeze(void) {
    adv_save_frozen();

    /* WICHTIG: den Arbeitszustand danach LEEREN. Ohne das bleibt g_adv.active
     * stehen, und die Startseite zeigt weiter "unterwegs, noch 6 Std." - obwohl
     * gar keine Karte mehr steckt. Genau dieser Fehler liess das Geraet so
     * wirken, als sei das alte Beast noch da. */
    memset(&g_adv, 0, sizeof(g_adv));
}

void adv_bind(void) {
    /* NUR einfrieren, wenn ueberhaupt noch etwas laeuft.
     *
     * Der Fall, der hier schiefging: Beim Ziehen der Karte hat adv_freeze()
     * bereits gespeichert UND g_adv geleert. Ein zweites adv_save_frozen() an
     * dieser Stelle haette den leeren Zustand ueber das eben eingefrorene
     * Abenteuer geschrieben - es waere spurlos verschwunden, die eingesetzte
     * Energie mit ihm.
     *
     * Die Abfrage deckt trotzdem den seltenen Fall ab, dass zwei Karten so
     * schnell getauscht werden, dass der 2-Sekunden-Poll das Ziehen verpasst
     * hat und gar kein Freeze lief. */
    if (g_adv.active) adv_save_frozen();

    make_key();          /* auf das neue Beast umstellen */
    adv_load(true);
}

void adv_begin(void) {
    stats_load();
    make_key();

    if (!game_has_beast()) {          /* ohne Karte: nichts laufen lassen */
        memset(&g_adv, 0, sizeof(g_adv));
        USBSerial.println("[ADV] keine Karte - kein Abenteuer.");
        return;
    }

    /* Dieselbe Ladefunktion wie beim Kartenwechsel - sie kennt das Freeze-Flag.
     * Ein Beast, das eingefroren im Regal lag, wuerde sonst mit einer laengst
     * abgelaufenen Endzeit aufwachen und waere sofort "zurueck". */
    adv_load(true);

    if (g_adv.active)
        USBSerial.printf("[ADV] laeuft noch, %d Minuten\n", adv_minutes_left());
}

int adv_energy_cost(int kind) {
    return (kind == ADV_EXPED) ? VIT_COST_EXPED : VIT_COST_ROAM;
}

/* Ohne Karte gibt es kein Abenteuer - egal, was im Speicher steht. Das ist das
 * Sicherheitsnetz unter dem Freeze: selbst wenn irgendwo ein Zustand haengen
 * bleibt, kann die Oberflaeche nicht behaupten, ein Beast sei unterwegs. */
bool adv_is_away(void)    { return game_has_beast() && g_adv.active  != 0; }
bool adv_has_result(void) { return game_has_beast() && g_adv.pending != 0; }

int adv_minutes_left(void) {
    if (!g_adv.active) return 0;
    uint32_t now = shp_now_minutes();
    if (now >= g_adv.endMin) return 0;
    return (int)(g_adv.endMin - now);
}

bool adv_start(int kind) {
    if (g_adv.active || g_adv.pending) return false;
    if (!game_has_beast())             return false;
    if (vit_is_collapsed())            return false;

    int cost = adv_energy_cost(kind);
    if (!vit_spend_energy(cost)) return false;   /* zu muede */

    uint32_t now = shp_now_minutes();
    g_adv.active   = 1;
    g_adv.kind     = (uint8_t)kind;
    g_adv.startMin = now;
    g_adv.endMin   = now + (uint32_t)((kind == ADV_EXPED ? ADV_EXPED_H : ADV_ROAM_H) * 60);
    g_adv.pending  = 0;
    g_adv.outcome  = ADV_NONE;
    adv_save();

    USBSerial.printf("[ADV] %s gestartet, zurueck in %d h\n",
                     kind == ADV_EXPED ? "Expedition" : "Streifzug",
                     kind == ADV_EXPED ? ADV_EXPED_H : ADV_ROAM_H);
    return true;
}

/* Ausgang wuerfeln. Die Expedition dauert viermal so lang - sie muss sich
 * auch spuerbar mehr lohnen, sonst nimmt sie niemand. */
static void roll_outcome(void) {
    bool exped = (g_adv.kind == ADV_EXPED);
    int r = (int)(esp_random() % 100);

    if (exped) {
        if      (r < 40) g_adv.outcome = ADV_FIND;       /* 40 % */
        else if (r < 60) g_adv.outcome = ADV_DISCOVER;   /* 20 % */
        else if (r < 85) g_adv.outcome = ADV_TREASURE;   /* 25 % */
        else             g_adv.outcome = ADV_MISHAP;     /* 15 % */
    } else {
        if      (r < 55) g_adv.outcome = ADV_FIND;       /* 55 % */
        else if (r < 68) g_adv.outcome = ADV_DISCOVER;   /* 13 % */
        else if (r < 78) g_adv.outcome = ADV_TREASURE;   /* 10 % */
        else             g_adv.outcome = ADV_MISHAP;     /* 22 % */
    }

    g_adv.item   = 0;
    g_adv.amount = 0;

    if (g_adv.outcome == ADV_FIND) {
        /* Beim Streifzug eher Beeren, bei der Expedition auch Fleisch
         * und gelegentlich ein Festmahl. */
        int f = (int)(esp_random() % 100);
        if (exped) {
            g_adv.item   = (f < 35) ? IT_BEERE : (f < 75) ? IT_FLEISCH
                         : (f < 92) ? IT_KRAFTNUSS : IT_FESTMAHL;
            g_adv.amount = 1 + (uint8_t)(esp_random() % 2);   /* 1..2 */
        } else {
            g_adv.item   = (f < 65) ? IT_BEERE : (f < 92) ? IT_FLEISCH : IT_KRAFTNUSS;
            g_adv.amount = 1;
        }
    } else if (g_adv.outcome == ADV_TREASURE) {
        g_adv.item   = (uint8_t)(IT_AMULETT + (esp_random() % 4));
        /* Nach 8 h darf eine Truhe auch mal doppelt ausfallen. */
        g_adv.amount = exped ? ((esp_random() % 100 < 35) ? 2 : 1) : 1;
    } else if (g_adv.outcome == ADV_DISCOVER) {
        /* Entdeckung: XP-Bonus (in adv_collect) PLUS ein kleines Futter obendrauf,
         * damit sich das Ergebnis nach etwas Handfestem anfuehlt. */
        g_adv.item   = (esp_random() % 2) ? IT_BEERE : IT_FLEISCH;
        g_adv.amount = exped ? (1 + (uint8_t)(esp_random() % 2)) : 1;
    }

    /* Eine Expedition (8 h) darf nie voellig leer ausgehen - auch bei Pech gibt
     * es ein kleines Trostfutter. Der Streifzug (2 h) bleibt haerter. */
    if (exped && g_adv.amount == 0) {
        g_adv.item   = (esp_random() % 2) ? IT_BEERE : IT_FLEISCH;
        g_adv.amount = exped ? (1 + (uint8_t)(esp_random() % 2)) : 1;
    }
}

void adv_tick(void) {
    if (!g_adv.active) return;
    if (shp_now_minutes() < g_adv.endMin) return;

    /* Zurueck. Ergebnis wird erst gewuerfelt, wenn es ankommt - so kann man
     * nicht per Uhr-Zurueckstellen nachwuerfeln. */
    g_adv.active  = 0;
    g_adv.pending = 1;
    roll_outcome();
    adv_save();

    USBSerial.printf("[ADV] zurueck! Ausgang %d\n", (int)g_adv.outcome);
}

/* --- Reise-Bemerkungen ---------------------------------------------------
 * Vier Phasen der Reise, je 15 kurze Zeilen. Welche gezeigt wird, haengt am
 * Fortschritt (Phase) und an einem Zeitfenster: beim Streifzug wechselt sie
 * alle 30 Minuten, bei der Expedition alle 60. Die Wahl haengt an startMin und
 * am Fenster, ist also stabil - sie flackert nicht bei jedem Bildaufbau und
 * wiederholt sich trotzdem nicht staendig. */
#define ADV_FLAVOR_N 15

static const char *FL_START[ADV_FLAVOR_N] = {
    "Der Weg ist frei, los geht es!",
    "Frische Luft! Endlich raus.",
    "Hinter dem Hügel geht es weiter.",
    "Die Spur sieht vielversprechend aus.",
    "Ein Vogel zeigt mir die Richtung.",
    "Noch ist hier alles vertraut.",
    "Der Boden ist weich vom Regen.",
    "Ich habe die Witterung aufgenommen.",
    "Ein guter Tag zum Aufbrechen.",
    "Das Tor liegt schon hinter mir.",
    "Meine Pfoten kribbeln vor Vorfreude.",
    "Der Pfad windet sich nach Norden.",
    "Da vorn liegt bestimmt was Gutes.",
    "Ich halte die Augen offen.",
    "Erst mal warmlaufen."
};

static const char *FL_WEG[ADV_FLAVOR_N] = {
    "Oh, der Berg ist aber hoch...",
    "Die Beere sieht gut aus, wird eingesteckt.",
    "Hier war lange niemand mehr.",
    "Ein Bach! Kurze Trinkpause.",
    "Zwischen den Felsen pfeift der Wind.",
    "Da hinten hat sich was bewegt.",
    "Der Wald wird dichter.",
    "Ich folge einer alten Spur.",
    "Riecht nach Moos und Regen.",
    "Ein Umweg, aber der lohnt sich.",
    "Diese Steine glitzern seltsam.",
    "Der Himmel zieht sich zu.",
    "Kurz verschnaufen, dann weiter.",
    "Fußabdrücke. Nicht meine.",
    "Von hier oben sieht man weit."
};

static const char *FL_TIEF[ADV_FLAVOR_N] = {
    "Hier kenne ich mich nicht mehr aus.",
    "Es wird kühler, ich bin weit weg.",
    "Etwas Altes liegt unter dem Laub.",
    "Die Schatten werden länger.",
    "Ich höre Schritte hinter mir.",
    "Ein verlassener Unterschlupf.",
    "Vorsicht, hier geht es steil runter.",
    "Diese Stille gefällt mir nicht.",
    "Da glimmt was zwischen den Wurzeln.",
    "Der Nebel schluckt jeden Laut.",
    "Den Rückweg sollte ich mir merken.",
    "Ein Knacken. Nur ein Ast, hoffe ich.",
    "Meine Tasche wird schwerer.",
    "So tief war ich noch nie drin.",
    "Hier lohnt genaues Hinsehen."
};

static const char *FL_HEIM[ADV_FLAVOR_N] = {
    "Ich mache mich auf den Rückweg.",
    "Der Heimweg fühlt sich kürzer an.",
    "Bald bin ich zurück!",
    "Ich habe was mitgebracht.",
    "Die Beine werden schwer.",
    "Da vorn wird es wieder vertraut.",
    "Ich freue mich auf was zu fressen.",
    "Der Rucksack drückt, aber es lohnt sich.",
    "Gleich ist das Tor in Sicht.",
    "Was für ein Tag.",
    "Genug gesehen für heute.",
    "Nur noch das letzte Stück.",
    "Zu Hause wartet ein warmer Platz.",
    "Ich rieche schon das Podest.",
    "Fast geschafft!"
};

static const char *const *FLAVOR_STAGE[4] = { FL_START, FL_WEG, FL_TIEF, FL_HEIM };

/* Fortschritt der laufenden Reise in Prozent (0..100). */
int adv_progress_pct(void) {
    if (!g_adv.active) return 0;
    uint32_t total = (g_adv.endMin > g_adv.startMin)
                   ? (g_adv.endMin - g_adv.startMin) : 1;
    uint32_t now = shp_now_minutes();
    if (now <= g_adv.startMin) return 0;
    uint32_t done = now - g_adv.startMin;
    if (done >= total) return 100;
    return (int)((done * 100) / total);
}

/* Die aktuelle Bemerkung von unterwegs. Leerstring, wenn nichts laeuft. */
const char* adv_flavor(void) {
    if (!g_adv.active) return "";
    int pct   = adv_progress_pct();
    int stage = (pct < 25) ? 0 : (pct < 50) ? 1 : (pct < 80) ? 2 : 3;

    int windowMin = (g_adv.kind == ADV_EXPED) ? 60 : 30;
    uint32_t now     = shp_now_minutes();
    uint32_t elapsed = (now > g_adv.startMin) ? (now - g_adv.startMin) : 0;
    uint32_t slot    = elapsed / (uint32_t)windowMin;

    uint32_t h = g_adv.startMin * 2654435761u + slot * 40503u + (uint32_t)stage * 97u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return FLAVOR_STAGE[stage][h % ADV_FLAVOR_N];
}

/* --- Boss nach der Expedition ---
 * Ab Level 20 kann eine Expedition in einer Boss-Begegnung muenden. Das gibt
 * der langen Wartezeit einen Hoehepunkt: nicht nur abholen, sondern ab und zu
 * noch ein echter Kampf mit garantierter Belohnung. */
#define ADV_BOSS_CHANCE_PCT 30
static uint8_t s_bossAfter = 0;

/* Steht nach dem Abholen ein Boss-Kampf an? Liefert SH_BOSS_LUNRAK/SOLVAR
 * oder 0 - und setzt sich beim Abholen zurueck (nur einmal abrufbar). */
uint8_t adv_boss_pending(void) { uint8_t b = s_bossAfter; s_bossAfter = 0; return b; }

/* Beute verbuchen. Erst hier landet etwas im Rucksack. */
void adv_collect(void) {
    if (!g_adv.pending) return;

    /* Nur die Expedition (8 h) kann einen Boss nach sich ziehen, und erst ab
     * einem voll entwickelten Beast. */
    s_bossAfter = 0;
    if (g_adv.kind == ADV_EXPED && g_state.level >= SH_BOSS_MIN_LEVEL
        && (esp_random() % 100) < ADV_BOSS_CHANCE_PCT) {
        s_bossAfter = (esp_random() % 2) ? SH_BOSS_LUNRAK : SH_BOSS_SOLVAR;
    }

    switch (g_adv.outcome) {
        case ADV_FIND:
        case ADV_TREASURE:
            if (g_adv.amount > 0) inv_add(g_adv.item, g_adv.amount);
            break;

        case ADV_DISCOVER:
            /* Bonus-XP PLUS das kleine Futter, das roll_outcome hinterlegt hat. */
            game_add_xp((g_adv.kind == ADV_EXPED) ? ADV_DISCOVER_XP_EXP : ADV_DISCOVER_XP);
            if (g_adv.amount > 0) inv_add(g_adv.item, g_adv.amount);
            break;

        case ADV_MISHAP:
            /* Pech kostet ENERGIE, nicht Bindung. Fuer den Ausgang eines
             * Abenteuers kann man nichts - das Beast dafuer weniger lieb zu
             * haben waere eine Strafe ohne Ursache. Erschoepfung ist spuerbar,
             * aber fair: man muss laenger warten, verliert aber nichts.
             * Bei einer Expedition hat roll_outcome ein Trostfutter hinterlegt. */
            vit_spend_energy(ADV_MISHAP_ENERGY);
            if (g_adv.amount > 0) inv_add(g_adv.item, g_adv.amount);
            break;

        default: break;
    }

    /* Erfolgreich zurueck (kein Pech) -> die gemeinsame Zeit staerkt die
     * Bindung. Das ist eine der wenigen Quellen, die sie ueberhaupt hebt. */
    if (g_adv.outcome != ADV_MISHAP) vit_add_bond(BOND_ADV_GOOD);

    if (g_adv.kind < 2) { s_done[g_adv.kind]++; stats_save(); }

    g_adv.pending = 0;
    g_adv.outcome = ADV_NONE;
    adv_save();
}

/* Schlanke Getter fuer die UI - damit das Ergebnisfenster ein Item-Icon und
 * die Menge zeigen kann, ohne die interne Struktur zu kennen. */
int adv_outcome_kind(void)   { return (int)g_adv.outcome; }
int adv_outcome_item(void)   { return (int)g_adv.item; }
int adv_outcome_amount(void) { return (int)g_adv.amount; }
bool adv_outcome_has_item(void) {
    /* Zeigt das Item-Icon, sobald wirklich etwas im Rucksack gelandet ist -
     * jetzt auch bei ENTDECKUNG (XP + Futter) und bei einer Expedition mit
     * Pech (Trostfutter). */
    return g_adv.amount > 0;
}

const char* adv_outcome_title(void) {
    switch (g_adv.outcome) {
        case ADV_FIND:      return "FUND";
        case ADV_DISCOVER:  return "ENTDECKUNG";
        case ADV_TREASURE:  return "SCHATZ";
        case ADV_MISHAP:    return "PECH";
        default:            return "ZURÜCK";
    }
}

const char* adv_outcome_text(void) {
    switch (g_adv.outcome) {
        case ADV_FIND:
            snprintf(s_text, sizeof(s_text), "Dein Beast bringt\n%dx %s mit.",
                     (int)g_adv.amount, ITEMS[g_adv.item].name);
            break;
        case ADV_DISCOVER:
            snprintf(s_text, sizeof(s_text),
                     "Es hat etwas entdeckt.\n+%d XP  ·  %s",
                     (g_adv.kind == ADV_EXPED) ? ADV_DISCOVER_XP_EXP : ADV_DISCOVER_XP,
                     ITEMS[g_adv.item].name);
            break;
        case ADV_TREASURE:
            snprintf(s_text, sizeof(s_text), "Eine Truhe!\n%s",
                     ITEMS[g_adv.item].name);
            break;
        case ADV_MISHAP:
            if (g_adv.amount > 0)
                snprintf(s_text, sizeof(s_text),
                         "Erschöpft zurück.\nImmerhin: %s", ITEMS[g_adv.item].name);
            else
                snprintf(s_text, sizeof(s_text),
                         "Nichts gefunden.\nEs ist erschöpft zurück.");
            break;
        default:
            snprintf(s_text, sizeof(s_text), "Dein Beast ist zurück.");
            break;
    }
    return s_text;
}
