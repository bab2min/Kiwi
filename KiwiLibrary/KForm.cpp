#include "stdafx.h"
#include "KForm.h"
#include "Utils.h"

using namespace std;

#ifdef _DEBUG
size_t KMorpheme::uid = 0;
#endif

KPOSTag makePOSTag(k_wstring tagStr)
{
	if (tagStr == KSTR("NNG")) return KPOSTag::NNG;
	if (tagStr == KSTR("NNP")) return KPOSTag::NNP;
	if (tagStr == KSTR("NNB")) return KPOSTag::NNB;
	if (tagStr == KSTR("NR")) return KPOSTag::NR;
	if (tagStr == KSTR("NP")) return KPOSTag::NP;
	if (tagStr == KSTR("VV")) return KPOSTag::VV;
	if (tagStr == KSTR("VA")) return KPOSTag::VA;
	if (tagStr == KSTR("VX")) return KPOSTag::VX;
	if (tagStr == KSTR("VCP")) return KPOSTag::VCP;
	if (tagStr == KSTR("VCN")) return KPOSTag::VCN;
	if (tagStr == KSTR("MM")) return KPOSTag::MM;
	if (tagStr == KSTR("MAG")) return KPOSTag::MAG;
	if (tagStr == KSTR("MAJ")) return KPOSTag::MAJ;
	if (tagStr == KSTR("IC")) return KPOSTag::IC;
	if (tagStr == KSTR("JKS")) return KPOSTag::JKS;
	if (tagStr == KSTR("JKC")) return KPOSTag::JKC;
	if (tagStr == KSTR("JKG")) return KPOSTag::JKG;
	if (tagStr == KSTR("JKO")) return KPOSTag::JKO;
	if (tagStr == KSTR("JKB")) return KPOSTag::JKB;
	if (tagStr == KSTR("JKV")) return KPOSTag::JKV;
	if (tagStr == KSTR("JKQ")) return KPOSTag::JKQ;
	if (tagStr == KSTR("JX")) return KPOSTag::JX;
	if (tagStr == KSTR("JC")) return KPOSTag::JC;
	if (tagStr == KSTR("EP")) return KPOSTag::EP;
	if (tagStr == KSTR("EF")) return KPOSTag::EF;
	if (tagStr == KSTR("EC")) return KPOSTag::EC;
	if (tagStr == KSTR("ETN")) return KPOSTag::ETN;
	if (tagStr == KSTR("ETM")) return KPOSTag::ETM;
	if (tagStr == KSTR("XPN")) return KPOSTag::XPN;
	if (tagStr == KSTR("XSN")) return KPOSTag::XSN;
	if (tagStr == KSTR("XSV")) return KPOSTag::XSV;
	if (tagStr == KSTR("XSA")) return KPOSTag::XSA;
	if (tagStr == KSTR("XR")) return KPOSTag::XR;
	if (tagStr == KSTR("SF")) return KPOSTag::SF;
	if (tagStr == KSTR("SP")) return KPOSTag::SP;
	if (tagStr == KSTR("SS")) return KPOSTag::SS;
	if (tagStr == KSTR("SE")) return KPOSTag::SE;
	if (tagStr == KSTR("SO")) return KPOSTag::SO;
	if (tagStr == KSTR("SW")) return KPOSTag::SW;
	if (tagStr == KSTR("NF")) return KPOSTag::NF;
	if (tagStr == KSTR("NV")) return KPOSTag::NV;
	if (tagStr == KSTR("NA")) return KPOSTag::NA;
	if (tagStr == KSTR("SL")) return KPOSTag::SL;
	if (tagStr == KSTR("SH")) return KPOSTag::SH;
	if (tagStr == KSTR("SN")) return KPOSTag::SN;
	if (tagStr == KSTR("V")) return KPOSTag::V;
	if (tagStr == KSTR("^")) return KPOSTag::UNKNOWN;
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

const wchar_t * tagToStringW(KPOSTag t)
{
	static const wchar_t* tags[] =
	{
		L"UN",
		L"NNG", L"NNP", L"NNB",
		L"VV", L"VA",
		L"MAG",
		L"NR", L"NP",
		L"VX",
		L"MM", L"MAJ",
		L"IC",
		L"XPN", L"XSN", L"XSV", L"XSA", L"XR",
		L"VCP", L"VCN",
		L"SF", L"SP", L"SS", L"SE", L"SO", L"SW",
		L"NF", L"NV", L"NA",
		L"SL", L"SH", L"SN",
		L"JKS", L"JKC", L"JKG", L"JKO", L"JKB", L"JKV", L"JKQ", L"JX", L"JC",
		L"EP", L"EF", L"EC", L"ETN", L"ETM",
		L"V",
		L"@"
	};
	assert(t < KPOSTag::MAX);
	return tags[(size_t)t];
}

KForm::KForm(const char16_t * _form)
{
	if (_form) form = _form;
}

void KForm::readFromBin(istream& is, const function<const KMorpheme*(size_t)>& mapper)
{
	form = readFromBinStream<k_string>(is);
	candidate.resize(readFromBinStream<uint32_t>(is));
	for (auto& c : candidate)
	{
		c = mapper(readFromBinStream<uint32_t>(is));
	}

	size_t s = readFromBinStream<uint32_t>(is);
	for (size_t i = 0; i < s; i++)
	{
		suffix.insert(readFromBinStream<char>(is));
	}
}

void KForm::writeToBin(ostream& os, const function<size_t(const KMorpheme*)>& mapper) const
{
	writeToBinStream(os, form);
	writeToBinStream<uint32_t>(os, candidate.size());
	for (auto c : candidate)
	{
		writeToBinStream<uint32_t>(os, mapper(c));
	}

	writeToBinStream<uint32_t>(os, suffix.size());
	for (auto c : suffix)
	{
		writeToBinStream<char>(os, c);
	}
}

void KMorpheme::readFromBin(istream& is, const function<const KMorpheme*(size_t)>& mapper)
{
	kform = nullptr;
	//fread(&kform, 1, 4, f);
	readFromBinStream(is, tag);
	readFromBinStream(is, vowel);
	readFromBinStream(is, polar);
	readFromBinStream(is, combineSocket);
	readFromBinStream(is, p);
	readFromBinStream(is, combined);

	size_t s = readFromBinStream<uint32_t>(is);
	if (s)
	{
		chunks = new vector<const KMorpheme*>(s);
		for (auto& c : *chunks)
		{
			c = mapper(readFromBinStream<uint32_t>(is));
		}
	}
}

void KMorpheme::writeToBin(ostream& os, const function<size_t(const KMorpheme*)>& mapper) const
{
	//fwrite(&kform, 1, 4, f);
	writeToBinStream(os, tag);
	writeToBinStream(os, vowel);
	writeToBinStream(os, polar);
	writeToBinStream(os, combineSocket);
	writeToBinStream(os, p);
	writeToBinStream(os, combined);

	writeToBinStream<uint32_t>(os, chunks ? chunks->size() : 0);
	if(chunks) for (auto c : *chunks)
	{
		writeToBinStream<uint32_t>(os, mapper(c));
	}
}
