#pragma once
#ifndef __LOCKFREESTACK__
#define __LOCKFREESTACK__

#include <Windows.h>

// 락프리 스택 (Treiber Stack).
//
// T는 포인터 타입이어야 하며, 가리키는 객체는 반드시 공개 멤버 `next` (T 타입)를 가지고 있어야 한다.
// 예: struct Foo { ...; Foo* next; };  LockFreeStack<Foo*> stack;
// 별도의 래핑 노드를 새로 할당하지 않고, push/pop되는 객체 자체의 next 필드를 체인에 그대로
// 사용하기 때문에 힙 할당 없이(=락 없이, 재할당 없이) 스택 동작만 수행한다. MemoryPool의
// 내부 프리리스트로 쓰인다.
//
// ABA 문제와 128비트 CAS
// -----------------------
// top 포인터 하나만 CAS로 갱신하면 다음과 같은 시나리오에서 문제가 생긴다:
//   1) 스레드 A가 pop()을 시작해 top(=X)을 읽는다.
//   2) 그 사이 다른 스레드들이 X를 pop, Y를 pop, 그리고 X를 다시 push 한다.
//      (X는 재활용되어 다시 스택 top에 올라왔다 - 포인터 값은 완전히 같다)
//   3) 스레드 A가 CAS(top, X, X->next)를 시도하면 "top이 여전히 X"이므로 성공해버리지만,
//      실제로는 스택 구조가 그 사이 완전히 바뀌어 있었다 - 이게 ABA 문제다.
// top과 함께 매 push/pop마다 단조 증가하는 태그(카운터)를 128비트로 묶어서 CAS하면,
// 포인터가 우연히 같은 값으로 되돌아와도 태그가 다르므로 CAS가 실패해 재시도하게 된다.
// x64에서는 _InterlockedCompareExchange128(16바이트 정렬 필요)로 이 128비트 CAS를 수행한다.
template <typename T>
class LockFreeStack
{
public:
	LockFreeStack()
	{
		m_head.ptr = nullptr;
		m_head.tag = 0;
	}

	// node를 스택 맨 위에 얹는다.
	void push(T node)
	{
		Head oldHead, newHead;
		do
		{
			oldHead = m_head; // 아래 CAS가 128비트를 통째로 검증하므로, 이 스냅샷이 읽는 도중 찢어져도(torn read) 안전하다 - 그저 CAS가 실패해 재시도될 뿐이다.
			node->next = oldHead.ptr;
			newHead.ptr = node;
			newHead.tag = oldHead.tag + 1;
		} while (!cas(oldHead, newHead));
	}

	// 비어있으면 nullptr
	T pop()
	{
		Head oldHead, newHead;
		do
		{
			oldHead = m_head;
			if (oldHead.ptr == nullptr)
				return nullptr;
			newHead.ptr = oldHead.ptr->next;
			newHead.tag = oldHead.tag + 1;
		} while (!cas(oldHead, newHead));

		return oldHead.ptr;
	}

	bool isEmpty() const
	{
		return m_head.ptr == nullptr;
	}

private:
	__declspec(align(16)) struct Head
	{
		T ptr;
		__int64 tag;
	};

	bool cas(Head& expected, const Head& desired)
	{
		return _InterlockedCompareExchange128(
			reinterpret_cast<LONG64*>(&m_head),
			desired.tag,
			reinterpret_cast<LONG64>(desired.ptr),
			reinterpret_cast<LONG64*>(&expected)) != 0;
	}

	Head m_head;
};

#endif
