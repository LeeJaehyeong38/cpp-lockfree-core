#pragma once
#ifndef __HASHTABLE__
#define __HASHTABLE__

#include <cstddef>

// 체이닝(연결리스트) 방식 해시테이블.
// 기존 포폴에서는 해시 함수가 "키를 바이트 단위로 더해서 테이블 크기로 나눈 나머지"였는데,
// 이 방식은 값 분포에 따라 충돌이 한쪽으로 몰리기 쉬워서 실측 벤치마크에서 삽입/조회 둘 다
// std::map(RB-Tree)보다도 느리게 나왔었다. 여기서는 대신 FNV-1a 해시를 써서 충돌을 줄인다.
template <typename K, typename V>
class HashTable
{
private:
	struct Node
	{
		K key;
		V value;
		Node* next;
	};

	Node** m_buckets;
	size_t m_bucketCount;
	int m_count;

	// FNV-1a: 키를 바이트 단위로 훑으면서 곱셈으로 비트를 섞는다 - 단순 합산보다
	// 비슷한 값들이 같은 버킷에 몰리는 경향이 훨씬 적다.
	size_t hashOf(const K& key) const
	{
		const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&key);
		unsigned long long h = 1469598103934665603ULL; // FNV offset basis
		for (size_t i = 0; i < sizeof(K); i++)
		{
			h ^= bytes[i];
			h *= 1099511628211ULL; // FNV prime
		}
		return (size_t)(h % m_bucketCount);
	}

public:
	// bucketCount는 소수로 주면 특정 배수 패턴의 키들이 몰리는 걸 줄일 수 있다.
	explicit HashTable(size_t bucketCount = 4099)
		: m_bucketCount(bucketCount), m_count(0)
	{
		m_buckets = new Node*[m_bucketCount];
		for (size_t i = 0; i < m_bucketCount; i++)
			m_buckets[i] = nullptr;
	}

	~HashTable()
	{
		for (size_t i = 0; i < m_bucketCount; i++)
		{
			Node* cur = m_buckets[i];
			while (cur != nullptr)
			{
				Node* next = cur->next;
				delete cur;
				cur = next;
			}
		}
		delete[] m_buckets;
	}

	// 이미 있는 키면 값만 덮어쓴다.
	void insert(const K& key, const V& value)
	{
		size_t idx = hashOf(key);
		for (Node* cur = m_buckets[idx]; cur != nullptr; cur = cur->next)
		{
			if (cur->key == key)
			{
				cur->value = value;
				return;
			}
		}
		Node* node = new Node{ key, value, m_buckets[idx] };
		m_buckets[idx] = node;
		m_count++;
	}

	bool find(const K& key, V& outValue) const
	{
		size_t idx = hashOf(key);
		for (Node* cur = m_buckets[idx]; cur != nullptr; cur = cur->next)
		{
			if (cur->key == key)
			{
				outValue = cur->value;
				return true;
			}
		}
		return false;
	}

	bool erase(const K& key)
	{
		size_t idx = hashOf(key);
		Node* prev = nullptr;
		Node* cur = m_buckets[idx];
		while (cur != nullptr)
		{
			if (cur->key == key)
			{
				if (prev != nullptr) prev->next = cur->next;
				else m_buckets[idx] = cur->next;
				delete cur;
				m_count--;
				return true;
			}
			prev = cur;
			cur = cur->next;
		}
		return false;
	}

	int getCount() const { return m_count; }
};

#endif
