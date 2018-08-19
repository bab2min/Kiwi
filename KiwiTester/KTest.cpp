#include "stdafx.h"
#include "../KiwiLibrary/KiwiHeader.h"
#include "../KiwiLibrary/Kiwi.h"
#include "KTest.h"
#include "../KiwiLibrary/Utils.h"
#include "LCS.hpp"

using namespace std;

KWordPair parseWordPOS(const u16string& str)
{
	if (str[0] == '/' && str[1] == '/')
	{
		return { u"/", makePOSTag(str.substr(2)), 0, 0 };
	}
	auto p = str.find('/');
	if (p == str.npos) return {};
	return { k_string{ str.begin(), str.begin() + p }, makePOSTag(str.substr(p + 1)), 0, 0 };
}

struct Counter
{
	struct value_type { template<typename T> value_type(const T&) { } };
	void push_back(const value_type&) { ++count; }
	size_t count = 0;
};

KTest::KTest(const char * testSetFile, Kiwi* kw, size_t topN) : totalCount(0), totalScore(0)
{
	FILE* f = nullptr;
	if (fopen_s(&f, testSetFile, "r")) throw exception();
	/*fseek(f, 0, SEEK_END);
	size_t totalSize = ftell(f);
	size_t printed = 0;
	rewind(f);*/
	char buf[32768];
	while (fgets(buf, 32768, f))
	{
		/*size_t cur = ftell(f);
		for (; printed < cur * 100.0 / totalSize; printed++) printf(".");*/
		try 
		{
			auto wstr = utf8_to_utf16(buf);
			if (wstr.back() == '\n') wstr.pop_back();
			auto fd = split(wstr, u'\t');
			if (fd.size() < 2) continue;
			TestResult tr;
			tr.q = fd[0];
			for (size_t i = 1; i < fd.size(); i++) tr.a.emplace_back(parseWordPOS(fd[i]));
			auto cands = kw->analyze(tr.q, topN);
			tr.r = cands[0].first;
			if (tr.a != tr.r)
			{
				auto diff = LCS::getDiff(tr.r.begin(), tr.r.end(), tr.a.begin(), tr.a.end());
				size_t common = 0;
				for (auto&& d : diff)
				{
					if (d.first < 0) tr.dr.emplace_back(d.second);
					else if (d.first > 0) tr.da.emplace_back(d.second);
					else common++;
				}
				tr.score = common / (float)diff.size();
				totalScore += tr.score;
				//tr.cands = cands;
				wrongList.emplace_back(tr);
			}
			else
			{
				totalScore += 1;
			}
			totalCount++;
		}
		catch (exception) 
		{

		}
	}
	fclose(f);
}

float KTest::getScore() const
{
	return totalScore / totalCount;
}

void KTest::TestResult::writeResult(FILE * output) const
{
	fputws((const wchar_t*)q.c_str(), output);
	fprintf(output, "\t%.3g\n", score);
	for (auto _r : da)
	{
		fputws((const wchar_t*)_r.str().c_str(), output);
		fputwc('/', output);
		fputs(tagToString(_r.tag()), output);
		fputwc('\t', output);
	}
	fputwc('\n', output);
	for (auto _r : dr)
	{
		fputws((const wchar_t*)_r.str().c_str(), output);
		fputwc('/', output);
		fputs(tagToString(_r.tag()), output);
		fputwc('\t', output);
	}
	fputwc('\n', output);
	fputwc('\n', output);
	/*for (auto _r : a)
	{
		fputws(_r.first.c_str(), output);
		fputwc('/', output);
		fputs(tagToString(_r.second), output);
		fputwc('\t', output);
	}
	fputwc('\n', output);
	for (auto res : cands)
	{
		for (auto _r : res.first)
		{
			fputws(_r.first.c_str(), output);
			fputwc('/', output);
			fputs(tagToString(_r.second), output);
			fputwc('\t', output);
		}
		fprintf(output, "%.4g\n", res.second);
	}
	fputwc('\n', output);*/
}
