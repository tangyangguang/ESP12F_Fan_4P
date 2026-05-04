// Mock Esp8266BaseLog.h for native testing
#ifndef ESP8266BASE_LOG_MOCK_H
#define ESP8266BASE_LOG_MOCK_H

#define ESP8266BASE_LOG_D(tag, fmt, ...) ((void)0)
#define ESP8266BASE_LOG_I(tag, fmt, ...) ((void)0)
#define ESP8266BASE_LOG_W(tag, fmt, ...) ((void)0)
#define ESP8266BASE_LOG_E(tag, fmt, ...) ((void)0)

class Esp8266BaseLog {
public:
    static void log(unsigned char level, const char* tag, const char* fmt, ...) {
        (void)level;
        (void)tag;
        (void)fmt;
    }
};

#endif
