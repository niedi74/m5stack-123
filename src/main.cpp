#include <Arduino.h>
#include <NimBLEDevice.h>
#include <M5GFX.h>

// ─── GC9A01 Display (M5Stack Dial pinout) ────────────────────────────────────
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_GC9A01 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;
public:
    LGFX() {
        {
            auto cfg       = _bus.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.pin_sclk   = 6;
            cfg.pin_mosi   = 5;
            cfg.pin_miso   = -1;
            cfg.pin_dc     = 4;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg         = _panel.config();
            cfg.pin_cs       = 7;
            cfg.pin_rst      = 8;
            cfg.pin_busy     = -1;
            cfg.panel_width  = 240;
            cfg.panel_height = 240;
            cfg.invert       = true;
            cfg.offset_x     = 0;
            cfg.offset_y     = 0;
            _panel.config(cfg);
        }
        {
            auto cfg        = _light.config();
            cfg.pin_bl      = 9;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
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
#define BTN_PIN 42  // encoder push-button, active LOW

// ─── App State ────────────────────────────────────────────────────────────────
static volatile float g_rpm  = 0;
static volatile float g_adv  = 0;
static volatile float g_tmp  = 0;
static volatile float g_vlt  = 0;
static volatile float g_map  = 0;  // 0x32 Absolutdruck kPa
static volatile float g_cur  = 0;  // 0x35 Zuendstrom A
static volatile bool  g_conn = false;
static bool           g_view = false;  // false=ADV/RPM  true=TMP/VLT

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
    int hi  = hexnib(d[1]);
    int lo  = hexnib(d[2]);
    int raw = (hi << 4) | lo;
    switch (d[0]) {
        case 0x30:
            g_rpm = hi * 800.0f + lo * 50.0f;
            Serial.printf("RPM:  %.0f\n", (float)g_rpm);
            break;
        case 0x31:
            g_adv = hi * 3.2f + lo * 0.2f;
            Serial.printf("ADV:  %.1f deg\n", (float)g_adv);
            break;
        case 0x32:
            g_map = (float)raw;
            Serial.printf("MAP:  %d kPa\n", raw);
            break;
        case 0x33:
            g_tmp = (float)(raw - 30);
            Serial.printf("TEMP: %d C\n", raw - 30);
            break;
        case 0x35:
            g_cur = raw / 8.65f;
            Serial.printf("CURR: %.1f A\n", raw / 8.65f);
            break;
        case 0x41:
            g_vlt = raw / 4.54f;
            Serial.printf("VOLT: %.2f V\n", raw / 4.54f);
            break;
        case 0x42:
            Serial.printf("0x42: %d\n", raw);
            break;
        case 0x0D:
            break;  // keepalive / Zyklusende
    }
}

// ─── NimBLE callbacks ─────────────────────────────────────────────────────────
static void startScan();

class ClientCB : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient*) override {
        g_conn = true;
        Serial.println("BLE: verbunden");
    }
    void onDisconnect(NimBLEClient*) override {
        g_conn = false;
        Serial.println("BLE: getrennt, starte Scan neu");
        startScan();
    }
};

class ScanCB : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        String addr = dev->getAddress().toString().c_str();
        addr.toLowerCase();
        if (addr == TARGET) {
            Serial.printf("BLE: Geraet gefunden %s\n", addr.c_str());
            NimBLEDevice::getScan()->stop();
            targetAddr = dev->getAddress();
            doConnect  = true;
        }
    }
};

static ClientCB clientCB;
static ScanCB   scanCB;

static void startScan() {
    Serial.printf("BLE: Scan laeuft, suche %s\n", TARGET);
    auto* s = NimBLEDevice::getScan();
    s->setAdvertisedDeviceCallbacks(&scanCB);
    s->setActiveScan(true);
    s->setInterval(100);
    s->setWindow(99);
    s->start(0, nullptr, false);
}

static void connectBLE() {
    Serial.println("BLE: verbinde...");
    if (!pClient) {
        pClient = NimBLEDevice::createClient();
        pClient->setClientCallbacks(&clientCB, false);
    }
    if (!pClient->connect(targetAddr)) {
        Serial.println("BLE: Verbindung fehlgeschlagen, neuer Scan");
        startScan();
        return;
    }
    auto* svc = pClient->getService(NUS_SVC);
    if (!svc) {
        Serial.println("BLE: NUS Service nicht gefunden!");
        pClient->disconnect();
        return;
    }
    auto* chr = svc->getCharacteristic(NUS_TX);
    if (!chr || !chr->canNotify()) {
        Serial.println("BLE: TX Characteristic nicht verfuegbar!");
        pClient->disconnect();
        return;
    }
    chr->registerForNotify([](NimBLERemoteCharacteristic*, uint8_t* data,
                               size_t len, bool) {
        decodeFrame(data, len);
    });
    Serial.println("BLE: Notify aktiv, Daten laufen...");
}

// ─── Display helpers ─────────────────────────────────────────────────────────
static void drawStatus() {
    display.fillRect(0, 0, 240, 20, TFT_BLACK);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(g_conn ? (uint32_t)TFT_GREEN : (uint32_t)TFT_RED);
    display.drawString(g_conn ? "BLE OK" : "Suche...", 6, 4);
    display.setTextColor(TFT_DARKGREY);
    display.setTextDatum(TR_DATUM);
    display.drawString(g_view ? "T/V" : "IGN", 234, 4);
}

static void drawHalf(LGFX_Sprite& spr, const char* val, const char* lbl,
                     uint32_t col, int pushY) {
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(col);
    spr.setFont(&fonts::Font7);
    spr.drawString(val, 120, 48);
    spr.setFont(&fonts::FreeSans9pt7b);
    spr.setTextColor(TFT_DARKGREY);
    spr.drawString(lbl, 120, 90);
    spr.pushSprite(0, pushY);
}

static void drawMain() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", (float)g_adv);
    drawHalf(sprTop, buf, "ADVANCE  deg", TFT_ORANGE, 20);
    snprintf(buf, sizeof(buf), "%d", (int)g_rpm);
    drawHalf(sprBot, buf, "RPM", TFT_WHITE, 130);
}

static void drawAux() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", (float)g_tmp);
    drawHalf(sprTop, buf, "TEMP  degC", TFT_CYAN, 20);
    snprintf(buf, sizeof(buf), "%.1f", (float)g_vlt);
    drawHalf(sprBot, buf, "VOLT  V", TFT_YELLOW, 130);
}

// ─── Arduino entry points ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== M5Dial 123TUNE+ BLE Client ===\n");

    display.init();
    display.setRotation(0);
    display.fillScreen(TFT_BLACK);
    display.setBrightness(200);

    sprTop.createSprite(240, 110);
    sprBot.createSprite(240, 110);

    pinMode(BTN_PIN, INPUT_PULLUP);

    NimBLEDevice::init("M5Dial-NUS");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    startScan();
}

void loop() {
    // encoder button with debounce
    static bool     lastBtn   = HIGH;
    static uint32_t tDebounce = 0;
    bool btn = digitalRead(BTN_PIN);
    if (btn != lastBtn && millis() - tDebounce > 50) {
        tDebounce = millis();
        if (btn == LOW) {
            g_view = !g_view;
            Serial.printf("Ansicht: %s\n", g_view ? "TEMP/VOLT" : "ADV/RPM");
        }
    }
    lastBtn = btn;

    if (doConnect) {
        doConnect = false;
        connectBLE();
    }

    drawStatus();
    g_view ? drawAux() : drawMain();

    delay(100);
}
