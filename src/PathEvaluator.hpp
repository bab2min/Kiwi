#include <fstream>

#include <kiwi/Kiwi.h>
#include <kiwi/Utils.h>
#include <kiwi/TemplateUtils.hpp>
#include <kiwi/Form.h>
#include <kiwi/LangModel.h>
#include <kiwi/Dataset.h>
#include "ArchAvailable.h"
#include "KTrie.h"
#include "FeatureTestor.h"
#include "FrozenTrie.hpp"
#include "StrUtils.h"
#include "SortUtils.hpp"
#include "LimitedVector.hpp"
#include "UnkFormScorer.h"
#include "PathEvaluator.h"
#include "BestPathContainer.hpp"

using namespace std;

namespace kiwi
{
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
		bool vowelE, infJ, badPairOfL, positiveE, contractableE, snEndswithPoint;
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
			snEndswithPoint{ curMorph->tag == POSTag::sn && !node->uform.empty() && node->uform.back() == u'.'},
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

			// discount for SN ending with point at beginning of sentence
			if (snEndswithPoint && (prevMorpheme->tag == POSTag::unknown || prevMorpheme->tag == POSTag::ef || prevMorpheme->tag == POSTag::sf))
			{
				accScore -= 5;
			}

			return accScore;
		}
	};

	inline bool isQuote(Kiwi::SpecialMorph m)
	{
		return m == Kiwi::SpecialMorph::singleQuoteOpen || m == Kiwi::SpecialMorph::singleQuoteClose
			|| m == Kiwi::SpecialMorph::doubleQuoteOpen || m == Kiwi::SpecialMorph::doubleQuoteClose;
	}


	template<PathEvaluatingMode mode, class WordLL>
	inline void insertToPathContainer(
		BestPathConatiner<mode, WordLL>& bestPathCont,
		const size_t topN,
		const Vector<SpecialState>& prevSpStates,
		const Morpheme* curMorph,
		const Morpheme* morphBase,
		typename WordLL::LmState&& state,
		const float score,
		const float firstChunkScore,
		const KGraphNode* node,
		const WordLL* base,
		const WordLL& prevPath,
		const RuleBasedScorer& ruleBasedScorer,
		const float dialectCost
	)
	{
		const auto insert = [&](uint8_t rootId)
		{
			const auto* prevMorpheme = &morphBase[prevPath.wid];
			auto spState = prevPath.spState;
			if (rootId != commonRootId)
			{
				spState = prevSpStates[rootId];
			}
			const float ruleScore = ruleBasedScorer(prevMorpheme, spState);
			const float candScoreWithRule = score + ruleScore;
			const float firstChunkScoreWithRule = firstChunkScore + ruleScore;

			// update special state
			if (ruleBasedScorer.curMorphSpecialType == Kiwi::SpecialMorph::singleQuoteOpen) spState.singleQuote = 1;
			else if (ruleBasedScorer.curMorphSpecialType == Kiwi::SpecialMorph::singleQuoteClose) spState.singleQuote = 0;
			else if (ruleBasedScorer.curMorphSpecialType == Kiwi::SpecialMorph::doubleQuoteOpen) spState.doubleQuote = 1;
			else if (ruleBasedScorer.curMorphSpecialType == Kiwi::SpecialMorph::doubleQuoteClose) spState.doubleQuote = 0;
			if (ruleBasedScorer.curMorphSbType)
			{
				spState.bulletHash = hashSbTypeOrder(ruleBasedScorer.curMorphSbType, ruleBasedScorer.curMorphSbOrder + 1);
			}

			const float curDialectCost = curMorph->dialect == Dialect::standard ? 0.f : dialectCost;
			bestPathCont.insert(topN, prevPath.rootId, rootId, curMorph, 
				candScoreWithRule - curDialectCost,
				firstChunkScoreWithRule - curDialectCost,
				base, &prevPath - base, move(state), spState);
		};

		if ((ruleBasedScorer.curMorphSbType || isQuote(ruleBasedScorer.curMorphSpecialType)) && prevPath.rootId == commonRootId)
		{
			for (uint8_t rootId = 0; rootId < prevSpStates.size(); ++rootId)
			{
				insert(rootId);
			}
		}
		else
		{
			insert(commonRootId);
		}
	}

	class FormEvaluator
	{
		const kchar_t* leftFormFirst;
		const kchar_t* leftFormLast;
		bool leftFormEndswithSSC;
		POSTag prevTag;

	public:
		template<class WordLL>
		FormEvaluator(const WordLL& prevPath, 
			const Vector<U16StringView>& ownFormList, 
			const Morpheme* morphBase
		)
		{
			if (prevPath.ownFormId)
			{
				leftFormFirst = ownFormList[prevPath.ownFormId - 1].data();
				leftFormLast = leftFormFirst + ownFormList[prevPath.ownFormId - 1].size();
			}
			else if (morphBase[prevPath.wid].kform && !morphBase[prevPath.wid].kform->empty())
			{
				leftFormFirst = morphBase[prevPath.wid].kform->data();
				leftFormLast = leftFormFirst + morphBase[prevPath.wid].kform->size();
			}
			else if (prevPath.morpheme->tag == POSTag::unknown && !prevPath.morpheme->chunks.empty())
			{
				// pretokenized morpheme이 이전 형태소인 경우
				const auto* lastMorph = prevPath.morpheme->chunks[prevPath.morpheme->chunks.size() - 1];
				leftFormFirst = lastMorph->getForm().data();
				leftFormLast = leftFormFirst + lastMorph->getForm().size();
			}
			else
			{
				leftFormFirst = prevPath.morpheme->getForm().data();
				leftFormLast = leftFormFirst + prevPath.morpheme->getForm().size();
			}
			leftFormEndswithSSC = leftFormFirst < leftFormLast && identifySpecialChr(leftFormLast[-1]) == POSTag::ssc;
			prevTag = prevPath.morpheme->tag;
		}

		bool operator()(const Morpheme* curMorph, const float ignoreCondScore, float& candScore) const
		{
			const CondVowel cvowel = curMorph->vowel;
			const CondPolarity cpolar = curMorph->polar;
			if (prevTag == POSTag::ssc || leftFormEndswithSSC)
			{
				// 이전 형태소가 닫는 괄호인 경우 좌측 결합조건을 적용하지 않음
			}
			else if (ignoreCondScore)
			{
				candScore += FeatureTestor::isMatched(leftFormFirst, leftFormLast, cvowel, cpolar) ? 0 : ignoreCondScore;
			}
			else
			{
				if (!FeatureTestor::isMatched(leftFormFirst, leftFormLast, cvowel, cpolar)) return false;
			}
			return true;
		}
	};

	template<class LmState>
	struct LmEvalData
	{
		LmState state;
		float score = 0, firstChunkScore = 0;
		uint32_t length = 0;
	};

	template<class WordLL, class Enable = void>
	struct PathEvaluator;

	template<class WordLL>
	struct PathEvaluator<WordLL, typename std::enable_if<!WordLL::LmState::transposed>::type>
	{
		const Kiwi* kw;
		const KiwiConfig& config;
		const KGraphNode* startNode;
		const size_t topN;
		Vector<WordLL>& pathes;
		Vector<size_t>& pathIndices;
		const Vector<U16StringView>& ownFormList;
		const Vector<SpecialState>& prevSpStates;

		PathEvaluator(const Kiwi* _kw,
			const KiwiConfig& _config,
			const KGraphNode* _startNode,
			size_t _topN,
			Vector<WordLL>& _pathes,
			Vector<size_t>& _pathIndices,
			const Vector<U16StringView>& _ownFormList,
			const Vector<SpecialState>& _prevSpStates
		)
			: kw{ _kw }, config{ _config }, startNode{ _startNode }, topN{ _topN },
			pathes{ _pathes }, pathIndices{ _pathIndices }, ownFormList{ _ownFormList }, prevSpStates{ _prevSpStates }
		{
		}

		template<class CandTy>
		void operator()(
			const size_t nodeIdx,
			const size_t ownFormId,
			CandTy&& cands,
			float unkFormDiscount,
			bool splitComplex = false,
			bool splitSaisiot = false,
			bool mergeSaisiot = false,
			const std::unordered_set<const Morpheme*>* blocklist = nullptr,
			Dialect allowedDialect = Dialect::standard,
			float dialectCost = 0.f
			) const
		{
			const size_t langVocabSize = kw->langMdl->vocabSize();
			auto* const node = startNode + nodeIdx;
			const size_t prevPathSize = pathes.size();
			
			float whitespaceDiscount = 0;
			if (node->uform.empty() && !node->form->form.empty() && node->spaceErrors)
			{
				whitespaceDiscount = -config.spacePenalty * node->spaceErrors;
			}
			const float typoDiscount = -node->typoCost * config.typoCostWeight;
			const float nodeLevelDiscount = whitespaceDiscount + typoDiscount + unkFormDiscount;

			size_t totalPrevPathes = 0;
			for (auto* prev = node->getPrev(); prev; prev = prev->getSibling())
			{
				totalPrevPathes += pathIndices[prev - startNode + 1] - pathIndices[prev - startNode];
			}

			for (bool ignoreCond : {false, true})
			{
				for (auto& curMorph : cands)
				{
					if (splitComplex && curMorph->hasComplex()) continue;
					if (blocklist && curMorph->hasMorpheme(*blocklist)) continue;
					if (curMorph->dialect != Dialect::standard && !(curMorph->dialect & allowedDialect)) continue;

					// 덧붙은 받침(zCoda)을 위한 지름길
					if (curMorph->tag == POSTag::z_coda)
					{
						for (auto* prev = node->getPrev(); prev; prev = prev->getSibling())
						{
							for (size_t p = pathIndices[prev - startNode]; p < pathIndices[prev - startNode + 1]; ++p)
							{
								auto lastTag = kw->morphemes[pathes[p].wid].tag;
								if (!isJClass(lastTag) && !isEClass(lastTag)) continue;
								pathes.emplace_back(pathes[p]);
								auto& newPath = pathes.back();
								newPath.accScore += curMorph->userScore * config.typoCostWeight;
								newPath.parent = p;
								newPath.morpheme = &kw->morphemes[curMorph->lmMorphemeId];
								newPath.wid = curMorph->lmMorphemeId;
							}
						}
						continue;
					}
					// 사이시옷(zSiot)을 위한 지름길
					if (curMorph->tag == POSTag::z_siot)
					{
						if (!(splitSaisiot || mergeSaisiot))
						{
							continue;
						}

						for (auto* prev = node->getPrev(); prev; prev = prev->getSibling())
						{
							for (size_t p = pathIndices[prev - startNode]; p < pathIndices[prev - startNode + 1]; ++p)
							{
								auto lastTag = kw->morphemes[pathes[p].wid].tag;
								if (!isNNClass(lastTag)) continue;
								pathes.emplace_back(pathes[p]);
								auto& newPath = pathes.back();
								newPath.accScore += curMorph->userScore * config.typoCostWeight;
								newPath.parent = p;
								newPath.morpheme = &kw->morphemes[curMorph->lmMorphemeId];
								newPath.wid = curMorph->lmMorphemeId;
							}
						}
						continue;
					}

					// if the morpheme has chunk set
					if (!curMorph->isSingle())
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
						evalSingleMorpheme<PathEvaluatingMode::topN>(pathes, node, ownFormId,
							curMorph, ignoreCond ? -10 : 0, nodeLevelDiscount, dialectCost);
					}
					else if (totalPrevPathes <= BestPathContainerTraits<PathEvaluatingMode::top1Small>::maxSize)
					{
						evalSingleMorpheme<PathEvaluatingMode::top1Small>(pathes, node, ownFormId,
							curMorph, ignoreCond ? -10 : 0, nodeLevelDiscount, dialectCost);
					}
					else if (totalPrevPathes <= BestPathContainerTraits<PathEvaluatingMode::top1Medium>::maxSize)
					{
						evalSingleMorpheme<PathEvaluatingMode::top1Medium>(pathes, node, ownFormId,
							curMorph, ignoreCond ? -10 : 0, nodeLevelDiscount, dialectCost);
					}
					else
					{
						evalSingleMorpheme<PathEvaluatingMode::top1>(pathes, node, ownFormId,
							curMorph, ignoreCond ? -10 : 0, nodeLevelDiscount, dialectCost);
					}

				}
				if (pathes.size() > prevPathSize) break;
			}

			thread_local Vector<float> maxScores;
			maxScores.clear();
			maxScores.resize((1 + prevSpStates.size()) * topN, -INFINITY);

			if (topN == 1)
			{
				for (size_t p = prevPathSize; p < pathes.size(); ++p)
				{
					auto& c = pathes[p];
					if (c.morpheme->combineSocket) continue;
					const auto rootId = c.rootId == commonRootId ? 0 : c.rootId + 1;
					maxScores[rootId] = max(maxScores[rootId], c.accScore);
				}
			}
			else
			{
				for (size_t p = prevPathSize; p < pathes.size(); ++p)
				{
					auto& c = pathes[p];
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

			size_t validPosition = prevPathSize;
			for (size_t p = prevPathSize; p < pathes.size(); ++p)
			{
				auto& c = pathes[p];
				const auto rootId = c.rootId == commonRootId ? 0 : c.rootId + 1;
				if (c.accScore + config.cutOffThreshold < maxScores[rootId * topN]) continue;
				if (validPosition != p) pathes[validPosition] = move(c);
				validPosition++;
			}
			pathes.resize(validPosition);
			pathIndices[nodeIdx + 1] = pathes.size();
		}

		template<PathEvaluatingMode mode>
		void evalSingleMorpheme(
			Vector<WordLL>& resultOut,
			const KGraphNode* node,
			const size_t ownFormId,
			const Morpheme* curMorph,
			const float ignoreCondScore,
			const float nodeLevelDiscount,
			const float dialectCost
		) const
		{
			thread_local BestPathConatiner<mode, WordLL> bestPathCont;
			
			const auto* langMdl = kw->getLangModel();
			const Morpheme* morphBase = kw->morphemes.data();
			const auto spacePenalty = config.spacePenalty;
			const bool allowedSpaceBetweenChunk = config.spaceTolerance > 0;

			const size_t langVocabSize = langMdl->vocabSize();

			const Morpheme* lastMorph;
			Wid firstWid;
			if (curMorph->isSingle())
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
				for (size_t p = pathIndices[prev - startNode]; p < pathIndices[prev - startNode + 1]; ++p)
				{
					auto& prevPath = pathes[p];
					// 사이시옷 뒤에 명사가 아닌 태그가 오거나 공백이 있는 경우 제외
					if (prevPath.morpheme->tag == POSTag::z_siot && (
						!isNNClass(curMorph->tag) || prev->endPos < node->startPos
						))
					{
						continue;
					}

					float candScore = prevPath.accScore + additionalScore;
					float firstChunkScore = additionalScore;
					if (prevPath.combineSocket)
					{
						// merge <v> <chunk> with only the same socket
						if (prevPath.combineSocket != curMorph->combineSocket || curMorph->isSingle())
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

					FormEvaluator formEvaluator{ prevPath, ownFormList, morphBase };
					if (!formEvaluator(curMorph, ignoreCondScore, candScore)) continue;

					auto cLmState = prevPath.lmState;
					if (curMorph->combineSocket && curMorph->isSingle())
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
						firstChunkScore += ll;
						if (!curMorph->isSingle())
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

					insertToPathContainer(bestPathCont, topN, prevSpStates, curMorph, morphBase, 
						move(cLmState), candScore, firstChunkScore, node, pathes.data(), prevPath, ruleBasedScorer, dialectCost);
				continueFor:;
				}
			}

			bestPathCont.writeTo(resultOut, curMorph, lastSeqId, ownFormId);
		}
	};

	template<class WordLL>
	struct MorphemeEvaluator
	{
		template<PathEvaluatingMode mode>
		void eval(
			Vector<WordLL>& resultOut,
			const Kiwi* kw,
			const KiwiConfig& config,
			const Vector<U16StringView>& ownForms,
			const Vector<WordLL>& pathes,
			const Vector<size_t>& pathIndices,
			size_t ownFormId,
			const Vector<const Morpheme*>& morphs,
			const KGraphNode* node,
			const KGraphNode* startNode,
			const size_t topN,
			const size_t totalPrevPathes,
			const float ignoreCondScore,
			const float nodeLevelDiscount,
			const float dialectCost,
			const Vector<SpecialState>& prevSpStates
		) const
		{
			thread_local BestPathConatiner<mode, WordLL> bestPathCont;
			thread_local Vector<LmEvalData<typename WordLL::LmState>> evalMatrix;
			thread_local Vector<Wid> nextWids;

			const auto* langMdl = kw->getLangModel();
			const Morpheme* morphBase = kw->morphemes.data();
			const auto spacePenalty = config.spacePenalty;
			const bool allowedSpaceBetweenChunk = config.spaceTolerance > 0;
			const size_t langVocabSize = langMdl->vocabSize();

			evalMatrix.resize(totalPrevPathes * morphs.size());
			nextWids.clear();

			size_t prevId = -1;
			size_t length;
			for (auto* prev = node->getPrev(); prev; prev = prev->getSibling())
			{
				for (size_t p = pathIndices[prev - startNode]; p < pathIndices[prev - startNode + 1]; ++p)
				{
					auto& prevPath = pathes[p];
					++prevId;
					FormEvaluator formEvaluator{ prevPath, ownForms, morphBase };
					for (size_t curId = 0; curId < morphs.size(); ++curId)
					{
						const auto curMorph = morphs[curId];
						float candScore = prevPath.accScore + curMorph->userScore + nodeLevelDiscount;
						float firstChunkScore = curMorph->userScore + nodeLevelDiscount;
						Wid firstWid;
						if (curMorph->isSingle())
						{
							firstWid = curMorph->lmMorphemeId;
						}
						else
						{
							firstWid = curMorph->chunks[0]->lmMorphemeId;
						}

						// 사이시옷 뒤에 명사가 아닌 태그가 오거나 공백이 있는 경우 제외
						if (prevPath.morpheme->tag == POSTag::z_siot && (
							!isNNClass(curMorph->tag) || prev->endPos < node->startPos
							))
						{
							goto invalidCandidate;
						}

						if (prevPath.combineSocket)
						{
							// merge <v> <chunk> with only the same socket
							if (prevPath.combineSocket != curMorph->combineSocket || curMorph->isSingle())
							{
								goto invalidCandidate;
							}
							if (prev->endPos < node->startPos)
							{
								if (allowedSpaceBetweenChunk) candScore -= spacePenalty;
								else goto invalidCandidate;
							}
							firstWid = morphBase[prevPath.wid].getCombined()->lmMorphemeId;
						}

						if (!formEvaluator(curMorph, ignoreCondScore, candScore)) continue;

						length = 0;
						if (curMorph->combineSocket && curMorph->isSingle())
						{
							// no op
						}
						else
						{
							if (morphBase[firstWid].tag == POSTag::p)
							{
								goto invalidCandidate;
							}

							if (curMorph->isSingle())
							{
								length = 1;
							}
							else
							{
								length = curMorph->chunks.size();
								for (size_t i = 1; i < length; ++i)
								{
									const Wid wid = curMorph->chunks[i]->lmMorphemeId;
									if (morphBase[wid].tag == POSTag::p)
									{
										goto invalidCandidate;
									}
								}
							}
						}
						evalMatrix[prevId * morphs.size() + curId].state = prevPath.lmState;
						evalMatrix[prevId * morphs.size() + curId].score = candScore;
						evalMatrix[prevId * morphs.size() + curId].firstChunkScore = firstChunkScore;
						evalMatrix[prevId * morphs.size() + curId].length = length;
						if (length > 0) nextWids.emplace_back(firstWid);
						if (length > 1)
						{
							for (size_t i = 1; i < length; ++i)
							{
								nextWids.emplace_back(curMorph->chunks[i]->lmMorphemeId);
							}
						}
						continue;
					invalidCandidate:
						evalMatrix[prevId * morphs.size() + curId].score = -INFINITY;
						evalMatrix[prevId * morphs.size() + curId].length = 0;
					}
				}
			}

			{
				size_t widOffset = 0;
				for (auto& e : evalMatrix)
				{
					//if (e.length == 0) continue;
					float score = 0;
					score += e.state.next(langMdl, nextWids[widOffset]);
					e.firstChunkScore += score;
					for (size_t i = 1; i < e.length; ++i)
					{
						score += e.state.next(langMdl, nextWids[widOffset + i]);
					}
					e.score += score;
					widOffset += e.length;
				}
			}

			for (size_t curId = 0; curId < morphs.size(); ++curId)
			{
				const auto curMorph = morphs[curId];
				bestPathCont.clear();

				const Morpheme* lastMorph;
				if (curMorph->isSingle())
				{
					lastMorph = curMorph->getCombined() ? curMorph->getCombined() : curMorph;
				}
				// if the morpheme has chunk set
				else
				{
					lastMorph = curMorph->chunks[curMorph->chunks.size() - 1];
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

				RuleBasedScorer ruleBasedScorer{ kw, curMorph, node };
				const float morphScore = kw->tagScorer.evalLeftBoundary(hasLeftBoundary(node), curMorph->tag);
				size_t prevId = -1;
				for (auto* prev = node->getPrev(); prev; prev = prev->getSibling())
				{
					for (size_t p = pathIndices[prev - startNode]; p < pathIndices[prev - startNode + 1]; ++p)
					{
						auto& prevPath = pathes[p];
						++prevId;
						auto& em = evalMatrix[prevId * morphs.size() + curId];
						if (em.score < -99999)
						{
							continue;
						}

						insertToPathContainer(bestPathCont, topN, prevSpStates, curMorph, morphBase, 
							move(em.state), em.score, em.firstChunkScore, node, pathes.data(), prevPath, ruleBasedScorer, dialectCost);
					}
				}

				bestPathCont.writeTo(resultOut, curMorph, lastSeqId, ownFormId);
			}
		}
	};

	template<class WordLL>
	struct PathEvaluator<WordLL, typename enable_if<WordLL::LmState::transposed>::type>
	{
		const Kiwi* kw;
		const KiwiConfig& config;
		const KGraphNode* startNode;
		const size_t topN;
		Vector<WordLL>& pathes;
		Vector<size_t>& pathIndices;
		const Vector<U16StringView>& ownFormList;
		const Vector<SpecialState>& prevSpStates;

		PathEvaluator(const Kiwi* _kw,
			const KiwiConfig& _config,
			const KGraphNode* _startNode,
			size_t _topN,
			Vector<WordLL>& _pathes,
			Vector<size_t>& _pathIndices,
			const Vector<U16StringView>& _ownFormList,
			const Vector<SpecialState>& _prevSpStates
		)
			: kw{ _kw }, config{ _config }, startNode{ _startNode }, topN{ _topN }, 
			pathes{ _pathes }, pathIndices{ _pathIndices }, ownFormList{ _ownFormList }, prevSpStates{ _prevSpStates }
		{
		}

		template<class CandTy>
		void operator()(
			const size_t nodeIdx,
			const size_t ownFormId,
			CandTy&& cands,
			float unkFormDiscount,
			bool splitComplex = false,
			bool splitSaisiot = false,
			bool mergeSaisiot = false,
			const std::unordered_set<const Morpheme*>* blocklist = nullptr,
			Dialect allowedDialect = Dialect::standard,
			float dialectCost = 0.f
			) const
		{
			thread_local Vector<float> maxScores;
			thread_local Vector<const Morpheme*> validMorphCands;
			const size_t langVocabSize = kw->langMdl->vocabSize();
			auto* const node = startNode + nodeIdx;
			const size_t prevPathSize = pathes.size();

			float whitespaceDiscount = 0;
			if (node->uform.empty() && !node->form->form.empty() && node->spaceErrors)
			{
				whitespaceDiscount = -config.spacePenalty * node->spaceErrors;
			}
			const float typoDiscount = -node->typoCost * config.typoCostWeight;
			const float nodeLevelDiscount = whitespaceDiscount + typoDiscount + unkFormDiscount;
			const Morpheme* zCodaMorph = nullptr;
			const Morpheme* zSiotMorph = nullptr;
			validMorphCands.clear();
			for (auto& curMorph : cands)
			{
				if (splitComplex && curMorph->hasComplex()) continue;
				if (blocklist && curMorph->hasMorpheme(*blocklist)) continue;
				if (curMorph->dialect != Dialect::standard && !(curMorph->dialect & allowedDialect)) continue;

				// 덧붙은 받침(zCoda)을 위한 지름길
				if (curMorph->tag == POSTag::z_coda)
				{
					zCodaMorph = curMorph;
					continue;
				}
				else if (curMorph->tag == POSTag::z_siot)
				{
					zSiotMorph = curMorph;
					continue;
				}

				if (!curMorph->isSingle())
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
				validMorphCands.emplace_back(curMorph);
			}

			for (bool ignoreCond : {false, true})
			{
				// 덧붙은 받침(zCoda)을 위한 지름길
				if (zCodaMorph)
				{
					for (auto* prev = node->getPrev(); prev; prev = prev->getSibling())
					{
						for (size_t p = pathIndices[prev - startNode]; p < pathIndices[prev - startNode + 1]; ++p)
						{
							auto lastTag = kw->morphemes[pathes[p].wid].tag;
							if (!isJClass(lastTag) && !isEClass(lastTag)) continue;
							pathes.emplace_back(pathes[p]);
							auto& newPath = pathes.back();
							newPath.accScore += zCodaMorph->userScore * config.typoCostWeight;
							newPath.parent = p;
							newPath.morpheme = &kw->morphemes[zCodaMorph->lmMorphemeId];
							newPath.wid = zCodaMorph->lmMorphemeId;
						}
					}
				}
				// 사이시옷(zSiot)을 위한 지름길
				if (zSiotMorph && (splitSaisiot || mergeSaisiot))
				{
					for (auto* prev = node->getPrev(); prev; prev = prev->getSibling())
					{
						for (size_t p = pathIndices[prev - startNode]; p < pathIndices[prev - startNode + 1]; ++p)
						{
							auto lastTag = kw->morphemes[pathes[p].wid].tag;
							if (!isNNClass(lastTag)) continue;
							pathes.emplace_back(pathes[p]);
							auto& newPath = pathes.back();
							newPath.accScore += zSiotMorph->userScore * config.typoCostWeight;
							newPath.parent = p;
							newPath.morpheme = &kw->morphemes[zSiotMorph->lmMorphemeId];
							newPath.wid = zSiotMorph->lmMorphemeId;
						}
					}
				}

				size_t totalPrevPathes = 0;
				for (auto* prev = node->getPrev(); prev; prev = prev->getSibling())
				{
					totalPrevPathes += pathIndices[prev - startNode + 1] - pathIndices[prev - startNode];
				}

				MorphemeEvaluator<WordLL> me;
				if (topN > 1)
				{
					me.template eval<PathEvaluatingMode::topN>(pathes, kw, config, ownFormList, pathes, pathIndices,
						ownFormId, validMorphCands,
						node, startNode, topN, totalPrevPathes, ignoreCond ? -10 : 0, nodeLevelDiscount, dialectCost, prevSpStates);
				}
				else if (totalPrevPathes <= BestPathContainerTraits<PathEvaluatingMode::top1Small>::maxSize)
				{
					me.template eval<PathEvaluatingMode::top1Small>(pathes, kw, config, ownFormList, pathes, pathIndices,
						ownFormId, validMorphCands,
						node, startNode, topN, totalPrevPathes, ignoreCond ? -10 : 0, nodeLevelDiscount, dialectCost, prevSpStates);
				}
				else if (totalPrevPathes <= BestPathContainerTraits<PathEvaluatingMode::top1Medium>::maxSize)
				{
					me.template eval<PathEvaluatingMode::top1Medium>(pathes, kw, config, ownFormList, pathes, pathIndices,
						ownFormId, validMorphCands,
						node, startNode, topN, totalPrevPathes, ignoreCond ? -10 : 0, nodeLevelDiscount, dialectCost, prevSpStates);
				}
				else
				{
					me.template eval<PathEvaluatingMode::top1>(pathes, kw, config, ownFormList, pathes, pathIndices,
						ownFormId, validMorphCands,
						node, startNode, topN, totalPrevPathes, ignoreCond ? -10 : 0, nodeLevelDiscount, dialectCost, prevSpStates);
				}
				if (pathes.size() > prevPathSize) break;
			}

			maxScores.clear();
			maxScores.resize((1 + prevSpStates.size()) * topN, -INFINITY);

			if (topN == 1)
			{
				for (size_t p = prevPathSize; p < pathes.size(); ++p)
				{
					auto& c = pathes[p];
					if (c.morpheme->combineSocket) continue;
					const auto rootId = c.rootId == commonRootId ? 0 : c.rootId + 1;
					maxScores[rootId] = max(maxScores[rootId], c.accScore);
				}
			}
			else
			{
				for (size_t p = prevPathSize; p < pathes.size(); ++p)
				{
					auto& c = pathes[p];
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

			size_t validPosition = prevPathSize;
			for (size_t p = prevPathSize; p < pathes.size(); ++p)
			{
				auto& c = pathes[p];
				const auto rootId = c.rootId == commonRootId ? 0 : c.rootId + 1;
				if (c.accScore + config.cutOffThreshold < maxScores[rootId * topN]) continue;
				if (validPosition != p) pathes[validPosition] = move(c);
				validPosition++;
			}
			pathes.resize(validPosition);
			pathIndices[nodeIdx + 1] = pathes.size();
		}
	};

	template<class WordLL>
	inline Path generateTokenList(const WordLL* base,
		size_t resultIdx,
		const utils::ContainerSearcher& csearcher,
		const KGraphNode* graph,
		const Vector<U16StringView>& ownFormList,
		float typoCostWeight,
		float dialectCost,
		const Morpheme* morphFirst,
		size_t langVocabSize,
		bool splitSaisiot)
	{
		Vector<const WordLL*> steps;
		for (auto s = base[resultIdx].parent; s > 0; s = base[s].parent)
		{
			steps.emplace_back(&base[s]);
		}

		const auto unifyMorpheme = [&](const Morpheme* morph)
		{
			if (!within(morph, morphFirst, morphFirst + langVocabSize) || morph->combined) return morph;
			return morphFirst + morph->lmMorphemeId;
		};

		Path ret;
		const WordLL* prev = base;
		for (auto it = steps.rbegin(); it != steps.rend(); ++it)
		{
			auto cur = *it;
			auto& gNode = graph[csearcher(cur - base)];

			const float scoreDiff = cur->accScore - prev->accScore;
			float typoCostDiff = gNode.typoCost;
			if (cur->morpheme->tag == POSTag::z_coda || cur->morpheme->tag == POSTag::z_siot)
			{
				typoCostDiff -= cur->morpheme->userScore;
			}
			float dialectCostDiff = cur->morpheme->dialect == Dialect::standard ? 0 : dialectCost;
			auto morpheme = cur->morpheme;
			const size_t numNewTokens = (splitSaisiot && morpheme->saisiot) || !(morpheme->chunks.empty() || morpheme->complex || morpheme->saisiot) 
				? morpheme->chunks.size() : 1;
			
			const float firstScore = cur->firstChunkScore + typoCostDiff * typoCostWeight; // typoCost는 첫번째 토큰이 전부 받았었으므로
			const float restScores = numNewTokens > 1 ? (scoreDiff - cur->firstChunkScore) / (numNewTokens - 1) : 0;
			typoCostDiff /= numNewTokens;
			dialectCostDiff /= numNewTokens;

			if (splitSaisiot && morpheme->saisiot)
			{
				for (size_t ch = 0; ch < numNewTokens; ++ch)
				{
					auto& p = morpheme->chunks.getSecond(ch);
					ret.emplace_back(
						unifyMorpheme(morpheme->chunks[ch]),
						KString{},
						gNode.startPos + p.first,
						gNode.startPos + p.second,
						ch == 0 ? firstScore : restScores,
						typoCostDiff,
						dialectCostDiff,
						typoCostDiff ? gNode.typoFormId : 0,
						&gNode - graph
					);
				}
				ret.back().end = gNode.endPos;
			}
			else if (morpheme->chunks.empty() || morpheme->complex || morpheme->saisiot)
			{
				ret.emplace_back(
					unifyMorpheme(morpheme),
					cur->ownFormId ? KString{ ownFormList[cur->ownFormId - 1].data(), ownFormList[cur->ownFormId - 1].size() } : KString{},
					gNode.startPos,
					gNode.endPos,
					firstScore,
					typoCostDiff,
					dialectCostDiff,
					typoCostDiff ? gNode.typoFormId : 0,
					&gNode - graph
				);
			}
			else if (morpheme->combineSocket)
			{
				ret.back().morph = ret.back().morph->getCombined();
				ret.back().end = gNode.startPos + morpheme->chunks.getSecond(0).second;
				ret.back().wordScore = firstScore;
				ret.back().typoCost = typoCostDiff;
				ret.back().dialectCost = dialectCostDiff;
				ret.back().typoFormId = typoCostDiff ? gNode.typoFormId : 0;
				for (size_t ch = 1; ch < numNewTokens; ++ch)
				{
					auto& p = morpheme->chunks.getSecond(ch);
					ret.emplace_back(
						unifyMorpheme(morpheme->chunks[ch]),
						KString{},
						gNode.startPos + p.first,
						gNode.startPos + p.second,
						restScores,
						typoCostDiff,
						dialectCostDiff,
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
						ch == 0 ? firstScore : restScores,
						typoCostDiff,
						dialectCostDiff,
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

	inline bool isDisconnected(const KGraphNode* graph, size_t graphSize, Vector<uint8_t>& reachable, size_t scanStart)
	{
		if (reachable[scanStart - 1]) return false;

		fill(reachable.begin() + scanStart, reachable.end(), 0);
		for (size_t i = scanStart; i < graphSize; ++i)
		{
			for (auto prev = graph[i].getPrev(); prev; prev = prev->getSibling())
			{
				if (reachable[prev - graph])
				{
					reachable[i] = 1;
					break;
				}
			}
		}
		return reachable[graphSize - 1] == 0;
	}

	template<class LangModel>
	template<bool useOOVGlobalConsistency>
	Vector<PathResult> BestPathFinder<LangModel>::findBestPathDispatched(
		const Kiwi* kw,
		const KiwiConfig& config,
		const Vector<SpecialState>& prevSpStates,
		const KString& normForm,
		const KGraphNode* graph,
		const size_t graphSize,
		const size_t topN,
		const size_t oovScoringType,
		bool openEnding,
		bool splitComplex,
		bool splitSaisiot,
		bool mergeSaisiot,
		const std::unordered_set<const Morpheme*>* blocklist,
		Dialect allowedDialects,
		float dialectCost,
		const SubstringCounter* substringCounter
	)
	{
		static constexpr size_t eosId = 1;
		using LmState = typename LangModel::LmStateType;
		using WordLLTy = WordLL<LmState, useOOVGlobalConsistency>;
		const auto* langMdl = kw->getLangModel();

		thread_local Vector<WordLLTy> pathes;
		thread_local Vector<size_t> pathIndices;
		pathes.clear();
		pathIndices.clear();
		pathIndices.resize(graphSize + 1, 0);
		thread_local UnorderedMap<uint32_t, uint32_t> oovGlobalCntArenaMap[2];
		Vector<uint8_t> reachable(graphSize, 0);
		Vector<U16StringView> ownFormList;
		Vector<const Morpheme*> unknownNodeCands, unknownNodeLCands;

		const size_t langVocabSize = langMdl->vocabSize();

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
		pathes.emplace_back(&kw->morphemes[0], 0.f, 0.f, 0, LmState{ langMdl }, SpecialState{}, commonRootId);
		pathIndices[1] = 1;
		reachable[0] = 1;

#ifdef DEBUG_PRINT
		cerr << "Token[" << 0 << "]" << endl;
		for (size_t p = pathIndices[0]; p < pathIndices[1]; ++p)
		{
			auto& tt = pathes[p];
			cerr << "(" << tt.accScore << "):\t";
			printDebugPath(cerr, tt);
			cerr << endl;
		}
#endif

		PathEvaluator<WordLLTy> evaluator{
			kw, config, startNode, topN, pathes, pathIndices, ownFormList, uniqStates,
		};
		
		UnkFormScorer unkFormScorer{ 
			config.oovRuleScale, 
			config.oovRuleBias,
			oovScoringType >= (size_t)Match::oovChrModel ? kw->nounChrMdl.get() : nullptr,
			config.oovChrBias,
			substringCounter,
			config.oovGlobalWeight,
			config.oovLocalWeight,
			config.oovGlobalMinFreq,
			oovScoringType >= (size_t)Match::oovChrFreqBranchModel
		};

		// middle nodes
		for (size_t i = 1; i < graphSize - 1; ++i)
		{
			auto* node = &graph[i];
			const bool isPretokenizedNode = (
				node->form 
				&& node->form->candidate.size() == 1 
				&& node->form->candidate[0]->tag == POSTag::unknown 
				&& !node->form->candidate[0]->chunks.empty()
			);
			size_t ownFormId = 0;
			if (!node->uform.empty())
			{
				ownFormList.emplace_back(node->uform);
				ownFormId = ownFormList.size();
			}

			if (node->form)
			{
				evaluator(i, ownFormId, node->form->candidate, 
					0.f, splitComplex, splitSaisiot, mergeSaisiot, blocklist, allowedDialects, dialectCost);
				if (!isPretokenizedNode && node->typoCost == 0 && node->typoFormId == 0
					&& all_of(node->form->candidate.begin(), node->form->candidate.end(), [](const Morpheme* m)
				{
					return m->combineSocket || !(m->chunks.empty() || m->complex || m->saisiot);
				}))
				{
					ownFormList.emplace_back(node->form->form);
					ownFormId = ownFormList.size();
					const float unkScore = unkFormScorer(node->form->form);
					evaluator(i, ownFormId, unknownNodeLCands, 
						unkScore, splitComplex, splitSaisiot, mergeSaisiot, blocklist, allowedDialects, dialectCost);
				};

				reachable[i] = any_of(pathes.begin() + pathIndices[i], pathes.begin() + pathIndices[i + 1],
					[](const auto& p) { return !p.combineSocket; }) ? 1 : 0;

				if (isDisconnected(graph, graphSize, reachable, i + 1))
				{
					ownFormList.emplace_back(U16StringView{ normForm }.substr(node->startPos, node->endPos - node->startPos));
					ownFormId = ownFormList.size();

					const float unkScore = unkFormScorer(ownFormList.back());
					evaluator(i, ownFormId, unknownNodeCands,
						unkScore, splitComplex, splitSaisiot, mergeSaisiot, blocklist, allowedDialects, dialectCost);
				}
			}
			else
			{
				const float unkScore = unkFormScorer(node->uform);
				evaluator(i, ownFormId, unknownNodeCands, 
					unkScore, splitComplex, splitSaisiot, mergeSaisiot, blocklist, allowedDialects, dialectCost);
			}

#ifdef DEBUG_PRINT
			cerr << "Token[" << i << "]" << endl;
			for (size_t p = pathIndices[i]; p < pathIndices[i + 1]; ++p)
			{
				auto& tt = pathes[p];
				cerr << "(" << tt.accScore << "):\t";
				printDebugPath(cerr, tt);
				cerr << endl;
			}
#endif
		}

		// end node		
		for (auto prev = endNode->getPrev(); prev; prev = prev->getSibling())
		{
			for (size_t pidx = pathIndices[prev - startNode]; pidx < pathIndices[prev - startNode + 1]; ++pidx)
			{
				auto& p = pathes[pidx];
				if (p.combineSocket) continue;
				if (!(p.morpheme->chunks.empty() || p.morpheme->complex || p.morpheme->saisiot))
				{
					if (p.morpheme->chunks.size() <= (p.morpheme->combineSocket ? 2 : 1))
					{
						if (!FeatureTestor::isMatched(nullptr, p.morpheme->vowel)) continue;
					}
				}
				if (p.morpheme->tag == POSTag::z_siot) continue;

				float c = p.accScore;
				float firstChunkScore = 0;
				if (!openEnding)
				{
					c += (firstChunkScore = p.lmState.next(langMdl, eosId));
					if (p.spState.singleQuote) c -= 2;
					if (p.spState.doubleQuote) c -= 2;
				}

				if (p.rootId == commonRootId)
				{
					for (size_t i = 0; i < uniqStates.size(); ++i)
					{
						pathes.emplace_back(nullptr, c, firstChunkScore, pidx, p.lmState, uniqStates[i], i);
					}
				}
				else
				{
					pathes.emplace_back(nullptr, c, firstChunkScore, pidx, p.lmState, p.spState, p.rootId);
				}
			}
		}
		pathIndices[graphSize] = pathes.size();

			}
		}

		sort(pathes.begin() + pathIndices[graphSize - 1], pathes.begin() + pathIndices[graphSize],
			[](const WordLLTy& a, const WordLLTy& b)
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
		for (size_t p = pathIndices[graphSize - 1]; p < pathIndices[graphSize]; ++p)
		{
			auto& tt = pathes[p];
			cerr << "(" << tt.accScore << "):\t";
			printDebugPath(cerr, tt);
			cerr << endl;
		}
#endif

		utils::ContainerSearcher csearcher{ pathIndices };
		Vector<PathResult> ret;
		size_t numUniqRootIdAndSpState;
		{
			UnorderedSet<pair<uint8_t, uint8_t>> uniqRootIdAndSpState;
			for (size_t p = pathIndices[graphSize - 1]; p < pathIndices[graphSize]; ++p)
			{
				auto& c = pathes[p];
				uniqRootIdAndSpState.emplace(c.rootId, (uint8_t)c.spState);
			}
			numUniqRootIdAndSpState = uniqRootIdAndSpState.size();
		}

		const size_t numCandsPerRootIdAndSpState = (size_t)std::ceil(topN * 2 / (double)numUniqRootIdAndSpState);
		size_t startIdx = 0;
		pair<uint8_t, uint8_t> prevRootIdAndSpState;
		if (pathIndices[graphSize - 1] < pathIndices[graphSize]) prevRootIdAndSpState = make_pair(pathes[pathIndices[graphSize - 1]].rootId, (uint8_t)pathes[pathIndices[graphSize - 1]].spState);
		for (size_t p = pathIndices[graphSize - 1]; p < pathIndices[graphSize]; ++p)
		{
			auto& c = pathes[p];
			auto curRootIdAndSpState = make_pair(c.rootId, (uint8_t)c.spState);
			if (prevRootIdAndSpState != curRootIdAndSpState)
			{
				startIdx = p - pathIndices[graphSize - 1];
				prevRootIdAndSpState = curRootIdAndSpState;
			}

			if ((p - pathIndices[graphSize - 1]) - startIdx < numCandsPerRootIdAndSpState)
			{
				auto tokens = generateTokenList(
					pathes.data(), p, csearcher, graph, ownFormList, config.typoCostWeight, dialectCost,
					kw->morphemes.data(), langVocabSize, splitSaisiot
				);
				ret.emplace_back(move(tokens), c.accScore, uniqStates[c.rootId], c.spState);
			}
		}
		sort(ret.begin(), ret.end(), [](const PathResult& a, const PathResult& b)
		{
			return a.score > b.score;
		});
		return ret;
	}

	template<class LangModel>
	Vector<PathResult> BestPathFinder<LangModel>::findBestPath(const Kiwi* kw,
		const KiwiConfig& config,
		const Vector<SpecialState>& prevSpStates,
		const KString& normForm,
		const KGraphNode* graph,
		const size_t graphSize,
		const size_t topN,
		const size_t oovScoringType,
		bool openEnding,
		bool splitComplex,
		bool splitSaisiot,
		bool mergeSaisiot,
		const std::unordered_set<const Morpheme*>* blocklist,
		Dialect allowedDialects,
		float dialectCost,
		const SubstringCounter* substringCounter
	)
	{
		if (oovGlobalMap)
		{
			return findBestPathDispatched<true>(kw, config, prevSpStates, normForm, graph, graphSize, topN, oovScoringType,
				openEnding, splitComplex, splitSaisiot, mergeSaisiot,
				blocklist, allowedDialects, dialectCost, substringCounter);
		}
		else
		{
			return findBestPathDispatched<false>(kw, config, prevSpStates, normForm, graph, graphSize, topN, oovScoringType,
				openEnding, splitComplex, splitSaisiot, mergeSaisiot,
				blocklist, allowedDialects, dialectCost, substringCounter);
		}
	}

}
