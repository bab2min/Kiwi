#include "stdafx.h"
#include "KTrie.h"
#include "KForm.h"
#include "Utils.h"

void splitJamo(wchar_t c, string& ret)
{
	static char choTable[] = { 1, 2, 4, 7, 8, 9, 17, 18, 19, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30 };
	static char jongTable[] = { 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 21, 22, 23, 24, 26, 27, 28, 29, 30 };
	auto t = c - 0xAC00;
	int jong = t % 28;
	int jung = (t / 28) % 21;
	int cho = (t / 28 / 21);
	ret.push_back(choTable[cho]);
	ret.push_back(jung + 31);
	if (jong) ret.push_back(jongTable[jong - 1]);
}


string splitJamo(wstring hangul)
{
	string ret;
	for (auto c : hangul)
	{
		assert(0xac00 <= c && c < 0xd7a4);
		splitJamo(c, ret);
	}
	return ret;
}

bool verifyHangul(wstring hangul)
{
	for (auto c : hangul)
	{
		if (!(0xac00 <= c && c < 0xd7a4)) return false;
	}
	return true;
}

wstring joinJamo(string jm)
{
	static char choInvTable[] = { -1, 0, 1, -1, 2, -1, -1, 3, 4, 5, -1, -1, -1, -1, -1, -1, -1, 6, 7, 8, -1, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
	static char jongInvTable[] = { 0, 1, 2, 3, 4, 5, 6, 7, -1, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, -1, 18, 19, 20, 21, 22, -1, 23, 24, 25, 26, 27};
	wstring ret;
	char cho = 0, jung = 0, jong = 0;
	auto flush = [&]()
	{
		if (!cho && jung)
		{
			ret.push_back(0x3130 + jung);
			jung = 0;
			return;
		}
		if (!jung && cho)
		{
			ret.push_back(0x3130 + cho);
			cho = 0;
			return;
		}

		wchar_t hangul = (choInvTable[cho] * 21 + (jung-31)) * 28 + jongInvTable[jong] + 0xAC00;
		ret.push_back(hangul);
		cho = jung = jong = 0;
	};

	for (auto c : jm)
	{
		if (c <= 30)
		{
			if (!cho)
			{
				cho = c;
			}
			else if (!jung || jongInvTable[c] < 0)
			{
				flush();
				cho = c;
			}
			else  if(!jong)
			{
				jong = c;
			}
			else
			{
				flush();
				cho = c;
			}
		}
		else
		{
			if (!cho)
			{
				jung = c;
				flush();
			}
			else if (!jung)
			{
				jung = c;
			}
			else if (!jong)
			{
				flush();
				jung = c;
			}
			else if(choInvTable[jong] >= 0)
			{
				auto t = jong;
				jong = 0;
				flush();
				cho = t;
				jung = c;
			}
			else
			{
				flush();
				jung = c;
				flush();
			}
		}
	}
	flush();
	return ret;
}

void printJM(const char* c, size_t len)
{
	auto e = c + len;
	for (; *c && c < e; c++)
	{
		wprintf(L"%c", *c + 0x3130);
	}
}

void printJM(const string& c)
{
	if (c.empty()) return;
	return printJM(&c[0], c.size());
}

void printJM(const KChunk& c, const char* p)
{
	if (c.isStr()) return printJM(p + c.begin, c.end - c.begin);
	return printf("*"), printJM(c.form->form);
}