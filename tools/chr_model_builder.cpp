#include <iostream>
#include <fstream>

#include <kiwi/Kiwi.h>
#include <kiwi/CoNgramModel.h>
#include <tclap/CmdLine.h>
#include "toolUtils.h"

using namespace std;
using namespace kiwi;

int run(const std::string& contextDef, 
	const std::string& embedding, 
	size_t maxLength, const std::string& output)
{
	try
	{
		tutils::Timer timer;
		auto ret = lm::CoNgramModelBase::buildChrModel(contextDef, embedding, maxLength);
		ret.writeToFile(output + "/nounchr.mdl");
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

	CmdLine cmd{ "Kiwi CoNgram Builder", ' ', KIWI_VERSION_STRING };

	ValueArg<string> cdef{ "c", "context-def", "context definition", true, "", "string" };
	ValueArg<string> emb{ "e", "emb", "embedding file", true, "", "string" };
	ValueArg<size_t> maxLength{ "l", "max-length", "max length of n-grams", false, (size_t)-1, "int"};
	ValueArg<string> output{ "o", "output", "", true, "", "string" };

	cmd.add(cdef);
	cmd.add(emb);
	cmd.add(maxLength);
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

	return run(cdef, emb, maxLength, output);
}
