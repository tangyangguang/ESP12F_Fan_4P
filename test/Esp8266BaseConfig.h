// Mock Esp8266BaseConfig.h for native testing
// 2-argument API: setInt(key, value), getInt(key, default)
#ifndef ESP8266BASE_CONFIG_MOCK_H
#define ESP8266BASE_CONFIG_MOCK_H

#include <stdint.h>
#include <stddef.h>

namespace Esp8266BaseConfig {
    int32_t getInt(const char* key, int32_t default_value = 0);
    bool setInt(const char* key, int32_t value);
    bool setIntDeferred(const char* key, int32_t value);
    bool getBool(const char* key, bool default_value = false);
    bool setBool(const char* key, bool value);
    bool setStr(const char* key, const char* value);
    bool getStr(const char* key, char* out, size_t out_len, const char* default_value = "");
    bool flush();
    bool clearAll();

    // Test helper — resets all stored values
    void reset();
}

#endif
