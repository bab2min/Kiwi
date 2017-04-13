//

#include "stdafx.h"
#include "../KiwiLibrary/Kiwi.h"
#include "../KiwiLibrary/Utils.h"
#include "KTest.h"

int main()
{
	system("chcp 65001");
	_wsetlocale(LC_ALL, L"korean");
	Timer timer;
	Kiwi kw("../ModelGenerator/");
	kw.prepare();
	printf("Loading Time : %g ms\n", timer.getElapsed());
	string testFiles[] = { "01s.txt", "02s.txt", "03s.txt" };
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

