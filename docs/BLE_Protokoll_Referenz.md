# 123\TUNE+ BLE Protokoll — Referenzdokument

Erstellt aus nRF Connect Log, aufgenommen **2026-05-20 18:35–18:36 Uhr**  
Gerät: 123\TUNE+ MAC `EF:A8:B2:DE:E0:9E`  
Fahrzeug: VW T2b, 2L Typ 4, Automatik, luftgekühlt

---

## 1. Geräteinformationen

| Eigenschaft | Wert |
|---|---|
| Gerätename | `123\TUNE+` |
| MAC-Adresse | `EF:A8:B2:DE:E0:9E` |
| Hersteller | Albertronic BV (Company ID `0x091A`) |
| Chip | nRF52810 UART AT Command |
| Firmware | `version: 1.4c(Albertronic BV)` |
| Advertising-Typ | Legacy BLE 4.1 |
| Advertising-Intervall | ~103 ms |
| TX Power | 4 dBm |
| Battery Level | 48% (bei Aufnahme) |
| Bonding | Nicht erforderlich (NOT BONDED) |
| Verschlüsselung | Keine |

---

## 2. Verbindungsparameter

| Parameter | Wert |
|---|---|
| Transport | LE 1M PHY |
| Connection Interval (initial) | 7.5 ms |
| Connection Interval (stabil) | 30.0 ms |
| Max Latency | 0 |
| Supervision Timeout | 4000–5000 ms |
| Verbindungszeit bis Daten | ~2 Sekunden |

---

## 3. GATT Services & Characteristics

### 3.1 Generic Access (`0x1800`)

| Characteristic | UUID | Properties | Wert |
|---|---|---|---|
| Device Name | `0x2A00` | READ, WRITE | `123\TUNE+` |
| Appearance | `0x2A01` | READ | `[0]` Unknown |
| Preferred Connection Params | `0x2A04` | READ | 20–40 ms, Latency 0, Timeout 400 |
| Central Address Resolution | `0x2AA6` | READ | Supported |

### 3.2 Generic Attribute (`0x1801`)

Leer.

### 3.3 Nordic UART Service — NUS ⭐ HAUPTSERVICE

| Characteristic | UUID | Properties | Funktion |
|---|---|---|---|
| RX | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | WRITE, WRITE NO RESPONSE | Kommandos → Gerät |
| TX | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | NOTIFY | Live-Daten ← Gerät |

> **Wichtig:** Kein Handshake, kein PIN, keine Authentifizierung nötig.  
> Nach Subscribe auf TX kommen sofort Live-Daten. Kein Write auf RX erforderlich.

### 3.4 Tx Power (`0x1804`)

| Characteristic | UUID | Properties | Wert |
|---|---|---|---|
| Tx Power Level | `0x2A07` | READ | 4 dBm |

### 3.5 Device Information (`0x180A`)

| Characteristic | UUID | Properties | Wert |
|---|---|---|---|
| Manufacturer Name | `0x2A29` | READ | `nRF52810 UART AT Command` |
| Serial Number | `0x2A25` | READ | `no data!` |
| Firmware Revision | `0x2A26` | READ | `version: 1.4c(Albertronic BV)` |

### 3.6 Battery Service (`0x180F`)

| Characteristic | UUID | Properties | Wert |
|---|---|---|---|
| Battery Level | `0x2A19` | NOTIFY, READ | 48% (`0x64` = 100% max) |

---

## 4. Datenformat — TX Notify Frames

### 4.1 Frame-Struktur

Jeder Frame ist **5 Bytes**:

```
Byte[0]  Typ-Byte       0x30 / 0x31 / 0x32 / 0x33 / 0x35 / 0x41 / 0x42 / 0x0D
Byte[1]  High-Nibble    ASCII-Hex-Zeichen  '0'–'9', 'A'–'F'
Byte[2]  Low-Nibble     ASCII-Hex-Zeichen  '0'–'9', 'A'–'F'
Byte[3]  Checksum       (Byte[0] + Byte[1] + Byte[2]) - 0x50
Byte[4]  Terminator     0x20 (Space) bei normalen Frames, 0x0D beim 0x42-Frame
```

**Dekodierung der Nibbles (Python):**

```python
b1 = int(chr(data[1]), 16)   # High-Nibble: ASCII-Zeichen → Hex-Wert
b2 = int(chr(data[2]), 16)   # Low-Nibble
raw = int(chr(data[1]) + chr(data[2]), 16)  # beide Nibbles als 1-Byte-Hex
```

**Dekodierung (C++ / Arduino):**

```cpp
auto hexnib = [](uint8_t c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
};
int hi  = hexnib(data[1]);
int lo  = hexnib(data[2]);
int raw = (hi << 4) | lo;
```

### 4.2 Frame-Typen

| Byte[0] | Typ | Einheit | Dekodierungsformel |
|---|---|---|---|
| `0x30` | RPM | U/min | `hi * 800 + lo * 50` |
| `0x31` | Zündwinkel (Advance) | ° KW | `hi * 3.2 + lo * 0.2` |
| `0x32` | Absolutdruck (MAP) | kPa | `(hi << 4) \| lo` |
| `0x33` | Temperatur | °C | `((hi << 4) \| lo) - 30` |
| `0x35` | Zündstrom | A | `((hi << 4) \| lo) / 8.65` |
| `0x41` | Spannung | V | `((hi << 4) \| lo) / 4.54` |
| `0x42` | Unbekannt | ? | Konstant 70 — vermutlich Konfigurationswert |
| `0x0D` | Keepalive/Separator | — | Ignorieren |

### 4.3 Checksum-Validierung

Die Checksum in Byte[3] erlaubt optionale Frame-Validierung:

```cpp
bool frameValid(const uint8_t* data) {
    return data[3] == (uint8_t)((data[0] + data[1] + data[2]) - 0x50);
}
```

Verifiziert an echten Log-Frames:

| Frame (Hex) | B0+B1+B2 | B0+B1+B2−0x50 | Byte[3] | OK? |
|---|---|---|---|---|
| `30 31 37 48 20` | 0x98 | 0x48 = `'H'` | `0x48` | ✓ |
| `30 31 36 47 20` | 0x97 | 0x47 = `'G'` | `0x47` | ✓ |
| `31 34 43 58 20` | 0xA8 | 0x58 = `'X'` | `0x58` | ✓ |
| `31 35 30 46 20` | 0x96 | 0x46 = `'F'` | `0x46` | ✓ |
| `32 36 34 4C 20` | 0x9C | 0x4C = `'L'` | `0x4C` | ✓ |
| `33 33 44 5A 20` | 0xAA | 0x5A = `'Z'` | `0x5A` | ✓ |
| `41 34 30 55 20` | 0xA5 | 0x55 = `'U'` | `0x55` | ✓ |
| `42 34 36 5C 0D` | 0xAC | 0x5C = `'\'` | `0x5C` | ✓ |

---

## 5. Echte Messwerte aus dem Log

Motor läuft, Leerlauf ~1100 RPM, Motor kalt/warm werdend.

| Frame (Raw) | Hex | Typ | Berechnung | Ergebnis |
|---|---|---|---|---|
| `017H ` | `30-31-37-48-20` | RPM | 1×800 + 7×50 | **1150 U/min** |
| `016G ` | `30-31-36-47-20` | RPM | 1×800 + 6×50 | **1100 U/min** |
| `14CX ` | `31-34-43-58-20` | Advance | 4×3.2 + 12×0.2 | **15.2°** |
| `150F ` | `31-35-30-46-20` | Advance | 5×3.2 + 0×0.2 | **16.0°** |
| `264L ` | `32-36-34-4C-20` | Druck | 0x64 = 100 | **100 kPa** |
| `33DZ ` | `33-33-44-5A-20` | Temp | 0x3D=61 − 30 | **31 °C** |
| `33E[ ` | `33-33-45-5B-20` | Temp | 0x3E=62 − 30 | **32 °C** |
| `33F\` | `33-33-46-5C-20` | Temp | 0x3F=63 − 30 | **33 °C** |
| `51DZ ` | `35-31-44-5A-20` | Strom | 0x1D=29 ÷ 8.65 | **~3.4 A** |
| `A40U ` | `41-34-30-55-20` | Spannung | 0x40=64 ÷ 4.54 | **14.1 V** |
| `B46\` | `42-34-36-5C-0D` | Unbekannt | 0x46 = 70 | **? konstant** |

---

## 6. Datenrate & Timing

- Frames kommen kontinuierlich, ca. **8–12 Frames/Sekunde**
- Jeder Sensor wird ca. alle **500–800 ms** aktualisiert
- Reihenfolge pro Zyklus: `RPM → Advance → Druck → Temp → Strom → Spannung → 0x42`
- `0x42`-Frame kommt immer mit `0x0D`-Terminator = Ende eines vollständigen Zyklus
- Verbindungstimeout nach ~**90 Sekunden Inaktivität** (GATT CONN TIMEOUT Error 8)

---

## 7. Verbindungsaufbau (aus Log)

```
1. BLE Scan starten (Active Scan)
2. Advertiser mit Name "123\TUNE+" oder MAC EF:A8:B2:DE:E0:9E gefunden
3. connectGATT() → Connection Interval verhandelt (7.5ms → 30ms)
4. Service Discovery: NUS Service 6e400001-... gefunden
5. TX Characteristic 6e400003-... → NOTIFY aktivieren (CCCD schreiben: 0x0100)
6. Gerät sendet sofort Daten — KEIN Write auf RX nötig!
```

**Kritisch:** Nur **eine Verbindung gleichzeitig** möglich. Wenn die offizielle
123\TUNE+ App verbunden ist, schlägt jede weitere Verbindung fehl.

---

## 8. NimBLE-Arduino Implementierung

```cpp
// platformio.ini:
// lib_deps = h2zero/NimBLE-Arduino @ ^1.4.3

#include <NimBLEDevice.h>

static const char* NUS_SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char* NUS_TX  = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
static const char* TARGET  = "ef:a8:b2:de:e0:9e";  // Kleinbuchstaben!

// ── Scan: Gerät anhand MAC finden ────────────────────────────────────────────
class ScanCB : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        String addr = dev->getAddress().toString().c_str();
        addr.toLowerCase();
        if (addr == TARGET) {
            NimBLEDevice::getScan()->stop();
            // Adresse für connect() speichern
        }
    }
};

// ── Verbinden & Notify aktivieren ────────────────────────────────────────────
bool connectAndSubscribe(NimBLEAddress addr) {
    NimBLEClient* client = NimBLEDevice::createClient();
    if (!client->connect(addr)) return false;

    NimBLERemoteService* svc = client->getService(NUS_SVC);
    if (!svc) { client->disconnect(); return false; }

    NimBLERemoteCharacteristic* chr = svc->getCharacteristic(NUS_TX);
    if (!chr || !chr->canNotify()) { client->disconnect(); return false; }

    // Callback für eingehende Notify-Frames registrieren
    // Kein Write auf NUS_RX nötig!
    chr->registerForNotify([](NimBLERemoteCharacteristic*, uint8_t* data,
                               size_t len, bool) {
        if (len < 3) return;
        int hi  = 0, lo = 0;
        auto h = [](uint8_t c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        hi = h(data[1]);  lo = h(data[2]);
        switch (data[0]) {
            case 0x30: Serial.printf("RPM:  %d\n",   hi*800 + lo*50);      break;
            case 0x31: Serial.printf("ADV:  %.1f°\n", hi*3.2f + lo*0.2f);  break;
            case 0x32: Serial.printf("MAP:  %d kPa\n", (hi<<4)|lo);        break;
            case 0x33: Serial.printf("TEMP: %d°C\n",  ((hi<<4)|lo) - 30);  break;
            case 0x35: Serial.printf("CURR: %.1f A\n", ((hi<<4)|lo)/8.65f);break;
            case 0x41: Serial.printf("VOLT: %.2f V\n", ((hi<<4)|lo)/4.54f);break;
        }
    });
    return true;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    NimBLEDevice::init("ESP32-NUS-Client");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    auto* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new ScanCB());
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->start(0, nullptr, false);  // 0 = unbegrenzt scannen
}
```

> **Hinweis für PlatformIO / VS Code:**  
> `NimBLEAdvertisedDeviceCallbacks` ist die v1.x-API.  
> In NimBLE-Arduino **v2.x** heißt die Basisklasse `NimBLEScanCallbacks` mit  
> `onResult(const NimBLEAdvertisedDevice* dev)`.  
> Pin auf `^1.4.3` in `platformio.ini` vermeidet den API-Wechsel.

---

## 9. Bekannte Eigenheiten

| # | Eigenheit |
|---|---|
| 1 | **NOT BONDED** — Gerät speichert keine Verbindungen, jedes Mal neu verbinden |
| 2 | **Nur eine Verbindung** gleichzeitig — App verbunden = nRF Connect schlägt fehl |
| 3 | **Sicherheitslücke** (bekannt seit 2017): Kein Schutz gegen unautorisierten RX-Zugriff |
| 4 | **0x42 Frame** konstant Wert 70 im Leerlauf — noch zu prüfen ob sich bei Last ändert |
| 5 | **Timeout** nach ~90 Sekunden Inaktivität (GATT CONN TIMEOUT Error 8) |
| 6 | **MAC-Adresse** ist ein statischer Random-Address-Typ (MSB `0xEF` → Bits 7:6 = `11`) |

---

## 10. Advertising Payload

```
AD Type 0xFF  Manufacturer Specific Data:
  Company ID:  0x091A  (Albertronic BV)
  Payload:     00 05 50 6C
                        ^^^^ vermutlich Seriennummer / Geräte-ID

Beispiel vollständiger Raw-Payload (aus nRF Connect History):
  02 01 06                       Flags: LE General Discoverable, BR/EDR not supported
  0A 09 31 32 33 5C 54 55 4E 45 2B   Complete Local Name: "123\TUNE+"
  05 FF 1A 09 00 05 50 6C        Manufacturer Data: ID=0x091A, data=0x0005506C
  02 0A 04                       TX Power Level: 4 dBm
```

---

## Anhang: Checksum-Formel (reverse-engineered)

```
checksum = (Byte[0] + Byte[1] + Byte[2]) - 0x50
```

Für einfaches Logging nicht benötigt — nur zur Frame-Validierung.

---

*Protokoll reverse-engineered via nRF Connect for Mobile, 2026-05-20*  
*Referenz: [iafilius/123Tune-plus-Simulator](https://github.com/iafilius/123Tune-plus-Simulator) (ältere Firmware-Version, anderes Protokoll)*
