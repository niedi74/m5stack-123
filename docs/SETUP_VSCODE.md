# VS Code + PlatformIO — Build & Flash

## Voraussetzungen

1. [VS Code](https://code.visualstudio.com/) installieren
2. Extension **PlatformIO IDE** installieren
   - Extensions-Tab (`Ctrl+Shift+X`) → `platformio.platformio-ide` suchen
   - Oder: Repo klonen, VS Code öffnet Extension-Empfehlung automatisch (`.vscode/extensions.json`)

## Projekt öffnen

```bash
git clone https://github.com/niedi74/m5stack-123.git
cd m5stack-123
git checkout claude/m5stack-ble-nus-client-oqxww
code .
```

PlatformIO erkennt `platformio.ini` automatisch und lädt die Libraries.

---

## Build

| Methode | Aktion |
|---|---|
| VS Code | Hämmersymbol in der unteren Statusleiste klicken |
| Tastenkürzel | `Ctrl+Alt+B` |
| Terminal | `pio run` |

Erwartete Ausgabe:
```
Building .pio/build/m5stack-stamps3/firmware.bin
... [SUCCESS]
```

---

## Flash (Upload)

### Automatisch (Normalfall)

| Methode | Aktion |
|---|---|
| VS Code | Pfeilsymbol (→) in der Statusleiste |
| Tastenkürzel | `Ctrl+Alt+U` |
| Terminal | `pio run -t upload` |

PlatformIO erkennt den COM-Port automatisch. Der ESP32-S3 wird per
`--before=usb_reset` automatisch in den Bootloader versetzt.

### Falls Auto-Erkennung fehlschlägt

**Windows:** Geräte-Manager öffnen → Anschlüsse (COM & LPT) → COM-Nummer notieren

Dann in `platformio.ini` die Zeile auskommentieren:
```ini
upload_port = COM8   ; ← eigene COM-Nummer einsetzen
```

**macOS/Linux:**
```bash
ls /dev/tty.usbmodem*   # macOS
ls /dev/ttyACM*          # Linux
```
```ini
upload_port = /dev/ttyACM0
```

> **Tipp:** Lokale Anpassungen in `platformio.local.ini` speichern
> (wird von `.gitignore` ausgeschlossen und nicht gepusht):
> ```ini
> [env:m5stack-stamps3]
> upload_port = COM8
> ```

### Manueller Bootloader-Modus (Fallback)

Falls der automatische Reset nicht funktioniert:

1. **BOOT-Taste** auf dem StampS3 gedrückt halten
2. **RST-Taste** kurz drücken und loslassen
3. **BOOT-Taste** loslassen
4. Flash-Befehl ausführen

Beim M5Stack Dial ist die BOOT-Taste die **Encoder-Achse gedrückt halten**
(GPIO42) während des Resets — oder der seitliche Taster falls vorhanden.

---

## Serial Monitor

| Methode | Aktion |
|---|---|
| VS Code | Steckersymbol in der Statusleiste |
| Terminal | `pio device monitor` |

Baud: **115200** (in `platformio.ini` konfiguriert)

Erwartete Ausgabe nach Boot:
```
=== M5Dial 123TUNE+ BLE Client ===
Knopf kurz: Ansicht  |  Knopf lang: RAW-Log an/aus

BLE: Scan laeuft, suche ef:a8:b2:de:e0:9e
```

Nach BLE-Verbindung:
```
BLE: Geraet gefunden ef:a8:b2:de:e0:9e
BLE: verbinde...
BLE: verbunden
BLE: Notify aktiv  [RAW-Log: AN]
[   2041ms] RAW(5): 30 31 37 48 20  |017H 
  RPM:  1150
[   2045ms] RAW(5): 31 34 43 58 20  |14CX 
  ADV:  15.2 deg
```

---

## Troubleshooting

| Problem | Lösung |
|---|---|
| `No device found` beim Upload | COM-Port manuell setzen oder Kabel wechseln |
| `error: Failed to connect` | Manuellen Bootloader-Modus versuchen (s.o.) |
| Library nicht gefunden | `pio lib install` oder PlatformIO-Cache leeren |
| Schwarzes Display | `display.init()` läuft, aber Backlight-Pin prüfen (GPIO9) |
| Kein Serial-Output | Baud auf 115200 prüfen; USB-Treiber ggf. neu installieren |
| BLE findet Gerät nicht | 123\\TUNE+ App schließen — nur eine Verbindung gleichzeitig möglich |
