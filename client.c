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

DWORD WINAPI recv_thread(LPVOID arg)
{
    SOCKET sock = (SOCKET) arg;
    char buf[BUFLEN];
    
    while (1) {
        memset(buf, 0, BUFLEN);
        int n = recv(sock, buf, BUFLEN - 1, 0);
        if (n <= 0) {
            printf("\nDisconnected from server\n");
            break;
        }
        buf[n] = '\0';
        
        printf("\r%s\n> ", buf); 
    }
    return 0;
}

int main(int argc, char *argv[])
{
    check_network_security();

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Failed to init winsock\n");
        return 1;
    }

    char serverip[INET_ADDRSTRLEN];
    memset(serverip, 0, INET_ADDRSTRLEN);
    bool ip_found = false;

    if (argc > 1) {
        strncpy(serverip, argv[1], INET_ADDRSTRLEN - 1);
        printf("Using Server IP from argument: %s\n", serverip);
        ip_found = true;
    } else {
        printf("Looking for server via UDP Broadcast...\n");

        SOCKET UDPSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (UDPSocket == INVALID_SOCKET) {
             printf("UDP Socket creation failed: %d\n", WSAGetLastError());
        } else {
            int reuse = 1;
            if (setsockopt(UDPSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse)) < 0) {
                printf("SO_REUSEADDR failed: %d\n", WSAGetLastError());
            }

            DWORD timeout = 3000;
            if (setsockopt(UDPSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
                 printf("SO_RCVTIMEO failed\n");
            }

            struct sockaddr_in listenaddr;
            struct sockaddr_in sender;
            memset(&listenaddr, 0, sizeof(listenaddr));

            listenaddr.sin_family = AF_INET;
            listenaddr.sin_port = htons(UDP_PORT);
            listenaddr.sin_addr.s_addr = INADDR_ANY;

            if (bind(UDPSocket, (struct sockaddr*) &listenaddr, sizeof(listenaddr)) == SOCKET_ERROR) {
                 printf("UDP Bind failed %d\n", WSAGetLastError());
            } else {
                 char udpbuffer[128];
                 int senderlength = sizeof(sender);
                 printf("Listening for server signal\n");
                 
                 int recieved = recvfrom(UDPSocket, udpbuffer, sizeof(udpbuffer) - 1, 0, (struct sockaddr*) &sender, &senderlength);

                 if (recieved > 0) {
                     udpbuffer[recieved] = '\0';
                     printf("Discovered server: %s\n", udpbuffer);
                     inet_ntop(AF_INET, &sender.sin_addr, serverip, sizeof(serverip));
                     printf("Server IP: %s\n", serverip);
                     ip_found = true;
                 } else {
                     printf("Discovery timed out. Server not found via UDP.\n");
                 }
            }
            closesocket(UDPSocket);
        }
    }

    if (!ip_found) {
        while(true) {
            printf("\nEnter Server IP manually: ");
            fgets(serverip, sizeof(serverip), stdin);
            serverip[strcspn(serverip, "\n")] = '\0';

            if (strlen(serverip) > 6) break;
            printf("Invalid IP. Try again\n");
        }
    }

    SOCKET ConnectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ConnectSocket == INVALID_SOCKET) {
        printf("TCP socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, serverip, &server.sin_addr);

    printf("Connecting to %s\n", serverip);
    if (connect(ConnectSocket, (struct sockaddr*) &server, sizeof(server)) == SOCKET_ERROR) {
        printf("Failed to connect to server %d\n", WSAGetLastError());
        closesocket(ConnectSocket);
        WSACleanup();
        return 1;
    }

    char username[32];
    char verifyBuf[32];
    
    while(true) {
        printf("Enter your name: ");
        fgets(username, sizeof(username), stdin);
        username[strcspn(username, "\n")] = '\0';

        if (strlen(username) == 0) continue;

        if (send(ConnectSocket, username, (int) strlen(username), 0) == SOCKET_ERROR) {
            printf("Send failed: %d\n", WSAGetLastError());
            break;
        }

        int v = recv(ConnectSocket, verifyBuf, 32, 0);
        if (v > 0) {
            verifyBuf[v] = '\0';
            if (strncmp(verifyBuf, "OK", 2) == 0) {
                printf("Login accepted!\n\n");
                break;
            } else {
                printf("Server rejected name (name taken)\n");
            }
        } else {
            printf("Server disconnected during login\n");
            return 1;
        }
    }

    CreateThread(NULL, 0, recv_thread, (LPVOID) ConnectSocket, 0, NULL);

    char buffer[BUFLEN];
    while (true)
    {
        printf("> ");
        fgets(buffer, BUFLEN, stdin);
        
        buffer[strcspn(buffer, "\n")] = '\0';

        if (strlen(buffer) > 0) {
            if (send(ConnectSocket, buffer, (int) strlen(buffer), 0) == SOCKET_ERROR) {
                printf("Send failed\n");
                break;
            }
        }
    }

    closesocket(ConnectSocket);
    WSACleanup();
    return 0;
}