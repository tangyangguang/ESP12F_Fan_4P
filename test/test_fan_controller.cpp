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

static uint8_t g_pin_mode[20]  = {0};
static uint8_t g_pin_state[20] = {0};
static uint32_t g_pwm_freq     = 0;
static uint8_t g_pwm_value[20] = {0};
static void (*g_isr_handlers[20])() = {nullptr};
static bool g_esp_restart_called = false;

void pinMode(uint8_t pin, uint8_t mode)                          { g_pin_mode[pin] = mode; }
uint8_t digitalRead(uint8_t pin)                                  { return g_pin_state[pin]; }
void digitalWrite(uint8_t pin, uint8_t val)                       { g_pin_state[pin] = val; }
void analogWriteFreq(uint32_t freq)                               { g_pwm_freq = freq; }
void analogWrite(uint8_t pin, uint8_t val)                        { g_pwm_value[pin] = val; }
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
    bool isConnected()               { return true; }
    const char* ip()                 { return "0.0.0.0"; }
    bool clearCredentials()          { return true; }
    bool setHostname(const char*)    { return true; }
    Esp8266BaseWiFiState state()     { return Esp8266BaseWiFiState::CONNECTED; }
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
    static MockWebServer g_mock_server;
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
    memset(g_web_args, 0, sizeof(g_web_args));
}
uint16_t MockWebServer::lastCode() { return g_web_last_code; }
const char* MockWebServer::lastBody() { return g_web_last_body; }

namespace Esp8266BaseWeb {
    MockWebServer& server()            { return g_mock_server; }
    bool checkAuth()                   { return true; }
    void sendHeader()                  {}
    void sendFooter()                  {}
    void sendChunk(const char*)        {}
    void sendContent_P(const char*)    {}
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
    memset(g_pwm_value, 0, sizeof(g_pwm_value));
    g_pwm_freq = 0;
    memset(g_isr_handlers, 0, sizeof(g_isr_handlers));
    g_esp_restart_called = false;
    Esp8266BaseConfig::reset();
    Esp8266BaseSleep::reset();
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

void test_controller_stop() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();

    ctrl.setSpeed(50);
    g_mock_millis = 1500; ctrl.tick();

    ctrl.stop();
    TEST_ASSERT_EQUAL(0, ctrl.getTimerRemaining());
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
    ctrl.testSaveConfig();

    FanDriver fan2(5, 12); ButtonDriver btn2(14, 4);
    LedIndicator led2(2, true); IRReceiverDriver ir2(13);
    FanController ctrl2(fan2, btn2, led2, ir2);
    ctrl2.begin();

    uint8_t p; uint64_t c;
    TEST_ASSERT_TRUE(ir2.getKeyCode(0, &p, &c));
    TEST_ASSERT_EQUAL(1,      p);
    TEST_ASSERT_EQUAL(0xE01F, c);
}

void test_controller_block_recovery() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    ctrl.begin();
    ctrl.setBlockDetectTime(500);

    ctrl.setSpeed(50);
    g_mock_millis = 2000; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_RUNNING, ctrl.getState());

    g_mock_millis = 2600; ctrl.tick();
    TEST_ASSERT_EQUAL(SYS_ERROR, ctrl.getState());

    ctrl.setSpeed(50);  // triggers recovery attempt
    g_mock_millis = 4200; ctrl.tick();
    // Recovery outcome depends on whether tach fires; here it won't, stays in error
    // Just verify no crash
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

    MockWebServer::setMethod(HTTP_GET);
    FanWeb::handleApiConfig();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"min_effective_speed\":15"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"sleep_wait\":90"));
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

void test_web_api_ir_status() {
    FanDriver fan(5, 12); ButtonDriver btn(14, 4);
    LedIndicator led(2, true); IRReceiverDriver ir(13);
    FanController ctrl(fan, btn, led, ir);
    FanWeb web(ctrl, ir);
    ctrl.begin();
    ir.setKeyCode(0, 1, 0xE01F);

    MockWebServer::setMethod(HTTP_GET);
    FanWeb::handleApiIrStatus();
    TEST_ASSERT_EQUAL(200, MockWebServer::lastCode());
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "\"ok\":true"));
    TEST_ASSERT_NOT_NULL(strstr(MockWebServer::lastBody(), "0x0000E01F"));  // %08llX pads to 8 hex digits
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

    // IRReceiverDriver
    RUN_TEST(test_ir_driver_begin);
    RUN_TEST(test_ir_learning_mode);
    RUN_TEST(test_ir_learning_timeout);
    RUN_TEST(test_ir_set_get_key_code);

    // FanController
    RUN_TEST(test_controller_begin);
    RUN_TEST(test_controller_set_speed);
    RUN_TEST(test_controller_min_effective_speed);
    RUN_TEST(test_controller_stop);
    RUN_TEST(test_controller_timer);
    RUN_TEST(test_controller_timer_countdown);
    RUN_TEST(test_controller_timer_countdown_while_idle);
    RUN_TEST(test_controller_power_on_restore);
    RUN_TEST(test_controller_power_on_restore_disabled);
    RUN_TEST(test_controller_sleep_mode);
    RUN_TEST(test_controller_wake_from_sleep);
    RUN_TEST(test_controller_run_duration);
    RUN_TEST(test_controller_config_persistence);
    RUN_TEST(test_controller_ir_persistence);
    RUN_TEST(test_controller_block_recovery);
    RUN_TEST(test_controller_factory_reset);

    // FanWeb
    RUN_TEST(test_web_api_speed_get);
    RUN_TEST(test_web_api_speed_set);
    RUN_TEST(test_web_api_speed_get_reports_target_when_blocked);
    RUN_TEST(test_web_api_timer);
    RUN_TEST(test_web_api_stop);
    RUN_TEST(test_web_api_config_get);
    RUN_TEST(test_web_api_ir_learn);
    RUN_TEST(test_web_api_ir_status);

    return UNITY_END();
}
