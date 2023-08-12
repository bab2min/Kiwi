#pragma once

#include <algorithm>
#include <kiwi/Types.h>

namespace kiwi
{
	class TagSequenceScorer
	{
		float leftBoundaryScores[2][(size_t)POSTag::max] = { { 0, }, };
	public:
		float weight;

		TagSequenceScorer(float _weight = 5);

		float evalLeftBoundary(bool hasLeftBoundary, POSTag right) const
		{
			return leftBoundaryScores[hasLeftBoundary ? 1 : 0][(size_t)clearIrregular(right)] * weight;
		}
	};

	bool isNounClass(POSTag tag);
	bool isVerbClass(POSTag tag);
	
	inline bool isEClass(POSTag tag)
	{
		return POSTag::ep <= tag && tag <= POSTag::etm;
	}
	
	inline bool isJClass(POSTag tag)
	{
		return POSTag::jks <= tag && tag <= POSTag::jc;
	}
	
	inline bool isSuffix(POSTag tag)
	{
		tag = clearIrregular(tag);
		return POSTag::xsn <= tag && tag <= POSTag::xsa;
	}
	
	inline bool isSpecialClass(POSTag tag)
	{
		return POSTag::sf <= tag && tag <= POSTag::sn;
	}

	inline bool isUserClass(POSTag tag)
	{
		return POSTag::user0 <= tag && tag <= POSTag::user4;
	}
}
