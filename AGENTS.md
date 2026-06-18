# AGENTS.md

## Repository layout (important)

The `main` branch only contains this file and an empty `README.md`. **All real code
lives in feature branches** and is not merged into `main`:

| Branch | Product | Build system |
|---|---|---|
| `feature/spartan-live-display` | M5Stack Dial firmware (flagship, "v2") | PlatformIO |
| `feature/bm6-display` | M5Stack Dial firmware | PlatformIO |
| `claude/m5stack-ble-nus-client-oqxww` | M5 firmware + `spartan-hub/` ESP32 gateway | PlatformIO |
| `claude/digits-clock-waveshare-esp32-7nxzT` | M5 firmware (clock page) | PlatformIO |
| `claude/esp32-hub-apk-EdAud` | `esp32_hub` Flutter Android app (WebView) | Flutter |

To work on a product, check out the relevant branch (e.g. `git checkout feature/bm6-display`).

## Cursor Cloud specific instructions

The VM snapshot already contains: PlatformIO Core (`~/.local/bin`), the Flutter SDK
(`~/flutter`), the Android SDK + NDK (`~/android-sdk`), and OpenJDK 17. PATH,
`ANDROID_SDK_ROOT`/`ANDROID_HOME`, and `JAVA_HOME=java-17` are exported from `~/.bashrc`.
The only thing the startup update script refreshes is the PlatformIO pip package.

### ESP32 / M5Stack firmware (PlatformIO)

- Build (compile only) from a checked-out firmware branch dir: `pio run`.
- There is **no hardware in the cloud VM**, so you can only compile. `pio run -t upload`
  and `pio device monitor` will not work. The `upload_port`/`monitor_port = COM4` lines in
  `platformio.ini` are Windows-specific and irrelevant here.
- The default `platform = espressif32` resolves to **Arduino-ESP32 core 2.0.17**
  (`ledcAttachPin` API). `feature/bm6-display`, `claude/m5stack-ble-nus-client-oqxww`, and
  the clock branch build cleanly with it.
- **Caveat — flagship branch:** `feature/spartan-live-display` uses the core-3.x API
  `ledcAttach(...)`, so it does **not** compile with the default core-2.0.17 platform.
  Building it requires an Arduino-core-3.x platform, e.g. pinning the pioarduino fork in
  `platformio.ini` (`platform = https://github.com/pioarduino/platform-espressif32/releases/download/54.03.21/platform-espressif32.zip`).
  Its `src/main.cpp` compiles fine under core 3.x; note that the newer pioarduino esptool
  can clash with PlatformIO's bundled `click` during `bootloader.bin` generation.

### Flutter app (`claude/esp32-hub-apk-EdAud`)

- Use **Java 17** (not the system default 21); `JAVA_HOME` is already set to it.
- Commands (from the branch's project dir): `flutter pub get`, `flutter analyze`,
  `flutter build apk --release`.
- The app is just a WebView pointing at the ESP32 hub IP, so it can be built but not
  meaningfully run without an Android device/emulator and the hub's network.
