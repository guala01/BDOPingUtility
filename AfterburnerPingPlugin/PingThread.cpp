// PingThread.cpp : implementation file
//
// created by Unwinder
/////////////////////////////////////////////////////////////////////////////
#include "stdafx.h"
#include "PingThread.h"
#include "PingGlobals.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <float.h>
#include <stdlib.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
/////////////////////////////////////////////////////////////////////////////
#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif
/////////////////////////////////////////////////////////////////////////////
// CPingThread
/////////////////////////////////////////////////////////////////////////////
IMPLEMENT_DYNCREATE(CPingThread, CWinThread)
/////////////////////////////////////////////////////////////////////////////
CPingThread::CPingThread()
{
}
/////////////////////////////////////////////////////////////////////////////
CPingThread::CPingThread(LPCSTR lpProcessName, DWORD dwPort)
{
	m_hEventKill		= CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hEventAsyncPing	= CreateEvent(NULL, TRUE, FALSE, NULL);
	m_bAutoDelete		= FALSE;

	m_bWsaReady			= FALSE;
	m_dwLastPingTick	= 0;
	m_dwLastResolveTick	= 0;
	m_dwCachedPort		= 0;
	m_bCachedIpv6		= FALSE;
	m_fltPing			= FLT_MAX;
	m_fltPingRaw		= FLT_MAX;
	m_pingHistoryCount	= 0;
	m_pingHistoryIndex	= 0;
	for (int i = 0; i < kPingHistorySize; i++)
		m_pingHistory[i] = 0.0f;

	QueryPerformanceFrequency(&m_qwFreq);

	int wsaResult = WSAStartup(MAKEWORD(2,2), &m_wsaData);
	if (!wsaResult)
		m_bWsaReady = TRUE;

	SetTarget(lpProcessName, dwPort);
}
/////////////////////////////////////////////////////////////////////////////
CPingThread::~CPingThread()
{
	if (m_bWsaReady)
		WSACleanup();
}
/////////////////////////////////////////////////////////////////////////////
void CPingThread::UpdatePing()
{
	if (!m_bWsaReady)
	{
		m_fltPing = FLT_MAX;
		return;
	}

	ULONGLONG now = GetTickCount64();
	if (m_dwLastPingTick && (now - m_dwLastPingTick) < 1000)
		return;

	if (m_strCachedAddr.IsEmpty() || !m_dwCachedPort || (now - m_dwLastResolveTick) > 5000 || m_fltPing == FLT_MAX)
	{
		char szAddr[64] = { 0 };
		DWORD dwPort = m_dwPort;
		BOOL bIpv6 = FALSE;

		if (!FindGameServerEndpoint(szAddr, sizeof(szAddr), &dwPort))
		{
			if (!FindExitLagEndpoint(szAddr, sizeof(szAddr), &dwPort, &bIpv6))
			{
				m_fltPing = FLT_MAX;
				return;
			}
		}

		m_strCachedAddr = szAddr;
		m_dwCachedPort = dwPort;
		m_bCachedIpv6 = bIpv6;
		m_dwLastResolveTick = now;
	}

	FLOAT fltPing = FLT_MAX;
	BOOL bPingOk = FALSE;
	if (m_bCachedIpv6)
		bPingOk = MeasureTcpPing6(m_strCachedAddr, m_dwCachedPort, &fltPing);
	else
		bPingOk = MeasureTcpPing(m_strCachedAddr, m_dwCachedPort, &fltPing);

	if (bPingOk)
	{
		m_fltPingRaw = fltPing;
		RecordPing(fltPing);
		m_fltPing = GetSmoothedPing();
		m_dwLastPingTick = now;
	}
	else
	{
		m_fltPingRaw = FLT_MAX;
		if (m_pingHistoryCount == 0)
			m_fltPing = FLT_MAX;
	}
}
/////////////////////////////////////////////////////////////////////////////
BOOL CPingThread::InitInstance()
{
	HANDLE waitObj[2];

	waitObj[0]			= m_hEventKill;
	waitObj[1]			= m_hEventAsyncPing;

	BOOL bProcess		= TRUE;

	while (bProcess)
	{
		DWORD dwResult = WaitForMultipleObjects(2, waitObj, FALSE, 1000);

		switch (dwResult)
		{
		case WAIT_OBJECT_0:
			bProcess = FALSE;
			break;

		case WAIT_OBJECT_0 + 1:
			ResetEvent(m_hEventAsyncPing);	
			UpdatePing();
			break;

		case WAIT_TIMEOUT:
			UpdatePing();
			break;
		}
	}

	return FALSE;
}
/////////////////////////////////////////////////////////////////////////////
int CPingThread::ExitInstance()
{
	return CWinThread::ExitInstance();
}
/////////////////////////////////////////////////////////////////////////////
BEGIN_MESSAGE_MAP(CPingThread, CWinThread)
	//{{AFX_MSG_MAP(CPingThread)
		// NOTE - the ClassWizard will add and remove mapping macros here.
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()
/////////////////////////////////////////////////////////////////////////////
// CPingThread message handlers
/////////////////////////////////////////////////////////////////////////////
void CPingThread::Kill()
{
	SetEvent(m_hEventKill);
		//set KILL event (it will terminate InitInstance) 
	SetThreadPriority(THREAD_PRIORITY_HIGHEST);
		//allow thread to run at high priority during killing

	WaitForSingleObject(m_hThread		, INFINITE);
		//wait until thread is dead
	Destroy();
		//delete thread
}
/////////////////////////////////////////////////////////////////////////////
void CPingThread::Destroy()
{
	delete this;
}
/////////////////////////////////////////////////////////////////////////////
void CPingThread::UpdatePingAsync()
{
	SetEvent(m_hEventAsyncPing);	
}
/////////////////////////////////////////////////////////////////////////////
void CPingThread::SetTarget(LPCSTR lpProcessName, DWORD dwPort)
{
	m_strProcessName = lpProcessName;
	m_dwPort = dwPort;
	m_fltPing = FLT_MAX;
}
/////////////////////////////////////////////////////////////////////////////
FLOAT CPingThread::GetPing()
{
	return m_fltPing;
}
/////////////////////////////////////////////////////////////////////////////
void CPingThread::RecordPing(FLOAT fltPing)
{
	m_pingHistory[m_pingHistoryIndex] = fltPing;
	m_pingHistoryIndex = (m_pingHistoryIndex + 1) % kPingHistorySize;
	if (m_pingHistoryCount < kPingHistorySize)
		m_pingHistoryCount++;
}
/////////////////////////////////////////////////////////////////////////////
FLOAT CPingThread::GetSmoothedPing()
{
	if (m_pingHistoryCount <= 0)
		return FLT_MAX;

	FLOAT sum = 0.0f;
	for (int i = 0; i < m_pingHistoryCount; i++)
		sum += m_pingHistory[i];

	return sum / m_pingHistoryCount;
}
/////////////////////////////////////////////////////////////////////////////
BOOL CPingThread::FindGameServerEndpoint(char* szAddr, int cchAddr, DWORD* pdwPort)
{
	if (!szAddr || cchAddr <= 0 || !pdwPort)
		return FALSE;

	DWORD pid = 0;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return FALSE;

	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	if (Process32First(snap, &entry))
	{
		do
		{
			if (!_stricmp(entry.szExeFile, m_strProcessName))
			{
				pid = entry.th32ProcessID;
				break;
			}
		} while (Process32Next(snap, &entry));
	}

	CloseHandle(snap);

	if (!pid)
	{
		APPEND_LOG1("Process not found: %s", (LPCSTR)m_strProcessName);
		return FALSE;
	}

	DWORD dwRetVal = 0;
	ULONG ulSize = 0;
	MIB_TCPTABLE2* pTcpTable = (MIB_TCPTABLE2*)malloc(sizeof(MIB_TCPTABLE2));
	if (!pTcpTable)
		return FALSE;

	ulSize = sizeof(MIB_TCPTABLE2);
	if ((dwRetVal = GetTcpTable2(pTcpTable, &ulSize, TRUE)) == ERROR_INSUFFICIENT_BUFFER)
	{
		free(pTcpTable);
		pTcpTable = (MIB_TCPTABLE2*)malloc(ulSize);
		if (!pTcpTable)
			return FALSE;
	}

	BOOL found = FALSE;
	if ((dwRetVal = GetTcpTable2(pTcpTable, &ulSize, TRUE)) == NO_ERROR)
	{
		for (DWORD i = 0; i < pTcpTable->dwNumEntries; i++)
		{
			if (pTcpTable->table[i].dwOwningPid == pid)
			{
				DWORD remotePort = ntohs((u_short)pTcpTable->table[i].dwRemotePort);
				if (remotePort == m_dwPort && pTcpTable->table[i].dwState == MIB_TCP_STATE_ESTAB)
				{
					struct in_addr ipAddr;
					ipAddr.S_un.S_addr = (u_long)pTcpTable->table[i].dwRemoteAddr;
					strncpy_s(szAddr, cchAddr, inet_ntoa(ipAddr), _TRUNCATE);
					*pdwPort = remotePort;
					found = TRUE;
					break;
				}
			}
		}
	}

	free(pTcpTable);

	if (!found)
		APPEND_LOG1("No ESTABLISHED TCP endpoint for port %d", m_dwPort);

	return found;
}
/////////////////////////////////////////////////////////////////////////////
BOOL CPingThread::FindExitLagEndpoint(char* szAddr, int cchAddr, DWORD* pdwPort, BOOL* pbIpv6)
{
	if (!szAddr || cchAddr <= 0 || !pdwPort || !pbIpv6)
		return FALSE;

	DWORD pid = 0;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return FALSE;

	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	if (Process32First(snap, &entry))
	{
		do
		{
			if (!_stricmp(entry.szExeFile, "ExitLag.exe"))
			{
				pid = entry.th32ProcessID;
				break;
			}
		} while (Process32Next(snap, &entry));
	}

	CloseHandle(snap);

	if (!pid)
		return FALSE;

	FLOAT bestPing = -1.0f;
	char bestAddr[64] = { 0 };
	DWORD bestPort = 0;
	BOOL bestIpv6 = FALSE;
	int tested = 0;

	DWORD dwRetVal = 0;
	ULONG ulSize = 0;
	MIB_TCPTABLE2* pTcpTable = (MIB_TCPTABLE2*)malloc(sizeof(MIB_TCPTABLE2));
	if (pTcpTable)
	{
		ulSize = sizeof(MIB_TCPTABLE2);
		if ((dwRetVal = GetTcpTable2(pTcpTable, &ulSize, TRUE)) == ERROR_INSUFFICIENT_BUFFER)
		{
			free(pTcpTable);
			pTcpTable = (MIB_TCPTABLE2*)malloc(ulSize);
		}

		if (pTcpTable && (dwRetVal = GetTcpTable2(pTcpTable, &ulSize, TRUE)) == NO_ERROR)
		{
			for (DWORD i = 0; i < pTcpTable->dwNumEntries; i++)
			{
				if (pTcpTable->table[i].dwOwningPid == pid && pTcpTable->table[i].dwState == MIB_TCP_STATE_ESTAB)
				{
					DWORD remotePort = ntohs((u_short)pTcpTable->table[i].dwRemotePort);
					if (remotePort == 443)
						continue;

					struct in_addr ipAddr;
					ipAddr.S_un.S_addr = (u_long)pTcpTable->table[i].dwRemoteAddr;
					const char* addrStr = inet_ntoa(ipAddr);
					if (!addrStr || !*addrStr)
						continue;

					FLOAT ping = FLT_MAX;
					if (MeasureTcpPing(addrStr, remotePort, &ping))
					{
						tested++;
						if (ping > bestPing)
						{
							strncpy_s(bestAddr, sizeof(bestAddr), addrStr, _TRUNCATE);
							bestPort = remotePort;
							bestPing = ping;
							bestIpv6 = FALSE;
						}
					}

					if (tested >= 20)
						break;
				}
			}
		}

		free(pTcpTable);
	}

	PMIB_TCP6TABLE2 pTcp6Table = NULL;
	ulSize = 0;
	if (GetTcp6Table2(pTcp6Table, &ulSize, TRUE) == ERROR_INSUFFICIENT_BUFFER)
	{
		pTcp6Table = (MIB_TCP6TABLE2*)malloc(ulSize);
		if (pTcp6Table && (dwRetVal = GetTcp6Table2(pTcp6Table, &ulSize, TRUE)) == NO_ERROR)
		{
			for (DWORD i = 0; i < pTcp6Table->dwNumEntries; i++)
			{
				if (pTcp6Table->table[i].dwOwningPid == pid && pTcp6Table->table[i].State == MIB_TCP_STATE_ESTAB)
				{
					DWORD remotePort = ntohs((u_short)pTcp6Table->table[i].dwRemotePort);
					if (remotePort == 443)
						continue;

					char addrStr[64] = { 0 };
					sockaddr_in6 sa6;
					ZeroMemory(&sa6, sizeof(sa6));
					sa6.sin6_family = AF_INET6;
					sa6.sin6_addr = pTcp6Table->table[i].RemoteAddr;
					DWORD addrLen = sizeof(addrStr);
					if (WSAAddressToStringA((LPSOCKADDR)&sa6, sizeof(sa6), NULL, addrStr, &addrLen) != 0)
						continue;
					if (!addrStr[0])
						continue;

					FLOAT ping = FLT_MAX;
					if (MeasureTcpPing6(addrStr, remotePort, &ping))
					{
						tested++;
						if (ping > bestPing)
						{
							strncpy_s(bestAddr, sizeof(bestAddr), addrStr, _TRUNCATE);
							bestPort = remotePort;
							bestPing = ping;
							bestIpv6 = TRUE;
						}
					}

					if (tested >= 20)
						break;
				}
			}
		}
	}

	if (pTcp6Table)
		free(pTcp6Table);

	if (bestPing >= 0.0f)
	{
		strncpy_s(szAddr, cchAddr, bestAddr, _TRUNCATE);
		*pdwPort = bestPort;
		*pbIpv6 = bestIpv6;
		APPEND_LOG2("ExitLag endpoint: %s:%d", bestAddr, bestPort);
		return TRUE;
	}

	APPEND_LOG("ExitLag endpoint not found");
	return FALSE;
}
/////////////////////////////////////////////////////////////////////////////
BOOL CPingThread::MeasureTcpPing(LPCSTR lpAddr, DWORD dwPort, FLOAT* pOutMs)
{
	if (!lpAddr || !pOutMs)
		return FALSE;

	SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == INVALID_SOCKET)
	{
		APPEND_LOG("Failed to create socket");
		return FALSE;
	}

	u_long nonBlocking = 1;
	ioctlsocket(s, FIONBIO, &nonBlocking);

	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_port = htons((u_short)dwPort);
	server.sin_addr.s_addr = inet_addr(lpAddr);

	LARGE_INTEGER start, end;
	QueryPerformanceCounter(&start);
	int result = connect(s, (struct sockaddr*)&server, sizeof(server));

	if (result == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK)
		{
			fd_set writeSet;
			FD_ZERO(&writeSet);
			FD_SET(s, &writeSet);

			TIMEVAL tv;
			tv.tv_sec = 2;
			tv.tv_usec = 0;

			int sel = select(0, NULL, &writeSet, NULL, &tv);
			if (sel > 0 && FD_ISSET(s, &writeSet))
			{
				int so_error = 0;
				int optlen = sizeof(so_error);
				getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&so_error, &optlen);
				if (so_error != 0)
				{
					APPEND_LOG1("Connect failed, SO_ERROR=%d", so_error);
					closesocket(s);
					return FALSE;
				}
			}
			else
			{
				APPEND_LOG("Connect timed out");
				closesocket(s);
				return FALSE;
			}
		}
		else
		{
			APPEND_LOG1("Connect failed, WSA error=%d", err);
			closesocket(s);
			return FALSE;
		}
	}

	QueryPerformanceCounter(&end);
	closesocket(s);

	double elapsedMs = ((double)(end.QuadPart - start.QuadPart) * 1000.0) / (double)m_qwFreq.QuadPart;
	*pOutMs = (FLOAT)elapsedMs;

	return TRUE;
}
/////////////////////////////////////////////////////////////////////////////
BOOL CPingThread::MeasureTcpPing6(LPCSTR lpAddr, DWORD dwPort, FLOAT* pOutMs)
{
	if (!lpAddr || !pOutMs)
		return FALSE;

	SOCKET s = socket(AF_INET6, SOCK_STREAM, 0);
	if (s == INVALID_SOCKET)
		return FALSE;

	u_long nonBlocking = 1;
	ioctlsocket(s, FIONBIO, &nonBlocking);

	struct sockaddr_in6 server6;
	ZeroMemory(&server6, sizeof(server6));
	server6.sin6_family = AF_INET6;
	server6.sin6_port = htons((u_short)dwPort);
	if (inet_pton(AF_INET6, lpAddr, &server6.sin6_addr) != 1)
	{
		closesocket(s);
		return FALSE;
	}

	LARGE_INTEGER start, end;
	QueryPerformanceCounter(&start);
	int result = connect(s, (struct sockaddr*)&server6, sizeof(server6));

	if (result == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK)
		{
			fd_set writeSet;
			FD_ZERO(&writeSet);
			FD_SET(s, &writeSet);

			TIMEVAL tv;
			tv.tv_sec = 2;
			tv.tv_usec = 0;

			int sel = select(0, NULL, &writeSet, NULL, &tv);
			if (sel > 0 && FD_ISSET(s, &writeSet))
			{
				int so_error = 0;
				int optlen = sizeof(so_error);
				getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&so_error, &optlen);
				if (so_error != 0)
				{
					closesocket(s);
					return FALSE;
				}
			}
			else
			{
				closesocket(s);
				return FALSE;
			}
		}
		else
		{
			closesocket(s);
			return FALSE;
		}
	}

	QueryPerformanceCounter(&end);
	closesocket(s);

	double elapsedMs = ((double)(end.QuadPart - start.QuadPart) * 1000.0) / (double)m_qwFreq.QuadPart;
	*pOutMs = (FLOAT)elapsedMs;

	return TRUE;
}

