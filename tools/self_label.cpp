#include <iostream>
#include <fstream>

#include <kiwi/Kiwi.h>
#include <kiwi/TypoTransformer.h>
#include <tclap/CmdLine.h>

#include "toolUtils.h"

using namespace std;
using namespace kiwi;
using namespace TCLAP;

Kiwi loadKiwiFromArg(const string& model, 
	const string& modelType, 
	const string& customDict,
	size_t numThreads = 2,
	bool normCoda = true,
	bool zCoda = true,
	bool loadMultiDict = true,
	float typoCostWeight = 0.f,
	bool bTypo = false,
	bool cTypo = false,
	bool lTypo = false,
	Dialect allowedDialects = Dialect::standard
)
{
	ModelType kiwiModelType = tutils::parseModelType(modelType);
	BuildOption opt = BuildOption::default_;

	if (!loadMultiDict) opt &= ~BuildOption::loadMultiDict;
	KiwiBuilder builder{ model, numThreads < 2 ? 2 : numThreads, opt, kiwiModelType, allowedDialects };

	if (!customDict.empty())
	{
		const size_t addedCnt = builder.loadDictionary(customDict);
		cerr << "Loaded custom dictionary: " << customDict << " (+" << addedCnt << " words)" << endl;
	}

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

	auto kiwi = builder.build(typo);
	if (typoCostWeight > 0)
	{
		kiwi.setTypoCostWeight(typoCostWeight);
	}
	return kiwi;
}


inline ostream& operator<<(ostream& ostr, const TokenInfo& token)
{
	ostr << utf16To8(token.str);
	if (token.senseId && !isSpecialClass(token.tag))
	{
		ostr << "__" << (int)token.senseId;
	}
	ostr << '\t' << tagToString(token.tag);
	return ostr;
}

UnorderedMap<pair<u16string, POSTag>, float> loadMorphemeRef(const string& path)
{
	UnorderedMap<pair<u16string, POSTag>, float> refMap;
	ifstream ifs{ path };
	if (!ifs)
	{
		cerr << "Cannot open morpheme ref file: " << path << endl;
		return refMap;
	}
	string line;

	vector<tuple<u16string, POSTag, uint64_t>> cnts;
	uint64_t totalCount = 0;
	while (getline(ifs, line))
	{
		auto parts = tutils::split(line, '\t');
		if (parts.size() < 3) continue;
		u16string form = utf8To16(string{ parts[0] });
		POSTag tag = toPOSTag(utf8To16(string{ parts[1] }));
		size_t count = stoull(string{ parts[2] });
		cnts.emplace_back(form, tag, count);
		totalCount += count;
	}

	for (auto& t : cnts)
	{
		const float prob = (float)(log((double)(get<2>(t) + 1)) - log((double)totalCount));
		refMap.emplace(make_pair(move(get<0>(t)), get<1>(t)), prob);
	}
	return refMap;
}

bool writeResult(ostream& ostr, const string& raw, const TokenResult& result, 
	float filterThreshold = 5, 
	const UnorderedMap<pair<u16string, POSTag>, float>& morphemeRef = {}
	)
{
	auto& tokens = result.first;

	if (filterThreshold > 0)
	{
		for (auto& token : tokens)
		{
			if ((token.tag == POSTag::nng || token.tag == POSTag::nnp) && token.morph->getForm().empty())
			{
				//cerr << "Low quality analysis detected. Skipped: " << utf16To8(token.str) << endl;
				return false;
			}
			if (isJClass(token.tag) || isEClass(token.tag))
			{
				auto it = morphemeRef.find(make_pair(token.str, token.tag));
				if (it != morphemeRef.end() && token.score < it->second - filterThreshold)
				{
					//cerr << "Low quality analysis detected. Skipped: " << utf16To8(token.str) << endl;
					return false;
				}
			}
		}
	}

	auto utf16raw = utf8To16(raw);
	
	if (tokens.empty()) return false;

	size_t prevIdx = 0;
	uint32_t prevWordPosition = tokens[0].wordPosition;
	uint32_t prevSentPosition = tokens[0].sentPosition;

	for (size_t i = 1; i < tokens.size(); ++i)
	{
		if ((prevWordPosition == tokens[i].wordPosition) &&
			(prevSentPosition == tokens[i].sentPosition))
		{

		}
		else
		{
			const size_t start = tokens[prevIdx].position, end = tokens[i - 1].position + tokens[i - 1].length;
			
			ostr << utf16To8(utf16raw.substr(start, end - start));
			for (size_t j = prevIdx; j < i; ++j)
			{
				ostr << '\t' << tokens[j];
			}
			ostr << '\n';

			prevIdx = i;
			prevWordPosition = tokens[i].wordPosition;
			prevSentPosition = tokens[i].sentPosition;
		}
	}

	if (prevIdx < tokens.size())
	{
		const size_t start = tokens[prevIdx].position, end = tokens.back().position + tokens.back().length;
		ostr << utf16To8(utf16raw.substr(start, end - start));
		for (size_t j = prevIdx; j < tokens.size(); ++j)
		{
			ostr << '\t' << tokens[j];
		}
		ostr << '\n';
	}
	ostr << endl;
	return true;
}

pair<size_t, size_t> selfLabel(Kiwi& kiwi, const string& inputs, ostream& ostr, 
	Dialect allowedDialects = Dialect::standard,
	float dialectCost = 6.f,
	const UnorderedMap<pair<u16string, POSTag>, float>& morphemeRef = {},
	size_t skipOffset = 0
	)
{
	ifstream ifs{ inputs };
	if (!ifs)
	{
		cerr << "Cannot open " << inputs << endl;
		return {0, 0};
	}
	string line;
	deque<tuple<string, future<TokenResult>>> futures;
	auto* pool = kiwi.getThreadPool();
	size_t total = 0, printed = 0;

	AnalyzeOption option{ Match::allWithNormalizing | Match::splitSaisiot, nullptr, false, allowedDialects, dialectCost };

	for (size_t n = 0; getline(ifs, line); ++n)
	{
		if (n < skipOffset) continue;
		while (futures.size() > kiwi.getNumThreads() * 8)
		{
			auto rawInput = move(get<0>(futures.front()));
			auto result = get<1>(futures.front()).get();
			futures.pop_front();

			printed += writeResult(ostr, rawInput, result, true, morphemeRef) ? 1 : 0;
			++total;
			if (total % 100 == 0)
			{
				cerr << printed << " / " << (total + skipOffset) << " lines processed." << endl;
			}
		}

		futures.emplace_back(
			line,
			pool->enqueue([&, line](size_t tid) { return kiwi.analyze(line, option);})
		);
	}

	while (!futures.empty())
	{
		auto rawInput = move(get<0>(futures.front()));
		auto result = get<1>(futures.front()).get();
		futures.pop_front();

		printed += writeResult(ostr, rawInput, result, true, morphemeRef) ? 1 : 0;
		++total;
		if (total % 100 == 0)
		{
			cerr << printed << " / " << (total + skipOffset) << " lines processed." << endl;
		}
	}
	return { printed, total };
}

int main(int argc, const char* argv[])
{
	tutils::setUTF8Output();

	CmdLine cmd{ "Kiwi Diff Tokenizations" };

	ValueArg<string> model{ "", "model", "Model A path", true, "", "string" };
	ValueArg<string> modelType{ "", "model-type", "Model A Type", false, "none", "string" };
	ValueArg<string> customDict{ "c", "custom-dict", "Custom dictionary path", false, "", "string" };
	ValueArg<string> output{ "o", "output", "output path", false, "", "string" };
	ValueArg<size_t> numThreads{ "t", "threads", "number of threads", false, 2, "int" };
	SwitchArg noNormCoda{ "", "no-normcoda", "without normalizing coda", false };
	SwitchArg noZCoda{ "", "no-zcoda", "without z-coda", false };
	SwitchArg noMulti{ "", "no-multi", "turn off multi dict", false };
	ValueArg<float> typoWeight{ "", "typo", "typo weight", false, 0.f, "float" };
	SwitchArg bTypo{ "", "btypo", "make basic-typo-tolerant model", false };
	SwitchArg cTypo{ "", "ctypo", "make continual-typo-tolerant model", false };
	SwitchArg lTypo{ "", "ltypo", "make lengthening-typo-tolerant model", false };
	ValueArg<string> dialect{ "d", "dialect", "allowed dialect", false, "standard", "string" };
	ValueArg<float> dialectCost{ "", "dialect-cost", "dialect cost", false, 6.f, "float" };
	ValueArg<string> morphemeRef{ "r", "morpheme-ref", "reference morpheme def", false, "", "string" };
	ValueArg<size_t> skipOffset{ "", "skip-offset", "skip offset for input files", false, 0, "int" };
	UnlabeledMultiArg<string> inputs{ "inputs", "targets", false, "string" };

	cmd.add(model);
	cmd.add(modelType);
	cmd.add(customDict);
	cmd.add(output);
	cmd.add(numThreads);
	cmd.add(noNormCoda);
	cmd.add(noZCoda);
	cmd.add(noMulti);
	cmd.add(typoWeight);
	cmd.add(bTypo);
	cmd.add(cTypo);
	cmd.add(lTypo);
	cmd.add(dialect);
	cmd.add(dialectCost);
	cmd.add(morphemeRef);
	cmd.add(skipOffset);
	cmd.add(inputs);

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

	Kiwi kiwi = loadKiwiFromArg(
		model, 
		modelType, 
		customDict,
		numThreads,
		!noNormCoda,
		!noZCoda,
		!noMulti,
		typoWeight,
		bTypo,
		cTypo,
		lTypo,
		parsedDialect);

	UnorderedMap<pair<u16string, POSTag>, float> morphemeRefMap;
	if (!morphemeRef.getValue().empty())
	{
		morphemeRefMap = loadMorphemeRef(morphemeRef);
	}

	unique_ptr<ofstream> ofstr;
	ostream* ostr = &cout;
	if (!output.getValue().empty())
	{
		ofstr = std::make_unique<ofstream>(output);
		ostr = ofstr.get();
	}

	for (auto& input : inputs)
	{
		cout << "input: " << input << endl;
		auto p = selfLabel(kiwi, input, *ostr, parsedDialect, dialectCost, morphemeRefMap, skipOffset);
		cout << "Completed. " << p.first << " / " << p.second << " lines written." << endl;
	}
}
