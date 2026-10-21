/* ============================================================
 *  adventure — Streifzug und Expedition
 *  ------------------------------------------------------------
 *  Das Beast geht in ECHTZEIT los und kommt nach 2 bzw. 8 Stunden zurueck.
 *  Die Expedition ist auf einen Arbeitstag gemuenzt: morgens losschicken,
 *  abends nachsehen, was es mitgebracht hat.
 *
 *  Waehrend es unterwegs ist:
 *    - es ist NICHT auf dem Podest (Home zeigt eine Wartemeldung)
 *    - die Saettigung sinkt doppelt so schnell (VIT_SAT_PER_H_ADV)
 *    - es kann nicht in die Arena
 *
 *  Vier Ausgaenge. Die Wahrscheinlichkeiten haengen an der Dauer:
 *  eine Expedition lohnt sich, aber sie kostet auch mehr Energie.
 *
 *  Speicherort: NVS-Blob "adv". Laeuft ueber shp_now_minutes(), also auch
 *  im Standby weiter - genau wie die Vitalwerte.
 * ============================================================ */
#ifndef ADVENTURE_H
#define ADVENTURE_H

#include <stdint.h>
#include <stdbool.h>

enum { ADV_ROAM = 0, ADV_EXPED };          /* Streifzug 2 h / Expedition 8 h */

/* Ausgaenge */
enum {
    ADV_NONE = 0,
    ADV_FIND,        /* Futter gefunden          */
    ADV_DISCOVER,    /* Entdeckung -> Bonus-XP   */
    ADV_TREASURE,    /* Schatz -> Ausruestung    */
    ADV_MISHAP       /* Pech -> nichts, muede    */
};

#define ADV_ROAM_H       2
#define ADV_EXPED_H      8
#define ADV_DISCOVER_XP      15   /* Entdeckung beim Streifzug (2 h)  */
#define ADV_DISCOVER_XP_EXP  40   /* Entdeckung bei der Expedition (8 h) */

typedef struct {
    uint8_t  active;      /* laeuft gerade ein Abenteuer?          */
    uint8_t  kind;        /* ADV_ROAM / ADV_EXPED                  */
    uint32_t startMin;    /* Minuten seit 1970                     */
    uint32_t endMin;      /* wann es zurueck ist                   */
    uint8_t  pending;     /* Ergebnis liegt zum Abholen bereit     */
    uint8_t  outcome;     /* ADV_*                                 */
    uint8_t  item;        /* Beute (Item-ID)                       */
    uint8_t  amount;      /* wie viel davon                        */
} Adventure;

extern Adventure g_adv;

void adv_begin(void);          /* in setup() nach vit_begin() und inv_begin() */
void adv_tick(void);           /* regelmaessig: prueft die Rueckkehr          */

bool adv_is_away(void);        /* Beast gerade unterwegs?                     */
bool adv_start(int kind);      /* losschicken; false = zu wenig Energie o.ae. */
int  adv_minutes_left(void);   /* Restzeit in Minuten                         */
int  adv_energy_cost(int kind);

bool adv_has_result(void);     /* Ergebnis wartet auf Abholung                */
void adv_collect(void);        /* Beute verbuchen und Ergebnis quittieren     */

int         adv_outcome_kind(void);
int         adv_outcome_item(void);
int         adv_outcome_amount(void);
bool        adv_outcome_has_item(void);
uint8_t     adv_boss_pending(void);   /* nach dem Abholen: SH_BOSS_* oder 0 */
int         adv_progress_pct(void);   /* Fortschritt der Reise 0..100        */
const char* adv_flavor(void);         /* Bemerkung von unterwegs             */
const char* adv_outcome_title(void);
const char* adv_outcome_text(void);


/* Pech kostet Energie, nicht Bindung. Fuer den Ausgang kann man nichts. */
#define ADV_MISHAP_ENERGY  10

/* Wie oft ist das Beast erfolgreich zurueckgekommen? [0]=Streifzug [1]=Expedition */
int adv_done_count(int kind);

/* ============================================================
 *  Beast-Wechsel — das Abenteuer friert MIT ein
 *  ------------------------------------------------------------
 *  Wird die Karte gezogen, steht auch die Abenteuer-Uhr still. Beim
 *  Wiedereinlegen laeuft sie genau dort weiter, wo sie aufgehoert hat.
 *
 *  Das ist die konsequente Fortsetzung des Vitalwert-Freeze: Waere das
 *  Abenteuer weitergelaufen, waehrend der Hunger eingefroren ist, koennte man
 *  den doppelten Hunger einer Expedition umgehen, indem man einfach die Karte
 *  zieht. So gibt es diese Hintertuer nicht.
 *
 *  Technisch: Beim Einfrieren wird die RESTZEIT gespeichert, nicht die absolute
 *  Endzeit. Beim Auftauen wird daraus wieder eine Endzeit.
 * ============================================================ */
void adv_bind(void);     /* Karte eingelegt: altes sichern, neues auftauen */
void adv_freeze(void);   /* Karte gezogen: Uhr anhalten                    */

#endif /* ADVENTURE_H */
