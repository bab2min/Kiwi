//

#include "stdafx.h"
#include "locale.h"
#include <chrono>

using namespace std;
#include "Kiwi.h"

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

int main()
{
	system("chcp 65001");
	_wsetlocale(LC_ALL, L"korean");
	Timer total, timer;
	Kiwi kw;
	kw.prepare();
	printf("Loading Time : %g ms\n", timer.getElapsed());
	FILE* file;
	if (fopen_s(&file, "../TestFiles/Spok.txt", "r")) throw exception();
	char buf[2048];
	wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;
	vector<string> wordList;
	wordList.emplace_back();
	size_t unit = 0;
	while (fgets(buf, 2048, file))
	{
		auto wstr = converter.from_bytes(buf);
		timer.reset();
		auto res = kw.analyze(wstr);
		double tm = timer.getElapsed();
		unit += res.first.size();
		if (!res.first.empty()) printf("Analyze (%zd) Time : %g ms\n", res.first.size(), tm);
		if (tm > 10 && false)
		{
			for (auto w : res.first)
			{
				wprintf(w.first.c_str());
				printf("/");
				printf(tagToString(w.second));
				printf(" + ");
			}
			getchar();
		}
		//printf("\n");
	}
	fclose(file);
	double tm = total.getElapsed();
	printf("Total (%zd) Time : %g ms\n", unit, tm);
	printf("Time per Unit : %g ms\n", tm / unit);
	getchar();
    return 0;
}

