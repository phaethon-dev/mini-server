#include <iostream>
#include <thread>
#include <atomic>
#include <stdexcept>
#include "http.h"
#include "socket.h"

static constexpr int PORT = 8080;
static constexpr int TIMEOUT = 5000;                   // Timeout in milliseconds
static constexpr int MAX_PENDING_CONNECTIONS = 5;      // Number of pending connections that can be queued
static constexpr int MAX_CLIENTS = 100;                // Maximum number of concurrent clients
static std::atomic<int> activeClients{0};              // Atomic counter to track the number of active clients
static constexpr const char *DOCUMENT_ROOT = "public"; // Root directory for serving static files

struct ClientCounter
{
    ~ClientCounter() { activeClients--; } // Decrement the active client count when a client is destroyed
};

void sendServerError(Socket &client)
{
    try
    {
        client.sendAll(makeErrorResponse(500, "Internal Server Error").toString());
    }
    catch (...)
    {
        std::cerr << "Failed to send error response to client." << std::endl;
    }
}

void handleClient(Socket client)
{
    ClientCounter clientCounter;
    try
    {
        // Drop clients that go silent
        if (!client.setReceiveTimeout(TIMEOUT))
        {
            std::cerr << "Failed to set receive timeout: " << WSAGetLastError() << std::endl;
            return;
        }
        // Drop clients that stop reading
        if (!client.setSendTimeout(TIMEOUT))
        {
            std::cerr << "Failed to set send timeout: " << WSAGetLastError() << std::endl;
            return;
        }
        auto requestOpt = client.readRequest();
        if (!requestOpt)
        {
            return; // Failed to read request or request was too large
        }
        HttpResponse response = handleRequest(*requestOpt, DOCUMENT_ROOT);
        if (!client.sendAll(response.toString()))
        {
            std::cerr << "Failed to send response: " << WSAGetLastError() << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception in client handler: " << e.what() << std::endl;
        sendServerError(client);
    }
    catch (...)
    {
        std::cerr << "Unknown exception in client handler." << std::endl;
        sendServerError(client);
    }
}

int main()
{
    WinsockSession session;
    if (!session.isInitialized())
    {
        std::cerr << "Failed to initialize Winsock: " << WSAGetLastError() << std::endl;
        return 1;
    }
    Socket serverSocket;
    if (!serverSocket.isValid())
    {
        std::cerr << "Failed to create socket: " << WSAGetLastError() << std::endl;
        return 1;
    }
    if (!serverSocket.bind(PORT))
    {
        std::cerr << "Failed to bind socket: " << WSAGetLastError() << std::endl;
        return 1;
    }
    if (!serverSocket.listen(MAX_PENDING_CONNECTIONS))
    {
        std::cerr << "Failed to listen: " << WSAGetLastError() << std::endl;
        return 1;
    }
    std::cout << "Server is listening on port " << PORT << "..." << std::endl;
    // Main server loop: accept connections and handle requests
    while (true)
    {
        Socket clientSocket = serverSocket.accept();
        if (!clientSocket.isValid())
        {
            std::cerr << "Failed to accept client connection: " << WSAGetLastError() << std::endl;
            continue;
        }
        if (activeClients.load() >= MAX_CLIENTS)
        {
            std::cerr << "Maximum number of clients reached." << std::endl;
            continue;
        }
        try
        {
            activeClients++;                                             // Increment the active client count before starting the thread
            std::thread(handleClient, std::move(clientSocket)).detach(); // Handle each client in a separate thread
        }
        catch (const std::system_error &e)
        {
            std::cerr << "Failed to create thread for client: " << e.what() << std::endl;
            activeClients--;
        }
    }
    return 0;
}