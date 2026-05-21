# Next Steps & Test-Plan

Reihenfolge der noch durchzuführenden Tests, mit klaren Code-Snippets und
erwartetem Output. Jeder Test soll **isoliert** sein (nur eine Änderung).

## ✅ Test V5 — Pairing/Bonding aktiviert (gerade geflasht)

**Code-Stand**: `src/main.cpp` enthält in `setup()`:
```cpp
NimBLEDevice::setSecurityAuth(true, false, true);
NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
```

**Test**:
- Boot-Modus → Flash → Normal-Boot
- Monitor öffnen, Zündung an
- Erwartet: nach Subscribe Pairing-Sequenz, dann **NTFY handle=15** mit 5-Byte-Frames

**Mögliche Ergebnisse**:
| Output | Diagnose |
|---|---|
| `NTFY handle=15 len=5 : 30 ...` | 🎉 Pairing war die Lösung! |
| Pairing-Fail im Log | Slave akzeptiert kein Pairing → andere Ursache |
| Disc bei Pairing-Versuch | Slave verbietet Pairing → andere Ursache |
| Wie V4 (nur Battery) | Pairing ändert nichts → H1 falsch |

---

## Test V6 — Nur NUS subscriben (falls V5 nicht hilft)

**Änderung in `connectBLE()`**: nur auf NUS subscriben, Battery weglassen.

```cpp
auto* svc = pClient->getService(NUS_SVC);
if (!svc) { pushLog("Kein NUS!"); return; }
auto* chr = svc->getCharacteristic(NUS_TX);
if (!chr) { pushLog("Kein TX!"); return; }
bool ok = chr->subscribe(true, onAnyNotify, true);
pushLog("Sub NUS: %s", ok ? "OK" : "FAIL");
// KEIN sub auf Battery!
```

**Erwartet**: Falls Battery vorher "den Slot belegte", sollten jetzt NUS-Frames kommen.

---

## Test V7 — `setConnectionParams` VOR connect

```cpp
if (!pClient) {
    pClient = NimBLEDevice::createClient();
    pClient->setClientCallbacks(&clientCB, false);
}
// Direkt mit Slave-Wunschparametern verbinden
pClient->setConnectionParams(16, 32, 0, 400);
pClient->connect(targetAddr);
```

**Hypothese**: iPhone setzt diese Params **im Conn Request selbst** (LL_CONNECTION_PARAM_REQ
im initial Connection Indication), nicht erst nach Connect. NimBLE-Default sind möglicherweise
andere Initial-Params, und der Slave reagiert auf das spätere Conn Param Update zu langsam.

---

## Test V8 — Längerer Delay zwischen Connect und Subscribe

```cpp
pClient->connect(targetAddr);
delay(1000);   // 1s warten bis Slave stable
// dann Subscribe
```

**Hypothese**: Slave-Firmware (nRF52810, alt) braucht Setup-Zeit.

---

## Test V9 — Manueller CCCD-Write mit anderem Modus

```cpp
auto* cccd = chr->getDescriptor(NimBLEUUID((uint16_t)0x2902));

// Schritt 1: erst 0x0000 (alles aus), dann 0x0001
uint16_t off = 0x0000;
cccd->writeValue((uint8_t*)&off, 2, true);
delay(200);
uint16_t on  = 0x0001;
cccd->writeValue((uint8_t*)&on, 2, true);
```

**Hypothese**: Vielleicht muss Reset-Subscribe-Pattern angewandt werden.

---

## Test V10 — Magic-Bytes auf RX

Probiere verschiedene Strings auf `6e400002` (RX, Handle 13) **NACH** Subscribe:

```cpp
auto* rx = svc->getCharacteristic("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
const char* cmds[] = {
    "$\r\n",      // simpler trigger
    "AT\r\n",     // AT command set (Albertronic firmware!)
    "AT?\r\n",
    "AT+RX\r\n",
    "\x02\x03",   // STX/ETX
};
for (auto cmd : cmds) {
    rx->writeValue((uint8_t*)cmd, strlen(cmd), true);
    delay(500);
    // Beobachten ob NUS TX antwortet
}
```

**Achtung**: bei einigen Bytes hatten wir früher Sofort-Disconnect. Vorsichtig testen.

---

## Test V11 — BLE Sniffer Capture

**Hardware**: nRF52840 Dongle mit nRF Sniffer Firmware + Wireshark.

**Schritte**:
1. nRF52840 Dongle mit nRF Sniffer Firmware flashen
2. Wireshark mit nRF-Plugin starten
3. Filter auf 123\TUNE+ MAC `ef:a8:b2:de:e0:9e`
4. Capture starten
5. iPhone-nRF-Connect verbinden + NUS-Subscribe → Capture läuft mit
6. Disconnect, Capture stoppen
7. Capture nochmal mit M5Dial-Connect statt iPhone

**Vergleich**: Welche Pakete schickt iPhone vor dem ersten NUS-Frame, die wir nicht schicken?

---

## Test V12 — Properties richtig dumpen

Bug im aktuellen Code: `prop=00` für alle Chars (hardcoded). Fix:

```cpp
uint8_t props = 0;
if (c->canBroadcast())       props |= 0x01;
if (c->canRead())            props |= 0x02;
if (c->canWriteNoResponse()) props |= 0x04;
if (c->canWrite())           props |= 0x08;
if (c->canNotify())          props |= 0x10;
if (c->canIndicate())        props |= 0x20;
if (c->canWriteSigned())     props |= 0x40;
if (c->hasExtendedProps())   props |= 0x80;
Serial.printf("  CHR %s h=%u prop=%02X\n",
              c->getUUID().toString().c_str(), c->getHandle(), props);
```

Mit korrekten Properties können wir prüfen ob NUS TX `0x90` (notify + auth-required) hat.

---

## Test V13 — `NimBLEConnInfo` nach Connect loggen

```cpp
void onConnect(NimBLEClient* p) override {
    NimBLEConnInfo info = p->getConnInfo();
    Serial.printf("Conn: enc=%d auth=%d bond=%d sec=%d\n",
                  info.isEncrypted(),
                  info.isAuthenticated(),
                  info.isBonded(),
                  info.getSecLevel());
}
```

Zeigt **direkt** ob Connection encrypted ist nach V5-Pairing.

---

## Aufräumen wenn's läuft

Sobald NUS-Frames kommen, Cleanup:

1. Service-Dump entfernen oder hinter Debug-Flag
2. `onAnyNotify` → spezifisch nur auf NUS TX
3. Battery-Subscribe entfernen (wir brauchen es nicht)
4. Heartbeat-Print entfernen oder runter auf 5s Intervall
5. RAW-Log ist im Code drin, per Long-Press toggelbar — gut so
6. MAP (0x32) und CURR (0x35) im Display ergänzen
7. Bonding-Info ins NVS speichern (NimBLE macht das automatisch wenn aktiv)
8. README aktualisieren mit "funktioniert" Status

---

## Git-Workflow für Tests

```powershell
# Branch pro Test
cd D:\_claude\M5stack\m5stack-123
git checkout -b test/V6-only-nus

# Code ändern, bauen, flashen, testen
# Bei Erfolg:
git add src/main.cpp docs/
git commit -m "test V6: only NUS subscribe — funktioniert!"
git push origin test/V6-only-nus

# Bei Misserfolg: Branch dokumentieren und löschen
```
