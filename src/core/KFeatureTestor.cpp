#include "KiwiHeader.h"
#include "KFeatureTestor.h"
#include "KForm.h"

using namespace kiwi;

bool kiwi::KFeatureTestor::isMatched(const k_char* begin, const k_char* end, KCondVowel vowel)
{
	if (vowel == KCondVowel::none) return true;
	if (begin == end) return false;
	if (vowel == KCondVowel::any) return true;
	switch (vowel)
	{
	case KCondVowel::vocalicH:
		if (end[-1] == 0x11C2) return true;
	case KCondVowel::vocalic:
		if (end[-1] == 0x11AF) return true;
	case KCondVowel::vowel:
		if (0x11A8 <= end[-1] && end[-1] <= 0x11C2) return false;
		return true;
	case KCondVowel::nonVocalicH:
		if (end[-1] == 0x11C2) return false;
	case KCondVowel::nonVocalic:
		if (end[-1] == 0x11AF) return false;
	case KCondVowel::nonVowel:
		if (0xAC00 <= end[-1] && end[-1] <= 0xD7A4) return false;
		return true;
	default:
		return false;
	}
}

bool kiwi::KFeatureTestor::isMatched(const k_char* begin, const k_char* end, KCondPolarity polar)
{
	if (polar == KCondPolarity::none) return true;
	if (begin == end) return true;
	for (auto it = end - 1; it >= begin; --it)
	{
		if (!(0xAC00 <= *it && *it <= 0xD7A4)) continue;
		int v = ((*it - 0xAC00) / 28) % 21;
		if (v == 0 || v == 2 || v == 8) return polar == KCondPolarity::positive;
		if (v == 18) continue;
		return polar == KCondPolarity::negative;
	}
	return polar == KCondPolarity::negative;
}

bool kiwi::KFeatureTestor::isMatched(const k_char* begin, const k_char* end, KCondVowel vowel, KCondPolarity polar)
{
	return isMatched(begin, end, vowel) && isMatched(begin, end, polar);
}

bool KFeatureTestor::isMatched(const k_string * form, KCondVowel vowel)
{
	return isMatched(form ? &(*form)[0] : nullptr, form ? &(*form)[0] + form->size() : nullptr, vowel);
}

bool KFeatureTestor::isMatched(const k_string * form, KCondPolarity polar)
{
	return isMatched(form ? &(*form)[0] : nullptr, form ? &(*form)[0] + form->size() : nullptr, polar);
}

bool KFeatureTestor::isMatched(const k_string * form, KCondVowel vowel, KCondPolarity polar)
{
	return isMatched(form ? &(*form)[0] : nullptr, form ? &(*form)[0] + form->size() : nullptr, vowel, polar);
}

