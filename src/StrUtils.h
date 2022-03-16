#pragma once
#include <string>
#include <vector>
#include <utility>
#include "string_view.hpp"

namespace kiwi
{
	template<class ChrIterator>
	inline float stof(ChrIterator begin, ChrIterator end)
	{
		if (begin == end) return 0;
		bool sign = false;
		switch (*begin)
		{
		case '-':
			sign = true;
		case '+':
			++begin;
			break;
		}
		double up = 0, down = 0;
		for (; begin != end; ++begin)
		{
			if ('0' <= *begin && *begin <= '9') up = up * 10 + (*begin - '0');
			else break;
		}
		if (begin != end && *begin == '.')
		{
			++begin;
			float d = 1;
			for (; begin != end; ++begin)
			{
				if ('0' <= *begin && *begin <= '9')
				{
					down = down * 10 + (*begin - '0');
					d /= 10;
				}
				else break;
			}
			up += down * d;
		}
		return up * (sign ? -1 : 1);
	}


	template<class BaseStr, class BaseChr, class OutIterator>
	OutIterator split(BaseStr&& s, BaseChr delim, OutIterator result, size_t maxSplit=-1)
	{
		size_t p = 0;
		for (size_t i = 0; i < maxSplit; ++i)
		{
			size_t t = s.find(delim, p);
			if (t == s.npos)
			{
				*(result++) = nonstd::basic_string_view<BaseChr>{ &s[p] , s.size() - p};
				return result;
			}
			else
			{
				*(result++) = nonstd::basic_string_view<BaseChr>{ &s[p] , t - p };
				p = t + 1;
			}
		}
		*(result++) = nonstd::basic_string_view<BaseChr>{ &s[p] , s.size() - p };
		return result;
	}

	template<class BaseChr, class Trait>
	inline std::vector<nonstd::basic_string_view<BaseChr, Trait>> split(nonstd::basic_string_view<BaseChr, Trait> s, BaseChr delim)
	{
		std::vector<nonstd::basic_string_view<BaseChr, Trait>> ret;
		split(s, delim, std::back_inserter(ret));
		return ret;
	}

	template<class BaseChr, class Trait, class Alloc>
	inline std::vector<nonstd::basic_string_view<BaseChr, Trait>> split(const std::basic_string<BaseChr, Trait, Alloc>& s, BaseChr delim)
	{
		std::vector<nonstd::basic_string_view<BaseChr, Trait>> ret;
		split(s, delim, std::back_inserter(ret));
		return ret;
	}

	inline void utf8To16(nonstd::string_view str, std::u16string& ret)
	{
		ret.clear();
		for (auto it = str.begin(); it != str.end(); ++it)
		{
			size_t code = 0;
			uint8_t byte = *it;
			if ((byte & 0xF8) == 0xF0)
			{
				code = (byte & 0x07) << 18;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F) << 12;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F) << 6;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xF0) == 0xE0)
			{
				code = (byte & 0x0F) << 12;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F) << 6;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xE0) == 0xC0)
			{
				code = (byte & 0x1F) << 6;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0x80) == 0x00)
			{
				code = byte;
			}
			else
			{
				throw UnicodeException{ "unicode error" };
			}

			if (code < 0x10000)
			{
				ret.push_back(code);
			}
			else if (code < 0x10FFFF)
			{
				code -= 0x10000;
				ret.push_back(0xD800 | (code >> 10));
				ret.push_back(0xDC00 | (code & 0x3FF));
			}
			else
			{
				throw UnicodeException{ "unicode error" };
			}
		}
	}

	inline std::u16string utf8To16(nonstd::string_view str)
	{
		std::u16string ret;
		utf8To16(str, ret);
		return ret;
	}

	inline std::u16string utf8To16(nonstd::string_view str, std::vector<size_t>& bytePositions)
	{
		std::u16string ret;
		bytePositions.clear();
		for (auto it = str.begin(); it != str.end(); ++it)
		{
			size_t pos = (size_t)(it - str.begin());
			size_t code = 0;
			uint8_t byte = *it;
			if ((byte & 0xF8) == 0xF0)
			{
				code = (byte & 0x07) << 18;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F) << 12;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F) << 6;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xF0) == 0xE0)
			{
				code = (byte & 0x0F) << 12;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F) << 6;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xE0) == 0xC0)
			{
				code = (byte & 0x1F) << 6;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0x80) == 0x00)
			{
				code = byte;
			}
			else
			{
				throw UnicodeException{ "unicode error" };
			}

			if (code < 0x10000)
			{
				ret.push_back(code);
				bytePositions.emplace_back(pos);
			}
			else if (code < 0x10FFFF)
			{
				code -= 0x10000;
				ret.push_back(0xD800 | (code >> 10));
				ret.push_back(0xDC00 | (code & 0x3FF));
				bytePositions.emplace_back(pos);
				bytePositions.emplace_back(pos);
			}
			else
			{
				throw UnicodeException{ "unicode error" };
			}
		}
		return ret;
	}

	inline std::string utf16To8(nonstd::u16string_view str)
	{
		std::string ret;
		for (auto it = str.begin(); it != str.end(); ++it)
		{
			size_t code = *it;
			if ((code & 0xFC00) == 0xD800)
			{
				if (++it == str.end()) throw UnicodeException{ "unpaired surrogate" };
				size_t code2 = *it;
				if ((code2 & 0xFC00) != 0xDC00) throw UnicodeException{ "unpaired surrogate" };
				code = ((code & 0x3FF) << 10) | (code2 & 0x3FF);
				code += 0x10000;
			}

			if (code <= 0x7F)
			{
				ret.push_back(code);
			}
			else if (code <= 0x7FF)
			{
				ret.push_back(0xC0 | (code >> 6));
				ret.push_back(0x80 | (code & 0x3F));
			}
			else if (code <= 0xFFFF)
			{
				ret.push_back(0xE0 | (code >> 12));
				ret.push_back(0x80 | ((code >> 6) & 0x3F));
				ret.push_back(0x80 | (code & 0x3F));
			}
			else if (code <= 0x10FFFF)
			{
				ret.push_back(0xF0 | (code >> 18));
				ret.push_back(0x80 | ((code >> 12) & 0x3F));
				ret.push_back(0x80 | ((code >> 6) & 0x3F));
				ret.push_back(0x80 | (code & 0x3F));
			}
			else
			{
				throw UnicodeException{ "unicode error" };
			}
		}

		return ret;
	}

	template<class It>
	inline KString normalizeHangul(It first, It last)
	{
		KString ret;
		ret.reserve((size_t)(std::distance(first, last) * 1.5));
		for (; first != last; ++first)
		{
			char16_t c = *first;
			if (c == 0xB42C) c = 0xB410;
			if (0xAC00 <= c && c < 0xD7A4)
			{
				int coda = (c - 0xAC00) % 28;
				ret.push_back(c - coda);
				if (coda) ret.push_back(coda + 0x11A7);
			}
			else
			{
				ret.push_back(c);
			}
		}
		return ret;
	}

	inline KString normalizeHangul(const std::u16string& hangul)
	{
		return normalizeHangul(hangul.begin(), hangul.end());
	}

	inline KString normalizeHangul(nonstd::u16string_view hangul)
	{
		return normalizeHangul(hangul.begin(), hangul.end());
	}

	template<class It>
	inline std::pair<KString, Vector<size_t>> normalizeHangulWithPosition(It first, It last)
	{
		KString ret;
		Vector<size_t> pos;
		ret.reserve((size_t)(std::distance(first, last) * 1.5));
		for (; first != last; ++first)
		{
			auto c = *first;
			pos.emplace_back(ret.size());
			if (c == 0xB42C) c = 0xB410;
			if (0xAC00 <= c && c < 0xD7A4)
			{
				int coda = (c - 0xAC00) % 28;
				ret.push_back(c - coda);
				if (coda) ret.push_back(coda + 0x11A7);
			}
			else
			{
				ret.push_back(c);
			}
		}
		pos.emplace_back(ret.size());
		return make_pair(move(ret), move(pos));
	}

	inline std::pair<KString, Vector<size_t>> normalizeHangulWithPosition(const std::u16string& hangul)
	{
		return normalizeHangulWithPosition(hangul.begin(), hangul.end());
	}

	inline std::pair<KString, Vector<size_t>> normalizeHangulWithPosition(nonstd::u16string_view hangul)
	{
		return normalizeHangulWithPosition(hangul.begin(), hangul.end());
	}

	inline KString normalizeHangul(const std::string& hangul)
	{
		return normalizeHangul(utf8To16(hangul));
	}

	inline KString normalizeHangul(nonstd::string_view hangul)
	{
		return normalizeHangul(utf8To16(hangul));
	}

	inline POSTag toPOSTag(nonstd::u16string_view tagStr)
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
		if (tagStr == u"V") return POSTag::p;
		if (tagStr == u"A") return POSTag::p;
		if (tagStr == u"^") return POSTag::unknown;
		if (tagStr == u"W_URL") return POSTag::w_url;
		if (tagStr == u"W_EMAIL") return POSTag::w_email;
		if (tagStr == u"W_HASHTAG") return POSTag::w_hashtag;
		if (tagStr == u"W_MENTION") return POSTag::w_mention;
		//assert(0);
		return POSTag::max;
	}
}