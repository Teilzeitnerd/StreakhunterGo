# Streakhunter Go

Ein eigenständiges Tamagotchi im Streakhunter-Universum, als Firmware für das
**Waveshare ESP32-S3-Touch-AMOLED-1.8**. Pflege dein Beast, schick es auf
Abenteuer, kämpfe in der Arena und lass es sich entwickeln – alles auf einem
kleinen AMOLED-Handheld, ganz ohne Löten.

Zum Board: [https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm)

---

## Hardware

Du brauchst nur **ein** fertiges Board, kein Zubehör, kein Löten:

- **Waveshare ESP32-S3-Touch-AMOLED-1.8** – ESP32-S3, 16 MB Flash, 368×448
  AMOLED, kapazitiver Touch, Akku-Management (AXP2101), Echtzeituhr (PCF85063),
  SD-Slot, 400 mAh Akku und Gehäuse sind dabei.
- Ein **USB-C-Kabel** zum Flashen.
- Optional eine **microSD-Karte** für das Beast (die „Dim-Card").

---

## Du willst nur spielen? (einfachster Weg)

Du musst **nichts** installieren. Flashe die fertige Firmware direkt aus dem
Browser:

1. Öffne den **Web-Flasher** (Link folgt, sobald die Release-Seite steht) in
   **Chrome** oder **Edge**.
2. Board per USB-C anstecken.
3. Auf **„Flashen"** klicken, im Fenster das Gerät auswählen, fertig.

Der Web-Flasher nutzt Web Serial – kein Treiber, keine IDE, keine Arduino-Kenntnisse.

---

## Du willst selbst bauen? (Maker-Weg)

### 1. Arduino IDE einrichten

- **Arduino IDE 2.x** installieren.
- Unter *Boardverwalter* das Paket **„esp32" von Espressif** installieren
  (Version 3.x).

### 2. Bibliotheken installieren

Genau diese fünf, mehr nicht. Alle über den Bibliotheksverwalter der IDE,
außer wo anders vermerkt:

| Bibliothek | Wofür | Quelle |
|---|---|---|
| **LVGL** `9.3.0` | gesamte Oberfläche | Bibliotheksverwalter (exakt v9.3.x) |
| **GFX Library for Arduino** | AMOLED-Treiber (CO5300, QSPI) | Bibliotheksverwalter |
| **Arduino_DriveBus** | Touch-Controller (CST816) | Bibliotheksverwalter |
| **XPowersLib** | Akku-Management (AXP2101) | Bibliotheksverwalter |
| **SensorLib** | Echtzeituhr (PCF85063) | Bibliotheksverwalter |

> Die ESP32-eigenen Teile (SD_MMC, Wire, Deep Sleep …) kommen mit dem
> Board-Paket und müssen **nicht** extra installiert werden.

### 3. Die LVGL-Konfiguration einsetzen (wichtig!)

LVGL braucht eine Konfigurationsdatei namens `lv_conf.h`. Die Standard-Datei
passt **nicht** – nutze die mitgelieferte aus [`config/lv_conf.h`](config/lv_conf.h).

Lege sie **neben** den `lvgl`-Ordner in deinem Arduino-Bibliotheksordner:

```
Arduino/libraries/
├── lv_conf.h   ←  hierhin kopieren
├── lvgl/
├── GFX_Library_for_Arduino/
└── ...
```

Ohne diese Datei (oder mit einer falschen) kompiliert das Projekt nicht sauber.

### 4. IDE-Einstellungen (unter *Werkzeuge*)

Diese Einstellungen müssen stimmen, sonst startet das Board nicht:

| Einstellung | Wert |
|---|---|
| Board | **ESP32S3 Dev Module** |
| USB CDC On Boot | **Enabled** |
| Flash Size | **16MB (128Mb)** |
| PSRAM | **OPI PSRAM** |
| Partition Scheme | (siehe Hinweis unten) |
| Upload Speed | 921600 |

> **Partition Scheme:** Im Sketch-Ordner liegt `partitions.csv`. Die
> arduino-esp32-Umgebung verwendet diese Datei **automatisch**. Du musst im
> Menü also nichts Besonderes wählen – die mitgelieferte Aufteilung (6 MB App)
> greift von selbst.

### 5. Kompilieren & flashen

- Den Ordner **`StreakhunterGo/`** öffnen (Doppelklick auf `StreakhunterGo.ino`).
- Board per USB-C anstecken, unter *Werkzeuge → Port* auswählen.
- Auf **Hochladen** klicken.

Kommt das Bild nach dem Flashen nicht, einmal den **RST-Taster** am Board drücken.

---

## Fertige Firmware erzeugen (für die Release-Seite / den Web-Flasher)

1. In der IDE: **Sketch → Kompilierte Binärdatei exportieren**.
2. Die `.bin`-Dateien liegen danach im Sketch-Ordner unter `build/`.
3. Für den Web-Flasher die drei Teile (Bootloader, Partitionen, App) zu einer
   `StreakhunterGo.bin` zusammenfügen und in [`firmware/`](firmware/) ablegen.

---

## Bedienung

- **Wischen links/rechts:** zwischen den Seiten (Profil · Start · Quests ·
  Abenteuer · Arena).
- **Nach oben wischen:** Statuskarte des Beasts.
- **Tippen:** auswählen.
- **BOOT-Taster:** aus dem Tiefschlaf aufwecken.

Das Beast lebt auf der SD-Karte. Ohne Karte geht es in den Urlaubsmodus und
nimmt keinen Schaden.

---

## Projektstruktur

```
StreakhunterGo/      der Arduino-Sketch (alle .ino/.cpp/.h/.c)
config/lv_conf.h     die passende LVGL-Konfiguration (v9.3.0)
firmware/            die fertige StreakhunterGo.bin
beast-cards/         die 6 Beast-Dateien fuer die SD-Karte (+ Anleitung)
```

---

## Hinweis

Streakhunter Go ist ein Hobby-/Fan-Projekt rund um das Streakhunter-Universum.
Prüfe vor einer Weitergabe, welche Grafiken und Namen frei verteilbar sind.
