#include <iostream>
#include <fstream>

#include <kiwi/Kiwi.h>
#include <kiwi/PCLanguageModel.h>
#include <tclap/CmdLine.h>
#include "toolUtils.h"

using namespace std;
using namespace kiwi;

int run(const std::string& morphemeDef, const std::string& contextDef, const std::string& embedding, size_t minCnt, const std::string& output)
{
	try
	{
		tutils::Timer timer;
		KiwiBuilder::buildMorphData(morphemeDef, output, minCnt);
		auto ret = pclm::PCLanguageModelBase::build(contextDef, embedding);
		ret.writeToFile(output + "/pclm.mdl");
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

	CmdLine cmd{ "Kiwi PCLanguageModel Builder", ' ', "0.21.0" };

	ValueArg<string> mdef{ "m", "morpheme-def", "morpheme definition", true, "", "string" };
	ValueArg<string> cdef{ "c", "context-def", "context definition", true, "", "string" };
	ValueArg<string> emb{ "e", "emb", "embedding file", true, "", "string" };
	ValueArg<size_t> minCnt{ "n", "min-cnt", "min count of morpheme", false, 10, "int" };
	ValueArg<string> output{ "o", "output", "", true, "", "string" };

	cmd.add(mdef);
	cmd.add(cdef);
	cmd.add(emb);
	cmd.add(minCnt);
	cmd.add(output);

	try
	{
		cmd.parse(argc, argv);
	}
	catch (const ArgException& e)
	{
		cerr << "error: " << e.error() << " for arg " << e.argId() << endl;
		return -1;
	}

	return run(mdef, cdef, emb, minCnt, output);
}
