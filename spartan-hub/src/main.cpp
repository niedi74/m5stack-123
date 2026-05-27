/*
 * Spartan3-Hub — ESP32 dev board (38P / ESP32-WROOM)
 *
 * Hardware:
 *   SN65HVD230  CAN-TX → GPIO5,  CAN-RX → GPIO4
 *   Spartan 3 V2 Blue (CAN-H) → SN65HVD230 CANH
 *   Spartan 3 V2 Purple (CAN-L) → SN65HVD230 CANL
 *   Spartan 3 V2 Black + White → GND
 *   Spartan 3 V2 Red → 12V switched (lab PSU during bench test)
 *   LSU 4.9 sensor plugged into Spartan
 *
 * Exposes BLE GATT server:
 *   Name:    Spartan3-Hub
 *   Service: 7f510001-5a6b-4d2a-9f20-14a7f3e20000
 *     Notify:  7f510002  (compact text every 250 ms)
 *     Write:   7f510003  (commands: "DEMO", "CAN", "RESET")
 *
 * Compact notify format (matches M5Stack decodeGatewayCompact):
 *   L{lambda}R{rpm}A{adv}M{map_kpa}
 * where rpm/adv/map are 0 until 123TUNE+ integration is added.
 *
 * Lambda is invalid (status != 3) → notify "L0.000R0A0.0M0" still sent
 * so M5 can detect gateway presence; lambdaValid only set on status=3.
 *
 * Serial output (115200):
 *   [  ms] CAN λ=0.987 AFR=14.5 Temp=350°C Status=3 raw=3CF 57 03
 *   [  ms] BLE notify L0.987R0A0.0M0
 *   [  ms] BLE client connected / disconnected
 */

#include <Arduino.h>
#include <driver/twai.h>
#include <NimBLEDevice.h>

// --- CAN ---
#define CAN_TX_PIN  GPIO_NUM_5
#define CAN_RX_PIN  GPIO_NUM_4
#define SPARTAN_CAN_ID   1024
#define LAMBDA_VALID_STATUS 3

// --- BLE ---
static const char* BLE_NAME     = "Spartan3-Hub";
static const char* SVC_UUID     = "7f510001-5a6b-4d2a-9f20-14a7f3e20000";
static const char* NOTIFY_UUID  = "7f510002-5a6b-4d2a-9f20-14a7f3e20000";
static const char* CMD_UUID     = "7f510003-5a6b-4d2a-9f20-14a7f3e20000";

static const uint32_t NOTIFY_INTERVAL_MS = 250;

// --- State ---
static volatile float    g_lambda = 0.0f;
static volatile int      g_temp   = 0;
static volatile int      g_status = 0;
static volatile bool     g_lambdaValid = false;
static volatile uint32_t g_lastCanMs  = 0;
static volatile float    g_rpm = 0.0f;
static volatile float    g_adv = 0.0f;
static volatile float    g_map = 0.0f;

static bool g_demoMode = false;
static bool g_canReady = false;

static NimBLEServer*         pServer     = nullptr;
static NimBLECharacteristic* pNotifyChr  = nullptr;
static NimBLECharacteristic* pCmdChr     = nullptr;
static uint32_t              g_clients   = 0;

// --- BLE callbacks ---
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
        g_clients++;
        Serial.printf("[%6lums] BLE client connected  (%u total)\n",
                      millis(), (unsigned)g_clients);
    }
    void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
        if (g_clients) g_clients--;
        Serial.printf("[%6lums] BLE client disconnected (reason=%d, %u left)\n",
                      millis(), reason, (unsigned)g_clients);
        NimBLEDevice::startAdvertising();
    }
};

class CmdCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& info) override {
        std::string val = chr->getValue();
        String cmd = String(val.c_str());
        cmd.toUpperCase();
        cmd.trim();
        Serial.printf("[%6lums] CMD: %s\n", millis(), cmd.c_str());
        if (cmd == "DEMO") {
            g_demoMode = true;
            Serial.printf("[%6lums] Demo mode ON\n", millis());
        } else if (cmd == "CAN") {
            g_demoMode = false;
            Serial.printf("[%6lums] Demo mode OFF\n", millis());
        } else if (cmd == "RESET") {
            ESP.restart();
        }
    }
};

// --- CAN setup ---
static bool setupCAN() {
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN,
                                                           TWAI_MODE_NORMAL);
    g.rx_queue_len = 10;
    twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g, &t, &f);
    if (err != ESP_OK) {
        Serial.printf("[CAN] install fail: %s\n", esp_err_to_name(err));
        return false;
    }
    err = twai_start();
    if (err != ESP_OK) {
        Serial.printf("[CAN] start fail: %s\n", esp_err_to_name(err));
        return false;
    }
    Serial.println("[CAN] 500 kbps TWAI started, filter=ALL");
    return true;
}

// --- CAN poll (non-blocking) ---
static void pollCAN() {
    twai_message_t msg;
    while (twai_receive(&msg, 0) == ESP_OK) {
        if (msg.identifier == SPARTAN_CAN_ID && msg.data_length_code == 4) {
            float lambda = ((msg.data[0] << 8) | msg.data[1]) / 1000.0f;
            int   temp   = msg.data[2] * 10;
            int   status = msg.data[3];

            Serial.printf("[%6lums] CAN λ=%.3f AFR=%.1f Temp=%d°C Status=%d"
                          " raw=%02X %02X %02X %02X\n",
                          millis(), lambda, lambda * 14.7f, temp, status,
                          msg.data[0], msg.data[1], msg.data[2], msg.data[3]);

            g_lambda      = lambda;
            g_temp        = temp;
            g_status      = status;
            g_lambdaValid = (status == LAMBDA_VALID_STATUS);
            g_lastCanMs   = millis();
            g_canReady    = true;
        }
    }
}

// --- Demo mode: simulate heating + warm lambda ~1.0 ±0.05 ---
static void updateDemo() {
    static uint32_t demoStart = 0;
    if (demoStart == 0) demoStart = millis();
    uint32_t age = millis() - demoStart;

    if (age < 8000) {
        // heating phase
        g_status      = 2;
        g_lambdaValid = false;
        g_temp        = (int)(age / 8000.0f * 700);
        g_lambda      = 0.0f;
    } else {
        g_status      = 3;
        g_lambdaValid = true;
        g_temp        = 750 + (int)(sin(age * 0.001f) * 20);
        // gentle oscillation around stoich
        g_lambda = 1.0f + 0.05f * sin(age * 0.0008f);
    }
    g_canReady = true;
}

// --- BLE notify ---
static void sendNotify() {
    if (!pNotifyChr || g_clients == 0) return;

    char buf[48];
    // Compact format: L{lambda}R{rpm}A{adv}M{map}
    // M5 decodeGatewayCompact: L...R...A...M...
    snprintf(buf, sizeof(buf), "L%.3fR%.0fA%.1fM%.0f",
             (float)g_lambda,
             (float)g_rpm,
             (float)g_adv,
             (float)g_map);

    pNotifyChr->setValue((uint8_t*)buf, strlen(buf));
    pNotifyChr->notify();

    Serial.printf("[%6lums] BLE notify %s  valid=%d clients=%u\n",
                  millis(), buf, (int)g_lambdaValid, (unsigned)g_clients);
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n\n=== Spartan3-Hub v1.0 ===");
    Serial.printf("CAN TX=GPIO%d  RX=GPIO%d\n", (int)CAN_TX_PIN, (int)CAN_RX_PIN);

    // CAN
    if (!setupCAN()) {
        Serial.println("[!] CAN init failed — running in demo mode");
        g_demoMode = true;
    }

    // BLE
    NimBLEDevice::init(BLE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    auto* svc      = pServer->createService(SVC_UUID);
    pNotifyChr     = svc->createCharacteristic(NOTIFY_UUID,
                         NIMBLE_PROPERTY::NOTIFY);
    pCmdChr        = svc->createCharacteristic(CMD_UUID,
                         NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    pCmdChr->setCallbacks(new CmdCallbacks());

    svc->start();

    auto* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(SVC_UUID);
    adv->setName(BLE_NAME);
    adv->start();

    Serial.printf("[BLE] advertising as \"%s\"\n", BLE_NAME);
    Serial.printf("[BLE] SVC  %s\n", SVC_UUID);
    Serial.printf("[BLE] NTFY %s\n", NOTIFY_UUID);
    Serial.printf("[BLE] CMD  %s\n", CMD_UUID);
}

void loop() {
    static uint32_t lastNotify = 0;

    if (g_demoMode) {
        updateDemo();
    } else {
        pollCAN();
        // CAN timeout: if no frame for 2s, mark invalid
        if (g_canReady && (millis() - g_lastCanMs > 2000)) {
            g_lambdaValid = false;
            Serial.printf("[%6lums] CAN timeout\n", millis());
        }
    }

    if (millis() - lastNotify >= NOTIFY_INTERVAL_MS) {
        lastNotify = millis();
        sendNotify();
    }

    // Log CAN bus status every 10s when no client connected
    static uint32_t lastStatus = 0;
    if (g_clients == 0 && millis() - lastStatus > 10000) {
        lastStatus = millis();
        twai_status_info_t info;
        if (!g_demoMode && twai_get_status_info(&info) == ESP_OK) {
            Serial.printf("[%6lums] CAN state=%d rx_q=%u tx_q=%u rx_err=%u tx_err=%u arb_lost=%u bus_err=%u\n",
                          millis(), (int)info.state,
                          info.msgs_to_rx, info.msgs_to_tx,
                          info.rx_error_counter, info.tx_error_counter,
                          info.arb_lost_count, info.bus_error_count);
        } else if (g_demoMode) {
            Serial.printf("[%6lums] DEMO λ=%.3f status=%d temp=%d\n",
                          millis(), (float)g_lambda, (int)g_status, (int)g_temp);
        }
    }
}
