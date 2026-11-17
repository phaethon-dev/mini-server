#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <ctime>

struct HttpRequest
{
    std::string method;                         // "GET"
    std::string path;                           // "/index.html"
    std::string version;                        // "HTTP/1.1"
    std::map<std::string, std::string> headers; // "host" -> "localhost:8080", ...
};

struct HttpResponse
{
    bool omitBody = false;
    int status = 200;
    std::string reason = "OK";
    std::map<std::string, std::string> headers;
    std::string body;

    std::string toString() const;
};

std::optional<HttpRequest> parseHttpRequest(const std::string &raw);

// Named wrappers, so calling code reads clearly
HttpResponse okResponse(const std::string &body, const std::string &contentType);
HttpResponse badRequest();       // 400
HttpResponse notFound();         // 404
HttpResponse methodNotAllowed(); // 405
std::optional<std::string> percentDecode(const std::string &input);
std::optional<std::filesystem::path> resolvePath(const std::filesystem::path &root, const std::string &urlPath);
std::optional<std::string> readFile(const std::filesystem::path &root); // returns the file's content if it exists, otherwise std::nullopt
bool responseKeepsAlive(const HttpResponse &response);
HttpResponse handleRequest(const std::string &raw, const std::string &documentRoot);
std::string formatHttpDate(std::time_t time);   // formatHttpDate(0) == "Thu, 01 Jan 1970 00:00:00 GMT"
// parseHttpDate("Thu, 01 Jan 1970 00:00:00 GMT") == 0
// parseHttpDate("garbage") == std::nullopt
std::optional<std::time_t> parseHttpDate(const std::string &text);

// General builders
HttpResponse makeResponse(int status, const std::string &reason,
                          const std::string &contentType, const std::string &body);
HttpResponse makeErrorResponse(int status, const std::string &reason);

// Returns the MIME type for a given file path, based on its extension.
std::string mimeTypeFor(const std::string &path);