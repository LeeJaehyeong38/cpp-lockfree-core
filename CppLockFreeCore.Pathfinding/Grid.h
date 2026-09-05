#pragma once
#ifndef __GRID__
#define __GRID__

#include <vector>

// 길찾기 대상이 되는 2차원 격자판. 칸 하나는 이동 가능(빈 칸)이거나 이동 불가(벽)
// 둘 중 하나이고, 좌표는 (x, y) - x가 가로, y가 세로다.
class Grid
{
public:
	Grid(int width, int height);

	bool isInBounds(int x, int y) const;
	bool isWalkable(int x, int y) const;
	void setWall(int x, int y);
	void clearWall(int x, int y);

	int getWidth() const { return m_width; }
	int getHeight() const { return m_height; }

	// A*/JPS 성능 비교용 테스트 맵을 만들 때 쓴다 - wallRatio 비율만큼 무작위로 벽을 채우되,
	// seed를 고정하면 두 알고리즘이 정확히 같은 맵에서 겨루도록 재현할 수 있다.
	void randomizeWalls(double wallRatio, unsigned int seed);

private:
	int m_width;
	int m_height;
	std::vector<bool> m_walls; // true = 벽

	int toIndex(int x, int y) const { return y * m_width + x; }
};

#endif
