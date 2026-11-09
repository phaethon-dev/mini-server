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

struct HttpResponse
{
    int status = 200;
    std::string reason = "OK";
    std::map<std::string, std::string> headers;
    std::string body;

    std::string toString() const;
};

std::optional<HttpRequest> parseHttpRequest(const std::string &raw);
std::optional<HttpRequest> parseHttpResponse(const std::string &line);
