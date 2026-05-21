#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <iostream>
#include <string>
#include <cstring>
#include "json.hpp"

#pragma comment(lib, "ws2_32")

using json = nlohmann::json;
using namespace std;

const int HEADER_SIZE = 2;
const int BUFFER_SIZE = 1024;
const int MAP_WIDTH = 24;
const int MAP_HEIGHT = 24;
const int MAX_PLAYERS = 4;

bool RecvExact(SOCKET sock, char* buf, int n)
{
    int received = 0;
    while (received < n)
    {
        int ret = recv(sock, buf + received, n - received, 0);
        if (ret <= 0) return false;
        received += ret;
    }
    return true;
}

bool RecvPacket(SOCKET sock, string& outJson)
{
    char header[3] = { 0 };
    if (!RecvExact(sock, header, HEADER_SIZE))
        return false;
    int bodyLen = atoi(header);
    if (bodyLen <= 0 || bodyLen >= BUFFER_SIZE)
        return false;
    char body[BUFFER_SIZE] = { 0 };
    if (!RecvExact(sock, body, bodyLen))
        return false;
    outJson = string(body, bodyLen);
    return true;
}
  
bool SendPacket(SOCKET sock, const string& jsonStr)
{
    char header[3];
    sprintf_s(header, "%02d", (int)jsonStr.length());
    string packet = string(header, 2) + jsonStr;
    int total = (int)packet.length();
    int sent = 0;
    while (sent < total)
    {
        int ret = send(sock, packet.c_str() + sent, total - sent, 0);
        if (ret <= 0) return false;
        sent += ret;
    }
    return true;
}

struct ClientInfo
{
    SOCKET sock = INVALID_SOCKET;
    int id = 0;
    int x = 0;
    int y = 0;
    bool connected = false;
};

void Broadcast(ClientInfo clients[], int count, const string& msg)
{
    for (int k = 0; k < count; ++k)
    {
        if (clients[k].connected)
        {
            SendPacket(clients[k].sock, msg);
        }
    }
}

int main()
{
    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET listenSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    SOCKADDR_IN addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(35000);

    ::bind(listenSocket, (SOCKADDR*)&addr, sizeof(addr));
    listen(listenSocket, SOMAXCONN);

    ClientInfo clients[MAX_PLAYERS];
    clients[0].id = 1; clients[0].x = 1;  clients[0].y = 1;
    clients[1].id = 2; clients[1].x = 22; clients[1].y = 1;
    clients[2].id = 3; clients[2].x = 1;  clients[2].y = 22;
    clients[3].id = 4; clients[3].x = 22; clients[3].y = 22;

    int clientCount = 0;

    TIMEVAL timeOut;
    timeOut.tv_sec = 0;
    timeOut.tv_usec = 100000;

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(listenSocket, &readSet);

    cout << "Server started on port 35000 (max " << MAX_PLAYERS << " players)" << endl;

    while (true)
    {
        fd_set copySet = readSet;
        int changed = select(0, &copySet, 0, 0, &timeOut);

        if (changed <= 0) continue;

        if (FD_ISSET(listenSocket, &copySet))
        {
            if (clientCount < MAX_PLAYERS)
            {
                SOCKADDR_IN clientAddr;
                int len = sizeof(clientAddr);
                SOCKET clientSock = accept(listenSocket, (SOCKADDR*)&clientAddr, &len);

                clients[clientCount].sock = clientSock;
                clients[clientCount].connected = true;
                clientCount++;

                cout << "Player " << clientCount << " connected: "
                    << inet_ntoa(clientAddr.sin_addr) << endl;

                FD_SET(clientSock, &readSet);

                {
                    json jInit;
                    jInit["type"] = "init";
                    jInit["id"] = clients[clientCount - 1].id;
                    string initStr = jInit.dump();
                    SendPacket(clientSock, initStr);
                }

                for (int k = 0; k < clientCount; ++k)
                {
                    if (!clients[k].connected) continue;

                    for (int p = 0; p < clientCount; ++p)
                    {
                        if (!clients[p].connected) continue;

                        json jSync;
                        jSync["type"] = "move";
                        jSync["id"] = clients[p].id;
                        jSync["x"] = clients[p].x;
                        jSync["y"] = clients[p].y;
                        string syncStr = jSync.dump();
                        SendPacket(clients[k].sock, syncStr);
                    }
                }
            }
        }

        for (int i = 0; i < clientCount; ++i)
        {
            if (!clients[i].connected) continue;
            if (!FD_ISSET(clients[i].sock, &copySet)) continue;

            string jsonStr;
            if (!RecvPacket(clients[i].sock, jsonStr))
            {
                cout << "Player " << clients[i].id << " disconnected." << endl;
                FD_CLR(clients[i].sock, &readSet);
                closesocket(clients[i].sock);
                clients[i].connected = false;

                {
                    json jLeave;
                    jLeave["type"] = "leave";
                    jLeave["id"] = clients[i].id;
                    string leaveStr = jLeave.dump();
                    Broadcast(clients, clientCount, leaveStr);
                }
                continue;
            }

            try
            {
                json jRecv = json::parse(jsonStr);
                string type = jRecv["type"].get<string>();

                if (type == "input")
                {
                    string dir = jRecv["dir"].get<string>();

                    int newX = clients[i].x;
                    int newY = clients[i].y;

                    if (dir == "W")      newY--;
                    else if (dir == "S") newY++;
                    else if (dir == "A") newX--;
                    else if (dir == "D") newX++;
                    else continue;

                    if (newX < 0 || newX >= MAP_WIDTH ||
                        newY < 0 || newY >= MAP_HEIGHT)
                        continue;

                    clients[i].x = newX;
                    clients[i].y = newY;

                    cout << "Player " << clients[i].id
                        << " -> (" << newX << ", " << newY << ")" << endl;

                    {
                        json jMove;
                        jMove["type"] = "move";
                        jMove["id"] = clients[i].id;
                        jMove["x"] = newX;
                        jMove["y"] = newY;
                        string moveStr = jMove.dump();
                        Broadcast(clients, clientCount, moveStr);
                    }
                }
            }
            catch (...)
            {
                cout << "Invalid JSON from client " << clients[i].id << endl;
            }
        }
    }

    closesocket(listenSocket);
    WSACleanup();
    return 0;
}
