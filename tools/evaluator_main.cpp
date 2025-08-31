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
using namespace TCLAP;

int main(int argc, const char* argv[])
{
	tutils::setUTF8Output();

	CmdLine cmd{ "Kiwi evaluator" };

	ValueArg<string> model{ "m", "model", "Kiwi model path", false, "models/base", "string" };
	ValueArg<string> output{ "o", "output", "output dir for evaluation errors", false, "", "string" };
	SwitchArg noNormCoda{ "", "no-normcoda", "without normalizing coda", false };
	SwitchArg noZCoda{ "", "no-zcoda", "without z-coda", false };
	SwitchArg noMulti{ "", "no-multi", "turn off multi dict", false };
	ValueArg<string> modelType{ "t", "type", "model type", false, "none", "string" };
	ValueArg<float> typoWeight{ "", "typo", "typo weight", false, 0.f, "float"};
	SwitchArg bTypo{ "", "btypo", "make basic-typo-tolerant model", false };
	SwitchArg cTypo{ "", "ctypo", "make continual-typo-tolerant model", false };
	SwitchArg lTypo{ "", "ltypo", "make lengthening-typo-tolerant model", false };
	ValueArg<string> dialect{ "d", "dialect", "allowed dialect", false, "standard", "string" };
	ValueArg<int> repeat{ "", "repeat", "repeat evaluation for benchmark", false, 1, "int" };
	UnlabeledMultiArg<string> inputs{ "inputs", "evaluation set (--morph, --disamb)", false, "string" };

	cmd.add(model);
	cmd.add(output);
	cmd.add(noNormCoda);
	cmd.add(noZCoda);
	cmd.add(noMulti);
	cmd.add(modelType);
	cmd.add(typoWeight);
	cmd.add(bTypo);
	cmd.add(cTypo);
	cmd.add(lTypo);
	cmd.add(dialect);
	cmd.add(repeat);
	cmd.add(inputs);

	try
	{
		cmd.parse(argc, argv);
	}
	catch (const ArgException& e)
	{
		cerr << "error: " << e.error() << " for arg " << e.argId() << endl;
		return -1;
	}
	ModelType kiwiModelType = ModelType::none;
	try
	{
		kiwiModelType = tutils::parseModelType(modelType);
	}
	catch (const exception& e)
	{
		cerr << e.what() << endl;
		return -1;
	}
	
	vector<string> morphInputs, disambInputs;

	string currentType = "";
	for (auto& input : inputs.getValue())
	{
		if (input.size() > 2 && input[0] == '-' && input[1] == '-')
		{
			currentType = input;
		}
		else
		{
			if (currentType == "--morph")
			{
				morphInputs.emplace_back(input);
			}
			else if (currentType == "--disamb")
			{
				disambInputs.emplace_back(input);
			}
			else
			{
				cerr << "Unknown argument: " << input << endl;
				return -1;
			}
		}
	}

	Dialect allowedDialect = parseDialects(dialect.getValue());

	if (morphInputs.size())
	{
		auto evaluator = Evaluator::create("morph");
		(*evaluator)(model, output, morphInputs,
			!noNormCoda, !noZCoda, !noMulti,
			kiwiModelType,
			typoWeight, bTypo, cTypo, lTypo,
			allowedDialect,
			repeat);
		cout << endl;
	}

	if (disambInputs.size())
	{
		auto evaluator = Evaluator::create("disamb");
		(*evaluator)(model, output, disambInputs,
			!noNormCoda, !noZCoda, !noMulti,
			kiwiModelType,
			typoWeight, bTypo, cTypo, lTypo,
			allowedDialect,
			repeat);
		cout << endl;
	}
}

