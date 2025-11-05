#include <cassert>
#include <fstream>
#include <kiwi/Utils.h>
#include <kiwi/Mmap.h>
#include "StrUtils.h"

namespace kiwi
{
	std::u16string utf8To16(const std::string & str)
	{
		return utf8To16(toStringView(str));
	}

	std::u16string utf8To16(const std::string& str, std::vector<size_t>& bytePositions)
	{
		return utf8To16(toStringView(str), bytePositions);
	}

	size_t utf8FromCode(std::string& ret, char32_t code)
	{
		if (code <= 0x7F)
		{
			ret.push_back(code);
			return 1;
		}
		else if (code <= 0x7FF)
		{
			ret.push_back(0xC0 | (code >> 6));
			ret.push_back(0x80 | (code & 0x3F));
			return 2;
		}
		else if (code <= 0xFFFF)
		{
			ret.push_back(0xE0 | (code >> 12));
			ret.push_back(0x80 | ((code >> 6) & 0x3F));
			ret.push_back(0x80 | (code & 0x3F));
			return 3;
		}
		else if (code <= 0x10FFFF)
		{
			ret.push_back(0xF0 | (code >> 18));
			ret.push_back(0x80 | ((code >> 12) & 0x3F));
			ret.push_back(0x80 | ((code >> 6) & 0x3F));
			ret.push_back(0x80 | (code & 0x3F));
			return 4;
		}
		return 0;
	}

	std::string utf8FromCode(char32_t code)
	{
		std::string ret;
		utf8FromCode(ret, code);
		return ret;
	}

	std::string utf16To8(const std::u16string & str)
	{
		return utf16To8(toStringView(str));
	}

	KString normalizeHangul(const std::u16string& hangul)
	{
		return normalizeHangul(hangul.begin(), hangul.end());
	}

	/**
	* 문자의 타입을 변별한다. 타입에 따라 다음 값을 반환한다.
	* 
	* - 공백: POSTag::unknown
	* - 한글: POSTag::max
	* - 숫자: POSTag::sn
	* - 로마자 알파벳: POSTag::sl
	* 
	*/
	POSTag identifySpecialChr(char32_t chr)
	{
		if (chr < 0x10000 && isSpace((char16_t)chr)) return POSTag::unknown;
		if (0x2000 <= chr && chr <= 0x200F) return POSTag::unknown;

		if ('0' <= chr && chr <= '9') return POSTag::sn;
		if (('A' <= chr && chr <= 'Z') ||
			('a' <= chr && chr <= 'z'))  return POSTag::sl;
		if (0xAC00 <= chr && chr < 0xD7A4) return POSTag::max;
		if (chr < 0x10000 && (isOldHangulOnset((char16_t)chr) 
			|| isOldHangulVowel((char16_t)chr) 
			|| isOldHangulCoda((char16_t)chr) 
			|| isOldHangulToneMark((char16_t)chr))
		) return POSTag::max;
		switch (chr)
		{
		case '.':
		case '!':
		case '?':
		case 0x2047:
		case 0x2048:
		case 0x2049:
		case 0x3002:
		case 0xff01:
		case 0xff0e:
		case 0xff1f:
		case 0xff61:
			return POSTag::sf;
		case '-':
		case '~':
		case 0x223c:
		case 0x301c:
		case 0xff5e:
			return POSTag::so;
		case 0x2026:
		case 0x205d:
			return POSTag::se;
		case ',':
		case ';':
		case ':':
		case '/':
		case 0xb7:
		case 0x3001:
		case 0xff0c:
		case 0xff1a:
		case 0xff1b:
		case 0xff64:
			return POSTag::sp;
		case '(':
		case '<':
		case '[':
		case '{':
		case 0x2018:
		case 0x201c:
		case 0x226a:
		case 0x3008:
		case 0x300a:
		case 0x300c:
		case 0x300e:
		case 0x3010:
		case 0x3014:
		case 0x3016:
		case 0x3018:
		case 0x301a:
		case 0xff08:
		case 0xff1c:
		case 0xff3b:
		case 0xff5b:
		case 0xff5f:
		case 0xff62:
			return POSTag::sso;
		case ')':
		case '>':
		case ']':
		case '}':
		case 0x2019:
		case 0x201d:
		case 0x226b:
		case 0x3009:
		case 0x300b:
		case 0x300d:
		case 0x300f:
		case 0x3011:
		case 0x3015:
		case 0x3017:
		case 0x3019:
		case 0x301b:
		case 0xff09:
		case 0xff1e:
		case 0xff3d:
		case 0xff5d:
		case 0xff60:
		case 0xff63:
			return POSTag::ssc;
		case '"':
		case '\'':
		case 0xad:
		case 0x2015:
		case 0x2500:
		case 0xff0d:
			return POSTag::ss;
		}
		if (isChineseChr(chr)) return POSTag::sh;
		if (0xd800 <= chr && chr <= 0xdfff) return POSTag::sh;
		
		return POSTag::sw;
	}

	size_t getSSType(char16_t c)
	{
		switch (c)
		{
		case '\'':
			return 1;
		case '"':
			return 2;
		case '(':
		case ')':
			return 3;
		case '<':
		case '>':
			return 4;
		case '[':
		case ']':
			return 5;
		case '{':
		case '}':
			return 6;
		case 0x2018:
		case 0x2019:
			return 7;
		case 0x201c:
		case 0x201d:
			return 8;
		case 0x226a:
		case 0x226b:
			return 9;
		case 0x3008:
		case 0x3009:
			return 10;
		case 0x300a:
		case 0x300b:
			return 11;
		case 0x300c:
		case 0x300d:
			return 12;
		case 0x300e:
		case 0x300f:
			return 13;
		case 0x3010:
		case 0x3011:
			return 14;
		case 0x3014:
		case 0x3015:
			return 15;
		case 0x3016:
		case 0x3017:
			return 16;
		case 0x3018:
		case 0x3019:
			return 17;
		case 0x301a:
		case 0x301b:
			return 18;
		case 0xff08:
		case 0xff09:
			return 19;
		case 0xff1c:
		case 0xff1e:
			return 20;
		case 0xff3b:
		case 0xff3d:
			return 21;
		case 0xff5b:
		case 0xff5d:
			return 22;
		case 0xff5f:
		case 0xff60:
			return 23;
		case 0xff62:
		case 0xff63:
			return 24;
		default:
			return 0;
		}
		return 0;
	}

	size_t getSBType(const std::u16string& form)
	{
		size_t format = 0, group = 0;
		char32_t chr = form[0];
		if (form.back() == u'.')
		{
			format = 1;
		}
		else if (form.back() == u')')
		{
			if (form[0] == u'(')
			{
				chr = form[1];
				format = 2;
			}
			else
			{
				format = 3;
			}
		}
		
		if (u'가' <= chr && chr <= u'힣') group = 1;
		else if (u'ㄱ' <= chr && chr <= u'ㅎ') group = 2;
		else if (u'0' <= chr && chr <= u'9') group = 3;
		else if (u'Ⅰ' <= chr && chr <= u'Ⅻ') group = 4;
		else if (u'ⅰ' <= chr && chr <= u'ⅻ') group = 5;
		else if (u'①' <= chr && chr <= u'⑳') return 24;
		else if (u'➀' <= chr && chr <= u'➉') return 24;
		else if (u'❶' <= chr && chr <= u'❿') return 25;
		else if (u'➊' <= chr && chr <= u'➓') return 25;
		else if (u'⑴' <= chr && chr <= u'⒇') return 26;
		else if (u'⒈' <= chr && chr <= u'⒛') return 27;

		return format | (group << 2);
	}

	POSTag toPOSTag(const std::u16string& tagStr)
	{
		return toPOSTag(toStringView(tagStr));
	}

	const char* tagToString(POSTag t)
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
			"XPN", "XSN", "XSV", "XSA", "XSM", "XR",
			"VCP", "VCN",
			"SF", "SP", "SS", "SSO", "SSC", "SE", "SO", "SW", "SB",
			"SL", "SH", "SN",
			"W_URL", "W_EMAIL", "W_MENTION", "W_HASHTAG", "W_SERIAL", "W_EMOJI",
			"JKS", "JKC", "JKG", "JKO", "JKB", "JKV", "JKQ", "JX", "JC",
			"EP", "EF", "EC", "ETN", "ETM",
			"Z_CODA", "Z_SIOT",
			"USER0", "USER1", "USER2", "USER3", "USER4",
			"P",
			"@"
		};
		if (isIrregular(t))
		{
			switch (clearIrregular(t))
			{
			case POSTag::vv: return "VV-I";
			case POSTag::va: return "VA-I";
			case POSTag::vx: return "VX-I";
			case POSTag::xsa: return "XSA-I";
			default: return "@";
			}
		}
		else
		{
			assert(t < POSTag::max);
			return tags[(size_t)t];
		}
	}

	const kchar_t* tagToKString(POSTag t)
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
			u"XPN", u"XSN", u"XSV", u"XSA", u"XSM", u"XR",
			u"VCP", u"VCN",
			u"SF", u"SP", u"SS", u"SSO", u"SSC", u"SE", u"SO", u"SW", u"SB",
			u"SL", u"SH", u"SN",
			u"W_URL", u"W_EMAIL", u"W_MENTION", u"W_HASHTAG", u"W_SERIAL", u"W_EMOJI",
			u"JKS", u"JKC", u"JKG", u"JKO", u"JKB", u"JKV", u"JKQ", u"JX", u"JC",
			u"EP", u"EF", u"EC", u"ETN", u"ETM",
			u"Z_CODA", u"Z_SIOT",
			u"USER0", u"USER1", u"USER2", u"USER3", u"USER4",
			u"P",
			u"@"
		};
		if (isIrregular(t))
		{
			switch (clearIrregular(t))
			{
			case POSTag::vv: return u"VV-I";
			case POSTag::va: return u"VA-I";
			case POSTag::vx: return u"VX-I";
			case POSTag::xsa: return u"XSA-I";
			default: return u"@";
			}
		}
		else
		{
			assert(t < POSTag::max);
			return tags[(size_t)t];
		}
	}

	const char* tagRToString(char16_t form, POSTag t)
	{
		if (isIrregular(t)) return tagToString(t);
		if (isHangulSyllable(form) && ((form - 0xAC00) % 28 == 7 || (form - 0xAC00) % 28 == 17 || (form - 0xAC00) % 28 == 19))
		{
			switch (t)
			{
			case POSTag::vv: return "VV-R";
			case POSTag::va: return "VA-R";
			case POSTag::vx: return "VX-R";
			case POSTag::xsa: return "XSA-R";
			default: break;
			}
		}
		return tagToString(t);
	}

	const kchar_t* tagRToKString(char16_t form, POSTag t)
	{
		if (isIrregular(t)) return tagToKString(t);
		if (isHangulSyllable(form) && ((form - 0xAC00) % 28 == 7 || (form - 0xAC00) % 28 == 17 || (form - 0xAC00) % 28 == 19))
		{
			switch (t)
			{
			case POSTag::vv: return u"VV-R";
			case POSTag::va: return u"VA-R";
			case POSTag::vx: return u"VX-R";
			case POSTag::xsa: return u"XSA-R";
			default: break;
			}
		}
		return tagToKString(t);
	}

	bool ComparatorIgnoringSpace::less(const KString& a, const KString& b, const char16_t space)
	{
		size_t i = 0, j = 0;
		while (i < a.size() && j < b.size())
		{
			if (a[i] == space && b[j] == space)
			{
				++i;
				++j;
				continue;
			}

			if (a[i] == space)
			{
				++i;
				continue;
			}

			if (b[j] == space)
			{
				++j;
				continue;
			}

			if (a[i] == b[j])
			{
				++i;
				++j;
				continue;
			}

			return a[i] < b[j];
		}
		if (i >= a.size() && j >= b.size()) return false;
		if (i >= a.size()) return true;
		return false;
	}

	bool ComparatorIgnoringSpace::equal(const KString& a, const KString& b, const kchar_t space)
	{
		size_t i = 0, j = 0;
		while (i < a.size() && j < b.size())
		{
			if (a[i] == space && b[j] == space)
			{
				++i;
				++j;
				continue;
			}

			if (a[i] == space)
			{
				++i;
				continue;
			}

			if (b[j] == space)
			{
				++j;
				continue;
			}

			if (a[i] == b[j])
			{
				++i;
				++j;
				continue;
			}

			return false;
		}
		if (i >= a.size() && j >= b.size()) return true;
		return false;
	}

	KString removeSpace(const KString& str, const kchar_t space)
	{
		KString ret;
		for (auto c : str)
		{
			if (c != space) ret.push_back(c);
		}
		return ret;
	}

	char16_t toCompatibleHangulConsonant(char16_t chr)
	{
		if (isHangulOnset(chr))
		{
			return u"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ"[chr - 0x1100];
		}
		else if (isHangulCoda(chr))
		{
			return u"ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ"[chr - 0x11A8];
		}
		return chr;
	}

	const char* modelTypeToStr(ModelType type)
	{
		switch (type)
		{
		case ModelType::none: return "none";
		case ModelType::largest: return "largest";
		case ModelType::knlm: return "knlm";
		case ModelType::knlmTransposed: return "knlm-transposed";
		case ModelType::sbg: return "sbg";
		case ModelType::cong: return "cong";
		case ModelType::congGlobal: return "cong-global";
		case ModelType::congFp32: return "cong-fp32";
		case ModelType::congGlobalFp32: return "cong-global-fp32";
		}
		return "unknown";
	}

	Dialect toDialect(std::string_view str)
	{
		if (str == u8"standard") return Dialect::standard;
		if (str == u8"std") return Dialect::standard;
		if (str == u8"표준") return Dialect::standard;
		if (str == u8"gyeonggi") return Dialect::gyeonggi;
		if (str == u8"gg") return Dialect::gyeonggi;
		if (str == u8"경기") return Dialect::gyeonggi;
		if (str == u8"chungcheong") return Dialect::chungcheong;
		if (str == u8"cc") return Dialect::chungcheong;
		if (str == u8"충청") return Dialect::chungcheong;
		if (str == u8"gangwon") return Dialect::gangwon;
		if (str == u8"gw") return Dialect::gangwon;
		if (str == u8"강원") return Dialect::gangwon;
		if (str == u8"gyeongsang") return Dialect::gyeongsang;
		if (str == u8"gs") return Dialect::gyeongsang;
		if (str == u8"경상") return Dialect::gyeongsang;
		if (str == u8"jeolla") return Dialect::jeolla;
		if (str == u8"jl") return Dialect::jeolla;
		if (str == u8"전라") return Dialect::jeolla;
		if (str == u8"jeju") return Dialect::jeju;
		if (str == u8"jj") return Dialect::jeju;
		if (str == u8"제주") return Dialect::jeju;
		if (str == u8"hwanghae") return Dialect::hwanghae;
		if (str == u8"hh") return Dialect::hwanghae;
		if (str == u8"황해") return Dialect::hwanghae;
		if (str == u8"hamgyeong") return Dialect::hamgyeong;
		if (str == u8"hg") return Dialect::hamgyeong;
		if (str == u8"함경") return Dialect::hamgyeong;
		if (str == u8"pyeongan") return Dialect::pyeongan;
		if (str == u8"pa") return Dialect::pyeongan;
		if (str == u8"평안") return Dialect::pyeongan;
		if (str == u8"archaic") return Dialect::archaic;
		if (str == u8"옛말") return Dialect::archaic;

		throw std::invalid_argument{ "Unknown dialect: " + std::string{ str } };
	}

	const char* dialectToStr(Dialect dialect)
	{
		switch (dialect)
		{
		case Dialect::standard: return "standard";
		case Dialect::gyeonggi: return "gyeonggi";
		case Dialect::chungcheong: return "chungcheong";
		case Dialect::gangwon: return "gangwon";
		case Dialect::gyeongsang: return "gyeongsang";
		case Dialect::jeolla: return "jeolla";
		case Dialect::jeju: return "jeju";
		case Dialect::hwanghae: return "hwanghae";
		case Dialect::hamgyeong: return "hamgyeong";
		case Dialect::pyeongan: return "pyeongan";
		case Dialect::archaic: return "archaic";
		}
		return "unknown";
	}

	Dialect parseDialects(std::string_view str)
	{
		Dialect ret = Dialect::standard;
		if (str == "all") return Dialect::all;
		for (auto& item : split(str, ','))
		{
			ret |= toDialect(item);
		}
		return ret;
	}

	namespace utils
	{
		std::function<std::unique_ptr<std::istream>(const std::string&)> makeFilesystemProvider(const std::string& modelPath)
		{
			return [modelPath](const std::string& filename) -> std::unique_ptr<std::istream> {
				std::string fullPath = modelPath + "/" + filename;
				auto stream = std::make_unique<std::ifstream>(fullPath, std::ios::binary);
				if (!stream->is_open()) {
					return nullptr;
				}
				return std::move(stream);
			};
		}

		std::function<std::unique_ptr<std::istream>(const std::string&)> makeMemoryProvider(const std::unordered_map<std::string, std::vector<char>>& fileData)
		{
			// Copy the fileData to ensure it stays alive for the lifetime of the provider
			auto sharedData = std::make_shared<std::unordered_map<std::string, std::vector<char>>>(fileData);
			
			return [sharedData](const std::string& filename) -> std::unique_ptr<std::istream> {
				auto it = sharedData->find(filename);
				if (it == sharedData->end()) {
					return nullptr;
				}
				
				const auto& data = it->second;
				return std::make_unique<utils::imstream>(data.data(), data.size());
			};
		}

		utils::MemoryObject createMemoryObjectFromStream(std::istream& stream)
		{
			stream.seekg(0, std::ios::end);
			if (stream) // seekable stream
			{
				size_t size = stream.tellg();
				stream.seekg(0, std::ios::beg);
				
				auto memoryOwner = std::make_shared<utils::MemoryOwner>(size);
				stream.read(static_cast<char*>(memoryOwner->get()), size);
				
				return utils::MemoryObject(std::move(*memoryOwner));
			}
			else // non-seekable stream
			{
				stream.clear();
				std::vector<char> buffer;
				static constexpr size_t chunkSize = 4096;
				char chunk[chunkSize];
				while (stream)
				{
					stream.read(chunk, chunkSize);
					buffer.insert(buffer.end(), chunk, chunk + stream.gcount());
				}
				
				auto memoryOwner = std::make_shared<utils::MemoryOwner>(buffer.size());
				std::memcpy(memoryOwner->get(), buffer.data(), buffer.size());
				
				return utils::MemoryObject(std::move(*memoryOwner));
			}
		}
	}
}
