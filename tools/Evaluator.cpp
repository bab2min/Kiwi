#include <fstream>
#include <iostream>

#include <kiwi/Utils.h>
#include "../src/StrUtils.h"
#include "Evaluator.h"
#include "toolUtils.h"
#include "LCS.hpp"

using namespace std;
using namespace kiwi;

unique_ptr<Evaluator> Evaluator::create(const std::string& evalType)
{
	if (evalType == "morph") return std::make_unique<MorphEvaluator>();
	if (evalType == "disamb") return std::make_unique<DisambEvaluator>();
	throw runtime_error{ "Unknown Evaluator Type" };
}

inline ostream& operator<<(ostream& o, const kiwi::TokenInfo& t)
{
	o << utf16To8(t.str);
	if (t.senseId) o << "__" << (int)t.senseId;
	o << "/" << kiwi::tagToString(t.tag);
	return o;
}

inline TokenInfo parseWordPOS(const u16string& str)
{
	auto p = str.rfind('/');
	if (p == str.npos) return {};
	u16string form;
	auto f = str.rfind(u"__", p);
	if (f != str.npos) form = str.substr(0, f);
	else form = str.substr(0, p);

	form = replace(u16string_view{ form.data(), form.size() }, u"_", u" ");
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
	if (clearIrregular(tag) >= POSTag::max) throw runtime_error{ "Wrong Input '" + utf16To8(str.substr(p + 1)) + "'" };
	return { form, tag, 0, 0 };
}

int Evaluator::operator()(const string& modelPath, 
	const string& output, 
	const vector<string>& input,
	bool normCoda, bool zCoda, bool multiDict, ModelType modelType,
	float typoCostWeight, bool bTypo, bool cTypo, bool lTypo,
	Dialect allowedDialect,
	int repeat)
{
	try
	{
		if (typoCostWeight > 0 && !bTypo && !cTypo && !lTypo)
		{
			bTypo = true;
		}
		else if (typoCostWeight == 0)
		{
			bTypo = false;
			cTypo = false;
			lTypo = false;
		}

		tutils::Timer timer;
		auto option = (BuildOption::default_ & ~BuildOption::loadMultiDict) | (multiDict ? BuildOption::loadMultiDict : BuildOption::none);
		auto typo = getDefaultTypoSet(DefaultTypoSet::withoutTypo);

		if (bTypo)
		{
			typo |= getDefaultTypoSet(DefaultTypoSet::basicTypoSet);
		}

		if (cTypo)
		{
			typo |= getDefaultTypoSet(DefaultTypoSet::continualTypoSet);
		}

		if (lTypo)
		{
			typo |= getDefaultTypoSet(DefaultTypoSet::lengtheningTypoSet);
		}

		Kiwi kw = KiwiBuilder{ modelPath, 1, option, modelType, allowedDialect }.build(
			typo
		);
		if (typoCostWeight > 0)
		{
			auto config = kw.getGlobalConfig();
			config.typoCostWeight = typoCostWeight;
			kw.setGlobalConfig(config);
		}

		cout << "Loading Time : " << timer.getElapsed() << " ms" << endl;
		cout << "ArchType : " << archToStr(kw.archType()) << endl;
		cout << "Model Type : " << modelTypeToStr(kw.modelType()) << endl;
		if (kw.getLangModel())
		{
			cout << "LM Size : " << (kw.getLangModel()->getMemorySize() / 1024. / 1024.) << " MB" << endl;
		}
		cout << "Mem Usage : " << (tutils::getCurrentPhysicalMemoryUsage() / 1024.) << " MB\n" << endl;

		double avgMicro = 0, avgMacro = 0;
		double cnt = 0;
		for (auto& tf : input)
		{
			cout << "Test file: " << tf << endl;
			try
			{
				auto result = eval(output, tf, kw, normCoda, zCoda, allowedDialect, repeat);
				avgMicro += result.first;
				avgMacro += result.second;
				++cnt;
				cout << "================" << endl;
			}
			catch (const std::exception& e)
			{
				cerr << e.what() << endl;
			}
		}

		cout << endl << "================" << endl;
		cout << "Avg Score" << endl;
		cout << avgMicro / cnt << ", " << avgMacro / cnt << endl;
		cout << "================" << endl;
		return 0;
	}
	catch (const exception& e)
	{
		cerr << e.what() << endl;
		return -1;
	}
}

auto MorphEvaluator::loadTestset(const string& testSetFile) const -> vector<TestResult>
{
	vector<TestResult> ret;
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
		for (auto s : split(fd[1], u' ')) tokens.emplace_back(s);
		TestResult tr;
		tr.q = u16string{ fd[0] };
		for (auto& t : tokens) tr.a.emplace_back(parseWordPOS(t));
		ret.emplace_back(std::move(tr));
	}
	return ret;
}

auto MorphEvaluator::computeScore(vector<TestResult>& preds, vector<TestResult>& errors) const -> Score
{
	errors.clear();

	size_t totalCount = 0, microCorrect = 0, microCount = 0;
	double totalScore = 0;

	for (auto& tr : preds)
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

auto DisambEvaluator::computeScore(vector<TestResult>& preds, vector<TestResult>& errors) const -> Score
{
	errors.clear();
	Score score;
	for (auto& tr : preds)
	{
		bool correct = false;
		for (auto& token : tr.result.first)
		{
			if (token.str == tr.target.str && 
				clearIrregular(token.tag) == clearIrregular(tr.target.tag))
			{
				correct = true;
				break;
			}
		}
		if (correct) score.acc += 1;
		else errors.emplace_back(tr);
		score.totalCount++;
	}
	score.acc /= score.totalCount;
	return score;
}

void MorphEvaluator::TestResult::writeResult(ostream& out) const
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

pair<double, double> MorphEvaluator::eval(const string& output, const string& file, kiwi::Kiwi& kiwi, bool normCoda, bool zCoda, Dialect allowedDialect, int repeat)
{
	const size_t topN = 1;
	AnalyzeOption option;
	option.match = (normCoda ? Match::allWithNormalizing : Match::all) & ~(zCoda ? Match::none : Match::zCoda);
	option.allowedDialects = allowedDialect;
	vector<TestResult> testsets = loadTestset(file), errors;
	tutils::Timer total;
	for (int i = 0; i < repeat; ++i)
	{
		for (auto& tr : testsets)
		{
			auto cands = kiwi.analyze(tr.q, topN, option);
			tr.r = cands[0].first;
		}
	}
	double tm = total.getElapsed() / repeat;
	auto score = computeScore(testsets, errors);

	cout << score.micro << ", " << score.macro << endl;
	cout << "Total (" << score.totalCount << " lines) Time : " << tm << " ms" << endl;
	cout << "Time per Line : " << tm / score.totalCount << " ms" << endl;

	if (!output.empty())
	{
		const size_t last_slash_idx = file.find_last_of("\\/");
		string name;
		if (last_slash_idx != file.npos) name = file.substr(last_slash_idx + 1);
		else name = file;

		ofstream out{ output + "/" + name };
		out << score.micro << ", " << score.macro << endl;
		out << "Total (" << score.totalCount << ") Time : " << tm << " ms" << endl;
		out << "Time per Unit : " << tm / score.totalCount << " ms" << endl;
		for (auto t : errors)
		{
			t.writeResult(out);
		}
	}
	return make_pair(score.micro, score.macro);
}

auto DisambEvaluator::loadTestset(const string& testSetFile) const -> vector<TestResult>
{
	vector<TestResult> ret;
	ifstream f{ testSetFile };
	if (!f) throw std::ios_base::failure{ "Cannot open '" + testSetFile + "'" };
	string line;
	while (getline(f, line))
	{
		while (line.back() == '\n' || line.back() == '\r') line.pop_back();
		auto wstr = utf8To16(line);
		auto fd = split(wstr, u'\t');
		if (fd.size() < 2) continue;
		TestResult tr;
		tr.target = parseWordPOS(u16string{ fd[0] });
		tr.text = u16string{ fd[1] };
		ret.emplace_back(move(tr));
	}
	return ret;
}

void DisambEvaluator::TestResult::writeResult(ostream& out) const
{
	out << target << '\t' << utf16To8(text) << '\t' << score << endl;
	for (auto& _r : result.first)
	{
		out << _r << '\t';
	}
	out << endl;
	out << endl;
}

pair<double, double> DisambEvaluator::eval(const string& output, const string& file, kiwi::Kiwi& kiwi, bool normCoda, bool zCoda, Dialect allowedDialect, int repeat)
{
	const size_t topN = 1;
	AnalyzeOption option;
	option.match = (normCoda ? Match::allWithNormalizing : Match::all) & ~(zCoda ? Match::none : Match::zCoda);
	option.allowedDialects = allowedDialect;
	vector<TestResult> testsets = loadTestset(file), errors;
	tutils::Timer total;
	for (int i = 0; i < repeat; ++i)
	{
		for (auto& tr : testsets)
		{
			auto cands = kiwi.analyze(tr.text, topN, option);
			tr.result = cands[0];
		}
	}
	double tm = total.getElapsed() / repeat;
	auto score = computeScore(testsets, errors);

	cout << score.acc << endl;
	cout << "Total (" << score.totalCount << " lines) Time : " << tm << " ms" << endl;
	cout << "Time per Line : " << tm / score.totalCount << " ms" << endl;

	if (!output.empty())
	{
		const size_t last_slash_idx = file.find_last_of("\\/");
		string name;
		if (last_slash_idx != file.npos) name = file.substr(last_slash_idx + 1);
		else name = file;

		ofstream out{ output + "/" + name };
		out << score.acc << endl;
		out << "Total (" << score.totalCount << ") Time : " << tm << " ms" << endl;
		out << "Time per Unit : " << tm / score.totalCount << " ms" << endl;
		for (auto t : errors)
		{
			t.writeResult(out);
		}
	}
	return make_pair(score.acc, score.acc);
}
