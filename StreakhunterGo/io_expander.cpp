/* ============================================================
 *  io_expander.cpp — XCA9554 @ 0x20, direkter Registerzugriff
 * ============================================================ */
#include <Arduino.h>
#include <Wire.h>
#include "io_expander.h"

#include "HWCDC.h"
extern HWCDC USBSerial;

#define REG_INPUT   0x00
#define REG_OUTPUT  0x01
#define REG_CONFIG  0x03

static bool    g_ok      = false;
static uint8_t g_outputs = 0x00;   /* Schattenkopie Output-Register */
static uint8_t g_config  = 0xFF;   /* Schattenkopie Config (1 = Eingang) */

/* --- I2C-Helfer --- */
static bool wr(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(EXP_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0);
}

static int rd(uint8_t reg) {
    Wire.beginTransmission(EXP_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom((uint8_t)EXP_ADDR, (uint8_t)1) != 1) return -1;
    return Wire.read();
}

int exp_read_input(void)  { return rd(REG_INPUT);  }
int exp_read_config(void) { return rd(REG_CONFIG); }
int exp_read_output(void) { return rd(REG_OUTPUT); }

bool exp_present(void) {
    Wire.beginTransmission(EXP_ADDR);
    return (Wire.endTransmission() == 0);
}

bool exp_begin(void) {
    g_ok = false;

    if (!exp_present()) {
        USBSerial.println("[EXP] FEHLER: kein ACK auf 0x20.");
        return false;
    }

    /* --- Ist-Zustand ZUERST lesen, bevor wir irgendetwas veraendern.
     *     Das verraet die Ruhepegel der unbekannten Leitungen. --- */
    int cfg0 = rd(REG_CONFIG);
    int in0  = rd(REG_INPUT);
    int out0 = rd(REG_OUTPUT);
    USBSerial.printf("[EXP] Ist-Zustand  Config=0x%02X  Input=0x%02X  Output=0x%02X\n",
                     cfg0, in0, out0);

    /* --- Nur die Reset-Leitungen (0,1,2,6) anfassen. 3,4,5,7 bleiben
     *     Eingaenge/hochohmig -> wir wissen nicht, was daran haengt. --- */
    g_outputs = (uint8_t)(out0 < 0 ? 0 : out0);
    g_outputs |= EXP_RESET_MASK;                  /* Ruhepegel HIGH */
    if (!wr(REG_OUTPUT, g_outputs)) {
        USBSerial.println("[EXP] FEHLER: Output-Register (0x01) nicht beschreibbar.");
        return false;
    }
    g_config = (uint8_t)~EXP_RESET_MASK;          /* 0xB8: nur 0,1,2,6 sind Ausgaenge */
    if (!wr(REG_CONFIG, g_config)) {
        USBSerial.println("[EXP] FEHLER: Config-Register (0x03) nicht beschreibbar.");
        return false;
    }

    int cfg = rd(REG_CONFIG);
    USBSerial.printf("[EXP] Readback  Config=0x%02X (Soll 0x%02X)\n", cfg, g_config);
    if (cfg != (int)g_config) {
        USBSerial.println("[EXP] WARNUNG: Config-Readback passt nicht.");
        return false;
    }

    g_ok = true;
    USBSerial.println("[EXP] OK - Ausgaenge: 0,1,2,6. Pins 3,4,5,7 bleiben Eingaenge.");
    return true;
}

void exp_make_output(uint8_t pin) {
    if (!g_ok || pin > 7) return;
    g_config &= (uint8_t)~(1 << pin);
    wr(REG_CONFIG, g_config);
}

void exp_make_input(uint8_t pin) {
    if (!g_ok || pin > 7) return;
    g_config |= (uint8_t)(1 << pin);
    wr(REG_CONFIG, g_config);
}

void exp_set(uint8_t pin, bool high) {
    if (!g_ok || pin > 7) return;
    if (high) g_outputs |=  (uint8_t)(1 << pin);
    else      g_outputs &= (uint8_t)~(1 << pin);
    wr(REG_OUTPUT, g_outputs);
}

void exp_reset_pulse_all(void) {
    if (!g_ok) return;
    g_outputs &= (uint8_t)~EXP_RESET_MASK;
    wr(REG_OUTPUT, g_outputs);
    delay(20);
    g_outputs |= (uint8_t)EXP_RESET_MASK;
    wr(REG_OUTPUT, g_outputs);
    delay(50);
    USBSerial.println("[EXP] Reset-Puls auf 0,1,2,6 gesendet.");
}

/* Vor dem Deep Sleep: die Reset-Leitungen auf LOW ziehen.
 * Nach dem Start stehen sie dauerhaft HIGH. Wenn dann die Versorgung der
 * angeschlossenen Bausteine abgeschaltet wird (Panel-Rails im Schlaf), speist
 * dieses HIGH ueber deren Schutzdioden Strom in die tote Rail - ein Leckpfad
 * im Milliampere-Bereich. LOW bedeutet schlicht "in Reset gehalten": die
 * Bausteine sind ohnehin stromlos, und beim Aufwachen setzt
 * exp_reset_pulse_all() sie sauber neu auf. */
void exp_sleep_low(void) {
    if (!g_ok) return;
    g_outputs &= (uint8_t)~EXP_RESET_MASK;
    wr(REG_OUTPUT, g_outputs);
}

void exp_reset_pulse(uint8_t pin) {
    if (!g_ok || pin > 7) return;
    exp_set(pin, true);  delay(10);
    exp_set(pin, false); delay(10);
    exp_set(pin, true);  delay(120);
}
