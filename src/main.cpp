#include <Arduino.h>
#include <NimBLEDevice.h>
#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_GC9A01.hpp>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>
#include <lgfx/v1/platforms/esp32/Light_PWM.hpp>

// ─── GC9A01 Display ───────────────────────────────────────────────────────────
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

// ─── BLE NUS ──────────────────────────────────────────────────────────────────
static const char* NUS_SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char* NUS_TX  = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
static const char* TARGET  = "ef:a8:b2:de:e0:9e";

// ─── Hardware ─────────────────────────────────────────────────────────────────
#define BTN_PIN       42
#define LONG_PRESS_MS 600

// ─── On-screen log ────────────────────────────────────────────────────────────
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

// ─── App State ────────────────────────────────────────────────────────────────
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

static NimBLEClient* pClient   = nullptr;
static NimBLEAddress targetAddr;
static volatile bool  doConnect = false;

// ─── Frame decoder ────────────────────────────────────────────────────────────
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

// ─── NimBLE callbacks ─────────────────────────────────────────────────────────
static void startScan();

class ClientCB : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient*) override {
        g_conn = true;
        pushLog("Verbunden!");
    }
    void onDisconnect(NimBLEClient*, int reason) override {
        g_conn  = false;
        g_rxCnt = 0;
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

// Globaler Notify-Handler — bekommt JEDE Notification, egal welche Char
static void onAnyNotify(NimBLERemoteCharacteristic* chr,
                        uint8_t* data, size_t len, bool isNotify) {
    Serial.printf("[%6lums] NTFY handle=%u len=%u :",
                  millis(), chr->getHandle(), (unsigned)len);
    for (size_t i = 0; i < len && i < 20; i++) Serial.printf(" %02X", data[i]);
    Serial.println();
    decodeFrame(data, len);
}

static void connectBLE() {
    pushLog("Verbinde...");
    if (!pClient) {
        pClient = NimBLEDevice::createClient();
        pClient->setClientCallbacks(&clientCB, false);
    }
    if (!pClient->connect(targetAddr)) {
        pushLog("Conn FAIL");
        startScan(); return;
    }

    // ALLE Services dumpen
    auto& svcs = pClient->getServices(true);
    Serial.printf("[%6lums] === Services: %u ===\n", millis(), (unsigned)svcs.size());
    for (auto* s : svcs) {
        Serial.printf("[%6lums] SVC %s\n", millis(), s->getUUID().toString().c_str());
        for (auto* c : s->getCharacteristics(true)) {
            Serial.printf("[%6lums]   CHR %s h=%u prop=%02X N=%d I=%d R=%d W=%d\n",
                          millis(), c->getUUID().toString().c_str(),
                          c->getHandle(),
                          (unsigned)0,
                          c->canNotify(), c->canIndicate(),
                          c->canRead(), c->canWrite());
        }
    }

    // Auf JEDE notify/indicate Char subscriben
    int subN = 0;
    for (auto* s : svcs) {
        for (auto* c : s->getCharacteristics()) {
            if (c->canNotify() || c->canIndicate()) {
                bool ok = c->subscribe(c->canNotify(), onAnyNotify, true);
                Serial.printf("[%6lums] sub %s -> %s\n",
                              millis(), c->getUUID().toString().c_str(),
                              ok ? "OK" : "FAIL");
                if (ok) subN++;
            }
        }
    }
    pushLog("Sub auf %d Chars", subN);
}

// ─── Display ──────────────────────────────────────────────────────────────────
// Status-Balken oben (36px): BLE-Status links, Zähler + Modus rechts
static void drawStatus() {
    display.fillRect(0, 0, 240, 36, TFT_BLACK);
    display.setFont(&fonts::FreeSans9pt7b);

    // BLE-Status (Zeile 1)
    display.setTextDatum(ML_DATUM);
    display.setTextColor(g_conn ? (uint32_t)TFT_GREEN : (uint32_t)TFT_RED);
    display.drawString(g_conn ? "BLE OK" : "Suche...", 64, 12);

    // Zähler + Modus (Zeile 2)
    display.setTextColor(0x404040);
    char buf[16];
    snprintf(buf, sizeof(buf), "IGN #%lu", (unsigned long)g_rxCnt);
    display.drawString(buf, 64, 26);

    display.setTextDatum(MR_DATUM);
    display.setTextColor(0x303030);
    display.drawString(g_view ? "T/V" : "ADV", 185, 19);
}

// Log-Ansicht: wenn noch keine Daten → BLE-Steps anzeigen
static void drawLog() {
    display.fillRect(0, 36, 240, 204, TFT_BLACK);
    display.setFont(&fonts::FreeSans9pt7b);
    int start = (g_logN > NLOG) ? g_logN - NLOG : 0;
    int count = min(g_logN, NLOG);
    for (int i = 0; i < count; i++) {
        int slot   = (start + i) % NLOG;
        bool newest = (start + i == g_logN - 1);
        display.setTextDatum(ML_DATUM);
        display.setTextColor(newest ? (uint32_t)TFT_WHITE : (uint32_t)0x444444);
        display.drawString(g_log[slot], 16, 52 + i * 27);
    }
}

// Daten-Hälften (je 102px Sprite)
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
    snprintf(buf, sizeof(buf), "%.1f", (float)g_adv);
    drawHalf(sprTop, buf, "ADVANCE  deg", TFT_ORANGE, 36);
    snprintf(buf, sizeof(buf), "%d", (int)g_rpm);
    drawHalf(sprBot, buf, "RPM", TFT_WHITE, 138);
}

static void drawAux() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", (float)g_tmp);
    drawHalf(sprTop, buf, "TEMP  degC", TFT_CYAN, 36);
    snprintf(buf, sizeof(buf), "%.1f", (float)g_vlt);
    drawHalf(sprBot, buf, "VOLT  V", TFT_YELLOW, 138);
}

// ─── Button: kurz = Ansicht, lang = Raw-Log toggle ────────────────────────────
static void handleButton() {
    static bool     lastBtn   = HIGH;
    static uint32_t pressTime = 0;
    static bool     longFired = false;

    bool btn = digitalRead(BTN_PIN);
    if (btn == LOW && lastBtn == HIGH) { pressTime = millis(); longFired = false; }
    if (btn == LOW && !longFired && millis() - pressTime >= LONG_PRESS_MS) {
        longFired = true;
        g_rawlog  = !g_rawlog;
        pushLog("RAW-Log: %s", g_rawlog ? "AN" : "AUS");
    }
    if (btn == HIGH && lastBtn == LOW && !longFired) {
        g_view = !g_view;
    }
    lastBtn = btn;
}

// ─── Setup / Loop ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== M5Dial 123TUNE+ ===");

    display.init();
    display.setRotation(2);
    display.fillScreen(TFT_BLACK);
    display.setBrightness(200);

    sprTop.createSprite(240, 102);
    sprBot.createSprite(240, 102);

    pinMode(BTN_PIN, INPUT_PULLUP);

    pushLog("Start...");
    NimBLEDevice::init("M5Dial-NUS");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    // Pairing/Bonding aktivieren — NUS TX könnte verschlüsselung verlangen
    NimBLEDevice::setSecurityAuth(true, false, true);  // bond, mitm=false, sc
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    startScan();
}

void loop() {
    handleButton();

    if (doConnect) { doConnect = false; connectBLE(); }

    // Heartbeat: jede Sekunde Status loggen wenn verbunden aber keine Daten
    static uint32_t lastHb = 0;
    if (g_conn && g_rxCnt == 0 && millis() - lastHb >= 1000) {
        lastHb = millis();
        Serial.printf("[%6lums] HB conn=%d rx=%lu\n",
                      millis(), pClient && pClient->isConnected() ? 1 : 0,
                      (unsigned long)g_rxCnt);
    }

    drawStatus();
    if (g_rxCnt == 0) {
        drawLog();
    } else {
        g_view ? drawAux() : drawMain();
    }

    delay(80);
}
