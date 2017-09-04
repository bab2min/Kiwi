#pragma once
class KFeatureTestor
{
	static bool _isVowel(const char* begin, const char* end);
	static bool _isVocalic(const char* begin, const char* end);
	static bool _isVocalicH(const char* begin, const char* end);
	static bool _notVowel(const char* begin, const char* end);
	static bool _notVocalic(const char* begin, const char* end);
	static bool _notVocalicH(const char* begin, const char* end);
	static bool _isPositive(const char* begin, const char* end);
	static bool _isNegative(const char* begin, const char* end);
public:
	static bool isVowel(const char* begin, const char* end);
	static bool isVocalic(const char* begin, const char* end);
	static bool isVocalicH(const char* begin, const char* end);
	static bool notVowel(const char* begin, const char* end);
	static bool notVocalic(const char* begin, const char* end);
	static bool notVocalicH(const char* begin, const char* end);
	static bool isPositive(const char* begin, const char* end);
	static bool isNegative(const char* begin, const char* end);
	static bool isPostposition(const char* begin, const char* end);

	// two consonants cannot consequently appear at start
	static bool isCorrectStart(const char* begin, const char* end);

	// two consonants cannot consequently appear at end
	static bool isCorrectEnd(const char* begin, const char* end);
};

