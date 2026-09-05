#pragma once
#ifndef __MEMORYPOOL__
#define __MEMORYPOOL__

#include <Windows.h>
#include "LockFreeStack.h"

// 락프리 메모리풀.
// alloc()/free()를 new/delete 대신 사용하면, 한 번 만든 블록을 재사용해서 힙 할당 빈도를 줄인다.
// 내부적으로 LockFreeStack<PoolNode*>를 프리리스트로 쓴다 - PoolNode가 자체적으로 next 필드를
// 갖고 있어서 스택 쪽에서 별도 할당 없이 체인을 관리할 수 있다.
template <typename T>
class MemoryPool
{
public:
	MemoryPool() : m_allocCount(0), m_useCount(0) {}

	// 프리리스트에 반납된 블록이 있으면 그걸 재사용하고, 없으면 새로 만든다.
	T* alloc()
	{
		PoolNode* node = m_freeList.pop();
		if (node == nullptr)
		{
			node = new PoolNode();
			InterlockedIncrement(&m_allocCount);
		}
		InterlockedIncrement(&m_useCount);
		return &node->data;
	}

	// 블록을 프리리스트로 반납한다 - 실제 delete는 안 하고 다음 alloc()에서 재사용된다.
	void free(T* p)
	{
		// data가 PoolNode의 첫 번째 멤버이므로, p의 주소는 곧 PoolNode의 주소와 같다.
		PoolNode* node = reinterpret_cast<PoolNode*>(p);
		InterlockedDecrement(&m_useCount);
		m_freeList.push(node);
	}

	long getAllocCount() const { return m_allocCount; } // 지금까지 실제로 new된 총 블록 수
	long getUseCount() const { return m_useCount; }      // 현재 사용 중인(=free되지 않은) 블록 수

private:
	struct PoolNode
	{
		T data;
		PoolNode* next; // LockFreeStack이 프리리스트 체인에 사용
	};

	LockFreeStack<PoolNode*> m_freeList;
	volatile LONG m_allocCount;
	volatile LONG m_useCount;
};

#endif
