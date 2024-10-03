#include <iostream>
#include <fstream>

#include <kiwi/Utils.h>
#include <kiwi/PatternMatcher.h>
#include <kiwi/Kiwi.h>
#include <tclap/CmdLine.h>
#include "toolUtils.h"
#include "Evaluator.h"

using namespace std;
using namespace kiwi;

int doEvaluate(const string& modelPath, const string& output, const vector<string>& input, 
	bool normCoda, bool zCoda, bool multiDict, bool useSBG, 
	float typoCostWeight, bool bTypo, bool cTypo, bool lTypo,
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

		Kiwi kw = KiwiBuilder{ modelPath, 1, option, useSBG }.build(
			typo
		);
		if (typoCostWeight > 0) kw.setTypoCostWeight(typoCostWeight);
		
		cout << "Loading Time : " << timer.getElapsed() << " ms" << endl;
		cout << "ArchType : " << archToStr(kw.archType()) << endl;
		cout << "LM Size : " << (kw.getKnLM()->getMemory().size() / 1024. / 1024.) << " MB" << endl;
		cout << "Mem Usage : " << (tutils::getCurrentPhysicalMemoryUsage() / 1024.) << " MB\n" << endl;
		
		double avgMicro = 0, avgMacro = 0;
		double cnt = 0;
		for (auto& tf : input)
		{
			cout << "Test file: " << tf << endl;
			try
			{
				Evaluator test{ tf, &kw, (normCoda ? Match::allWithNormalizing : Match::all) & ~(zCoda ? Match::none : Match::zCoda) };
				tutils::Timer total;
				for (int i = 0; i < repeat; ++i)
				{
					test.run();
				}
				double tm = total.getElapsed() / repeat;
				auto result = test.evaluate();

				cout << result.micro << ", " << result.macro << endl;
				cout << "Total (" << result.totalCount << " lines) Time : " << tm << " ms" << endl;
				cout << "Time per Line : " << tm / result.totalCount << " ms" << endl;

				avgMicro += result.micro;
				avgMacro += result.macro;
				cnt++;

				if (!output.empty())
				{
					const size_t last_slash_idx = tf.find_last_of("\\/");
					string name;
					if (last_slash_idx != tf.npos) name = tf.substr(last_slash_idx + 1);
					else name = tf;

					ofstream out{ output + "/" + name };
					out << result.micro << ", " << result.macro << endl;
					out << "Total (" << result.totalCount << ") Time : " << tm << " ms" << endl;
					out << "Time per Unit : " << tm / result.totalCount << " ms" << endl;
					for (auto t : test.getErrors())
					{
						t.writeResult(out);
					}
				}
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

using namespace TCLAP;

int main(int argc, const char* argv[])
{
	CmdLine cmd{ "Kiwi evaluator" };

	ValueArg<string> model{ "m", "model", "Kiwi model path", false, "models/base", "string" };
	ValueArg<string> output{ "o", "output", "output dir for evaluation errors", false, "", "string" };
	SwitchArg noNormCoda{ "", "no-normcoda", "without normalizing coda", false };
	SwitchArg noZCoda{ "", "no-zcoda", "without z-coda", false };
	SwitchArg noMulti{ "", "no-multi", "turn off multi dict", false };
	SwitchArg useSBG{ "", "sbg", "use SkipBigram", false };
	ValueArg<float> typoWeight{ "", "typo", "typo weight", false, 0.f, "float"};
	SwitchArg bTypo{ "", "btypo", "make basic-typo-tolerant model", false };
	SwitchArg cTypo{ "", "ctypo", "make continual-typo-tolerant model", false };
	SwitchArg lTypo{ "", "ltypo", "make lengthening-typo-tolerant model", false };
	ValueArg<int> repeat{ "", "repeat", "repeat evaluation for benchmark", false, 1, "int" };
	UnlabeledMultiArg<string> files{ "files", "evaluation set files", true, "string" };

	cmd.add(model);
	cmd.add(output);
	cmd.add(files);
	cmd.add(noNormCoda);
	cmd.add(noZCoda);
	cmd.add(noMulti);
	cmd.add(useSBG);
	cmd.add(typoWeight);
	cmd.add(bTypo);
	cmd.add(cTypo);
	cmd.add(lTypo);
	cmd.add(repeat);

	try
	{
		cmd.parse(argc, argv);
	}
	catch (const ArgException& e)
	{
		cerr << "error: " << e.error() << " for arg " << e.argId() << endl;
		return -1;
	}
	return doEvaluate(model, output, files.getValue(), 
		!noNormCoda, !noZCoda, !noMulti, useSBG, typoWeight, bTypo, cTypo, lTypo, repeat);
}

