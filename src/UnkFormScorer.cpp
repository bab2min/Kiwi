#include <kiwi/Dataset.h>
#include "UnkFormScorer.h"
#include "sais/fm_index.hpp"

using namespace std;
using namespace kiwi;

UnkFormScorer::UnkFormScorer(float scale, float bias,
	const lm::CoNgramModelBase* _chrModel, float _chrBias,
	const sais::FmIndex<char16_t>* _fmIndex,
	float _globalWeight,
	float _localWeight,
	float _globalMinFreq,
	bool _useChrFreqBranchModel)
	: chrModel{ _chrModel }, fmIndex{ _fmIndex }, 
	oovRuleScale{ scale }, oovRuleBias{ bias }, 
	chrBias{ _chrBias },
	globalWeight{ _globalWeight }, localWeight{ _localWeight },
	globalMinFreq{ _globalMinFreq },
	useChrFreqBranchModel{ _useChrFreqBranchModel }
{
	if (chrModel)
	{
		chrModel->progressOneStep(bosNodeIdx, bosContextIdx, 0); // BOS
	}
}

float UnkFormScorer::ruleBasedScore(const U16StringView& form) const
{
	float penalty = 0;
	if (form.size() > 0)
	{
		char32_t chrs[2] = { 0,0 };
		for (size_t i = 0, j = 0; i < form.size() && j < 2; ++j)
		{
			if (isHighSurrogate(form[i]))
			{
				chrs[j] = mergeSurrogate(form[i], form[i + 1]);
				i += 2;
			}
			else
			{
				chrs[j] = form[i];
				++i;
			}
		}
		if (isEmoji(chrs[0], chrs[1])) penalty = -10;
	}

	return penalty - (form.size() * oovRuleScale + oovRuleBias);
}

float UnkFormScorer::chrBasedScore(const U16StringView& form) const
{
	int32_t nodeIdx = bosNodeIdx;
	uint32_t contextIdx = bosContextIdx;
	ChrTokenizer tokenizer;
	float score = 0;
	for (char16_t c : form)
	{
		const size_t token = tokenizer.encodeOne(c);
		score += chrModel->progressOneStep(nodeIdx, contextIdx, token);
	}
	score += chrModel->progressOneStep(nodeIdx, contextIdx, 0); // EOS
	score -= chrBias;
	return score;
}

float UnkFormScorer::chrFreqBasedScore(const U16StringView& form) const
{
	int32_t nodeIdx = bosNodeIdx;
	uint32_t contextIdx = bosContextIdx;
	ChrTokenizer tokenizer;
	float score = 0;

	pair<size_t, size_t> fmRange;
	for (size_t i = 0; i < form.size(); ++i)
	{
		const auto c = form[i];
		const size_t depth = chrModel->getNodeDepth(nodeIdx);
		const float globalContextFreq = depth < i ? globalMinFreq : max(chrModel->getContextFrequency(contextIdx), globalMinFreq);
		const float globalContextFreqSat = tanhf(globalContextFreq / globalWeight) * globalWeight;
		const size_t token = tokenizer.encodeOne(c);
		const float lprob = chrModel->progressOneStep(nodeIdx, contextIdx, token);
		if (i == 0)
		{
			fmRange = fmIndex->initRange(c);
			score += lprob;
		}
		else
		{
			const float localContextFreq = fmRange.second - fmRange.first - 1;
			fmRange = fmIndex->nextRange(fmRange, c);
			if (localContextFreq > 0)
			{
				const float localContextFreqSat = tanhf(localContextFreq / localWeight) * localWeight;
				const float localFreq = (float)(fmRange.second - fmRange.first - 1) * (localContextFreqSat / localContextFreq);
				const float globalFreq = globalContextFreqSat * expf(lprob);
				const float mixedProb = logf((localFreq + globalFreq) / (localContextFreqSat + globalContextFreqSat));
				score += mixedProb;
			}
			else
			{
				score += lprob;
			}
		}
	}
	score += chrModel->progressOneStep(nodeIdx, contextIdx, 0); // EOS
	score -= chrBias;
	return score;
}

float UnkFormScorer::chrFreqBranchBasedScore(const U16StringView& form) const
{
	// not implemented yet
	int32_t nodeIdx = bosNodeIdx;
	uint32_t contextIdx = bosContextIdx;
	ChrTokenizer tokenizer;
	float score = 0;

	Vector<pair<char16_t, float>> nextChrs;
	pair<size_t, size_t> fmRange;
	for (size_t i = 0; i < form.size(); ++i)
	{
		const auto c = form[i];
		const size_t depth = chrModel->getNodeDepth(nodeIdx);
		const float globalContextFreq = depth < i ? globalMinFreq : max(chrModel->getContextFrequency(contextIdx), globalMinFreq);
		const float globalContextFreqSat = tanhf(globalContextFreq / globalWeight) * globalWeight;
		const size_t token = tokenizer.encodeOne(c);
		const float lprob = chrModel->progressOneStep(nodeIdx, contextIdx, token);
		const float branchEntropy = chrModel->getContextEntropy(contextIdx);
		if (i == 0)
		{
			fmRange = fmIndex->initRange(c);
			nextChrs.clear();
			fmIndex->enumNextChr(fmRange, [&](char16_t nextChr, size_t cnt)
			{
				nextChrs.emplace_back(nextChr, (float)cnt);
			});
			score += lprob;
		}
		else
		{
			const float localContextFreq = fmRange.second - fmRange.first - 1;
			fmRange = fmIndex->nextRange(fmRange, c);
			if (localContextFreq > 0)
			{
				nextChrs.clear();
				fmIndex->enumNextChr(fmRange, [&](char16_t nextChr, size_t cnt)
				{
					nextChrs.emplace_back(nextChr, (float)cnt);
				});
				const float localContextFreqSat = tanhf(localContextFreq / localWeight) * localWeight;
				const float localFreq = (float)(fmRange.second - fmRange.first - 1) * (localContextFreqSat / localContextFreq);
				const float globalFreq = globalContextFreqSat * expf(lprob);
				const float mixedProb = logf((localFreq + globalFreq) / (localContextFreqSat + globalContextFreqSat));
				score += mixedProb;
			}
			else
			{
				score += lprob;
			}
		}
	}
	score += chrModel->progressOneStep(nodeIdx, contextIdx, 0); // EOS
	score -= chrBias;
	return score;
}