#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _POSIX_C_SOURCE 199309L


#include<stdio.h>
#include<time.h>
#include<stdlib.h>
#include<winsock2.h>
#include <sys\timeb.h> 
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <intrin.h>

#define RTSS_SHARED_MEMORY_NAME "RTSSSharedMemoryV2"

typedef struct RTSS_SHARED_MEMORY
{
    DWORD dwSignature;
    DWORD dwVersion;

    DWORD dwAppEntrySize;
    DWORD dwAppArrOffset;
    DWORD dwAppArrSize;

    DWORD dwOSDEntrySize;
    DWORD dwOSDArrOffset;
    DWORD dwOSDArrSize;

    DWORD dwOSDFrame;

    LONG dwBusy;
} RTSS_SHARED_MEMORY, *LPRTSS_SHARED_MEMORY;

typedef struct RTSS_SHARED_MEMORY_OSD_ENTRY
{
    char szOSD[256];
    char szOSDOwner[256];
    char szOSDEx[4096];
} RTSS_SHARED_MEMORY_OSD_ENTRY, *LPRTSS_SHARED_MEMORY_OSD_ENTRY;

static BOOL UpdateOSD(LPCSTR lpText, LPCSTR mapName)
{
    BOOL bResult = FALSE;

    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, RTSS_SHARED_MEMORY_NAME);

    if (hMapFile)
    {
        LPVOID pMapAddr = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        LPRTSS_SHARED_MEMORY pMem = (LPRTSS_SHARED_MEMORY)pMapAddr;

        if (pMem)
        {
            if ((pMem->dwSignature == 'RTSS') && (pMem->dwVersion >= 0x00020000))
            {
                for (DWORD dwPass = 0; dwPass < 2; dwPass++)
                {
                    for (DWORD dwEntry = 1; dwEntry < pMem->dwOSDArrSize; dwEntry++)
                    {
                        LPRTSS_SHARED_MEMORY_OSD_ENTRY pEntry = (LPRTSS_SHARED_MEMORY_OSD_ENTRY)((LPBYTE)pMem + pMem->dwOSDArrOffset + dwEntry * pMem->dwOSDEntrySize);

                        if (dwPass)
                        {
                            if (!pEntry->szOSDOwner[0])
                            {
                                strncpy_s(pEntry->szOSDOwner, sizeof(pEntry->szOSDOwner), mapName, _TRUNCATE);
                            }
                        }

                        if (!strcmp(pEntry->szOSDOwner, mapName))
                        {
                            if (pMem->dwVersion >= 0x00020007)
                            {
                                if (pMem->dwVersion >= 0x0002000e)
                                {
                                    LONG dwBusy = _interlockedbittestandset(&pMem->dwBusy, 0);

                                    if (!dwBusy)
                                    {
                                        strncpy_s(pEntry->szOSDEx, sizeof(pEntry->szOSDEx), lpText, _TRUNCATE);
                                        pMem->dwBusy = 0;
                                    }
                                }
                                else
                                {
                                    strncpy_s(pEntry->szOSDEx, sizeof(pEntry->szOSDEx), lpText, _TRUNCATE);
                                }
                            }
                            else
                            {
                                strncpy_s(pEntry->szOSD, sizeof(pEntry->szOSD), lpText, _TRUNCATE);
                            }

                            pMem->dwOSDFrame++;
                            bResult = TRUE;
                            break;
                        }
                    }

                    if (bResult)
                        break;
                }
            }

            UnmapViewOfFile(pMapAddr);
        }

        CloseHandle(hMapFile);
    }

    return bResult;
}

static void ReleaseOSD(LPCSTR mapName)
{
    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, RTSS_SHARED_MEMORY_NAME);

    if (hMapFile)
    {
        LPVOID pMapAddr = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        LPRTSS_SHARED_MEMORY pMem = (LPRTSS_SHARED_MEMORY)pMapAddr;

        if (pMem)
        {
            if ((pMem->dwSignature == 'RTSS') && (pMem->dwVersion >= 0x00020000))
            {
                for (DWORD dwEntry = 1; dwEntry < pMem->dwOSDArrSize; dwEntry++)
                {
                    LPRTSS_SHARED_MEMORY_OSD_ENTRY pEntry = (LPRTSS_SHARED_MEMORY_OSD_ENTRY)((LPBYTE)pMem + pMem->dwOSDArrOffset + dwEntry * pMem->dwOSDEntrySize);

                    if (!strcmp(pEntry->szOSDOwner, mapName))
                    {
                        SecureZeroMemory(pEntry, pMem->dwOSDEntrySize);
                        pMem->dwOSDFrame++;
                    }
                }
            }

            UnmapViewOfFile(pMapAddr);
        }

        CloseHandle(hMapFile);
    }
}

#pragma comment(lib,"ws2_32.lib") 
#pragma comment(lib, "iphlpapi.lib")

#define NETWORK_ERROR -1

#define NETWORK_OK 0
#define RTSS_OSD_OWNER "BDOPingUtility"

#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))


int main(int argc, char* argv[])
{
    LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
    LARGE_INTEGER Frequency;
    LARGE_INTEGER TotalPing = { 0 }, AvgPing;
    int nret, i, loops;
    WSADATA wsa;
    SOCKET s;
    struct sockaddr_in server;

    PMIB_TCPTABLE2 pTcpTable;
    ULONG ulSize = 0;
    DWORD dwRetVal = 0;

    int pid = 0;
    char* target = "BlackDesert64.exe";

    char szLocalAddr[128];
    char szRemoteAddr[128];
    char szGameServerRemoteAddr[128];
    int dwGameServerPort = 0;
    struct in_addr IpAddr;
    PROCESSENTRY32 entry;

    if (argc == 1) { loops = 10; }
    else { loops = atoi(argv[1]); }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        printf("Invalid handle error: %s", GetLastError());
    }

    entry.dwSize = sizeof(PROCESSENTRY32);

    //Safety check
    if (!Process32First(snap, &entry)) {
        printf("Error retriving process info: %s", GetLastError());
        CloseHandle(snap);
    }

    printf("Looking for %s process!\n", target);

    do {
        //printf("%s\n",pe32.szExeFile); // print all process list
        if (strcmp(entry.szExeFile, target) == 0) {
            pid = entry.th32ProcessID;
        }

    } while (Process32Next(snap, &entry));
    CloseHandle(snap);

    if (pid != 0) {
        printf("Detected %s PID is %d !\n", target, pid);
    }
    else {
        printf("Process '%s' not found. Exiting\n", target);
        return 0;
    }

    printf("Inizializing TCP table\n");

    pTcpTable = (MIB_TCPTABLE2*)MALLOC(sizeof(MIB_TCPTABLE2));
    if (pTcpTable == NULL) {
        printf("Error allocating memory\n");
        return 1;
    }

    ulSize = sizeof(MIB_TCPTABLE);
    if ((dwRetVal = GetTcpTable2(pTcpTable, &ulSize, TRUE)) == ERROR_INSUFFICIENT_BUFFER) {
        FREE(pTcpTable);
        pTcpTable = (MIB_TCPTABLE2*)MALLOC(ulSize);
        if (pTcpTable == NULL) {
            printf("Error allocating memory\n");
            return 1;
        }
    }
    // Get bdo connection data
    if ((dwRetVal = GetTcpTable2(pTcpTable, &ulSize, TRUE)) == NO_ERROR) {
        for (i = 0; i < (int)pTcpTable->dwNumEntries; i++) {
            if (pTcpTable->table[i].dwOwningPid == pid) {

                IpAddr.S_un.S_addr = (u_long)pTcpTable->table[i].dwLocalAddr;
                strcpy_s(szLocalAddr, sizeof(szLocalAddr), inet_ntoa(IpAddr));
                IpAddr.S_un.S_addr = (u_long)pTcpTable->table[i].dwRemoteAddr;
                strcpy_s(szRemoteAddr, sizeof(szRemoteAddr), inet_ntoa(IpAddr));
                if (ntohs((u_short)pTcpTable->table[i].dwRemotePort) == 8889)
                {
                    dwGameServerPort = ntohs((u_short)pTcpTable->table[i].dwRemotePort);
                    strcpy_s(szGameServerRemoteAddr, sizeof(szRemoteAddr), szRemoteAddr);
                    printf("TCP[%d] State: %ld - ", i, pTcpTable->table[i].dwState);
                    switch (pTcpTable->table[i].dwState) {
                    case MIB_TCP_STATE_CLOSED:
                        printf("CLOSED\n");
                        break;
                    case MIB_TCP_STATE_LISTEN:
                        printf("LISTEN\n");
                        break;
                    case MIB_TCP_STATE_SYN_SENT:
                        printf("SYN-SENT\n");
                        break;
                    case MIB_TCP_STATE_SYN_RCVD:
                        printf("SYN-RECEIVED\n");
                        break;
                    case MIB_TCP_STATE_ESTAB:
                        printf("ESTABLISHED\n");
                        break;
                    case MIB_TCP_STATE_FIN_WAIT1:
                        printf("FIN-WAIT-1\n");
                        break;
                    case MIB_TCP_STATE_FIN_WAIT2:
                        printf("FIN-WAIT-2 \n");
                        break;
                    case MIB_TCP_STATE_CLOSE_WAIT:
                        printf("CLOSE-WAIT\n");
                        break;
                    case MIB_TCP_STATE_CLOSING:
                        printf("CLOSING\n");
                        break;
                    case MIB_TCP_STATE_LAST_ACK:
                        printf("LAST-ACK\n");
                        break;
                    case MIB_TCP_STATE_TIME_WAIT:
                        printf("TIME-WAIT\n");
                        break;
                    case MIB_TCP_STATE_DELETE_TCB:
                        printf("DELETE-TCB\n");
                        break;
                    default:
                        printf("UNKNOWN dwState value\n");
                        break;
                    }
                    printf("%s Game Server ip: %s@%d\n", target, szGameServerRemoteAddr, dwGameServerPort);
                }
            }
        }
    }
    else {
        printf("\tGetTcpTable2 failed with %d\n", dwRetVal);
        FREE(pTcpTable);
        return 1;
    }

    printf(" \nInitialising Winsock... ");
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf(" Failed.Error Code : % d ", WSAGetLastError());
        return 1;
    }

    printf(" Initialised.\n");


    server.sin_addr.s_addr = inet_addr(szGameServerRemoteAddr);
    server.sin_family = AF_INET;
    server.sin_port = htons(dwGameServerPort);

    //Connect to bdo server and ping

    for (int i = 0; i < loops; i++)
    {
        char osdText[128];

        if ((s = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
        {
            printf(" Could not create socket : % d ", WSAGetLastError());
        }
        //printf(" Socket created.\n ");

        QueryPerformanceFrequency(&Frequency);
        QueryPerformanceCounter(&StartingTime);
        nret = connect(s, (struct sockaddr*)&server, sizeof(server));
        if (nret == SOCKET_ERROR)
        {
            nret = WSAGetLastError();
            printf("socket error %d", nret);
            WSACleanup();
            return NETWORK_ERROR;
        }
        QueryPerformanceCounter(&EndingTime);
        ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;
        ElapsedMicroseconds.QuadPart *= 1000000;
        ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;

        printf("Connected, took: %d.%.2dms\n", ElapsedMicroseconds.QuadPart / 1000, ElapsedMicroseconds.QuadPart % 1000);
        snprintf(osdText, sizeof(osdText), "BDO Ping: %d.%02d ms", (int)(ElapsedMicroseconds.QuadPart / 1000), (int)(ElapsedMicroseconds.QuadPart % 1000));
        UpdateOSD(osdText, RTSS_OSD_OWNER);
        TotalPing.QuadPart = TotalPing.QuadPart + ElapsedMicroseconds.QuadPart;
        closesocket(s);
        Sleep(1000);
    }
    WSACleanup();
    AvgPing.QuadPart = TotalPing.QuadPart / loops;

    printf("Pinged %d times, average ping of: %d.%.2dms", loops, AvgPing.QuadPart / 1000, AvgPing.QuadPart % 1000);
    {
        char osdText[128];
        snprintf(osdText, sizeof(osdText), "BDO Avg Ping: %d.%02d ms", (int)(AvgPing.QuadPart / 1000), (int)(AvgPing.QuadPart % 1000));
        UpdateOSD(osdText, RTSS_OSD_OWNER);
    }

    if (pTcpTable != NULL) {
        FREE(pTcpTable);
        pTcpTable = NULL;
    }
    ReleaseOSD(RTSS_OSD_OWNER);
    getchar();
    return 0;
}
