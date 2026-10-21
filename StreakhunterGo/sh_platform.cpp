/* ============================================================
 *  sh_platform.cpp (TAMA) — NVS + RTC.
 *  Board: Waveshare ESP32-S3-Touch-AMOLED-1.8
 * ============================================================ */
#include "sh_platform.h"
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <string.h>
#include "pin_config.h"
#include "SensorPCF85063.hpp"
#include "XPowersLib.h"
#include "Arduino_GFX_Library.h"
#include "io_expander.h"

#include "HWCDC.h"
extern HWCDC USBSerial;

/* Der QSPI-Datenbus zum Display lebt im Sketch (.ino). */
extern Arduino_DataBus *bus;

/* Eigener NVS-Namespace: das Tama ist ein eigenstaendiger Fork und teilt sich
 * KEINEN Spielstand mit der Watch. */
#define NVS_NS "shtama"

static Preferences    g_prefs;
static XPowersPMU     g_pmu;
static bool           g_pmu_ok  = false;
static int            g_battPct = -1;
static bool           g_battChg = false;
static uint32_t       g_battMs  = 0;
static bool           g_battValid = false;
static SensorPCF85063 g_rtc;
static bool           g_rtc_ok  = false;

/* Gecachte Zeit: die RTC nicht bei jedem Aufruf ueber I2C anfassen. */
static RTC_DateTime   g_dt;
static bool           g_dtValid = false;
static uint32_t       g_dtMs    = 0;

static void rtc_refresh(void) {
    if (!g_rtc_ok) return;
    if (g_dtValid && (millis() - g_dtMs) < 500) return;   /* Cache 0,5 s */
    g_dt      = g_rtc.getDateTime();
    g_dtValid = true;
    g_dtMs    = millis();
}

/* Uhr aus der Compile-Zeit stellen. Wird NUR benutzt, wenn die RTC noch nie
 * lief (Jahr < 2024). Eine einmal gestellte Zeit ueberlebt Neustarts UND
 * das Flashen. Der Offset gleicht grob die Upload-Dauer aus. */
#define SHP_UPLOAD_OFFSET_S 25
static void rtc_set_from_build_time(void) {
    char mon[4] = {0};
    int  dd = 1, yyyy = 2026, hh = 0, mi = 0, ss = 0;
    sscanf(__DATE__, "%3s %d %d", mon, &dd, &yyyy);
    sscanf(__TIME__, "%d:%d:%d", &hh, &mi, &ss);

    const char *M = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *p = strstr(M, mon);
    int mm = p ? (int)((p - M) / 3) + 1 : 1;

    ss += SHP_UPLOAD_OFFSET_S;
    mi += ss / 60; ss %= 60;
    hh += mi / 60; mi %= 60;
    dd += hh / 24; hh %= 24;

    g_rtc.setDateTime((uint16_t)yyyy, (uint8_t)mm, (uint8_t)dd,
                      (uint8_t)hh, (uint8_t)mi, (uint8_t)ss);
    g_dtValid = false;
}

/* Panel-Versorgung frueh hochfahren - VOR gfx->begin(). Startet die PMU (falls
 * noch nicht geschehen) und setzt die vier ALDO-Rails auf die Board-Spannungen.
 * Muss nach dem Deep Sleep laufen, weil wir die Rails dort abschalten. */
void shp_pmu_panel_power(void) {
    if (!g_pmu_ok) {
        g_pmu_ok = g_pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
        if (!g_pmu_ok) {
            USBSerial.println("[PMU] AXP2101 nicht gefunden - Panel-Rails ungewiss.");
            return;
        }
    }
    g_pmu.setALDO1Voltage(3300); g_pmu.enableALDO1();   /* VL1_3.3V */
    g_pmu.setALDO2Voltage(3300); g_pmu.enableALDO2();   /* VL2_3.3V */
    g_pmu.setALDO3Voltage(3000); g_pmu.enableALDO3();   /* VL3_3V  (Panel) */
    g_pmu.setALDO4Voltage(1800); g_pmu.enableALDO4();   /* VL3_1.8V (Panel) */
    delay(30);   /* kurz stabilisieren, bevor das Panel angesprochen wird */
    USBSerial.println("[PMU] Panel-Rails hochgefahren (ALDO1-4).");
}

/* --- PMIC: nur Messungen und die PWR-Taste. Keine Rails, keine Ladewerte. --- */
static void pmu_init(void) {
    g_pmu_ok = g_pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
    if (!g_pmu_ok) {
        USBSerial.println("[PMU] AXP2101 nicht gefunden - kein Akkustand, keine PWR-Taste.");
        return;
    }
    g_pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    g_pmu.clearIrqStatus();
    g_pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);   /* kurzer Tastendruck */
    g_pmu.enableBattDetection();
    g_pmu.enableBattVoltageMeasure();
    g_pmu.enableVbusVoltageMeasure();
    g_pmu.enableSystemVoltageMeasure();
    g_pmu.disableTemperatureMeasure();                 /* ungenutzt -> spart Dauer-ADC */

    /* Panel-Rails werden separat und FRUEH gesetzt (shp_pmu_panel_power),
     * damit sie schon vor gfx->begin() stehen - wichtig nach dem Deep Sleep,
     * wo wir sie zuvor abgeschaltet haben. */
    USBSerial.println("[PMU] AXP2101 OK (Panel-Rails via shp_pmu_panel_power).");
}

/* --- IMU (QMI8658, I2C 0x6B) dauerhaft stilllegen ---
 *  Der Bewegungssensor wird beim Tamagotchi gar nicht genutzt, laeuft nach dem
 *  Power-On aber im Standardmodus weiter und zieht so dauerhaft Strom. Wir
 *  schalten Beschleunigungssensor UND Gyroskop komplett ab (CTRL7 = 0). Damit
 *  geht der Chip in seinen Ruhezustand und zieht nur noch wenige Mikroampere -
 *  im Betrieb wie im Deep Sleep. Einmal beim Boot reicht, weil ein
 *  Deep-Sleep-Aufwachen ohnehin ein Neustart ist. */
#define QMI8658_ADDR   0x6B
#define QMI8658_CTRL7  0x08
static void imu_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(QMI8658_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}
void shp_imu_sleep(void) {
    /* Pruefen, ob der Chip ueberhaupt da ist (nicht jedes Board hat ihn). */
    Wire.beginTransmission(QMI8658_ADDR);
    if (Wire.endTransmission() != 0) {
        USBSerial.println("[IMU] QMI8658 nicht gefunden - nichts abzuschalten.");
        return;
    }
    imu_write(QMI8658_CTRL7, 0x00);   /* Accel + Gyro AUS -> Ruhezustand */
    USBSerial.println("[IMU] QMI8658 stillgelegt - spart Dauerstrom.");
}

static void batt_refresh(void) {
    if (!g_pmu_ok) return;
    uint32_t now = millis();
    if (g_battValid && (now - g_battMs) < 5000) return;   /* Cache 5 s */
    if (g_pmu.isBatteryConnect()) {
        int p = (int)g_pmu.getBatteryPercent();
        g_battPct = (p < 0) ? -1 : (p > 100 ? 100 : p);
    } else {
        g_battPct = -1;
    }
    g_battChg   = g_pmu.isCharging();
    g_battMs    = now;
    g_battValid = true;
}

bool shp_pmu_ok(void) { return g_pmu_ok; }

int shp_battery_percent(void) { batt_refresh(); return g_battPct; }

/* Rohe Akkuspannung in Millivolt. Anders als die Prozentanzeige (die aus einer
 * nichtlinearen Spannungskurve geschaetzt wird) ist das ein direkt
 * vergleichbarer Messwert - das richtige Werkzeug, um Verbrauch zu beurteilen. */
/* --- Schlafbilanz -------------------------------------------------------
 * Vor jedem Deep Sleep werden Spannung und Uhrzeit im RTC-Speicher abgelegt;
 * beim Aufwachen wird die Differenz auf einen Zaehler addiert. So summiert sich
 * die Messung ueber MEHRERE Schlafphasen, statt bei jedem kurzen Blick aufs
 * Geraet von vorn zu beginnen. Wachzeiten fliessen bewusst nicht ein - gemessen
 * wird nur, was der Schlaf kostet.
 *
 * Wichtig: haengt beim Aufwachen USB dran, laedt der AXP2101 und die gemessene
 * Spannung ist die LADESPANNUNG, nicht die Ruhespannung. Solche Messungen sind
 * wertlos und werden als ungueltig gemeldet. */
RTC_DATA_ATTR static int32_t  s_accDropMv = 0;   /* Summe verbrauchter mV       */
RTC_DATA_ATTR static uint32_t s_accMin    = 0;   /* Summe geschlafener Minuten  */
RTC_DATA_ATTR static uint32_t s_cycles    = 0;   /* Anzahl Schlafphasen         */
RTC_DATA_ATTR static uint32_t s_sleepMv   = 0;   /* Spannung beim Einschlafen   */
RTC_DATA_ATTR static uint32_t s_sleepMin  = 0;   /* Uhrzeit beim Einschlafen    */

/* Neues Messfenster beginnen (Kaltstart oder nach dem Laden). */
void shp_sleep_reset(void) {
    s_accDropMv = 0; s_accMin = 0; s_cycles = 0;
    s_sleepMv   = 0; s_sleepMin = 0;
}

/* Direkt vor dem Deep Sleep aufrufen. */
void shp_sleep_mark(void) {
    if (!g_pmu_ok) return;
    s_sleepMv  = (uint32_t)g_pmu.getBattVoltage();
    s_sleepMin = shp_now_minutes();
}

/* Bilanz abfragen. Die Verrechnung der gerade beendeten Schlafphase passiert
 * genau einmal je Boot - danach liefern weitere Aufrufe dieselben Summen. */
bool shp_sleep_report(int *minutes, int *dropMv, bool *onCharger, int *cycles) {
    if (!g_pmu_ok || s_sleepMv == 0) return false;

    static bool accounted = false;         /* normales RAM: pro Boot einmal */
    int mv = (int)g_pmu.getBattVoltage();

    if (!accounted) {
        uint32_t now   = shp_now_minutes();
        uint32_t slept = (now > s_sleepMin) ? (now - s_sleepMin) : 0;
        s_accMin    += slept;
        s_accDropMv += (int32_t)s_sleepMv - mv;
        s_cycles++;
        accounted = true;
    }

    if (minutes) *minutes = (int)s_accMin;
    if (dropMv)  *dropMv  = (int)s_accDropMv;
    if (cycles)  *cycles  = (int)s_cycles;
    /* Nur die erprobte isCharging()-Abfrage nutzen. Zusaetzlich gilt eine
     * GESTIEGENE Spannung als sicheres Zeichen fuer Netzbetrieb - genau der
     * Fall "Akku voll am Kabel", den isCharging() nicht meldet. */
    if (onCharger) *onCharger = g_pmu.isCharging() || (mv > (int)s_sleepMv);
    return s_accMin > 0;
}

int shp_battery_mv(void) {
    if (!g_pmu_ok) return -1;
    return (int)g_pmu.getBattVoltage();
}
bool shp_is_charging(void)    { batt_refresh(); return g_battChg; }

/* PWR-Taste abfragen. Gedrosselt (~12 Hz), damit der I2C-Bus nicht dauernd
 * belastet wird - im Standby laeuft der Loop sonst frei durch. */
bool shp_power_button_pressed(void) {
    if (!g_pmu_ok) return false;
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < 80) return false;
    last = now;
    g_pmu.getIrqStatus();
    bool pressed = g_pmu.isPekeyShortPressIrq();
    g_pmu.clearIrqStatus();
    return pressed;
}

/* IRQ-Flags im AXP loeschen. Der IRQ-Ausgang des Chips ist Open-Drain und
 * bleibt LOW, solange ein Interrupt ansteht - erst das Loeschen laesst ihn
 * wieder auf HIGH gehen. Genau daran erkennt man den Pin. */
/* ============================================================
 *  Rails vor dem Deep Sleep abschalten
 *  ------------------------------------------------------------
 *  Der ESP32-Deep-Sleep schaltet nur den ESP32 ab. Die Rails des AXP2101
 *  laufen weiter und versorgen Chips, die nie schlafen. Das war die Ursache
 *  fuer die ~18 % Verlust pro Nacht.
 *
 *  Wir schalten NUR ab, was gefahrlos ist:
 *    BLDO2 (2800 mV)  Audio-Codec/Verstaerker. Das Spiel hat keinen Ton -
 *                     dieses Rail ist reiner Leerlauf-Verbrauch.
 *    DLDO1 ( 500 mV)  laeuft ins Leere. 500 mV ist kein gueltiges Rail.
 *
 *  Panel- und Logik-Rails (DC1, DC3, ALDO*, BLDO1) bleiben AN - das Panel
 *  schlaeft ohnehin schon per Deep-Standby-Befehl, und diese Rails
 *  abzuschalten wuerde das Aufwachen gefaehrden.
 *
 *  Schrittweise testbar ueber die Schalter unten. */
#define PMU_SLEEP_KILL_BLDO2  1
#define PMU_SLEEP_KILL_DLDO1  1

/* Panel- und Logik-Rails vor dem Deep Sleep. Bis zu vier ALDO-Rails versorgen
 * beim Waveshare-1.8-AMOLED das Display (laut Schaltplan: ALDO1=VL1_3.3V,
 * ALDO2=VL2_3.3V, ALDO3=VL3_3V, ALDO4=VL3_1.8V). Sie laufen sonst im Deep Sleep
 * weiter und ziehen ~11 mA - der eigentliche Akkufresser.
 *
 * Weil das Aufwachen aus dem Deep Sleep ein kompletter NEUSTART ist (setup()
 * mit gfx->begin() und voller Panel-Init), duerfen wir sie hier gefahrlos
 * abschalten - beim Boot kommen sie ueber pmu_init wieder hoch.
 *
 * Schrittweise testbar: erst nur ALDO1, dann mehr. So laesst sich einkreisen,
 * welche Rail wieviel bringt und ob das Aufwachen zuverlaessig bleibt. */
#define PMU_SLEEP_KILL_ALDO1  1
#define PMU_SLEEP_KILL_ALDO2  1
#define PMU_SLEEP_KILL_ALDO3  1
#define PMU_SLEEP_KILL_ALDO4  1

void shp_pmu_sleep_rails(void) {
    if (!g_pmu_ok) return;
#if PMU_SLEEP_KILL_BLDO2
    g_pmu.disableBLDO2();
    USBSerial.println("[PMU] BLDO2 (Audio) aus.");
#endif
#if PMU_SLEEP_KILL_DLDO1
    g_pmu.disableDLDO1();
    USBSerial.println("[PMU] DLDO1 aus.");
#endif
#if PMU_SLEEP_KILL_ALDO4
    g_pmu.disableALDO4();
    USBSerial.println("[PMU] ALDO4 (Panel 1.8V) aus.");
#endif
#if PMU_SLEEP_KILL_ALDO3
    g_pmu.disableALDO3();
    USBSerial.println("[PMU] ALDO3 (Panel 3V) aus.");
#endif
#if PMU_SLEEP_KILL_ALDO2
    g_pmu.disableALDO2();
    USBSerial.println("[PMU] ALDO2 (VL2 3.3V) aus.");
#endif
#if PMU_SLEEP_KILL_ALDO1
    g_pmu.disableALDO1();
    USBSerial.println("[PMU] ALDO1 (VL1 3.3V) aus.");
#endif
}

/* Beim Aufwachen wieder anschalten - falls spaeter doch Audio dazukommt oder
 * ein Rail gebraucht wird, ist es dann wieder da. */
void shp_pmu_wake_rails(void) {
    if (!g_pmu_ok) return;
#if PMU_SLEEP_KILL_BLDO2
    g_pmu.setBLDO2Voltage(2800);
    g_pmu.enableBLDO2();
#endif
#if PMU_SLEEP_KILL_DLDO1
    g_pmu.enableDLDO1();
#endif
}

void shp_pmu_clear_irq(void) {
    if (!g_pmu_ok) return;
    g_pmu.getIrqStatus();
    g_pmu.clearIrqStatus();
}

void shp_init(void) {
    /* Wire wurde im Sketch bereits gestartet; begin() hier ist unkritisch. */
    pmu_init();
    shp_imu_sleep();      /* ungenutzten Bewegungssensor abschalten (Strom sparen) */
    g_rtc_ok = g_rtc.begin(Wire, IIC_SDA, IIC_SCL);
    if (!g_rtc_ok) {
        USBSerial.println("[RTC] PCF85063 NICHT gefunden - Datum laeuft im Fallback!");
        return;
    }
    RTC_DateTime dt = g_rtc.getDateTime();
    if ((int)dt.getYear() < 2024) {
        USBSerial.println("[RTC] Uhr lief noch nie -> aus Compile-Zeit gestellt.");
        rtc_set_from_build_time();
    }
    rtc_refresh();
    USBSerial.printf("[RTC] OK  %04d-%02d-%02d  %02d:%02d\n",
                     (int)g_dt.getYear(), (int)g_dt.getMonth(), (int)g_dt.getDay(),
                     (int)g_dt.getHour(), (int)g_dt.getMinute());
}

bool shp_rtc_ok(void) { return g_rtc_ok; }

int shp_rtc_year(void) {
    if (!g_rtc_ok) return 0;
    rtc_refresh();
    return (int)g_dt.getYear();
}

void shp_clock(int *hour, int *minute) {
    if (!g_rtc_ok) { if (hour) *hour = 0; if (minute) *minute = 0; return; }
    rtc_refresh();
    if (hour)   *hour   = (int)g_dt.getHour();
    if (minute) *minute = (int)g_dt.getMinute();
}

void shp_set_clock(int hour, int minute) {
    if (!g_rtc_ok) return;
    rtc_refresh();
    g_rtc.setDateTime((uint16_t)g_dt.getYear(), (uint8_t)g_dt.getMonth(), (uint8_t)g_dt.getDay(),
                      (uint8_t)hour, (uint8_t)minute, 0);
    g_dtValid = false;
}

void shp_date(int *year, int *month, int *day, int *weekday) {
    if (!g_rtc_ok) {
        if (year) *year = 1970; if (month) *month = 1;
        if (day)  *day  = 1;    if (weekday) *weekday = 4;
        return;
    }
    rtc_refresh();
    if (year)    *year    = (int)g_dt.getYear();
    if (month)   *month   = (int)g_dt.getMonth();
    if (day)     *day     = (int)g_dt.getDay();
    if (weekday) *weekday = (int)g_dt.getWeek();
}

/* Tage seit 1970 (Howard-Hinnant-Algorithmus, 1:1 aus der Watch). */
uint32_t shp_days_from_date(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + (long)doe - 719468;
    return (uint32_t)days;
}

uint32_t shp_now_minutes(void) {
    if (g_rtc_ok) {
        rtc_refresh();
        uint32_t days = shp_days_from_date((int)g_dt.getYear(), (int)g_dt.getMonth(), (int)g_dt.getDay());
        return days * 1440u + (uint32_t)g_dt.getHour() * 60u + (uint32_t)g_dt.getMinute();
    }
    return (uint32_t)(millis() / 60000UL);   /* Notnagel ohne RTC */
}

uint32_t shp_day_index(void) {
    if (g_rtc_ok) {
        rtc_refresh();
        return shp_days_from_date((int)g_dt.getYear(), (int)g_dt.getMonth(), (int)g_dt.getDay());
    }
    return (uint32_t)(millis() / 86400000UL);   /* Fallback ohne RTC */
}

/* ============================================================
 *  Spielstand (NVS)
 * ============================================================ */
bool shp_load(SaveState *s) {
    g_prefs.begin(NVS_NS, true);                     /* read-only */
    size_t n = g_prefs.getBytesLength("save");
    bool ok = false;
    if (n == sizeof(SaveState)) {
        g_prefs.getBytes("save", s, sizeof(SaveState));
        ok = (s->version == SH_SAVE_VERSION);
    }
    g_prefs.end();
    return ok;
}

void shp_save(const SaveState *s) {
    g_prefs.begin(NVS_NS, false);
    g_prefs.putBytes("save", s, sizeof(SaveState));
    g_prefs.end();
}

void shp_save_device(const SaveState *s) {
    /* Beim Tama identisch zu shp_save: das Beast lebt auf der Karte,
     * der Rest im NVS. */
    shp_save(s);
}

bool shp_load_blob(const char *key, void *buf, size_t len) {
    g_prefs.begin(NVS_NS, true);
    size_t n = g_prefs.getBytesLength(key);
    bool ok = (n == len);
    if (ok) g_prefs.getBytes(key, buf, len);
    g_prefs.end();
    return ok;
}

void shp_save_blob(const char *key, const void *buf, size_t len) {
    g_prefs.begin(NVS_NS, false);
    g_prefs.putBytes(key, buf, len);
    g_prefs.end();
}

bool shp_factory_reset_once(const char *token) {
    g_prefs.begin(NVS_NS, false);
    char cur[40] = {0};
    g_prefs.getString("rstTok", cur, sizeof(cur));
    bool need = (strcmp(cur, token) != 0);
    if (need) {
        g_prefs.clear();
        g_prefs.putString("rstTok", token);
    }
    g_prefs.end();
    return need;
}

/* ============================================================
 *  Sonstiges
 * ============================================================ */
void shp_vibrate(uint16_t ms) {
    (void)ms;   /* Dieses Board hat keinen Vibrationsmotor. Spaeter ggf. Piepton. */
}

/* ============================================================
 *  Display-Standby / Wake
 *  ------------------------------------------------------------
 *  Der QSPI-Bus lebt im Sketch (.ino). Die Panel-Register sprechen wir direkt
 *  per MIPI-DCS ueber den Bus an, weil Arduino_GFX (CO5300) dafuer keine
 *  Methoden hat. Sequenz 1:1 aus der laufenden Watch-Firmware; einziger
 *  Unterschied: der Hardware-Reset laeuft ueber den Expander statt ueber
 *  digitalWrite(LCD_RESET, ...) - diesen GPIO gibt es hier nicht.
 * ============================================================ */
static bool    g_screenOff = false;
static uint8_t g_bright    = 200;

static void amoled_apply_brightness(uint8_t level) {
    if (!bus) return;
    bus->beginWrite(); bus->writeC8D8(0x53, 0x20); bus->endWrite();  /* BCTRL an     */
    bus->beginWrite(); bus->writeC8D8(0x51, level); bus->endWrite();  /* Helligkeit   */
}

void shp_set_brightness(uint8_t level) {
    g_bright = level;
    if (!g_screenOff) amoled_apply_brightness(level);
}

uint8_t shp_get_brightness(void) { return g_bright; }

/* Beim Schieben des Reglers wird die Helligkeit sofort angewendet, aber NICHT
 * gesichert - sonst schriebe jeder Pixel Bewegung in den Flash. Gesichert wird
 * erst beim Loslassen. */
void shp_commit_brightness(void) {
    shp_save_blob("bright", &g_bright, sizeof(g_bright));
}

bool shp_screen_is_off(void) { return g_screenOff; }

void shp_display_standby(void) {
    if (!bus || g_screenOff) return;
    bus->beginWrite(); bus->writeC8D8(0x51, 0x00);  bus->endWrite();  /* Helligkeit 0 */
    bus->beginWrite(); bus->writeCommand(0x28);     bus->endWrite();  /* Display OFF  */
    bus->beginWrite(); bus->writeCommand(0x10);     bus->endWrite();  /* Sleep In     */
    bus->beginWrite(); bus->writeC8D8(0x4F, 0x01);  bus->endWrite();  /* Deep Standby */
    g_screenOff = true;
}

void shp_display_wake(uint8_t expResetPin) {
    if (!bus || !g_screenOff) return;

    /* Hardware-Reset ueber den Expander: HIGH 10 ms -> LOW 10 ms -> HIGH 120 ms.
     * Genau das Timing, das auf der Watch funktioniert hat. */
    exp_reset_pulse(expResetPin);

    /* Panel-Register neu senden. KEIN gfx->begin()! (Lektion 1) */
    bus->beginWrite(); bus->writeCommand(0x11);     bus->endWrite();  /* Sleep Out       */
    delay(120);
    bus->beginWrite(); bus->writeC8D8(0xFE, 0x00);  bus->endWrite();  /* Vendor-Register */
    bus->beginWrite(); bus->writeC8D8(0xC4, 0x80);  bus->endWrite();  /* SPI-Mode-Control*/
    bus->beginWrite(); bus->writeC8D8(0x3A, 0x55);  bus->endWrite();  /* RGB565          */
    bus->beginWrite(); bus->writeC8D8(0x53, 0x20);  bus->endWrite();  /* CTRL Display    */
    bus->beginWrite(); bus->writeC8D8(0x63, 0xFF);  bus->endWrite();  /* HBM-Helligkeit  */
    bus->beginWrite(); bus->writeC8D8(0x35, 0x00);  bus->endWrite();  /* Tearing Effect  */
    /* KEIN Display-ON (0x29) hier -> erst nach dem Redraw via shp_display_show(). */

    g_screenOff = false;
    /* Gesicherte Helligkeit holen, bevor das Panel sie anwendet. */
    uint8_t saved = 0;
    if (shp_load_blob("bright", &saved, sizeof(saved)) && saved >= 40) g_bright = saved;
    amoled_apply_brightness(g_bright);
}

void shp_display_show(void) {
    if (!bus) return;
    bus->beginWrite(); bus->writeCommand(0x29); bus->endWrite();      /* Display ON */
}

void shp_reboot(void) {
    delay(50);
    ESP.restart();
}
