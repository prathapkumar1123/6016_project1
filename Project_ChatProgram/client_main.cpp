#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <conio.h>
#include <future>

#include "Buffer.h"
#include "Message.h"

#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_PORT "8412"
#define LOCAL_HOST_ADDR "127.0.0.1"

int sendMessage(const std::string& msg, const std::string& name, MESSAGE_TYPE type, SOCKET socket) {
    ChatMessage message;
    message.message = msg;
    message.from = name;
    message.messageLength = msg.length();
    message.nameLength = name.length();
    message.header.messageType = type;
    message.header.packetSize = message.message.length() +
        message.from.length() +
        sizeof(message.messageLength) +
        sizeof(message.header.messageType) +
        sizeof(message.nameLength) +
        sizeof(message.header.packetSize);

    const int bufSize = 512;
    Buffer buffer(bufSize);

    // Write our packet to the buffer
    buffer.WriteUInt32LE(message.header.packetSize);
    buffer.WriteUInt32LE(message.header.messageType);
    buffer.WriteUInt32LE(message.messageLength);
    buffer.WriteUInt32LE(message.nameLength);
    buffer.WriteString(message.message);
    buffer.WriteString(message.from);

    int result = send(socket, reinterpret_cast<const char*>(buffer.m_BufferData.data()), message.header.packetSize, 0);
    return result;
}

void closeSocketConnection(SOCKET socket) {
    closesocket(socket);
    WSACleanup();
}

int joinRoom(std::string& name, std::string& selectedRoom, SOCKET& socket) {
    int selectRoomResult = sendMessage(selectedRoom, name, JOIN_ROOM, socket);
    if (selectRoomResult == SOCKET_ERROR) {
        int errorCode = WSAGetLastError();
        if (errorCode == WSAEWOULDBLOCK) {
            // No data available right now, continue the loop or do other work.
        }
        else if (errorCode == WSANOTINITIALISED) {
            // WSA not yet initialized. We
        }
        else {
            // Other errors here.
            // Close the socket if necessary.
            printf("\Join Room failed with error %d", WSAGetLastError());
            closeSocketConnection(socket);
            return 1;
        }
    }

    return 0;
}

void receiveMessages(SOCKET socket) {
    const int bufSize = 512;
    Buffer buffer(bufSize);

    int result = recv(socket, reinterpret_cast<char*>(buffer.m_BufferData.data()), bufSize, 0);
    if (result == SOCKET_ERROR) {
        int errorCode = WSAGetLastError();

        if (errorCode == WSAEWOULDBLOCK) {
            // No data available right now, continue the loop or do other work.
        }
        else if (errorCode == WSANOTINITIALISED) {
            // WSA not yet initialized.
        }
        else {
            // Handle other errors here.
            // Close the socket if necessary.
            printf("\nrecv failed with error %d", WSAGetLastError());
            closeSocketConnection(socket);
            return;
        }
    }

    if (result > 0) {
        uint32_t packetSize = buffer.ReadUInt32LE();
        uint32_t messageType = buffer.ReadUInt32LE();

        if (messageType == NOTIFICATION) {
            uint32_t messageLength = buffer.ReadUInt32LE();
            uint32_t nameLength = buffer.ReadUInt32LE();

            std::string msg = buffer.ReadString(messageLength);

            std::cout << "\r";
            std::cout << msg;
            std::cout.flush();

            std::cout << "\nYou: ";
        }
        else if (messageType == TEXT) {
            uint32_t messageLength = buffer.ReadUInt32LE();
            uint32_t nameLength = buffer.ReadUInt32LE();
            std::string msg = buffer.ReadString(messageLength);
            std::string name = buffer.ReadString(nameLength);

            std::cout << "\r";
            std::cout << name << ": " << msg;
            std::cout.flush();

            std::cout << "\nYou: ";
        }
    }
}

void sendLeaveMessage(std::string name, std::string roomname, SOCKET socket) {
    int result = sendMessage(roomname, name, LEAVE_ROOM, socket);
    if (result == SOCKET_ERROR) {
        printf("\nsend failed with error %d", WSAGetLastError());
        closeSocketConnection(socket);
        return;
    }
}

void printLine() {
    printf("\n-------------------------------------\n");
}

void displayRoomInfo() {
    printLine();
    printf("Existing Rooms: \n1. games\n2. study\n3. news");
    printLine();
}

int main() {

    // Initialize WinSock
    WSADATA wsaData;
    int result;
    bool isRunning = true;

    // Set version 2.2 with MAKEWORD(2,2)
    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        printf("\nWSAStartup failed with error %d", result);
        return 1;
    }

    struct addrinfo* info = nullptr;
    struct addrinfo hints;

    ZeroMemory(&hints, sizeof(hints));    // Ensure we don't have garbage data
    hints.ai_family = AF_INET;            // IPv4
    hints.ai_socktype = SOCK_STREAM;    // Stream
    hints.ai_protocol = IPPROTO_TCP;    // TCP
    hints.ai_flags = AI_PASSIVE;

    result = getaddrinfo(LOCAL_HOST_ADDR, DEFAULT_PORT, &hints, &info);
    if (result == SOCKET_ERROR) {
        printf("\ngetaddrinfo failed with error %d", result);
        WSACleanup();
        return 1;
    }

    // Socket
    SOCKET clientSocket = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (clientSocket == INVALID_SOCKET) {
        printf("\nsocket failed with error %d", WSAGetLastError());
        freeaddrinfo(info);
        WSACleanup();
        return 1;
    }

    // Connect
    result = connect(clientSocket, info->ai_addr, static_cast<int>(info->ai_addrlen));
    if (result == SOCKET_ERROR) { // Fixed the condition here
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
            // The connection is still in progress. You can continue checking.
        }
        else {
            // Handle other error codes and possibly close the socket.
            printf("\nconnect failed with error %d", WSAGetLastError());
            closeSocketConnection(clientSocket); // Use the correct function
            freeaddrinfo(info);
            WSACleanup();
            return 1;
        }
    }
    printf("\nConnected to the server successfully!");

    // Set Non-Blocking
    u_long mode = 1;
    result = ioctlsocket(clientSocket, FIONBIO, &mode);
    if (result == SOCKET_ERROR) {
        printf("listen failed with error %d\n", WSAGetLastError());
        closesocket(clientSocket);
        freeaddrinfo(info);
        WSACleanup();
        return 1;
    }

    displayRoomInfo();

    std::string name;
    std::string selectedRoom;

    std::cout << "\nEnter your name: ";
    std::getline(std::cin, name); // Use getline to handle spaces in the name

    std::cout << "Enter an existing room name or create a new room: ";
    std::getline(std::cin, selectedRoom);

    result = joinRoom(name, selectedRoom, clientSocket);
    if (result != 0) {
        printf("\nJoining Room Failed.\n");
    }

    printf("\n\n*** Type a message and press 'Enter' to send ***");
    printf("\n*** Type 'exit' to quit ***\n\n");

    // Create a separate thread for receiving messages
    std::thread receiveThread([&] {
        while (isRunning) {
            receiveMessages(clientSocket);
        }
    });

    while (isRunning) {

        std::string message;
        std::cout << "You: ";
        std::getline(std::cin, message);

        if (message == "exit") {
            sendLeaveMessage(name, selectedRoom, clientSocket);
            isRunning = false;
            break;
        }

        if (message.compare(0, 3, "\\LR") == 0) {
            std::string roomName = message.substr(3);
            std::cout << "Room Name: " << roomName << std::endl;

            sendLeaveMessage(name, roomName, clientSocket);
        }
        else if (!message.empty()) {
            int result = sendMessage(message, name, TEXT, clientSocket);
            if (result == SOCKET_ERROR) {
                printf("\nsend failed with error %d", WSAGetLastError());
                closeSocketConnection(clientSocket);
                break;
            }
        }
    }

    receiveThread.join();

    system("Pause");

    // Close
    closeSocketConnection(clientSocket);

    return 0;
}
