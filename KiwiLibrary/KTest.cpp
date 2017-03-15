#include "stdafx.h"
#include "KTest.h"
#include "Utils.h"

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

KTest::KTest(const char * testSetFile, Kiwi* kw) : totalCount(0)
{
	FILE* f = nullptr;
	if (fopen_s(&f, testSetFile, "r")) throw exception();
	char buf[2048];
	wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;
	while (fgets(buf, 2048, f))
	{
		auto wstr = converter.from_bytes(buf);
		if (wstr.back() == '\n') wstr.pop_back();
		auto fd = split(wstr, '\t');
		if (fd.size() < 2) continue;
		TestResult tr;
		tr.q = fd[0];
		for (size_t i = 1; i < fd.size(); i++) tr.a.emplace_back(parseWordPOS(fd[i]));
		tr.r = kw->analyze(tr.q).first;
		if (tr.a != tr.r) wrongList.emplace_back(tr);
		totalCount++;
	}
	fclose(f);
}

float KTest::getScore() const
{
	return 1 - wrongList.size() / (float)totalCount;
}