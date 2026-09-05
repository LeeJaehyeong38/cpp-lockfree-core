// EchoServer.cpp : CppLockFreeCore 라이브러리를 사용하는 에코 서버.
//

#include "CLockFreeCore.h"
#include <iostream>
#include <cmath>

// ----------------------------------------------------------------------------
// 서버가 실제로 떠 있는 상태에서 진짜 소켓으로 접속해보는 네트워크 레이어 회귀 테스트.
// 서버 기동 직후 한 번 돌려서, 기본 에코/패킷 재조립/비정상 패킷 방어가 여전히 되는지 확인한다.
// ----------------------------------------------------------------------------

// 127.0.0.1:6000으로 접속해서 소켓을 만든다. 실패하면 INVALID_SOCKET.
static SOCKET connectToServer()
{
	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
	SOCKADDR_IN addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(6000);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (connect(sock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR)
	{
		closesocket(sock);
		return INVALID_SOCKET;
	}

	DWORD recvTimeout = 3000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeout, sizeof(recvTimeout));
	return sock;
}

// 정상 패킷을 한 번에 보내고, 보낸 값 그대로 에코가 돌아오는지 확인한다.
static bool testEcho()
{
	SOCKET sock = connectToServer();
	if (sock == INVALID_SOCKET)
	{
		printf("[에코 테스트] connect 실패: %d\n", WSAGetLastError());
		return false;
	}

	short header = 8;
	double value = 1.2345;
	char sendBuf[10];
	memcpy(sendBuf, &header, 2);
	memcpy(sendBuf + 2, &value, 8);
	send(sock, sendBuf, 10, 0);

	char recvBuf[10];
	int totalRecv = 0;
	while (totalRecv < 10)
	{
		int n = recv(sock, recvBuf + totalRecv, 10 - totalRecv, 0);
		if (n <= 0) break;
		totalRecv += n;
	}

	bool ok = false;
	if (totalRecv == 10)
	{
		short respHeader;
		double respValue;
		memcpy(&respHeader, recvBuf, 2);
		memcpy(&respValue, recvBuf + 2, 8);
		ok = (respHeader == 8 && fabs(respValue - 1.2345) < 0.0001);
	}
	printf("[네트워크 테스트: 기본 에코] 결과: %s (받은 바이트=%d)\n", ok ? "성공" : "실패", totalRecv);
	fflush(stdout); // 콘솔 종류에 따라 라인버퍼링이 기대대로 안 먹는 경우가 있어 확실하게 밀어낸다
	closesocket(sock);
	return ok;
}

// 논리적으로 하나인 패킷을 헤더/페이로드로 쪼개서 시간차를 두고 보낸다.
// 서버의 TCP 재조립 로직이 완전한 패킷이 쌓일 때까지 제대로 기다리는지 확인한다.
static bool testFragmentedPacket()
{
	SOCKET sock = connectToServer();
	if (sock == INVALID_SOCKET)
	{
		printf("[네트워크 테스트: 조각 패킷] connect 실패: %d\n", WSAGetLastError());
		return false;
	}

	short header = 8;
	double value = 9.8765;
	send(sock, (char*)&header, 2, 0); // 헤더만 먼저 보낸다
	Sleep(200);
	send(sock, (char*)&value, 8, 0);  // 페이로드는 나중에 - 서버는 recv 두 번에 걸쳐 조립해야 한다

	char recvBuf[10];
	int totalRecv = 0;
	while (totalRecv < 10)
	{
		int n = recv(sock, recvBuf + totalRecv, 10 - totalRecv, 0);
		if (n <= 0) break;
		totalRecv += n;
	}

	bool ok = false;
	if (totalRecv == 10)
	{
		short respHeader;
		double respValue;
		memcpy(&respHeader, recvBuf, 2);
		memcpy(&respValue, recvBuf + 2, 8);
		ok = (respHeader == 8 && fabs(respValue - 9.8765) < 0.0001);
	}
	printf("[네트워크 테스트: 조각 패킷 재조립] 결과: %s (받은 바이트=%d)\n", ok ? "성공" : "실패", totalRecv);
	fflush(stdout);
	closesocket(sock);
	return ok;
}

// MAX_PACKET_SIZE를 넘는 헤더값을 보내서, 서버가 죽지 않고 해당 세션만 안전하게 끊는지 확인한다.
static bool testOversizedHeaderRejected()
{
	SOCKET sock = connectToServer();
	if (sock == INVALID_SOCKET)
	{
		printf("[네트워크 테스트: 비정상 헤더 방어] connect 실패: %d\n", WSAGetLastError());
		return false;
	}

	short badHeader = 5000; // MAX_PACKET_SIZE(4000) 초과
	send(sock, (char*)&badHeader, 2, 0);

	char buf[1];
	int n = recv(sock, buf, 1, 0);
	// 서버가 정상 방어했다면 페이로드 없이 그냥 연결을 끊는다 (recv 0 또는 에러)
	bool ok = (n <= 0);
	printf("[네트워크 테스트: 비정상 헤더 방어] 결과: %s (recv 반환값=%d)\n", ok ? "성공" : "실패", n);
	fflush(stdout);
	closesocket(sock);
	return ok;
}

// 위 3개 테스트를 순서대로 돌리고 결과를 종합해서 찍는다.
static void runNetworkLayerTests()
{
	Sleep(300); // 서버가 listen 상태가 될 시간을 준다
	printf("===== 네트워크 레이어 테스트 시작 =====\n");
	fflush(stdout);
	bool echoOk = testEcho();
	bool fragOk = testFragmentedPacket();
	bool rejectOk = testOversizedHeaderRejected();
	printf("===== 네트워크 레이어 테스트 종료 (에코: %s / 조각재조립: %s / 비정상헤더방어: %s) =====\n",
		echoOk ? "성공" : "실패", fragOk ? "성공" : "실패", rejectOk ? "성공" : "실패");
	fflush(stdout);
}

// ----------------------------------------------------------------------------
// 실제 에코 서버 본체.
// 접속/해제/패킷 하나하나마다 로그를 찍으면 부하테스트 중(CCU 수천 단위)엔 콘솔 출력 자체가
// 병목이 된다. 그래서 이벤트 단위 로그 대신, 아래 statusReporterThread가 "현재 접속자 수 /
// 최근 처리량" 같은 집계 상태만 주기적으로 찍는다.
// ----------------------------------------------------------------------------

static volatile LONGLONG g_totalPacketsProcessed = 0; // 지금까지 처리한 총 패킷 수 (상태 리포터가 읽음)

class TestServer:public CLockFreeCore
{
public:
    void onRecv(__int64 sessionId, CPacket*) override;

private:

};

// 받은 double 값을 그대로 돌려보낸다 (에코).
void TestServer::onRecv(__int64 sessionId, CPacket* pack)
{
    CPacket* _pack = new CPacket(10);

    double d;
    (*pack) >> d;
    (*_pack) << d;

    InterlockedIncrement64(&g_totalPacketsProcessed);
    sendPacket(sessionId, _pack);
}

static const int STATUS_INTERVAL_SEC = 2;

struct StatusReporterArg
{
	CLockFreeCore* server;
};

// STATUS_INTERVAL_SEC마다 "현재 접속자 수 / 그 사이 처리한 패킷 수"를 찍는 상태 리포터 스레드.
static DWORD WINAPI statusReporterThread(LPVOID arg)
{
	CLockFreeCore* server = ((StatusReporterArg*)arg)->server;
	LONGLONG lastCount = 0;
	while (true)
	{
		Sleep(STATUS_INTERVAL_SEC * 1000);
		LONGLONG now = g_totalPacketsProcessed;
		LONGLONG delta = now - lastCount;
		lastCount = now;
		printf("[상태] 접속자 수=%d  최근 %d초간 처리=%lld건 (초당 약 %lld건)\n",
			server->getSessionCount(), STATUS_INTERVAL_SEC, delta, delta / STATUS_INTERVAL_SEC);
		fflush(stdout);
	}
}

int main()
{
    setvbuf(stdout, NULL, _IOLBF, 1024); // 콘솔로 redirect될 때도 로그가 실시간으로 보이도록 라인버퍼링

    TestServer s;
    ServerOption aa;
    aa.sessionMax = 6000; // 봇 부하테스트(최대 5000 CCU)를 지원하기 위해 기본값(1000)보다 여유있게 설정
    s.start(aa);

    runNetworkLayerTests();

    static StatusReporterArg reporterArg;
    reporterArg.server = &s;
    CreateThread(NULL, 0, statusReporterThread, &reporterArg, 0, NULL);

    while (true)
    {
        Sleep(1000);
    }
}
