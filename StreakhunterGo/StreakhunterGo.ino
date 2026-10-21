/* ============================================================
 *  StreakhunterGo.ino — Streakhunter Go (Waveshare ESP32-S3-Touch-AMOLED-1.8)
 *  Board: Waveshare ESP32-S3-Touch-AMOLED-1.8
 *
 *  Neu gegenueber Schritt 2:
 *    - sh_platform (NVS + RTC PCF85063)
 *    - streakhunter.c (Spiellogik, UNVERAENDERT uebernommen)
 *    - game.cpp (Schritt-Sync entfernt)
 *    - sh_sd.cpp (Dim-Card, UNVERAENDERT uebernommen)
 *
 *  Ziel: Das Beast von der SD-Karte erscheint auf dem Display und im
 *        Serial Monitor. Karte im Betrieb ziehen/stecken -> Anzeige folgt.
 *
 *  Dateien im Sketch-Ordner:
 *    StreakhunterGo.ino, pin_config.h, partitions.csv,
 *    io_expander.h/.cpp, sh_platform.h/.cpp, game.h/.cpp,
 *    streakhunter.h/.c, sh_sd.h/.cpp, assets.h,
 *    img_ruga_1..3, img_pintan_1..3, img_fearo_1..3,
 *    img_leona_1..3, img_lurco_1..3, img_grelo_1..3   (18 Sprites)
 * ============================================================ */
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <lvgl.h>
#include "lv_conf.h"
#include "pin_config.h"
#include "io_expander.h"

#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "esp_log.h"
#include "esp_sleep.h"

#include "HWCDC.h"
HWCDC USBSerial;

/* 1 = SD-Suchlauf beim Start (probiert die unbekannten Expander-Pins durch).
 * ERLEDIGT: die SD laeuft, sobald 3,4,5,7 Eingaenge bleiben. -> aus. */
#define SD_PIN_HUNT 0

/* 1 = Display-Reset-Suchlauf.
 * ERLEDIGT: Pin 0 UND Pin 1 wecken das Panel (gleiches Reset-Netz).
 * Pin 2 und 6 nicht. -> aus. */
#define RESET_PIN_HUNT 0

/* Der Display-Reset am Expander. Pin 1 wuerde auch gehen; wir fassen bewusst
 * nur EINE Leitung an, um Nebenwirkungen (z.B. Touch-Reset) zu vermeiden. */
#define TAMA_LCD_RST_EXP_PIN 0

/* Auto-Aus nach Inaktivitaet. Aufgeweckt wird per Fingertipp. */
#define SCREEN_TIMEOUT_MS 30000      /* Display aus nach 30 s ohne Beruehrung */

/* ============================================================
 *  ENERGIESPAREN — zwei Stufen
 *  ------------------------------------------------------------
 *  Stufe 1 (nach 30 s):   Panel aus, CPU laeuft in kurzen Haeppchen weiter.
 *                         Wecken dauert ~320 ms. Fuer die Kaffeepause.
 *
 *  Stufe 2 (nach 5 min):  DEEP SLEEP. CPU und RAM aus, nur die RTC-Domain
 *                         bleibt wach. Verbrauch faellt von Milliampere auf
 *                         Mikroampere.
 *
 *  Warum das hier so gut funktioniert: Das Geraet muss im Schlaf NICHTS tun.
 *  Saettigung, Energie und Abenteuer haengen an der ECHTEN Uhr (RTC), nicht
 *  an millis(). Beim Aufwachen rechnet vit_tick() die ganze verstrichene Zeit
 *  nach - ob fuenf Minuten oder drei Tage vergangen sind, ist ihm egal.
 *
 *  Der Preis: Aufwachen aus Deep Sleep ist ein NEUSTART. setup() laeuft
 *  komplett durch. Deshalb die zweite Stufe erst nach 5 Minuten - wer das
 *  Geraet gerade weggelegt hat, soll es sofort wieder wecken koennen.
 * ============================================================ */
/* Signalleitungen vor dem Deep Sleep abkoppeln. Sollte Leckstroeme in die
 * abgeschalteten Rails verhindern - hat aber das Aufwachen des Displays
 * zerstoert. Deshalb standardmaessig AUS. Die Gegenmassnahme beim Booten
 * (Leitungen wieder freigeben) laeuft bewusst IMMER, damit ein Geraet, das
 * noch isoliert eingeschlafen ist, sauber zurueckkommt. */
#define DEEP_SLEEP_ISOLATE_PINS 0

#define STANDBY_BEFORE_SLEEP_MS 30000 /* Zeit im Panel-Standby, dann Deep Sleep.
                                       * An den Standby-Beginn gekoppelt, damit der
                                       * Uebergang garantiert greift. Ergebnis: ~60 s
                                       * nach der letzten Beruehrung schlaeft das
                                       * Geraet echt (30 s Display-Timeout + 30 s). */



/* ============================================================
 *  WECKEN — der BOOT-Taster (GPIO 0)
 *  ------------------------------------------------------------
 *  Zwei Suchlaeufe haben gezeigt: Der IRQ-Ausgang des AXP2101 liegt auf KEINEM
 *  freien GPIO. Die PWR-Taste haengt nur am I2C-Bus, und der schlaeft im Deep
 *  Sleep mit - sie kann also nicht wecken.
 *
 *  Der BOOT-Taster dagegen liegt sauber auf GPIO 0, ist RTC-faehig und liefert
 *  klare Flanken (im Test 14 von 14 Druecken erkannt). Er ist damit unsere
 *  Weckquelle - und weil wir TP_INT dafuer nicht mehr brauchen, darf der
 *  Touch-Controller im Schlaf endlich abschalten. Das spart die 1-2 mA, die er
 *  im Monitor-Modus dauerhaft gezogen hat.
 *
 *  Ruhezustand HIGH (Pull-up), gedrueckt LOW.
 * ============================================================ */
#define WAKE_BTN 0

/* ============================================================
 *  PWR_PIN_HUNT — Suchlauf nach dem AXP-Interrupt-Pin
 *  ------------------------------------------------------------
 *  Die PWR-Taste wird bisher ueber I2C abgefragt (g_pmu.getIrqStatus()). Im
 *  Deep Sleep schlaeft die CPU, es gibt kein I2C - sie kann das Geraet also
 *  NICHT wecken. Deshalb ist TP_INT derzeit die einzige Weckquelle.
 *
 *  Der AXP2101 hat aber einen eigenen IRQ-Ausgang, und der PKEY_SHORT_IRQ ist
 *  bereits freigeschaltet: bei jedem Tastendruck zieht der Chip diesen Pin auf
 *  LOW. Wir wissen nur nicht, an welchem GPIO er auf diesem Board haengt.
 *
 *  Der Suchlauf findet es heraus. Danach koennen wir:
 *    - ueber die PWR-Taste aus dem Deep Sleep wecken
 *    - den Touch-Controller im Schlaf KOMPLETT abschalten (spart 1-2 mA)
 *
 *  ERGEBNIS DES SUCHLAUFS (erledigt, nicht nochmal noetig):
 *      GPIO 13  liegt fest auf LOW - auch nach clearIrqStatus(). Kein IRQ.
 *      GPIO 0, 17, 18  bleiben HIGH und reagieren auf keinen Tastendruck.
 *
 *  Der AXP-Interrupt ist auf diesem Board NICHT herausgefuehrt. Waveshare
 *  fragt die Taste per I2C ab, also braucht das Board keinen Pin dafuer.
 *
 *  FOLGE: Die PWR-Taste kann NICHT aus dem Deep Sleep wecken (kein I2C im
 *  Schlaf). TP_INT (GPIO 21) bleibt die einzige Weckquelle - Tap-to-wake
 *  muss bleiben. Diese Frage ist geklaert, der Suchlauf bleibt nur als Beleg.
 * ============================================================ */
#define PWR_PIN_HUNT 0

/* 1 = CPU zusaetzlich in 50-ms-Haeppchen schlafen legen. Bringt laut Watch-
 * Messung nur wenig (der Octal-PSRAM bleibt bestromt) und stoert am USB-Kabel
 * den Serial Monitor. Zum Messen einschalten, zum Entwickeln aus lassen. */
/* 1 = CPU zusaetzlich in 50-ms-Haeppchen schlafen legen. Bringt laut Watch-
 * Messung nur wenig (der Octal-PSRAM bleibt bestromt) und stoert am USB-Kabel
 * den Serial Monitor. Zum Messen einschalten, zum Entwickeln aus lassen.
 *
 * Fuer die Akkulaufzeit ist ohnehin der DEEP SLEEP entscheidend (siehe unten):
 * dort geht der PSRAM mit aus, und genau das macht den Unterschied zwischen
 * Milliampere und Mikroampere. Light Sleep ist nur die Ueberbrueckung dorthin. */
#define TAMA_LIGHT_SLEEP 0

#include "sh_platform.h"
#include "streakhunter.h"
#include "game.h"
#include "sh_sd.h"
#include "assets.h"
#include "ui.h"
#include "vitals.h"
#include "items.h"
#include "adventure.h"
#include "equip.h"
#include "loadout.h"
#include <driver/rtc_io.h>   /* Pull-up in der RTC-Domain halten */
#include <driver/gpio.h>     /* Pins vor dem Schlaf hochohmig legen */

/* ============================================================
 *  Display / Touch  (unveraendert aus Schritt 2)
 * ============================================================ */
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_GFX *gfx = new Arduino_CO5300(bus, GFX_NOT_DEFINED /* RST via Expander */,
                                      0, LCD_WIDTH, LCD_HEIGHT,
                                      16 /* col_offset1 */, 0, 0, 0);

std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
  std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void Arduino_IIC_Touch_Interrupt(void);

std::unique_ptr<Arduino_IIC> CST816(new Arduino_CST816x(IIC_Bus, CST816T_DEVICE_ADDRESS,
                                                        DRIVEBUS_DEFAULT_VALUE, TP_INT,
                                                        Arduino_IIC_Touch_Interrupt));

void Arduino_IIC_Touch_Interrupt(void) { CST816->IIC_Interrupt_Flag = true; }

static bool g_touchOk = false;
static uint32_t g_lastActivityMs = 0;   /* letzte Beruehrung -> Auto-Aus-Timer */

/* --- Wischgesten: Rohkoordinaten aus dem Touch-Callback ---
 * Bewusst NICHT ueber LVGL-Gesten: antippbare Kinder (Icon-Kacheln, das
 * Beast) wuerden sie schlucken. Die UI liest diese vier Werte in
 * ui_handle_swipe(). Gleiche Loesung wie in der Watch-Firmware. */
int32_t g_swipeStartX = 0, g_swipeStartY = 0;
int32_t g_swipeEndX   = 0, g_swipeEndY   = 0;
volatile bool g_swipePending = false;

/* Groesste Auslenkung vom Aufsetzpunkt WAEHREND des Drucks. Daran erkennt die
 * UI, ob es ein Tipp oder ein Wisch war - zuverlaessiger als der Endpunkt,
 * weil der CST816 beim Abheben springende Koordinaten liefert. */
volatile int32_t g_touchMaxDev = 0;

/* Zaehlt jeden NEUEN Fingerkontakt. Die UI nutzt das, um zu erkennen, ob ein
 * Klick zum selben Druck gehoert wie eine gerade ausgeloeste Aktion.
 *
 * Warum das noetig ist: Der CST816 pulst TP_INT nur beim AUFSETZEN und beim
 * ABHEBEN, nicht durchgehend. Ein einziger Tipp erzeugt deshalb ZWEI
 * Press/Release-Zyklen. Ohne diesen Zaehler wuerde ein Overlay, das im ersten
 * Zyklus aufgeht, den zweiten Zyklus als eigenen Klick verbuchen und sich
 * sofort wieder schliessen. */
volatile uint32_t g_touchPress = 0;

/* Zeitpunkt der letzten Beruehrung. Die UI erkennt daran, ob der Finger
 * WIRKLICH weg war - das ist die einzige verlaessliche Grenze zwischen zwei
 * Tipps. Der Druckzykluszaehler taugt dafuer nicht: das Abheben erzeugt
 * selbst einen neuen Zyklus. */
volatile uint32_t g_touchLastMs = 0;

/* Wie lange ein Druck nach der letzten Flanke noch als bestehend gilt.
 * Grosszuegig, damit ein normaler Tipp moeglichst EIN Zyklus bleibt. */
#define TOUCH_HOLD_MS 140

/* 1 = jeder Druckzyklus wird im Serial Monitor protokolliert. */
#define DEBUG_TOUCH 1

#define CST816_ADDR   0x15
#define CST816_IRQCTL 0xFA

/* Antwortet der Touch-Controller noch? (Nach dem Reset-Puls pruefen wir das:
 * falls der Display-Reset den CST816 mit resettet, sehen wir es sofort.) */
static bool touch_alive(void) {
  Wire.beginTransmission(0x15);
  return (Wire.endTransmission() == 0);
}

/* ============================================================
 *  LVGL
 * ============================================================ */
static uint32_t     screenWidth, screenHeight, bufSize;
static lv_display_t *disp;
static lv_color_t   *disp_draw_buf;


static uint32_t millis_cb(void) { return millis(); }

static void my_disp_flush(lv_display_t *d, const lv_area_t *area, uint8_t *px_map) {
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map,
                          lv_area_get_width(area), lv_area_get_height(area));
  lv_disp_flush_ready(d);
}

static void rounder_event_cb(lv_event_t *e) {
  lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
  uint16_t x1 = area->x1, x2 = area->x2, y1 = area->y1, y2 = area->y2;
  area->x1 = (x1 >> 1) << 1;
  area->y1 = (y1 >> 1) << 1;
  area->x2 = ((x2 >> 1) << 1) + 1;
  area->y2 = ((y2 >> 1) << 1) + 1;
}

/* ============================================================
 *  CST816 direkt auslesen — die einzige verlaessliche Quelle
 *  ------------------------------------------------------------
 *  TP_INT taugt bei diesem Chip NICHT als Kontaktindikator: er pulst nur,
 *  statt den Pegel zu halten (beim FT3168 der Watch ist es umgekehrt - daher
 *  laeuft dieselbe UI-Logik dort problemlos). Jeder Versuch, den Druckzustand
 *  aus dem Pin abzuleiten, erzeugt Phantomzyklen: ein Tipp zerfaellt in zwei
 *  Press/Release-Paare, Overlays schliessen sich selbst, Kacheln flackern.
 *
 *  Register 0x02 sagt direkt, wie viele Finger aufliegen. Das ist eindeutig.
 *
 *  Registerkarte (CST816S/CST820):
 *      0x02  FingerNum
 *      0x03  XposH (untere 4 Bit)      0x04  XposL
 *      0x05  YposH (untere 4 Bit)      0x06  YposL
 *
 *  TP_INT bleibt fuer EINE Sache zustaendig: das Aufwecken aus dem Standby.
 *  Dafuer genuegt ein Impuls.
 * ============================================================ */
static bool cst816_read(int32_t *x, int32_t *y) {
  uint8_t b[5];
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0x02);                       /* FingerNum */
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)CST816_ADDR, (uint8_t)5) != 5) return false;
  for (int i = 0; i < 5; i++) b[i] = Wire.read();

  if (b[0] == 0) return false;            /* kein Finger */
  *x = (int32_t)(((b[1] & 0x0F) << 8) | b[2]);
  *y = (int32_t)(((b[3] & 0x0F) << 8) | b[4]);
  return true;
}

static void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
  static uint32_t pressMs = 0;
  static bool     wasDown = false;
  static int32_t  lastX = 0, lastY = 0;

  if (!g_touchOk) { data->state = LV_INDEV_STATE_REL; return; }
  CST816->IIC_Interrupt_Flag = false;     /* Flag nur noch fuers Aufwecken */

  int32_t x = 0, y = 0;
  bool down = cst816_read(&x, &y);        /* Fingeranzahl aus Register 0x02 */

  if (down) {
    g_lastActivityMs = millis();
    g_touchLastMs    = millis();
    lastX = x;
    lastY = y;

    if (!wasDown) {                       /* Finger setzt auf */
      g_swipeStartX = x;
      g_swipeStartY = y;
      g_touchMaxDev = 0;
      g_touchPress++;
      pressMs = millis();
      wasDown = true;
    } else {                              /* groesste Auslenkung mitschreiben */
      int32_t dx = x - g_swipeStartX; if (dx < 0) dx = -dx;
      int32_t dy = y - g_swipeStartY; if (dy < 0) dy = -dy;
      int32_t d  = (dx > dy) ? dx : dy;
      if (d > g_touchMaxDev) g_touchMaxDev = d;
    }

    data->state   = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    if (wasDown) {                        /* Finger geht hoch */
      g_swipeEndX    = lastX;
      g_swipeEndY    = lastY;
      g_swipePending = true;
      wasDown = false;
#if DEBUG_TOUCH
      /* Zum Gegenpruefen die Werte der Library daneben. Weichen sie stark ab,
       * stimmt meine Registerkarte nicht - dann sieht man es sofort. */
      int32_t libX = CST816->IIC_Read_Device_Value(CST816->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
      int32_t libY = CST816->IIC_Read_Device_Value(CST816->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
      USBSerial.printf("[TOUCH] #%lu  %lu ms  maxDev %3d  (%3d,%3d)->(%3d,%3d)   Lib zuletzt: (%d,%d)\n",
                       (unsigned long)g_touchPress, (unsigned long)(millis() - pressMs),
                       (int)g_touchMaxDev, (int)g_swipeStartX, (int)g_swipeStartY,
                       (int)lastX, (int)lastY, (int)libX, (int)libY);
#endif
    }
    data->state   = LV_INDEV_STATE_REL;
    data->point.x = lastX;
    data->point.y = lastY;
  }
}

static void i2c_scan(const char *tag) {
  USBSerial.printf("[I2C] Scan %s:", tag);
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) USBSerial.printf(" 0x%02X", a);
  }
  USBSerial.println();
}

/* ============================================================
 *  SD-Suchlauf
 *  ------------------------------------------------------------
 *  Die Karte meldet "send_op_cond Timeout" (0x107) = sie antwortet auf der
 *  untersten Protokollebene nicht. Verdacht: eine der unbekannten Expander-
 *  Leitungen (3, 4, 5, 7) schaltet die Stromversorgung oder den Reset des
 *  SD-Slots. Wir probieren sie systematisch durch, statt zu raten.
 * ============================================================ */
static bool sd_try(void) {
  ShBeastCard c;
  return shsd_probe(&c) != 0;   /* 0 = keine Karte; 1/2 = Karte antwortet */
}

static void sd_hunt(void) {
  const uint8_t cand[4] = { 3, 4, 5, 7 };

  USBSerial.println("=== SD-Suchlauf ===");
  USBSerial.printf("[SD-HUNT] Input-Register im Grundzustand: 0x%02X\n", exp_read_input());

  if (sd_try()) {
    USBSerial.println("[SD-HUNT] Karte laeuft bereits im Grundzustand. Nichts zu tun.");
    return;
  }

  /* Jeden Kandidaten einzeln als Ausgang HIGH ziehen. */
  for (int i = 0; i < 4; i++) {
    uint8_t p = cand[i];
    exp_make_output(p);
    exp_set(p, true);
    delay(120);
    USBSerial.printf("[SD-HUNT] Pin %u = HIGH ... ", (unsigned)p);
    if (sd_try()) {
      USBSerial.println("KARTE ERKANNT!");
      USBSerial.printf(">>> TREFFER: Expander-Pin %u schaltet die SD-Karte frei. <<<\n", (unsigned)p);
      return;
    }
    USBSerial.println("nichts");
    exp_make_input(p);          /* wieder hochohmig, sauberer Zustand */
    delay(50);
  }

  /* Alle vier gleichzeitig HIGH (falls es zwei Leitungen braucht). */
  for (int i = 0; i < 4; i++) { exp_make_output(cand[i]); exp_set(cand[i], true); }
  delay(150);
  USBSerial.print("[SD-HUNT] Pins 3,4,5,7 alle HIGH ... ");
  if (sd_try()) {
    USBSerial.println("KARTE ERKANNT!");
    USBSerial.println(">>> TREFFER: die Kombination schaltet die SD frei - jetzt einzeln eingrenzen. <<<");
    return;
  }
  USBSerial.println("nichts");
  for (int i = 0; i < 4; i++) exp_make_input(cand[i]);

  USBSerial.println("[SD-HUNT] Kein Expander-Pin schaltet die Karte frei.");
  USBSerial.println("[SD-HUNT] -> Ursache liegt woanders (Karte, Kontakt, oder SD-Pins stimmen nicht).");
}

/* ============================================================
 *  Display-Reset-Suchlauf (Schritt 4)
 *  ------------------------------------------------------------
 *  Das Panel kommt aus dem Deep Standby NUR ueber einen echten Hardware-Reset
 *  zurueck. Der laeuft hier ueber den Expander - aber welcher der vier Pins
 *  (0, 1, 2, 6) ist es? Wir probieren sie der Reihe nach durch und schreiben
 *  die getestete Nummer gross aufs Display. Bei welchem Pin das Bild
 *  zurueckkommt, das ist der Display-Reset.
 * ============================================================ */
#if RESET_PIN_HUNT
static const uint8_t g_rstCand[4] = { 0, 1, 2, 6 };
static int      g_rstIdx   = 0;
static uint32_t g_rstNext  = 0;
static int      g_rstPhase = 0;   /* 0 = Bild zeigen, 1 = Standby, 2 = Wecken */
static lv_obj_t *g_lblHunt = NULL;

static void hunt_label(const char *txt) {
  if (!g_lblHunt) {
    g_lblHunt = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_color(g_lblHunt, lv_color_hex(0x00FF80), LV_PART_MAIN);
  }
  lv_label_set_text(g_lblHunt, txt);
  lv_obj_align(g_lblHunt, LV_ALIGN_TOP_MID, 0, 44);
}

static void reset_hunt_tick(void) {
  if (millis() < g_rstNext) return;

  if (g_rstIdx >= 4) {                                  /* alle durch */
    if (g_rstPhase != 9) {
      g_rstPhase = 9;
      USBSerial.println("[RST-HUNT] Alle vier Pins durch. Falls NIE ein Bild kam:");
      USBSerial.println("[RST-HUNT] Der Reset haengt an einer anderen Leitung - melden!");
      hunt_label("FERTIG");
    }
    return;
  }

  uint8_t p = g_rstCand[g_rstIdx];

  switch (g_rstPhase) {
    case 0:                                             /* Bild steht, gleich Standby */
      USBSerial.printf("[RST-HUNT] --- Test mit Expander-Pin %u ---\n", (unsigned)p);
      USBSerial.println("[RST-HUNT] Panel geht in den Deep Standby (Bild wird schwarz) ...");
      shp_display_standby();
      g_rstPhase = 1;
      g_rstNext  = millis() + 2500;
      break;

    case 1:                                             /* jetzt aufwecken */
      USBSerial.printf("[RST-HUNT] Wecke ueber Pin %u ...\n", (unsigned)p);
      shp_display_wake(p);
      gfx->setRotation(0);                              /* MADCTL nach HW-Reset zurueck */
      {
        char t[32];
        snprintf(t, sizeof(t), "RESET-PIN %u", (unsigned)p);
        hunt_label(t);
      }
      lv_obj_invalidate(lv_screen_active());
      lv_refr_now(NULL);                                /* erst komplett zeichnen ... */
      shp_display_show();                               /* ... DANN Display an        */
      USBSerial.printf("[RST-HUNT] Siehst du jetzt \"RESET-PIN %u\"? Dann ist es dieser Pin.\n",
                       (unsigned)p);
      g_rstPhase = 0;
      g_rstIdx++;
      g_rstNext = millis() + 8000;                      /* 8 s Zeit zum Hinschauen */
      break;
  }
}
#endif /* RESET_PIN_HUNT */

/* Spielstand ausfuehrlich im Serial Monitor ausgeben. */
static void dump_state(void) {
  USBSerial.println("--- Spielstand ---");
  USBSerial.printf("  onboarded : %u\n", (unsigned)g_state.onboarded);
  USBSerial.printf("  Beast     : %s\n", game_has_beast() ? "von Karte" : "KEINE Karte");
  if (game_has_beast()) {
    USBSerial.printf("  Name      : %s (Spezies %u, Stufe %d)\n",
                     game_player_name(), (unsigned)g_state.beastId, game_player_stage());
    USBSerial.printf("  Level     : %u   XP %lu (%d/%d im Level)\n",
                     (unsigned)g_state.level, (unsigned long)g_state.xp,
                     game_xp_into_level(), game_xp_span());
    Stats st = game_player_stats();
    USBSerial.printf("  Stats     : ATK %d  DEF %d  SPD %d  HP %d\n", st.atk, st.def, st.spd, st.hp);
  }
  USBSerial.printf("  Streak    : %u (max %u)\n", (unsigned)g_state.streak, (unsigned)g_state.maxStreak);
  USBSerial.printf("  Tag-Index : %lu   (lastDay %lu)\n",
                   (unsigned long)shp_day_index(), (unsigned long)g_state.lastDay);
  USBSerial.printf("  Claim     : %s   Kaempfe frei: %d\n",
                   g_state.claimedToday ? "heute erledigt" : "offen", game_arena_left());
  USBSerial.println("------------------");
}

/* ============================================================
 *  CST816: Interrupt waehrend der GANZEN Beruehrung
 *  ------------------------------------------------------------
 *  Der Kern des Touch-Problems. Auf der Watch (FT3168) BLEIBT TP_INT LOW,
 *  solange der Finger liegt - dort genuegt ein GPIO-Read, um den Druck zu
 *  erkennen. Der CST816 dagegen kann so konfiguriert sein, dass er nur beim
 *  Aufsetzen und beim Abheben PULST. Dann zerfaellt ein einziger Tipp in zwei
 *  Press/Release-Zyklen: der erste oeffnet ein Overlay, der zweite schliesst
 *  es sofort wieder.
 *
 *  Register 0xFA (IrqCtl), CST816-Familie:
 *      Bit 7 EnTest · Bit 6 EnTouch · Bit 5 EnChange · Bit 4 EnMotion
 *  EnTouch = periodischer Impuls, SOLANGE beruehrt wird. Genau das wollen wir.
 *
 *  Wir lesen das Register vorher und nachher aus - dann steht im Log, ob die
 *  Registerkarte stimmt, statt dass wir es hoffen muessen.
 * ============================================================ */
static void cst816_irq_continuous(void) {
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(CST816_IRQCTL);
  if (Wire.endTransmission(false) != 0) { USBSerial.println("[TP] IrqCtl nicht lesbar."); return; }
  if (Wire.requestFrom((uint8_t)CST816_ADDR, (uint8_t)1) != 1) { USBSerial.println("[TP] IrqCtl kein Byte."); return; }
  uint8_t before = Wire.read();

  uint8_t want = (uint8_t)((before | 0x60) & ~0x10);   /* EnTouch+EnChange an, EnMotion aus */
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(CST816_IRQCTL);
  Wire.write(want);
  Wire.endTransmission();

  Wire.beginTransmission(CST816_ADDR);
  Wire.write(CST816_IRQCTL);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)CST816_ADDR, (uint8_t)1);
  uint8_t after = Wire.read();

  USBSerial.printf("[TP] IrqCtl 0x%02X -> 0x%02X (Soll 0x%02X)  %s\n",
                   before, after, want,
                   (after == want) ? "OK - Dauer-Interrupt aktiv"
                                   : "ABWEICHUNG - Register haelt den Wert nicht");
}

/* ============================================================
 *  setup()
 * ============================================================ */
#if PWR_PIN_HUNT
/* ============================================================
 *  BOOT-Tasten-Test
 *  ------------------------------------------------------------
 *  Ergebnis der beiden PWR-Suchlaeufe: Der IRQ-Ausgang des AXP2101 liegt auf
 *  KEINEM freien GPIO. GPIO 13 blieb auch nach dem Loeschen LOW (also fest auf
 *  Masse, kein Interrupt), und kein Pin reagierte auf den Tastendruck.
 *  Die PWR-Taste kann daher nicht aus dem Deep Sleep wecken - sie haengt nur
 *  am I2C-Bus, und der schlaeft mit.
 *
 *  ABER: GPIO 0 ist auf fast jedem ESP32-Board der BOOT-Taster, und GPIO 0 ist
 *  RTC-faehig - also weckfaehig. Im Suchlauf wurde nur PWR gedrueckt, nie BOOT.
 *  Genau das pruefen wir jetzt.
 *
 *  Druecke waehrend des Tests BEIDE Tasten nacheinander, damit wir sehen, ob
 *  der BOOT-Taster ueberhaupt existiert und auf GPIO 0 liegt.
 * ============================================================ */
static void pwr_pin_hunt(void) {
  const int cand[] = { 0, 17, 18 };
  const int N = sizeof(cand) / sizeof(cand[0]);
  int last[4];

  USBSerial.println();
  USBSerial.println("=== BOOT-Tasten-Test ===");
  USBSerial.println();
  USBSerial.println("Druecke 25 Sekunden lang abwechselnd:");
  USBSerial.println("   - den BOOT-Taster   (falls am Gehaeuse erreichbar)");
  USBSerial.println("   - die PWR-Taste     (zum Vergleich)");
  USBSerial.println();
  USBSerial.println("Gesucht: ein Pin, der beim Druecken auf LOW geht.");
  USBSerial.println();

  for (int i = 0; i < N; i++) {
    pinMode(cand[i], INPUT_PULLUP);
    delay(5);
    last[i] = digitalRead(cand[i]);
    USBSerial.printf("   GPIO %2d Ruhezustand: %s\n", cand[i], last[i] ? "HIGH" : "LOW");
  }
  USBSerial.println();

  uint32_t t0 = millis();
  int hits = 0;

  while (millis() - t0 < 25000) {
    for (int i = 0; i < N; i++) {
      int v = digitalRead(cand[i]);
      if (v != last[i]) {
        USBSerial.printf("   [%5lu ms] GPIO %2d -> %-4s %s\n",
                         (unsigned long)(millis() - t0), cand[i],
                         v ? "HIGH" : "LOW",
                         (!v) ? "<<< GEDRUECKT!" : "(losgelassen)");
        last[i] = v;
        if (!v) hits++;
      }
    }
    delay(5);
  }

  USBSerial.println();
  USBSerial.printf("=== Ende. %d Tastendruecke erkannt. ===\n", hits);
  if (hits > 0) {
    USBSerial.println("Der Pin, der LOW wurde, kann uns aus dem Deep Sleep wecken.");
  } else {
    USBSerial.println("Kein Taster auf einem freien GPIO. Dann bleibt nur Tap-to-wake");
    USBSerial.println("oder der RESET-Taster - der startet neu, was praktisch dasselbe");
    USBSerial.println("ist, weil Aufwachen aus dem Deep Sleep ohnehin ein Neustart ist.");
  }
}
#endif

void setup() {
  uint32_t bootT0 = millis();
  bool fromDeepSleep = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);
  USBSerial.begin(115200);
  USBSerial.setTxTimeoutMs(0);

  /* Diese zwei Sekunden gibt es NUR beim Kaltstart - sie sind die Wartezeit,
   * bis sich der Serial Monitor verbunden hat. Beim Aufwachen aus dem Deep
   * Sleep waeren sie reine Verschwendung: dort zaehlt jede Millisekunde, bis
   * das Bild wieder da ist. Sie machten allein 2 von 2,9 Sekunden Boot aus. */
  if (!fromDeepSleep) delay(2000);

  USBSerial.println();
  USBSerial.println("=== Streakhunter Go ===");

  WiFi.mode(WIFI_OFF);
  btStop();
  setCpuFrequencyMhz(240);

  /* BOOT-Taster: unsere einzige Weckquelle. Nach einem Deep Sleep steht der
   * Pin noch unter RTC-Kontrolle - erst deinit() gibt ihn dem normalen GPIO
   * zurueck, sonst liefert digitalRead() Unsinn. */
  if (fromDeepSleep) {
    rtc_gpio_deinit((gpio_num_t)WAKE_BTN);
    /* Rettung: ALLE Leitungen freigeben, die eine fruehere Firmware vor dem
     * Schlaf isoliert haben koennte. Das laeuft bewusst immer und unabhaengig
     * vom Schalter - ein Geraet, das noch isoliert eingeschlafen ist, wuerde
     * sonst mit schwarzem Display zurueckkommen, weil ohne I2C weder der
     * Expander noch die Panel-Rails erreichbar sind. */
    static const int WAKE_PINS[] = {
      LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3, LCD_SCLK, LCD_CS,
      IIC_SDA, IIC_SCL, TP_INT,
      I2S_MCK_IO, I2S_BCK_IO, I2S_DI_IO, I2S_DO_IO, I2S_WS_IO,
      SDMMC_CLK, SDMMC_CMD, SDMMC_DATA
    };
    for (unsigned i = 0; i < sizeof(WAKE_PINS) / sizeof(WAKE_PINS[0]); i++) {
      gpio_num_t g = (gpio_num_t)WAKE_PINS[i];
      if (rtc_gpio_is_valid_gpio(g)) {
        rtc_gpio_hold_dis(g);      /* falls der Pad-Zustand gehalten wurde */
        rtc_gpio_deinit(g);        /* zurueck unter normale GPIO-Kontrolle */
      }
      gpio_reset_pin(g);
    }
  }
  pinMode(WAKE_BTN, INPUT_PULLUP);

  /* --- Expander zuerst: er traegt die Reset-Leitungen --- */
  Wire.begin(IIC_SDA, IIC_SCL);
  if (!fromDeepSleep) i2c_scan("Start");   /* Diagnose, beim Wecken unnoetig */
  if (exp_begin()) exp_reset_pulse_all();

  /* Panel-Stromversorgung hochfahren, BEVOR das Display initialisiert wird.
   * Nach dem Deep Sleep sind die ALDO-Rails abgeschaltet - ohne das hier
   * bekaeme gfx->begin() ein stromloses Panel. */
  shp_pmu_panel_power();

  /* --- Display --- */
  if (!gfx->begin(40000000)) USBSerial.println("[GFX] begin() FEHLGESCHLAGEN!");
  gfx->fillScreen(RGB565_BLACK);

  /* --- Touch (nicht blockierend) --- */
  for (int i = 0; i < 5 && !g_touchOk; i++) {
    if (CST816->begin()) g_touchOk = true;
    else delay(300);
  }
  if (g_touchOk) {
    /* WICHTIG fuers Aufwecken: Der Touch-Controller darf NICHT von sich aus
     * einschlafen, sonst liefert er im Panel-Standby keinen Interrupt mehr und
     * TP_INT bleibt HIGH -> das Geraet laesst sich nicht mehr wecken.
     *
     * DAS GILT NICHT MEHR: Geweckt wird jetzt ueber den BOOT-Taster (GPIO 0).
     * Der Touch-Controller darf also von sich aus einschlafen - genau das
     * spart im Standby Strom. Er wacht bei Beruehrung selbst wieder auf,
     * solange das Panel an ist; das reicht voellig fuer die Bedienung. */
    cst816_irq_continuous();
    USBSerial.println("[TP] CST816 OK (darf im Standby schlafen)");
  } else {
    USBSerial.println("[TP] CST816 fehlt - weiter ohne Touch.");
  }

  /* --- LVGL --- */
  lv_init();
  lv_tick_set_cb(millis_cb);
  screenWidth  = gfx->width();
  screenHeight = gfx->height();
  /* Der Puffer muss den bewegten Bereich in EINEM Stueck fassen.
   *
   * Vorher: 80 Zeilen. Das Beast ist aber 260 px hoch - bewegt es sich, muss
   * LVGL alte und neue Position neu zeichnen, und dieser Bereich passte nicht
   * in den Puffer. Also zerlegte LVGL ihn in vier Streifen mit vier einzelnen
   * QSPI-Transfers. Bei 60 fps waren das 240 Transfers je Sekunde - daher das
   * Stocken und Reissen.
   *
   * 160 Zeilen fassen den Sprite-Bereich in ein bis zwei Stuecken. Kostet
   * 368 x 160 x 2 = 118 KB, die aus dem internen RAM kommen sollen (PSRAM waere
   * zu langsam - der Fallback unten greift nur im Notfall). */
  bufSize      = screenWidth * 160;

  disp_draw_buf = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  bool bufInternal = (disp_draw_buf != NULL);

  if (!disp_draw_buf) disp_draw_buf = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_8BIT);
  if (!disp_draw_buf) { USBSerial.println("[LVGL] Draw-Buffer fehlgeschlagen!"); return; }

  /* Wo der Puffer liegt, entscheidet ueber die Fluessigkeit: internes RAM ist
   * um ein Vielfaches schneller als PSRAM. Landet er im PSRAM, ruckelt es -
   * dann muss bufSize wieder kleiner werden. */
  USBSerial.printf("[LVGL] Draw-Buffer: %u KB im %s\n",
                   (unsigned)(bufSize * 2 / 1024),
                   bufInternal ? "internen RAM (schnell)" : "PSRAM (LANGSAM!)");

  disp = lv_display_create(screenWidth, screenHeight);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, disp_draw_buf, NULL, bufSize * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);

  /* 60 statt 30 Bilder je Sekunde.
   *
   * LVGL zeichnet standardmaessig nur alle 33 ms neu - bei einer 200-ms-Animation
   * sind das gerade sechs Bilder, und genau so sieht es dann auch aus. Mit 16 ms
   * wird die Huepf-Bewegung fluessig.
   *
   * lv_display_set_refresh_period() gibt es in dieser LVGL-Fassung noch nicht.
   * Hinter dem Neuzeichnen steckt aber ein ganz normaler lv_timer - den holen
   * wir uns und stellen ihn direkt um. Das ist dieselbe Sache, nur eine Ebene
   * tiefer, und funktioniert in jeder 9er-Version.
   *
   * Der Aufwand ist kleiner, als er klingt: LVGL zeichnet nur die Bereiche neu,
   * die sich geaendert haben (RENDER_MODE_PARTIAL) - beim Huepfen also nur das
   * Sprite-Rechteck. Und das Panel ist ohnehin nur 30 Sekunden an. */
  lv_timer_t *refr = lv_display_get_refr_timer(disp);
  if (refr) {
    lv_timer_set_period(refr, 16);
    USBSerial.println("[LVGL] Bildrate auf 60 fps (16 ms).");
  } else {
    USBSerial.println("[LVGL] Refresh-Timer nicht erreichbar - bleibt bei 30 fps.");
  }
  lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read);

  /* ===================== STREAKHUNTER ===================== */
  shp_init();                       /* NVS + RTC */

  /* SDMMC-Fehlerlog daempfen: der Hot-Swap-Poll erzeugt sonst im Sekundentakt
   * rote Zeilen, sobald keine Karte steckt. Unsere eigenen Meldungen bleiben. */
  esp_log_level_set("sdmmc_common", ESP_LOG_NONE);
  esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_NONE);

#if SD_PIN_HUNT
  sd_hunt();
#endif

  /* SD-Karte: nachsehen, was drauf ist (schreibt NICHT). */
  {
    ShBeastCard c;
    int r = shsd_probe(&c);
    if (r == 0)      USBSerial.println("[SD] keine Karte erkannt");
    else if (r == 2) USBSerial.println("[SD] Karte da, aber kein gueltiges /beast.shb");
    else             USBSerial.printf("[SD] Beast auf der Karte: \"%s\"  Spezies=%u  Lv=%u  Stufe=%u  XP=%lu\n",
                                      c.name, (unsigned)c.species, (unsigned)c.level,
                                      (unsigned)c.stage, (unsigned long)c.xp);
  }

  game_begin();
  /* Ohne Onboarding-Screen (kommt erst mit der UI) das Spiel scharf schalten,
   * sonst laedt game_begin() die Karte gar nicht erst. */
  if (!g_state.onboarded) {
    USBSerial.println("[GAME] frischer Stand -> Onboarding automatisch abgeschlossen.");
    game_finish_onboarding();
  }
  game_poll_card();                 /* Karte jetzt aktiv schalten, falls vorhanden */
  vit_begin();                      /* Vitalwerte laden (nach der Karte!) */
  inv_begin();                      /* Rucksack laden                     */
  adv_begin();                      /* laeuft noch ein Abenteuer?          */
  equip_begin();                    /* liegt noch Ausruestung an?          */
  loadout_begin();                  /* die zwei Kampf-Attacken laden       */
  /* ======================================================== */

  ui_init();
  dump_state();

  g_lastActivityMs = millis();
  g_swipePending   = false;   /* der Weck-Tipp ist keine Geste und kein Klick */
  g_touchMaxDev    = 9999;

  USBSerial.printf("[SETUP] fertig in %lu ms%s.\n",
                   (unsigned long)(millis() - bootT0),
                   fromDeepSleep ? "  <<< aus Deep Sleep geweckt (BOOT-Taster)" : "");
  USBSerial.printf("[SETUP] Panel aus nach %d s, dann %d s Standby -> Deep Sleep. Wecken: BOOT-Taster.\n",
                   SCREEN_TIMEOUT_MS / 1000, STANDBY_BEFORE_SLEEP_MS / 1000);

  /* Bilanz des letzten Schlafs. Steht auch in den Einstellungen auf dem
   * Display - dort ist sie aussagekraeftig, weil kein USB-Kabel dranhaengt. */
  {
    if (!fromDeepSleep) shp_sleep_reset();     /* Kaltstart: neues Messfenster */
    int mins = 0, drop = 0, cyc = 0; bool chg = false;
    if (fromDeepSleep && shp_sleep_report(&mins, &drop, &chg, &cyc)) {
      USBSerial.printf("[AKKU] Schlaf %d:%02d h, Verlust %d mV, %d Zyklen%s\n",
                       mins / 60, mins % 60, drop, cyc,
                       chg ? "  (am Kabel - Wert unbrauchbar)" : "");
    }
  }

#if PWR_PIN_HUNT
  pwr_pin_hunt();
#endif

#if RESET_PIN_HUNT
  USBSerial.println();
  USBSerial.println("=== Display-Reset-Suchlauf startet in 6 Sekunden ===");
  USBSerial.println("Das Panel geht gleich aus und wird ueber Pin 0, 1, 2 und 6 zu wecken versucht.");
  USBSerial.println("Merk dir, bei WELCHER Pin-Nummer das Bild zurueckkommt.");
  g_rstNext = millis() + 6000;
#endif
}

/* ============================================================
 *  Bildschirm aus / an
 *  ------------------------------------------------------------
 *  Aus: Panel-Deep-Standby (Register 0x4F) - der grosse Akku-Hebel.
 *  An : Hardware-Reset ueber Expander-Pin 0, Panel-Init, kompletter Redraw,
 *       und ERST DANN Display-ON (sonst blitzt undefinierter Panel-RAM auf).
 *  Der Touch-Controller laeuft im Standby weiter -> ein Fingertipp weckt.
 * ============================================================ */
static uint32_t g_sleepStartMs = 0;   /* fuer die Sperrzeit des Weck-Tasters */

static void screen_sleep(void) {
  if (shp_screen_is_off()) return;
  g_sleepStartMs = millis();
  g_swipePending = false;   /* keine halbe Geste ins Aufwachen mitnehmen */
  USBSerial.println("[STANDBY] Panel -> Deep Standby.");
  shp_display_standby();
}

static void screen_wake(void) {
  if (!shp_screen_is_off()) return;
  uint32_t t0 = millis();

  shp_display_wake(TAMA_LCD_RST_EXP_PIN);
  gfx->setRotation(0);                    /* MADCTL nach dem HW-Reset zurueck */

  ui_tick();
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(NULL);                      /* erst komplett zeichnen ...  */
  shp_display_show();                     /* ... DANN das Display an     */

  g_lastActivityMs = millis();
  g_swipePending   = false;   /* der Weck-Tipp ist keine Wischgeste */
  USBSerial.printf("[WAKE] Panel zurueck in %lu ms | Touch antwortet: %s\n",
                   (unsigned long)(millis() - t0), touch_alive() ? "ja" : "NEIN!");
}

/* ============================================================
 *  Deep Sleep
 *  ------------------------------------------------------------
 *  Geweckt wird ueber TP_INT (GPIO 21) - beim ESP32-S3 ein RTC-faehiger Pin,
 *  deshalb funktioniert ext0. Der Pegel ist per Pull-up HIGH und faellt bei
 *  Beruehrung auf LOW; wir wecken auf LOW.
 *
 *  Der CST816 bleibt versorgt (wir schalten keine Rails ab) und steht im
 *  MONITOR-Modus - er meldet sich also auch im Schlaf.
 * ============================================================ */
static void enter_deep_sleep(void) {
  USBSerial.println("[DEEP] Fuenf Minuten Ruhe - gehe in Deep Sleep.");
  USBSerial.println("[DEEP] Aufwecken mit dem BOOT-Taster.");

  /* Offene Aenderungen sichern. Streng genommen unnoetig (die Zeitstempel
   * werden beim Boot ohnehin nachgerechnet), aber billig und sauber. */
  vit_flush();

  /* Beast-Karte sauber aushaengen. Bleibt SD_MMC aktiv, haelt das die Karte
   * wach - und eine SD-Karte zieht im Leerlauf leicht ein paar Milliampere,
   * also genau die Groessenordnung, die uns im Schlaf noch fehlt. Beim
   * Aufwachen (= Neustart) meldet sie sich bei Bedarf von selbst wieder an. */
  shsd_end();

  /* Spannung und Uhrzeit merken - daraus rechnet der naechste Boot den echten
   * Schlafverbrauch aus. */
  shp_sleep_mark();
  USBSerial.printf("[AKKU] vor dem Schlaf: %d mV\n", shp_battery_mv());

  if (!shp_screen_is_off()) shp_display_standby();

  /* Reset-Leitungen des Expanders auf LOW, BEVOR den Bausteinen der Strom
   * genommen wird. Sonst speisen die dauerhaft hohen Pegel ueber die
   * Schutzdioden Strom in die abgeschalteten Rails. */
  exp_sleep_low();

  /* Unkritische Rails abschalten (Audio, Leerlauf-Rail). Das ist der Hebel
   * gegen die naechtlichen 18 % - der Deep Sleep allein laesst sie an. */
  shp_pmu_sleep_rails();

  /* --- Leckstroeme ueber die Signalleitungen abstellen ---
   * Die Panel-, Touch-, Audio- und SD-Rails sind jetzt spannungslos. Bleiben
   * die zugehoerigen ESP32-Pins aber angesteuert oder mit Pull-Widerstand
   * versehen, fliesst Strom ueber die Schutzdioden der stromlosen Bausteine
   * zurueck - im Milliampere-Bereich, und genau das frisst weiter Akku.
   * Deshalb werden alle diese Pins hochohmig gelegt und, wo moeglich, in der
   * RTC-Domain isoliert. Der Weck-Taster bleibt ausgenommen. */
#if DEEP_SLEEP_ISOLATE_PINS
  {
    /* Bewusst NUR Display und Audio. Der I2C-Bus bleibt unangetastet: daran
     * haengen PMU und RTC, die weiter versorgt werden - dort gibt es also gar
     * keinen Leckpfad, wohl aber das Risiko, dass der PMU nach dem Aufwachen
     * nicht mehr erreichbar ist und die Panel-Rails aus bleiben. Dasselbe gilt
     * fuer die Leitungen zur Beast-Karte. */
    static const int SLEEP_PINS[] = {
      LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3, LCD_SCLK, LCD_CS,  /* Display QSPI */
      I2S_MCK_IO, I2S_BCK_IO, I2S_DI_IO, I2S_DO_IO, I2S_WS_IO, PA    /* Audio        */
    };
    for (unsigned i = 0; i < sizeof(SLEEP_PINS) / sizeof(SLEEP_PINS[0]); i++) {
      int pin = SLEEP_PINS[i];
      if (pin == WAKE_BTN) continue;
      gpio_reset_pin((gpio_num_t)pin);                 /* hochohmig, ohne Pull */
      if (rtc_gpio_is_valid_gpio((gpio_num_t)pin))
        rtc_gpio_isolate((gpio_num_t)pin);             /* im Schlaf abgekoppelt */
    }
    USBSerial.println("[DEEP] Signalleitungen isoliert.");
  }
#endif

  /* Pull-up in der RTC-Domain halten, sonst floatet der Pin im Schlaf und
   * das Geraet wacht sofort wieder auf. */
  rtc_gpio_pullup_en((gpio_num_t)WAKE_BTN);
  rtc_gpio_pulldown_dis((gpio_num_t)WAKE_BTN);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_BTN, 0);   /* 0 = LOW weckt */

  USBSerial.flush();
  delay(20);
  esp_deep_sleep_start();
  /* ab hier kommt nichts mehr - der naechste Schritt ist setup() */
}

/* ============================================================
 *  loop()
 * ============================================================ */
void loop() {
  static uint32_t lastSec  = 0;
  static uint32_t lastCard = 0;

  bool off = shp_screen_is_off();

  /* PWR-Taste: kurzer Druck schaltet den Bildschirm an oder aus. */
  if (shp_power_button_pressed()) {
    if (off) { screen_wake();  off = false; }
    else     { screen_sleep(); off = true;  }
    g_lastActivityMs = millis();
  }

  if (off) {
    /* Panel schlaeft. Geweckt wird ueber den BOOT-Taster (GPIO 0) - NICHT mehr
     * ueber den Touch. Damit darf der CST816 von sich aus einschlafen, was im
     * Standby 1-2 mA spart. Ein Fingertipp auf das schwarze Display tut jetzt
     * bewusst nichts.
     *
     * WICHTIG - hier lag ein Fehler: Geprueft wird die FLANKE, nicht der Pegel.
     * GPIO 0 ist der BOOT-Pin und haengt am USB-Kabel an der Auto-Reset-Leitung;
     * er kann dort laenger LOW liegen. Wer nur den Pegel abfragt, weckt das
     * Geraet dann sofort wieder auf, sobald es eingeschlafen ist. Dazu eine
     * kurze Sperrzeit, damit derselbe Druck nicht aus- und gleich wieder
     * einschaltet. */
    static int  lastBtn   = HIGH;
    static bool btnArmed  = false;

    int btn = digitalRead(WAKE_BTN);

    /* Erst scharf, wenn der Taster einmal HIGH war und die Sperrzeit um ist. */
    if (!btnArmed) {
      if (btn == HIGH && millis() - g_sleepStartMs > 400) btnArmed = true;
      lastBtn = btn;
    } else if (lastBtn == HIGH && btn == LOW) {          /* echte Flanke */
      uint32_t t = millis();
      while (digitalRead(WAKE_BTN) == LOW && millis() - t < 600) delay(5);   /* loslassen abwarten */
      btnArmed = false;
      lastBtn  = HIGH;
      screen_wake();
      off = false;
    } else {
      lastBtn = btn;
    }

    if (off) {
      static uint32_t lastDiag = 0;
      if (millis() - lastDiag >= 5000) {
        lastDiag = millis();
        uint32_t inStandby = (millis() - g_sleepStartMs) / 1000;
        USBSerial.printf("[STANDBY] %lu s im Standby. Deep Sleep in %ld s. BOOT weckt.%s\n",
                         (unsigned long)inStandby,
                         (long)(STANDBY_BEFORE_SLEEP_MS / 1000 - inStandby),
                         btnArmed ? "" : "  (Taster noch gesperrt)");
      }

      /* Lange genug im Standby -> Deep Sleep. Bewusst an g_sleepStartMs
       * (Standby-Beginn) gekoppelt, NICHT an g_lastActivityMs: So kann kein
       * versehentlicher Aktivitaets-Reset im Standby den Uebergang blockieren.
       * Genau das liess das Geraet frueher im teuren Standby haengen. */
      if (millis() - g_sleepStartMs > STANDBY_BEFORE_SLEEP_MS) enter_deep_sleep();
    }
    } else {
    lv_task_handler();
    ui_handle_swipe();               /* Wischgesten auswerten */
    if (millis() - g_lastActivityMs > SCREEN_TIMEOUT_MS) {
      screen_sleep();
      off = true;
    }
  }

  /* 1x/Sek: Tageswechsel pruefen (laeuft auch bei ausgeschaltetem Panel weiter) */
  if (millis() - lastSec >= 1000) {
    lastSec = millis();
    game_tick();
    vit_tick();          /* laeuft AUCH bei ausgeschaltetem Panel weiter */
    adv_tick();          /* prueft die Rueckkehr vom Abenteuer            */
    if (!off) ui_tick();
  }

  /* alle 2 s: Karte pruefen (Hot-Swap). */
  if (millis() - lastCard >= 2000) {
    lastCard = millis();
    int ev = game_poll_card();
    if (ev == 1) { USBSerial.println("[SD] Karte eingesteckt."); dump_state(); if (!off) ui_card_changed(); }
    if (ev == 2) { USBSerial.println("[SD] Karte entfernt.");    dump_state(); if (!off) ui_card_changed(); }
  }

#if TAMA_LIGHT_SLEEP
  if (off) {
    esp_sleep_enable_timer_wakeup(50000ULL);   /* 50 ms */
    esp_light_sleep_start();
  } else {
    delay(5);
  }
#else
  delay(5);
#endif
}
