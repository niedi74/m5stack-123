# Dokumentations-Index — m5stack-123

## Aktiver Debug-Stand

→ **[SESSION_2026-05-21.md](SESSION_2026-05-21.md)** — Vollständiges Session-Log
→ **[BLE_DEBUG_ANALYSIS.md](BLE_DEBUG_ANALYSIS.md)** — Technische Analyse + Hypothesen
→ **[NEXT_STEPS.md](NEXT_STEPS.md)** — Test-Plan V5...V13

## Referenz

→ [BLE_Protokoll_Referenz.md](BLE_Protokoll_Referenz.md) — 5-Byte-Frame-Protokoll
→ [SETUP_VSCODE.md](SETUP_VSCODE.md) — VSCode + PlatformIO Setup
→ [PDSIT_Tuning_Abstimmung.md](PDSIT_Tuning_Abstimmung.md) — Vergaser-Abstimmung (off-topic)

## Aktueller Code-Stand

- `src/main.cpp` (V5) — mit Pairing/Bonding, Service-Dump, Heartbeat
- `platformio.ini` — NimBLE 2.x, COM4-Upload
- Branch: `master` (lokal vor möglicher claude-Branch im Remote)

## Letztes Problem

**NUS TX (`6e400003`) liefert keine Notifications**, obwohl:
- Subscribe technisch OK
- CCCD = 0x0001 nach Subscribe
- iPhone/Android nRF Connect bekommt die Daten

Battery-Service (`0x2A19`) sendet dagegen normal — also ist BLE-Empfang
grundsätzlich OK. Ursache wahrscheinlich slave-seitig (Auth, Init-Trigger oder
Subscribe-Reihenfolge). Siehe [BLE_DEBUG_ANALYSIS.md](BLE_DEBUG_ANALYSIS.md).
