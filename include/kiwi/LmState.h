#pragma once

#include <memory>
#include "Utils.h"
#include "Trie.hpp"
#include "Knlm.h"
#include "SkipBigramModel.h"

namespace kiwi
{
	struct LangModel
	{
		std::shared_ptr<lm::KnLangModelBase> knlm;
		std::shared_ptr<sb::SkipBigramModelBase> sbg;
	};
}
