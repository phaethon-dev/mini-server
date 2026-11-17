#include "http.h"
#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h> // _wstat64 and struct _stat64
#include <ctime>      // std::time_t
#include <iomanip>

namespace fs = std::filesystem;

namespace
{
    const std::string OPTIONAL_WHITESPACE = " \t";

    // Helper functions for trimming whitespace from strings
    std::string leftTrim(const std::string &text)
    {
        std::size_t start = text.find_first_not_of(OPTIONAL_WHITESPACE);
        return (start == std::string::npos) ? "" : text.substr(start);
    }

    std::string rightTrim(const std::string &text)
    {
        std::size_t end = text.find_last_not_of(OPTIONAL_WHITESPACE);
        return (end == std::string::npos) ? "" : text.substr(0, end + 1);
    }

    std::string trim(const std::string &text)
    {
        return leftTrim(rightTrim(text));
    }

    int hexCharToInt(char c)
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        return -1;
    }

    // Helper function to convert a string to lowercase
    std::string toLowerCase(const std::string &text)
    {
        std::string result = text;
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c)
                       { return std::tolower(c); });
        return result;
    }

    // Splits the input text into lines based on "\r\n" delimiter
    std::vector<std::string> splitLines(const std::string &text)
    {
        std::vector<std::string> lines;
        std::size_t start = 0;
        std::size_t end = text.find("\r\n");
        // Loop to find each line and add it to the vector
        while (end != std::string::npos)
        {
            // Extract the line and add it to the vector
            lines.push_back(text.substr(start, end - start));
            start = end + 2; // Move past the "\r\n"
            end = text.find("\r\n", start);
        }
        // Add the last line if there's any remaining text
        if (start < text.size())
        {
            lines.push_back(text.substr(start));
        }
        return lines;
    }

    bool isValidMethod(const std::string &method)
    {
        // Define HTTP methods that are considered valid according to the HTTP standard
        static const std::vector<std::string> validMethods = {
            "GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS", "TRACE", "CONNECT", "PATCH"};
        return std::find(validMethods.begin(), validMethods.end(), method) != validMethods.end();
    }

    bool isValidPath(const std::string &path)
    {
        // Path must start with '/' and contain only valid URL characters
        if (path.empty() || path[0] != '/')
        {
            return false; // Path must start with '/'
        }
        for (char c : path)
        {
            unsigned char code = static_cast<unsigned char>(c);
            if (code < 33 || code > 126) // Only visible ASCII; anything else must be %-encoded
            {
                return false;
            }
        }
        return true;
    }

    bool isValidHeaderName(const std::string &name)
    {
        if (name.empty())
        {
            return false; // Header name can't be empty
        }
        for (char c : name)
        {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-')
            {
                return false; // Header name can only contain alphanumeric characters and dashes
            }
        }
        return true;
    }

    std::optional<std::pair<std::string, std::string>> parseHeaderLine(const std::string &line)
    {
        std::size_t colonPos = line.find(':');
        if (colonPos == std::string::npos)
        {
            return std::nullopt; // No colon found, invalid header line
        }
        std::string name = line.substr(0, colonPos);
        if (!isValidHeaderName(name))
        {
            return std::nullopt; // Invalid header name
        }
        std::string value = trim(line.substr(colonPos + 1));
        if (value.find('\r') != std::string::npos ||
            value.find('\n') != std::string::npos ||
            value.find('\0') != std::string::npos)
        {
            return std::nullopt; // Header value must not contain CR or LF or null characters
        }
        return std::make_pair(toLowerCase(name), value);
    }

    bool isValidHttpVersion(const std::string &version)
    {
        if (version.size() != 8 || version.substr(0, 5) != "HTTP/")
        {
            return false; // Must be exactly 8 characters and start with "HTTP/"
        }
        if (!std::isdigit(static_cast<unsigned char>(version[5])) ||
            version[6] != '.' ||
            !std::isdigit(static_cast<unsigned char>(version[7])))
        {
            return false; // Must be in the format "HTTP/x.y" where x and y are digits
        }
        return true;
    }

    // Parses the request line of an HTTP request and returns an HttpRequest object if valid
    std::optional<HttpRequest> parseRequestLine(const std::string &line)
    {
        auto first = line.find(' ');
        if (first == std::string::npos)
        {
            return std::nullopt; // No spaces found, invalid request line
        }
        auto second = line.find(' ', first + 1);
        if (second == std::string::npos)
        {
            return std::nullopt; // Only one space found, invalid request line
        }
        if (line.find(' ', second + 1) != std::string::npos)
        {
            return std::nullopt; // More than two spaces found, invalid request line
        }
        HttpRequest request;
        request.method = line.substr(0, first);
        if (!isValidMethod(request.method))
        {
            return std::nullopt; // Invalid HTTP method
        }
        request.path = line.substr(first + 1, second - first - 1);
        if (!isValidPath(request.path))
        {
            return std::nullopt; // Invalid path
        }
        request.version = line.substr(second + 1);
        if (!isValidHttpVersion(request.version))
        {
            return std::nullopt; // Version must be in the format "HTTP/x.y"
        }
        return request;
    }

    // Returns when the file was last changed, in seconds since 1 Jan 1970 (GMT)
    std::optional<std::time_t> lastModified(const fs::path &path)
    {
        struct _stat64 info;                    // an empty box for the file's details
        if (_wstat64(path.c_str(), &info) != 0) // ask Windows to fill it in; 0 means success
        {
            return std::nullopt; // file missing or unreadable
        }
        return info.st_mtime; // the "last modified" slot
    }

    HttpResponse buildResponse(const HttpRequest &request, const std::string &documentRoot)
    {
        if (request.method != "GET" && request.method != "HEAD")
        {
            return methodNotAllowed(); // Only GET method is allowed
        }
        // Everything from '?' onward is a query string, not part of the file name
        std::string path = request.path.substr(0, request.path.find('?'));
        auto decodedPath = percentDecode(path);
        if (!decodedPath)
        {
            return badRequest(); // Invalid percent encoding
        }
        auto resolvedPath = resolvePath(documentRoot, *decodedPath);
        if (!resolvedPath)
        {
            return notFound(); // File not found or path traversal attempt
        }
        auto fileContent = readFile(*resolvedPath);
        if (!fileContent)
        {
            return notFound(); // File could not be read
        }
        HttpResponse response = okResponse(*fileContent, mimeTypeFor(resolvedPath->string()));
        const auto modifiedTime = lastModified(*resolvedPath);
        if (modifiedTime)
        {
            response.headers["Last-Modified"] = formatHttpDate(*modifiedTime);
        }
        // Conditional GET: has the file changed since the browser's cached copy?
        const auto ifModifiedSince = request.headers.find("if-modified-since");
        if (modifiedTime && ifModifiedSince != request.headers.end())
        {
            const auto cachedTime = parseHttpDate(ifModifiedSince->second);
            if (cachedTime && *modifiedTime <= *cachedTime)
            {
                response.status = 304;
                response.reason = "Not Modified";
                response.omitBody = true;
            }
        }
        return response;
    }

    bool clientAllowsKeepAlive(const HttpRequest &request)
    {
        if (request.version != "HTTP/1.1")
        {
            return false;
        }
        for (const auto &header : request.headers)
        {
            if (toLowerCase(header.first) == "content-length")
            {
                return false;
            }
            if (toLowerCase(header.first) == "transfer-encoding")
            {
                return false;
            }
            if (toLowerCase(header.first) == "connection" && toLowerCase(header.second) == "close")
            {
                return false;
            }
        }
        return true;
    }
}

HttpResponse okResponse(const std::string &body, const std::string &contentType)
{
    return makeResponse(200, "OK", contentType, body);
}

HttpResponse badRequest()
{
    return makeErrorResponse(400, "Bad Request");
}

HttpResponse notFound()
{
    return makeErrorResponse(404, "Not Found");
}

HttpResponse methodNotAllowed()
{
    HttpResponse response = makeErrorResponse(405, "Method Not Allowed");
    response.headers["Allow"] = "GET, HEAD";
    return response;
}

std::optional<std::string> percentDecode(const std::string &input)
{
    std::string decoded;
    decoded.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] == '%' && i + 2 < input.size())
        {
            const int high = hexCharToInt(input[i + 1]);
            const int low = hexCharToInt(input[i + 2]);
            if (high == -1 || low == -1)
            {
                return std::nullopt;
            }
            const int value = (high << 4) | low;
            if (value == 0 || value == '/')
            {
                return std::nullopt; // an encoded NUL could cut the path short, and an encoded slash hides a separator
            }
            decoded.push_back(static_cast<char>(value));
            i += 2;
        }
        else if (input[i] == '%')
        {
            return std::nullopt;
        }
        else
        {
            decoded.push_back(input[i]);
        }
    }
    return decoded;
}

std::optional<std::filesystem::path> resolvePath(const std::filesystem::path &root, const std::string &urlPath)
{
    if (urlPath.find("..") != std::string::npos || urlPath.find('\\') != std::string::npos ||
        urlPath.find(':') != std::string::npos || urlPath.find("//") != std::string::npos)
    {
        return std::nullopt; // Layer 1: reject "..", backslashes, drive letters, and "//" outright.
    }
    std::filesystem::path requestedPath = root / urlPath.substr(1); // Remove leading '/' from urlPath
    std::error_code ec;
    std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(requestedPath, ec);
    if (ec)
    {
        return std::nullopt; // Error occurred while resolving the path
    }
    std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec)
    {
        return std::nullopt; // Error occurred while resolving the path
    }
    std::filesystem::path relative = canonicalPath.lexically_relative(canonicalRoot);
    if (relative.empty() || *relative.begin() == "..")
    {
        return std::nullopt; // The resolved path is outside the root directory
    }
    if (std::filesystem::is_directory(canonicalPath))
    {
        canonicalPath /= "index.html"; // a folder means its index page
    }
    if (!std::filesystem::exists(canonicalPath))
    {
        return std::nullopt; // The resolved path does not exist
    }
    if (!std::filesystem::is_regular_file(canonicalPath))
    {
        return std::nullopt; // The resolved path is not a regular file
    }
    return canonicalPath;
}

std::optional<std::string> readFile(const std::filesystem::path &root)
{
    std::ifstream file(root, std::ios::binary);
    if (!file)
    {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad())
    {
        return std::nullopt;
    }

    return buffer.str();
}

bool responseKeepsAlive(const HttpResponse &response)
{
    const auto connection = response.headers.find("Connection");
    return connection != response.headers.end() && connection->second == "keep-alive";
}

HttpResponse handleRequest(const std::string &raw, const std::string &documentRoot)
{
    auto request = parseHttpRequest(raw);
    if (!request)
    {
        return badRequest(); // Invalid request format
    }
    HttpResponse response = buildResponse(*request, documentRoot);
    if (request->method == "HEAD")
    {
        response.omitBody = true;
    }
    if (clientAllowsKeepAlive(*request) && response.status != 400)
    {
        response.headers["Connection"] = "keep-alive";
    }
    else
    {
        response.headers["Connection"] = "close";
    }
    return response;
}

std::string formatHttpDate(std::time_t time)
{
    std::tm parts{};
    if (gmtime_s(&parts, &time) != 0)
    {
        return "";
    }
    char buffer[64];
    //%a is the day of the week, %d is the numerical day, %b is the month, %Y is the year, and %H:%M:%S is the 24 hour time
    std::size_t length = std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &parts);
    return std::string(buffer, length);
}

std::optional<std::time_t> parseHttpDate(const std::string &text)
{
    std::istringstream in(text);
    std::tm parts{};
    in >> std::get_time(&parts, "%a, %d %b %Y %H:%M:%S GMT");
    if (in.fail())
    {
        return std::nullopt;
    }
    std::time_t time = _mkgmtime(&parts);
    if (time == -1)
    {
        return std::nullopt;
    }
    if (formatHttpDate(time) != text)
    {
        return std::nullopt;
    }
    return time;
}

HttpResponse makeResponse(int status, const std::string &reason,
                          const std::string &contentType, const std::string &body)
{
    HttpResponse response;
    response.status = status;
    response.reason = reason;
    response.headers["Content-Type"] = contentType;
    // The "Connection: close" header is added to indicate that the server will close the connection after sending the response
    response.headers["Connection"] = "close";
    response.body = body;
    return response;
}

HttpResponse makeErrorResponse(int status, const std::string &reason)
{
    // Generates a simple HTML page for the error response
    std::string page = "<html><body><h1>" + std::to_string(status) + " " + reason + "</h1></body></html>";
    return makeResponse(status, reason, "text/html", page);
}

std::string mimeTypeFor(const std::string &path)
{
    // Find the last dot in the path to determine the file extension
    std::size_t dotPos = path.find_last_of('.');
    if (dotPos == std::string::npos || dotPos == path.size() - 1)
    {
        return "application/octet-stream"; // No extension found, default to binary stream
    }
    if (path.find('/', dotPos) != std::string::npos)
    {
        return "application/octet-stream"; // Dot is part of a directory name, not an extension
    }
    if (dotPos == 0 || path[dotPos - 1] == '/')
    {
        return "application/octet-stream"; // Dot is part of a hidden file in a directory, not an extension
    }
    std::string extension = path.substr(dotPos + 1);
    extension = toLowerCase(extension); // Convert extension to lowercase for case-insensitive comparison
    // Map of common file extensions to their corresponding MIME types
    static const std::map<std::string, std::string> mimeTypes = {
        {"html", "text/html"},
        {"htm", "text/html"},
        {"css", "text/css"},
        {"js", "text/javascript"},
        {"json", "application/json"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"txt", "text/plain"},
        {"pdf", "application/pdf"},
        // Add more mappings as needed
    };
    auto mime = mimeTypes.find(extension);
    if (mime != mimeTypes.end())
    {
        return mime->second; // Return the corresponding MIME type if found
    }
    return "application/octet-stream"; // Default MIME type for unknown extensions
}

std::string HttpResponse::toString() const
{
    std::string result = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
    for (const auto &header : headers)
    {
        if (toLowerCase(header.first) == "content-length")
        {
            continue; // toString always writes its own, calculated Content-Length
        }

        result += header.first + ": " + header.second + "\r\n";
    }
    result += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    result += "\r\n"; // End of headers
    if (!omitBody)
    {
        result += body; // Append the body
    }
    return result;
}

std::optional<HttpRequest> parseHttpRequest(const std::string &raw)
{
    // Split the raw HTTP request into lines
    std::vector<std::string> lines = splitLines(raw);
    if (lines.empty())
    {
        return std::nullopt; // No lines found, invalid request
    }

    // Parse the request line (the first line of the HTTP request)
    auto request = parseRequestLine(lines[0]);
    if (!request)
    {
        return std::nullopt;
    }

    bool foundBlankLine = false;
    // Parse headers
    for (std::size_t i = 1; i < lines.size(); ++i)
    {
        const std::string &line = lines[i];
        if (line.empty())
        {
            foundBlankLine = true;
            break; // Empty line indicates end of headers
        }
        auto header = parseHeaderLine(line);
        if (!header)
        {
            return std::nullopt; // Invalid header line
        }
        if (request->headers.count(header->first))
        {
            return std::nullopt; // Duplicate header name found
        }
        request->headers[header->first] = header->second;
    }
    if (!foundBlankLine)
    {
        return std::nullopt; // No blank line found, invalid request
    }
    return request;
}