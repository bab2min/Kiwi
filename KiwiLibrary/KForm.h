#pragma once

#define P_MIN -40.f

enum class KPOSTag : uint8_t
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
	SL, SH, SN,
	JKS, JKC, JKG, JKO, JKB, JKV, JKQ, JX, JC,
	EP, EF, EC, ETN, ETM,
	V,
	MAX,
};

enum class KCondVowel : uint8_t
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

KPOSTag makePOSTag(std::u16string tagStr);
const char* tagToString(KPOSTag t);
const k_char* tagToStringW(KPOSTag t);
struct KForm;

struct KMorpheme
{
#ifdef _DEBUG
	static size_t uid;
	size_t id;
#endif
	KMorpheme(const k_string& _form = {},
		KPOSTag _tag = KPOSTag::UNKNOWN, 
		KCondVowel _vowel = KCondVowel::none,
		KCondPolarity _polar = KCondPolarity::none, 
		uint8_t _combineSocket = 0)
		: tag(_tag), vowel(_vowel), polar(_polar), combineSocket(_combineSocket)
#ifdef  _DEBUG
		, id(uid++)
#endif //  _DEBUG
	{
	}

	KMorpheme(KMorpheme&& m)
	{
#ifdef _DEBUG
		id = m.id;
#endif
		kform = m.kform;
		tag = m.tag;
		vowel = m.vowel;
		polar = m.polar;
		combineSocket = m.combineSocket;
		std::swap(chunks, m.chunks);
		combined = m.combined;
	}

	~KMorpheme() 
	{ 
		if (chunks) delete chunks;
	}
	const k_string& getForm() const { return *kform; }
	const k_string* kform = nullptr;
	KPOSTag tag = KPOSTag::UNKNOWN;
	KCondVowel vowel = KCondVowel::none;
	KCondPolarity polar = KCondPolarity::none;
	uint8_t combineSocket = 0;
	std::vector<const KMorpheme*>* chunks = nullptr;
	int32_t combined = 0;
	const KMorpheme* getCombined() const { return this + combined; }
	void readFromBin(std::istream& is, const std::function<const KMorpheme*(size_t)>& mapper);
	void writeToBin(std::ostream& os, const std::function<size_t(const KMorpheme*)>& mapper) const;

	friend std::ostream& operator<< (std::ostream& os, const KMorpheme& morph);
};

struct KForm
{
	k_string form;
	std::vector<const KMorpheme*> candidate;
	std::unordered_set<k_char> suffix;
	KForm(const k_char* _form = nullptr);
	KForm(const k_string& _form) : form(_form) {}

	void readFromBin(std::istream& is, const std::function<const KMorpheme*(size_t)>& mapper);
	void writeToBin(std::ostream& os, const std::function<size_t(const KMorpheme*)>& mapper) const;
};

