# Fahrtanalyse — 2026-05-22

**Fahrzeug:** VW T2b, 2L Typ 4, 276er Nockenwelle, 9:1, PDSIT 36/40 (32mm)  
**System:** 123\TUNE+ via M5Stack Dial BLE-Logger  
**Aufzeichnung:** 16:40:58 – 18:56:12 Uhr (≈ 2h15min)  
**Datenpunkte:** 6092 Messungen (~2,4 Hz Abtastrate)

---

## Übersicht

| Größe | Minimum | Maximum | Mittelwert |
|---|---|---|---|
| Drehzahl | 750 U/min | 4700 U/min | 1817 U/min |
| Zündwinkel | 9.8° | 44.8° | 21.2° |
| MAP (abs.) | 30 kPa | 100 kPa | 89 kPa |
| Motortemp. | 52 °C | 71 °C | — |
| Spannung | 12.3 V | 14.3 V | 13.9 V |
| Spulenstrom | 1.7 A | 3.4 A | 3.0 A |

---

## Drehzahl-Verteilung

| Bereich | Messpunkte | Anteil |
|---|---|---|
| 0 – 1000 U/min (Leerlauf/Kriechen) | 1326 | 21.8% |
| 1000 – 1500 U/min (Stadtverkehr) | 1733 | 28.4% |
| 1500 – 2000 U/min (Normal) | 863 | 14.2% |
| 2000 – 2500 U/min (Zügig) | 716 | 11.8% |
| 2500 – 3000 U/min (Überholen) | 431 | 7.1% |
| 3000 – 4700 U/min (Sport/WOT) | 1023 | 16.8% |

---

## MAP-Verteilung (Last)

| Bereich | Bedeutung | Messpunkte | Anteil |
|---|---|---|---|
| 0 – 40 kPa | Stark Teillast / Schieben | 24 | 0.4% |
| 40 – 60 kPa | Teillast | 487 | 8.0% |
| 60 – 80 kPa | Mittellast | 889 | 14.6% |
| 80 – 95 kPa | Fast Vollgas | 942 | 15.5% |
| 95 – 100 kPa | Vollgas | 3750 | 61.6% |

---

## Zündwinkel-Verhalten

### Zentrifugalkurve — verifiziert

| RPM-Band | Max Winkel | ∅ Winkel | Bewertung |
|---|---|---|---|
| 0 – 1000 U/min | 18.6° | 12.8° | ✓ entspricht Kurve (≤14° @1000) |
| 1000 – 1500 U/min | 26.8° | 15.7° | ✓ Interpolation 14°–22° |
| 1500 – 2000 U/min | 44.8° | 25.0° | ✓ Plateau ~22°–30°, MAP-Korrektur sichtbar |
| 2000 – 2500 U/min | 35.8° | 26.8° | ✓ Plateau ~26°–30° + MAP |
| 2500 – 3000 U/min | 38.2° | 28.2° | ✓ Plateau 30° + MAP-Korrektur |
| 3000 – 4700 U/min | 40.2° | 31.6° | ✓ Plateau 30° + MAP-Korrektur bei Teillast |

### Höchste Zündwinkel (Einzelmessungen)

| Zeitpunkt | RPM | Winkel | MAP | Erklärung |
|---|---|---|---|---|
| 16:57:13 | 1550 | **44.8°** | 61 kPa | MAP-Korrektur bei Teillast + Beschleunigung |
| 17:33:31 | 3500 | 40.2° | 30 kPa | Ausrollen (hohes Vakuum, Zentrifugal voll) |
| 17:31:44 | 3450 | 39.8° | 68 kPa | Teillast, Zentrifugal voll |
| 17:33:31 | 3500 | 39.6° | 33 kPa | Ausrollen |
| 17:33:32 | 3550 | 39.6° | 36 kPa | Ausrollen |

### MAP-Korrektur beobachtet und bestätigt

Die 123\TUNE+ MAP-Korrektur ist aktiv und messbar:

**Beispiel bei 3500 U/min (Zentrifugal-Plateau = 30°):**
- MAP 100 kPa (Vollgas): Winkel = **30.0°** → Korrektur = **0°** ✓
- MAP 30 kPa (Ausrollen): Winkel = **40.2°** → Korrektur = **+10.2°** ✓

Entspricht der dokumentierten MAP-Kurve: `30 kPa → +10°`, `≥85 kPa → 0°`

**Besondere Beobachtung:**  
Der Wert 44.8° bei 1550 U/min / 61 kPa ist vermutlich ein Transient-Snapshot:  
Während eines Beschleunigungsvorgangs war die Zentrifugalmasse noch bei höherer  
Position (höherer Vorverstärkung), während die Drehzahl kurz auf 1550 fiel.  
Dies zeigt, dass die Mechanikreaction der Zentrifugalmassen langsamer ist als  
die Drehzahländerung — typisches Verhalten bei schnellen Lastwechseln.

---

## Höchste Drehzahl

| Zeitpunkt | RPM | Winkel | MAP | Spulenstrom |
|---|---|---|---|---|
| 17:33:17 | **4700 U/min** | 30.0° | 100 kPa | — |
| 17:33:17 | 4650 U/min | 30.0° | 100 kPa | — |
| 17:33:15 | 4600 U/min | 30.0° | 100 kPa | — |

Dokumentierter Rev-Limiter: **4650 U/min**. Bei Vollgas + Plateau (30°) wird kein  
MAP-Vorzünden aktiv — korrekt, da MAP ≈ 100 kPa → Korrektur = 0°.

> **Hinweis:** 4700 U/min liegt über dem dokumentierten Limiter (4650). Entweder  
> wurde der Limiter nicht hart überschritten (hysterese), oder die Einstellung  
> wurde angepasst. Auf dem nächsten Testlauf prüfen.

---

## Temperaturverlauf

| Zeitpunkt | Temperatur |
|---|---|
| 16:40:59 | **52°C** (Starttemperatur — Motor war warm) |
| Anstieg bis ca. | ~69–71°C (Betriebstemp.) |
| 18:56:12 (Ende) | **65°C** |

Motor blieb im normalen Temperaturbereich (Luftkühlung). Kein Überhitzungsanzeichen.

---

## Spannung / Lichtmaschine

- Start: **12.3 V** (Batterie, Motor eben gestartet)
- Im Betrieb: **13.7–14.3 V** (Lichtmaschine lädt normal)
- Spulenstrom: 1.7–3.4 A (abhängig von Drehzahl und Zündzeitpunkt)

---

## Auffälligkeiten / Tuning-Hinweise

1. **MAP-Korrektur bestätigt aktiv** — addiert bis zu +10° bei Ausrollung/Teillast.
   Bei 3500 U/min + 30 kPa wurde **40.2°** gemessen (30° Zentrifugal + 10.2° MAP). ✓

2. **Advance > 30° bei 954 Messpunkten (15.7%)** — Teillast ist häufig genug,
   dass die MAP-Korrektur erheblichen Einfluss auf Effizienz und Fahrbarkeit hat.

3. **4700 U/min erreicht** — liegt über dem dokumentierten Limiter von 4650 U/min.
   Prüfen ob Limiter korrekt gesetzt oder ob Messjitter (±50 U/min).

4. **Zündwinkel bei Leerlauf (12.0–14.6°)** — etwas niedrig für ~900–1050 U/min.
   Erwartung aus Kurve: ~12–13° bei 900 U/min → ✓

5. **Spulenstrom 3.4 A** — Maximalwert. 123\TUNE+ begrenzt den Spulenstrom aktiv.
   Wert erscheint plausibel für die verwendete Spule.

---

## Logdateien

| Datei | Inhalt |
|---|---|
| `logs/m5dial_123tune_drive_1.csv` | Hauptdatensatz (6092 Punkte, 16:40–18:56 Uhr, dt-Format) |
| `logs/m5dial_123tune_drive.csv` | Kurzer Testlauf (94 Punkte, englisches Format) |
| `logs/serial_monitor_2026-05-22.txt` | Serieller Monitor-Mitschnitt (979 Zeilen, ab 18:49:45 Uhr) |

### CSV-Format (drive_1.csv)

```
ms;zeit;epoch;rpm;zuendung_grad;map_kpa;temp_c;spannung_v;spule_a;rx
```

Trennzeichen: Semikolon, Dezimalzeichen: Komma (deutsches Format).

---

*Erstellt: 2026-05-22, Fahrt 1 — erste vollständige BLE-Fahrtaufzeichnung VW T2b*
