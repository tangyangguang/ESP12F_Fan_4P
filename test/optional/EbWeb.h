// Mock EbWeb.h for native testing
#ifndef EB_WEB_MOCK_H
#define EB_WEB_MOCK_H

#include <stdint.h>
#include <stddef.h>

namespace EbWeb {
    enum Method { GET = 0, POST, ANY };

    Method getMethod();
    bool getRequestBody(char* out, size_t out_len);
    void send(uint16_t code, const char* content_type, const char* content);
    void sendJSON(uint16_t code, const char* json);
    void sendHTML(uint16_t code, const char* html);
    void addRoute(const char* path, void (*handler)());
    void addPostRoute(const char* path, void (*handler)());

    // Test helpers
    uint16_t lastCode();
    const char* lastContent();
    void setBody(const char* body);
    void setMethod(Method m);
    void reset();
}

#endif
