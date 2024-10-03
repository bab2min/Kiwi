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

	enum class PathEvaluatingMode
	{
		topN,
		top1,
		top1Small,
	};

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
		static void evalPath(const Kiwi* kw,
			const KGraphNode* startNode,
			const KGraphNode* node,
			const size_t topN,
			Vector<Vector<WordLL<LmState>>>& cache,
			const Vector<U16StringView>& ownFormList,
			size_t i,
			size_t ownFormId,
			CandTy&& cands,
			bool unknownForm,
			const Vector<SpecialState>& prevSpStates,
			bool splitComplex = false,
			const std::unordered_set<const Morpheme*>* blocklist = nullptr
		);

		template<PathEvaluatingMode mode, class LmState>
		static void evalSingleMorpheme(
			Vector<WordLL<LmState>>& resultOut,
			const Kiwi* kw,
			const Vector<U16StringView>& ownForms,
			const Vector<Vector<WordLL<LmState>>>& cache,
			size_t ownFormId,
			const Morpheme* curMorph,
			const KGraphNode* node,
			const KGraphNode* startNode,
			const size_t topN,
			const float ignoreCondScore,
			const float nodeLevelDiscount,
			const Vector<SpecialState>& prevSpStates
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
		uint8_t rootId = 0;
		SpecialState spState;

		WordLL() = default;

		WordLL(const Morpheme* _morph, float _accScore, float _accTypoCost, const WordLL* _parent, LmState _lmState, SpecialState _spState)
			: morpheme{ _morph },
			accScore{ _accScore },
			accTypoCost{ _accTypoCost },
			parent{ _parent },
			lmState{ _lmState },
			spState{ _spState },
			rootId{ parent ? parent->rootId : (uint8_t)0 }
		{
		}

		const WordLL* root() const
		{
			if (parent) return parent->root();
			else return this;
		}
	};

	static constexpr uint8_t commonRootId = -1;

	template<class LmState>
	struct PathHash
	{
		LmState lmState;
		uint8_t rootId, spState;

		PathHash(LmState _lmState = {}, uint8_t _rootId = 0, SpecialState _spState = {})
			: lmState{ _lmState }, rootId{ _rootId }, spState { _spState }
		{
		}

		PathHash(const WordLL<LmState>& wordLl, const Morpheme* morphBase)
			: PathHash{ wordLl.lmState, wordLl.rootId, wordLl.spState }
		{
		}

		bool operator==(const PathHash& o) const
		{
			return lmState == o.lmState && rootId == o.rootId && spState == o.spState;
		}
	};

	template<size_t windowSize, ArchType _arch, class VocabTy>
	struct PathHash<SbgState<windowSize, _arch, VocabTy>>
	{
		using LmState = SbgState<windowSize, _arch, VocabTy>;

		KnLMState<_arch, VocabTy> lmState;
		array<VocabTy, 4> lastMorphemes;
		uint8_t rootId, spState;

		PathHash(LmState _lmState = {}, uint8_t _rootId = 0, SpecialState _spState = {})
			: lmState{ _lmState }, rootId{ _rootId }, spState{ _spState }
		{
			_lmState.getLastHistory(lastMorphemes.data(), lastMorphemes.size());
		}


		PathHash(const WordLL<LmState>& wordLl, const Morpheme* morphBase)
			: PathHash{ wordLl.lmState, wordLl.rootId, wordLl.spState }
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

	struct WordLLGreater
	{
		template<class LmState>
		bool operator()(const WordLL<LmState>& a, const WordLL<LmState>& b) const
		{
			return a.accScore > b.accScore;
		}
	};

	template<class LmState>
	inline std::ostream& printDebugPath(std::ostream& os, const WordLL<LmState>& src)
	{
		if (src.parent)
		{
			printDebugPath(os, *src.parent);
		}
		
		if (src.morpheme) src.morpheme->print(os);
		else os << "NULL";
		os << " , ";
		return os;
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

	inline bool isQuote(Kiwi::SpecialMorph m)
	{
		return m == Kiwi::SpecialMorph::singleQuoteOpen || m == Kiwi::SpecialMorph::singleQuoteClose
			|| m == Kiwi::SpecialMorph::doubleQuoteOpen || m == Kiwi::SpecialMorph::doubleQuoteClose;
	}

	template<PathEvaluatingMode mode, class LmState>
	class BestPathConatiner;

	template<class LmState>
	class BestPathConatiner<PathEvaluatingMode::topN, LmState>
	{
		// pair: [index, size]
		UnorderedMap<PathHash<LmState>, pair<uint32_t, uint32_t>> bestPathIndex;
		Vector<WordLL<LmState>> bestPathValues;
	public:
		inline void clear()
		{
			bestPathIndex.clear();
			bestPathValues.clear();
		}

		inline void insert(const PathHash<LmState>& ph, size_t topN, uint8_t rootId, 
			const Morpheme* morph, float accScore, float accTypoCost, const WordLL<LmState>* parent, LmState&& lmState, SpecialState spState)
		{
			auto inserted = bestPathIndex.emplace(ph, make_pair((uint32_t)bestPathValues.size(), 1));
			if (inserted.second)
			{
				bestPathValues.emplace_back(morph, accScore, accTypoCost, parent, move(lmState), spState);
				if (rootId != commonRootId) bestPathValues.back().rootId = rootId;
				bestPathValues.resize(bestPathValues.size() + topN - 1);
			}
			else
			{
				auto bestPathFirst = bestPathValues.begin() + inserted.first->second.first;
				auto bestPathLast = bestPathValues.begin() + inserted.first->second.first + inserted.first->second.second;
				if (distance(bestPathFirst, bestPathLast) < topN)
				{
					*bestPathLast = WordLL<LmState>{ morph, accScore, accTypoCost, parent, move(lmState), spState };
					if (rootId != commonRootId) bestPathLast->rootId = rootId;
					push_heap(bestPathFirst, bestPathLast + 1, WordLLGreater{});
					++inserted.first->second.second;
				}
				else
				{
					if (accScore > bestPathFirst->accScore)
					{
						pop_heap(bestPathFirst, bestPathLast, WordLLGreater{});
						*(bestPathLast - 1) = WordLL<LmState>{ morph, accScore, accTypoCost, parent, move(lmState), spState };
						if (rootId != commonRootId) (*(bestPathLast - 1)).rootId = rootId;
						push_heap(bestPathFirst, bestPathLast, WordLLGreater{});
					}
				}
			}
		}

		inline void writeTo(Vector<WordLL<LmState>>& resultOut, const Morpheme* curMorph, Wid lastSeqId, size_t ownFormId)
		{
			for (auto& p : bestPathIndex)
			{
				const auto index = p.second.first;
				const auto size = p.second.second;
				for (size_t i = 0; i < size; ++i)
				{
					resultOut.emplace_back(move(bestPathValues[index + i]));
					auto& newPath = resultOut.back();

					// fill the rest information of resultOut
					newPath.wid = lastSeqId;
					if (curMorph->chunks.empty() || curMorph->complex)
					{
						newPath.combineSocket = curMorph->combineSocket;
						newPath.ownFormId = ownFormId;
					}
				}
			}
		}
	};

	template<class LmState>
	class BestPathConatiner<PathEvaluatingMode::top1, LmState>
	{
		UnorderedMap<PathHash<LmState>, WordLL<LmState>> bestPathes;
	public:
		inline void clear()
		{
			bestPathes.clear();
		}

		inline void insert(const PathHash<LmState>& ph, size_t topN, uint8_t rootId, 
			const Morpheme* morph, float accScore, float accTypoCost, const WordLL<LmState>* parent, LmState&& lmState, SpecialState spState)
		{
			WordLL<LmState> newPath{ morph, accScore, accTypoCost, parent, move(lmState), spState };
			if (rootId != commonRootId) newPath.rootId = rootId;
			auto inserted = bestPathes.emplace(ph, newPath);
			if (!inserted.second)
			{
				auto& target = inserted.first->second;
				if (accScore > target.accScore)
				{
					target = newPath;
				}
			}
		}

		inline void writeTo(Vector<WordLL<LmState>>& resultOut, const Morpheme* curMorph, Wid lastSeqId, size_t ownFormId)
		{
			for (auto& p : bestPathes)
			{
				resultOut.emplace_back(move(p.second));
				auto& newPath = resultOut.back();

				// fill the rest information of resultOut
				newPath.wid = lastSeqId;
				if (curMorph->chunks.empty() || curMorph->complex)
				{
					newPath.combineSocket = curMorph->combineSocket;
					newPath.ownFormId = ownFormId;
				}
			}
		}
	};

	template<class LmState>
	class BestPathConatiner<PathEvaluatingMode::top1Small, LmState>
	{
		Vector<PathHash<LmState>> bestPathIndicesSmall;
		Vector<WordLL<LmState>> bestPathValuesSmall;
	public:

		inline void clear()
		{
			bestPathIndicesSmall.clear();
			bestPathValuesSmall.clear();
		}

		inline void insert(const PathHash<LmState>& ph, size_t topN, uint8_t rootId, 
			const Morpheme* morph, float accScore, float accTypoCost, const WordLL<LmState>* parent, LmState&& lmState, SpecialState spState)
		{
			const auto it = find(bestPathIndicesSmall.begin(), bestPathIndicesSmall.end(), ph);
			if (it == bestPathIndicesSmall.end())
			{
				bestPathIndicesSmall.push_back(ph);
				bestPathValuesSmall.emplace_back(morph, accScore, accTypoCost, parent, move(lmState), spState);
				if (rootId != commonRootId) bestPathValuesSmall.back().rootId = rootId;
			}
			else
			{
				auto& target = bestPathValuesSmall[it - bestPathIndicesSmall.begin()];
				if (accScore > target.accScore)
				{
					target = WordLL<LmState>{ morph, accScore, accTypoCost, parent, move(lmState), spState };
					if (rootId != commonRootId) target.rootId = rootId;
				}
			}
		}

		inline void writeTo(Vector<WordLL<LmState>>& resultOut, const Morpheme* curMorph, Wid lastSeqId, size_t ownFormId)
		{
			for (auto& p : bestPathValuesSmall)
			{
				resultOut.emplace_back(move(p));
				auto& newPath = resultOut.back();

				// fill the rest information of resultOut
				newPath.wid = lastSeqId;
				if (curMorph->chunks.empty() || curMorph->complex)
				{
					newPath.combineSocket = curMorph->combineSocket;
					newPath.ownFormId = ownFormId;
				}
			}
		}
	};

	template<PathEvaluatingMode mode, class LmState>
	void PathEvaluator::evalSingleMorpheme(
		Vector<WordLL<LmState>>& resultOut,
		const Kiwi* kw,
		const Vector<U16StringView>& ownForms,
		const Vector<Vector<WordLL<LmState>>>& cache,
		size_t ownFormId,
		const Morpheme* curMorph,
		const KGraphNode* node,
		const KGraphNode* startNode,
		const size_t topN,
		const float ignoreCondScore,
		const float nodeLevelDiscount,
		const Vector<SpecialState>& prevSpStates
	)
	{
		thread_local BestPathConatiner<mode, LmState> bestPathCont;
		thread_local Vector<uint8_t> rootIds;

		const LangModel& langMdl = kw->langMdl;
		const Morpheme* morphBase = kw->morphemes.data();
		const auto spacePenalty = kw->spacePenalty;
		const bool allowedSpaceBetweenChunk = kw->spaceTolerance > 0;

		const size_t langVocabSize = langMdl.knlm->getHeader().vocab_size;

		const Morpheme* lastMorph;
		Wid firstWid;
		if (curMorph->chunks.empty() || curMorph->complex)
		{
			lastMorph = curMorph->getCombined() ? curMorph->getCombined() : curMorph;
			firstWid = curMorph->lmMorphemeId;
		}
		// if the morpheme has chunk set
		else
		{
			lastMorph = curMorph->chunks[curMorph->chunks.size() - 1];
			firstWid = curMorph->chunks[0]->lmMorphemeId;
		}

		Wid lastSeqId;
		if (within(lastMorph, kw->morphemes.data() + langVocabSize, kw->morphemes.data() + kw->morphemes.size()))
		{
			lastSeqId = lastMorph - kw->morphemes.data();
		}
		else
		{
			lastSeqId = lastMorph->lmMorphemeId;
		}


		bestPathCont.clear();
		const float additionalScore = curMorph->userScore + nodeLevelDiscount + kw->tagScorer.evalLeftBoundary(hasLeftBoundary(node), curMorph->tag);

		RuleBasedScorer ruleBasedScorer{ kw, curMorph, node };

		for (auto* prev = node->getPrev(); prev; prev = prev->getSibling())
		{
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
					firstWid = morphBase[prevPath.wid].getCombined()->lmMorphemeId;
				}

				const kchar_t* leftFormFirst, * leftFormLast;
				if (prevPath.ownFormId)
				{
					leftFormFirst = ownForms[prevPath.ownFormId - 1].data();
					leftFormLast = leftFormFirst + ownForms[0].size();
				}
				else if (morphBase[prevPath.wid].kform && !morphBase[prevPath.wid].kform->empty())
				{
					leftFormFirst = morphBase[prevPath.wid].kform->data();
					leftFormLast = leftFormFirst + morphBase[prevPath.wid].kform->size();
				}
				else
				{
					leftFormFirst = prevPath.morpheme->getForm().data();
					leftFormLast = leftFormFirst + prevPath.morpheme->getForm().size();
				}

				const CondVowel cvowel = curMorph->vowel;
				const CondPolarity cpolar = curMorph->polar;
				const bool leftFormEndswithSSC = leftFormFirst < leftFormLast && identifySpecialChr(leftFormLast[-1]) == POSTag::ssc;
				if (prevPath.morpheme->tag == POSTag::ssc || leftFormEndswithSSC)
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
				if (curMorph->combineSocket && (curMorph->chunks.empty() || curMorph->complex))
				{
					// no-op
				}
				else
				{
					if (morphBase[firstWid].tag == POSTag::p)
					{
						// prohibit <v> without <chunk>
						goto continueFor;
					}
					float ll = cLmState.next(langMdl, firstWid);
					candScore += ll;
					if (!(curMorph->chunks.empty() || curMorph->complex))
					{
						for (size_t i = 1; i < curMorph->chunks.size(); ++i)
						{
							const auto wid = curMorph->chunks[i]->lmMorphemeId;
							if (morphBase[wid].tag == POSTag::p)
							{
								// prohibit <v> without <chunk>
								goto continueFor;
							}
							ll = cLmState.next(langMdl, wid);
							candScore += ll;
						}
					}
				}

				if ((ruleBasedScorer.curMorphSbType || isQuote(ruleBasedScorer.curMorphSpecialType)) && prevPath.rootId == commonRootId)
				{
					rootIds.resize(prevSpStates.size());
					iota(rootIds.begin(), rootIds.end(), 0);
				}
				else
				{
					rootIds.resize(1);
					rootIds[0] = commonRootId;
				}

				for (auto rootId : rootIds)
				{
					const auto* prevMorpheme = &morphBase[prevPath.wid];
					auto spState = prevPath.spState;
					if (rootId != commonRootId)
					{
						spState = prevSpStates[rootId];
					}
					const float candScoreWithRule = candScore + ruleBasedScorer(prevMorpheme, spState);

					// update special state
					if (ruleBasedScorer.curMorphSpecialType == Kiwi::SpecialMorph::singleQuoteOpen) spState.singleQuote = 1;
					else if (ruleBasedScorer.curMorphSpecialType == Kiwi::SpecialMorph::singleQuoteClose) spState.singleQuote = 0;
					else if (ruleBasedScorer.curMorphSpecialType == Kiwi::SpecialMorph::doubleQuoteOpen) spState.doubleQuote = 1;
					else if (ruleBasedScorer.curMorphSpecialType == Kiwi::SpecialMorph::doubleQuoteClose) spState.doubleQuote = 0;
					if (ruleBasedScorer.curMorphSbType)
					{
						spState.bulletHash = hashSbTypeOrder(ruleBasedScorer.curMorphSbType, ruleBasedScorer.curMorphSbOrder + 1);
					}

					PathHash<LmState> ph{ cLmState, prevPath.rootId, spState };
					bestPathCont.insert(ph, topN, rootId, curMorph, candScoreWithRule, prevPath.accTypoCost + node->typoCost, &prevPath, move(cLmState), spState);
				}

			continueFor:;
			}
		}

		bestPathCont.writeTo(resultOut, curMorph, lastSeqId, ownFormId);
		return;
	}

	template<class LmState, class CandTy>
	void PathEvaluator::evalPath(const Kiwi* kw,
		const KGraphNode* startNode,
		const KGraphNode* node,
		const size_t topN,
		Vector<Vector<WordLL<LmState>>>& cache,
		const Vector<U16StringView>& ownFormList,
		size_t i,
		size_t ownFormId,
		CandTy&& cands,
		bool unknownForm,
		const Vector<SpecialState>& prevSpStates,
		bool splitComplex,
		const std::unordered_set<const Morpheme*>* blocklist
	)
	{
		const size_t langVocabSize = kw->langMdl.knlm->getHeader().vocab_size;
		auto& nCache = cache[i];
		Vector<WordLL<LmState>> refCache;

		float whitespaceDiscount = 0;
		if (node->uform.empty() && !node->form->form.empty() && node->spaceErrors)
		{
			whitespaceDiscount = -kw->spacePenalty * node->spaceErrors;
		}
		const float typoDiscount = -node->typoCost * kw->typoCostWeight;
		float unknownFormDiscount = 0;
		if (unknownForm)
		{
			size_t unknownLen = node->uform.empty() ? node->form->form.size() : node->uform.size();
			unknownFormDiscount = -(unknownLen * kw->unkFormScoreScale + kw->unkFormScoreBias);
		}

		const float nodeLevelDiscount = whitespaceDiscount + typoDiscount + unknownFormDiscount;

		size_t totalPrevPathes = 0;
		for (auto* prev = node->getPrev(); prev; prev = prev->getSibling())
		{
			totalPrevPathes += cache[prev - startNode].size();
		}
		const bool useContainerForSmall = totalPrevPathes <= 48;

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

				// if the morpheme has chunk set
				if (!(curMorph->chunks.empty()|| curMorph->complex))
				{
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
				}

				if (topN > 1)
				{
					evalSingleMorpheme<PathEvaluatingMode::topN>(nCache, kw, ownFormList, cache, 
						ownFormId, curMorph, 
						node, startNode, topN, ignoreCond ? -10 : 0, nodeLevelDiscount, prevSpStates);
				}
				else if (useContainerForSmall)
				{
					evalSingleMorpheme<PathEvaluatingMode::top1Small>(nCache, kw, ownFormList, cache, 
						ownFormId, curMorph, 
						node, startNode, topN, ignoreCond ? -10 : 0, nodeLevelDiscount, prevSpStates);
				}
				else
				{
					evalSingleMorpheme<PathEvaluatingMode::top1>(nCache, kw, ownFormList, cache,
						ownFormId, curMorph,
						node, startNode, topN, ignoreCond ? -10 : 0, nodeLevelDiscount, prevSpStates);
				}
				
			}
			if (!nCache.empty()) break;
		}

		thread_local Vector<float> maxScores;
		maxScores.clear();
		maxScores.resize((1 + prevSpStates.size()) * topN, -INFINITY);

		if (topN == 1)
		{
			for (auto& c : nCache)
			{
				if (c.morpheme->combineSocket) continue;
				const auto rootId = c.rootId == commonRootId ? 0 : c.rootId + 1;
				maxScores[rootId] = max(maxScores[rootId], c.accScore);
			}
		}
		else
		{
			for (auto& c : nCache)
			{
				if (c.morpheme->combineSocket) continue;
				const auto rootId = c.rootId == commonRootId ? 0 : c.rootId + 1;
				if (c.accScore > maxScores[rootId * topN])
				{
					pop_heap(maxScores.begin() + rootId * topN, maxScores.begin() + (rootId + 1) * topN, greater<float>{});
					maxScores[rootId * topN + topN - 1] = c.accScore;
					push_heap(maxScores.begin() + rootId * topN, maxScores.begin() + (rootId + 1) * topN, greater<float>{});
				}
			}
		}

		size_t validCount = 0;
		for (size_t i = 0; i < nCache.size(); ++i)
		{
			const auto rootId = nCache[i].rootId == commonRootId ? 0 : nCache[i].rootId + 1;
			if (nCache[i].accScore + kw->cutOffThreshold < maxScores[rootId * topN]) continue;
			if (validCount != i) nCache[validCount] = move(nCache[i]);
			validCount++;
		}
		nCache.resize(validCount);
	}


	template<class LmState>
	inline PathEvaluator::Path generateTokenList(const WordLL<LmState>* result,
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
				ret.back().end = gNode.endPos;
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
				ret.back().end = gNode.endPos;
			}
			prev = cur;
		}
		return ret;
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

		auto uniqStates = prevSpStates;
		sort(uniqStates.begin(), uniqStates.end());
		uniqStates.erase(unique(uniqStates.begin(), uniqStates.end()), uniqStates.end());
		if (prevSpStates.empty())
		{
			uniqStates.emplace_back();
		}

		// start node
		cache[0].emplace_back(&kw->morphemes[0], 0.f, 0.f, nullptr, LmState{ kw->langMdl }, SpecialState{});
		cache[0].back().rootId = commonRootId;

#ifdef DEBUG_PRINT
		cerr << "Token[" << 0 << "]" << endl;
		for (auto& tt : cache[0])
		{
			cerr << "(" << tt.accScore << "):\t";
			printDebugPath(cerr, tt);
			cerr << endl;
		}
#endif

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

			if (node->form)
			{
				evalPath<LmState>(kw, startNode, node, topN, cache, ownFormList, i, ownFormId, node->form->candidate, false, uniqStates, splitComplex, blocklist);
				if (all_of(node->form->candidate.begin(), node->form->candidate.end(), [](const Morpheme* m)
				{
					return m->combineSocket || (!m->chunks.empty() && !m->complex);
				}))
				{
					ownFormList.emplace_back(node->form->form);
					ownFormId = ownFormList.size();
					evalPath<LmState>(kw, startNode, node, topN, cache, ownFormList, i, ownFormId, unknownNodeLCands, true, uniqStates, splitComplex, blocklist);
				};
			}
			else
			{
				evalPath<LmState>(kw, startNode, node, topN, cache, ownFormList, i, ownFormId, unknownNodeCands, true, uniqStates, splitComplex, blocklist);
			}

#ifdef DEBUG_PRINT
			cerr << "Token[" << i << "]" << endl;
			for (auto& tt : cache[i])
			{
				cerr << "(" << tt.accScore << "):\t";
				printDebugPath(cerr, tt);
				cerr << endl;
			}
#endif
		}

		// end node		
		auto& cand = cache.back();
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
				if (p.rootId == commonRootId)
				{
					for (size_t i = 0; i < uniqStates.size(); ++i)
					{
						cand.emplace_back(nullptr, c, p.accTypoCost, &p, p.lmState, uniqStates[i]);
						cand.back().rootId = i;
					}
				}
				else
				{
					cand.emplace_back(nullptr, c, p.accTypoCost, &p, p.lmState, p.spState);
				}
			}
		}

		sort(cand.begin(), cand.end(),
			[](const WordLL<LmState>& a, const WordLL<LmState>& b)
			{
				if (a.rootId < b.rootId) return true;
				if (a.rootId > b.rootId) return false;
				if (a.spState < b.spState) return true;
				if (a.spState > b.spState) return false;
				return a.accScore > b.accScore;
			}
		);

#ifdef DEBUG_PRINT
		cerr << "Token[last]" << endl;
		for (auto& tt : cache.back())
		{
			cerr << "(" << tt.accScore << "):\t";
			printDebugPath(cerr, tt);
			cerr << endl;
		}
#endif

		utils::ContainerSearcher<WordLL<LmState>> csearcher{ cache };
		Vector<ChunkResult> ret;
		size_t numUniqRootIdAndSpState;
		{
			UnorderedSet<pair<uint8_t, uint8_t>> uniqRootIdAndSpState;
			for (auto& c : cand)
			{
				uniqRootIdAndSpState.emplace(c.rootId, (uint8_t)c.spState);
			}
			numUniqRootIdAndSpState = uniqRootIdAndSpState.size();
		}

		const size_t numCandsPerRootIdAndSpState = (size_t)std::ceil(topN * 2 / (double)numUniqRootIdAndSpState);
		size_t startIdx = 0;
		pair<uint8_t, uint8_t> prevRootIdAndSpState;
		if (!cand.empty()) prevRootIdAndSpState = make_pair(cand[0].rootId, (uint8_t)cand[0].spState);
		for (size_t i = 0; i < cand.size(); ++i)
		{
			auto curRootIdAndSpState = make_pair(cand[i].rootId, (uint8_t)cand[i].spState);
			if (prevRootIdAndSpState != curRootIdAndSpState)
			{
				startIdx = i;
				prevRootIdAndSpState = curRootIdAndSpState;
			}

			if (i - startIdx < numCandsPerRootIdAndSpState)
			{
				auto tokens = generateTokenList(
					&cand[i], csearcher, graph, ownFormList, kw->typoCostWeight,
					kw->morphemes.data(), langVocabSize
				);
				ret.emplace_back(move(tokens), cand[i].accScore, uniqStates[cand[i].rootId], cand[i].spState);
			}
		}
		sort(ret.begin(), ret.end(), [](const ChunkResult& a, const ChunkResult& b)
		{
			return a.score > b.score;
		});
		return ret;
	}
}
