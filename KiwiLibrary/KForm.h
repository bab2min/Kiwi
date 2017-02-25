#pragma once

#define P_MIN -20.f

enum class KPOSTag : char
{
	UNKNOWN,
	NNG, NNP, NNB, NR, NP,
	VV, VA, VX,
	MM, MAG, MAJ,
	IC,
	XPN, XSN, XSV, XSA, XR,
	VCP, VCN,
	SF, SP, SS, SE, SO, SW,
	NF, NV, NA,
	SL, SH, SN,
	JKS, JKC, JKG, JKO, JKB, JKV, JKQ, JX, JC,
	EP, EF, EC, ETN, ETM,
	V,
	MAX,
};

enum class KCondVowel : char
{
	none,
	any,
	vowel,
	vocalic,
	vocalicH,
	nonVowel,
	nonVocalic,
	nonVocalicH
};

enum class KCondPolarity : char
{
	none,
	positive,
	negative
};

KPOSTag makePOSTag(wstring tagStr);
const char* tagToString(KPOSTag t);

struct KMorpheme
{
//#ifdef _DEBUG
	static size_t uid;
	string form;
	size_t id;
//#endif
	KMorpheme(string _form = "", 
		KPOSTag _tag = KPOSTag::UNKNOWN, 
		KCondVowel _vowel = KCondVowel::none,
		KCondPolarity _polar = KCondPolarity::none, 
		float _p = 0)
		: tag(_tag), vowel(_vowel), polar(_polar), p(_p)
//#ifdef  _DEBUG
		, form(_form), id(uid++)
//#endif //  _DEBUG
	{}

	KPOSTag tag = KPOSTag::UNKNOWN;
	KCondVowel vowel = KCondVowel::none;
	KCondPolarity polar = KCondPolarity::none;
	float p = 0;
	vector<const KMorpheme*> chunks;
};

struct KForm
{
	string form;
	vector<const KMorpheme*> candidate;
	KCondVowel vowel = KCondVowel::none;
	KCondPolarity polar = KCondPolarity::none;
	bool hasFirstV = false;
	float maxP = 0;
	KForm(const char* _form = nullptr);
	KForm(const string& _form) : form(_form) {}
	void updateCond();
};

