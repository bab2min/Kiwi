#include "core/Kiwi.h"
#include "core/Utils.h"
#include "KEval.h"
#include "LCS.hpp"

using namespace std;
using namespace kiwi;

KWordPair parseWordPOS(const u16string& str)
{
	if (str[0] == '/' && str[1] == '/')
	{
		return { u"/", makePOSTag(str.substr(2)), 0, 0 };
	}
	auto p = str.find('/');
	if (p == str.npos) return {};
	u16string form{ str.begin(), str.begin() + p };
	if (str[p + 1] == 'E')
	{
		if (form[0] == 0xC544) form[0] = 0xC5B4; // 아
		if (form[0] == 0xC558) form[0] = 0xC5C8; // 았
	}
	switch (form[0])
	{
	case 0x3134: // ㄴ
		form[0] = 0x11AB; break;
	case 0x3139: // ㄹ
		form[0] = 0x11AF; break;
	case 0x3141: // ㅁ
		form[0] = 0x11B7; break;
	case 0x3142: // ㅂ
		form[0] = 0x11B8; break;
	}
	return { form, makePOSTag(str.substr(p + 1)), 0, 0 };
}

struct Counter
{
	struct value_type { template<typename T> value_type(const T&) { } };
	void push_back(const value_type&) { ++count; }
	size_t count = 0;
};

KEval::KEval(const char * testSetFile, Kiwi* kw, size_t topN) : totalCount(0), totalScore(0)
{
	ifstream f{ testSetFile };
	string line;
	while (getline(f, line))
	{
		try 
		{
			auto wstr = utf8_to_utf16(line);
			if (wstr.back() == '\n') wstr.pop_back();
			auto fd = split(wstr, u'\t');
			if (fd.size() < 2) continue;
			TestResult tr;
			tr.q = fd[0];
			for (size_t i = 1; i < fd.size(); i++) tr.a.emplace_back(parseWordPOS(fd[i]));
			auto cands = kw->analyze(tr.q, topN, PatternMatcher::all);
			tr.r = cands[0].first;
			if (tr.a != tr.r)
			{
				auto diff = LCS::getDiff(tr.r.begin(), tr.r.end(), tr.a.begin(), tr.a.end(), [](const KWordPair& a, const KWordPair& b)
				{
					if (a.tag() != b.tag()) return false;
					if (a.tag() == KPOSTag::JKO) return true;
					if (a.str() == u"은" && u"ᆫ" == b.str()) return true;
					if (b.str() == u"은" && u"ᆫ" == a.str()) return true;
					if (a.str() == u"을" && u"ᆯ" == b.str()) return true;
					if (b.str() == u"을" && u"ᆯ" == a.str()) return true;
					if (a.str() == u"음" && u"ᆷ" == b.str()) return true;
					if (b.str() == u"음" && u"ᆷ" == a.str()) return true;
					if (a.str() == u"그것" && u"그거" == b.str()) return true;
					if (b.str() == u"그것" && u"그거" == a.str()) return true;
					if (a.str() == u"것" && u"거" == b.str()) return true;
					if (b.str() == u"것" && u"거" == a.str()) return true;
					return a == b;
				});
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
}

float KEval::getScore() const
{
	return totalScore / totalCount;
}

void KEval::TestResult::writeResult(ostream& out) const
{
	out << utf16_to_utf8(q) << '\t' << score << endl;
	for (auto& _r : da)
	{
		out << _r << '\t';
	}
	out << endl;
	for (auto& _r : dr)
	{
		out << _r << '\t';
	}
	out << endl;
	out << endl;
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
