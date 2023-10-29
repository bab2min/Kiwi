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
#include "LimitedVector.hpp"

using namespace std;

namespace kiwi
{
	struct SpecialState
	{
		uint8_t singleQuote : 1;
		uint8_t doubleQuote : 1;
		uint8_t bulletHash : 6;

		SpecialState() : singleQuote{ 0 }, doubleQuote{ 0 }, bulletHash{ 0 }
		{
		}

		operator uint8_t() const
		{
			return reinterpret_cast<const uint8_t&>(*this);
		}

		bool operator<(const SpecialState& o) const
		{
			return (uint8_t)(*this) < (uint8_t)o;
		}

		bool operator==(const SpecialState& o) const
		{
			return (uint8_t)(*this) == (uint8_t)o;
		}
	};

	template<class LmState>
	struct WordLL;

	using Wid = uint32_t;

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
			uint32_t nodeId = 0;

			Result(const Morpheme* _morph = nullptr,
				const KString& _str = {},
				uint32_t _begin = 0,
				uint32_t _end = 0,
				float _wordScore = 0,
				float _typoCost = 0,
				uint32_t _typoFormId = 0,
				uint32_t _nodeId = 0
			)
				: morph{ _morph }, str{ _str }, begin{ _begin }, end{ _end },
				wordScore{ _wordScore }, typoCost{ _typoCost }, typoFormId{ _typoFormId }, nodeId{ _nodeId }
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

		struct ChunkResult
		{
			Path path;
			float score = 0;
			SpecialState prevState;
			SpecialState curState;

			ChunkResult(Path&& _path = {}, float _score = 0, SpecialState _prevState = {}, SpecialState _curState = {})
				: path{ move(_path) }, score{ _score }, prevState{ _prevState }, curState{ _curState }
			{}

			ChunkResult(const Path& _path, float _score = 0, SpecialState _prevState = {}, SpecialState _curState = {})
				: path{ _path }, score{ _score }, prevState{ _prevState }, curState{ _curState }
			{}
		};

		template<class LmState>
		static Vector<ChunkResult> findBestPath(const Kiwi* kw,
			const Vector<SpecialState>& prevSpStates,
			const KGraphNode* graph,
			const size_t graphSize,
			const size_t topN,
			bool openEnd,
			bool splitComplex = false,
			const std::unordered_set<const Morpheme*>* blocklist = nullptr
		);

		template<class LmState, class CandTy>
		static float evalPath(const Kiwi* kw,
			const KGraphNode* startNode,
			const KGraphNode* node,
			const size_t topN,
			Vector<Vector<WordLL<LmState>>>& cache,
			const Vector<U16StringView>& ownFormList,
			size_t i,
			size_t ownFormId,
			CandTy&& cands,
			bool unknownForm,
			bool splitComplex = false,
			const std::unordered_set<const Morpheme*>* blocklist = nullptr
		);

		template<class LmState>
		static void evalSingleMorpheme(
			Vector<WordLL<LmState>>& resultOut,
			const Kiwi* kw,
			const Vector<U16StringView>& ownForms,
			const Vector<Vector<WordLL<LmState>>>& cache,
			array<Wid, 4> seq,
			array<Wid, 4> oseq,
			size_t chSize,
			uint8_t combSocket,
			size_t ownFormId,
			const Morpheme* curMorph,
			const KGraphNode* node,
			const KGraphNode* startNode,
			const size_t topN,
			const float ignoreCondScore,
			const float nodeLevelDiscount
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

	template<class LmState>
	struct WordLL
	{
		const Morpheme* morpheme = nullptr;
		float accScore = 0, accTypoCost = 0;
		const WordLL* parent = nullptr;
		LmState lmState;
		Wid wid = 0;
		uint16_t ownFormId = 0;
		uint8_t combineSocket = 0;
		SpecialState spState, rootSpState;

		WordLL() = default;

		WordLL(const Morpheme* _morph, float _accScore, float _accTypoCost, const WordLL* _parent, LmState _lmState, SpecialState _spState)
			: morpheme{ _morph },
			accScore{ _accScore },
			accTypoCost{ _accTypoCost },
			parent{ _parent },
			lmState{ _lmState },
			spState{ _spState },
			rootSpState{ parent ? parent->rootSpState : spState }
		{
		}

		const WordLL* root() const
		{
			if (parent) return parent->root();
			else return this;
		}
	};

	template<class LmState>
	struct PathHash
	{
		LmState lmState;
		uint8_t spState;

		PathHash(LmState _lmState = {}, SpecialState _spState = {})
			: lmState{ _lmState }, spState{ _spState }
		{
		}

		PathHash(const WordLL<LmState>& wordLl, const Morpheme* morphBase)
			: PathHash{ wordLl.lmState, wordLl.root()->spState }
		{
		}

		bool operator==(const PathHash& o) const
		{
			return lmState == o.lmState && spState == o.spState;
		}
	};

	template<size_t windowSize, ArchType _arch, class VocabTy>
	struct PathHash<SbgState<windowSize, _arch, VocabTy>>
	{
		using LmState = SbgState<windowSize, _arch, VocabTy>;

		KnLMState<_arch, VocabTy> lmState;
		array<VocabTy, 4> lastMorphemes;
		uint8_t spState;

		PathHash(LmState _lmState = {}, SpecialState _spState = {})
			: lmState{ _lmState }, spState{ _spState }
		{
			_lmState.getLastHistory(lastMorphemes.data(), lastMorphemes.size());
		}


		PathHash(const WordLL<LmState>& wordLl, const Morpheme* morphBase)
			: PathHash{ wordLl.lmState, wordLl.root()->spState }
		{
		}

		bool operator==(const PathHash& o) const
		{
			return lmState == o.lmState && lastMorphemes == o.lastMorphemes && spState == o.spState;
		}
	};

	template<class LmState>
	struct Hash<PathHash<LmState>>
	{
		size_t operator()(const PathHash<LmState>& p) const
		{
			size_t ret = 0;
			if (sizeof(PathHash<LmState>) % sizeof(size_t))
			{
				auto ptr = reinterpret_cast<const uint32_t*>(&p);
				for (size_t i = 0; i < sizeof(PathHash<LmState>) / sizeof(uint32_t); ++i)
				{
					ret ^= ptr[i];
				}
			}
			else
			{
				auto ptr = reinterpret_cast<const size_t*>(&p);
				for (size_t i = 0; i < sizeof(PathHash<LmState>) / sizeof(size_t); ++i)
				{
					ret ^= ptr[i];
				}
			}
			return ret;
		}
	};

	struct GenericGreater
	{
		template<class A, class B>
		bool operator()(A&& a, B&& b)
		{
			return a > b;
		}
	};

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
			if (POSTag::sf <= tag && tag <= POSTag::sb) return true;
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

	inline uint8_t hashSbTypeOrder(uint8_t type, uint8_t order)
	{
		return ((type << 1) ^ (type >> 7) ^ order) % 63 + 1;
	}

	struct RuleBasedScorer
	{
		Kiwi::SpecialMorph curMorphSpecialType;
		size_t curMorphSbType;
		int curMorphSbOrder;
		bool vowelE, infJ, badPairOfL, positiveE, contractableE;
		CondPolarity condP;

		RuleBasedScorer(const Kiwi* kw, const Morpheme* curMorph, const KGraphNode* node)
			:
			curMorphSpecialType{ kw->determineSpecialMorphType(kw->morphToId(curMorph)) },
			curMorphSbType{ curMorph->tag == POSTag::sb ? getSBType(joinHangul(*curMorph->kform)) : 0 },
			curMorphSbOrder{ curMorphSbType ? curMorph->senseId : 0 },
			vowelE{ isEClass(curMorph->tag) && curMorph->kform && hasNoOnset(*curMorph->kform) },
			infJ{ isInflectendaJ(curMorph) },
			badPairOfL{ isBadPairOfVerbL(curMorph) },
			positiveE{ isEClass(curMorph->tag) && node->form && node->form->form[0] == u'아' },
			contractableE{ isEClass(curMorph->tag) && curMorph->kform && !curMorph->kform->empty() && (*curMorph->kform)[0] == u'어' },
			condP{ curMorph->polar }
		{
		}

		float operator()(const Morpheme* prevMorpheme, const SpecialState prevSpState) const
		{
			float accScore = 0;

			// 불규칙 활용 형태소 뒤에 모음 어미가 붙는 경우 벌점 부여
			if (vowelE && isIrregular(prevMorpheme->tag))
			{
				accScore -= 10;
			}
			// 나/너/저 뒤에 주격 조사 '가'가 붙는 경우 벌점 부여
			if (infJ && isInflectendaNP(prevMorpheme))
			{
				accScore -= 5;
			}
			// ㄹ 받침 용언 뒤에 으/느/ㅅ으로 시작하는 형태소가 올 경우 벌점 부여
			if (badPairOfL && isVerbL(prevMorpheme))
			{
				accScore -= 7;
			}
			// 동사 뒤가 아니거나, 앞의 동사가 양성이 아닌데, 양성모음용 어미가 등장한 경우 벌점 부여
			if (positiveE && !isPositiveVerb(prevMorpheme))
			{
				accScore -= 100;
			}
			// 아/어로 시작하는 어미가 받침 없는 동사 뒤에서 축약되지 않은 경우 벌점 부여
			if (contractableE && isVerbVowel(prevMorpheme))
			{
				accScore -= 3;
			}
			// 형용사 사용 불가 어미인데 형용사 뒤에 등장
			if (condP == CondPolarity::non_adj && (prevMorpheme->tag == POSTag::va || prevMorpheme->tag == POSTag::xsa))
			{
				accScore -= 10;
			}
			if (curMorphSpecialType <= Kiwi::SpecialMorph::singleQuoteNA)
			{
				if (static_cast<uint8_t>(curMorphSpecialType) != prevSpState.singleQuote)
				{
					accScore -= 2;
				}
			}
			else if (curMorphSpecialType <= Kiwi::SpecialMorph::doubleQuoteNA)
			{
				if ((static_cast<uint8_t>(curMorphSpecialType) - 3) != prevSpState.doubleQuote)
				{
					accScore -= 2;
				}
			}

			// discount for SB in form "[가-하]."
			if (curMorphSbType == 5)
			{
				accScore -= 5;
			}

			if (curMorphSbType && isEClass(prevMorpheme->tag) && prevMorpheme->tag != POSTag::ef)
			{
				accScore -= 10;
			}

			if (curMorphSbType && prevSpState.bulletHash == hashSbTypeOrder(curMorphSbType, curMorphSbOrder))
			{
				accScore += 3;
			}

			return accScore;
		}
	};

	template<class LmState>
	void PathEvaluator::evalSingleMorpheme(
		Vector<WordLL<LmState>>& resultOut,
		const Kiwi* kw,
		const Vector<U16StringView>& ownForms,
		const Vector<Vector<WordLL<LmState>>>& cache,
		array<Wid, 4> seq,
		array<Wid, 4> oseq,
		size_t chSize,
		uint8_t combSocket,
		size_t ownFormId,
		const Morpheme* curMorph,
		const KGraphNode* node,
		const KGraphNode* startNode,
		const size_t topN,
		const float ignoreCondScore,
		const float nodeLevelDiscount
	)
	{
		// pair: [index, size]
		thread_local UnorderedMap<PathHash<LmState>, pair<uint32_t, uint32_t>> bestPathIndex;
		thread_local Vector<WordLL<LmState>> bestPathValues;
		bestPathIndex.clear();
		bestPathValues.clear();

		const LangModel& langMdl = kw->langMdl;
		const Morpheme* morphBase = kw->morphemes.data();
		const auto spacePenalty = kw->spacePenalty;
		const bool allowedSpaceBetweenChunk = kw->spaceTolerance > 0;

		float additionalScore = curMorph->userScore + nodeLevelDiscount;
		additionalScore += kw->tagScorer.evalLeftBoundary(hasLeftBoundary(node), curMorph->tag);

		RuleBasedScorer ruleBasedScorer{ kw, curMorph, node };

		float discountForCombining = curMorph->combineSocket ? -15 : 0;

		const size_t vocabSize = langMdl.knlm->getHeader().vocab_size;
		for (auto* prev = node->getPrev(); prev; prev = prev->getSibling())
		{
			assert(prev != node);
			for (auto& prevPath : cache[prev - startNode])
			{
				float candScore = prevPath.accScore + additionalScore;
				if (prevPath.combineSocket)
				{
					// merge <v> <chunk> with only the same socket
					if (prevPath.combineSocket != curMorph->combineSocket || (curMorph->chunks.empty() || curMorph->complex))
					{
						continue;
					}
					if (prev->endPos < node->startPos)
					{
						if (allowedSpaceBetweenChunk) candScore -= spacePenalty;
						else continue;
					}
					seq[0] = morphBase[prevPath.wid].getCombined()->lmMorphemeId;
				}

				const kchar_t* leftFormFirst, * leftFormLast;
				if (prevPath.ownFormId)
				{
					leftFormFirst = ownForms[prevPath.ownFormId - 1].data();
					leftFormLast = ownForms[prevPath.ownFormId - 1].data() + ownForms[0].size();
				}
				else if (morphBase[prevPath.wid].kform)
				{
					leftFormFirst = morphBase[prevPath.wid].kform->data();
					leftFormLast = morphBase[prevPath.wid].kform->data() + morphBase[prevPath.wid].kform->size();
				}
				else
				{
					leftFormFirst = nullptr;
					leftFormLast = nullptr;
				}

				CondVowel cvowel = curMorph->vowel;
				CondPolarity cpolar = curMorph->polar;
				if (prevPath.morpheme->tag == POSTag::ssc)
				{
					// 이전 형태소가 닫는 괄호인 경우 좌측 결합조건을 적용하지 않음
				}
				else if (ignoreCondScore)
				{
					candScore += FeatureTestor::isMatched(leftFormFirst, leftFormLast, cvowel, cpolar) ? 0 : ignoreCondScore;
				}
				else
				{
					if (!FeatureTestor::isMatched(leftFormFirst, leftFormLast, cvowel, cpolar)) continue;
				}

				auto cLmState = prevPath.lmState;
				Wid lSeq = 0;
				if (curMorph->combineSocket && (curMorph->chunks.empty() || curMorph->complex))
				{
					lSeq = prevPath.wid;
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

				{
					const auto* prevMorpheme = &morphBase[prevPath.wid];
					const auto prevSpState = prevPath.spState;
					candScore += ruleBasedScorer(prevMorpheme, prevSpState);

					PathHash<LmState> ph{ cLmState, prevPath.rootSpState };
					auto inserted = bestPathIndex.emplace(ph, make_pair(bestPathValues.size(), 1));
					if (inserted.second)
					{
						bestPathValues.emplace_back(curMorph, candScore, prevPath.accTypoCost + node->typoCost, &prevPath, move(cLmState), prevPath.spState);
					}
					else
					{
						auto& target = bestPathValues[inserted.first->second.first];
						if (candScore > target.accScore)
						{
							target = WordLL<LmState>{ curMorph, candScore, prevPath.accTypoCost + node->typoCost, &prevPath, move(cLmState), prevPath.spState };
						}
						++inserted.first->second.second;
					}
				}

			continueFor:;
			}
		}

		for (auto& p : bestPathIndex)
		{
			const auto index = p.second.first;
			const auto size = p.second.second;
			resultOut.emplace_back(move(bestPathValues[index]));
			auto& newPath = resultOut.back();

			// fill the rest information of resultOut
			if (ruleBasedScorer.curMorphSpecialType == Kiwi::SpecialMorph::singleQuoteOpen) newPath.spState.singleQuote = 1;
			else if (ruleBasedScorer.curMorphSpecialType == Kiwi::SpecialMorph::singleQuoteClose) newPath.spState.singleQuote = 0;
			else if (ruleBasedScorer.curMorphSpecialType == Kiwi::SpecialMorph::doubleQuoteOpen) newPath.spState.doubleQuote = 1;
			else if (ruleBasedScorer.curMorphSpecialType == Kiwi::SpecialMorph::doubleQuoteClose) newPath.spState.doubleQuote = 0;
			if (ruleBasedScorer.curMorphSbType)
			{
				newPath.spState.bulletHash = hashSbTypeOrder(ruleBasedScorer.curMorphSbType, ruleBasedScorer.curMorphSbOrder + 1);
			}

			if (curMorph->chunks.empty() || curMorph->complex)
			{
				newPath.wid = oseq[0];
				newPath.combineSocket = combSocket;
				newPath.ownFormId = ownFormId;
			}
			else
			{
				newPath.wid = oseq[chSize - 1];
			}
		}
		return;
	}

	template<class LmState, class CandTy>
	float PathEvaluator::evalPath(const Kiwi* kw,
		const KGraphNode* startNode,
		const KGraphNode* node,
		const size_t topN,
		Vector<Vector<WordLL<LmState>>>& cache,
		const Vector<U16StringView>& ownFormList,
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
		Vector<WordLL<LmState>> refCache;

		float whitespaceDiscount = 0;
		if (node->uform.empty() && node->endPos - node->startPos > node->form->form.size())
		{
			whitespaceDiscount = -kw->spacePenalty * (node->endPos - node->startPos - node->form->form.size());
		}
		const float typoDiscount = -node->typoCost * kw->typoCostWeight;
		float unknownFormDiscount = 0;
		if (unknownForm)
		{
			size_t unknownLen = node->uform.empty() ? node->form->form.size() : node->uform.size();
			unknownFormDiscount = -(unknownLen * kw->unkFormScoreScale + kw->unkFormScoreBias);
		}

		const float nodeLevelDiscount = whitespaceDiscount + typoDiscount + unknownFormDiscount;

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
							auto lastTag = kw->morphemes[p.wid].tag;
							if (!isJClass(lastTag) && !isEClass(lastTag)) continue;
							nCache.emplace_back(p);
							auto& newPath = nCache.back();
							newPath.accScore += curMorph->userScore * kw->typoCostWeight;
							newPath.accTypoCost -= curMorph->userScore;
							newPath.parent = &p;
							newPath.morpheme = &kw->morphemes[curMorph->lmMorphemeId];
							newPath.wid = curMorph->lmMorphemeId;
						}
					}
					continue;
				}

				array<Wid, 4> seq = { 0, };
				array<Wid, 4> oseq = { 0, };
				uint8_t combSocket = 0;
				CondVowel condV = curMorph->vowel;
				CondPolarity condP = curMorph->polar;
				size_t chSize = 1;
				// if the morpheme has chunk set
				if (!curMorph->chunks.empty() && !curMorph->complex)
				{
					chSize = curMorph->chunks.size();
					// '하다/하게/하지'가 '다/게/지'로 축약된 경우인데 앞에 공백이 있는 경우는 탐색후보에서 제외
					if (node->prev && node[-(int)node->prev].endPos < node->startPos
						&& curMorph->kform
						&& curMorph->kform->size() == 1
						&& ((*curMorph->kform)[0] == u'다' || (*curMorph->kform)[0] == u'게' || (*curMorph->kform)[0] == u'지')
						&& curMorph->chunks[0]->kform
						&& curMorph->chunks[0]->kform->size() == 1
						&& (*curMorph->chunks[0]->kform)[0] == u'하')
					{
						continue;
					}

					for (size_t i = 0; i < chSize; ++i)
					{
						seq[i] = curMorph->chunks[i]->lmMorphemeId;
						if (within(curMorph->chunks[i], kw->morphemes.data() + langVocabSize, kw->morphemes.data() + kw->morphemes.size()))
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
					if (within(curMorph->getCombined() ? curMorph->getCombined() : curMorph, kw->morphemes.data() + langVocabSize, kw->morphemes.data() + kw->morphemes.size()))
					{
						oseq[0] = curMorph - kw->morphemes.data();
					}
					else
					{
						oseq[0] = seq[0];
					}
					combSocket = curMorph->combineSocket;
				}

				evalSingleMorpheme(nCache, kw, ownFormList, cache, seq, oseq, chSize, combSocket, ownFormId, curMorph, node, startNode, topN, ignoreCond ? -10 : 0, nodeLevelDiscount);
			}
			if (!nCache.empty()) break;
		}

		tMax = -INFINITY;
		for (auto& c : nCache)
		{
			if (c.morpheme->combineSocket) continue;
			tMax = max(tMax, c.accScore);
		}

		size_t validCount = 0;
		for (size_t i = 0; i < nCache.size(); ++i)
		{
			if (nCache[i].accScore + kw->cutOffThreshold < tMax) continue;
			if (validCount != i) nCache[validCount] = move(nCache[i]);
			validCount++;
		}
		nCache.resize(validCount);
		return tMax;
	}


	template<class LmState>
	inline pair<PathEvaluator::Path, const WordLL<LmState>*> generateTokenList(const WordLL<LmState>* result,
		const utils::ContainerSearcher<WordLL<LmState>>& csearcher,
		const KGraphNode* graph,
		const Vector<U16StringView>& ownFormList,
		float typoCostWeight,
		const Morpheme* morphFirst,
		size_t langVocabSize)
	{
		Vector<const WordLL<LmState>*> steps;
		for (auto s = result->parent; s->parent; s = s->parent)
		{
			steps.emplace_back(s);
		}

		const auto unifyMorpheme = [&](const Morpheme* morph)
		{
			if (!within(morph, morphFirst, morphFirst + langVocabSize) || morph->combined) return morph;
			return morphFirst + morph->lmMorphemeId;
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
					cur->ownFormId ? KString{ ownFormList[cur->ownFormId - 1].data(), ownFormList[cur->ownFormId - 1].size() } : KString{},
					gNode.startPos,
					gNode.endPos,
					scoreDiff,
					typoCostDiff,
					typoCostDiff ? gNode.typoFormId : 0,
					&gNode - graph
				);
			}
			else if (morpheme->combineSocket)
			{
				ret.back().morph = ret.back().morph->getCombined();
				ret.back().end = gNode.startPos + morpheme->chunks.getSecond(0).second;
				ret.back().wordScore = scoreDiff;
				ret.back().typoCost = typoCostDiff;
				ret.back().typoFormId = typoCostDiff ? gNode.typoFormId : 0;
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
						typoCostDiff ? gNode.typoFormId : 0,
						&gNode - graph
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
						typoCostDiff ? gNode.typoFormId : 0,
						&gNode - graph
					);
				}
			}
			prev = cur;
		}
		return make_pair(ret, steps.back()->parent);
	}

	template<class LmState>
	Vector<PathEvaluator::ChunkResult> PathEvaluator::findBestPath(const Kiwi* kw,
		const Vector<SpecialState>& prevSpStates,
		const KGraphNode* graph,
		const size_t graphSize,
		const size_t topN,
		bool openEnd,
		bool splitComplex,
		const std::unordered_set<const Morpheme*>* blocklist
	)
	{
		static constexpr size_t eosId = 1;

		Vector<Vector<WordLL<LmState>>> cache(graphSize);
		Vector<U16StringView> ownFormList;
		Vector<const Morpheme*> unknownNodeCands, unknownNodeLCands;

		const size_t langVocabSize = kw->langMdl.knlm->getHeader().vocab_size;

		const KGraphNode* startNode = graph;
		const KGraphNode* endNode = graph + graphSize - 1;

		unknownNodeCands.emplace_back(kw->getDefaultMorpheme(POSTag::nng));
		unknownNodeCands.emplace_back(kw->getDefaultMorpheme(POSTag::nnp));
		unknownNodeLCands.emplace_back(kw->getDefaultMorpheme(POSTag::nnp));

		// start node
		if (prevSpStates.empty())
		{
			cache[0].emplace_back(&kw->morphemes[0], 0.f, 0.f, nullptr, LmState{ kw->langMdl }, SpecialState{});
		}
		else
		{
			auto uniqStates = prevSpStates;
			sort(uniqStates.begin(), uniqStates.end());
			uniqStates.erase(unique(uniqStates.begin(), uniqStates.end()), uniqStates.end());
			for (auto& spState : uniqStates)
			{
				cache[0].emplace_back(&kw->morphemes[0], 0.f, 0.f, nullptr, LmState{ kw->langMdl }, spState);
			}
		}

		// middle nodes
		for (size_t i = 1; i < graphSize - 1; ++i)
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
				tMax = evalPath<LmState>(kw, startNode, node, topN, cache, ownFormList, i, ownFormId, node->form->candidate, false, splitComplex, blocklist);
				if (all_of(node->form->candidate.begin(), node->form->candidate.end(), [](const Morpheme* m)
				{
					return m->combineSocket || (!m->chunks.empty() && !m->complex);
				}))
				{
					ownFormList.emplace_back(node->form->form);
					ownFormId = ownFormList.size();
					tMax = min(tMax, evalPath<LmState>(kw, startNode, node, topN, cache, ownFormList, i, ownFormId, unknownNodeLCands, true, splitComplex, blocklist));
				};
			}
			else
			{
				tMax = evalPath<LmState>(kw, startNode, node, topN, cache, ownFormList, i, ownFormId, unknownNodeCands, true, splitComplex, blocklist);
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
				if (p.combineSocket) continue;
				if (!p.morpheme->chunks.empty() && !p.morpheme->complex)
				{
					if (p.morpheme->chunks.size() <= (p.morpheme->combineSocket ? 2 : 1))
					{
						if (!FeatureTestor::isMatched(nullptr, p.morpheme->vowel)) continue;
					}
				}

				float c = p.accScore + (openEnd ? 0 : p.lmState.next(kw->langMdl, eosId));
				if (p.spState.singleQuote) c -= 2;
				if (p.spState.doubleQuote) c -= 2;
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
		Vector<ChunkResult> ret;
		for (size_t i = 0; i < min(topN, cand.size()); ++i)
		{
			auto tokens = generateTokenList(
				&cand[i], csearcher, graph, ownFormList, kw->typoCostWeight,
				kw->morphemes.data(), langVocabSize
			);
			ret.emplace_back(move(tokens.first), cand[i].accScore, tokens.second->spState, cand[i].spState);
		}
		return ret;
	}
}
