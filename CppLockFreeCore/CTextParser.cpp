#include "CTextParser.h"
#include <Windows.h>
CTextParser::CTextParser()
{
}

CTextParser::~CTextParser()
{
}
bool CTextParser::loadText(const char* TextName)
{
	FILE* pFile; //읽는 파일

	pFile = fopen(TextName, "rb");

	if (pFile == NULL)
	{
		printf("파일을(%s) 열 수 없습니다",TextName);
		return false;
	}

	fseek(pFile, 0, SEEK_END);
	m_fileSize = ftell(pFile);
	fseek(pFile, 0, SEEK_SET);

	m_fileBuffer = (char*)malloc(m_fileSize);
	fseek(pFile, 0, SEEK_SET);
	//	rewind(pFile);
	fread(m_fileBuffer, m_fileSize, 1, pFile);
	fclose(pFile);
	checkNameSpace();
	return true;

}

void CTextParser::checkNameSpace()
{
	m_curPos = 0; //처음부터 검색 
	m_maxNameSpace = m_fileSize;//끝까지 검색
	int NsCount = 0; //현재 까지 찾은 네임스페이스 갯수
	char chWord[256];

	char* chpBuffer = &m_fileBuffer[m_curPos];
	int len;
	for (int i = 0; i < MAX_NAMESPACE_COUNT; i++)
	{
		m_nsMaxPos[i] = m_fileSize;  // 혹시 몰라서 
		m_nsPos[i] = 0;
	}
	while (getNextWord(&chpBuffer, &len))
	{

		if (chpBuffer[0]==':')
		{
			memset(chWord, 0, 256);
			memcpy(chWord, chpBuffer+1, len-1);

			m_nsPos[NsCount] = m_curPos;//현재위치 저장 
			strcpy(m_nsStr[NsCount], chWord);// 현재 네임스페이스 저장 

			while (getNextWord(&chpBuffer, &len))
			{
				
					memset(chWord, 0, 256);
					memcpy(chWord, chpBuffer, len);
					if (0 == strcmp(chWord, "}"))
					{
						m_nsMaxPos[NsCount] = m_curPos;
						break;
					}
			}
			NsCount++;
			
		}
	}
	/*for (int i = 0; i < MAX_NAMESPACE_COUNT; i++)
	{
		printf("시작 위치 %d \n끝 위치 %d \n이름 %s!\n", m_nsPos[i],m_nsMaxPos[i],m_nsStr[i]);
	}*/
	m_curPos = 0;
}
bool CTextParser::skipNoneCommand()
{
	char* chpBuffer = &m_fileBuffer[m_curPos];

	while (1)
	{
		if (m_curPos  >= m_maxNameSpace)
			return false;

		if (*chpBuffer == '/' && *(chpBuffer + 1) == '/')  //주석 나오면 아랫줄로 엔터 나올때까지 이동 
		{
			while (*chpBuffer != '\n')
			{
				if (m_curPos >= m_maxNameSpace)
					return false;
				chpBuffer++;
				m_curPos++;
			}
		}

		   //    콤마                  마침표                  스페이스            백스페이스?            탭                   라인피드               캐리지 리턴 
		if (*chpBuffer == ',' || *chpBuffer == '.' || *chpBuffer == 0x20 || *chpBuffer == 0x08 || *chpBuffer == 0x09 || *chpBuffer == 0x0a || *chpBuffer == 0x0d)
		{
			chpBuffer++;
			m_curPos++;
		}
		else
		{
			break;
		}
	}

	return true;
}
bool CTextParser::getNextWord(char** chppBuffer, int* ipLength)
{
	
	if (skipNoneCommand())
	{
		char* chpBuffer = &m_fileBuffer[m_curPos];
		int startPos = m_curPos;
		while ( 1)
		{
			if (*chpBuffer == '/' && *(chpBuffer + 1) == '/') //주석 나오면 
			{
				break;
			}
			//    콤마                       스페이스            백스페이스?            탭                   라인피드               캐리지 리턴 
			if (*chpBuffer == ','  || *chpBuffer == 0x20 || *chpBuffer == 0x08 || *chpBuffer == 0x09 || *chpBuffer == 0x0a || *chpBuffer == 0x0d)
			{
				break;
			}
			chpBuffer++;
			m_curPos++;
		}
		chpBuffer = &m_fileBuffer[startPos];
		*chppBuffer = chpBuffer;
		*ipLength = m_curPos- startPos;
		return true;
	}
	

	return false;
}
bool CTextParser::getStringWord(char** chppBuffer, int* ipLength)
{
	if (skipNoneCommand())
	{
		char* chpBuffer = &m_fileBuffer[m_curPos];

		if (*chpBuffer != '"') // 가져올 문자열이 " 로시작하지않으면 실패 
			return false;
		
		// " 뒤부터 문자열받아옴 
		chpBuffer++;
		m_curPos++;
		int startPos = m_curPos;
		while (1)
		{
			//  " 만날때까지 문자열 받음 
			if (*chpBuffer == '"')
			{
				break;
			}
			chpBuffer++;
			m_curPos++;
		}
		chpBuffer = &m_fileBuffer[startPos];
		*chppBuffer = chpBuffer;
		*ipLength = m_curPos - startPos;
		return true;
	}
	return true;
}

bool CTextParser::getValueString(const char* szName, char* ipValue, const char* NameSpace )
{
	char* chpBuff;
	char chWord[256];
	int iLenth;
	if (0 == strcmp(NameSpace, ""))
	{
		// 네임스페이스를 안넣으면 처음부터 끝까지 뒤질거임 
		m_curPos = 0;
		m_maxNameSpace = m_fileSize;
	}
	else
	{
		bool isFind = false;
		for (int i = 0; i < MAX_NAMESPACE_COUNT; i++)
		{
			if (0 == strcmp(NameSpace, m_nsStr[i]))
			{
				m_curPos = m_nsPos[i];
				m_maxNameSpace = m_nsMaxPos[i];
				isFind = true;
			}
		}
		if (isFind == false) //해당 네임스페이스 못찾으면 에러 
			return false;
	}
	while (getNextWord(&chpBuff, &iLenth))
	{
		memset(chWord, 0, 256);
		memcpy(chWord, chpBuff, iLenth);

		if (0 == strcmp(szName, chWord))
		{
			if (getNextWord(&chpBuff, &iLenth))
			{
				memset(chWord, 0, 256);
				memcpy(chWord, chpBuff, iLenth);
				if (0 == strcmp(chWord, "="))
				{
					if (getStringWord(&chpBuff, &iLenth))
					{
						memset(chWord, 0, 256);
						memcpy(chWord, chpBuff, iLenth);
						strcpy(ipValue, chWord);
						return true;
					}
					return false;
				}
			}
			
			return false;
		}

	}
	return true;
}

bool CTextParser::getValueWString(const char* szName, wchar_t* ipValue, const char* NameSpace)
{
	char* chpBuff;
	char chWord[256];
	int iLenth;
	if (0 == strcmp(NameSpace, ""))
	{
		// 네임스페이스를 안넣으면 처음부터 끝까지 뒤질거임 
		m_curPos = 0;
		m_maxNameSpace = m_fileSize;
	}
	else
	{
		bool isFind = false;
		for (int i = 0; i < MAX_NAMESPACE_COUNT; i++)
		{
			if (0 == strcmp(NameSpace, m_nsStr[i]))
			{
				m_curPos = m_nsPos[i];
				m_maxNameSpace = m_nsMaxPos[i];
				isFind = true;
			}
		}
		if (isFind == false) //해당 네임스페이스 못찾으면 에러 
			return false;
	}
	while (getNextWord(&chpBuff, &iLenth))
	{
		memset(chWord, 0, 256);
		memcpy(chWord, chpBuff, iLenth);

		if (0 == strcmp(szName, chWord))
		{
			if (getNextWord(&chpBuff, &iLenth))
			{
				memset(chWord, 0, 256);
				memcpy(chWord, chpBuff, iLenth);
				if (0 == strcmp(chWord, "="))
				{
					if (getStringWord(&chpBuff, &iLenth))
					{
						memset(chWord, 0, 256);
						memcpy(chWord, chpBuff, iLenth);
						mbstowcs(ipValue, chWord, 256);
						return true;
					}
					return false;
				}
			}

			return false;
		}

	}
	return true;
}
bool CTextParser::getValueInt(const char* szName, int* ipValue, const char* NameSpace )
{
	char* chpBuff;
	char chWord[256];
	int iLenth;
	if (0 == strcmp(NameSpace, ""))
	{
		// 네임스페이스를 안넣으면 처음부터 끝까지 뒤질거임 
		m_curPos = 0;
		m_maxNameSpace = m_fileSize;
	}
	else
	{
		bool isFind = false;
		for (int i = 0; i < MAX_NAMESPACE_COUNT; i++)
		{
			if (0 == strcmp(NameSpace, m_nsStr[i]))
			{
				m_curPos = m_nsPos[i];
				m_maxNameSpace = m_nsMaxPos[i];
				isFind = true;
			}
		}
		if (isFind == false) //해당 네임스페이스 못찾으면 에러 
			return false;
	}
	while (getNextWord(&chpBuff,&iLenth))
	{
		memset(chWord, 0, 256);
		memcpy(chWord, chpBuff, iLenth);

		if (0 == strcmp(szName, chWord))
		{
			if (getNextWord(&chpBuff, &iLenth))
			{
				memset(chWord, 0, 256);
				memcpy(chWord, chpBuff, iLenth);
				if (0 == strcmp(chWord,"="))
				{
					if (getNextWord(&chpBuff, &iLenth))
					{
						memset(chWord, 0, 256);
						memcpy(chWord, chpBuff, iLenth);
						*ipValue = atoi(chWord);
						return true;
					}
					return false;
				}
			}

			return false;
		}

	}
	return true;
}

bool CTextParser::getValueDouble(const char* szName, double* ipValue, const char* NameSpace)
{
	char* chpBuff;
	char chWord[256];
	int iLenth;
	if (0 == strcmp(NameSpace, ""))
	{
		// 네임스페이스를 안넣으면 처음부터 끝까지 뒤질거임 
		m_curPos = 0;
		m_maxNameSpace = m_fileSize;
	}
	else
	{
		bool isFind = false;
		for (int i = 0; i < MAX_NAMESPACE_COUNT; i++)
		{
			if (0 == strcmp(NameSpace, m_nsStr[i]))
			{
				m_curPos = m_nsPos[i];
				m_maxNameSpace = m_nsMaxPos[i];
				isFind = true;
			}
		}
		if (isFind == false) //해당 네임스페이스 못찾으면 에러 
			return false;
	}
	while (getNextWord(&chpBuff, &iLenth))
	{
		memset(chWord, 0, 256);
		memcpy(chWord, chpBuff, iLenth);

		if (0 == strcmp(szName, chWord))
		{
			if (getNextWord(&chpBuff, &iLenth))
			{
				memset(chWord, 0, 256);
				memcpy(chWord, chpBuff, iLenth);
				if (0 == strcmp(chWord, "="))
				{
					if (getNextWord(&chpBuff, &iLenth))
					{
						memset(chWord, 0, 256);
						memcpy(chWord, chpBuff, iLenth);
						*ipValue = atof(chWord);
						return true;
					}
					return false;
				}
			}

			return false;
		}

	}
	return true;
}