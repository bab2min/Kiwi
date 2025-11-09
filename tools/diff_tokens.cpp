#include <iostream>
#include <fstream>

#include <kiwi/Kiwi.h>
#include <kiwi/TypoTransformer.h>
#include <tclap/CmdLine.h>

#include "toolUtils.h"

using namespace std;
using namespace kiwi;
using namespace TCLAP;

Kiwi loadKiwiFromArg(const string& model, const string& modelType, 
	size_t numThreads = 2,
	bool normCoda = true,
	bool zCoda = true,
	bool loadMultiDict = true,
	float typoCostWeight = 0.f,
	bool bTypo = false,
	bool cTypo = false,
	bool lTypo = false,
	Dialect allowedDialects = Dialect::standard)
{
	ModelType kiwiModelType = tutils::parseModelType(modelType);
	BuildOption opt = BuildOption::default_;

	if (!loadMultiDict) opt &= ~BuildOption::loadMultiDict;
	KiwiBuilder builder{ model, numThreads < 2 ? 2 : numThreads, opt, kiwiModelType, allowedDialects };

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

	auto kiwi =  builder.build(typo);
	auto config = kiwi.getGlobalConfig();
	config.typoCostWeight = typoCostWeight;
	kiwi.setGlobalConfig(config);
	return kiwi;
}

inline bool isEqual(const TokenInfo* a, size_t aSize, const TokenInfo* b, size_t bSize, bool ignoreTag = false)
{
	if (aSize != bSize) return false;
	for (size_t i = 0; i < aSize; ++i)
	{
		if (a[i].str != b[i].str) return false;
		if (!ignoreTag && a[i].tag != b[i].tag) return false;
	}
	return true;
}

inline ostream& operator<<(ostream& ostr, const TokenInfo& token)
{
	ostr << utf16To8(token.str);
	if (token.senseId && !isSpecialClass(token.tag))
	{
		ostr << "__" << (int)token.senseId;
	}
	ostr << '/' << tagToString(token.tag);
	return ostr;
}

bool printDiffTokens(ostream& ostr, const string& raw, const TokenInfo* a, size_t aSize, const TokenInfo* b, size_t bSize, bool ignoreTag = false, bool showSame = false)
{
	if (isEqual(a, aSize, b, bSize, ignoreTag) != showSame) return false;
	ostr << raw << '\t';
	for (size_t i = 0; i < aSize; ++i)
	{
		if (i) ostr << ' ';
		ostr << a[i];
	}
	if (!showSame || ignoreTag)
	{
		ostr << '\t';
		for (size_t i = 0; i < bSize; ++i)
		{
			if (i) ostr << ' ';
			ostr << b[i];
		}
	}
	ostr << endl;
	return true;
}

pair<size_t, size_t> diffTokens(ostream& ostr, const string& raw, const TokenResult& a, const TokenResult& b, bool sentenceLevel, bool ignoreTag = false, bool showSame = false)
{
	size_t diff = 0, total = 0;
	if (sentenceLevel)
	{
		thread_local vector<pair<size_t, size_t>> aBounds, bBounds, sentBounds;
		aBounds.clear();
		bBounds.clear();
		sentBounds.clear();
		auto& aTokens = a.first;
		auto& bTokens = b.first;
		for (size_t i = 1; i < aTokens.size(); ++i)
		{
			if (aTokens[i - 1].sentPosition != aTokens[i].sentPosition)
			{
				aBounds.emplace_back(aTokens[i - 1].endPos(), i);
			}
		}

		for (size_t i = 1; i < bTokens.size(); ++i)
		{
			if (bTokens[i - 1].sentPosition != bTokens[i].sentPosition)
			{
				bBounds.emplace_back(bTokens[i - 1].endPos(), i);
			}
		}

		// find intersection between aBounds and bBounds and store in sentBounds
		sentBounds.emplace_back(0, 0);
		auto aIt = aBounds.begin();
		auto bIt = bBounds.begin();
		while (aIt != aBounds.end() && bIt != bBounds.end())
		{
			if (aIt->first < bIt->first)
			{
				++aIt;
			}
			else if (aIt->first > bIt->first)
			{
				++bIt;
			}
			else
			{
				sentBounds.emplace_back(aIt->second, bIt->second);
				++aIt;
				++bIt;
			}
		}
		sentBounds.emplace_back(aTokens.size(), bTokens.size());

		const u16string u16raw = utf8To16(raw);

		for (size_t i = 1; i < sentBounds.size(); ++i)
		{
			const auto aStart = sentBounds[i - 1].first;
			const auto aEnd = sentBounds[i].first;
			const auto bStart = sentBounds[i - 1].second;
			const auto bEnd = sentBounds[i].second;
			const auto rawSent = u16raw.substr(aTokens[aStart].position, aTokens[aEnd - 1].endPos() - aTokens[aStart].position);
			const bool isDiff = printDiffTokens(ostr, utf16To8(rawSent), aTokens.data() + aStart, aEnd - aStart, bTokens.data() + bStart, bEnd - bStart, ignoreTag, showSame);
			if (isDiff) ++diff;
			++total;
		}
		if (showSame) ostr << endl;
	}
	else
	{
		const bool isDiff = printDiffTokens(ostr, raw, a.first.data(), a.first.size(), b.first.data(), b.first.size(), ignoreTag, showSame);
		if (isDiff) ++diff;
		++total;
	}
	return { diff, total };
}

pair<size_t, size_t> diffInputs(Kiwi& kiwiA, Kiwi& kiwiB, const string& inputs, ostream& ostr, bool sentenceLevel, 
	bool ignoreTag = false, bool showSame = false,
	Dialect allowedDialects = Dialect::standard,
	float dialectCost = 6.f
	)
{
	ifstream ifs{ inputs };
	if (!ifs)
	{
		cerr << "Cannot open " << inputs << endl;
		return { 0, 0 };
	}
	string line;
	deque<tuple<string, future<TokenResult>, future<TokenResult>>> futures;
	auto* poolA = kiwiA.getThreadPool();
	auto* poolB = kiwiB.getThreadPool();
	size_t diff = 0, total = 0;

	AnalyzeOption option{ Match::allWithNormalizing, nullptr, false, allowedDialects, dialectCost };

	while (getline(ifs, line))
	{
		while (futures.size() > kiwiA.getNumThreads() * 2)
		{
			auto rawInput = move(get<0>(futures.front()));
			auto resultA = get<1>(futures.front()).get();
			auto resultB = get<2>(futures.front()).get();
			futures.pop_front();

			auto p = diffTokens(ostr, rawInput, resultA, resultB, sentenceLevel, ignoreTag, showSame);
			diff += p.first;
			total += p.second;
		}

		futures.emplace_back(
			line,
			poolA->enqueue([&, line](size_t tid) { return kiwiA.analyze(line, option);}),
			poolB->enqueue([&, line](size_t tid) { return kiwiB.analyze(line, option);})
		);
	}

	while (!futures.empty())
	{
		auto rawInput = move(get<0>(futures.front()));
		auto resultA = get<1>(futures.front()).get();
		auto resultB = get<2>(futures.front()).get();
		futures.pop_front();

		auto p = diffTokens(ostr, rawInput, resultA, resultB, sentenceLevel, ignoreTag, showSame);
		diff += p.first;
		total += p.second;
	}
	return { diff, total };
}

int main(int argc, const char* argv[])
{
	tutils::setUTF8Output();

	CmdLine cmd{ "Kiwi Diff Tokenizations" };

	ValueArg<string> modelA{ "", "model-a", "Model A path", true, "", "string" };
	ValueArg<string> modelAType{ "", "model-a-type", "Model A Type", false, "none", "string" };
	ValueArg<string> modelB{ "", "model-b", "Model B path", true, "", "string" };
	ValueArg<string> modelBType{ "", "model-b-type", "Model B Type", false, "none", "string" };
	ValueArg<string> output{ "o", "output", "output path", false, "", "string" };
	ValueArg<size_t> numThreads{ "t", "threads", "number of threads", false, 2, "int" };
	SwitchArg sentence{ "", "sentence", "diff in sentence level", false };
	SwitchArg ignoreTag{ "i", "ignore-tag", "ignore tag", false };
	SwitchArg showSame{ "s", "show-same", "show the same result only", false };
	SwitchArg noNormCoda{ "", "no-normcoda", "without normalizing coda", false };
	SwitchArg noZCoda{ "", "no-zcoda", "without z-coda", false };
	SwitchArg noMulti{ "", "no-multi", "turn off multi dict", false };
	ValueArg<float> typoWeight{ "", "typo", "typo weight", false, 0.f, "float" };
	SwitchArg bTypo{ "", "btypo", "make basic-typo-tolerant model", false };
	SwitchArg cTypo{ "", "ctypo", "make continual-typo-tolerant model", false };
	SwitchArg lTypo{ "", "ltypo", "make lengthening-typo-tolerant model", false };
	ValueArg<string> dialect{ "d", "dialect", "allowed dialect", false, "standard", "string" };
	ValueArg<float> dialectCost{ "", "dialect-cost", "dialect cost", false, 6.f, "float" };
	UnlabeledMultiArg<string> inputs{ "inputs", "targets", false, "string" };

	cmd.add(modelA);
	cmd.add(modelAType);
	cmd.add(modelB);
	cmd.add(modelBType);
	cmd.add(output);
	cmd.add(inputs);
	cmd.add(numThreads);
	cmd.add(sentence);
	cmd.add(ignoreTag);
	cmd.add(showSame);
	cmd.add(noNormCoda);
	cmd.add(noZCoda);
	cmd.add(noMulti);
	cmd.add(typoWeight);
	cmd.add(bTypo);
	cmd.add(cTypo);
	cmd.add(lTypo);
	cmd.add(dialect);
	cmd.add(dialectCost);

	try
	{
		cmd.parse(argc, argv);
	}
	catch (const ArgException& e)
	{
		cerr << "error: " << e.error() << " for arg " << e.argId() << endl;
		return -1;
	}
	
	Dialect parsedDialect = parseDialects(dialect.getValue());

	Kiwi kiwiA = loadKiwiFromArg(modelA, modelAType, numThreads,
		!noNormCoda,
		!noZCoda,
		!noMulti,
		typoWeight,
		bTypo,
		cTypo,
		lTypo,
		parsedDialect);

	Kiwi kiwiB = loadKiwiFromArg(modelB, modelBType, numThreads,
		!noNormCoda,
		!noZCoda,
		!noMulti,
		typoWeight,
		bTypo,
		cTypo,
		lTypo,
		parsedDialect);

	unique_ptr<ofstream> ofstr;
	ostream* ostr = &cout;
	if (!output.getValue().empty())
	{
		ofstr = std::make_unique<ofstream>(output);
		ostr = ofstr.get();
	}

	cout << "Model A: " << modelA.getValue() << endl;
	cout << "Model B: " << modelB.getValue() << endl;
	for (auto& input : inputs)
	{
		cout << "input: " << input << " ";
		cout.flush();
		auto p = diffInputs(kiwiA, kiwiB, input, *ostr, sentence, ignoreTag, showSame, parsedDialect, dialectCost);
		cout << "(diff: " << p.first << " / " << p.second << ")" << endl;
	}
}
