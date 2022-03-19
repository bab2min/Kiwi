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

int doEvaluate(const string& modelPath, const string& output, const vector<string>& input)
{
	try
	{
		tutils::Timer timer;
		Kiwi kw = KiwiBuilder{ modelPath, 1 }.build();
		
		cout << "Loading Time : " << timer.getElapsed() << " ms" << endl;
		cout << "LM Size : " << (kw.getLangModel()->getMemory().size() / 1024. / 1024.) << " MB" << endl;
		cout << "Mem Usage : " << (tutils::getCurrentPhysicalMemoryUsage() / 1024.) << " MB\n" << endl;
		
		double avgMicro = 0, avgMacro = 0;
		double cnt = 0;
		for (auto& tf : input)
		{
			cout << "Test file: " << tf << endl;
			try
			{
				Evaluator test{ tf, &kw };
				tutils::Timer total;
				test.run();
				double tm = total.getElapsed();
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

	ValueArg<string> model{ "m", "model", "Kiwi model path", false, "ModelGenerator", "string" };
	ValueArg<string> output{ "o", "output", "output dir for evaluation errors", false, "", "string" };
	UnlabeledMultiArg<string> files{ "files", "evaluation set files", true, "string" };

	cmd.add(model);
	cmd.add(output);
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
	return doEvaluate(model, output, files.getValue());
}

