#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdbool.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT "27015"
#define BUFLEN 512

int main()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    struct addrinfo hints;
    struct addrinfo* result = NULL;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;        // IPv4
    hints.ai_socktype = SOCK_STREAM;  // TCP
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    getaddrinfo(NULL, PORT, &hints, &result);

    SOCKET listenSock = socket(
        result->ai_family,
        result->ai_socktype,
        result->ai_protocol
    );

    bind(listenSock, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);

    listen(listenSock, SOMAXCONN);
    printf("Server listening on port %s...\n", PORT);

    while (true) {
        printf("Waiting for client\n");
        SOCKET client = accept(listenSock, NULL, NULL);
        if (client == INVALID_SOCKET) continue;

        printf("Client connected\n");

        char buffer[BUFLEN];

        while (true) {
            int received = recv(client, buffer, BUFLEN, 0);
            if (received <= 0) break;

            send(client, buffer, received, 0);
        }

        printf("Client disconnected\n");
        closesocket(client);
    }

    closesocket(listenSock);
    WSACleanup();
    return 0;
}
