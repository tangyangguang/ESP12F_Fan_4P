// Fan Controller Unit Tests
// Run with: pio test -e native

#include <unity.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "Arduino.h"
#include "Esp8266BaseWeb.h"
#include "Esp8266BaseWiFi.h"

// ─── Mock implementations ────────────────────────────────────────────────────

static uint32_t g_mock_millis = 0;

uint32_t millis() { return g_mock_millis; }
void yield() {}
void delay(uint32_t ms) { g_mock_millis += ms; }
void noInterrupts() {}
void interrupts() {}

static uint8_t g_pin_mode[20]  = {0};
static uint8_t g_pin_state[20] = {0};
static uint8_t g_pin_write_kind[20] = {0};
static uint32_t g_pwm_freq     = 0;
static uint8_t g_pwm_value[20] = {0};
static void (*g_isr_handlers[20])() = {nullptr};
static bool g_esp_restart_called = false;

void pinMode(uint8_t pin, uint8_t mode)                          { g_pin_mode[pin] = mode; }
uint8_t digitalRead(uint8_t pin)                                  { return g_pin_state[pin]; }
void digitalWrite(uint8_t pin, uint8_t val)                       { g_pin_state[pin] = val; g_pin_write_kind[pin] = 1; }
void analogWriteFreq(uint32_t freq)                               { g_pwm_freq = freq; }
void analogWrite(uint8_t pin, uint8_t val)                        { g_pwm_value[pin] = val; g_pin_write_kind[pin] = 2; }
void attachInterrupt(uint8_t pin, void (*h)(), int)               { g_isr_handlers[pin] = h; }
void detachInterrupt(uint8_t pin)                                  { g_isr_handlers[pin] = nullptr; }
uint8_t digitalPinToInterrupt(uint8_t pin)                        { return pin; }

ESPClass ESP;
void ESPClass::restart()           { g_esp_restart_called = true; }
bool ESPClass::restartCalled()     { bool v = g_esp_restart_called; g_esp_restart_called = false; return v; }

WiFiClass WiFi;

// ─── Mock Esp8266BaseConfig ─────────────────────────────────────────────────

namespace {
    struct IntEntry  { char key[40]; int32_t value; bool used; };
    struct BoolEntry { char key[40]; bool    value; bool used; };
    struct StrEntry  { char key[40]; char    value[64]; bool used; };

    static IntEntry  g_cfg_int[64]  = {};
    static BoolEntry g_cfg_bool[16] = {};
    static StrEntry  g_cfg_str[8]   = {};

    int findInt(const char* k)  { for (int i=0;i<64;i++) if (g_cfg_int[i].used  && strcmp(g_cfg_int[i].key,k)==0)  return i; return -1; }
    int findBool(const char* k) { for (int i=0;i<16;i++) if (g_cfg_bool[i].used && strcmp(g_cfg_bool[i].key,k)==0) return i; return -1; }
    int findStr(const char* k)  { for (int i=0;i< 8;i++) if (g_cfg_str[i].used  && strcmp(g_cfg_str[i].key,k)==0)  return i; return -1; }
}

namespace Esp8266BaseConfig {
    int32_t getInt(const char* key, int32_t def) {
        int i = findInt(key); return i >= 0 ? g_cfg_int[i].value : def;
    }
    bool setInt(const char* key, int32_t val) {
        int i = findInt(key);
        if (i < 0) for (int j=0;j<64;j++) if (!g_cfg_int[j].used) { i=j; strncpy(g_cfg_int[j].key,key,39); g_cfg_int[j].used=true; break; }
        if (i>=0) g_cfg_int[i].value = val;
        return true;
    }
    bool setIntDeferred(const char* key, int32_t val) { return setInt(key, val); }
    bool getBool(const char* key, bool def) {
        int i = findBool(key); return i >= 0 ? g_cfg_bool[i].value : def;
    }
    bool setBool(const char* key, bool val) {
        int i = findBool(key);
        if (i < 0) for (int j=0;j<16;j++) if (!g_cfg_bool[j].used) { i=j; strncpy(g_cfg_bool[j].key,key,39); g_cfg_bool[j].used=true; break; }
        if (i>=0) g_cfg_bool[i].value = val;
        return true;
    }
    bool setStr(const char* key, const char* val) {
        int i = findStr(key);
        if (i < 0) for (int j=0;j<8;j++) if (!g_cfg_str[j].used) { i=j; strncpy(g_cfg_str[j].key,key,39); g_cfg_str[j].used=true; break; }
        if (i>=0) strncpy(g_cfg_str[i].value, val, 63);
        return true;
    }
    bool getStr(const char* key, char* out, size_t len, const char* def) {
        int i = findStr(key);
        strncpy(out, i>=0 ? g_cfg_str[i].value : def, len-1);
        out[len-1] = '\0';
        return i >= 0;
    }
    bool flush() { return true; }
    bool clearAll() {
        memset(g_cfg_int, 0, sizeof(g_cfg_int));
        memset(g_cfg_bool, 0, sizeof(g_cfg_bool));
        memset(g_cfg_str, 0, sizeof(g_cfg_str));
        return true;
    }
    void reset() {
        memset(g_cfg_int, 0, sizeof(g_cfg_int));
        memset(g_cfg_bool, 0, sizeof(g_cfg_bool));
        memset(g_cfg_str, 0, sizeof(g_cfg_str));
    }
}

// ─── Mock Esp8266BaseSleep ──────────────────────────────────────────────────

namespace Esp8266BaseSleep {
    static bool g_modem_sleep = false;
    bool modemSleep()         { g_modem_sleep = true; return true; }
    void wakeModem()          { g_modem_sleep = false; }
    bool lightSleep(uint32_t) { return true; }
    bool noSleep()            { g_modem_sleep = false; return true; }
    bool isModemSleeping()    { return g_modem_sleep; }
    void reset()              { g_modem_sleep = false; }
}

// ─── Mock Esp8266BaseWiFi ───────────────────────────────────────────────────

namespace Esp8266BaseWiFi {
    static bool g_connected = true;
    bool isConnected()               { return g_connected; }
    const char* ip()                 { return "0.0.0.0"; }
    bool clearCredentials()          { return true; }
    bool setHostname(const char*)    { return true; }
    Esp8266BaseWiFiState state()     { return g_connected ? Esp8266BaseWiFiState::CONNECTED : Esp8266BaseWiFiState::AP_CONFIG; }
    void testSetConnected(bool connected) { g_connected = connected; }
}

// ─── Mock Esp8266BaseNTP ────────────────────────────────────────────────────

namespace Esp8266BaseNTP {
    bool isSynced() { return true; }
    bool formatTo(char* out, size_t len, const char*) {
        strncpy(out, "2026-05-01 12:00:00", len-1); out[len-1]='\0'; return true;
    }
}

// ─── Mock Esp8266BaseWeb (MockWebServer) ────────────────────────────────────

namespace {
    struct WebArg { char name[32]; char value[128]; };
    static int      g_web_method   = HTTP_GET;
    static WebArg   g_web_args[8]  = {};
    static int      g_web_arg_cnt  = 0;
    static uint16_t g_web_last_code = 0;
    static char     g_web_last_body[1024] = {};
    static char     g_web_page_body[12288] = {};
    static MockWebServer g_mock_server;

    void appendPageBody(const char* text) {
        if (!text) return;
        size_t used = strlen(g_web_page_body);
        if (used >= sizeof(g_web_page_body) - 1) return;
        strncat(g_web_page_body, text, sizeof(g_web_page_body) - used - 1);
    }
}

int      MockWebServer::method()              const { return g_web_method; }
bool     MockWebServer::hasArg(const char* n) const {
    for (int i=0;i<g_web_arg_cnt;i++) if (strcmp(g_web_args[i].name,n)==0) return true;
    return false;
}
String   MockWebServer::arg(const char* n) const {
    for (int i=0;i<g_web_arg_cnt;i++) if (strcmp(g_web_args[i].name,n)==0) return String(g_web_args[i].value);
    return String("");
}
void     MockWebServer::send(uint16_t code, const char*, const char* body) {
    g_web_last_code = code;
    strncpy(g_web_last_body, body ? body : "", sizeof(g_web_last_body)-1);
    g_web_last_body[sizeof(g_web_last_body)-1] = '\0';
}
void     MockWebServer::setMethod(int m)                  { g_web_method = m; }
void     MockWebServer::setArg(const char* n, const char* v) {
    for (int i=0;i<g_web_arg_cnt;i++) if (strcmp(g_web_args[i].name,n)==0) {
        strncpy(g_web_args[i].value, v, 127); return;
    }
    if (g_web_arg_cnt < 8) {
        strncpy(g_web_args[g_web_arg_cnt].name, n, 31);
        strncpy(g_web_args[g_web_arg_cnt].value, v, 127);
        g_web_arg_cnt++;
    }
}
void     MockWebServer::reset() {
    g_web_method = HTTP_GET; g_web_arg_cnt = 0;
    g_web_last_code = 0; g_web_last_body[0] = '\0';
    g_web_page_body[0] = '\0';
    memset(g_web_args, 0, sizeof(g_web_args));
}
uint16_t MockWebServer::lastCode() { return g_web_last_code; }
const char* MockWebServer::lastBody() { return g_web_last_body; }

namespace Esp8266BaseWeb {
    MockWebServer& server()            { return g_mock_server; }
    bool checkAuth()                   { return true; }
    void sendHeader()                  {}
    void sendFooter()                  {}
    void sendChunk(const char* s)      { appendPageBody(s); }
    void sendContent_P(const char* s)  { appendPageBody(s); }
    bool addPage(const char*, void(*)()) { return true; }
    bool addPage(const char*, const char*, void(*)()) { return true; }
    bool addApi(const char*, void(*)())  { return true; }
    void setDefaultAuth(const char*, const char*) {}
}

// ─── Include sources under test ─────────────────────────────────────────────

#include "fan/FanDriver.h"
#include "fan/ButtonDriver.h"
#include "fan/LedIndicator.h"
#include "fan/IRReceiverDriver.h"
#include "fan/FanController.h"
#include "web/FanWeb.h"

// ─── Test helpers ────────────────────────────────────────────────────────────

static void makeController(FanDriver& fan, ButtonDriver& btn, LedIndicator& led,
                            IRReceiverDriver& ir, FanController& ctrl) {
    fan.begin();
    ctrl.begin();
    (void)btn; (void)led; (void)ir;
}

// ─── Test setUp / tearDown ───────────────────────────────────────────────────

void setUp() {
    g_mock_millis = 0;
    memset(g_pin_mode, 0, sizeof(g_pin_mode));
    memset(g_pin_state, 0, sizeof(g_pin_state));
    memset(g_pin_write_kind, 0, sizeof(g_pin_write_kind));
    memset(g_pwm_value, 0, sizeof(g_pwm_value));
    g_pwm_freq = 0;
    memset(g_isr_handlers, 0, sizeof(g_isr_handlers));
    g_esp_restart_called = false;
    Esp8266BaseConfig::reset();
    Esp8266BaseSleep::reset();
    Esp8266BaseWiFi::testSetConnected(true);
    MockWebServer::reset();
}

void tearDown() {}

// ─── FanDriver Tests ─────────────────────────────────────────────────────────

void test_fan_driver_begin() {
    FanDriver fan(5, 12);
    TEST_ASSERT_TRUE(fan.begin());
    TEST_ASSERT_EQUAL(OUTPUT,       g_pin_mode[5]);
    TEST_ASSERT_EQUAL(INPUT_PULLUP, g_pin_mode[12]);
    TEST_ASSERT_EQUAL(25000,        g_pwm_freq);
    TEST_ASSERT_EQUAL(0,            g_pwm_value[5]);
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan.getState());
}

void test_fan_driver_set_speed() {
    FanDriver fan(5, 12);
    fan.begin();
    TEST_ASSERT_TRUE(fan.setSpeed(50));
    TEST_ASSERT_EQUAL(FAN_STATE_SOFT_START, fan.getState());

    g_mock_millis = 2000;
    fan.tick();
    TEST_ASSERT_EQUAL(50, fan.getSpeed());
    TEST_ASSERT_EQUAL(FAN_STATE_RUNNING, fan.getState());
}

void test_fan_driver_soft_stop() {
    FanDriver fan(5, 12);
    fan.begin();
    fan.setBlockDetectTime(10000);
    fan.setSpeed(50);
    g_mock_millis = 2000; fan.tick();
    TEST_ASSERT_EQUAL(FAN_STATE_RUNNING, fan.getState());

    fan.setSpeed(0);
    TEST_ASSERT_EQUAL(FAN_STATE_SOFT_STOP, fan.getState());

    g_mock_millis = 4000; fan.tick();
    TEST_ASSERT_EQUAL(0, fan.getSpeed());
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan.getState());
}

void test_fan_driver_block_detection() {
    FanDriver fan(5, 12);
    fan.begin();
    fan.setBlockDetectTime(500);
    fan.setSpeed(50);
    g_mock_millis = 2000; fan.tick();
    TEST_ASSERT_EQUAL(FAN_STATE_RUNNING, fan.getState());

    g_mock_millis = 2600; fan.tick();
    TEST_ASSERT_TRUE(fan.isBlocked());
    TEST_ASSERT_EQUAL(FAN_STATE_BLOCKED, fan.getState());
    TEST_ASSERT_EQUAL(0, g_pwm_value[5]);
}

void test_fan_driver_reset_block() {
    FanDriver fan(5, 12);
    fan.begin();
    fan.setBlockDetectTime(500);
    fan.setSpeed(50);
    g_mock_millis = 1500; fan.tick();
    g_mock_millis = 2100; fan.tick();
    TEST_ASSERT_TRUE(fan.isBlocked());

    fan.resetBlock();
    TEST_ASSERT_FALSE(fan.isBlocked());
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan.getState());
}

void test_fan_driver_rpm_calculation() {
    FanDriver fan(5, 12);
    fan.begin();
    g_mock_millis = 0;
    if (g_isr_handlers[12]) g_isr_handlers[12]();
    g_mock_millis = 250;
    if (g_isr_handlers[12]) g_isr_handlers[12]();
    g_mock_millis = 500;
    fan.tick();
    TEST_ASSERT_EQUAL(120, fan.getRpm());
}

void test_fan_driver_set_speed_blocked() {
    FanDriver fan(5, 12);
    fan.begin();
    fan.setBlockDetectTime(500);
    fan.setSpeed(50);
    g_mock_millis = 1500; fan.tick();
    g_mock_millis = 2100; fan.tick();
    TEST_ASSERT_TRUE(fan.isBlocked());
    TEST_ASSERT_FALSE(fan.setSpeed(80));
}

// ─── ButtonDriver Tests ──────────────────────────────────────────────────────

void test_button_driver_begin() {
    ButtonDriver btn(14, 4);
    TEST_ASSERT_TRUE(btn.begin());
    TEST_ASSERT_EQUAL(INPUT_PULLUP, g_pin_mode[14]);
    TEST_ASSERT_EQUAL(INPUT_PULLUP, g_pin_mode[4]);
}

void test_button_accel_short_press() {
    g_pin_state[14] = HIGH; g_pin_state[4] = HIGH;  // set before begin so stable state is HIGH
    ButtonDriver btn(14, 4);
    btn.begin();
    g_mock_millis = 0; btn.getEvent();  // no-op: stable already matches raw

    // Press: raw change at 100ms, stable at 160ms
    g_mock_millis = 100; g_pin_state[14] = LOW; btn.getEvent();
    g_mock_millis = 160; btn.getEvent();  // _stable_accel=LOW, _accel_pressed=true

    // Release: raw change at 300ms, stable at 360ms → BTN_ACCEL_SHORT
    g_mock_millis = 300; g_pin_state[14] = HIGH; btn.getEvent();
    g_mock_millis = 360;
    ButtonEvent evt = btn.getEvent();
    TEST_ASSERT_EQUAL(BTN_ACCEL_SHORT, evt);
}

void test_button_decel_short_press() {
    g_pin_state[14] = HIGH; g_pin_state[4] = HIGH;
    ButtonDriver btn(14, 4);
    btn.begin();
    g_mock_millis = 0; btn.getEvent();

    // Press: raw at 100ms, stable at 160ms
    g_mock_millis = 100; g_pin_state[4] = LOW; btn.getEvent();
    g_mock_millis = 160; btn.getEvent();  // _stable_decel=LOW, _decel_pressed=true

    // Release: raw at 300ms, stable at 360ms → BTN_DECEL_SHORT
    g_mock_millis = 300; g_pin_state[4] = HIGH; btn.getEvent();
    g_mock_millis = 360;
    ButtonEvent evt = btn.getEvent();
    TEST_ASSERT_EQUAL(BTN_DECEL_SHORT, evt);
}

void test_button_both_long_press() {
    g_pin_state[14] = HIGH; g_pin_state[4] = HIGH;
    ButtonDriver btn(14, 4);
    btn.begin();
    g_mock_millis = 0; btn.getEvent();

    // Press both buttons, let debounce settle at 200ms
    g_mock_millis = 100; g_pin_state[14] = LOW; btn.getEvent();
    g_mock_millis = 110; g_pin_state[4]  = LOW; btn.getEvent();
    g_mock_millis = 200; btn.getEvent();  // stable: both pressed, press_ticks recorded

    // Hold for > 5s from the stable press time (200ms)
    g_mock_millis = 5300;
    ButtonEvent evt = btn.getEvent();
    TEST_ASSERT_EQUAL(BTN_BOTH_LONG, evt);
}

void test_button_both_long_requires_full_release_before_new_short_press() {
    g_pin_state[14] = HIGH; g_pin_state[4] = HIGH;
    ButtonDriver btn(14, 4);
    btn.begin();
    g_mock_millis = 0; btn.getEvent();

    g_mock_millis = 100; g_pin_state[14] = LOW; btn.getEvent();
    g_mock_millis = 110; g_pin_state[4] = LOW; btn.getEvent();
    g_mock_millis = 200; btn.getEvent();
    g_mock_millis = 5300;
    TEST_ASSERT_EQUAL(BTN_BOTH_LONG, btn.getEvent());

    g_mock_millis = 5400; g_pin_state[4] = HIGH; btn.getEvent();
    g_mock_millis = 5460; TEST_ASSERT_EQUAL(BTN_NONE, btn.getEvent());
    g_mock_millis = 5600; g_pin_state[4] = LOW; btn.getEvent();
    g_mock_millis = 5660; TEST_ASSERT_EQUAL(BTN_NONE, btn.getEvent());
    g_mock_millis = 5800; g_pin_state[4] = HIGH; btn.getEvent();
    g_mock_millis = 5860; TEST_ASSERT_EQUAL(BTN_NONE, btn.getEvent());

    g_mock_millis = 6000; g_pin_state[14] = HIGH; btn.getEvent();
    g_mock_millis = 6060; TEST_ASSERT_EQUAL(BTN_NONE, btn.getEvent());

    g_mock_millis = 6200; g_pin_state[4] = LOW; btn.getEvent();
    g_mock_millis = 6260; btn.getEvent();
    g_mock_millis = 6400; g_pin_state[4] = HIGH; btn.getEvent();
    g_mock_millis = 6460; TEST_ASSERT_EQUAL(BTN_DECEL_SHORT, btn.getEvent());
}

// ─── IRReceiverDriver Tests ──────────────────────────────────────────────────

void test_ir_driver_begin() {
    IRReceiverDriver ir(13);
    TEST_ASSERT_TRUE(ir.begin());
}

void test_ir_learning_mode() {
    IRReceiverDriver ir(13);
    ir.begin();
    TEST_ASSERT_FALSE(ir.isLearning());
    TEST_ASSERT_TRUE(ir.startLearning(0));
    TEST_ASSERT_TRUE(ir.isLearning());
    TEST_ASSERT_EQUAL(0, ir.getLearnedKeyIndex());
    TEST_ASSERT_EQUAL(10, ir.getLearningRemaining());
}

void test_ir_learning_timeout() {
    IRReceiverDriver ir(13);
    ir.begin();
    ir.startLearning(0);
    g_mock_millis = 0;
    TEST_ASSERT_TRUE(ir.isLearning());
    g_mock_millis = 10000;
    ir.getEvent();
    TEST_ASSERT_FALSE(ir.isLearning());
}

void test_ir_set_get_key_code() {
    IRReceiverDriver ir(13);
    ir.begin();
    ir.setKeyCode(0, 1, 0xE01F);
    uint8_t proto; uint64_t code;
    TEST_ASSERT_TRUE(ir.getKeyCode(0, &proto, &code));
    TEST_ASSERT_EQUAL(1,      proto);
    TEST_ASSERT_EQUAL(0xE01F, code);
}

void test_ir_extended_timer_key_code() {
    IRReceiverDriver ir(13);
    ir.begin();
    ir.setKeyCode(IR_KEY_TIMER_8H, 2, 0x00FFAA55);
    uint8_t proto; uint64_t code;
    TEST_ASSERT_TRUE(ir.getKeyCode(IR_KEY_TIMER_8H, &proto, &code));
    TEST_ASSERT_EQUAL(2, proto);
    TEST_ASSERT_EQUAL(0x00FFAA55, code);
    TEST_ASSERT_TRUE(ir.startLearning(IR_KEY_TIMER_8H));
    TEST_ASSERT_EQUAL(IR_KEY_TIMER_8H, ir.getLearnedKeyIndex());
    TEST_ASSERT_FALSE(ir.startLearning(IR_KEY_COUNT));
}

void test_ir_learning_rejects_duplicate_code_for_other_key() {
    IRReceiverDriver ir(13);
    ir.begin();
    ir.setKeyCode(IR_KEY_TIMER_2H, 76, 0xD88);

    g_mock_millis = 1000;
    TEST_ASSERT_FALSE(ir.testLearnDecoded(IR_KEY_TIMER_4H, 76, 0xD88));
    TEST_ASSERT_TRUE(ir.isLearning());
    TEST_ASSERT_EQUAL(0, ir.getLearnedSequence());
    TEST_ASSERT_EQUAL(1, ir.getLearnRejectSequence());
    TEST_ASSERT_EQUAL(IR_KEY_TIMER_2H, ir.getDuplicateKeyIndex());
    TEST_ASSERT_EQUAL(10, ir.getLearningRemaining());

    TEST_ASSERT_TRUE(ir.startLearning(IR_KEY_TIMER_4H));
    TEST_ASSERT_EQUAL(IR_KEY_COUNT, ir.getDuplicateKeyIndex());

    uint8_t proto; uint64_t code;
    TEST_ASSERT_TRUE(ir.getKeyCode(IR_KEY_TIMER_4H, &proto, &code));
    TEST_ASSERT_EQUAL(0, proto);
    TEST_ASSERT_EQUAL(0, code);

    ir.setKeyCode(IR_KEY_TIMER_4H, 0, 0);
    TEST_ASSERT_TRUE(ir.testLearnDecoded(IR_KEY_TIMER_2H, 76, 0xD88));
    TEST_ASSERT_FALSE(ir.isLearning());
    TEST_ASSERT_EQUAL(1, ir.getLearnedSequence());
    TEST_ASSERT_EQUAL(IR_KEY_COUNT, ir.getDuplicateKeyIndex());
}

// ─── LedIndicator Tests ─────────────────────────────────────────────────────

void test_led_indicator_active_low_gear_brightness() {
    LedIndicator led(2, true);
    led.begin();

    led.setGear(0); led.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);

    led.setGear(1); led.tick();
    TEST_ASSERT_EQUAL(2, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(191, g_pwm_value[2]);

    led.setGear(2); led.tick();
    TEST_ASSERT_EQUAL(2, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(127, g_pwm_value[2]);

    led.setGear(3); led.tick();
    TEST_ASSERT_EQUAL(2, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(63, g_pwm_value[2]);

    led.setGear(4); led.tick();
    TEST_ASSERT_EQUAL(2, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(0, g_pwm_value[2]);
}

void test_led_indicator_pwm_to_off_writes_real_off() {
    LedIndicator led(2, true);
    led.begin();

    led.setGear(4); led.tick();
    TEST_ASSERT_EQUAL(2, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(0, g_pwm_value[2]);

    led.setGear(0); led.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);
}

void test_led_indicator_slow_and_fast_blink_timing() {
    LedIndicator led(2, true);
    led.begin();

    led.setOverride(LED_SLOW_BLINK);
    g_mock_millis = 0; led.tick();
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);
    g_mock_millis = 499; led.tick();
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);
    g_mock_millis = 500; led.tick();
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
    g_mock_millis = 999; led.tick();
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
    g_mock_millis = 1000; led.tick();
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);

    led.setOverride(LED_FAST_BLINK);
    g_mock_millis = 1099; led.tick();
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);
    g_mock_millis = 1100; led.tick();
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
    g_mock_millis = 1200; led.tick();
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);
}

void test_led_indicator_flash_restores_gear_brightness() {
    LedIndicator led(2, true);
    led.begin();

    led.setGear(2);
    led.flashOnce();
    g_mock_millis = 0; led.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);

    g_mock_millis = 199; led.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);

    g_mock_millis = 200; led.tick();
    TEST_ASSERT_EQUAL(2, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(127, g_pwm_value[2]);
}

void test_led_indicator_flash_inverts_full_brightness() {
    LedIndicator led(2, true);
    led.begin();

    led.setGear(4);
    led.tick();
    TEST_ASSERT_EQUAL(2, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(0, g_pwm_value[2]);

    led.flashOnce();
    g_mock_millis = 0; led.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);

    g_mock_millis = 200; led.tick();
    TEST_ASSERT_EQUAL(2, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(0, g_pwm_value[2]);
}

void test_led_indicator_flash_inverts_slow_blink_on_phase() {
    LedIndicator led(2, true);
    led.begin();

    led.setOverride(LED_SLOW_BLINK);
    g_mock_millis = 500; led.tick();
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);

    led.flashOnce();
    g_mock_millis = 501; led.tick();
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);

    g_mock_millis = 701; led.tick();
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
}

void test_led_indicator_flash_restores_wifi_slow_blink() {
    LedIndicator led(2, true);
    led.begin();

    led.setOverride(LED_SLOW_BLINK);
    led.flashOnce();
    g_mock_millis = 0; led.tick();
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);

    g_mock_millis = 200; led.tick();
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);
    g_mock_millis = 500; led.tick();
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
}

void test_led_indicator_flash_duration_zero_disables_feedback() {
    LedIndicator led(2, true);
    led.begin();

    led.setGear(2);
    led.tick();
    TEST_ASSERT_EQUAL(2, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(127, g_pwm_value[2]);

    led.setFlashDuration(0);
    led.flashOnce();
    led.tick();
    TEST_ASSERT_EQUAL(2, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(127, g_pwm_value[2]);
}

void test_led_indicator_flash_duration_2000ms_restores_after_window() {
    LedIndicator led(2, true);
    led.begin();

    led.setFlashDuration(2000);
    TEST_ASSERT_EQUAL(2000, led.getFlashDuration());
    led.setGear(2);
    led.flashOnce();

    g_mock_millis = 1999; led.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);

    g_mock_millis = 2000; led.tick();
    TEST_ASSERT_EQUAL(2, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(127, g_pwm_value[2]);
}

void test_led_indicator_flash_does_not_override_fast_blink() {
    LedIndicator led(2, true);
    led.begin();

    led.setOverride(LED_FAST_BLINK);
    g_mock_millis = 100; led.tick();
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);

    led.flashOnce();
    g_mock_millis = 199; led.tick();
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
    g_mock_millis = 200; led.tick();
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);
}

// ─── FanController Tests ─────────────────────────────────────────────────────

void test_controller_begin() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    TEST_ASSERT_TRUE(ctrl.begin());
    TEST_ASSERT_EQUAL(SYS_IDLE, ctrl.getState());
}

void test_controller_set_speed() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    TEST_ASSERT_TRUE(ctrl.setSpeed(50));
    TEST_ASSERT_EQUAL(SYS_RUNNING, ctrl.getState());

    g_mock_millis = 2000; ctrl.tick();
    TEST_ASSERT_EQUAL(50, ctrl.getCurrentSpeed());
}

void test_controller_min_effective_speed() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    ctrl.setMinEffectiveSpeed(15);

    ctrl.setSpeed(5);
    g_mock_millis = 2000; ctrl.tick();
    TEST_ASSERT_EQUAL(15, ctrl.getCurrentSpeed());
}

void test_controller_min_effective_speed_applies_to_soft_start_floor() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    ctrl.setBlockDetectTime(60000);
    ctrl.setMinEffectiveSpeed(30);

    ctrl.setSpeed(30);
    g_mock_millis = 1; ctrl.tick();
    TEST_ASSERT_EQUAL(30, ctrl.getCurrentSpeed());
}

void test_controller_set_speed_clamps_public_input_to_100() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    fan.setBlockDetectTime(60000);
    fan.setSoftStartTime(0);

    ctrl.setSpeed(250);
    ctrl.tick();
    TEST_ASSERT_EQUAL(100, ctrl.getTargetSpeed());
    TEST_ASSERT_EQUAL(100, ctrl.getCurrentSpeed());
}

void test_controller_stop() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    ctrl.setSpeed(50);
    g_mock_millis = 1500; ctrl.tick();

    ctrl.stop();
    TEST_ASSERT_EQUAL(0, ctrl.getTimerRemaining());
    TEST_ASSERT_EQUAL(0, ctrl.getTargetSpeed());
    TEST_ASSERT_EQUAL(0, ctrl.getCurrentGear());
}

void test_controller_stop_clears_error_state() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    ctrl.setBlockDetectTime(500);

    ctrl.setSpeed(50);
    g_mock_millis = 2000; ctrl.tick();
    g_mock_millis = 2600; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_ERROR, ctrl.getState());

    ctrl.stop();
    TEST_ASSERT_EQUAL(SYS_IDLE, ctrl.getState());
    TEST_ASSERT_FALSE(ctrl.isBlocked());
    TEST_ASSERT_EQUAL(0, ctrl.getTargetSpeed());
    TEST_ASSERT_EQUAL(0, ctrl.getCurrentGear());
}

void test_controller_timer() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    TEST_ASSERT_TRUE(ctrl.setTimer(3600));
    TEST_ASSERT_EQUAL(3600, ctrl.getTimerRemaining());
    TEST_ASSERT_FALSE(ctrl.setTimer(400000));
}

void test_controller_timer_countdown() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    fan.setBlockDetectTime(60000);  // must be after begin() to avoid _loadConfig() overwrite
    fan.setSoftStartTime(0);

    ctrl.setSpeed(50);
    ctrl.setTimer(10);

    // Tick once per second for 12s; timer starts counting from setTimer() call
    for (int i = 1; i <= 12; i++) {
        g_mock_millis = i * 1000;
        ctrl.tick();
    }
    TEST_ASSERT_EQUAL(0, ctrl.getTimerRemaining());
}

void test_controller_timer_catches_up_after_late_tick() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    ctrl.setTimer(10);
    g_mock_millis = 3000;
    ctrl.tick();

    TEST_ASSERT_EQUAL(7, ctrl.getTimerRemaining());
}

void test_controller_run_duration_catches_up_after_late_tick() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    fan.setBlockDetectTime(60000);
    fan.setSoftStartTime(0);

    ctrl.setSpeed(50);
    g_mock_millis = 3500;
    ctrl.tick();

    TEST_ASSERT_EQUAL(3, ctrl.getTotalRunDuration());
}

void test_controller_timer_countdown_while_idle() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    ctrl.setTimer(3);

    for (int i = 1; i <= 3; i++) {
        g_mock_millis = i * 1000;
        ctrl.tick();
    }

    TEST_ASSERT_EQUAL(0, ctrl.getTimerRemaining());
}

void test_controller_timer_countdown_while_error() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    ctrl.setBlockDetectTime(500);

    ctrl.setSpeed(50);
    g_mock_millis = 2000; ctrl.tick();
    g_mock_millis = 2600; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_ERROR, ctrl.getState());

    ctrl.setTimer(3);
    for (int i = 1; i <= 3; i++) {
        g_mock_millis = 2600 + i * 1000;
        ctrl.tick();
    }

    TEST_ASSERT_EQUAL(0, ctrl.getTimerRemaining());
}

void test_controller_power_on_restore() {
    Esp8266BaseConfig::setInt("fan_last_speed", 60);
    Esp8266BaseConfig::setInt("fan_last_timer", 1800);
    Esp8266BaseConfig::setBool("fan_auto_restore", true);
    Esp8266BaseConfig::setInt("fan_soft_start", 0);    // instant speed for test
    Esp8266BaseConfig::setInt("fan_block_detect", 60000);  // disable block detect

    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    // Tick at 100ms: speed is instant (soft_start=0), timer hasn't ticked yet
    g_mock_millis = 100; ctrl.tick();
    TEST_ASSERT_EQUAL(60, ctrl.getCurrentSpeed());
    TEST_ASSERT_EQUAL(1800, ctrl.getTimerRemaining());
    TEST_ASSERT_EQUAL(SYS_RUNNING, ctrl.getState());
}

void test_controller_power_on_restore_disabled() {
    Esp8266BaseConfig::setInt("fan_last_speed", 60);
    Esp8266BaseConfig::setBool("fan_auto_restore", false);

    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    TEST_ASSERT_EQUAL(0, ctrl.getCurrentSpeed());
    TEST_ASSERT_EQUAL(SYS_IDLE, ctrl.getState());
}

void test_controller_led_flash_duration_defaults_to_200() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    TEST_ASSERT_EQUAL(200, ctrl.getLedFlashDuration());
    TEST_ASSERT_EQUAL(200, led.getFlashDuration());
}

void test_controller_led_flash_duration_loads_bounds() {
    Esp8266BaseConfig::setInt("fan_led_flash_ms", 0);
    FanDriver fan0(5, 12); ButtonDriver btn0(14, 4);
    LedIndicator led0(2, true); IRReceiverDriver ir0(13);
    FanController ctrl0(fan0, btn0, led0, ir0);
    ctrl0.begin();
    TEST_ASSERT_EQUAL(0, ctrl0.getLedFlashDuration());
    TEST_ASSERT_EQUAL(0, led0.getFlashDuration());

    Esp8266BaseConfig::setInt("fan_led_flash_ms", 2000);
    FanDriver fan1(5, 12); ButtonDriver btn1(14, 4);
    LedIndicator led1(2, true); IRReceiverDriver ir1(13);
    FanController ctrl1(fan1, btn1, led1, ir1);
    ctrl1.begin();
    TEST_ASSERT_EQUAL(2000, ctrl1.getLedFlashDuration());
    TEST_ASSERT_EQUAL(2000, led1.getFlashDuration());
}

void test_controller_led_flash_duration_clamps_config_values() {
    Esp8266BaseConfig::setInt("fan_led_flash_ms", -1);
    FanDriver fan0(5, 12); ButtonDriver btn0(14, 4);
    LedIndicator led0(2, true); IRReceiverDriver ir0(13);
    FanController ctrl0(fan0, btn0, led0, ir0);
    ctrl0.begin();
    TEST_ASSERT_EQUAL(0, ctrl0.getLedFlashDuration());

    Esp8266BaseConfig::setInt("fan_led_flash_ms", 2001);
    FanDriver fan1(5, 12); ButtonDriver btn1(14, 4);
    LedIndicator led1(2, true); IRReceiverDriver ir1(13);
    FanController ctrl1(fan1, btn1, led1, ir1);
    ctrl1.begin();
    TEST_ASSERT_EQUAL(2000, ctrl1.getLedFlashDuration());
}

void test_controller_set_led_flash_duration_clamps_to_2000() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    ctrl.setLedFlashDuration(2500);
    TEST_ASSERT_EQUAL(2000, ctrl.getLedFlashDuration());
    TEST_ASSERT_EQUAL(2000, led.getFlashDuration());
    TEST_ASSERT_EQUAL(2000, Esp8266BaseConfig::getInt("fan_led_flash_ms", -1));
}

void test_controller_auto_restore_disabled_skips_restore_state_writes() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    ctrl.setAutoRestore(false);
    ctrl.setTimer(1800);

    g_mock_millis = 6000;
    ctrl.setSpeed(50);

    TEST_ASSERT_EQUAL(-1, Esp8266BaseConfig::getInt("fan_last_speed", -1));
    TEST_ASSERT_EQUAL(-1, Esp8266BaseConfig::getInt("fan_last_timer", -1));
}

void test_controller_runtime_state_saved_for_timer_and_stop() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    fan.setBlockDetectTime(60000);
    fan.setSoftStartTime(0);
    fan.setSoftStopTime(0);

    ctrl.setTimer(1800);
    TEST_ASSERT_EQUAL(1800, Esp8266BaseConfig::getInt("fan_last_timer", -1));

    ctrl.setSpeed(42);
    TEST_ASSERT_EQUAL(42, Esp8266BaseConfig::getInt("fan_last_speed", -1));
    TEST_ASSERT_EQUAL(2, ctrl.getCurrentGear());

    ctrl.stop();
    TEST_ASSERT_EQUAL(0, Esp8266BaseConfig::getInt("fan_last_speed", -1));
    TEST_ASSERT_EQUAL(0, Esp8266BaseConfig::getInt("fan_last_timer", -1));
}

void test_controller_sleep_mode() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    ctrl.setSleepWaitTime(5);

    // Fan stops immediately (soft_stop_time default is 1s, but no tach so block triggers)
    // Disable block detection and soft stop for this test
    fan.setBlockDetectTime(60000);
    fan.setSoftStopTime(0);

    ctrl.setSpeed(0);
    g_mock_millis = 100; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_IDLE, ctrl.getState());

    g_mock_millis = 6000; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_SLEEP, ctrl.getState());
    TEST_ASSERT_TRUE(ctrl.isSleeping());
}

void test_controller_wake_from_sleep() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    ctrl.setSleepWaitTime(5);
    fan.setBlockDetectTime(60000);
    fan.setSoftStopTime(0);

    ctrl.setSpeed(0);
    g_mock_millis = 6000; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_SLEEP, ctrl.getState());

    // Trigger a button press to wake (sets _last_operation_tick via setSpeed inside processButtonEvents)
    // Directly call setSpeed to simulate wake
    g_mock_millis = 7000;
    ctrl.setSpeed(25);
    ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_RUNNING, ctrl.getState());
    TEST_ASSERT_FALSE(ctrl.isSleeping());
}

void test_controller_button_wake_from_sleep_keeps_running_state() {
    g_pin_state[14] = HIGH; g_pin_state[4] = HIGH;
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    ctrl.setSleepWaitTime(5);
    fan.setBlockDetectTime(60000);
    fan.setSoftStartTime(0);
    fan.setSoftStopTime(0);

    ctrl.setSpeed(0);
    g_mock_millis = 6000; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_SLEEP, ctrl.getState());

    g_mock_millis = 6100; g_pin_state[14] = LOW; ctrl.tick();
    g_mock_millis = 6160; ctrl.tick();
    g_mock_millis = 6300; g_pin_state[14] = HIGH; ctrl.tick();
    g_mock_millis = 6360; ctrl.tick();

    TEST_ASSERT_EQUAL(SYS_RUNNING, ctrl.getState());
    TEST_ASSERT_FALSE(ctrl.isSleeping());
    TEST_ASSERT_EQUAL(25, ctrl.getTargetSpeed());
}

void test_controller_run_duration() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    fan.setBlockDetectTime(60000);  // must be after begin()
    fan.setSoftStartTime(0);

    ctrl.setSpeed(50);
    // Tick once per second for 5s; run_duration counts seconds in RUNNING state
    for (int i = 1; i <= 5; i++) {
        g_mock_millis = i * 1000;
        ctrl.tick();
    }
    TEST_ASSERT_EQUAL(5, ctrl.getTotalRunDuration());
}

void test_controller_config_persistence() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    ctrl.setMinEffectiveSpeed(20);
    ctrl.setSoftStartTime(2000);
    ctrl.setSoftStopTime(3000);
    ctrl.setBlockDetectTime(2000);
    ctrl.setSleepWaitTime(120);
    ctrl.setAutoRestore(false);
    FanDriver fan2(5, 12); ButtonDriver btn2(14, 4);
    LedIndicator led2(2, true); IRReceiverDriver ir2(13);
    FanController ctrl2(fan2, btn2, led2, ir2);
    ctrl2.begin();

    TEST_ASSERT_EQUAL(20,   ctrl2.getMinEffectiveSpeed());
    TEST_ASSERT_EQUAL(2000, ctrl2.getSoftStartTime());
    TEST_ASSERT_EQUAL(3000, ctrl2.getSoftStopTime());
    TEST_ASSERT_EQUAL(2000, ctrl2.getBlockDetectTime());
    TEST_ASSERT_EQUAL(120,  ctrl2.getSleepWaitTime());
    TEST_ASSERT_FALSE(ctrl2.getAutoRestore());
}

void test_controller_ir_persistence() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    ir.setKeyCode(0, 1, 0xE01F);
    ir.setKeyCode(1, 1, 0xD827);
    ir.setKeyCode(IR_KEY_TIMER_8H, 2, 0x00FFAA55);
    ctrl.testSaveConfig();

    FanDriver fan2(5, 12); ButtonDriver btn2(14, 4);
    LedIndicator led2(2, true); IRReceiverDriver ir2(13);
    FanController ctrl2(fan2, btn2, led2, ir2);
    ctrl2.begin();

    uint8_t p; uint64_t c;
    TEST_ASSERT_TRUE(ir2.getKeyCode(0, &p, &c));
    TEST_ASSERT_EQUAL(1,      p);
    TEST_ASSERT_EQUAL(0xE01F, c);
    TEST_ASSERT_TRUE(ir2.getKeyCode(IR_KEY_TIMER_8H, &p, &c));
    TEST_ASSERT_EQUAL(2, p);
    TEST_ASSERT_EQUAL(0x00FFAA55, c);
}

void test_controller_ir_persistence_keeps_equivalent_existing_format() {
    Esp8266BaseConfig::setStr("fan_ir_key_0", "1:E01F");
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    ir.setKeyCode(0, 1, 0xE01F);
    ctrl.testSaveConfig();

    char value[32];
    TEST_ASSERT_TRUE(Esp8266BaseConfig::getStr("fan_ir_key_0", value, sizeof(value), ""));
    TEST_ASSERT_EQUAL_STRING("1:E01F", value);
}

void test_controller_clear_ir_code_persists_empty_value() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    ir.setKeyCode(IR_KEY_TIMER_8H, 2, 0x00FFAA55);
    ctrl.testSaveConfig();

    TEST_ASSERT_TRUE(ctrl.clearIRCode(IR_KEY_TIMER_8H));
    uint8_t proto; uint64_t code;
    TEST_ASSERT_TRUE(ir.getKeyCode(IR_KEY_TIMER_8H, &proto, &code));
    TEST_ASSERT_EQUAL(0, proto);
    TEST_ASSERT_EQUAL(0, code);

    char value[32];
    TEST_ASSERT_TRUE(Esp8266BaseConfig::getStr("fan_ir_key_7", value, sizeof(value), "x"));
    TEST_ASSERT_EQUAL_STRING("", value);
    TEST_ASSERT_FALSE(ctrl.clearIRCode(IR_KEY_TIMER_8H));
}

void test_controller_block_recovery_failure_stays_error() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    ctrl.setSoftStartTime(0);
    ctrl.setBlockDetectTime(500);

    ctrl.setSpeed(50);
    g_mock_millis = 600; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_RUNNING, ctrl.getState());

    g_mock_millis = 1200; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_ERROR, ctrl.getState());

    ctrl.setSpeed(50);  // triggers recovery attempt
    g_mock_millis = 2700; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_ERROR, ctrl.getState());
    TEST_ASSERT_FALSE(fan.isBlocked());
}

void test_controller_block_recovery_success_returns_to_running() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    ctrl.setSoftStartTime(0);
    ctrl.setBlockDetectTime(500);

    ctrl.setSpeed(50);
    g_mock_millis = 600; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_RUNNING, ctrl.getState());
    g_mock_millis = 1200; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_ERROR, ctrl.getState());
    TEST_ASSERT_EQUAL(1, ctrl.getTotalRunDuration());

    ctrl.setSpeed(50);
    for (uint8_t i = 0; i < 20; i++) {
        if (g_isr_handlers[12] != nullptr) g_isr_handlers[12]();
    }
    g_mock_millis = 1800; ctrl.tick();
    TEST_ASSERT_TRUE(fan.getRpm() > 0);
    for (uint8_t i = 0; i < 20; i++) {
        if (g_isr_handlers[12] != nullptr) g_isr_handlers[12]();
    }
    g_mock_millis = 2300; ctrl.tick();
    TEST_ASSERT_TRUE(fan.getRpm() > 0);
    g_mock_millis = 2700; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_RUNNING, ctrl.getState());
    TEST_ASSERT_FALSE(fan.isBlocked());
    TEST_ASSERT_EQUAL(1, ctrl.getTotalRunDuration());

    g_mock_millis = 3700; ctrl.tick();
    TEST_ASSERT_EQUAL(2, ctrl.getTotalRunDuration());
}

void test_controller_wifi_disconnected_uses_slow_blink() {
    Esp8266BaseWiFi::testSetConnected(false);
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    g_mock_millis = 0; ctrl.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);

    g_mock_millis = 1000; ctrl.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
}

void test_controller_wifi_disconnected_overrides_running_gear() {
    Esp8266BaseWiFi::testSetConnected(false);
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    fan.setBlockDetectTime(60000);
    fan.setSoftStartTime(0);

    ctrl.setSpeed(50);
    g_mock_millis = 1000; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_RUNNING, ctrl.getState());
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
}

void test_controller_fault_fast_blink_overrides_wifi_and_gear() {
    Esp8266BaseWiFi::testSetConnected(false);
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    ctrl.setBlockDetectTime(500);

    ctrl.setSpeed(50);
    g_mock_millis = 2000; ctrl.tick();
    g_mock_millis = 2600; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_ERROR, ctrl.getState());

    g_mock_millis = 2800; ctrl.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
}

void test_controller_stop_clears_fault_to_wifi_slow_blink() {
    Esp8266BaseWiFi::testSetConnected(false);
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    ctrl.setBlockDetectTime(500);

    ctrl.setSpeed(50);
    g_mock_millis = 2000; ctrl.tick();
    g_mock_millis = 2600; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_ERROR, ctrl.getState());

    ctrl.stop();
    TEST_ASSERT_EQUAL(SYS_IDLE, ctrl.getState());
    g_mock_millis = 3600; ctrl.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);
    g_mock_millis = 4600; ctrl.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
}

void test_controller_web_speed_operation_flashes_once() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();
    fan.setBlockDetectTime(60000);

    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("speed", "50");
    FanWeb::handleApiSpeed();
    ctrl.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);

    g_mock_millis = 200; ctrl.tick();
    TEST_ASSERT_EQUAL(2, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(127, g_pwm_value[2]);
}

void test_controller_timer_operation_flashes_once() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    ctrl.setTimer(1800);
    ctrl.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
}

void test_controller_button_valid_operation_flashes_once() {
    g_pin_state[14] = HIGH; g_pin_state[4] = HIGH;
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    fan.setBlockDetectTime(60000);

    g_mock_millis = 100; g_pin_state[14] = LOW; ctrl.tick();
    g_mock_millis = 160; ctrl.tick();
    g_mock_millis = 300; g_pin_state[14] = HIGH; ctrl.tick();
    g_mock_millis = 360; ctrl.tick();

    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
    TEST_ASSERT_EQUAL(1, ctrl.getCurrentGear());
}

void test_controller_button_boundary_operation_does_not_flash() {
    g_pin_state[14] = HIGH; g_pin_state[4] = HIGH;
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    fan.setBlockDetectTime(60000);

    g_mock_millis = 100; g_pin_state[4] = LOW; ctrl.tick();
    g_mock_millis = 160; ctrl.tick();
    g_mock_millis = 300; g_pin_state[4] = HIGH; ctrl.tick();
    g_mock_millis = 360; ctrl.tick();

    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);
    TEST_ASSERT_EQUAL(0, ctrl.getCurrentGear());
}

void test_controller_ir_timer_operation_flashes_once() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    ir.testQueueEvent(IR_EVENT_TIMER_30M);
    ctrl.tick();
    TEST_ASSERT_EQUAL(1800, ctrl.getTimerRemaining());
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
}

void test_controller_ir_extended_timer_operations() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    ir.testQueueEvent(IR_EVENT_TIMER_4H);
    ctrl.tick();
    TEST_ASSERT_EQUAL(14400, ctrl.getTimerRemaining());

    ir.testQueueEvent(IR_EVENT_TIMER_8H);
    ctrl.tick();
    TEST_ASSERT_EQUAL(28800, ctrl.getTimerRemaining());
}

void test_controller_ir_learning_success_flashes_once() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    ir.testMarkLearned(IR_KEY_STOP, 1, 0xE01F);
    ctrl.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
}

void test_controller_factory_reset() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    ctrl.setMinEffectiveSpeed(20);
    ctrl.setAutoRestore(false);
    ctrl.resetFactory();
    TEST_ASSERT_TRUE(ESP.restartCalled());
    TEST_ASSERT_EQUAL(10, Esp8266BaseConfig::getInt("fan_min_speed", 10));
    TEST_ASSERT_TRUE(Esp8266BaseConfig::getBool("fan_auto_restore", true));
}

// ─── FanWeb Tests ─────────────────────────────────────────────────────────────

void test_web_api_speed_get() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();
    fan.setBlockDetectTime(60000);

    ctrl.setSpeed(42);
    g_mock_millis = 2000; ctrl.tick();

    MockWebServer::setMethod(HTTP_GET);
    FanWeb::handleApiSpeed();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"speed\":42"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"target_speed\":42"));
}

void test_web_api_speed_set() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();
    fan.setBlockDetectTime(60000);

    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("speed", "75");
    FanWeb::handleApiSpeed();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_EQUAL(75, ctrl.getTargetSpeed());

    g_mock_millis = 2000; ctrl.tick();
    TEST_ASSERT_EQUAL(75, ctrl.getCurrentSpeed());
}

void test_web_api_speed_get_reports_target_when_blocked() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();
    ctrl.setBlockDetectTime(500);

    ctrl.setSpeed(50);
    g_mock_millis = 2000; ctrl.tick();
    g_mock_millis = 2600; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_ERROR, ctrl.getState());

    MockWebServer::setMethod(HTTP_GET);
    FanWeb::handleApiSpeed();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"speed\":0"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"target_speed\":50"));
}

void test_web_status_page_merges_blocked_and_shows_business_metrics() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();
    ctrl.setBlockDetectTime(500);
    ctrl.setMinEffectiveSpeed(12);
    ctrl.setSoftStartTime(700);
    ctrl.setSoftStopTime(800);
    ctrl.setSleepWaitTime(45);
    ctrl.setLedFlashDuration(300);
    ctrl.setSpeed(50);
    g_mock_millis = 2000; ctrl.tick();
    g_mock_millis = 2600; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_ERROR, ctrl.getState());

    FanWeb::handleStatusPage();
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "Error / Blocked"));
    TEST_ASSERT_NULL(strstr(g_web_page_body, "<span>IP</span>"));
    TEST_ASSERT_NULL(strstr(g_web_page_body, "<span>Blocked</span>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span>RPM</span>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span>Gear</span>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span>Min speed</span>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span>Soft start / stop</span>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span>Block detect</span>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span>Sleep wait</span>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span>Restore</span>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span>LED flash</span>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span>Date</span>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span>Time</span>"));
    TEST_ASSERT_NULL(strstr(g_web_page_body, "<span>Clock</span>"));
}

void test_web_status_page_topline_hides_target_when_equal() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();
    fan.setBlockDetectTime(60000);
    fan.setSoftStartTime(0);

    ctrl.setSpeed(75);
    g_mock_millis = 100; ctrl.tick();

    FanWeb::handleStatusPage();
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span id=outTop>75"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span id=rpmTop>0 rpm"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span id=targetTopWrap style='display:none'"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "Target <span id=tgtTop>75"));
}

void test_web_status_page_topline_shows_target_when_different() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();
    fan.setBlockDetectTime(60000);

    ctrl.setSpeed(75);
    g_mock_millis = 100; ctrl.tick();

    FanWeb::handleStatusPage();
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span id=outTop>10"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span id=rpmTop>0 rpm"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "<span id=targetTopWrap>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "Target <span id=tgtTop>75"));
}

void test_web_api_status_reports_business_metrics() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();
    ctrl.setMinEffectiveSpeed(12);
    ctrl.setSoftStartTime(700);
    ctrl.setSoftStopTime(800);
    ctrl.setBlockDetectTime(900);
    ctrl.setSleepWaitTime(45);
    ctrl.setAutoRestore(false);
    ctrl.setLedFlashDuration(300);

    MockWebServer::setMethod(HTTP_GET);
    FanWeb::handleApiStatus();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"gear\":0"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"rpm\":0"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"min_speed\":12"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"soft_start\":700"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"soft_stop\":800"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"block_detect\":900"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"sleep_wait\":45"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"auto_restore\":false"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"led_flash_ms\":300"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"ir_reject_seq\":0"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"ir_duplicate_key\":8"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"ir_last_code\":\"0x"));
}

void test_web_api_timer() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();

    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("seconds", "7200");
    FanWeb::handleApiTimer();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_EQUAL(7200, ctrl.getTimerRemaining());
}

void test_web_api_stop() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();

    ctrl.setSpeed(50);
    ctrl.setTimer(3600);

    MockWebServer::setMethod(HTTP_POST);
    FanWeb::handleApiStop();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_EQUAL(0, ctrl.getTimerRemaining());
}

void test_web_api_config_get() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();
    ctrl.setMinEffectiveSpeed(15);
    ctrl.setSleepWaitTime(90);
    ctrl.setLedFlashDuration(1200);

    MockWebServer::setMethod(HTTP_GET);
    FanWeb::handleApiConfig();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"min_effective_speed\":15"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"sleep_wait\":90"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"led_flash_ms\":1200"));
}

void test_web_api_config_rejects_invalid_values() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();

    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("block_detect", "0");
    FanWeb::handleApiConfig();
    TEST_ASSERT_EQUAL(400, MockWebServer::lastCode());

    MockWebServer::reset();
    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("min_speed", "-1");
    FanWeb::handleApiConfig();
    TEST_ASSERT_EQUAL(400, MockWebServer::lastCode());

    MockWebServer::reset();
    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("led_flash_ms", "2001");
    FanWeb::handleApiConfig();
    TEST_ASSERT_EQUAL(400, MockWebServer::lastCode());

    MockWebServer::reset();
    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("led_flash_ms", "-1");
    FanWeb::handleApiConfig();
    TEST_ASSERT_EQUAL(400, MockWebServer::lastCode());

    MockWebServer::reset();
    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("led_flash_ms", "abc");
    FanWeb::handleApiConfig();
    TEST_ASSERT_EQUAL(400, MockWebServer::lastCode());
}

void test_web_api_config_rejects_without_partial_apply() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();

    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("min_speed", "15");
    MockWebServer::setArg("soft_start", "99999");
    FanWeb::handleApiConfig();

    TEST_ASSERT_EQUAL(400, MockWebServer::lastCode());
    TEST_ASSERT_EQUAL(10, ctrl.getMinEffectiveSpeed());
    TEST_ASSERT_EQUAL(10, Esp8266BaseConfig::getInt("fan_min_speed", 10));
}

void test_web_api_config_save_flashes_once() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();

    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("min_speed", "12");
    FanWeb::handleApiConfig();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());

    ctrl.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(LOW, g_pin_state[2]);
}

void test_web_api_config_no_change_does_not_flash() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();

    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("min_speed", "10");
    FanWeb::handleApiConfig();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"changed\":0"));

    ctrl.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);
}

void test_web_api_config_led_flash_ms_zero_disables_feedback() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();

    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("led_flash_ms", "0");
    FanWeb::handleApiConfig();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_EQUAL(0, ctrl.getLedFlashDuration());
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"changed\":1"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"led_flash_ms\":0"));

    ctrl.tick();
    TEST_ASSERT_EQUAL(1, g_pin_write_kind[2]);
    TEST_ASSERT_EQUAL(HIGH, g_pin_state[2]);
}

void test_web_api_config_led_flash_ms_2000_saves() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();

    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("led_flash_ms", "2000");
    FanWeb::handleApiConfig();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_EQUAL(2000, ctrl.getLedFlashDuration());
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"changed\":1"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"led_flash_ms\":2000"));
}

void test_config_page_contains_led_flash_field() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();

    FanWeb::handleConfigPage();
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "name=led_flash_ms"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "min=0 max=2000"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "0 disables action feedback flash."));
}

void test_config_page_contains_extended_ir_learning_buttons() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();
    ir.setKeyCode(IR_KEY_TIMER_2H, 2, 0x00FFAA55);
    ir.setKeyCode(IR_KEY_TIMER_8H, 2, 0x00FFAA55);

    FanWeb::handleConfigPage();
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "id=irv0"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, ">Speed Up</b>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "id=irv7"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "Duplicate: protocol 2 - 0x0000000000FFAA55"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "id=irv7 class=warn"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "learn(6,\"4 h\")"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, ">4 h</b>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "learn(7,\"8 h\")"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, ">8 h</b>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "clearIr(7,\"8 h\")"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "confirm('Clear IR code for '+n+'?')"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, ">Clear</button>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "setIrRow(i,v)"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "Already assigned to '+irName(d.ir_duplicate_key)"));
}

void test_status_page_contains_4h_and_8h_timer_presets() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();

    FanWeb::handleStatusPage();
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "tm(240)"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, ">4 h</button>"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, "tm(480)"));
    TEST_ASSERT_NOT_NULL(strstr(g_web_page_body, ">8 h</button>"));
}

void test_web_api_ir_learn() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();

    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("key_index", "2");
    FanWeb::handleApiIrLearn();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_TRUE(ir.isLearning());
    TEST_ASSERT_EQUAL(2, ir.getLearnedKeyIndex());
}

void test_web_api_ir_learn_accepts_8h_and_rejects_out_of_range() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();

    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("key_index", "7");
    FanWeb::handleApiIrLearn();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_TRUE(ir.isLearning());
    TEST_ASSERT_EQUAL(IR_KEY_TIMER_8H, ir.getLearnedKeyIndex());
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"rej_seq\":0"));

    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("key_index", "8");
    FanWeb::handleApiIrLearn();
    TEST_ASSERT_EQUAL(400, MockWebServer::lastCode());
}

void test_web_api_ir_clear() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();
    ir.setKeyCode(IR_KEY_TIMER_8H, 2, 0x00FFAA55);
    ctrl.testSaveConfig();

    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("key_index", "7");
    MockWebServer::setArg("clear", "1");
    FanWeb::handleApiIrLearn();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"changed\":true"));

    uint8_t proto; uint64_t code;
    TEST_ASSERT_TRUE(ir.getKeyCode(IR_KEY_TIMER_8H, &proto, &code));
    TEST_ASSERT_EQUAL(0, proto);
    TEST_ASSERT_EQUAL(0, code);

    MockWebServer::setMethod(HTTP_POST);
    MockWebServer::setArg("key_index", "7");
    MockWebServer::setArg("clear", "1");
    FanWeb::handleApiIrLearn();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"changed\":false"));
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    UNITY_BEGIN();

    // FanDriver
    RUN_TEST(test_fan_driver_begin);
    RUN_TEST(test_fan_driver_set_speed);
    RUN_TEST(test_fan_driver_soft_stop);
    RUN_TEST(test_fan_driver_block_detection);
    RUN_TEST(test_fan_driver_reset_block);
    RUN_TEST(test_fan_driver_rpm_calculation);
    RUN_TEST(test_fan_driver_set_speed_blocked);

    // ButtonDriver
    RUN_TEST(test_button_driver_begin);
    RUN_TEST(test_button_accel_short_press);
    RUN_TEST(test_button_decel_short_press);
    RUN_TEST(test_button_both_long_press);
    RUN_TEST(test_button_both_long_requires_full_release_before_new_short_press);

    // IRReceiverDriver
    RUN_TEST(test_ir_driver_begin);
    RUN_TEST(test_ir_learning_mode);
    RUN_TEST(test_ir_learning_timeout);
    RUN_TEST(test_ir_set_get_key_code);
    RUN_TEST(test_ir_extended_timer_key_code);
    RUN_TEST(test_ir_learning_rejects_duplicate_code_for_other_key);

    // LedIndicator
    RUN_TEST(test_led_indicator_active_low_gear_brightness);
    RUN_TEST(test_led_indicator_pwm_to_off_writes_real_off);
    RUN_TEST(test_led_indicator_slow_and_fast_blink_timing);
    RUN_TEST(test_led_indicator_flash_restores_gear_brightness);
    RUN_TEST(test_led_indicator_flash_inverts_full_brightness);
    RUN_TEST(test_led_indicator_flash_inverts_slow_blink_on_phase);
    RUN_TEST(test_led_indicator_flash_restores_wifi_slow_blink);
    RUN_TEST(test_led_indicator_flash_duration_zero_disables_feedback);
    RUN_TEST(test_led_indicator_flash_duration_2000ms_restores_after_window);
    RUN_TEST(test_led_indicator_flash_does_not_override_fast_blink);

    // FanController
    RUN_TEST(test_controller_begin);
    RUN_TEST(test_controller_set_speed);
    RUN_TEST(test_controller_min_effective_speed);
    RUN_TEST(test_controller_min_effective_speed_applies_to_soft_start_floor);
    RUN_TEST(test_controller_set_speed_clamps_public_input_to_100);
    RUN_TEST(test_controller_stop);
    RUN_TEST(test_controller_stop_clears_error_state);
    RUN_TEST(test_controller_timer);
    RUN_TEST(test_controller_timer_countdown);
    RUN_TEST(test_controller_timer_catches_up_after_late_tick);
    RUN_TEST(test_controller_run_duration_catches_up_after_late_tick);
    RUN_TEST(test_controller_timer_countdown_while_idle);
    RUN_TEST(test_controller_timer_countdown_while_error);
    RUN_TEST(test_controller_power_on_restore);
    RUN_TEST(test_controller_power_on_restore_disabled);
    RUN_TEST(test_controller_led_flash_duration_defaults_to_200);
    RUN_TEST(test_controller_led_flash_duration_loads_bounds);
    RUN_TEST(test_controller_led_flash_duration_clamps_config_values);
    RUN_TEST(test_controller_set_led_flash_duration_clamps_to_2000);
    RUN_TEST(test_controller_auto_restore_disabled_skips_restore_state_writes);
    RUN_TEST(test_controller_runtime_state_saved_for_timer_and_stop);
    RUN_TEST(test_controller_sleep_mode);
    RUN_TEST(test_controller_wake_from_sleep);
    RUN_TEST(test_controller_button_wake_from_sleep_keeps_running_state);
    RUN_TEST(test_controller_run_duration);
    RUN_TEST(test_controller_config_persistence);
    RUN_TEST(test_controller_ir_persistence);
    RUN_TEST(test_controller_ir_persistence_keeps_equivalent_existing_format);
    RUN_TEST(test_controller_clear_ir_code_persists_empty_value);
    RUN_TEST(test_controller_block_recovery_failure_stays_error);
    RUN_TEST(test_controller_block_recovery_success_returns_to_running);
    RUN_TEST(test_controller_wifi_disconnected_uses_slow_blink);
    RUN_TEST(test_controller_wifi_disconnected_overrides_running_gear);
    RUN_TEST(test_controller_fault_fast_blink_overrides_wifi_and_gear);
    RUN_TEST(test_controller_stop_clears_fault_to_wifi_slow_blink);
    RUN_TEST(test_controller_web_speed_operation_flashes_once);
    RUN_TEST(test_controller_timer_operation_flashes_once);
    RUN_TEST(test_controller_button_valid_operation_flashes_once);
    RUN_TEST(test_controller_button_boundary_operation_does_not_flash);
    RUN_TEST(test_controller_ir_timer_operation_flashes_once);
    RUN_TEST(test_controller_ir_extended_timer_operations);
    RUN_TEST(test_controller_ir_learning_success_flashes_once);
    RUN_TEST(test_controller_factory_reset);

    // FanWeb
    RUN_TEST(test_web_api_speed_get);
    RUN_TEST(test_web_api_speed_set);
    RUN_TEST(test_web_api_speed_get_reports_target_when_blocked);
    RUN_TEST(test_web_status_page_merges_blocked_and_shows_business_metrics);
    RUN_TEST(test_web_status_page_topline_hides_target_when_equal);
    RUN_TEST(test_web_status_page_topline_shows_target_when_different);
    RUN_TEST(test_web_api_status_reports_business_metrics);
    RUN_TEST(test_web_api_timer);
    RUN_TEST(test_web_api_stop);
    RUN_TEST(test_web_api_config_get);
    RUN_TEST(test_web_api_config_rejects_invalid_values);
    RUN_TEST(test_web_api_config_rejects_without_partial_apply);
    RUN_TEST(test_web_api_config_save_flashes_once);
    RUN_TEST(test_web_api_config_no_change_does_not_flash);
    RUN_TEST(test_web_api_config_led_flash_ms_zero_disables_feedback);
    RUN_TEST(test_web_api_config_led_flash_ms_2000_saves);
    RUN_TEST(test_config_page_contains_led_flash_field);
    RUN_TEST(test_config_page_contains_extended_ir_learning_buttons);
    RUN_TEST(test_status_page_contains_4h_and_8h_timer_presets);
    RUN_TEST(test_web_api_ir_learn);
    RUN_TEST(test_web_api_ir_learn_accepts_8h_and_rejects_out_of_range);
    RUN_TEST(test_web_api_ir_clear);

    return UNITY_END();
}
