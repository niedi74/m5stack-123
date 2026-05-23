# Projekt-Zusammenfassung: M5Stack Dial als 123\TUNE+ BLE-Monitor

**Fahrzeug:** VW T2b, 2L Typ 4, Automatik, luftgekühlt  
**Ziel:** M5Stack Dial (ESP32-S3) empfängt Live-Daten vom 123\TUNE+ Zündsteuergerät via BLE NUS und zeigt sie auf dem runden Display an.  
**Stand:** 2026-05-21

---

## Was bisher erarbeitet wurde

### Hardware

| Komponente | Detail |
|---|---|
| Mikrocontroller | M5Stack Dial (StampS3, ESP32-S3) |
| Display | GC9A01, 240×240 px, rund |
| BLE-Gegenstelle | 123\TUNE+ Zündsteuergerät, nRF52810 |
| CAN-Transceiver | SN65HVD230 (für spätere CAN-Erweiterung) |
| Optokoppler | Hella-Eingänge, Bremse, N/P, SET/RES, Zündplus-Status |

> **Wichtig:** Optokoppler sind NICHT für CAN vorgesehen — nur für Digitaleingänge.  
> CAN braucht ausschließlich den SN65HVD230.

### Software-Stand

- **PlatformIO-Projekt** vollständig: `platformio.ini`, `src/main.cpp`
- **NimBLE-Arduino ^2.0.0** API (v2.x; früher v1.4.x, migriert nach Commit `e07af16`)
- **M5GFX** mit manuellem LGFX-Panel (GC9A01, SPI2)
- **LGFX_Sprite** für flimmerfreie Display-Updates
- **Encoder-Button** (PIN 42): Kurzdruck = View-Wechsel, Langer Druck (>600 ms) = Raw-Log Ein/Aus
- **Raw-Log-Modus**: Alle BLE-Frames mit Zeitstempel als Hex-Dump auf Serial
- **Auto-Reconnect** bei Verbindungsverlust via `startScan()` in `onDisconnect`

### BLE-Protokoll (reverse-engineered)

**Frame-Aufbau (5 Bytes):**

```
Byte[0]  Typ       0x30=RPM, 0x31=Advance, 0x32=MAP, 0x33=Temp,
                   0x35=Strom, 0x41=Volt, 0x42=const70, 0x0D=Keepalive
Byte[1]  Hi-Nibble ASCII-Hex ('0'–'F')
Byte[2]  Lo-Nibble ASCII-Hex ('0'–'F')
Byte[3]  Checksum  = (Byte[0] + Byte[1] + Byte[2]) - 0x50
Byte[4]  Terminator 0x20 (Space) — von nRF Connect NICHT angezeigt
```

**Dekodierung:**

| Typ | Einheit | Formel |
|---|---|---|
| `0x30` RPM | U/min | `hi × 800 + lo × 50` |
| `0x31` Advance | °KW | `hi × 3.2 + lo × 0.2` |
| `0x32` MAP | kPa | `(hi << 4) \| lo` |
| `0x33` Temp | °C | `((hi << 4) \| lo) − 30` |
| `0x35` Strom | A | `((hi << 4) \| lo) / 8.65` |
| `0x41` Spannung | V | `((hi << 4) \| lo) / 4.54` |
| `0x42` Unbekannt | ? | Konstant 70 im Leerlauf |

**Echte Messwerte (Motor Leerlauf ~1100 RPM, kalt):**

| nRF Connect | Frame (Hex) | Wert |
|---|---|---|
| `016G` | `30 31 36 47 20` | 1100 U/min |
| `264L` | `32 36 34 4C 20` | 100 kPa |
| `51DZ` | `35 31 44 5A 20` | 3.35 A |
| `33DZ` | `33 33 44 5A 20` | 31 °C |
| `A40U` | `41 34 30 55 20` | 14.1 V |
| `B46\` | `42 34 36 5C 0D` | 70 (const) |

### Wichtige Erkenntnisse aus nRF Connect Screenshots

1. **nRF Connect zeigt nur 4 Bytes** — das 5. Byte (0x20 Terminator) wird nicht dargestellt
2. **CCCD bleibt nach Disconnect aktiv** — "Notifications enabled" bleibt im CCCD nach Verbindungstrennung sichtbar (Screenshot 14:09); kein erneutes Schreiben nötig nach Reconnect
3. **Advertising pausiert während Verbindung** — in der History sichtbare Lücke bei 18:26–18:29 Uhr; normales nRF52-Verhalten
4. **Exakte Advertising-Intervalle:** 99 ms, 105 ms, 108 ms, 110 ms (Mittel ~103 ms)
5. **RSSI:** −87 bis −91 dBm bei ~5–10 m Abstand
6. **Preferred Connection Params:** `Connection Interval: 20.00ms - 40.00ms, Max Latency: 0, Supervision Timeout Multiplier: 400`
7. **Battery Level:** 48% (bestätigt bei 18:29 und 18:30 Uhr)
8. **Generic Attribute Service (0x1801):** Leer — kein Inhalt
9. **Keine Authentifizierung, kein Bonding** — direkt verbinden und auf NUS TX subscriben

### Behobene Fehler (Entwicklungshistorie)

| Problem | Ursache | Fix |
|---|---|---|
| Kein Serial-Output | `decodeFrame()` hatte keine `Serial.printf()` Aufrufe | Alle Frame-Typen mit Output ergänzt |
| Manuelle LGFX-Includes | Unnötige `#include <lgfx/v1/panel/...>` | Entfernt, nur `#include <M5GFX.h>` |
| Fehlende Frame-Typen | `0x32` (MAP) und `0x35` (Strom) fehlten | In `switch-case` ergänzt |
| COM8 hardcoded | `upload_port = COM8` in platformio.ini | Auf Auto-Detect umgestellt |
| API-Mischung | NimBLE v1.x + v2.x APIs vermischt | Standardisiert auf v1.4.x |
| Upload schlägt fehl | ESP32-S3 USB CDC braucht spezielle Reset-Sequenz | `--before=usb_reset --after=hard_reset` in `upload_flags` |

---

## Motor & Vergaser-Tuning

**Motor:** Type 4/914, 276er Nockenwelle (scharf), 9:1 Verdichtung  
**Vergaser:** PDSIT 36/40, 32mm Venturi (original 30mm)  
**Düsen:** LKD=105, HD=145, LLD=60

**Aktuelle Zündkurve (statisch 0° KW, Rev-Limiter 4650 RPM):**

| RPM | Zentrifugal (°KW) |
|---|---|
| 500 | 8° |
| 1000 | 14° |
| 1600 | 22° |
| 1800 | 26° |
| 2500 | 30° |
| >2500 | 30° (Plateau) |

**MAP-Korrektur (aktiv ab 1500 RPM):**

| MAP (kPa) | Korrektur (°) |
|---|---|
| 0 | +11° |
| 30 | +10° |
| 50 | +8° |
| 60 | +5° |
| 73 | +2° |
| ≥85 | 0° |

**Problem:** 0.1 bar Unterdruck → Lambda 0.7–0.8 (zu fett)  
**Helmholtz-Resonanzsystem** auf dem Ansaugkrümmer: 8er Leitung, 2 Resonatoren (20 cm / 25 cm), Volumina 2×200 ml + 3×300 ml

---

## Roadmap

| Phase | Inhalt | Status |
|---|---|---|
| 1 | BLE NUS Client, Display, Raw-Log | ✅ Fertig |
| 2 | Lambda-Sensor (LSU 4.9 / CJ125) | Geplant |
| 3 | Geschwindigkeit, Gang-Erkennung | Geplant |
| 4 | SD-Logging, CSV-Export | Geplant |
| 5 | CAN-Bus (SN65HVD230), Multi-ESP32 | Geplant |
| 6 | Tempomat (Cruise Control) | Geplant |
| 7 | Erweiterte Sensoren | Geplant |

---

## Dateien im Repository

```
m5stack-123/
├── platformio.ini          PlatformIO-Konfiguration (ESP32-S3, NimBLE, M5GFX)
├── src/
│   └── main.cpp            Hauptprogramm (BLE, Display, Encoder, Raw-Log)
├── docs/
│   ├── BLE_Protokoll_Referenz.md   Vollständige BLE-Protokoll-Dokumentation
│   ├── PDSIT_Tuning_Abstimmung.md  Vergaser-Abstimmungsnotizen
│   ├── SETUP_VSCODE.md             VS Code / PlatformIO Build-Anleitung
│   └── ZUSAMMENFASSUNG.md          Diese Datei
├── tuning/
│   └── kurven_aktuell.md           Aktuelle Zündkurve
├── .vscode/
│   └── extensions.json             VS Code Extension-Empfehlungen
└── README.md               Projekt-Übersicht
```

---

*Letzte Aktualisierung: 2026-05-21*
