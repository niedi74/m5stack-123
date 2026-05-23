---
created: 2025-05-23
source: hermes
tags: [hermes, m5stack, code-review, findings, embedded]
---

# Code-Analyse: M5Stack-123 — Hermes Findings & Anregungen

**Repository:** `niedi74/m5stack-123`  
**Branch:** `claude/m5stack-ble-nus-client-oqxww`  
**Analyse-Datei:** `src/main.cpp` (1.782 Zeilen)  
**Stand:** 2025-05-23

---

## 1. Bug: `pushLog` off-by-one

**Zeile:** ~65–72  
**Risiko:** Buffer-Overrun bei Logs > 37 Zeichen

`tmp` ist 38 Bytes. `strncpy(buf, tmp, 37)` + `buf[37] = 0` ist unsicher — wenn `tmp` genau 37 Zeichen lang ist, fehlt das Null-Byte (strncpy kopiert max 37, setzt aber kein \0 wenn Quelle >= Limit). `buf[37]` überschreibt dann das 38. Byte, aber `tmp` kann 38 Zeichen haben.

**Empfehlung:** `vsnprintf` direkt in Ziel-Buffer schreiben, kein `strncpy`.

---

## 2. Heap-Fragmentierung im Hot-Path

**Betroffene Funktionen:** `localTimestamp()`, `handleState()`, `appendLiveCsv()`  
**Risiko:** Bei 2h+ Fahrten fragmentiert der ESP32-Heap; möglicher Absturz oder Watchdog

`String`-Objekte werden in `loop()` (alle 80ms) allokiert:
- `localTimestamp()` gibt `String` zurück
- `handleState()` baut JSON als `String json; json.reserve(420)`
- `appendLiveCsv()` nutzt `String ts = localTimestamp()`

**Empfehlung:** Statische `char[]`-Buffer mit `snprintf` statt `String`. Betrifft auch `deFloat()` — No-Alloc-Variante `deFloatTo(char* out, size_t len, ...)` erstellen.

---

## 3. Fehlender ESP Task Watchdog

**Risiko:** Wenn `connectBLE()` oder `runReadOnlyDump()` blockieren/hängen bleiben, reagiert das Gerät nie wieder

**Empfehlung:** `esp_task_wdt_init(10, true)` + `esp_task_wdt_add(NULL)` in `setup()`. Nach jedem `delay()` in blockierenden Funktionen `esp_task_wdt_reset()` aufrufen.

---

## 4. BLE-Scan: endlos = Akku leer

**Zeile:** ~1196  
**Code:** `s->start(0, false);` — Scannt ohne Timeout

Wenn das Gerät im Fahrzeug parkt und kein BLE-Ziel da ist, scannt es ewig. Akku wird über Nacht leer.

**Empfehlung:** Exponentielles Back-Off.
- Scan 10 Sekunden → Pause 5 Sekunden → Scan 10s → Pause 10s → … bis max 30s Pause
- Bei Verbindung Backlog-Index zurücksetzen

---

## 5. main.cpp ist 1.782 Zeilen

**Risiko:** Phase 5–7 (CAN, Tempomat, erweiterte Sensoren) skalieren nicht in einer Datei

**Empfehlung:** Modularisieren in `src/`:
- `globals.h/.cpp` — shared state, enums, pins, constants
- `ble_client.h/.cpp` — scan, connect, NUS, notify, tune commands
- `display.h/.cpp` — all draw* functions, LGFX setup
- `webgui.h/.cpp` — WebServer handlers, DNS captive
- `logging.h/.cpp` — SPIFFS CSV, rotation, append
- `tune.h/.cpp` — Live Tune FSM, safety guards
- `wifi_manager.h/.cpp` — WiFi, WPS, NTP, RTC
- `input.h/.cpp` — encoder, button, touch, buzzer

`main.cpp` sollte nur noch `setup()`, `loop()` und Includes enthalten.

---

## 6. Branching unklar

**Status:** `main` ist fast leer (nur README.md). Aktiver Code liegt auf `claude/m5stack-ble-nus-client-oqxww`.

**Empfehlung:**
- `main` als stabilen Branch definieren (v0.3.0-ui taggen)
- Feature-Branches für Phase 2–7
- Der Branch-Name `claude/m5stack-ble-nus-client-oqxww` deutet auf automatische Branch-Erstellung hin — umbenennen oder mergen wenn stabil

---

## 7. NimBLE-Version: Code v2, Doku noch v1

**Status:** Bereichert in `ZUSAMMENFASSUNG.md` — durch Hermes bereits gefixt und gepusht

**Fakt:** `platformio.ini` verlangt `^2.0.0`. Code nutzt v2-APIs (`NimBLEConnInfo`, `NimBLEAttValue`, `onDisconnect(NimBLEClient*, int)`). Frühere Commits hatten v1.4.3.

---

## 8. Deutsches CSV-Format

**Positiv bemerkt:** Semikolon-Trenner + Komma-Dezimal. Passt zu Excel/deutschem Zahlenformat. Keine Änderung empfohlen.

---

## Positiv hervorzuheben

- **Safety-First bei Live-Tune:** ARM-Timeout (30s), ±10 Schritte Limit, Exit nur über Null-Rückstellung. Bei einem Zündwinkel-Projekt essentiell.
- **Dokumentation auf Profi-Niveau:** BLE-Protokollreferenz mit Checksum-Formel, Fahrtanalyse mit statistischer Auswertung, Release-Tracking, Zündkurven-Snapshots.
- **WebGUI im Gerät:** Kein Laptop nötig, responsive CSS-Grid, CSV-Download, WPS-Setup, Zeit-Sync.
- **Reverse-Engineered BLE-Protokoll:** Mit nRF Connect, Screenshots, Hypothesen-Tests V5–V13 systematisch analysiert.

---

## Prioritäten (meine Reihenfolge)

| # | Thema | Aufwand | Risiko | Impact |
|---|-------|---------|--------|--------|
| 1 | Bug-Fix `pushLog` | 5 min | NULL | Absturz vermeiden |
| 2 | Watchdog | 15 min | Niedrig | Stabilität |
| 3 | Heap-Frag (String→char) | 1 h | Mittel | Langzeitstabilität |
| 4 | BLE-Scan Back-Off | 30 min | Niedrig | Akkulaufzeit |
| 5 | Modularisierung | 3–4 h | Mittel | Erweiterbarkeit |
| 6 | Branching/Tags | 10 min | NULL | Workflow |

---

*Analyse durchgeführt von Hermes — 2025-05-23*
