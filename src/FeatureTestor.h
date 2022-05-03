#pragma once

#include <kiwi/Types.h>

namespace kiwi
{
	class FeatureTestor
	{
	public:
		static bool isMatched(const kchar_t* begin, const kchar_t* end, CondVowel vowel);
		static bool isMatched(const kchar_t* begin, const kchar_t* end, CondPolarity polar);
		static bool isMatchedApprox(const kchar_t* begin, const kchar_t* end, CondPolarity polar);
		static bool isMatched(const kchar_t* begin, const kchar_t* end, CondVowel vowel, CondPolarity polar);

		static bool isMatched(const KString* form, CondVowel vowel);
		static bool isMatched(const KString* form, CondPolarity polar);
		static bool isMatched(const KString* form, CondVowel vowel, CondPolarity polar);

		static bool isMatchedApprox(const kchar_t* begin, const kchar_t* end, CondVowel vowel, CondPolarity polar);
	};

	inline bool hasNoOnset(const KString& form)
	{
		return u'아' <= form[0] && form[0] <= u'이';
	}
}
