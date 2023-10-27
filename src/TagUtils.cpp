#include <kiwi/TagUtils.h>

using namespace std;
using namespace kiwi;

bool kiwi::isNounClass(POSTag tag)
{
	static auto nouns = { 
		POSTag::nng, POSTag::nnp, POSTag::nnb, 
		POSTag::nr, POSTag::np, 
		POSTag::xsn, POSTag::xr, 
		POSTag::sl, POSTag::sh, POSTag::sn, 
		POSTag::w_url, POSTag::w_email, POSTag::w_mention, POSTag::w_hashtag,
		POSTag::etn,
	};
	return find(nouns.begin(), nouns.end(), tag) != nouns.end();
}

bool kiwi::isVerbClass(POSTag tag)
{
	static auto verbs = { POSTag::vv, POSTag::va, POSTag::vx, POSTag::xsv, POSTag::xsa, POSTag::vcp, POSTag::vcn, };
	return find(verbs.begin(), verbs.end(), clearIrregular(tag)) != verbs.end();
}

namespace kiwi
{
	inline bool isAllowedSeq(POSTag left, POSTag right)
	{
		if (isNounClass(left) && isEClass(right))
		{
			return false;
		}
		if ((isVerbClass(left) || isEClass(left)) && right == POSTag::vcp)
		{
			return false;
		}
		if (isVerbClass(left) && !isEClass(right))
		{
			return false;
		}
		if ((!isVerbClass(left) && !isEClass(left)) && isEClass(right))
		{
			return false;
		}
		return true;
	}
}

TagSequenceScorer::TagSequenceScorer(float _weight) : weight{ _weight }
{
	for (auto t : { POSTag::nnp, POSTag::np, POSTag::ic })
	{
		leftBoundaryScores[0][(size_t)t] = -1;
	}
	leftBoundaryScores[0][(size_t)POSTag::sb] = -3;

	for (size_t r = 0; r < (size_t)POSTag::max; ++r)
	{
		leftBoundaryScores[1][r] = (isEClass((POSTag)r) || isJClass((POSTag)r) || isSuffix((POSTag)r) || (POSTag)r == POSTag::vcp) ? -1 : 0;
	}
}
