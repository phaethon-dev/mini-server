#include "http.h"
#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>
#include <cstddef>

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
    response.headers["Allow"] = "GET";
    return response;
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

HttpResponse makeErrorResponse(int status, const std::string &reason){  
    // Generates a simple HTML page for the error response
    std::string page = "<html><body><h1>" + std::to_string(status) + " " + reason + "</h1></body></html>";
    return makeResponse(status, reason, "text/html", page);
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
    result += body;   // Append the body
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