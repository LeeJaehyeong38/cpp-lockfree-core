// BotClient.cpp : cpp-lockfree-core 에코 서버용 부하테스트 클라이언트.
//
// 프로세스를 여러 개 띄우는 대신, 한 프로세스 안에서 스레드로 "가상 클라이언트"를 병렬로
// 띄워 동시 접속(CCU)을 흉내낸다. 각 가상 클라이언트는 자기 소켓으로 직접 접속해서
// 실제 TCP 커넥션을 맺으므로, 서버 입장에서는 진짜 CCU와 동일하게 보인다.
//
// 실행 옵션은 코드에 박아넣지 않고 BotClient.config(실행파일과 같은 폴더)에서 읽는다.
// 파일이 없으면 기본값을 쓴다. 형식:
//   ccu_list=100,1000,5000     -> CCU(동시접속) 단계별 테스트 목록. 이 예시는 100명, 1000명,
//                                  5000명 순서로 3번 나눠서 부하테스트를 돈다는 뜻.
//   packets_per_conn=5         -> 가상 클라이언트 한 명이 접속당 몇 개의 패킷을 주고받을지.
//   repeat_iterations=2000     -> "접속 - 패킷 1개 - 즉시 해제"를 몇 번 반복할지
//                                  (세션 재활용 경로를 스트레스 테스트하는 반복 접속-해제 시나리오용).
//   sustain_seconds=15         -> ccu_list 마지막 값만큼 접속을 유지한 채로 몇 초 동안 계속
//                                  패킷을 주고받을지 (0이면 이 단계를 건너뜀). 순간 CCU 테스트는
//                                  1초도 안 돼 끝나버려서 실제로 부하가 걸리는 모습을 보기 어려운데,
//                                  이 단계는 접속을 끊지 않고 지속적으로 트래픽을 흘려보내면서
//                                  1초마다 진행 상황을 출력한다.
//
// 커맨드라인 인자를 주면 config 대신 그 값으로 한 번만 실행한다:
//   BotClient.exe <CCU> <연결당 패킷수>      예) BotClient.exe 1000 5
//   BotClient.exe --repeat <반복횟수>         예) BotClient.exe --repeat 2000
//   BotClient.exe --sustain <CCU> <초>       예) BotClient.exe --sustain 1000 15

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <Windows.h>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>

static const char* g_serverIp = "127.0.0.1";
static const int g_serverPort = 6000;
static LARGE_INTEGER g_freq; // nowMs()의 QueryPerformanceCounter 환산에 쓰는 클럭 주파수

// BotClient.config에서 읽어오는(또는 기본값으로 채워지는) 실행 설정.
struct BotConfig
{
	std::vector<int> ccuList = { 100, 1000, 5000 };
	int packetsPerConn = 5;
	int repeatIterations = 2000;
	int sustainSeconds = 15;
};

// "key=value" 줄들로 이뤄진 간단한 설정 파일을 읽는다. 없거나 읽기 실패하면 기본값을 그대로 쓴다.
static BotConfig loadConfig(const std::string& path)
{
	BotConfig cfg;
	std::ifstream file(path);
	if (!file.is_open())
	{
		printf("[설정] %s 없음 - 기본값 사용\n", path.c_str());
		return cfg;
	}

	std::string line;
	while (std::getline(file, line))
	{
		// 빈 줄이나 '#'으로 시작하는 주석 줄은 건너뛴다.
		size_t firstNonSpace = line.find_first_not_of(" \t\r\n");
		if (firstNonSpace == std::string::npos || line[firstNonSpace] == '#')
			continue;

		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string key = line.substr(0, eq);
		std::string value = line.substr(eq + 1);

		if (key == "ccu_list")
		{
			cfg.ccuList.clear();
			std::stringstream ss(value);
			std::string tok;
			while (std::getline(ss, tok, ','))
			{
				if (!tok.empty())
					cfg.ccuList.push_back(atoi(tok.c_str()));
			}
		}
		else if (key == "packets_per_conn")
		{
			cfg.packetsPerConn = atoi(value.c_str());
		}
		else if (key == "repeat_iterations")
		{
			cfg.repeatIterations = atoi(value.c_str());
		}
		else if (key == "sustain_seconds")
		{
			cfg.sustainSeconds = atoi(value.c_str());
		}
	}

	printf("[설정] %s 로드 완료\n", path.c_str());
	return cfg;
}

// 지연시간 측정용 고해상도 타임스탬프(ms).
static double nowMs()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart * 1000.0 / (double)g_freq.QuadPart;
}

// 에코 서버(127.0.0.1:6000)에 접속한 소켓을 하나 만든다. 실패하면 INVALID_SOCKET.
static SOCKET connectToServer()
{
	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(g_serverPort);
	inet_pton(AF_INET, g_serverIp, &addr.sin_addr);

	if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
	{
		closesocket(sock);
		return INVALID_SOCKET;
	}

	DWORD timeout = 5000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
	return sock;
}

// 가상 클라이언트 하나(스레드 하나)의 동작: 접속 -> N개 패킷 echo 왕복 -> 접속 종료.
// 보낸 값과 받은 값이 실제로 같은지도 검증한다(바이트 수만 맞고 값이 깨진 경우를 잡기 위함).
static void runVirtualClient(int index, int packetsPerConn,
	std::atomic<int>& totalSent, std::atomic<int>& totalRecv,
	std::atomic<int>& totalFailed, std::atomic<int>& totalMismatch,
	std::mutex& latMutex, std::vector<double>& latencies)
{
	SOCKET sock = connectToServer();
	if (sock == INVALID_SOCKET)
	{
		totalFailed++;
		return;
	}

	std::vector<double> localLatencies;
	localLatencies.reserve(packetsPerConn);

	for (int p = 0; p < packetsPerConn; p++)
	{
		short header = 8;
		double payload = (double)(index * 100000 + p);
		char packet[10];
		memcpy(packet, &header, 2);
		memcpy(packet + 2, &payload, 8);

		double t0 = nowMs();
		int sentBytes = send(sock, packet, 10, 0);
		if (sentBytes != 10)
		{
			totalFailed++;
			break;
		}
		totalSent++;

		char resp[10];
		int got = 0;
		bool broken = false;
		while (got < 10)
		{
			int n = recv(sock, resp + got, 10 - got, 0);
			if (n <= 0) { broken = true; break; }
			got += n;
		}
		double t1 = nowMs();

		if (!broken && got == 10)
		{
			double respValue;
			memcpy(&respValue, resp + 2, 8);
			if (respValue != payload)
				totalMismatch++; // 바이트 수는 맞는데 값이 다름 - 큐/재조립 어딘가 꼬였다는 뜻

			totalRecv++;
			localLatencies.push_back(t1 - t0);
		}
		else
		{
			totalFailed++;
			break;
		}
	}

	closesocket(sock);

	std::lock_guard<std::mutex> lock(latMutex);
	latencies.insert(latencies.end(), localLatencies.begin(), localLatencies.end());
}

// ccu개의 스레드를 동시에 띄워 각자 접속시킨다 (프로세스 하나로 CCU를 흉내내는 핵심 부분).
// 스레드를 한꺼번에 수천 개 만들다 보면 OS가 스레드 생성을 거부할 수 있는데(자원 부족 등),
// std::thread 생성자는 이럴 때 예외를 던진다 - 잡아주지 않으면 프로그램 전체가 그대로 죽으니
// 반드시 try/catch로 감싸서 그 가상 클라이언트만 실패 처리하고 넘어가야 한다.
static void runCcuTest(int ccu, int packetsPerConn)
{
	std::atomic<int> totalSent(0), totalRecv(0), totalFailed(0), totalMismatch(0);
	std::mutex latMutex;
	std::vector<double> latencies;

	double start = nowMs();

	std::vector<std::thread> threads;
	threads.reserve(ccu);
	for (int i = 0; i < ccu; i++)
	{
		try
		{
			threads.emplace_back(runVirtualClient, i, packetsPerConn,
				std::ref(totalSent), std::ref(totalRecv), std::ref(totalFailed), std::ref(totalMismatch),
				std::ref(latMutex), std::ref(latencies));
		}
		catch (const std::exception& e)
		{
			// 스레드 생성 자체가 실패한 경우 - 이 가상 클라이언트는 포기하고 계속 진행한다.
			totalFailed++;
			printf("[경고] 스레드 생성 실패(%d번째): %s\n", i, e.what());
		}
	}
	for (auto& t : threads) t.join();

	double elapsedSec = (nowMs() - start) / 1000.0;

	std::sort(latencies.begin(), latencies.end());
	double p50 = -1, p95 = -1, p99 = -1;
	if (!latencies.empty())
	{
		p50 = latencies[latencies.size() * 50 / 100];
		p95 = latencies[latencies.size() * 95 / 100];
		p99 = latencies[std::min(latencies.size() - 1, latencies.size() * 99 / 100)];
	}

	double pps = elapsedSec > 0 ? totalRecv.load() / elapsedSec : 0;

	printf("CCU=%-5d 연결실패=%-4d 전송=%-6d 수신=%-6d 값불일치=%-4d 경과=%6.2fs 처리량=%8.0fpps p50=%6.2fms p95=%6.2fms p99=%6.2fms\n",
		ccu, totalFailed.load(), totalSent.load(), totalRecv.load(), totalMismatch.load(),
		elapsedSec, pps, p50, p95, p99);
	fflush(stdout); // 콘솔 종류에 따라 setvbuf(_IOLBF)만으로는 즉시 안 보이는 경우가 있어 확실하게 밀어낸다
}

// 연결 - echo 1회 - 즉시 해제를 반복해서 서버의 세션 프리리스트 재사용 경로를 스트레스 준다.
static void runRepeatTest(int iterations)
{
	int success = 0, fail = 0, mismatch = 0;
	double start = nowMs();

	for (int i = 0; i < iterations; i++)
	{
		SOCKET sock = connectToServer();
		if (sock == INVALID_SOCKET) { fail++; continue; }

		short header = 8;
		double payload = (double)i;
		char packet[10];
		memcpy(packet, &header, 2);
		memcpy(packet + 2, &payload, 8);
		send(sock, packet, 10, 0);

		char resp[10];
		int got = 0;
		while (got < 10)
		{
			int n = recv(sock, resp + got, 10 - got, 0);
			if (n <= 0) break;
			got += n;
		}
		if (got == 10)
		{
			double respValue;
			memcpy(&respValue, resp + 2, 8);
			if (respValue != payload) mismatch++;
			success++;
		}
		else fail++;

		closesocket(sock);
	}

	double elapsed = (nowMs() - start) / 1000.0;
	printf("반복=%d 성공=%d 실패=%d 값불일치=%d 경과=%.2fs\n", iterations, success, fail, mismatch, elapsed);
	fflush(stdout);
}

// 가상 클라이언트 하나가 접속을 끊지 않고 endTimeMs까지 계속 패킷을 주고받는다.
// runVirtualClient(순간 CCU 테스트용)와 달리 정해진 개수만 보내고 끝나는 게 아니라,
// 시간이 다 될 때까지 접속을 유지한 채로 반복한다 - 실제로 트래픽이 지속되는 모습을 보여주기 위함.
static void runVirtualClientSustained(int index, double endTimeMs,
	std::atomic<int>& totalSent, std::atomic<int>& totalRecv, std::atomic<int>& totalFailed)
{
	SOCKET sock = connectToServer();
	if (sock == INVALID_SOCKET)
	{
		totalFailed++;
		return;
	}

	int seq = 0;
	while (nowMs() < endTimeMs)
	{
		short header = 8;
		double payload = (double)(index * 1000000 + seq);
		char packet[10];
		memcpy(packet, &header, 2);
		memcpy(packet + 2, &payload, 8);

		if (send(sock, packet, 10, 0) != 10) { totalFailed++; break; }
		totalSent++;

		char resp[10];
		int got = 0;
		bool broken = false;
		while (got < 10)
		{
			int n = recv(sock, resp + got, 10 - got, 0);
			if (n <= 0) { broken = true; break; }
			got += n;
		}
		if (broken) { totalFailed++; break; }
		totalRecv++;
		seq++;
	}

	closesocket(sock);
}

// ccu개의 접속을 durationSec 동안 계속 유지하면서 지속적으로 트래픽을 흘려보낸다.
// 순간 CCU 테스트(runCcuTest)는 접속 -> 몇 개 보내고 -> 바로 해제라서 1초도 안 걸리는데,
// 이 테스트는 접속을 끊지 않고 초 단위로 진행 상황을 계속 출력해서 실제로 부하가 걸리는
// 모습을 눈으로 볼 수 있게 한다.
static void runSustainedTest(int ccu, int durationSec)
{
	if (ccu <= 0 || durationSec <= 0) return;

	printf("===== 지속 부하 테스트: %d명 접속 유지, %d초간 =====\n", ccu, durationSec);
	fflush(stdout);

	std::atomic<int> totalSent(0), totalRecv(0), totalFailed(0);
	std::atomic<bool> stopReporter(false);

	double start = nowMs();
	double endTimeMs = start + (double)durationSec * 1000.0;

	// 1초마다 "지금까지 몇 초 지났고 최근 1초간 처리량이 얼마인지"를 찍는 별도 스레드.
	// 본 스레드들이 끝날 때까지 계속 돌면서, 접속이 실제로 살아서 트래픽을 주고받고 있다는 걸 보여준다.
	std::thread reporter([&]()
	{
		int lastRecv = 0;
		int elapsedSec = 0;
		while (!stopReporter.load())
		{
			Sleep(1000);
			elapsedSec++;
			int nowRecv = totalRecv.load();
			int delta = nowRecv - lastRecv;
			lastRecv = nowRecv;
			printf("[진행] %2d초 경과  누적수신=%-7d  최근1초처리량=%6dpps  연결실패=%d\n",
				elapsedSec, nowRecv, delta, totalFailed.load());
			fflush(stdout);
		}
	});

	std::vector<std::thread> threads;
	threads.reserve(ccu);
	for (int i = 0; i < ccu; i++)
	{
		try
		{
			threads.emplace_back(runVirtualClientSustained, i, endTimeMs,
				std::ref(totalSent), std::ref(totalRecv), std::ref(totalFailed));
		}
		catch (const std::exception& e)
		{
			totalFailed++;
			printf("[경고] 스레드 생성 실패(%d번째): %s\n", i, e.what());
		}
	}
	for (auto& t : threads) t.join();

	stopReporter = true;
	reporter.join();

	double elapsedSec = (nowMs() - start) / 1000.0;
	double pps = elapsedSec > 0 ? totalRecv.load() / elapsedSec : 0;
	printf("===== 지속 부하 테스트 종료: 총수신=%d 연결실패=%d 평균처리량=%.0fpps (경과=%.2fs) =====\n",
		totalRecv.load(), totalFailed.load(), pps, elapsedSec);
	fflush(stdout);
}

int main(int argc, char** argv)
{
	QueryPerformanceFrequency(&g_freq);
	setvbuf(stdout, NULL, _IOLBF, 1024);

	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	// 실행 모드 4가지: --repeat 지정, --sustain 지정, CCU/패킷수 직접 지정, 인자 없으면 config 파일 기반 기본 시나리오.
	if (argc >= 3 && strcmp(argv[1], "--repeat") == 0)
	{
		int iterations = atoi(argv[2]);
		printf("===== 연결/해제 반복 테스트: %d회 =====\n", iterations);
		fflush(stdout);
		runRepeatTest(iterations);
	}
	else if (argc >= 4 && strcmp(argv[1], "--sustain") == 0)
	{
		int ccu = atoi(argv[2]);
		int durationSec = atoi(argv[3]);
		runSustainedTest(ccu, durationSec);
	}
	else if (argc >= 3)
	{
		int ccu = atoi(argv[1]);
		int packetsPerConn = atoi(argv[2]);
		printf("===== CCU 부하테스트: %d명 x %d패킷 =====\n", ccu, packetsPerConn);
		fflush(stdout);
		runCcuTest(ccu, packetsPerConn);
	}
	else
	{
		printf("사용법:\n");
		printf("  BotClient.exe <CCU> <패킷수>       예) BotClient.exe 1000 5\n");
		printf("  BotClient.exe --repeat <반복횟수>   예) BotClient.exe --repeat 2000\n");
		printf("  BotClient.exe --sustain <CCU> <초>  예) BotClient.exe --sustain 1000 15\n");
		printf("인자 없이 실행하면 BotClient.config 설정을 읽어 기본 시나리오를 실행합니다.\n\n");

		BotConfig cfg = loadConfig("BotClient.config");

		for (int ccu : cfg.ccuList)
			runCcuTest(ccu, cfg.packetsPerConn);

		// 순간 CCU 테스트는 접속하자마자 끝나버려서 실제로 부하가 걸리는 모습을 보기 어렵다.
		// ccu_list의 마지막(가장 큰) 값만큼 접속을 유지한 채로 sustain_seconds 동안 지속적으로
		// 트래픽을 흘려보내면서 1초마다 진행 상황을 출력한다.
		if (!cfg.ccuList.empty())
			runSustainedTest(cfg.ccuList.back(), cfg.sustainSeconds);

		printf("\n연결/해제 반복 테스트...\n");
		fflush(stdout);
		runRepeatTest(cfg.repeatIterations);
	}

	WSACleanup();

	// 더블클릭으로 실행하면 프로그램이 끝나자마자 콘솔 창이 바로 닫혀서 결과를 못 보고
	// 놓치기 쉽다. 터미널에서 직접 실행할 땐 상관없지만, 안전하게 한 번 멈춰준다.
	printf("\n종료하려면 Enter를 누르세요...\n");
	getchar();

	return 0;
}
