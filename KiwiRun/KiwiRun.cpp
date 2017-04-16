// KiwiRun.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#ifdef _WIN32
#include "../KiwiLibrary/KiwiHeader.h"
#include "../KiwiLibrary/Kiwi.h"
#else
#include "../KiwiLibraryLinux/KiwiHeader.h"
#include "../KiwiLibraryLinux/Kiwi.h"
#endif
unordered_map<string, vector<string>> parseArg(int argc, const char** argv)
{
	unordered_map<string, vector<string>> ret;
	string key = "";
	for (int i = 0; i < argc; i++)
	{
		if (argv[i][0] == '-')
		{
			if (!key.empty()) ret[key].emplace_back("");
			key = argv[i] + 1;
		}
		else
		{
			ret[key].emplace_back(argv[i]);
			key = "";
		}
	}
	if (!key.empty()) ret[key].emplace_back("");
	return ret;
}

void help()
{
	printf("Kiwi : Korean Intelligent Word Identifier\n"
		"version: 0.1\n"
		"\n"
		"= Usage =\n"
		"kiwi [-m model] [-u user] [-o output] [-c cache] [-n number] input...\n"
		"\tmodel: (Optional) path of model file\n"
		"\tuser: (Optional) path of user dictionary file\n"
		"\toutput: (Optional) path of result file\n"
		"\tcache: (Optional) max size of cache data\n"
		"\tnumber: (Optional) the number of analyzed result, default = 1\n"
		"\tinput: path of input file to be analyzed\n");
}

int main(int argc, const char** argv)
{
#ifdef _WIN32
	system("chcp 65001");
	_wsetlocale(LC_ALL, L"korean");
#endif
	auto arg = parseArg(argc - 1, argv + 1);
	string model = "";
	string userDict = "";
	string output = "";
	size_t maxCache = -1;
	size_t topN = 1;
	if (!arg["m"].empty())
	{
		model = arg["m"][0];
	}
	if (!arg["u"].empty())
	{
		userDict = arg["u"][0];
	}
	if (!arg["o"].empty())
	{
		output = arg["o"][0];
	}
	if (!arg["c"].empty())
	{
		maxCache = atoi(arg["c"][0].c_str());
	}
	if (!arg["n"].empty())
	{
		topN = atoi(arg["n"][0].c_str());
	}

	if (arg[""].empty())
	{
		help();
		return -1;
	}

	Kiwi* kiwi = nullptr;
	try
	{
		kiwi = new Kiwi{ model.c_str(), maxCache };
		if (!userDict.empty()) kiwi->loadUserDictionary(userDict.c_str());
		kiwi->prepare();
	}
	catch (exception e)
	{
		printf("#### %s.\n", e.what());
		return -1;
	}

	FILE* of = nullptr;
	if (output.empty()) 
	{
		of = stdout;
	}
	else
	{
		if (fopen_s(&of, output.c_str(), "w"))
		{
			printf("#### Cannot write file '%s'.\n", output.c_str());
			return -1;
		}
	}

	for (const auto& input : arg[""])
	{
		FILE* f = nullptr;
		if (fopen_s(&f, input.c_str(), "r"))
		{
			printf("#### Cannot open file '%s'.\n", input.c_str());
			continue;
		}
		char buf[8192];
		wstring_convert<codecvt_utf8_utf16<k_wchar>, k_wchar> converter;
		while (fgets(buf, 8192, f))
		{
			try
			{
				auto wstr = converter.from_bytes(buf);
				auto cands = kiwi->analyze(wstr, topN);
				for (const auto& c : cands)
				{
					for (const auto& m : c.first)
					{
						if (of == stdout)
						{
#ifdef _WIN32
							fputws(m.first.c_str(), of);
#else
							fputs(converter.to_bytes(m.first).c_str(), of);
#endif
						}
						else fputs(converter.to_bytes(m.first).c_str(), of);
						fputc('/', of);
						fputs(tagToString(m.second), of);
						fputc('\t', of);
					}
					fputc('\n', of);
				}
				if (topN > 1) fputc('\n', of);
			}
			catch (exception e)
			{
				printf("#### Uncaught exception '%s'.\n", e.what());
				continue;
			}
		}
		fclose(f);
	}
	if(of != stdout) fclose(of);
    return 0;
}

