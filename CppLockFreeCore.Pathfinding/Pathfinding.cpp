// A*와 JPS(Jump Point Search)를 같은 격자, 같은 시작/도착점으로 돌려서
// 경로 길이(정합성 확인용)와 확장 노드 수, 걸린 시간을 나란히 비교하는 데모.
// 기존 포트폴리오 PDF에서 "부록"으로 다루던 길찾기 파트를 별도 프로젝트로 옮겨왔다 -
// 세션/네트워크를 다루는 Core 라이브러리와는 직접적인 연관이 없는 독립 기능이라
// 여기서는 Core를 참조하지 않고 그리드/알고리즘만 다룬다.

#include <windows.h>
#include <cstdio>
#include "Grid.h"
#include "AStar.h"
#include "JPS.h"

namespace
{
	double queryElapsedMs(LARGE_INTEGER start, LARGE_INTEGER end, LARGE_INTEGER freq)
	{
		return (double)(end.QuadPart - start.QuadPart) * 1000.0 / (double)freq.QuadPart;
	}

	// 무작위 벽 맵에서 시작/도착점이 벽에 파묻히지 않도록 좌상단/우하단 근처를 강제로 뚫어준다.
	void carveEndpoints(Grid& grid, Point start, Point goal)
	{
		grid.clearWall(start.x, start.y);
		grid.clearWall(goal.x, goal.y);
	}

	void runComparison(int width, int height, double wallRatio, unsigned int seed)
	{
		Grid grid(width, height);
		grid.randomizeWalls(wallRatio, seed);

		Point start = { 0, 0 };
		Point goal = { width - 1, height - 1 };
		carveEndpoints(grid, start, goal);

		LARGE_INTEGER freq, t0, t1;
		QueryPerformanceFrequency(&freq);

		QueryPerformanceCounter(&t0);
		PathResult astarResult = AStar::findPath(grid, start, goal);
		QueryPerformanceCounter(&t1);
		astarResult.elapsedMs = queryElapsedMs(t0, t1, freq);

		QueryPerformanceCounter(&t0);
		PathResult jpsResult = JPS::findPath(grid, start, goal);
		QueryPerformanceCounter(&t1);
		jpsResult.elapsedMs = queryElapsedMs(t0, t1, freq);

		printf("격자 %4d x %-4d (벽 비율 %.0f%%)\n", width, height, wallRatio * 100.0);

		if (!astarResult.found || !jpsResult.found)
		{
			printf("  경로를 못 찾음 (A*=%s, JPS=%s) - 이 시드/비율에서는 시작과 도착이 막혀있음\n",
				astarResult.found ? "성공" : "실패", jpsResult.found ? "성공" : "실패");
			return;
		}

		// 두 알고리즘의 경로 "칸 수"가 같아야 정상이다 - JPS가 확장 노드를 줄이는 최적화일 뿐
		// 실제로 찾아내는 최단 경로 자체는 A*와 동일해야 하기 때문에, 여기서 어긋나면 구현 버그다.
		bool sameLength = (astarResult.path.size() == jpsResult.path.size());

		printf("  A*  : 경로 길이=%4zu  확장노드=%6d  소요=%6.3fms\n",
			astarResult.path.size(), astarResult.nodesExpanded, astarResult.elapsedMs);
		printf("  JPS : 경로 길이=%4zu  확장노드=%6d  소요=%6.3fms  %s\n",
			jpsResult.path.size(), jpsResult.nodesExpanded, jpsResult.elapsedMs,
			sameLength ? "(A*와 경로 길이 일치)" : "(!! 경로 길이 불일치 - 버그 의심)");
	}
}

int main()
{
	setvbuf(stdout, NULL, _IOLBF, 1024); // 콘솔로 redirect될 때도 로그가 실시간으로 보이도록 라인버퍼링

	printf("=== A* vs JPS 길찾기 비교 ===\n\n");

	// 격자 크기를 키워가면서 비교 - 맵이 커질수록 JPS의 확장노드 절감 효과가 더 뚜렷하게 보여야 한다.
	runComparison(50, 50, 0.2, 1234);
	runComparison(200, 200, 0.2, 1234);
	runComparison(500, 500, 0.2, 1234);
	runComparison(500, 500, 0.35, 5678); // 벽이 더 많은 맵에서의 비교

	printf("\n종료하려면 Enter를 누르세요...\n");
	getchar();
	return 0;
}
