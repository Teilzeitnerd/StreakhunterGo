/* ============================================================
 *  ui (TAMA) — LVGL-Oberflaeche (LVGL v9)
 *  ------------------------------------------------------------
 *  Fuenf Seiten, per Wischen durchblaettert:
 *
 *      PROFIL  <-  HOME  ->  QUEST  ->  ABENTEUER  ->  ARENA
 *
 *  Der Home-Screen steht. Die vier anderen sind Geruest.
 * ============================================================ */
#ifndef UI_H
#define UI_H

void ui_init(void);          /* einmal nach der LVGL-Initialisierung   */
void ui_tick(void);          /* ca. 1x/Sek: Werte auffrischen          */
void ui_card_changed(void);  /* Karte rein/raus -> Anzeige neu         */
void ui_handle_swipe(void);  /* haeufig pollen: wertet Wischgesten aus */

#endif /* UI_H */
