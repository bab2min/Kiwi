#pragma once

#define P_MIN -40.f

enum class KPOSTag : char
{
	UNKNOWN,
	NNG, NNP, NNB, 
	VV, VA, 
	MAG, 
	NR, NP,
	VX,
	MM, MAJ,
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

KPOSTag makePOSTag(k_wstring tagStr);
const char* tagToString(KPOSTag t);
const wchar_t* tagToStringW(KPOSTag t);
struct KForm;

struct KMorpheme
{
#ifdef _DEBUG
	static size_t uid;
	size_t id;
	string form;
#endif
	KMorpheme(const k_string& _form = {},
		KPOSTag _tag = KPOSTag::UNKNOWN, 
		KCondVowel _vowel = KCondVowel::none,
		KCondPolarity _polar = KCondPolarity::none, 
		float _p = 0, char _combineSocket = 0)
		: tag(_tag), vowel(_vowel), polar(_polar), p(_p), combineSocket(_combineSocket)
#ifdef  _DEBUG
		, id(uid++), form(_form.begin(), _form.end())
#endif //  _DEBUG
	{
	}

	KMorpheme(KMorpheme&& m)
	{
#ifdef _DEBUG
		id = m.id;
		swap(form, m.form);
#endif
		kform = m.kform;
		wform = m.wform;
		tag = m.tag;
		vowel = m.vowel;
		polar = m.polar;
		combineSocket = m.combineSocket;
		p = m.p;
		swap(chunks, m.chunks);
		combined = m.combined;
	}

	~KMorpheme() 
	{ 
		if (chunks) delete chunks;
	}
	const k_string& getForm() const { return *kform; }
	const k_string* kform = nullptr;
	const k_wstring* wform = nullptr;
	KPOSTag tag = KPOSTag::UNKNOWN;
	KCondVowel vowel = KCondVowel::none;
	KCondPolarity polar = KCondPolarity::none;
	char combineSocket = 0;
	float p = 0;
	std::vector<const KMorpheme*>* chunks = nullptr;
	int32_t combined = 0;
	const KMorpheme* getCombined() const { return this + combined; }
	void readFromBin(std::istream& is, const std::function<const KMorpheme*(size_t)>& mapper);
	void writeToBin(std::ostream& os, const std::function<size_t(const KMorpheme*)>& mapper) const;
};

struct KForm
{
	k_string form;
	k_wstring wform;
	std::vector<const KMorpheme*> candidate;
	std::unordered_set<char> suffix;
	KForm(const char16_t* _form = nullptr);
	KForm(const k_string& _form) : form(_form) {}

	void readFromBin(std::istream& is, const std::function<const KMorpheme*(size_t)>& mapper);
	void writeToBin(std::ostream& os, const std::function<size_t(const KMorpheme*)>& mapper) const;
};

