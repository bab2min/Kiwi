#include "stdafx.h"
#include "../KiwiLibrary/Kiwi.h"
#include "KTest.h"
#include "../KiwiLibrary/Utils.h"

KWordPair parseWordPOS(wstring str)
{
	if (str[0] == '/' && str[1] == '/')
	{
		return { L"/", makePOSTag(str.substr(2)) };
	}
	auto p = str.find('/');
	if (p == str.npos) return {};
	return { str.substr(0, p), makePOSTag(str.substr(p + 1)) };
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
	char buf[4096];
	wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;
	while (fgets(buf, 4096, f))
	{
		try 
		{
			auto wstr = converter.from_bytes(buf);
			if (wstr.back() == '\n') wstr.pop_back();
			auto fd = split(wstr, '\t');
			if (fd.size() < 2) continue;
			TestResult tr;
			tr.q = fd[0];
			for (size_t i = 1; i < fd.size(); i++) tr.a.emplace_back(parseWordPOS(fd[i]));
			auto cands = kw->analyze(tr.q, topN);
			tr.r = cands[0].first;
			if (tr.a != tr.r)
			{
				auto setA = tr.a, setR = tr.r;
				sort(setA.begin(), setA.end());
				sort(setR.begin(), setR.end());
				Counter c;
				set_intersection(setA.begin(), setA.end(), setR.begin(), setR.end(), back_inserter(c));
				tr.score = c.count / float(setA.size() + setR.size() - c.count);
				totalScore += tr.score;
				tr.cands = cands;
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
	fputws(q.c_str(), output);
	fprintf(output, "\t%.3g\n", score);
	for (auto _r : a)
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
	fputwc('\n', output);
}
