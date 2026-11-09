#include <cassert>
#include <iostream>
#include <string>

#include "http.h"

// Counts how many times needle appears in text
int countOf(const std::string &text, const std::string &needle)
{
    int count = 0;
    for (std::size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + 1))
    {
        ++count;
    }
    return count;
}

void testExactOutput()
{
    // A default response: 200 OK, no headers, empty body
    HttpResponse empty;
    assert(empty.toString() ==
           "HTTP/1.1 200 OK\r\n"
           "Content-Length: 0\r\n"
           "\r\n");

    // A typical error response, byte for byte
    HttpResponse notFound;
    notFound.status = 404;
    notFound.reason = "Not Found";
    notFound.headers["Content-Type"] = "text/plain";
    notFound.body = "missing";
    assert(notFound.toString() ==
           "HTTP/1.1 404 Not Found\r\n"
           "Content-Type: text/plain\r\n"
           "Content-Length: 7\r\n"
           "\r\n"
           "missing");

    // Several headers come out in the map's (alphabetical) order, before Content-Length
    HttpResponse several;
    several.headers["Content-Type"] = "text/html";
    several.headers["Connection"] = "close";
    several.body = "<p>hi</p>";
    assert(several.toString() ==
           "HTTP/1.1 200 OK\r\n"
           "Connection: close\r\n"
           "Content-Type: text/html\r\n"
           "Content-Length: 9\r\n"
           "\r\n"
           "<p>hi</p>");
}

void testContentLength()
{
    // Counts bytes, not characters: "é" is 2 bytes in UTF-8
    HttpResponse utf8;
    utf8.body = "caf\xC3\xA9";
    assert(utf8.toString().find("Content-Length: 5\r\n") != std::string::npos);

    // Every byte counts, including a NUL in the middle of binary data
    HttpResponse binary;
    binary.body = std::string("a\0b", 3);
    std::string text = binary.toString();
    assert(text.find("Content-Length: 3\r\n") != std::string::npos);
    assert(text.substr(text.size() - 3) == std::string("a\0b", 3)); // body sent unchanged

    // A Content-Length set by the caller is ignored, whatever its case:
    // there must be exactly one, with the calculated value
    HttpResponse manual;
    manual.headers["Content-Length"] = "999";
    manual.body = "hi";
    assert(countOf(manual.toString(), "Content-Length") == 1);
    assert(manual.toString().find("Content-Length: 2\r\n") != std::string::npos);

    HttpResponse lowercase;
    lowercase.headers["content-length"] = "999";
    lowercase.body = "hi";
    assert(countOf(lowercase.toString(), "ength: ") == 1);
    assert(lowercase.toString().find("999") == std::string::npos);
}

void testHeaders()
{
    // A header with an empty value is still written
    HttpResponse emptyValue;
    emptyValue.headers["X-Empty"] = "";
    assert(emptyValue.toString().find("X-Empty: \r\n") != std::string::npos);

    // Headers end with exactly one blank line, and nothing follows an empty body
    HttpResponse r;
    r.headers["Connection"] = "close";
    std::string text = r.toString();
    assert(text.size() >= 4 && text.substr(text.size() - 4) == "\r\n\r\n");
    assert(countOf(text, "\r\n\r\n") == 1);
}

int main()
{
    testExactOutput();
    testContentLength();
    testHeaders();

    std::cout << "All tests passed!\n";
    return 0;
}
