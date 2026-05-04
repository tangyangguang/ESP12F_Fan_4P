// Mock Esp8266BaseNTP.h for native testing
#ifndef ESP8266BASE_NTP_MOCK_H
#define ESP8266BASE_NTP_MOCK_H

#include <stddef.h>

namespace Esp8266BaseNTP {
    bool isSynced();
    bool formatTo(char* out, size_t out_len, const char* format);
}

#endif
