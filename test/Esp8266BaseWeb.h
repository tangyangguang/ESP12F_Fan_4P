// Mock Esp8266BaseWeb.h for native testing
#ifndef ESP8266BASE_WEB_MOCK_H
#define ESP8266BASE_WEB_MOCK_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "Arduino.h"

// Mock web server — stores last request/response for test assertions
class MockWebServer {
public:
    int method() const;
    String arg(const char* name) const;
    String arg(const String& name) const { return arg(name.c_str()); }
    bool hasArg(const char* name) const;
    void send(uint16_t code, const char* content_type, const char* body);

    // Test helpers
    static void setMethod(int m);
    static void setArg(const char* name, const char* value);
    static void reset();
    static uint16_t lastCode();
    static const char* lastBody();
};

namespace Esp8266BaseWeb {
    MockWebServer& server();
    bool checkAuth();
    void sendHeader();
    void sendFooter();
    void sendChunk(const char* text);
    void sendContent_P(const char* text);
    bool addPage(const char* path, void (*handler)());
    bool addPage(const char* path, const char* title, void (*handler)());
    bool addApi(const char* path, void (*handler)());
    void setDefaultAuth(const char* user, const char* pass);
}

#endif
