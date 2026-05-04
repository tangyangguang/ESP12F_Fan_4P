// Mock EbNTP.h for native testing
#ifndef EB_NTP_MOCK_H
#define EB_NTP_MOCK_H

#include <stdint.h>
#include <stddef.h>

namespace EbNTP {
    bool isSynced();
    bool formatTo(char* out, size_t out_len, const char* format);
}

#endif
