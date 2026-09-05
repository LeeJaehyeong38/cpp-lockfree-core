#pragma once
#ifndef __PATHTYPES__
#define __PATHTYPES__

#include <vector>

// A*와 JPS가 공유하는 좌표/결과 타입. 두 알고리즘을 같은 기준으로 비교하려면
// 반환 형태부터 동일해야 한다.
struct Point
{
	int x;
	int y;

	bool operator==(const Point& other) const { return x == other.x && y == other.y; }
};

struct PathResult
{
	bool found;
	std::vector<Point> path;   // 시작점부터 도착점까지 순서대로
	int nodesExpanded;         // 알고리즘이 실제로 확장(펼쳐본)한 노드 수 - JPS가 A*보다 적어야 정상
	double elapsedMs;          // 측정용, 알고리즘 내부가 아니라 호출부(Benchmark용 데모)에서 채운다
};

#endif
