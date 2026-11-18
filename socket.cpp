#include "socket.h"

namespace
{
    // Sets a socket timeout (SO_RCVTIMEO or SO_SNDTIMEO) in each platform's own format
    bool setTimeout(SocketHandle handle, int option, int milliseconds)
    {
#ifdef _WIN32
        DWORD timeout = static_cast<DWORD>(milliseconds); // Windows: a number of milliseconds
#else
        timeval timeout{}; // Linux: seconds + microseconds
        timeout.tv_sec = milliseconds / 1000;
        timeout.tv_usec = (milliseconds % 1000) * 1000;
#endif
        return ::setsockopt(handle, SOL_SOCKET, option,
                            reinterpret_cast<const char *>(&timeout), sizeof(timeout)) != SOCKET_ERROR_RESULT;
    }
}

// Move constructor, transfers ownership of the socket handle from another Socket
Socket::Socket(Socket &&other) noexcept : handle_(other.handle_), pending_(std::move(other.pending_))
{
    other.handle_ = INVALID_SOCKET_HANDLE; // the old object no longer owns it
}

Socket::Socket()
{
    handle_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

Socket::Socket(SocketHandle handle) : handle_(handle)
{
}

Socket::~Socket()
{
    close();
}

Socket &Socket::operator=(Socket &&other) noexcept
{
    if (this != &other)
    {
        close();
        handle_ = other.handle_;
        other.handle_ = INVALID_SOCKET_HANDLE;
        pending_ = std::move(other.pending_);
    }
    return *this;
}

bool Socket::bind(int port)
{
#ifndef _WIN32
    int enable = 1;
    if (::setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == SOCKET_ERROR_RESULT)
    {
        return false;
    }
#endif
    sockaddr_in address{};        // {} zeroes every field first
    address.sin_family = AF_INET; // IPv4
    address.sin_port = htons(static_cast<u_short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1

    return ::bind(handle_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != SOCKET_ERROR_RESULT;
}

bool Socket::listen(int backlog)
{
    return ::listen(handle_, backlog) != SOCKET_ERROR_RESULT; // backlog is the number of pending connections that can be queued
}

Socket Socket::accept()
{
    SocketHandle client = ::accept(handle_, nullptr, nullptr); // Accept a new connection, returns a new socket handle for the client
    return Socket(client);
}

bool Socket::setReceiveTimeout(int milliseconds)
{
    return setTimeout(handle_, SO_RCVTIMEO, milliseconds);
}

bool Socket::setSendTimeout(int milliseconds)
{
    return setTimeout(handle_, SO_SNDTIMEO, milliseconds);
}

bool Socket::sendAll(const std::string &data)
{
    std::size_t sent = 0;
    while (sent < data.size())
    {
        int result = ::send(handle_, data.data() + sent,
                            static_cast<int>(data.size() - sent), SEND_FLAGS);
        if (result == SOCKET_ERROR_RESULT)
        {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

std::optional<std::string> Socket::readRequest(std::size_t maxBytes)
{
    std::string request = std::move(pending_);
    pending_.clear();
    char buffer[1024];
    auto start = std::chrono::steady_clock::now();
    while (true)
    {
        const std::size_t end = request.find("\r\n\r\n");
        if (end != std::string::npos)
        {
            pending_ = request.substr(end + 4);
            request = request.substr(0, end + 4);
            return request; // the blank line arrived: the headers are complete
        }
        int received = ::recv(handle_, buffer, sizeof(buffer), 0);
        if (received == SOCKET_ERROR_RESULT || received == 0)
        {
            return std::nullopt; // an error, or the client disconnected
        }
        request.append(buffer, static_cast<std::size_t>(received));
        if (request.size() >= maxBytes)
        {
            return std::nullopt; // too big, and still no end of headers
        }
        if (std::chrono::steady_clock::now() - start > requestTimeLimit)
        {
            return std::nullopt; // timeout after requestTimeLimit duration
        }
    }
}

void Socket::close()
{
    if (handle_ != INVALID_SOCKET_HANDLE)
    {
#ifdef _WIN32
        ::closesocket(handle_);
#else
        ::close(handle_);
#endif
        handle_ = INVALID_SOCKET_HANDLE;
    }
}