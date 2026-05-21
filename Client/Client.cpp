#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <Windows.h>
#include <iostream>
#include <string>
#include <cstring>
#include <process.h>
#include <conio.h>
#include "json.hpp"

#pragma comment(lib, "ws2_32")

using json = nlohmann::json;
using namespace std;

const int HEADER_SIZE = 2;
const int BUFFER_SIZE = 1024;
const int MAP_WIDTH = 24;
const int MAP_HEIGHT = 24;
const int MAX_PLAYERS = 4;

const char PLAYER_SYMBOLS[MAX_PLAYERS] = { 'A', 'B', 'C', 'D' };

struct PlayerInfo
{
    int id = 0;
    int x = -1;
    int y = -1;
    bool active = false;
};

int myId = 0;
PlayerInfo players[MAX_PLAYERS];
bool running = true;

CRITICAL_SECTION cs;

int FindPlayerIndex(int id)
{
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        if (players[i].id == id) return i;
    }
    return -1;
}

int RegisterPlayer(int id)
{
    int idx = FindPlayerIndex(id);
    if (idx >= 0) return idx;

    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        if (!players[i].active && players[i].id == 0)
        {
            players[i].id = id;
            players[i].active = true;
            return i;
        }
    }
    return -1;
}

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

void RenderMap()
{
    COORD pos = { 0, 0 };
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);

    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ci);
    ci.bVisible = FALSE;
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ci);

    string screen;
    screen.reserve(3000);

    screen += "+";
    for (int x = 0; x < MAP_WIDTH; ++x) screen += "--";
    screen += "+\n";

    EnterCriticalSection(&cs);
    PlayerInfo localPlayers[MAX_PLAYERS];
    for (int i = 0; i < MAX_PLAYERS; ++i)
        localPlayers[i] = players[i];
    int localMyId = myId;
    LeaveCriticalSection(&cs);

    for (int y = 0; y < MAP_HEIGHT; ++y)
    {
        screen += "|";
        for (int x = 0; x < MAP_WIDTH; ++x)
        {
            bool drawn = false;
            for (int p = 0; p < MAX_PLAYERS; ++p)
            {
                if (localPlayers[p].active &&
                    localPlayers[p].x == x && localPlayers[p].y == y)
                {
                    screen += PLAYER_SYMBOLS[p];
                    screen += ' ';
                    drawn = true;
                    break;
                }
            }
            if (!drawn)
                screen += ". ";
        }
        screen += "|\n";
    }

    screen += "+";
    for (int x = 0; x < MAP_WIDTH; ++x) screen += "--";
    screen += "+\n";
    int myIdx = -1;
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        if (localPlayers[i].id == localMyId)
        {
            myIdx = i;
            break;
        }
    }

    screen += "You are [";
    if (myIdx >= 0) screen += PLAYER_SYMBOLS[myIdx];
    else screen += '?';
    screen += "] | Players: ";

    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        if (localPlayers[i].active)
        {
            screen += PLAYER_SYMBOLS[i];
            if (localPlayers[i].id == localMyId)
                screen += "(me) ";
            else
                screen += " ";
        }
    }
    screen += "          \n";

    screen += "Move: W A S D | Q to quit              \n";
    screen += "                                        \n";

    cout << screen;
}

unsigned WINAPI RecvThread(void* arg)
{
    SOCKET sock = *(SOCKET*)arg;

    while (running)
    {
        string jsonStr;
        if (!RecvPacket(sock, jsonStr))
        {
            cout << "\nDisconnected from server." << endl;
            running = false;
            break;
        }

        try
        {
            json jRecv = json::parse(jsonStr);
            string type = jRecv["type"].get<string>();

            if (type == "init")
            {
                EnterCriticalSection(&cs);
                myId = jRecv["id"].get<int>();
                LeaveCriticalSection(&cs);
            }
            else if (type == "move")
            {
                int id = jRecv["id"].get<int>();
                int x = jRecv["x"].get<int>();
                int y = jRecv["y"].get<int>();

                EnterCriticalSection(&cs);
                int idx = RegisterPlayer(id);
                if (idx >= 0)
                {
                    players[idx].x = x;
                    players[idx].y = y;
                    players[idx].active = true;
                }
                LeaveCriticalSection(&cs);

                RenderMap();
            }
            else if (type == "leave")
            {
                int id = jRecv["id"].get<int>();

                EnterCriticalSection(&cs);
                int idx = FindPlayerIndex(id);
                if (idx >= 0)
                {
                    players[idx].active = false;
                    players[idx].id = 0;
                    players[idx].x = -1;
                    players[idx].y = -1;
                }
                LeaveCriticalSection(&cs);

                RenderMap();
            }
        }
        catch (...) {}
    }

    return 0;
}

int main()
{
    InitializeCriticalSection(&cs);

    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        players[i].id = 0;
        players[i].x = -1;
        players[i].y = -1;
        players[i].active = false;
    }

    char serverIP[64] = { 0 };
    cout << "Server IP (Enter for 127.0.0.1): ";
    cin.getline(serverIP, sizeof(serverIP));
    if (strlen(serverIP) == 0)
        strcpy_s(serverIP, "127.0.0.1");

    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    SOCKADDR_IN addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(serverIP);
    addr.sin_port = htons(35000);

    cout << "Connecting to " << serverIP << ":35000..." << endl;

    if (connect(sock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        cout << "Connection failed!" << endl;
        WSACleanup();
        return 1;
    }

    {
        string jsonStr;
        if (RecvPacket(sock, jsonStr))
        {
            json jRecv = json::parse(jsonStr);
            myId = jRecv["id"].get<int>();
            cout << "Assigned ID: " << myId << endl;
        }
    }

    HANDLE hRecvThread = (HANDLE)_beginthreadex(0, 0, RecvThread, &sock, 0, 0);

    system("cls");
    RenderMap();

    while (running)
    {
        if (_kbhit())
        {
            int key = _getch();

            if (key == 'w' || key == 'W' ||
                key == 'a' || key == 'A' ||
                key == 's' || key == 'S' ||
                key == 'd' || key == 'D')
            {
                json jInput;
                jInput["type"] = "input";
                jInput["dir"] = string(1, (char)toupper(key));
                string inputStr = jInput.dump();
                if (!SendPacket(sock, inputStr))
                {
                    cout << "\nSend failed!" << endl;
                    running = false;
                    break;
                }
            }
            else if (key == 'q' || key == 'Q')
            {
                running = false;
                break;
            }
        }
        Sleep(16);
    }

    closesocket(sock);
    WaitForSingleObject(hRecvThread, 1000);
    CloseHandle(hRecvThread);
    DeleteCriticalSection(&cs);
    WSACleanup();

    return 0;
}
