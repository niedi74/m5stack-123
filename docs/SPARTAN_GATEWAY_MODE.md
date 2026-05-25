# M5 Dial: Direct 123TUNE+ Mode and Spartan Gateway Mode

## Goal

The M5 Dial should support two selectable connection modes:

| Mode | BLE target | Purpose |
| --- | --- | --- |
| `123 direkt` | 123TUNE+ | Existing direct cockpit display and debug fallback |
| `Spartan Gateway` | Spartan ESP board, advertised as `Spartan3-Hub` | Combined cockpit data from 123TUNE+, Spartan Lambda and later reed speed |

This keeps the existing working 123TUNE+ path and adds the new gateway path without forcing a hard cutover.

## Menu Structure

Add one persistent setting in the M5 menu:

```text
Verbindung
  123 direkt
  Spartan Gateway
```

Expected behavior:

1. Store the selected mode in Preferences/NVS.
2. On mode change, disconnect the current BLE client.
3. Clear stale runtime values on the display.
4. Start a fresh scan for the selected target.
5. Show the active mode on the settings/status screen.

## Direct 123TUNE+ Mode

Keep the current BLE NUS implementation:

| Item | Value |
| --- | --- |
| Service UUID | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| TX Notify UUID | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| RX Write UUID | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| Known target address | `EF:A8:B2:DE:E0:9E` |

This mode should continue to decode RPM, advance, MAP, temperature, voltage and tune commands exactly as today.

## Spartan Gateway Mode

In gateway mode the M5 connects to the Spartan ESP instead of the 123TUNE+.

| Item | Value |
| --- | --- |
| Advertised name | `Spartan3-Hub` |
| Service UUID | `7f510001-5a6b-4d2a-9f20-14a7f3e20000` |
| Status Notify UUID | `7f510002-5a6b-4d2a-9f20-14a7f3e20000` |
| Command Write UUID | `7f510003-5a6b-4d2a-9f20-14a7f3e20000` |
| Initial payload | JSON text |
| Notify interval | 250 ms |

For early testing, scan by advertised name plus service UUID. The Spartan ESP also exposes its current BLE address in its serial boot log and in `/state` as `ble_address`; store that address only as an optional fast reconnect/debug hint.

## Gateway Payload V1

The current Spartan gateway prototype sends JSON on the status characteristic. Example fields:

```json
{
  "valid": true,
  "lambda": 1.000,
  "temperature": 780,
  "status": "OK",
  "status_code": 3,
  "source": "DEMO",
  "can_ready": true,
  "age_ms": 42,
  "ble_clients": 1,
  "ble_name": "Spartan3-Hub",
  "ble_address": "xx:xx:xx:xx:xx:xx"
}
```

Planned later fields:

| Field | Source |
| --- | --- |
| `rpm` | 123TUNE+ via Spartan gateway |
| `advance` | 123TUNE+ via Spartan gateway |
| `map` | 123TUNE+ via Spartan gateway |
| `engine_temp` | 123TUNE+ via Spartan gateway |
| `battery_v` | 123TUNE+ via Spartan gateway |
| `speed` | Reed contact via Spartan gateway |

## Implementation Checklist

1. Add enum `ConnectionMode { DIRECT_123TUNE, SPARTAN_GATEWAY }`.
2. Store/load `ConnectionMode` in Preferences.
3. Add menu item `Verbindung` with the two choices.
4. Split BLE connect code into target profiles:
   - 123TUNE+ profile: NUS service and existing decoder.
   - Spartan profile: gateway service and JSON parser.
5. Add a small common telemetry model used by the display pages.
6. In direct mode, fill only 123TUNE+ fields.
7. In gateway mode, fill Lambda, 123TUNE+ and speed fields as they become available.
8. Add reconnect logic per mode.
9. Add status text: `123 direkt`, `Gateway`, `Scan`, `Verbunden`, `Fehler`.
10. Later replace JSON parsing with a compact binary frame once the field set is stable.

## Notes

The gateway mode is the preferred final cockpit setup because the M5 gets one combined data stream. Direct 123TUNE+ mode stays valuable as a fallback, for comparison testing, and for driving before the gateway board is fully installed.
