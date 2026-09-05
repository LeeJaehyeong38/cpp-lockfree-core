#pragma once
#ifndef __CPACKET__
#define __CPACKET__

#include <stdlib.h>
#include <memory.h> 
#include <Windows.h>

#define HEADER_SIZE 2
// 클라이언트가 헤더에 선언할 수 있는 페이로드 최대 크기. 이 값 없이 헤더를 그대로 믿고
// 버퍼를 다루면, 조작된(비정상적으로 큰) 헤더값으로 힙 오버플로우를 유발할 수 있다.
#define MAX_PACKET_SIZE 4000
// 송수신용 패킷 버퍼. 앞 HEADER_SIZE(2)바이트는 헤더(페이로드 크기), 그 뒤가 실제 데이터다.
// operator<</>>는 타입별로 데이터를 채우거나(<<) 꺼내면서(>>) 그만큼 커서를 밀거나 당긴다 -
// 밀고 당기는 방식만 다르고 나머지 로직은 전부 동일한 패턴이라 타입마다 반복된다.
class CPacket
{
public:
	CPacket();
	~CPacket();
	CPacket(int size); // size바이트짜리 버퍼로 생성 (헤더+페이로드를 담을 만큼 잡아야 한다)

	CPacket& operator << (byte data);
	CPacket& operator >> (byte& data);
	CPacket& operator << (int data);
	CPacket& operator >> (int& data);
	CPacket& operator << (WORD data);
	CPacket& operator >> (WORD& data);
	CPacket& operator << (DWORD data);
	CPacket& operator >> (DWORD& data);
	CPacket& operator << (short data);
	CPacket& operator >> (short& data);
	CPacket& operator << (double data);
	CPacket& operator >> (double& data);
	int getTotalUseSize(); // 헤더 포함한 사이즈
	int getUseSize();      // 페이로드만의 사이즈
	void clear();
	char* getBuffer();     // 페이로드 시작 위치

	void setData(char*, int);
	void setCur(int);
	void getData(char*, int);

	char* getHeaderBuffer(); // 버퍼 맨 앞(헤더 포함 전체) 시작 위치
	void setHeader(char*);
	void getHeader(char*);

private:

	void setHeader(short hearder); // 미사용 - 호출부 없음, setHeader(char*)만 실제로 쓰인다
	int m_size;
	int m_curSize;
	char* m_buffer;
};

#endif