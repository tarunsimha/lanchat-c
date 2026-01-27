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

#define PORT "27015"
#define BUFLEN 512

int main(int argc, char **argv)
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    struct addrinfo *result = NULL;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    getaddrinfo(argv[1], PORT, &hints, &result);

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

    char buffer[BUFLEN];

    while (true)
    {
        printf("> ");
        fgets(buffer, BUFLEN, stdin);

        send(ConnectSocket, buffer, (int) strlen(buffer), 0);

        int recieved = recv(ConnectSocket, buffer, BUFLEN - 1, 0);
        if (recieved <= 0) break;

        buffer[recieved] = '\0';
        printf("Server: %s", buffer);
    }

    closesocket(ConnectSocket);
    WSACleanup();
    return 0;
}