#include <iostream>
#include <fstream>

#include <kiwi/Kiwi.h>
#include <tclap/CmdLine.h>
#include "toolUtils.h"

using namespace std;
using namespace kiwi;

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
	ValueArg<size_t> lmMinCnt{ "", "min_cnt", "min count of LM", false, 1, "int" };
	ValueArg<size_t> lmLastOrderMinCnt{ "", "last_min_cnt", "min count of the last order of LM", false, 2, "int" };
	ValueArg<string> output{ "o", "output", "output model path", true, "", "string" };
	ValueArg<size_t> sbgSize{ "", "sbg_size", "sbg size", false, 1000000, "int" };
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
	args.lmMinCnt = lmMinCnt;
	args.lmLastOrderMinCnt = lmLastOrderMinCnt;
	args.numWorkers = workers;
	args.sbgSize = sbgSize;
	return run(args, output, skipBigram);
}

