#include <fstream>
#include <iostream>

#include <kiwi/Utils.h>
#include "Evaluator.h"
#include "LCS.hpp"

using namespace std;
using namespace kiwi;

TokenInfo parseWordPOS(const u16string& str)
{
	if (str[0] == '/' && str[1] == '/')
	{
		return { u"/", toPOSTag(str.substr(2)), 0, 0 };
	}
	auto p = str.find('/');
	if (p == str.npos) return {};
	u16string form{ str.begin(), str.begin() + p };
	if (str[p + 1] == 'E')
	{
		if (form[0] == u'아' || form[0] == u'여') form[0] = u'어';
		if (form[0] == u'았' || form[0] == u'였') form[0] = u'었';
	}
	switch (form[0])
	{
	case u'\u3134': // ㄴ
		form[0] = u'\u11AB'; break;
	case u'\u3139': // ㄹ
		form[0] = u'\u11AF'; break;
	case u'\u3141': // ㅁ
		form[0] = u'\u11B7'; break;
	case u'\u3142': // ㅂ
		form[0] = u'\u11B8'; break;
	}
	return { form, toPOSTag(str.substr(p + 1)), 0, 0 };
}

Evaluator::Evaluator(const std::string& testSetFile, Kiwi* kw, size_t topN)
{
	ifstream f{ testSetFile };
	string line;
	while (getline(f, line))
	{
		auto wstr = utf8To16(line);
		if (wstr.back() == '\n') wstr.pop_back();
		auto fd = split(wstr, u'\t');
		if (fd.size() < 2) continue;
		vector<u16string> tokens;
		for (size_t i = 1; i < fd.size(); ++i)
		{
			split(fd[i], u' ', back_inserter(tokens));
		}
		TestResult tr;
		tr.q = fd[0];
		for (auto& t : tokens) tr.a.emplace_back(parseWordPOS(t));
		auto cands = kw->analyze(tr.q, topN, Match::all);
		tr.r = cands[0].first;
		if (tr.a != tr.r)
		{
			auto diff = lcs::getDiff(tr.r.begin(), tr.r.end(), tr.a.begin(), tr.a.end(), [](const TokenInfo& a, const TokenInfo& b)
			{
				if (a.tag != b.tag) return false;
				if (a.tag == POSTag::jko) return true;
				if (a.str == u"은" && u"ᆫ" == b.str) return true;
				if (b.str == u"은" && u"ᆫ" == a.str) return true;
				if (a.str == u"을" && u"ᆯ" == b.str) return true;
				if (b.str == u"을" && u"ᆯ" == a.str) return true;
				if (a.str == u"음" && u"ᆷ" == b.str) return true;
				if (b.str == u"음" && u"ᆷ" == a.str) return true;
				if (a.str == u"그것" && u"그거" == b.str) return true;
				if (b.str == u"그것" && u"그거" == a.str) return true;
				if (a.str == u"것" && u"거" == b.str) return true;
				if (b.str == u"것" && u"거" == a.str) return true;
				return a == b;
			});
			size_t common = 0;
			for (auto&& d : diff)
			{
				if (d.first < 0) tr.dr.emplace_back(d.second);
				else if (d.first > 0) tr.da.emplace_back(d.second);
				else common++;
			}
			tr.score = common / (double)diff.size();
			totalScore += tr.score;
			microCorrect += common;
			microCount += diff.size();
			errors.emplace_back(tr);
		}
		else
		{
			totalScore += 1;
			microCorrect += tr.r.size();
			microCount += tr.r.size();
		}
		totalCount++;
	}
}

double Evaluator::getMacroScore() const
{
	return totalScore / totalCount;
}

double Evaluator::getMicroScore() const
{
	return microCorrect / (double)microCount;
}

ostream& operator<<(ostream& o, const kiwi::TokenInfo& t)
{
	return o << utf16To8(t.str) << "/" << kiwi::tagToString(t.tag);
}

void Evaluator::TestResult::writeResult(ostream& out) const
{
	out << utf16To8(q) << '\t' << score << endl;
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
}
