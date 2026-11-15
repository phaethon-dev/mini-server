#include <iostream>
#include "http.h"
#include "socket.h"

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
    if (!serverSocket.bind(8080))
    {
        std::cerr << "Failed to bind socket: " << WSAGetLastError() << std::endl;
        return 1;
    }
    if (!serverSocket.listen())
    {
        std::cerr << WSAGetLastError() << std::endl;
        return 1;
    }
    std::cout << "Server is listening on port 8080..." << std::endl;
    // Main server loop: accept connections and handle requests
    while (true)
    {
        Socket clientSocket = serverSocket.accept();
        if (!clientSocket.isValid())
        {
            std::cerr << "Failed to accept client connection: " << WSAGetLastError() << std::endl;
            continue;
        }
        if (!clientSocket.setReceiveTimeout(5000))
        {
            std::cerr << "Failed to set receive timeout: " << WSAGetLastError() << std::endl;
            clientSocket.close();
            continue;
        } // Set a 5-second timeout for receiving data
        if (!clientSocket.setSendTimeout(5000))
        {
            std::cerr << "Failed to set send timeout: " << WSAGetLastError() << std::endl;
            clientSocket.close();
            continue;
        } // Set a 5-second timeout for sending data
        auto requestOpt = clientSocket.readRequest();
        if (!requestOpt)
        {
            clientSocket.close();
            continue;
        }
        HttpResponse response = handleRequest(*requestOpt, "public");
        if (!clientSocket.sendAll(response.toString()))
        {
            std::cerr << "Failed to send response: " << WSAGetLastError() << std::endl;
        }
        clientSocket.close();
    }
    return 0;
}