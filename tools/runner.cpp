#include <iostream>
#include <fstream>

#include <kiwi/Kiwi.h>
#include <tclap/CmdLine.h>
#include "toolUtils.h"

using namespace std;
using namespace kiwi;

void printResult(Kiwi& kw, const string& line, int topn, ostream& out)
{
	for (auto& result : kw.analyze(line, topn, Match::all))
	{
		for (auto& t : result.first)
		{
			out << utf16To8(t.str) << '/' << tagToString(t.tag) << '\t';
		}
		out << endl;
	}
	if (topn > 1) out << endl;
}

int run(const string& modelPath, bool buildFromRaw, bool benchmark, const string& output, const string& user, int topn, const vector<string>& input)
{
	try
	{
		tutils::Timer timer;
		size_t lines = 0, bytes = 0;
		Kiwi kw;
		if (buildFromRaw)
		{
			kw = KiwiBuilder{ KiwiBuilder::fromRawDataTag, modelPath, 1 }.build();
		}
		else
		{
			kw = KiwiBuilder{ modelPath, 1 }.build();
		}

		if (benchmark)
		{
			cout << "Loading Time : " << timer.getElapsed() << " ms" << endl;
			cout << "ArchType : " << archToStr(kw.archType()) << endl;
			cout << "LM Size : " << (kw.getLangModel()->getMemory().size() / 1024. / 1024.) << " MB" << endl;
			cout << "Mem Usage : " << (tutils::getCurrentPhysicalMemoryUsage() / 1024.) << " MB" << endl;
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
			for (string line; getline(cin, line);)
			{
				printResult(kw, line, topn, *out);
				++lines;
				bytes += line.size();
			}
		}
		else
		{
			timer.reset();
			for (auto& f : input)
			{
				ifstream in{ f };
				for (string line; getline(in, line);)
				{
					printResult(kw, line, topn, *out);
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

	CmdLine cmd{ "Kiwi CLI", ' ', "0.10.0" };

	ValueArg<string> model{ "m", "model", "Kiwi model path", true, "", "string" };
	SwitchArg build{ "b", "build", "build model from raw data" };
	SwitchArg benchmark{ "e", "benchmark", "benchmark performance" };
	ValueArg<string> output{ "o", "output", "output file path", false, "", "string" };
	ValueArg<string> user{ "u", "user", "user dictionary path", false, "", "string" };
	ValueArg<int> topn{ "n", "topn", "top-n of result", false, 1, "int > 0" };
	UnlabeledMultiArg<string> files{ "inputs", "input files", false, "string" };

	cmd.add(model);
	cmd.add(build);
	cmd.add(benchmark);
	cmd.add(output);
	cmd.add(user);
	cmd.add(topn);
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
	return run(model, build, benchmark, output, user, topn, files.getValue());
}

