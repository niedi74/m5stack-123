# Follow-up zu Hermes Findings - 2026-05-23

Bezug: `reviews/hermes_2025-05-23_m5stack-findings.md`

## Ergebnis

| Finding | Bewertung | Aktion |
| --- | --- | --- |
| `pushLog` Buffer | Kein Overflow im aktuellen Code: `vsnprintf(..., 38, ...)` terminiert bereits, Zielindex `37` liegt im 38-Byte-Puffer. | Keine kosmetische Aenderung am Fahrtest-Kandidaten. |
| Heap/`String` | Als Langzeit-Haertung sinnvoll; `appendLiveCsv()` arbeitet waehrend einer Fahrt regelmaessig. `handleState()` laeuft dagegen nur bei Web-Anfragen. | Fuer separaten Langzeittest/Refactor vorgemerkt. |
| Task Watchdog | Grundsaetzlich sinnvoll, greift aber in BLE-Discovery und Read-Dumps ein und muss mit realen Verbindungszeiten getestet werden. | Nicht blind in den geflashten Kandidaten aufgenommen. |
| Endloser BLE-Scan | Bestaetigtes Energieproblem bei ausgeschalteter 123Tune+. | Behoben und auf `COM4` getestet. |
| Modularisierung | Vor CAN/Lambda-Ausbau sinnvoll. | Nach Stabilisierung der aktuellen UI einplanen. |
| Branch/Tags | Stabile Meilensteine sollen sichtbar sein; aktuelles UI ist noch Hardware-Kandidat. | `v0.1.0-core` und `v0.2.0-webgui` existieren; `v0.3.0-ui` erst nach Touch/Buzzer/Display-Test. |
| NimBLE-Doku | Hermes-Fix wurde uebernommen. | Erledigt. |
| CSV-Format | Semikolon und deutsches Dezimalformat passen. | Beibehalten. |

## Umgesetzter BLE-Scan-Fix

Der Scanner laeuft nicht mehr dauerhaft, wenn die Zuendung bzw. 123Tune+ nicht
erreichbar ist:

1. Scan-Fenster: `10 s`.
2. Pausen nach erfolglosen Fenstern: `5 s`, `10 s`, `20 s`, danach maximal `30 s`.
3. Sobald das Ziel gefunden oder eine BLE-Verbindung hergestellt wird, wird
   das Backoff zurueckgesetzt.
4. Nach einem Verbindungsverlust startet die Suche sofort wieder, damit der
   Betrieb waehrend der Fahrt nicht durch eine lange Wartezeit ausgebremst wird.

## Verifikation

- `pio run` erfolgreich.
- Firmware auf `COM4` geflasht.
- Ohne verbundene 123Tune+ im seriellen Monitor bestaetigt:

```text
Scan Ende r=0
Scan Pause 5s
Scan 10s...
Scan Ende r=0
Scan Pause 10s
Scan 10s...
Scan Ende r=0
Scan Pause 20s
```

- WebGUI blieb im Heim-WLAN erreichbar; `/state` zeigte Tune weiterhin
  gesperrt (`tune_armed=false`, `tune_active=false`).

## Naechste Stabilitaetsarbeit

Vor dem echten Live-Tune-Fahrtest bleiben die physischen Tests fuer Touch,
Buzzer und Display-Lesbarkeit erforderlich. Danach ist der sinnvollste
Langzeittest eine Fahrt mit Logging und Beobachtung des freien Heaps, bevor
die `String`-Pfade gezielt umgebaut werden.
