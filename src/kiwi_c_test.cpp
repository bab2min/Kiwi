#include <cstdio>
#include <tchar.h>
#include <windows.h>
#include <locale.h>

#include "kiwi_c.h"

int reader(int id, kchar16_t* buf, void* user)
{
	if (id >= 10)
	{
		return 0;
	}
	if (buf == nullptr) return 20;
	swprintf_s((wchar_t*)buf, 20, L"테스트 %d입니다.", id);
}

int receiver(int id, PKIWIRESULT kr, void* user)
{
	int size = kiwiResult_getSize(kr);
	for (int i = 0; i < size; i++)
	{
		printf("%g\t", kiwiResult_getProb(kr, i));
		int num = kiwiResult_getWordNum(kr, i);
		for (int j = 0; j < num; j++)
		{
			wprintf(L"%s/%s(%d~%d)\t", kiwiResult_getWordFormW(kr, i, j), kiwiResult_getWordTagW(kr, i, j),
				kiwiResult_getWordPosition(kr, i, j), kiwiResult_getWordPosition(kr, i, j) + kiwiResult_getWordLength(kr, i, j));
		}
		printf("\n");
	}

	kiwiResult_close(kr);
	return 0;
}

int main()
{
	system("chcp 65001");
	_wsetlocale(LC_ALL, L"korean");
	for (size_t t = 0; t < 10; ++t)
	{
		PKIWI kw = kiwi_init("ModelGenerator/", 0, 0);
		kiwi_prepare(kw);
		kiwi_analyzeMW(kw, reader, receiver, nullptr, 10);
		PKIWIRESULT kr;
		FILE* f = fopen("test/sample/longText.txt", "r");
		fseek(f, 0, SEEK_END);
		size_t len = ftell(f);
		kchar16_t* longText = (kchar16_t*)malloc(len);
		fseek(f, 0, SEEK_SET);
		fread(longText, 1, len, f);
		fclose(f);
		kr = kiwi_analyzeW(kw, (const kchar16_t*)longText, 1);
		free(longText);
		int size = kiwiResult_getSize(kr);
		for (int i = 0; i < size; i++)
		{
			printf("%g\t", kiwiResult_getProb(kr, i));
			int num = kiwiResult_getWordNum(kr, i);
			for (int j = 0; j < num && j < 1000; j++)
			{
				wprintf(L"%s/%s(%d~%d)\t", kiwiResult_getWordFormW(kr, i, j), kiwiResult_getWordTagW(kr, i, j),
					kiwiResult_getWordPosition(kr, i, j), kiwiResult_getWordPosition(kr, i, j) + kiwiResult_getWordLength(kr, i, j));
			}
			printf("\n");
		}
		kiwiResult_close(kr);
		kiwi_close(kw);
	}
	getchar();
    return 0;
}

