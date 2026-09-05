// Benchmark.cpp : CppLockFreeCore 라이브러리의 자료구조 정합성 테스트 + 성능 벤치마크 모음.
//

#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <map>
#include <unordered_map>
#include <queue>
#include "LockFreeStack.h"
#include "LockFreeQueue.h"
#include "Map.h"
#include "HashTable.h"
#include "MemoryPool.h"
#include "Benchmark.h"

// ----------------------------------------------------------------------------
// LockFreeStack / LockFreeQueue 다중 스레드 Push/Pop 정합성 테스트
// (데이터 유실이나 중복 없이 정확히 넣은 만큼만 나오는지 확인한다)
// ----------------------------------------------------------------------------

struct StackTestNode
{
	int value;
	StackTestNode* next;
};

// producerCount개의 스레드가 각각 countPerThread개씩 push하고,
// consumerCount개의 스레드가 동시에 pop해서 전부 회수한다.
// 마지막에 [0, producerCount*countPerThread) 값이 정확히 한 번씩만 나왔는지 검사한다.
static bool testLockFreeStack(int producerCount, int consumerCount, int countPerThread)
{
	LockFreeStack<StackTestNode*> stack;
	int total = producerCount * countPerThread;
	std::vector<std::atomic<int>> seen(total);
	for (int i = 0; i < total; i++)
		seen[i] = 0;

	std::atomic<int> poppedCount(0);
	std::atomic<bool> producersDone(false);

	std::vector<std::thread> producers;
	for (int p = 0; p < producerCount; p++)
	{
		producers.emplace_back([p, countPerThread, &stack]()
		{
			for (int i = 0; i < countPerThread; i++)
			{
				StackTestNode* node = new StackTestNode();
				node->value = p * countPerThread + i;
				stack.push(node);
			}
		});
	}

	std::vector<std::thread> consumers;
	for (int c = 0; c < consumerCount; c++)
	{
		consumers.emplace_back([&]()
		{
			for (;;)
			{
				StackTestNode* node = stack.pop();
				if (node != nullptr)
				{
					seen[node->value]++;
					delete node;
					poppedCount++;
				}
				else if (producersDone.load() && stack.isEmpty())
				{
					break;
				}
			}
		});
	}

	for (auto& t : producers) t.join();
	producersDone = true;
	for (auto& t : consumers) t.join();

	bool ok = (poppedCount.load() == total);
	for (int i = 0; i < total && ok; i++)
	{
		if (seen[i].load() != 1)
			ok = false;
	}

	printf("[LockFreeStack 테스트] 생산자 %d개 x %d개 = %d개 push, %d개 pop, 결과: %s\n",
		producerCount, countPerThread, total, poppedCount.load(), ok ? "성공" : "실패");
	return ok;
}

// producerCount개의 스레드가 각각 countPerThread개씩 push하고,
// consumerCount개의 스레드가 동시에 pop해서 전부 회수한다. (바운디드 큐라 producer는 실패 시 재시도)
static bool testLockFreeQueue(int producerCount, int consumerCount, int countPerThread)
{
	int total = producerCount * countPerThread;
	LockFreeQueue<int> queue(1024); // 2의 거듭제곱 용량
	std::vector<std::atomic<int>> seen(total);
	for (int i = 0; i < total; i++)
		seen[i] = 0;

	std::atomic<int> poppedCount(0);
	std::atomic<bool> producersDone(false);

	std::vector<std::thread> producers;
	for (int p = 0; p < producerCount; p++)
	{
		producers.emplace_back([p, countPerThread, &queue]()
		{
			for (int i = 0; i < countPerThread; i++)
			{
				int value = p * countPerThread + i;
				while (!queue.push(value))
				{
					std::this_thread::yield(); // 큐가 가득 찼으면 소비자가 비울 때까지 재시도
				}
			}
		});
	}

	std::vector<std::thread> consumers;
	for (int c = 0; c < consumerCount; c++)
	{
		consumers.emplace_back([&]()
		{
			for (;;)
			{
				int value;
				if (queue.pop(value))
				{
					seen[value]++;
					poppedCount++;
				}
				else if (producersDone.load())
				{
					// producer가 다 끝난 뒤에도 남은 값이 있을 수 있으니 한 번 더 확인
					if (!queue.pop(value))
						break;
					seen[value]++;
					poppedCount++;
				}
			}
		});
	}

	for (auto& t : producers) t.join();
	producersDone = true;
	for (auto& t : consumers) t.join();

	bool ok = (poppedCount.load() == total);
	for (int i = 0; i < total && ok; i++)
	{
		if (seen[i].load() != 1)
			ok = false;
	}

	printf("[LockFreeQueue 테스트] 생산자 %d개 x %d개 = %d개 push, %d개 pop, 결과: %s\n",
		producerCount, countPerThread, total, poppedCount.load(), ok ? "성공" : "실패");
	return ok;
}

// 커스텀 Red-Black Tree Map 검증: 삽입 후 전부 정확한 값으로 조회되는지,
// 절반을 삭제한 후 삭제된 건 안 나오고 남은 건 여전히 정확한지 확인한다.
// (단일 스레드 테스트 - Map 자체는 스레드 세이프하지 않고 호출측에서 락으로 감싸는 설계이기 때문)
static bool testMap(int count)
{
	Map<__int64, int> map;
	for (int i = 0; i < count; i++)
		map.insert((__int64)i, i * 7 + 1);

	bool ok = (map.getCount() == count);

	for (int i = 0; i < count && ok; i++)
	{
		int value = 0;
		if (!map.find((__int64)i, value) || value != i * 7 + 1)
			ok = false;
	}

	// 짝수 키만 삭제
	for (int i = 0; i < count; i += 2)
		map.erase((__int64)i);

	int expectedCount = count - (count + 1) / 2;
	if (map.getCount() != expectedCount)
		ok = false;

	for (int i = 0; i < count && ok; i++)
	{
		int value = 0;
		bool found = map.find((__int64)i, value);
		if (i % 2 == 0)
		{
			if (found) ok = false; // 삭제된 키가 여전히 조회되면 실패
		}
		else
		{
			if (!found || value != i * 7 + 1) ok = false;
		}
	}

	printf("[Map(RB-Tree) 테스트] %d개 삽입 후 짝수 키 삭제, 결과: %s (최종 개수: %d/%d)\n",
		count, ok ? "성공" : "실패", map.getCount(), expectedCount);
	return ok;
}

// FNV-1a 해시테이블 검증: testMap과 동일한 시나리오(삽입 -> 조회 -> 짝수 키 삭제 -> 재조회).
static bool testHashTable(int count)
{
	HashTable<__int64, int> table;
	for (int i = 0; i < count; i++)
		table.insert((__int64)i, i * 7 + 1);

	bool ok = (table.getCount() == count);

	for (int i = 0; i < count && ok; i++)
	{
		int value = 0;
		if (!table.find((__int64)i, value) || value != i * 7 + 1)
			ok = false;
	}

	for (int i = 0; i < count; i += 2)
		table.erase((__int64)i);

	int expectedCount = count - (count + 1) / 2;
	if (table.getCount() != expectedCount)
		ok = false;

	for (int i = 0; i < count && ok; i++)
	{
		int value = 0;
		bool found = table.find((__int64)i, value);
		if (i % 2 == 0)
		{
			if (found) ok = false;
		}
		else
		{
			if (!found || value != i * 7 + 1) ok = false;
		}
	}

	printf("[HashTable(FNV-1a) 테스트] %d개 삽입 후 짝수 키 삭제, 결과: %s (최종 개수: %d/%d)\n",
		count, ok ? "성공" : "실패", table.getCount(), expectedCount);
	return ok;
}

static void runLockFreeStructureTests()
{
	printf("===== 자료구조 테스트 시작 =====\n");
	bool stackOk = testLockFreeStack(4, 4, 10000);
	bool queueOk = testLockFreeQueue(4, 4, 10000);
	bool mapOk = testMap(10000);
	bool hashOk = testHashTable(10000);
	printf("===== 자료구조 테스트 종료 (스택: %s / 큐: %s / Map: %s / HashTable: %s) =====\n",
		stackOk ? "성공" : "실패", queueOk ? "성공" : "실패", mapOk ? "성공" : "실패", hashOk ? "성공" : "실패");
}

// ----------------------------------------------------------------------------
// 벤치마크 1: 락 방식별 공유 카운터 증가 성능 비교
//
// "락"이라고 뭉뚱그리지 않고 3가지를 명확히 나눠서 비교한다:
//   1) SRWLock으로 감싼 증가
//   2) CAS(InterlockedCompareExchange) 루프로 직접 만든 스핀락으로 감싼 증가
//   3) 락 없이 InterlockedIncrement 하나로 원자적으로 증가 (이론상 가장 빨라야 정상)
// N개 스레드가 공유 카운터를 M번씩 증가시키는 데 걸리는 총 시간을 측정한다.
// ----------------------------------------------------------------------------

static void benchIncrementWithSRWLock(int threadCount, int perThread)
{
	SRWLOCK lock;
	InitializeSRWLock(&lock);
	long counter = 0;

	std::vector<std::thread> threads;
	for (int t = 0; t < threadCount; t++)
	{
		threads.emplace_back([&]()
		{
			for (int i = 0; i < perThread; i++)
			{
				AcquireSRWLockExclusive(&lock);
				counter++;
				ReleaseSRWLockExclusive(&lock);
			}
		});
	}
	for (auto& th : threads) th.join();
}

static void benchIncrementWithSpinlock(int threadCount, int perThread)
{
	volatile LONG lockFlag = 0;
	long counter = 0;

	std::vector<std::thread> threads;
	for (int t = 0; t < threadCount; t++)
	{
		threads.emplace_back([&]()
		{
			for (int i = 0; i < perThread; i++)
			{
				while (InterlockedCompareExchange(&lockFlag, 1, 0) != 0)
					; // 스핀 (다른 스레드가 놓아줄 때까지 CAS 재시도)
				counter++;
				InterlockedExchange(&lockFlag, 0);
			}
		});
	}
	for (auto& th : threads) th.join();
}

static void benchIncrementWithInterlocked(int threadCount, int perThread)
{
	volatile LONG counter = 0;

	std::vector<std::thread> threads;
	for (int t = 0; t < threadCount; t++)
	{
		threads.emplace_back([&]()
		{
			for (int i = 0; i < perThread; i++)
				InterlockedIncrement(&counter);
		});
	}
	for (auto& th : threads) th.join();
}

static void runLockBenchmark()
{
	const int threadCount = 5;
	const int perThread = 10000; // 기존 포폴과 동일한 조건(5스레드 x 1만회)으로 비교 가능하게 맞춤
	const int warmup = 2;
	const int trials = 10;

	printf("===== 벤치마크: 락 방식별 공유 카운터 증가 (%d스레드 x %d회, warmup=%d trial=%d) =====\n",
		threadCount, perThread, warmup, trials);

	BenchResult srw = Benchmark::run(warmup, trials, [&]() { benchIncrementWithSRWLock(threadCount, perThread); });
	Benchmark::print("SRWLock", srw);

	BenchResult spin = Benchmark::run(warmup, trials, [&]() { benchIncrementWithSpinlock(threadCount, perThread); });
	Benchmark::print("CAS 스핀락(직접 구현)", spin);

	BenchResult atomicOnly = Benchmark::run(warmup, trials, [&]() { benchIncrementWithInterlocked(threadCount, perThread); });
	Benchmark::print("InterlockedIncrement(락 없음)", atomicOnly);

	printf("===== 벤치마크 종료 =====\n");
}

// ----------------------------------------------------------------------------
// 벤치마크 2: 락프리 자료구조 vs Mutex 기반 자료구조 컨텐션 비교
// (각 스레드가 push 하나 - pop 하나를 반복해서, 자료구조가 계속 붐비는 상태를 유지한다)
// ----------------------------------------------------------------------------

// LockFreeStack과 같은 인터페이스(push/pop)를 가진 mutex 기반 스택 - 대조군.
class MutexStack
{
public:
	void push(StackTestNode* node)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		node->next = m_head;
		m_head = node;
	}
	StackTestNode* pop()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_head == nullptr) return nullptr;
		StackTestNode* node = m_head;
		m_head = m_head->next;
		return node;
	}
private:
	std::mutex m_mutex;
	StackTestNode* m_head = nullptr;
};

// LockFreeQueue의 대조군 - std::queue를 mutex로 감싼 것.
class MutexQueue
{
public:
	void push(int v) { std::lock_guard<std::mutex> lock(m_mutex); m_queue.push(v); }
	bool pop(int& v)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_queue.empty()) return false;
		v = m_queue.front();
		m_queue.pop();
		return true;
	}
private:
	std::mutex m_mutex;
	std::queue<int> m_queue;
};

static void benchLockFreeStackContention(int threadCount, int perThread)
{
	LockFreeStack<StackTestNode*> stack;
	std::vector<std::thread> threads;
	for (int t = 0; t < threadCount; t++)
	{
		threads.emplace_back([&]()
		{
			for (int i = 0; i < perThread; i++)
			{
				StackTestNode* node = new StackTestNode();
				node->value = i;
				stack.push(node);
				StackTestNode* popped = stack.pop();
				if (popped) delete popped;
			}
		});
	}
	for (auto& th : threads) th.join();
	StackTestNode* n;
	while ((n = stack.pop()) != nullptr) delete n;
}

static void benchMutexStackContention(int threadCount, int perThread)
{
	MutexStack stack;
	std::vector<std::thread> threads;
	for (int t = 0; t < threadCount; t++)
	{
		threads.emplace_back([&]()
		{
			for (int i = 0; i < perThread; i++)
			{
				StackTestNode* node = new StackTestNode();
				node->value = i;
				stack.push(node);
				StackTestNode* popped = stack.pop();
				if (popped) delete popped;
			}
		});
	}
	for (auto& th : threads) th.join();
	StackTestNode* n;
	while ((n = stack.pop()) != nullptr) delete n;
}

static void benchLockFreeQueueContention(int threadCount, int perThread)
{
	LockFreeQueue<int> queue(1024);
	std::vector<std::thread> threads;
	for (int t = 0; t < threadCount; t++)
	{
		threads.emplace_back([&]()
		{
			for (int i = 0; i < perThread; i++)
			{
				while (!queue.push(i))
					std::this_thread::yield();
				int value;
				while (!queue.pop(value))
					std::this_thread::yield();
			}
		});
	}
	for (auto& th : threads) th.join();
}

static void benchMutexQueueContention(int threadCount, int perThread)
{
	MutexQueue queue;
	std::vector<std::thread> threads;
	for (int t = 0; t < threadCount; t++)
	{
		threads.emplace_back([&]()
		{
			for (int i = 0; i < perThread; i++)
			{
				queue.push(i);
				int value;
				while (!queue.pop(value))
					std::this_thread::yield();
			}
		});
	}
	for (auto& th : threads) th.join();
}

static void runLockFreeVsMutexBenchmark()
{
	const int threadCount = 5;
	const int perThread = 10000;
	const int warmup = 1;
	const int trials = 5;

	printf("===== 벤치마크: 락프리 vs Mutex 자료구조 컨텐션 (%d스레드 x %d회, warmup=%d trial=%d) =====\n",
		threadCount, perThread, warmup, trials);

	BenchResult lfStack = Benchmark::run(warmup, trials, [&]() { benchLockFreeStackContention(threadCount, perThread); });
	Benchmark::print("LockFreeStack", lfStack);

	BenchResult mtxStack = Benchmark::run(warmup, trials, [&]() { benchMutexStackContention(threadCount, perThread); });
	Benchmark::print("Mutex + 연결리스트 스택", mtxStack);

	BenchResult lfQueue = Benchmark::run(warmup, trials, [&]() { benchLockFreeQueueContention(threadCount, perThread); });
	Benchmark::print("LockFreeQueue", lfQueue);

	BenchResult mtxQueue = Benchmark::run(warmup, trials, [&]() { benchMutexQueueContention(threadCount, perThread); });
	Benchmark::print("Mutex + std::queue", mtxQueue);

	printf("===== 벤치마크 종료 =====\n");
}

// ----------------------------------------------------------------------------
// 벤치마크 3: Map(커스텀 RB-Tree) vs std::map vs std::unordered_map
// 데이터 크기를 늘려가며 "삽입 N개 + 조회 N개"에 걸리는 시간을 비교한다.
// ----------------------------------------------------------------------------

static void benchCustomMap(int count)
{
	Map<int, int> map;
	for (int i = 0; i < count; i++)
		map.insert(i, i);
	for (int i = 0; i < count; i++)
	{
		int v = 0;
		map.find(i, v);
	}
}

static void benchStdMap(int count)
{
	std::map<int, int> map;
	for (int i = 0; i < count; i++)
		map[i] = i;
	for (int i = 0; i < count; i++)
	{
		auto it = map.find(i);
		(void)it;
	}
}

static void benchStdUnorderedMap(int count)
{
	std::unordered_map<int, int> map;
	for (int i = 0; i < count; i++)
		map[i] = i;
	for (int i = 0; i < count; i++)
	{
		auto it = map.find(i);
		(void)it;
	}
}

// 기존 포폴의 "직접 만든 해시테이블이 해시 함수 품질 문제로 std::map보다도 느렸다"는
// 결과를 FNV-1a로 재현/재검증한다.
static void benchCustomHashTable(int count)
{
	HashTable<int, int> table(count * 2 + 1); // 로드팩터를 낮게 잡아 체인 길이를 줄인다
	for (int i = 0; i < count; i++)
		table.insert(i, i);
	for (int i = 0; i < count; i++)
	{
		int v = 0;
		table.find(i, v);
	}
}

static void runMapBenchmark()
{
	const int warmup = 1;
	const int trials = 5;
	int sizes[] = { 1000, 10000, 50000 };

	for (int count : sizes)
	{
		printf("===== 벤치마크: Map/HashTable 삽입 %d개 + 조회 %d개 (warmup=%d trial=%d) =====\n", count, count, warmup, trials);

		BenchResult custom = Benchmark::run(warmup, trials, [&]() { benchCustomMap(count); });
		Benchmark::print("Map(커스텀 RB-Tree)", custom);

		BenchResult hashTable = Benchmark::run(warmup, trials, [&]() { benchCustomHashTable(count); });
		Benchmark::print("HashTable(커스텀 FNV-1a)", hashTable);

		BenchResult stdm = Benchmark::run(warmup, trials, [&]() { benchStdMap(count); });
		Benchmark::print("std::map", stdm);

		BenchResult stdum = Benchmark::run(warmup, trials, [&]() { benchStdUnorderedMap(count); });
		Benchmark::print("std::unordered_map", stdum);
	}
	printf("===== 벤치마크 종료 =====\n");
}

// ----------------------------------------------------------------------------
// 벤치마크 4: MemoryPool vs new/delete
// MemoryPool은 warmup 실행에서 이미 한 번 채워진 프리리스트를 재사용하게 되므로,
// "계속 실행되는 서버에서 풀이 데워진 상태"에 가까운 현실적인 비교가 된다.
// ----------------------------------------------------------------------------

struct PoolBenchItem
{
	int values[8];
};

static void benchMemoryPool(int count)
{
	static MemoryPool<PoolBenchItem> pool; // 여러 trial에 걸쳐 프리리스트가 재사용되도록 static으로 둔다
	std::vector<PoolBenchItem*> ptrs;
	ptrs.reserve(count);
	for (int i = 0; i < count; i++)
		ptrs.push_back(pool.alloc());
	for (int i = 0; i < count; i++)
		pool.free(ptrs[i]);
}

static void benchNewDelete(int count)
{
	std::vector<PoolBenchItem*> ptrs;
	ptrs.reserve(count);
	for (int i = 0; i < count; i++)
		ptrs.push_back(new PoolBenchItem());
	for (int i = 0; i < count; i++)
		delete ptrs[i];
}

static void runMemoryPoolBenchmark()
{
	const int warmup = 2;
	const int trials = 10;
	const int count = 10000;

	printf("===== 벤치마크: MemoryPool vs new/delete (%d개, warmup=%d trial=%d) =====\n", count, warmup, trials);

	BenchResult pool = Benchmark::run(warmup, trials, [&]() { benchMemoryPool(count); });
	Benchmark::print("MemoryPool(alloc/free)", pool);

	BenchResult raw = Benchmark::run(warmup, trials, [&]() { benchNewDelete(count); });
	Benchmark::print("new/delete", raw);

	printf("===== 벤치마크 종료 =====\n");
}

int main()
{
	setvbuf(stdout, NULL, _IOLBF, 1024); // 콘솔로 redirect될 때도 로그가 실시간으로 보이도록 라인버퍼링

	runLockFreeStructureTests();
	runLockBenchmark();
	runLockFreeVsMutexBenchmark();
	runMapBenchmark();
	runMemoryPoolBenchmark();

	return 0;
}
