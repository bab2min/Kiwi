#include "stdafx.h"
#include "KForm.h"

#ifdef _DEBUG
size_t KMorpheme::uid = 0;
#endif

KPOSTag makePOSTag(wstring tagStr)
{
	if (tagStr == L"NNG") return KPOSTag::NNG;
	if (tagStr == L"NNP") return KPOSTag::NNP;
	if (tagStr == L"NNB") return KPOSTag::NNB;
	if (tagStr == L"NR") return KPOSTag::NR;
	if (tagStr == L"NP") return KPOSTag::NP;
	if (tagStr == L"VV") return KPOSTag::VV;
	if (tagStr == L"VA") return KPOSTag::VA;
	if (tagStr == L"VX") return KPOSTag::VX;
	if (tagStr == L"VCP") return KPOSTag::VCP;
	if (tagStr == L"VCN") return KPOSTag::VCN;
	if (tagStr == L"MM") return KPOSTag::MM;
	if (tagStr == L"MAG") return KPOSTag::MAG;
	if (tagStr == L"MAJ") return KPOSTag::MAJ;
	if (tagStr == L"IC") return KPOSTag::IC;
	if (tagStr == L"JKS") return KPOSTag::JKS;
	if (tagStr == L"JKC") return KPOSTag::JKC;
	if (tagStr == L"JKG") return KPOSTag::JKG;
	if (tagStr == L"JKO") return KPOSTag::JKO;
	if (tagStr == L"JKB") return KPOSTag::JKB;
	if (tagStr == L"JKV") return KPOSTag::JKV;
	if (tagStr == L"JKQ") return KPOSTag::JKQ;
	if (tagStr == L"JX") return KPOSTag::JX;
	if (tagStr == L"JC") return KPOSTag::JC;
	if (tagStr == L"EP") return KPOSTag::EP;
	if (tagStr == L"EF") return KPOSTag::EF;
	if (tagStr == L"EC") return KPOSTag::EC;
	if (tagStr == L"ETN") return KPOSTag::ETN;
	if (tagStr == L"ETM") return KPOSTag::ETM;
	if (tagStr == L"XPN") return KPOSTag::XPN;
	if (tagStr == L"XSN") return KPOSTag::XSN;
	if (tagStr == L"XSV") return KPOSTag::XSV;
	if (tagStr == L"XSA") return KPOSTag::XSA;
	if (tagStr == L"XR") return KPOSTag::XR;
	if (tagStr == L"SF") return KPOSTag::SF;
	if (tagStr == L"SP") return KPOSTag::SP;
	if (tagStr == L"SS") return KPOSTag::SS;
	if (tagStr == L"SE") return KPOSTag::SE;
	if (tagStr == L"SO") return KPOSTag::SO;
	if (tagStr == L"SW") return KPOSTag::SW;
	if (tagStr == L"NF") return KPOSTag::NF;
	if (tagStr == L"NV") return KPOSTag::NV;
	if (tagStr == L"NA") return KPOSTag::NA;
	if (tagStr == L"SL") return KPOSTag::SL;
	if (tagStr == L"SH") return KPOSTag::SH;
	if (tagStr == L"SN") return KPOSTag::SN;
	if (tagStr == L"V") return KPOSTag::V;
	if (tagStr == L"^") return KPOSTag::UNKNOWN;
	//assert(0);
	return KPOSTag::MAX;
}

const char * tagToString(KPOSTag t)
{
	static const char* tags[] = 
	{
		"UN",
		"NNG", "NNP", "NNB", 
		"VV", "VA", 
		"MAG", 
		"NR", "NP",
		"VX",
		"MM", "MAJ",
		"IC",
		"XPN", "XSN", "XSV", "XSA", "XR",
		"VCP", "VCN",
		"SF", "SP", "SS", "SE", "SO", "SW",
		"NF", "NV", "NA",
		"SL", "SH", "SN",
		"JKS", "JKC", "JKG", "JKO", "JKB", "JKV", "JKQ", "JX", "JC",
		"EP", "EF", "EC", "ETN", "ETM",
		"V",
		"@"
	};
	assert(t < KPOSTag::MAX);
	return tags[(size_t)t];
}

KForm::KForm(const char * _form)
{
	if (_form) form = {_form, _form + strlen(_form)};
}

void KForm::updateCond()
{
	KCondVowel cv = candidate[0]->vowel;
	KCondPolarity cp = candidate[0]->polar;
	//maxP = candidate[0]->p;
	for (auto m : candidate)
	{
		if (cv != m->vowel)
		{
			cv = (int)cv && (int)m->vowel ? KCondVowel::any : KCondVowel::none;
		}
		if (cp != m->polar) cp = KCondPolarity::none;
		if (!m->chunks.empty() && m->chunks[0]->tag == KPOSTag::V) hasFirstV = true;
		//if (m->p > maxP) maxP = m->p;
	}
	vowel = cv;
	polar = cp;
	if (suffix.find(0) != suffix.end()) suffix = {};
}

void writeString(const string& str, FILE* f)
{
	size_t s = str.size();
	fwrite(&s, 1, 4, f);
	if (s) fwrite(&str[0], 1, s, f);
}

void readString(string& str, FILE* f)
{
	size_t s = 0;
	fread(&s, 1, 4, f);
	str.resize(s);
	if (s) fread(&str[0], 1, s, f);
}

void KForm::readFromBin(FILE * f, const function<const KMorpheme*(size_t)>& mapper)
{
	readString(form, f);
	size_t s = 0;
	fread(&s, 1, 4, f);
	candidate.resize(s);
	for (auto& c : candidate)
	{
		size_t cid = 0;
		fread(&cid, 1, 4, f);
		c = mapper(cid);
	}
	fread(&vowel, 1, 1, f);
	fread(&polar, 1, 1, f);
	fread(&hasFirstV, 1, 1, f);

	fread(&s, 1, 4, f);
	for (size_t i = 0; i < s; i++)
	{
		char c;
		fread(&c, 1, 1, f);
		suffix.insert(c);
	}
}

void KForm::writeToBin(FILE * f, const function<size_t(const KMorpheme*)>& mapper) const
{
	writeString(form, f);
	size_t s = candidate.size();
	fwrite(&s, 1, 4, f);
	for (auto c : candidate)
	{
		size_t cid = mapper(c);
		fwrite(&cid, 1, 4, f);
	}
	fwrite(&vowel, 1, 1, f);
	fwrite(&polar, 1, 1, f);
	fwrite(&hasFirstV, 1, 1, f);

	s = suffix.size();
	fwrite(&s, 1, 4, f);
	for (auto c : suffix)
	{
		fwrite(&c, 1, 1, f);
	}
}

void KMorpheme::readFromBin(FILE * f, const function<const KMorpheme*(size_t)>& mapper)
{
	readString(form, f);
	fread(&tag, 1, 1, f);
	fread(&vowel, 1, 1, f);
	fread(&polar, 1, 1, f);
	fread(&combineSocket, 1, 1, f);
	fread(&p, 1, 4, f);

	size_t s = 0;
	fread(&s, 1, 4, f);
	chunks.resize(s);
	for (auto& c : chunks)
	{
		size_t cid = 0;
		fread(&cid, 1, 4, f);
		c = mapper(cid);
	}
}

void KMorpheme::writeToBin(FILE * f, const function<size_t(const KMorpheme*)>& mapper) const
{
	writeString(form, f);
	fwrite(&tag, 1, 1, f);
	fwrite(&vowel, 1, 1, f);
	fwrite(&polar, 1, 1, f);
	fwrite(&combineSocket, 1, 1, f);
	fwrite(&p, 1, 4, f);

	size_t s = chunks.size();
	fwrite(&s, 1, 4, f);
	for (auto c : chunks)
	{
		size_t cid = mapper(c);
		fwrite(&cid, 1, 4, f);
	}
}
