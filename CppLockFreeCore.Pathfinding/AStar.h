#pragma once
#ifndef __ASTAR__
#define __ASTAR__

#include "Grid.h"
#include "PathTypes.h"

// 표준 A* 구현. 대각선 이동을 포함한 8방향 이동을 지원하고,
// 휴리스틱은 대각선 이동 비용을 반영한 옥타일(Octile) 거리를 쓴다.
// JPS와 성능을 비교하기 위한 "기준점" 역할이라 특별한 최적화 없이 정석대로 구현했다.
class AStar
{
public:
	static PathResult findPath(const Grid& grid, Point start, Point goal);
};

#endif
