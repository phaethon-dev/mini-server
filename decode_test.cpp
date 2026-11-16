#include <cassert>
#include <iostream>
#include <optional>
#include <string>

#include "http.h"

// The folder the tests serve files from (see handler_test.cpp)
const std::string ROOT = "test_public";

// The contents of test_secret.txt, which sits OUTSIDE test_public and must never be served
const std::string SECRET = "TOP-SECRET-DO-NOT-SERVE";

// Builds a GET request for a path and runs it through the handler
HttpResponse get(const std::string &path)
{
    return handleRequest("GET " + path + " HTTP/1.1\r\nHost: localhost\r\n\r\n", ROOT);
}

// True if a response refuses the request and doesn't leak the secret file
bool blocked(const HttpResponse &response)
{
    return response.status != 200 && response.body.find(SECRET) == std::string::npos;
}

// Shorthand: true if decoding fails
bool rejected(const std::string &input)
{
    return !percentDecode(input).has_value();
}

void testPlainText()
{
    // Nothing to decode: returned unchanged
    assert(percentDecode("") == "");
    assert(percentDecode("/") == "/");
    assert(percentDecode("/index.html") == "/index.html");
    assert(percentDecode("/docs/readme.txt") == "/docs/readme.txt");

    // '+' means space only in HTML form data, never in a URL path
    assert(percentDecode("/hello+world.txt") == "/hello+world.txt");
}

void testDecoding()
{
    assert(percentDecode("%20") == " ");
    assert(percentDecode("/hello%20world.txt") == "/hello world.txt");
    assert(percentDecode("%41%42%43") == "ABC");  // several in a row
    assert(percentDecode("a%41b") == "aAb");      // between ordinary characters
    assert(percentDecode("%7E") == "~");

    // Hex digits are case-insensitive
    assert(percentDecode("%4a") == "J");
    assert(percentDecode("%4A") == "J");
    assert(percentDecode("%2e%2E") == "..");

    // Bytes above 127 (UTF-8 for non-English letters) must survive the char conversion
    assert(percentDecode("%FF") == std::string("\xFF"));
    assert(percentDecode("caf%C3%A9") == "caf\xC3\xA9"); // "café" in UTF-8
}

void testDecodesOnlyOnce()
{
    // %25 is '%' itself
    assert(percentDecode("%25") == "%");

    // Decoding twice would turn %2520 into a space and %252e into '.'. Exactly once:
    assert(percentDecode("%2520") == "%20");
    assert(percentDecode("%252e%252e") == "%2e%2e");
}

void testMalformed()
{
    // '%' must be followed by exactly two hex digits
    assert(rejected("%"));            // nothing after it
    assert(rejected("/index.html%")); // at the very end
    assert(rejected("%4"));           // only one digit
    assert(rejected("abc%4"));        // only one digit, at the end
    assert(rejected("%zz"));          // not hex
    assert(rejected("%g0"));          // first digit not hex
    assert(rejected("%0g"));          // second digit not hex
    assert(rejected("% 20"));         // space is not a hex digit
    assert(rejected("%%20"));         // a bare '%' before a valid escape
}

void testForbiddenBytes()
{
    // Decision: an encoded NUL byte is rejected (it can cut a path short in some APIs)
    assert(rejected("%00"));
    assert(rejected("/style.css%00.png"));

    // Decision: an encoded slash is rejected (it hides a path separator from filters)
    assert(rejected("%2F"));
    assert(rejected("%2f"));
    assert(rejected("/docs%2Freadme.txt"));
}

void testHandlerDecodesPaths()
{
    // An encoded file name is served
    HttpResponse spaced = get("/hello%20world.txt");
    assert(spaced.status == 200);
    assert(spaced.body == "hello with a space");
    assert(spaced.headers.at("Content-Type") == "text/plain");

    // An encoded '.' is just a '.'
    assert(get("/index%2Ehtml").status == 200);
    assert(get("/index%2Ehtml").body == get("/index.html").body);

    // '+' is not a space, so this file doesn't exist
    assert(get("/hello+world.txt").status == 404);
}

void testHandlerRejectsBadEncoding()
{
    assert(get("/%zz").status == 400);
    assert(get("/index.html%").status == 400);
    assert(get("/index%2").status == 400);
    assert(get("/style.css%00.png").status == 400); // encoded NUL
    assert(get("/docs%2Freadme.txt").status == 400); // encoded slash
    assert(get("/docs%2freadme.txt").status == 400);
}

void testEncodedTraversal()
{
    // Encoded ".." must be caught after decoding
    assert(blocked(get("/%2e%2e/test_secret.txt")));
    assert(blocked(get("/%2E%2E/test_secret.txt")));
    assert(blocked(get("/.%2e/test_secret.txt")));
    assert(blocked(get("/docs/%2e%2e/%2e%2e/test_secret.txt")));

    // Encoded backslash and drive-letter colon
    assert(blocked(get("/%2e%2e%5Ctest_secret.txt")));
    assert(blocked(get("/C%3A/Windows/win.ini")));

    // Double-encoded: decoded once this is "%2e%2e", a folder name that doesn't exist
    assert(blocked(get("/%252e%252e/test_secret.txt")));

    // Same decision as plain traversal: 404, revealing nothing
    assert(get("/%2e%2e/test_secret.txt").status == 404);
}

void testQueryStrings()
{
    // The query is removed BEFORE decoding, so bad encoding in it doesn't matter
    assert(get("/index.html?x=%zz").status == 200);
    assert(get("/index.html?x=%2F").status == 200);

    // An encoded '?' is part of the file name, not the start of a query
    assert(get("/index.html%3Fx=1").status == 404);
}

int main()
{
    testPlainText();
    testDecoding();
    testDecodesOnlyOnce();
    testMalformed();
    testForbiddenBytes();
    testHandlerDecodesPaths();
    testHandlerRejectsBadEncoding();
    testEncodedTraversal();
    testQueryStrings();

    std::cout << "All tests passed!\n";
    return 0;
}
