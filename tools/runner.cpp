#include <iostream>
#include <fstream>

#include <kiwi/Kiwi.h>
#include <tclap/CmdLine.h>
#include "toolUtils.h"

using namespace std;
using namespace kiwi;

void printResult(Kiwi& kw, 
	const string& line, 
	int topn, 
	bool score, 
	AnalyzeOption option,
	ostream& out)
{
	for (auto& result : kw.analyze(line, topn, option))
	{
		for (auto& t : result.first)
		{
			out << utf16To8(t.str) << '/' << tagToString(t.tag) << '\t';
		}
		if (score) out << setprecision(5) << result.second;
		out << endl;
	}
	if (topn > 1) out << endl;
}

int run(const string& modelPath, bool benchmark, const string& output, const string& user, int topn, int tolerance, 
	float typoCostWeight,
	bool bTypo,
	bool cTypo,
	bool lTypo,
	Dialect allowedDialects,
	float dialectCost,
	bool score, 
	ModelType modelType, 
	Match oovScoringType,
	const vector<string>& input)
{
	try
	{
		tutils::Timer timer;
		size_t lines = 0, bytes = 0;

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

		string typoStr = "";
		if (bTypo)
		{
			typo |= getDefaultTypoSet(DefaultTypoSet::basicTypoSet);
			typoStr += "basic";
		}

		if (cTypo)
		{
			typo |= getDefaultTypoSet(DefaultTypoSet::continualTypoSet);
			if (!typoStr.empty()) typoStr += "+";
			typoStr += "continual";
		}

		if (lTypo)
		{
			typo |= getDefaultTypoSet(DefaultTypoSet::lengtheningTypoSet);
			if (!typoStr.empty()) typoStr += "+";
			typoStr += "lengthening";
		}


		Kiwi kw = KiwiBuilder{ modelPath, 1, BuildOption::default_, modelType }.build();
		auto ptt = typo.prepare(true);

		AnalyzeOption option{ Match::allWithNormalizing | oovScoringType, nullptr, false, allowedDialects, dialectCost };
		option.typoTransformer = &ptt;

		cout << "Kiwi v" << KIWI_VERSION_STRING << endl;
		if (tolerance)
		{
			auto config = kw.getGlobalConfig();
			config.spaceTolerance = tolerance;
			kw.setGlobalConfig(config);
			cout << "SpaceTolerance: " << tolerance << endl;
		}
		if (typoCostWeight > 0)
		{
			auto config = kw.getGlobalConfig();
			config.typoCostWeight = typoCostWeight;
			kw.setGlobalConfig(config);
			cout << "Typo Correction Cost Weight: " << typoCostWeight << endl;
		}

		if (benchmark)
		{
			cout << "Loading Time : " << timer.getElapsed() << " ms" << endl;
			cout << "ArchType : " << archToStr(kw.archType()) << endl;
			cout << "LM Size : " << (kw.getLangModel()->getMemorySize() / 1024. / 1024.) << " MB" << endl;
			cout << "Mem Usage : " << (tutils::getCurrentPhysicalMemoryUsage() / 1024.) << " MB" << endl;
			cout << "ModelType : " << modelTypeToStr(kw.getLangModel()->getType()) << endl;
			cout << "OOV Scoring : " << tutils::oovScoringTypeToStr(oovScoringType) << endl;
			cout << "Typo Correction: " << (typoStr.empty() ? "none" : typoStr) << endl;
		}

		ostream* out = &cout;
		unique_ptr<ofstream> fout;
		if (!output.empty())
		{
			fout = kiwi::make_unique<ofstream>(output);
			out = fout.get();
		}

		if (input.empty())
		{
			timer.reset();
#ifdef _WIN32
			for (wstring line; (cout << ">> ").flush(), getline(wcin, line);)
			{
				printResult(kw, utf16To8((const char16_t*)line.c_str()), topn, score, option, *out);
				++lines;
				bytes += line.size();
			}
#else
			for (string line; (cout << ">> ").flush(), getline(cin, line);)
			{
				printResult(kw, line, topn, score, option, *out);
				++lines;
				bytes += line.size();
			}
#endif
		}
		else
		{
			timer.reset();
			for (auto& f : input)
			{
				ifstream in{ f };
				if (!in)
				{
					throw runtime_error{ "cannot open file: " + f };
				}

				for (string line; getline(in, line);)
				{
					printResult(kw, line, topn, score, option, *out);
					++lines;
					bytes += line.size();
				}
			}
		}

		if (benchmark)
		{
			double tm = timer.getElapsed();
			cout << "Total: " << tm << " ms, " << lines << " lines, " << (bytes / 1024.) << " KB" << endl;
			cout << "Elapsed per line: " << tm / lines << " ms" << endl;
			cout << "Elapsed per KB: " << tm / (bytes / 1024.) << " ms" << endl;
			cout << "KB per second: " << (bytes / 1024.) / (tm / 1000) << " KB" << endl;
			cout << "====================\n" << endl;
		}
		return 0;
	}
	catch (const exception& e)
	{
		cerr << e.what() << endl;
		return -1;
	}
}

using namespace TCLAP;

int main(int argc, const char* argv[])
{
	tutils::setUTF8Output();

	CmdLine cmd{ "Kiwi CLI", ' ', KIWI_VERSION_STRING};

	ValueArg<string> model{ "m", "model", "Kiwi model path", true, "", "string" };
	ValueArg<string> modelType{ "", "model-type", "Model Type", false, "none", "string" };
	SwitchArg benchmark{ "e", "benchmark", "benchmark performance" };
	ValueArg<string> output{ "o", "output", "output file path", false, "", "string" };
	ValueArg<string> user{ "u", "user", "user dictionary path", false, "", "string" };
	ValueArg<int> topn{ "n", "topn", "top-n of result", false, 1, "int > 0" };
	ValueArg<int> tolerance{ "t", "tolerance", "space tolerance of Kiwi", false, 0, "int >= 0" };
	ValueArg<float> typoWeight{ "", "typo", "typo weight", false, 0.f, "float" };
	SwitchArg bTypo{ "", "btypo", "make basic-typo-tolerant model", false };
	SwitchArg cTypo{ "", "ctypo", "make continual-typo-tolerant model", false };
	SwitchArg lTypo{ "", "ltypo", "make lengthening-typo-tolerant model", false };
	ValueArg<string> dialect{ "d", "dialect", "allowed dialect", false, "standard", "string" };
	ValueArg<float> dialectCost{ "", "dialect-cost", "dialect cost", false, 6.f, "float" };
	ValueArg<string> oovScoring{ "x", "oov-scoring", "OOV scoring method (none, rule, chr, chrfreq, chrfreqbranch)", false, "rule", "string" };
	SwitchArg score{ "s", "score", "print score together" };
	UnlabeledMultiArg<string> files{ "inputs", "input files", false, "string" };

	cmd.add(model);
	cmd.add(modelType);
	cmd.add(benchmark);
	cmd.add(output);
	cmd.add(user);
	cmd.add(topn);
	cmd.add(tolerance);
	cmd.add(typoWeight);
	cmd.add(bTypo);
	cmd.add(cTypo);
	cmd.add(lTypo);
	cmd.add(dialect);
	cmd.add(dialectCost);
	cmd.add(oovScoring);
	cmd.add(score);
	cmd.add(files);

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
	ModelType kiwiModelType = ModelType::none;
	try
	{
		kiwiModelType = tutils::parseModelType(modelType);
	}
	catch (const exception& e)
	{
		cerr << e.what() << endl;
		return -1;
	}

	Match oovScoringType = Match::oovRuleOnly;
	try
	{
		oovScoringType = tutils::parseOOVScoring(oovScoring.getValue());
	}
	catch (const exception& e)
	{
		cerr << e.what() << endl;
		return -1;
	}

	return run(model, benchmark, output, user, topn, tolerance, 
		typoWeight, 
		bTypo,
		cTypo,
		lTypo,
		parsedDialect,
		dialectCost,
		score, 
		kiwiModelType, 
		oovScoringType,
		files.getValue());
}

