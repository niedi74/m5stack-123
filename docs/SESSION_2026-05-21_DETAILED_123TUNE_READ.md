# Session 2026-05-21 - 123Tune+ BLE, XAPK analysis and read-only progress

## Scope

Goal for this session was to get the M5 Dial / ESP32-S3 talking to the real
123\TUNE+ distributor via Bluetooth LE and to document the original app
behavior well enough to continue safely.

Current rule: read-only first. Permanent write/map changes are intentionally
not enabled yet.

## Local paths

| Item | Path |
| --- | --- |
| Main workspace | `D:\_claude\M5stack\m5stack-123` |
| Original app XAPK | `D:\_claude\M5stack\123Tune+_8.2.0_APKPure.xapk` |
| XAPK extract | `D:\_claude\M5stack\xapk_123tune_8_2_0_extract` |
| Decompiled assemblies | `D:\_claude\M5stack\xapk_123tune_8_2_0_analysis\decompiled` |
| Extracted DLLs | `D:\_claude\M5stack\xapk_123tune_8_2_0_analysis\assemblies` |
| iafilius reference clone | `D:\_claude\M5stack\refs\123Tune-plus-Simulator` |

## Original app verification

The first APK at `D:\_claude\M5stack\123tune.apk` was not 123Tune+. It was
Aptoide (`cm.aptoide.pt`). It must not be used as protocol reference.

The correct package is:

```text
com.albertronic.x123tuneplus
```

The XAPK `123Tune+_8.2.0_APKPure.xapk` was verified as the real app:

| Field | Value |
| --- | --- |
| App | `123\Tune+` |
| Package | `com.albertronic.x123tuneplus` |
| Version | `8.2.0` |
| Version code | `94` |
| Target SDK | `36` |
| XAPK SHA256 | `8B19AA1D8086CFD3C40008BBE270E9228B37CFA72623B1322EF88E843CBEAC7B` |
| Base APK SHA256 | `5A8D8183DDA4B4150CC9F34EDD9C95B40AC8D0C5EB5A59578F19082F4BCF5C91` |

The app is Xamarin/.NET/MAUI based. `libassembly-store.so` contained an XABA
assembly store with compressed XALZ assemblies. 364 assemblies were extracted.

Important extracted assemblies:

```text
123TunePlus.dll
123Connection.dll
123Connection.BLE.dll
123Components.dll
123Charts.dll
```

## BLE UUIDs

The current device is Raytac/Nordic UART style:

| Name | UUID |
| --- | --- |
| NUS service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| NUS RX, write | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| NUS TX, notify | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| CCCD | `00002902-0000-1000-8000-00805f9b34fb` |
| DIS service | `0000180a-0000-1000-8000-00805f9b34fb` |
| Battery service | `0000180f-0000-1000-8000-00805f9b34fb` |

Decompiled source:

```text
123Connection\_123Connection\RaytacUuids.cs
123Connection.BLE\_123ConnectionBLEAndroid\DeviceBleAdapterRaytac.cs
```

## Original Android connect flow

From the decompiled app:

1. Connect with Android BLE transport LE when available.
2. Wait about 750 ms after connected.
3. Discover services.
4. Enable TX notification on NUS TX.
5. Write CCCD with notification value `01 00`.
6. Mark device connected.
7. On `OnDeviceConnectedEvent`, send ping and carriage return:

```text
$
\r
```

8. Ping timer runs every 1650 ms if `ConnectedDevice.NeedsPing == true`.

All BLE devices constructed by the app set:

```text
NeedsPing = true
```

## M5 Dial / ESP32-S3 status

Board and port:

| Item | Value |
| --- | --- |
| Board | M5Stack StampS3 / ESP32-S3 |
| Port | COM4 |
| Framework | Arduino / PlatformIO |
| BLE lib | NimBLE-Arduino 2.5.0 |

Current firmware behavior in `src/main.cpp`:

- scans for target MAC `ef:a8:b2:de:e0:9e`
- connects with BLE LE client
- sets connection parameters `16..32`, latency `0`, timeout `400`
- disables automatic MTU exchange and uses MTU 23
- dumps services and characteristics
- subscribes only to NUS TX, not every notify characteristic
- writes CCCD off first, then subscribes
- reads CCCD back
- stores NUS RX characteristic for commands
- sends `$`, `\r`, then read commands
- pings with `$` every 1650 ms
- shifts the top status text down for the round M5 Dial display so `BLE OK`
  and `IGN #` are not clipped by the bezel

Observed successful connection:

```text
Verbunden!
Conn: itvl=40.0ms lat=0 to=4000ms mtu=23 enc=0 auth=0 bond=0
NUS TX h=15 prop=10 N=1 I=0
NUS RX h=13 prop=0C W=1 WNR=1
NUS CCCD read len=2 : 01 00
Sub NUS: OK
TX ping '$' -> OK
```

First response after CR:

```text
NTFY handle=15 len=1 : 0D
```

That proves the NUS channel is alive.

## Read commands confirmed

The app sends command strings as:

```text
<command>\r$
```

Confirmed read-only commands:

| Command | Purpose | Status |
| --- | --- | --- |
| `v@` | version/settings/live summary | works |
| `I@` | information/UID/license/double-vac flags | identified, not yet tested on ESP |
| `10@` | legacy EEPROM block 0 | works |
| `11@` | legacy EEPROM block 1 | works |
| `12@` | legacy EEPROM block 2 | works |
| `13@` | legacy EEPROM block 3 | works |
| `0000-40@` | V2 64-byte block read | rejected/echo + bell on this device |

The `0000-40@` test produced only echo plus `0x07`, so this distributor appears
to use the legacy 16-byte block format for curve reads.

Example response:

```text
TX cmd 'v@\r$' -> OK
NTFY handle=15 len=18 : 76 40 0D 33 35 33 36 36 34 34 31 2D 31 34 2D 34 36 20
```

ASCII interpretation:

```text
v@\r35366441-14-46
```

Example legacy block:

```text
TX cmd '10@\r$' -> OK
NTFY handle=15 len=20 : 31 30 40 0D 46 46 20 46 46 20 30 41 20 32 38 20 31 34 20 34
```

ASCII interpretation:

```text
10@\rFF FF 0A 28 14 4
```

The notification can be split/truncated by serial output wrapping, so the next
step is to buffer response text on the ESP until CR/space block completion.

## Current app screenshot values

The phone app showed the actual map currently loaded in the distributor:

### Device/settings

```text
Device: 123\TUNE+
Serial: 20588
Firmware: V1.4.4
Last seen: 23:21:53
Static ignition timing: 0 degrees
Battery setting: 12 Volt
```

### Centrifugal/advance curve

| Point | RPM | Crankshaft degrees |
| --- | ---: | ---: |
| 1 | 500 | 8.0 |
| 2 | 1000 | 14.0 |
| 3 | 1600 | 22.0 |
| 4 | 1800 | 26.0 |
| 5 | 2500 | 30.0 |
| 6 | 3000 | 30.0 |
| 7 | 3400 | 30.0 |
| 8 | 8000 | 30.0 |

### MAP curve

| Point | Absolute pressure kPa | Crankshaft degrees |
| --- | ---: | ---: |
| 1 | 0 | 11.0 |
| 2 | 30 | 10.0 |
| 3 | 50 | 8.0 |
| 4 | 60 | 5.0 |
| 5 | 73 | 2.0 |
| 6 | 85 | 0.0 |
| 7 | 87 | 0.0 |
| 8 | 100 | 0.0 |
| 9 | 200 | 0.0 |

MAP ignore/start threshold:

```text
1500 rpm
```

## Local `.123` curve file

Found file:

```text
D:\_claude\M5stack\mai 2026 irgendwie.123
```

It is not a raw `.bin`; it is UTF-16 XML. It contains a different saved curve,
not the current distributor curve shown in the app screenshots.

Saved `.123` file values:

```text
Advance:
500  -> 0
1000 -> 0
1500 -> 10
2500 -> 24
4000 -> 29
8000 -> 29

MAP:
0   -> 10
65  -> 10
87  -> 0
100 -> 0
200 -> 0
StartMAP: 1500
```

Use this file as a format reference only, not as current distributor truth.

## Reference project

The useful older reverse-engineering project is:

```text
https://github.com/iafilius/123Tune-plus-Simulator
```

Relevant notes from its changelog/source:

- reverse engineered from original iOS client work around 2017/2018
- ESP32 version added later
- implements `v@`
- implements MAP curve read/write
- implements advance RPM/advance read/write
- contains conversion helpers for RPM, pressure, advance, graph point numbers,
  PIN and checksum

Important local files:

```text
D:\_claude\M5stack\refs\123Tune-plus-Simulator\ESP32-Arduino\ESP32_123Tune_plus_server\RX.ino
D:\_claude\M5stack\refs\123Tune-plus-Simulator\ESP32-Arduino\ESP32_123Tune_plus_server\ble.ino
D:\_claude\M5stack\refs\123Tune-plus-Simulator\Changelog.txt
```

Known conversion hints from `RX.ino`:

```text
RPM live/frame scaling:
MSB * 800 + LSB * 50

Advance live/frame scaling:
MSB * 3.2 + LSB * 0.2

Checksum:
ID + 0x10 + (MSB - 0x30) + (LSB - 0x30)
```

These are consistent with the current ESP decoder for live frames.

## Write/tune status

Not enabled yet:

- EEPROM/map write
- permanent advance curve write
- permanent MAP curve write
- PIN/auth write

Identified but deliberately deferred live tune commands:

| Command | Meaning |
| --- | --- |
| `T` | toggle tune mode |
| `A` | advance up |
| `R` | retard/down |

Implemented on 2026-05-22 behind an explicit tune-test guard:

- `tune_arm`: enables the guard, no command is sent to the distributor
- long press on the M5Dial button toggles tune mode by sending `T`
- while tune mode is active, rotary encoder steps send:
  - clockwise: `A`
  - counter-clockwise: `R`
- `tune_up`, `tune_down`, `tune_zero`, `tune_off`, `tune_disarm` are available
  over USB serial for controlled tests
- this does not write the map/EEPROM and is intended only for temporary live
  offset testing
- display color indicates the tracked temporary offset:
  - orange: tracked offset is zero / map baseline
  - red: positive/advance steps
  - blue: negative/retard steps
- the main display additionally shows `TUNE +N` or `TUNE -N` next to the
  advance value while tune mode is active
- CSV rows include `map_bar`, `tune_active`, and `tune_steps` for later analysis

Planned safety model:

1. Read current distributor map completely.
2. Decode and compare against screenshots.
3. Save read backup locally.
4. Add UI/command guard for tune/write actions. Done for live tune.
5. Test live `T/A/R` in standstill first.
6. Permanent writes only after backup and confirmation.

## Display layout note

The first status layout placed the top labels too high for the visible round
display area. Photos showed `BLE OK` and `IGN #` clipped at the top.

Changed in `src/main.cpp`:

```text
Status bar fill: 0..44 px
BLE OK y:        20
IGN # y:         34
Mode y:          27
Top data sprite: y=44
Bottom sprite:   y=140
```

This is still a compact diagnostic layout, not the final UI.

## Handoff for another AI / Claude Code

Do not restart from BLE basics. The important facts are already proven:

```text
BLE connect works.
NUS subscribe works.
NUS RX write works.
v@ read works.
10@..13@ legacy reads work.
V2 read command 0000-40@ is wrong for this distributor.
Permanent write is intentionally not implemented.
```

If continuing this project:

1. Keep all work in `D:\_claude\M5stack\m5stack-123`.
2. Treat `src/main.cpp` as a diagnostic firmware, not finished UI code.
3. Keep commands read-only until decoded EEPROM data matches app screenshots.
4. Use `D:\_claude\M5stack\refs\123Tune-plus-Simulator` as the old protocol
   reference, especially `RX.ino` and `ble.ino`.
5. Use decompiled app files from
   `D:\_claude\M5stack\xapk_123tune_8_2_0_analysis\decompiled` as the current
   app reference.
6. When using Git in this workspace, include:

```powershell
git -c safe.directory=D:/_claude/M5stack/m5stack-123 ...
```

because the repository can otherwise trigger a dubious ownership warning.

Useful commands:

```powershell
cd D:\_claude\M5stack\m5stack-123
pio run
pio run -t upload --upload-port COM4
pio device monitor --port COM4 --baud 115200
```

Expected monitor lines for a good read-only session:

```text
Sub NUS: OK
TX ping '$' -> OK
TX enter CR -> OK
TX cmd 'v@\r$' -> OK
TX cmd '10@\r$' -> OK
TX cmd '11@\r$' -> OK
TX cmd '12@\r$' -> OK
TX cmd '13@\r$' -> OK
```

The next implementation task is not "find BLE UUIDs"; it is response buffering
and EEPROM decode.

## Future architecture notes

The broader plan is:

- 123Tune over BLE on the M5 Dial / ESP32-S3
- Spartan 3 V2 Lambda data as another channel
- two additional ESP32 nodes over CAN
- one read/log/control core before any write-heavy tuning behavior

Potential combined telemetry:

```text
RPM
Advance
MAP / pressure
temperature
voltage
Lambda / AFR
controller status
CAN-distributed display/logger/control data
```

## Next steps

1. Stop auto-read after `10@..13@` from being just debug output; buffer the data.
2. Parse ASCII block responses into 64 EEPROM bytes.
3. Decode 64 bytes using app and iafilius conversion logic.
4. Compare decoded curves against the phone screenshots above.
5. Add `I@` read test.
6. Keep firmware read-only until decoded curves match the app 1:1.

## Update 2026-05-22 - live mode and road-test logging

Morning motor-running test confirmed that all live values update over NUS:

```text
RPM
Advance
MAP / pressure
temperature
voltage
coil current
```

The pasted monitor log showed many successful pings. The `TX ping '$' -> FAIL`
events happened at the same time as `Disc reason=520`, so they are currently
treated as symptoms of a disconnect/reconnect, not as proof that the 1650 ms
ping itself is wrong.

Firmware change in `src/main.cpp`:

- default connect path is now live-only
- after subscribe it sends only `$` and `\r`
- automatic `v@`, `10@`, `11@`, `12@`, `13@` reads are disabled by
  `kReadOnConnect = false`
- long button press triggers the read-only dump manually
- normal notify hex logging is quiet by default
- serial output prints a compact live line every 500 ms:

```text
LIVE rpm=1000 adv=13.4 map=100 temp= 19 volt=13.7 cur=3.3 rx=1234
```

The compact `LIVE` line is emitted only when decoded RPM is greater than
650 U/min. This keeps ignition-only and start/stop setup noise out of
standstill and road-test logs.

This format is intended for the next long-running driving/logging test because
it is much easier to store and compare than the raw 5-byte notification dump.

Display change:

- main page now shows `ADVANCE deg`, `MAP bar`, and `RPM`
- short press still switches to the temperature/voltage page
- long press starts the read-only `v@` and `10@..13@` dump

Important next test:

1. Flash this live-mode build.
2. Start monitor with:

```powershell
pio device monitor --port COM4 --baud 115200
```

3. Start the engine and let it idle for several minutes.
4. Verify that live lines continue without frequent `Disc reason=520`.
5. For a road test, log the monitor output to a file from PowerShell, e.g.:

```powershell
pio device monitor --port COM4 --baud 115200 | Tee-Object -FilePath D:\_claude\M5stack\logs\drive-test-2026-05-22.txt
```

Do not use permanent write/tune functions during the first driving log. Keep
this run read-only.

## Update 2026-05-22 - internal CSV logging and mini WebGUI

Firmware now has a first self-contained logging layer:

- SPIFFS is mounted on boot
- current log file: `/drive.csv`
- rotated old log file: `/drive_old.csv`
- CSV header from the 2026-05-22 logger/time update:

```csv
ms;zeit;epoch;rpm;zuendung_grad;map_kpa;map_bar;temp_c;spannung_v;spule_a;rx;tune_active;tune_steps
```

- rows are written only when RPM is greater than 650 U/min
- when home WiFi is connected, firmware starts NTP with the German local
  timezone and writes local wall-clock time into `zeit`
- the M5Dial BM8563 RTC is used on I2C `SDA=G11` / `SCL=G12`:
  - boot loads system time from RTC when plausible
  - successful NTP writes the current local time back to RTC
  - with the backup battery fitted, time can survive reboot/offline use
- if neither RTC nor NTP is valid yet, `zeit` falls back to `BOOT+<millis>`
  and `epoch` is `0`
- CSV uses semicolons and decimal commas so German Excel/LibreOffice imports it
  without column/decimal confusion
- if an older comma-separated `/drive.csv` exists, firmware rotates it to
  `/drive_old.csv` once and starts a fresh semicolon CSV
- if NTP does not answer in the local network, the WebGUI can set time from the
  browser/laptop clock via `Sync from browser` and write that value to RTC
- current log rotates at about 1.2 MB so the default 1.5 MB SPIFFS partition is
  not filled completely

Mini WebGUI:

- runs on port 80
- if no WiFi credentials are stored, the M5Dial starts setup AP:

```text
SSID:     M5Dial-123-Setup
Password: open
URL:      http://192.168.4.1/
```

- WebGUI actions:
  - download current CSV
  - download old rotated CSV
  - clear current CSV
  - enter home WiFi SSID/password
  - start WPS push-button setup

WPS flow for FRITZ!Box:

1. Connect phone/laptop to `M5Dial-123-Setup`.
2. Open `http://192.168.4.1/`.
3. Click `Start WPS`.
4. Press `Connect/WPS` on the FRITZ!Box.
5. On success, the M5Dial stores SSID/password in Preferences/NVS and connects
   to the home WiFi on future boots.

If WPS is unreliable on a given router, the fallback is either the WebGUI SSID
form or a later `.env`/build-flag based development-only credential file. Avoid
committing real WiFi secrets.

Serial helper commands:

- `wifi_status`: show WiFi mode, connection, IP and saved SSID name
- `time_status`: show whether NTP/local time is valid and the current timestamp
- `time_set <epoch>`: set ESP system time from USB serial and write RTC
