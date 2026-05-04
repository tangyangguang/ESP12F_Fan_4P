// Mock Esp8266BaseSleep.h for native testing
#ifndef ESP8266BASE_SLEEP_MOCK_H
#define ESP8266BASE_SLEEP_MOCK_H

namespace Esp8266BaseSleep {
    bool modemSleep();
    void wakeModem();
    bool lightSleep(uint32_t ms = 0);
    bool noSleep();
}

#endif
