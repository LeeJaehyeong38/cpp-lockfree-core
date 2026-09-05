#pragma once
#ifndef __CPacket__
#define __CPacket__
#include <stdlib.h>
#include <process.h>
#include <memory.h> 
#include <windows.h>

// send/recv 버퍼용 링버퍼. front(읽는 위치)/rear(쓰는 위치)가 배열 끝에서 처음으로 돌아가며
// 순환하는 구조라, 한 번의 read/write가 버퍼 경계에서 앞뒤로 잘릴 수 있다 - 그때를 대비해서
// getDirect*Size로 "한 번에 처리 가능한 만큼"만 알려주고, enqueue/dequeue가 필요하면 나머지를
// 이어서 처리한다.
class CRingBuff
{
public:
	CRingBuff();
	~CRingBuff();
	CRingBuff(int BuffSize);

	void init(int size);

	void lock(int type=0);
	void unlock();

	int enqueue(const char* pBuffer, int size);
	int dequeue(char* pBuffer, int size,bool isch=false);
	int peek(char* pBuffer, int size);


	bool isEmpty();
	bool isFull();

	int getSize();//총사이즈
	int getUseSize();//지금 사용중인 사이즈 
	int getFreeSize();//남은 사이즈 
	int getDirectReadSize(); //읽을수있는 사이즈 (짤려서 앞뒤 나눠 들어갔을경우 현재 한번에 읽히는 크기) 
	int getDirectWriteSize(); //쓸수 있는 사이즈 (짤려서 앞뒤 나눠 들어갔을경우 현재 한번에 쓰이는 크기) 

	char* getFrontPos();//지금 읽고있는 위치 포인터 
	char* getRearPos();//지금 쓰고있는 위치 포인터 

	void moveFrontPos(int size);//해당 사이즈만큼 이동 
	void moveRearPos(int size);//해당 사이즈만큼 이동 

	void clearBuffer();

	char* getBuffer();


	char* getUseWriteBuffer();
	char* getUseReadBuffer();
private:
	SRWLOCK m_lock;
	int m_lockType;
	int m_front;
	int m_rear;
	int m_size;
	int ff; // 미사용 - 어디서도 안 쓰임
	char* m_buffer;
};



#endif