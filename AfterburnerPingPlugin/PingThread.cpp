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

		if (!FindGameServerEndpoint(szAddr, sizeof(szAddr), &dwPort))
		{
			m_fltPing = FLT_MAX;
			return;
		}

		m_strCachedAddr = szAddr;
		m_dwCachedPort = dwPort;
		m_dwLastResolveTick = now;
	}

	FLOAT fltPing = FLT_MAX;
	if (MeasureTcpPing(m_strCachedAddr, m_dwCachedPort, &fltPing))
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
		return FALSE;

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

	return found;
}
/////////////////////////////////////////////////////////////////////////////
BOOL CPingThread::MeasureTcpPing(LPCSTR lpAddr, DWORD dwPort, FLOAT* pOutMs)
{
	if (!lpAddr || !pOutMs)
		return FALSE;

	SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == INVALID_SOCKET)
		return FALSE;

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

