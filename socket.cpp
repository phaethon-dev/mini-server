#include "socket.h"

// Move constructor, transfers ownership of the socket handle from another Socket
Socket::Socket(Socket &&other) noexcept : handle_(other.handle_)
{
    other.handle_ = INVALID_SOCKET; // the old object no longer owns it
}

Socket::Socket()
{
    handle_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

Socket::Socket(SOCKET handle) : handle_(handle)
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
        other.handle_ = INVALID_SOCKET;
    }
    return *this;
}

bool Socket::bind(int port)
{
    sockaddr_in address{};        // {} zeroes every field first
    address.sin_family = AF_INET; // IPv4
    address.sin_port = htons(static_cast<u_short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1

    return ::bind(handle_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != SOCKET_ERROR;
}

bool Socket::listen(int backlog)
{
    return ::listen(handle_, backlog) != SOCKET_ERROR; // backlog is the number of pending connections that can be queued
}

Socket Socket::accept()
{
    SOCKET client = ::accept(handle_, nullptr, nullptr); // Accept a new connection, returns a new socket handle for the client
    return Socket(client);
}

bool Socket::sendAll(const std::string &data)
{
    std::size_t sent = 0;
    while (sent < data.size())
    {
        int result = ::send(handle_, data.data() + sent,
                            static_cast<int>(data.size() - sent), 0);
        if (result == SOCKET_ERROR)
        {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

std::optional<std::string> Socket::readRequest(std::size_t maxBytes)
{
    std::string request;
    char buffer[1024];

    while (true)
    {
        int received = ::recv(handle_, buffer, sizeof(buffer), 0);
        if (received == SOCKET_ERROR || received == 0)
        {
            return std::nullopt;   // an error, or the client disconnected
        }
        request.append(buffer, static_cast<std::size_t>(received));

        if (request.find("\r\n\r\n") != std::string::npos)
        {
            return request;        // the blank line arrived: the headers are complete
        }
        if (request.size() >= maxBytes)
        {
            return std::nullopt;   // too big, and still no end of headers
        }
    }
}

void Socket::close()
{
    if (handle_ != INVALID_SOCKET)
    {
        ::closesocket(handle_);
        handle_ = INVALID_SOCKET;
    }
}