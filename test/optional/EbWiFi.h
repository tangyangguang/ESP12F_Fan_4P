// Mock EbWiFi.h for native testing
#ifndef EB_WIFI_MOCK_H
#define EB_WIFI_MOCK_H

#include <stdint.h>

namespace EbWiFi {
    enum PowerSaveMode { POWER_SAVE_OFF = 0, POWER_SAVE_MODEM };

    bool setPowerSaveMode(PowerSaveMode mode);
    bool isConnected();
    PowerSaveMode getPowerSaveMode();

    // Test helpers
    void reset();
}

#endif
