#include "CLockFreeCore.h"


CLockFreeCore::CLockFreeCore()
{
}

CLockFreeCore::~CLockFreeCore()
{
}


// IOCP 완료 통지를 큐에서 하나씩 뽑아서 처리하는 워커 스레드. start()에서 workerMax개 만큼 띄운다.
DWORD WINAPI CLockFreeCore::workerThread(LPVOID arg)
{
	CLockFreeCore* server=(CLockFreeCore*)arg;

	HANDLE hcp = server->m_hcp;

	while (1)
	{
		DWORD cbTransferred = 0;
		Session* client_Session = NULL;
		OverlappedEx* ptr = NULL;

		GetQueuedCompletionStatus(hcp, &cbTransferred, (PULONG_PTR)&client_Session, (LPOVERLAPPED*)&ptr, INFINITE);

		if (client_Session == NULL)
		{
			server->onError(0, L"완료 통지에 세션 정보가 없음");
			continue;
		}

		// ptr이 NULL이면 이 IO 자체가 실패했다는 뜻 - 세션을 종료 처리한다.
		if (ptr == NULL)
		{
			server->onError(0, L"IO 작업 실패 또는 타임아웃");
			server->removeSession(client_Session->m_sessionId, true);
			continue;

		}

		// cbTransferred가 0이면 상대방이 연결을 끊었다는 뜻(recv/send 둘 다 마찬가지) - 정상적인 종료 경로다.
		if (cbTransferred == 0)
		{
			InterlockedDecrement((LONG*)&client_Session->m_ioCount);
			server->removeSession(client_Session->m_sessionId, true);
			continue;
		}

		// recv 완료 - 링버퍼에 쌓인 걸 패킷 단위로 잘라서 onRecv로 넘긴다.
		if (ptr->type == 0)
		{
			client_Session->m_recvRingBuff.moveRearPos(cbTransferred);

			// TCP 패킷 프레이밍(재조립).
			// TCP는 "바이트 스트림"이라 recv 한 번에 패킷 여러 개가 붙어서 오기도 하고,
			// 반대로 패킷 하나가 여러 recv에 걸쳐 잘려서 오기도 한다. 그래서 패킷 앞에
			// "뒤에 몇 바이트짜리 페이로드가 오는지"를 담은 2바이트 헤더(HEADER_SIZE)를
			// 붙여서 패킷 경계를 스스로 표시하게 했다.
			bool protocolError = false;
			while (client_Session->m_recvRingBuff.getUseSize() >= HEADER_SIZE)
			{
				short header;
				client_Session->m_recvRingBuff.peek((char*)&header, HEADER_SIZE);

				// 헤더값은 클라이언트가 마음대로 채워 보낼 수 있는 값이라 그대로 믿으면 안 된다.
				// 검증 없이 그 크기만큼 버퍼에 dequeue하면 조작된 헤더값으로 힙 오버플로우가
				// 날 수 있어서, 상한선을 넘으면 그 세션만 끊는다.
				if (header < 0 || header > MAX_PACKET_SIZE)
				{
					server->onError(0, L"비정상 패킷 헤더 크기");
					server->removeSession(client_Session->m_sessionId, true);
					protocolError = true;
					break;
				}

				int buffsize = client_Session->m_recvRingBuff.getUseSize();
				if (header + HEADER_SIZE > buffsize)
				{
					// 페이로드가 아직 다 안 왔다 - 다음 recv 완료 때 이어서 채워지길 기다린다.
					break;
				}

				// 헤더가 선언한 크기만큼만 정확히 담을 수 있게 그때그때 패킷을 만든다.
				CPacket _pack(HEADER_SIZE + header);
				client_Session->m_recvRingBuff.dequeue(_pack.getHeaderBuffer(), HEADER_SIZE+header);
				_pack.setCur(header + HEADER_SIZE);

				server->onRecv(client_Session->m_sessionId, &_pack);
			}

			if (!protocolError)
				server->recvPost(client_Session);
		}

		// send 완료 - 보낸 만큼 send 큐에서 빼고, 남은 게 있으면 이어서 또 보낸다.
		if (ptr->type == 1)
		{
			for (int i = 0; i < client_Session->m_sendPacketCount; i++)
			{
				CPacket* c=NULL;

				if (client_Session->m_sendRingBuff.getUseSize() < sizeof(CPacket*))
				{
					server->onError(0, L"send 큐 dequeue 크기 불일치");
				}
				client_Session->m_sendRingBuff.dequeue((char*)&c, sizeof(CPacket*));
				if(c !=NULL)
					delete c;
			}
			client_Session->m_isSendPending = false;
			if (client_Session->m_sendRingBuff.getUseSize() > 0 && client_Session->m_isSendPending == false)
			{
				server->sendPost(client_Session);
			}
		}

		InterlockedDecrement((LONG*)&client_Session->m_ioCount);
		// 정상 완료 후 폴링 호출 - 이 세션이 이미 종료 처리 중(m_isClosing)이고 IoCount가 0이 된
		// 경우에만 removeSession 내부에서 실제 정리가 이뤄진다. 그 외엔 즉시 리턴하니 매번 불러도 안전하다.
		server->removeSession(client_Session->m_sessionId, false);
	}

	// 워커 쓰레드는 stop()에서 종료 신호를 받아야 여기까지 빠져나온다 - 정상적인 서버 종료 흐름이라
	// 콘솔에 찍을 필요는 없다.
	return 0;
}

// accept 전용 루프. 접속 하나마다 프리리스트에서 세션을 하나 꺼내서 초기화하고 recv를 건다.
DWORD WINAPI CLockFreeCore::acceptThread(LPVOID arg)
{
	CLockFreeCore* server = (CLockFreeCore*)arg;
	while (true)
	{
		SOCKET Sock;
		SOCKADDR_IN sockaddr;
		int addrlen = sizeof(sockaddr);

		Sock = accept(server->m_listenSocket, (SOCKADDR*)&sockaddr, &addrlen);

		if (Sock == INVALID_SOCKET)
		{
			server->onError(WSAGetLastError(), L"accept 실패");
			break;
		}

		// accept 직후 사용자 콜백에 ip 차단 여부를 물어본다.
		char* ipAnsi = inet_ntoa(sockaddr.sin_addr);
		WCHAR ipWide[20] = { 0 };
		MultiByteToWideChar(CP_ACP, 0, ipAnsi, -1, ipWide, 20);
		if (server->onConnectionRequest(ipWide, ntohs(sockaddr.sin_port)) == false)
		{
			closesocket(Sock);
			continue;
		}

		bool opt = server->m_nagle;
		int retval = setsockopt(Sock, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));
		if (retval == SOCKET_ERROR)
		{
			server->onError(WSAGetLastError(), L"setsockopt(TCP_NODELAY) 실패");
		}

		// 접속 자체를 로그로 남기고 싶으면 onClientJoin 콜백에서 하면 된다 -
		// 라이브러리가 매 접속마다 무조건 콘솔에 찍지는 않는다.

		// 세션 프리리스트에서 하나 꺼내온다. 없으면(전부 사용 중) 접속을 거부한다.
		Session* client = server->m_sessionFreeList.pop();
		if (client == NULL)
		{
			closesocket(Sock);
			continue;
		}

		// 세션 ID는 재사용 없이 계속 증가만 한다 - 세션 슬롯이 재활용돼도
		// 이전 ID를 들고 있는 쪽이 다른 세션을 잘못 건드리는 일이 없게 하기 위함.
		client->m_sessionId = server->m_nextSessionId;
		InterlockedIncrement64(&server->m_nextSessionId);

		client->m_socket = Sock;
		client->m_isSendPending = false;
		client->m_ioCount = 0;
		client->m_isClosing = 0;
		client->m_isTornDown = 0;
		client->m_recvRingBuff.init(DEFAULT_BUFFER_SIZE);
		client->m_sendRingBuff.init(DEFAULT_BUFFER_SIZE);
		client->m_isSessionOn = true;
		ZeroMemory(&client->m_recvOverlapped.overlapped, sizeof(client->m_recvOverlapped.overlapped));
		ZeroMemory(&client->m_sendOverlapped.overlapped, sizeof(client->m_sendOverlapped.overlapped));
		client->m_recvOverlapped.type = 0;
		client->m_sendOverlapped.type = 1;

		EnterCriticalSection(&server->m_criticalSection);
		server->m_sessionMap.insert(client->m_sessionId, client);
		LeaveCriticalSection(&server->m_criticalSection);

		CreateIoCompletionPort((HANDLE)client->m_socket, server->m_hcp, (ULONG_PTR)client, 0);

		server->onClientJoin(client->m_sessionId, client);

		server->recvPost(client);
	}

	return 0;
}

// 서버 기동. 세션풀/워커스레드/리슨소켓 준비까지 순서대로 다 한다.
bool CLockFreeCore::start(ServerOption opt)
{
	InitializeCriticalSection(&m_criticalSection);

	lstrcpyW( m_ip, opt.ip);
	m_port= opt.port;
	m_workerMax= opt.workerMax;
	m_runMax= opt.runMax;
	m_nagle= opt.nagle;
	m_sessionMax= opt.sessionMax;

	// 세션 객체를 미리 한 번에 통째로 할당해두고, 전부 프리리스트에 담아둔다.
	// 이후엔 접속마다 새로 new하지 않고 프리리스트에서 꺼내 쓰고(pop), 끊기면 반납한다(push).
	m_sessionPool = new Session[m_sessionMax];
	for (int i = 0; i < m_sessionMax; i++)
		m_sessionFreeList.push(&m_sessionPool[i]);

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		return false;
	}

	m_hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, m_runMax);
	if (m_hcp == NULL) return false;

	// 워커 스레드(workerMax개) + accept 스레드(1개) 자리를 만들어둔다.
	m_workerThreadHandles = new HANDLE[m_workerMax + 1];

	for (int i = 0; i < m_workerMax; i++)
	{
		m_workerThreadHandles[i] = CreateThread(NULL, 0, workerThread, this, 0, NULL);
		if (m_workerThreadHandles[i] == NULL)
			return false;
	}

	m_listenSocket = socket(AF_INET, SOCK_STREAM, 0);

	if (m_listenSocket == INVALID_SOCKET)
	{
		onError(WSAGetLastError(), L"listen socket 생성 실패");
		return false;
	}

	SOCKADDR_IN 	serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(m_port);

	int retval = ::bind(m_listenSocket, (SOCKADDR*)&serveraddr, sizeof(serveraddr));

	if (retval == SOCKET_ERROR)
	{
		onError(WSAGetLastError(), L"bind 실패");
		return false;
	}

	retval = listen(m_listenSocket, SOMAXCONN);

	if (retval == SOCKET_ERROR)
	{
		onError(WSAGetLastError(), L"listen 실패");
		return false;
	}

	m_workerThreadHandles[m_workerMax] = CreateThread(NULL, 0, acceptThread, this, 0, NULL);
	return true;
}

void CLockFreeCore::stop()
{
	delete[] m_sessionPool;
	delete[] m_workerThreadHandles;
}

int CLockFreeCore::getSessionCount()
{
	EnterCriticalSection(&m_criticalSection);
	int count = m_sessionMap.getCount();
	LeaveCriticalSection(&m_criticalSection);
	return count;
}

// 해당 세션에 새 WSARecv를 건다.
void CLockFreeCore::recvPost(Session* session)
{
	// 이미 종료 처리 중인 세션에는 새 Recv를 걸지 않는다.
	if (session->m_isClosing)
		return;

	DWORD recvbytes;
	DWORD flags = 0;
	WSABUF wsabuf;
	wsabuf.buf = session->m_recvRingBuff.getUseWriteBuffer();
	wsabuf.len = session->m_recvRingBuff.getDirectWriteSize();

	InterlockedIncrement((LONG*)&session->m_ioCount);

	// removeSession이 방금 위의 m_isClosing 체크와 이 증가 사이에 끼어들었을 수 있어 재확인한다.
	if (session->m_isClosing)
	{
		InterlockedDecrement((LONG*)&session->m_ioCount);
		return;
	}

	int retval = WSARecv(session->m_socket, &wsabuf, 1, &recvbytes, &flags, (OVERLAPPED*)&session->m_recvOverlapped, NULL);

	if (retval == SOCKET_ERROR)
	{
		int errorCode = WSAGetLastError();
		if (errorCode != ERROR_IO_PENDING)
		{
			InterlockedDecrement((LONG*)&session->m_ioCount);
			onError(errorCode, L"WSARecv 실패");
			removeSession(session->m_sessionId, true);
		}
	}
}

// 해당 세션의 send 큐(m_sendRingBuff에 쌓인 CPacket* 포인터들)를 모아서 한 번에 WSASend를 건다.
// 이미 send가 진행 중이면(m_isSendPending) 아무것도 안 하고 리턴 - 완료 통지가 오면
// workerThread에서 다시 이 함수를 불러서 남은 걸 이어 보낸다.
void CLockFreeCore::sendPost(Session* session)
{
	bool bSend= InterlockedExchange((LONG*)&session->m_isSendPending,(LONG)TRUE);
	if (bSend == false)
	{
		// 이미 종료 처리 중인 세션에는 새 Send를 걸지 않는다.
		if (session->m_isClosing)
		{
			InterlockedExchange((LONG*)&session->m_isSendPending, (LONG)FALSE);
			return;
		}

		DWORD cbTransferred;
		WSABUF wsabuf[1000];
		session->m_sendPacketCount = session->m_sendRingBuff.getUseSize() / sizeof(CPacket*);
		if (session->m_sendPacketCount > 1000)
			session->m_sendPacketCount = 1000;

		if (session->m_sendRingBuff.getUseSize() < sizeof(CPacket*) * session->m_sendPacketCount)
		{
			// getUseSize()로 이미 계산한 개수만큼 peek하는 건데 그새 모자라졌다는 건
			// send 큐 카운팅 어딘가 꼬였다는 뜻 - 정상 동작에서는 나오면 안 되는 상태다.
			onError(0, L"send 큐 크기 불일치");
		}

		CPacket* c[1000];

		session->m_sendRingBuff.peek((char*)&c, sizeof(CPacket*) * session->m_sendPacketCount);
		for (int i = 0; i < session->m_sendPacketCount; i++)
		{
			wsabuf[i].buf = c[i]->getHeaderBuffer();
			wsabuf[i].len = c[i]->getTotalUseSize();
		}
		InterlockedIncrement((LONG*)&session->m_ioCount);

		// removeSession이 IoCount 확인과 이 증가 사이에 끼어들었을 수 있어 재확인한다.
		if (session->m_isClosing)
		{
			InterlockedDecrement((LONG*)&session->m_ioCount);
			InterlockedExchange((LONG*)&session->m_isSendPending, (LONG)FALSE);
			return;
		}

		int retval = WSASend(session->m_socket, wsabuf, session->m_sendPacketCount, &cbTransferred, 0, (OVERLAPPED*)&session->m_sendOverlapped, NULL);

		if (retval == SOCKET_ERROR)
		{
			int errorCode = WSAGetLastError();
			if (errorCode != ERROR_IO_PENDING)
			{
				InterlockedDecrement((LONG*)&session->m_ioCount);
				InterlockedExchange((LONG*)&session->m_isSendPending, (LONG)FALSE);
				onError(errorCode, L"WSASend 실패");
				removeSession(session->m_sessionId, true);
			}
		}
	}
}

// 세션 종료 처리. bMarkClosing=true인 경우에만 실제로 "닫는 중" 표시를 새로 하고,
// 그 외엔 이미 닫는 중인 세션의 IoCount가 0이 됐는지 확인하는 폴링 호출이다.
void CLockFreeCore::removeSession(__int64 sessionId, bool bMarkClosing)
{
	Session* session = findSession(sessionId);
	if (session == NULL)
		return;

	if (bMarkClosing)
	{
		// 새 Send/Recv가 더 이상 시작되지 못하게 먼저 표시한다.
		// sendPost/recvPost는 IoCount 증가 직후 이 값을 재확인해서 스스로 물러난다.
		InterlockedExchange(&session->m_isClosing, 1);
	}

	if (!session->m_isClosing)
	{
		// 종료 신호가 없는 정상 완료 후 폴링 호출 - 할 일이 없다.
		return;
	}

	if (session->m_ioCount != 0)
	{
		// 아직 진행 중인 IO가 남아있다. 그 IO가 끝나서 IoCount를 낮추고
		// removeSession(id, false)를 다시 부를 때 마저 정리된다.
		return;
	}

	// closesocket 등 실제 정리는 정확히 한 번만 수행돼야 한다 - CAS로 선점한 스레드만 진행.
	if (InterlockedCompareExchange(&session->m_isTornDown, 1, 0) != 0)
		return;

	while (session->m_sendRingBuff.getUseSize() >= sizeof(CPacket*))
	{
		CPacket* c = NULL;
		session->m_sendRingBuff.dequeue((char*)&c, sizeof(CPacket*));
		if (c != NULL)
			delete c;
	}
	if (session->m_sendRingBuff.getUseSize() > 0)
		onError(0, L"세션 정리 중 send 큐가 완전히 비워지지 않음");

	closesocket(session->m_socket);
	onClientLeave(session->m_sessionId);
	session->m_isSessionOn = false;

	// 세션ID -> 세션 조회 테이블에서 지우고, 세션 객체는 프리리스트로 반납해서 재사용 가능하게 한다.
	EnterCriticalSection(&m_criticalSection);
	m_sessionMap.erase(session->m_sessionId);
	LeaveCriticalSection(&m_criticalSection);

	m_sessionFreeList.push(session);
}

bool CLockFreeCore::disconnect(__int64 sessionId)
{
	removeSession(sessionId, true);
	return true;
}

// 패킷을 세션의 send 큐에 넣고, 지금 send 중이 아니면 바로 sendPost를 건다.
bool CLockFreeCore::sendPacket(__int64 sessionId, CPacket* pack)
{
	Session* _session= findSession(sessionId);
	if (_session == NULL)
		return false;

	short s = pack->getUseSize();
	pack->setHeader((char*)&s);
	if(_session->m_sendRingBuff.getFreeSize()>= sizeof(CPacket*))
	{
		_session->m_sendRingBuff.enqueue((char*)&pack, sizeof(CPacket*));
	}
	else
	{
		// send 큐가 꽉 찼다 - 예전엔 여기서 pack을 버리지도 delete하지도 않고 그냥 넘어가서
		// 메모리 누수 + 호출한 쪽은 실패한 줄도 모르는 상태였다. 지금은 정리하고 실패를 알린다.
		onError(0, L"send 큐가 가득 참 - 패킷 버림");
		delete pack;
		return false;
	}

	if (_session->m_isSendPending == false)
	{
		sendPost(_session);
	}
	return true;
}

// 세션ID로 세션을 찾는다. Map(RB-Tree)이 스레드 세이프하지 않아서 m_criticalSection으로 감싼다.
Session* CLockFreeCore::findSession(__int64 sessionId)
{
	Session* result = NULL;
	EnterCriticalSection(&m_criticalSection);
	m_sessionMap.find(sessionId, result);
	LeaveCriticalSection(&m_criticalSection);
	return result;
}
