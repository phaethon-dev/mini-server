#include <cassert>
#include <iostream>
#include <string>

#include "http.h"

// True if the response has a header with exactly this name and value
bool hasHeader(const HttpResponse &response, const std::string &name, const std::string &value)
{
    auto it = response.headers.find(name);
    return it != response.headers.end() && it->second == value;
}

// The HTML page every error response should carry, e.g. "<h1>404 Not Found</h1>"
std::string errorPage(const std::string &statusAndReason)
{
    return "<html><body><h1>" + statusAndReason + "</h1></body></html>";
}

void testStatusLines()
{
    assert(badRequest().status == 400);
    assert(badRequest().reason == "Bad Request");

    assert(notFound().status == 404);
    assert(notFound().reason == "Not Found");

    assert(methodNotAllowed().status == 405);
    assert(methodNotAllowed().reason == "Method Not Allowed");
}

void testSharedHeaders()
{
    // Every error response is an HTML page, and the connection closes afterwards
    for (const HttpResponse &response : {badRequest(), notFound(), methodNotAllowed()})
    {
        assert(hasHeader(response, "Content-Type", "text/html"));
        assert(hasHeader(response, "Connection", "close"));
    }
}

void testBodies()
{
    // The page shows the same status and reason as the status line
    assert(badRequest().body == errorPage("400 Bad Request"));
    assert(notFound().body == errorPage("404 Not Found"));
    assert(methodNotAllowed().body == errorPage("405 Method Not Allowed"));

    // The general builder works for any status, not just the named ones
    HttpResponse teapot = makeErrorResponse(418, "I'm a teapot");
    assert(teapot.status == 418);
    assert(teapot.reason == "I'm a teapot");
    assert(teapot.body == errorPage("418 I'm a teapot"));
}

void testAllowHeader()
{
    // RFC 9110 requires Allow on every 405, listing the supported methods
    assert(hasHeader(methodNotAllowed(), "Allow", "GET, HEAD"));

    // ...and it belongs only on the 405
    assert(badRequest().headers.count("Allow") == 0);
    assert(notFound().headers.count("Allow") == 0);
}

void testExactOutput()
{
    // The complete bytes sent for a 404, headers in alphabetical order
    assert(notFound().toString() ==
           "HTTP/1.1 404 Not Found\r\n"
           "Connection: close\r\n"
           "Content-Type: text/html\r\n"
           "Content-Length: 48\r\n"
           "\r\n"
           "<html><body><h1>404 Not Found</h1></body></html>");

    // The 405 adds Allow, which sorts before the other headers
    assert(methodNotAllowed().toString() ==
           "HTTP/1.1 405 Method Not Allowed\r\n"
           "Allow: GET, HEAD\r\n"
           "Connection: close\r\n"
           "Content-Type: text/html\r\n"
           "Content-Length: 57\r\n"
           "\r\n"
           "<html><body><h1>405 Method Not Allowed</h1></body></html>");
}

int main()
{
    testStatusLines();
    testSharedHeaders();
    testBodies();
    testAllowHeader();
    testExactOutput();

    std::cout << "All tests passed!\n";
    return 0;
}
