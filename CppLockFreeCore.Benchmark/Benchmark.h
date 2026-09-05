#pragma once
#ifndef __BENCHMARK__
#define __BENCHMARK__

#include <Windows.h>
#include <vector>
#include <algorithm>
#include <functional>
#include <cstdio>

// 벤치마크 공통 규칙
// - 워밍업(warmup) 실행을 몇 번 버리고 시작한다 (JIT/캐시/스레드 예열 편차 제거 목적 - 여기선 순수
//   네이티브 C++이라 JIT는 없지만, 첫 실행은 페이지 폴트/캐시 미스가 몰려 편차가 크다).
// - 단일 실행 시간이 아니라 N번 반복해서 "평균/중앙값(median)/최댓값"을 같이 보고한다.
//   평균만 보면 한두 번의 이상치(OS 스케줄링 노이즈 등)에 쉽게 휘둘리기 때문에, 중앙값을 기준으로 삼는다.
struct BenchResult
{
	double avgMs;
	double medianMs;
	double minMs;
	double maxMs;
};

class Benchmark
{
public:
	// warmupCount번은 버리고, trialCount번 측정해서 결과를 낸다.
	// fn 한 번 호출이 "측정하고 싶은 작업 전체(1회분)"이다.
	static BenchResult run(int warmupCount, int trialCount, const std::function<void()>& fn)
	{
		for (int i = 0; i < warmupCount; i++)
			fn();

		std::vector<double> samples;
		samples.reserve(trialCount);

		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);

		for (int i = 0; i < trialCount; i++)
		{
			LARGE_INTEGER start, end;
			QueryPerformanceCounter(&start);
			fn();
			QueryPerformanceCounter(&end);
			double ms = (double)(end.QuadPart - start.QuadPart) * 1000.0 / (double)freq.QuadPart;
			samples.push_back(ms);
		}

		std::sort(samples.begin(), samples.end());
		double sum = 0;
		for (double s : samples) sum += s;

		BenchResult result;
		result.avgMs = sum / samples.size();
		result.medianMs = samples[samples.size() / 2];
		result.minMs = samples.front();
		result.maxMs = samples.back();
		return result;
	}

	// 결과 한 줄 출력.
	static void print(const char* name, const BenchResult& r)
	{
		printf("  %-32s 평균 %8.3fms  중앙값 %8.3fms  최소 %8.3fms  최대 %8.3fms\n",
			name, r.avgMs, r.medianMs, r.minMs, r.maxMs);
		fflush(stdout); // 콘솔 종류에 따라 setvbuf(_IOLBF)만으로는 즉시 안 보이는 경우가 있어 확실하게 밀어낸다
	}
};

#endif
