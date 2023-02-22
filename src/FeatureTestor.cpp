#include <kiwi/Types.h>
#include "FeatureTestor.h"

using namespace kiwi;

bool FeatureTestor::isMatched(const kchar_t* begin, const kchar_t* end, CondVowel vowel)
{
	if (vowel == CondVowel::none) return true;
	if (begin == end) return false;
	if (vowel == CondVowel::any) return true;

	if (vowel == CondVowel::applosive)
	{
		switch (end[-1])
		{
		case u'\u11A8':
		case u'\u11A9':
		case u'\u11AA':
		case u'\u11AE':
		case u'\u11B8':
		case u'\u11B9':
		case u'\u11BA':
		case u'\u11BB':
		case u'\u11BD':
		case u'\u11BE':
		case u'\u11BF':
		case u'\u11C0':
		case u'\u11C1':
			return true;
		default:
			return false;
		}
	}

	if (!(u'\uAC00' <= end[-1] && end[-1] <= u'\uD7A4') &&
		!(u'\u11A8' <= end[-1] && end[-1] <= u'\u11C2')) return true;

	switch (vowel)
	{
	case CondVowel::vocalic_h:
		if (end[-1] == u'\u11C2') return true;
	case CondVowel::vocalic:
		if (end[-1] == u'\u11AF') return true;
	case CondVowel::vowel:
		if (u'\u11A8' <= end[-1] && end[-1] <= u'\u11C2') return false;
		return true;
	case CondVowel::non_vocalic_h:
		if (end[-1] == u'\u11C2') return false;
	case CondVowel::non_vocalic:
		if (end[-1] == u'\u11AF') return false;
	case CondVowel::non_vowel:
		if (u'\uAC00' <= end[-1] && end[-1] <= u'\uD7A4') return false;
		return true;
	default:
		return false;
	}
}

bool FeatureTestor::isMatched(const kchar_t* begin, const kchar_t* end, CondPolarity polar)
{
	if (polar == CondPolarity::none || polar == CondPolarity::non_adj) return true;
	if (begin == end) return true;
	for (auto it = end - 1; it >= begin; --it)
	{
		if (u'\u11A8' <= *it && *it <= u'\u11C2') continue;
		if (!(u'\uAC00' <= *it && *it <= u'\uD7A4')) break;
		int v = ((*it - u'\uAC00') / 28) % 21;
		if (v == 0 || v == 2 || v == 8) return polar == CondPolarity::positive;
		if (v == 18 && it == end - 1) continue;
		return polar == CondPolarity::negative;
	}
	return polar == CondPolarity::negative;
}

bool FeatureTestor::isMatchedApprox(const kchar_t* begin, const kchar_t* end, CondPolarity polar)
{
	if (polar == CondPolarity::none) return true;
	if (begin == end) return true;
	for (auto it = end - 1; it >= begin; --it)
	{
		if (u'\u11A8' <= *it && *it <= u'\u11C2') continue;
		if (!(u'\uAC00' <= *it && *it <= u'\uD7A4')) break;
		int v = ((*it - u'\uAC00') / 28) % 21;
		if (v == 0 || v == 2 || v == 8) return polar == CondPolarity::positive;
		if (v == 18) return true;
		return polar == CondPolarity::negative;
	}
	return polar == CondPolarity::negative;
}

bool FeatureTestor::isMatched(const kchar_t* begin, const kchar_t* end, CondVowel vowel, CondPolarity polar)
{
	return isMatched(begin, end, vowel) && isMatched(begin, end, polar);
}

bool FeatureTestor::isMatched(const KString * form, CondVowel vowel)
{
	return isMatched(form ? &(*form)[0] : nullptr, form ? &(*form)[0] + form->size() : nullptr, vowel);
}

bool FeatureTestor::isMatched(const KString * form, CondPolarity polar)
{
	return isMatched(form ? &(*form)[0] : nullptr, form ? &(*form)[0] + form->size() : nullptr, polar);
}

bool FeatureTestor::isMatched(const KString * form, CondVowel vowel, CondPolarity polar)
{
	return isMatched(form ? &(*form)[0] : nullptr, form ? &(*form)[0] + form->size() : nullptr, vowel, polar);
}

bool FeatureTestor::isMatchedApprox(const kchar_t* begin, const kchar_t* end, CondVowel vowel, CondPolarity polar)
{
	return isMatched(begin, end, vowel) && isMatchedApprox(begin, end, polar);
}
