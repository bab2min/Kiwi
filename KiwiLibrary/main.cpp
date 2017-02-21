//

#include "stdafx.h"
#include "locale.h"
#include <memory>
#include <iostream>
#include <string>
#include <locale>
#include <codecvt>

using namespace std;


#include "KTrie.h"
#include "Utils.h"

shared_ptr<KTrie> buildTrie()
{
	shared_ptr<KTrie> kt = make_shared<KTrie>();
	FILE* file;
	if (fopen_s(&file, "../ModelGenerator/model.txt", "r")) throw exception();
	char buf[2048];
	wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;
	while (fgets(buf, 2048, file))
	{
		auto wstr = converter.from_bytes(buf);
		auto p = wstr.find('\t');
		if (p == wstr.npos) continue;
		int i = 0;
		for (auto w : wstr.substr(0, p))
		{
			if (w < 0x3130) continue;
			buf[i++] = w - 0x3130;
		}
		if (!i) continue;
		buf[i] = 0;
		kt->build(buf);
	}
	fclose(file);
	kt->fillFail();
	return kt;
}

void printJM(const char* c, size_t len)
{
	auto e = c + len;
	for (;*c && c < e;c++)
	{
		wprintf(L"%c", *c + 0x3130);
	}
}

int main()
{
	system("chcp 65001");
	_wsetlocale(LC_ALL, L"korean");

	shared_ptr<KTrie> kt = buildTrie();

	FILE* file;
	if (fopen_s(&file, "../TestFiles/01.txt", "r")) throw exception();
	char buf[2048];
	wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;
	vector<vector<char>> wordList;
	wordList.emplace_back();
	while (fgets(buf, 2048, file))
	{
		auto wstr = converter.from_bytes(buf);
		for (auto c : wstr)
		{
			if (0xAC00 <= c && c < 0xD7A4)
			{
				auto jm = splitJamo(c);
				wordList.back().insert(wordList.back().end(), jm.begin(), jm.end());
			}
			else
			{
				if (wordList.back().empty()) continue;
				else wordList.emplace_back();
			}
		}
	}
	for (auto w : wordList) 
	{
		if (w.empty()) continue;
		printJM(&w[0], w.size());
		printf("\n");
		auto pats = kt->searchAllPatterns(w);
		for (auto pat : pats)
		{
			printJM(&w[pat.first], pat.second - pat.first);
			printf(", ");
		}
		printf("\n\n");
	}
	fclose(file);
    return 0;
}

