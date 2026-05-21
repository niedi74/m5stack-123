# PDSIT 36/40 Vergaser - Abstimmungsprotokoll

**Motor:** Type 4 / 914 - aufgebohrt
- Nockenwelle: 276er (scharf)
- Verdichtung: 9:1
- Custom Saugrohr (größer als original)
- Venturi: 32mm (original war 30mm bei 914/2.0)

**Zündanlage:** 123 mit MAP-Korrektur, Klopfsensor, Live-Tuning via App

---

## Aktuelle Konfiguration (Stand: Mai 2026)

| Komponente | Wert | Notizen |
|---|---|---|
| **Luftkorrekturdüse (LKD)** | 105 | Gerade eingebaut, minimal Übergangs-Abmagerung |
| **Hauptdüse (HD)** | 145 | Wird bald auf 147 getestet |
| **Leerlaufdüse (LLD)** | 60 | Stabilität erforderlich, nicht kleiner |
| **Leerlaufdrehzahl** | wechselbar | Aktuell nicht dokumentiert |

---

## Problem-Charakterisierung

### Kritischer Bereich: 0.1 bar Unterdruck (~2000-3000 U/min)

- **Lambda:** 0.7-0.8 (zu fett)
- **Symptome:** Abmagerung nach Übergang, Übergangsstöße
- **Ursache:** Leerlaufdüse (60er) speist aggressiv in den Übergangsbereich, LKD war zu klein

### Schiebebetrieb (0 bar)
- **Lambda:** 0.9-1.0 ✓ passt
- **Stabilität:** Mit 60er LLD stabil ✓

### Vollast (100 kPa)
- **Lambda:** 0.9-1.1 (OK, nicht ideal aber fahrtechnisch akzeptabel)
- **CHT:** Zylinder 3 heiß (~180-190°C), nach Überholvorgang -25/30°C sichtbar

---

## Helmholtz-Resonanzsystem (Ansaugrohr)

- 8er Leitung zwischen Saugrohren
- 2 Resonanzkörper (Flaschen) bei 20cm und 25cm Abgang
- Volumen: 2×200ml und 3×300ml
- Effekt: Stabilisiert MAP-Signal, wirkt sich auf Teillast aus

---

## Zündungs-Setup (123er)

**MAP-Kurve (aktuell):**

| MAP [kPa] | Grad KW |
|---|---|
| 0 | 11,0 |
| 30 | 10,0 |
| 50 | 8,0 |
| 60 | 5,0 |
| 73 | 2,0 |
| 85 | 0,0 |
| 87+ | 0,0 |

**Zentrifugale Kurve:**
- Drehzahlgrenze: 4650 U/min
- Statischer Punkt: 0°
- Max ~30° ab 3400 U/min

**Tuning-Erfahrung:**
- 15-20° Frühzündung im Konstantfahrtbereich (Teillast) optimal
- 102er Sprit + Klopfsensorik erlaubt aggressives Tuning
- Live-Tuning via App während Fahrt möglich (±/Tasten für ganzes Band)

---

## Verbrauchsentwicklung

| Phase | LKD | HD | Verbrauch | Notizen |
|---|---|---|---|---|
| Frühjahr 2026 | 90 | 145 | ~15,0 L/100km | Historisch (ggf. 13,5-14 früher?) |
| Mit 105er LKD | 105 | 145 | ~14,0-14,2 L/100km | -1L auf 120km Landstraße/AB |
| **Nächst (geplant)** | 105 | **147** | ? | HD leicht erhöhen |

---

## Abstimmungs-Roadmap

### Phase 1: Übergangs-Abmagerung fixen ✓ LAUFEND
- [x] LKD auf 105 getauscht
- [ ] HD auf 147 testen (Übergangs-Abmagerung beseitigen)
- [ ] Konstante Fahrten durchführen (Verbrauch messen)

### Phase 2: MAP-Feinabstimmung
- [ ] Live-Korrektur im 0.1-bar-Fenster testen
- [ ] MAP-Kurve im Teillast anpassen (0.1-0.5 bar Bereich)
- [ ] CHT monitoren (Zylinder 3 im Auge behalten)

### Phase 3: Leerlaufdüse (nur falls nötig)
- [ ] Evt. auf 62/63 testen (nur wenn Laufruhe leidet)
- [ ] Nicht vorsorglich vergrößern!

### Phase 4: LKD Final-Optimierung (nur falls alles passt)
- [ ] 110er LKD testweise → weitere ~2-3% Verbrauchseinsparung?
- [ ] 115er LKD → noch mehr, aber HD/LLD müssen nachziehen
- [ ] **Erst am Ende!** Nur wenn obige Punkte optimiert

---

## Messpunkte für Logging

Sobald Logging verfügbar:

<!-- TODO: Kanäle definieren: Lambda, CHT Zyl.3, MAP, RPM, Verbrauch/Zeit -->

---

## Physikalische Hintergründe

### LKD-Wirkung (Flächen-Modell)

Luftmassenstrom ∝ Düsenfläche (d²)

| LKD | Relative Fläche | vs. 90er |
|---|---|---|
| 90 | 8100 | Basis |
| 100 | 10000 | +23% |
| 105 | 11025 | +36% |
| 110 | 12100 | +49% |
| 115 | 13225 | +63% |

**Real im Teillastbereich (0.1 bar):**
- LKD wirkt zu ~20% des Gesamtgemisches
- 36% Luftmehr × 20% Gewichtung = **~7% Abmagerung**
- Auf Verbrauch: ~5% Ersparnis bei Teillastfahrt

### Venturi-Skalierung

Original 914/2.0: 30mm Venturi → 115 LKD, 135 HD  
Dein Setup: 32mm Venturi (6,7% größer)

Skalierter Sollwert LKD: 115 × 1,067 = **~122**  
Skalierter Sollwert HD:  135 × 1,067 = **~144** ✓ (passt zu 145)

→ Deine 105er LKD ist bewusst kleiner (für Teillast-Abmagerung)

---

## Kontakt / Unterstützung

**Vergaser-Berater:** Gisbert (hat 32er Venturi geliefert)
- Erwähnte: 100er LKD (evt. 90er) als Startwert
- Erwähnte: 63-65er Leerlaufdüse (aktuell 60er passt besser)
- Hinweis: Umbau auf wechselbare LKD möglich, aber nicht getestet

---

**Letzte Aktualisierung:** 16.05.2026  
**Status:** Phase 1 (HD-Test ausstehend)
