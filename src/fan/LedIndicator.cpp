#include "fan/LedIndicator.h"

#include <Arduino.h>
#include "Esp8266BaseLog.h"

LedIndicator::LedIndicator(uint8_t pin, bool active_low)
    : _pin(pin)
    , _active_low(active_low)
    , _current_gear(0)
    , _base_mode(LED_OFF)
    , _last_toggle(0)
    , _blink_state(false)
    , _flash_start(0)
    , _flash_active(false)
    , _flash_output_on(false)
    , _output_on(false)
    , _flash_duration_ms(DEFAULT_FLASH_DURATION) {
}

bool LedIndicator::begin() {
    pinMode(_pin, OUTPUT);
    analogWriteRange(255);
    writeDigital(false);

    ESP8266BASE_LOG_I("LedInd", "Initialized: GPIO%d, active_low=%s", _pin, _active_low ? "true" : "false");
    return true;
}

void LedIndicator::tick() {
    update();
}

void LedIndicator::setGear(uint8_t gear) {
    if (gear > 4) gear = 4;
    _current_gear = gear;
    LedMode next = gear > 0 ? LED_ON : LED_OFF;
    if (_base_mode != next) {
        _base_mode = next;
        resetBlinkClock();
    }
}

void LedIndicator::setOverride(LedMode mode) {
    if (mode == LED_SINGLE_FLASH) {
        flashOnce();
        return;
    }
    if (_base_mode != mode) {
        _base_mode = mode;
        resetBlinkClock();
    }
    if (mode == LED_FAST_BLINK) {
        _flash_active = false;
    }
}

void LedIndicator::setFlashDuration(uint16_t ms) {
    _flash_duration_ms = ms;
    if (_flash_duration_ms == 0) {
        _flash_active = false;
    }
}

uint16_t LedIndicator::getFlashDuration() const {
    return _flash_duration_ms;
}

void LedIndicator::flashOnce() {
    if (_flash_duration_ms == 0) return;
    if (_base_mode == LED_FAST_BLINK) return;
    _flash_start = millis();
    _flash_output_on = !_output_on;
    _flash_active = true;
}

void LedIndicator::update() {
    uint32_t now = millis();
    if (_flash_active && _base_mode != LED_FAST_BLINK) {
        if (now - _flash_start < _flash_duration_ms) {
            writeDigital(_flash_output_on);
            return;
        }
        _flash_active = false;
    }

    switch (_base_mode) {
        case LED_OFF:
            writeDigital(false);
            break;

        case LED_ON:
            if (_current_gear <= 4) {
                static const uint8_t brightness_by_gear[5] = {0, 64, 128, 192, 255};
                writeBrightness(brightness_by_gear[_current_gear]);
            } else {
                writeBrightness(255);
            }
            break;

        case LED_SLOW_BLINK:
            if (now - _last_toggle >= SLOW_BLINK_INTERVAL) {
                _blink_state = !_blink_state;
                _last_toggle = now;
            }
            writeDigital(_blink_state);
            break;

        case LED_FAST_BLINK:
            if (now - _last_toggle >= FAST_BLINK_INTERVAL) {
                _blink_state = !_blink_state;
                _last_toggle = now;
            }
            writeDigital(_blink_state);
            break;

        default:
            writeDigital(false);
            break;
    }
}

void LedIndicator::writeDigital(bool on) {
    _output_on = on;
    digitalWrite(_pin, _active_low ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
}

void LedIndicator::writeBrightness(uint8_t brightness) {
    if (brightness == 0) {
        writeDigital(false);
        return;
    }

    _output_on = true;
    analogWrite(_pin, _active_low ? static_cast<uint8_t>(255 - brightness) : brightness);
}

void LedIndicator::resetBlinkClock() {
    _last_toggle = millis();
    _blink_state = false;
}
