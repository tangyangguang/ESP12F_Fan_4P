// Mock EbLogger.h for native testing
#ifndef EB_LOGGER_MOCK_H
#define EB_LOGGER_MOCK_H

#include <stdint.h>

namespace EbLogger {
    bool begin(uint8_t level = 0, uint32_t baud_rate = 115200);
    void log(uint8_t level, const char* tag, const char* format, ...);
}

#define EB_LOG_D(tag, fmt, ...) ((void)0)
#define EB_LOG_I(tag, fmt, ...) ((void)0)
#define EB_LOG_W(tag, fmt, ...) ((void)0)
#define EB_LOG_E(tag, fmt, ...) ((void)0)

#endif
