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

int myId = 0;
int myX = 0, myY = 0;
int otherX = -1, otherY = -1;
bool running = true;

CRITICAL_SECTION cs;

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
    screen.reserve(2000);

    screen += "+";
    for (int x = 0; x < MAP_WIDTH; ++x) screen += "--";
    screen += "+\n";

    EnterCriticalSection(&cs);
    int p1x = 0, p1y = 0, p2x = 0, p2y = 0;
    if (myId == 1)
    {
        p1x = myX; p1y = myY;
        p2x = otherX; p2y = otherY;
    }
    else
    {
        p1x = otherX; p1y = otherY;
        p2x = myX; p2y = myY;
    }
    LeaveCriticalSection(&cs);

    for (int y = 0; y < MAP_HEIGHT; ++y)
    {
        screen += "|";
        for (int x = 0; x < MAP_WIDTH; ++x)
        {
            if (x == p1x && y == p1y)
                screen += "P ";
            else if (x == p2x && y == p2y)
                screen += "W ";
            else
                screen += ". ";
        }
        screen += "|\n";
    }

    screen += "+";
    for (int x = 0; x < MAP_WIDTH; ++x) screen += "--";
    screen += "+\n";

    if (myId == 0)
        screen += "You are [P] | Other is [W]          \n";
    else if (myId == 1)
        screen += "You are [W] | Other is [P]          \n";

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
            json j = json::parse(jsonStr);
            string type = j["type"].get<string>();

            if (type == "init")
            {
                EnterCriticalSection(&cs);
                myId = j["id"].get<int>();
                LeaveCriticalSection(&cs);
            }
            else if (type == "move")
            {
                int id = j["id"].get<int>();
                int x = j["x"].get<int>();
                int y = j["y"].get<int>();

                EnterCriticalSection(&cs);
                if (id == myId)
                {
                    myX = x;
                    myY = y;
                }
                else
                {
                    otherX = x;
                    otherY = y;
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
            json j = json::parse(jsonStr);
            myId = j["id"].get<int>();
            if (myId == 1)
            {
                myX = 2; myY = 2;
            }
            else
            {
                myX = 17; myY = 12;
            }
            cout << "Assigned ID: " << myId;
            if (myId == 1) cout << " (P)" << endl;
            else           cout << " (W)" << endl;
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

            if (key == 'w' || key == 'W')
            {
                EnterCriticalSection(&cs);
                if (myY > 0) myY--;
                LeaveCriticalSection(&cs);
            }
            else if (key == 's' || key == 'S')
            {
                EnterCriticalSection(&cs);
                if (myY < MAP_HEIGHT - 1) myY++;
                LeaveCriticalSection(&cs);
            }
            else if (key == 'a' || key == 'A')
            {
                EnterCriticalSection(&cs);
                if (myX > 0) myX--;
                LeaveCriticalSection(&cs);
            }
            else if (key == 'd' || key == 'D')
            {
                EnterCriticalSection(&cs);
                if (myX < MAP_WIDTH - 1) myX++;
                LeaveCriticalSection(&cs);
            }
            else if (key == 'q' || key == 'Q')
            {
                running = false;
                break;
            }
            else
            {
                continue;
            }

            EnterCriticalSection(&cs);
            json j;
            j["type"] = "move";
            j["id"] = myId;
            j["x"] = myX;
            j["y"] = myY;
            LeaveCriticalSection(&cs);

            if (!SendPacket(sock, j.dump()))
            {
                cout << "\nSend failed!" << endl;
                running = false;
                break;
            }
            RenderMap();
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