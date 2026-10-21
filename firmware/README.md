# Firmware

**StreakhunterGo.bin** ist das fertige, flashbare Gesamt-Abbild (Bootloader +
Partitionen + App in einem). 

- **Web-Flasher (ESP Web Tools):** an Offset **0x0** flashen.
- **esptool (Kommandozeile):**
  ```
  esptool --chip esp32s3 write_flash 0x0 StreakhunterGo.bin
  ```

Board: Waveshare ESP32-S3-Touch-AMOLED-1.8 (16 MB Flash).
