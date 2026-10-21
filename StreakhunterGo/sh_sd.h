/* ============================================================
 *  sh_sd.h  -  SD-Karte als "Dim-Card" fuer ein einzelnes Beast.
 *  Etappe 1: SDMMC mounten, Beast-Datei lesen/schreiben (mit CRC),
 *  Selbsttest. Beruehrt den bestehenden Spielstand NICHT.
 * ============================================================ */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Eine Karte = ein Beast. Datei liegt unter /beast.shb auf der SD-Karte. */
#define SHB_MAGIC    0x53484231u   /* 'S''H''B''1' */
#define SHB_VERSION  1
#define SHB_NAME_LEN 16

/* Fester, gepackter Aufbau -> spaeter auch am PC erzeugbar (40 Bytes). */
typedef struct __attribute__((packed)) {
    uint32_t magic;                 /* SHB_MAGIC                          */
    uint16_t version;               /* SHB_VERSION                        */
    uint16_t species;               /* Beast-ID (Index in die Beast-Tab.) */
    uint16_t stage;                 /* Evolutionsstufe                    */
    uint16_t level;                 /* Level                              */
    uint32_t xp;                    /* Erfahrungspunkte                   */
    uint32_t birthDay;              /* Tagesindex der "Geburt"            */
    char     name[SHB_NAME_LEN];    /* Anzeigename, 0-terminiert          */
    uint32_t crc;                   /* CRC32 ueber alle Felder davor      */
} ShBeastCard;

bool shsd_begin(void);          /* SDMMC mounten (idempotent)             */
void shsd_end(void);            /* wieder aushaengen                      */
bool shsd_card_present(void);   /* Karte gemountet & vorhanden?           */

bool shsd_read_beast(ShBeastCard *out);     /* /beast.shb lesen + pruefen */
bool shsd_write_beast(const ShBeastCard *in);/* /beast.shb schreiben      */

/* Hot-Swap-Probe: 0 = keine Karte, 1 = Karte mit Beast (in *out), 2 = Karte
 * ohne gueltiges Beast. Mountet kurz und gibt die Karte wieder frei. */
int shsd_probe(ShBeastCard *out);

/* Etappe-1-Selbsttest: Karte mounten, evtl. vorhandenes Beast zeigen,
 * Test-Beast schreiben + zuruecklesen. Ausgabe ueber USBSerial.
 * Liefert true bei Erfolg. */
bool shsd_selftest(void);

#ifdef __cplusplus
}
#endif
