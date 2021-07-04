#pragma once

#include "KForm.h"

namespace kiwi
{
	class KFeatureTestor
	{
	public:
		static bool isMatched(const kchar_t* begin, const kchar_t* end, CondVowel vowel);
		static bool isMatched(const kchar_t* begin, const kchar_t* end, CondPolarity polar);
		static bool isMatched(const kchar_t* begin, const kchar_t* end, CondVowel vowel, CondPolarity polar);

		static bool isMatched(const KString* form, CondVowel vowel);
		static bool isMatched(const KString* form, CondPolarity polar);
		static bool isMatched(const KString* form, CondVowel vowel, CondPolarity polar);
	};

}