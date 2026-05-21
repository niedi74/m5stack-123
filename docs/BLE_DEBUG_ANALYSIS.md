# BLE Debug Analyse — NUS TX silent, Battery loud

> Technische Tiefenanalyse: warum `6e400003` (NUS TX, Handle 15) keine Notifications
> an ESP32-S3 NimBLE-Client liefert, obwohl CCCD korrekt auf `0x0001` gesetzt ist und
> dasselbe Gerät an iPhone/Android-nRF-Connect normal sendet.

## Kernfakten

| Aspekt | NUS TX (Handle 15) | Battery (Handle 29) |
|---|---|---|
| UUID | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | `0x2A19` |
| `canNotify()` | true | true |
| Subscribe Return | OK | OK |
| CCCD Pre-Subscribe | `0x0000` | nicht gemessen |
| CCCD Post-Subscribe | `0x0001` | nicht gemessen |
| Notifications empfangen (ESP32) | **0** | viele (~5/s) |
| Notifications empfangen (iPhone) | viele (alle Frame-Typen) | nicht relevant |
| Inhalt | (nichts) | 1 Byte `0x2D` / `0x2C` alternierend |

## Was 0x2D / 0x2C bedeuten könnten

Bei Battery Service (`0x180F`) ist der Wert nach Spec 0-100% Batterie.
`0x2D` = 45, `0x2C` = 44 → könnten Spannung sein, oder einfach
ein Counter/Heartbeat zur Lebenszeichen-Erzeugung im 123\TUNE+. Inhalt ist
für das Debug-Ziel egal — der Punkt ist, **dass dort Notifications fliessen**,
also der ESP32-S3 NimBLE-Stack grundsätzlich Notifications empfangen kann.

## Beweisführung: ESP32 hört zu

NimBLE-Pfad `BLE_GAP_EVENT_NOTIFY_RX → m_notifyCallback`:

```cpp
// NimBLEClient.cpp:1165
case BLE_GAP_EVENT_NOTIFY_RX: {
    if (pClient->m_connHandle != event->notify_rx.conn_handle) return 0;
    NimBLERemoteCharacteristic* pChr = pClient->getCharacteristic(
        event->notify_rx.attr_handle);  // Lookup per attr_handle
    if (pChr->m_notifyCallback != nullptr) {
        pChr->m_notifyCallback(pChr, data, len, !indication);
    }
    return 0;
}
```

Da Battery (handle 29) den Callback feuert, ist klar:
- ✅ BLE-Connection ist intakt
- ✅ Conn-Handle stimmt
- ✅ Notify-Dispatch funktioniert
- ✅ Per-Char `m_notifyCallback` ist registriert

→ Wenn Handle 15 trotzdem nicht feuert, hat der **Slave keine** `BLE_GAP_EVENT_NOTIFY_RX`
für Handle 15 gesendet. Das Problem ist **slave-seitig**, nicht in unserem Stack.

## Subscribe-Pfad in NimBLE 2.x

```cpp
// NimBLERemoteCharacteristic.cpp:241
bool setNotify(uint16_t val, notify_callback notifyCallback, bool response) const {
    m_notifyCallback = notifyCallback;
    NimBLERemoteDescriptor* desc = getDescriptor(NimBLEUUID((uint16_t)0x2902));
    if (desc == nullptr) return true;     // ← Callback set, aber CCCD nicht geschrieben!
    return desc->writeValue(reinterpret_cast<uint8_t*>(&val), 2, response);
}
```

Wichtig: Wenn CCCD-Deskriptor **nicht gefunden** wird, gibt `setNotify` `true` zurück
obwohl gar kein Slave-Write stattfand. Unser CCCD-Readback (`post=0001`) widerlegt
diesen Fall — die Char hat eine CCCD, sie wurde geschrieben, der Slave hat den Wert
gespeichert und gibt ihn beim Read zurück.

## Was iPhone/Android anders machen (Hypothesen)

| Mechanismus | iPhone Standard | NimBLE-Default | Relevant? |
|---|---|---|---|
| ATT MTU Exchange | Request 185-247 | Bleibt 23 (keiner schlägt vor) | Nein (5-Byte Frames) |
| Initial Conn Interval | 30ms | 24ms | Marginal |
| Slave Latency | Accept slave proposal | Akzeptiert (V3+) | Nein |
| Supervision Timeout | 6000-20000ms | 4000ms (Slave-Wunsch) | Marginal |
| Pairing/Bonding | "Just Works" silent | Off by Default | **JA, plausibel** |
| Privacy/RPA | Random Resolvable | Public | Eventuell |
| LE Secure Connections | Ja | Ja in V5 aktiv | gleich |
| PHY Preferred | 1M | 1M | gleich |

→ **Pairing/Bonding** ist die wahrscheinlichste Lücke. Apple paired *transparent*
und ohne PIN-Dialog wenn IO-Cap = NO_INPUT_OUTPUT, Slave fordert MITM nicht.

## Wenn Pairing nicht hilft — weitere Tests

### Test A: Nur NUS subscriben, kein Battery
Begründung: Vielleicht nimmt Battery dem Slave die "Notification Queue Slot" weg.
```cpp
auto* chr = pClient->getService(NUS_SVC)->getCharacteristic(NUS_TX);
chr->subscribe(true, onAnyNotify, true);
// KEIN sub auf Battery!
```

### Test B: Subscribe-Reihenfolge umdrehen
Battery zuerst, dann NUS.

### Test C: Vor Subscribe einen kleinen Delay
```cpp
delay(500);  // 500ms nach connect, vor subscribe
```

### Test D: Explizit Connection Params VOR connect setzen
```cpp
pClient->setConnectionParams(16, 32, 0, 400);
pClient->connect(targetAddr);
```

### Test E: Magic-Bytes auf RX schreiben
Bisher getestet (führt zu sofortigem Disconnect):
- `0x01`
- `"START\r\n"`

Noch nicht getestet (Hinweise aus 123-Forum/Reverse-Eng):
- `"$\r\n"`
- `"AT\r\n"` (AT-Command-Style passend zur "UART AT Command" Firmware)
- `"\x02\x03"` (STX/ETX)

### Test F: Sniffer-Capture vom iPhone-Connect
Mit nRF Sniffer + Wireshark beim iPhone-Verbinden mitschneiden →
sieht man **genau** welche Pakete iPhone vor/nach Subscribe schickt.

## Supervision Timeout (reason=520) — Was es bedeutet

- `520 = 0x208 = BLE_HS_ERR_HCI_BASE (0x200) + HCI Error 0x08 (Connection Timeout)`
- Bedeutet: Master + Slave haben sich für `supervision_timeout × 10ms`
  nicht gehört → BLE-Spec sagt "Connection lost".
- Mit `T=400` (4000ms) hört der Slave nach 4s Stille zu reagieren auf.
- **Trotz Battery-Notifications all 200ms** bricht die Connection.
  Das ist seltsam — die Notifications halten die Connection eigentlich am Leben.

Mögliche Erklärung: Slave reagiert nicht mehr auf Conn-Events (z.B. Empty PDUs
vom Master), schickt aber noch Notifications. Wenn Master keine Connection
Update Request bekommt oder seine PDUs zu lange ausbleiben.

Oder: 4 Sekunden absolute Silence von Slave Richtung Master:
```
46857ms NTFY 2D
47057ms NTFY 2C   ← Δ=200ms
47507ms NTFY 2D   ← Δ=450ms
51266ms NTFY 2C
                    ← grosse Lücke
55386ms Disc reason=520
```

Lücke 51266 → 55386 = 4120ms. Genau ein Supervision Timeout. Slave hat
einfach aufgehört zu senden.

→ **Beobachtung**: Battery-Notifications hören 4s vor Disconnect auf.
Möglicherweise ist das ein Slave-Bug oder Slave-Logik: "Wenn Master nichts
schickt → in Idle gehen → nach Supervision Timeout disconnect".

## Was wir als Nächstes wissen müssen

1. **Bringt Pairing/Bonding die NUS-Daten?** (V5 läuft, Test ausstehend)
2. **Bringt Single-Char-Subscribe (nur NUS) etwas?** (Test C danach)
3. **Was schickt iPhone zwischen Subscribe und erstem NUS-Frame?**
   (BLE-Sniffer-Capture, falls notwendig)
4. **Was sind die `prop=` Bits der NUS TX Char?** (im Dump hardcoded auf 00, Bug — sollte
   `c->m_properties` sein, aber das ist private. Workaround: alle canX() abfragen)

## Erweiterung des Diagnose-Codes (geplant)

```cpp
// Properties richtig dumpen
uint8_t props = 0;
if (c->canRead())            props |= 0x02;
if (c->canWriteNoResponse()) props |= 0x04;
if (c->canWrite())           props |= 0x08;
if (c->canNotify())          props |= 0x10;
if (c->canIndicate())        props |= 0x20;
if (c->canWriteSigned())     props |= 0x40;
if (c->hasExtendedProps())   props |= 0x80;

// Pairing-Status nach Connect loggen
NimBLEConnInfo info = pClient->getConnInfo();
Serial.printf("Encrypted=%d Authenticated=%d Bonded=%d\n",
              info.isEncrypted(), info.isAuthenticated(), info.isBonded());

// Erster NUS-Frame Inhalt nach Subscribe loggen separat (auch wenn 0 bytes)
```
