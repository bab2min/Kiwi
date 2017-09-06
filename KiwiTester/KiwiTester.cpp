//

#include "stdafx.h"
#include "../KiwiLibrary/KiwiHeader.h"
#include "../KiwiLibrary/Kiwi.h"
#include "KTest.h"

class Timer
{
public:
	chrono::steady_clock::time_point point;
	Timer()
	{
		reset();
	}
	void reset()
	{
		point = chrono::high_resolution_clock::now();
	}

	double getElapsed() const
	{
		return chrono::duration <double, milli>(chrono::high_resolution_clock::now() - point).count();
	}
};

#include "windows.h"
#include "psapi.h"
#include "../KiwiLibrary/Utils.h"
int main2()
{
	system("chcp 65001");
	_wsetlocale(LC_ALL, L"korean");
	Kiwi kw{ "../ModelGenerator/" };
	kw.prepare();
	Timer timer;
	auto text = L"¸¶ÃÆ´Ù.";
	for (int i = 0; i < 1; i++)
	{
		kw.clearCache();
		auto res = kw.analyze(text, 10);
		if(i == 0) for (auto r : res)
		{
			printf("%.3g\t", r.second);
			for (auto& p : r.first)
			{
				wprintf(L"%s/%s\t", p.first.c_str(), tagToStringW(p.second));
			}
			printf("\n");
		}
	}
	printf("\n==== %gms\n", timer.getElapsed());
	/*timer.reset();
	for (int i = 0; i < 500; i++)
	{
		kw.clearCache();
		auto res = kw.analyzeOld(text, 5);
		if (i == 0) for (auto r : res)
		{
			printf("%.3g\t", r.second);
			for (auto& p : r.first)
			{
				wprintf(L"%s/%s\t", p.first.c_str(), tagToStringW(p.second));
			}
			printf("\n");
		}
	}
	printf("\n==== %gms\n", timer.getElapsed());*/
	getchar();
	return 0;
}

int main()
{
	system("chcp 65001");
	_wsetlocale(LC_ALL, L"korean");
	Timer timer;
	Kiwi kw{ "../ModelGenerator/"};
	kw.prepare();
	printf("Loading Time : %g ms\n", timer.getElapsed());
	PROCESS_MEMORY_COUNTERS pmc;
	GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
	SIZE_T memUsed = pmc.WorkingSetSize;
	printf("Mem Usage : %g MB\n", memUsed / 1024.f / 1024.f);
#ifdef CUSTOM_ALLOC
	//*
	for (auto i : KSingleLogger::getInstance().totalAlloc)
	{
		printf("%zd\t%zd\n", i.first, i.second);
	}
	printf("\n");
	for (auto i : KSingleLogger::getInstance().maxAlloc)
	{
		printf("%zd\t%zd\n", i.first, i.second);
	}
	//*/
#endif
	string testFiles[] = { "01s.txt", "02s.txt", "03s.txt", "17s.txt", "18s.txt"};
	for (auto tf : testFiles)
	{
		Timer total;
		KTest test{ ("../TestSets/" + tf).c_str(), &kw };
		double tm = total.getElapsed();

		printf("%g\n", test.getScore());
		printf("Total (%zd) Time : %g ms\n", test.getTotalCount(), tm);
		printf("Time per Unit : %g ms\n", tm / test.getTotalCount());

		FILE* out;
		fopen_s(&out, ("wrongs" + tf).c_str(), "w");
		fprintf(out, "%g\n", test.getScore());
		fprintf(out, "Total (%zd) Time : %g ms\n", test.getTotalCount(), tm);
		fprintf(out, "Time per Unit : %g ms\n\n", tm / test.getTotalCount());
		for (auto t : test.getWrongList())
		{
			t.writeResult(out);
		}
		fclose(out);

#ifdef CUSTOM_ALLOC
//*
		for (auto i : KSingleLogger::getInstance().totalAlloc)
		{
			printf("%zd\t%zd\n", i.first, i.second);
		}
		printf("\n");
		for (auto i : KSingleLogger::getInstance().maxAlloc)
		{
			printf("%zd\t%zd\n", i.first, i.second);
		}
//*/
#endif
	}
	getchar();
	return 0;
}

