#include "AStar.h"
#include <queue>
#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>

namespace
{
	// 대각선(1개당 sqrt(2))과 직선(1개당 1) 이동을 섞어서 쓸 때 실제 최단 비용과 맞아떨어지는 휴리스틱.
	// 유클리드 거리보다 이 맵의 이동 규칙(8방향, 코너 컷 금지)에 더 정확하게 맞는다.
	float octileDistance(Point a, Point b)
	{
		int dx = std::abs(a.x - b.x);
		int dy = std::abs(a.y - b.y);
		int minD = std::min(dx, dy);
		int maxD = std::max(dx, dy);
		return static_cast<float>(maxD - minD) + 1.41421356f * minD;
	}

	// 대각선 이동은 양옆 두 칸이 "전부 다" 벽일 때만 막는다 - 하나라도 열려 있으면 그쪽 틈으로
	// 비집고 지나갈 수 있다고 본다. JPS의 강제 이웃(forced neighbor) 판정 공식 자체가 이 규칙을
	// 전제로 만들어져 있어서, 여기를 "둘 다 열려야 통과" 같은 더 엄격한 규칙으로 바꾸면 A*와 JPS가
	// 서로 다른 경로를 찾아버린다 (실제로 이 차이 때문에 JPS가 있는 경로를 못 찾는 버그가 있었다).
	bool canMoveDiagonally(const Grid& grid, int x, int y, int dx, int dy)
	{
		return grid.isWalkable(x + dx, y) || grid.isWalkable(x, y + dy);
	}
}

PathResult AStar::findPath(const Grid& grid, Point start, Point goal)
{
	PathResult result;
	result.found = false;
	result.nodesExpanded = 0;
	result.elapsedMs = 0.0;

	const int width = grid.getWidth();
	const int height = grid.getHeight();
	const size_t cellCount = static_cast<size_t>(width) * height;
	auto toIndex = [width](int x, int y) { return y * width + x; };

	if (!grid.isWalkable(start.x, start.y) || !grid.isWalkable(goal.x, goal.y))
		return result;

	std::vector<float> gScore(cellCount, std::numeric_limits<float>::infinity());
	std::vector<int> cameFrom(cellCount, -1);
	std::vector<bool> closed(cellCount, false);

	// (f값, 좌표 인덱스) 쌍을 f값 기준 최소 힙으로 관리한다.
	using OpenEntry = std::pair<float, int>;
	std::priority_queue<OpenEntry, std::vector<OpenEntry>, std::greater<OpenEntry>> open;

	gScore[toIndex(start.x, start.y)] = 0.0f;
	open.push({ octileDistance(start, goal), toIndex(start.x, start.y) });

	static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static const int DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

	while (!open.empty())
	{
		int currentIdx = open.top().second;
		open.pop();

		if (closed[currentIdx])
			continue; // 같은 노드가 더 낮은 f값으로 큐에 여러 번 들어갈 수 있어서 중복 방문은 건너뛴다
		closed[currentIdx] = true;
		result.nodesExpanded++;

		int cx = currentIdx % width;
		int cy = currentIdx / width;

		if (cx == goal.x && cy == goal.y)
		{
			// 목표 도달 - cameFrom을 따라 거꾸로 올라가며 경로를 복원한다
			std::vector<Point> reversed;
			int idx = currentIdx;
			while (idx != -1)
			{
				reversed.push_back({ idx % width, idx / width });
				idx = cameFrom[idx];
			}
			result.path.assign(reversed.rbegin(), reversed.rend());
			result.found = true;
			return result;
		}

		for (int dir = 0; dir < 8; dir++)
		{
			int nx = cx + DX[dir];
			int ny = cy + DY[dir];
			if (!grid.isWalkable(nx, ny))
				continue;

			bool isDiagonal = (DX[dir] != 0 && DY[dir] != 0);
			if (isDiagonal && !canMoveDiagonally(grid, cx, cy, DX[dir], DY[dir]))
				continue;

			int neighborIdx = toIndex(nx, ny);
			if (closed[neighborIdx])
				continue;

			float stepCost = isDiagonal ? 1.41421356f : 1.0f;
			float tentativeG = gScore[currentIdx] + stepCost;
			if (tentativeG < gScore[neighborIdx])
			{
				gScore[neighborIdx] = tentativeG;
				cameFrom[neighborIdx] = currentIdx;
				float f = tentativeG + octileDistance({ nx, ny }, goal);
				open.push({ f, neighborIdx });
			}
		}
	}

	return result; // 큐가 비었는데 목표를 못 찾았다 - 경로가 없는 경우
}
