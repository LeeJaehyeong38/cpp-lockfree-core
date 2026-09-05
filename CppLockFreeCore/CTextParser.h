#pragma once
#ifndef __CTEXTPARSER__
#define __CTEXTPARSER__


#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#define MAX_NAMESPACE_COUNT 20
class CTextParser
{
public:
	CTextParser();
	~CTextParser();
	bool loadText(const char* TextName);
	bool getValueInt(const char* szName, int* ipValue ,const char* NameSpace ="");
	bool getValueDouble(const char* szName, double* ipValue, const char* NameSpace = "");
	bool getValueString(const char* szName, char* ipValue, const char* NameSpace = "");

	bool getValueWString(const char* szName, wchar_t* ipValue, const char* NameSpace = "");
	
private: 
	int m_fileSize; // 파일 사이즈 
	char* m_fileBuffer; //  파일데이터들 
	int m_curPos;   // 현재 포인터 위치 
	int m_maxNameSpace;  /// 네임스페이스 마지막 범위   ( 네임스페이스 위치를 벗어 나면 검색하지 않게하려고 )
	int m_nsPos[MAX_NAMESPACE_COUNT]; //네임스페이스 시작 위치들
	int m_nsMaxPos[MAX_NAMESPACE_COUNT]; //네임스페이스 끝나는 위치들
	char m_nsStr[MAX_NAMESPACE_COUNT][256]; //네임스페이스 이름들   

	void checkNameSpace();
	bool skipNoneCommand();
	bool getNextWord(char** chppBuffer, int* ipLength);
	bool getStringWord(char** chppBuffer, int* ipLength);
};




#endif