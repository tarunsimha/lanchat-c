#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdbool.h>

#pragma comment(lib, "ws2_32.lib")

#define UDP_PORT 27016
#define PORT "27015"
#define BUFLEN 512
#define MAX_CLIENTS 32

SOCKET clients[MAX_CLIENTS];
int client_count = 0;
CRITICAL_SECTION client_lock;

DWORD WINAPI udp_thread(LPVOID arg)
{
    int broadcastEnable = 1;
    SOCKET UDPBroadcast = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    setsockopt(UDPBroadcast, SOL_SOCKET, SO_BROADCAST, (char*) &broadcastEnable, sizeof(broadcastEnable));

    struct sockaddr_in broadcastaddr;
    memset(&broadcastaddr, 0, sizeof(broadcastaddr));
    broadcastaddr.sin_family = AF_INET;
    broadcastaddr.sin_port = htons(UDP_PORT);
    broadcastaddr.sin_addr.s_addr = INADDR_BROADCAST;

    printf("UDP DISCOVERY BROADCAST STARTED\n");

    const char* msg = "CHAT_SERVER:27015";
    while (true) {
        sendto(UDPBroadcast, msg, (int) strlen(msg), 0, (struct sockaddr*) &broadcastaddr, sizeof(broadcastaddr));
        Sleep(1000);
    }

    closesocket(UDPBroadcast);
    return 0;
}

void add_client(SOCKET s)
{
    EnterCriticalSection(&client_lock);
    if (client_count < MAX_CLIENTS) clients[client_count++] = s;
    LeaveCriticalSection(&client_lock);
}

void remove_client(SOCKET s)
{
    EnterCriticalSection(&client_lock);
    for (int i = 0; i < client_count; i++) {
        if (clients[i] == s) {
            clients[i] = clients[--client_count];
            break;
        }
    }
    LeaveCriticalSection(&client_lock);
}

void tcp_broadcast(SOCKET sender, char *buf, int len)
{
    EnterCriticalSection(&client_lock);
    for (int i = 0; i < client_count; i++) {
        if (clients[i] != sender) {
            send(clients[i], buf, len, 0);
        }
    }
    LeaveCriticalSection(&client_lock);
}

DWORD WINAPI client_thread(LPVOID arg)
{
    SOCKET client = (SOCKET) arg;
    char buffer[BUFLEN];

    add_client(client);
    printf("Client connected (%d clients)\n", client_count);

    while (true)
    {
        int n = recv(client, buffer, BUFLEN, 0);
        if (n <= 0) break;
        tcp_broadcast(client, buffer, n);
    }

    remove_client(client);
    closesocket(client);
    printf("Client disconnected\n");
    return 0;
}

int main()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    InitializeCriticalSection(&client_lock);
    CreateThread(NULL, 0, udp_thread, NULL, 0, NULL);

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
    printf("Server listening on port %s\n", PORT);

    while (true) {
        printf("Waiting for client\n");
        SOCKET client = accept(listenSock, NULL, NULL);

        if (client != INVALID_SOCKET) {
            CreateThread(NULL, 0, client_thread, (LPVOID) client, 0, NULL);
        }
    }

    closesocket(listenSock);
    WSACleanup();
    return 0;
}
