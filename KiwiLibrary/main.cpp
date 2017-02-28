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
#include "KForm.h"
#include "KModelMgr.h"
#include "KFeatureTestor.h"
#include "Utils.h"

vector<KForm> formList(2500);

shared_ptr<KTrie> buildTrie()
{
	shared_ptr<KTrie> kt = make_shared<KTrie>();
	FILE* file;
	if (fopen_s(&file, "../ModelGenerator/model.txt", "r")) throw exception();
	char buf[2048];
	wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;
	int noForm = 0;
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
		formList[noForm] = {buf}; // duplication of forms possible. should remove
		kt->build(buf, &formList[noForm++]);
	}
	fclose(file);
	kt->fillFail();
	return kt;
}



void printJM(const KChunk& c, const char* p)
{
	if (c.isStr()) return printJM(p + c.begin, c.end - c.begin);
	return printf("*"), printJM(c.form->form);
}

void enumPossible(const KModelMgr& mdl, const vector<KChunk>& ch, const char* ostr, vector<pair<vector<pair<string, KPOSTag>>, float>>& ret)
{
	static bool(*vowelFunc[])(const char*, const char*) = {
		KFeatureTestor::isPostposition,
		KFeatureTestor::isVowel,
		KFeatureTestor::isVocalic,
		KFeatureTestor::isVocalicH,
		KFeatureTestor::notVowel,
		KFeatureTestor::notVocalic,
		KFeatureTestor::notVocalicH,
	};
	static bool(*polarFunc[])(const char*, const char*) = {
		KFeatureTestor::isPositive,
		KFeatureTestor::notPositive
	};

	vector<size_t> idx(ch.size());
	while (1)
	{
		const KMorpheme* before = nullptr;
		KPOSTag bfTag = KPOSTag::UNKNOWN;
		float ps = 0;
		vector<pair<string, KPOSTag>> mj;
		for (size_t i = 0; i < idx.size(); i++)
		{
			if (ch[i].isStr())
			{
				auto curTag = mdl.findMaxiumTag(before, i + 1 < idx.size() && !ch[i + 1].isStr() ? ch[i + 1].form->candidate[idx[i + 1]] : nullptr);
				ps += powf(ch[i].end - ch[i].begin, 2.0f) * -0.0625f;
				ps += mdl.getTransitionP(bfTag, curTag);
				bfTag = curTag;
				if (ps < P_MIN) goto next;
				mj.emplace_back(string(ostr + ch[i].begin, ostr + ch[i].end), bfTag);
				before = nullptr;
				continue;
			}
			auto& c = ch[i].form->candidate[idx[i]];
			ps += c->p;
			const char* bBegin = nullptr;
			const char* bEnd = nullptr;
			if (!mj.empty())
			{
				bBegin = &mj.back().first[0];
				bEnd = bBegin + mj.back().first.size();
			}
			if ((int)c->vowel && !vowelFunc[(int)c->vowel - 1](bBegin, bEnd)) goto next;
			if ((int)c->polar && !polarFunc[(int)c->polar - 1](bBegin, bEnd)) goto next;
			if (c->chunks.empty())
			{
				if (!KFeatureTestor::isCorrectEnd(bBegin, bEnd)) goto next;
				mj.emplace_back(c->form, c->tag);
				ps += mdl.getTransitionP(bfTag, c->tag);
				bfTag = c->tag;
			}
			else
			{
				size_t x = 0;
				if (mj.size() && c->chunks[0]->tag == KPOSTag::V)
				{
					if (before) goto next;
					mj.back().first += c->chunks[0]->form;
					x++;
				}
				if(!KFeatureTestor::isCorrectEnd(&mj.back().first[0], &mj.back().first[0] + mj.back().first.size())) goto next;
				for (; x < c->chunks.size(); x++)
				{
					auto& ch = c->chunks[x];
					mj.emplace_back(ch->form, ch->tag);
				}
				ps += mdl.getTransitionP(bfTag, c->chunks[c->chunks[0]->tag == KPOSTag::V ? 1 : 0]->tag);
				bfTag = c->chunks.back()->tag;
			}
			if (ps < P_MIN) goto next;
			before = c;
		}
		ret.emplace_back(move(mj), ps);
	next:;

		idx[0]++;
		for (size_t i = 0; i < idx.size(); i++)
		{
			if (idx[i] >= (ch[i].isStr() ? 1 : ch[i].form->candidate.size()))
			{
				idx[i] = 0;
				if (i + 1 >= idx.size()) goto exit;
				idx[i + 1]++;
			}
		}
	}
exit:;
}

int main()
{
	system("chcp 65001");
	_wsetlocale(LC_ALL, L"korean");

	KModelMgr mdl("../ModelGenerator/pos.txt", "../ModelGenerator/model.txt", /*"../ModelGenerator/combined.txt"*/ nullptr);
	mdl.solidify();
	shared_ptr<KTrie> kt = mdl.makeTrie();

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
				splitJamo(c, wordList.back());
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
		auto ss = kt->split(w);
		printf("Splited : %zd\n", ss.size());
		vector<pair<vector<pair<string, KPOSTag>>, float>> cands;
		for (auto s : ss)
		{
			for (auto t : s)
			{
				printJM(t, &w[0]);
				printf(", ");
			}
			printf("\n");
			//enumPossible(mdl, s, &w[0], cands);
		}
		int n = 0;
		sort(cands.begin(), cands.end(), [](const pair<vector<pair<string, KPOSTag>>, float>& a, const pair<vector<pair<string, KPOSTag>>, float>& b)
		{
			return a.second > b.second;
		});
		for (auto c : cands)
		{
			printf("%03d\t%g\t", n++, c.second);
			for (auto d : c.first)
			{
				printJM(d.first);
				printf("/");
				printf(tagToString(d.second));
				printf(" + ");
			}
			printf("\n");
		}
		printf("\n\n");
		getchar();
	}
	fclose(file);
    return 0;
}

