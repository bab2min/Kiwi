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
	string testFiles[] = { "01.txt", "02.txt", "03.txt" };
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
	}
	getchar();
    return 0;
}

