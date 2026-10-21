/* ============================================================
 *  sh_sd.cpp  -  SD-Karte als "Dim-Card" (Etappe 1)
 * ============================================================ */
#include "sh_sd.h"
#include <Arduino.h>
#include "pin_config.h"     /* SDMMC_CLK / SDMMC_CMD / SDMMC_DATA des Boards */
#include "FS.h"
#include "SD_MMC.h"
#include "HWCDC.h"
#include <string.h>

extern HWCDC USBSerial;          /* Log-Ausgabe wie im restlichen Projekt */

/* --- SD-Pins (SDMMC, 1-Bit-Modus) ---------------------------------------
 * Werden direkt aus pin_config.h uebernommen (CLK=2, CMD=1, D0=3 bei diesem
 * Board). CS wird im SDMMC-Modus nicht gebraucht.                          */
#define SHSD_CLK SDMMC_CLK
#define SHSD_CMD SDMMC_CMD
#define SHSD_D0  SDMMC_DATA

#define SHB_PATH "/beast.shb"

static bool s_mounted = false;

/* CRC32 (Poly 0xEDB88320), klein und ohne Tabelle. */
static uint32_t sd_crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1)));
    }
    return c ^ 0xFFFFFFFFu;
}

bool shsd_begin(void) {
    if (s_mounted) return true;
    if (!SD_MMC.setPins(SHSD_CLK, SHSD_CMD, SHSD_D0)) return false;
    /* 1-Bit-Modus (true), bei Fehler NICHT formatieren (false). Still beim
     * Pollen -> keine Log-Ausgaben hier. */
    if (!SD_MMC.begin("/sdcard", true, false)) return false;
    if (SD_MMC.cardType() == CARD_NONE) { SD_MMC.end(); return false; }
    s_mounted = true;
    return true;
}

void shsd_end(void) {
    if (s_mounted) { SD_MMC.end(); s_mounted = false; }
}

bool shsd_card_present(void) {
    return s_mounted && SD_MMC.cardType() != CARD_NONE;
}

bool shsd_read_beast(ShBeastCard *out) {
    if (!out || !shsd_begin()) return false;
    File f = SD_MMC.open(SHB_PATH, FILE_READ);
    if (!f) return false;
    ShBeastCard c;
    size_t n = f.read((uint8_t *)&c, sizeof(c));
    f.close();
    if (n != sizeof(c))       return false;
    if (c.magic != SHB_MAGIC) return false;
    uint32_t want = c.crc; c.crc = 0;
    if (sd_crc32((uint8_t *)&c, sizeof(c)) != want) return false;
    c.crc = want;
    *out = c;
    return true;
}

bool shsd_write_beast(const ShBeastCard *in) {
    if (!in || !shsd_begin()) return false;
    ShBeastCard c = *in;
    c.magic   = SHB_MAGIC;
    c.version = SHB_VERSION;
    c.crc     = 0;
    c.crc     = sd_crc32((uint8_t *)&c, sizeof(c));
    /* Absturzsicher: erst in eine Temp-Datei schreiben, dann umbenennen. Faellt
       der Strom (leerer Akku) WAEHREND des Schreibens aus, bleibt die alte
       beast.shb unversehrt statt halb ueberschrieben zu werden. */
    const char *tmp = "/beast.tmp";
    SD_MMC.remove(tmp);
    File f = SD_MMC.open(tmp, FILE_WRITE);
    if (!f) { USBSerial.println("[SD] Schreiben nicht moeglich"); return false; }
    size_t n = f.write((uint8_t *)&c, sizeof(c));
    f.flush();
    f.close();
    if (n != sizeof(c)) { USBSerial.println("[SD] Schreiben unvollstaendig"); SD_MMC.remove(tmp); return false; }
    SD_MMC.remove(SHB_PATH);                       /* alte Datei entfernen ... */
    if (!SD_MMC.rename(tmp, SHB_PATH)) {           /* ... dann Temp final machen */
        USBSerial.println("[SD] Umbenennen fehlgeschlagen");
        return false;
    }
    return true;
}

bool shsd_selftest(void) {
    USBSerial.println("=== SD-Selbsttest (Etappe 1) ===");
    USBSerial.printf("[SD] Pins: CLK=%d CMD=%d D0=%d (1-Bit)\n", SHSD_CLK, SHSD_CMD, SHSD_D0);

    if (!shsd_begin()) {
        USBSerial.println("=== Selbsttest FEHLER (kein Mount) ===");
        return false;
    }

    uint64_t mb = SD_MMC.cardSize() / (1024ULL * 1024ULL);
    USBSerial.printf("[SD] Karte ok, Groesse: %llu MB\n", (unsigned long long)mb);

    /* Schon ein Beast drauf? */
    ShBeastCard rd;
    if (shsd_read_beast(&rd)) {
        USBSerial.printf("[SD] vorhandenes Beast: \"%s\"  Spezies=%u  Lv=%u  Stufe=%u  XP=%lu\n",
                         rd.name, rd.species, rd.level, rd.stage, (unsigned long)rd.xp);
    } else {
        USBSerial.println("[SD] noch kein gueltiges Beast auf der Karte");
    }

    /* Test-Beast GRELO schreiben ... */
    ShBeastCard t;
    memset(&t, 0, sizeof(t));
    t.species  = 0;          /* Platzhalter-Spezies fuer Etappe 1 */
    t.stage    = 1;
    t.level    = 1;
    t.xp       = 0;
    t.birthDay = 0;
    strncpy(t.name, "GRELO", SHB_NAME_LEN - 1);
    if (!shsd_write_beast(&t)) {
        USBSerial.println("=== Selbsttest FEHLER (Schreiben) ===");
        return false;
    }
    USBSerial.println("[SD] Test-Beast \"GRELO\" geschrieben");

    /* ... und zuruecklesen + vergleichen. */
    ShBeastCard back;
    if (!shsd_read_beast(&back)) {
        USBSerial.println("=== Selbsttest FEHLER (Zuruecklesen) ===");
        return false;
    }
    bool ok = (strcmp(back.name, "GRELO") == 0) && (back.level == 1) && (back.stage == 1);
    USBSerial.printf("[SD] zurueckgelesen: \"%s\"  Lv=%u  Stufe=%u  ->  %s\n",
                     back.name, back.level, back.stage, ok ? "OK" : "MISMATCH");
    USBSerial.println(ok ? "=== Selbsttest BESTANDEN ===" : "=== Selbsttest FEHLER ===");
    return ok;
}

/* Hot-Swap-Probe: frischer Mount-Versuch, Beast lesen, wieder freigeben.
 * Rueckgabe: 0 = keine Karte, 1 = Karte mit gueltigem Beast (in *out),
 * 2 = Karte da, aber kein/ungueltiges Beast. Laesst die Karte ungemountet. */
int shsd_probe(ShBeastCard *out) {
    shsd_end();                         /* sauberer Zustand -> echte Neuerkennung */
    if (!shsd_begin()) return 0;        /* keine Karte */
    int r = 2;
    if (out && shsd_read_beast(out)) r = 1;
    shsd_end();                         /* wieder freigeben (Strom) */
    return r;
}
