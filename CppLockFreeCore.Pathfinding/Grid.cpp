#include "Grid.h"
#include <random>

Grid::Grid(int width, int height)
	: m_width(width), m_height(height), m_walls(static_cast<size_t>(width) * height, false)
{
}

bool Grid::isInBounds(int x, int y) const
{
	return x >= 0 && y >= 0 && x < m_width && y < m_height;
}

bool Grid::isWalkable(int x, int y) const
{
	if (!isInBounds(x, y))
		return false;
	return !m_walls[toIndex(x, y)];
}

void Grid::setWall(int x, int y)
{
	if (isInBounds(x, y))
		m_walls[toIndex(x, y)] = true;
}

void Grid::clearWall(int x, int y)
{
	if (isInBounds(x, y))
		m_walls[toIndex(x, y)] = false;
}

void Grid::randomizeWalls(double wallRatio, unsigned int seed)
{
	std::mt19937 rng(seed);
	std::uniform_real_distribution<double> dist(0.0, 1.0);

	for (int y = 0; y < m_height; y++)
	{
		for (int x = 0; x < m_width; x++)
			m_walls[toIndex(x, y)] = (dist(rng) < wallRatio);
	}
}
