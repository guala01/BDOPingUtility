#ifndef _PINGTHREAD_H_
#define _PINGTHREAD_H_
/////////////////////////////////////////////////////////////////////////////
#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
/////////////////////////////////////////////////////////////////////////////
#include <winsock2.h>
/////////////////////////////////////////////////////////////////////////////
// CPingThread thread
/////////////////////////////////////////////////////////////////////////////
class CPingThread : public CWinThread
{
	DECLARE_DYNCREATE(CPingThread)
protected:
	CPingThread();           // protected constructor used by dynamic creation
private:
	enum { kPingHistorySize = 5 };

// Attributes
public:
	CPingThread(LPCSTR lpProcessName, DWORD dwPort);

// Operations
public:
	void	Kill();
	void	Destroy();

	void	SetTarget(LPCSTR lpProcessName, DWORD dwPort);

	void	UpdatePingAsync();
	void	UpdatePing();
	FLOAT	GetPing();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPingThread)
	public:
	virtual BOOL	InitInstance();
	virtual int		ExitInstance();
	//}}AFX_VIRTUAL

// Implementation
protected:
	CString		m_strProcessName;
	DWORD		m_dwPort;
	CString		m_strCachedAddr;
	DWORD		m_dwCachedPort;
	BOOL		m_bCachedIpv6;
	HANDLE		m_hEventKill;
	HANDLE		m_hEventAsyncPing;
	FLOAT		m_fltPing;
	FLOAT		m_fltPingRaw;
	FLOAT		m_pingHistory[kPingHistorySize];
	int			m_pingHistoryCount;
	int			m_pingHistoryIndex;
	WSADATA		m_wsaData;
	BOOL		m_bWsaReady;
	LARGE_INTEGER m_qwFreq;
	ULONGLONG	m_dwLastPingTick;
	ULONGLONG	m_dwLastResolveTick;

	virtual ~CPingThread();

	BOOL	FindGameServerEndpoint(char* szAddr, int cchAddr, DWORD* pdwPort);
	BOOL	FindExitLagEndpoint(char* szAddr, int cchAddr, DWORD* pdwPort, BOOL* pbIpv6);
	BOOL	MeasureTcpPing(LPCSTR lpAddr, DWORD dwPort, FLOAT* pOutMs);
	BOOL	MeasureTcpPing6(LPCSTR lpAddr, DWORD dwPort, FLOAT* pOutMs);
	void	RecordPing(FLOAT fltPing);
	FLOAT	GetSmoothedPing();

	// Generated message map functions
	//{{AFX_MSG(CPingThread)
		// NOTE - the ClassWizard will add and remove member functions here.
	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()
};
/////////////////////////////////////////////////////////////////////////////
//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.
/////////////////////////////////////////////////////////////////////////////
#endif // !defined(AFX_SAVEVIDEITHREAD_H__D90467AA_68B1_4B74_83FD_BD3FCF6EF1E4__INCLUDED_)
/////////////////////////////////////////////////////////////////////////////
