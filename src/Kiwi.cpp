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

using namespace std;

namespace kiwi
{
	class PathEvaluator
	{
	public:
		struct Result
		{
			const Morpheme* morph = nullptr;
			KString str;
			uint32_t begin = 0, end = 0;
			float wordScore = 0, typoCost = 0;
			uint32_t typoFormId = 0;
			Result(const Morpheme* _morph = nullptr,
				const KString& _str = {},
				uint32_t _begin = 0,
				uint32_t _end = 0,
				float _wordScore = 0,
				float _typoCost = 0,
				uint32_t _typoFormId = 0
			)
				: morph{ _morph }, str{ _str }, begin{ _begin }, end{ _end }, 
				wordScore{ _wordScore }, typoCost{ _typoCost }, typoFormId{ _typoFormId }
			{
			}

			bool operator==(const Result& o) const
			{
				return morph == o.morph
					&& str == o.str
					&& begin == o.begin
					&& end == o.end
					&& wordScore == o.wordScore
					&& typoCost == o.typoCost
					&& typoFormId == o.typoFormId;
			}
		};
		using Path = Vector<Result>;

		template<class LmState>
		static Vector<std::pair<Path, float>> findBestPath(const Kiwi* kw, 
			const Vector<KGraphNode>& graph, 
			size_t topN, 
			bool openEnd,
			bool splitComplex = false,
			const std::unordered_set<const Morpheme*>* blocklist = nullptr
		);

		template<class LmState, class CandTy, class CacheTy>
		static float evalPath(const Kiwi* kw, const KGraphNode* startNode, const KGraphNode* node,
			CacheTy& cache, Vector<KString>& ownFormList,
			size_t i, size_t ownFormId, CandTy&& cands, bool unknownForm,
			bool splitComplex = false,
			const std::unordered_set<const Morpheme*>* blocklist = nullptr
		);
	};

	using FnFindBestPath = decltype(&PathEvaluator::findBestPath<KnLMState<ArchType::none, uint16_t>>);

	template<template<ArchType> class LmState>
	struct FindBestPathGetter
	{
		template<std::ptrdiff_t i>
		struct Wrapper
		{
			static constexpr FnFindBestPath value = &PathEvaluator::findBestPath<LmState<static_cast<ArchType>(i)>>;
		};
	};

	Kiwi::Kiwi(ArchType arch, LangModel _langMdl, bool typoTolerant)
		: langMdl(_langMdl)
	{
		selectedArch = arch;
		dfSplitByTrie = (void*)getSplitByTrieFn(selectedArch, typoTolerant);
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
		for (auto& t : tokens)
		{
			if (t.tag != POSTag::sso && t.tag != POSTag::ssc) continue;
			uint32_t i = &t - tokens.data();
			uint32_t type = getSSType(t.str[0]);
			if (!type) continue;
			if (t.tag == POSTag::sso)
			{
				pStack.emplace_back(i, type);
			}
			else if (t.tag == POSTag::ssc)
			{
				for (size_t j = 0; j < pStack.size(); ++j)
				{
					if (pStack.rbegin()[j].second == type)
					{
						t.pairedToken = pStack.rbegin()[j].first;
						tokens[pStack.rbegin()[j].first].pairedToken = i;
						pStack.erase(pStack.end() - j - 1, pStack.end());
						break;
					}
				}
			}
		}
	}

	class SentenceParser
	{
		enum class State
		{
			none = 0,
			ef,
			efjx,
			sf,
		} state = State::none;
	public:

		bool next(const TokenInfo& t, bool forceNewSent = false)
		{
			bool ret = false;
			if (forceNewSent)
			{
				state = State::none;
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
				case POSTag::jc:
				case POSTag::jkb:
				case POSTag::jkc:
				case POSTag::jkg:
				case POSTag::jko:
				case POSTag::jkq:
				case POSTag::jks:
				case POSTag::jkv:
				case POSTag::jx:
					if (t.tag == POSTag::jx && *t.morph->kform == u"요")
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
				case POSTag::sf:
				case POSTag::ssc:
					break;
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
				default:
					ret = true;
					state = State::none;
					break;
				}
				break;
			}
			return ret;
		}
	};

	inline bool hasSentences(const TokenInfo* first, const TokenInfo* last)
	{
		SentenceParser sp;
		for (; first != last; ++first)
		{
			if (sp.next(*first)) return true;
		}
		return sp.next({});
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
		/*
		* 문장 분리 기준
		* 1) 종결어미(ef) (요/jx)? (so|sw|sh|sp|se|sf|(닫는 괄호))*
		* 2) 종결구두점(sf) (so|sw|sh|sp|se|(닫는 괄호))*
		* 3) 단 종결어미(ef) 바로 다음에 '요'가 아닌 조사(j)나 보조용언(vx)이 뒤따르는 경우는 제외
		*/

		SentenceParser sp;
		uint32_t sentPos = 0, lastSentPos = 0, subSentPos = 0, accumSubSent = 1, accumWordPos = 0, lastWordPos = 0;
		size_t nlPos = 0, lastNlPos = 0, nestedEnd = 0;
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			auto& t = tokens[i];
			if (sp.next(t, nestedEnd && i == nestedEnd))
			{
				if (nestedEnd)
				{
					subSentPos++;
					accumSubSent++;
				}
				else
				{
					sentPos++;
					accumSubSent = 1;
				}
			}

			if (!nestedEnd && t.tag == POSTag::sso && t.pairedToken != (uint32_t)-1
				&& hasSentences(&tokens[i], &tokens[t.pairedToken])
				&& ((t.pairedToken + 1 < tokens.size() && isNestedRight(tokens[t.pairedToken + 1]))
					|| (i > 0 && isNestedLeft(tokens[i - 1])))
				)
			{
				nestedEnd = t.pairedToken;
				subSentPos = accumSubSent;
			}
			else if (nestedEnd && i > nestedEnd)
			{
				nestedEnd = 0;
				subSentPos = 0;
			}

			while (nlPos < newlines.size() && newlines[nlPos] < t.position) nlPos++;
			
			t.lineNumber = (uint32_t)nlPos;
			if (nlPos > lastNlPos + 1 && sentPos == lastSentPos && !nestedEnd)
			{
				sentPos++;
			}
			t.sentPosition = sentPos;
			t.subSentPosition = (i == nestedEnd || i == tokens[nestedEnd].pairedToken) ? 0 : subSentPos;
			
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

	vector<TokenResult> Kiwi::analyze(const u16string& str, size_t topN, Match matchOptions, const std::unordered_set<const Morpheme*>* blocklist) const
	{
		auto chunk = str.begin();
		Vector<u16string::const_iterator> sents;
		sents.emplace_back(chunk);
		while (chunk != str.end())
		{
			POSTag tag = identifySpecialChr(*chunk);
			while (chunk != str.end() && !(tag == POSTag::sf || tag == POSTag::se || *chunk == u':'))
			{
				++chunk;
				if (chunk - sents.back() >= 512)
				{
					if (0xDC00 <= *chunk && *chunk <= 0xDFFF) --chunk;
					sents.emplace_back(chunk);
					break;
				}
				if (chunk == str.end()) break;
				tag = identifySpecialChr(*chunk);
			}
			if (chunk == str.end()) break;
			++chunk;
			if (tag == POSTag::se)
			{
				sents.emplace_back(chunk);
				continue;
			}
			if (chunk != str.end() && (identifySpecialChr(*chunk) == POSTag::unknown || identifySpecialChr(*chunk) == POSTag::sf))
			{
				while (chunk != str.end() && (identifySpecialChr(*chunk) == POSTag::unknown || identifySpecialChr(*chunk) == POSTag::sf))
				{
					++chunk;
				}
				sents.emplace_back(chunk);
			}
		}
		if (sents.back() != str.end()) sents.emplace_back(str.end());
		if (sents.size() <= 1) return vector<TokenResult>(1);
		vector<TokenResult> ret = analyzeSent(sents[0], sents[1], topN, matchOptions, blocklist);
		if (ret.empty())
		{
			return vector<TokenResult>(1);
		}
		while (ret.size() < topN) ret.emplace_back(ret.back());
		for (size_t i = 2; i < sents.size(); ++i)
		{
			auto res = analyzeSent(sents[i - 1], sents[i], topN, matchOptions, blocklist);
			if (res.empty()) continue;
			for (size_t n = 0; n < topN; ++n)
			{
				auto& r = res[min(n, res.size() - 1)];
				transform(r.first.begin(), r.first.end(), back_inserter(ret[n].first), [&sents, i](TokenInfo& p)
				{
					p.position += (uint32_t)distance(sents[0], sents[i - 1]);
					return p;
				});
				ret[n].second += r.second;
			}
		}

		auto newlines = allNewLinePositions(str);
		for (auto& r : ret)
		{
			fillPairedTokenInfo(r.first);
			fillSentLineInfo(r.first, newlines);
		}
		return ret;
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

	template<class _map, class _key, class _value, class _comp>
	void emplaceMaxCnt(_map& dest, _key&& key, _value&& value, size_t maxCnt, _comp comparator)
	{
		auto itp = dest.find(key);
		if (itp == dest.end())
		{
			typename _map::mapped_type emp;
			emp.reserve(maxCnt);
			itp = dest.emplace(key, move(emp)).first;
		}
		if (itp->second.size() < maxCnt)
		{
			itp->second.emplace_back(value);
			push_heap(itp->second.begin(), itp->second.end(), comparator);
		}
		else
		{
			pop_heap(itp->second.begin(), itp->second.end(), comparator);
			if (comparator(value, itp->second.back())) itp->second.back() = value;
			push_heap(itp->second.begin(), itp->second.end(), comparator);
		}
	}

	using Wid = uint32_t;
	struct MInfo
	{
		Wid wid;
		uint8_t combineSocket;
		uint8_t ownFormId;
		MInfo(Wid _wid = 0, uint8_t _combineSocket = 0, uint8_t _ownFormId = 0)
			: wid(_wid), combineSocket(_combineSocket), ownFormId(_ownFormId)
		{}
	};

	using SpecialState = array<uint8_t, 2>;

	template<class LmState>
	struct WordLL
	{
		const Morpheme* morpheme = nullptr;
		MInfo back;
		float accScore = 0, accTypoCost = 0;
		const WordLL* parent = nullptr;
		LmState lmState;
		SpecialState spState = { { 0, } };

		WordLL() = default;

		WordLL(const Morpheme* _morph, float _accScore, float _accTypoCost, const WordLL* _parent, LmState _lmState, SpecialState _spState)
			: morpheme{ _morph }, accScore{ _accScore }, accTypoCost{ _accTypoCost }, parent{ _parent }, lmState{ _lmState }, spState(_spState)
		{
		}
	};

	template<class LmState>
	struct WordLLP
	{
		const Morpheme* lastMorpheme = nullptr;
		float accScore = 0, accTypoCost = 0;
		const WordLL<LmState>* parent = nullptr;
		LmState lmState;
		SpecialState spState = { { 0, } };

		WordLLP() = default;

		WordLLP(const Morpheme* _lastMorph, float _accScore, float _accTypoCost, const WordLL<LmState>* _parent, LmState _lmState, SpecialState _spState)
			: lastMorpheme{ _lastMorph }, accScore{ _accScore }, accTypoCost{ _accTypoCost }, 
			parent{ _parent }, lmState{ _lmState }, spState(_spState)
		{
		}
	};

	template<class LmState, class _Type>
	void evalTrigram(const LangModel& langMdl, 
		const Morpheme* morphBase, 
		const Vector<KString>& ownForms, 
		const Vector<Vector<WordLL<LmState>>>& cache,
		array<Wid, 4> seq, 
		size_t chSize, 
		const Morpheme* curMorph, 
		const KGraphNode* node, 
		const KGraphNode* startNode, 
		_Type& maxWidLL, 
		float ignoreCondScore,
		float spacePenalty,
		bool allowedSpaceBetweenChunk
	)
	{
		size_t vocabSize = langMdl.knlm->getHeader().vocab_size;
		for (auto* prev = node->getPrev(); prev; prev = prev->getSibling())
		{
			assert(prev != node);
			for (auto& p : cache[prev - startNode])
			{
				float candScore = p.accScore;
				if (p.back.combineSocket)
				{
					// merge <v> <chunk> with only the same socket
					if (p.back.combineSocket != curMorph->combineSocket || curMorph->chunks.empty())
					{
						continue;
					}
					if (prev->endPos < node->startPos)
					{
						if (allowedSpaceBetweenChunk) candScore -= spacePenalty;
						else continue;
					}
					seq[0] = morphBase[p.back.wid].getCombined()->lmMorphemeId;
				}

				auto leftForm = p.back.ownFormId ? &ownForms[p.back.ownFormId - 1] : morphBase[p.back.wid].kform;

				if (ignoreCondScore)
				{
					candScore += FeatureTestor::isMatched(leftForm, curMorph->vowel, curMorph->polar) ? 0 : ignoreCondScore;
				}
				else
				{
					if (!FeatureTestor::isMatched(leftForm, curMorph->vowel, curMorph->polar)) continue;
				}

				auto cLmState = p.lmState;
				Wid lSeq = 0;
				if (curMorph->combineSocket && curMorph->chunks.empty())
				{
					lSeq = p.back.wid;
				}
				else
				{
					lSeq = seq[chSize - 1];
					for (size_t i = 0; i < chSize; ++i)
					{
						if (morphBase[seq[i]].tag == POSTag::p)
						{
							// prohibit <v> without <chunk>
							goto continueFor;
						}
						float ll = cLmState.next(langMdl, seq[i]);
						candScore += ll;
					}
				}
				emplaceMaxCnt(maxWidLL, lSeq, WordLLP<LmState>{ &morphBase[p.back.wid], candScore, p.accTypoCost + node->typoCost, &p, move(cLmState), p.spState }, 3, 
					[](const WordLLP<LmState>& a, const WordLLP<LmState>& b) 
					{ 
						return a.accScore > b.accScore; 
					}
				);
			continueFor:;
			}
		}
	}

	inline bool hasLeftBoundary(const KGraphNode* node)
	{
		// 시작 지점은 항상 왼쪽 경계로 처리
		if (node->getPrev()->endPos == 0) return true; 

		// 이전 노드의 끝지점이 현재 노드보다 작은 경우 왼쪽 경계로 처리
		if (node->getPrev()->endPos < node->startPos) return true;
		
		// 이전 노드가 구두점이나 특수 문자인 경우
		if (!node->getPrev()->uform.empty())
		{
			// 닫는 괄호는 왼쪽 경계로 처리하지 않음
			auto c = node->getPrev()->uform.back();
			auto tag = identifySpecialChr(c);
			if (tag == POSTag::ssc || c == u'"' || c == u'\'') return false;
			
			// 나머지 특수문자는 왼쪽 경계로 처리
			if (POSTag::sf <= tag && tag <= POSTag::sw) return true;
		}
		return false;
	}

	inline bool isInflectendaNP(const Morpheme* morph)
	{
		return morph->tag == POSTag::np && morph->kform->size() == 1 && (
			(*morph->kform)[0] == u'나' || (*morph->kform)[0] == u'너' || (*morph->kform)[0] == u'저');
	}

	inline bool isInflectendaJ(const Morpheme* morph)
	{
		return (morph->tag == POSTag::jks || morph->tag == POSTag::jkc) && morph->kform->size() == 1 && (*morph->kform)[0] == u'가';
	}

	inline bool isVerbL(const Morpheme* morph)
	{
		return isVerbClass(morph->tag) && morph->kform && !morph->kform->empty() && morph->kform->back() == u'ᆯ';
	}

	inline bool isBadPairOfVerbL(const Morpheme* morph)
	{
		auto onset = (morph->kform && !morph->kform->empty()) ? morph->kform->front() : 0;
		return onset == u'으' || onset == u'느' || (u'사' <= onset && onset <= u'시');
	}

	inline bool isPositiveVerb(const Morpheme* morph)
	{
		return isVerbClass(morph->tag) && FeatureTestor::isMatched(morph->kform, CondPolarity::positive);
	}

	inline bool isNegativeVerb(const Morpheme* morph)
	{
		return isVerbClass(morph->tag) && FeatureTestor::isMatched(morph->kform, CondPolarity::negative);
	}

	inline bool isVerbVowel(const Morpheme* morph)
	{
		return isVerbClass(morph->tag) && morph->kform && !morph->kform->empty() && !isHangulCoda(morph->kform->back());
	}

	template<class LmState, class CandTy, class CacheTy>
	float PathEvaluator::evalPath(const Kiwi* kw, 
		const KGraphNode* startNode, 
		const KGraphNode* node,
		CacheTy& cache, 
		Vector<KString>& ownFormList,
		size_t i, 
		size_t ownFormId, 
		CandTy&& cands, 
		bool unknownForm,
		bool splitComplex,
		const std::unordered_set<const Morpheme*>* blocklist
	)
	{
		const size_t langVocabSize = kw->langMdl.knlm->getHeader().vocab_size;
		auto& nCache = cache[i];

		float whitespaceDiscount = 0;
		if (node->uform.empty() && node->endPos - node->startPos > node->form->form.size())
		{
			whitespaceDiscount = -kw->spacePenalty * (node->endPos - node->startPos - node->form->form.size());
		}

		float tMax = -INFINITY;
		for (bool ignoreCond : {false, true})
		{
			for (auto& curMorph : cands)
			{
				if (splitComplex && curMorph->getCombined()->complex) continue;
				if (blocklist && blocklist->count(curMorph->getCombined())) continue;

				// 덧붙은 받침(zCoda)을 위한 지름길
				if (curMorph->tag == POSTag::z_coda)
				{
					for (auto* prev = node->getPrev(); prev; prev = prev->getSibling())
					{
						for (auto& p : cache[prev - startNode])
						{
							auto lastTag = kw->morphemes[p.back.wid].tag;
							if (!isJClass(lastTag) && !isEClass(lastTag)) continue;
							nCache.emplace_back(p);
							nCache.back().accScore += curMorph->userScore * kw->typoCostWeight;
							nCache.back().accTypoCost -= curMorph->userScore;
							nCache.back().parent = &p;
							nCache.back().morpheme = &kw->morphemes[curMorph->lmMorphemeId];
							nCache.back().back.wid = curMorph->lmMorphemeId;
						}
					}
					continue;
				}

				array<Wid, 4> seq = { 0, };
				array<Wid, 4> oseq = { 0, };
				uint8_t combSocket = 0;
				CondVowel condV = CondVowel::none;
				CondPolarity condP = CondPolarity::none;
				size_t chSize = 1;
				bool isUserWord = false;
				// if the morpheme has chunk set
				if (!curMorph->chunks.empty() && !curMorph->complex)
				{
					chSize = curMorph->chunks.size();
					for (size_t i = 0; i < chSize; ++i)
					{
						seq[i] = curMorph->chunks[i]->lmMorphemeId;
						if (curMorph->chunks[i] - kw->morphemes.data() >= langVocabSize)
						{
							oseq[i] = curMorph->chunks[i] - kw->morphemes.data();
						}
						else
						{
							oseq[i] = seq[i];
						}
					}
				}
				else
				{
					seq[0] = curMorph->lmMorphemeId;
					if ((curMorph->getCombined() ? curMorph->getCombined() : curMorph) - kw->morphemes.data() >= langVocabSize)
					{
						isUserWord = true;
						oseq[0] = curMorph - kw->morphemes.data();
					}
					else
					{
						oseq[0] = seq[0];
					}
					combSocket = curMorph->combineSocket;
				}
				condV = curMorph->vowel;
				condP = curMorph->polar;

				UnorderedMap<Wid, Vector<WordLLP<LmState>>> maxWidLL;
				evalTrigram(kw->langMdl, kw->morphemes.data(), ownFormList, cache, seq, chSize, curMorph, node, startNode, maxWidLL, ignoreCond ? -10 : 0, kw->spacePenalty, kw->spaceTolerance > 0);
				if (maxWidLL.empty()) continue;

				float estimatedLL = curMorph->userScore + whitespaceDiscount - node->typoCost * kw->typoCostWeight;
				// if a form of the node is unknown, calculate log poisson distribution for word-tag
				if (unknownForm)
				{
					size_t unknownLen = node->uform.empty() ? node->form->form.size() : node->uform.size();
					estimatedLL -= unknownLen * kw->unkFormScoreScale + kw->unkFormScoreBias;
				}

				float discountForCombining = curMorph->combineSocket ? -15 : 0;
				estimatedLL += kw->tagScorer.evalLeftBoundary(hasLeftBoundary(node), curMorph->tag);

				auto curMorphSpecialType = kw->determineSpecialMorphType(kw->morphToId(curMorph));
				
				bool vowelE = isEClass(curMorph->tag) && curMorph->kform && hasNoOnset(*curMorph->kform);
				bool infJ = isInflectendaJ(curMorph);
				bool badPairOfL = isBadPairOfVerbL(curMorph);
				bool positiveE = isEClass(curMorph->tag) && node->form && node->form->form[0] == u'아';
				bool contractableE = isEClass(curMorph->tag) && curMorph->kform && !curMorph->kform->empty() && (*curMorph->kform)[0] == u'어';
				
				for (auto& p : maxWidLL)
				{
					for (auto& q : p.second)
					{
						q.accScore += estimatedLL;
						// 불규칙 활용 형태소 뒤에 모음 어미가 붙는 경우 벌점 부여
						if (vowelE && isIrregular(q.lastMorpheme->tag))
						{
							q.accScore -= 10;
						}
						// 나/너/저 뒤에 주격 조사 '가'가 붙는 경우 벌점 부여
						if (infJ && isInflectendaNP(q.lastMorpheme))
						{
							q.accScore -= 5;
						}
						// ㄹ 받침 용언 뒤에 으/느/ㅅ으로 시작하는 형태소가 올 경우 벌점 부여
						if (badPairOfL && isVerbL(q.lastMorpheme))
						{
							q.accScore -= 7;
						}
						// 동사 뒤가 아니거나, 앞의 동사가 양성이 아닌데, 양성모음용 어미가 등장한 경우 벌점 부여
						if (positiveE && !isPositiveVerb(q.lastMorpheme))
						{
							q.accScore -= 100;
						}
						// 아/어로 시작하는 어미가 받침 없는 동사 뒤에서 축약되지 않은 경우 벌점 부여
						if (contractableE && isVerbVowel(q.lastMorpheme))
						{
							q.accScore -= 3;
						}
						// 형용사 사용 불가 어미인데 형용사 뒤에 등장
						if (condP == CondPolarity::non_adj && (q.lastMorpheme->tag == POSTag::va || q.lastMorpheme->tag == POSTag::xsa))
						{
							q.accScore -= 10;
						}
						if (curMorphSpecialType <= Kiwi::SpecialMorph::singleQuoteNA)
						{
							if (static_cast<uint8_t>(curMorphSpecialType) != q.spState[0])
							{
								q.accScore -= 2;
							}
						}
						else if (curMorphSpecialType <= Kiwi::SpecialMorph::doubleQuoteNA)
						{
							if ((static_cast<uint8_t>(curMorphSpecialType) - 3) != q.spState[1])
							{
								q.accScore -= 2;
							}
						}

						tMax = max(tMax, q.accScore + discountForCombining);
					}
				}

				for (auto& p : maxWidLL)
				{
					for (auto& q : p.second)
					{
						if (q.accScore <= tMax - kw->cutOffThreshold) continue;
						nCache.emplace_back(curMorph, q.accScore, q.accTypoCost, q.parent, q.lmState, q.spState);
						
						if (curMorphSpecialType == Kiwi::SpecialMorph::singleQuoteOpen) nCache.back().spState[0] = 1;
						else if (curMorphSpecialType == Kiwi::SpecialMorph::singleQuoteClose) nCache.back().spState[0] = 0;
						else if (curMorphSpecialType == Kiwi::SpecialMorph::doubleQuoteOpen) nCache.back().spState[1] = 1;
						else if (curMorphSpecialType == Kiwi::SpecialMorph::doubleQuoteClose) nCache.back().spState[1] = 0;

						auto& back = nCache.back().back;
						if (curMorph->chunks.empty() || curMorph->complex)
						{
							back.wid = oseq[0];
							back.combineSocket = combSocket;
							back.ownFormId = ownFormId;
						}
						else
						{
							auto& p = curMorph->chunks.getSecond(chSize - 1);
							back.wid = oseq[chSize - 1];
						}
					}
				}
			}
			if (!nCache.empty()) break;
		}
		return tMax;
	}


	template<class LmState>
	inline PathEvaluator::Path generateTokenList(const WordLL<LmState>* result, 
		const utils::ContainerSearcher<WordLL<LmState>>& csearcher, 
		const KGraphNode* graph, 
		const Vector<KString>& ownFormList,
		float typoCostWeight,
		const Morpheme* morphBase,
		size_t langVocabSize)
	{
		Vector<const WordLL<LmState>*> steps;
		for (auto s = result->parent; s->parent; s = s->parent)
		{
			steps.emplace_back(s);
		}

		const auto unifyMorpheme = [&](const Morpheme* morph)
		{
			if (morph >= morphBase + langVocabSize || morph->combined) return morph;
			return morphBase + morph->lmMorphemeId;
		};

		PathEvaluator::Path ret;
		const WordLL<LmState>* prev = steps.back()->parent;
		for (auto it = steps.rbegin(); it != steps.rend(); ++it)
		{
			auto cur = *it;
			float scoreDiff = cur->accScore - prev->accScore;
			float typoCostDiff = cur->accTypoCost - prev->accTypoCost;
			auto morpheme = cur->morpheme;
			size_t numNewTokens = (morpheme->chunks.empty() || morpheme->complex) ? 1 : morpheme->chunks.size();
			auto& gNode = graph[csearcher(cur)];
			scoreDiff += typoCostDiff * typoCostWeight;
			scoreDiff /= numNewTokens;
			typoCostDiff /= numNewTokens;

			if (morpheme->chunks.empty() || morpheme->complex)
			{
				ret.emplace_back(
					unifyMorpheme(morpheme),
					cur->back.ownFormId ? ownFormList[cur->back.ownFormId - 1] : KString{},
					gNode.startPos,
					gNode.endPos,
					scoreDiff,
					typoCostDiff,
					gNode.typoFormId
				);
			}
			else if (morpheme->combineSocket)
			{
				ret.back().morph = ret.back().morph->getCombined();
				ret.back().end = gNode.startPos + morpheme->chunks.getSecond(0).second;
				ret.back().wordScore = scoreDiff;
				ret.back().typoCost = typoCostDiff;
				ret.back().typoFormId = gNode.typoFormId;
				for (size_t ch = 1; ch < numNewTokens; ++ch)
				{
					auto& p = morpheme->chunks.getSecond(ch);
					ret.emplace_back(
						unifyMorpheme(morpheme->chunks[ch]),
						KString{},
						gNode.startPos + p.first,
						gNode.startPos + p.second,
						scoreDiff,
						typoCostDiff,
						gNode.typoFormId
					);
				}
			}
			else
			{
				for (size_t ch = 0; ch < numNewTokens; ++ch)
				{
					auto& p = morpheme->chunks.getSecond(ch);
					ret.emplace_back(
						unifyMorpheme(morpheme->chunks[ch]),
						KString{},
						gNode.startPos + p.first,
						gNode.startPos + p.second,
						scoreDiff,
						typoCostDiff,
						gNode.typoFormId
					);
				}
			}
			prev = cur;
		}
		return ret;
	}

	template<class LmState>
	Vector<pair<PathEvaluator::Path, float>> PathEvaluator::findBestPath(const Kiwi* kw, 
		const Vector<KGraphNode>& graph, 
		size_t topN, 
		bool openEnd,
		bool splitComplex,
		const std::unordered_set<const Morpheme*>* blocklist
	)
	{
		static constexpr size_t eosId = 1;

		Vector<Vector<WordLL<LmState>>> cache(graph.size());
		Vector<KString> ownFormList;
		Vector<const Morpheme*> unknownNodeCands, unknownNodeLCands;

		const size_t langVocabSize = kw->langMdl.knlm->getHeader().vocab_size;

		const KGraphNode* startNode = &graph.front();
		const KGraphNode* endNode = &graph.back();

		unknownNodeCands.emplace_back(kw->getDefaultMorpheme(POSTag::nng));
		unknownNodeCands.emplace_back(kw->getDefaultMorpheme(POSTag::nnp));
		unknownNodeLCands.emplace_back(kw->getDefaultMorpheme(POSTag::nnp));

		// start node
		cache.front().emplace_back(&kw->morphemes[0], 0.f, 0.f, nullptr, LmState{kw->langMdl}, SpecialState{0,});

		// middle nodes
		for (size_t i = 1; i < graph.size() - 1; ++i)
		{
			auto* node = &graph[i];
			size_t ownFormId = 0;
			if (!node->uform.empty())
			{
				ownFormList.emplace_back(node->uform);
				ownFormId = ownFormList.size();
			}
			float tMax = -INFINITY;

			if (node->form)
			{
				tMax = evalPath<LmState>(kw, startNode, node, cache, ownFormList, i, ownFormId, node->form->candidate, false, splitComplex, blocklist);
				if (all_of(node->form->candidate.begin(), node->form->candidate.end(), [](const Morpheme* m)
				{
					return m->combineSocket || !m->chunks.empty();
				}))
				{
					ownFormList.emplace_back(node->form->form);
					ownFormId = ownFormList.size();
					tMax = min(tMax, evalPath<LmState>(kw, startNode, node, cache, ownFormList, i, ownFormId, unknownNodeLCands, true, splitComplex, blocklist));
				};
			}
			else
			{
				tMax = evalPath<LmState>(kw, startNode, node, cache, ownFormList, i, ownFormId, unknownNodeCands, true, splitComplex, blocklist);
			}

			// heuristically remove cands having lower ll to speed up
			if (cache[i].size() > topN)
			{
				UnorderedMap<array<uint16_t, 4>, pair<WordLL<LmState>*, float>> bestPathes;
				float cutoffScore = -INFINITY, cutoffScoreWithCombined = -INFINITY;
				for (auto& c : cache[i])
				{
					array<uint16_t, 4> lastNgram = { 0, };
					size_t j = lastNgram.size();
					for (const WordLL<LmState>* it = &c; it && j > 0; it = it->parent, --j)
					{
						lastNgram[j - 1] = it->morpheme - kw->morphemes.data();
					}
					lastNgram[3] |= (c.spState[0] << 14) | (c.spState[1] << 15);
					auto insertResult = bestPathes.emplace(lastNgram, make_pair(&c, c.accScore));
					if (!insertResult.second)
					{
						if (c.accScore > insertResult.first->second.second)
						{
							insertResult.first->second = make_pair(&c, c.accScore);
						}
					}
					if (c.back.combineSocket)
					{
						cutoffScoreWithCombined = max(cutoffScoreWithCombined, c.accScore);
					}
					else
					{
						cutoffScore = max(cutoffScore, c.accScore);
					}
				}
				cutoffScore -= kw->cutOffThreshold;
				cutoffScoreWithCombined -= kw->cutOffThreshold;

				Vector<WordLL<LmState>> reduced;
				for (auto& p : bestPathes)
				{
					auto& c = *p.second.first;
					float cutoff = (c.back.combineSocket) ? cutoffScoreWithCombined : cutoffScore;
					if (reduced.size() < topN || c.accScore >= cutoff) reduced.emplace_back(move(c));
				}
				cache[i] = move(reduced);
			}

#ifdef DEBUG_PRINT
			cout << "== " << i << " (tMax: " << tMax << ") ==" << endl;
			for (auto& tt : cache[i])
			{
				cout << tt.accScore << '\t';
				for (auto& m : tt.morphs)
				{
					kw->morphemes[m.wid].print(cout) << '\t';
				}
				cout << endl;
			}
			cout << "========" << endl;
#endif
		}

		// end node		
		for (auto prev = endNode->getPrev(); prev; prev = prev->getSibling())
		{
			for (auto& p : cache[prev - startNode])
			{
				if (p.back.combineSocket) continue;
				if (!p.morpheme->chunks.empty() && !p.morpheme->complex)
				{
					if (p.morpheme->chunks.size() <= (p.morpheme->combineSocket ? 2 : 1))
					{
						if (!FeatureTestor::isMatched(nullptr, p.morpheme->vowel)) continue;
					}
				}

				float c = p.accScore + (openEnd ? 0 : p.lmState.next(kw->langMdl, eosId));
				if (p.spState[0]) c -= 2;
				if (p.spState[1]) c -= 2;
				cache.back().emplace_back(nullptr, c, p.accTypoCost, &p, p.lmState, p.spState);
			}
		}

		auto& cand = cache.back();
		sort(cand.begin(), cand.end(), 
			[](const WordLL<LmState>& a, const WordLL<LmState>& b) 
			{ 
				return a.accScore > b.accScore; 
			}
		);

#ifdef DEBUG_PRINT
		cout << "== LAST ==" << endl;
		for (auto& tt : cache.back())
		{
			cout << tt.accScore << '\t';
			for (auto& m : tt.morphs)
			{
				kw->morphemes[m.wid].print(cout) << '\t';
			}
			cout << endl;
		}
		cout << "========" << endl;

#endif

		utils::ContainerSearcher<WordLL<LmState>> csearcher{ cache };
		Vector<pair<Path, float>> ret;
		for (size_t i = 0; i < min(topN, cand.size()); ++i)
		{
			ret.emplace_back(generateTokenList(&cand[i], csearcher, graph.data(), ownFormList, kw->typoCostWeight, kw->morphemes.data(), langVocabSize), cand[i].accScore);
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
	* @param sentence 어절번호를 생성할 문장.
	* @return sentence 에 대한 어절번호를 담고있는 vector.
	*/
	template<class It>
	const vector<uint16_t> getWordPositions(It first, It last)
	{
		vector<uint16_t> wordPositions(distance(first, last));
		uint32_t position = 0;
		bool continuousSpace = false;

		for (size_t i = 0; first != last; ++first, ++i)
		{
			wordPositions[i] = position;

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

		return wordPositions;
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
		while(next != last)
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

	std::vector<TokenResult> Kiwi::analyzeSent(const std::u16string::const_iterator& sBegin, const std::u16string::const_iterator& sEnd, 
		size_t topN, 
		Match matchOptions, 
		bool openEnd, 
		const std::unordered_set<const Morpheme*>* blocklist
	) const
	{
		auto normalized = normalizeHangulWithPosition(sBegin, sEnd);
		auto& normalizedStr = normalized.first;
		auto& positionTable = normalized.second;

		if (!!(matchOptions & Match::normalizeCoda)) normalizeCoda(normalizedStr.begin(), normalizedStr.end());
		// 분석할 문장에 포함된 개별 문자에 대해 어절번호를 생성한다
		std::vector<uint16_t> wordPositions = getWordPositions(sBegin, sEnd);
		
		auto nodes = (*reinterpret_cast<FnSplitByTrie>(dfSplitByTrie))(
			forms.data(), 
			typoPtrs.data(), 
			formTrie, 
			normalizedStr, 
			matchOptions, 
			maxUnkFormSize, 
			spaceTolerance, 
			typoCostWeight
		);
		vector<TokenResult> ret;
		if (nodes.size() <= 2)
		{
			ret.emplace_back();
			return ret;
		}

		Vector<std::pair<PathEvaluator::Path, float>> res = (*reinterpret_cast<FnFindBestPath>(dfFindBestPath))(
			this, 
			nodes, 
			topN, 
			openEnd, 
			!!(matchOptions & Match::splitComplex), 
			blocklist
		);
		for (auto& r : res)
		{
			vector<TokenInfo> rarr;
			const KString* prevMorph = nullptr;
			for (auto& s : r.first)
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
				rarr.emplace_back(joined, s.morph->tag);
				auto& token = rarr.back();
				token.morph = s.morph;
				size_t beginPos = (upper_bound(positionTable.begin(), positionTable.end(), s.begin) - positionTable.begin()) - 1;
				size_t endPos = lower_bound(positionTable.begin(), positionTable.end(), s.end) - positionTable.begin();
				token.position = (uint32_t)beginPos;
				token.length = (uint16_t)(endPos - beginPos);
				token.score = s.wordScore;
				token.typoCost = s.typoCost;
				token.typoFormId = s.typoFormId;

				// Token의 시작위치(position)을 이용해 Token이 포함된 어절번호(wordPosition)를 얻음
				token.wordPosition = wordPositions[token.position];
				prevMorph = s.morph->kform;
			}
			rarr.erase(joinAffixTokens(rarr.begin(), rarr.end(), matchOptions), rarr.end());
			ret.emplace_back(rarr, r.second);
		}
		if (ret.empty()) ret.emplace_back();
		return ret;
	}

	const Morpheme* Kiwi::getDefaultMorpheme(POSTag tag) const
	{
		return &morphemes[getDefaultMorphemeId(tag)];
	}

	template<class Str, class ...Rest>
	auto Kiwi::_asyncAnalyze(Str&& str, Rest&&... args) const
	{
		if (!pool) throw Exception{ "`asyncAnalyze` doesn't work at single thread mode." };
		return pool->enqueue([=, str = std::forward<Str>(str)](size_t)
		{
			return analyze(str, args...);
		});
	}

	template<class Str, class ...Rest>
	auto Kiwi::_asyncAnalyzeEcho(Str&& str, Rest&&... args) const
	{
		if (!pool) throw Exception{ "`asyncAnalyze` doesn't work at single thread mode." };
		return pool->enqueue([=, str = std::forward<Str>(str)](size_t) mutable
		{
			auto ret = analyze(str, args...);
			return make_pair(move(ret), move(str));
		});
	}

	future<vector<TokenResult>> Kiwi::asyncAnalyze(const string& str, size_t topN, Match matchOptions, const std::unordered_set<const Morpheme*>* blocklist) const
	{
		return _asyncAnalyze(str, topN, matchOptions, blocklist);
	}

	future<vector<TokenResult>> Kiwi::asyncAnalyze(string&& str, size_t topN, Match matchOptions, const std::unordered_set<const Morpheme*>* blocklist) const
	{
		return _asyncAnalyze(std::move(str), topN, matchOptions, blocklist);
	}

	future<TokenResult> Kiwi::asyncAnalyze(const string& str, Match matchOptions, const std::unordered_set<const Morpheme*>* blocklist) const
	{
		return _asyncAnalyze(str, matchOptions, blocklist);
	}

	future<TokenResult> Kiwi::asyncAnalyze(string&& str, Match matchOptions, const std::unordered_set<const Morpheme*>* blocklist) const
	{
		return _asyncAnalyze(std::move(str), matchOptions, blocklist);
	}

	future<pair<TokenResult, string>> Kiwi::asyncAnalyzeEcho(string&& str, Match matchOptions, const std::unordered_set<const Morpheme*>* blocklist) const
	{
		return _asyncAnalyzeEcho(std::move(str), matchOptions, blocklist);
	}

	future<vector<TokenResult>> Kiwi::asyncAnalyze(const u16string& str, size_t topN, Match matchOptions, const std::unordered_set<const Morpheme*>* blocklist) const
	{
		return _asyncAnalyze(str, topN, matchOptions, blocklist);
	}

	future<vector<TokenResult>> Kiwi::asyncAnalyze(u16string&& str, size_t topN, Match matchOptions, const std::unordered_set<const Morpheme*>* blocklist) const
	{
		return _asyncAnalyze(std::move(str), topN, matchOptions, blocklist);
	}

	future<TokenResult> Kiwi::asyncAnalyze(const u16string& str, Match matchOptions, const std::unordered_set<const Morpheme*>* blocklist) const
	{
		return _asyncAnalyze(str, matchOptions, blocklist);
	}

	future<TokenResult> Kiwi::asyncAnalyze(u16string&& str, Match matchOptions, const std::unordered_set<const Morpheme*>* blocklist) const
	{
		return _asyncAnalyze(std::move(str), matchOptions, blocklist);
	}

	future<pair<TokenResult, u16string>> Kiwi::asyncAnalyzeEcho(u16string&& str, Match matchOptions, const std::unordered_set<const Morpheme*>* blocklist) const
	{
		return _asyncAnalyzeEcho(std::move(str), matchOptions, blocklist);
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
		const size_t* p = &typoPtrs[typoFormId];
		return joinHangul(typoPool.begin() + p[0], typoPool.begin() + p[1]);
	}

	vector<const Morpheme*> Kiwi::findMorpheme(const u16string& s, POSTag tag) const
	{
		vector<const Morpheme*> ret;
		auto normalized = normalizeHangul(s);
		auto form = (*reinterpret_cast<FnFindForm>(dfFindForm))(formTrie, normalized);
		if (!form) return ret;
		for (auto c : form->candidate)
		{
			if (c->combineSocket
				|| (tag != POSTag::unknown 
				&& clearIrregular(c->tag) != tag))
				continue;
			ret.emplace_back(c);
		}
		return ret;
	}
}
