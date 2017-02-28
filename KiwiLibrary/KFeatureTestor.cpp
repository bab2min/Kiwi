#include "stdafx.h"
#include "KFeatureTestor.h"

bool KFeatureTestor::_isVowel(const char * begin, const char * end)
{
	return end[-1] > 30;
}

bool KFeatureTestor::_isVocalic(const char * begin, const char * end)
{
	return _isVowel(begin, end) || end[-1] == 9;
}

bool KFeatureTestor::_isVocalicH(const char * begin, const char * end)
{
	return _isVocalic(begin, end) || end[-1] == 30;
}

bool KFeatureTestor::_notVowel(const char * begin, const char * end)
{
	return !_isVowel(begin, end);
}

bool KFeatureTestor::_notVocalic(const char * begin, const char * end)
{
	return !_isVocalic(begin, end);
}

bool KFeatureTestor::_notVocalicH(const char * begin, const char * end)
{
	return !_isVocalicH(begin, end);
}

bool KFeatureTestor::_isPositive(const char * begin, const char * end)
{
	for (auto e = end - 1; e >= begin; e--)
	{
		if (*e <= 30 || *e == 49) continue;
		if (*e == 31 || *e == 33 || *e == 39) return true;
		return false;
	}
	return false;
}

bool KFeatureTestor::_notPositive(const char * begin, const char * end)
{
	return !_isPositive(begin, end);
}

bool KFeatureTestor::isVowel(const char * begin, const char * end)
{
	return begin < end && _isVowel(begin, end);
}

bool KFeatureTestor::isVocalic(const char * begin, const char * end)
{
	return begin < end && _isVocalic(begin, end);
}

bool KFeatureTestor::isVocalicH(const char * begin, const char * end)
{
	return begin < end && _isVocalicH(begin, end);
}

bool KFeatureTestor::notVowel(const char * begin, const char * end)
{
	return begin < end && _notVowel(begin, end);
}

bool KFeatureTestor::notVocalic(const char * begin, const char * end)
{
	return begin < end && _notVocalic(begin, end);
}

bool KFeatureTestor::notVocalicH(const char * begin, const char * end)
{
	return begin < end && _notVocalicH(begin, end);
}

bool KFeatureTestor::isPositive(const char * begin, const char * end)
{
	return begin < end && _isPositive(begin, end);
}

bool KFeatureTestor::notPositive(const char * begin, const char * end)
{
	return begin < end && _notPositive(begin, end);
}

bool KFeatureTestor::isPostposition(const char * begin, const char * end)
{
	return begin < end;
}

bool KFeatureTestor::isCorrectStart(const char * begin, const char * end)
{
	return !(end - begin >= 2 && begin[0] <= 30 && begin[1] <= 30)
		/*&& !(end - begin == 1 && begin[0] <= 30)*/;
}

bool KFeatureTestor::isCorrectEnd(const char * begin, const char * end)
{
	return !(end - begin >= 2 && end[-1] <= 30 && end[-2] <= 30) 
		&& !(end - begin == 1 && end[-1] <= 30);
}
