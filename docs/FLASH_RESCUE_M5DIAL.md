# M5Dial Flash-Rescue — Lokale Session

**Ziel:** M5Dial Bootloop beheben, sauber booten, dann schrittweise zurück.  
**Branch:** `feature/spartan-live-display`  
**Fix:** `board_build.arduino.memory_type = qio_opi` — bereits committed & gepusht.

---

## Schritt 1 — Repo holen

```powershell
cd D:\_claude\M5stack\m5stack-123
git fetch origin
git checkout feature/spartan-live-display
git pull origin feature/spartan-live-display
```

Prüfen ob der Fix drin ist:
```powershell
findstr "qio_opi" platformio.ini
# Muss ausgeben: board_build.arduino.memory_type = qio_opi
```

---

## Schritt 2 — M5Dial anschließen

- USB-C Kabel in M5Dial
- Port: **COM4** (sonst `upload_port` in platformio.ini anpassen)
- M5Dial **einschalten** (Seitentaste oder USB-Power)

---

## Schritt 3 — Build + Flash

```powershell
pio run -e m5stack-stamps3 -t upload
```

Falls COM4 nicht erkannt wird:
```powershell
# Port suchen
pio device list
# Dann in platformio.ini: upload_port = COMx
pio run -e m5stack-stamps3 -t upload
```

---

## Schritt 4 — Serial Monitor öffnen

```powershell
pio device monitor -p COM4 -b 115200
```

**Erwarteter Output (stabil, kein Crash):**
```
=== M5Dial 123TUNE+ boot 1 ===
=== M5Dial 123TUNE+ boot 2 ===
...
[BOOT] RESCUE: 123 direct
alive
alive
alive
```

**Display:** "M5 BOOT" bleibt stehen (grün auf schwarz). Kein Blinken mehr.

---

## Schritt 5 — Wenn stabil: Rescue-Flag entfernen

In `platformio.ini` die Zeile ändern:
```ini
; WAS:
-D M5_RESCUE_DIRECT_ONLY=1
; ENTFERNEN oder auskommentieren:
; -D M5_RESCUE_DIRECT_ONLY=1
```

Dann wieder flashen:
```powershell
pio run -e m5stack-stamps3 -t upload
```

Jetzt startet BLE (123-direkt Modus), WiFi bleibt aus bis du es aktivierst.

---

## Warum der Bootloop passierte

| Ursache | Wirkung |
|---------|---------|
| `qio_opi` fehlte | OPI-PSRAM falsch initialisiert |
| `LGFX_Sprite::createSprite()` in setup() | Allociert PSRAM → Cache-Fault |
| Cache-Fault | Guru Meditation → Reset |
| Reset → Repeat | Bootloop: "kurz hell, dann dunkel" |

**Fix:** `board_build.arduino.memory_type = qio_opi` + `flash_size=16MB` + `partitions=default_16MB.csv`

---

## Hardware-Referenz M5Dial

| Eigenschaft | Wert |
|-------------|------|
| Modul | ESP32-S3-WROOM-1 **N16R8** |
| Flash | 16 MB QIO |
| PSRAM | 8 MB **OPI (Octal)** |
| USB | Nativ USB-C (kein UART-Chip) |
| Port | COM4 |

---

*Fix committed: 2026-06-19, Branch: feature/spartan-live-display*
