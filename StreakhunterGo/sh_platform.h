/* ============================================================
 *  sh_platform (TAMA) — Hardware-Schicht, die EINZIGE Stelle mit Board-Bezug.
 *  ------------------------------------------------------------
 *  Bewusst klein gehalten (Schritt 3): nur das, was game.cpp und sh_sd.cpp
 *  brauchen. Speichern/Laden ueber ESP32-Flash (Preferences/NVS), Datum/Zeit
 *  ueber die RTC PCF85063 (I2C 0x51, im Scan bestaetigt).
 *
 *  NICHT drin (kommt spaeter, bewusst weggelassen):
 *    - Schrittzaehler / IMU     -> entfaellt beim Tamagotchi komplett
 *    - Display-Standby / Reset  -> Schritt 4 (laeuft ueber den XCA9554-Expander,
 *                                  NICHT mehr ueber digitalWrite(LCD_RESET,..))
 *    - AXP2101 / Akku           -> Schritt 4
 *    - Audio (ES8311)           -> spaeter
 * ============================================================ */
#ifndef SH_PLATFORM_H
#define SH_PLATFORM_H

#include <stdint.h>
#include <stddef.h>
#include "streakhunter.h"

/* Einmalig in setup() aufrufen (nach Wire.begin()). */
void     shp_init(void);

/* Aktuelle Tagesnummer (Tage seit 1970). Treibt Tages-/Wochenwechsel. */
uint32_t shp_day_index(void);

/* Minuten seit 1970. Treibt die Vitalwerte.
 * WICHTIG: kommt aus der RTC, NICHT aus millis(). Sonst wuerden Hunger und
 * Energie einfrieren, sobald das Geraet im Standby liegt - und genau dann
 * vergeht die meiste Zeit. */
uint32_t shp_now_minutes(void);

/* Uhrzeit / Datum. Beim Tama keine Hauptanzeige mehr, aber die Zeit wird fuer
 * Hunger-/Schlafzyklen weiterhin gebraucht. weekday: 0=Sonntag .. 6=Samstag. */
void     shp_clock(int *hour, int *minute);
void     shp_set_clock(int hour, int minute);
void     shp_date(int *year, int *month, int *day, int *weekday);
int      shp_rtc_year(void);
bool     shp_rtc_ok(void);

/* Spielstand laden (true = vorhandener Stand) / speichern. */
bool     shp_load(SaveState *s);
void     shp_save(const SaveState *s);
void     shp_save_device(const SaveState *s);   /* nur Geraeteteil (Kartenmodus) */
bool     shp_load_blob(const char *key, void *buf, size_t len);
void     shp_save_blob(const char *key, const void *buf, size_t len);

/* Kompletter NVS-Reset, 1x pro Token. */
bool     shp_factory_reset_once(const char *token);

/* ============================================================
 *  PMIC AXP2101 (I2C 0x34) — NUR LESEN.
 *  Bewusst KEINE Rails geschaltet und KEINE Ladeparameter gesetzt:
 *  Lektion 3 (DCDC1-4 nie anfassen) und weil es fuer dieses Board keine
 *  AXP-Demo gibt, an der man die Werte verifizieren koennte.
 * ============================================================ */
bool     shp_pmu_ok(void);
int      shp_battery_percent(void);   /* 0..100, -1 = unbekannt */
bool     shp_is_charging(void);
bool     shp_power_button_pressed(void);   /* true bei kurzem PWR-Tastendruck */
void     shp_pmu_clear_irq(void);          /* IRQ-Flags loeschen (IRQ-Pin -> HIGH) */
void     shp_pmu_sleep_rails(void);        /* unkritische Rails vor Deep Sleep aus */
void     shp_imu_sleep(void);              /* Bewegungssensor stilllegen (ungenutzt) */
void     shp_pmu_panel_power(void);        /* Panel-Rails hochfahren (vor gfx->begin) */
int      shp_battery_mv(void);             /* rohe Akkuspannung in mV (echter Messwert) */

/* Schlafbilanz: vor dem Deep Sleep merken, nach dem Aufwachen abfragen.
 * Liefert false, wenn noch keine Messung vorliegt. dropMv ist positiv, wenn
 * Ladung verbraucht wurde. onCharger=true bedeutet: Messwert unbrauchbar. */
void     shp_sleep_mark(void);
bool     shp_sleep_report(int *minutes, int *dropMv, bool *onCharger, int *cycles);
void     shp_sleep_reset(void);            /* neues Messfenster (Kaltstart)   */
void     shp_pmu_wake_rails(void);         /* dieselben beim Aufwachen wieder an    */

/* Optionales haptisches Feedback (dieses Board hat keinen Motor -> Stub).
 * Bleibt in der API, damit game.cpp unveraendert kompiliert. */
void     shp_vibrate(uint16_t ms);

/* ============================================================
 *  Display-Standby (Schritt 4)
 *  ------------------------------------------------------------
 *  Der Akku-Hebel: AMOLED-Panel-Deep-Standby ueber Register 0x4F.
 *  Aufwecken geht NUR ueber einen echten Hardware-Reset - und der laeuft
 *  auf diesem Board ueber den XCA9554-Expander, nicht ueber einen GPIO.
 *  NIEMALS gfx->begin() zum Wecken aufrufen (SPI-Bus doppelt init -> Absturz).
 * ============================================================ */

/* Panel in den Deep Standby: Helligkeit 0, Display OFF, Sleep In, 0x4F=0x01. */
void     shp_display_standby(void);

/* Aufwecken: Hardware-Reset ueber den angegebenen EXPANDER-Pin, danach die
 * Panel-Register neu senden. Schaltet das Display NOCH NICHT an - das macht
 * shp_display_show() nach dem kompletten Redraw (sonst gruenes Aufblitzen). */
void     shp_display_wake(uint8_t expResetPin);

/* Display-Ausgang an (DISPON 0x29). Erst NACH dem Redraw aufrufen. */
void     shp_display_show(void);

bool     shp_screen_is_off(void);

/* Helligkeit 0..255 (MIPI-DCS 0x51 ueber den QSPI-Bus). */
void     shp_set_brightness(uint8_t level);   /* sofort anwenden, nicht sichern */
void     shp_commit_brightness(void);         /* aktuellen Wert ins NVS sichern  */
uint8_t  shp_get_brightness(void);

/* Geraet neu starten. */
void     shp_reboot(void);

/* Hilfsfunktion: Datum -> Tagesnummer. */
uint32_t shp_days_from_date(int year, int month, int day);

#endif /* SH_PLATFORM_H */
