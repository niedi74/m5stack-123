#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Preferences.h>
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
#define LONG_PRESS_MS 600

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
static bool              g_view  = false;   // false=ADV/RPM  true=TMP/VLT
static bool              g_rawlog = false;
static bool              g_readRequested = false;
static bool              g_readBusy = false;

static NimBLEClient* pClient   = nullptr;
static NimBLERemoteCharacteristic* pNusRx = nullptr;
static NimBLEAddress targetAddr;
static volatile bool  doConnect = false;

static constexpr bool kReadOnConnect = false;  // live mode stays quiet; long press starts read-only dump
static constexpr float kLogMinRpm = 650.0f;    // suppress ignition/start-only noise in drive logs

// --- Local logging / Web GUI ---
static WebServer   web(80);
static Preferences prefs;
static bool        g_fsOk = false;
static bool        g_wifiAp = false;
static bool        g_wpsActive = false;
static bool        g_saveWifiAfterWps = false;
static uint32_t    g_lastWifiCheck = 0;

static const char* LOG_FILE = "/drive.csv";
static const char* OLD_LOG_FILE = "/drive_old.csv";
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

static String bootTimestamp() {
    char buf[24];
    snprintf(buf, sizeof(buf), "BOOT+%lu", (unsigned long)millis());
    return String(buf);
}

static void ensureLogHeader() {
    if (!g_fsOk) return;
    bool needsHeader = !SPIFFS.exists(LOG_FILE);
    if (!needsHeader) {
        File existing = SPIFFS.open(LOG_FILE, FILE_READ);
        needsHeader = !existing || existing.size() == 0;
        if (existing) existing.close();
    }
    if (needsHeader) {
        File f = SPIFFS.open(LOG_FILE, FILE_WRITE);
        if (f) {
            f.println("ms,time,rpm,advance_deg,map_kpa,temp_c,volt_v,coil_a,rx");
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
    f.printf("%lu,%s,%d,%.1f,%d,%d,%.1f,%.1f,%lu\n",
             (unsigned long)millis(),
             bootTimestamp().c_str(),
             (int)g_rpm,
             (float)g_adv,
             (int)g_map,
             (int)g_tmp,
             (float)g_vlt,
             (float)g_cur,
             (unsigned long)g_rxCnt);
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
    String html;
    html.reserve(2200);
    html += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>M5Dial 123Tune</title><style>";
    html += "body{font-family:system-ui,Segoe UI,Arial;margin:24px;background:#111;color:#eee}";
    html += "a,button{display:inline-block;margin:6px 8px 6px 0;padding:10px 12px;background:#e94b1b;color:white;text-decoration:none;border:0;border-radius:4px}";
    html += "input{display:block;margin:6px 0 12px;padding:10px;width:min(360px,90vw)}";
    html += ".muted{color:#aaa}.box{border:1px solid #333;padding:14px;margin:14px 0;max-width:560px}";
    html += "</style></head><body><h2>M5Dial 123Tune Logger</h2>";
    html += "<div class='box'><div>Mode: " + mode + "</div><div>IP: " + ip + "</div>";
    html += "<div>BLE: " + String(g_conn ? "connected" : "searching") + "</div>";
    html += "<div>RPM: " + String((int)g_rpm) + " / ADV: " + String((float)g_adv, 1) + " / MAP: " + String((int)g_map) + "</div></div>";
    html += "<div class='box'><h3>Logs</h3>";
    html += "<div>Current: " + humanBytes(fileSize(LOG_FILE)) + "</div>";
    html += "<div>Old rotated: " + humanBytes(fileSize(OLD_LOG_FILE)) + "</div>";
    html += "<a href='/download'>Download current CSV</a><a href='/download_old'>Download old CSV</a><a href='/clear'>Clear current log</a></div>";
    html += "<div class='box'><h3>Home WiFi</h3><form action='/wifi' method='get'>";
    html += "<input name='ssid' placeholder='SSID'><input name='pass' placeholder='Password' type='password'>";
    html += "<button type='submit'>Save WiFi and reboot</button></form>";
    html += "<a href='/wps'>Start WPS</a>";
    html += "<p class='muted'>WPS: first click Start WPS here, then press Connect/WPS on the FRITZ!Box.</p>";
    html += "<p class='muted'>If no home WiFi is saved, connect to AP M5Dial-123-Setup and open 192.168.4.1.</p></div>";
    html += "</body></html>";
    web.send(200, "text/html", html);
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
    web.on("/wifi", HTTP_GET, handleWifiSave);
    web.on("/wps", HTTP_GET, handleWpsStart);
    web.on("/clear", HTTP_GET, handleClearLog);
    web.on("/download", HTTP_GET, []() { sendLogFile(LOG_FILE, "m5dial_123tune_drive.csv"); });
    web.on("/download_old", HTTP_GET, []() { sendLogFile(OLD_LOG_FILE, "m5dial_123tune_drive_old.csv"); });
    web.begin();
}

static void onWifiEvent(WiFiEvent_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            if (g_saveWifiAfterWps) {
                prefs.putString("ssid", WiFi.SSID());
                prefs.putString("pass", WiFi.psk());
                g_saveWifiAfterWps = false;
                pushLog("WPS gespeichert");
            }
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
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("M5Dial-123-Setup", "12345678");
    g_wifiAp = true;
    pushLog("AP 192.168.4.1");
}

static void setupWifi() {
    prefs.begin("net", false);
    WiFi.onEvent(onWifiEvent);
    WiFi.setHostname("m5dial-123");
    WiFi.mode(WIFI_STA);

    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    if (ssid.length() > 0) {
        WiFi.begin(ssid.c_str(), pass.c_str());
        pushLog("WiFi connect...");
    } else {
        startSetupAp();
    }
    setupWebGui();
}

static void maintainWifi() {
    web.handleClient();
    if (millis() - g_lastWifiCheck < 5000) return;
    g_lastWifiCheck = millis();

    if (WiFi.status() == WL_CONNECTED) {
        static bool announced = false;
        if (!announced) {
            announced = true;
            pushLog("WiFi %s", WiFi.localIP().toString().c_str());
        }
        return;
    }

    if (prefs.getString("ssid", "").length() > 0 && millis() > 20000) {
        startSetupAp();
    }
}

// --- NimBLE callbacks ---
static void startScan();

class ClientCB : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient*) override {
        g_conn = true;
        pushLog("Verbunden!");
        logConnInfo("Conn");
    }
    void onDisconnect(NimBLEClient*, int reason) override {
        g_conn  = false;
        g_rxCnt = 0;
        pNusRx  = nullptr;
        pushLog("Disc reason=%d", reason);
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
    display.setTextColor(0x303030);
    display.drawString(g_view ? "T/V" : "ADV", 184, 27);
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
    display.setTextColor(TFT_ORANGE);
    display.drawString(buf, 120, 78);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(TFT_DARKGREY);
    display.drawString("ADVANCE  deg", 120, 115);

    snprintf(buf, sizeof(buf), "%d", (int)g_map);
    display.setFont(&fonts::Font4);
    display.setTextColor(TFT_SKYBLUE);
    display.drawString(buf, 120, 151);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(TFT_DARKGREY);
    display.drawString("MAP  kPa", 120, 174);

    snprintf(buf, sizeof(buf), "%d", (int)g_rpm);
    display.setFont(&fonts::Font7);
    display.setTextColor(TFT_WHITE);
    display.drawString(buf, 120, 205);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(TFT_DARKGREY);
    display.drawString("RPM", 120, 229);
}

static void drawAux() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", (float)g_tmp);
    drawHalf(sprTop, buf, "TEMP  degC", TFT_CYAN, 44);
    snprintf(buf, sizeof(buf), "%.1f", (float)g_vlt);
    drawHalf(sprBot, buf, "VOLT  V", TFT_YELLOW, 140);
}

// --- Button: short = view, long = read-only dump ---
static void handleButton() {
    static bool     lastBtn   = HIGH;
    static uint32_t pressTime = 0;
    static bool     longFired = false;

    bool btn = digitalRead(BTN_PIN);
    if (btn == LOW && lastBtn == HIGH) { pressTime = millis(); longFired = false; }
    if (btn == LOW && !longFired && millis() - pressTime >= LONG_PRESS_MS) {
        longFired = true;
        g_readRequested = true;
        pushLog("Read angefragt");
    }
    if (btn == HIGH && lastBtn == LOW && !longFired) {
        g_view = !g_view;
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
    display.setBrightness(200);

    sprTop.createSprite(240, 102);
    sprBot.createSprite(240, 102);

    pinMode(BTN_PIN, INPUT_PULLUP);

    pushLog("Start...");
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
    maintainWifi();
    handleButton();

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
    if (g_rxCnt == 0) {
        drawLog();
    } else {
        g_view ? drawAux() : drawMain();
    }

    delay(80);
}
