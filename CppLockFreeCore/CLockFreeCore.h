#pragma once
#ifndef __CLOCKFREECORE__
#define __CLOCKFREECORE__
#define  _WINSOCK_DEPRECATED_NO_WARNINGS
#pragma comment (lib,"ws2_32")
#include <WinSock2.h>
#include <cstdio>
#include "CRingBuff.h"
#include "CPacket.h"
#include "LockFreeStack.h"
#include "Map.h"
using namespace std;

#define DEFAULT_BUFFER_SIZE 30000

// 서버 시작 시 넘기는 옵션 묶음.
struct ServerOption
{
	WCHAR ip[20] = L"127.0.0.1";
	int port = 6000;
	int workerMax = 8;   // 워커(IO 완료 처리) 스레드 수
	int runMax = 0;      // IOCP 동시 실행 스레드 수. 0이면 CPU 코어 수만큼
	bool nagle = true;
	int sessionMax = 1000;
};

// GetQueuedCompletionStatus로 완료 통지를 받을 때, 그게 recv 완료인지 send 완료인지 구분하는 용도.
struct OverlappedEx
{
	OVERLAPPED overlapped;
	int type; // 0 = recv, 1 = send
};

// 접속 하나당 하나씩 대응되는 세션 정보.
struct Session
{

	__int64 m_sessionId;
	SOCKET m_socket;
	CRingBuff m_recvRingBuff;
	CRingBuff m_sendRingBuff;
	OverlappedEx m_recvOverlapped;
	OverlappedEx m_sendOverlapped;
	int m_ioCount; // 지금 진행 중인 send/recv 개수. 0이 돼야 세션을 완전히 정리할 수 있다.

	// Interlocked 계열 함수는 4바이트(LONG) 단위로 읽고 쓴다. bool(1바이트)에 (LONG*)로
	// 캐스팅해서 쓰면 뒤에 붙은 다른 멤버 바이트까지 같이 건드리는 미정의 동작이 나므로,
	// Interlocked로 다룰 플래그는 반드시 LONG으로 선언한다.
	volatile LONG m_isSendPending;
	bool m_isSessionOn;

	// 세션 종료 레이스 컨디션 방지용 (removeSession ↔ sendPost/recvPost 동시 접근 가드).
	// m_isClosing  : "더 이상 새 IO를 걸지 마라"는 신호. sendPost/recvPost는 IoCount 증가 직후 이 값을 재확인한다.
	// m_isTornDown : closesocket 등 실제 정리를 "정확히 한 번만" 수행하도록 보장하는 CAS 가드.
	volatile LONG m_isClosing;
	volatile LONG m_isTornDown;

	int m_sendPacketCount = 0;

	// 세션 프리리스트(LockFreeStack<Session*>)가 체인 연결에 쓰는 next 포인터.
	// 세션 사용 중엔 의미 없고, 프리리스트에 반납돼 있는 동안만 유효하다.
	Session* next;

	Session()
	{
		m_isSessionOn = false;
		m_isClosing = 0;
		m_isTornDown = 0;
		next = nullptr;
	};
};



class CLockFreeCore
{
public:
	CLockFreeCore();
	~CLockFreeCore();

	bool start(ServerOption); // IOCP 초기화 + 워커/accept 스레드 기동 + listen까지 한 번에 처리
	void stop();
	int getSessionCount();

	bool disconnect(__int64 sessionId);
	bool sendPacket(__int64 sessionId, CPacket*);

	// 사용자 콜백. 상속받은 클래스에서 재정의해서 쓴다.
	virtual bool onConnectionRequest(WCHAR* ip, int port) { return true; } // accept 직후 호출, false 반환 시 접속 거부(IP 차단용)
	virtual void onClientJoin(__int64 sessionId, Session* session) {}     // 세션 등록 완료 후 호출
	virtual void onClientLeave(__int64 sessionId) {}                      // 세션 정리 완료 후 호출
	virtual void onRecv(__int64 sessionId, CPacket*) = 0;                 // 패킷 한 개 수신 완료마다 호출
	virtual void onError(int errorCode, const WCHAR* msg) {}              // 소켓/시스템 콜 에러 발생 시 호출

private:
	WCHAR m_ip[20];
	int m_port;
	int m_workerMax;
	int m_runMax;
	bool m_nagle;
	int m_sessionMax; // 0이면 제한 없음

	// 세션 관리: 배열을 미리 통째로 만들어두고(m_sessionPool), 사용 여부는
	// LockFreeStack(프리리스트)과 Map(활성 세션 조회) 2개로 나눠서 관리한다.
	Session* m_sessionPool;                    // 세션 객체 실 메모리(m_sessionMax개), start()에서 일괄 할당
	LockFreeStack<Session*> m_sessionFreeList;  // 비어있는(미사용) 세션 프리리스트
	Map<__int64, Session*> m_sessionMap;        // 활성 세션ID -> Session* 조회용. m_criticalSection으로 보호
	SOCKET m_listenSocket;
	HANDLE m_hcp;
	__int64 m_nextSessionId=0;
	HANDLE* m_workerThreadHandles; // 워커 스레드(workerMax개) + acceptThread(1개), start()에서 동적 할당
	CRITICAL_SECTION m_criticalSection; // m_sessionMap 접근 보호용

	static DWORD WINAPI workerThread(LPVOID); // IOCP 완료 통지 처리 루프 (recv/send 완료마다 여기서 처리)
	static DWORD WINAPI acceptThread(LPVOID); // accept 전용 루프

	Session* findSession(__int64);

	// bMarkClosing=true  : 연결 종료를 실제로 감지한 지점(recv 0바이트, IO 실패 등)에서 호출 - 세션을 "닫는 중"으로 표시한다.
	// bMarkClosing=false : 정상 완료 후의 폴링 호출 - 이미 닫는 중인 세션의 IoCount가 0이 됐는지 확인만 한다.
	void removeSession(__int64 sessionId, bool bMarkClosing);

	void recvPost(Session*); // 해당 세션에 WSARecv 건다
	void sendPost(Session*); // 해당 세션의 send 큐를 모아서 WSASend 건다
};




#endif
