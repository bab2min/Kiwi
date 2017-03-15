//

#include "stdafx.h"
#include "locale.h"
using namespace std;
#include "Kiwi.h"
#include "Utils.h"
#include "KTest.h"

int main()
{
	system("chcp 65001");
	_wsetlocale(LC_ALL, L"korean");
	Timer timer;
	Kiwi kw;
	kw.prepare();
	printf("Loading Time : %g ms\n", timer.getElapsed());
	Timer total;
	KTest test { "../TestSets/01.txt", &kw };
	double tm = total.getElapsed();
	
	printf("%g\n", test.getScore());
	printf("Total (%zd) Time : %g ms\n", test.getTotalCount(), tm);
	printf("Time per Unit : %g ms\n", tm / test.getTotalCount());
	
	FILE* out;
	fopen_s(&out, "wrongs.txt", "w");
	for (auto t : test.getWrongList())
	{
		fputws(t.q.c_str(), out);
		fputwc('\t', out);
		for (auto r : t.r)
		{
			fputws(r.first.c_str(), out);
			fputwc('/', out);
			fputs(tagToString(r.second), out);
			fputwc('\t', out);
		}
		fputs("\t:\t", out);
		for (auto r : t.a)
		{
			fputws(r.first.c_str(), out);
			fputwc('/', out);
			fputs(tagToString(r.second), out);
			fputwc('\t', out);
		}
		fputwc('\n', out);
	}
	fclose(out);
	getchar();
	/*FILE* file;
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
	getchar();*/
    return 0;
}

