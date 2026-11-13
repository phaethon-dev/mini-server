#include <cassert>
#include <iostream>
#include <string>
#include "http.h"

int main()
{
    assert(handleRequest("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n").status == 200);
    assert(handleRequest("POST / HTTP/1.1\r\n\r\n").status == 405);
    assert(handleRequest("hello there").status == 400);
    assert(handleRequest("").status == 400);
    return 0;
}