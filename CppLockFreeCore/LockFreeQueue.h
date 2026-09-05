#pragma once
#ifndef __LOCKFREEQUEUE__
#define __LOCKFREEQUEUE__

#include <Windows.h>

// Dmitry Vyukov의 Bounded MPMC(Multi-Producer Multi-Consumer) 락프리 큐를 참고해 구현.
//
// LockFreeStack은 128비트 태그드 CAS로 ABA를 막았지만, 이 큐는 다른 방식을 쓴다:
// 각 슬롯마다 "시퀀스 번호"를 두고, 생산자/소비자는 자기 순서(pos)와 슬롯의 시퀀스가 맞는지
// 확인한 뒤 8바이트 CAS 한 번으로 그 슬롯을 선점한다. 포인터 자체를 태그할 필요가 없어서
// 128비트 CAS 없이도(=평범한 CompareExchange64만으로) 락프리 동작이 성립한다.
//
// 용도로는 네트워크 스레드(IOCP Worker)가 잡(패킷/이벤트)을 만들어 push하고,
// 별도의 로직 스레드가 pop해서 처리하는 "네트워크-로직 스레드 분리" 패턴을 염두에 두고 만들었다.
//
// 트레이드오프: 용량이 2의 거듭제곱으로 고정된 바운디드 큐다. 무제한 큐(Michael&Scott 등)보다
// 구현이 단순하고 성능도 예측 가능하지만, 큐가 가득 차면 push가 실패한다(false 반환) - 게임 서버
// 잡큐라면 이 백프레셔(과부하 시 밀어내기)가 오히려 안전장치로 작용한다.
template <typename T>
class LockFreeQueue
{
public:
	// capacity는 2의 거듭제곱이어야 한다 (인덱스 계산을 pos % capacity 대신 pos & (capacity-1)로 하기 위해).
	explicit LockFreeQueue(size_t capacity)
		: m_mask(capacity - 1)
	{
		m_buffer = new Cell[capacity];
		for (size_t i = 0; i < capacity; ++i)
			m_buffer[i].sequence = (LONG64)i;
		m_enqueuePos = 0;
		m_dequeuePos = 0;
	}

	~LockFreeQueue()
	{
		delete[] m_buffer;
	}

	// 큐가 가득 차 있으면 false (백프레셔)
	bool push(const T& value)
	{
		Cell* cell;
		LONG64 pos = m_enqueuePos;
		for (;;)
		{
			cell = &m_buffer[pos & m_mask];
			LONG64 seq = cell->sequence;
			LONG64 diff = seq - pos;

			if (diff == 0)
			{
				// 이 슬롯이 지금 우리 차례다 - CAS로 선점 시도
				if (InterlockedCompareExchange64(&m_enqueuePos, pos + 1, pos) == pos)
					break;
			}
			else if (diff < 0)
			{
				return false; // 큐가 가득 참
			}
			else
			{
				pos = m_enqueuePos; // 다른 생산자가 선점했다 - 최신 위치로 다시 읽고 재시도
			}
		}

		cell->data = value;
		// 데이터를 다 쓴 "다음에" 시퀀스를 올려야, 소비자가 반쯤 쓰인 데이터를 읽지 않는다.
		InterlockedExchange64(&cell->sequence, pos + 1);
		return true;
	}

	// 큐가 비어 있으면 false
	bool pop(T& outValue)
	{
		Cell* cell;
		LONG64 pos = m_dequeuePos;
		for (;;)
		{
			cell = &m_buffer[pos & m_mask];
			LONG64 seq = cell->sequence;
			LONG64 diff = seq - (pos + 1);

			if (diff == 0)
			{
				if (InterlockedCompareExchange64(&m_dequeuePos, pos + 1, pos) == pos)
					break;
			}
			else if (diff < 0)
			{
				return false; // 큐가 비어 있음
			}
			else
			{
				pos = m_dequeuePos;
			}
		}

		outValue = cell->data;
		// 이 슬롯을 "한 바퀴 뒤"에 다시 쓸 생산자에게 넘겨준다.
		InterlockedExchange64(&cell->sequence, pos + m_mask + 1);
		return true;
	}

private:
	struct Cell
	{
		volatile LONG64 sequence;
		T data;
	};

	Cell* m_buffer;
	LONG64 m_mask;
	volatile LONG64 m_enqueuePos;
	volatile LONG64 m_dequeuePos;

	// TODO: 캐시라인 false sharing(패딩) 최적화는 필요성이 확인되면 추가
};

#endif
