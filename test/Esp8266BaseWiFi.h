// Mock Esp8266BaseWiFi.h for native testing
#ifndef ESP8266BASE_WIFI_MOCK_H
#define ESP8266BASE_WIFI_MOCK_H

#include <stdint.h>

enum class Esp8266BaseWiFiState : uint8_t {
    IDLE       = 0,
    CONNECTING = 1,
    CONNECTED  = 2,
    AP_CONFIG  = 3,
    FAILED     = 4
};

namespace Esp8266BaseWiFi {
    bool isConnected();
    const char* ip();
    bool clearCredentials();
    Esp8266BaseWiFiState state();
}

#endif
