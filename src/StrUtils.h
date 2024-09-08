#pragma once
#include <vector>
#include <utility>
#include <kiwi/Types.h>
#include <kiwi/Utils.h>
#include "string_view.hpp"

namespace kiwi
{
	template<class ChrIterator>
	inline long stol(ChrIterator begin, ChrIterator end)
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
		long up = 0;
		for (; begin != end; ++begin)
		{
			if ('0' <= *begin && *begin <= '9') up = up * 10 + (*begin - '0');
			else break;
		}
		return up * (sign ? -1 : 1);
	}

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
		return (float)up * (sign ? -1 : 1);
	}


	template<class BaseStr, class BaseChr, class OutIterator>
	OutIterator split(BaseStr&& s, BaseChr delim, OutIterator result, size_t maxSplit = -1, BaseChr delimEscape = 0)
	{
		size_t p = 0, e = 0;
		for (size_t i = 0; i < maxSplit; ++i)
		{
			size_t t = s.find(delim, p);
			if (t == s.npos)
			{
				*(result++) = nonstd::basic_string_view<BaseChr>{ &s[e] , s.size() - e};
				return result;
			}
			else
			{
				if (delimEscape && delimEscape != delim && t > 0 && s[t - 1] == delimEscape)
				{
					p = t + 1;
				}
				else if (delimEscape && delimEscape == delim && t < s.size() - 1 && s[t + 1] == delimEscape)
				{
					p = t + 2;
				}
				else
				{
					*(result++) = nonstd::basic_string_view<BaseChr>{ &s[e] , t - e };
					p = t + 1;
					e = t + 1;
				}
			}
		}
		*(result++) = nonstd::basic_string_view<BaseChr>{ &s[e] , s.size() - e };
		return result;
	}

	template<class BaseChr, class Trait>
	inline std::vector<nonstd::basic_string_view<BaseChr, Trait>> split(nonstd::basic_string_view<BaseChr, Trait> s, BaseChr delim, BaseChr delimEscape = 0)
	{
		std::vector<nonstd::basic_string_view<BaseChr, Trait>> ret;
		split(s, delim, std::back_inserter(ret), -1, delimEscape);
		return ret;
	}

	template<class BaseChr, class Trait, class Alloc>
	inline std::vector<nonstd::basic_string_view<BaseChr, Trait>> split(const std::basic_string<BaseChr, Trait, Alloc>& s, BaseChr delim, BaseChr delimEscape = 0)
	{
		std::vector<nonstd::basic_string_view<BaseChr, Trait>> ret;
		split(s, delim, std::back_inserter(ret), -1, delimEscape);
		return ret;
	}

	template<class BaseStr, class StrFrom, class StrTo, class OutIterator>
	OutIterator replace(BaseStr&& s, StrFrom&& from, StrTo&& to, OutIterator result)
	{
		size_t p = 0;
		while (true)
		{
			size_t t = s.find(from, p);
			if (t == s.npos)
			{
				break;
			}
			else
			{
				result = std::copy(s.begin() + p, s.begin() + t, result);
				result = std::copy(to.begin(), to.end(), result);
				p = t + from.size();
			}
		}
		result = std::copy(s.begin() + p, s.end(), result);
		return result;
	}

	template<class BaseChr, class Trait, class Alloc = std::allocator<BaseChr>>
	inline std::basic_string<BaseChr, Trait, Alloc> replace(
		nonstd::basic_string_view<BaseChr, Trait> s, 
		nonstd::basic_string_view<BaseChr, Trait> from, 
		nonstd::basic_string_view<BaseChr, Trait> to)
	{
		std::basic_string<BaseChr, Trait, Alloc> ret;
		ret.reserve(s.size());
		replace(s, from, to, std::back_inserter(ret));
		return ret;
	}

	template<class BaseChr, class Trait, size_t fromSize, size_t toSize, class Alloc = std::allocator<BaseChr>>
	inline std::basic_string<BaseChr, Trait, Alloc> replace(
		nonstd::basic_string_view<BaseChr, Trait> s,
		const BaseChr(&from)[fromSize],
		const BaseChr(&to)[toSize])
	{
		return replace(s, nonstd::basic_string_view<BaseChr, Trait>{ from, fromSize - 1 }, nonstd::basic_string_view<BaseChr, Trait>{ to, toSize - 1 });
	}
	

	inline void utf8To16(nonstd::string_view str, std::u16string& ret)
	{
		ret.clear();
		for (auto it = str.begin(); it != str.end(); ++it)
		{
			uint32_t code = 0;
			uint32_t byte = (uint8_t)*it;
			if ((byte & 0xF8) == 0xF0)
			{
				code = (uint32_t)((byte & 0x07) << 18);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (uint32_t)((byte & 0x3F) << 12);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (uint32_t)((byte & 0x3F) << 6);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xF0) == 0xE0)
			{
				code = (uint32_t)((byte & 0x0F) << 12);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (uint32_t)((byte & 0x3F) << 6);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xE0) == 0xC0)
			{
				code = (uint32_t)((byte & 0x1F) << 6);
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
				ret.push_back((char16_t)code);
			}
			else if (code < 0x10FFFF)
			{
				code -= 0x10000;
				ret.push_back((char16_t)(0xD800 | (code >> 10)));
				ret.push_back((char16_t)(0xDC00 | (code & 0x3FF)));
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

	template<class Ty, class Alloc>
	inline std::u16string utf8To16(nonstd::string_view str, std::vector<Ty, Alloc>& bytePositions)
	{
		std::u16string ret;
		bytePositions.clear();
		for (auto it = str.begin(); it != str.end(); ++it)
		{
			size_t pos = (size_t)(it - str.begin());
			uint32_t code = 0;
			uint32_t byte = *it;
			if ((byte & 0xF8) == 0xF0)
			{
				code = (uint32_t)((byte & 0x07) << 18);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (uint32_t)((byte & 0x3F) << 12);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (uint32_t)((byte & 0x3F) << 6);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xF0) == 0xE0)
			{
				code = (uint32_t)((byte & 0x0F) << 12);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (uint32_t)((byte & 0x3F) << 6);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xE0) == 0xC0)
			{
				code = (uint32_t)((byte & 0x1F) << 6);
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
				ret.push_back((char16_t)code);
				bytePositions.emplace_back(pos);
			}
			else if (code < 0x10FFFF)
			{
				code -= 0x10000;
				ret.push_back((char16_t)(0xD800 | (code >> 10)));
				ret.push_back((char16_t)(0xDC00 | (code & 0x3FF)));
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

	template<class Ty, class Alloc>
	inline std::u16string utf8To16ChrPoisition(nonstd::string_view str, std::vector<Ty, Alloc>& chrPositions)
	{
		std::u16string ret;
		size_t chrPosition = 0;
		chrPositions.clear();
		for (auto it = str.begin(); it != str.end(); ++it)
		{
			uint32_t code = 0;
			uint32_t byte = *it;
			if ((byte & 0xF8) == 0xF0)
			{
				code = (uint32_t)((byte & 0x07) << 18);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (uint32_t)((byte & 0x3F) << 12);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (uint32_t)((byte & 0x3F) << 6);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xF0) == 0xE0)
			{
				code = (uint32_t)((byte & 0x0F) << 12);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (uint32_t)((byte & 0x3F) << 6);
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xE0) == 0xC0)
			{
				code = (uint32_t)((byte & 0x1F) << 6);
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
				ret.push_back((char16_t)code);
				chrPositions.emplace_back(chrPosition++);
			}
			else if (code < 0x10FFFF)
			{
				code -= 0x10000;
				ret.push_back((char16_t)(0xD800 | (code >> 10)));
				ret.push_back((char16_t)(0xDC00 | (code & 0x3FF)));
				chrPositions.emplace_back(chrPosition);
				chrPositions.emplace_back(chrPosition++);
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
			if (isHighSurrogate(code))
			{
				if (++it == str.end()) throw UnicodeException{ "unpaired surrogate" };
				size_t code2 = *it;
				if (!isLowSurrogate(code2)) throw UnicodeException{ "unpaired surrogate" };
				code = mergeSurrogate(code, code2);
			}

			if (code <= 0x7F)
			{
				ret.push_back((char)code);
			}
			else if (code <= 0x7FF)
			{
				ret.push_back((char)(0xC0 | (code >> 6)));
				ret.push_back((char)(0x80 | (code & 0x3F)));
			}
			else if (code <= 0xFFFF)
			{
				ret.push_back((char)(0xE0 | (code >> 12)));
				ret.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
				ret.push_back((char)(0x80 | (code & 0x3F)));
			}
			else if (code <= 0x10FFFF)
			{
				ret.push_back((char)(0xF0 | (code >> 18)));
				ret.push_back((char)(0x80 | ((code >> 12) & 0x3F)));
				ret.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
				ret.push_back((char)(0x80 | (code & 0x3F)));
			}
			else
			{
				throw UnicodeException{ "unicode error" };
			}
		}

		return ret;
	}

	template<class Ty, class Alloc>
	inline std::string utf16To8(nonstd::u16string_view str, std::vector<Ty, Alloc>& positions)
	{
		std::string ret;
		positions.clear();
		for (auto it = str.begin(); it != str.end(); ++it)
		{
			size_t code = *it;
			positions.emplace_back(ret.size());
			if (isHighSurrogate(code))
			{
				if (++it == str.end()) throw UnicodeException{ "unpaired surrogate" };
				size_t code2 = *it;
				if (!isLowSurrogate(code2)) throw UnicodeException{ "unpaired surrogate" };
				positions.emplace_back(ret.size());
				code = mergeSurrogate(code, code2);
			}

			if (code <= 0x7F)
			{
				ret.push_back((char)code);
			}
			else if (code <= 0x7FF)
			{
				ret.push_back((char)(0xC0 | (code >> 6)));
				ret.push_back((char)(0x80 | (code & 0x3F)));
			}
			else if (code <= 0xFFFF)
			{
				ret.push_back((char)(0xE0 | (code >> 12)));
				ret.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
				ret.push_back((char)(0x80 | (code & 0x3F)));
			}
			else if (code <= 0x10FFFF)
			{
				ret.push_back((char)(0xF0 | (code >> 18)));
				ret.push_back((char)(0x80 | ((code >> 12) & 0x3F)));
				ret.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
				ret.push_back((char)(0x80 | (code & 0x3F)));
			}
			else
			{
				throw UnicodeException{ "unicode error" };
			}
		}
		positions.emplace_back(ret.size());

		return ret;
	}

	inline std::string utf16To8(const char16_t* str)
	{
		return utf16To8(U16StringView{ str });
	}

	inline std::string utf16To8(char16_t str)
	{
		return utf16To8(U16StringView{ &str, 1 });
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

	template<class It, class StrOut, class PosOut>
	inline void normalizeHangulWithPosition(It first, It last, StrOut strOut, PosOut posOut)
	{
		size_t s = 0;
		for (; first != last; ++first)
		{
			auto c = *first;
			*posOut++ = s;
			if (c == 0xB42C) c = 0xB410;
			if (0xAC00 <= c && c < 0xD7A4)
			{
				int coda = (c - 0xAC00) % 28;
				*strOut++ = (c - coda);
				s++;
				if (coda)
				{
					*strOut++ = (coda + 0x11A7);
					s++;
				}
			}
			else
			{
				*strOut++ = c;
				s++;
			}
		}
		*posOut++ = s;
	}

	template<class It>
	inline std::pair<KString, Vector<size_t>> normalizeHangulWithPosition(It first, It last)
	{
		KString ret;
		Vector<size_t> pos;
		ret.reserve((size_t)(std::distance(first, last) * 1.5));
		normalizeHangulWithPosition(first, last, std::back_inserter(ret), std::back_inserter(pos));
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
		if (tagStr == u"XSM") return POSTag::xsm;
		if (tagStr == u"XR") return POSTag::xr;
		if (tagStr == u"SF") return POSTag::sf;
		if (tagStr == u"SP") return POSTag::sp;
		if (tagStr == u"SS") return POSTag::ss;
		if (tagStr == u"SSO") return POSTag::sso;
		if (tagStr == u"SSC") return POSTag::ssc;
		if (tagStr == u"SE") return POSTag::se;
		if (tagStr == u"SO") return POSTag::so;
		if (tagStr == u"SW") return POSTag::sw;
		if (tagStr == u"SB") return POSTag::sb;
		if (tagStr == u"NF") return POSTag::unknown;
		if (tagStr == u"NV") return POSTag::unknown;
		if (tagStr == u"NA") return POSTag::unknown;
		if (tagStr == u"UNK") return POSTag::unknown;
		if (tagStr == u"UN") return POSTag::unknown;
		if (tagStr == u"SL") return POSTag::sl;
		if (tagStr == u"SH") return POSTag::sh;
		if (tagStr == u"SN") return POSTag::sn;
		if (tagStr == u"Z_CODA") return POSTag::z_coda;
		if (tagStr == u"V") return POSTag::p;
		if (tagStr == u"A") return POSTag::p;
		if (tagStr == u"^") return POSTag::unknown;
		if (tagStr == u"W_URL") return POSTag::w_url;
		if (tagStr == u"W_EMAIL") return POSTag::w_email;
		if (tagStr == u"W_HASHTAG") return POSTag::w_hashtag;
		if (tagStr == u"W_MENTION") return POSTag::w_mention;
		if (tagStr == u"W_SERIAL") return POSTag::w_serial;
		if (tagStr == u"W_EMOJI") return POSTag::w_emoji;

		if (tagStr == u"USER0") return POSTag::user0;
		if (tagStr == u"USER1") return POSTag::user1;
		if (tagStr == u"USER2") return POSTag::user2;
		if (tagStr == u"USER3") return POSTag::user3;
		if (tagStr == u"USER4") return POSTag::user4;
		
		if (tagStr == u"VV-I") return setIrregular(POSTag::vv);
		if (tagStr == u"VA-I") return setIrregular(POSTag::va);
		if (tagStr == u"VX-I") return setIrregular(POSTag::vx);
		if (tagStr == u"XSA-I") return setIrregular(POSTag::xsa);

		if (tagStr == u"VV-R") return POSTag::vv;
		if (tagStr == u"VA-R") return POSTag::va;
		if (tagStr == u"VX-R") return POSTag::vx;
		if (tagStr == u"XSA-R") return POSTag::xsa;
		//assert(0);
		return POSTag::max;
	}

	template<class It>
	inline void normalizeCoda(It begin, It end)
	{
		static char16_t codaToOnsetTable[] = {
			0x3131, // ㄱ
			0x3131, // ㄲ
			0x3145, // ㄳ
			0x3134, // ㄴ
			0x3148, // ㄵ
			0x314E, // ㄶ
			0x3137, // ㄷ
			0x3139, // ㄹ
			0x3131, // ㄺ
			0x3141, // ㄻ
			0x3142, // ㄼ
			0x3145, // ㄽ
			0x314C, // ㄾ
			0x314D, // ㄿ
			0x314E, // ㅀ
			0x3141, // ㅁ
			0x3142, // ㅂ
			0x3145, // ㅄ
			0x3145, // ㅅ
			0x3145, // ㅆ
			0x3147, // ㅇ
			0x3148, // ㅈ
			0x314A, // ㅊ
			0x314B, // ㅋ
			0x314C, // ㅌ
			0x314D, // ㅍ
			0x314E, // ㅎ
		};
		static char16_t codaConversionTable[] = {
			0, // ㄱ
			0x11A8, // ㄲ
			0x11A8, // ㄳ
			0, // ㄴ
			0x11AB, // ㄵ
			0x11AB, // ㄶ
			0, // ㄷ
			0, // ㄹ
			0x11AF, // ㄺ
			0x11AF, // ㄻ
			0x11AF, // ㄼ
			0x11AF, // ㄽ
			0x11AF, // ㄾ
			0x11AF, // ㄿ
			0x11AF, // ㅀ
			0, // ㅁ
			0, // ㅂ
			0x11B8, // ㅄ
			0, // ㅅ
			0x11BA, // ㅆ
			0, // ㅇ
			0, // ㅈ
			0, // ㅊ
			0, // ㅋ
			0, // ㅌ
			0, // ㅍ
			0, // ㅎ
		};
		char16_t before = 0;
		for (auto it = begin; it != end; ++it)
		{
			if (0x11A8 <= before && before <= 0x11C2)
			{
				auto offset = before - 0x11A8;
				if (*it == codaToOnsetTable[offset])
				{
					if (codaConversionTable[offset]) it[-1] = codaConversionTable[offset];
					else it[-1] = *it;
				}
			}
			before = *it;
		}
	}

	inline bool isChineseChr(char32_t c)
	{
		return (0x4E00 <= c && c <= 0x9FFF)
			|| (0x3400 <= c && c <= 0x4DBF)
			|| (0x20000 <= c && c <= 0x2A6DF)
			|| (0x2A700 <= c && c <= 0x2B73F)
			|| (0x2B820 <= c && c <= 0x2CEAF)
			|| (0x2CEB0 <= c && c <= 0x2EBEF)
			|| (0x30000 <= c && c <= 0x3134F)
			|| (0x31350 <= c && c <= 0x323AF)
			|| (0xF900 <= c && c <= 0xFAFF)
			|| (0x2F800 <= c && c <= 0x2FA1F)
			|| (0x2F00 <= c && c <= 0x2FDF)
			|| (0x2E80 <= c && c <= 0x2EFF)
		;
	}
}
