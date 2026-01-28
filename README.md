# LANChat-C

A basic chatroom program I built from scratch in C to learn how networking actually works deep within

## Features
* **Auto-Discovery:** You don't need to know the Server IP. The client finds it using UDP broadcasting automatically on your LAN.
* **Real-time Chat:** Messages pop up instantly for everyone connected.
* **Duplicate Name Check:** The server rejects you if you try to steal someone's username.
* **Works Offline:** Great for LAN parties or when the internet is down.

## How to compile
You need GCC (MinGW) installed.
Since this uses the ws2_32.lib library, you need to tell the linker to link it

### Server
```bash
gcc server.c -o server.exe -lws2_32
```

### Client
```bash
gcc client.c -o client.exe -lws2_32
```

## How to run it

### Step 1: Start the server
Run this on one computer
```bash
./server.exe
```

If you are running VirtualBox or VPN, the auto-discovery might get confused. You should then force the server to use Wi-Fi IP like this:
```bash
./server.exe <your-device-local-ipv4>
```
**Example:**
```bash
./server.exe 192.168.1.51
```

### Step 2: Start the clients
Run this on another computer (or the same one for testing)
```bash
./client.exe
```
The client should say **'Discovered Server!'** and connect automatically. If it doesn't then the program will ask you to type the server address manually

**Note:** This program works only on private networks so if you use a public network, change it to private in the wi-fi settings

## Troubleshooting common errors
Networking is a bit tricky in C, there might be many possible reasons the program won't work

1. **Windows Firewall:** It loves to block UDP packets. Make sure the network is set to private or turn off the firewall briefly.

2. **Virtual machines:** If you have VirtualBox installed, it might be "stealing" the broadcast packets. Use the specific IP command mentioned in Step 1.

3. **Wi-Fi isolation:** Some universities or cafes block devices from talking to each other. This works best on home Wi-Fi.