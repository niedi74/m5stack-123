# M5Stack Dial – BLE Datenanzeige für 123\TUNE+

Echtzeit-Anzeige von Zündung, Drehzahl, Temperatur und Lambda für einen **VW T2b (2L Typ 4, Automatik, luftgekühlt)** – direkt vom 123\TUNE+ Zündverteiler per Bluetooth Low Energy auf einem M5Stack Dial.

---

## Fahrzeug & Hardware

| Komponente | Details |
|---|---|
| Fahrzeug | VW T2b, 2L Typ 4-Motor, Automatikgetriebe, luftgekühlt |
| Anzeige | M5Stack Dial (StampS3 / ESP32-S3, GC9A01 240×240 rund) |
| Zündung | 123\TUNE+ Albertronic BV, Firmware 1.4c |
| Lambda | Bosch LSU 4.9 via Spartan Lambda 3 V2 |
| Geschwindigkeit | Reed-Kontakt an Antriebswelle, 10 Impulse/Umdrehung |

---

## BLE Protokoll – Nordic UART Service (NUS)

| Parameter | Wert |
|---|---|
| Service UUID | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| TX Notify UUID | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| RX Write UUID | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| Gerät MAC | `EF:A8:B2:DE:E0:9E` |
| Verbindung | Kein PIN, kein Handshake – Gerät sendet sofort nach Subscribe |

### Frame-Format

Jeder Frame ist **5 Bytes ASCII**:

```
Byte[0]  = Typ-Byte (siehe Tabelle)
Byte[1]  = High-Nibble als ASCII-Hex-Zeichen  ('0'–'F')
Byte[2]  = Low-Nibble  als ASCII-Hex-Zeichen  ('0'–'F')
Byte[3]  = 0x0D (\r)
Byte[4]  = 0x0A (\n)
```

### Frame-Typen

| Byte[0] | Bedeutung | Einheit | Formel | Bereich |
|---|---|---|---|---|
| `0x30` | Drehzahl (RPM) | 1/min | `hi×800 + lo×50` | 0–12 750 |
| `0x31` | Zündvoreilung | ° KW | `hi×3.2 + lo×0.2` | 0–51.0 |
| `0x32` | Ansaugdruck | kPa | `hi×16 + lo` (Rohwert) | TBD |
| `0x33` | Temperatur | °C | `(hi×16 + lo) − 30` | −30–225 |
| `0x35` | Strom | A | `hi×16 + lo` (Rohwert) | TBD |
| `0x41` | Versorgungsspannung | V | `(hi×16 + lo) ÷ 4.54` | 0–56 V |
| `0x42` | Unbekannt | – | ignorieren | – |
| `0x0D` | Keepalive / \r | – | ignorieren | – |

### Dekodierungsbeispiele (aus nRF Connect Log)

```
Frame: 30 31 34 0D 0A  →  Typ=RPM,  hi=1, lo=4  →  1×800 + 4×50 = 1000 RPM
Frame: 31 30 41 0D 0A  →  Typ=ADV,  hi=0, lo=A  →  0×3.2 + 10×0.2 = 2.0°
Frame: 33 34 36 0D 0A  →  Typ=TEMP, hex=0x46=70  →  70−30 = 40 °C
Frame: 41 35 38 0D 0A  →  Typ=VOLT, hex=0x58=88  →  88÷4.54 ≈ 19.4 V  (→ ×0.659 Teiler)
```

> **Hinweis:** Der Spannungs-Teiler im 123\TUNE+ skaliert den Messbereich auf ca. 0–15 V Bordspannung.

---

## PlatformIO Setup

### Voraussetzungen

- [PlatformIO IDE](https://platformio.org/) oder PlatformIO Core CLI
- Python 3.x

### Installation

```bash
git clone https://github.com/niedi74/m5stack-123.git
cd m5stack-123
git checkout claude/m5stack-ble-nus-client-oqxww
pio run -t upload
pio device monitor
```

### `platformio.ini` – Schlüsselparameter

```ini
board    = m5stack-stamps3   # StampS3 / ESP32-S3
platform = espressif32
framework = arduino

lib_deps =
    h2zero/NimBLE-Arduino @ ^1.4.3
    m5stack/M5GFX         @ ^0.2.0
```

### Display-Pinout (GC9A01, manuell konfiguriert)

| Signal | GPIO |
|---|---|
| SPI SCK | 6 |
| SPI MOSI | 5 |
| DC | 4 |
| CS | 7 |
| RST | 8 |
| Backlight | 9 |
| Encoder Button | 42 |

---

## Anzeige-Bedienung

| Aktion | Funktion |
|---|---|
| Encoder-Knopf kurz drücken | Seiten `ADV`, `T/V`, `SETTINGS`, `TUNE` wechseln |
| Display antippen | Standardmäßig deaktiviert; optional nur Wechsel `ADV` / `T/V` |
| `SETTINGS` | Töne (ab Werk `OFF`), Touch-Navigation und Helligkeit einstellen |
| `SETTINGS` -> `Demo mode` | Im Stand simulierte Live-Werte und Bedienung testen |
| **Hauptansicht** | Zündvoreilung (orange, oben) + RPM (weiß, unten) |
| **Aux-Ansicht** | Temperatur (cyan, oben) + Spannung (gelb, unten) |
| BLE-Status oben links | grün = verbunden, rot = Suche läuft |

Im Fahrbetrieb gilt: Wenn bei mehr als `650 RPM` noch kein Home-WLAN
verbunden ist, schaltet die Firmware WLAN und Setup-AP still aus. Dadurch
stören keine WiFi-Retry-Meldungen die Anzeige während der Fahrt.

### Demo-Modus fuer Standtests

Im Menue `SETTINGS` kann `Demo mode` per Long-Press eingeschaltet werden.
Das Display kennzeichnet ihn deutlich mit `DEMO` und `SIM TEST` und zeigt
auf der `ADV`-Hauptseite simulierte Werte fuer RPM, Zuendung, MAP,
Temperatur und Spannung. Beim Ausschalten kehrt es ebenfalls zu `ADV` zurueck.

- Die `TUNE`-Seite kann im Demo-Modus inklusive ARM, Start, +/- und Exit
  ausprobiert werden, sendet dabei aber keine Befehle an die 123Tune+.
- Simulierte Werte werden nicht in das Fahrtlog geschrieben und loesen keine
  WiFi-Abschaltung wegen hoher RPM aus.
- Der Modus ist nicht dauerhaft gespeichert, endet bei einem echten
  BLE-Connect und ist nach einem Neustart wieder `OFF`.
- Fuer Diagnose per USB stehen `demo_on`, `demo_status` und `demo_off` bereit.

---

## Reifen & Geschwindigkeit

Der Reed-Kontakt sitzt an der Antriebswelle und erzeugt **10 Impulse pro Umdrehung**.
Der Reifenumfang ist konfigurierbar und lässt sich per GPS kalibrieren.

| Reifengröße | Umfang |
|---|---|
| 185/80 R14 | 1 910 mm |
| 205/80 R14 | 2 155 mm |

```
Geschwindigkeit [km/h] = (Impulse/s ÷ 10) × Umfang [m] × 3.6
```

---

## Zündkurven-Versionen (`tuning/`)

Der Ordner [`tuning/`](tuning/) enthält Snapshots der jeweils eingestellten
Zentrifugal- und MAP-Kurven, benannt nach Datum und Fahrzustand.
Aktuelle Einstellung: [`tuning/kurven_aktuell.md`](tuning/kurven_aktuell.md)

---

## Interface-Hardware – Übersicht

### Optokoppler (galvanische Trennung 12V-Bordelektrik → 3,3V ESP32)

Optokoppler werden ausschließlich für **digitale KFZ-Signale** eingesetzt –
nicht für Datenbusse.

| Signal | Richtung | Beschreibung |
|---|---|---|
| Bremslicht | Eingang | Bremse betätigt (Tempomat-Abbruch) |
| N/P-Kontakt | Eingang | Neutral / Parken erkannt (Automatik) |
| SET / RES | Eingang | Hella-Tempomat-Bedieneinheit, Set + Resume |
| Zündplus-Status | Eingang (optional) | Zündung EIN/AUS erkennen (Wake/Sleep) |
| Tempomat-Ausgang | Ausgang | Ansteuerung bestehender Hella-Eingang |

> Typisches Bauteil: PC817 oder TLP291 – Vorwiderstand auf 12V-Seite ca. 680 Ω

### CAN-Bus – SN65HVD230

Für die CAN-Bus-Kommunikation zwischen ESP32-Knoten wird **kein Optokoppler**
verwendet, sondern ein dedizierter CAN-Transceiver.

| Parameter | Details |
|---|---|
| Chip | **SN65HVD230** (Texas Instruments, 3,3V-kompatibel) |
| Schnittstelle | CANH / CANL (differenziell, 120 Ω Abschlusswiderstand je Ende) |
| ESP32-Anbindung | TX → GPIO, RX → GPIO, TWAI-Peripheral des ESP32 |
| Baudrate | 500 kBit/s (konfigurierbar) |
| Buszugang | Keine Galvanik-Isolation nötig, da alle Knoten gemeinsame Masse |

---

## Entwicklungs-Roadmap

### Phase 1 – BLE Display ✅
- NimBLE Client, NUS Subscribe
- Frame-Dekodierung RPM / Advance / Temp / Volt
- GC9A01 Echtzeit-Anzeige, Encoder-Ansichtswechsel

### Phase 2 – Lambda-Anzeige (Spartan 3 V2)
- UART-Auslese vom Spartan 3 V2 Controller
- Fallback: Analogeingang 0–5 V → AFR / Lambda
- Anzeige auf Dial-Display

### Phase 3 – Geschwindigkeit & Fahrstufenerkennung
- Reed-Kontakt (10 Impulse/Umdrehung) → ISR-Zähler
- Geschwindigkeit berechnen, GPS-Kalibrierung
- Fahrstufenerkennung Automatik aus Impuls-Muster / Übersetzungsverhältnis

### Phase 4 – Datenlogging
- SD-Karte: CSV-Log aller Kanäle mit Zeitstempel
- Konfigurierbares Log-Intervall
- Fahrt-Sessions mit Start/Stop per Encoder

### Phase 5 – CAN-Bus & Multi-ESP32
- **SN65HVD230** CAN-Transceiver an jedem ESP32-Knoten
- CAN-Bus als Backbone (TWAI-Peripheral, 500 kBit/s)
- Zentraler Datenbroker, verteilte Sensor-Knoten
- OTA-Updates über CAN oder WiFi

### Phase 6 – Tempomat (Cruise Control via Hella-Einheit)
- **Optokoppler-Eingänge:** Bremssignal, N/P-Kontakt, SET / RES der Hella-Bedieneinheit
- **Optokoppler-Ausgang:** Ansteuerung des Hella-Tempomat-Eingangs
- Optional: Zündplus-Status per Optokoppler für Wake/Sleep
- Kein Optokoppler am CAN-Bus

### Phase 7 – Erweiterte Sensorik
- 2. Öldrucksensor + Öltemperatur
- Zylinderkopf-Temperatursensor (CHT)
- Alle Kanäle im Datenlog und auf zusätzlichen Display-Seiten

---

## Lizenz

MIT – Privatprojekt, keine Gewährleistung.

---

## Offizielle M5Dial-Dokumentation

- [M5Dial Hardware, PinMap und Schaltplan](https://docs.m5stack.com/en/core/M5Dial)
- [M5Dial Arduino Quick Start](https://docs.m5stack.com/en/arduino/m5dial/program)
- [M5Dial Buzzer API und Beispiel](https://docs.m5stack.com/en/arduino/m5dial/buzzer)
- [M5Dial offizielle Arduino-Bibliothek](https://github.com/m5stack/M5Dial)

Die offiziellen Dokumente werden verlinkt und nicht als Fremddateien in dieses
Repository kopiert. Dadurch bleibt die jeweils aktuelle Herstellerfassung die
Referenz.

## Release-Staende

Die nachpruefbare Entwicklung von Logger, WebGUI und Bedienoberflaeche ist in
[docs/RELEASES_2026-05-23.md](docs/RELEASES_2026-05-23.md) beschrieben.
