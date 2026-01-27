// When including windows.h don't include unnecessary stuff
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h> // Including Windows API
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// Only works for IDE based compiler linkers
#pragma comment(lib, "ws2_32.lib")
// For g++, use -lws2_32 compiler flag

#define UDP_PORT 27016
#define PORT 27015
#define BUFLEN 512

DWORD WINAPI recv_thread(LPVOID arg)
{
    SOCKET sock = (SOCKET)arg;
    char buf[BUFLEN];
    memset(buf, 0, BUFLEN);
    while (1) {
        int n = recv(sock, buf, BUFLEN - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        printf("\nMessage recieved: %s> ", buf);
    }
    return 0;
}

int main()
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET UDPSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    struct sockaddr_in listenaddr;
    struct sockaddr_in sender;
    memset(&listenaddr, 0, sizeof(listenaddr));

    listenaddr.sin_family = AF_INET;
    listenaddr.sin_port = htons(UDP_PORT);
    listenaddr.sin_addr.s_addr = INADDR_ANY;

    bind(UDPSocket, (struct sockaddr*) &listenaddr, sizeof(listenaddr));

    int senderlength = sizeof(sender);
    memset(&sender, 0, sizeof(sender));

    char udpbuffer[128];
    int recieved = recvfrom(UDPSocket, udpbuffer, sizeof(udpbuffer) - 1, 0, (struct sockaddr*) &sender, &senderlength);

    if (recieved <= 0) {
        printf("UDP recv failed: %d\n", WSAGetLastError());
        return 1;
    }

    udpbuffer[recieved] = '\0';
    printf("Discovered server message: %s\n", udpbuffer);

    char serverip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sender.sin_addr, serverip, sizeof(serverip));

    printf("Server ip: %s\n", serverip);

    closesocket(UDPSocket);

    /*
    struct addrinfo *result = NULL;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    getaddrinfo(serverip, PORT, &hints, &result);

    SOCKET ConnectSocket = INVALID_SOCKET;
    ConnectSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    
    int res = connect(ConnectSocket, result->ai_addr, (int) result->ai_addrlen);
    if (res == SOCKET_ERROR) {
        printf("Connection failed %d\n", res);
        printf("%d\n", WSAGetLastError());
        return 1;
    }
    printf("Connected successfully\n");
    freeaddrinfo(result);
    */

    SOCKET ConnectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, serverip, &server.sin_addr);

    connect(ConnectSocket, (struct sockaddr*) &server, sizeof(server));
    printf("Connected to chat\n");

    CreateThread(NULL, 0, recv_thread, (LPVOID) ConnectSocket, 0, NULL);

    char buffer[BUFLEN];

    while (true)
    {
        printf("> ");
        fgets(buffer, BUFLEN, stdin);

        send(ConnectSocket, buffer, (int) strlen(buffer), 0);
    }

    closesocket(ConnectSocket);
    WSACleanup();
    return 0;
}