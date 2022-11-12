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

	class LmObjectBase
	{
	public:
		virtual ~LmObjectBase() {}
		
		virtual size_t vocabSize() const = 0;

		virtual float evalSequence(const uint32_t* seq, size_t length, size_t stride) const = 0;

		virtual void predictNext(const uint32_t* seq, size_t length, size_t stride, float* outScores) const = 0;

		virtual void evalSequences(
			const uint32_t* prefix, size_t prefixLength, size_t prefixStride, 
			const uint32_t* suffix, size_t suffixLength, size_t suffixStride,
			size_t seqSize, const uint32_t** seq, const size_t* seqLength, const size_t* seqStride, float* outScores
		) const = 0;
	};
}
