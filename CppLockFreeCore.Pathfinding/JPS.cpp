#include "JPS.h"
#include <queue>
#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>

namespace
{
	float octileDistance(Point a, Point b)
	{
		int dx = std::abs(a.x - b.x);
		int dy = std::abs(a.y - b.y);
		int minD = std::min(dx, dy);
		int maxD = std::max(dx, dy);
		return static_cast<float>(maxD - minD) + 1.41421356f * minD;
	}

	int sign(int v) { return (v > 0) - (v < 0); }

	// A*와 반드시 같은 규칙을 써야 한다 (AStar.cpp의 canMoveDiagonally 주석 참고) - 양옆 두 칸이
	// 전부 다 벽일 때만 대각선 이동을 막는다.
	bool canMoveDiagonally(const Grid& grid, int x, int y, int dx, int dy)
	{
		return grid.isWalkable(x + dx, y) || grid.isWalkable(x, y + dy);
	}

	// (x,y)에서 (dx,dy) 방향으로 계속 전진하며 "점프 포인트"를 찾는다.
	// 점프 포인트는 셋 중 하나: 목표 지점 / 강제 이웃(옆이 막혀서 방향을 꺾어야만 하는 지점)이
	// 생기는 지점 / 더는 못 가는 막다른 지점(이 경우 false를 반환). 이 재귀 하나가 JPS의 핵심이고,
	// A*라면 한 칸씩 큐에 넣었을 중간 칸들을 전부 건너뛰게 해준다.
	bool jump(const Grid& grid, int x, int y, int dx, int dy, Point goal, Point& outJumpPoint)
	{
		int nx = x + dx;
		int ny = y + dy;

		if (!grid.isWalkable(nx, ny))
			return false;
		if (dx != 0 && dy != 0 && !canMoveDiagonally(grid, x, y, dx, dy))
			return false; // 대각선으로 벽 모서리를 뚫고 지나가는 건 금지

		if (nx == goal.x && ny == goal.y)
		{
			outJumpPoint = { nx, ny };
			return true;
		}

		if (dx != 0 && dy != 0)
		{
			// 대각선 이동 중 강제 이웃 검사 - 진행 방향 옆 칸이 막혀 있는데 대각선 뒤쪽은
			// 뚫려 있으면, 여기서 방향을 꺾어야만 갈 수 있는 지점이 생긴 것이라 점프 포인트다.
			if ((!grid.isWalkable(nx - dx, ny) && grid.isWalkable(nx - dx, ny + dy)) ||
				(!grid.isWalkable(nx, ny - dy) && grid.isWalkable(nx + dx, ny - dy)))
			{
				outJumpPoint = { nx, ny };
				return true;
			}

			// 대각선으로 한 칸 나아갈 때마다, 그 지점에서 가로/세로 방향으로도 미리 점프를
			// 시도해본다 - 그쪽에서 강제 이웃이 발견되면 지금 이 대각선 지점이 점프 포인트가 된다.
			Point dummy;
			if (jump(grid, nx, ny, dx, 0, goal, dummy) || jump(grid, nx, ny, 0, dy, goal, dummy))
			{
				outJumpPoint = { nx, ny };
				return true;
			}
		}
		else if (dx != 0) // 가로 이동
		{
			if ((!grid.isWalkable(nx, ny + 1) && grid.isWalkable(nx + dx, ny + 1)) ||
				(!grid.isWalkable(nx, ny - 1) && grid.isWalkable(nx + dx, ny - 1)))
			{
				outJumpPoint = { nx, ny };
				return true;
			}
		}
		else // 세로 이동 (dy != 0)
		{
			if ((!grid.isWalkable(nx + 1, ny) && grid.isWalkable(nx + 1, ny + dy)) ||
				(!grid.isWalkable(nx - 1, ny) && grid.isWalkable(nx - 1, ny + dy)))
			{
				outJumpPoint = { nx, ny };
				return true;
			}
		}

		return jump(grid, nx, ny, dx, dy, goal, outJumpPoint);
	}

	// 현재 칸에서 다음에 시도해볼 이동 방향들을 추린다. A*라면 매번 8방향을 전부 확장하지만,
	// JPS는 "부모(직전 칸)에서 온 방향"을 기준으로 자연스러운 진행 방향 + 강제 이웃 방향만 남긴다 -
	// 이 가지치기가 확장 노드 수를 줄이는 또 하나의 축이다. 시작 노드는 부모가 없어 8방향 그대로 쓴다.
	std::vector<std::pair<int, int>> prunedDirections(const Grid& grid, int x, int y, bool hasParent, int px, int py)
	{
		std::vector<std::pair<int, int>> dirs;

		if (!hasParent)
		{
			static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
			static const int DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
			for (int i = 0; i < 8; i++)
				dirs.push_back({ DX[i], DY[i] });
			return dirs;
		}

		int dx = sign(x - px);
		int dy = sign(y - py);

		if (dx != 0 && dy != 0)
		{
			if (grid.isWalkable(x, y + dy)) dirs.push_back({ 0, dy });
			if (grid.isWalkable(x + dx, y)) dirs.push_back({ dx, 0 });
			if (canMoveDiagonally(grid, x, y, dx, dy)) dirs.push_back({ dx, dy });
			if (!grid.isWalkable(x - dx, y) && grid.isWalkable(x, y + dy)) dirs.push_back({ -dx, dy });
			if (!grid.isWalkable(x, y - dy) && grid.isWalkable(x + dx, y)) dirs.push_back({ dx, -dy });
		}
		else if (dx != 0)
		{
			if (grid.isWalkable(x + dx, y)) dirs.push_back({ dx, 0 });
			if (!grid.isWalkable(x, y + 1) && grid.isWalkable(x + dx, y + 1)) dirs.push_back({ dx, 1 });
			if (!grid.isWalkable(x, y - 1) && grid.isWalkable(x + dx, y - 1)) dirs.push_back({ dx, -1 });
		}
		else if (dy != 0)
		{
			if (grid.isWalkable(x, y + dy)) dirs.push_back({ 0, dy });
			if (!grid.isWalkable(x + 1, y) && grid.isWalkable(x + 1, y + dy)) dirs.push_back({ 1, dy });
			if (!grid.isWalkable(x - 1, y) && grid.isWalkable(x - 1, y + dy)) dirs.push_back({ -1, dy });
		}

		return dirs;
	}
}

PathResult JPS::findPath(const Grid& grid, Point start, Point goal)
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

	using OpenEntry = std::pair<float, int>;
	std::priority_queue<OpenEntry, std::vector<OpenEntry>, std::greater<OpenEntry>> open;

	int startIdx = toIndex(start.x, start.y);
	gScore[startIdx] = 0.0f;
	open.push({ octileDistance(start, goal), startIdx });

	while (!open.empty())
	{
		int currentIdx = open.top().second;
		open.pop();

		if (closed[currentIdx])
			continue;
		closed[currentIdx] = true;
		result.nodesExpanded++;

		int cx = currentIdx % width;
		int cy = currentIdx / width;

		if (cx == goal.x && cy == goal.y)
		{
			// 점프 포인트들만 이어진 경로를 복원한 다음, 점프 포인트 사이 직선 구간을 한 칸씩
			// 채워 넣어서 A*와 동일한 형태(칸 단위 전체 경로)로 맞춰준다.
			std::vector<Point> waypoints;
			int idx = currentIdx;
			while (idx != -1)
			{
				waypoints.push_back({ idx % width, idx / width });
				idx = cameFrom[idx];
			}
			std::reverse(waypoints.begin(), waypoints.end());

			result.path.push_back(waypoints[0]);
			for (size_t i = 1; i < waypoints.size(); i++)
			{
				Point from = waypoints[i - 1];
				Point to = waypoints[i];
				int stepX = sign(to.x - from.x);
				int stepY = sign(to.y - from.y);
				Point cur = from;
				while (!(cur.x == to.x && cur.y == to.y))
				{
					cur.x += stepX;
					cur.y += stepY;
					result.path.push_back(cur);
				}
			}

			result.found = true;
			return result;
		}

		bool hasParent = (cameFrom[currentIdx] != -1);
		int px = hasParent ? cameFrom[currentIdx] % width : 0;
		int py = hasParent ? cameFrom[currentIdx] / width : 0;

		for (auto& dir : prunedDirections(grid, cx, cy, hasParent, px, py))
		{
			Point jumpPoint;
			if (!jump(grid, cx, cy, dir.first, dir.second, goal, jumpPoint))
				continue;

			int jumpIdx = toIndex(jumpPoint.x, jumpPoint.y);
			if (closed[jumpIdx])
				continue;

			float stepCost = octileDistance({ cx, cy }, jumpPoint);
			float tentativeG = gScore[currentIdx] + stepCost;
			if (tentativeG < gScore[jumpIdx])
			{
				gScore[jumpIdx] = tentativeG;
				cameFrom[jumpIdx] = currentIdx;
				float f = tentativeG + octileDistance(jumpPoint, goal);
				open.push({ f, jumpIdx });
			}
		}
	}

	return result;
}
