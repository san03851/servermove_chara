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
    int id = 0;       // 1 = P, 2 = W, 3 = o
    int x = 0;
    int y = 0;
    bool connected = false;
};

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

    ClientInfo clients[10];
    clients[0].id = 1; clients[0].x = 0;  clients[0].y = 12;
    clients[1].id = 2; clients[1].x = 10; clients[1].y = 22;
    //clients[2].id = 3; clients[1].x = 57; clients[1].y = 17;
    //clients[3].id = 4; clients[1].x = 67; clients[1].y = 43;
    //clients[4].id = 5; clients[1].x = 77; clients[1].y = 53;
    //clients[5].id = 6; clients[1].x = 27; clients[1].y = 15;
    //clients[6].id = 7; clients[1].x = 37; clients[1].y = 24;
    //clients[7].id = 8; clients[1].x = 47; clients[1].y = 74;
    //clients[8].id = 9; clients[1].x = 80; clients[1].y = 80;
    //clients[9].id = 10; clients[1].x = 99; clients[1].y = 99;

    int clientCount = 0;

    TIMEVAL timeOut;
    timeOut.tv_sec = 0;
    timeOut.tv_usec = 100000;

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(listenSocket, &readSet);

    while (true)
    {
        fd_set copySet = readSet;
        int changed = select(0, &copySet, 0, 0, &timeOut);

        if (changed <= 0) continue;

        if (FD_ISSET(listenSocket, &copySet))
        {
            if (clientCount < 2)
            {
                SOCKADDR_IN clientAddr;
                int len = sizeof(clientAddr);
                SOCKET clientSock = accept(listenSocket, (SOCKADDR*)&clientAddr, &len);

                clients[clientCount].sock = clientSock;
                clients[clientCount].connected = true;
                clientCount++;

                cout << "Client " << clientCount << " connected: "
                    << inet_ntoa(clientAddr.sin_addr) << endl;

                FD_SET(clientSock, &readSet);

                json initJ;
                initJ["type"] = "init";
                initJ["id"] = clients[clientCount - 1].id;
                SendPacket(clientSock, initJ.dump());
                if (clientCount >= 0)
                {
                    for (int k = 0; k < 2; ++k)
                    {
                        json m1;
                        m1["type"] = "move";
                        m1["id"] = clients[0].id;
                        m1["x"] = clients[0].x;
                        m1["y"] = clients[0].y;
                        SendPacket(clients[k].sock, m1.dump());

                        json m2;
                        m2["type"] = "move";
                        m2["id"] = clients[1].id;
                        m2["x"] = clients[1].x;
                        m2["y"] = clients[1].y;
                        SendPacket(clients[k].sock, m2.dump());
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
                cout << "Client " << clients[i].id << " disconnected." << endl;
                FD_CLR(clients[i].sock, &readSet);
                closesocket(clients[i].sock);
                clients[i].connected = false;
                continue;
            }

            try
            {
                json j = json::parse(jsonStr);
                string type = j["type"].get<string>();

                if (type == "move")
                {
                    clients[i].x = j["x"].get<int>();
                    clients[i].y = j["y"].get<int>();

                    cout << "Player " << clients[i].id
                        << " -> (" << clients[i].x << ", " << clients[i].y << ")" << endl;

                    string broadcastStr = j.dump();
                    for (int k = 0; k < clientCount; ++k)
                    {
                        if (clients[k].connected)
                        {
                            SendPacket(clients[k].sock, broadcastStr);
                        }
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