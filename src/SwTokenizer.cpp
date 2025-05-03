#include <unordered_set>
#include <set>

#include <nlohmann/json.hpp>

#include <kiwi/SwTokenizer.h>
#include <kiwi/Kiwi.h>

#include "FrozenTrie.hpp"
#include "StrUtils.h"
#include "UnicodeCase.h"
#include "RaggedVector.hpp"

#include "sais/fm_index.hpp"

using namespace std;
using namespace kiwi;

namespace kiwi
{
	inline bool testRepetition(const char16_t* s, size_t l)
	{
		if (l < 5) return false;
		for (size_t i = 1; i <= l / 3; ++i)
		{
			bool all = true;
			for (size_t j = 1; j < l / i; ++j)
			{
				if (!equal(s, s + i, s + i * j))
				{
					all = false;
					break;
				}
			}
			if (all) return true;
		}
		return false;
	}

	inline bool isTagForPrefix(POSTag tag)
	{
		switch (tag)
		{
		case POSTag::nng:
		case POSTag::nnp:
		case POSTag::nnb:
		case POSTag::np:
		case POSTag::nr:
		case POSTag::mag:
		case POSTag::maj:
		case POSTag::mm:
		case POSTag::xr:
		case POSTag::xpn:
		case POSTag::ic:
		case POSTag::xsm:
		case POSTag::z_coda:
			return true;
		default:
			return false;
		}
	}

	inline bool isTagForPunct(POSTag tag)
	{
		switch (tag)
		{
		case POSTag::sf:
		case POSTag::sp:
		case POSTag::ss:
		case POSTag::sso:
		case POSTag::ssc:
		case POSTag::se:
		case POSTag::so:
		case POSTag::sw:
			return true;
		default:
			return false;
		}
	}

	inline POSTag toReprTag(POSTag tag, bool simpleTag = true)
	{
		if (!simpleTag) return tag;
		switch (tag)
		{
		case POSTag::nng:
		case POSTag::nnp:
		case POSTag::nnb:
		case POSTag::np:
		case POSTag::nr:
		case POSTag::xr:
			return POSTag::nng;
		case POSTag::vv:
		case POSTag::va:
		case POSTag::vx:
		case POSTag::vcp:
		case POSTag::vcn:
		case POSTag::xsv:
		case POSTag::xsa:
			return POSTag::vv;
		case POSTag::vvi:
		case POSTag::vai:
		case POSTag::vxi:
		case POSTag::xsai:
			return POSTag::vvi;
		case POSTag::mm:
		case POSTag::maj:
		case POSTag::mag:
			return POSTag::mag;
		case POSTag::sf:
		case POSTag::sp:
		case POSTag::ss:
		case POSTag::sso:
		case POSTag::ssc:
		case POSTag::se:
		case POSTag::so:
		case POSTag::sw:
		case POSTag::sb:
			return POSTag::sf;
		case POSTag::jks:
		case POSTag::jkc:
		case POSTag::jkg:
		case POSTag::jko:
		case POSTag::jkb:
		case POSTag::jkv:
		case POSTag::jkq:
		case POSTag::jx:
		case POSTag::jc:
			return POSTag::jks;
		case POSTag::ep:
		case POSTag::ef:
		case POSTag::ec:
		case POSTag::etn:
		case POSTag::etm:
			return POSTag::ep;
		}
		return tag;
	}

	const char* tagToReprStr(POSTag tag)
	{
		tag = toReprTag(tag);
		switch (tag)
		{
		case POSTag::nng:
			return "N";
		case POSTag::mag:
			return "M";
		case POSTag::vv:
			return "V";
		case POSTag::vvi:
			return "V-I";
		case POSTag::jks:
			return "J";
		case POSTag::ep:
			return "E";
		case POSTag::xsn:
			return "XSN";
		case POSTag::sf:
			return "S";
		}
		return nullptr;
	}

	inline POSTag reprStrToTag(const string& str)
	{
		if (str == "N") return POSTag::nng;
		if (str == "M") return POSTag::mag;
		if (str == "V") return POSTag::vv;
		if (str == "V-I") return POSTag::vvi;
		if (str == "J") return POSTag::jks;
		if (str == "E") return POSTag::ep;
		if (str == "XSN") return POSTag::xsn;
		if (str == "S") return POSTag::sf;
		return toPOSTag(utf8To16(str));
	}

	inline u16string toUTF16(u16string&& str)
	{
		return move(str);
	}

	inline u16string toUTF16(string&& str)
	{
		return utf8To16(str);
	}

	inline u16string toUTF16(const u16string& str)
	{
		return str;
	}

	inline u16string toUTF16(const string& str)
	{
		return utf8To16(str);
	}

	template<class Fn>
	void foreachU32Chr(U16StringView s, Fn&& fn)
	{
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (isHighSurrogate(s[i]))
			{
				fn(mergeSurrogate(s[i], s[i + 1]));
				++i;
			}
			else
			{
				fn(s[i]);
			}
		}
	}

	inline set<char32_t> getChrsPreserved(const map<char32_t, uint32_t>& chrCnts, double chrCoverage)
	{
		size_t totChrCnt = 0;
		for (auto& p : chrCnts)
		{
			totChrCnt += p.second;
		}
		totChrCnt *= chrCoverage;

		Vector<pair<char32_t, uint32_t>> sortedChr{ chrCnts.begin(), chrCnts.end() };
		sort(sortedChr.begin(), sortedChr.end(), [](const pair<char32_t, uint32_t>& a, const pair<char32_t, uint32_t>& b)
		{
			return a.second > b.second;
		});

		set<char32_t> ret;
		size_t accum = 0;
		for (auto& p : sortedChr)
		{
			ret.emplace(p.first);
			accum += p.second;
			if (accum >= totChrCnt) break;
		}
		return ret;
	}

	inline const Morpheme* findVerbalSuffix(const Morpheme* morph, const Vector<const Morpheme*>& suffices)
	{
		auto tag = clearIrregular(morph->tag);
		auto irreg = isIrregular(morph->tag);
		if (!(tag == POSTag::vv || tag == POSTag::va)) return nullptr;
		for (auto s : suffices)
		{
			auto stag = clearIrregular(s->tag);
			if (stag != (tag == POSTag::vv ? POSTag::xsv : POSTag::xsa) || irreg != isIrregular(s->tag)) continue;
			if (morph->kform->size() <= s->kform->size()) continue;
			if (equal(morph->kform->end() - s->kform->size(), morph->kform->end(), s->kform->begin()))
			{
				return s;
			}
		}
		return nullptr;
	}

	inline pair<const Morpheme*, const Morpheme*> findEomiSuffix(const Kiwi* kiwi, const Morpheme* morph, const Vector<const Morpheme*>& suffices)
	{
		auto tag = morph->tag;
		if (!(tag == POSTag::ef || tag == POSTag::ec)) return make_pair(nullptr, nullptr);
		for (auto s : suffices)
		{
			if (morph->kform->size() <= s->kform->size()) continue;
			if (equal(morph->kform->end() - s->kform->size(), morph->kform->end(), s->kform->begin()))
			{
				auto base = kiwi->findMorphemes(u16string{ morph->kform->begin(), morph->kform->end() - s->kform->size() }, tag);
				if (base.empty()) return make_pair(nullptr, nullptr);
				return make_pair(base[0], s);
			}
		}
		return make_pair(nullptr, nullptr);
	}

	static constexpr size_t numHangulOpenSyllable = 19 * 21;

	template<ArchType arch, bool generateOffset>
	bool tokenizeSubword(
		const SwTokenizerConfig& config,
		const utils::FrozenTrie<kchar_t, uint32_t>& trie,
		const Vector<SwToken>& vocabs,
		const Vector<uint32_t>& tokenFallbacks,
		const Vector<float>& tokenLProbs,
		const Vector<uint32_t>& hangulFallbackChrs,
		const Vector<uint32_t>& byteFallbackChrs,
		size_t unkTokenId,
		size_t glueTokenId,
		U16StringView str,
		bool spacePrefix,
		vector<uint32_t>& out,
		vector<pair<uint32_t, uint32_t>>* offset,
		uint32_t offsetBias
	)
	{
		Vector<pair<Vector<uint32_t>, float>> pathes, cands;
		Vector<Vector<uint32_t>> pathesEndPtr, candsEndPtr;
		std::string u8bytes;
		auto node = trie.root();
		if (spacePrefix) node = node->template nextOpt<arch>(trie, u' ');
		for (size_t i = 0; i < str.size(); ++i)
		{
			cands.clear();
			if (generateOffset) candsEndPtr.clear();

			auto nnode = node->template nextOpt<arch>(trie, str[i]);
			while (!nnode)
			{
				node = node->fail();
				if (!node) break;
				nnode = node->template nextOpt<arch>(trie, str[i]);
			}

			if (nnode)
			{
				size_t v = nnode->val(trie);

				if (trie.hasMatch(v))
				{
					auto tokenId = v - 1;
					auto& p = vocabs[tokenId];
					if (p.length == i + 1)
					{
						if (p.flags != SwTokenFlag::glue)
						{
							cands.emplace_back(Vector<uint32_t>{ (uint32_t)tokenId }, tokenLProbs[tokenId]);
							if (generateOffset) candsEndPtr.emplace_back(Vector<uint32_t>{ (uint32_t)(i + 1) });
						}
					}
					else
					{
						auto& l = pathes[i - p.length];
						if (!l.first.empty() && (
							!(p.flags == SwTokenFlag::none && !config.useGlueToken)
							|| p.flags == SwTokenFlag::glue
							|| p.flags == SwTokenFlag::chinese
							))
						{
							cands.emplace_back(l);
							if (generateOffset) candsEndPtr.emplace_back(pathesEndPtr[i - p.length]);

							if (p.flags == SwTokenFlag::none)
							{
								cands.back().first.emplace_back(glueTokenId);
								cands.back().second += tokenLProbs[glueTokenId];
								if (generateOffset) candsEndPtr.back().emplace_back(i - p.length + 1);
							}
							cands.back().first.emplace_back(tokenId);
							cands.back().second += tokenLProbs[tokenId];
							if (generateOffset) candsEndPtr.back().emplace_back(i + 1);
						}
					}
				}

				if (trie.hasMatch(v) || trie.hasSubmatch(v))
				{
					for (auto submatcher = nnode->fail(); submatcher; submatcher = submatcher->fail())
					{
						v = submatcher->val(trie);
						if (trie.hasMatch(v))
						{
							auto tokenId = v - 1;
							auto& p = vocabs[tokenId];
							if (p.length > i) continue;
							auto& l = pathes[i - p.length];
							if (!l.first.empty() && (
								!(p.flags == SwTokenFlag::none && !config.useGlueToken)
								|| p.flags == SwTokenFlag::glue
								|| p.flags == SwTokenFlag::chinese
								))
							{
								cands.emplace_back(l);
								if (generateOffset) candsEndPtr.emplace_back(pathesEndPtr[i - p.length]);
								if (p.flags == SwTokenFlag::none)
								{
									cands.back().first.emplace_back(glueTokenId);
									cands.back().second += tokenLProbs[glueTokenId];
									if (generateOffset) candsEndPtr.back().emplace_back(i - p.length + 1);
								}
								cands.back().first.emplace_back(tokenId);
								cands.back().second += tokenLProbs[tokenId];
								if (generateOffset) candsEndPtr.back().emplace_back(i + 1);
							}
						}
					}
				}
			}

			if (cands.empty())
			{
				if (config.fallbackHangul && isHangulSyllable(str[i]) && (str[i] - 0xAC00) % 28 > 0)
				{
					size_t openSyllable = (str[i] - 0xAC00) / 28;
					size_t coda = (str[i] - 0xAC00) % 28 - 1;
					coda += numHangulOpenSyllable * 2;
					if ((hangulFallbackChrs[openSyllable] != -1 || hangulFallbackChrs[openSyllable + numHangulOpenSyllable] != -1)
						&& hangulFallbackChrs[coda] != -1)
					{
						bool sp = (i == 0 && spacePrefix);
						auto v = pathes.empty() ? Vector<uint32_t>{} : pathes.back().first;
						auto s = pathes.empty() ? 0 : pathes.back().second;
						size_t inserted = 2;
						if (sp && hangulFallbackChrs[openSyllable] != -1)
						{
							v.emplace_back(hangulFallbackChrs[openSyllable]);
						}
						else if (!sp && hangulFallbackChrs[openSyllable + numHangulOpenSyllable] != -1)
						{
							v.emplace_back(hangulFallbackChrs[openSyllable + numHangulOpenSyllable]);
						}
						else if (config.useGlueToken && !sp && hangulFallbackChrs[openSyllable] != -1)
						{
							v.emplace_back(glueTokenId);
							v.emplace_back(hangulFallbackChrs[openSyllable]);
							inserted++;
						}
						else
						{
							inserted = 0;
						}

						if (inserted)
						{
							v.emplace_back(hangulFallbackChrs[coda]);
							s -= 100;
							pathes.emplace_back(move(v), s);
							if (generateOffset)
							{
								if (pathesEndPtr.empty()) pathesEndPtr.emplace_back();
								else pathesEndPtr.push_back(pathesEndPtr.back());
								pathesEndPtr.back().resize(pathesEndPtr.back().size() + inserted, i + 1);
							}
							goto endOfLoop;
						}
					}
				}

				if (config.fallbackByte)
				{
					bool sp = (i == 0 && spacePrefix);
					char32_t c;
					if (isHighSurrogate(str[i]) && i + 1 < str.size())
					{
						c = mergeSurrogate(str[i], str[i + 1]);
						i++;
					}
					else
					{
						c = str[i];
					}

					auto v = pathes.empty() ? Vector<uint32_t>{} : pathes.back().first;
					auto s = pathes.empty() ? 0 : pathes.back().second;

					if (c >= 0x10000) // surrogate
					{
						pathes.emplace_back();
						if (generateOffset)
						{
							if (pathesEndPtr.empty()) pathesEndPtr.emplace_back();
							else pathesEndPtr.push_back(pathesEndPtr.back());
						}
					}

					u8bytes.clear();
					if (sp) u8bytes.push_back(' ');
					utf8FromCode(u8bytes, c);
					for (uint8_t b : u8bytes)
					{
						v.emplace_back(byteFallbackChrs[b]);
					}
					s -= 100;
					pathes.emplace_back(move(v), s);
					if (generateOffset)
					{
						if (pathesEndPtr.empty()) pathesEndPtr.emplace_back();
						else pathesEndPtr.push_back(pathesEndPtr.back());
						pathesEndPtr.back().resize(pathesEndPtr.back().size() + u8bytes.size(), i + 1);
					}
					goto endOfLoop;
				}

				if (config.wholeWordUnk)
				{
					pathes.emplace_back();
					if (generateOffset) pathesEndPtr.emplace_back();
				}
				else if (unkTokenId != (size_t)-1)
				{
					if (pathes.empty())
					{
						pathes.emplace_back(Vector<uint32_t>{ (uint32_t)unkTokenId }, -100);
						if (generateOffset) pathesEndPtr.emplace_back(Vector<uint32_t>{ (uint32_t)(i + 1) });
					}
					else
					{
						auto v = pathes.back().first;
						auto s = pathes.back().second;
						if (generateOffset) pathesEndPtr.push_back(pathesEndPtr.back());
						if (v.back() != unkTokenId)
						{
							v.emplace_back(unkTokenId);
							s -= 100;
							if (generateOffset) pathesEndPtr.back().emplace_back(i + 1);
						}
						else
						{
							if (generateOffset) pathesEndPtr.back().back() = i + 1;
						}
						pathes.emplace_back(move(v), s);
					}
				}
				else
				{
					if (pathes.empty())
					{
						pathes.emplace_back(Vector<uint32_t>{ }, -100);
						if (generateOffset) pathesEndPtr.emplace_back(Vector<uint32_t>{ });
					}
					else
					{
						auto v = pathes.back();
						pathes.emplace_back(move(v));
						if (generateOffset) pathesEndPtr.push_back(pathesEndPtr.back());
					}
				}
			}
			else
			{
				size_t bestPath = 0;
				for (size_t i = 1; i < cands.size(); ++i)
				{
					if (cands[i].second > cands[bestPath].second)
					{
						bestPath = i;
					}
				}
				pathes.emplace_back(move(cands[bestPath]));
				if (generateOffset) pathesEndPtr.emplace_back(move(candsEndPtr[bestPath]));
			}
		endOfLoop:
			node = nnode ? nnode : trie.root();
		}

		if (pathes.back().first.empty())
		{
			if (unkTokenId != (size_t)-1) out.emplace_back(unkTokenId);
			if (generateOffset) offset->emplace_back(offsetBias, offsetBias + str.size());
			return false;
		}
		else
		{
			out.insert(out.end(), pathes.back().first.begin(), pathes.back().first.end());
			if (generateOffset)
			{
				auto& p = pathesEndPtr.back();
				for (size_t i = 0; i < p.size(); ++i)
				{
					bool isGlueToken = pathes.back().first[i] == glueTokenId;
					offset->emplace_back(offsetBias + (i ? p[i - 1] : 0) - (!isGlueToken && i && p[i - 1] == p[i] ? 1 : 0), offsetBias + p[i]);
				}
			}
			return true;
		}
	}

	using FnTokenizeSubword = decltype(&tokenizeSubword<ArchType::none, false>);

	template<bool generateOffset>
	struct TokenizeSubwordGetter
	{
		template<ptrdiff_t i>
		struct Wrapper
		{
			static constexpr FnTokenizeSubword value = &tokenizeSubword<static_cast<ArchType>(i), generateOffset>;
		};
	};

	inline void utf8To16IgnoringErrors(std::string_view str, std::u16string& ret)
	{
		ret.clear();
		for (auto it = str.begin(); it != str.end(); ++it)
		{
			uint32_t code = 0;
			auto safeIt = it;
			bool success = true;
			do
			{
				uint32_t byte = (uint8_t)*it;
				if ((byte & 0xF8) == 0xF0)
				{
					code = (uint32_t)((byte & 0x07) << 18);
					if (++it == str.end() || ((byte = *it) & 0xC0) != 0x80)
					{
						success = false;
						break;
					}

					code |= (uint32_t)((byte & 0x3F) << 12);
					if (++it == str.end() || ((byte = *it) & 0xC0) != 0x80)
					{
						success = false;
						break;
					}
					code |= (uint32_t)((byte & 0x3F) << 6);
					if (++it == str.end() || ((byte = *it) & 0xC0) != 0x80)
					{
						success = false;
						break;
					}
					code |= (byte & 0x3F);
				}
				else if ((byte & 0xF0) == 0xE0)
				{
					code = (uint32_t)((byte & 0x0F) << 12);
					if (++it == str.end() || ((byte = *it) & 0xC0) != 0x80)
					{
						success = false;
						break;
					}
					code |= (uint32_t)((byte & 0x3F) << 6);
					if (++it == str.end() || ((byte = *it) & 0xC0) != 0x80)
					{
						success = false;
						break;
					}
					code |= (byte & 0x3F);
				}
				else if ((byte & 0xE0) == 0xC0)
				{
					code = (uint32_t)((byte & 0x1F) << 6);
					if (++it == str.end() || ((byte = *it) & 0xC0) != 0x80)
					{
						success = false;
						break;
					}
					code |= (byte & 0x3F);
				}
				else if ((byte & 0x80) == 0x00)
				{
					code = byte;
				}
				else
				{
					success = false;
					break;
				}
			} while (0);

			if (success)
			{
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
			}
			else
			{
				for (auto j = safeIt; j <= it && j != str.end(); ++j)
				{
					uint8_t byte = *j;
					ret += u"<0x";
					ret.push_back(u"0123456789ABCDEF"[byte >> 4]);
					ret.push_back(u"0123456789ABCDEF"[byte & 0xF]);
					ret += u">";
				}
				if (it == str.end()) return;
			}
		}
	}

	inline std::u16string utf8To16IgnoringErrors(std::string_view str)
	{
		std::u16string ret;
		utf8To16IgnoringErrors(str, ret);
		return ret;
	}
}

SwTokenizerBuilder::SwTokenizerBuilder(const Kiwi& _kiwi, const SwTokenizerConfig& _config)
	: kiwi{ &_kiwi }, config{ _config }
{

}
SwTokenizerBuilder::SwTokenizerBuilder(const SwTokenizerBuilder&) = default;
SwTokenizerBuilder::SwTokenizerBuilder(SwTokenizerBuilder&&) = default;
SwTokenizerBuilder::~SwTokenizerBuilder() = default;
SwTokenizerBuilder& SwTokenizerBuilder::operator=(const SwTokenizerBuilder&) = default;
SwTokenizerBuilder& SwTokenizerBuilder::operator=(SwTokenizerBuilder&&) = default;

void SwTokenizerBuilder::addToken(const char* form, POSTag tag, SwTokenFlag flags, float lprob)
{
	tokens.emplace_back(form, tag, flags, lprob);
}

void SwTokenizerBuilder::addToken(const string& form, POSTag tag, SwTokenFlag flags, float lprob)
{
	tokens.emplace_back(form, tag, flags, lprob);
}

SwTokenizer SwTokenizerBuilder::build() const
{
	auto bestArch = getSelectedArch(ArchType::default_);
	SwTokenizer ret{ bestArch };
	auto& vocab = ret.vocab;
	ret.kiwi = kiwi;
	ret.config = config;
	vector<const Morpheme*> matchedMorphs;

	utils::ContinuousTrie<utils::TrieNode<kchar_t, uint32_t>> trie{ 1 };
	size_t offset = 0;
	ret.swToMorph.resize(tokens.size(), -1);
	if (config.fallbackHangul)
	{
		ret.hangulFallbackChrs.resize(numHangulOpenSyllable * 2 + 27, -1);
	}
	if (config.fallbackByte || config.newlineToken)
	{
		ret.byteFallbackChrs.resize(256);
	}

	for (auto& t : tokens)
	{
		size_t tokenId = &t - tokens.data();
		auto u16form = utf8To16(t.form);
		vocab.vocabStrPool += u16form;
		vocab.vocabStrPool.push_back('\0');
		vocab.vocabs.emplace_back((const char16_t*)offset, u16form.size(), t.pos, t.flags);
		ret.tokenLProbs.emplace_back(t.lprob);
		offset += u16form.size() + 1;

		if (t.pos == POSTag::unknown)
		{
			if (t.flags == SwTokenFlag::none)
			{
				bool isPunct = config.splitPunct, isChinese = config.splitChinese;
				foreachU32Chr(u16form, [&](char32_t c)
				{
					if (!isTagForPunct(identifySpecialChr(c))) isPunct = false;
					if (!isChineseChr(c)) isChinese = false;
				});
				if (isPunct) vocab.vocabs.back().flags = SwTokenFlag::punct;
				if (isChinese) vocab.vocabs.back().flags = SwTokenFlag::chinese;
			}

			if (config.fallbackHangul && u16form.size() == 1)
			{
				size_t id = -1;
				if (isHangulSyllable(u16form[0]) && (u16form[0] - 0xAC00) % 28 == 0)
				{
					id = (u16form[0] - 0xAC00) / 28;
					if (t.flags == SwTokenFlag::subword) id += numHangulOpenSyllable;
				}
				else if (isHangulCoda(u16form[0]) && t.flags == SwTokenFlag::subword)
				{
					id = numHangulOpenSyllable * 2 + (u16form[0] - 0x11A8);
				}

				if (id != -1)
				{
					ret.hangulFallbackChrs[id] = tokenId;
				}
			}

			if (t.flags == SwTokenFlag::byte)
			{
				if (config.fallbackByte)
				{
					int id = stoi(t.form);
					if (id < 0 || id >= 256) throw SwTokenizerException{ "wrong byte token: " + t.form };
					ret.byteFallbackChrs[id] = tokenId;
					vocab.vocabs.back().byte = id;
				}
				continue;
			}

			if (t.flags == SwTokenFlag::glue)
			{
				ret.specialTokenIds[SwTokenizerConfig::glue] = tokenId;
				continue;
			}
			else if (t.flags == SwTokenFlag::none)
			{
				u16form.insert(u16form.begin(), u' ');
			}

			if (trie.build(u16form.begin(), u16form.end(), tokenId + 1)->val != tokenId + 1)
			{
				throw SwTokenizerException{ "duplicated token: " + t.form };
			}
		}
		else
		{
			matchedMorphs.clear();
			kiwi->findMorphemes(matchedMorphs, u16form, config.simpleTag ? POSTag::unknown : t.pos);
			if (matchedMorphs.empty()) continue;
			auto rtag = toReprTag(t.pos);
			const Morpheme* firstMorph = nullptr;
			for (auto m : matchedMorphs)
			{
				if (config.simpleTag && toReprTag(m->tag) != rtag) continue;
				if (joinHangul(m->getForm()) != u16form) continue;
				auto morphId = kiwi->morphToId(m);
				if (!firstMorph) firstMorph = m;
				if (ret.morphToSw.size() <= morphId)
				{
					ret.morphToSw.resize(morphId + 1, -1);
				}

				if (ret.morphToSw[morphId] != -1)
				{
					throw SwTokenizerException{ "duplicated token: " + t.form + "/" + tagToString(t.pos) };
				}
				ret.morphToSw[morphId] = tokenId;
			}
			if (firstMorph) ret.swToMorph[tokenId] = kiwi->morphToId(firstMorph);
		}
	}

	for (auto& t : vocab.vocabs)
	{
		t.form = vocab.vocabStrPool.data() + (size_t)t.form;
	}
	
	ret.tokenFallbacks.resize(tokens.size(), -1);

	for (size_t i = 0; i <= SwTokenizerConfig::eos; ++i)
	{
		if (config.specialTokens[i].empty())
		{
			ret.specialTokenIds[i] = -1;
		}
		else
		{
			if (any_of(config.specialTokens[i].begin(), config.specialTokens[i].end(), isSpace))
			{
				throw SwTokenizerException{ "Special token shouldn't contain any whitespace characters, but: " + config.specialTokens[i]};
			}
			auto u16str = utf8To16(config.specialTokens[i]);
			auto node = trie.find(u16str.begin(), u16str.end());
			if (!node) throw SwTokenizerException{ "Token " + config.specialTokens[i] + " is not found in the vocabulary."};
			ret.specialTokenIds[i] = node->val - 1;
		}
	}
	
	for (auto& t : tokens)
	{
		size_t tokenId = &t - tokens.data();
		if (t.pos != POSTag::unknown) continue;
		if (t.flags != SwTokenFlag::none) continue;
		auto& v = vocab.vocabs[tokenId];
		auto node = trie.build(v.form, v.form + v.length, tokenId + 1);
		if (node->val != tokenId + 1)
		{
			ret.tokenFallbacks[node->val - 1] = tokenId;
		}
	}

	ret.trie = utils::freezeTrie(move(trie), bestArch);

	Vector<pair<KString, uint32_t>> eomiSuffices;
	Vector<const Morpheme*> verbalSuffices;
	for (size_t i = 0; i < kiwi->getMorphemeSize(); ++i)
	{
		auto morph = kiwi->idToMorph(i);
		if (!morph->kform
			|| morph->kform->empty()) continue;
		if (morph->tag == POSTag::jx 
			&& kiwi->idToMorph(morph->lmMorphemeId)->getForm() == u"이요"
			&& morph->getForm()[0] != u'이')
		{
			if (i < ret.morphToSw.size() && ret.morphToSw[i] != -1) eomiSuffices.emplace_back(morph->getForm(), ret.morphToSw[i]);
		}

		if (isVerbClass(morph->tag) && !morph->complex)
		{
			if (i < ret.morphToSw.size() && ret.morphToSw[i] != -1) verbalSuffices.emplace_back(morph);
		}
	}
	sort(verbalSuffices.begin(), verbalSuffices.end(), [&](const Morpheme* a, const Morpheme* b)
	{
		return a->kform->size() > b->kform->size();
	});

	vector<const Morpheme*> foundMorphs;
	vector<uint32_t> buf;
	for (size_t i = 0; i < kiwi->getMorphemeSize(); ++i)
	{
		auto morph = kiwi->idToMorph(i);
		if (!morph->kform || morph->kform->size() <= 1) continue;
		if (i < ret.morphToSw.size() && ret.morphToSw[i] != -1) continue;
		if (config.splitEomi && isEClass(morph->tag))
		{
			auto& f = morph->getForm();
			for (auto& p : eomiSuffices)
			{
				if (f.size() <= p.first.size()) continue;
				if (f.substr(f.size() - p.first.size()) != p.first) continue;
				foundMorphs.clear();
				auto prefix = joinHangul(f.substr(0, f.size() - p.first.size()));
				kiwi->findMorphemes(foundMorphs, prefix);

				for (auto m : foundMorphs)
				{
					if (toReprTag(morph->tag, config.simpleTag) == toReprTag(m->tag, config.simpleTag))
					{
						auto j = kiwi->morphToId(m);
						if (j < ret.morphToSw.size() && ret.morphToSw[j] != -1)
						{
							ret.splitCands.emplace(i, SwTokenizer::SplittedWord{ 
								Vector<uint32_t>{ ret.morphToSw[j], p.second },
								Vector<uint32_t>{ (uint32_t)(f.size() - p.first.size()), (uint32_t)f.size() }
							});
							goto endSplitEomi;
						}
					}
				}
			}
		endSplitEomi:;
		}
		else if (config.splitVerb && isVerbClass(morph->tag) && !morph->complex)
		{
			auto suffix = findVerbalSuffix(morph, verbalSuffices);
			if (!suffix) continue;
			auto prefix = morph->kform->substr(0, morph->kform->size() - suffix->kform->size());
			buf.clear();
			vector<pair<uint32_t, uint32_t>> offsets;
			ret.tokenizeSubword(joinHangul(prefix), true, buf, &offsets);
			Vector<uint32_t> tokenized;
			Vector<uint32_t> boundaries;
			tokenized.insert(tokenized.end(), buf.begin(), buf.end());
			tokenized.emplace_back(ret.morphToSw[kiwi->morphToId(suffix)]);
			for (auto& p : offsets) boundaries.emplace_back(p.second);
			boundaries.emplace_back(morph->kform->size());
			ret.splitCands.emplace(i, SwTokenizer::SplittedWord{
				move(tokenized),
				move(boundaries)
			});
		}
	}

	return ret;
}

SwTokenizer::SwTokenizer(ArchType archType)
{
	static tp::Table<FnTokenizeSubword, AvailableArch> table{ TokenizeSubwordGetter<false>{} };
	static tp::Table<FnTokenizeSubword, AvailableArch> tableWithOffset{ TokenizeSubwordGetter<true>{} };
	if (archType != ArchType::default_)
	{
		dfTokenizeSubword = reinterpret_cast<void*>(table[static_cast<ptrdiff_t>(archType)]);
		dfTokenizeSubwordWithOffset = reinterpret_cast<void*>(tableWithOffset[static_cast<ptrdiff_t>(archType)]);

		if (!dfTokenizeSubword || !dfTokenizeSubwordWithOffset) throw SwTokenizerException{ string{ "Unsupported archType: " } + archToStr(archType) };
	}
}

SwTokenizer::SwTokenizer(const SwTokenizer& o) = default;
SwTokenizer::SwTokenizer(SwTokenizer&&) = default;
SwTokenizer::~SwTokenizer() = default;
SwTokenizer& SwTokenizer::operator=(const SwTokenizer&) = default;
SwTokenizer& SwTokenizer::operator=(SwTokenizer&&) = default;

SwTokenizer::Vocab::Vocab() = default;
SwTokenizer::Vocab::Vocab(Vocab&&) = default;
SwTokenizer::Vocab& SwTokenizer::Vocab::operator=(Vocab&&) = default;
SwTokenizer::Vocab::~Vocab() = default;

SwTokenizer::Vocab::Vocab(const Vocab& o)
	: vocabs{ o.vocabs }, vocabStrPool{ o.vocabStrPool }
{
	size_t offset = 0;
	for (auto& v : vocabs)
	{
		v.form = vocabStrPool.data() + offset;
		offset += v.length + 1;
	}
}

SwTokenizer::Vocab& SwTokenizer::Vocab::operator=(const Vocab& o)
{
	vocabs = o.vocabs;
	vocabStrPool = o.vocabStrPool;
	size_t offset = 0;
	for (auto& v : vocabs)
	{
		v.form = vocabStrPool.data() + offset;
		offset += v.length + 1;
	}
	return *this;
}

vector<uint32_t> SwTokenizer::encode(const string& str, vector<pair<uint32_t, uint32_t>>* offset, bool offsetInChrLevel) const
{
	vector<uint32_t> ret;
	encode(ret, str, offset, offsetInChrLevel);
	return ret;
}


vector<uint32_t> SwTokenizer::encode(const TokenInfo* tokens, size_t size, vector<pair<uint32_t, uint32_t>>* offset) const
{
	vector<uint32_t> ret;
	encode(ret, tokens, tokens + size, offset);
	return ret;
}

vector<uint32_t> SwTokenizer::encode(const vector<pair<string, POSTag>>& morphs, vector<pair<uint32_t, uint32_t>>* offset) const
{
	vector<uint32_t> ret;
	encode(ret, morphs, offset);
	return ret;
}

vector<uint32_t> SwTokenizer::encode(const vector<pair<u16string, POSTag>>& morphs, vector<pair<uint32_t, uint32_t>>* offset) const
{
	vector<uint32_t> ret;
	encode(ret, morphs, offset);
	return ret;
}

vector<uint32_t> SwTokenizer::encode(const vector<tuple<u16string, POSTag, bool>>& morphs, vector<pair<uint32_t, uint32_t>>* offset) const
{
	vector<uint32_t> ret;
	encode(ret, morphs, offset);
	return ret;
}

bool SwTokenizer::tokenizeSubword(U16StringView str,
	bool spacePrefix,
	vector<uint32_t>& out,
	vector<pair<uint32_t, uint32_t>>* offset,
	uint32_t offsetBias
) const
{
	return reinterpret_cast<FnTokenizeSubword>(offset ? dfTokenizeSubwordWithOffset : dfTokenizeSubword)(
		config, trie,
		vocab.vocabs, tokenFallbacks, tokenLProbs, hangulFallbackChrs, byteFallbackChrs,
		specialTokenIds[SwTokenizerConfig::unk], specialTokenIds[SwTokenizerConfig::glue],
		str,
		spacePrefix,
		out,
		offset,
		offsetBias
	);
}

template<class TokenIt>
void SwTokenizer::encode(vector<uint32_t>& ret, TokenIt first, TokenIt last, vector<pair<uint32_t, uint32_t>>* offset) const
{
	Vector<uint8_t> codaBias;
	auto* baseMorph = kiwi->idToMorph(0);
	size_t startPosition = -1, lastPosition = -1;
	size_t offsetStart = offset ? offset->size() : 0;
	u16string tokenBuf;
	bool spacePrefix = true;
	const auto pushSubwords = [&]()
	{
		if (startPosition == -1) return;
		auto success = tokenizeSubword(tokenBuf, spacePrefix, ret, offset, startPosition);
		tokenBuf.clear();
	};

	size_t lastLineNumber = 0;
	for (; first != last; ++first)
	{
		decltype(*first) t = *first;
		size_t id = t.morph ? (t.morph - baseMorph) : -1;

		if (config.newlineToken && t.lineNumber > lastLineNumber)
		{
			pushSubwords();
			
			uint32_t prevPosition = 0;
			if (offset && !offset->empty())
			{
				prevPosition = offset->back().second;
			}

			for (size_t i = lastLineNumber; i < t.lineNumber; ++i)
			{
				ret.emplace_back(byteFallbackChrs['\n']);
				if (offset) offset->emplace_back(prevPosition, t.position);
			}
			startPosition = -1;
			lastLineNumber = t.lineNumber;
		}

		// Morpheme
		if (id < morphToSw.size() && morphToSw[id] != -1)
		{
			pushSubwords();
			ret.emplace_back(morphToSw[id]);
			if (offset) offset->emplace_back(t.position, t.position + t.length);
			startPosition = -1;
			lastPosition = t.position + t.length;
			continue;
		}

		// splitted morphemes
		auto it = splitCands.find(id);
		if (it != splitCands.end())
		{
			pushSubwords();
			ret.insert(ret.end(), it->second.tokenIds.begin(), it->second.tokenIds.end());
			if (offset)
			{
				for (size_t i = 0; i < it->second.boundaries.size(); ++i)
				{
					offset->emplace_back(t.position + (i ? it->second.boundaries[i - 1] : 0), t.position + it->second.boundaries[i]);
				}
			}
			startPosition = -1;
			lastPosition = t.position + t.length;
			continue;
		}

		if (config.splitVerb && isVerbClass(t.tag))
		{
			if (t.morph && t.morph->complex)
			{
				pushSubwords();
				if (offset)
				{
					codaBias.clear();
					uint8_t a = 0;
					for (size_t i = 0; i < t.morph->kform->size(); ++i)
					{
						codaBias.emplace_back(a);
						a += isHangulCoda((*t.morph->kform)[i]) ? 1 : 0;
					}
					codaBias.emplace_back(a);
				}

				size_t i = 0;
				for (auto m : t.morph->chunks)
				{
					id = m - baseMorph;
					if (id < morphToSw.size() && morphToSw[id] != -1)
					{
						ret.emplace_back(morphToSw[id]);
						if (offset)
						{
							auto p = t.morph->chunks.getSecond(i);
							uint32_t start = p.first - codaBias[p.first];
							uint32_t end = p.first + p.second - codaBias[p.first + p.second];
							offset->emplace_back(t.position + start, t.position + end);
						}
					}
					else
					{
						startPosition = t.position;
						tokenBuf = joinHangul(m->getForm());
						spacePrefix = true;
						pushSubwords();
					}
					++i;
				}
				startPosition = -1;
				lastPosition = t.position + t.length;
				continue;
			}
			else
			{
				// to do
			}
		}

		if (config.splitEomi && isEClass(t.tag))
		{
			if (t.morph && t.morph->complex)
			{
				pushSubwords();
				size_t i = 0;
				for (auto m : t.morph->chunks)
				{
					id = m - baseMorph;
					if (id < morphToSw.size() && morphToSw[id] != -1)
					{
						ret.emplace_back(morphToSw[id]);
						if (offset)
						{
							auto p = t.morph->chunks.getSecond(i);
							offset->emplace_back(t.position + p.first, t.position + p.first + p.second);
						}
					}
					else
					{
						// to do
					}
					++i;
				}
				startPosition = -1;
				lastPosition = t.position + t.length;
				continue;
			}
		}

		if (t.str.empty())
		{
			throw SwTokenizerException{ "`encode` accepts empty str token." };
		}

		if (startPosition != -1 && lastPosition == t.position)
		{
			lastPosition = t.position + t.length;
			if (config.doLowercase) toLower16(t.str.begin(), t.str.end(), back_inserter(tokenBuf));
			else tokenBuf += t.str;
		}
		else
		{
			pushSubwords();
			startPosition = t.position;
			spacePrefix = lastPosition < startPosition || lastPosition == -1;
			if (config.doLowercase) toLower16(t.str.begin(), t.str.end(), back_inserter(tokenBuf));
			else tokenBuf += t.str;
			lastPosition = t.position + t.length;
		}
	}
	pushSubwords();
}

void SwTokenizer::encode(vector<uint32_t>& ret, const string& str, vector<pair<uint32_t, uint32_t>>* offset, bool offsetInChrLevel) const
{
	Vector<size_t> positions;
	size_t offsetStart = offset ? offset->size() : 0;
	u16string u16;
	if (offsetInChrLevel)
	{
		u16 = utf8To16ChrPoisition(str, positions);
		positions.emplace_back(positions.empty() ? 0 : (positions.back() + 1));
	}
	else
	{
		u16 = utf8To16(str, positions);
		positions.emplace_back(str.size());
	}
	auto tokens = kiwi->analyze(move(u16), Match::normalizeCoda | Match::zCoda).first;
	
	encode(ret, tokens.begin(), tokens.end(), offset);

	if (offset)
	{
		for (size_t i = offsetStart; i < offset->size(); ++i)
		{
			(*offset)[i].first = positions[(*offset)[i].first];
			(*offset)[i].second = positions[(*offset)[i].second];
		}
	}
}

void SwTokenizer::encode(vector<uint32_t>& ret, const TokenInfo* tokens, size_t size, vector<pair<uint32_t, uint32_t>>* offset) const
{
	encode(ret, tokens, tokens + size, offset);
}

namespace kiwi
{
	template<class Ty>
	class TokenInfoConvertingIterator
	{
		const Kiwi* kw = nullptr;
		const Ty* ptr = nullptr;
		Vector<uint32_t>* offsetOut = nullptr;
		size_t accumPosition = 0;

		mutable size_t tokenIdx = -1;
		mutable size_t offset = 0;
		mutable Vector<TokenInfo> tokens;

		template<class Str>
		void store(const pair<Str, POSTag>& p) const
		{
			return store(make_tuple(p.first, p.second, false));
		}

		template<class Str>
		void store(const tuple<Str, POSTag, bool>& p) const
		{
			tokens.clear();
			tokenIdx = 0;
			u16string str = toUTF16(get<0>(p));
			POSTag tag = get<1>(p);
			size_t position = accumPosition + (get<2>(p) ? 1 : 0);
			size_t length = str.size();
			if (offsetOut) offsetOut->emplace_back(position + length);
			offset = str.size() + (get<2>(p) ? 1 : 0);
			auto morphs = kw->findMorphemes(str, tag);
			if (morphs.empty())
			{
				if (isVerbClass(tag))
				{
					str.push_back(u'다');
					auto results = kw->analyze(str, Match::none).first;
					tokens.insert(tokens.end(), results.begin(), results.end() - (results.size() > 1 ? 1 : 0));
					for (auto& t : tokens) t.position += position;
				}
				else
				{
					tokens.emplace_back(move(str), tag, length, position);
				}
			}
			else
			{
				tokens.emplace_back(move(str), tag, length, position);
				tokens.back().morph = morphs[0];
			}
		}


	public:

		TokenInfoConvertingIterator(const Kiwi* _kw, const Ty* _ptr = nullptr, Vector<uint32_t>* _offsetOut = nullptr)
			: kw{ _kw }, ptr{ _ptr }, offsetOut{ _offsetOut }
		{
		}

		const TokenInfo& operator*() const
		{
			if (tokenIdx == -1) store(*ptr);
			return tokens[tokenIdx];
		}

		TokenInfoConvertingIterator& operator++()
		{
			if (++tokenIdx >= tokens.size())
			{
				tokenIdx = -1;
				accumPosition += offset;
				++ptr;
			}
			return *this;
		}

		bool operator==(const TokenInfoConvertingIterator& o) const
		{
			return ptr == o.ptr;
		}

		bool operator!=(const TokenInfoConvertingIterator& o) const
		{
			return !operator==(o);
		}
	};

	template<class Ty>
	TokenInfoConvertingIterator<Ty> makeTokenInfoConvertingIterator(const Kiwi* kw, const Ty* ptr, Vector<uint32_t>* offsetOut)
	{
		return TokenInfoConvertingIterator<Ty>{ kw, ptr, offsetOut };
	}
}

void SwTokenizer::encode(vector<uint32_t>& out, const vector<pair<string, POSTag>>& morphs, vector<pair<uint32_t, uint32_t>>* offset) const
{
	Vector<uint32_t> chrOffset;
	if (offset) chrOffset.emplace_back(0);
	size_t offsetStart = offset ? offset->size() : 0;
	encode(out, 
		makeTokenInfoConvertingIterator(kiwi, morphs.data(), offset ? &chrOffset : nullptr), 
		makeTokenInfoConvertingIterator(kiwi, morphs.data() + morphs.size(), offset ? &chrOffset : nullptr),
		offset
	);
	if (offset)
	{
		for (size_t i = offsetStart; i < offset->size(); ++i)
		{
			(*offset)[i].first = std::upper_bound(chrOffset.begin(), chrOffset.end(), (*offset)[i].first) - chrOffset.begin() - 1;
			(*offset)[i].second = std::lower_bound(chrOffset.begin(), chrOffset.end(), (*offset)[i].second) - chrOffset.begin();
		}
	}
}

void SwTokenizer::encode(vector<uint32_t>& out, const vector<pair<u16string, POSTag>>& morphs, vector<pair<uint32_t, uint32_t>>* offset) const
{
	Vector<uint32_t> chrOffset;
	if (offset) chrOffset.emplace_back(0);
	size_t offsetStart = offset ? offset->size() : 0;
	encode(out,
		makeTokenInfoConvertingIterator(kiwi, morphs.data(), offset ? &chrOffset : nullptr),
		makeTokenInfoConvertingIterator(kiwi, morphs.data() + morphs.size(), offset ? &chrOffset : nullptr),
		offset
	);
	if (offset)
	{
		for (size_t i = offsetStart; i < offset->size(); ++i)
		{
			(*offset)[i].first = std::upper_bound(chrOffset.begin(), chrOffset.end(), (*offset)[i].first) - chrOffset.begin() - 1;
			(*offset)[i].second = std::lower_bound(chrOffset.begin(), chrOffset.end(), (*offset)[i].second) - chrOffset.begin();
		}
	}
}

void SwTokenizer::encode(vector<uint32_t>& out, const vector<tuple<u16string, POSTag, bool>>& morphs, vector<pair<uint32_t, uint32_t>>* offset) const
{
	Vector<uint32_t> chrOffset;
	if (offset) chrOffset.emplace_back(0);
	size_t offsetStart = offset ? offset->size() : 0;
	encode(out,
		makeTokenInfoConvertingIterator(kiwi, morphs.data(), offset ? &chrOffset : nullptr),
		makeTokenInfoConvertingIterator(kiwi, morphs.data() + morphs.size(), offset ? &chrOffset : nullptr),
		offset
	);
	if (offset)
	{
		for (size_t i = offsetStart; i < offset->size(); ++i)
		{
			(*offset)[i].first = std::upper_bound(chrOffset.begin(), chrOffset.end(), (*offset)[i].first) - chrOffset.begin() - 1;
			(*offset)[i].second = std::lower_bound(chrOffset.begin(), chrOffset.end(), (*offset)[i].second) - chrOffset.begin();
		}
	}
}

template<class It>
string SwTokenizer::decode(It first, It last, bool ignoreErrors) const
{
	auto joiner = kiwi->newJoiner(false);
	string u8bytes;
	for (; first != last; ++first)
	{
		auto id = *first;
		auto& v = vocab.vocabs[id];
		// byte
		if (v.flags == SwTokenFlag::byte)
		{
			u8bytes.push_back(v.byte);
			continue;
		}
		else
		{
			if (!u8bytes.empty())
			{
				joiner.add(ignoreErrors? utf8To16IgnoringErrors(u8bytes) : utf8To16(u8bytes), POSTag::unknown);
				u8bytes.clear();
			}
		}

		// to do: detect the best tag from ambiguous tags in simpleTag mode
		// morpheme
		if (id < swToMorph.size() && swToMorph[id] != -1)
		{
			joiner.add(swToMorph[id]);
			continue;
		}
		
		// subword
		bool insertSpace = v.flags == SwTokenFlag::none || v.flags == SwTokenFlag::special;
		joiner.add(U16StringView{ v.form, v.length }, POSTag::unknown, insertSpace ? cmb::Space::insert_space : cmb::Space::none);
	}

	if (!u8bytes.empty())
	{
		joiner.add(ignoreErrors ? utf8To16IgnoringErrors(u8bytes) : utf8To16(u8bytes), POSTag::unknown);
		u8bytes.clear();
	}
	return joiner.getU8();
	
}

string SwTokenizer::decode(const vector<uint32_t>& ids, bool ignoreErrors) const
{
	return decode(ids.begin(), ids.end(), ignoreErrors);
}

string SwTokenizer::decode(const uint32_t* ids, size_t length, bool ignoreErrors) const
{
	return decode(ids, ids + length, ignoreErrors);
}

future<vector<uint32_t>> SwTokenizer::asyncEncode(const string& str) const
{
	auto* pool = kiwi->getThreadPool();
	if (!pool) throw SwTokenizerException{ "async mode is unavailable in `numThreads == 0`" };
	return pool->enqueue([&](size_t, const string& str)
	{
		return encode(str);
	}, str);
}

future<pair<vector<uint32_t>, vector<pair<uint32_t, uint32_t>>>> SwTokenizer::asyncEncodeOffset(const string& str, bool offsetInChrLevel) const
{
	auto* pool = kiwi->getThreadPool();
	if (!pool) throw SwTokenizerException{ "async mode is unavailable in `numThreads == 0`" };
	return pool->enqueue([&, offsetInChrLevel](size_t, const string& str)
	{
		vector<pair<uint32_t, uint32_t>> offset;
		auto ids = encode(str, &offset, offsetInChrLevel);
		return make_pair(move(ids), move(offset));
	}, str);
}

namespace kiwi
{
	template<class Ty, class Key>
	inline Ty getItem(const nlohmann::json& json, Key&& key)
	{
		auto it = json.find(key);
		if (json.end() == it) throw SwTokenizerException{ "Missing key '" + string{ key } + "'" };
		try
		{
			return it->template get<Ty>();
		}
		catch (...)
		{
			throw SwTokenizerException{ "key '" + string{ key } + "' has wrong type." };
		}
	}

	template<class Ty, class Key>
	inline Ty getItemWithDefault(const nlohmann::json& json, Key&& key, const Ty& v)
	{
		auto it = json.find(key);
		if (json.end() == it || it->is_null()) return v;
		try
		{
			return it->template get<Ty>();
		}
		catch (...)
		{
			throw SwTokenizerException{ "key '" + string{ key } + "' has wrong type." };
		}
	}

	static constexpr const char* spTokenNames[] = {
		"unk_token", "cls_token", "sep_token", "pad_token", "mask_token", "bos_token", "eos_token",
	};
}

ostream& SwTokenizer::save(ostream& ostr) const
{
	nlohmann::json j;
	j["version"] = "1.0";
	j["decoder"] = {
		{ "type", "Kiwi" },
		{ "version", KIWI_VERSION_STRING },
	};
	j["model"] = { 
		{ "lowercase", config.doLowercase },
		{ "split_chinese", config.splitChinese },
		{ "split_punct", config.splitPunct },
		{ "split_verb", config.splitVerb },
		{ "split_eomi", config.splitEomi },
		{ "simple_tag", config.simpleTag },
		{ "glue_token", nullptr },
		{ "fallback_hangul", config.fallbackHangul },
		{ "fallback_byte", config.fallbackByte },
		{ "newline_token", config.newlineToken },
	};
	if (specialTokenIds[SwTokenizerConfig::glue] != -1) j["model"]["glue_token"] = specialTokenIds[SwTokenizerConfig::glue];
	if (config.additionalJson.empty())
	{
		j["additional"] = nullptr; 
	}
	else
	{
		j["additional"] = nlohmann::json::parse(config.additionalJson);
	}

	for (size_t i = 0; i <= SwTokenizerConfig::eos; ++i)
	{
		if (specialTokenIds[i] != -1) j["model"][spTokenNames[i]] = config.specialTokens[i];
		else j["model"][spTokenNames[i]] = nullptr;
	}

	nlohmann::json jvocab = { size() };
	for (size_t i = 0; i < size(); ++i)
	{
		auto& v = vocab.vocabs[i];
		jvocab[i] = { nullptr, nullptr, nullptr, nullptr };
		jvocab[i][0] = utf16To8(U16StringView{ v.form, v.length });
		if (v.pos != POSTag::unknown) jvocab[i][1] = (config.simpleTag ? tagToReprStr : tagToString)(v.pos);
		switch (v.flags)
		{
		case SwTokenFlag::none:
		case SwTokenFlag::punct:
		case SwTokenFlag::chinese:
			jvocab[i][2] = "word";
			break;
		case SwTokenFlag::subword:
			jvocab[i][2] = "subword";
			break;
		case SwTokenFlag::special:
			jvocab[i][2] = "special";
			break;
		case SwTokenFlag::glue:
			jvocab[i][2] = "glue";
			break;
		case SwTokenFlag::byte:
			jvocab[i][2] = "byte";
			break;
		}
		jvocab[i][3] = tokenLProbs[i];
	}
	j["model"].emplace("vocab", move(jvocab));
	auto old = ostr.width(1);
	ostr << j;
	ostr.width(old);
	return ostr;
}

SwTokenizer SwTokenizer::load(const Kiwi& kiwi, istream& istr)
{
	auto j = nlohmann::json::parse(istr);
	if (!j["decoder"].is_object()) throw SwTokenizerException{ "Missing key 'decoder'" };
	if (!j["model"].is_object()) throw SwTokenizerException{ "Missing key 'model'" };
	if (j["decoder"]["type"].get<string>() != "Kiwi") throw SwTokenizerException{ "Unsupported `decoder.type`: " + j["decoder"]["type"].dump() };
	SwTokenizerConfig config;
	auto& m = j["model"];
	if (!m["vocab"].is_array()) throw SwTokenizerException{ "Missing key 'vocab'" };
	config.doLowercase = getItem<bool>(m, "lowercase");
	config.splitChinese = getItem<bool>(m, "split_chinese");
	config.splitPunct = getItem<bool>(m, "split_punct");
	config.splitVerb = getItem<bool>(m, "split_verb");
	config.splitEomi = getItem<bool>(m, "split_eomi");
	config.simpleTag = getItem<bool>(m, "simple_tag");
	config.useGlueToken = m["glue_token"].is_number();
	config.fallbackHangul = getItem<bool>(m, "fallback_hangul");
	config.fallbackByte = getItem<bool>(m, "fallback_byte");
	config.newlineToken = getItem<bool>(m, "newline_token");
	if (j.count("additional"))
	{
		config.additionalJson = j["additional"].dump();
	}

	for (size_t i = 0; i <= SwTokenizerConfig::eos; ++i)
	{
		config.specialTokens[i] = getItemWithDefault<string>(m, spTokenNames[i], "");
	}

	SwTokenizerBuilder builder{ kiwi, config };
	for (auto& v : m["vocab"])
	{
		if (v.size() != 4) throw SwTokenizerException{ "Invalid vocab item: " + v.dump()};
		string form = v[0].get<string>();
		POSTag tag = POSTag::unknown;
		if (!v[1].is_null())
		{
			if (config.simpleTag) tag = reprStrToTag(v[1].get<string>());
			else tag = toPOSTag(utf8To16(v[1].get<string>()));

			if (tag == POSTag::max) throw SwTokenizerException{ "Invalid vocab item " + v.dump() };
		}
		SwTokenFlag flag = SwTokenFlag::none;
		if (v[2].get<string>() == "word") flag = SwTokenFlag::none;
		else if (v[2].get<string>() == "subword") flag = SwTokenFlag::subword;
		else if (v[2].get<string>() == "special") flag = SwTokenFlag::special;
		else if (v[2].get<string>() == "glue") flag = SwTokenFlag::glue;
		else if (v[2].get<string>() == "byte") flag = SwTokenFlag::byte;
		float lprob = v[3].get<float>();
		builder.addToken(form, tag, flag, lprob);
	}
	
	return builder.build();
}

enum class UnigramSwTrainer::PrefixAvailability : uint8_t
{
	deleted = 0,
	available = 1,
	preserved = 2,
};

UnigramSwTrainer::UnigramSwTrainer(const Kiwi& _kiwi, const SwTokenizerConfig& _config, const UnigramSwTrainerConfig& _trainConfig)
	: kiwi{ &_kiwi }, config{ _config }, trainConfig{ _trainConfig }
{}

UnigramSwTrainer::UnigramSwTrainer(const UnigramSwTrainer&) = default;
UnigramSwTrainer::UnigramSwTrainer(UnigramSwTrainer&&) = default;
UnigramSwTrainer::~UnigramSwTrainer() = default;
UnigramSwTrainer& UnigramSwTrainer::operator=(const UnigramSwTrainer&) = default;
UnigramSwTrainer& UnigramSwTrainer::operator=(UnigramSwTrainer&&) = default;

void UnigramSwTrainer::addWord(const u16string& str, const Vector<const Morpheme*>& morphs, const Vector<size_t>& boundaries, bool spacePrefix)
{
	auto& rsents = sents.get();
	
	const auto emplace = [&](size_t s, size_t e, bool spacePrefix, const Morpheme* morph = nullptr, const Vector<size_t>* bounds = nullptr)
	{
		u16string sstr;
		sstr.reserve((spacePrefix ? 1 : 0) + e - s);
		if (spacePrefix) sstr.push_back(u' ');
		sstr.insert(sstr.end(), str.begin() + s, str.begin() + e);
		auto wid = wordMap.emplace(sstr, wordMap.size()).first->second;
		wordCnts.resize(max(wordCnts.size(), wid + 1));
		wordCnts[wid]++;
		rsents.add_data(-(int32_t)wid - 1);
		if (morph && !wordSuffix.count(wid))
		{
			WordCand wc{ nullptr };
			auto& tokenizations = wc.tokenizations.get();
			tokenizations.emplace_back();
			tokenizations.add_data(-(int32_t)wid - 1);
			tokenizations.emplace_back();
			bool first = false;
			for (auto m : morph->chunks)
			{
				if (isTagForPrefix(m->tag))
				{
					auto s = joinHangul(*m->kform);
					if (first) s.insert(s.begin(), u' ');
					auto wid = wordMap.emplace(move(s), wordMap.size()).first->second;
					wordCnts.resize(max(wordCnts.size(), wid + 1));
					tokenizations.add_data(-(int32_t)wid - 1);
				}
				else
				{
					tokenizations.add_data(kiwi->morphToId(toReprMorph(m)));
				}
				first = false;
			}
			wordSuffix.emplace(wid, move(wc));
		}
		if (bounds && !wordSuffix.count(wid))
		{
			WordCand wc{ nullptr };
			wc.hasBoundaries = true;
			auto& tokenizations = wc.tokenizations.get();
			tokenizations.emplace_back();
			for (auto i : *bounds)
			{
				tokenizations.add_data(i);
			}
			wordSuffix.emplace(wid, move(wc));
		}
	};

	if (morphs.size() == 1 && morphs[0]->complex)
	{
		emplace(0, str.size(), spacePrefix, morphs[0]);
	}
	else if (config.useGlueToken && boundaries.size() > 1 && all_of(str.begin(), str.end(), isHangulSyllable))
	{
		emplace(0, str.size(), spacePrefix, nullptr, &boundaries);
	}
	else
	{
		size_t start = 0;
		for (size_t i = 0; i < str.size(); ++i)
		{
			bool surrogate = isHighSurrogate(str[i]);
			if (config.splitChinese
				&& isChineseChr(surrogate ? mergeSurrogate(str[i], str[i + 1]) : str[i]))
			{
				if (start < i) emplace(start, i, start == 0 ? spacePrefix : false);
				emplace(i, i + (surrogate ? 2 : 1), true);
				start = i + (surrogate ? 2 : 1);
				i += (surrogate ? 1 : 0);
				continue;
			}

			if (config.splitPunct
				&& isTagForPunct(identifySpecialChr(str[i])))
			{
				if (start < i) emplace(start, i, start == 0 ? spacePrefix : false);
				emplace(i, i + 1, true);
				start = i + 1;
				continue;
			}
		}
		if (start < str.size()) emplace(start, str.size(), start == 0 ? spacePrefix : false);
	}
}

const Morpheme* UnigramSwTrainer::toReprMorph(const Morpheme* morph)
{
	auto key = make_pair(*morph->kform, config.simpleTag ? toReprTag(morph->tag) : morph->tag);
	return reprMorphMap.emplace(key, morph).first->second;
}

template<class Feeder>
size_t UnigramSwTrainer::_addSentences(Feeder&& feeder)
{
	Deque<future<pair<TokenResult, u16string>>> futures;
	auto& rsents = sents.get();
	const auto* morphBase = kiwi->idToMorph(0);
	Vector<const Morpheme*> verbalSuffices;
	Vector<const Morpheme*> eomiSuffices;
	UnorderedMap<Vector<const Morpheme*>, const Morpheme*> complexMorphemes;
	size_t addedSentences = 0;
	if (config.splitVerb)
	{
		for (size_t i = 0; i < kiwi->getMorphemeSize(); ++i)
		{
			if (!isVerbClass(morphBase[i].tag)) continue;
			if (morphBase[i].complex)
			{
				complexMorphemes.emplace(
					Vector<const Morpheme*>{ morphBase[i].chunks.begin(), morphBase[i].chunks.end() }, 
					&morphBase[i]
				);
			}
			else if(morphBase[i].kform && !morphBase[i].kform->empty())
			{
				verbalSuffices.emplace_back(&morphBase[i]);
			}
		}

		sort(verbalSuffices.begin(), verbalSuffices.end(), [&](const Morpheme* a, const Morpheme* b)
		{
			return a->kform->size() > b->kform->size();
		});
	}

	if (config.splitEomi)
	{
		// '~요' 및 그 이형태에 대해서만 분리한다
		for (size_t i = 0; i < kiwi->getMorphemeSize(); ++i)
		{
			if (morphBase[i].tag != POSTag::jx
				|| !morphBase[i].kform
				|| morphBase[i].kform->empty()) continue;
			if (morphBase[morphBase[i].lmMorphemeId].getForm() == u"이요"
				&& morphBase[i].getForm()[0] != u'이')
			{
				eomiSuffices.emplace_back(morphBase + i);
			}
		}
	}

	const auto receiveResult = [&]()
	{
		auto res = futures.front().get();
		futures.pop_front();
		if (res.first.first.empty()) return;

		uint32_t lastTokenEnd = -1;
		u16string contToken;
		Vector<const Morpheme*> contMorphs;
		Vector<size_t> contBoundaries;
		bool spacePrefix = false;
		bool isPrevNumber = false;
		rsents.emplace_back();
		for (auto& token : res.first.first)
		{
			if ((isTagForPrefix(token.tag) || !token.morph->kform || token.morph->kform->empty()) 
				&& lastTokenEnd == token.position
				&& !(trainConfig.preventMixedDigitTokens && token.tag == POSTag::sn)
				&& !isPrevNumber)
			{
				if (contToken.empty())
				{
					spacePrefix = false;
					if (config.splitPunct && isTagForPunct(token.tag)) spacePrefix = true;
					if (config.splitChinese && token.tag == POSTag::sh) spacePrefix = true;
				}
				if (config.doLowercase) toLower16(token.str.begin(), token.str.end(), back_inserter(contToken));
				else contToken += token.str;
				contMorphs.emplace_back(token.morph);
				contBoundaries.emplace_back(contToken.size());
				lastTokenEnd = token.position + token.length;
				continue;
			}

			if (!contToken.empty())
			{
				addWord(contToken, contMorphs, contBoundaries, spacePrefix);
				contToken.clear();
				contMorphs.clear();
				contBoundaries.clear();
				if (!(trainConfig.preventMixedDigitTokens && token.tag == POSTag::sn)
					&& !isPrevNumber) lastTokenEnd = -1;
			}

			if (isTagForPrefix(token.tag) || !token.morph->kform || token.morph->kform->empty())
			{
				spacePrefix = lastTokenEnd != token.position && !isOldHangulCoda(token.str[0]) && !isOldHangulVowel(token.str[0]);
				if (config.splitPunct && isTagForPunct(token.tag)) spacePrefix = true;
				if (config.splitChinese && token.tag == POSTag::sh) spacePrefix = true;
				if (config.doLowercase) toLower16(token.str.begin(), token.str.end(), back_inserter(contToken));
				else contToken += token.str;
				contMorphs.emplace_back(token.morph);
				contBoundaries.emplace_back(contToken.size());
				lastTokenEnd = token.position + token.length;
				isPrevNumber = (trainConfig.preventMixedDigitTokens && token.tag == POSTag::sn);
			}
			else
			{
				const Morpheme* verbSuffix = nullptr;
				auto eomiSuffix = findEomiSuffix(kiwi, token.morph, eomiSuffices);
				if (config.splitVerb && token.morph->complex)
				{
					token.str.insert(token.str.begin(), u' ');
					token.str.push_back(u'\x00'); // add suffix for distinguishing from normal words
					auto wid = wordMap.emplace(token.str, wordMap.size()).first->second;
					wordCnts.resize(max(wordCnts.size(), wid + 1));
					wordCnts[wid]++;
					if (!wordSuffix.count(wid))
					{
						Vector<const Morpheme*> submorphs{
							token.morph->chunks.begin(),
							token.morph->chunks.end()
						};
						Vector<int32_t> subids;
						bool first = true;
						for (auto m : submorphs)
						{
							if (isTagForPrefix(m->tag))
							{
								auto s = joinHangul(*m->kform);
								if (first) s.insert(s.begin(), u' ');
								auto wid = wordMap.emplace(move(s), wordMap.size()).first->second;
								wordCnts.resize(max(wordCnts.size(), wid + 1));
								subids.emplace_back(-(int32_t)wid - 1);
							}
							else
							{
								subids.emplace_back(kiwi->morphToId(toReprMorph(m)));
							}
							first = false;
						}

						WordCand wc{ toReprMorph(token.morph) };
						auto& tokenizations = wc.tokenizations.get();
						tokenizations.emplace_back();
						tokenizations.insert_data(subids.begin(), subids.end());
						for (size_t i = 1; i < submorphs.size() - 2; ++i)
						{
							auto it = complexMorphemes.find(Vector<const Morpheme*>{ submorphs.begin() + i, submorphs.end() });
							if (it == complexMorphemes.end()) continue;
							tokenizations.emplace_back();
							tokenizations.insert_data(subids.begin(), subids.begin() + i);
							tokenizations.add_data(kiwi->morphToId(toReprMorph(it->second)));
						}
						knownPrefixSize = std::max(knownPrefixSize, (size_t)*std::max_element(tokenizations.raw().begin(), tokenizations.raw().end()) + 1);
						knownPrefixSize = std::max(knownPrefixSize, (size_t)kiwi->morphToId(wc.morph) + 1);
						wordSuffix.emplace(wid, move(wc));
					}
					rsents.add_data(-(int32_t)wid - 1);
				}
				else if (config.splitVerb && (verbSuffix = findVerbalSuffix(token.morph, verbalSuffices)))
				{
					token.str.insert(token.str.begin(), u' ');
					token.str.push_back(u'\x00'); // add suffix for distinguishing from normal words
					auto wid = wordMap.emplace(token.str, wordMap.size()).first->second;
					wordCnts.resize(max(wordCnts.size(), wid + 1));
					wordCnts[wid]++;
					WordCand wc{ toReprMorph(token.morph), toReprMorph(verbSuffix) };
					knownPrefixSize = std::max(knownPrefixSize, (size_t)kiwi->morphToId(wc.suffix) + 1);
					knownPrefixSize = std::max(knownPrefixSize, (size_t)kiwi->morphToId(wc.morph) + 1);
					wordSuffix.emplace(wid, move(wc));
					rsents.add_data(-(int32_t)wid - 1);
				}
				else if (config.splitEomi && eomiSuffix.first)
				{
					token.str.push_back(u'\x01'); // add suffix for distinguishing from normal words
					auto wid = wordMap.emplace(token.str, wordMap.size()).first->second;
					wordCnts.resize(max(wordCnts.size(), wid + 1));
					wordCnts[wid]++;
					WordCand wc{ toReprMorph(token.morph), toReprMorph(eomiSuffix.second) };
					wc.baseEomi = toReprMorph(eomiSuffix.first);
					knownPrefixSize = std::max(knownPrefixSize, (size_t)kiwi->morphToId(wc.suffix) + 1);
					knownPrefixSize = std::max(knownPrefixSize, (size_t)kiwi->morphToId(wc.morph) + 1);
					knownPrefixSize = std::max(knownPrefixSize, (size_t)kiwi->morphToId(wc.baseEomi) + 1);
					wordSuffix.emplace(wid, move(wc));
					rsents.add_data(-(int32_t)wid - 1);
				}
				else
				{
					rsents.add_data(kiwi->morphToId(toReprMorph(token.morph)));
				}
				lastTokenEnd = token.position + token.length;
			}
		}
		
		if (!contToken.empty())
		{
			addWord(contToken, contMorphs, contBoundaries, spacePrefix);
		}
		addedSentences++;
	};

	while(1)
	{
		auto s = feeder();
		if (s.empty()) break;
		auto s16 = toUTF16(move(s));
		futures.emplace_back(kiwi->asyncAnalyzeEcho(move(s16), Match::normalizeCoda | Match::zCoda));
		if (futures.size() > kiwi->getNumThreads() * 4 && !futures.empty())
		{
			receiveResult();
		}
	}

	while (!futures.empty())
	{
		receiveResult();
	}
	return addedSentences;
}

size_t UnigramSwTrainer::addSentences(const function<string()>& feeder)
{
	return _addSentences(feeder);
}

size_t UnigramSwTrainer::addSentences(const function<u16string()>& feeder)
{
	return _addSentences(feeder);
}

float UnigramSwTrainer::buildSubwordVocabs(const size_t minCnt, const size_t maxPrefixLength)
{
	auto& rawTokens = sents.get().raw();
	int32_t minV = 0, maxV = 0;
	for (auto v : rawTokens)
	{
		minV = min(v, minV);
		maxV = max(v, maxV);
	}

	knownPrefixSize = std::max((size_t)maxV + 1, knownPrefixSize);
	tokenFreqs.clear();
	tokenFreqs.resize((int64_t)knownPrefixSize - minV);
	for (auto i : rawTokens)
	{
		tokenFreqs[i >= 0 ? i : (i + tokenFreqs.size())]++;
	}

	u16string allTexts;
	map<char32_t, uint32_t> chrCnts, prefixChrCnts;
	set<char32_t> chrsPreserved;

	{
		size_t chrLength = 0;
		for (auto& p : wordMap)
		{
			U16StringView s{ p.first };
			if (!s.empty() && s[0] == u' ') s = s.substr(1);
			if (!s.empty() && s.back() <= 1) s = s.substr(0, s.size() - 1);
			foreachU32Chr(s, [&, i = 0](char32_t c) mutable
			{
				chrCnts[c]++;
				if (i++ == 0 && p.first.front() == u' ') prefixChrCnts[c]++;
			});
			if (p.first.size() <= 1) continue;
			chrLength += (p.first.size() + 1) * wordCnts[p.second];
		}
		allTexts.reserve(chrLength + 1);
		allTexts.resize(1);

		for (auto& p : wordMap)
		{
			U16StringView s{ p.first };
			if (!s.empty() && s[0] == u' ') s = s.substr(1);
			if (!s.empty() && s.back() <= 1) s = s.substr(0, s.size() - 1);
			if (s.size() <= 1) continue;
			for (size_t i = 0; i < wordCnts[p.second]; ++i)
			{
				allTexts.insert(allTexts.end(), s.rbegin(), s.rend());
				allTexts.push_back(' ');
			}
		}

		chrsPreserved = getChrsPreserved(chrCnts, trainConfig.chrCoverage);
	}

	sais::FmIndex<char16_t> fi{ allTexts.data(), allTexts.size() };

	utils::ContinuousTrie<utils::TrieNode<char16_t, size_t>> trie{ 1 };

	chrPrefix.clear();
	chrPrefix.resize(knownPrefixSize);
	
	prefixAvailable.clear();


	/*for (size_t i = 0; i < ivSize; ++i)
	{
		auto morph = kiwi->idToMorph(i);
		if (!morph->kform || morph->kform->empty() || !isTagForPrefix(morph->tag))
		{
			chrPrefix.emplace_back();
			continue;
		}
		auto s = joinHangul(*morph->kform);
		s.insert(s.begin(), u' ');
		trie.build(s.begin(), s.end(), i);
		chrPrefix.emplace_back(move(s));
	}*/

	if (config.useGlueToken)
	{
		chrPrefix.emplace_back();
		prefixAvailable.emplace_back(PrefixAvailability::preserved);
	}

	if (config.fallbackHangul)
	{
		for (char16_t c = 0xAC00; c < 0xD7A4; c += 28)
		{
			chrPrefix.emplace_back(u16string{ u' ', c });
			trie.build(chrPrefix.back().begin(), chrPrefix.back().end(), chrPrefix.size() - 1);
			prefixAvailable.emplace_back(PrefixAvailability::preserved);
			prefixChrCnts.erase(c);
		}

		for (char16_t c = 0x11A8; c < 0x11A7 + 28; ++c)
		{
			chrPrefix.emplace_back(&c, &c + 1);
			trie.build(chrPrefix.back().begin(), chrPrefix.back().end(), chrPrefix.size() - 1);
			prefixAvailable.emplace_back(PrefixAvailability::preserved);
			chrCnts.erase(c);
		}

		for (auto it = chrsPreserved.begin(); it != chrsPreserved.end(); )
		{
			if (isHangulSyllable(*it))
			{
				it = chrsPreserved.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	for (auto p : chrCnts)
	{
		auto c = p.first;
		if (config.splitChinese && isChineseChr(c)) continue;
		if (c < 0x10000)
		{
			char16_t c16 = c;
			chrPrefix.emplace_back(&c16, &c16 + 1);
		}
		else
		{
			auto c16 = decomposeSurrogate(c);
			chrPrefix.emplace_back(c16.begin(), c16.end());
		}
		trie.build(chrPrefix.back().begin(), chrPrefix.back().end(), chrPrefix.size() - 1);
		prefixAvailable.emplace_back(chrsPreserved.count(c) ? PrefixAvailability::preserved : PrefixAvailability::available);
	}

	for (auto p : prefixChrCnts)
	{
		auto c = p.first;
		if (c < 0x10000)
		{
			char16_t c16 = c;
			chrPrefix.emplace_back(u16string{ u' ', c16 });
		}
		else
		{
			auto c16 = decomposeSurrogate(c);
			chrPrefix.emplace_back(u16string{ u' ', c16[0], c16[1] });
		}
		trie.build(chrPrefix.back().begin(), chrPrefix.back().end(), chrPrefix.size() - 1);
		prefixAvailable.emplace_back(chrsPreserved.count(c) ? PrefixAvailability::preserved : PrefixAvailability::available);
	}

	/*
	* 접미사 목록 중에서 등장 빈도가 minCnt 이상인 것들만 추려낸다.
	* 단 접미사 abc와 bc의 빈도가 동일하다면 bc는 온전히 abc 패턴에 포함되는 것이므로 bc를 굳이 등록할 필요가 없음.
	* abc와 ab의 빈도가 동일한 경우도 마찬가지로 ab를 등록할 필요가 없음.
	*/
	{
		Vector<UnorderedMap<u16string, size_t>> candCnts;
		fi.enumSuffices(minCnt, [&](const sais::FmIndex<char16_t>::SuffixTy& s, const sais::FmIndex<char16_t>::TraceTy& t)
		{
			bool isSubword = s[0] != u' ';
			size_t realSize = s.size() - (isSubword ? 0 : 1);
			if (realSize <= 1) return true;
			if (isLowSurrogate(s.front())) return false;
			if (isHighSurrogate(s.back())) return true;
			if (count(s.begin(), s.end(), '\x00') || count(s.begin(), s.end(), '\x01') || count(s.begin() + 1, s.end(), u' ') || s.size() > maxPrefixLength) return false;
			if (trainConfig.removeRepetitive && testRepetition(s.data() + (isSubword ? 0 : 1), s.size() - (isSubword ? 0 : 1))) return false;
		
			size_t cnt = t.back().second - t.back().first;
			if (candCnts.size() <= realSize - 2) candCnts.resize(realSize - 1);
			candCnts[realSize - 2].emplace(s, cnt);
			return true;
		});
	
		for (size_t i = 1; i < candCnts.size(); ++i)
		{
			auto& cands = candCnts[i];
			auto& subCands = candCnts[i - 1];
			for (auto& p : cands)
			{
				auto it = subCands.find(p.first.substr(p.first[0] == u' ' ? 2 : 1));
				if (it != subCands.end() && it->second == p.second)
				{
					subCands.erase(it);
				}
			
				it = subCands.find(p.first.substr(0, p.first.size() - 1));
				if (it != subCands.end() && it->second == p.second)
				{
					subCands.erase(it);
				}
			}
		}

		Vector<const u16string*> sortedCands;
		for (auto& cands : candCnts)
		{
			for (auto& p : cands)
			{
				sortedCands.emplace_back(&p.first);
			}
		}

		sort(sortedCands.begin(), sortedCands.end(), [&](const u16string* a, const u16string* b)
		{
			return *a < *b;
		});

		for (auto s : sortedCands)
		{
			chrPrefix.emplace_back(*s);
			trie.build(s->begin(), s->end(), chrPrefix.size() - 1);
			prefixAvailable.emplace_back(PrefixAvailability::available);
		}
	}

	chrTrie = utils::FrozenTrie<char16_t, size_t>{ trie, ArchTypeHolder<ArchType::none>() };

	wordBestTokenizations.clear();
	wordBestTokenizations.resize(wordMap.size());
	invWordMap.clear();
	invWordMap.resize(wordMap.size());
	for (auto& p : wordMap)
	{
		auto wordSuffixIt = wordSuffix.find(p.second);
		if (wordSuffixIt == wordSuffix.end())
		{
			wordBestTokenizations[p.second] = tokenizeShort(p.first);
		}
		else
		{
			auto& m = wordSuffixIt->second;
			if (m.morph)
			{
				wordBestTokenizations[p.second].emplace_back((uint32_t)kiwi->morphToId(m.morph));
			}
			/*else if (m.hasBoundaries)
			{
				wordBestTokenizations[p.second] = tokenizeShort(p.first, m.tokenizations.get().raw());
			}*/
			else
			{
				wordBestTokenizations[p.second] = tokenizeShort(p.first);
			}
		}
		invWordMap[p.second] = &p;
	}
	return updateProb(true);
}

Vector<uint32_t> UnigramSwTrainer::tokenizeShort(U16StringView s, bool spacePrefix) const
{
	RaggedVector<uint32_t> pathes;
	Vector<Vector<uint32_t>> cands;
	auto node = chrTrie.root();
	if (s[0] == u' ')
	{
		spacePrefix = true;
		s = s.substr(1);
	}

	if (spacePrefix) node = node->template nextOpt<ArchType::none>(chrTrie, u' ');
	for (size_t i = 0; i < s.size(); ++i)
	{
		pathes.emplace_back();
		auto nnode = node->template nextOpt<ArchType::none>(chrTrie, s[i]);
		while (!nnode)
		{
			node = node->fail();
			nnode = node->template nextOpt<ArchType::none>(chrTrie, s[i]);
		}
		size_t prefixId = nnode->val(chrTrie);
		
		cands.clear();
		if (chrTrie.hasMatch(prefixId))
		{
			auto& p = chrPrefix[prefixId];
			if (spacePrefix ? (p[0] == u' ' && p.size() == i + 2) : p.size() == i + 1)
			{
				pathes.add_data(prefixId);
				node = nnode;
				continue;
			}
			else if(i >= p.size())
			{
				cands.emplace_back(pathes[i - p.size()].begin(), pathes[i - p.size()].end());
				cands.back().emplace_back(prefixId);
			}
		}
		
		if (chrTrie.hasMatch(prefixId) || chrTrie.hasSubmatch(prefixId))
		{
			for (auto submatcher = nnode->fail(); submatcher; submatcher = submatcher->fail())
			{
				prefixId = submatcher->val(chrTrie);
				if (chrTrie.hasMatch(prefixId))
				{
					auto& p = chrPrefix[prefixId];
					if (i < p.size()) continue;
					cands.emplace_back(pathes[i - p.size()].begin(), pathes[i - p.size()].end());
					cands.back().emplace_back(prefixId);
				}
			}
		}
		
		if (!cands.empty())
		{
			size_t shortestPath = 0;
			for (size_t i = 1; i < cands.size(); ++i)
			{
				if (cands[i].size() < cands[shortestPath].size())
				{
					shortestPath = i;
				}
			}
			pathes.insert_data(cands[shortestPath].begin(), cands[shortestPath].end());
		}
		node = nnode;
	}
	Vector<uint32_t> ret{ pathes[pathes.size() - 1].begin(), pathes[pathes.size() - 1].end() };
	return ret;
}

Vector<uint32_t> UnigramSwTrainer::tokenizeShort(U16StringView s, const Vector<int32_t>& boundaries) const
{
	Vector<uint32_t> ret;
	size_t last = 0;
	bool spacePrefix = false;
	if (s[0] == u' ')
	{
		spacePrefix = true;
		s = s.substr(1);
	}

	for (auto b : boundaries)
	{
		if (!ret.empty())
		{
			ret.emplace_back(knownPrefixSize);
		}

		auto sub = tokenizeShort(s.substr(last, b - last), last == 0 ? spacePrefix : false);
		ret.insert(ret.end(), sub.begin(), sub.end());
		last = b;
	}
	return ret;
}

pair<Vector<uint32_t>, float> UnigramSwTrainer::tokenizeBest(U16StringView s, bool spacePrefix, const Vector<int32_t>* boundaries) const
{
	Vector<pair<Vector<uint32_t>, float>> pathes, cands;
	auto node = chrTrie.root();
	if (s[0] == u' ')
	{
		spacePrefix = true;
		s = s.substr(1);
	}

	if (spacePrefix) node = node->template nextOpt<ArchType::none>(chrTrie, u' ');
	for (size_t i = 0; i < s.size(); ++i)
	{
		auto nnode = node->template nextOpt<ArchType::none>(chrTrie, s[i]);
		while (!nnode)
		{
			node = node->fail();
			nnode = node->template nextOpt<ArchType::none>(chrTrie, s[i]);
		}
		size_t prefixId = nnode->val(chrTrie);

		cands.clear();
		if (chrTrie.hasMatch(prefixId))
		{
			auto& p = chrPrefix[prefixId];
			const auto available = prefixId < knownPrefixSize || prefixAvailable[prefixId - knownPrefixSize] != PrefixAvailability::deleted;
			if (spacePrefix ? (p[0] == u' ' && p.size() == i + 2) : p.size() == i + 1)
			{
				if (available)
				{
					cands.emplace_back(Vector<uint32_t>((size_t)1, (uint32_t)prefixId), prefixLProbs[prefixId]);
				}
			}
			else if (i >= p.size())
			{
				auto& l = pathes[i - p.size()];
				if (!l.first.empty() && available)
				{
					cands.emplace_back(l);
					cands.back().first.emplace_back(prefixId);
					cands.back().second += prefixLProbs[prefixId];
				}
			}
		}

		if (chrTrie.hasMatch(prefixId) || chrTrie.hasSubmatch(prefixId))
		{
			for (auto submatcher = nnode->fail(); submatcher; submatcher = submatcher->fail())
			{
				prefixId = submatcher->val(chrTrie);
				if (chrTrie.hasMatch(prefixId))
				{
					auto& p = chrPrefix[prefixId];
					const auto available = prefixId < knownPrefixSize || prefixAvailable[prefixId - knownPrefixSize] != PrefixAvailability::deleted;
					if (i < p.size()) continue;
					auto& l = pathes[i - p.size()];
					if (!l.first.empty() && available)
					{
						cands.emplace_back(l);
						cands.back().first.emplace_back(prefixId);
						cands.back().second += prefixLProbs[prefixId];
					}
				}
			}
		}

		if (cands.empty())
		{
			pathes.emplace_back();
		}
		else
		{
			size_t bestPath = 0;
			for (size_t i = 1; i < cands.size(); ++i)
			{
				if (cands[i].second > cands[bestPath].second)
				{
					bestPath = i;
				}
			}
			pathes.emplace_back(move(cands[bestPath]));
		}
		node = nnode;
	}

	if (boundaries && !boundaries->empty())
	{
		static constexpr size_t maxBoundaries = 5;
		Vector<int32_t> subBoundaries;
		float glueScore = prefixLProbs[knownPrefixSize];
		for (size_t j = 0; j < min(boundaries->size(), maxBoundaries); ++j)
		{
			auto b = (*boundaries)[j];
			if (b >= s.size() || b == 0) continue;
			subBoundaries.clear();
			for (size_t k = j + 1; k < min(boundaries->size(), maxBoundaries); ++k)
			{
				subBoundaries.emplace_back((*boundaries)[k] - b);
			}
			if (pathes[b - 1].first.empty()) continue;
			
			auto subs = tokenizeBest(s.substr(b), true, &subBoundaries);
			if (subs.first.empty()) continue;
			float score = pathes[b - 1].second + glueScore + subs.second;
			if (score >= pathes.back().second)
			{
				auto& finalPath = pathes.back().first;
				finalPath.clear();
				finalPath.insert(finalPath.end(), pathes[b - 1].first.begin(), pathes[b - 1].first.end());
				finalPath.emplace_back(knownPrefixSize);
				finalPath.insert(finalPath.end(), subs.first.begin(), subs.first.end());
				pathes.back().second = score;
			}
		}
	}

	return pathes.back();
}

pair<Vector<uint32_t>, float> UnigramSwTrainer::tokenizeBest(const WordCand& m) const
{
	pair<Vector<uint32_t>, float> whole, split;
	auto wholeId = m.morph ? kiwi->morphToId(m.morph) : -1;
	if (m.morph)
	{
		whole.first.emplace_back(wholeId);
		whole.second = prefixLProbs[wholeId] - ((wholeId >= knownPrefixSize && prefixAvailable[wholeId - knownPrefixSize] == PrefixAvailability::deleted) ? 1e+9 : 0);
	}

	if (m.baseEomi)
	{
		auto id = kiwi->morphToId(m.baseEomi);
		split.first.emplace_back(id);
		split.second = prefixLProbs[id] - ((id >= knownPrefixSize && prefixAvailable[id - knownPrefixSize] == PrefixAvailability::deleted) ? 1e+9 : 0);

		id = kiwi->morphToId(m.suffix);
		split.first.emplace_back(id);
		split.second += prefixLProbs[id];
	}
	else if (!m.suffix)
	{
		Vector<pair<Vector<uint32_t>, float>> cands;
		if (m.morph) cands.emplace_back(move(whole));
		for (auto p : m.tokenizations.get())
		{
			Vector<uint32_t> ids;
			float score = 0;
			for (auto i : p)
			{
				if (i >= 0)
				{
					ids.emplace_back(i);
					score += prefixLProbs[i] - ((i >= knownPrefixSize && prefixAvailable[i - knownPrefixSize] == PrefixAvailability::deleted) ? 1e+9 : 0);
				}
				else
				{
					size_t wid = -i - 1;
					auto bestSubs = tokenizeBest(invWordMap[wid]->first);
					ids.insert(ids.end(), bestSubs.first.begin(), bestSubs.first.end());
					score += bestSubs.second;
				}
			}
			cands.emplace_back(move(ids), score);
		}

		const pair<Vector<uint32_t>, float>* bestCand = &cands[0];
		for (auto& p : cands)
		{
			if (p.second > bestCand->second)
			{
				bestCand = &p;
			}
		}
		return *bestCand;
	}
	else
	{
		auto prefix = joinHangul(m.morph->kform->begin(), m.morph->kform->end() - m.suffix->kform->size());
		split = tokenizeBest(prefix, true);
		if (!split.first.empty())
		{
			auto id = kiwi->morphToId(m.suffix);
			split.first.emplace_back(id);
			split.second += prefixLProbs[id];
		}
	}
	if (split.first.empty() || whole.second > split.second) return whole;
	else return split;
}

float UnigramSwTrainer::updateProb(bool init)
{
	auto& rawTokens = sents.get().raw();
	prefixFreqs.clear();
	prefixFreqs.resize(chrPrefix.size());
	prefixLProbs.resize(chrPrefix.size());
	
	size_t totCnt = 0;
	for (size_t i = 0; i < knownPrefixSize; ++i)
	{
		prefixFreqs[i] += tokenFreqs[i];
		totCnt += tokenFreqs[i];
	}

	for (size_t i = knownPrefixSize; i < tokenFreqs.size(); ++i)
	{
		auto wid = tokenFreqs.size() - 1 - i;
		for (auto j : wordBestTokenizations[wid])
		{
			prefixFreqs[j] += tokenFreqs[i];
		}
		totCnt += wordBestTokenizations[wid].size() * tokenFreqs[i];
	}

	const double discnt = init ? 0.999 : 0.999999, smoothing = (1 - discnt) / prefixFreqs.size();
	double totCntF = totCnt;
	for (size_t i = 0; i < prefixFreqs.size(); ++i)
	{
		prefixLProbs[i] = log(prefixFreqs[i] / max(totCntF, 1.) * discnt + smoothing);
	}

	if (config.useGlueToken && init)
	{
		prefixLProbs[knownPrefixSize] = -3;
	}
	
	currentVocabSize = 0;
	size_t c = 0;
	double m = 0;
	for (size_t i = 0; i < prefixFreqs.size(); ++i)
	{
		if (prefixFreqs[i])
		{
			m += (prefixLProbs[i] - m) * prefixFreqs[i] / (c + prefixFreqs[i]);
			c += prefixFreqs[i];
		}
		if (prefixFreqs[i] || (
			i >= knownPrefixSize && prefixAvailable[i - knownPrefixSize] == PrefixAvailability::preserved
		))
		{
			currentVocabSize++;
		}
	}
	return m;
}

size_t UnigramSwTrainer::reduceVocab(float ratio, size_t minVocabSize)
{
	if (minVocabSize == 0) minVocabSize = trainConfig.vocabSize;
	if (minVocabSize < config.numSpecialTokens())
	{
		throw invalid_argument{ "`minVocabSize` must be greater than `numSpecialTokens()`" };
	}
	minVocabSize -= config.numSpecialTokens();

	Vector<pair<float, uint32_t>> alivePrefices;
	for (size_t i = knownPrefixSize; i < prefixLProbs.size(); ++i)
	{
		if ((!trainConfig.reduceStrict && !prefixFreqs[i])
			|| prefixAvailable[i - knownPrefixSize] != PrefixAvailability::available
		) continue;
		alivePrefices.emplace_back(prefixLProbs[i], i);
	}
	sort(alivePrefices.begin(), alivePrefices.end());
	size_t reductionSize = min((size_t)(currentVocabSize * ratio), max(currentVocabSize, minVocabSize) - minVocabSize);

	if (reductionSize <= 0) return 0;

	size_t r = 0;
	for (size_t i = 0; i < alivePrefices.size(); ++i)
	{
		if (prefixFreqs[alivePrefices[i].second]) ++r;
		prefixAvailable[alivePrefices[i].second - knownPrefixSize] = PrefixAvailability::deleted;
		if (r >= reductionSize) break;
	}
	return r;
}

void UnigramSwTrainer::updateTokenization()
{
	utils::forEach(kiwi->getThreadPool(), wordMap, [&](size_t tid, const pair<const u16string, size_t>& p)
	{
		auto wordSuffixIt = wordSuffix.find(p.second);
		if (wordSuffixIt == wordSuffix.end())
		{
			wordBestTokenizations[p.second] = tokenizeBest(p.first).first;
		}
		else if (wordSuffixIt->second.hasBoundaries)
		{
			wordBestTokenizations[p.second] = tokenizeBest(p.first, false, &wordSuffixIt->second.tokenizations.get().raw()).first;
		}
		else
		{
			wordBestTokenizations[p.second] = tokenizeBest(wordSuffixIt->second).first;
		}
	});
}

vector<u16string> UnigramSwTrainer::getUnkExamples() const
{
	vector<u16string> ret;
	for (auto& p : wordMap)
	{
		if (!wordBestTokenizations[p.second].empty()) continue;
		U16StringView s{ p.first };
		if (!s.empty() && s[0] == u' ') s = s.substr(1);
		ret.emplace_back(s);
	}
	return ret;
}

ostream& UnigramSwTrainer::writeVocabs(ostream& os) const
{
	Vector<pair<float, size_t>> sortedVocabs;
	for (size_t i = 0; i < prefixLProbs.size(); ++i)
	{
		if (!prefixFreqs[i] && (
			i < knownPrefixSize ||
			prefixAvailable[i - knownPrefixSize] != PrefixAvailability::preserved
		)) continue;
		sortedVocabs.emplace_back(prefixLProbs[i], i);
	}

	sort(sortedVocabs.rbegin(), sortedVocabs.rend());

	for (auto& p : config.specialTokens)
	{
		if (!p.empty()) os << p << '\t' << 0.f << endl;
	}

	for (auto& p : sortedVocabs)
	{
		if (p.second < knownPrefixSize)
		{
			auto m = kiwi->idToMorph(p.second);
			os << utf16To8(joinHangul(m->getForm())) << '/' 
				<< (config.simpleTag ? tagToReprStr : tagToString)(m->tag) 
				<< '\t' << p.first << endl;
		}
		else if (chrPrefix[p.second][0] == u' ')
		{
			os << utf16To8(chrPrefix[p.second].substr(1)) << '\t' << p.first << endl;
		}
		else
		{
			os << "##" << utf16To8(chrPrefix[p.second]) << '\t' << p.first << endl;
		}
	}
	return os;
}

SwTokenizer UnigramSwTrainer::build() const
{
	SwTokenizerBuilder builder{ *kiwi, config };
	Vector<pair<float, size_t>> sortedVocabs;
	for (size_t i = 0; i < prefixLProbs.size(); ++i)
	{
		if (!prefixFreqs[i] && (
			i < knownPrefixSize ||
			prefixAvailable[i - knownPrefixSize] != PrefixAvailability::preserved
		)) continue;
		sortedVocabs.emplace_back(prefixLProbs[i], i);
	}

	sort(sortedVocabs.rbegin(), sortedVocabs.rend());

	for (auto& p : config.specialTokens)
	{
		if (!p.empty()) builder.addToken(p, POSTag::unknown, SwTokenFlag::special, 0);
	}

	for (auto& p : sortedVocabs)
	{
		if (p.second < knownPrefixSize)
		{
			auto m = kiwi->idToMorph(p.second);
			builder.addToken(utf16To8(joinHangul(m->getForm())), m->tag, SwTokenFlag::none, p.first);
		}
		else if (config.useGlueToken && p.second == knownPrefixSize)
		{
			builder.addToken("", POSTag::unknown, SwTokenFlag::glue, p.first);
		}
		else if (chrPrefix[p.second][0] == u' ')
		{
			builder.addToken(utf16To8(chrPrefix[p.second].substr(1)), POSTag::unknown, SwTokenFlag::none, p.first);
		}
		else
		{
			builder.addToken(utf16To8(chrPrefix[p.second]), POSTag::unknown, SwTokenFlag::subword, p.first);
		}
	}

	if (config.fallbackByte)
	{
		for (size_t i = 0; i < 256; ++i)
		{
			builder.addToken(to_string(i), POSTag::unknown, SwTokenFlag::byte, 0);
		}
	}
	else if (config.newlineToken)
	{
		builder.addToken("10", POSTag::unknown, SwTokenFlag::byte, 0);
	}

	return builder.build();
}

ostream& UnigramSwTrainer::writeTokenizer(ostream& os) const
{
	return build().save(os);
}

UnigramSwTrainer::WordCand::WordCand(const Morpheme* _morph, const Morpheme* _suffix)
	: morph{ _morph }, suffix{ _suffix }
{}

UnigramSwTrainer::WordCand::WordCand(const WordCand&) = default;
UnigramSwTrainer::WordCand::WordCand(WordCand&&) = default;
UnigramSwTrainer::WordCand& UnigramSwTrainer::WordCand::operator=(const WordCand&) = default;
UnigramSwTrainer::WordCand& UnigramSwTrainer::WordCand::operator=(WordCand&&) = default;
UnigramSwTrainer::WordCand::~WordCand() = default;
