#include <cassert>
#include <ctime>
#include <iostream>
#include <optional>
#include <string>

#include "http.h"

// The folder the tests serve files from (see handler_test.cpp)
const std::string ROOT = "test_public";

// Builds a request with any method and runs it through the handler. Extra header
// lines must end in "\r\n", for example: send("GET", "/", "Connection: close\r\n")
HttpResponse send(const std::string &method, const std::string &path, const std::string &extraHeaders = "")
{
    return handleRequest(method + " " + path + " HTTP/1.1\r\nHost: localhost\r\n" + extraHeaders + "\r\n", ROOT);
}

// A GET carrying an If-Modified-Since header with the given date text
HttpResponse getIfModifiedSince(const std::string &path, const std::string &date)
{
    return send("GET", path, "If-Modified-Since: " + date + "\r\n");
}

// The value of a response header, or "" if it's missing
std::string headerOf(const HttpResponse &response, const std::string &name)
{
    auto it = response.headers.find(name);
    return it == response.headers.end() ? "" : it->second;
}

// True if the text ends exactly with the given suffix
bool endsWith(const std::string &text, const std::string &suffix)
{
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The Last-Modified date the server reports for a file, as a number
std::time_t serverTimeOf(const std::string &path)
{
    std::optional<std::time_t> time = parseHttpDate(headerOf(send("GET", path), "Last-Modified"));
    assert(time.has_value());
    return *time;
}

void testFormatHttpDate()
{
    assert(formatHttpDate(0) == "Thu, 01 Jan 1970 00:00:00 GMT");             // the Unix epoch
    assert(formatHttpDate(1794601290) == "Fri, 13 Nov 2026 20:21:30 GMT");    // GMT, not local time
    assert(formatHttpDate(951782400) == "Tue, 29 Feb 2000 00:00:00 GMT");     // a leap day
    assert(formatHttpDate(946684799) == "Fri, 31 Dec 1999 23:59:59 GMT");     // one second before 2000
    assert(formatHttpDate(1794601290).size() == 29);                         // always the same length
}

void testParseHttpDate()
{
    assert(parseHttpDate("Thu, 01 Jan 1970 00:00:00 GMT") == 0);
    assert(parseHttpDate("Fri, 13 Nov 2026 20:21:30 GMT") == 1794601290);
    assert(parseHttpDate("Tue, 29 Feb 2000 00:00:00 GMT") == 951782400);

    // Parsing undoes formatting, for any time
    for (std::time_t time : {std::time_t(0), std::time_t(1), std::time_t(951782400),
                             std::time_t(1794601290), std::time_t(2147483647), std::time_t(4102444800)})
    {
        assert(parseHttpDate(formatHttpDate(time)) == time);
    }
}

void testParseRejectsBadDates()
{
    assert(!parseHttpDate(""));
    assert(!parseHttpDate("garbage"));
    assert(!parseHttpDate("Fri, 13 Nov 2026 20:21:30"));      // no time zone
    assert(!parseHttpDate("Fri, 13 Nov 2026 20:21:30 UTC"));  // HTTP dates always say GMT
    assert(!parseHttpDate("Fri, 13 Nov 2026 20:21:30 GMTX")); // trailing junk
    assert(!parseHttpDate(" Fri, 13 Nov 2026 20:21:30 GMT")); // leading space
    assert(!parseHttpDate("Mon, 13 Nov 2026 20:21:30 GMT"));  // wrong weekday: the 13th is a Friday
    assert(!parseHttpDate("Fri, 3 Nov 2026 20:21:30 GMT"));   // day must be two digits
    assert(!parseHttpDate("Mon, 30 Feb 2026 00:00:00 GMT"));  // February has no 30th
    assert(!parseHttpDate("Fri, 13 Nov 2026 25:00:00 GMT"));  // no hour 25

    // Decision: the two obsolete formats from RFC 9110 are not accepted. An unreadable
    // date only means the file is sent in full, so this never breaks a request
    assert(!parseHttpDate("Friday, 13-Nov-26 20:21:30 GMT")); // RFC 850 format
    assert(!parseHttpDate("Fri Nov 13 20:21:30 2026"));       // C asctime() format
}

void testLastModifiedHeader()
{
    // Every file served gets a Last-Modified date the server itself can read back
    for (const std::string path : {"/", "/style.css", "/images/pixel.png", "/hello%20world.txt"})
    {
        HttpResponse response = send("GET", path);
        assert(response.status == 200);
        assert(parseHttpDate(headerOf(response, "Last-Modified")).has_value());
    }

    // HEAD reports the same date
    assert(headerOf(send("HEAD", "/"), "Last-Modified") == headerOf(send("GET", "/"), "Last-Modified"));

    // Errors aren't files, so they have no date
    assert(headerOf(send("GET", "/missing.html"), "Last-Modified") == "");
    assert(headerOf(send("POST", "/"), "Last-Modified") == "");
}

void testNotModified()
{
    HttpResponse full = send("GET", "/index.html");
    std::string date = headerOf(full, "Last-Modified");

    // Sending the date back means "I still have this version"
    HttpResponse cached = getIfModifiedSince("/index.html", date);
    assert(cached.status == 304);
    assert(cached.reason == "Not Modified");
    assert(cached.omitBody);

    // Same headers as the 200, including the Content-Length of the cached file...
    assert(cached.headers == full.headers);
    assert(cached.toString().find("Content-Length: " + std::to_string(full.body.size()) + "\r\n") != std::string::npos);

    // ...but nothing after the blank line
    assert(cached.toString().rfind("HTTP/1.1 304 Not Modified\r\n", 0) == 0);
    assert(endsWith(cached.toString(), "\r\n\r\n"));

    // A 304 is a success, so keep-alive rules apply as usual
    assert(headerOf(cached, "Connection") == "keep-alive");
    assert(headerOf(send("GET", "/index.html", "If-Modified-Since: " + date + "\r\nConnection: close\r\n"),
                    "Connection") == "close");
}

void testComparesDates()
{
    std::time_t fileTime = serverTimeOf("/style.css");

    // The browser's copy is older than the file: send the new version
    assert(getIfModifiedSince("/style.css", formatHttpDate(fileTime - 1)).status == 200);
    assert(getIfModifiedSince("/style.css", "Thu, 01 Jan 1970 00:00:00 GMT").status == 200);

    // Same time or later: not modified since then
    assert(getIfModifiedSince("/style.css", formatHttpDate(fileTime)).status == 304);
    assert(getIfModifiedSince("/style.css", formatHttpDate(fileTime + 1)).status == 304);
    assert(getIfModifiedSince("/style.css", formatHttpDate(fileTime + 86400)).status == 304);

    // A full 200 really does carry the body
    assert(getIfModifiedSince("/style.css", formatHttpDate(fileTime - 1)).body == "body { color: red; }");
}

void testUnreadableDatesAreIgnored()
{
    // RFC 9110: an invalid If-Modified-Since is ignored, and the file is sent in full
    for (const std::string date : {"garbage", "", "Friday, 13-Nov-26 20:21:30 GMT", "yesterday"})
    {
        HttpResponse response = getIfModifiedSince("/index.html", date);
        assert(response.status == 200);
        assert(!response.omitBody);
        assert(response.body == send("GET", "/index.html").body);
    }
}

void testOnlyWhereA200WouldBe()
{
    const std::string farFuture = "If-Modified-Since: Fri, 01 Jan 2100 00:00:00 GMT\r\n";

    // HEAD answers 304 the same way
    HttpResponse head = send("HEAD", "/index.html", farFuture);
    assert(head.status == 304);
    assert(head.omitBody);

    // Errors stay errors, whatever the header says
    assert(send("GET", "/missing.html", farFuture).status == 404);
    assert(send("GET", "/../test_secret.txt", farFuture).status == 404);
    assert(send("GET", "/%zz", farFuture).status == 400);
    assert(send("POST", "/index.html", farFuture).status == 405);
}

int main()
{
    testFormatHttpDate();
    testParseHttpDate();
    testParseRejectsBadDates();
    testLastModifiedHeader();
    testNotModified();
    testComparesDates();
    testUnreadableDatesAreIgnored();
    testOnlyWhereA200WouldBe();

    std::cout << "All tests passed!\n";
    return 0;
}
