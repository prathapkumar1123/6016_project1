#pragma once

// WinSock2 Windows Sockets
#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>

#include "Buffer.h"
#include "Message.h"

// Need to link Ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_PORT "8412"
#define LOCAL_HOST_ADDR "127.0.0.1"


WSADATA wsaData;
struct addrinfo* info = nullptr;
struct addrinfo hints;

// Define a data structure to represent a room
struct ChatRoom {
	std::string roomName; // Room name to represent a room
	std::vector<SOCKET> clients;  // List of clients in this room
};


// Clean up connections and addr info.
void cleanUp() {
	freeaddrinfo(info);
	WSACleanup();
}

// Handle errors, clean memory if needed.
void handleError(std::string scenario, bool freeMemoryOnError) {
	int errorCode = WSAGetLastError();
	if (errorCode == WSAEWOULDBLOCK) {
		// No data available right now, continue the loop or do other work.
	}
	else if (errorCode == WSANOTINITIALISED) {
		// WSA not yet initialized.
	}
	else {
		// Other errors here.
		std::cout << scenario << " failed. Error - " << errorCode << std::endl;

		// Close the socket if necessary.
		if (freeMemoryOnError) {
			cleanUp();
		}
	}
}


// Broadcast message to the other connections in the room, except the sender.
void BroadcastMessage(const std::string& msg, const std::string& name, MESSAGE_TYPE type, SOCKET senderSocket, const std::map<std::string, ChatRoom>& rooms) {
	for (const auto& room : rooms) {
		for (SOCKET clientSocket : room.second.clients) {

			auto it = std::find(room.second.clients.begin(), room.second.clients.end(), senderSocket);

			if (it == room.second.clients.end()) {
				// senderSocket is not in this room's clients
				continue; // Skip broadcasting to this room
			}

			if (clientSocket != senderSocket) {
				// Create a ChatMessage to send
				ChatMessage message;
				message.message = msg;
				message.from = name;
				message.messageLength = msg.length();
				message.nameLength = name.length();
				message.header.messageType = type;
				message.header.packetSize = message.message.length()
					+ sizeof(message.messageLength)
					+ sizeof(message.header.messageType)
					+ sizeof(message.from)
					+ sizeof(message.header.packetSize);

				const int bufSize = 512;
				Buffer buffer(bufSize);

				// Write our packet to the buffer
				buffer.WriteUInt32LE(message.header.packetSize);
				buffer.WriteUInt32LE(message.header.messageType);
				buffer.WriteUInt32LE(message.messageLength);
				buffer.WriteUInt32LE(message.nameLength);
				buffer.WriteString(message.message);
				buffer.WriteString(message.from);

				// Send the message to the client
				int result = send(clientSocket, (const char*)(&buffer.m_BufferData[0]), message.header.packetSize, 0);
				if (result == SOCKET_ERROR) {
					printf("Failed to broadcast message to client %d\n", WSAGetLastError());
					return;
				}
			}
		}
	}
}


// Create pre-defined rooms for users to enter  
void createRooms(std::map<std::string, ChatRoom>& rooms) {
	std::string gameroom = "games";
	std::string studyroom = "study";
	std::string newsroom = "news";

	ChatRoom room1;
	room1.roomName = gameroom;

	ChatRoom room2;
	room2.roomName = studyroom;

	ChatRoom room3;
	room3.roomName = newsroom;

	rooms[gameroom] = room1;
	rooms[studyroom] = room2;
	rooms[newsroom] = room3;

	printf("%s .... Room Created\n", gameroom.c_str());
	printf("%s .... Room Created\n", studyroom.c_str());
	printf("%s .... Room Created\n", newsroom.c_str());
}


// Print a horizontal line as a separator
void printLine() {
	printf("\n--------------------------------------\n");
}


// Server code execution begins
int main(int arg, char** argv) {
	printf("Initializing Server...\n\n");

	// Initialize WinSock
	int result;

	// Set version 2.2 with MAKEWORD(2,2)
	result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) {
		printf("WSAStartup failed with error %d\n", result);
		return 1;
	}
	printf("WSAStartup           --->  Success!\n");

	
	ZeroMemory(&hints, sizeof(hints));	// ensure we don't have garbage data 
	hints.ai_family = AF_INET;			// IPv4
	hints.ai_socktype = SOCK_STREAM;	// Stream
	hints.ai_protocol = IPPROTO_TCP;	// TCP
	hints.ai_flags = AI_PASSIVE;

	result = getaddrinfo(NULL, DEFAULT_PORT, &hints, &info);
	if (result != 0) {
		handleError("GetAddrInfo", true);
		return 1;
	}

	printf("Geting Address Info  --->  Success!\n");

	// Socket
	SOCKET listenSocket = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
	if (listenSocket == INVALID_SOCKET) {
		printf("socket failed with error %d\n", WSAGetLastError());
		freeaddrinfo(info);
		WSACleanup();
		return 1;
	}
	printf("Socket Created       --->  Success!\n");

	// Bind
	result = bind(listenSocket, info->ai_addr, (int)info->ai_addrlen);
	if (result == SOCKET_ERROR) {
		printf("Bind failed - Error %d\n", WSAGetLastError());
		closesocket(listenSocket);
		cleanUp();
		return 1;
	}

	// Listen
	result = listen(listenSocket, SOMAXCONN);
	if (result == SOCKET_ERROR) {
		printf("Listen failed - Error %d\n", WSAGetLastError());
		closesocket(listenSocket);
		cleanUp();
		return 1;
	}
	printf("Listening to socket  --->  Success!\n");


	// Creating rooms
	printLine();
	printf("\nCreating rooms... \n");

	std::map<std::string, ChatRoom> rooms;
	createRooms(rooms);
	printLine();


	// Initialize active sockets and sockets ready for reading
	FD_SET activeSockets;
	FD_SET socketsReadyForReading;

	FD_ZERO(&activeSockets);
	FD_ZERO(&socketsReadyForReading);

	// Use a timeval to prevent select from waiting forever.
	timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;

	while (true)
	{
		// Reset the socketsReadyForReading
		FD_ZERO(&socketsReadyForReading);
		FD_SET(listenSocket, &socketsReadyForReading);

		for (const auto& room : rooms) {
			for (int i = 0; i < room.second.clients.size(); i++) {
				FD_SET(room.second.clients[i], &socketsReadyForReading);
			}
		}

		int count = select(0, &socketsReadyForReading, NULL, NULL, &tv);
		if (count == 0) {
			continue;
		}

		if (count == SOCKET_ERROR) {
			handleError("Socket Select", false);
			continue;
		}

		for (auto& room : rooms) {

			for (int i = 0; i < room.second.clients.size(); i++) {

				SOCKET socket = room.second.clients[i];

				if (FD_ISSET(socket, &socketsReadyForReading))
				{
					const int bufSize = 512;
					Buffer buffer(bufSize);

					// Socket recv result checks
					// -1 : SOCKET_ERROR -- Get more info received from WSAGetLastError() after 
					//  0 : Client disconnected
					// >0 : The number of bytes received.
					int result = recv(socket, (char*)(&buffer.m_BufferData[0]), bufSize, 0);
					if (result == SOCKET_ERROR) {
						printf("11 recv failed with error %d\n", WSAGetLastError());

						auto it = std::find(room.second.clients.begin(), room.second.clients.end(), socket);

						// Check if the socket was found and then erase it
						if (it != room.second.clients.end()) {
							room.second.clients.erase(it);
						}

						FD_CLR(socket, &activeSockets);
						continue;
					}

					if (result == 0) {
						// Remove the disconnected client from the list
						closesocket(socket);

						FD_CLR(socket, &activeSockets);

						auto& clients = room.second.clients;

						// Find the iterator for the socket you want to remove
						auto it = std::find(room.second.clients.begin(), room.second.clients.end(), socket);

						// Check if the socket was found and then erase it
						if (it != room.second.clients.end()) {
							room.second.clients.erase(it);
						}

						closesocket(socket);

						i--; // Decrement the index since we removed an element

						continue;
					}

					if (result > 0) {
						// Get the data from buffer.
						uint32_t packetSize = buffer.ReadUInt32LE();
						uint32_t messageType = buffer.ReadUInt32LE();

						// Check data based on message type.
						if (messageType == NOTIFICATION) {
							uint32_t messageLength = buffer.ReadUInt32LE();
							uint32_t nameLength = buffer.ReadUInt32LE();

							std::string msg = buffer.ReadString(messageLength);

							printf(msg.c_str());
							printf("\n");
						}
						else if (messageType == TEXT) {
							// We know this is a ChatMessage
							uint32_t messageLength = buffer.ReadUInt32LE();
							uint32_t nameLength = buffer.ReadUInt32LE();
							std::string msg = buffer.ReadString(messageLength);
							std::string name = buffer.ReadString(nameLength);

							BroadcastMessage(msg, name, TEXT, socket, rooms);
						}
						else if (messageType == LEAVE_ROOM) {
							uint32_t messageLength = buffer.ReadUInt32LE();
							uint32_t nameLength = buffer.ReadUInt32LE();
							std::string roomName = buffer.ReadString(messageLength);
							std::string name = buffer.ReadString(nameLength);

							bool socketFoundInAnyRoom = false;

							// Broadcast a leave message to other clients in the room
							std::string leaveMessage = name + " has left the room.\n";
							printf(leaveMessage.c_str());

							BroadcastMessage(leaveMessage, name, NOTIFICATION, socket, rooms);

							// Iterate through all rooms in rooms
							for (auto& roomPair : rooms) {
								ChatRoom& chatroom = roomPair.second;
								// Check if the current room matches the roomName
								if (roomPair.first == roomName) {
									auto it = std::find(chatroom.clients.begin(), chatroom.clients.end(), socket);

									if (it != chatroom.clients.end()) {
										// Remove the socket from the current room
										chatroom.clients.erase(it);
										// Set a flag to indicate that the socket was found in the current room
										socketFoundInAnyRoom = true;
									}
								}
							}

							if (!socketFoundInAnyRoom) {
								// If the socket was not found in any room, close and clear it
								FD_CLR(socket, &socketsReadyForReading);
								closesocket(socket);  // Close the client socket
							}
						}
					}
				}
			}
		}

		if (count > 0) {

			if (FD_ISSET(listenSocket, &socketsReadyForReading)) {

				SOCKET newConnection = accept(listenSocket, NULL, NULL);

				if (newConnection == INVALID_SOCKET) {

					printf("accept failed with error: %d\n", WSAGetLastError());

				} else {

					std::string roomChoice;
					std::string selectedRoom;

					const int bufSize = 512;
					Buffer buffer(bufSize);

					int result = recv(newConnection, (char*)(&buffer.m_BufferData[0]), bufSize, 0);
					if (result == SOCKET_ERROR) {
						int errorCode = WSAGetLastError();
						if (errorCode == WSANOTINITIALISED) {

						}
						else if (errorCode == WSAEWOULDBLOCK) {

						}
						else {
							printf("recv failed with error %d\n", WSAGetLastError());
							closesocket(listenSocket);
							freeaddrinfo(info);
							WSACleanup();
							break;
						}
					}
					else {

						uint32_t packetSize = buffer.ReadUInt32LE();
						uint32_t messageType = buffer.ReadUInt32LE();

						if (messageType == JOIN_ROOM) {
							uint32_t messageLength = buffer.ReadUInt32LE();
							uint32_t nameLength = buffer.ReadUInt32LE();
							std::string msg = buffer.ReadString(messageLength);
							std::string name = buffer.ReadString(nameLength);

							selectedRoom = msg;

							std::string joinMessage = name + " has joined the room .\n";

							printf("%s has joined the room.\n", name.c_str());

							// Split selectedRoom into individual room names based on commas
							std::vector<std::string> roomNames;
							std::string roomName;

							std::istringstream ss(selectedRoom);

							while (std::getline(ss, name, ',')) {
								roomNames.push_back(name);
							}

							// Add the new connection to each room
							for (const std::string& room : roomNames) {
								if (rooms.find(room) != rooms.end()) {
									rooms[room].clients.push_back(newConnection);
								}
								else {
									// Room doesn't exist, create a new room
									ChatRoom newRoom;
									newRoom.roomName = room;
									rooms[room] = newRoom;
									rooms[room].clients.push_back(newConnection);
								}
							}


							BroadcastMessage(joinMessage, name, NOTIFICATION, newConnection, rooms);
						}
					}

					//activeConnections.push_back(newConnection);
					FD_SET(newConnection, &activeSockets);
					FD_CLR(listenSocket, &socketsReadyForReading);

					printf("Client connected with Socket: %d\n", (int) newConnection);
				}
			}
		}
	}

	system("Pause");

	// Cleanup resources and close socket connection.
	closesocket(listenSocket);
	cleanUp();

	return 0;
}