#pragma once

#include <winsock2.h>
#include <string>
#include <optional>
#include <cstddef>
#include <chrono>

class WinsockSession
{
public:
    WinsockSession()
    {
        initialized = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0); // Check if Winsock was initialized successfully
    }
    ~WinsockSession()
    {
        if(initialized)
        {
            WSACleanup();
        }
    }
    WinsockSession(const WinsockSession &) = delete;
    WinsockSession &operator=(const WinsockSession &) = delete; // Disable copy constructor and assignment operator
    bool isInitialized() const
    {
        return initialized;
    }

private:
    WSADATA wsaData;
    bool initialized = false;
};

class Socket
{
public:
    Socket();                       // public: creates a new TCP socket by calling ::socket(...)
    explicit Socket(SOCKET handle); // takes over an existing handle; accept() uses this
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete; // Disable copy constructor and assignment operator
    Socket(Socket &&other) noexcept;            // move constructor
    Socket &operator=(Socket &&other) noexcept; // move assignment operator
    ~Socket();

    bool isValid() const { return handle_ != INVALID_SOCKET; } // Returns true if the socket is valid (not INVALID_SOCKET)
    bool bind(int port);
    bool listen(int backlog = 5);
    Socket accept();
    bool setReceiveTimeout(DWORD ms);
    bool setSendTimeout(DWORD ms);
    bool sendAll(const std::string &data);
    std::optional<std::string> readRequest(std::size_t maxBytes = maximumBytes); // Default max size is set to maximumBytes
    void close();

private:
    SOCKET handle_ = INVALID_SOCKET;                  // INVALID_SOCKET means "no socket"
    static constexpr std::size_t maximumBytes = 8192; // Maximum bytes to read from a request, to prevent reading excessively large requests
    static constexpr std::chrono::seconds requestTimeLimit{10}; // Default receive timeout in seconds for the whole request, to prevent hanging indefinitely
    std::string pending_;
};