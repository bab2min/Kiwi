﻿#include <iostream>
#include <fstream>

#include <windows.h>
#include <psapi.h>
#include <kiwi/Utils.h>
#include <kiwi/PatternMatcher.h>
#include "KEval.h"
#include <kiwi/Kiwi.h>

class Timer
{
public:
	std::chrono::high_resolution_clock::time_point point;
	Timer()
	{
		reset();
	}
	void reset()
	{
		point = std::chrono::high_resolution_clock::now();
	}

	double getElapsed() const
	{
		return std::chrono::duration <double, std::milli>(std::chrono::high_resolution_clock::now() - point).count();
	}
};

using namespace std;
using namespace kiwi;

/*
int main()
{
	//SetConsoleOutputCP(CP_UTF8);
	//setvbuf(stdout, nullptr, _IOFBF, 1000);
	KModelMgr km{ "../ModelGenerator/" };
	km.solidify();
	string line;
	wstring_convert<codecvt_utf8_utf16<wchar_t>, wchar_t> cvt;
	while (getline(cin, line))
	{
		auto nodes = km.getTrie()->split(normalizeHangul(utf8To16(line)));
		auto res = km.findBestPath(nodes, 10);
		for (auto&& r : res)
		{
			cout << r.second << '\t';
			for (auto&& s : r.first)
			{
				if (s->kform) cout << utf16To8(*s->kform) << '/' << tagToString(s->tag);
			}
		}
	}
	return 0;
}
*/

/*int main2()
{
	system("chcp 65001");
	_wsetlocale(LC_ALL, L"korean");
	Kiwi kw{ "../ModelGenerator/" };
	kw.prepare();
	Timer timer;
	auto text = L"마쳤다.";
	for (int i = 0; i < 1; i++)
	{
		kw.clearCache();
		auto res = kw.analyze(text, 10);
		if(i == 0) for (auto r : res)
		{
			printf("%.3g\t", r.second);
			for (auto& p : r.first)
			{
				wprintf(L"%s/%s\t", p.str().c_str(), tagToKString(p.tag()));
			}
			printf("\n");
		}
	}
	printf("\n==== %gms\n", timer.getElapsed());
	timer.reset();
	for (int i = 0; i < 500; i++)
	{
		kw.clearCache();
		auto res = kw.analyzeOld(text, 5);
		if (i == 0) for (auto r : res)
		{
			printf("%.3g\t", r.second);
			for (auto& p : r.first)
			{
				wprintf(L"%s/%s\t", p.first.c_str(), tagToKString(p.second));
			}
			printf("\n");
		}
	}
	printf("\n==== %gms\n", timer.getElapsed());
	getchar();
	return 0;
}
*/

int main()
{
/*#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_CHECK_ALWAYS_DF);
#endif*/
	SetConsoleOutputCP(CP_UTF8);
	setvbuf(stdout, nullptr, _IOFBF, 1000);

	//KiwiBuilder{ KiwiBuilder::fromRawDataTag, "ModelGenerator", 0 }.saveModel("ModelGenerator");

	Timer timer;
	Kiwi kw = KiwiBuilder{ "ModelGenerator" }.build();
	//Kiwi kw{ "ModelGenerator/", (size_t)-1, 0, 3 };
	//kw.prepare();
	//kw.analyze(u"남미풍의 강렬한 원색끼리의 조화, 수채화 같이 안온한 배색 등 색의 분위기를 강조하는 기하학적 무늬, 꽃무늬 디자인이 주류를 이루고 있다.", 10);
	//return 0;
	//kw.setCutOffThreshold(10);
	/*if (0)
	{
		auto flist = { "kowiki.txt" };
		for (auto list : flist)
		{
			
			auto res = kw.extractAddWords([&]()
			{
				auto ifs = make_shared<ifstream>(string{"G:/"} + list);
				return [&]() -> u16string
				{
					string line;
					while (getline(*ifs, line))
					{
						if (line.size()) return utf8To16(line);
					}
					return {};
				};
			}, 16, 20, 0.015f, -3.6);

			ofstream ofs{ string{"extracted_"} + list + ".txt" };
			for (auto& r : res)
			{
				ofs << utf16To8(r.form) << '\t' << r.score << '\t' << r.freq
					<< '\t' << r.lCohesion << '\t' << r.rCohesion
					<< '\t' << r.lBranch << '\t' << r.rBranch
					<< '\t' << r.posScore[POSTag::nnp] << endl;
			}
		}
		return 0;
	}*/
	//kw.addUserWord(u"골리", POSTag::nnp, -5);
	//kw.prepare();
	/*auto ret = kw.analyze(u8R""(너도 곧 알게될거야. '알게될거야'는 노래 제목이다.)"", 10, PatternMatcher::all);
	for (auto& p : ret[0].first)
	{
		cout << p << endl;
	}

	return 0;*/

	if (0)
	{
		Timer tm;
		ifstream ifs{ "G:/namu_raw.txt" };
		ofstream ofs{ "G:/namu_tagged.txt" };
		kw.analyze(1, [&ifs]() -> u16string
		{
			string line;
			while (getline(ifs, line))
			{
				auto sstr = line;
				if (sstr.size()) return utf8To16(sstr);
			}
			return {};
		}, [&ofs](size_t id, vector<TokenResult>&& res)
		{
			for (auto& r : res[0].first)
			{
				ofs << utf16To8(r.str) << '/' << tagToString(r.tag) << ' ';
			}
			ofs << endl;
		}, Match::all);
		return 0;
	}

	cout << "Loading Time : " << timer.getElapsed() << " ms" << endl;
	PROCESS_MEMORY_COUNTERS pmc;
	GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
	SIZE_T memUsed = pmc.WorkingSetSize;
	cout << "Mem Usage : " << memUsed / 1024.f / 1024.f << " MB" << endl;

	string testFiles[] = { "01s.txt", "02s.txt", "03s.txt", "17s.txt", "18s.txt", "13s.txt", 
		//"15s.txt",
	};
	for (auto& tf : testFiles)
	{
		Timer total;
		KEval test{ ("data/evaluation/" + tf).c_str(), &kw };
		double tm = total.getElapsed();

		cout << endl << test.getScore() << endl;
		cout << "Total (" << test.getTotalCount() << ") Time : " << tm << " ms" << endl;
		cout << "Time per Unit : " << tm / test.getTotalCount() << " ms" << endl;
		
		ofstream out{ "eval_result/wrongsV2" + tf };
		out << test.getScore() << endl;
		out << "Total (" << test.getTotalCount() << ") Time : " << tm << " ms" << endl;
		out << "Time per Unit : " << tm / test.getTotalCount() << " ms" << endl;
		for (auto t : test.getWrongList())
		{
			t.writeResult(out);
		}

		/*for (auto& p : KSingleLogger::getInstance().totalAlloc)
		{
			cout << p.first << "bytes\t" << p.second << endl;
		}*/
	}
	//getchar();
	return 0;
}

