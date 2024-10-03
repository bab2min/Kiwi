#include <fstream>
#include <iostream>

#include <kiwi/Utils.h>
#include "../src/StrUtils.h"
#include "Evaluator.h"
#include "LCS.hpp"

using namespace std;
using namespace kiwi;

TokenInfo parseWordPOS(const u16string& str)
{
	auto p = str.rfind('/');
	if (p == str.npos) return {};
	u16string form = replace(nonstd::u16string_view(str.data(), p), u"_", u" ");
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
	u16string tagStr = str.substr(p + 1);
	if (tagStr.find('-') != tagStr.npos)
	{
		tagStr.erase(tagStr.begin() + tagStr.find('-'), tagStr.end());
	}
	POSTag tag = toPOSTag(tagStr);
	if (tag >= POSTag::max) throw runtime_error{ "Wrong Input '" + utf16To8(str.substr(p + 1)) + "'" };
	return { form, tag, 0, 0 };
}

Evaluator::Evaluator(const std::string& testSetFile, const Kiwi* _kw, Match _matchOption, size_t _topN)
	: kw{ _kw }, matchOption{ _matchOption }, topN{ _topN }
{
	ifstream f{ testSetFile };
	if (!f) throw std::ios_base::failure{ "Cannot open '" + testSetFile + "'" };
	string line;
	while (getline(f, line))
	{
		while (line.back() == '\n' || line.back() == '\r') line.pop_back();
		auto wstr = utf8To16(line);
		auto fd = split(wstr, u'\t');
		if (fd.size() < 2) continue;
		vector<u16string> tokens;
		for (size_t i = 1; i < fd.size(); ++i)
		{
			for (auto s : split(fd[i], u' ')) tokens.emplace_back(s.to_string());
		}
		TestResult tr;
		tr.q = fd[0].to_string();
		for (auto& t : tokens) tr.a.emplace_back(parseWordPOS(t));
		testsets.emplace_back(std::move(tr));
	}
}

void Evaluator::run()
{
	for (auto& tr : testsets)
	{
		auto cands = kw->analyze(tr.q, topN, matchOption);
		tr.r = cands[0].first;
	}
}

Evaluator::Score Evaluator::evaluate()
{
	errors.clear();

	size_t totalCount = 0, microCorrect = 0, microCount = 0;
	double totalScore = 0;

	for (auto& tr : testsets)
	{
		if (tr.a != tr.r)
		{
			auto diff = lcs::getDiff(tr.r.begin(), tr.r.end(), tr.a.begin(), tr.a.end(), [](const TokenInfo& a, const TokenInfo& b)
			{
				if (clearIrregular(a.tag) != clearIrregular(b.tag)) return false;
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
				return a.str == b.str;
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
	Score ret;
	ret.micro = microCorrect / (double)microCount;
	ret.macro = totalScore / totalCount;
	ret.totalCount = totalCount;
	return ret;
}

ostream& operator<<(ostream& o, const kiwi::TokenInfo& t)
{
	o << utf16To8(t.str);
	if (t.senseId) o << "__" << (int)t.senseId;
	o << "/" << kiwi::tagToString(t.tag);
	return o;
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
