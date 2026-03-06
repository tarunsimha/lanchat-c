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
} client_info;

client_info clients[MAX_CLIENTS];
int client_count = 0;
CRITICAL_SECTION client_lock;
char server_bind_ip[64] = {0};

void check_network_security()
{
    FILE *fp;
    char output[1024];
    int is_public = 0;

    printf("[SYSTEM] Checking network security profile...\n");

    fp = _popen("powershell -Command \"Get-NetConnectionProfile | Select-Object -ExpandProperty NetworkCategory\"", "r");

    if (fp == NULL) {
        printf("[ERROR] Could not check network profile.\n");
        return;
    }

    while (fgets(output, sizeof(output), fp) != NULL) {
        if (strstr(output, "Public") != NULL) {
            is_public = 1;
        }
    }

    _pclose(fp);

    if (is_public) {
        printf("[WARN] Public network detected. Discovery may be blocked.\n");
        printf("       Please change the network type to Private in Windows settings.\n");
    } else {
        printf("[SUCCESS] Network is Private. Discovery active.\n");
    }
}

void broadcast_message(const char* message, SOCKET sender_socket)
{
    SOCKET temp_sockets[MAX_CLIENTS];
    int current_count = 0;

    EnterCriticalSection(&client_lock);
    current_count = client_count;
    for (int i = 0; i < current_count; i++) {
        temp_sockets[i] = clients[i].socket;
    }
    LeaveCriticalSection(&client_lock);

    for (int i = 0; i < current_count; i++) {
        if (temp_sockets[i] != sender_socket) {
            send(temp_sockets[i], message, (int)strlen(message), 0);
        }
    }
}

DWORD WINAPI udp_broadcast_thread(LPVOID arg)
{
    SOCKET udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int broadcast_enable = 1;
    setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast_enable, sizeof(broadcast_enable));

    char broadcast_ip[64] = "255.255.255.255";

    if (strlen(server_bind_ip) > 0) {
        char temp_ip[64];
        strncpy(temp_ip, server_bind_ip, 63);

        char *last_dot = strrchr(temp_ip, '.');
        if (last_dot != NULL) {
            *last_dot = '\0';
            snprintf(broadcast_ip, sizeof(broadcast_ip), "%s.255", temp_ip);
            printf("[UDP] Directed broadcast IP: %s\n", broadcast_ip);
        }

        struct sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = 0;
        inet_pton(AF_INET, server_bind_ip, &bind_addr.sin_addr);

        if (bind(udp_socket, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
            printf("[UDP] Bind failed: %d\n", WSAGetLastError());
        }
    }

    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, broadcast_ip, &broadcast_addr.sin_addr);

    printf("[UDP] Broadcasting on port %d...\n", UDP_PORT);

    const char* beacon_message = "CHAT_SERVER:27015";

    while (true) {
        int bytes_sent = sendto(udp_socket, beacon_message, (int)strlen(beacon_message), 0,
                               (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));

        if (bytes_sent == SOCKET_ERROR) {
            printf("[UDP] Broadcast error: %d\n", WSAGetLastError());
        }

        Sleep(2000);
    }

    closesocket(udp_socket);
    return 0;
}

DWORD WINAPI client_handler_thread(LPVOID arg)
{
    client_info current_client;
    SOCKET client_socket = (SOCKET)arg;
    current_client.socket = client_socket;

    DWORD recv_timeout = 600000;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&recv_timeout, sizeof(recv_timeout));

    char name_buffer[64];
    int bytes_received = recv(client_socket, name_buffer, sizeof(name_buffer) - 1, 0);
    if (bytes_received <= 0) {
        closesocket(client_socket);
        return 0;
    }

    name_buffer[bytes_received] = '\0';
    name_buffer[strcspn(name_buffer, "\r\n")] = '\0';

    if (strlen(name_buffer) == 0) {
        send(client_socket, "ERR_INVALID_NAME", 16, 0);
        closesocket(client_socket);
        return 0;
    }

    bool is_duplicate = false;
    bool is_full = false;

    EnterCriticalSection(&client_lock);
    if (client_count >= MAX_CLIENTS) {
        is_full = true;
    } else {
        for (int i = 0; i < client_count; i++) {
            if (strcmp(clients[i].name, name_buffer) == 0) {
                is_duplicate = true;
                break;
            }
        }
    }
    LeaveCriticalSection(&client_lock);

    if (is_full) {
        printf("[REJECT] Server full (%d clients)\n", MAX_CLIENTS);
        send(client_socket, "FULL", 4, 0);
        closesocket(client_socket);
        return 0;
    }

    if (is_duplicate) {
        printf("[REJECT] Duplicate name: %s\n", name_buffer);
        send(client_socket, "NO", 2, 0);
        closesocket(client_socket);
        return 0;
    } else {
        send(client_socket, "OK", 2, 0);
    }

    strncpy(current_client.name, name_buffer, sizeof(current_client.name) - 1);
    current_client.name[sizeof(current_client.name) - 1] = '\0';

    EnterCriticalSection(&client_lock);
    clients[client_count++] = current_client;
    LeaveCriticalSection(&client_lock);
    
    printf("[JOIN] %s connected. (Total: %d)\n", current_client.name, client_count);

    char system_message[128];
    snprintf(system_message, sizeof(system_message), "\n*** %s joined the room ***\n", current_client.name);
    broadcast_message(system_message, client_socket);

    char buffer[BUFLEN];
    char formatted_msg[BUFLEN + 128];

    while (true) {
        memset(buffer, 0, BUFLEN);
        int n = recv(client_socket, buffer, BUFLEN - 1, 0);
        if (n <= 0) break;

        buffer[n] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        if (strlen(buffer) == 0) continue;

        snprintf(formatted_msg, sizeof(formatted_msg), "[%s] %s", current_client.name, buffer);
        broadcast_message(formatted_msg, client_socket);
        printf("%s: %s\n", current_client.name, buffer);
    }

    snprintf(system_message, sizeof(system_message), "\n*** %s left the room ***\n", current_client.name);
    broadcast_message(system_message, client_socket);

    EnterCriticalSection(&client_lock);
    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket == client_socket) {
            clients[i] = clients[--client_count];
            break;
        }
    }
    LeaveCriticalSection(&client_lock);

    printf("[LEAVE] %s disconnected.\n", current_client.name);
    closesocket(client_socket);
    return 0;
}

int main(int argc, char *argv[])
{
    printf("=== LAN CHATROOM SERVER ===\n\n");
    check_network_security();

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("[FATAL] Winsock initialization failed.\n");
        return 1;
    }

    if (argc > 1) {
        strncpy(server_bind_ip, argv[1], 63);
        printf("[INFO] Forcing interface: %s\n", server_bind_ip);
    }

    InitializeCriticalSection(&client_lock);
    CreateThread(NULL, 0, udp_broadcast_thread, NULL, 0, NULL);

    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &result) != 0) {
        printf("[FATAL] getaddrinfo failed.\n");
        return 1;
    }

    SOCKET listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (bind(listen_socket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        printf("[FATAL] Bind failed: %d\n", WSAGetLastError());
        return 1;
    }

    freeaddrinfo(result);
    listen(listen_socket, SOMAXCONN);

    printf("[INFO] Server listening on port %s...\n", PORT);

    while (true) {
        SOCKET client_socket = accept(listen_socket, NULL, NULL);
        if (client_socket != INVALID_SOCKET) {
            CreateThread(NULL, 0, client_handler_thread, (LPVOID)client_socket, 0, NULL);
        }
    }

    closesocket(listen_socket);
    WSACleanup();
    return 0;
}