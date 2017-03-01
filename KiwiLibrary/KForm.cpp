#include "stdafx.h"
#include "KForm.h"

//#ifdef _DEBUG
size_t KMorpheme::uid = 0;
//#endif

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
	return KPOSTag::UNKNOWN;
}

const char * tagToString(KPOSTag t)
{
	static const char* tags[] = 
	{
		"UNKNOWN",
		"NNG", "NNP", "NNB", "NR", "NP",
		"VV", "VA", "VX",
		"MM", "MAG", "MAJ",
		"IC",
		"XPN", "XSN", "XSV", "XSA", "XR",
		"VCP", "VCN",
		"SF", "SP", "SS", "SE", "SO", "SW",
		"NF", "NV", "NA",
		"SL", "SH", "SN",
		"JKS", "JKC", "JKG", "JKO", "JKB", "JKV", "JKQ", "JX", "JC",
		"EP", "EF", "EC", "ETN", "ETM",
		"V"
	};
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
	maxP = candidate[0]->p;
	for (auto m : candidate)
	{
		if (cv != m->vowel)
		{
			cv = (int)cv && (int)m->vowel ? KCondVowel::any : KCondVowel::none;
		}
		if (cp != m->polar) cp = KCondPolarity::none;
		if (!m->chunks.empty() && m->chunks[0]->tag == KPOSTag::V) hasFirstV = true;
		if (m->p > maxP) maxP = m->p;
	}
	vowel = cv;
	polar = cp;
	if (suffix.find(0) != suffix.end()) suffix = {};
}
