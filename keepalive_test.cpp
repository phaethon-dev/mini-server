#include <cassert>
#include <iostream>
#include <string>

#include "http.h"

// The folder the tests serve files from (see handler_test.cpp)
const std::string ROOT = "test_public";

// Builds a request from a request line plus optional extra header lines, and runs it
// through the handler. Each extra header must end in "\r\n", for example:
//     send("GET / HTTP/1.1", "Connection: close\r\n")
HttpResponse send(const std::string &requestLine, const std::string &extraHeaders = "")
{
    return handleRequest(requestLine + "\r\nHost: localhost\r\n" + extraHeaders + "\r\n", ROOT);
}

// The response's Connection header, or "" if it has none
std::string connectionOf(const HttpResponse &response)
{
    auto it = response.headers.find("Connection");
    return it == response.headers.end() ? "" : it->second;
}

void testStaysOpen()
{
    // HTTP/1.1 keeps the connection open by default
    assert(connectionOf(send("GET / HTTP/1.1")) == "keep-alive");
    assert(connectionOf(send("GET /style.css HTTP/1.1")) == "keep-alive");

    // Asking for keep-alive explicitly changes nothing
    assert(connectionOf(send("GET / HTTP/1.1", "Connection: keep-alive\r\n")) == "keep-alive");

    // A missing file is still a valid request, so there's no reason to close
    HttpResponse missing = send("GET /missing.html HTTP/1.1");
    assert(missing.status == 404);
    assert(connectionOf(missing) == "keep-alive");
}

void testClientAsksToClose()
{
    assert(connectionOf(send("GET / HTTP/1.1", "Connection: close\r\n")) == "close");

    // Connection values are case-insensitive
    assert(connectionOf(send("GET / HTTP/1.1", "Connection: Close\r\n")) == "close");
    assert(connectionOf(send("GET / HTTP/1.1", "Connection: CLOSE\r\n")) == "close");

    // The file is still served; only the connection closes afterwards
    assert(send("GET / HTTP/1.1", "Connection: close\r\n").status == 200);
}

void testHttp10Closes()
{
    // Decision: keep-alive is only supported for HTTP/1.1
    assert(connectionOf(send("GET / HTTP/1.0")) == "close");
    assert(connectionOf(send("GET / HTTP/1.0", "Connection: keep-alive\r\n")) == "close");

    // Still served normally
    assert(send("GET / HTTP/1.0").status == 200);
}

void testBadRequestsClose()
{
    // If a request can't be parsed, we can't know where it ends, so nothing after it is trusted
    HttpResponse garbage = handleRequest("hello there", ROOT);
    assert(garbage.status == 400);
    assert(connectionOf(garbage) == "close");

    // Parsed, but rejected later in handleRequest: bad percent-encoding
    HttpResponse badEncoding = send("GET /%zz HTTP/1.1");
    assert(badEncoding.status == 400);
    assert(connectionOf(badEncoding) == "close");
}

void testRequestBodiesClose()
{
    // The server never reads request bodies. If it kept the connection open, the
    // unread body would be parsed as the next request (request smuggling territory)
    assert(connectionOf(send("GET / HTTP/1.1", "Content-Length: 5\r\n")) == "close");
    assert(connectionOf(send("GET / HTTP/1.1", "Transfer-Encoding: chunked\r\n")) == "close");

    // Header names are case-insensitive too
    assert(connectionOf(send("GET / HTTP/1.1", "content-length: 5\r\n")) == "close");

    // A POST with a body is rejected AND closed
    HttpResponse post = send("POST / HTTP/1.1", "Content-Length: 5\r\n");
    assert(post.status == 405);
    assert(connectionOf(post) == "close");
}

void testEveryResponseSaysWhatHappens()
{
    // The client must always be told: every response has a Connection header
    assert(connectionOf(send("GET / HTTP/1.1")) != "");
    assert(connectionOf(send("GET /missing.html HTTP/1.1")) != "");
    assert(connectionOf(send("GET / HTTP/1.0")) != "");
    assert(connectionOf(send("POST / HTTP/1.1")) != "");
    assert(connectionOf(handleRequest("hello there", ROOT)) != "");
}

int main()
{
    testStaysOpen();
    testClientAsksToClose();
    testHttp10Closes();
    testBadRequestsClose();
    testRequestBodiesClose();
    testEveryResponseSaysWhatHappens();

    // Decision: a POST with NO body headers. It's rejected with 405, but there's no body
    // left in the socket, so keeping the connection open is safe. Pick one:
    assert(connectionOf(send("POST / HTTP/1.1")) == "keep-alive"); // valid request, nothing unread
    // assert(connectionOf(send("POST / HTTP/1.1")) == "close");   // close on every error except 404

    std::cout << "All tests passed!\n";
    return 0;
}
