#include "KiwiHeader.h"
#include "KFeatureTestor.h"
#include "KForm.h"

using namespace kiwi;

bool KFeatureTestor::isMatched(const k_string * form, KCondVowel vowel)
{
	if (vowel == KCondVowel::none || vowel == KCondVowel::any) return true;
	if (!form) return false;
	switch (vowel)
	{
	case KCondVowel::vocalicH:
		if (form->back() == 0x11C2) return true;
	case KCondVowel::vocalic:
		if (form->back() == 0x11AF) return true;
	case KCondVowel::vowel:
		if (0xAC00 <= form->back() && form->back() <= 0xD7A4) return true;
		return false;
	case KCondVowel::nonVocalicH:
		if (form->back() == 0x11C2) return false;
	case KCondVowel::nonVocalic:
		if (form->back() == 0x11AF) return false;
	case KCondVowel::nonVowel:
		if (0xAC00 <= form->back() && form->back() <= 0xD7A4) return false;
		return true;
	default:
		return false;
	}
}

bool KFeatureTestor::isMatched(const k_string * form, KCondPolarity polar)
{
	if (polar == KCondPolarity::none) return true;
	if (!form) return false;
	for (auto it = form->rbegin(); it != form->rend(); ++it)
	{
		if (!(0xAC00 <= *it && *it <= 0xD7A4)) continue;
		int v = ((*it - 0xAC00) / 28) % 21;
		if (v == 0 || v == 2 || v == 8) return polar == KCondPolarity::positive;
		if (v == 18) continue;
		return polar == KCondPolarity::negative;
	}
	return polar == KCondPolarity::negative;
}

bool KFeatureTestor::isMatched(const k_string * form, KCondVowel vowel, KCondPolarity polar)
{
	return isMatched(form, vowel) && isMatched(form, polar);
}

