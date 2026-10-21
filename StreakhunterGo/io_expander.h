/* ============================================================
 *  io_expander — XCA9554 I/O-Expander @ 0x20 (I2C)
 *  ------------------------------------------------------------
 *  Das 1.8"-Board hat KEINEN Display-Reset-GPIO. Die Reset-
 *  Leitungen haengen am Expander. Waveshares Demo pulst beim
 *  Start die Pins 0, 1, 2 und 6.
 *
 *  WICHTIG (hart gelernt): Nur die Pins anfassen, deren Funktion
 *  bekannt ist! Ein frueherer Stand hat ALLE acht Leitungen zu
 *  Ausgaengen gemacht und auf LOW gezogen -> die SD-Karte liess
 *  sich nicht mehr initialisieren (sdmmc send_op_cond Timeout).
 *  Jetzt bleiben 3, 4, 5 und 7 Eingaenge (hochohmig, Werkszustand).
 *
 *  Registerkarte (TCA9554-kompatibel, per Readback bestaetigt):
 *      0x00 = Input Port
 *      0x01 = Output Port
 *      0x02 = Polarity Inversion
 *      0x03 = Configuration (Bit=1 -> Eingang, Bit=0 -> Ausgang)
 * ============================================================ */
#ifndef IO_EXPANDER_H
#define IO_EXPANDER_H

#include <stdint.h>

#define EXP_ADDR 0x20

/* Diese vier Leitungen pulst Waveshares Demo beim Start. */
#define EXP_RESET_MASK  ((1 << 0) | (1 << 1) | (1 << 2) | (1 << 6))

/* Einmalig nach Wire.begin() aufrufen. Liest den Ist-Zustand aus (Diagnose),
 * macht NUR die Reset-Leitungen zu Ausgaengen und verifiziert per Readback. */
bool exp_begin(void);

bool exp_present(void);

/* Reset-Puls auf 0,1,2,6: LOW -> 20 ms -> HIGH (wie die Demo). */
void exp_reset_pulse_all(void);
void exp_sleep_low(void);            /* Reset-Leitungen vor dem Deep Sleep auf LOW */

/* Reset-Puls auf EINEM Pin (Schritt 4: welcher ist der Display-Reset?)
 * Timing wie in der Panel-Wake-Sequenz: HIGH 10 ms, LOW 10 ms, HIGH 120 ms. */
void exp_reset_pulse(uint8_t pin);

/* --- Einzelne Leitungen (fuer Diagnose / spaetere Funde) --- */
void exp_make_output(uint8_t pin);   /* Pin zum Ausgang machen              */
void exp_make_input(uint8_t pin);    /* Pin wieder hochohmig (Werkszustand) */
void exp_set(uint8_t pin, bool high);/* Ausgangspegel setzen                */

/* Rohregister lesen (-1 = Fehler). Fuer die Diagnose-Ausgabe. */
int  exp_read_input(void);
int  exp_read_config(void);
int  exp_read_output(void);

#endif /* IO_EXPANDER_H */
