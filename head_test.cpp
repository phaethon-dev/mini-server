#include <cassert>
#include <iostream>
#include <string>

#include "http.h"

// The folder the tests serve files from (see handler_test.cpp)
const std::string ROOT = "test_public";

// Builds a request with any method and runs it through the handler. Extra header
// lines must end in "\r\n", for example: send("HEAD", "/", "Connection: close\r\n")
HttpResponse send(const std::string &method, const std::string &path,
                  const std::string &extraHeaders = "", const std::string &version = "HTTP/1.1")
{
    return handleRequest(method + " " + path + " " + version + "\r\nHost: localhost\r\n" +
                             extraHeaders + "\r\n",
                         ROOT);
}

// True if the text ends exactly with the given suffix
bool endsWith(const std::string &text, const std::string &suffix)
{
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void testToStringWithoutBody()
{
    HttpResponse response;
    response.headers["Content-Type"] = "text/plain";
    response.body = "hello";
    response.omitBody = true;

    // Content-Length still describes the body that GET would send...
    // ...but nothing comes after the blank line
    assert(response.toString() ==
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/plain\r\n"
           "Content-Length: 5\r\n"
           "\r\n");

    // The body itself is untouched, only left out of the output
    assert(response.body == "hello");
}

void testOmitBodyIsOffByDefault()
{
    HttpResponse response;
    response.body = "hello";
    assert(!response.omitBody);
    assert(endsWith(response.toString(), "\r\n\r\nhello"));

    // Responses built by the helpers send their bodies too
    assert(!okResponse("x", "text/plain").omitBody);
    assert(!notFound().omitBody);
}

void testHeadMatchesGet()
{
    HttpResponse get = send("GET", "/");
    HttpResponse head = send("HEAD", "/");

    // Same status and exactly the same headers...
    assert(head.status == 200);
    assert(head.status == get.status);
    assert(head.headers == get.headers);

    // ...the body is known (so Content-Length is right), just not sent
    assert(head.omitBody);
    assert(!get.omitBody);
    assert(head.body == get.body);

    // On the wire: identical up to the blank line, and nothing after it
    std::string getText = get.toString();
    std::string headText = head.toString();
    assert(endsWith(headText, "\r\n\r\n"));
    assert(getText == headText + get.body);
}

void testHeadOnOtherFiles()
{
    // A binary file: Content-Length counts all 16 bytes, NULs included
    HttpResponse image = send("HEAD", "/images/pixel.png");
    assert(image.status == 200);
    assert(image.headers.at("Content-Type") == "image/png");
    assert(image.toString().find("Content-Length: 16\r\n") != std::string::npos);
    assert(endsWith(image.toString(), "\r\n\r\n"));

    // Percent-decoding works the same as for GET
    HttpResponse spaced = send("HEAD", "/hello%20world.txt");
    assert(spaced.status == 200);
    assert(spaced.omitBody);
}

void testHeadErrorsHaveNoBody()
{
    // A missing file: the 404's headers, but no error page
    HttpResponse missing = send("HEAD", "/missing.html");
    assert(missing.status == 404);
    assert(missing.omitBody);
    assert(endsWith(missing.toString(), "\r\n\r\n"));

    // Traversal is blocked for HEAD exactly as for GET
    HttpResponse traversal = send("HEAD", "/../test_secret.txt");
    assert(traversal.status == 404);
    assert(traversal.omitBody);

    // Bad encoding: still a 400, still no body
    HttpResponse badEncoding = send("HEAD", "/%zz");
    assert(badEncoding.status == 400);
    assert(badEncoding.omitBody);
}

void testKeepAliveRulesStillApply()
{
    assert(send("HEAD", "/").headers.at("Connection") == "keep-alive");
    assert(send("HEAD", "/", "Connection: close\r\n").headers.at("Connection") == "close");
    assert(send("HEAD", "/", "", "HTTP/1.0").headers.at("Connection") == "close");
}

void testOtherMethodsStillRejected()
{
    // GET and HEAD are the supported methods, and the 405 says so
    for (const std::string method : {"POST", "PUT", "DELETE", "OPTIONS"})
    {
        HttpResponse response = send(method, "/");
        assert(response.status == 405);
        assert(response.headers.at("Allow") == "GET, HEAD");
        assert(!response.omitBody); // only HEAD drops the body
    }

    // Methods are case-sensitive: "head" is not HEAD
    assert(send("head", "/").status != 200);
}

int main()
{
    testToStringWithoutBody();
    testOmitBodyIsOffByDefault();
    testHeadMatchesGet();
    testHeadOnOtherFiles();
    testHeadErrorsHaveNoBody();
    testKeepAliveRulesStillApply();
    testOtherMethodsStillRejected();

    std::cout << "All tests passed!\n";
    return 0;
}
