#include <cassert>
#include <iostream>
#include <string>

#include "http.h"

// The folder the tests serve files from. Paths are relative to the project folder,
// which is where `mingw32-make run` runs the tests.
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

void testRequestErrors()
{
    // These are decided before any file is looked at
    assert(handleRequest("POST / HTTP/1.1\r\n\r\n", ROOT).status == 405);
    assert(handleRequest("hello there", ROOT).status == 400);
    assert(handleRequest("", ROOT).status == 400);
}

void testServingFiles()
{
    // "/" serves index.html
    HttpResponse index = get("/");
    assert(index.status == 200);
    assert(index.body == "<html><body><h1>Test index</h1></body></html>");
    assert(index.headers.at("Content-Type") == "text/html");

    // A file by name, with its MIME type
    HttpResponse css = get("/style.css");
    assert(css.status == 200);
    assert(css.body == "body { color: red; }");
    assert(css.headers.at("Content-Type") == "text/css");

    // A file in a subfolder
    HttpResponse readme = get("/docs/readme.txt");
    assert(readme.status == 200);
    assert(readme.body == "hello from docs");
    assert(readme.headers.at("Content-Type") == "text/plain");

    // A binary file is served byte for byte, including NUL bytes
    HttpResponse image = get("/images/pixel.png");
    assert(image.status == 200);
    assert(image.body == std::string("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16));
    assert(image.headers.at("Content-Type") == "image/png");

    // A folder serves its own index.html
    HttpResponse blog = get("/blog/");
    assert(blog.status == 200);
    assert(blog.body == "<html><body><h1>Blog index</h1></body></html>");
    assert(blog.headers.at("Content-Type") == "text/html");
}

void testMissingFiles()
{
    assert(get("/missing.html").status == 404);
    assert(get("/docs/missing.txt").status == 404);
    assert(get("/nofolder/file.txt").status == 404);
}

void testPathTraversal()
{
    // Every one of these tries to reach test_secret.txt (or worse) outside the root.
    // Whatever status you choose for them, none may succeed or leak the secret.
    assert(blocked(get("/../test_secret.txt")));            // the classic
    assert(blocked(get("/docs/../../test_secret.txt")));    // climbing out from a subfolder
    assert(blocked(get("/./../test_secret.txt")));          // with a "." segment first
    assert(blocked(get("/..\\test_secret.txt")));           // Windows backslash separator
    assert(blocked(get("/docs\\..\\..\\test_secret.txt"))); // backslashes throughout
    assert(blocked(get("/..")));                            // the parent folder itself
    assert(blocked(get("/C:/Windows/win.ini")));            // a drive letter (absolute path)
    assert(blocked(get("//test_secret.txt")));              // a doubled slash
}

void testQueryStrings()
{
    // A query string: ignore everything after "?"
    assert(get("/style.css?v=2").status == 200);                 // query string ignored
    assert(get("/?page=1").body == get("/").body);               // the root, with a query
    assert(get("/missing.html?x=1").status == 404);              // still missing
    assert(blocked(get("/../test_secret.txt?x=1")));             // traversal still blocked

    assert(get("/index.html?file=../test_secret.txt").body.find(SECRET) == std::string::npos);
}

int main()
{
    testRequestErrors();
    testServingFiles();
    testMissingFiles();
    testPathTraversal();
    testQueryStrings();

    // What status does a traversal attempt get? (The tests above only require "not 200".)
    // assert(get("/../test_secret.txt").status == 400);   // Bad Request
    // assert(get("/../test_secret.txt").status == 403);   // Forbidden
    assert(get("/../test_secret.txt").status == 404); // Not Found: reveals nothing

    // A ".." that stays inside the root: reject every "..", or only ones that escape?
    // assert(get("/docs/../index.html").status == 200);  // allowed, it resolves to /index.html
    assert(get("/docs/../index.html").status != 200); // rejected, any ".." is suspicious

    // A folder: serve its index.html if it has one, otherwise 404?
    assert(get("/docs/").status == 404); // docs has no index.html

    std::cout << "All tests passed!\n";
    return 0;
}
