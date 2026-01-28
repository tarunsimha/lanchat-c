#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#pragma comment(lib, "ws2_32.lib")

#define UDP_PORT 27016
#define PORT "27015"
#define BUFLEN 512
#define MAX_CLIENTS 32

typedef struct {
    SOCKET socket;
    char name[64];
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;
CRITICAL_SECTION client_lock;
char server_bind_ip[64] = {0};

void check_network_security()
{
    FILE *fp;
    char output[1024];
    int is_public = 0;

    printf("Checking Network Security Profile\n");

    fp = _popen(
        "powershell -Command \"Get-NetConnectionProfile | Select-Object -ExpandProperty NetworkCategory\"",
        "r"
    );

    if (fp == NULL) {
        printf("Could not check network profile.\n");
        return;
    }

    while (fgets(output, sizeof(output), fp) != NULL) {
        if (strstr(output, "Public") != NULL) {
            is_public = 1;
        }
    }

    _pclose(fp);

    if (is_public) {
        printf("Public network is detected, please change the network type to private in system settings\n");
    } else {
        printf("Network is Private. Discovery should work.\n");
    }
}

void broadcast_raw(const char* message, SOCKET sender_socket)
{
    SOCKET temp_sockets[MAX_CLIENTS];
    int count = 0;

    EnterCriticalSection(&client_lock);
    count = client_count;
    for (int i = 0; i < count; i++) {
        temp_sockets[i] = clients[i].socket;
    }
    LeaveCriticalSection(&client_lock);

    for (int i = 0; i < count; i++) {
        if (temp_sockets[i] != sender_socket) {
            send(temp_sockets[i], message, (int)strlen(message), 0);
        }
    }
}

DWORD WINAPI udp_thread(LPVOID arg)
{
    SOCKET UDPBroadcast = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    int broadcastEnable = 1;
    setsockopt(
        UDPBroadcast,
        SOL_SOCKET,
        SO_BROADCAST,
        (char*)&broadcastEnable,
        sizeof(broadcastEnable)
    );

    char broadcast_ip[64] = "255.255.255.255";

    if (strlen(server_bind_ip) > 0) {
        char temp_ip[64];
        strncpy(temp_ip, server_bind_ip, 63);

        char *last_dot = strrchr(temp_ip, '.');
        if (last_dot != NULL) {
            *last_dot = '\0';
            snprintf(broadcast_ip, sizeof(broadcast_ip), "%s.255", temp_ip);
            printf("Calculated Directed Broadcast IP: %s\n", broadcast_ip);
        }

        struct sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = 0;
        inet_pton(AF_INET, server_bind_ip, &bind_addr.sin_addr);

        if (bind(UDPBroadcast, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
            printf("UDP Bind failed on Server: %d\n", WSAGetLastError());
        }
    }

    struct sockaddr_in broadcastaddr;
    memset(&broadcastaddr, 0, sizeof(broadcastaddr));
    broadcastaddr.sin_family = AF_INET;
    broadcastaddr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, broadcast_ip, &broadcastaddr.sin_addr);

    printf("Broadcasting to %s on port %d...\n", broadcast_ip, UDP_PORT);

    const char* msg = "CHAT_SERVER:27015";

    while (true) {
        int sent = sendto(
            UDPBroadcast,
            msg,
            (int)strlen(msg),
            0,
            (struct sockaddr*)&broadcastaddr,
            sizeof(broadcastaddr)
        );

        if (sent == SOCKET_ERROR) {
            printf("UDP Broadcast failed: %d\n", WSAGetLastError());
        }

        Sleep(2000);
    }

    closesocket(UDPBroadcast);
    return 0;
}

void add_client(Client s)
{
    EnterCriticalSection(&client_lock);
    if (client_count < MAX_CLIENTS) {
        clients[client_count++] = s;
    }
    LeaveCriticalSection(&client_lock);
}

void remove_client(Client s)
{
    EnterCriticalSection(&client_lock);
    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket == s.socket) {
            clients[i] = clients[--client_count];
            break;
        }
    }
    LeaveCriticalSection(&client_lock);
}

DWORD WINAPI client_thread(LPVOID arg)
{
    Client client_obj;
    SOCKET client = (SOCKET)arg;
    client_obj.socket = client;

    char namebuf[64];
    int n = recv(client, namebuf, sizeof(namebuf) - 1, 0);
    if (n <= 0) {
        closesocket(client);
        return 0;
    }

    namebuf[n] = '\0';

    bool duplicate = false;
    EnterCriticalSection(&client_lock);
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].name, namebuf) == 0) {
            duplicate = true;
            break;
        }
    }
    LeaveCriticalSection(&client_lock);

    if (duplicate) {
        printf("Rejected duplicate name: %s\n", namebuf);
        send(client, "NO", 2, 0);
        closesocket(client);
        return 0;
    } else {
        send(client, "OK", 2, 0);
    }

    strncpy(client_obj.name, namebuf, sizeof(client_obj.name) - 1);
    client_obj.name[sizeof(client_obj.name) - 1] = '\0';

    add_client(client_obj);
    printf("%s connected.\n", client_obj.name);

    char joinmsg[128];
    snprintf(joinmsg, sizeof(joinmsg), "\n%s joined the chat.\n", client_obj.name);
    broadcast_raw(joinmsg, client);

    char buffer[BUFLEN];
    char formatted_msg[BUFLEN + 64];

    while (true) {
        memset(buffer, 0, BUFLEN);
        int n = recv(client, buffer, BUFLEN - 1, 0);
        if (n <= 0) {
            break;
        }

        buffer[n] = '\0';
        snprintf(formatted_msg, sizeof(formatted_msg), "%s: %s", client_obj.name, buffer);
        broadcast_raw(formatted_msg, client);
    }

    char leavemsg[128];
    snprintf(leavemsg, sizeof(leavemsg), "\n%s left the chat.\n", client_obj.name);
    broadcast_raw(leavemsg, client);

    printf("%s disconnected.\n", client_obj.name);
    remove_client(client_obj);
    closesocket(client);

    return 0;
}

int main(int argc, char *argv[])
{
    check_network_security();

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Failed to init winsock\n");
        return 1;
    }

    if (argc > 1) {
        strncpy(server_bind_ip, argv[1], 63);
        printf("Forcing UDP Broadcast via interface: %s\n", server_bind_ip);
    }

    InitializeCriticalSection(&client_lock);
    CreateThread(NULL, 0, udp_thread, NULL, 0, NULL);

    struct addrinfo hints;
    struct addrinfo* result = NULL;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &result) != 0) {
        printf("getaddrinfo failed: %d\n", WSAGetLastError());
        return 1;
    }

    SOCKET listenSock = socket(
        result->ai_family,
        result->ai_socktype,
        result->ai_protocol
    );

    if (bind(listenSock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        printf("Bind failed with error %d\n", WSAGetLastError());
        freeaddrinfo(result);
        closesocket(listenSock);
        return 1;
    }

    freeaddrinfo(result);

    if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
        printf("Listen failed with error %d\n", WSAGetLastError());
        closesocket(listenSock);
        return 1;
    }

    printf("Server listening on port %s\n", PORT);
    printf("Waiting for clients\n");

    while (true) {
        SOCKET client = accept(listenSock, NULL, NULL);
        if (client != INVALID_SOCKET) {
            CreateThread(NULL, 0, client_thread, (LPVOID)client, 0, NULL);
        } else {
            printf("Accept failed: %d\n", WSAGetLastError());
        }
    }

    closesocket(listenSock);
    WSACleanup();
    return 0;
}