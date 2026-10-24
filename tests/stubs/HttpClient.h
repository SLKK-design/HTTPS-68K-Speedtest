#pragma once
/* Host-test stub for src/core/net/HttpClient.h.
   Shadowed via include path in tests/run_tests.sh — claude_client.cpp's
   #include "HttpClient.h" resolves here instead of the real (MacTCP/mbedTLS)
   client. Captures PostStream arguments so tests can assert on the built
   request body. */

#include <string>
#include <functional>

class HttpResponse
{
public:
    bool         Success    = true;
    unsigned int StatusCode = 200;
    std::string  Content;
};

class HttpClient
{
public:
    std::string lastEndpoint;
    std::string lastBody;
    std::string lastAuth;
    std::string lastHeaders;
    int         postCount = 0;
    std::function<void(const char*, int)> onChunk;
    std::function<void(HttpResponse&)>    onComplete;

    void SetAuthorization(std::string a)   { lastAuth = a; }
    void SetExtraHeaders(const char* h)    { lastHeaders = h ? h : ""; }
    void PostStream(const std::string& requestUri, const std::string& content,
                    std::function<void(const char*, int)> chunk,
                    std::function<void(HttpResponse&)> done)
    {
        lastEndpoint = requestUri;
        lastBody     = content;
        onChunk      = chunk;
        onComplete   = done;
        postCount++;
    }
};
