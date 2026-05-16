#include <Arduino.h>
#include "Esp8266Base.h"
#include "Esp8266BaseWeb.h"
#include "Esp8266BaseWiFi.h"

#include "fan/FanDriver.h"
#include "fan/ButtonDriver.h"
#include "fan/LedIndicator.h"
#include "fan/IRReceiverDriver.h"
#include "fan/FanController.h"
#include "web/FanWeb.h"

// ─── Pin definitions ────────────────────────────────────────────────────────
static const uint8_t PIN_FAN_PWM = 5;     // GPIO5 / D1
static const uint8_t PIN_FAN_TACH = 12;   // GPIO12 / D6
static const uint8_t PIN_BTN_ACCEL = 14;  // GPIO14 / D5
static const uint8_t PIN_BTN_DECEL = 4;   // GPIO4 / D2
static const uint8_t PIN_LED = 2;         // GPIO2 / D4 (板载 LED)
static const uint8_t PIN_IR_RECV = 13;    // GPIO13 / D7
static const int BOOT_PIN = 0;            // GPIO0 / D3 (Boot button)

// ─── Driver instances ───────────────────────────────────────────────────────
static FanDriver fanDriver(PIN_FAN_PWM, PIN_FAN_TACH);
static ButtonDriver btnDriver(PIN_BTN_ACCEL, PIN_BTN_DECEL);
static LedIndicator ledIndicator(PIN_LED, true);  // active_low = true
static IRReceiverDriver irDriver(PIN_IR_RECV);
static FanController fanController(fanDriver, btnDriver, ledIndicator, irDriver);
static FanWeb fanWeb(fanController, irDriver);

// ─── BOOT button: hold 1s to clear WiFi credentials ─────────────────────────
static const uint32_t PRESS_DURATION_MS = 1000;
static const uint32_t BOOT_DEBOUNCE_MS = 50;
static uint32_t pressStartTime = 0;
static uint32_t lastBootRawChange = 0;
static bool lastBootRawState = HIGH;
static bool stableBootState = HIGH;
static bool isPressed = false;
static bool bootActionTriggered = false;

// ─── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    // Debug serial
    Serial.begin(115200);
    Serial.println("\n>>> BOOT STARTED <<<");
    pinMode(BOOT_PIN, INPUT_PULLUP);

    // Firmware identification & Hostname
    Esp8266Base::setFirmwareInfo("ESP12F_Fan_4P", "0.1.0");
    Esp8266BaseWeb::setDefaultAuth("admin", "admin123");
    Esp8266BaseWeb::setDeviceName("ESP Fan");
    Esp8266BaseWeb::setHomePath("/fan");
    Esp8266BaseWeb::setHomeMode(Esp8266BaseWebHomeMode::FUSED_HOME);
    Esp8266BaseWeb::setSystemNavMode(Esp8266BaseWebSystemNavMode::FOOTER_COMPACT);
    Esp8266BaseLog::enableConfigAudit(true);
    Esp8266BaseLog::enableConfigReadAudit(true);
    Serial.println(">>> Calling Esp8266Base::begin() <<<");

    // Initialize all modules (Log, Config, WiFi, Web, OTA, NTP, MDNS, WDT)
    Esp8266Base::begin();

    // Initialize fan controller
    fanController.begin();

    // Register custom Web pages and APIs (must be after begin())
    bool webRoutesOk = true;
    webRoutesOk &= Esp8266BaseWeb::addPage("/fan", "Fan", FanWeb::handleStatusPage);
    webRoutesOk &= Esp8266BaseWeb::addPage("/config", "Settings", FanWeb::handleConfigPage);
    webRoutesOk &= Esp8266BaseWeb::addPage("/ir", "IR", FanWeb::handleIrPage);
    webRoutesOk &= Esp8266BaseWeb::addApi("/api/speed", FanWeb::handleApiSpeed);
    webRoutesOk &= Esp8266BaseWeb::addApi("/api/timer", FanWeb::handleApiTimer);
    webRoutesOk &= Esp8266BaseWeb::addApi("/api/stop", FanWeb::handleApiStop);
    webRoutesOk &= Esp8266BaseWeb::addApi("/api/config", FanWeb::handleApiConfig);
    webRoutesOk &= Esp8266BaseWeb::addApi("/api/ir/learn", FanWeb::handleApiIrLearn);
    webRoutesOk &= Esp8266BaseWeb::addApi("/api/status", FanWeb::handleApiStatus);
    if (!webRoutesOk) {
        ESP8266BASE_LOG_E("Main", "custom Web route registration incomplete");
    }

    ESP8266BASE_LOG_I("Main", "ESP12F_Fan_4P started");
}

// ─── Main loop ───────────────────────────────────────────────────────────────

void loop() {
    // Advance all base modules (WiFi, Config flush, Web server, NTP, MDNS, WDT)
    Esp8266Base::handle();

    // Update fan controller state machine
    fanController.tick();

    // ─── BOOT button: hold 1s to clear WiFi credentials ────────────────────
    bool currentState = digitalRead(BOOT_PIN);
    uint32_t now = millis();
    if (currentState != lastBootRawState) {
        lastBootRawState = currentState;
        lastBootRawChange = now;
    }
    if (now - lastBootRawChange >= BOOT_DEBOUNCE_MS && currentState != stableBootState) {
        stableBootState = currentState;
        if (stableBootState == LOW) {
            pressStartTime = now;
            isPressed = true;
            bootActionTriggered = false;
        } else {
            isPressed = false;
            bootActionTriggered = false;
        }
    }
    if (stableBootState == LOW && isPressed && !bootActionTriggered &&
        (now - pressStartTime >= PRESS_DURATION_MS)) {
        bootActionTriggered = true;
        ESP8266BASE_LOG_I("Main", "Button held 1s, clearing WiFi credentials");
        Esp8266BaseConfig::flush(); // Ensure pending writes are saved
        Esp8266BaseWiFi::clearCredentials();
        Esp8266BaseConfig::flush();
        ESP.restart();
    }

    // Yield to SDK
    yield();
}
