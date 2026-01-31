#pragma once
#include <kiwi/CoNgramModel.h>

namespace sais
{
	template<typename T>
	class FmIndex;
}

namespace kiwi
{
	class UnkFormScorer
	{
		const lm::CoNgramModelBase* chrModel = nullptr;
		const sais::FmIndex<char16_t>* fmIndex = nullptr;
		float oovRuleScale = 0;
		float oovRuleBias = 0;
		float chrBias = 0;
		float globalWeight = 0;
		float localWeight = 0;
		float globalMinFreq = 4.f;
		int32_t bosNodeIdx = 0;
		uint32_t bosContextIdx = 0;
		bool useChrFreqBranchModel = false;

	public:
		UnkFormScorer(float scale, float bias,
			const lm::CoNgramModelBase* _chrModel, float _chrBias,
			const sais::FmIndex<char16_t>* _fmIndex,
			float _globalWeight = 60.f,
			float _localWeight = 3.f,
			float _globalMinFreq = 4.f,
			bool _useChrFreqBranchModel = false);

		float ruleBasedScore(const U16StringView& form) const;

		float chrBasedScore(const U16StringView& form) const;

		float chrFreqBasedScore(const U16StringView& form) const;

		float chrFreqBranchBasedScore(const U16StringView& form) const;

		float operator()(const U16StringView& form) const
		{
			if (chrModel && fmIndex)
			{
				if (useChrFreqBranchModel)
				{
					return chrFreqBranchBasedScore(form);
				}
				else
				{
					return chrFreqBasedScore(form);
				}
			}
			else if (chrModel)
			{
				return chrBasedScore(form);
			}
			else
			{
				return ruleBasedScore(form);
			}
		}
	};
}