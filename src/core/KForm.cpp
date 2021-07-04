#include <cassert>

#include "KForm.h"
#include "Utils.h"
#include "serializer.hpp"

using namespace std;
using namespace kiwi;

#ifdef _DEBUG
size_t Morpheme::uid = 0;
#endif

POSTag kiwi::toPOSTag(const u16string& tagStr)
{
	if (tagStr == u"NNG") return POSTag::nng;
	if (tagStr == u"NNP") return POSTag::nnp;
	if (tagStr == u"NNB") return POSTag::nnb;
	if (tagStr == u"NR") return POSTag::nr;
	if (tagStr == u"NP") return POSTag::np;
	if (tagStr == u"VV") return POSTag::vv;
	if (tagStr == u"VA") return POSTag::va;
	if (tagStr == u"VX") return POSTag::vx;
	if (tagStr == u"VCP") return POSTag::vcp;
	if (tagStr == u"VCN") return POSTag::vcn;
	if (tagStr == u"MM") return POSTag::mm;
	if (tagStr == u"MAG") return POSTag::mag;
	if (tagStr == u"MAJ") return POSTag::maj;
	if (tagStr == u"IC") return POSTag::ic;
	if (tagStr == u"JKS") return POSTag::jks;
	if (tagStr == u"JKC") return POSTag::jkc;
	if (tagStr == u"JKG") return POSTag::jkg;
	if (tagStr == u"JKO") return POSTag::jko;
	if (tagStr == u"JKB") return POSTag::jkb;
	if (tagStr == u"JKV") return POSTag::jkv;
	if (tagStr == u"JKQ") return POSTag::jkq;
	if (tagStr == u"JX") return POSTag::jx;
	if (tagStr == u"JC") return POSTag::jc;
	if (tagStr == u"EP") return POSTag::ep;
	if (tagStr == u"EF") return POSTag::ef;
	if (tagStr == u"EC") return POSTag::ec;
	if (tagStr == u"ETN") return POSTag::etn;
	if (tagStr == u"ETM") return POSTag::etm;
	if (tagStr == u"XPN") return POSTag::xpn;
	if (tagStr == u"XSN") return POSTag::xsn;
	if (tagStr == u"XSV") return POSTag::xsv;
	if (tagStr == u"XSA") return POSTag::xsa;
	if (tagStr == u"XR") return POSTag::xr;
	if (tagStr == u"SF") return POSTag::sf;
	if (tagStr == u"SP") return POSTag::sp;
	if (tagStr == u"SS") return POSTag::ss;
	if (tagStr == u"SE") return POSTag::se;
	if (tagStr == u"SO") return POSTag::so;
	if (tagStr == u"SW") return POSTag::sw;
	if (tagStr == u"NF") return POSTag::unknown;
	if (tagStr == u"NV") return POSTag::unknown;
	if (tagStr == u"NA") return POSTag::unknown;
	if (tagStr == u"SL") return POSTag::sl;
	if (tagStr == u"SH") return POSTag::sh;
	if (tagStr == u"SN") return POSTag::sn;
	if (tagStr == u"V") return POSTag::v;
	if (tagStr == u"A") return POSTag::v;
	if (tagStr == u"^") return POSTag::unknown;
	if (tagStr == u"W_URL") return POSTag::w_url;
	if (tagStr == u"W_EMAIL") return POSTag::w_email;
	if (tagStr == u"W_HASHTAG") return POSTag::w_hashtag;
	if (tagStr == u"W_MENTION") return POSTag::w_mention;
	//assert(0);
	return POSTag::max;
}

const char * kiwi::tagToString(POSTag t)
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
		"SL", "SH", "SN",
		"W_URL", "W_EMAIL", "W_MENTION", "W_HASHTAG",
		"JKS", "JKC", "JKG", "JKO", "JKB", "JKV", "JKQ", "JX", "JC",
		"EP", "EF", "EC", "ETN", "ETM",
		"V",
		"@"
	};
	assert(t < POSTag::max);
	return tags[(size_t)t];
}

const kchar_t * kiwi::tagToKString(POSTag t)
{
	static const kchar_t* tags[] =
	{
		u"UN",
		u"NNG", u"NNP", u"NNB",
		u"VV", u"VA",
		u"MAG",
		u"NR", u"NP",
		u"VX",
		u"MM", u"MAJ",
		u"IC",
		u"XPN", u"XSN", u"XSV", u"XSA", u"XR",
		u"VCP", u"VCN",
		u"SF", u"SP", u"SS", u"SE", u"SO", u"SW",
		u"SL", u"SH", u"SN",
		u"W_URL", u"W_EMAIL", u"W_MENTION", u"W_HASHTAG",
		u"JKS", u"JKC", u"JKG", u"JKO", u"JKB", u"JKV", u"JKQ", u"JX", u"JC",
		u"EP", u"EF", u"EC", u"ETN", u"ETM",
		u"V",
		u"@"
	};
	assert(t < POSTag::max);
	return tags[(size_t)t];
}

Form::Form(const kchar_t * _form)
{
	if (_form) form = _form;
}

template<class _Istream>
void Form::readFromBin(_Istream& is, const function<const Morpheme*(size_t)>& mapper)
{
	form = serializer::readFromBinStream<KString>(is);
	vowel = serializer::readFromBinStream<CondVowel>(is);
	polar = serializer::readFromBinStream<CondPolarity>(is);
	candidate.resize(serializer::readFromBinStream<uint32_t>(is));
	for (auto& c : candidate)
	{
		c = mapper(serializer::readFromBinStream<uint32_t>(is));
	}
}

template void Form::readFromBin<istream>(istream& is, const function<const Morpheme*(size_t)>& mapper);
template void Form::readFromBin<serializer::imstream>(serializer::imstream& is, const function<const Morpheme*(size_t)>& mapper);

void Form::writeToBin(ostream& os, const function<size_t(const Morpheme*)>& mapper) const
{
	serializer::writeToBinStream(os, form);
	serializer::writeToBinStream(os, vowel);
	serializer::writeToBinStream(os, polar);
	serializer::writeToBinStream<uint32_t>(os, candidate.size());
	for (auto c : candidate)
	{
		serializer::writeToBinStream<uint32_t>(os, mapper(c));
	}
}

template<class _Istream>
void Morpheme::readFromBin(_Istream& is, const function<const Morpheme*(size_t)>& mapper)
{
	kform = (const KString*)serializer::readFromBinStream<uint32_t>(is);
	serializer::readFromBinStream(is, tag);
	serializer::readFromBinStream(is, vowel);
	serializer::readFromBinStream(is, polar);
	serializer::readFromBinStream(is, combineSocket);
	serializer::readFromBinStream(is, combined);
	serializer::readFromBinStream(is, userScore);

	size_t s = serializer::readFromBinStream<uint32_t>(is);
	if (s)
	{
		chunks.reset(new vector<const Morpheme*>(s));
		for (auto& c : *chunks)
		{
			c = mapper(serializer::readFromBinStream<uint32_t>(is));
		}
	}
}

template void Morpheme::readFromBin<istream>(istream& is, const function<const Morpheme*(size_t)>& mapper);
template void Morpheme::readFromBin<serializer::imstream>(serializer::imstream& is, const function<const Morpheme*(size_t)>& mapper);

void Morpheme::writeToBin(ostream& os, const function<size_t(const Morpheme*)>& mapper) const
{
	serializer::writeToBinStream<uint32_t>(os, (uint32_t)(size_t)kform);
	serializer::writeToBinStream(os, tag);
	serializer::writeToBinStream(os, vowel);
	serializer::writeToBinStream(os, polar);
	serializer::writeToBinStream(os, combineSocket);
	serializer::writeToBinStream(os, combined);
	serializer::writeToBinStream(os, userScore);

	serializer::writeToBinStream<uint32_t>(os, chunks ? chunks->size() : 0);
	if(chunks) for (auto c : *chunks)
	{
		serializer::writeToBinStream<uint32_t>(os, mapper(c));
	}
}

std::ostream & kiwi::Morpheme::print(std::ostream & os) const
{
	os << utf16To8(kform ? u16string{ kform->begin(), kform->end() } : u"_");
	os << '/';
	os << tagToString(tag);
	if (combineSocket) os << '+' << (size_t)combineSocket;
	return os;
}
