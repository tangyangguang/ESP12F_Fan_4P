#include "fan/FanDriver.h"

#include <Arduino.h>
#include "Esp8266BaseLog.h"

FanDriver* FanDriver::s_instance = nullptr;

FanDriver::FanDriver(uint8_t pwm_pin, uint8_t tach_pin)
    : _pwm_pin(pwm_pin)
    , _tach_pin(tach_pin)
    , _current_speed(0)
    , _target_speed(0)
    , _soft_start_time(1000)
    , _soft_stop_time(1000)
    , _block_detect_time(1500)
    , _min_effective_speed(10)
    , _soft_start_tick(0)
    , _block_start_tick(0)
    , _state(FAN_STATE_IDLE)
    , _tach_count(0)
    , _last_tach_ms(0)
    , _rpm(0)
    , _last_rpm_update_ms(0) {
    s_instance = this;
}

bool FanDriver::begin() {
    pinMode(_pwm_pin, OUTPUT);
    analogWriteFreq(25000);  // 25KHz PWM
    analogWrite(_pwm_pin, 0);

    pinMode(_tach_pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(_tach_pin), tachISR, FALLING);

    _state = FAN_STATE_IDLE;
    _last_tach_ms = millis();
    _last_rpm_update_ms = millis();
    _block_start_tick = millis();
    ESP8266BASE_LOG_I("FanDrv", "Initialized: PWM=GPIO%d, TACH=GPIO%d, freq=25KHz", _pwm_pin, _tach_pin);
    return true;
}

void FanDriver::tick() {
    uint32_t now = millis();

    // Update RPM (calculate every 500ms)
    if (now - _last_rpm_update_ms >= 500) {
        uint32_t elapsed = now - _last_tach_ms;
        noInterrupts();
        uint32_t tach_count = _tach_count;
        _tach_count = 0;
        interrupts();
        if (elapsed > 0 && tach_count > 0) {
            // RPM = (pulses / 2) * (60000 / elapsed_ms)
            // Assuming 2 pulses per revolution for typical 4-wire fans
            _rpm = static_cast<uint16_t>((tach_count * 30000UL) / elapsed);
        } else {
            _rpm = 0;
        }
        _last_tach_ms = now;
        _last_rpm_update_ms = now;
    }

    // Soft start/stop gradient
    if (_state == FAN_STATE_SOFT_START) {
        uint32_t elapsed = now - _soft_start_tick;
        if (_soft_start_time > 0 && elapsed < _soft_start_time) {
            uint32_t progress = (elapsed * 100) / _soft_start_time;
            uint8_t gradient_speed = static_cast<uint8_t>((_target_speed * progress) / 100);
            if (_min_effective_speed > 0 &&
                gradient_speed < _min_effective_speed &&
                _target_speed >= _min_effective_speed) {
                gradient_speed = _min_effective_speed;
            }
            analogWrite(_pwm_pin, gradient_speed * 255 / 100);
            _current_speed = gradient_speed;
        } else {
            _current_speed = _target_speed;
            analogWrite(_pwm_pin, _current_speed * 255 / 100);
            _state = FAN_STATE_RUNNING;
            _block_start_tick = 0;
            ESP8266BASE_LOG_I("FanDrv", "Soft start complete: %d%%", _current_speed);
        }
    } else if (_state == FAN_STATE_SOFT_STOP) {
        uint32_t elapsed = now - _soft_start_tick;
        if (_soft_stop_time > 0 && elapsed < _soft_stop_time) {
            uint32_t progress = (elapsed * 100) / _soft_stop_time;
            uint8_t gradient_speed = static_cast<uint8_t>(_current_speed * (100 - progress) / 100);
            analogWrite(_pwm_pin, gradient_speed * 255 / 100);
            _current_speed = gradient_speed;
        } else {
            _current_speed = 0;
            analogWrite(_pwm_pin, 0);
            _state = FAN_STATE_IDLE;
            ESP8266BASE_LOG_I("FanDrv", "Soft stop complete");
        }
    }

    // Block detection
    uint8_t block_threshold = _min_effective_speed > 0 ? _min_effective_speed : 1;
    if (_state == FAN_STATE_RUNNING && _current_speed >= block_threshold) {
        if (_rpm == 0) {
            // Only start counting if we haven't already started
            if (_block_start_tick == 0) {
                _block_start_tick = now;
            } else if (now - _block_start_tick >= _block_detect_time) {
                _state = FAN_STATE_BLOCKED;
                analogWrite(_pwm_pin, 0);
                _current_speed = 0;
                ESP8266BASE_LOG_E("FanDrv", "BLOCK DETECTED! Speed=%d%%, no RPM for %lums",
                         _target_speed, static_cast<unsigned long>(_block_detect_time));
            }
        } else {
            _block_start_tick = 0;  // Reset when RPM detected
        }
    }
}

bool FanDriver::setSpeed(uint8_t speed) {
    if (speed > 100) speed = 100;

    if (_state == FAN_STATE_BLOCKED) {
        ESP8266BASE_LOG_W("FanDrv", "setSpeed(%d) rejected: blocked", speed);
        return false;
    }

    _target_speed = speed;
    uint32_t now = millis();

    if (speed == 0) {
        if (_current_speed > 0 && _soft_stop_time > 0) {
            _state = FAN_STATE_SOFT_STOP;
            _soft_start_tick = now;
            ESP8266BASE_LOG_I("FanDrv", "Soft stop start: %d%% -> 0%% (%lums)",
                     _current_speed, static_cast<unsigned long>(_soft_stop_time));
        } else {
            _current_speed = 0;
            analogWrite(_pwm_pin, 0);
            _state = FAN_STATE_IDLE;
            ESP8266BASE_LOG_I("FanDrv", "Stop immediate");
        }
    } else {
        _block_start_tick = 0;
        if (_current_speed == 0 && _soft_start_time > 0) {
            _state = FAN_STATE_SOFT_START;
            _soft_start_tick = now;
            ESP8266BASE_LOG_I("FanDrv", "Soft start: 0%% -> %d%% (%lums)",
                     speed, static_cast<unsigned long>(_soft_start_time));
        } else {
            _current_speed = speed;
            analogWrite(_pwm_pin, speed * 255 / 100);
            _state = FAN_STATE_RUNNING;
            _block_start_tick = 0;
            ESP8266BASE_LOG_I("FanDrv", "Speed set: %d%%", speed);
        }
    }

    return true;
}

uint8_t FanDriver::getSpeed() const {
    return _current_speed;
}

uint16_t FanDriver::getRpm() const {
    return _rpm;
}

FanState FanDriver::getState() const {
    return _state;
}

void FanDriver::setSoftStartTime(uint16_t ms) {
    _soft_start_time = ms;
}

void FanDriver::setSoftStopTime(uint16_t ms) {
    _soft_stop_time = ms;
}

void FanDriver::setBlockDetectTime(uint16_t ms) {
    _block_detect_time = ms;
}

void FanDriver::setMinEffectiveSpeed(uint8_t speed) {
    if (speed > 50) speed = 50;
    _min_effective_speed = speed;
}

bool FanDriver::isBlocked() const {
    return _state == FAN_STATE_BLOCKED;
}

void FanDriver::resetBlock() {
    if (_state == FAN_STATE_BLOCKED) {
        _state = FAN_STATE_IDLE;
        _current_speed = 0;
        _target_speed = 0;
        _block_start_tick = millis();
        analogWrite(_pwm_pin, 0);
    }
}

void IRAM_ATTR FanDriver::tachISR() {
    if (s_instance != nullptr) {
        s_instance->_tach_count++;
    }
}
