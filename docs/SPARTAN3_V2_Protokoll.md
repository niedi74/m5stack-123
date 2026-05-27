# Spartan 3 V2 — Protokoll-Referenz

Quelle: 14point7 Spartan 3 v2 User Manual, Jan 21 2026  
Gerät: 14point7 Spartan 3 V2 Wideband Lambda Controller (LSU 4.9)

---

## 1. Kabelfarben & Anschlüsse

| Farbe | Name | Anschluss | Hinweis |
|---|---|---|---|
| **Rot** | Power | 12V geschaltet | Sicherung 5A, Kraftstoffpumpen-Relais empfohlen |
| **Schwarz** | Electronics Ground | Masse | Dort wo Interfacing-Gerät geerdet ist |
| **Weiß** | Heater Ground | Masse | Motorblock oder Karosserie |
| **Grün** | High Performance Analog Output | ECU / Gauge / Logger | 0–5V linear, Standard: 0.68–1.36 Lambda |
| **Braun** | Standard Performance Analog Output | ECU / Gauge | Standard: Heizstatus-Anzeige |
| **Blau** | CAN High | SN65HVD230 CANH | |
| **Lila** | CAN Low | SN65HVD230 CANL | |
| **Orange** | UART TX | ESP32 RX (GPIO16) | „Rx-Orange" am USB-Adapter |
| **Gelb** | UART RX | ESP32 TX (GPIO17) | „Tx-Yellow" am USB-Adapter |
| **Grau** | UART Ground | GND | „Gnd-Grey" am USB-Adapter |

> ⚠️ **Bootloader-Falle:** Wenn beim Einschalten das **weiße Kabel (Heater Ground) NICHT** verbunden ist, startet Spartan im Bootloader-Modus statt normal!  
> ⚠️ **Lambda-Sonde** muss eingesteckt sein, sonst startet Spartan nicht.

---

## 2. CAN Bus Protokoll

### Standardeinstellungen

| Parameter | Wert |
|---|---|
| Baud Rate | **500 kbit/s** |
| Adressierung | 11-bit |
| CAN ID | **1024 (0x400)** |
| DLC | 4 Bytes |
| Senderate | 50 Hz (alle 20 ms) |
| Byte-Reihenfolge | Big-endian |
| Terminierungswiderstand | Aktiviert (intern) |

### Paketformat (Standard / Lambda)

```
Data[0]  Lambda × 1000, High-Byte
Data[1]  Lambda × 1000, Low-Byte
Data[2]  Sensortemperatur ÷ 10
Data[3]  Status

Lambda  = (Data[0] << 8 | Data[1]) / 1000.0
AFR     = Lambda × 14.7          (Benzin)
Temp°C  = Data[2] × 10
```

### Status-Codes

| Wert | Bedeutung |
|---|---|
| 0 | Reserviert |
| 1 | Warte auf Trigger vor Aufheizung |
| 2 | Sensor heizt auf |
| **3** | **Normalbetrieb — Daten gültig** |
| 4+ | Reserviert |

### ESP32 CAN-Empfang (TWAI)

```cpp
#include <driver/twai.h>

void setup() {
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_5, GPIO_NUM_4,
                                                           TWAI_MODE_NORMAL);
    twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    twai_driver_install(&g, &t, &f);
    twai_start();
}

void loop() {
    twai_message_t msg;
    if (twai_receive(&msg, pdMS_TO_TICKS(100)) == ESP_OK) {
        if (msg.identifier == 1024 && msg.data_length_code == 4) {
            float lambda = ((msg.data[0] << 8) | msg.data[1]) / 1000.0f;
            float afr    = lambda * 14.7f;
            int   temp   = msg.data[2] * 10;
            int   status = msg.data[3];
            Serial.printf("Lambda=%.3f  AFR=%.1f  Temp=%d°C  Status=%d\n",
                          lambda, afr, temp, status);
        }
    }
}
```

---

## 3. UART / Serielle Befehle

**Baud Rate: 9600, 8N1, ASCII (Groß-/Kleinschreibung egal)**

```cpp
HardwareSerial spartan(2);
spartan.begin(9600, SERIAL_8N1, 16, 17);  // RX=GPIO16, TX=GPIO17
spartan.println("GETHW");                 // Firmware-Version abfragen
```

### Wichtige Befehle

| Befehl | Funktion | Standard |
|---|---|---|
| `GETHW` | Hardware-Version lesen | — |
| `GETFW` | Firmware-Version lesen | — |
| `SETTYPEx` | Sensor-Typ: 0=LSU4.9, 1=LSU ADV | 0 |
| `SETCANID1024` | CAN-ID setzen | 1024 |
| `SETCANBAUD500000` | CAN-Baud setzen | 500000 |
| `SETCANR1` | CAN-Terminierung ein | 1 (ein) |
| `SETCANFORMAT0` | CAN-Format: 0=Standard Lambda | 0 |
| `SETCANDR50` | CAN-Datenrate in Hz | 50 |
| `SETPERF1` | Performance: 0=20ms, 1=10ms, 2=lean | 1 |
| `SETLAMFIVEV1.36` | Lambda bei 5V (Analogausgang) | 1.36 |
| `SETLAMZEROV0.68` | Lambda bei 0V (Analogausgang) | 0.68 |
| `SETNBMODE0` | Brauner Ausgang: 0=Simulated NB, 2=Heizstatus | 0 |
| `MEMRESET` | Werkseinstellungen zurücksetzen | — |

---

## 4. Analogausgang (Grünes Kabel)

**High Performance Linear Output — 0 bis 5V**

| Spannung | Lambda | AFR (Benzin) |
|---|---|---|
| 0.0 V | 0.68 | 10.0 |
| 1.25 V | 0.85 | 12.5 |
| 2.50 V | 1.02 | 15.0 |
| 3.75 V | 1.19 | 17.5 |
| 5.00 V | 1.36 | 20.0 |

Formel: `Lambda = 0.68 + (volt_5v / 5.0) * 0.68`

### Anschluss an ESP32 (Spannungsteiler 0–5V → 0–3.3V)

```
Grünes Kabel ──┬── 10 kΩ ──┬── GPIO34 (ADC)
               │            │
              GND         33 kΩ
                            │
                           GND
```

Skalierung: 5V × 33/(10+33) = **3.84V** → passt für ESP32 (max 3.3V, Spannungsteiler gibt max 3.1V bei Lambda 1.36)

```cpp
float readLambdaAnalog(int pin) {
    int   raw    = analogRead(pin);
    float volt   = raw * 3.3f / 4095.0f;          // ADC-Spannung
    float volt5  = volt * (10.0f + 33.0f) / 33.0f; // zurück auf 0–5V
    return 0.68f + (volt5 / 5.0f) * 0.68f;         // Lambda
}
```

---

## 5. Unterstützte CAN-Formate

| Format | Befehl | CAN-ID | Baud |
|---|---|---|---|
| Standard Lambda (Default) | `SETCANFORMAT0` | 1024 | 500k |
| Link ECU | `SETCANFORMAT1` | 950 | 1Mbit |
| Adaptronic ECU | `SETCANFORMAT2` | 1024 | 1Mbit |
| MegaSquirt 3 | `SETCANFORMAT0` | 1024 | 500k |
| Haltech (emul. WBC1) | `SETCANFORMAT3` | — | 1Mbit |
| Extended CAN | `SETCANFORMAT5` | 1024 | 500k |

---

## 6. Anschlussschema ESP32 ↔ Spartan 3 V2

```
Spartan 3 V2          ESP32 38P Dev Board
  Blau   (CAN-H) ──── SN65HVD230 CANH
  Lila   (CAN-L) ──── SN65HVD230 CANL
  Grau   (GND)   ──── GND
  Orange (TX)    ──── GPIO16 (RX2)         ← optional UART
  Gelb   (RX)    ──── GPIO17 (TX2)         ← optional UART
  Grün   (Analog)──── 10kΩ/33kΩ Teiler ── GPIO34 (ADC) ← optional

SN65HVD230             ESP32
  TX   ──────────────── GPIO5 (CAN TX)
  RX   ──────────────── GPIO4 (CAN RX)
  VCC  ──────────────── 3.3V
  GND  ──────────────── GND
```

> **CAN-Terminierung:** Spartan hat internen 120Ω-Widerstand (Standard: ein).  
> ESP32-Seite braucht ebenfalls 120Ω zwischen CAN-H und CAN-L wenn kein weiteres Gerät am Bus ist.  
> Viele SN65HVD230-Module haben diesen Widerstand als Jumper oder SMD bereits drauf.

---

*Erstellt: 2026-05-27 — Quelle: 14point7 Spartan 3 v2 User Manual Jan 21 2026*
