#pragma once
#ifndef __MAP__
#define __MAP__

// 커스텀 Red-Black Tree 기반 Map (CLRS 표준 알고리즘 구현). std::map도 내부적으로는 거의
// 이런 RB-tree라서, 조회는 O(log n)이다.
//
// 참고: 세션 조회만 놓고 보면 "슬롯+세대 카운터를 세션ID에 인코딩해서 배열에 O(1) 직접 접근"하는
// 방식이 성능/구현 단순성 면에서 더 낫다. 그럼에도 이 Map을 쓰는 건 트리 자료구조를 직접
// 구현해보는 것 자체가 포트폴리오 취지라, 일부러 더 느린 방식을 택한 것이다.
//
// 스레드 세이프하지 않다 - 락은 여기 넣지 않고, 사용하는 쪽(CLockFreeCore의 m_criticalSection)
// 책임으로 분리해둔다.
template <typename K, typename V>
class Map
{
private:
	enum Color { RED, BLACK };

	struct Node
	{
		K key;
		V value;
		Color color;
		Node* left;
		Node* right;
		Node* parent;

		Node(const K& k, const V& v, Node* nilNode)
			: key(k), value(v), color(RED), left(nilNode), right(nilNode), parent(nilNode) {}
	};

	Node* m_nil;  // 센티널(리프) 노드 - nullptr 대신 사용해 경계 처리를 단순화한다 (CLRS 관례)
	Node* m_root;
	int m_count;

	void rotateLeft(Node* x)
	{
		Node* y = x->right;
		x->right = y->left;
		if (y->left != m_nil) y->left->parent = x;
		y->parent = x->parent;
		if (x->parent == m_nil) m_root = y;
		else if (x == x->parent->left) x->parent->left = y;
		else x->parent->right = y;
		y->left = x;
		x->parent = y;
	}

	void rotateRight(Node* x)
	{
		Node* y = x->left;
		x->left = y->right;
		if (y->right != m_nil) y->right->parent = x;
		y->parent = x->parent;
		if (x->parent == m_nil) m_root = y;
		else if (x == x->parent->right) x->parent->right = y;
		else x->parent->left = y;
		y->right = x;
		x->parent = y;
	}

	void insertFixup(Node* z)
	{
		while (z->parent->color == RED)
		{
			if (z->parent == z->parent->parent->left)
			{
				Node* y = z->parent->parent->right; // 삼촌 노드
				if (y->color == RED)
				{
					z->parent->color = BLACK;
					y->color = BLACK;
					z->parent->parent->color = RED;
					z = z->parent->parent;
				}
				else
				{
					if (z == z->parent->right)
					{
						z = z->parent;
						rotateLeft(z);
					}
					z->parent->color = BLACK;
					z->parent->parent->color = RED;
					rotateRight(z->parent->parent);
				}
			}
			else // 좌우 대칭
			{
				Node* y = z->parent->parent->left;
				if (y->color == RED)
				{
					z->parent->color = BLACK;
					y->color = BLACK;
					z->parent->parent->color = RED;
					z = z->parent->parent;
				}
				else
				{
					if (z == z->parent->left)
					{
						z = z->parent;
						rotateRight(z);
					}
					z->parent->color = BLACK;
					z->parent->parent->color = RED;
					rotateLeft(z->parent->parent);
				}
			}
		}
		m_root->color = BLACK;
	}

	Node* findNode(const K& key) const
	{
		Node* cur = m_root;
		while (cur != m_nil)
		{
			if (key == cur->key) return cur;
			cur = (key < cur->key) ? cur->left : cur->right;
		}
		return m_nil;
	}

	void transplant(Node* u, Node* v)
	{
		if (u->parent == m_nil) m_root = v;
		else if (u == u->parent->left) u->parent->left = v;
		else u->parent->right = v;
		v->parent = u->parent;
	}

	Node* minimum(Node* x) const
	{
		while (x->left != m_nil) x = x->left;
		return x;
	}

	void eraseFixup(Node* x)
	{
		while (x != m_root && x->color == BLACK)
		{
			if (x == x->parent->left)
			{
				Node* w = x->parent->right;
				if (w->color == RED)
				{
					w->color = BLACK;
					x->parent->color = RED;
					rotateLeft(x->parent);
					w = x->parent->right;
				}
				if (w->left->color == BLACK && w->right->color == BLACK)
				{
					w->color = RED;
					x = x->parent;
				}
				else
				{
					if (w->right->color == BLACK)
					{
						w->left->color = BLACK;
						w->color = RED;
						rotateRight(w);
						w = x->parent->right;
					}
					w->color = x->parent->color;
					x->parent->color = BLACK;
					w->right->color = BLACK;
					rotateLeft(x->parent);
					x = m_root;
				}
			}
			else // 좌우 대칭
			{
				Node* w = x->parent->left;
				if (w->color == RED)
				{
					w->color = BLACK;
					x->parent->color = RED;
					rotateRight(x->parent);
					w = x->parent->left;
				}
				if (w->right->color == BLACK && w->left->color == BLACK)
				{
					w->color = RED;
					x = x->parent;
				}
				else
				{
					if (w->left->color == BLACK)
					{
						w->right->color = BLACK;
						w->color = RED;
						rotateLeft(w);
						w = x->parent->left;
					}
					w->color = x->parent->color;
					x->parent->color = BLACK;
					w->left->color = BLACK;
					rotateRight(x->parent);
					x = m_root;
				}
			}
		}
		x->color = BLACK;
	}

	void clear(Node* node)
	{
		if (node == m_nil) return;
		clear(node->left);
		clear(node->right);
		delete node;
	}

public:
	Map()
	{
		m_nil = new Node(K(), V(), nullptr);
		m_nil->color = BLACK;
		m_nil->left = m_nil->right = m_nil->parent = m_nil;
		m_root = m_nil;
		m_count = 0;
	}

	~Map()
	{
		clear(m_root);
		delete m_nil;
	}

	// 이미 있는 키면 값만 덮어쓴다.
	void insert(const K& key, const V& value)
	{
		Node* existing = findNode(key);
		if (existing != m_nil)
		{
			existing->value = value;
			return;
		}

		Node* z = new Node(key, value, m_nil);
		Node* y = m_nil;
		Node* x = m_root;
		while (x != m_nil)
		{
			y = x;
			x = (z->key < x->key) ? x->left : x->right;
		}
		z->parent = y;
		if (y == m_nil) m_root = z;
		else if (z->key < y->key) y->left = z;
		else y->right = z;

		m_count++;
		insertFixup(z);
	}

	// 찾으면 true를 반환하고 outValue에 채워준다.
	bool find(const K& key, V& outValue) const
	{
		Node* node = findNode(key);
		if (node == m_nil) return false;
		outValue = node->value;
		return true;
	}

	bool erase(const K& key)
	{
		Node* z = findNode(key);
		if (z == m_nil) return false;

		Node* y = z;
		Node* x;
		Color yOriginalColor = y->color;

		if (z->left == m_nil)
		{
			x = z->right;
			transplant(z, z->right);
		}
		else if (z->right == m_nil)
		{
			x = z->left;
			transplant(z, z->left);
		}
		else
		{
			y = minimum(z->right);
			yOriginalColor = y->color;
			x = y->right;
			if (y->parent == z)
			{
				x->parent = y;
			}
			else
			{
				transplant(y, y->right);
				y->right = z->right;
				y->right->parent = y;
			}
			transplant(z, y);
			y->left = z->left;
			y->left->parent = y;
			y->color = z->color;
		}

		if (yOriginalColor == BLACK)
			eraseFixup(x);

		delete z;
		m_count--;
		return true;
	}

	int getCount() const { return m_count; }
};

#endif
