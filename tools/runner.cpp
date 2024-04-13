#include <iostream>
#include <fstream>

#include <kiwi/Kiwi.h>
#include <tclap/CmdLine.h>
#include "toolUtils.h"

using namespace std;
using namespace kiwi;

void printResult(Kiwi& kw, const string& line, int topn, bool score, ostream& out)
{
	for (auto& result : kw.analyze(line, topn, Match::allWithNormalizing))
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

int run(const string& modelPath, bool benchmark, const string& output, const string& user, int topn, int tolerance, float typos, bool score, bool sbg, const vector<string>& input)
{
	try
	{
		tutils::Timer timer;
		size_t lines = 0, bytes = 0;
		Kiwi kw = KiwiBuilder{ modelPath, 1, BuildOption::default_, sbg }.build(typos > 0 ? DefaultTypoSet::basicTypoSet : DefaultTypoSet::withoutTypo);

		cout << "Kiwi v" << KIWI_VERSION_STRING << endl;
		if (tolerance)
		{
			kw.setSpaceTolerance(tolerance);
			cout << "SpaceTolerance: " << tolerance << endl;
		}
		if (typos > 0)
		{
			kw.setTypoCostWeight(typos);
			cout << "Typo Correction Cost Weight: " << typos << endl;
		}

		if (benchmark)
		{
			cout << "Loading Time : " << timer.getElapsed() << " ms" << endl;
			cout << "ArchType : " << archToStr(kw.archType()) << endl;
			cout << "LM Size : " << (kw.getKnLM()->getMemory().size() / 1024. / 1024.) << " MB" << endl;
			cout << "Mem Usage : " << (tutils::getCurrentPhysicalMemoryUsage() / 1024.) << " MB" << endl;
			cout << "ModelType : " << (sbg ? "sbg" : "knlm") << endl;
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
				printResult(kw, utf16To8((const char16_t*)line.c_str()), topn, score, *out);
				++lines;
				bytes += line.size();
			}
#else
			for (string line; (cout << ">> ").flush(), getline(cin, line);)
			{
				printResult(kw, line, topn, score, *out);
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
					printResult(kw, line, topn, score, *out);
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
	SwitchArg benchmark{ "e", "benchmark", "benchmark performance" };
	ValueArg<string> output{ "o", "output", "output file path", false, "", "string" };
	ValueArg<string> user{ "u", "user", "user dictionary path", false, "", "string" };
	ValueArg<int> topn{ "n", "topn", "top-n of result", false, 1, "int > 0" };
	ValueArg<int> tolerance{ "t", "tolerance", "space tolerance of Kiwi", false, 0, "int >= 0" };
	ValueArg<float> typos{ "", "typos", "typo cost weight", false, 0.f, "float >= 0" };
	SwitchArg sbg{ "", "sbg", "use SkipBigram" };
	SwitchArg score{ "s", "score", "print score together" };
	UnlabeledMultiArg<string> files{ "inputs", "input files", false, "string" };

	cmd.add(model);
	cmd.add(benchmark);
	cmd.add(output);
	cmd.add(user);
	cmd.add(topn);
	cmd.add(tolerance);
	cmd.add(score);
	cmd.add(files);
	cmd.add(typos);
	cmd.add(sbg);

	try
	{
		cmd.parse(argc, argv);
	}
	catch (const ArgException& e)
	{
		cerr << "error: " << e.error() << " for arg " << e.argId() << endl;
		return -1;
	}
	return run(model, benchmark, output, user, topn, tolerance, typos, score, sbg, files.getValue());
}

