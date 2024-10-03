#include <fstream>

#include <kiwi/Kiwi.h>
#include <kiwi/Utils.h>
#include <kiwi/TemplateUtils.hpp>
#include <kiwi/Form.h>
#include "ArchAvailable.h"
#include "KTrie.h"
#include "FeatureTestor.h"
#include "FrozenTrie.hpp"
#include "LmState.hpp"
#include "StrUtils.h"
#include "SortUtils.hpp"
#include "serializer.hpp"
#include "Joiner.hpp"
#include "PathEvaluator.hpp"

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
		LangModel _langMdl, 
		bool typoTolerant, 
		bool continualTypoTolerant, 
		bool lengtheningTypoTolerant)
		: langMdl(_langMdl)
	{
		selectedArch = arch;
		dfSplitByTrie = (void*)getSplitByTrieFn(selectedArch, 
			typoTolerant, 
			continualTypoTolerant, 
			lengtheningTypoTolerant);
		dfFindForm = (void*)getFindFormFn(selectedArch);

		static tp::Table<FnFindBestPath, AvailableArch> lmKnLM_8{ FindBestPathGetter<WrappedKnLM<uint8_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmKnLM_16{ FindBestPathGetter<WrappedKnLM<uint16_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmKnLM_32{ FindBestPathGetter<WrappedKnLM<uint32_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmKnLM_64{ FindBestPathGetter<WrappedKnLM<uint64_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmSbg_8{ FindBestPathGetter<WrappedSbg<8, uint8_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmSbg_16{ FindBestPathGetter<WrappedSbg<8, uint16_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmSbg_32{ FindBestPathGetter<WrappedSbg<8, uint32_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmSbg_64{ FindBestPathGetter<WrappedSbg<8, uint64_t>::type>{} };

		if (langMdl.sbg)
		{
			switch (langMdl.sbg->getHeader().keySize)
			{
			case 1:
				dfFindBestPath = (void*)lmSbg_8[static_cast<std::ptrdiff_t>(selectedArch)];
				break;
			case 2:
				dfFindBestPath = (void*)lmSbg_16[static_cast<std::ptrdiff_t>(selectedArch)];
				break;
			case 4:
				dfFindBestPath = (void*)lmSbg_32[static_cast<std::ptrdiff_t>(selectedArch)];
				break;
			case 8:
				dfFindBestPath = (void*)lmSbg_64[static_cast<std::ptrdiff_t>(selectedArch)];
				break;
			default:
				throw Exception{ "Wrong `lmKeySize`" };
			}
		}
		else if(langMdl.knlm)
		{
			switch (langMdl.knlm->getHeader().key_size)
			{
			case 1:
				dfFindBestPath = (void*)lmKnLM_8[static_cast<std::ptrdiff_t>(selectedArch)];
				break;
			case 2:
				dfFindBestPath = (void*)lmKnLM_16[static_cast<std::ptrdiff_t>(selectedArch)];
				break;
			case 4:
				dfFindBestPath = (void*)lmKnLM_32[static_cast<std::ptrdiff_t>(selectedArch)];
				break;
			case 8:
				dfFindBestPath = (void*)lmKnLM_64[static_cast<std::ptrdiff_t>(selectedArch)];
				break;
			default:
				throw Exception{ "Wrong `lmKeySize`" };
			}
		}
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
		if (!(matchOptions & (Match::joinNounPrefix | Match::joinNounSuffix | Match::joinVerbSuffix | Match::joinAdjSuffix | Match::joinAdvSuffix))) return last;
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
				&& ((POSTag::nng <= nextToken.tag && nextToken.tag <= POSTag::nnb) || nextToken.tag == POSTag::sn)
			)
			{
				concatTokens(current, nextToken, nextToken.tag);
				++next;
			}
			// (NN. | SN) + XSN => (NN. | SN)
			else if (!!(matchOptions & Match::joinNounSuffix)
				&& nextToken.tag == POSTag::xsn
				&& ((POSTag::nng <= current.tag && current.tag <= POSTag::nnb) || current.tag == POSTag::sn)
			)
			{
				concatTokens(current, nextToken, current.tag);
				++next;
			}
			// (NN. | XR) + XSV => VV
			else if (!!(matchOptions & Match::joinVerbSuffix)
				&& clearIrregular(nextToken.tag) == POSTag::xsv
				&& ((POSTag::nng <= current.tag && current.tag <= POSTag::nnb) || current.tag == POSTag::xr)
			)
			{
				concatTokens(current, nextToken, setIrregular(POSTag::vv, isIrregular(nextToken.tag)));
				++next;
			}
			// (NN. | XR) + XSA => VA
			else if (!!(matchOptions & Match::joinAdjSuffix)
				&& clearIrregular(nextToken.tag) == POSTag::xsa
				&& ((POSTag::nng <= current.tag && current.tag <= POSTag::nnb) || current.tag == POSTag::xr)
			)
			{
				concatTokens(current, nextToken, setIrregular(POSTag::va, isIrregular(nextToken.tag)));
				++next;
			}
			// (NN. | XR) + XSM => MAG
			else if (!!(matchOptions & Match::joinAdvSuffix)
				&& nextToken.tag == POSTag::xsm
				&& ((POSTag::nng <= current.tag && current.tag <= POSTag::nnb) || current.tag == POSTag::xr)
				)
			{
				concatTokens(current, nextToken, POSTag::mag);
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
		Vector<SpecialState>& spStatesByRet,
		const Vector<PathEvaluator::ChunkResult>& pathes,
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
			UnorderedMap<uint8_t, uint32_t> prevParents;
			Vector<uint8_t> selectedPathes(pathes.size());
			for (size_t i = 0; i < ret.size(); ++i)
			{
				auto pred = [&](const PathEvaluator::ChunkResult& p)
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
					ret.push_back(ret[parent]);
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

		UnorderedMap<uint8_t, uint32_t> spStateCnt;
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
				updateTokenInfoScript(token);
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
		Vector<SpecialState> sortedSpStatesByRet;
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
			spStatesByRet.emplace_back(sortedSpStatesByRet[i]);
		}
	}

	inline void makePretokenizedSpanGroup(
		PretokenizedSpanGroup& ret, 
		const std::vector<PretokenizedSpan>& pretokenized, 
		const Vector<uint32_t>& positionTable, 
		const KString& normStr,
		FnFindForm findForm,
		const utils::FrozenTrie<kchar_t, const Form*>& formTrie
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
		ret.morphemes.reserve(totTokens);
		for (auto& s : pretokenized)
		{
			ret.spans.emplace_back();
			auto& span = ret.spans.back();
			span.begin = positionTable[s.begin];
			span.end = positionTable[s.end];

			if (s.tokenization.empty())
			{
				auto formStr = normStr.substr(span.begin, span.end - span.begin);
				span.form = findForm(formTrie, formStr); // reuse the predefined form & morpheme
				if (!span.form) // or use a fallback form
				{
					span.form = formTrie.value((size_t)POSTag::nnp);
				}
			}
			else if (s.tokenization.size() == 1)
			{
				auto formStr = normalizeHangul(s.tokenization[0].form);
				auto* tform = findForm(formTrie, formStr); 
				if (tform && tform->candidate.size() == 1 &&
					areTagsEqual(tform->candidate[0]->tag, s.tokenization[0].tag, !!s.tokenization[0].inferRegularity))
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
						for (auto m : tform->candidate)
						{
							if (areTagsEqual(m->tag, s.tokenization[0].tag, s.tokenization[0].inferRegularity))
							{
								foundMorph[i++] = m;
								if (i >= 2) break;
							}
						}
					}

					form.candidate = FixedVector<const Morpheme*>{ (size_t)(foundMorph[1] ? 2 : 1) };
					
					if (foundMorph[0])
					{
						form.candidate[0] = foundMorph[0];
						if (foundMorph[1])
						{
							form.candidate[1] = foundMorph[1];
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
						morph.lmMorphemeId = getDefaultMorphemeId(s.tokenization[0].tag);
						form.candidate[0] = &morph;
					}
					span.form = &form;
				}
			}
			else
			{
				ret.forms.emplace_back();
				auto& form = ret.forms.back();
				form.candidate = FixedVector<const Morpheme*>{ 1 };
				ret.morphemes.emplace_back();
				auto& morph = ret.morphemes.back();
				morph.vowel = CondVowel::none;
				morph.polar = CondPolarity::none;
				morph.complex = 0;
				morph.chunks = FixedPairVector<const Morpheme*, std::pair<uint8_t, uint8_t>>{ s.tokenization.size() };
				for (size_t i = 0; i < s.tokenization.size(); ++i)
				{
					auto& t = s.tokenization[i];
					auto formStr = normalizeHangul(t.form);
					auto* tform = findForm(formTrie, formStr);
					const Morpheme* foundMorph = nullptr;
					if (tform)
					{
						for (auto m : tform->candidate)
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
						cmorph.tag = t.tag;
						cmorph.lmMorphemeId = getDefaultMorphemeId(t.tag);
						foundMorph = &cmorph;
					}

					morph.chunks[i] = foundMorph;
					morph.chunks.getSecond(i) = make_pair(positionTable[t.begin + s.begin] - span.begin, positionTable[t.end + s.begin] - span.begin);
				}

				form.candidate[0] = &morph;
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

	vector<TokenResult> Kiwi::analyze(const u16string& str, size_t topN, Match matchOptions, 
		const std::unordered_set<const Morpheme*>* blocklist,
		const std::vector<PretokenizedSpan>& pretokenized
	) const
	{
		thread_local KString normalizedStr;
		thread_local Vector<uint32_t> positionTable;
		thread_local PretokenizedSpanGroup pretokenizedGroup;
		normalizedStr.clear();
		positionTable.clear();
		pretokenizedGroup.clear();
		normalizeHangulWithPosition(str.begin(), str.end(), back_inserter(normalizedStr), back_inserter(positionTable));

		if (!!(matchOptions & Match::normalizeCoda)) normalizeCoda(normalizedStr.begin(), normalizedStr.end());

		makePretokenizedSpanGroup(
			pretokenizedGroup, 
			pretokenized, 
			positionTable, 
			normalizedStr, 
			reinterpret_cast<FnFindForm>(dfFindForm), 
			formTrie
		);

		// 분석할 문장에 포함된 개별 문자에 대해 어절번호를 생성한다
		thread_local Vector<uint16_t> wordPositions;
		wordPositions.clear();
		getWordPositions(wordPositions, str.begin(), str.end());
		
		vector<TokenResult> ret;
		Vector<SpecialState> spStatesByRet;
		thread_local Vector<KGraphNode> nodes;
		thread_local Vector<uint32_t> nodeInWhichPretokenized;
		const auto* pretokenizedFirst = pretokenizedGroup.spans.data();
		const auto* pretokenizedLast = pretokenizedFirst + pretokenizedGroup.spans.size();
		size_t splitEnd = 0;
		while (splitEnd < normalizedStr.size())
		{
			nodes.clear();
			auto* pretokenizedPrev = pretokenizedFirst;
			splitEnd = (*reinterpret_cast<FnSplitByTrie>(dfSplitByTrie))(
				nodes,
				forms.data(),
				typoPtrs.data(),
				formTrie,
				U16StringView{ normalizedStr.data() + splitEnd, normalizedStr.size() - splitEnd },
				splitEnd,
				matchOptions,
				maxUnkFormSize,
				spaceTolerance,
				continualTypoCost,
				lengtheningTypoCost,
				pretokenizedFirst,
				pretokenizedLast
			);

			if (nodes.size() <= 2) continue;
			findPretokenizedGroupOfNode(nodeInWhichPretokenized, nodes, pretokenizedPrev, pretokenizedFirst);

			Vector<PathEvaluator::ChunkResult> res = (*reinterpret_cast<FnFindBestPath>(dfFindBestPath))(
				this,
				spStatesByRet,
				nodes.data(),
				nodes.size(),
				topN,
				false,
				!!(matchOptions & Match::splitComplex),
				blocklist
			);
			insertPathIntoResults(ret, spStatesByRet, res, topN, matchOptions, integrateAllomorph, positionTable, wordPositions, pretokenizedGroup, nodeInWhichPretokenized);
		}

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

	const Morpheme* Kiwi::getDefaultMorpheme(POSTag tag) const
	{
		return &morphemes[getDefaultMorphemeId(tag)];
	}

	template<class Str, class Pretokenized, class ...Rest>
	auto Kiwi::_asyncAnalyze(Str&& str, Pretokenized&& pt, Rest&&... args) const
	{
		if (!pool) throw Exception{ "`asyncAnalyze` doesn't work at single thread mode." };
		return pool->enqueue([=, str = forward<Str>(str), pt = forward<Pretokenized>(pt)](size_t, Rest... largs)
		{
			return analyze(str, largs..., pt);
		}, forward<Rest>(args)...);
	}

	template<class Str, class Pretokenized, class ...Rest>
	auto Kiwi::_asyncAnalyzeEcho(Str&& str, Pretokenized&& pt, Rest&&... args) const
	{
		if (!pool) throw Exception{ "`asyncAnalyze` doesn't work at single thread mode." };
		return pool->enqueue([=, str = forward<Str>(str), pt = forward<Pretokenized>(pt)](size_t, Rest... largs) mutable
		{
			auto ret = analyze(str, largs..., pt);
			return make_pair(move(ret), move(str));
		}, forward<Rest>(args)...);
	}

	future<vector<TokenResult>> Kiwi::asyncAnalyze(const string& str, size_t topN, Match matchOptions, 
		const unordered_set<const Morpheme*>* blocklist,
		const vector<PretokenizedSpan>& pretokenized
	) const
	{
		return _asyncAnalyze(str, pretokenized, topN, matchOptions, blocklist);
	}

	future<vector<TokenResult>> Kiwi::asyncAnalyze(string&& str, size_t topN, Match matchOptions, 
		const unordered_set<const Morpheme*>* blocklist,
		vector<PretokenizedSpan>&& pretokenized
	) const
	{
		return _asyncAnalyze(move(str), move(pretokenized), topN, matchOptions, blocklist);
	}

	future<TokenResult> Kiwi::asyncAnalyze(const string& str, Match matchOptions, 
		const unordered_set<const Morpheme*>* blocklist,
		const vector<PretokenizedSpan>& pretokenized
	) const
	{
		return _asyncAnalyze(str, pretokenized, matchOptions, blocklist);
	}

	future<TokenResult> Kiwi::asyncAnalyze(string&& str, Match matchOptions, 
		const unordered_set<const Morpheme*>* blocklist,
		vector<PretokenizedSpan>&& pretokenized
	) const
	{
		return _asyncAnalyze(move(str), move(pretokenized), matchOptions, blocklist);
	}

	future<pair<TokenResult, string>> Kiwi::asyncAnalyzeEcho(string&& str, Match matchOptions, 
		const unordered_set<const Morpheme*>* blocklist,
		vector<PretokenizedSpan>&& pretokenized
	) const
	{
		return _asyncAnalyzeEcho(move(str), move(pretokenized), matchOptions, blocklist);
	}

	future<vector<TokenResult>> Kiwi::asyncAnalyze(const u16string& str, size_t topN, Match matchOptions, 
		const unordered_set<const Morpheme*>* blocklist,
		const vector<PretokenizedSpan>& pretokenized
	) const
	{
		return _asyncAnalyze(str, pretokenized, topN, matchOptions, blocklist);
	}

	future<vector<TokenResult>> Kiwi::asyncAnalyze(u16string&& str, size_t topN, Match matchOptions, 
		const unordered_set<const Morpheme*>* blocklist,
		vector<PretokenizedSpan>&& pretokenized
	) const
	{
		return _asyncAnalyze(move(str), move(pretokenized), topN, matchOptions, blocklist);
	}

	future<TokenResult> Kiwi::asyncAnalyze(const u16string& str, Match matchOptions, 
		const unordered_set<const Morpheme*>* blocklist,
		const vector<PretokenizedSpan>& pretokenized
	) const
	{
		return _asyncAnalyze(str, pretokenized, matchOptions, blocklist);
	}

	future<TokenResult> Kiwi::asyncAnalyze(u16string&& str, Match matchOptions, 
		const unordered_set<const Morpheme*>* blocklist,
		vector<PretokenizedSpan>&& pretokenized
	) const
	{
		return _asyncAnalyze(move(str), move(pretokenized), matchOptions, blocklist);
	}

	future<pair<TokenResult, u16string>> Kiwi::asyncAnalyzeEcho(u16string&& str, Match matchOptions, 
		const unordered_set<const Morpheme*>* blocklist,
		vector<PretokenizedSpan>&& pretokenized
	) const
	{
		return _asyncAnalyzeEcho(move(str), move(pretokenized), matchOptions, blocklist);
	}

	using FnNewAutoJoiner = cmb::AutoJoiner(Kiwi::*)() const;

	template<template<ArchType> class LmState>
	struct NewAutoJoinerGetter
	{
		template<std::ptrdiff_t i>
		struct Wrapper
		{
			static constexpr FnNewAutoJoiner value = &Kiwi::newJoinerImpl<LmState<static_cast<ArchType>(i)>>;
		};
	};

	cmb::AutoJoiner Kiwi::newJoiner(bool lmSearch) const
	{
		static tp::Table<FnNewAutoJoiner, AvailableArch> lmVoid{ NewAutoJoinerGetter<VoidState>{} };
		static tp::Table<FnNewAutoJoiner, AvailableArch> lmKnLM_8{ NewAutoJoinerGetter<WrappedKnLM<uint8_t>::type>{} };
		static tp::Table<FnNewAutoJoiner, AvailableArch> lmKnLM_16{ NewAutoJoinerGetter<WrappedKnLM<uint16_t>::type>{} };
		static tp::Table<FnNewAutoJoiner, AvailableArch> lmKnLM_32{ NewAutoJoinerGetter<WrappedKnLM<uint32_t>::type>{} };
		static tp::Table<FnNewAutoJoiner, AvailableArch> lmKnLM_64{ NewAutoJoinerGetter<WrappedKnLM<uint64_t>::type>{} };
		static tp::Table<FnNewAutoJoiner, AvailableArch> lmSbg_8{ NewAutoJoinerGetter<WrappedSbg<8, uint8_t>::type>{} };
		static tp::Table<FnNewAutoJoiner, AvailableArch> lmSbg_16{ NewAutoJoinerGetter<WrappedSbg<8, uint16_t>::type>{} };
		static tp::Table<FnNewAutoJoiner, AvailableArch> lmSbg_32{ NewAutoJoinerGetter<WrappedSbg<8, uint32_t>::type>{} };
		static tp::Table<FnNewAutoJoiner, AvailableArch> lmSbg_64{ NewAutoJoinerGetter<WrappedSbg<8, uint64_t>::type>{} };
		
		const auto archIdx = static_cast<std::ptrdiff_t>(selectedArch);

		if (lmSearch)
		{
			size_t vocabTySize = langMdl.knlm->getHeader().key_size;
			if (langMdl.sbg)
			{
				switch (vocabTySize)
				{
				case 1:
					return (this->*lmSbg_8[archIdx])();
				case 2:
					return (this->*lmSbg_16[archIdx])();
				case 4:
					return (this->*lmSbg_32[archIdx])();
				case 8:
					return (this->*lmSbg_64[archIdx])();
				default:
					throw Exception{ "invalid `key_size`=" + to_string(vocabTySize)};
				}
			}
			else
			{
				switch (vocabTySize)
				{
				case 1:
					return (this->*lmKnLM_8[archIdx])();
				case 2:
					return (this->*lmKnLM_16[archIdx])();
				case 4:
					return (this->*lmKnLM_32[archIdx])();
				case 8:
					return (this->*lmKnLM_64[archIdx])();
				default:
					throw Exception{ "invalid `key_size`=" + to_string(vocabTySize) };
				}
			}
		}
		else
		{
			return (this->*lmVoid[archIdx])();
		}
	}

	using FnNewLmObject = std::unique_ptr<LmObjectBase>(*)(const LangModel&);

	template<class Ty>
	std::unique_ptr<LmObjectBase> makeNewLmObject(const LangModel& lm)
	{
		return make_unique<LmObject<Ty>>(lm);
	}

	template<template<ArchType> class LmState>
	struct NewLmObjectGetter
	{
		template<std::ptrdiff_t i>
		struct Wrapper
		{
			static constexpr FnNewLmObject value = makeNewLmObject<LmState<static_cast<ArchType>(i)>>;
		};
	};

	std::unique_ptr<LmObjectBase> Kiwi::newLmObject() const
	{
		static tp::Table<FnNewLmObject, AvailableArch> lmKnLM_8{ NewLmObjectGetter<WrappedKnLM<uint8_t>::type>{} };
		static tp::Table<FnNewLmObject, AvailableArch> lmKnLM_16{ NewLmObjectGetter<WrappedKnLM<uint16_t>::type>{} };
		static tp::Table<FnNewLmObject, AvailableArch> lmKnLM_32{ NewLmObjectGetter<WrappedKnLM<uint32_t>::type>{} };
		static tp::Table<FnNewLmObject, AvailableArch> lmKnLM_64{ NewLmObjectGetter<WrappedKnLM<uint64_t>::type>{} };
		static tp::Table<FnNewLmObject, AvailableArch> lmSbg_8{ NewLmObjectGetter<WrappedSbg<8, uint8_t>::type>{} };
		static tp::Table<FnNewLmObject, AvailableArch> lmSbg_16{ NewLmObjectGetter<WrappedSbg<8, uint16_t>::type>{} };
		static tp::Table<FnNewLmObject, AvailableArch> lmSbg_32{ NewLmObjectGetter<WrappedSbg<8, uint32_t>::type>{} };
		static tp::Table<FnNewLmObject, AvailableArch> lmSbg_64{ NewLmObjectGetter<WrappedSbg<8, uint64_t>::type>{} };

		const auto archIdx = static_cast<std::ptrdiff_t>(selectedArch);

		size_t vocabTySize = langMdl.knlm->getHeader().key_size;
		if (langMdl.sbg)
		{
			switch (vocabTySize)
			{
			case 1:
				return (lmSbg_8[archIdx])(langMdl);
			case 2:
				return (lmSbg_16[archIdx])(langMdl);
			case 4:
				return (lmSbg_32[archIdx])(langMdl);
			case 8:
				return (lmSbg_64[archIdx])(langMdl);
			default:
				throw Exception{ "invalid `key_size`=" + to_string(vocabTySize) };
			}
		}
		else
		{
			switch (vocabTySize)
			{
			case 1:
				return (lmKnLM_8[archIdx])(langMdl);
			case 2:
				return (lmKnLM_16[archIdx])(langMdl);
			case 4:
				return (lmKnLM_32[archIdx])(langMdl);
			case 8:
				return (lmKnLM_64[archIdx])(langMdl);
			default:
				throw Exception{ "invalid `key_size`=" + to_string(vocabTySize) };
			}
		}
	}

	u16string Kiwi::getTypoForm(size_t typoFormId) const
	{
		if (typoFormId >= typoPtrs.size()) return {};
		const size_t* p = &typoPtrs[typoFormId];
		return joinHangul(typoPool.begin() + p[0], typoPool.begin() + p[1]);
	}

	void Kiwi::findMorpheme(vector<const Morpheme*>& ret, const u16string& s, POSTag tag) const
	{
		auto normalized = normalizeHangul(s);
		auto form = (*reinterpret_cast<FnFindForm>(dfFindForm))(formTrie, normalized);
		if (!form) return;
		tag = clearIrregular(tag);
		for (auto c : form->candidate)
		{
			if (c->combineSocket
				|| (tag != POSTag::unknown
					&& clearIrregular(c->tag) != tag))
				continue;
			ret.emplace_back(c);
		}
	}

	vector<const Morpheme*> Kiwi::findMorpheme(const u16string& s, POSTag tag) const
	{
		vector<const Morpheme*> ret;
		findMorpheme(ret, s, tag);
		return ret;
	}
}
