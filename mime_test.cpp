#include <cassert>
#include <iostream>
#include <string>

#include "http.h"

int main()
{
    assert(mimeTypeFor("/.html") == "application/octet-stream"); // hidden file, no extension
    assert(mimeTypeFor("/.hidden") == "application/octet-stream");
    assert(mimeTypeFor("/docs/.html") == "application/octet-stream"); // hidden file in a folder
    assert(mimeTypeFor("/.config.txt") == "text/plain");              // hidden file WITH an extension
    assert(mimeTypeFor("/script.js") == "text/javascript");           // current standard for JavaScript MIME type

    assert(mimeTypeFor("/index.html") == "text/html");
    assert(mimeTypeFor("/images/Photo.PNG") == "image/png");              // case-insensitive
    assert(mimeTypeFor("/archive.tar.gz") == "application/octet-stream"); // last dot wins
    assert(mimeTypeFor("/README") == "application/octet-stream");         // no extension
    assert(mimeTypeFor("/file.") == "application/octet-stream");          // dot with nothing after
    assert(mimeTypeFor("/folder.v2/file") == "application/octet-stream"); // dot in a folder name
    assert(mimeTypeFor("/folder.v2/file.txt") == "text/plain");

    HttpResponse ok = okResponse(std::string("a\0b", 3), "image/png");
    assert(ok.status == 200 && ok.reason == "OK");
    assert(ok.headers.at("Content-Type") == "image/png");
    assert(ok.body == std::string("a\0b", 3)); // binary body kept exactly

    std::cout << "All tests passed!\n";
    return 0;
}