# M5 Display Flicker Diagnose 2026-05-27

## Symptom

- Main / Tune / Settings / Lambda Pages flackern im Betrieb
- "BLE OK", "ADVANCE deg", "BAT 13.69V" pulsieren / sind manchmal verwaschen
- Im Boot- und WiFi-Scan-Modus tritt das Flackern NICHT auf
- Aux-Page (TEMP + VOLT/BAT) flackert NICHT

## Bestaetigt

- Existiert auch auf Pre-Session-Branch claude/m5stack-ble-nus-client-oqxww
  (also nicht durch BM6 / BAT-Display verursacht)
- BM6 hat es leicht verstaerkt durch zusaetzliche BLE-Last

## Ursache (Tearing)

drawAux benutzt Sprites (sprTop, sprBot) -> off-screen rendern, dann in
einem push() -> Display-Refresh und Draw kollidieren nicht.

drawMain, drawTune, drawSettings, drawLambdaPage zeichnen direkt aufs
Display: fillRect(...) + drawString(...). Wenn der Display-Refresh (~60Hz
ST7789) gerade ueber die geloeschten Pixel laeuft bevor der Text drauf
ist, sieht man die schwarze Flaeche kurz.

Im Boot/Scan-Modus laeuft loop() gleichmaessig schnell -> Auge integriert
die kurzen Schwarz-Phasen weg. Sobald BLE-Notifications den Loop unregel-
maessig machen, faellt Draw und Display-Refresh out-of-sync -> Tearing
sichtbar.

## Falsche Annahme die NICHT geholfen hat

Draw-Throttle auf 10 Hz (Commit 2fb005f, reverted) hat den Flicker
schlimmer gemacht -- bei 10 Hz sieht das Auge die einzelnen Redraws
als Oszilloskop-aehnliche Blinks.

## Proper Fix (TODO morgen)

drawMain, drawTune, drawSettings, drawLambdaPage auf Sprite-basiertes
Doppel-Buffering umstellen. Vorbild: drawAux (Z.2434+).

Schritte:
1. Pro Page einen LGFX_Sprite anlegen (oder den fillRect-Bereich in einen
   Sprite umleiten)
2. Alle Texte + Hintergrund in den Sprite zeichnen
3. Am Ende des Page-Draws: sprite.pushSprite(0, 44)
4. Test: Flicker sollte auch im Betrieb mit BLE-Last verschwinden

Aufwand ~30 min, isolierter Branch z.B. feature/sprite-mainpage.

## Vergleich

| Page | Aktuell | Flicker | TODO |
|---|---|---|---|
| Aux | Sprite | nein | OK |
| Main | direkt | ja | Sprite |
| Tune | direkt | ja | Sprite |
| Settings | direkt | ja | Sprite |
| Lambda | direkt | ja | Sprite |
| Log | direkt | unbekannt | pruefen |
| Status (oberer Bereich) | direkt | unbekannt | pruefen |
