#pragma once

#include <algorithm>
#include <kiwi/Types.h>

namespace kiwi
{
	class TagSequenceScorer
	{
		float seqScores[(size_t)POSTag::max][(size_t)POSTag::max] = { 0, };
		float leftBoundaryScores[2][(size_t)POSTag::max] = { 0, };
	public:
		float weight;

		TagSequenceScorer(float _weight = 5);

		float evalSeqs(POSTag left, POSTag right) const
		{
			return seqScores[(size_t)left][(size_t)right] * weight;
		}

		float evalLeftBoundary(bool hasLeftBoundary, POSTag right) const
		{
			return leftBoundaryScores[hasLeftBoundary ? 1 : 0][(size_t)right] * weight;
		}
	};

	bool isNounClass(POSTag tag);
	bool isVerbClass(POSTag tag);
	bool isEClass(POSTag tag);
	bool isJClass(POSTag tag);
	bool isSuffix(POSTag tag);
}
