#include "CPacket.h"


CPacket::CPacket()
{
	m_size = 512;
	m_curSize = HEADER_SIZE;
	m_buffer = (char*)malloc(512);
}

CPacket::CPacket(int size)
{
	m_size = size;
	m_curSize = HEADER_SIZE;
	m_buffer = (char*)malloc(size);
}

CPacket::~CPacket()
{
	free(m_buffer);
}


// 값 자체가 아니라 반드시 &hearder(주소)를 넘겨야 한다 - 값을 그대로 포인터로 캐스팅하면
// 임의 주소를 읽어오는 셈이라 크래시/정보누출로 이어진다.
void CPacket::setHeader(short hearder)
{
	memcpy_s(&m_buffer[0], sizeof(short), (char*)&hearder, sizeof(short));
}

void CPacket::clear()
{
	m_curSize = 0;
}

int CPacket::getTotalUseSize()
{
	return m_curSize;
}

int CPacket::getUseSize()
{
	return m_curSize - HEADER_SIZE;
}
char* CPacket::getBuffer()
{
	return &m_buffer[HEADER_SIZE];
}
void CPacket::setCur(int size)
{
	m_curSize = size;
}
void CPacket::setData(char* buff, int size)
{

	if (m_curSize + size <= m_size)
	{
		memcpy_s(&m_buffer[m_curSize], size, buff, size);
		m_curSize += size;
	}


}

void CPacket::getData(char* data, int size)
{

	if (m_curSize + size <= m_size)
	{
		memcpy_s(data, size, &m_buffer[HEADER_SIZE], size);
		memcpy_s(&m_buffer[HEADER_SIZE], m_curSize - HEADER_SIZE, &m_buffer[size], m_curSize - HEADER_SIZE);
		m_curSize -= size;
	}


}

char* CPacket::getHeaderBuffer()
{
	return m_buffer;
}

void CPacket::setHeader(char* buff)
{

	memcpy_s(&m_buffer[0], HEADER_SIZE, buff, HEADER_SIZE);
}

void CPacket::getHeader(char* data)
{

	memcpy_s(data, HEADER_SIZE, &m_buffer[0], HEADER_SIZE);
}

// 아래 operator<</>> 전부 패턴이 같다: << 는 버퍼 끝에 데이터를 붙이고 커서를 그만큼 미는 것,
// >> 는 페이로드 맨 앞에서 데이터를 꺼내고 남은 데이터를 앞으로 당긴 뒤 커서를 줄이는 것.
// (즉 >>는 큐처럼 앞에서부터 빼내는 동작 - 스택처럼 뒤에서 빼는 게 아니다)
CPacket& CPacket::operator<<(byte data)
{
	if (m_curSize + sizeof(byte) <= m_size)
	{
		memcpy_s(&m_buffer[m_curSize], sizeof(byte), (char*)&data, sizeof(byte));
		m_curSize += sizeof(byte);
	}
	return *this;
}

CPacket& CPacket::operator>>(byte& data)
{
	if (m_curSize >= sizeof(byte))
	{
		memcpy_s(&data, sizeof(byte), &m_buffer[HEADER_SIZE], sizeof(byte));
		memcpy_s(&m_buffer[HEADER_SIZE], m_curSize - HEADER_SIZE, &m_buffer[sizeof(byte)], m_curSize - HEADER_SIZE);
		m_curSize -= sizeof(byte);
	}
	return *this;
}

CPacket& CPacket::operator<<(int data)
{
	if (m_curSize + sizeof(int) <= m_size)
	{
		memcpy_s(&m_buffer[m_curSize], sizeof(int), (char*)&data, sizeof(int));
		m_curSize += sizeof(int);
	}
	return *this;
}

CPacket& CPacket::operator>>(int& data)
{
	if (m_curSize >= sizeof(int))
	{
		memcpy_s(&data, sizeof(int), &m_buffer[HEADER_SIZE], sizeof(int));
		memcpy_s(&m_buffer[HEADER_SIZE], m_curSize - HEADER_SIZE, &m_buffer[sizeof(int)], m_curSize - HEADER_SIZE);
		m_curSize -= sizeof(int);
	}
	return *this;
}

CPacket& CPacket::operator<<(WORD data)
{
	if (m_curSize + sizeof(WORD) <= m_size)
	{
		memcpy_s(&m_buffer[m_curSize], sizeof(WORD), (char*)&data, sizeof(WORD));
		m_curSize += sizeof(WORD);
	}
	return *this;
}

CPacket& CPacket::operator>>(WORD& data)
{
	if (m_curSize >= sizeof(WORD))
	{
		memcpy_s(&data, sizeof(WORD), &m_buffer[HEADER_SIZE], sizeof(WORD));
		memcpy_s(&m_buffer[HEADER_SIZE], m_curSize - HEADER_SIZE, &m_buffer[sizeof(WORD)], m_curSize - HEADER_SIZE);
		m_curSize -= sizeof(WORD);
	}
	return *this;
}
CPacket& CPacket::operator<<(DWORD data)
{
	if (m_curSize + sizeof(DWORD) <= m_size)
	{
		memcpy_s(&m_buffer[m_curSize], sizeof(DWORD), (char*)&data, sizeof(DWORD));
		m_curSize += sizeof(DWORD);
	}
	return *this;
}

CPacket& CPacket::operator>>(DWORD& data)
{
	if (m_curSize >= sizeof(DWORD))
	{
		memcpy_s(&data, sizeof(DWORD), &m_buffer[HEADER_SIZE], sizeof(DWORD));
		memcpy_s(&m_buffer[HEADER_SIZE], m_curSize - HEADER_SIZE, &m_buffer[sizeof(DWORD)], m_curSize - HEADER_SIZE);
		m_curSize -= sizeof(DWORD);
	}
	return *this;
}
CPacket& CPacket::operator<<(short data)
{
	if (m_curSize + sizeof(short) <= m_size)
	{
		memcpy_s(&m_buffer[m_curSize], sizeof(short), (char*)&data, sizeof(short));
		m_curSize += sizeof(short);
	}
	return *this;
}

CPacket& CPacket::operator>>(short& data)
{
	if (m_curSize >= sizeof(short))
	{
		memcpy_s(&data, sizeof(short), &m_buffer[HEADER_SIZE], sizeof(short));
		memcpy_s(&m_buffer[HEADER_SIZE], m_curSize - HEADER_SIZE, &m_buffer[sizeof(short)], m_curSize - HEADER_SIZE);
		m_curSize -= sizeof(short);
	}
	return *this;
}


CPacket& CPacket::operator<<(double data)
{
	if (m_curSize + sizeof(double) <= m_size)
	{
		memcpy_s(&m_buffer[m_curSize], sizeof(double), (char*)&data, sizeof(double));
		m_curSize += sizeof(double);
	}
	return *this;
}

CPacket& CPacket::operator>>(double& data)
{
	if (m_curSize >= sizeof(double))
	{
		memcpy_s(&data, sizeof(double), &m_buffer[HEADER_SIZE], sizeof(double));
		memcpy_s(&m_buffer[HEADER_SIZE], m_curSize - HEADER_SIZE, &m_buffer[sizeof(double)], m_curSize - HEADER_SIZE);
		m_curSize -= sizeof(double);
	}
	return *this;
}