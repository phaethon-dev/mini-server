#pragma once

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h> // socket, bind, listen, accept, send, recv, setsockopt
#include <netinet/in.h> // sockaddr_in, INADDR_LOOPBACK
#include <arpa/inet.h>  // htons, htonl
#include <unistd.h>     // close
#include <cerrno>
#include <sys/time.h>   // timeval, for socket timeouts
#endif
#include <string>
#include <optional>
#include <cstddef>
#include <chrono>

// The error code from the most recent failed socket call
inline int lastSocketError()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle INVALID_SOCKET_HANDLE = INVALID_SOCKET;
constexpr int SOCKET_ERROR_RESULT = SOCKET_ERROR;
constexpr int SEND_FLAGS = 0; 
#else
using SocketHandle = int;
constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;
constexpr int SOCKET_ERROR_RESULT = -1;
constexpr int SEND_FLAGS = MSG_NOSIGNAL; 
#endif

class WinsockSession
{
public:
    WinsockSession()
    {
#ifdef _WIN32
        initialized = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0); // Check if Winsock was initialized successfully
#else
        initialized = true;
#endif
    }
    ~WinsockSession()
    {
        if (initialized)
        {
#ifdef _WIN32
            WSACleanup();
#endif
        }
    }
    WinsockSession(const WinsockSession &) = delete;
    WinsockSession &operator=(const WinsockSession &) = delete; // Disable copy constructor and assignment operator
    bool isInitialized() const
    {
        return initialized;
    }

private:
#ifdef _WIN32
    WSADATA wsaData;
#endif
    bool initialized = false;
};

class Socket
{
public:
    Socket();                       // public: creates a new TCP socket by calling ::socket(...)
    explicit Socket(SocketHandle handle); // takes over an existing handle; accept() uses this
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete; // Disable copy constructor and assignment operator
    Socket(Socket &&other) noexcept;            // move constructor
    Socket &operator=(Socket &&other) noexcept; // move assignment operator
    ~Socket();

    bool isValid() const {return handle_ != INVALID_SOCKET_HANDLE;} // Returns true if the socket is valid (not INVALID_SOCKET_HANDLE)
    bool bind(int port);
    bool listen(int backlog = 5);
    Socket accept();
    bool setReceiveTimeout(int milliseconds);
    bool setSendTimeout(int milliseconds);
    bool sendAll(const std::string &data);
    std::optional<std::string> readRequest(std::size_t maxBytes = maximumBytes); // Default max size is set to maximumBytes
    void close();

private:
    SocketHandle handle_ = INVALID_SOCKET_HANDLE;                        // Invalid handle means "no socket"
    static constexpr std::size_t maximumBytes = 8192;           // Maximum bytes to read from a request, to prevent reading excessively large requests
    static constexpr std::chrono::seconds requestTimeLimit{10}; // Default receive timeout in seconds for the whole request, to prevent hanging indefinitely
    std::string pending_;
};