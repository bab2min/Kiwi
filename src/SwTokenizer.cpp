#include <unordered_set>
#include <set>

#include <kiwi/SwTokenizer.h>
#include <kiwi/Kiwi.h>

#include "FrozenTrie.hpp"
#include "StrUtils.h"
#include "UnicodeCase.h"
#include "RaggedVector.hpp"

#include "sais/fm_index.hpp"

using namespace std;
using namespace kiwi;

SwTokenizer::SwTokenizer(const SwTokenizer&) = default;
SwTokenizer::SwTokenizer(SwTokenizer&&) = default;
SwTokenizer& SwTokenizer::operator=(const SwTokenizer&) = default;
SwTokenizer& SwTokenizer::operator=(SwTokenizer&&) = default;
SwTokenizer::~SwTokenizer() = default;

SwTokenizer::SwTokenizer(const Kiwi & _kiwi, const SwTokenizerConfig & _config, const vector<SwToken>&tokens)
	: kiwi{ &_kiwi }, config{ _config }
{
	
}

SwTokenizer::SwTokenizer(const Kiwi & _kiwi, const SwTokenizerConfig & _config, size_t numMorphemes, const vector<string>& subwordTokens)
	: kiwi{ &_kiwi }, config{ _config }
{
	Vector<pair<KString, uint32_t>> subwordList;

	for (auto& t : subwordTokens)
	{
		vocab.emplace_back(t);
		swToMorph.emplace_back(0);
		// subword token
		if (t.size() > 2 && t[0] == '#' && t[1] == '#')
		{
			subwordList.emplace_back(normalizeHangul(t.begin() + 2, t.end()), vocab.size() - 1);
		}
		else
		{
			subwordList.emplace_back(u" " + normalizeHangul(t), vocab.size() - 1);
		}
	}

	if (config.integrateAllomoprh)
	{
		unordered_set<uint32_t> uniq;
		for (size_t i = 0; i < kiwi->getMorphemeSize(); ++i)
		{
			if (uniq.size() >= numMorphemes) break;

			auto* morph = kiwi->idToMorph(i);
			if (!morph) break;
			if (!morph->kform || morph->kform->empty()) continue;
			if (morph->combined) continue;
			if (uniq.count(morph->lmMorphemeId)) continue;

			uniq.emplace(morph->lmMorphemeId);
			if (morphToSw.size() <= morph->lmMorphemeId)
			{
				morphToSw.resize(morph->lmMorphemeId + 1);
			}
			morphToSw[morph->lmMorphemeId] = vocab.size();
			swToMorph.emplace_back((uint32_t)i);
			vocab.emplace_back(utf16To8(joinHangul(*morph->kform)), morph->tag);
			if (morph->tag == POSTag::nng || morph->tag == POSTag::nnp)
			{
				subwordList.emplace_back(u" " + *morph->kform, vocab.size() - 1);
			}
		}
	}
	else
	{

	}

	sort(subwordList.begin(), subwordList.end());
	utils::ContinuousTrie<utils::TrieNode<char16_t, uint32_t, utils::ConstAccess<map<char16_t, int32_t>>>> formTrie{ 1 };
	size_t estimatedNodeSize = 0;
	const KString* prevForm = nullptr;
	for (auto& f : subwordList)
	{
		if (!prevForm)
		{
			estimatedNodeSize += f.first.size();
			prevForm = &f.first;
			continue;
		}
		size_t commonPrefix = 0;
		while (commonPrefix < std::min(prevForm->size(), f.first.size())
			&& (*prevForm)[commonPrefix] == f.first[commonPrefix]) ++commonPrefix;
		estimatedNodeSize += f.first.size() - commonPrefix;
		prevForm = &f.first;
	}
	formTrie.reserveMore(estimatedNodeSize);

	decltype(formTrie)::CacheStore<KString> cache;
	for (auto& f : subwordList)
	{
		formTrie.buildWithCaching(f.first, f.second, cache);
	}
	trie = utils::FrozenTrie<char16_t, uint32_t>{ formTrie, ArchTypeHolder<ArchType::none>{} };
}

bool SwTokenizer::tokenizeSubword(const KString& str, std::vector<uint32_t>& out) const
{
	if (config.wholeTokenUnk)
	{

	}
	else
	{
		auto* node = trie.root()->template nextOpt<ArchType::none>(trie, ' ');
		for (auto c : str)
		{
			auto* nnode = node->template nextOpt<ArchType::none>(trie, c);
			if (nnode)
			{
				//nnode->val(trie);
				node = nnode;
			}
			else
			{
				node = trie.root()->template nextOpt<ArchType::none>(trie, c);
			}
		}
	}
	return false;
}

vector<uint32_t> SwTokenizer::encode(const string& str, vector<pair<uint32_t, uint32_t>>* offset) const
{
	vector<uint32_t> ret;
	auto tokens = kiwi->analyze(str, Match::allWithNormalizing).first;
	auto* baseMorph = kiwi->idToMorph(0);
	for (auto& t : tokens)
	{
		size_t id = t.morph - baseMorph;
		// Morpheme
		if (id < morphToSw.size() && morphToSw[id])
		{
			ret.emplace_back(morphToSw[id]);
			continue;
		}

		if (!tokenizeSubword(normalizeHangul(t.str), ret))
		{
			// add Unk
			continue;
		}
	}
	return ret;
}

string SwTokenizer::decode(const vector<uint32_t>& ids) const
{
	auto joiner = kiwi->newJoiner(false);
	for (auto id : ids)
	{
		// morpheme
		if (id < swToMorph.size() && swToMorph[id])
		{
			joiner.add(swToMorph[id]);
			continue;
		}
	}
	return joiner.getU8();
}

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
	//case POSTag::z_coda:
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

inline POSTag toReprTag(POSTag tag)
{
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

inline const char* tagToReprStr(POSTag tag)
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
	case POSTag::xsm:
		return "XSM";
	case POSTag::sf:
		return "S";
	case POSTag::z_coda:
		return "Z";
	}
	return nullptr;
}

inline u16string toUTF16(u16string&& str)
{
	return move(str);
}

inline u16string toUTF16(string&& str)
{
	return utf8To16(str);
}

template<class Fn>
void foreachWord(const u16string& str, Fn&& fn)
{
	for (size_t i = 0; i < str.size(); ++i)
	{
		while (i < str.size() && isSpace(str[i])) ++i;
		size_t s = i;
		while (i < str.size() && !isSpace(str[i])) ++i;
		if (s < i)
		{
			fn(nonstd::u16string_view{ &str[s], i - s });
		}
	}
}

enum class UnigramSwTrainer::PrefixAvailability : uint8_t
{
	deleted = 0,
	available = 1,
	preserved = 2,
};

template<class Fn>
void foreachU32Chr(const u16string& s, Fn&& fn)
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

UnigramSwTrainer::UnigramSwTrainer(const Kiwi& _kiwi, const SwTokenizerConfig& _config, const UnigramSwTrainerConfig& _trainConfig)
	: kiwi{ &_kiwi }, config{ _config }, trainConfig{ _trainConfig }
{}

UnigramSwTrainer::UnigramSwTrainer(const UnigramSwTrainer&) = default;
UnigramSwTrainer::UnigramSwTrainer(UnigramSwTrainer&&) = default;
UnigramSwTrainer::~UnigramSwTrainer() = default;
UnigramSwTrainer& UnigramSwTrainer::operator=(const UnigramSwTrainer&) = default;
UnigramSwTrainer& UnigramSwTrainer::operator=(UnigramSwTrainer&&) = default;

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
			auto base = kiwi->findMorpheme(u16string{ morph->kform->begin(), morph->kform->end() - s->kform->size() }, tag);
			if (base.empty()) return make_pair(nullptr, nullptr);
			return make_pair(base[0], s);
		}
	}
	return make_pair(nullptr, nullptr);
}

void UnigramSwTrainer::addWord(const u16string& str, const Vector<const Morpheme*>& morphs, const Vector<size_t>& boundaries)
{
	auto& rsents = sents.get();
	
	const auto emplace = [&](size_t s, size_t e, const Morpheme* morph = nullptr, const Vector<size_t>* bounds = nullptr)
	{
		auto wid = wordMap.emplace(str.substr(s, e - s), wordMap.size()).first->second;
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
			for (auto m : morph->chunks)
			{
				if (isTagForPrefix(m->tag))
				{
					auto wid = wordMap.emplace(joinHangul(*m->kform), wordMap.size()).first->second;
					wordCnts.resize(max(wordCnts.size(), wid + 1));
					tokenizations.add_data(-(int32_t)wid - 1);
				}
				else
				{
					tokenizations.add_data(kiwi->morphToId(toReprMorph(m)));
				}
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
		emplace(0, str.size(), morphs[0]);
	}
	else if (config.useGlueToken && boundaries.size() > 1 && all_of(str.begin(), str.end(), isHangulSyllable))
	{
		emplace(0, str.size(), nullptr, &boundaries);
	}
	else
	{
		size_t start = 0;
		for (size_t i = 0; i < str.size(); ++i)
		{
			if (config.splitChinese
				&& isHighSurrogate(str[i])
				&& isChineseChr(mergeSurrogate(str[i], str[i + 1])))
			{
				if (start < i) emplace(start, i);
				emplace(i, i + 2);
				start = i + 2;
				++i;
				continue;
			}

			if (config.splitChinese
				&& !isHighSurrogate(str[i])
				&& isChineseChr(str[i]))
			{
				if (start < i) emplace(start, i);
				emplace(i, i + 1);
				start = i + 1;
				continue;
			}

			if (config.splitPunct
				&& isTagForPunct(identifySpecialChr(str[i])))
			{
				if (start < i) emplace(start, i);
				emplace(i, i + 1);
				start = i + 1;
				continue;
			}
		}
		if (start < str.size()) emplace(start, str.size());
	}
}

const Morpheme* UnigramSwTrainer::toReprMorph(const Morpheme* morph)
{
	if (!config.simpleTag) return morph;
	auto key = make_pair(*morph->kform, toReprTag(morph->tag));
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
					Vector<const Morpheme*>{ morphBase[i].chunks.data(), morphBase[i].chunks.data() + morphBase[i].chunks.size() }, 
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
		rsents.emplace_back();
		for (auto& token : res.first.first)
		{
			if ((isTagForPrefix(token.tag) || !token.morph->kform || token.morph->kform->empty()) 
				&& lastTokenEnd == token.position)
			{
				if (config.doLowercase) toLower16(token.str.begin(), token.str.end(), back_inserter(contToken));
				else contToken += token.str;
				contMorphs.emplace_back(token.morph);
				contBoundaries.emplace_back(contToken.size());
				lastTokenEnd = token.position + token.length;
				continue;
			}

			if (!contToken.empty())
			{
				addWord(contToken, contMorphs, contBoundaries);
				contToken.clear();
				contMorphs.clear();
				contBoundaries.clear();
				lastTokenEnd = -1;
			}

			if (isTagForPrefix(token.tag) || !token.morph->kform || token.morph->kform->empty())
			{
				if (config.doLowercase) toLower16(token.str.begin(), token.str.end(), back_inserter(contToken));
				else contToken += token.str;
				contMorphs.emplace_back(token.morph);
				contBoundaries.emplace_back(contToken.size());
				lastTokenEnd = token.position + token.length;
			}
			else
			{
				const Morpheme* verbSuffix = nullptr;
				auto eomiSuffix = findEomiSuffix(kiwi, token.morph, eomiSuffices);
				if (config.splitVerb && token.morph->complex)
				{
					token.str.push_back(u'\x00'); // add suffix for distinguishing from normal words
					auto wid = wordMap.emplace(token.str, wordMap.size()).first->second;
					wordCnts.resize(max(wordCnts.size(), wid + 1));
					wordCnts[wid]++;
					if (!wordSuffix.count(wid))
					{
						Vector<const Morpheme*> submorphs{
							token.morph->chunks.data(),
							token.morph->chunks.data() + token.morph->chunks.size()
						};
						Vector<int32_t> subids;
						for (auto m : submorphs)
						{
							if (isTagForPrefix(m->tag))
							{
								auto wid = wordMap.emplace(joinHangul(*m->kform), wordMap.size()).first->second;
								wordCnts.resize(max(wordCnts.size(), wid + 1));
								subids.emplace_back(-(int32_t)wid - 1);
							}
							else
							{
								subids.emplace_back(kiwi->morphToId(toReprMorph(m)));
							}
						}

						WordCand wc{ token.morph };
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
						wordSuffix.emplace(wid, move(wc));
					}
					rsents.add_data(-(int32_t)wid - 1);
				}
				else if (config.splitVerb && (verbSuffix = findVerbalSuffix(token.morph, verbalSuffices)))
				{
					token.str.push_back(u'\x00'); // add suffix for distinguishing from normal words
					auto wid = wordMap.emplace(token.str, wordMap.size()).first->second;
					wordCnts.resize(max(wordCnts.size(), wid + 1));
					wordCnts[wid]++;
					WordCand wc{ token.morph, verbSuffix };
					wordSuffix.emplace(wid, move(wc));
					rsents.add_data(-(int32_t)wid - 1);
				}
				else if (config.splitEomi && eomiSuffix.first)
				{
					token.str.push_back(u'\x01'); // add suffix for distinguishing from normal words
					auto wid = wordMap.emplace(token.str, wordMap.size()).first->second;
					wordCnts.resize(max(wordCnts.size(), wid + 1));
					wordCnts[wid]++;
					WordCand wc{ token.morph, eomiSuffix.second };
					wc.baseEomi = eomiSuffix.first;
					wordSuffix.emplace(wid, move(wc));
					rsents.add_data(-(int32_t)wid - 1);
				}
				else
				{
					rsents.add_data(kiwi->morphToId(toReprMorph(token.morph)));
				}
			}
		}
		
		if (!contToken.empty())
		{
			addWord(contToken, contMorphs, contBoundaries);
		}
		addedSentences++;
	};

	while(1)
	{
		auto s = feeder();
		if (s.empty()) break;
		auto s16 = toUTF16(std::move(s));
		futures.emplace_back(kiwi->asyncAnalyzeEcho(std::move(s16), Match::normalizeCoda | Match::zCoda));
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
	auto mm = std::minmax_element(rawTokens.begin(), rawTokens.end());
	knownPrefixSize = *mm.second + 1;
	tokenFreqs.resize(*mm.second + 1 - *mm.first);
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
			foreachU32Chr(p.first, [&, i = 0](char32_t c) mutable
			{
				chrCnts[c]++;
				if (i++ == 0) prefixChrCnts[c]++;
			});
			if (p.first.size() <= 1) continue;
			chrLength += (p.first.size() + 1) * wordCnts[p.second];
		}
		allTexts.reserve(chrLength + 1);
		allTexts.resize(1);

		for (auto& p : wordMap)
		{
			if (p.first.size() <= 1) continue;
			for (size_t i = 0; i < wordCnts[p.second]; ++i)
			{
				allTexts.insert(allTexts.end(), p.first.rbegin(), p.first.rend());
				allTexts.push_back(' ');
			}
		}

		chrsPreserved = getChrsPreserved(chrCnts, trainConfig.chrCoverage);
	}

	sais::FmIndex<char16_t> fi{ allTexts.data(), allTexts.size() };

	utils::ContinuousTrie<utils::TrieNode<char16_t, size_t>> trie{ 1 };

	chrPrefix.resize(knownPrefixSize);

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

	for (auto p : chrCnts)
	{
		auto c = p.first;
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
			if (isLowSurrogate(s.front()) || isHighSurrogate(s.back())) return false;
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

	wordBestTokenizations.resize(wordMap.size());
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

Vector<uint32_t> UnigramSwTrainer::tokenizeShort(nonstd::u16string_view s) const
{
	RaggedVector<uint32_t> pathes;
	Vector<Vector<uint32_t>> cands;
	auto node = chrTrie.root();
	node = node->template nextOpt<ArchType::none>(chrTrie, u' ');
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
			if (p[0] == u' ' && p.size() == i + 2)
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

Vector<uint32_t> UnigramSwTrainer::tokenizeShort(nonstd::u16string_view s, const Vector<int32_t>& boundaries) const
{
	Vector<uint32_t> ret;
	size_t last = 0;
	for (auto b : boundaries)
	{
		if (!ret.empty())
		{
			ret.emplace_back(knownPrefixSize);
		}

		auto sub = tokenizeShort(s.substr(last, b - last));
		ret.insert(ret.end(), sub.begin(), sub.end());
		last = b;
	}
	return ret;
}

pair<Vector<uint32_t>, float> UnigramSwTrainer::tokenizeBest(nonstd::u16string_view s, const Vector<int32_t>* boundaries) const
{
	Vector<pair<Vector<uint32_t>, float>> pathes, cands;
	auto node = chrTrie.root();
	node = node->template nextOpt<ArchType::none>(chrTrie, u' ');
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
			if (p[0] == u' ' && p.size() == i + 2)
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
		for (size_t j = 0; j < std::min(boundaries->size(), maxBoundaries); ++j)
		{
			auto b = (*boundaries)[j];
			if (b >= s.size() || b == 0) continue;
			subBoundaries.clear();
			for (size_t k = j + 1; k < std::min(boundaries->size(), maxBoundaries); ++k)
			{
				subBoundaries.emplace_back((*boundaries)[k] - b);
			}
			if (pathes[b - 1].first.empty()) continue;
			
			auto subs = tokenizeBest(s.substr(b), &subBoundaries);
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
		whole.second = prefixLProbs[wholeId] - (prefixAvailable[wholeId] == PrefixAvailability::deleted ? 1e+9 : 0);
	}

	if (m.baseEomi)
	{
		auto id = kiwi->morphToId(m.baseEomi);
		split.first.emplace_back(id);
		split.second = prefixLProbs[id] - (prefixAvailable[id] == PrefixAvailability::deleted ? 1e+9 : 0);

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
					score += prefixLProbs[i] - (prefixAvailable[i] == PrefixAvailability::deleted ? 1e+9 : 0);
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
		split = tokenizeBest(prefix);
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
		prefixLProbs[i] = std::log(prefixFreqs[i] / totCntF * discnt + smoothing);
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
		if (!prefixFreqs[i]) continue;
		m += (prefixLProbs[i] - m) * prefixFreqs[i] / (c + prefixFreqs[i]);
		c += prefixFreqs[i];
		currentVocabSize++;
	}
	return m;
}

size_t UnigramSwTrainer::reduceVocab(float ratio, size_t minVocabSize)
{
	if (minVocabSize == 0) minVocabSize = config.vocabSize;
	if (minVocabSize < config.numSpecialTokens())
	{
		throw std::invalid_argument{ "`minVocabSize` must be greater than `numSpecialTokens()`" };
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
			wordBestTokenizations[p.second] = tokenizeBest(p.first, &wordSuffixIt->second.tokenizations.get().raw()).first;
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
		ret.emplace_back(p.first);
	}
	return ret;
}

ostream& UnigramSwTrainer::writeVocabs(ostream& os) const
{
	Vector<pair<float, size_t>> sortedVocabs;
	for (size_t i = 0; i < prefixLProbs.size(); ++i)
	{
		if (!prefixFreqs[i]) continue;
		sortedVocabs.emplace_back(prefixLProbs[i], i);
	}

	sort(sortedVocabs.rbegin(), sortedVocabs.rend());

	for (auto p : { 
		&config.unkToken, &config.clsToken, &config.sepToken, &config.padToken, 
		&config.maskToken, &config.bosToken, &config.eosToken
	})
	{
		if (!p->empty()) os << *p << '\t' << 0.f << endl;
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

UnigramSwTrainer::WordCand::WordCand(const Morpheme* _morph, const Morpheme* _suffix)
	: morph{ _morph }, suffix{ _suffix }
{}

UnigramSwTrainer::WordCand::WordCand(const WordCand&) = default;
UnigramSwTrainer::WordCand::WordCand(WordCand&&) = default;
UnigramSwTrainer::WordCand& UnigramSwTrainer::WordCand::operator=(const WordCand&) = default;
UnigramSwTrainer::WordCand& UnigramSwTrainer::WordCand::operator=(WordCand&&) = default;
UnigramSwTrainer::WordCand::~WordCand() = default;
