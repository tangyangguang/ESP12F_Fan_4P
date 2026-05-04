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
static uint32_t pressStartTime = 0;
static bool isPressed = false;

// ─── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    // Debug serial
    Serial.begin(115200);
    Serial.println("\n>>> BOOT STARTED <<<");

    // Firmware identification & Hostname
    Esp8266Base::setFirmwareInfo("ESP12F_Fan_4P", "0.1.0");
    Esp8266Base::setHostname("esp-fan");
    Esp8266BaseLog::enableFileSink("/logs/app.log", 16384, ESP8266BASE_LOG_LEVEL, 4);
    Esp8266BaseLog::enableConfigAudit(true);
    Esp8266BaseLog::enableConfigReadAudit(true);
    Serial.println(">>> Calling Esp8266Base::begin() <<<");

    // Initialize all modules (Log, Config, WiFi, Web, OTA, NTP, MDNS, WDT)
    Esp8266Base::begin();

    // Initialize fan controller
    fanController.begin();

    // Register custom Web pages and APIs (must be after begin())
    bool webRoutesOk = true;
    webRoutesOk &= Esp8266BaseWeb::addPage("/fan", FanWeb::handleStatusPage);
    webRoutesOk &= Esp8266BaseWeb::addPage("/config", FanWeb::handleConfigPage);
    webRoutesOk &= Esp8266BaseWeb::addApi("/api/speed", FanWeb::handleApiSpeed);
    webRoutesOk &= Esp8266BaseWeb::addApi("/api/timer", FanWeb::handleApiTimer);
    webRoutesOk &= Esp8266BaseWeb::addApi("/api/stop", FanWeb::handleApiStop);
    webRoutesOk &= Esp8266BaseWeb::addApi("/api/config", FanWeb::handleApiConfig);
    webRoutesOk &= Esp8266BaseWeb::addApi("/api/ir/learn", FanWeb::handleApiIrLearn);
    webRoutesOk &= Esp8266BaseWeb::addApi("/api/ir/status", FanWeb::handleApiIrStatus);
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
    if (currentState == LOW && !isPressed) {
        pressStartTime = millis();
        isPressed = true;
    } else if (currentState == HIGH && isPressed) {
        isPressed = false;
    } else if (currentState == LOW && isPressed && (millis() - pressStartTime >= PRESS_DURATION_MS)) {
        ESP8266BASE_LOG_I("Main", "Button held 1s, clearing WiFi credentials");
        Esp8266BaseConfig::flush(); // Ensure pending writes are saved
        Esp8266BaseWiFi::clearCredentials();
        delay(500);
        ESP.restart();
    }

    // Yield to SDK
    yield();
}
