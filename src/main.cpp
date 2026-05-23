#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include <Wire.h>
#include "esp_wps.h"
#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_GC9A01.hpp>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>
#include <lgfx/v1/platforms/esp32/Light_PWM.hpp>

// --- GC9A01 Display ---
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_GC9A01 _gc9a01;
    lgfx::Bus_SPI      _spi;
    lgfx::Light_PWM    _bl;
public:
    LGFX() {
        { auto cfg = _spi.config();
          cfg.spi_host = SPI2_HOST; cfg.spi_mode = 0; cfg.freq_write = 40000000;
          cfg.pin_sclk = 6; cfg.pin_mosi = 5; cfg.pin_miso = -1; cfg.pin_dc = 4;
          _spi.config(cfg); _gc9a01.setBus(&_spi); }
        { auto cfg = _gc9a01.config();
          cfg.pin_cs = 7; cfg.pin_rst = 8; cfg.pin_busy = -1;
          cfg.panel_width = 240; cfg.panel_height = 240;
          cfg.invert = true; cfg.offset_x = 0; cfg.offset_y = 0;
          _gc9a01.config(cfg); }
        { auto cfg = _bl.config();
          cfg.pin_bl = 9; cfg.invert = false; cfg.freq = 44100; cfg.pwm_channel = 7;
          _bl.config(cfg); _gc9a01.setLight(&_bl); }
        setPanel(&_gc9a01);
    }
};

static LGFX        display;
static LGFX_Sprite sprTop(&display);
static LGFX_Sprite sprBot(&display);

// --- BLE NUS ---
static const char* NUS_SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char* NUS_RX  = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static const char* NUS_TX  = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
static const char* TARGET  = "ef:a8:b2:de:e0:9e";

// --- Hardware ---
#define BTN_PIN       42
#define ENC_A_PIN     41
#define ENC_B_PIN     40
#define BUZZER_PIN    3
#define TOUCH_ADDR    0x38
#define TOUCH_INT_PIN 14
#define LONG_PRESS_MS 600
#define TUNE_HOLD_MS  2000

// --- On-screen log ---
#define NLOG 6
static char g_log[NLOG][38];
static int  g_logN = 0;

static void pushLog(const char* fmt, ...) {
    char tmp[38];
    va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
    strncpy(g_log[g_logN % NLOG], tmp, 37);
    g_log[g_logN % NLOG][37] = 0;
    g_logN++;
    Serial.printf("[%6lums] %s\n", millis(), tmp);
}

// --- App State ---
static volatile float    g_rpm   = 0;
static volatile float    g_adv   = 0;
static volatile float    g_tmp   = 0;
static volatile float    g_vlt   = 0;
static volatile float    g_map   = 0;
static volatile float    g_cur   = 0;
static volatile bool     g_conn  = false;
static volatile uint32_t g_rxCnt = 0;
enum UiPage : uint8_t { PAGE_MAIN, PAGE_AUX, PAGE_SETTINGS, PAGE_TUNE, PAGE_COUNT };
enum BeepKind : uint8_t { BEEP_ACTION, BEEP_BLE, BEEP_ERROR };

static UiPage            g_page = PAGE_MAIN;
static bool              g_rawlog = false;
static bool              g_readRequested = false;
static bool              g_readBusy = false;
static bool              g_tuneArmed = false;
static bool              g_tuneActive = false;
static int               g_tuneSteps = 0;
static int8_t            g_encoderAccum = 0;
static uint32_t          g_lastTuneStepMs = 0;
static uint32_t          g_tuneArmedAt = 0;
static uint8_t           g_settingIndex = 0;
static bool              g_buzzerEnabled = true;
static bool              g_beepActions = true;
static bool              g_beepBle = true;
static bool              g_beepErrors = true;
static uint8_t           g_brightness = 200;
static uint32_t          g_beepUntil = 0;
static bool              g_touchDown = false;

static NimBLEClient* pClient   = nullptr;
static NimBLERemoteCharacteristic* pNusRx = nullptr;
static NimBLEAddress targetAddr;
static volatile bool  doConnect = false;

static constexpr bool kReadOnConnect = false;  // live mode stays quiet; long press starts read-only dump
static constexpr float kLogMinRpm = 650.0f;    // suppress ignition/start-only noise in drive logs
static constexpr int kTuneMaxSteps = 10;       // temporary test correction limit in each direction
static constexpr uint32_t kTuneArmTimeoutMs = 30000;  // ARM expires unless LIVE is confirmed
static constexpr uint8_t kBuzzerChannel = 6;

// --- Local logging / Web GUI ---
static WebServer   web(80);
static DNSServer   dns;
static Preferences prefs;
static bool        g_fsOk = false;
static bool        g_wifiAp = false;
static bool        g_wpsActive = false;
static bool        g_saveWifiAfterWps = false;
static bool        g_captiveActive = false;
static bool        g_haveSavedWifi = false;
static bool        g_ntpStarted = false;
static bool        g_timeValid = false;
static bool        g_rtcOk = false;
static bool        g_rtcValid = false;
static bool        g_rtcWrittenFromNtp = false;
static int         g_ntpPolls = 0;
static char        g_timeSource[12] = "boot";
static uint32_t    g_lastWifiCheck = 0;
static String      g_serialLine;

static const char* LOG_FILE = "/drive.csv";
static const char* OLD_LOG_FILE = "/drive_old.csv";
static const char* LOG_HEADER = "ms;zeit;epoch;rpm;zuendung_grad;map_kpa;map_bar;temp_c;spannung_v;spule_a;rx;tune_active;tune_steps";
static const char* LOCAL_TZ = "CET-1CEST,M3.5.0,M10.5.0/3";
static constexpr uint8_t RTC_ADDR = 0x51;
static constexpr uint8_t RTC_SDA = 11;
static constexpr uint8_t RTC_SCL = 12;
static constexpr size_t kMaxLogBytes = 1200000;  // keep room in the default 1.5 MB SPIFFS partition
static esp_wps_config_t wpsConfig;

static uint8_t charProps(NimBLERemoteCharacteristic* c) {
    uint8_t props = 0;
    if (c->canBroadcast())       props |= 0x01;
    if (c->canRead())            props |= 0x02;
    if (c->canWriteNoResponse()) props |= 0x04;
    if (c->canWrite())           props |= 0x08;
    if (c->canNotify())          props |= 0x10;
    if (c->canIndicate())        props |= 0x20;
    if (c->canWriteSigned())     props |= 0x40;
    if (c->hasExtendedProps())   props |= 0x80;
    return props;
}

static void logConnInfo(const char* tag) {
    if (!pClient || !pClient->isConnected()) return;
    NimBLEConnInfo info = pClient->getConnInfo();
    Serial.printf("[%6lums] %s: itvl=%.1fms lat=%u to=%ums mtu=%u enc=%d auth=%d bond=%d\n",
                  millis(), tag,
                  info.getConnInterval() * 1.25f,
                  info.getConnLatency(),
                  info.getConnTimeout() * 10,
                  info.getMTU(),
                  info.isEncrypted(), info.isAuthenticated(), info.isBonded());
}

// --- Frame decoder ---
static int hexnib(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static void decodeFrame(const uint8_t* d, size_t n) {
    if (n < 3) return;
    g_rxCnt++;
    int hi  = hexnib(d[1]);
    int lo  = hexnib(d[2]);
    int raw = (hi << 4) | lo;

    if (g_rawlog) {
        Serial.printf("[%6lums] RAW:", millis());
        for (size_t i = 0; i < n; i++) Serial.printf(" %02X", d[i]);
        Serial.println();
    }

    switch (d[0]) {
        case 0x30: g_rpm = hi * 800.0f + lo * 50.0f; break;
        case 0x31: g_adv = hi * 3.2f   + lo * 0.2f;  break;
        case 0x32: g_map = (float)raw;                break;
        case 0x33: g_tmp = (float)(raw - 30);         break;
        case 0x35: g_cur = raw / 8.65f;               break;
        case 0x41: g_vlt = raw / 4.54f;               break;
        case 0x42: case 0x0D: break;
        default: break;
    }
}

static void printLiveSummary() {
    Serial.printf("[%6lums] LIVE rpm=%4d adv=%4.1f map=%3d temp=%3d volt=%4.1f cur=%3.1f rx=%lu\n",
                  millis(),
                  (int)g_rpm,
                  (float)g_adv,
                  (int)g_map,
                  (int)g_tmp,
                  (float)g_vlt,
                  (float)g_cur,
                  (unsigned long)g_rxCnt);
}

static uint8_t bcdToBin(uint8_t v) {
    return ((v >> 4) * 10) + (v & 0x0F);
}

static uint8_t binToBcd(uint8_t v) {
    return ((v / 10) << 4) | (v % 10);
}

static void setTimeSource(const char* source) {
    strncpy(g_timeSource, source, sizeof(g_timeSource) - 1);
    g_timeSource[sizeof(g_timeSource) - 1] = 0;
}

static bool rtcWriteRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool rtcReadRegisters(uint8_t reg, uint8_t* data, size_t len) {
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    return Wire.requestFrom((int)RTC_ADDR, (int)len) == (int)len &&
           Wire.readBytes(data, len) == len;
}

static bool rtcWriteFromSystemTime() {
    if (!g_rtcOk || !g_timeValid) return false;

    time_t now = time(nullptr);
    struct tm info;
    localtime_r(&now, &info);

    uint8_t data[8] = {
        0x02,
        binToBcd(info.tm_sec),
        binToBcd(info.tm_min),
        binToBcd(info.tm_hour),
        binToBcd(info.tm_mday),
        binToBcd(info.tm_wday),
        binToBcd(info.tm_mon + 1),
        binToBcd((info.tm_year + 1900) % 100)
    };

    Wire.beginTransmission(RTC_ADDR);
    Wire.write(data, sizeof(data));
    if (Wire.endTransmission() != 0) return false;

    g_rtcValid = true;
    g_rtcWrittenFromNtp = true;
    pushLog("RTC gesetzt");
    return true;
}

static bool rtcLoadSystemTime() {
    if (!g_rtcOk) return false;

    uint8_t data[7] = {};
    if (!rtcReadRegisters(0x02, data, sizeof(data))) return false;
    if (data[0] & 0x80) {
        pushLog("RTC Zeit ungueltig");
        return false;
    }

    struct tm info = {};
    info.tm_sec = bcdToBin(data[0] & 0x7F);
    info.tm_min = bcdToBin(data[1] & 0x7F);
    info.tm_hour = bcdToBin(data[2] & 0x3F);
    info.tm_mday = bcdToBin(data[3] & 0x3F);
    info.tm_wday = bcdToBin(data[4] & 0x07);
    info.tm_mon = bcdToBin(data[5] & 0x1F) - 1;
    info.tm_year = bcdToBin(data[6]) + 100;  // 2000-based for this project lifetime.
    info.tm_isdst = -1;

    if (info.tm_year < 124 || info.tm_mon < 0 || info.tm_mon > 11 ||
        info.tm_mday < 1 || info.tm_mday > 31 || info.tm_hour > 23 ||
        info.tm_min > 59 || info.tm_sec > 59) {
        pushLog("RTC Datum unplausibel");
        return false;
    }

    time_t epoch = mktime(&info);
    if (epoch <= 1700000000) {
        pushLog("RTC Epoch unplausibel");
        return false;
    }

    timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    g_timeValid = true;
    g_rtcValid = true;
    setTimeSource("RTC");
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &info);
    pushLog("RTC Zeit %s", buf);
    return true;
}

static bool setSystemEpoch(time_t epoch, const char* source) {
    if (epoch <= 1700000000) return false;

    timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    g_timeValid = true;
    setTimeSource(source);

    struct tm info;
    localtime_r(&epoch, &info);
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &info);
    pushLog("%s Zeit %s", source, buf);
    if (g_rtcOk) rtcWriteFromSystemTime();
    return true;
}

static void setupRtcTime() {
    setenv("TZ", LOCAL_TZ, 1);
    tzset();
    Wire.begin(RTC_SDA, RTC_SCL);
    Wire.setClock(400000);

    Wire.beginTransmission(RTC_ADDR);
    g_rtcOk = Wire.endTransmission() == 0;
    pushLog("RTC: %s", g_rtcOk ? "OK" : "FAIL");
    if (!g_rtcOk) return;

    rtcWriteRegister(0x00, 0x00);
    rtcWriteRegister(0x01, 0x00);
    rtcLoadSystemTime();
}

static bool updateTimeState() {
    time_t now = time(nullptr);
    bool valid = now > 1700000000;
    if (valid && !g_timeValid) {
        struct tm info;
        localtime_r(&now, &info);
        char buf[24];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &info);
        pushLog("Zeit OK %s", buf);
        setTimeSource("NTP");
    }
    g_timeValid = valid;
    if (valid && g_ntpStarted && g_rtcOk && !g_rtcWrittenFromNtp &&
        strcmp(g_timeSource, "NTP") == 0) {
        rtcWriteFromSystemTime();
    }
    return valid;
}

static void startNtpIfNeeded() {
    if (g_ntpStarted || WiFi.status() != WL_CONNECTED) return;
    setenv("TZ", LOCAL_TZ, 1);
    tzset();
    configTzTime(LOCAL_TZ,
                 "192.168.0.1",
                 "fritz.box",
                 "pool.ntp.org");
    g_ntpStarted = true;
    pushLog("NTP start");
}

static String localTimestamp() {
    updateTimeState();
    if (g_timeValid) {
        struct tm info;
        time_t now = time(nullptr);
        localtime_r(&now, &info);
        char buf[24];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &info);
        return String(buf);
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "BOOT+%lu", (unsigned long)millis());
    return String(buf);
}

static String deFloat(float value, uint8_t precision) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%.*f", precision, value);
    String out(buf);
    out.replace('.', ',');
    return out;
}

static float mapBar() {
    return (float)g_map / 100.0f;
}

static const char* pageName() {
    switch (g_page) {
        case PAGE_AUX: return "T/V";
        case PAGE_SETTINGS: return "SET";
        case PAGE_TUNE: return "TUNE";
        default: return "ADV";
    }
}

static void stopBeep() {
    ledcWriteTone(kBuzzerChannel, 0);
    g_beepUntil = 0;
}

static void beep(BeepKind kind) {
    bool allowed = g_buzzerEnabled;
    if (kind == BEEP_ACTION) allowed = allowed && g_beepActions;
    if (kind == BEEP_BLE) allowed = allowed && g_beepBle;
    if (kind == BEEP_ERROR) allowed = allowed && g_beepErrors;
    if (!allowed) return;

    uint16_t freq = kind == BEEP_ERROR ? 1800 : (kind == BEEP_BLE ? 5200 : 4200);
    uint16_t duration = kind == BEEP_ERROR ? 160 : 45;
    ledcWriteTone(kBuzzerChannel, freq);
    g_beepUntil = millis() + duration;
}

static void serviceBuzzer() {
    if (g_beepUntil != 0 && millis() >= g_beepUntil) stopBeep();
}

static void saveUiSettings() {
    prefs.putBool("buzzer", g_buzzerEnabled);
    prefs.putBool("beep_btn", g_beepActions);
    prefs.putBool("beep_ble", g_beepBle);
    prefs.putBool("beep_err", g_beepErrors);
    prefs.putUChar("bright", g_brightness);
}

static void loadUiSettings() {
    g_buzzerEnabled = prefs.getBool("buzzer", true);
    g_beepActions = prefs.getBool("beep_btn", true);
    g_beepBle = prefs.getBool("beep_ble", true);
    g_beepErrors = prefs.getBool("beep_err", true);
    g_brightness = prefs.getUChar("bright", 200);
    if (g_brightness < 40) g_brightness = 40;
    display.setBrightness(g_brightness);
}

static void advancePage() {
    if (g_page == PAGE_TUNE && g_tuneActive) {
        beep(BEEP_ERROR);
        return;
    }
    UiPage oldPage = g_page;
    g_page = static_cast<UiPage>((static_cast<uint8_t>(g_page) + 1) % PAGE_COUNT);
    if (oldPage == PAGE_TUNE && g_tuneArmed && !g_tuneActive) {
        g_tuneArmed = false;
        g_tuneArmedAt = 0;
        pushLog("Tune ARM verworfen");
    }
    beep(BEEP_ACTION);
}

static bool readTouchPressed() {
    uint8_t points = 0;
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(0x02);  // FT3267 touch-point count.
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)TOUCH_ADDR, 1) != 1) return false;
    points = Wire.read() & 0x0F;
    return points > 0;
}

static void handleTouch() {
    static uint32_t lastPoll = 0;
    if (millis() - lastPoll < 40) return;
    lastPoll = millis();

    bool down = readTouchPressed();
    if (down && !g_touchDown) advancePage();
    g_touchDown = down;
}

static void ensureLogHeader() {
    if (!g_fsOk) return;
    bool needsHeader = !SPIFFS.exists(LOG_FILE);
    if (!needsHeader) {
        File existing = SPIFFS.open(LOG_FILE, FILE_READ);
        needsHeader = !existing || existing.size() == 0;
        if (existing && !needsHeader) {
            String header = existing.readStringUntil('\n');
            header.trim();
            if (!header.equals(LOG_HEADER)) {
                existing.close();
                SPIFFS.remove(OLD_LOG_FILE);
                SPIFFS.rename(LOG_FILE, OLD_LOG_FILE);
                needsHeader = true;
                pushLog("Log Format neu");
            }
        }
        if (existing) existing.close();
    }
    if (needsHeader) {
        File f = SPIFFS.open(LOG_FILE, FILE_WRITE);
        if (f) {
            f.println(LOG_HEADER);
            f.close();
        }
    }
}

static void rotateLogIfNeeded() {
    if (!g_fsOk || !SPIFFS.exists(LOG_FILE)) return;
    File f = SPIFFS.open(LOG_FILE, FILE_READ);
    size_t sz = f ? f.size() : 0;
    if (f) f.close();
    if (sz <= kMaxLogBytes) return;

    SPIFFS.remove(OLD_LOG_FILE);
    SPIFFS.rename(LOG_FILE, OLD_LOG_FILE);
    ensureLogHeader();
    pushLog("Log rotiert");
}

static void appendLiveCsv() {
    if (!g_fsOk || g_rpm <= kLogMinRpm) return;
    rotateLogIfNeeded();

    File f = SPIFFS.open(LOG_FILE, FILE_APPEND);
    if (!f) return;
    String ts = localTimestamp();
    f.printf("%lu;%s;%ld;%d;%s;%d;%s;%d;%s;%s;%lu;%d;%+d\n",
             (unsigned long)millis(),
             ts.c_str(),
             g_timeValid ? (long)time(nullptr) : 0L,
             (int)g_rpm,
             deFloat((float)g_adv, 1).c_str(),
             (int)g_map,
             deFloat(mapBar(), 2).c_str(),
             (int)g_tmp,
             deFloat((float)g_vlt, 1).c_str(),
             deFloat((float)g_cur, 1).c_str(),
             (unsigned long)g_rxCnt,
             g_tuneActive ? 1 : 0,
             g_tuneSteps);
    f.close();
}

static String humanBytes(size_t bytes) {
    char buf[24];
    if (bytes >= 1048576) {
        snprintf(buf, sizeof(buf), "%.2f MB", bytes / 1048576.0f);
    } else if (bytes >= 1024) {
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0f);
    } else {
        snprintf(buf, sizeof(buf), "%u B", (unsigned)bytes);
    }
    return String(buf);
}

static size_t fileSize(const char* path) {
    if (!g_fsOk || !SPIFFS.exists(path)) return 0;
    File f = SPIFFS.open(path, FILE_READ);
    size_t sz = f ? f.size() : 0;
    if (f) f.close();
    return sz;
}

static void sendLogFile(const char* path, const char* downloadName) {
    if (!g_fsOk || !SPIFFS.exists(path)) {
        web.send(404, "text/plain", "Log file not found");
        return;
    }
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) {
        web.send(500, "text/plain", "Cannot open log file");
        return;
    }
    web.sendHeader("Content-Disposition", String("attachment; filename=\"") + downloadName + "\"");
    web.streamFile(f, "text/csv");
    f.close();
}

static void handleRoot() {
    String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    String mode = WiFi.status() == WL_CONNECTED ? "Home WiFi" : "Setup AP";
    String timeText = localTimestamp();
    String html;
    html.reserve(10200);
    html += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>M5Dial 123Tune</title><style>";
    html += "body{font-family:system-ui,Segoe UI,Arial;margin:24px;background:#111;color:#eee}";
    html += ".layout{display:grid;grid-template-columns:minmax(320px,700px) minmax(320px,590px);gap:22px;align-items:start}.mirrors{display:grid;grid-template-columns:repeat(2,minmax(270px,320px));gap:18px}.side{min-width:0}@media(max-width:1080px){.layout{display:block}}@media(max-width:680px){.mirrors{display:block}}";
    html += "a,button{display:inline-block;margin:6px 8px 6px 0;padding:10px 12px;background:#e94b1b;color:white;text-decoration:none;border:0;border-radius:4px}";
    html += "input{display:block;margin:6px 0 12px;padding:10px;width:min(360px,90vw)}";
    html += ".muted{color:#aaa}.box{border:1px solid #333;padding:14px;margin:0 0 14px;max-width:560px}";
    html += ".dial{width:min(82vw,320px);aspect-ratio:1;border-radius:50%;background:#050505;margin:4px 0 18px;position:relative;border:10px solid #242424;box-shadow:inset 0 0 38px #1d2830,0 0 16px #000;color:#ddd;overflow:hidden}";
    html += ".top{position:absolute;top:28px;left:0;right:0;font-size:14px;font-weight:700}.ble{position:absolute;left:72px;color:#2577ff}.ign{position:absolute;left:72px;top:16px;color:#e93b2f}.mode{position:absolute;right:72px;color:#3e75ff}";
    html += ".adv{position:absolute;top:76px;left:0;right:0;text-align:center;font-size:58px;line-height:1;font-weight:800;color:#f39c12}.lbl{font-size:14px;color:#ccc;font-weight:700;letter-spacing:0}.tunelbl{position:absolute;top:135px;left:0;right:0;text-align:center;font-size:16px;font-weight:800;color:#f39c12}";
    html += ".map{position:absolute;top:162px;left:0;right:0;text-align:center;font-size:28px;font-weight:800;color:#46b9ff}.maplbl{position:absolute;top:193px;left:0;right:0;text-align:center;color:#888;font-size:14px;font-weight:700}";
    html += ".rpm{position:absolute;bottom:30px;left:0;right:0;text-align:center;font-size:44px;font-weight:800;color:#fff}.rpmlbl{position:absolute;bottom:14px;left:0;right:0;text-align:center;color:#888;font-size:14px;font-weight:700}";
    html += ".big1{position:absolute;top:82px;left:0;right:0;text-align:center;font-size:58px;line-height:1;font-weight:800}.lbl1{position:absolute;top:138px;left:0;right:0;text-align:center;color:#ddd;font-size:16px;font-weight:800}.big2{position:absolute;top:174px;left:0;right:0;text-align:center;font-size:58px;line-height:1;font-weight:800}.lbl2{position:absolute;top:230px;left:0;right:0;text-align:center;color:#ddd;font-size:16px;font-weight:800}";
    html += ".screen-title{position:absolute;top:54px;left:0;right:0;text-align:center;font-size:22px;font-weight:800;color:#efefef}.items{position:absolute;top:92px;left:45px;right:42px;font-size:15px;font-weight:700;line-height:2}.item{display:flex;justify-content:space-between;color:#888}.item.sel{color:#f39c12}.on{color:#35d46b}.off{color:#777}.warn{color:#ff453a}.safe{color:#ffab19}.tunestate{position:absolute;top:92px;left:0;right:0;text-align:center;font-size:27px;font-weight:800}.tunehelp{position:absolute;top:128px;left:30px;right:30px;text-align:center;color:#aaa;font-size:13px;font-weight:700}.tunestep{position:absolute;top:164px;left:0;right:0;text-align:center;font-size:56px;font-weight:800}.tunemetric{position:absolute;bottom:28px;left:0;right:0;text-align:center;color:#aaa;font-size:14px;font-weight:700}";
    html += ".hidden{display:none}";
    html += ".red{color:#ff3838}.blue{color:#3aa0ff}.orange{color:#f39c12}";
    html += "</style></head><body><h2>M5Dial 123Tune</h2><div class='layout'><div class='mirrors'>";
    html += "<div class='dial'>";
    html += "<div class='top'><span id='ble' class='ble'>BLE</span><span id='ign' class='ign'>IGN #0</span><span id='mode' class='mode'>ADV</span></div>";
    html += "<div id='adv' class='adv orange'>0.0</div>";
    html += "<div id='tunelbl' class='tunelbl orange'>ADVANCE&nbsp; deg</div>";
    html += "<div id='map' class='map'>0.00</div><div class='maplbl'>MAP&nbsp; bar</div>";
    html += "<div id='rpm' class='rpm'>0</div><div class='rpmlbl'>RPM</div>";
    html += "</div>";
    html += "<div class='dial'>";
    html += "<div class='top'><span id='ble2' class='ble'>BLE</span><span id='ign2' class='ign'>IGN #0</span><span class='mode'>T/V</span></div>";
    html += "<div id='aux1' class='big1' style='color:#00ffff'>0</div><div class='lbl1'>TEMP&nbsp; degC</div>";
    html += "<div id='aux2' class='big2' style='color:#ffff00'>0.0</div><div class='lbl2'>VOLT&nbsp; V</div>";
    html += "</div>";
    html += "<div class='dial'><div class='top'><span id='ble3' class='ble'>BLE</span><span id='ign3' class='ign'>IGN #0</span><span class='mode'>SET</span></div>";
    html += "<div class='screen-title'>SETTINGS</div><div class='items'>";
    html += "<div id='set0' class='item'><span>Buzzer</span><span id='buzz' class='on'>ON</span></div>";
    html += "<div id='set1' class='item'><span>Button tone</span><span id='btnbeep' class='on'>ON</span></div>";
    html += "<div id='set2' class='item'><span>BLE tone</span><span id='blebeep' class='on'>ON</span></div>";
    html += "<div id='set3' class='item'><span>Error tone</span><span id='errbeep' class='on'>ON</span></div>";
    html += "<div id='set4' class='item'><span>Brightness</span><span id='bright'>200</span></div></div></div>";
    html += "<div class='dial'><div class='top'><span id='ble4' class='ble'>BLE</span><span id='ign4' class='ign'>IGN #0</span><span class='mode warn'>TUNE</span></div>";
    html += "<div class='screen-title warn'>LIVE TUNE</div><div id='tunestate' class='tunestate safe'>LOCKED</div>";
    html += "<div id='tunehelp' class='tunehelp'>Hold on device 2s to ARM</div><div id='tunestep' class='tunestep orange'>+0</div>";
    html += "<div id='tunemetric' class='tunemetric'>ADV 0.0 deg | RPM 0</div></div>";
    html += "</div><div class='side'>";
    html += "<div class='box'><div>Mode: " + mode + "</div><div>IP: " + ip + "</div>";
    html += "<div>Time: " + timeText + " (" + String(g_timeValid ? g_timeSource : "boot") + ")</div>";
    html += "<div>GW: " + WiFi.gatewayIP().toString() + " / DNS: " + WiFi.dnsIP().toString() + "</div>";
    html += "<div>RTC: " + String(g_rtcOk ? (g_rtcValid ? "valid" : "seen") : "missing") + " / NTP polls: " + String(g_ntpPolls) + "</div>";
    html += "<div>BLE: " + String(g_conn ? "connected" : "searching") + "</div>";
    html += "<div>RPM: " + String((int)g_rpm) + " / ADV: " + String((float)g_adv, 1) + " / MAP: " + String(mapBar(), 2) + " bar</div></div>";
    html += "<div class='box'><h3>Time</h3>";
    html += "<button onclick=\"fetch('/time_set?epoch='+Math.floor(Date.now()/1000)).then(()=>location.reload())\">Sync from browser</button>";
    html += "<p class='muted'>Uses this phone/laptop clock and stores it in the M5Dial RTC.</p></div>";
    html += "<div class='box'><h3>Logs</h3>";
    html += "<div>Current: " + humanBytes(fileSize(LOG_FILE)) + "</div>";
    html += "<div>Old rotated: " + humanBytes(fileSize(OLD_LOG_FILE)) + "</div>";
    html += "<a href='/download'>Download current CSV</a><a href='/download_old'>Download old CSV</a><a href='/clear'>Clear current log</a></div>";
    html += "<div class='box'><h3>Home WiFi</h3><form action='/wifi' method='get'>";
    html += "<input name='ssid' placeholder='SSID'><input name='pass' placeholder='Password' type='password'>";
    html += "<button type='submit'>Save WiFi and reboot</button></form>";
    html += "<a href='/wps'>Start WPS</a>";
    html += "<p class='muted'>WPS: first click Start WPS here, then press Connect/WPS on the FRITZ!Box.</p>";
    html += "<p class='muted'>Setup AP fallback: connect to M5Dial-123-Setup (DHCP automatic, no static client IP required), then open 192.168.4.1.</p></div></div></div>";
    html += "<script>";
    html += "function c(s){return s>0?'red':s<0?'blue':'orange'}";
    html += "function yn(id,on){let e=document.getElementById(id);e.textContent=on?'ON':'OFF';e.className=on?'on':'off'}";
    html += "function paint(d){if(!d)return;";
    html += "mode.textContent=d.tune_active?('T'+(d.tune_steps>=0?'+':'')+d.tune_steps):'ADV';";
    html += "adv.textContent=Number(d.adv).toFixed(1);adv.className='adv '+c(d.tune_steps);";
    html += "tunelbl.textContent=d.tune_active?('TUNE '+(d.tune_steps>=0?'+':'')+d.tune_steps):'ADVANCE  deg';tunelbl.className='tunelbl '+c(d.tune_steps);";
    html += "map.textContent=Number(d.map_bar).toFixed(2);rpm.textContent=d.rpm;aux1.textContent=d.temp;aux2.textContent=Number(d.volt).toFixed(1);";
    html += "yn('buzz',d.buzzer);yn('btnbeep',d.beep_actions);yn('blebeep',d.beep_ble);yn('errbeep',d.beep_errors);bright.textContent=d.brightness;";
    html += "for(let i=0;i<5;i++)document.getElementById('set'+i).className='item '+(i==d.setting_index?'sel':'');";
    html += "let st=d.tune_active?'LIVE':(d.tune_armed?'ARMED':'LOCKED');tunestate.textContent=st;tunestate.className='tunestate '+(d.tune_active?'warn':(d.tune_armed?'safe':'off'));";
    html += "tunehelp.textContent=d.tune_active?'Rotate on device +/-; hold 2s to EXIT':(d.tune_armed?'Hold on device 2s to START':'Hold on device 2s to ARM');";
    html += "tunestep.textContent=(d.tune_steps>=0?'+':'')+d.tune_steps;tunestep.className='tunestep '+c(d.tune_steps);tunemetric.textContent='ADV '+Number(d.adv).toFixed(1)+' deg | RPM '+d.rpm;}";
    html += "async function upd(){try{let r=await fetch('/state',{cache:'no-store'});let d=await r.json();";
    html += "ble.textContent=d.ble?'BLE OK':'Suche...';ble.style.color=d.ble?'#1ec85a':'#e33';";
    html += "ble2.textContent=ble.textContent;ble2.style.color=ble.style.color;ble3.textContent=ble.textContent;ble3.style.color=ble.style.color;ble4.textContent=ble.textContent;ble4.style.color=ble.style.color;";
    html += "ign.textContent='IGN #'+d.rx;ign2.textContent=ign.textContent;ign3.textContent=ign.textContent;ign4.textContent=ign.textContent;paint(d);}catch(e){}}";
    html += "upd();setInterval(upd,2000);</script>";
    html += "</body></html>";
    web.send(200, "text/html", html);
}

static void handleState() {
    String json;
    json.reserve(420);
    json += "{";
    json += "\"ble\":" + String(g_conn ? "true" : "false") + ",";
    json += "\"page\":\"" + String(pageName()) + "\",";
    json += "\"rx\":" + String((unsigned long)g_rxCnt) + ",";
    json += "\"rpm\":" + String((int)g_rpm) + ",";
    json += "\"adv\":" + String((float)g_adv, 1) + ",";
    json += "\"map\":" + String((int)g_map) + ",";
    json += "\"map_bar\":" + String(mapBar(), 2) + ",";
    json += "\"temp\":" + String((int)g_tmp) + ",";
    json += "\"volt\":" + String((float)g_vlt, 1) + ",";
    json += "\"tune_armed\":" + String(g_tuneArmed ? "true" : "false") + ",";
    json += "\"tune_active\":" + String(g_tuneActive ? "true" : "false") + ",";
    json += "\"tune_steps\":" + String(g_tuneSteps) + ",";
    json += "\"buzzer\":" + String(g_buzzerEnabled ? "true" : "false") + ",";
    json += "\"beep_actions\":" + String(g_beepActions ? "true" : "false") + ",";
    json += "\"beep_ble\":" + String(g_beepBle ? "true" : "false") + ",";
    json += "\"beep_errors\":" + String(g_beepErrors ? "true" : "false") + ",";
    json += "\"brightness\":" + String(g_brightness);
    json += ",\"setting_index\":" + String(g_settingIndex);
    json += "}";
    web.send(200, "application/json", json);
}

static void handleTimeSet() {
    if (!web.hasArg("epoch")) {
        web.send(400, "text/plain", "Missing epoch");
        return;
    }

    time_t epoch = (time_t)web.arg("epoch").toInt();
    if (!setSystemEpoch(epoch, "Browser")) {
        web.send(400, "text/plain", "Invalid epoch");
        return;
    }
    web.send(200, "text/plain", localTimestamp());
}

static void handleWifiSave() {
    String ssid = web.arg("ssid");
    String pass = web.arg("pass");
    ssid.trim();
    if (ssid.length() == 0) {
        web.send(400, "text/plain", "Missing ssid");
        return;
    }
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    web.send(200, "text/plain", "WiFi saved. Rebooting...");
    delay(500);
    ESP.restart();
}

static void handleClearLog() {
    if (g_fsOk) {
        SPIFFS.remove(LOG_FILE);
        ensureLogHeader();
    }
    web.sendHeader("Location", "/");
    web.send(302, "text/plain", "");
}

static void initWpsConfig() {
    memset(&wpsConfig, 0, sizeof(wpsConfig));
    wpsConfig.wps_type = WPS_TYPE_PBC;
    strncpy(wpsConfig.factory_info.manufacturer, "M5Stack", sizeof(wpsConfig.factory_info.manufacturer) - 1);
    strncpy(wpsConfig.factory_info.model_number, "M5Dial", sizeof(wpsConfig.factory_info.model_number) - 1);
    strncpy(wpsConfig.factory_info.model_name, "M5Dial 123Tune", sizeof(wpsConfig.factory_info.model_name) - 1);
    strncpy(wpsConfig.factory_info.device_name, "m5dial-123", sizeof(wpsConfig.factory_info.device_name) - 1);
}

static void stopWps() {
    if (!g_wpsActive) return;
    esp_wifi_wps_disable();
    g_wpsActive = false;
}

static bool startWps() {
    stopWps();
    initWpsConfig();
    WiFi.mode(g_wifiAp ? WIFI_AP_STA : WIFI_STA);
    esp_err_t en = esp_wifi_wps_enable(&wpsConfig);
    if (en != ESP_OK) {
        pushLog("WPS enable FAIL");
        return false;
    }
    esp_err_t st = esp_wifi_wps_start(0);
    if (st != ESP_OK) {
        esp_wifi_wps_disable();
        pushLog("WPS start FAIL");
        return false;
    }
    g_wpsActive = true;
    pushLog("WPS gestartet");
    return true;
}

static void handleWpsStart() {
    bool ok = startWps();
    web.send(200, "text/plain", ok ? "WPS started. Press Connect/WPS on FRITZ!Box now." : "WPS start failed");
}

static void setupWebGui() {
    web.on("/", HTTP_GET, handleRoot);
    web.on("/state", HTTP_GET, handleState);
    web.on("/time_set", HTTP_GET, handleTimeSet);
    web.on("/wifi", HTTP_GET, handleWifiSave);
    web.on("/wps", HTTP_GET, handleWpsStart);
    web.on("/clear", HTTP_GET, handleClearLog);
    web.on("/download", HTTP_GET, []() { sendLogFile(LOG_FILE, "m5dial_123tune_drive.csv"); });
    web.on("/download_old", HTTP_GET, []() { sendLogFile(OLD_LOG_FILE, "m5dial_123tune_drive_old.csv"); });
    web.onNotFound([]() {
        if (g_wifiAp) {
            web.sendHeader("Location", "http://192.168.4.1/", true);
            web.send(302, "text/plain", "");
            return;
        }
        web.send(404, "text/plain", "Not found");
    });
    web.begin();
}

static void onWifiEvent(WiFiEvent_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            if (g_saveWifiAfterWps) {
                String ssid = WiFi.SSID();
                String psk = WiFi.psk();
                if (ssid.length() > 0 && psk.length() > 0) {
                    prefs.putString("ssid", ssid);
                    prefs.putString("pass", psk);
                    pushLog("WPS gespeichert");
                } else {
                    pushLog("WPS ohne Key");
                }
                g_saveWifiAfterWps = false;
            }
            startNtpIfNeeded();
            break;
        case ARDUINO_EVENT_WPS_ER_SUCCESS:
            pushLog("WPS OK");
            stopWps();
            g_saveWifiAfterWps = true;
            delay(10);
            WiFi.begin();
            break;
        case ARDUINO_EVENT_WPS_ER_FAILED:
            pushLog("WPS fehlgeschl.");
            stopWps();
            break;
        case ARDUINO_EVENT_WPS_ER_TIMEOUT:
            pushLog("WPS timeout");
            stopWps();
            break;
        case ARDUINO_EVENT_WPS_ER_PBC_OVERLAP:
            pushLog("WPS overlap");
            stopWps();
            break;
        default:
            break;
    }
}


static void startSetupAp() {
    if (g_wifiAp) return;
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                      IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    // Open setup AP for maximum compatibility during onboarding.
    WiFi.softAP("M5Dial-123-Setup", nullptr, 6, 0, 4);
    dns.start(53, "*", IPAddress(192, 168, 4, 1));
    g_captiveActive = true;
    g_wifiAp = true;
    pushLog("AP 192.168.4.1");
}

static void setupWifi() {
    prefs.begin("net", false);
    loadUiSettings();
    WiFi.onEvent(onWifiEvent);
    WiFi.setHostname("m5dial-123");
    WiFi.mode(WIFI_STA);

    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    g_haveSavedWifi = ssid.length() > 0;
    if (ssid.length() > 0) {
        if (prefs.getBool("static", false)) {
            IPAddress ip, gw, mask, dns1;
            ip.fromString(prefs.getString("ip", "192.168.0.13"));
            gw.fromString(prefs.getString("gw", "192.168.0.1"));
            mask.fromString(prefs.getString("mask", "255.255.255.0"));
            dns1.fromString(prefs.getString("dns", "192.168.0.1"));
            WiFi.config(ip, gw, mask, dns1);
            pushLog("WiFi static %s", ip.toString().c_str());
        }
        WiFi.begin(ssid.c_str(), pass.c_str());
        pushLog("WiFi connect...");
    } else {
        startSetupAp();
    }
    setupWebGui();
}

static void maintainWifi() {
    if (g_captiveActive) dns.processNextRequest();
    web.handleClient();
    if (millis() - g_lastWifiCheck < 5000) return;
    g_lastWifiCheck = millis();

    if (WiFi.status() == WL_CONNECTED) {
        static bool announced = false;
        if (!announced) {
            announced = true;
            pushLog("WiFi %s", WiFi.localIP().toString().c_str());
        }
        startNtpIfNeeded();
        updateTimeState();
        if (!g_timeValid && g_ntpStarted && g_ntpPolls < 12) {
            g_ntpPolls++;
            pushLog("NTP warte %d", g_ntpPolls);
        }
        return;
    }

    if (g_haveSavedWifi && WiFi.status() != WL_CONNECTED) {
        static uint32_t lastReconnect = 0;
        if (millis() - lastReconnect >= 10000) {
            lastReconnect = millis();
            WiFi.reconnect();
            pushLog("WiFi retry...");
        }
        return;
    }
}

static void printWifiStatus() {
    String ssid = prefs.getString("ssid", "");
    Serial.printf("[WIFI] mode=%s conn=%d ip=%s gw=%s dns=%s saved_ssid=%s static=%d saved_ip=%s time=%s rtc=%d/%d ntp=%d polls=%d\n",
                  g_wifiAp ? "AP" : "STA",
                  WiFi.status() == WL_CONNECTED ? 1 : 0,
                  WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "-",
                  WiFi.gatewayIP().toString().c_str(),
                  WiFi.dnsIP().toString().c_str(),
                  ssid.length() ? ssid.c_str() : "<none>",
                  prefs.getBool("static", false) ? 1 : 0,
                  prefs.getString("ip", "-").c_str(),
                  localTimestamp().c_str(),
                  g_rtcOk ? 1 : 0,
                  g_rtcValid ? 1 : 0,
                  g_ntpStarted ? 1 : 0,
                  g_ntpPolls);
}

static bool tuneSendToggle();
static bool tuneStep(int dir);
static void tuneZero();

static void handleSerialCommand(String line) {
    line.trim();
    if (line.length() == 0) return;

    if (line.equalsIgnoreCase("wifi_status")) {
        printWifiStatus();
        return;
    }

    if (line.equalsIgnoreCase("wifi_clear")) {
        prefs.putString("ssid", "");
        prefs.putString("pass", "");
        prefs.putBool("static", false);
        Serial.println("[WIFI] cleared, rebooting");
        delay(300);
        ESP.restart();
        return;
    }

    if (line.equalsIgnoreCase("wifi_dhcp")) {
        prefs.putBool("static", false);
        Serial.println("[WIFI] DHCP enabled, rebooting");
        delay(300);
        ESP.restart();
        return;
    }

    if (line.equalsIgnoreCase("time_status")) {
        Serial.printf("[TIME] valid=%d epoch=%ld local=%s source=%s ntp_started=%d polls=%d rtc=%d/%d gw=%s dns=%s\n",
                      g_timeValid ? 1 : 0,
                      g_timeValid ? (long)time(nullptr) : 0L,
                      localTimestamp().c_str(),
                      g_timeSource,
                      g_ntpStarted ? 1 : 0,
                      g_ntpPolls,
                      g_rtcOk ? 1 : 0,
                      g_rtcValid ? 1 : 0,
                      WiFi.gatewayIP().toString().c_str(),
                      WiFi.dnsIP().toString().c_str());
        return;
    }

    if (line.startsWith("time_set ")) {
        String value = line.substring(9);
        value.trim();
        time_t epoch = (time_t)value.toInt();
        if (setSystemEpoch(epoch, "Serial")) {
            Serial.printf("[TIME] set ok epoch=%ld local=%s\n",
                          (long)epoch,
                          localTimestamp().c_str());
        } else {
            Serial.println("[TIME] invalid epoch");
        }
        return;
    }

    if (line.equalsIgnoreCase("tune_status")) {
        Serial.printf("[TUNE] armed=%d active=%d steps=%+d conn=%d rpm=%d adv=%.1f map=%d\n",
                      g_tuneArmed ? 1 : 0,
                      g_tuneActive ? 1 : 0,
                      g_tuneSteps,
                      g_conn ? 1 : 0,
                      (int)g_rpm,
                      (float)g_adv,
                      (int)g_map);
        return;
    }

    if (line.equalsIgnoreCase("tune_arm")) {
        g_tuneArmed = true;
        g_tuneArmedAt = millis();
        g_page = PAGE_TUNE;
        pushLog("Tune ARM 30s");
        Serial.println("[TUNE] armed. use tune_on, tune_up, tune_down, tune_zero, tune_off");
        return;
    }

    if (line.equalsIgnoreCase("tune_disarm")) {
        if (g_tuneActive) tuneSendToggle();
        g_tuneArmed = false;
        g_tuneArmedAt = 0;
        g_tuneSteps = 0;
        pushLog("Tune DISARM");
        Serial.println("[TUNE] disarmed");
        return;
    }

    if (line.equalsIgnoreCase("tune_on")) {
        g_page = PAGE_TUNE;
        if (!g_tuneActive) tuneSendToggle();
        return;
    }

    if (line.equalsIgnoreCase("tune_off")) {
        if (g_tuneActive) tuneSendToggle();
        return;
    }

    if (line.equalsIgnoreCase("tune_up")) {
        tuneStep(1);
        return;
    }

    if (line.equalsIgnoreCase("tune_down")) {
        tuneStep(-1);
        return;
    }

    if (line.equalsIgnoreCase("tune_zero")) {
        tuneZero();
        return;
    }

    if (line.startsWith("wifi_static ")) {
        // Format: wifi_static <ssid> <pass> <ip> [gateway] [mask] [dns]
        String parts[7];
        int count = 0;
        int start = 0;
        while (count < 7) {
            int sep = line.indexOf(' ', start);
            if (sep < 0) {
                parts[count++] = line.substring(start);
                break;
            }
            parts[count++] = line.substring(start, sep);
            start = sep + 1;
            while (start < (int)line.length() && line[start] == ' ') start++;
        }
        if (count < 4) {
            Serial.println("[WIFI] usage: wifi_static <ssid> <pass> <ip> [gateway] [mask] [dns]");
            return;
        }

        IPAddress ip, gw, mask, dns1;
        if (!ip.fromString(parts[3])) {
            Serial.println("[WIFI] invalid static ip");
            return;
        }
        gw.fromString(count > 4 ? parts[4] : "192.168.0.1");
        mask.fromString(count > 5 ? parts[5] : "255.255.255.0");
        dns1.fromString(count > 6 ? parts[6] : "192.168.0.1");

        prefs.putString("ssid", parts[1]);
        prefs.putString("pass", parts[2]);
        prefs.putBool("static", true);
        prefs.putString("ip", ip.toString());
        prefs.putString("gw", gw.toString());
        prefs.putString("mask", mask.toString());
        prefs.putString("dns", dns1.toString());
        Serial.printf("[WIFI] saved static ssid=%s ip=%s rebooting\n",
                      parts[1].c_str(), ip.toString().c_str());
        delay(300);
        ESP.restart();
        return;
    }

    if (line.startsWith("wifi ")) {
        // Format: wifi <ssid> <pass>
        int p1 = line.indexOf(' ');
        int p2 = line.indexOf(' ', p1 + 1);
        if (p2 < 0) {
            Serial.println("[WIFI] usage: wifi <ssid> <pass>");
            return;
        }
        String ssid = line.substring(p1 + 1, p2);
        String pass = line.substring(p2 + 1);
        ssid.trim();
        pass.trim();
        if (ssid.length() == 0) {
            Serial.println("[WIFI] empty ssid");
            return;
        }
        prefs.putString("ssid", ssid);
        prefs.putString("pass", pass);
        prefs.putBool("static", false);
        Serial.printf("[WIFI] saved ssid=%s rebooting\n", ssid.c_str());
        delay(300);
        ESP.restart();
        return;
    }

    Serial.println("[CMD] unknown. use: wifi_status | time_status | time_set <epoch> | tune_arm | tune_on | tune_up | tune_down | tune_zero | tune_off | tune_disarm | wifi_clear | wifi_dhcp | wifi <ssid> <pass> | wifi_static <ssid> <pass> <ip>");
}

static void pollSerialCommands() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            handleSerialCommand(g_serialLine);
            g_serialLine = "";
            continue;
        }
        if (g_serialLine.length() < 200) g_serialLine += c;
    }
}

// --- NimBLE callbacks ---
static void startScan();

class ClientCB : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient*) override {
        g_conn = true;
        pushLog("Verbunden!");
        beep(BEEP_BLE);
        logConnInfo("Conn");
    }
    void onDisconnect(NimBLEClient*, int reason) override {
        g_conn  = false;
        g_rxCnt = 0;
        pNusRx  = nullptr;
        pushLog("Disc reason=%d", reason);
        beep(BEEP_ERROR);
        startScan();
    }
    bool onConnParamsUpdateRequest(NimBLEClient*, const ble_gap_upd_params* p) override {
        pushLog("ParaReq %u-%u L%u T%u",
                p->itvl_min, p->itvl_max, p->latency, p->supervision_timeout);
        return true;  // Slave-Wunsch IMMER akzeptieren
    }
    void onMTUChange(NimBLEClient*, uint16_t mtu) override {
        pushLog("MTU=%u", mtu);
    }
};

class ScanCB : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        String addr = dev->getAddress().toString().c_str();
        addr.toLowerCase();
        if (addr == TARGET) {
            NimBLEDevice::getScan()->stop();
            targetAddr = dev->getAddress();
            doConnect  = true;
            pushLog("Gefunden!");
        }
    }
};

static ClientCB clientCB;
static ScanCB   scanCB;

static void startScan() {
    pushLog("Scan...");
    auto* s = NimBLEDevice::getScan();
    s->setScanCallbacks(&scanCB);
    s->setActiveScan(true);
    s->setInterval(100);
    s->setWindow(99);
    s->start(0, false);
}

// Notify handler: receives every notification regardless of characteristic
static void onAnyNotify(NimBLERemoteCharacteristic* chr,
                        uint8_t* data, size_t len, bool isNotify) {
    if (g_rawlog) {
        Serial.printf("[%6lums] NTFY handle=%u len=%u :",
                      millis(), chr->getHandle(), (unsigned)len);
        for (size_t i = 0; i < len && i < 20; i++) Serial.printf(" %02X", data[i]);
        Serial.println();
    }
    decodeFrame(data, len);
}

static void sendRaytacPing() {
    if (!g_conn || !pClient || !pClient->isConnected() || !pNusRx) return;

    const uint8_t ping = '$';
    bool ok = pNusRx->writeValue(&ping, 1, true);
    Serial.printf("[%6lums] TX ping '$' -> %s\n", millis(), ok ? "OK" : "FAIL");
}

static void sendRaytacEnter() {
    if (!g_conn || !pClient || !pClient->isConnected() || !pNusRx) return;

    const uint8_t cr = '\r';
    bool ok = pNusRx->writeValue(&cr, 1, true);
    Serial.printf("[%6lums] TX enter CR -> %s\n", millis(), ok ? "OK" : "FAIL");
}

static void sendRaytacCommand(const char* command) {
    if (!g_conn || !pClient || !pClient->isConnected() || !pNusRx) return;

    char buf[24];
    snprintf(buf, sizeof(buf), "%s\r$", command);
    bool ok = pNusRx->writeValue((uint8_t*)buf, strlen(buf), true);
    Serial.printf("[%6lums] TX cmd '%s\\\\r$' -> %s\n",
                  millis(), command, ok ? "OK" : "FAIL");
}

static bool sendRaytacCommandChecked(const char* command) {
    if (!g_conn || !pClient || !pClient->isConnected() || !pNusRx) {
        pushLog("Tune: kein BLE");
        return false;
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "%s\r$", command);
    bool ok = pNusRx->writeValue((uint8_t*)buf, strlen(buf), true);
    Serial.printf("[%6lums] TX tune '%s\\\\r$' -> %s\n",
                  millis(), command, ok ? "OK" : "FAIL");
    return ok;
}

static bool tuneSendToggle() {
    if (!g_tuneArmed) {
        pushLog("Tune gesperrt");
        beep(BEEP_ERROR);
        return false;
    }
    if (!sendRaytacCommandChecked("T")) return false;
    g_tuneActive = !g_tuneActive;
    if (g_tuneActive) {
        g_tuneSteps = 0;
        pushLog("Tune EIN");
        beep(BEEP_ERROR);
    } else {
        pushLog("Tune AUS");
        beep(BEEP_ACTION);
    }
    return true;
}

static bool tuneStep(int dir) {
    if (!g_tuneArmed || !g_tuneActive) {
        pushLog("Tune nicht aktiv");
        beep(BEEP_ERROR);
        return false;
    }
    if ((dir > 0 && g_tuneSteps >= kTuneMaxSteps) ||
        (dir < 0 && g_tuneSteps <= -kTuneMaxSteps)) {
        pushLog("Tune Limit %+d", g_tuneSteps);
        beep(BEEP_ERROR);
        return false;
    }
    if (millis() - g_lastTuneStepMs < 150) return false;
    g_lastTuneStepMs = millis();

    const char* cmd = dir > 0 ? "A" : "R";
    if (!sendRaytacCommandChecked(cmd)) return false;
    g_tuneSteps += dir > 0 ? 1 : -1;
    pushLog("Tune %+d", g_tuneSteps);
    beep(BEEP_ACTION);
    return true;
}

static void tuneZero() {
    if (!g_tuneArmed || !g_tuneActive) {
        pushLog("Tune nicht aktiv");
        return;
    }
    int guard = 0;
    while (g_tuneSteps != 0 && guard++ < 30) {
        int dir = g_tuneSteps > 0 ? -1 : 1;
        g_lastTuneStepMs = 0;
        if (!tuneStep(dir)) break;
        delay(180);
    }
    pushLog("Tune zero %+d", g_tuneSteps);
}

static void runReadOnlyDump() {
    if (!g_conn || !pClient || !pClient->isConnected() || !pNusRx) return;

    g_readBusy = true;
    pushLog("Read dump...");
    sendRaytacCommand("v@");
    delay(500);
    sendRaytacCommand("10@");
    delay(500);
    sendRaytacCommand("11@");
    delay(500);
    sendRaytacCommand("12@");
    delay(500);
    sendRaytacCommand("13@");
    g_readBusy = false;
    pushLog("Read dump OK");
}


static void connectBLE() {
    pushLog("Verbinde...");
    if (!pClient) {
        pClient = NimBLEDevice::createClient();
        pClient->setClientCallbacks(&clientCB, false);
    }
    // Match the 123\TUNE+ preferred parameters from the nRF Connect trace.
    pClient->setConnectionParams(16, 32, 0, 400);
    if (!pClient->connect(targetAddr, true, false, false)) {
        pushLog("Conn FAIL");
        startScan(); return;
    }
    logConnInfo("PostConnect");
    delay(750);

    // ALLE Services dumpen
    auto& svcs = pClient->getServices(true);
    Serial.printf("[%6lums] === Services: %u ===\n", millis(), (unsigned)svcs.size());
    for (auto* s : svcs) {
        Serial.printf("[%6lums] SVC %s\n", millis(), s->getUUID().toString().c_str());
        for (auto* c : s->getCharacteristics(true)) {
            Serial.printf("[%6lums]   CHR %s h=%u prop=%02X N=%d I=%d R=%d W=%d\n",
                          millis(), c->getUUID().toString().c_str(),
                          c->getHandle(),
                          charProps(c),
                          c->canNotify(), c->canIndicate(),
                          c->canRead(), c->canWrite());
        }
    }

    auto* svc = pClient->getService(NUS_SVC);
    if (!svc) {
        pushLog("Kein NUS!");
        return;
    }

    auto* tx = svc->getCharacteristic(NUS_TX);
    auto* rx = svc->getCharacteristic(NUS_RX);
    pNusRx = rx;
    if (!tx) {
        pushLog("Kein NUS TX!");
        return;
    }

    Serial.printf("[%6lums] NUS TX h=%u prop=%02X N=%d I=%d\n",
                  millis(), tx->getHandle(), charProps(tx), tx->canNotify(), tx->canIndicate());
    if (rx) {
        Serial.printf("[%6lums] NUS RX h=%u prop=%02X W=%d WNR=%d\n",
                      millis(), rx->getHandle(), charProps(rx),
                      rx->canWrite(), rx->canWriteNoResponse());
    }

    auto* cccd = tx->getDescriptor(NimBLEUUID((uint16_t)0x2902));
    if (cccd) {
        uint16_t off = 0x0000;
        bool offOk = cccd->writeValue((uint8_t*)&off, 2, true);
        delay(150);
        Serial.printf("[%6lums] NUS CCCD off -> %s\n", millis(), offOk ? "OK" : "FAIL");
    } else {
        Serial.printf("[%6lums] NUS CCCD fehlt\n", millis());
    }

    bool ok = tx->subscribe(true, onAnyNotify, true);
    Serial.printf("[%6lums] sub NUS TX -> %s\n", millis(), ok ? "OK" : "FAIL");

    if (cccd) {
        NimBLEAttValue v = cccd->readValue();
        Serial.printf("[%6lums] NUS CCCD read len=%u :",
                      millis(), (unsigned)v.length());
        for (size_t i = 0; i < v.length(); i++) Serial.printf(" %02X", v.data()[i]);
        Serial.println();
    }

    pushLog("Sub NUS: %s", ok ? "OK" : "FAIL");
    sendRaytacPing();
    delay(120);
    sendRaytacEnter();
    if (kReadOnConnect) {
        delay(250);
        runReadOnlyDump();
    }
}

// --- Display ---
// Status bar top: shifted down to stay inside the visible round display area.
static void drawStatus() {
    display.fillRect(0, 0, 240, 44, TFT_BLACK);
    display.setFont(&fonts::FreeSans9pt7b);

    display.setTextDatum(ML_DATUM);
    display.setTextColor(g_conn ? (uint32_t)TFT_GREEN : (uint32_t)TFT_RED);
    display.drawString(g_conn ? "BLE OK" : "Suche...", 66, 20);

    display.setTextColor(0x404040);
    char buf[16];
    snprintf(buf, sizeof(buf), "IGN #%lu", (unsigned long)g_rxCnt);
    display.drawString(buf, 66, 34);

    display.setTextDatum(MR_DATUM);
    if (g_tuneActive) {
        display.setTextColor(g_tuneSteps > 0 ? (uint32_t)TFT_RED :
                             g_tuneSteps < 0 ? (uint32_t)TFT_SKYBLUE :
                                               (uint32_t)TFT_ORANGE);
        char tuneBuf[18];
        snprintf(tuneBuf, sizeof(tuneBuf), "T%+d", g_tuneSteps);
        display.drawString(tuneBuf, 184, 27);
    } else {
        display.setTextColor(g_tuneArmed ? (uint32_t)TFT_ORANGE : (uint32_t)0x303030);
        display.drawString(pageName(), 184, 27);
    }
}

// Log view: show BLE steps until data arrives
static void drawLog() {
    display.fillRect(0, 44, 240, 196, TFT_BLACK);
    display.setFont(&fonts::FreeSans9pt7b);
    int start = (g_logN > NLOG) ? g_logN - NLOG : 0;
    int count = min(g_logN, NLOG);
    for (int i = 0; i < count; i++) {
        int slot   = (start + i) % NLOG;
        bool newest = (start + i == g_logN - 1);
        display.setTextDatum(ML_DATUM);
        display.setTextColor(newest ? (uint32_t)TFT_WHITE : (uint32_t)0x444444);
        display.drawString(g_log[slot], 18, 62 + i * 25);
    }
}

// Data halves (102px sprite each)
static void drawHalf(LGFX_Sprite& spr, const char* val, const char* lbl,
                     uint32_t col, int pushY) {
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(col);
    spr.setFont(&fonts::Font7);
    spr.drawString(val, 120, 46);
    spr.setFont(&fonts::FreeSans9pt7b);
    spr.setTextColor(TFT_DARKGREY);
    spr.drawString(lbl, 120, 88);
    spr.pushSprite(0, pushY);
}

static void drawMain() {
    char buf[16];
    display.fillRect(0, 44, 240, 196, TFT_BLACK);
    display.setTextDatum(MC_DATUM);

    snprintf(buf, sizeof(buf), "%.1f", (float)g_adv);
    display.setFont(&fonts::Font7);
    display.setTextColor(g_tuneSteps > 0 ? (uint32_t)TFT_RED :
                         g_tuneSteps < 0 ? (uint32_t)TFT_SKYBLUE :
                                           (uint32_t)TFT_ORANGE);
    display.drawString(buf, 120, 72);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(TFT_DARKGREY);
    if (g_tuneActive) {
        char tuneBuf[18];
        snprintf(tuneBuf, sizeof(tuneBuf), "TUNE %+d", g_tuneSteps);
        display.setTextColor(g_tuneSteps > 0 ? (uint32_t)TFT_RED :
                             g_tuneSteps < 0 ? (uint32_t)TFT_SKYBLUE :
                                               (uint32_t)TFT_ORANGE);
        display.drawString(tuneBuf, 120, 102);
        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(TFT_DARKGREY);
        display.drawString("ADVANCE  deg", 120, 119);
    } else {
        display.drawString("ADVANCE  deg", 120, 106);
    }

    snprintf(buf, sizeof(buf), "%.2f", mapBar());
    display.setFont(&fonts::Font4);
    display.setTextColor(TFT_SKYBLUE);
    display.drawString(buf, 120, g_tuneActive ? 148 : 140);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(TFT_DARKGREY);
    display.drawString("MAP  bar", 120, g_tuneActive ? 170 : 162);

    snprintf(buf, sizeof(buf), "%d", (int)g_rpm);
    display.setFont(&fonts::Font6);
    display.setTextColor(TFT_WHITE);
    display.drawString(buf, 120, g_tuneActive ? 202 : 199);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(TFT_DARKGREY);
    display.drawString("RPM", 120, 224);
}

static void drawAux() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", (float)g_tmp);
    drawHalf(sprTop, buf, "TEMP  degC", TFT_CYAN, 44);
    snprintf(buf, sizeof(buf), "%.1f", (float)g_vlt);
    drawHalf(sprBot, buf, "VOLT  V", TFT_YELLOW, 140);
}

static void drawSettings() {
    const char* labels[] = { "Buzzer", "Button tone", "BLE tone", "Error tone", "Brightness" };
    bool values[] = { g_buzzerEnabled, g_beepActions, g_beepBle, g_beepErrors };
    display.fillRect(0, 44, 240, 196, TFT_BLACK);
    display.setTextDatum(MC_DATUM);
    display.setFont(&fonts::FreeSans12pt7b);
    display.setTextColor(TFT_WHITE);
    display.drawString("SETTINGS", 120, 58);

    display.setFont(&fonts::FreeSans9pt7b);
    for (uint8_t i = 0; i < 5; ++i) {
        int y = 92 + i * 27;
        display.setTextDatum(ML_DATUM);
        display.setTextColor(i == g_settingIndex ? (uint32_t)TFT_ORANGE : (uint32_t)TFT_DARKGREY);
        display.drawString(i == g_settingIndex ? ">" : " ", 25, y);
        display.drawString(labels[i], 43, y);
        display.setTextDatum(MR_DATUM);
        char value[8];
        if (i < 4) {
            snprintf(value, sizeof(value), "%s", values[i] ? "ON" : "OFF");
            display.setTextColor(values[i] ? (uint32_t)TFT_GREEN : (uint32_t)TFT_DARKGREY);
        } else {
            snprintf(value, sizeof(value), "%u", g_brightness);
            display.setTextColor(TFT_SKYBLUE);
        }
        display.drawString(value, 208, y);
    }
}

static void drawTune() {
    display.fillRect(0, 44, 240, 196, TFT_BLACK);
    display.setTextDatum(MC_DATUM);
    display.setFont(&fonts::FreeSans12pt7b);
    display.setTextColor(TFT_RED);
    display.drawString("LIVE TUNE", 120, 58);

    const char* state = g_tuneActive ? "LIVE" : (g_tuneArmed ? "ARMED" : "LOCKED");
    uint32_t stateColor = g_tuneActive ? (uint32_t)TFT_RED :
                          g_tuneArmed ? (uint32_t)TFT_ORANGE :
                                        (uint32_t)TFT_DARKGREY;
    display.setFont(&fonts::Font4);
    display.setTextColor(stateColor);
    display.drawString(state, 120, 92);

    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(TFT_DARKGREY);
    display.drawString(g_tuneActive ? "DREHEN +/-  HOLD EXIT" :
                       (g_tuneArmed ? "HOLD 2s START" : "HOLD 2s ARM"), 120, 118);

    char buf[18];
    snprintf(buf, sizeof(buf), "%+d", g_tuneSteps);
    display.setFont(&fonts::Font7);
    display.setTextColor(g_tuneSteps > 0 ? (uint32_t)TFT_RED :
                         g_tuneSteps < 0 ? (uint32_t)TFT_SKYBLUE :
                                           (uint32_t)TFT_ORANGE);
    display.drawString(buf, 120, 160);
    snprintf(buf, sizeof(buf), "ADV %.1f   RPM %d", (float)g_adv, (int)g_rpm);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(TFT_DARKGREY);
    display.drawString(buf, 120, 215);
}

static void changeSettingSelection(int dir) {
    int next = static_cast<int>(g_settingIndex) + dir;
    if (next < 0) next = 4;
    if (next > 4) next = 0;
    g_settingIndex = static_cast<uint8_t>(next);
    beep(BEEP_ACTION);
}

static void activateSetting() {
    switch (g_settingIndex) {
        case 0:
            g_buzzerEnabled = !g_buzzerEnabled;
            if (!g_buzzerEnabled) stopBeep();
            break;
        case 1: g_beepActions = !g_beepActions; break;
        case 2: g_beepBle = !g_beepBle; break;
        case 3: g_beepErrors = !g_beepErrors; break;
        case 4:
            g_brightness = g_brightness < 120 ? 140 : (g_brightness < 180 ? 200 : (g_brightness < 230 ? 255 : 80));
            display.setBrightness(g_brightness);
            break;
    }
    saveUiSettings();
    if (g_buzzerEnabled) beep(BEEP_ACTION);
}

static void handleEncoder() {
    static uint8_t lastState = 0;
    static bool initialized = false;

    uint8_t state = (digitalRead(ENC_A_PIN) ? 1 : 0) |
                    (digitalRead(ENC_B_PIN) ? 2 : 0);
    if (!initialized) {
        initialized = true;
        lastState = state;
        return;
    }
    if (state == lastState) return;

    static const int8_t table[16] = {
        0, -1,  1,  0,
        1,  0,  0, -1,
       -1,  0,  0,  1,
        0,  1, -1,  0
    };
    int8_t delta = table[(lastState << 2) | state];
    lastState = state;
    if (delta == 0) return;

    g_encoderAccum += delta;
    if (g_encoderAccum >= 4) {
        g_encoderAccum = 0;
        if (g_page == PAGE_SETTINGS) changeSettingSelection(1);
        else if (g_page == PAGE_TUNE && g_tuneActive) tuneStep(1);
    } else if (g_encoderAccum <= -4) {
        g_encoderAccum = 0;
        if (g_page == PAGE_SETTINGS) changeSettingSelection(-1);
        else if (g_page == PAGE_TUNE && g_tuneActive) tuneStep(-1);
    }
}

// Short press pages through the UI; long press acts only within the visible context.
static void handleButton() {
    static bool     lastBtn   = HIGH;
    static uint32_t pressTime = 0;
    static bool     longFired = false;

    bool btn = digitalRead(BTN_PIN);
    if (btn == LOW && lastBtn == HIGH) { pressTime = millis(); longFired = false; }
    uint32_t holdMs = g_page == PAGE_TUNE ? TUNE_HOLD_MS : LONG_PRESS_MS;
    if (btn == LOW && !longFired && millis() - pressTime >= holdMs) {
        longFired = true;
        if (g_page == PAGE_SETTINGS) {
            activateSetting();
        } else if (g_page == PAGE_TUNE) {
            if (g_tuneActive) {
                tuneZero();
                if (g_tuneSteps == 0 && tuneSendToggle()) {
                    g_tuneArmed = false;
                    g_tuneArmedAt = 0;
                    pushLog("Tune SAFE EXIT");
                }
            } else if (!g_tuneArmed) {
                g_tuneArmed = true;
                g_tuneArmedAt = millis();
                pushLog("Tune ARM 30s");
                beep(BEEP_ERROR);
            } else if (tuneSendToggle()) {
                g_tuneArmedAt = 0;
            }
        } else {
            g_readRequested = true;
            pushLog("Read angefragt");
            beep(BEEP_ACTION);
        }
    }
    if (btn == HIGH && lastBtn == LOW && !longFired) {
        advancePage();
    }
    lastBtn = btn;
}

// --- Setup / Loop ---
void setup() {
    Serial.begin(115200);
    delay(1200);
    for (int i = 0; i < 5; ++i) {
        Serial.printf("\n=== M5Dial 123TUNE+ boot %d ===\n", i + 1);
        delay(200);
    }

    display.init();
    display.setRotation(2);
    display.fillScreen(TFT_BLACK);
    display.setBrightness(g_brightness);

    sprTop.createSprite(240, 102);
    sprBot.createSprite(240, 102);

    pinMode(BTN_PIN, INPUT_PULLUP);
    pinMode(ENC_A_PIN, INPUT_PULLUP);
    pinMode(ENC_B_PIN, INPUT_PULLUP);
    pinMode(TOUCH_INT_PIN, INPUT_PULLUP);
    ledcSetup(kBuzzerChannel, 4000, 8);
    ledcAttachPin(BUZZER_PIN, kBuzzerChannel);
    stopBeep();

    pushLog("Start...");
    setupRtcTime();
    g_fsOk = SPIFFS.begin(true);
    pushLog("SPIFFS: %s", g_fsOk ? "OK" : "FAIL");
    ensureLogHeader();
    setupWifi();

    NimBLEDevice::init("M5Dial-NUS");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(23);
    startScan();
}

void loop() {
    pollSerialCommands();
    maintainWifi();
    serviceBuzzer();
    handleEncoder();
    handleButton();
    handleTouch();

    if (g_tuneArmed && !g_tuneActive && g_tuneArmedAt != 0 &&
        millis() - g_tuneArmedAt >= kTuneArmTimeoutMs) {
        g_tuneArmed = false;
        g_tuneArmedAt = 0;
        pushLog("Tune ARM timeout");
        beep(BEEP_ERROR);
    }

    if (doConnect) { doConnect = false; connectBLE(); }

    if (g_readRequested && !g_readBusy) {
        g_readRequested = false;
        runReadOnlyDump();
    }

    // The original 123\TUNE+ Android app pings BLE devices every 1650 ms.
    static uint32_t lastPing = 0;
    if (g_conn && millis() - lastPing >= 1650) {
        lastPing = millis();
        sendRaytacPing();
    }

    // Heartbeat: jede Sekunde Status loggen wenn verbunden aber keine Daten
    static uint32_t lastHb = 0;
    if (g_conn && g_rxCnt == 0 && millis() - lastHb >= 1000) {
        lastHb = millis();
        Serial.printf("[%6lums] HB conn=%d rx=%lu\n",
                      millis(), pClient && pClient->isConnected() ? 1 : 0,
                      (unsigned long)g_rxCnt);
    }

    static uint32_t lastLive = 0;
    if (g_conn && g_rxCnt > 0 && g_rpm > kLogMinRpm && millis() - lastLive >= 500) {
        lastLive = millis();
        printLiveSummary();
        appendLiveCsv();
    }

    drawStatus();
    if (g_rxCnt == 0 && (g_page == PAGE_MAIN || g_page == PAGE_AUX)) {
        drawLog();
    } else if (g_page == PAGE_MAIN) {
        drawMain();
    } else if (g_page == PAGE_AUX) {
        drawAux();
    } else if (g_page == PAGE_SETTINGS) {
        drawSettings();
    } else {
        drawTune();
    }

    delay(80);
}
