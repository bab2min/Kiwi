#include <iostream>
#include <fstream>

#include <kiwi/Kiwi.h>
#include <tclap/CmdLine.h>
#include "toolUtils.h"

using namespace std;
using namespace kiwi;

vector<size_t> splitMultipleInts(const string& s, const char delim = ',')
{
	vector<size_t> ret;
	size_t p = 0, e = 0;
	while (1)
	{
		size_t t = s.find(delim, p);
		if (t == s.npos)
		{
			ret.emplace_back(atoi(&s[e]));
			return ret;
		}
		else
		{
			ret.emplace_back(atoi(&s[e]));
			p = t + 1;
			e = t + 1;
		}
	}
}

int run(const KiwiBuilder::ModelBuildArgs& args, const string& output, bool skipBigram)
{
	try
	{
		tutils::Timer timer;
		if (skipBigram)
		{
			cout << "Build SkipBigram model based on KnLM: " << output << endl;
			KiwiBuilder kb{ output, args };
		}
		else
		{
			KiwiBuilder{ args }.saveModel(output);
		}
		double tm = timer.getElapsed();
		cout << "Total: " << tm << " ms " << endl;
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

	CmdLine cmd{ "Kiwi ModelBuilder", ' ', "0.11.0" };

	ValueArg<string> morpheme{ "", "morpheme", "morpheme files", true, "", "string" };
	SwitchArg compress{ "", "compress", "compress LM" };
	SwitchArg quantize{ "", "quantize", "quantize LM" };
	SwitchArg tagHistory{ "", "history", "use tag history of LM" };
	SwitchArg skipBigram{ "", "skipbigram", "build skipbigram model" };
	ValueArg<size_t> workers{ "w", "workers", "number of workers", false, 1, "int" };
	ValueArg<size_t> morMinCnt{ "", "morpheme_min_cnt", "min count of morpheme", false, 10, "int" };
	ValueArg<size_t> lmOrder{ "", "order", "order of LM", false, 4, "int" };
	ValueArg<string> lmMinCnt{ "", "min_cnt", "min count of LM", false, "1", "multiple ints with comma"};
	ValueArg<size_t> lmLastOrderMinCnt{ "", "last_min_cnt", "min count of the last order of LM", false, 2, "int" };
	ValueArg<string> output{ "o", "output", "output model path", true, "", "string" };
	ValueArg<size_t> sbgSize{ "", "sbg_size", "sbg size", false, 1000000, "int" };
	ValueArg<double> sbgEpochs{ "", "sbg_epochs", "sbg epochs", false, 10, "double" };
	ValueArg<size_t> sbgEvalSetRatio{ "", "sbg_eval_ratio", "", false, 20, "int" };
	ValueArg<size_t> sbgMinCnt{ "", "sbg_min_cnt", "", false, 150, "int" };
	ValueArg<size_t> sbgMinCoCnt{ "", "sbg_min_co_cnt", "", false, 20, "int" };
	UnlabeledMultiArg<string> inputs{ "inputs", "input copora", true, "string" };

	cmd.add(output);
	cmd.add(inputs);
	cmd.add(morpheme);
	cmd.add(compress);
	cmd.add(quantize);
	cmd.add(tagHistory);
	cmd.add(skipBigram);
	cmd.add(morMinCnt);
	cmd.add(lmOrder);
	cmd.add(lmMinCnt);
	cmd.add(lmLastOrderMinCnt);
	cmd.add(workers);
	cmd.add(sbgSize);
	cmd.add(sbgEpochs);
	cmd.add(sbgEvalSetRatio);
	cmd.add(sbgMinCnt);
	cmd.add(sbgMinCoCnt);

	try
	{
		cmd.parse(argc, argv);
	}
	catch (const ArgException& e)
	{
		cerr << "error: " << e.error() << " for arg " << e.argId() << endl;
		return -1;
	}
	KiwiBuilder::ModelBuildArgs args;
	args.morphemeDef = morpheme;
	args.corpora = inputs.getValue();
	args.compressLm = compress;
	args.quantizeLm = quantize;
	args.useLmTagHistory = tagHistory;
	args.minMorphCnt = morMinCnt;
	args.lmOrder = lmOrder;
	args.numWorkers = workers;
	args.sbgSize = sbgSize;
	args.sbgEpochs = sbgEpochs;
	args.sbgEvalSetRatio = sbgEvalSetRatio;
	args.sbgMinCount = sbgMinCnt;
	args.sbgMinCoCount = sbgMinCoCnt;

	auto v = splitMultipleInts(lmMinCnt.getValue());
	
	if (v.empty())
	{
		args.lmMinCnts.resize(1, 1);
	}
	else if (v.size() == 1 || v.size() == lmOrder)
	{
		args.lmMinCnts = v;
	}
	else
	{
		cerr << "error: min_cnt size should be 1 or equal to order" << endl;
		return -1;
	}
	return run(args, output, skipBigram);
}

