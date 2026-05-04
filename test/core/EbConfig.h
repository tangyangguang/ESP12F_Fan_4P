// Mock EbConfig.h for native testing
#ifndef EB_CONFIG_MOCK_H
#define EB_CONFIG_MOCK_H

#include <stddef.h>
#include <stdint.h>

namespace EbConfig {
    bool setInt(const char* ns, const char* key, int32_t value);
    int32_t getInt(const char* ns, const char* key, int32_t default_value = 0);
    bool setBool(const char* ns, const char* key, bool value);
    bool getBool(const char* ns, const char* key, bool default_value = false);
    bool setStr(const char* ns, const char* key, const char* value);
    bool getStr(const char* ns, const char* key, char* out, size_t out_len, const char* default_value = "");
    bool clearNamespace(const char* ns);
}

#endif
