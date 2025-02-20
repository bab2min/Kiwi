#include <iostream>
#include <fstream>

#include <kiwi/Kiwi.h>
#include <kiwi/CoNgramModel.h>
#include <tclap/CmdLine.h>
#include "toolUtils.h"

using namespace std;
using namespace kiwi;

int run(const std::string& morphemeDef, const std::string& contextDef, const std::string& embedding, 
	size_t minCnt, size_t maxLength, const std::string& output, bool useVLE, bool reorderContextIdx = true)
{
	try
	{
		tutils::Timer timer;
		KiwiBuilder::buildMorphData(morphemeDef, output, minCnt);
		auto ret = lm::CoNgramModelBase::build(contextDef, embedding, maxLength, useVLE, reorderContextIdx);
		ret.writeToFile(output + "/cong.mdl");
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
	ValueArg<size_t> maxLength{ "l", "max-length", "max length of n-grams", false, (size_t)-1, "int"};
	ValueArg<string> output{ "o", "output", "", true, "", "string" };
	SwitchArg useVLE{ "", "use-vle", "use VLE", false };
	SwitchArg preserveContextIdx{ "p", "preserve-context-idx", "preserve context index", false };

	cmd.add(mdef);
	cmd.add(cdef);
	cmd.add(emb);
	cmd.add(minCnt);
	cmd.add(maxLength);
	cmd.add(output);
	cmd.add(useVLE);
	cmd.add(preserveContextIdx);

	try
	{
		cmd.parse(argc, argv);
	}
	catch (const ArgException& e)
	{
		cerr << "error: " << e.error() << " for arg " << e.argId() << endl;
		return -1;
	}

	return run(mdef, cdef, emb, minCnt, maxLength, output, useVLE, !preserveContextIdx);
}
