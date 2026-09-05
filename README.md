# cpp-lockfree-core

Windows IOCP 기반 비동기 TCP 서버 라이브러리. 락프리 자료구조(Treiber 스택, Vyukov MPMC 큐), 직접 구현한 트리/해시 컨테이너, 부하 테스트 도구, A*/JPS 길찾기 사이드 프로젝트까지 포함한 C++17 / Win32 프로젝트입니다.

## 구성

| 프로젝트 | 역할 |
|---|---|
| `CppLockFreeCore` | 정적 라이브러리(.lib). IOCP 코어, 락프리 스택/큐, 메모리 풀, Map(Red-Black Tree), HashTable(FNV-1a). `main()` 없음 |
| `CppLockFreeCore.EchoServer` | `CLockFreeCore`를 상속받은 에코 서버 데모 |
| `CppLockFreeCore.Benchmark` | 자료구조 정합성 테스트 + 성능 벤치마크(락 방식/컨테이너/메모리 풀) |
| `CppLockFreeCore.BotClient` | 실제 TCP로 접속하는 네이티브 부하 테스트 클라이언트 |
| `CppLockFreeCore.Pathfinding` | A* / JPS 길찾기 비교 (Core를 참조하지 않는 독립 프로젝트) |

## 빌드

Visual Studio 2019 이상, Windows SDK 필요. 솔루션(`CppLockFreeCore.sln`)을 열어 `x64 / Debug` 또는 `x64 / Release`로 전체 빌드하면 5개 프로젝트가 모두 빌드됩니다.

```bash
MSBuild.exe CppLockFreeCore.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64
```

`CppLockFreeCore` 프로젝트만 단독으로 빌드하면 `.lib` 하나만 산출됩니다(실행 파일 없음) - 라이브러리와 도구를 물리적으로 분리해뒀습니다.

## 실행

```bash
x64\Debug\CppLockFreeCore.EchoServer.exe      # 127.0.0.1:6000 에코 서버 기동
x64\Debug\CppLockFreeCore.Benchmark.exe       # 정합성 테스트 + 벤치마크 실행 후 종료
x64\Debug\CppLockFreeCore.BotClient.exe       # 서버가 떠 있어야 함
x64\Debug\CppLockFreeCore.Pathfinding.exe     # A* vs JPS 비교
```

BotClient 사용법:

```bash
CppLockFreeCore.BotClient.exe                     # config 파일 기반 기본 CCU 시나리오
CppLockFreeCore.BotClient.exe --repeat 2000       # 접속-해제 2000회 반복 시나리오
CppLockFreeCore.BotClient.exe --sustain 5000 15   # 5,000 CCU 접속을 15초간 유지하는 지속 부하 모드
```

## 라이브러리 사용 예시

```cpp
class EchoServer : public CLockFreeCore
{
    void onRecv(__int64 sessionId, CPacket* pack) override
    {
        double d;  (*pack) >> d;
        CPacket* echo = new CPacket(10);
        (*echo) << d;
        sendPacket(sessionId, echo);
    }
    void onError(int code, const WCHAR* msg) override { wprintf(msg); }
};

ServerOption opt;
opt.port = 6000;
opt.workerMax = 8;
opt.sessionMax = 6000;

EchoServer server;
server.start(opt);
```

`onConnectionRequest` / `onClientJoin` / `onClientLeave` / `onRecv` / `onError` 5개 가상 함수 중 필요한 지점만 오버라이드해서 쓰는 구조입니다. 라이브러리는 스스로 접속/패킷 단위 로그를 남기지 않고, 진짜 이상 상황만 `onError`로 알립니다.

## 핵심 설계

- **세션 생명주기**: `m_isClosing`(신규 IO 즉시 차단) → `m_isTornDown`(CAS로 실제 정리를 정확히 한 번만 실행) 2단계 원자적 가드로 종료 경합을 차단
- **세션 조회**: 메모리 풀(실체) + 락프리 스택(프리리스트) + 직접 구현한 Red-Black Tree Map(ID→포인터 조회) 3계층 분리
- **동기화**: RingBuffer 보호에 SRWLock 채택 - CAS 스핀락/InterlockedIncrement와 실측 비교 후 결정
- **락프리 스택**: 128비트 태그드 CAS(`_InterlockedCompareExchange128`)로 ABA 문제 차단, 세션 프리리스트·메모리 풀의 공통 기반
- **락프리 큐**: Vyukov의 바운디드 MPMC 큐 - 슬롯 시퀀스 번호만으로 8바이트 CAS 기반 구현
- **패킷 검증**: 2바이트 헤더 + 페이로드, `MAX_PACKET_SIZE`(4000) 상한 초과 시 해당 세션만 즉시 종료

## 벤치마크 요약 (실측)

5스레드 x 1만회 x 10회 반복 기준, ms 단위:

| 비교 | 결과 |
|---|---|
| SRWLock vs InterlockedIncrement vs CAS 스핀락 | 0.73 / 0.89 / 11.51 |
| 락프리 큐 vs Mutex 큐 | 5.99 vs 35.79 (약 6배) |
| 락프리 스택 vs Mutex 스택 | 30.35 vs 31.11 (거의 동일 - 힙 할당 비용이 지배적) |
| 메모리 풀 vs new/delete | 1.53 vs 3.00 (약 2배) |
| HashTable(FNV-1a) vs Map vs std::unordered_map vs std::map (5만 개) | 17.37 / 28.34 / 51.32 / 87.33 |

BotClient 부하 테스트(로컬 루프백 1대 기준):

| CCU | 처리량 | p50 | p95 | p99 |
|---|---|---|---|---|
| 1,000 | 248,503pps | 0.06ms | 0.12ms | 0.18ms |
| 5,000 | 42,741pps | 0.05ms | 0.10ms | 0.17ms |

5,000 CCU를 15초간 유지하는 지속 부하 모드에서는 총 3,331,884건 수신(평균 164,665pps)을 확인했습니다.

상세 설계 배경과 트레이드오프는 포트폴리오 슬라이드 문서를 참고하세요.
