// Mock EbBus.h for native testing
#ifndef EB_BUS_MOCK_H
#define EB_BUS_MOCK_H

#include <stdint.h>

namespace EbBus {
    bool begin();
    bool subscribe(const char* event, void* callback, void* user_data = nullptr);
    bool publish(const char* event, const char* data = "");
}

#endif
