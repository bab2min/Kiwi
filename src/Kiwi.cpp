#include <fstream>
#include <numeric>

#include <kiwi/Kiwi.h>
#include <kiwi/Utils.h>
#include <kiwi/TemplateUtils.hpp>
#include <kiwi/Form.h>
#include <kiwi/LangModel.h>
#include "ArchAvailable.h"
#include "KTrie.h"
#include "FeatureTestor.h"
#include "FrozenTrie.hpp"
#include "StrUtils.h"
#include "SortUtils.hpp"
#include "serializer.hpp"
#include "Joiner.hpp"
#include "PathEvaluator.hpp"
#include "Kiwi.hpp"

using namespace std;

namespace kiwi
{
#ifdef KIWI_USE_BTREE
#ifdef KIWI_USE_MIMALLOC
	template<typename K, typename V> using BMap = btree::map<K, V, less<K>, mi_stl_allocator<pair<const K, V>>>;
#else
	template<typename K, typename V> using BMap = btree::map<K, V, less<K>>;
#endif
#else
	template<typename K, typename V> using BMap = Map<K, V>;
#endif

	vector<PretokenizedSpan> Kiwi::mapPretokenizedSpansToU16(const vector<PretokenizedSpan>& orig, const vector<size_t>& bytePositions)
	{
		vector<PretokenizedSpan> buf;
		for (auto& s : orig)
		{
			buf.emplace_back(s);
			buf.back().begin = upper_bound(bytePositions.begin(), bytePositions.end(), s.begin) - bytePositions.begin() - 1;
			buf.back().end = lower_bound(bytePositions.begin(), bytePositions.end(), s.end) - bytePositions.begin();
		}
		return buf;
	}

	Kiwi::Kiwi(ArchType arch, 
		const std::shared_ptr<lm::ILangModel> & _langMdl, 
		bool typoTolerant, 
		bool continualTypoTolerant, 
		bool lengtheningTypoTolerant)
		: langMdl{ _langMdl }, selectedArch{ arch }
	{
		dfSplitByTrie = (void*)getSplitByTrieFn(selectedArch, 
			typoTolerant, 
			continualTypoTolerant, 
			lengtheningTypoTolerant);
		dfFindForm = (void*)getFindFormFn(selectedArch, typoTolerant);
		dfFindFormWithPrefix = (void*)getFindFormWithPrefixFn(selectedArch, typoTolerant);
		dfFindBestPath = langMdl ? langMdl->getFindBestPathFn() : nullptr;
		dfNewJoiner = langMdl ? langMdl->getNewJoinerFn() : nullptr;
	}

	Kiwi::~Kiwi() = default;

	Kiwi::Kiwi(Kiwi&&) noexcept = default;

	Kiwi& Kiwi::operator=(Kiwi&&) = default;

	inline vector<size_t> allNewLinePositions(const u16string& str)
	{
		vector<size_t> ret;
		bool isCR = false;
		for (size_t i = 0; i < str.size(); ++i)
		{
			switch (str[i])
			{
			case 0x0D:
				isCR = true;
				ret.emplace_back(i);
				break;
			case 0x0A:
				if (!isCR) ret.emplace_back(i);
				isCR = false;
				break;
			case 0x0B:
			case 0x0C:
			case 0x85:
			case 0x2028:
			case 0x2029:
				isCR = false;
				ret.emplace_back(i);
				break;
			}
		}
		return ret;
	}

	inline void fillPairedTokenInfo(vector<TokenInfo>& tokens)
	{
		Vector<pair<uint32_t, uint32_t>> pStack;
		Vector<pair<uint32_t, uint32_t>> bStack;
		for (auto& t : tokens)
		{
			const uint32_t i = &t - tokens.data();
			if (t.tag == POSTag::sso)
			{
				uint32_t type = getSSType(t.str[0]);
				if (!type) continue;
				pStack.emplace_back(i, type);
			}
			else if (t.tag == POSTag::ssc)
			{
				uint32_t type = getSSType(t.str[0]);
				if (!type) continue;
				for (auto j = pStack.rbegin(); j != pStack.rend(); ++j)
				{
					if (j->second != type) continue;
					t.pairedToken = j->first;
					tokens[j->first].pairedToken = i;
					pStack.erase(j.base() - 1, pStack.end());
					break;
				}
			}
			else if (t.tag == POSTag::sb)
			{
				uint32_t type = getSBType(t.str);
				if (!type) continue;
				
				for (auto j = bStack.rbegin(); j != bStack.rend(); ++j)
				{
					if (j->second != type) continue;
					tokens[j->first].pairedToken = i;
					bStack.erase(j.base() - 1, bStack.end());
					break;
				}
				bStack.emplace_back(i, type);
			}
			else
			{
				continue;
			}
		}
	}

	/*
	* 문장 분리 기준
	* 1) 종결어미(ef) (요/jx)? (z_coda)? (so|sw|sh|sp|se|sf|(닫는 괄호))*
	* 2) 종결구두점(sf) (so|sw|sh|sp|se|(닫는 괄호))*
	* 3) 단 종결어미(ef) 바로 다음에 '요'가 아닌 조사(j)나 보조용언(vx), vcp, etm, 다른 어미(ec)가 뒤따르는 경우는 제외
	*/
	class SentenceParser
	{
		enum class State
		{
			none = 0,
			ef,
			efjx,
			z_coda,
			sf,
		} state = State::none;
		size_t lastPosition = 0;
		size_t lastLineNumber = 0;
	public:

		bool next(const TokenInfo& t, size_t lineNumber, bool forceNewSent = false)
		{
			bool ret = false;
			if (forceNewSent)
			{
				state = State::none;
				lastPosition = t.position + t.length;
				return true;
			}

			switch (state)
			{
			case State::none:
				if (t.tag == POSTag::ef)
				{
					state = State::ef;
				}
				else if (t.tag == POSTag::sf)
				{
					state = State::sf;
				}
				break;
			case State::ef:
				if (t.tag == POSTag::vx)
				{
					state = State::none;
					break;
				}
			case State::efjx:
				switch (t.tag)
				{
				case POSTag::z_coda:
					state = State::z_coda;
					break;
				case POSTag::jc:
				case POSTag::jkb:
				case POSTag::jkc:
				case POSTag::jkg:
				case POSTag::jko:
				case POSTag::jkq:
				case POSTag::jks:
				case POSTag::jkv:
				case POSTag::jx:
				case POSTag::vcp:
				case POSTag::etm:
				case POSTag::ec:
					if (t.tag == POSTag::jx && t.morph && *t.morph->kform == u"요")
					{
						if (state == State::ef)
						{
							state = State::efjx;
						}
						else
						{
							ret = true;
							state = State::none;
						}
					}
					else
					{
						state = State::none;
					}
					break;
				case POSTag::so:
				case POSTag::sw:
				case POSTag::sh:
				case POSTag::sp:
				case POSTag::se:
				case POSTag::ssc:
					break;
				case POSTag::sf:
					state = State::sf;
					break;
				case POSTag::sso:
					if (lineNumber == lastLineNumber) break;
				default:
					ret = true;
					state = State::none;
					break;
				}
				break;
			case State::z_coda:
				switch (t.tag)
				{
				case POSTag::so:
				case POSTag::sw:
				case POSTag::sh:
				case POSTag::sp:
				case POSTag::se:
				case POSTag::sf:
				case POSTag::ssc:
					break;
				case POSTag::sso:
					if (lineNumber == lastLineNumber) break;
				default:
					ret = true;
					state = State::none;
					break;
				}
				break;
			case State::sf:
				switch (t.tag)
				{
				case POSTag::so:
				case POSTag::sw:
				case POSTag::sh:
				case POSTag::se:
				case POSTag::sp:
				case POSTag::ssc:
					break;
				case POSTag::sso:
					if (lineNumber != lastLineNumber)
					{
						ret = true;
						state = State::none;
					}
					break;
				case POSTag::sl:
				case POSTag::sn:
					if (lastPosition == t.position)
					{
						state = State::none;
						break;
					}
				default:
					ret = true;
					state = State::none;
					break;
				}
				break;
			}
			lastPosition = t.position + t.length;
			lastLineNumber = lineNumber;
			return ret;
		}
	};

	inline bool hasSentences(const TokenInfo* first, const TokenInfo* last)
	{
		SentenceParser sp;
		for (; first != last; ++first)
		{
			if (sp.next(*first, 0)) return true;
		}
		return sp.next({}, 0);
	}

	inline bool isNestedLeft(const TokenInfo& t)
	{
		return isJClass(t.tag) || (isEClass(t.tag) && t.tag != POSTag::ef) || t.tag == POSTag::sp;
	}

	inline bool isNestedRight(const TokenInfo& t)
	{
		return isJClass(t.tag) || isEClass(t.tag) || (isVerbClass(t.tag) && t.str == u"하") || t.tag == POSTag::vcp || t.tag == POSTag::sp;
	}

	/**
	* @brief tokens에 문장 번호 및 줄 번호를 채워넣는다.
	*/
	inline void fillSentLineInfo(vector<TokenInfo>& tokens, const vector<size_t>& newlines)
	{
		SentenceParser sp;
		uint32_t sentPos = 0, lastSentPos = 0, subSentPos = 0, accumSubSent = 1, accumWordPos = 0, lastWordPos = 0;
		size_t nlPos = 0, lastNlPos = 0, nestedSentEnd = 0, nestedEnd = 0;
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			auto& t = tokens[i];
			if ((i >= nestedEnd) && sp.next(t, nlPos, nestedSentEnd && i == nestedSentEnd))
			{
				bool includePrevToken = i > 1 && 
					(tokens[i - 1].tag == POSTag::so 
						|| tokens[i - 1].tag == POSTag::sw 
						|| tokens[i - 1].tag == POSTag::sp 
						|| tokens[i - 1].tag == POSTag::se
						|| tokens[i - 1].tag == POSTag::sso)
					&& tokens[i - 1].endPos() == tokens[i].position
					&& tokens[i - 1].position > tokens[i - 2].endPos();
				if (nestedSentEnd)
				{
					subSentPos++;
					accumSubSent++;
					if (includePrevToken)
					{
						tokens[i - 1].subSentPosition = subSentPos;
					}
				}
				else
				{
					sentPos++;
					accumSubSent = 1;
					if (includePrevToken)
					{
						tokens[i - 1].sentPosition = sentPos;
						tokens[i - 1].wordPosition = 0;
						accumWordPos = 0;
					}
				}
			}

			if (!nestedSentEnd && !nestedEnd && t.tag == POSTag::sso && t.pairedToken != (uint32_t)-1)
			{
				if (!hasSentences(&tokens[i], &tokens[t.pairedToken]))
				{
					nestedEnd = t.pairedToken;
					subSentPos = 0;
				}
				else if ((t.pairedToken + 1 < tokens.size() && isNestedRight(tokens[t.pairedToken + 1]))
						|| (i > 0 && isNestedLeft(tokens[i - 1])))
				{
					nestedSentEnd = t.pairedToken;
					subSentPos = accumSubSent;
				}
			}
			else if (nestedSentEnd && i > nestedSentEnd)
			{
				nestedSentEnd = 0;
				subSentPos = 0;
			}
			else if (nestedEnd && i >= nestedEnd)
			{
				nestedEnd = 0;
				subSentPos = 0;
			}

			while (nlPos < newlines.size() && newlines[nlPos] < t.position) nlPos++;
			
			t.lineNumber = (uint32_t)nlPos;
			if (nlPos > lastNlPos + 1 && sentPos == lastSentPos && !nestedSentEnd)
			{
				sentPos++;
			}
			t.sentPosition = sentPos;
			t.subSentPosition = (i == nestedSentEnd || i == tokens[nestedSentEnd].pairedToken) ? 0 : subSentPos;
			
			if (sentPos != lastSentPos)
			{
				accumWordPos = 0;
				accumSubSent = 1;
			}
			else if (t.wordPosition != lastWordPos)
			{
				accumWordPos++;
			}
			lastWordPos = t.wordPosition;
			t.wordPosition = accumWordPos;

			lastSentPos = sentPos;
			lastNlPos = nlPos;
		}
	}

	vector<pair<size_t, size_t>> Kiwi::splitIntoSents(const u16string& str, Match matchOptions, TokenResult* tokenizedResultOut) const
	{
		vector<pair<size_t, size_t>> ret;
		uint32_t sentPos = -1;
		auto res = analyze(str, matchOptions);
		for (auto& t : res.first)
		{
			if (t.sentPosition != sentPos)
			{
				ret.emplace_back(t.position, (size_t)t.position + t.length);
				sentPos = t.sentPosition;
			}
			else
			{
				ret.back().second = (size_t)t.position + t.length;
			}
		}
		if (tokenizedResultOut) *tokenizedResultOut = move(res);
		return ret;
	}

	vector<pair<size_t, size_t>> Kiwi::splitIntoSents(const string& str, Match matchOptions, TokenResult* tokenizedResultOut) const
	{
		vector<size_t> bytePositions;
		u16string u16str = utf8To16(str, bytePositions);
		bytePositions.emplace_back(str.size());
		vector<pair<size_t, size_t>> ret = splitIntoSents(u16str, matchOptions, tokenizedResultOut);
		for (auto& r : ret)
		{
			r.first = bytePositions[r.first];
			r.second = bytePositions[r.second];
		}
		return ret;
	}

	/**
	* @brief 주어진 문자열에 나타난 개별 문자들에 대해 어절번호(wordPosition) 생성하여 반환한다.
	* @details 문자열의 길이와 동일한 크기의 std::vector<uint16_t>를 생성한 뒤, 문자열 내 개별 문자가
	* 나타난 인덱스와 동일한 위치에 각 문자에 대한 어절번호(wordPosition)를 기록한다.
	* 어절번호는 단순히 각 어절의 등장순서로부터 얻어진다.
	* 예를 들어, '나는 학교에 간다'라는 문자열의 경우 '나는'이 첫 번째 어절이므로 여기에포함된
	* 개별 문자인 '나'와 '는'의 어절번호는 0이다. 마찬가지로 '학교에'에 포함된 문자들의 어절번호는 1, 
	* '간다'에 포함된 문자들의 어절번호는 2이다.
	* 따라서 최종적으로 문자열 '나는 학교에 간다'로부터 {0, 0, 0, 1, 1, 1, 1, 2, 2}의 어절번호가
	* 생성된다.
	*
	*/
	template<class It>
	void getWordPositions(Vector<uint16_t>& out, It first, It last)
	{
		out.resize(distance(first, last));
		uint32_t position = 0;
		bool continuousSpace = false;

		for (size_t i = 0; first != last; ++first, ++i)
		{
			out[i] = position;

			if (isSpace(*first))
			{
				if (!continuousSpace) ++position;
				continuousSpace = true;
			}
			else
			{
				continuousSpace = false;
			}
		}
	}

	inline void concatTokens(TokenInfo& dest, const TokenInfo& src, POSTag tag)
	{
		dest.tag = tag;
		dest.morph = nullptr;
		dest.length = (uint16_t)(src.position + src.length - dest.position);
		dest.str += src.str;
	}

	template<class TokenInfoIt>
	TokenInfoIt joinAffixTokens(TokenInfoIt first, TokenInfoIt last, Match matchOptions)
	{
		if (!(matchOptions & (Match::joinNounPrefix 
							| Match::joinNounSuffix 
							| Match::joinVerbSuffix 
							| Match::joinAdjSuffix 
							| Match::joinAdvSuffix 
							| Match::mergeSaisiot
							| Match::joinParticleYo))) return last;
		if (std::distance(first, last) < 2) return last;

		auto next = first;
		++next;
		while (next != last)
		{
			TokenInfo& current = *first;
			TokenInfo& nextToken = *next;

			// XPN + (NN. | SN) => (NN. | SN)
			if (!!(matchOptions & Match::joinNounPrefix) 
				&& current.tag == POSTag::xpn 
				&& (isNNClass(nextToken.tag) || nextToken.tag == POSTag::sn)
			)
			{
				concatTokens(current, nextToken, nextToken.tag);
				++next;
			}
			// (NN. | SN) + XSN => (NN. | SN)
			else if (!!(matchOptions & Match::joinNounSuffix)
				&& nextToken.tag == POSTag::xsn
				&& (isNNClass(current.tag) || current.tag == POSTag::sn)
			)
			{
				concatTokens(current, nextToken, current.tag);
				++next;
			}
			// (NN. | XR) + XSV => VV
			else if (!!(matchOptions & Match::joinVerbSuffix)
				&& clearIrregular(nextToken.tag) == POSTag::xsv
				&& (isNNClass(current.tag) || current.tag == POSTag::xr)
			)
			{
				concatTokens(current, nextToken, setIrregular(POSTag::vv, isIrregular(nextToken.tag)));
				++next;
			}
			// (NN. | XR) + XSA => VA
			else if (!!(matchOptions & Match::joinAdjSuffix)
				&& clearIrregular(nextToken.tag) == POSTag::xsa
				&& (isNNClass(current.tag) || current.tag == POSTag::xr)
			)
			{
				concatTokens(current, nextToken, setIrregular(POSTag::va, isIrregular(nextToken.tag)));
				++next;
			}
			// (NN. | XR) + XSM => MAG
			else if (!!(matchOptions & Match::joinAdvSuffix)
				&& nextToken.tag == POSTag::xsm
				&& (isNNClass(current.tag) || current.tag == POSTag::xr)
				)
			{
				concatTokens(current, nextToken, POSTag::mag);
				++next;
			}
			// NN. + Z_SIOT + NN. => NN
			else if (!!(matchOptions & Match::mergeSaisiot)
				&& nextToken.tag == POSTag::z_siot
				&& isNNClass(current.tag)
				&& next + 1 != last
				&& isNNClass((next + 1)->tag))
			{
				current.str.back() += (0x11BA - 0x11A7);
				concatTokens(current, *(next + 1), POSTag::nng);
				++next;
				++next;
			}
			// (EC | EF) + JX(요) => (EC | EF)
			else if (!!(matchOptions & Match::joinParticleYo)
				&& nextToken.tag == POSTag::jx
				&& nextToken.morph && *nextToken.morph->kform == u"요"
				&& (current.tag == POSTag::ec || current.tag == POSTag::ef))
			{
				concatTokens(current, nextToken, current.tag);
				++next;
			}
			else
			{
				++first;
				if (first != next) *first = std::move(*next);
				++next;
			}
		}
		return ++first;
	}

	inline void updateTokenInfoScript(TokenInfo& info)
	{
		if (!(info.tag == POSTag::sl || info.tag == POSTag::sh || info.tag == POSTag::sw || info.tag == POSTag::w_emoji)) return;
		if ((info.morph && info.morph->kform && !info.morph->kform->empty())) return;
		if (info.str.empty()) return;
		char32_t c = info.str[0];
		if (isHighSurrogate(c))
		{
			c = mergeSurrogate(c, info.str[1]);
		}
		info.script = chr2ScriptType(c);
		if (info.script == ScriptType::latin)
		{
			info.tag = POSTag::sl;
		}
	}

	inline void toCompatibleJamo(u16string& str)
	{
		for (auto& c : str)
		{
			c = toCompatibleHangulConsonant(c);
		}
	}

	inline void insertPathIntoResults(
		vector<TokenResult>& ret, 
		Vector<PackedState>& spStatesByRet,
		Vector<uint8_t>& oovTotalCnt,
		const Vector<PathResult>& pathes,
		size_t topN, 
		Match matchOptions,
		bool integrateAllomorph,
		const Vector<uint32_t>& positionTable,
		const Vector<uint16_t>& wordPositions,
		const PretokenizedSpanGroup& pretokenizedGroup,
		const Vector<uint32_t>& nodeInWhichPretokenized
	)
	{
		Vector<size_t> parentMap;

		uint32_t oovCntArenaMinPtr = -1;
		if (!!(matchOptions & Match::oovTotalConsistency))
		{
			for (auto& p : pathes)
			{
				oovCntArenaMinPtr = min(oovCntArenaMinPtr, p.curState.oovCntArenaPtr());
			}
		}

		if (ret.empty())
		{
			const size_t n = min(pathes.size(), topN * 2);
			ret.resize(n);
			spStatesByRet.resize(n);
			parentMap.resize(n);
			iota(parentMap.begin(), parentMap.end(), 0);
		}
		else
		{
			UnorderedMap<PackedState, uint32_t> prevParents;
			Vector<uint8_t> selectedPathes(pathes.size());
			for (size_t i = 0; i < ret.size(); ++i)
			{
				auto pred = [&](const PathResult& p)
				{
					return p.prevState == spStatesByRet[i];
				};
				size_t parent = find_if(pathes.begin() + prevParents[spStatesByRet[i]], pathes.end(), pred) - pathes.begin();
				if (parent >= pathes.size() && prevParents[spStatesByRet[i]])
				{
					parent = find_if(pathes.begin(), pathes.end(), pred) - pathes.begin();
				}

				parentMap.emplace_back(parent);
				if (parent < pathes.size())
				{
					selectedPathes[parent] = 1;
					prevParents[spStatesByRet[i]] = parent + 1;
				}
			}

			for (size_t i = 0; i < pathes.size(); ++i)
			{
				if (selectedPathes[i]) continue;
				size_t parent = find(spStatesByRet.begin(), spStatesByRet.end(), pathes[i].prevState) - spStatesByRet.begin();
				
				if (parent < ret.size())
				{
					ret.emplace_back();
					ret.back().first.reserve(ret[parent].first.size() + pathes[i].path.size());
					// insert, not =, so the room reserved for the path is kept.
					ret.back().first.insert(ret.back().first.end(), ret[parent].first.begin(), ret[parent].first.end());
					ret.back().second = ret[parent].second;
					spStatesByRet.push_back(spStatesByRet[parent]);
					parentMap.emplace_back(i);
				}
				else
				{
					// Here is unreachable branch in the normal case.
					throw std::runtime_error{ "" };
				}
			}
		}

		UnorderedMap<PackedState, uint32_t> spStateCnt;
		size_t validTarget = 0;
		for (size_t i = 0; i < ret.size(); ++i)
		{
			if (parentMap[i] < pathes.size() && spStateCnt[pathes[parentMap[i]].curState] < topN)
			{
				if (validTarget != i) ret[validTarget] = move(ret[i]);
			}
			else
			{
				continue;
			}

			auto& r = pathes[parentMap[i]];
			auto& rarr = ret[validTarget].first;

			const KString* prevMorph = nullptr;
			for (auto& s : r.path)
			{
				if (!s.str.empty() && s.str[0] == ' ') continue;
				u16string joined;
				do
				{
					if (!integrateAllomorph)
					{
						if (POSTag::ep <= s.morph->tag && s.morph->tag <= POSTag::etm)
						{
							if ((*s.morph->kform)[0] == u'\uC5B4') // 어
							{
								if (prevMorph && prevMorph[0].back() == u'\uD558') // 하
								{
									joined = joinHangul(u"\uC5EC" + s.morph->kform->substr(1)); // 여
									break;
								}
								else if (FeatureTestor::isMatched(prevMorph, CondPolarity::positive))
								{
									joined = joinHangul(u"\uC544" + s.morph->kform->substr(1)); // 아
									break;
								}
							}
						}
					}
					joined = joinHangul(s.str.empty() ? *s.morph->kform : s.str);
				} while (0);
				
				if (!!(matchOptions & Match::compatibleJamo))
				{
					toCompatibleJamo(joined);
				}

				rarr.emplace_back(joined, s.morph->tag);
				auto& token = rarr.back();
				token.morph = within(s.morph, pretokenizedGroup.morphemes) ? nullptr : s.morph;
				size_t beginPos = (upper_bound(positionTable.begin(), positionTable.end(), s.begin) - positionTable.begin()) - 1;
				size_t endPos = lower_bound(positionTable.begin(), positionTable.end(), s.end) - positionTable.begin();
				token.position = (uint32_t)beginPos;
				token.length = (uint16_t)(endPos - beginPos);
				token.score = s.wordScore;
				token.typoCost = s.typoCost;
				token.typoFormId = s.typoFormId;
				token.senseId = s.morph->senseId;
				if ((s.morph->tag == POSTag::nng || s.morph->tag == POSTag::nnp) && !s.str.empty())
				{
					token.senseId = -1; // OOV인 경우에는 senseId를 -1로 설정
				}
				updateTokenInfoScript(token);
				token.dialect = s.morph->dialect;
				auto ptId = nodeInWhichPretokenized[s.nodeId] + 1;
				if (ptId)
				{
					token.typoFormId = ptId;
				}

				// Token의 시작위치(position)을 이용해 Token이 포함된 어절번호(wordPosition)를 얻음
				token.wordPosition = wordPositions[token.position];
				prevMorph = s.morph->kform;
			}
			rarr.erase(joinAffixTokens(rarr.begin(), rarr.end(), matchOptions), rarr.end());
			ret[validTarget].second += r.score;
			spStatesByRet[validTarget] = r.curState;
			spStateCnt[r.curState]++;
			validTarget++;
		}
		Vector<size_t> idx(validTarget);
		iota(idx.begin(), idx.end(), 0);
		sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return ret[a].second > ret[b].second; });
		
		Vector<TokenResult> sortedRet;
		Vector<PackedState> sortedSpStatesByRet;
		const size_t maxCands = min(topN * 2, validTarget);
		for (size_t i = 0; i < maxCands; ++i)
		{
			sortedRet.emplace_back(move(ret[idx[i]]));
			sortedSpStatesByRet.emplace_back(spStatesByRet[idx[i]]);
		}
		ret.clear();
		spStatesByRet.clear();
		for (size_t i = 0; i < maxCands; ++i)
		{
			ret.emplace_back(move(sortedRet[i]));
			if (!!(matchOptions & Match::oovTotalConsistency))
			{
				spStatesByRet.emplace_back(sortedSpStatesByRet[i].specialState(), sortedSpStatesByRet[i].oovCntArenaPtr() - oovCntArenaMinPtr);
			}
			else
			{
				spStatesByRet.emplace_back(sortedSpStatesByRet[i]);
			}
		}
		if (!!(matchOptions & Match::oovTotalConsistency)) oovTotalCnt.erase(oovTotalCnt.begin(), oovTotalCnt.begin() + oovCntArenaMinPtr);
	}

	inline void makePretokenizedSpanGroup(
		PretokenizedSpanGroup& ret, 
		const std::vector<PretokenizedSpan>& pretokenized, 
		const Vector<uint32_t>& positionTable, 
		const KString& normStr,
		FnFindForm findForm,
		const utils::FrozenTrie<kchar_t, const Form*>& formTrie,
		const Form* formData
	)
	{
		if (pretokenized.empty()) return;
		size_t totTokens = 0;
		for (auto& s : pretokenized)
		{	
			if (s.tokenization.size() <= 1)
			{
				totTokens++;
			}
			else
			{
				totTokens += s.tokenization.size() + 1;
			}
		}

		ret.spans.reserve(pretokenized.size());
		ret.formStrs.reserve(totTokens);
		ret.forms.reserve(pretokenized.size());
		ret.candidates.reserve(pretokenized.size() * 2); // Form마다 후보는 둘 이하
		ret.morphemes.reserve(totTokens);
		{
			size_t numSlots = 0;
			for (auto& s : pretokenized) if (s.tokenization.size() > 1) numSlots += Morpheme::chunkSlots(s.tokenization.size());
			ret.chunks.reserve(numSlots);
		}
		for (auto& s : pretokenized)
		{
			ret.spans.emplace_back();
			auto& span = ret.spans.back();
			span.begin = positionTable[s.begin];
			span.end = positionTable[s.end];

			if (s.tokenization.empty())
			{
				auto formStr = normStr.substr(span.begin, span.end - span.begin);
				span.form = findForm(formTrie, formData, formStr); // reuse the predefined form & morpheme
				if (!span.form) // or use a fallback form
				{
					span.form = formTrie.value((size_t)POSTag::nnp);
				}
			}
			else if (s.tokenization.size() == 1)
			{
				auto formStr = normalizeHangul(s.tokenization[0].form);
				auto* tform = findForm(formTrie, formData, formStr); 
				if (tform && tform->candidate().size() == 1 &&
					areTagsEqual(tform->candidate()[0]->tag, s.tokenization[0].tag, !!s.tokenization[0].inferRegularity))
					// reuse the predefined form & morpheme
				{
					span.form = tform;
				}
				else  // or add a new form & morpheme
				{
					ret.forms.emplace_back();
					auto& form = ret.forms.back();
					form.form = move(formStr);
					const Morpheme* foundMorph[2] = { nullptr, nullptr };
					if (tform)
					{
						size_t i = 0;
						for (auto m : tform->candidate())
						{
							if (areTagsEqual(m->tag, s.tokenization[0].tag, s.tokenization[0].inferRegularity))
							{
								foundMorph[i++] = m;
								if (i >= 2) break;
							}
						}
					}

					const size_t firstCand = ret.candidates.size();
					if (foundMorph[0])
					{
						ret.candidates.emplace_back(foundMorph[0]);
						if (foundMorph[1])
						{
							ret.candidates.emplace_back(foundMorph[1]);
						}
					}
					else
					{
						ret.morphemes.emplace_back();
						auto& morph = ret.morphemes.back();
						morph.kform = &form.form;
						morph.tag = s.tokenization[0].tag;
						morph.vowel = CondVowel::none;
						morph.polar = CondPolarity::none;
						morph.complex = 0;
						morph.saisiot = 0;
						morph.lmMorphemeId = getDefaultMorphemeId(s.tokenization[0].tag);
						ret.candidates.emplace_back(&morph);
					}
					form.setCandidates(ret.candidates.data() + firstCand, ret.candidates.size() - firstCand);
					span.form = &form;
				}
			}
			else
			{
				ret.forms.emplace_back();
				auto& form = ret.forms.back();
				ret.morphemes.emplace_back();
				auto& morph = ret.morphemes.back();
				morph.vowel = CondVowel::none;
				morph.polar = CondPolarity::none;
				morph.complex = 0;
				morph.saisiot = 0;
				auto [chunkMorphs, chunkPositions] = morph.allocChunks(ret.chunks, s.tokenization.size());
				for (size_t i = 0; i < s.tokenization.size(); ++i)
				{
					auto& t = s.tokenization[i];
					auto formStr = normalizeHangul(t.form);
					auto* tform = findForm(formTrie, formData, formStr);
					const Morpheme* foundMorph = nullptr;
					if (tform)
					{
						for (auto m : tform->candidate())
						{
							if (m->tag == t.tag)
							{
								foundMorph = m;
								break;
							}
						}
					}

					if (!foundMorph)
					{
						ret.morphemes.emplace_back();
						auto& cmorph = ret.morphemes.back();
						ret.formStrs.emplace_back(move(formStr));
						cmorph.kform = &ret.formStrs.back();
						cmorph.vowel = CondVowel::none;
						cmorph.polar = CondPolarity::none;
						cmorph.complex = 0;
						cmorph.saisiot = 0;
						cmorph.tag = t.tag;
						cmorph.lmMorphemeId = getDefaultMorphemeId(t.tag);
						foundMorph = &cmorph;
					}

					chunkMorphs[i] = foundMorph;
					chunkPositions[i] = make_pair(positionTable[t.begin + s.begin] - span.begin, positionTable[t.end + s.begin] - span.begin);
				}

				ret.candidates.emplace_back(&morph);
				form.setCandidates(&ret.candidates.back(), 1);
				span.form = &form;
			}
		}
		
		sort(ret.spans.begin(), ret.spans.end(), [](const PretokenizedSpanGroup::Span& a, const PretokenizedSpanGroup::Span& b)
		{
			return a.begin < b.begin;
		});

		for (size_t i = 1; i < ret.spans.size(); ++i)
		{
			if (ret.spans[i - 1].end > ret.spans[i].begin) throw invalid_argument{ "`PretokenizedSpan`s should not have overlapped ranges." };
		}
	}

	inline void findPretokenizedGroupOfNode(Vector<uint32_t>& ret, const Vector<KGraphNode>& nodes,
		const PretokenizedSpanGroup::Span* first, const PretokenizedSpanGroup::Span* last)
	{
		ret.clear();
		auto* cur = first;
		for (size_t i = 0; i < nodes.size(); ++i)
		{
			while (cur != last && nodes[i].startPos >= cur->end) ++cur;
			if (cur == last) break;

			if (cur->begin <= nodes[i].startPos && nodes[i].endPos <= cur->end)
			{
				ret.emplace_back(cur - first);
			}
			else
			{
				ret.emplace_back(-1);
			}
		}
		ret.resize(nodes.size(), -1);
	}

	void KiwiConfig::validate() const
	{
		if (cutOffThreshold < 0)
		{
			throw invalid_argument{ "`cutOffThreshold` should be >= 0." };
		}

		if (oovRuleScale < 0)
		{
			throw invalid_argument{ "`oovRuleScale` should be >= 0." };
		}

		if (oovGlobalWeight <= 0)
		{
			throw invalid_argument{ "`oovGlobalWeight` should be > 0." };
		}

		if (oovLocalWeight <= 0)
		{
			throw invalid_argument{ "`oovLocalWeight` should be > 0." };
		}

		if (oovGlobalMinFreq <= 0)
		{
			throw invalid_argument{ "`oovGlobalMinFreq` should be >= 0." };
		}

		if (spacePenalty <= 0)
		{
			throw invalid_argument{ "`spacePenalty` should be > 0." };
		}

		if (typoCostWeight <= 0)
		{
			throw invalid_argument{ "`typoCostWeight` should be > 0." };
		}

		if (maxUnkFormSize <= 0)
		{
			throw invalid_argument{ "`maxUnkFormSize` should be > 0." };
		}
	}

	std::ostream* logStream = &std::cerr;
	int doLogging = 0;

	template<class ChunkNodesCallback>
	vector<TokenResult> Kiwi::analyzeWithNodes(const u16string& str, size_t topN, AnalyzeOption option,
		const vector<PretokenizedSpan>& pretokenized, const KiwiConfig& config,
		KString& normalizedStr, Vector<uint32_t>& positionTable, ChunkNodesCallback&& chunkNodesCallback) const
	{
		thread_local PretokenizedSpanGroup pretokenizedGroup;

		normalizedStr.clear();
		positionTable.clear();
		pretokenizedGroup.clear();
		normalizeHangulWithPosition(str.begin(), str.end(), back_inserter(normalizedStr), back_inserter(positionTable));

		if (!!(option.match & Match::normalizeCoda)) normalizeCoda(normalizedStr.begin(), normalizedStr.end());

		if (topN && (option.match & Match::oovMask) >= Match::oovChrModel && !nounChrMdl)
		{
			throw invalid_argument{ "`oovChrModel` option is set but the character-level noun model is not loaded." };
		}

		if (option.allowedDialects != Dialect::standard && option.typoTransformer == nullptr)
		{
			option.typoTransformer = getDefaultPreparedTypoSet(DefaultTypoSet::dialect);
			option.typoThreshold = 2.5f;
		}

		makePretokenizedSpanGroup(
			pretokenizedGroup, 
			pretokenized, 
			positionTable, 
			normalizedStr, 
			reinterpret_cast<FnFindForm>(dfFindForm), 
			formTrie,
			forms.data()
		);

		// 분석할 문장에 포함된 개별 문자에 대해 어절번호를 생성한다
		thread_local Vector<uint16_t> wordPositions;
		wordPositions.clear();
		getWordPositions(wordPositions, str.begin(), str.end());
		
		SubstringCounter substringCounter;
		if (topN && ((option.match & Match::oovMask) >= Match::oovChrFreqModel || !!(option.match & Match::oovTotalConsistency)))
		{
			thread_local Vector<char16_t> filteredStr;
			filteredStr.clear();
			filteredStr.reserve(normalizedStr.size());
			for (size_t i = 0; i < normalizedStr.size(); ++i)
			{
				auto c = normalizedStr[i];
				const POSTag chrType = identifySpecialChr(c);
				switch (chrType)
				{
				case POSTag::unknown:
				case POSTag::sf:
				case POSTag::sp:
				case POSTag::ss:
				case POSTag::sso:
				case POSTag::ssc:
				case POSTag::se:
				case POSTag::so:
				case POSTag::sw:
				case POSTag::sb:
					c = u' ';
					break;
				}
				filteredStr.emplace_back(c);
			}
			substringCounter = SubstringCounter{ filteredStr.data(), filteredStr.size() };
		}

		vector<TokenResult> ret;
		Vector<PackedState> spStatesByRet;
		thread_local Vector<KGraphNode> nodes;
		thread_local Vector<uint32_t> nodeInWhichPretokenized;
		thread_local UnorderedMap<U16StringView, size_t> oovTotalMap;
		thread_local UnorderedMap<OovOrForm,Vector<uint16_t>> oovPrefixLists;
		thread_local Vector<uint8_t> oovTotalCnt;
		oovTotalMap.clear();
		oovPrefixLists.clear();
		oovTotalCnt.clear();
		const auto* pretokenizedFirst = pretokenizedGroup.spans.data();
		const auto* pretokenizedLast = pretokenizedFirst + pretokenizedGroup.spans.size();
		size_t splitEnd = 0;
		chrono::steady_clock::time_point startTime;
		while (splitEnd < normalizedStr.size())
		{
			if (doLogging)
			{
				startTime = chrono::steady_clock::now();
			}
			nodes.clear();
			auto* pretokenizedPrev = pretokenizedFirst;
			const size_t newSplitEnd = (*reinterpret_cast<FnSplitByTrie>(dfSplitByTrie))(
				nodes,
				forms.data(),
				typoPtrs.data(),
				formTrie,
				U16StringView{ normalizedStr.data() + splitEnd, normalizedStr.size() - splitEnd },
				splitEnd,
				option.match,
				option.allowedDialects,
				config.maxUnkFormSize,
				config.maxUnkFormSizeFollowedByJClass,
				config.spaceTolerance,
				option.typoTransformer,
				option.typoThreshold,
				continualTypoCost,
				lengtheningTypoCost,
				pretokenizedFirst,
				pretokenizedLast
			);

			if (doLogging)
			{
				auto input = utf16To8(joinHangul(normalizedStr.substr(splitEnd, newSplitEnd - splitEnd)));
				*logStream << "Input: " << input << "\nNodes: " << nodes.size() << endl;
			}
			splitEnd = newSplitEnd;

			if (nodes.size() <= 2) continue;
			chunkNodesCallback(nodes);
			if (!topN) continue;
			findPretokenizedGroupOfNode(nodeInWhichPretokenized, nodes, pretokenizedPrev, pretokenizedFirst);

			Vector<PathResult> res = (*reinterpret_cast<FnFindBestPath>(dfFindBestPath))(FindBestPathArgs{
				this,
				config,
				spStatesByRet,
				normalizedStr,
				nodes.data(),
				nodes.size(),
				topN,
				(size_t)(option.match & Match::oovMask),
				!!(option.match & Match::oovTotalConsistency) ? &oovTotalMap : nullptr,
				!!(option.match & Match::oovTotalConsistency) ? &oovTotalCnt : nullptr,
				!!(option.match & Match::oovTotalConsistency) ? &oovPrefixLists : nullptr,
				!!(option.match & Match::oovTotalConsistency) ? &ret : nullptr,
				option.openEnding && splitEnd == normalizedStr.size(),
				!!(option.match & Match::splitComplex),
				!!(option.match & Match::splitSaisiot),
				!!(option.match & Match::mergeSaisiot),
				option.blocklist,
				option.allowedDialects,
				option.dialectCost,
				(option.match & Match::oovMask) >= Match::oovChrFreqModel ? &substringCounter : nullptr
			});
			insertPathIntoResults(ret, spStatesByRet, oovTotalCnt,
				res, topN, option.match, config.integrateAllomorph, 
				positionTable, wordPositions, pretokenizedGroup, nodeInWhichPretokenized);
			if (doLogging)
			{
				auto duration = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - startTime).count();
				*logStream << "Time: " << duration << "ms\n" << endl;
			}
		}

		if (!topN) return ret;
		sort(ret.begin(), ret.end(), [](const TokenResult& a, const TokenResult& b)
		{
			return a.second > b.second;
		});
		if (ret.size() > topN) ret.erase(ret.begin() + topN, ret.end());
		
		auto newlines = allNewLinePositions(str);
		for (auto& r : ret)
		{
			fillPairedTokenInfo(r.first);
			fillSentLineInfo(r.first, newlines);
		}

		if (ret.empty()) ret.emplace_back();
		return ret;
	}

	vector<TokenResult> Kiwi::analyze(const u16string& str, size_t topN, AnalyzeOption option,
		const vector<PretokenizedSpan>& pretokenized,
		const optional<KiwiConfig>& overrideConfig
	) const
	{
		if (topN == 0)
		{
			throw invalid_argument{ "`topN` should be > 0." };
		}

		thread_local KString normalizedStr;
		thread_local Vector<uint32_t> positionTable;
		return analyzeWithNodes(str, topN, option, pretokenized, overrideConfig.value_or(globalConfig),
			normalizedStr, positionTable, [](const Vector<KGraphNode>&) {});
	}

	namespace
	{
		// analyze와 같은 기준: identifySpecialChr로 판정하고, 라틴 문자는 updateTokenInfoScript처럼 SL로 본다
		inline POSTag chrTag(const u16string& str, size_t k)
		{
			char32_t c = str[k];
			if (isHighSurrogate(str[k]) && k + 1 < str.size() && isLowSurrogate(str[k + 1])) c = mergeSurrogate(str[k], str[k + 1]);
			else if (isLowSurrogate(str[k]) && k > 0 && isHighSurrogate(str[k - 1])) c = mergeSurrogate(str[k - 1], str[k]);
			if (c < 0x10000 && (isCompatibleHangulConsonant((char16_t)c) || isHangulVowel((char16_t)c))) return POSTag::max; // 자모 구간은 analyze 노드에서 온다
			const POSTag t = identifySpecialChr(c);
			if ((t == POSTag::sh || t == POSTag::sw) && chr2ScriptType(c) == ScriptType::latin) return POSTag::sl;
			if (t == POSTag::sh && !isChineseChr(c)) return POSTag::sw;
			return t;
		}

		// 간선의 정규화 위치를 상태로 바꾸고 상태 위치 표를 채운다. 같은 간선이 여럿이면 비용이 가장 작은 것만 남긴다
		void finalizeMorphGraph(MorphGraph& graph, const KString& normalizedStr, const Vector<uint32_t>& positionTable)
		{
			// 상태는 정규화된 문자열에서 공백을 건너뛴 위치이다. 같은 위치에서 끝나는 노드들은 Kiwi에서도 모두 이어질 수 있다.
			// 앞 공백을 건너뛴 시작 위치는 0으로 둔다
			const size_t n = normalizedStr.size();
			Vector<uint32_t> stateOf(n + 1);
			stateOf[n] = (uint32_t)n;
			for (size_t i = n; i-- > 0;) stateOf[i] = isSpace(normalizedStr[i]) ? stateOf[i + 1] : (uint32_t)i;
			const size_t start = stateOf[0];
			for (size_t i = 0; i <= n && stateOf[i] == start; ++i) stateOf[i] = 0;

			auto& edges = graph.edges;
			for (auto& e : edges)
			{
				e.from = stateOf[e.from];
				e.to = stateOf[e.to];
			}

			// positionTable[i]는 원문 i번째 글자의 정규화 위치이다. 간선은 from에서 시작해 to 앞의 공백 직전에서 끝난다.
			graph.numStates = (uint32_t)n + 1;
			graph.statePositions.resize(graph.numStates);
			for (size_t s = 0; s <= n; ++s)
			{
				const size_t b = s ? s : start;
				size_t en = s;
				while (en > 0 && isSpace(normalizedStr[en - 1])) --en;
				graph.statePositions[s].first = (uint32_t)((upper_bound(positionTable.begin(), positionTable.end(), b) - positionTable.begin()) - 1);
				graph.statePositions[s].second = (uint32_t)(lower_bound(positionTable.begin(), positionTable.end(), en) - positionTable.begin());
			}

			sort(edges.begin(), edges.end(), [](const MorphGraphEdge& a, const MorphGraphEdge& b)
			{
				return tie(a.from, a.to, a.morphs, a.typoCost) < tie(b.from, b.to, b.morphs, b.typoCost);
			});
			edges.erase(unique(edges.begin(), edges.end(), [](const MorphGraphEdge& a, const MorphGraphEdge& b)
			{
				return a.from == b.from && a.to == b.to && a.morphs == b.morphs;
			}), edges.end());
		}
	}

	MorphGraph Kiwi::buildMorphGraph(const u16string& str, AnalyzeOption option,
		const MorphGraphOption& graphOption,
		const optional<KiwiConfig>& overrideConfig) const
	{
		const KiwiConfig config = overrideConfig.value_or(globalConfig);
		const size_t maxUnkSpanSize = graphOption.maxUnkSpanSize, maxUnkSpanSizeWithSpace = graphOption.maxUnkSpanSizeWithSpace;
		KString normalizedStr;
		Vector<uint32_t> positionTable;
		MorphGraph ret;
		// 간선의 from, to는 우선 정규화 위치로 두고, finalizeMorphGraph에서 상태로 바꾼다
		auto& edges = ret.edges;

		const Morpheme* morphFirst = morphemes.data();
		const size_t langVocabSize = langMdl->vocabSize();
		const auto unifyMorpheme = [&](const Morpheme* morph)
		{
			if (!within(morph, morphFirst, morphFirst + langVocabSize) || morph->combined) return morph;
			return morphFirst + morph->lmMorphemeId;
		};

		using Morphs = vector<pair<u16string, POSTag>>;
		const auto appendMorph = [&](Morphs& out, U16StringView form, POSTag tag)
		{
			auto joined = joinHangul(form.begin(), form.end());
			if (!!(option.match & Match::compatibleJamo)) toCompatibleJamo(joined);
			out.emplace_back(move(joined), tag);
		};

		// 형태소 분석과 같은 조각·노드에서 간선을 만든다
		const auto addNodeEdges = [&](const Vector<KGraphNode>& nodes)
		{
			// 노드마다 그 노드로 끝나는 간선 (번호, 시작 위치). 덧붙은 받침(z_coda)을 앞 간선에 붙일 때 쓴다.
			thread_local Vector<Vector<pair<size_t, uint32_t>>> edgesByLastNode;
			if (edgesByLastNode.size() < nodes.size()) edgesByLastNode.resize(nodes.size());
			for (size_t i = 0; i < nodes.size(); ++i) edgesByLastNode[i].clear();
			Vector<pair<size_t, const Morpheme*>> zCodaNodes;
			const auto addNodeEdge = [&](size_t last, uint32_t begin, uint32_t end, float typoCost, Morphs&& morphs)
			{
				edges.push_back({ begin, end, typoCost, move(morphs) });
				edgesByLastNode[last].emplace_back(edges.size() - 1, begin);
			};
			for (size_t i = 1; i + 1 < nodes.size(); ++i)
			{
				auto& node = nodes[i];
				if (!node.form)
				{
					Morphs morphs;
					appendMorph(morphs, node.uform, POSTag::unknown);
					edges.push_back({ node.startPos, node.endPos, node.typoCost, move(morphs) });
					continue;
				}
				for (auto m : node.form->candidate())
				{
					if (!isDialectAllowed(m->dialect, option.allowedDialects) || (m->tag == POSTag::unknown && m->chunks().empty())) continue;
					if (m->tag == POSTag::z_coda)
					{
						zCodaNodes.emplace_back(i, m);
						continue;
					}
					if (m->combineSocket && m->isSingle()) continue;
					if (!m->combineSocket)
					{
						if (m->isSingle())
						{
							const auto u = unifyMorpheme(m);
							// 원문 표면이 있으면 그것을, 없으면 형태소의 형태를 쓴다
							const U16StringView form = node.uform.empty() ? U16StringView{ *u->kform } : node.uform;
							if (!node.uform.empty() && isPunctClass(u->tag))
							{
								Morphs base;
								appendMorph(base, form, POSTag::sf);
								for (auto t = POSTag::sf; t <= POSTag::sw; t = (POSTag)((uint8_t)t + 1))
								{
									Morphs morphs = base;
									morphs[0].second = t;
									addNodeEdge(i, node.startPos, node.endPos, node.typoCost, move(morphs));
								}
								continue;
							}
							Morphs morphs;
							appendMorph(morphs, form, u->tag);
							addNodeEdge(i, node.startPos, node.endPos, node.typoCost, move(morphs));
						}
						else
						{
							Morphs morphs;
							for (auto ch : m->chunks())
							{
								const auto uc = unifyMorpheme(ch);
								appendMorph(morphs, *uc->kform, uc->tag);
							}
							addNodeEdge(i, node.startPos, node.endPos, node.typoCost, move(morphs));
						}
						continue;
					}
					// 앞 노드의 분할 형태소(더/V)와 이 노드(ㅂ/V + 어/EC → 워)를 합쳐 덥/VA + 어/EC를 내는 간선
					for (auto prev = node.getPrev(); prev; prev = prev->getSibling())
					{
						if (!prev->form) continue;
						for (auto pm : prev->form->candidate())
						{
							if (pm->combineSocket != m->combineSocket || !pm->isSingle() || !isDialectAllowed(pm->dialect, option.allowedDialects)) continue;
							Morphs fused;
							appendMorph(fused, *pm->getCombined()->kform, pm->getCombined()->tag);
							for (size_t ch = 1; ch < m->chunks().size(); ++ch)
							{
								const auto uc = unifyMorpheme(m->chunks()[ch]);
								appendMorph(fused, *uc->kform, uc->tag);
							}
							addNodeEdge(i, prev->startPos, node.endPos, prev->typoCost + node.typoCost, move(fused));
						}
					}
				}
			}

			// 덧붙은 받침은 Kiwi의 경로 평가처럼 조사·어미로 끝나는 앞 간선에 붙인다. 자모 편집 오타 노드 뒤에는 붙이지 않는다.
			for (auto [zi, zm] : zCodaNodes)
			{
				auto& z = nodes[zi];
				const KString& coda = *unifyMorpheme(zm)->kform;
				for (auto prev = z.getPrev(); prev; prev = prev->getSibling())
				{
					if (prev->fuzzy) continue;
					for (auto [ei, begin] : edgesByLastNode[prev - nodes.data()])
					{
						const POSTag lastTag = edges[ei].morphs.back().second;
						if (!isJClass(lastTag) && !isEClass(lastTag)) continue;
						Morphs morphs = edges[ei].morphs;
						appendMorph(morphs, coda, POSTag::z_coda);
						edges.push_back({ begin, z.endPos, edges[ei].typoCost + z.typoCost, move(morphs) });
					}
				}
			}
		};
		// graphOption.topN > 0이면 같은 노드에서 찾은 상위 분석도 topPaths로 받는다
		ret.topPaths = analyzeWithNodes(str, graphOption.topN, option, {}, config, normalizedStr, positionTable, addNodeEdges);

		// 글자별 태그. 끝에 unknown을 하나 두어 구간을 훑는 반복이 끝에서 멈추게 한다
		Vector<POSTag> tags(str.size() + 1, POSTag::unknown);
		for (size_t k = 0; k < str.size(); ++k) tags[k] = chrTag(str, k);

		const auto addSpan = [&](size_t a, size_t b, u16string surface, POSTag tag)
		{
			Morphs morphs;
			morphs.emplace_back(move(surface), tag);
			edges.push_back({ positionTable[a], positionTable[b], 0, move(morphs) });
		};

		// 미등록어 구간(태그 UN): 한글 음절로만 된 구간은 어디서나, 공백을 낀 구간은 공백을 하나로 줄여서. 품사는 쓰는 쪽이 정한다
		for (size_t a = 0; a < str.size(); ++a)
		{
			if (!isHangulSyllable(str[a])) continue;
			size_t chrs = 0;
			bool hasSpace = false;
			u16string surface;
			for (size_t b = a; b < str.size(); ++b)
			{
				if (tags[b] == POSTag::unknown)
				{
					if (!hasSpace || surface.back() != u' ') surface.push_back(u' ');
					hasSpace = true;
					continue;
				}
				if (!isHangulSyllable(str[b])) break;
				surface.push_back(str[b]);
				++chrs;
				if (hasSpace ? chrs > maxUnkSpanSizeWithSpace : chrs > max(maxUnkSpanSize, maxUnkSpanSizeWithSpace)) break;
				if (!hasSpace && chrs > maxUnkSpanSize) continue;
				addSpan(a, b + 1, surface, POSTag::unknown);
			}
		}

		// 로마자·숫자·기호가 섞인 미등록어 구간(조지 5세, FC 헤네프 05): 어절 첫머리나 여는 괄호·따옴표 뒤에서 시작한다
		for (size_t a = 0; maxUnkSpanSizeWithSpace && a < str.size(); ++a)
		{
			const auto ca = tags[a];
			if (!isHangulSyllable(str[a]) && ca != POSTag::sl && ca != POSTag::sn) continue;
			if (a > 0 && tags[a - 1] != POSTag::unknown && !isSSClass(tags[a - 1])) continue;
			size_t chrs = 0;
			bool mixed = false, hasLetter = false;
			u16string surface;
			for (size_t b = a; b < str.size(); ++b)
			{
				const auto cb = tags[b];
				if (cb == POSTag::unknown)
				{
					if (surface.back() != u' ') surface.push_back(u' ');
					continue;
				}
				const bool syllable = isHangulSyllable(str[b]);
				if (isSSClass(cb) || cb == POSTag::sh || (cb == POSTag::max && !syllable)) break;
				if (++chrs > maxUnkSpanSizeWithSpace) break;
				surface.push_back(str[b]);
				mixed = mixed || !syllable;
				hasLetter = hasLetter || syllable || cb == POSTag::sl;
				// 로마자·숫자 연속 구간은 한 토큰이므로 그 구간이 끝나는 곳에서만 끊는다
				const bool runEnd = syllable || tags[b + 1] != cb;
				if (mixed && hasLetter && runEnd && (syllable || cb == POSTag::sl || cb == POSTag::sn)) addSpan(a, b + 1, surface, POSTag::unknown);
			}
		}

		// 비한글 구간은 문자 종류별로도 나눈다: 로마자 SL, 숫자 SN, 한자 SH, 기호는 부분 구간마다 문장부호 태그
		static constexpr size_t maxSymbolRun = 8;
		for (size_t a = 0; a < str.size();)
		{
			const auto ca = tags[a];
			size_t b = a + 1;
			if (ca == POSTag::max || ca == POSTag::unknown)
			{
				a = b;
				continue;
			}
			const bool isSym = isPunctClass(ca);
			while (b < str.size())
			{
				const auto cb = tags[b];
				if (isSym ? !isPunctClass(cb) : cb != ca) break;
				++b;
			}
			if (isSym)
			{
				for (size_t x = a; x < b; ++x)
				{
					if (isLowSurrogate(str[x])) continue;
					for (size_t y = x + 1; y <= min(b, x + maxSymbolRun); ++y)
					{
						if (y < b && isLowSurrogate(str[y])) continue;
						for (auto t = POSTag::sf; t <= POSTag::sw; t = (POSTag)((uint8_t)t + 1)) addSpan(x, y, str.substr(x, y - x), t);
					}
				}
			}
			else
			{
				addSpan(a, b, str.substr(a, b - a), ca);
			}
			a = b;
		}

		// 마침표가 낀 로마자·숫자 구간: 약어는 마침표까지 SL(trans., L.Goldmann), 마침표가 둘 이상인 숫자열은 W_SERIAL(6.10., 1.2.0),
		// http 없는 도메인은 W_URL(recruit.nhncorp.com)
		for (size_t a = 0; a < str.size(); ++a)
		{
			const bool startOfRun = a == 0 || !(tags[a - 1] == POSTag::sl || tags[a - 1] == POSTag::sn || str[a - 1] == '-' || str[a - 1] == '.');
			if (!startOfRun) continue;
			if (tags[a] == POSTag::sl)
			{
				for (size_t b = a, dots = 0; b < str.size() && b - a < 32;)
				{
					while (tags[b] == POSTag::sl) ++b;
					if (dots) addSpan(a, b, str.substr(a, b - a), POSTag::sl);
					if (b >= str.size() || str[b] != '.') break;
					++b;
					++dots;
					addSpan(a, b, str.substr(a, b - a), POSTag::sl);
					if (tags[b] != POSTag::sl) break;
				}
			}
			if (tags[a] == POSTag::sn)
			{
				for (size_t b = a, dots = 0; b < str.size() && b - a < 32;)
				{
					while (tags[b] == POSTag::sn) ++b;
					if (dots >= 2) addSpan(a, b, str.substr(a, b - a), POSTag::w_serial);
					if (b >= str.size() || str[b] != '.') break;
					++b;
					if (++dots >= 2) addSpan(a, b, str.substr(a, b - a), POSTag::w_serial);
					if (tags[b] != POSTag::sn) break;
				}
			}
			if (tags[a] == POSTag::sl || tags[a] == POSTag::sn || str[a] == '-')
			{
				size_t b = a, labels = 0, lastLabel = a;
				while (b < str.size() && b - a < 128)
				{
					size_t c = b;
					while (tags[c] == POSTag::sl || tags[c] == POSTag::sn || str[c] == '-') ++c;
					if (c == b) break;
					++labels;
					lastLabel = b;
					b = c;
					if (b < str.size() && str[b] == '.' && (tags[b + 1] == POSTag::sl || tags[b + 1] == POSTag::sn || str[b + 1] == '-')) ++b;
					else break;
				}
				size_t lower = 0;
				for (size_t k = lastLabel; k < b; ++k) lower += 'a' <= str[k] && str[k] <= 'z';
				if (labels >= 2 && lower == b - lastLabel && 2 <= lower && lower <= 6)  // 마지막 부분이 com, kr 같은 소문자 2~6자
				{
					size_t e = b;
					while (e < str.size() && (str[e] == '/' || tags[e] == POSTag::sl || tags[e] == POSTag::sn || str[e] == '-' || str[e] == '.' || str[e] == '_' || str[e] == '?' || str[e] == '=' || str[e] == '&' || str[e] == '%')) ++e;
					while (e > b && str[e - 1] == '.') --e;
					addSpan(a, e, str.substr(a, e - a), POSTag::w_url);
					if (e != b) addSpan(a, b, str.substr(a, b - a), POSTag::w_url);
				}
			}
		}

		finalizeMorphGraph(ret, normalizedStr, positionTable);
		return ret;
	}

	const Morpheme* Kiwi::getDefaultMorpheme(POSTag tag) const
	{
		return &morphemes[getDefaultMorphemeId(tag)];
	}

	template<class Str, class Pretokenized, class ...Rest>
	auto Kiwi::_asyncAnalyze(Str&& str, Pretokenized&& pt, const optional<KiwiConfig>& overrideConfig, Rest&&... args) const
	{
		if (!pool) throw Exception{ "`asyncAnalyze` doesn't work at single thread mode." };
		return pool->enqueue([=, 
			str = forward<Str>(str), 
			pt = forward<Pretokenized>(pt),
			config = overrideConfig.value_or(globalConfig)](size_t, Rest... largs)
		{
			return analyze(str, largs..., pt, config);
		}, forward<Rest>(args)...);
	}

	template<class Str, class Pretokenized, class ...Rest>
	auto Kiwi::_asyncAnalyzeEcho(Str&& str, Pretokenized&& pt, const optional<KiwiConfig>& overrideConfig, Rest&&... args) const
	{
		if (!pool) throw Exception{ "`asyncAnalyze` doesn't work at single thread mode." };
		return pool->enqueue([=, 
			str = forward<Str>(str), 
			pt = forward<Pretokenized>(pt), 
			config = overrideConfig.value_or(globalConfig)](size_t, Rest... largs) mutable
		{
			auto ret = analyze(str, largs..., pt, config);
			return make_pair(move(ret), move(str));
		}, forward<Rest>(args)...);
	}

	future<vector<TokenResult>> Kiwi::asyncAnalyze(const string& str, size_t topN, AnalyzeOption option,
		const vector<PretokenizedSpan>& pretokenized, const optional<KiwiConfig>& overrideConfig
	) const
	{
		return _asyncAnalyze(str, pretokenized, overrideConfig, topN, option);
	}

	future<vector<TokenResult>> Kiwi::asyncAnalyze(string&& str, size_t topN, AnalyzeOption option,
		vector<PretokenizedSpan>&& pretokenized, const optional<KiwiConfig>& overrideConfig
	) const
	{
		return _asyncAnalyze(move(str), move(pretokenized), overrideConfig, topN, option);
	}

	future<TokenResult> Kiwi::asyncAnalyze(const string& str, AnalyzeOption option,
		const vector<PretokenizedSpan>& pretokenized, const optional<KiwiConfig>& overrideConfig
	) const
	{
		return _asyncAnalyze(str, pretokenized, overrideConfig, option);
	}

	future<TokenResult> Kiwi::asyncAnalyze(string&& str, AnalyzeOption option,
		vector<PretokenizedSpan>&& pretokenized, const optional<KiwiConfig>& overrideConfig
	) const
	{
		return _asyncAnalyze(move(str), move(pretokenized), overrideConfig, option);
	}

	future<pair<TokenResult, string>> Kiwi::asyncAnalyzeEcho(string&& str, AnalyzeOption option,
		vector<PretokenizedSpan>&& pretokenized, const optional<KiwiConfig>& overrideConfig
	) const
	{
		return _asyncAnalyzeEcho(move(str), move(pretokenized), overrideConfig, option);
	}

	future<vector<TokenResult>> Kiwi::asyncAnalyze(const u16string& str, size_t topN, AnalyzeOption option,
		const vector<PretokenizedSpan>& pretokenized, const optional<KiwiConfig>& overrideConfig
	) const
	{
		return _asyncAnalyze(str, pretokenized, overrideConfig, topN, option);
	}

	future<vector<TokenResult>> Kiwi::asyncAnalyze(u16string&& str, size_t topN, AnalyzeOption option,
		vector<PretokenizedSpan>&& pretokenized, const optional<KiwiConfig>& overrideConfig
	) const
	{
		return _asyncAnalyze(move(str), move(pretokenized), overrideConfig, topN, option);
	}

	future<TokenResult> Kiwi::asyncAnalyze(const u16string& str, AnalyzeOption option,
		const vector<PretokenizedSpan>& pretokenized, const optional<KiwiConfig>& overrideConfig
	) const
	{
		return _asyncAnalyze(str, pretokenized, overrideConfig, option);
	}

	future<TokenResult> Kiwi::asyncAnalyze(u16string&& str, AnalyzeOption option,
		vector<PretokenizedSpan>&& pretokenized, const optional<KiwiConfig>& overrideConfig
	) const
	{
		return _asyncAnalyze(move(str), move(pretokenized), overrideConfig, option);
	}

	future<pair<TokenResult, u16string>> Kiwi::asyncAnalyzeEcho(u16string&& str, AnalyzeOption option,
		vector<PretokenizedSpan>&& pretokenized, const optional<KiwiConfig>& overrideConfig
	) const
	{
		return _asyncAnalyzeEcho(move(str), move(pretokenized), overrideConfig, option);
	}

	cmb::AutoJoiner Kiwi::newJoiner(bool lmSearch) const
	{
		if (lmSearch)
		{
			return (*reinterpret_cast<FnNewJoiner>(dfNewJoiner))(this);
		}
		else
		{
			return cmb::AutoJoiner{ *this, cmb::Candidate<lm::VoidState<ArchType::none>>{ *combiningRule, langMdl.get() }};
		}
	}

	u16string Kiwi::getTypoForm(size_t typoFormId) const
	{
		if (typoFormId >= typoPtrs.size()) return {};
		const size_t* p = &typoPtrs[typoFormId];
		return joinHangul(typoPool.begin() + p[0], typoPool.begin() + p[1]);
	}

	void Kiwi::findMorphemes(vector<const Morpheme*>& ret, u16string_view s, POSTag tag, uint8_t senseId) const
	{
		auto normalized = normalizeHangul(s);
		auto form = (*reinterpret_cast<FnFindForm>(dfFindForm))(formTrie, forms.data(), normalized);
		if (!form) return;
		tag = clearIrregular(tag);
		for (auto c : form->candidate())
		{
			if (c->combineSocket
				|| (tag != POSTag::unknown
					&& clearIrregular(c->tag) != tag)
				|| (senseId != undefSenseId
					&& c->senseId != senseId))
				continue;
			ret.emplace_back(c);
		}
	}

	const Morpheme* Kiwi::findMorpheme(u16string_view s, POSTag tag, uint8_t senseId) const
	{
		auto normalized = normalizeHangul(s);
		auto form = (*reinterpret_cast<FnFindForm>(dfFindForm))(formTrie, forms.data(), normalized);
		if (!form) return nullptr;
		tag = clearIrregular(tag);
		for (auto c : form->candidate())
		{
			if (c->combineSocket
				|| c->tag == POSTag::unknown
				|| c->tag == POSTag::p
				|| (tag != POSTag::unknown
					&& clearIrregular(c->tag) != tag)
				|| (senseId != (uint8_t)-1
					&& c->senseId != senseId))
				continue;
			return c;
		}
		return nullptr;
	}

	vector<const Morpheme*> Kiwi::findMorphemes(u16string_view s, POSTag tag, uint8_t senseId) const
	{
		vector<const Morpheme*> ret;
		findMorphemes(ret, s, tag, senseId);
		return ret;
	}

	size_t Kiwi::findMorphemesWithPrefix(const Morpheme** out, size_t size, u16string_view s, POSTag tag, uint8_t senseId) const
	{
		auto normalized = normalizeHangul(s);
		auto [form, matchPrefixLen] = (*reinterpret_cast<FnFindFormWithPrefix>(dfFindFormWithPrefix))(formTrie, forms.data(), normalized);
		
		tag = clearIrregular(tag);
		size_t cnt = 0;
		for (; form; ++form)
		{
			for (auto c : form->candidate())
			{
				if (c->combineSocket
					|| c->tag == POSTag::unknown
					|| c->tag == POSTag::p
					|| (tag != POSTag::unknown
						&& clearIrregular(c->tag) != tag)
					|| (senseId != undefSenseId
						&& c->senseId != senseId))
					continue;
				if (cnt < size)
				{
					out[cnt++] = c;
				}
				else
				{
					break;
				}
			}

			if (cnt >= size) break;
		}
		return cnt;
	}

	vector<const Morpheme*> Kiwi::findMorphemesWithPrefix(size_t size, u16string_view s, POSTag tag, uint8_t senseId) const
	{
		vector<const Morpheme*> ret(size);
		size_t cnt = findMorphemesWithPrefix(ret.data(), size, s, tag, senseId);
		ret.resize(cnt);
		return ret;
	}
}
