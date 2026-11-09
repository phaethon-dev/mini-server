#include <cassert>
#include <iostream>
#include <string>

#include "http.h"

// Builds a request with one header line, so header cases are one line each
std::string withHeader(const std::string &headerLine)
{
    return "GET / HTTP/1.1\r\n" + headerLine + "\r\n\r\n";
}

// Builds a request from just a request line, with no headers
std::string withRequestLine(const std::string &requestLine)
{
    return requestLine + "\r\n\r\n";
}

// The value parsed for one header, for tests that expect the request to be accepted
std::string headerValue(const std::string &headerLine, const std::string &name)
{
    auto request = parseHttpRequest(withHeader(headerLine));
    assert(request);
    return request->headers.at(name);
}

void testValidRequests()
{
    // A complete request: every field is filled in correctly
    auto r = parseHttpRequest(
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "User-Agent: curl/8.0\r\n"
        "Accept: */*\r\n"
        "\r\n");
    assert(r);
    assert(r->method == "GET");
    assert(r->path == "/index.html");
    assert(r->version == "HTTP/1.1");
    assert(r->headers.size() == 3);
    assert(r->headers.at("host") == "localhost:8080");
    assert(r->headers.at("user-agent") == "curl/8.0");
    assert(r->headers.at("accept") == "*/*");

    // No headers at all
    auto noHeaders = parseHttpRequest(withRequestLine("GET / HTTP/1.1"));
    assert(noHeaders);
    assert(noHeaders->path == "/");
    assert(noHeaders->headers.empty());

    // Parsing stops at the blank line, so a body is not treated as headers
    auto withBody = parseHttpRequest("GET / HTTP/1.1\r\nHost: x\r\n\r\nnot a header line");
    assert(withBody);
    assert(withBody->headers.size() == 1);
}

void testRequestLine()
{
    // Accepted: any well-formed method and version. Whether the server supports
    // them is decided later, not by the parser.
    assert(parseHttpRequest(withRequestLine("POST /submit HTTP/1.1")));
    assert(parseHttpRequest(withRequestLine("HEAD / HTTP/1.0")));
    assert(parseHttpRequest(withRequestLine("GET / HTTP/2.0")));

    // Rejected: wrong number of parts (exactly three, separated by single spaces)
    assert(!parseHttpRequest(""));                                       // empty input
    assert(!parseHttpRequest("\r\n\r\n"));                               // empty request line
    assert(!parseHttpRequest(withRequestLine("GET")));                   // 1 part
    assert(!parseHttpRequest(withRequestLine("GET /")));                 // 2 parts
    assert(!parseHttpRequest(withRequestLine("GET / HTTP/1.1 extra")));  // 4 parts
    assert(!parseHttpRequest(withRequestLine("GET\t/\tHTTP/1.1")));      // tabs are not separators

    // Rejected: empty method or bad path
    assert(!parseHttpRequest(withRequestLine(" / HTTP/1.1")));             // empty method
    assert(!parseHttpRequest(withRequestLine("GET index.html HTTP/1.1"))); // path must start with /

    // Method: uppercase letters only (every standard method is), and case-sensitive
    assert(parseHttpRequest(withRequestLine("DELETE / HTTP/1.1")));
    assert(!parseHttpRequest(withRequestLine("get / HTTP/1.1")));  // lowercase
    assert(!parseHttpRequest(withRequestLine("Get / HTTP/1.1")));  // mixed case
    assert(!parseHttpRequest(withRequestLine("G@T / HTTP/1.1")));  // symbol
    assert(!parseHttpRequest(withRequestLine("GET1 / HTTP/1.1"))); // digit

    // Path: visible ASCII characters only, codes 33 ('!') to 126 ('~').
    // Control characters here would become part of a filename in Milestone 4.
    assert(parseHttpRequest(withRequestLine("GET /a-b_c.d~e/f?x=1&y=%20 HTTP/1.1"))); // ordinary URL symbols
    assert(!parseHttpRequest(withRequestLine("GET /a\tb HTTP/1.1")));                   // tab
    assert(!parseHttpRequest(withRequestLine("GET /a\rb HTTP/1.1")));                   // carriage return
    assert(!parseHttpRequest(withRequestLine(std::string("GET /a\0b HTTP/1.1", 17))));  // NUL (the length keeps the \0)
    assert(!parseHttpRequest(withRequestLine("GET /a\x7F" "b HTTP/1.1")));              // DEL (code 127)
    assert(!parseHttpRequest(withRequestLine("GET /caf\xC3\xA9 HTTP/1.1")));            // non-ASCII: must be %-encoded

    // Rejected: version must match HTTP/<digit>.<digit> exactly (RFC 9112)
    assert(!parseHttpRequest(withRequestLine("GET / FTP/1.0")));    // wrong protocol
    assert(!parseHttpRequest(withRequestLine("GET / http/1.1")));   // case-sensitive
    assert(!parseHttpRequest(withRequestLine("GET / HTTP/")));      // no version number
    assert(!parseHttpRequest(withRequestLine("GET / HTTP/1")));     // missing ".digit"
    assert(!parseHttpRequest(withRequestLine("GET / HTTP/1.")));    // missing last digit
    assert(!parseHttpRequest(withRequestLine("GET / HTTP/11")));    // no dot
    assert(!parseHttpRequest(withRequestLine("GET / HTTP/abc")));   // not digits
    assert(!parseHttpRequest(withRequestLine("GET / HTTP/1.1.1"))); // too long
}

void testHeaderNames()
{
    // Accepted: names are stored in lowercase, and may contain digits and dashes
    auto upper = parseHttpRequest(withHeader("HOST: example"));
    assert(upper);
    assert(upper->headers.count("host") == 1);
    assert(upper->headers.count("HOST") == 0);
    assert(parseHttpRequest(withHeader("X-Request-Id-2: abc")));

    // Rejected: only letters, digits, and '-' are allowed, with no surrounding whitespace
    assert(!parseHttpRequest(withHeader("Host localhost")));    // no colon
    assert(!parseHttpRequest(withHeader(": localhost")));       // empty name
    assert(!parseHttpRequest(withHeader("Host : localhost")));  // space before the colon
    assert(!parseHttpRequest(withHeader("Host\t: localhost"))); // tab before the colon
    assert(!parseHttpRequest(withHeader(" Host: localhost")));  // leading space (obsolete line folding)
    assert(!parseHttpRequest(withHeader("Ho st: localhost")));  // space inside the name
    assert(!parseHttpRequest(withHeader("Host@: localhost")));  // symbol not allowed
}

void testHeaderValues()
{
    // Accepted: optional whitespace (spaces and tabs) around the value is removed
    assert(headerValue("Host:   localhost   ", "host") == "localhost");
    assert(headerValue("Host:\t localhost \t", "host") == "localhost");
    assert(headerValue("Host:localhost", "host") == "localhost");

    // Accepted: only the FIRST colon splits, so a colon inside the value is kept
    assert(headerValue("Host: localhost:8080", "host") == "localhost:8080");

    // Accepted: an empty value
    assert(headerValue("X-Empty:", "x-empty") == "");

    // Rejected: control characters in the value (a stray CR enables request smuggling)
    assert(!parseHttpRequest(withHeader("Host: x\r")));
}

void testRequestStructure()
{
    // Rejected: lines must end in \r\n, not a bare \n
    assert(!parseHttpRequest("GET / HTTP/1.1\nHost: x\n\n"));

    // Rejected: the headers must end with a blank line (an incomplete request)
    assert(!parseHttpRequest("GET / HTTP/1.1\r\nHost: x"));

    // Rejected: one bad header makes the whole request invalid
    assert(!parseHttpRequest("GET / HTTP/1.1\r\nHost: x\r\nBad Header: y\r\n\r\n"));

    // Rejected: duplicate header names, including ones that differ only in case.
    // Deliberately stricter than RFC 9110, which allows most headers to repeat.
    assert(!parseHttpRequest(withHeader("X-A: 1\r\nX-A: 2")));
    assert(!parseHttpRequest(withHeader("Host: a\r\nhost: b")));
}

int main()
{
    testValidRequests();
    testRequestLine();
    testHeaderNames();
    testHeaderValues();
    testRequestStructure();

    std::cout << "All tests passed!\n";
    return 0;
}
