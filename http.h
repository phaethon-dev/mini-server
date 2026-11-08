#pragma once

#include <map>
#include <optional>
#include <string>

struct HttpRequest
{
    std::string method;                           // "GET"
    std::string path;                             // "/index.html"
    std::string version;                          // "HTTP/1.1"
    std::map<std::string, std::string> headers;   // "host" -> "localhost:8080", ...
};

std::optional<HttpRequest> parseHttpRequest(const std::string &raw);
