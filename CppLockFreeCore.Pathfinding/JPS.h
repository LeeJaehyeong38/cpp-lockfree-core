#pragma once
#ifndef __JPS__
#define __JPS__

#include "Grid.h"
#include "PathTypes.h"

// JPS(Jump Point Search) 구현. A*와 결과(최단 경로)는 동일해야 하지만,
// 방향이 안 바뀌는 구간은 한 칸씩 확장하지 않고 "점프 포인트"까지 한 번에 건너뛰어서
// 확장 노드 수를 크게 줄인다. 균일 비용(모든 칸 이동 비용이 같은) 격자에서만 성립하는 최적화라
// 웨이트가 있는 맵에는 못 쓴다는 제약이 있다 - 이 프로젝트는 벽/빈칸만 있는 격자라 조건에 맞는다.
class JPS
{
public:
	static PathResult findPath(const Grid& grid, Point start, Point goal);
};

#endif
