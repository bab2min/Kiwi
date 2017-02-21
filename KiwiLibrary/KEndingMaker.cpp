#include "stdafx.h"
#include "KEndingMaker.h"
#include "HangulMacro.h"

bool isVowel(wchar_t c, bool positive)
{
	return ㅏ[0] <= c && c <= ㅣ[0];
}

bool isVocalic(wchar_t c, bool positive)
{
	return isVowel(c, positive) || ㄹ[0] == c;
}

bool isVocalicH(wchar_t c, bool positive)
{
	return isVocalic(c, positive) || ㅎ[0] == c;
}

bool notVocalic(wchar_t c, bool positive)
{
	return !isVocalic(c, positive);
}

bool notVocalicH(wchar_t c, bool positive)
{
	return !isVocalicH(c, positive);
}

bool isPositive(wchar_t c, bool positive)
{
	return positive;
}

bool notPositive(wchar_t c, bool positive)
{
	return !positive;
}

bool always(wchar_t c, bool positive)
{
	return true;
}

KEndingMaker::KEndingMaker() : vMorphList{
	//Honorific
	{
		{},
		{ ㅅ ㅣ, isVocalicH},
		{ ㅡ ㅅ ㅣ, notVocalicH},
	},
	//Tense
	{
		{},
		{ ㄴ, isVocalicH },
		{ ㄴ ㅡ ㄴ, isVocalicH },
		{ ㅏ ㅆ,  isPositive},
		{ ㅓ ㅆ,  notPositive },
		{ ㅏ ㅆ ㅓ ㅆ,  isPositive },
		{ ㅓ ㅆ ㅓ ㅆ,  notPositive },
	},
	//Reality
	{
		{},
		{ ㄱ ㅔ ㅆ, always}
	},
	//Formality
	{
		{},
		{ ㅅ ㅡ ㅂ, isVocalic},
		{ ㅡ ㅂ, isVocalic },
		{ ㅂ, notVocalic },
	},
	//Syntactic Mood
	{
		{},
		{ ㄴ ㅣ, always},
		{ ㄷ ㅣ, always },
		{ ㅅ ㅣ, always },
		{ ㄴ ㅡ ㄴ, always },
		{ ㄷ ㅓ, always },
		{ ㄷ ㅓ ㄴ, always },
		{ ㄴ ㅣ, always },
		{ ㄴ, isVocalicH },
		{ ㅡ ㄴ, notVocalicH },
		{ ㄹ, isVocalicH },
		{ ㅡ ㄹ, notVocalicH },
		{ ㄱ ㅣ, always },
		{ ㅁ, isVocalicH },
		{ ㅡ ㅁ, notVocalicH },
		{ ㅣ, always },
		{ ㄱ ㅔ, always },
		{ ㄷ ㅗ ㄹ ㅗ ㄱ, always },
		{ ㄱ ㅗ, always },
		{ ㅁ ㅕ, isVocalicH },
		{ ㅡ ㅁ ㅕ, notVocalicH },
		{ ㅁ ㅕ ㄴ, isVocalicH },
		{ ㅡ ㅁ ㅕ ㄴ, notVocalicH },
		{ ㅁ ㅕ ㄴ ㅅ ㅓ, isVocalicH },
		{ ㅡ ㅁ ㅕ ㄴ ㅅ ㅓ, notVocalicH },
		{ ㄴ ㅏ, isVocalicH },
		{ ㅡ ㄴ ㅏ, notVocalicH },
		{ ㄴ ㅣ, isVocalicH },
		{ ㅡ ㄴ ㅣ, notVocalicH },
		{ ㄴ ㅣ ㄲ ㅏ, isVocalicH },
		{ ㅡ ㄴ ㅣ ㄲ ㅏ, notVocalicH },
		{ ㅏ ㅅ ㅓ, isPositive},
		{ ㅓ ㅅ ㅓ, notPositive },
		{ ㅏ, isPositive },
		{ ㅓ, notPositive },
		{ ㄴ ㅡ ㄴ ㄷ ㅔ, always },
		{ ㅈ ㅣ, always },
		{ ㄱ ㅓ ㄴ ㅏ, always },
	},
	//Pragmatic Mood
	{
		{},
		{ ㄷ ㅏ, always },
		{ ㄲ ㅏ, always },
		{ ㅗ, always },
		{ ㅛ, always },
		{ ㄹ ㅏ, always },
		{ ㅈ ㅏ, always },
		{ ㄴ ㅑ, always },
		{ ㄴ ㅔ, always },
		{ ㄷ ㅔ, always },
		{ ㅅ ㅔ, always },
		{ ㄱ ㅏ, always },
		{ ㄱ ㅔ, always },
		{ ㅈ ㅣ, always },
		{ ㅏ ㄹ ㅏ, isPositive },
		{ ㅓ ㄹ ㅏ, notPositive },
		{ ㅏ, isPositive },
		{ ㅓ, notPositive },
	},
	//Polite
	{
		{},
		{ ㅛ, always}
	},
}
{
}

