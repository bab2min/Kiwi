#pragma once

#include "KForm.h"

namespace kiwi
{
	class KFeatureTestor
	{
	public:
		static bool isMatched(const k_string* form, KCondVowel vowel);
		static bool isMatched(const k_string* form, KCondPolarity polar);
		static bool isMatched(const k_string* form, KCondVowel vowel, KCondPolarity polar);
	};

}