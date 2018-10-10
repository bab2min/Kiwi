//

#include "stdafx.h"
#include <windows.h>
#include <psapi.h>
#include "../KiwiLibrary/KiwiHeader.h"
#include "../KiwiLibrary/Utils.h"
#include "../KiwiLibrary/Kiwi.h"
#include "../KiwiLibrary/KModelMgr.h"
#include "KTest.h"

#include "../KiwiLibrary/KWordDetector.h"

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
		auto nodes = km.getTrie()->split(normalizeHangul(utf8_to_utf16(line)));
		auto res = km.findBestPath(nodes, 10);
		for (auto&& r : res)
		{
			cout << r.second << '\t';
			for (auto&& s : r.first)
			{
				if (s->kform) cout << utf16_to_utf8(*s->kform) << '/' << tagToString(s->tag);
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
				wprintf(L"%s/%s\t", p.str().c_str(), tagToStringW(p.tag()));
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
				wprintf(L"%s/%s\t", p.first.c_str(), tagToStringW(p.second));
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
#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_CHECK_ALWAYS_DF);
#endif
	SetConsoleOutputCP(CP_UTF8);
	setvbuf(stdout, nullptr, _IOFBF, 1000);
	Timer timer;
	Kiwi kw{ "../ModelGenerator/", (size_t)-1, 0 };
	//kw.prepare();
	//kw.analyze(u"±â´ë°¡ÄÇ¾ú´Ù.", 10);
	//kw.setCutOffThreshold(5);
	if(0)
	{
		auto flist = { "xaa", "xab", "xac", "xad", "xae", "xaf" };
		for (auto list : flist)
		{
			ifstream ifs{ string{"D:/"} + list };
			auto res = kw.extractAddWords([&ifs](size_t id) -> u16string
			{
				if (id == 0)
				{
					ifs.clear();
					ifs.seekg(0);
					string line;
				}
				string line;
				while (getline(ifs, line))
				{
					auto sstr = line/*.substr(0, line.find('\t'))*/;
					if (sstr.size()) return utf8_to_utf16(sstr);
				}
				return {};
			}, 16, 20, 0.015f, -3.6);

			ofstream ofs{ string{"extracted_"} + list + ".txt" };
			for (auto& r : res)
			{
				ofs << utf16_to_utf8(r.form) << '\t' << r.score << '\t' << r.freq
					<< '\t' << r.lCohesion << '\t' << r.rCohesion
					<< '\t' << r.lBranch << '\t' << r.rBranch
					<< '\t' << r.posScore[KPOSTag::NNP] << endl;
			}
		}
		return 0;
	}
	kw.loadUserDictionary("dict_namu2.txt");
	kw.prepare();

	if (1)
	{
		Timer tm;
		ifstream ifs{ "D:/namu_raw.txt" };
		ofstream ofs{ "D:/namu_tagged.txt" };
		kw.analyze(1, [&ifs](size_t id) -> u16string
		{
			if (id == 0)
			{
				ifs.clear();
				ifs.seekg(0);
				string line;
			}
			string line;
			while (getline(ifs, line))
			{
				auto sstr = line;
				if (sstr.size()) return utf8_to_utf16(sstr);
			}
			return {};
		}, [&ofs](size_t id, vector<KResult>&& res)
		{
			for (auto& r : res[0].first)
			{
				ofs << utf16_to_utf8(r.str()) << '/' << tagToString(r.tag()) << '\t';
			}
			ofs << endl;
		});
		return 0;
	}

	cout << "Loading Time : " << timer.getElapsed() << " ms" << endl;
	PROCESS_MEMORY_COUNTERS pmc;
	GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
	SIZE_T memUsed = pmc.WorkingSetSize;
	cout << "Mem Usage : " << memUsed / 1024.f / 1024.f << " MB" << endl;
	
	kw.analyze(KSTR(R"!( )!"), 10);

	string testFiles[] = { "01s.txt", "02s.txt", "03s.txt", "17s.txt", "18s.txt", "13s.txt", "15s.txt", };
	for (auto tf : testFiles)
	{
		Timer total;
		KTest test{ ("../TestSets/" + tf).c_str(), &kw };
		double tm = total.getElapsed();

		cout << endl << test.getScore() << endl;
		cout << "Total (" << test.getTotalCount() << ") Time : " << tm << " ms" << endl;
		cout << "Time per Unit : " << tm / test.getTotalCount() << " ms" << endl;
		
		ofstream out{ "wrongsV2" + tf };
		out << test.getScore() << endl;
		out << "Total (" << test.getTotalCount() << ") Time : " << tm << " ms" << endl;
		out << "Time per Unit : " << tm / test.getTotalCount() << " ms" << endl;
		for (auto t : test.getWrongList())
		{
			t.writeResult(out);
		}

		for (auto& p : KSingleLogger::getInstance().totalAlloc)
		{
			cout << p.first << "bytes\t" << p.second << endl;
		}
	}
	getchar();
	return 0;
}

