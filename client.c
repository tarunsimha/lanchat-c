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
#define PORT 27015
#define BUFLEN 512

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
        printf("[SUCCESS] Network is Private. Ready for discovery.\n");
    }
}

bool is_connected = true;

DWORD WINAPI receive_thread(LPVOID arg)
{
    SOCKET socket = (SOCKET)arg;
    char buffer[BUFLEN];
    
    while (is_connected) {
        memset(buffer, 0, BUFLEN);
        int bytes_received = recv(socket, buffer, BUFLEN - 1, 0);
        if (bytes_received <= 0) {
            if (is_connected) {
                printf("\n[DISCONNECT] Connection lost to server.\n");
                is_connected = false;
            }
            break;
        }
        buffer[bytes_received] = '\0';
        
        printf("\r%s\n[You] > ", buffer); 
        fflush(stdout);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    printf("=== LAN CHATROOM CLIENT ===\n\n");
    check_network_security();

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("[FATAL] Winsock initialization failed.\n");
        return 1;
    }

    char server_ip[INET_ADDRSTRLEN];
    memset(server_ip, 0, INET_ADDRSTRLEN);
    bool ip_found = false;

    if (argc > 1) {
        strncpy(server_ip, argv[1], INET_ADDRSTRLEN - 1);
        printf("[INFO] Using server IP from argument: %s\n", server_ip);
        ip_found = true;
    } else {
        printf("[UDP] Looking for server via broadcast...\n");

        SOCKET udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udp_socket == INVALID_SOCKET) {
             printf("[ERROR] UDP socket creation failed: %d\n", WSAGetLastError());
        } else {
            int reuse = 1;
            setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

            DWORD timeout = 3000;
            setsockopt(udp_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

            struct sockaddr_in listen_addr;
            struct sockaddr_in sender_addr;
            memset(&listen_addr, 0, sizeof(listen_addr));

            listen_addr.sin_family = AF_INET;
            listen_addr.sin_port = htons(UDP_PORT);
            listen_addr.sin_addr.s_addr = INADDR_ANY;

            if (bind(udp_socket, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) == SOCKET_ERROR) {
                 printf("[ERROR] UDP bind failed: %d\n", WSAGetLastError());
            } else {
                 char udp_buffer[128];
                 int sender_length = sizeof(sender_addr);
                 printf("[UDP] Listening for server signal...\n");
                 
                 int bytes_received = recvfrom(udp_socket, udp_buffer, sizeof(udp_buffer) - 1, 0,
                                              (struct sockaddr*)&sender_addr, &sender_length);

                 if (bytes_received > 0) {
                     udp_buffer[bytes_received] = '\0';
                     if (strstr(udp_buffer, "CHAT_SERVER") != NULL) {
                        printf("[UDP] Discovered server: %s\n", udp_buffer);
                        inet_ntop(AF_INET, &sender_addr.sin_addr, server_ip, sizeof(server_ip));
                        printf("[UDP] Server IP: %s\n", server_ip);
                        ip_found = true;
                     }
                 } else {
                     printf("[UDP] Discovery timed out.\n");
                 }
            }
            closesocket(udp_socket);
        }
    }

    if (!ip_found) {
        while (true) {
            printf("\nEnter server IP manually: ");
            if (fgets(server_ip, sizeof(server_ip), stdin) == NULL) break;
            server_ip[strcspn(server_ip, "\n")] = '\0';

            struct sockaddr_in sa;
            if (inet_pton(AF_INET, server_ip, &(sa.sin_addr)) == 1) break;
            
            printf("[ERROR] Invalid IPv4 address. Try again.\n");
        }
    }

    SOCKET tcp_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcp_socket == INVALID_SOCKET) {
        printf("[FATAL] TCP socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    printf("[TCP] Connecting to %s...\n", server_ip);
    if (connect(tcp_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("[FATAL] Connection failed: %d\n", WSAGetLastError());
        closesocket(tcp_socket);
        WSACleanup();
        return 1;
    }

    char nickname[32];
    char verify_buffer[64];
    
    while (true) {
        printf("Enter your nickname: ");
        if (fgets(nickname, sizeof(nickname), stdin) == NULL) {
            closesocket(tcp_socket);
            WSACleanup();
            return 0;
        }
        nickname[strcspn(nickname, "\r\n")] = '\0';

        if (strlen(nickname) == 0) continue;

        if (send(tcp_socket, nickname, (int)strlen(nickname), 0) == SOCKET_ERROR) {
            printf("[ERROR] Send failed: %d\n", WSAGetLastError());
            closesocket(tcp_socket);
            WSACleanup();
            return 1;
        }

        memset(verify_buffer, 0, sizeof(verify_buffer));
        int bytes_received = recv(tcp_socket, verify_buffer, sizeof(verify_buffer) - 1, 0);
        if (bytes_received > 0) {
            verify_buffer[bytes_received] = '\0';
            if (strncmp(verify_buffer, "OK", 2) == 0) {
                printf("[SUCCESS] Login accepted! Welcome to the chat.\n\n");
                break;
            } else if (strncmp(verify_buffer, "FULL", 4) == 0) {
                printf("[REJECT] Server is full. Try again later.\n");
                closesocket(tcp_socket);
                WSACleanup();
                return 0;
            } else if (strncmp(verify_buffer, "NO", 2) == 0) {
                printf("[REJECT] Nickname taken. Choose another.\n");
            } else {
                printf("[ERROR] Server error: %s\n", verify_buffer);
            }
        } else {
            printf("[FATAL] Server disconnected during login.\n");
            closesocket(tcp_socket);
            WSACleanup();
            return 1;
        }
    }

    CreateThread(NULL, 0, receive_thread, (LPVOID)tcp_socket, 0, NULL);

    char message_buffer[BUFLEN];
    while (is_connected) {
        printf("[You] > ");
        if (fgets(message_buffer, BUFLEN, stdin) == NULL) break;
        
        message_buffer[strcspn(message_buffer, "\r\n")] = '\0';

        if (strlen(message_buffer) > 0 && is_connected) {
            if (send(tcp_socket, message_buffer, (int)strlen(message_buffer), 0) == SOCKET_ERROR) {
                if (is_connected) printf("[ERROR] Send failed.\n");
                break;
            }
        }
    }

    is_connected = false;
    closesocket(tcp_socket);
    WSACleanup();
    return 0;
}