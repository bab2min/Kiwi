#include <iostream>
#include <fstream>
#include "PathEvaluator.hpp"
#include "Joiner.hpp"
#include "Kiwi.hpp"
#include "PCLanguageModel.hpp"
#include "StrUtils.h"
#include "FrozenTrie.hpp"
#include "qgemm.h"

using namespace std;

namespace kiwi
{
	inline size_t padMultipleOf(size_t n, size_t multiple)
	{
		return (n + multiple - 1) / multiple * multiple;
	}

	template<size_t windowSize, ArchType arch, class VocabTy, class VlVocabTy, bool quantized>
	struct MorphemeEvaluator<lm::PcLMState<windowSize, arch, VocabTy, VlVocabTy, quantized>>
	{
		using LmState = lm::PcLMState<windowSize, arch, VocabTy, VlVocabTy, quantized>;

		template<PathEvaluatingMode mode>
		void eval(
			Vector<WordLL<LmState>>& resultOut,
			const Kiwi* kw,
			const Vector<U16StringView>& ownForms,
			const Vector<Vector<WordLL<LmState>>>& cache,
			size_t ownFormId,
			const Vector<const Morpheme*>& morphs,
			const KGraphNode* node,
			const KGraphNode* startNode,
			const size_t topN,
			const size_t totalPrevPathes,
			const float ignoreCondScore,
			const float nodeLevelDiscount,
			const Vector<SpecialState>& prevSpStates
		) const
		{
			thread_local BestPathConatiner<mode, LmState> bestPathCont;
			thread_local Vector<const WordLL<LmState>*> regularPrevPathes;
			thread_local Vector<pair<const KGraphNode*, const WordLL<LmState>*>> combiningPrevPathes;
			thread_local Vector<const Morpheme*> regularMorphs, regularDistantMorphs, combiningLMorphs, combiningRMorphs;
			thread_local Vector<LmState> prevLmStates, nextLmStates;
			thread_local Vector<VocabTy> nextWids, nextDistantWids;
			thread_local Vector<float> scores;

			const auto* langMdl = static_cast<const lm::PcLangModel<arch, VocabTy, VlVocabTy, windowSize, quantized>*>(kw->getLangModel());
			const Morpheme* morphBase = kw->morphemes.data();
			const auto spacePenalty = kw->spacePenalty;
			const bool allowedSpaceBetweenChunk = kw->spaceTolerance > 0;
			const size_t langVocabSize = langMdl->vocabSize();

			regularPrevPathes.clear();
			combiningPrevPathes.clear();
			regularMorphs.clear();
			regularDistantMorphs.clear();
			combiningLMorphs.clear();
			combiningRMorphs.clear();
			prevLmStates.clear();
			nextWids.clear();
			nextDistantWids.clear();

			for (auto* prev = node->getPrev(); prev; prev = prev->getSibling())
			{
				for (auto& prevPath : cache[prev - startNode])
				{
					if (prevPath.combineSocket)
					{
						combiningPrevPathes.emplace_back(prev, &prevPath);
						continue;
					}
					regularPrevPathes.emplace_back(&prevPath);
				}
			}

			prevLmStates.resize(regularPrevPathes.size());
			for (size_t i = 0; i < regularPrevPathes.size(); ++i)
			{
				prevLmStates[i] = regularPrevPathes[i]->lmState;
			}

			for (auto& curMorph : morphs)
			{
				if (curMorph->combineSocket)
				{
					(curMorph->isSingle() ? combiningLMorphs : combiningRMorphs).emplace_back(curMorph);
					continue;
				}
				Wid firstWid;
				if (curMorph->isSingle())
				{
					firstWid = curMorph->lmMorphemeId;
				}
				else
				{
					firstWid = curMorph->chunks[0]->lmMorphemeId;
				}

				if (morphBase[firstWid].tag == POSTag::p)
				{
					continue;
				}
				if (windowSize > 0 && langMdl->distantTokenMask(firstWid))
				{
					regularDistantMorphs.emplace_back(curMorph);
					nextDistantWids.emplace_back(firstWid);
				}
				else
				{
					regularMorphs.emplace_back(curMorph);
					nextWids.emplace_back(firstWid);
				}
			}

			if (windowSize > 0)
			{
				regularMorphs.insert(regularMorphs.end(), regularDistantMorphs.begin(), regularDistantMorphs.end());
				nextWids.insert(nextWids.end(), nextDistantWids.begin(), nextDistantWids.end());
			}

			if (nextWids.size() > 0)
			{
				if (prevLmStates.size() == 1 && nextWids.size() == 1)
				{
					nextLmStates.resize(1);
					scores.resize(1);
					nextLmStates[0] = prevLmStates[0];
					scores[0] = nextLmStates[0].next(langMdl, nextWids[0]);
				}
				else
				{
				nextLmStates.resize(prevLmStates.size() * nextWids.size());
				scores.resize(prevLmStates.size() * nextWids.size());
				langMdl->progressMatrix<windowSize>(prevLmStates.data(), nextWids.data(), prevLmStates.size(), nextWids.size(), nextDistantWids.size(), nextLmStates.data(), scores.data());
				}
			}

			for (size_t curId = 0; curId < regularMorphs.size(); ++curId)
			{
				const auto* curMorph = regularMorphs[curId];
				bestPathCont.clear();

				size_t length = 1;
				const Morpheme* lastMorph;
				if (curMorph->isSingle())
				{
					lastMorph = curMorph->getCombined() ? curMorph->getCombined() : curMorph;
				}
				// if the morpheme has chunk set
				else
				{
					lastMorph = curMorph->chunks[curMorph->chunks.size() - 1];
					length = curMorph->chunks.size();
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
				const float morphScore = curMorph->userScore + nodeLevelDiscount + kw->tagScorer.evalLeftBoundary(hasLeftBoundary(node), curMorph->tag);
				size_t prevId = -1;
				for (auto* prevPath : regularPrevPathes)
				{
					++prevId;
					auto& state = nextLmStates[prevId * regularMorphs.size() + curId];
					auto score = prevPath->accScore + morphScore + scores[prevId * regularMorphs.size() + curId];

					FormEvaluator formEvaluator{ *prevPath, ownForms, morphBase };
					if (!formEvaluator(curMorph, ignoreCondScore, score)) continue;

					for (size_t i = 1; i < length; ++i)
					{
						const auto wid = curMorph->chunks[i]->lmMorphemeId;
						if (morphBase[wid].tag == POSTag::p)
						{
							goto continueFor;
						}
						score += state.next(langMdl, wid);
					}

					insertToPathContainer(bestPathCont, topN, prevSpStates, curMorph, morphBase, move(state), score, node, *prevPath, ruleBasedScorer);
				continueFor:;
				}

				bestPathCont.writeTo(resultOut, curMorph, lastSeqId, ownFormId);
			}

			for (auto* curMorph : combiningLMorphs)
			{
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
				const float morphScore = curMorph->userScore + nodeLevelDiscount + kw->tagScorer.evalLeftBoundary(hasLeftBoundary(node), curMorph->tag);
				for (auto* prevPath : regularPrevPathes)
				{
					auto state = prevPath->lmState;
					float score = prevPath->accScore + morphScore;

					FormEvaluator formEvaluator{ *prevPath, ownForms, morphBase };
					if (!formEvaluator(curMorph, ignoreCondScore, score)) continue;

					insertToPathContainer(bestPathCont, topN, prevSpStates, curMorph, morphBase, move(state), score, node, *prevPath, ruleBasedScorer);
				}
				bestPathCont.writeTo(resultOut, curMorph, lastSeqId, ownFormId);
			}

			for (auto* curMorph : combiningRMorphs)
			{
				bestPathCont.clear();
				size_t length = 1;
				const Morpheme* lastMorph;
				if (curMorph->isSingle())
				{
					lastMorph = curMorph->getCombined() ? curMorph->getCombined() : curMorph;
				}
				// if the morpheme has chunk set
				else
				{
					lastMorph = curMorph->chunks[curMorph->chunks.size() - 1];
					length = curMorph->chunks.size();
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
				const float morphScore = curMorph->userScore + nodeLevelDiscount + kw->tagScorer.evalLeftBoundary(hasLeftBoundary(node), curMorph->tag);
				for (auto& p : combiningPrevPathes)
				{
					auto* prev = p.first;
					auto* prevPath = p.second;
					float score = prevPath->accScore + morphScore;
					// merge <v> <chunk> with only the same socket
					if (prevPath->combineSocket != curMorph->combineSocket || curMorph->isSingle())
					{
						continue;
					}
					if (prev->endPos < node->startPos)
					{
						if (allowedSpaceBetweenChunk) score -= spacePenalty;
						else continue;
					}
					Wid firstWid = morphBase[prevPath->wid].getCombined()->lmMorphemeId;
					auto state = prevPath->lmState;
					score += state.next(langMdl, firstWid);

					for (size_t i = 1; i < length; ++i)
					{
						const auto wid = curMorph->chunks[i]->lmMorphemeId;
						if (morphBase[wid].tag == POSTag::p)
						{
							goto continueFor2;
						}
						score += state.next(langMdl, wid);
					}

					insertToPathContainer(bestPathCont, topN, prevSpStates, curMorph, morphBase, move(state), score, node, *prevPath, ruleBasedScorer);
				continueFor2:;
				}
				bestPathCont.writeTo(resultOut, curMorph, lastSeqId, ownFormId);
			}
		}
	};

	namespace lm
	{
		inline float half2float(uint16_t h)
		{
			union
			{
				uint32_t i;
				float f;
			} u;
			u.i = (uint32_t)(h & 0x8000) << 16;
			u.i |= ((uint32_t)(h & 0x7FFF) + 0x1C000) << 13;
			return u.f;
		}

		inline void dequantize(float* out, const int8_t* ints, size_t n, float scale)
		{
			for (size_t i = 0; i < n; ++i)
			{
				out[i] = ints[i] * scale;
			}
		}

		inline void addBias(uint8_t* out, const int8_t* ints, size_t n)
		{
			for (size_t i = 0; i < n; ++i)
			{
				out[i] = ints[i] + 128;
			}
		}

		template<class Arr>
		void logsoftmaxInplace(Arr& arr)
		{
			arr -= arr.maxCoeff();
			arr -= std::log(arr.exp().sum());
		}

		template<ArchType arch, class KeyType, class VlKeyType, size_t windowSize, bool quantized>
		PcLangModel<arch, KeyType, VlKeyType, windowSize, quantized>::PcLangModel(utils::MemoryObject&& mem) : PcLangModelBase{ mem }
		{
			auto* ptr = reinterpret_cast<const char*>(mem.get());

			Vector<uint32_t> nodeSizes(header.numNodes);
			streamvbyte_decode_0124(reinterpret_cast<const uint8_t*>(ptr + header.nodeOffset), nodeSizes.data(), header.numNodes);

			static constexpr size_t kvAlignment = ArchInfo<arch>::alignment;
			size_t paddedKVSize = 0;
			for (size_t i = 0; i < nodeSizes.size(); ++i)
			{
				if (!nodeSizes[i]) continue;
				paddedKVSize += padMultipleOf(nodeSizes[i] * (sizeof(VlKeyType) + sizeof(int32_t)), kvAlignment);
			}

			keyValueData = make_unique<uint8_t[]>(paddedKVSize + kvAlignment);
			alignedKeyValueData = reinterpret_cast<uint8_t*>(padMultipleOf(reinterpret_cast<size_t>(keyValueData.get()), kvAlignment));
			auto keyData = make_unique<VlKeyType[]>(header.numNodes - 1);
			if (std::is_same<VlKeyType, uint32_t>::value)
			{
				streamvbyte_decode(reinterpret_cast<const uint8_t*>(ptr + header.keyOffset), (uint32_t*)keyData.get(), header.numNodes - 1);
			}
			else
			{
				Vector<uint32_t> tempKeyData(header.numNodes - 1);
				streamvbyte_decode(reinterpret_cast<const uint8_t*>(ptr + header.keyOffset), tempKeyData.data(), header.numNodes - 1);
				std::copy(tempKeyData.begin(), tempKeyData.end(), keyData.get());
			}
			Vector<uint32_t> values(header.numNodes);
			streamvbyte_decode_0124(reinterpret_cast<const uint8_t*>(ptr + header.valueOffset), values.data(), header.numNodes);

			size_t numNonLeafNodes = 0, numLeafNodes = 0;
			for (size_t i = 0; i < header.numNodes; ++i)
			{
				if (nodeSizes[i]) numNonLeafNodes++;
				else numLeafNodes++;
			}

			nodeData = make_unique<MyNode[]>(numNonLeafNodes);
			auto valueData = make_unique<int32_t[]>(header.numNodes - 1);

			size_t nonLeafIdx = 0, leafIdx = 0, nextOffset = 0;
			Vector<std::array<size_t, 3>> keyRanges;
			for (size_t i = 0; i < header.numNodes; ++i)
			{
				if (nodeSizes[i])
				{
					auto& node = nodeData[nonLeafIdx];
					if (!keyRanges.empty())
					{
						auto& back = keyRanges.back();
						valueData[back[1]] = nonLeafIdx - back[0];
					}
					node.value = values[i];
					node.numNexts = nodeSizes[i];
					keyRanges.emplace_back(std::array<size_t, 3>{ nonLeafIdx, (size_t)nextOffset, (size_t)(nextOffset + node.numNexts) });
					nextOffset += nodeSizes[i];
					nonLeafIdx++;
				}
				else
				{
					auto& back = keyRanges.back();
					valueData[back[1]] = -(int32_t)values[i];
					back[1]++;
					while (keyRanges.back()[1] == keyRanges.back()[2])
					{
						keyRanges.pop_back();
						if (keyRanges.empty()) break;
						keyRanges.back()[1]++;
					}
					leafIdx++;
				}
			}

			uint8_t* kvDataPtr = const_cast<uint8_t*>(alignedKeyValueData);
			nonLeafIdx = 0;
			nextOffset = 0;
			for (size_t i = 0; i < header.numNodes; ++i)
			{
				if (!nodeSizes[i]) continue;
				auto& node = nodeData[nonLeafIdx];
				node.nextOffset = (uint32_t)(kvDataPtr - alignedKeyValueData);
				memcpy(kvDataPtr, &keyData[nextOffset], node.numNexts * sizeof(VlKeyType));
				memcpy(kvDataPtr + node.numNexts * sizeof(VlKeyType), &valueData[nextOffset], node.numNexts * sizeof(int32_t));
				kvDataPtr += node.numNexts * (sizeof(VlKeyType) + sizeof(int32_t));
				nextOffset += node.numNexts;
				nonLeafIdx++;
			}

			allRootValueData = make_unique<int32_t[]>(header.vocabSize);
			memset(allRootValueData.get(), 0, sizeof(int32_t) * header.vocabSize);
			for (size_t i = 0; i < nodeData[0].numNexts; ++i)
			{
				allRootValueData[keyData[i]] = valueData[i];
			}
			Vector<uint8_t> tempBuf;
			for (size_t i = 0; i < nonLeafIdx; ++i)
			{
				auto& node = nodeData[i];
				nst::prepareKV<arch, VlKeyType, int32_t>(const_cast<uint8_t*>(&alignedKeyValueData[node.nextOffset]), node.numNexts, tempBuf);
			}

			Deque<MyNode*> dq;
			for (dq.emplace_back(&nodeData[0]); !dq.empty(); dq.pop_front())
			{
				auto p = dq.front();
				for (size_t i = 0; i < p->numNexts; ++i)
				{
					auto kv = nst::extractKV<arch, VlKeyType, int32_t>(&alignedKeyValueData[p->nextOffset], p->numNexts, i);
					if (kv.second <= 0) continue;
					auto* child = &p[kv.second];
					child->lower = findLowerNode(p, kv.first) - child;
					if (child->value == 0)
					{
						child->value = findLowerValue(p, kv.first);
					}
					dq.emplace_back(child);
				}
			}

			{
				const size_t contextEmbSize = header.contextSize * contextEmbStride();
				const size_t distantEmbSize = windowSize > 0 ? header.vocabSize * distantEmbStride() : 0;
				const size_t outputEmbSize = header.vocabSize * outputEmbStride();
				const size_t positionConfSize = windowSize > 0 ? (header.windowSize + 1) * sizeof(float) : 0;
				const size_t distantMaskSize = windowSize > 0 ? (header.vocabSize + 7) / 8 : 0;

				allEmbs = make_unique<uint8_t[]>(contextEmbSize + outputEmbSize + distantEmbSize + positionConfSize + distantMaskSize);
				auto p = allEmbs.get();
				contextEmbPtr = reinterpret_cast<uint8_t*>(p);
				distantEmbPtr = reinterpret_cast<uint8_t*>(p += contextEmbSize);
				outputEmbPtr = reinterpret_cast<uint8_t*>(p += distantEmbSize);
				positionConfidPtr = reinterpret_cast<float*>(p += outputEmbSize);
				distantMaskPtr = reinterpret_cast<const uint8_t*>(p += positionConfSize);
				if (windowSize == 0)
				{
					distantEmbPtr = nullptr;
					positionConfidPtr = nullptr;
					distantMaskPtr = nullptr;
				}
			}
			
			auto* eptr = ptr + header.embOffset;
			auto* optr = const_cast<uint8_t*>(contextEmbPtr);
			for (size_t i = 0; i < header.contextSize; ++i)
			{
				if (quantized)
				{
					addBias(optr, reinterpret_cast<const int8_t*>(eptr), header.dim);
					optr += header.dim;
					eptr += header.dim;
					*reinterpret_cast<float*>(optr) = half2float(*reinterpret_cast<const uint16_t*>(eptr)); // scale
					optr += sizeof(float);
					eptr += sizeof(uint16_t);
				}
				else
				{
					const float scale = half2float(*reinterpret_cast<const uint16_t*>(eptr + header.dim));
					dequantize(reinterpret_cast<float*>(optr), reinterpret_cast<const int8_t*>(eptr), header.dim, scale);
					optr += header.dim * sizeof(float);
					eptr += header.dim + sizeof(uint16_t);
				}

				*reinterpret_cast<float*>(optr) = -half2float(*reinterpret_cast<const uint16_t*>(eptr)); // bias
				optr += sizeof(float);
				eptr += sizeof(uint16_t);
				if (windowSize > 0)
				{
					*reinterpret_cast<float*>(optr) = half2float(*reinterpret_cast<const uint16_t*>(eptr)); // confidence
					optr += sizeof(float);
					eptr += sizeof(uint16_t);
					*reinterpret_cast<float*>(optr) = half2float(*reinterpret_cast<const uint16_t*>(eptr)); // valid token sum
					optr += sizeof(float);
					eptr += sizeof(uint16_t);
				}
				else
				{
					eptr += sizeof(uint16_t) * 2;
				}
			}

			optr = const_cast<uint8_t*>(outputEmbPtr);
			for (size_t i = 0; i < header.vocabSize; ++i)
			{
				auto* qvals = reinterpret_cast<const int8_t*>(eptr);
				if (quantized)
				{
					memcpy(optr, qvals, header.dim);
					optr += header.dim;
					eptr += header.dim;
					*reinterpret_cast<float*>(optr) = half2float(*reinterpret_cast<const uint16_t*>(eptr));
					optr += sizeof(float);
					eptr += sizeof(uint16_t);
					*reinterpret_cast<int32_t*>(optr) = accumulate(qvals, qvals + header.dim, 0) * 128;
					optr += sizeof(int32_t);
				}
				else
				{
					const float scale = half2float(*reinterpret_cast<const uint16_t*>(eptr + header.dim));
					dequantize(reinterpret_cast<float*>(optr), qvals, header.dim, scale);
					optr += header.dim * sizeof(float);
					eptr += header.dim + sizeof(uint16_t);
				}
			}

			if (windowSize > 0)
			{
				optr = const_cast<uint8_t*>(distantEmbPtr);
				for (size_t i = 0; i < header.vocabSize; ++i)
				{
					if (quantized)
					{
						addBias(optr, reinterpret_cast<const int8_t*>(eptr), header.dim);
						optr += header.dim;
						eptr += header.dim;
						*reinterpret_cast<float*>(optr) = half2float(*reinterpret_cast<const uint16_t*>(eptr)); // scale
						optr += sizeof(float);
						eptr += sizeof(uint16_t);
					}
					else
					{
						const float scale = half2float(*reinterpret_cast<const uint16_t*>(eptr + header.dim));
						dequantize(reinterpret_cast<float*>(optr), reinterpret_cast<const int8_t*>(eptr), header.dim, scale);
						optr += header.dim * sizeof(float);
						eptr += header.dim + sizeof(uint16_t);
					}

					*reinterpret_cast<float*>(optr) = -half2float(*reinterpret_cast<const uint16_t*>(eptr)); // bias
					optr += sizeof(float);
					eptr += sizeof(uint16_t);
					*reinterpret_cast<float*>(optr) = half2float(*reinterpret_cast<const uint16_t*>(eptr)); // confidence
					optr += sizeof(float);
					eptr += sizeof(uint16_t);
					if (quantized)
					{
						optr += sizeof(float);
					}
				}

				const_cast<float*>(positionConfidPtr)[0] = 0;
				for (size_t i = 0; i < header.windowSize; ++i)
				{
					const_cast<float*>(positionConfidPtr)[i + 1] = half2float(*reinterpret_cast<const uint16_t*>(eptr));
					eptr += sizeof(uint16_t);
				}

				optr = const_cast<uint8_t*>(distantMaskPtr);
				const size_t compressedDistantMaskSize = (header.vocabSize + 7) / 8;
				std::copy(eptr, eptr + compressedDistantMaskSize, optr);
			}
		}

		template<ArchType arch, class KeyType, class VlKeyType, size_t windowSize, bool quantized>
		float PcLangModel<arch, KeyType, VlKeyType, windowSize, quantized>::progress(int32_t& nodeIdx,
			uint32_t& contextIdx,
			std::array<KeyType, windowSize + 1>& history,
			KeyType next) const
		{
			const bool validDistantToken = distantTokenMask(next);
			float ll = 0;

			if (windowSize > 0 && validDistantToken)
			{
				if constexpr (quantized)
				{
					int32_t contextIdcs[1 + windowSize];
					float lls[(1 + windowSize) * 2];
					int32_t nextIdx[1] = { next };

					copy(positionConfidPtr, positionConfidPtr + windowSize + 1, lls);
					lls[0] += getContextConfid(contextIdx);
					contextIdcs[0] = contextIdx;
					for (size_t i = 0; i < windowSize; ++i)
					{
						const auto historyToken = history[i];
						lls[i + 1] += historyToken ? getDistantConfid(historyToken) : -99999;
						contextIdcs[i + 1] = (historyToken ? historyToken : 0) + header.contextSize;
					}
					LogSoftmax<arch>{}(lls, std::integral_constant<size_t, windowSize + 1>());

					qgemm::scatteredGEMMOpt<arch>(
						1 + windowSize, 1, header.dim,
						getContextQuantEmb(0), contextIdcs, contextEmbStride(),
						getOutputQuantEmb(0), nextIdx, outputEmbStride(),
						&lls[1 + windowSize], 1);

					for (size_t i = 0; i < 1 + windowSize; ++i)
					{
						lls[i] += lls[i + 1 + windowSize];
					}
					lls[0] -= getContextValidTokenSum(contextIdx);
					ll = LogSumExp<arch>{}(lls, std::integral_constant<size_t, windowSize + 1>());
					ll += getContextValidTokenSum(contextIdx);
				}
				else
				{
					thread_local Eigen::MatrixXf mat;
					mat.resize(header.dim, 1 + windowSize);
					thread_local Eigen::VectorXf lls;
					lls.resize(1 + windowSize);

					lls = Eigen::Map<const Eigen::VectorXf>{ positionConfidPtr, windowSize + 1 };
					lls[0] += getContextConfid(contextIdx);
					for (size_t i = 0; i < windowSize; ++i)
					{
						const auto historyToken = history[i];
						lls[i + 1] += historyToken ? getDistantConfid(historyToken) : -99999;
					}
					logsoftmaxInplace(lls.array());

					mat.col(0) = Eigen::Map<const Eigen::VectorXf>{ getContextEmb(contextIdx), header.dim };
					lls[0] += getContextBias(contextIdx);
					for (size_t i = 0; i < windowSize; ++i)
					{
						const auto historyToken = history[i];
						if (historyToken) mat.col(i + 1) = Eigen::Map<const Eigen::VectorXf>{ getDistantEmb(historyToken), header.dim };
						else mat.col(i + 1).setZero();
						lls[i + 1] += getDistantBias(historyToken);
					}
					lls.tail(windowSize).array() += getContextValidTokenSum(contextIdx);
					Eigen::Map<const Eigen::VectorXf> outputVec{ getOutputEmb(next), header.dim };
					lls += mat.transpose() * outputVec;
					ll = LogSumExp<arch>{}(lls.data(), std::integral_constant<size_t, windowSize + 1>());
				}
			}
			else 
			{
				if constexpr (quantized)
				{
					const auto* contextPtr = getContextQuantEmb(contextIdx);
					const auto* outputPtr = getOutputQuantEmb(next);
					int32_t acc = qgemm::dotprod<arch>(contextPtr, outputPtr, header.dim);
					const float contextScale = *reinterpret_cast<const float*>(contextPtr + header.dim),
						outputScale = *reinterpret_cast<const float*>(outputPtr + header.dim),
						contextBias = *reinterpret_cast<const float*>(contextPtr + header.dim + sizeof(float));
					const int32_t hsum = *reinterpret_cast<const int32_t*>(outputPtr + header.dim + sizeof(float));
					acc -= hsum;
					ll = acc * contextScale * outputScale + contextBias;
				}
				else
				{
					ll = getContextBias(contextIdx);
					Eigen::Map<const Eigen::VectorXf> contextVec{ getContextEmb(contextIdx), header.dim };
					Eigen::Map<const Eigen::VectorXf> outputVec{ getOutputEmb(next), header.dim };
					ll += (contextVec.transpose() * outputVec)[0];
				}
			}
			
			contextIdx = progressContextNode(nodeIdx, next);
			if (windowSize > 0)
			{
				if (history[windowSize])
				{
					memcpy(&history[0], &history[1], windowSize * sizeof(KeyType));
				}
				history[windowSize] = validDistantToken ? next : 0;
			}
			return ll;
		}

		template<ArchType arch, class KeyType, class VlKeyType, size_t windowSize, bool quantized>
		struct PcLangModel<arch, KeyType, VlKeyType, windowSize, quantized>::TLSForProgressMatrix
		{
			Vector<pair<int32_t, uint32_t>> contextCache;
			Vector<uint64_t> contextIdcs, historyIdcs, nextIdcs;
			Vector<uint32_t> inverseContextIdcs, inverseHistoryIdcs, inverseNextIdcs;
			Vector<float> resultBuf, confidenceBuf, scoreBuf;
			UnorderedMap<uint32_t, uint32_t> historyMap;
			Vector<uint32_t> uniqHistoryTokens;
			Vector<float> inputEmbBuf, outputEmbBuf; // only for non-quantized
			Vector<int32_t> contextIdcs2, nextIdcs2; // only for quantized
		};

		// specialization for windowSize > 0
		template<ArchType arch, class KeyType, class VlKeyType, size_t windowSize, bool quantized>
		template<size_t _windowSize>
		inline auto PcLangModel<arch, KeyType, VlKeyType, windowSize, quantized>::nextState(
			const typename std::enable_if<(_windowSize > 0), LmStateType>::type& state, KeyType next, 
			bool cacheIsValid, pair<int32_t, uint32_t>& cache) const -> LmStateType
		{
			LmStateType ret{ state.node }; // partially initialized
			if (cacheIsValid)
			{
				ret.node = cache.first;
				ret.contextIdx = cache.second;
			}
			else
			{
			ret.contextIdx = progressContextNode(ret.node, next);
				cache = std::make_pair(ret.node, ret.contextIdx);
			}

			if (state.history[windowSize])
			{
				memcpy(&ret.history[0], &state.history[1], windowSize * sizeof(KeyType));
			}
			else
			{
				memcpy(&ret.history[0], &state.history[0], windowSize * sizeof(KeyType));
			}
			ret.history[windowSize] = distantTokenMask(next) ? next : 0;
			return ret;
		}

		// specialization for windowSize == 0
		template<ArchType arch, class KeyType, class VlKeyType, size_t windowSize, bool quantized>
		template<size_t _windowSize>
		inline auto PcLangModel<arch, KeyType, VlKeyType, windowSize, quantized>::nextState(
			const typename std::enable_if<_windowSize == 0, LmStateType>::type& state, KeyType next, 
			bool cacheIsValid, pair<int32_t, uint32_t>& cache) const -> LmStateType
		{
			LmStateType ret{ state.node }; // partially initialized
			if (cacheIsValid)
			{
				ret.node = cache.first;
				ret.contextIdx = cache.second;
			}
			else
			{
			ret.contextIdx = progressContextNode(ret.node, next);
				cache = std::make_pair(ret.node, ret.contextIdx);
			}
			return ret;
		}

		inline uint64_t mergePair(uint32_t a, uint32_t b)
		{
			return ((uint64_t)a << 32) | b;
		}

		inline pair<uint32_t, uint32_t> splitPair(uint64_t a)
		{
			return make_pair(a >> 32, a & 0xFFFFFFFF);
		}

		template<ArchType arch, class KeyType, class VlKeyType, size_t windowSize, bool quantized>
		inline void PcLangModel<arch, KeyType, VlKeyType, windowSize, quantized>::progressMatrixWSort(
			TLSForProgressMatrix& tls, const LmStateType* prevStates, const KeyType* nextIds,
			size_t prevStateSize, size_t nextIdSize, size_t numValidDistantTokens,
			LmStateType* outStates, float* outScores) const
		{
			static constexpr size_t scoreBatchSize = 32;

			tls.contextIdcs.resize(prevStateSize);
			tls.historyIdcs.clear();
			tls.nextIdcs.resize(nextIdSize);
			tls.inverseContextIdcs.resize(prevStateSize);
			tls.inverseHistoryIdcs.clear();
			tls.inverseHistoryIdcs.resize(prevStateSize * windowSize, -1);
			tls.inverseNextIdcs.resize(nextIdSize);
			if (quantized)
			{
				tls.contextIdcs2.clear();
				tls.nextIdcs2.clear();
			}
			else
			{
				tls.inputEmbBuf.resize(prevStateSize * header.dim);
				tls.outputEmbBuf.resize(nextIdSize * header.dim);
			}
			tls.confidenceBuf.resize(prevStateSize * 2);
			tls.scoreBuf.resize(scoreBatchSize * (windowSize + 2));

			const size_t numInvalidDistantTokens = nextIdSize - numValidDistantTokens;
			for (size_t i = 0; i < nextIdSize; ++i)
			{
				tls.nextIdcs[i] = mergePair(nextIds[i], i);
			}
			sort(tls.nextIdcs.begin(), tls.nextIdcs.begin() + numInvalidDistantTokens);
			sort(tls.nextIdcs.begin() + numInvalidDistantTokens, tls.nextIdcs.end());
			size_t uniqOutputSize = 0;
			for (size_t i = 0; i < nextIdSize; ++i)
			{
				const auto nextId = splitPair(tls.nextIdcs[i]).first;
				const auto idx = splitPair(tls.nextIdcs[i]).second;
				if (i == 0 || nextId != splitPair(tls.nextIdcs[i - 1]).first)
				{
					if (quantized)
					{
						tls.nextIdcs2.emplace_back(nextId);
					}
					else
					{
						copy(getOutputEmb(nextId), getOutputEmb(nextId) + header.dim, &tls.outputEmbBuf[uniqOutputSize * header.dim]);
					}
					uniqOutputSize++;
				}
				tls.inverseNextIdcs[idx] = uniqOutputSize - 1;
			}
			tls.resultBuf.resize(prevStateSize * uniqOutputSize);

			for (size_t i = 0; i < prevStateSize; ++i)
			{
				tls.contextIdcs[i] = mergePair(prevStates[i].contextIdx, i);
			}
			sort(tls.contextIdcs.begin(), tls.contextIdcs.end());
			size_t uniqInputSize = 0;
			for (size_t i = 0; i < prevStateSize; ++i)
			{
				const auto contextId = splitPair(tls.contextIdcs[i]).first;
				const auto idx = splitPair(tls.contextIdcs[i]).second;
				if (i == 0 || contextId != splitPair(tls.contextIdcs[i - 1]).first)
				{
					if (quantized)
					{
						tls.contextIdcs2.emplace_back(contextId);
					}
					else
					{
						copy(getContextEmb(contextId), getContextEmb(contextId) + header.dim, &tls.inputEmbBuf[uniqInputSize * header.dim]);
						fill(&tls.resultBuf[uniqInputSize * uniqOutputSize], &tls.resultBuf[(uniqInputSize + 1) * uniqOutputSize], getContextBias(contextId));
					}
					tls.confidenceBuf[uniqInputSize * 2] = getContextConfid(contextId);
					tls.confidenceBuf[uniqInputSize * 2 + 1] = getContextValidTokenSum(contextId);
					uniqInputSize++;
				}
				tls.inverseContextIdcs[idx] = uniqInputSize - 1;
			}

			size_t uniqHistorySize = 0;
			if (prevStateSize <= 8) // use vector for small size
			{
				for (size_t i = 0; i < prevStateSize; ++i)
				{
					for (size_t j = 0; j < windowSize; ++j)
					{
						const auto historyToken = prevStates[i].history[j];
						if (historyToken)
						{
							tls.historyIdcs.emplace_back(mergePair(historyToken, i * windowSize + j));
						}
					}
				}
				sort(tls.historyIdcs.begin(), tls.historyIdcs.end());
				uniqHistorySize = 0;
				for (size_t i = 0; i < tls.historyIdcs.size(); ++i)
				{
					const auto historyToken = splitPair(tls.historyIdcs[i]).first;
					const auto idx = splitPair(tls.historyIdcs[i]).second;
					if (i == 0 || historyToken != splitPair(tls.historyIdcs[i - 1]).first)
					{
						uniqHistorySize++;
					}
					tls.inverseHistoryIdcs[idx] = uniqHistorySize - 1;
				}
				if (!quantized)
				{
					tls.inputEmbBuf.resize((uniqInputSize + uniqHistorySize) * header.dim);
				}
				tls.confidenceBuf.resize(uniqInputSize * 2 + uniqHistorySize);
				tls.resultBuf.resize(padMultipleOf(uniqInputSize + uniqHistorySize, 8) * padMultipleOf(uniqOutputSize, 8));

				uniqHistorySize = 0;
				for (size_t i = 0; i < tls.historyIdcs.size(); ++i)
				{
					const auto historyToken = splitPair(tls.historyIdcs[i]).first;
					const auto idx = splitPair(tls.historyIdcs[i]).second;
					if (i == 0 || historyToken != splitPair(tls.historyIdcs[i - 1]).first)
					{
						if (quantized)
						{
							tls.contextIdcs2.emplace_back(historyToken + header.contextSize);
						}
						else
						{
							copy(getDistantEmb(historyToken), getDistantEmb(historyToken) + header.dim, &tls.inputEmbBuf[(uniqInputSize + uniqHistorySize) * header.dim]);
							fill(&tls.resultBuf[(uniqInputSize + uniqHistorySize) * uniqOutputSize], &tls.resultBuf[(uniqInputSize + uniqHistorySize + 1) * uniqOutputSize], getDistantBias(historyToken));
						}
						tls.confidenceBuf[uniqInputSize * 2 + uniqHistorySize] = getDistantConfid(historyToken);
						uniqHistorySize++;
					}
				}
			}
			else // use map for large size
			{
				tls.historyMap.clear();
				tls.uniqHistoryTokens.clear();
				for (size_t i = 0; i < prevStateSize; ++i)
				{
					for (size_t j = 0; j < windowSize; ++j)
					{
						const auto historyToken = prevStates[i].history[j];
						if (!historyToken) continue;
						const auto idx = i * windowSize + j;
						auto inserted = tls.historyMap.emplace(historyToken, tls.historyMap.size());
						tls.inverseHistoryIdcs[idx] = inserted.first->second;
						if (inserted.second) tls.uniqHistoryTokens.emplace_back(historyToken);
					}
				}
				uniqHistorySize = tls.historyMap.size();
				if (!quantized)
				{
					tls.inputEmbBuf.resize((uniqInputSize + uniqHistorySize) * header.dim);
				}
				tls.confidenceBuf.resize(uniqInputSize * 2 + uniqHistorySize);
				tls.resultBuf.resize(padMultipleOf(uniqInputSize + uniqHistorySize, 8) * padMultipleOf(uniqOutputSize, 8));

				for (size_t i = 0; i < tls.uniqHistoryTokens.size(); ++i)
				{
					const auto historyToken = tls.uniqHistoryTokens[i];
					if (quantized)
					{
						tls.contextIdcs2.emplace_back(historyToken + header.contextSize);
					}
					else
					{
						copy(getDistantEmb(historyToken), getDistantEmb(historyToken) + header.dim, &tls.inputEmbBuf[(uniqInputSize + i) * header.dim]);
						fill(&tls.resultBuf[(uniqInputSize + i) * uniqOutputSize], &tls.resultBuf[(uniqInputSize + i + 1) * uniqOutputSize], getDistantBias(historyToken));
					}
					tls.confidenceBuf[uniqInputSize * 2 + i] = getDistantConfid(historyToken);
				}
			}
			Eigen::Map<Eigen::MatrixXf> resultMap{ tls.resultBuf.data(), (Eigen::Index)uniqOutputSize, (Eigen::Index)(uniqInputSize + uniqHistorySize) };

			if constexpr (quantized)
			{
				//thread_local Map<pair<uint32_t, uint32_t>, size_t> shapeCnt;
				//shapeCnt[make_pair(uniqInputSize + uniqHistorySize, uniqOutputSize)]++;
				//ScopedTimer<> timer{ 0 };
				qgemm::scatteredGEMMOpt<arch>(
					uniqInputSize + uniqHistorySize, uniqOutputSize, header.dim,
					getContextQuantEmb(0), tls.contextIdcs2.data(), contextEmbStride(),
					getOutputQuantEmb(0), tls.nextIdcs2.data(), outputEmbStride(),
					tls.resultBuf.data(), uniqOutputSize);
			}
			else
			{
				Eigen::Map<Eigen::MatrixXf> inputMap{ tls.inputEmbBuf.data(), header.dim, (Eigen::Index)(uniqInputSize + uniqHistorySize) };
				Eigen::Map<Eigen::MatrixXf> outputMap{ tls.outputEmbBuf.data(), header.dim, (Eigen::Index)uniqOutputSize };
				resultMap += outputMap.transpose() * inputMap;
			}

			pair<int32_t, uint32_t> contextCache;
			for (size_t j = 0; j < nextIdSize; ++j)
			{
			for (size_t i = 0; i < prevStateSize; ++i)
			{
					const auto& state = prevStates[i];
					const bool cacheIsValid = i > 0 && state.node == prevStates[i - 1].node;
					outStates[i * nextIdSize + j] = nextState<windowSize>(state, nextIds[j], cacheIsValid, contextCache);
				}
			}

			for (size_t i = 0; i < prevStateSize; ++i)
			{
				const auto& state = prevStates[i];
				const bool cacheIsValid = i > 0 && state.node == prevStates[i - 1].node;
				for (size_t j = 0; j < numInvalidDistantTokens; ++j)
				{
					outScores[i * nextIdSize + j] = resultMap(tls.inverseNextIdcs[j], tls.inverseContextIdcs[i]);
				}
			}

			auto* validTokenSumBuf = tls.scoreBuf.data() + scoreBatchSize * (windowSize + 1);
			for (size_t i = 0; i < prevStateSize * numValidDistantTokens; i += scoreBatchSize)
			{
				const size_t batchSize = std::min(scoreBatchSize, prevStateSize * numValidDistantTokens - i);
				for (size_t j = 0; j < batchSize; ++j)
				{
					const auto pIdx = (i + j) / numValidDistantTokens;
					const auto nIdx = (i + j) % numValidDistantTokens + numInvalidDistantTokens;
					tls.scoreBuf[j] = tls.confidenceBuf[tls.inverseContextIdcs[pIdx] * 2];
					validTokenSumBuf[j] = tls.confidenceBuf[tls.inverseContextIdcs[pIdx] * 2 + 1];
					for (size_t k = 0; k < windowSize; ++k)
					{
						const auto idx = tls.inverseHistoryIdcs[pIdx * windowSize + k];
						tls.scoreBuf[j + (k + 1) * scoreBatchSize] = idx == -1 ? -99999 : tls.confidenceBuf[uniqInputSize * 2 + idx];
					}
				}
				Eigen::Map<Eigen::Array<float, -1, windowSize + 1>> scoreMap{ tls.scoreBuf.data(), (Eigen::Index)scoreBatchSize, windowSize + 1 };
				scoreMap.rowwise() += Eigen::Map<const Eigen::Array<float, 1, windowSize + 1>>{ positionConfidPtr, 1, windowSize + 1 };
				LogSoftmaxTransposed<arch, windowSize + 1>{}(tls.scoreBuf.data(), batchSize, scoreBatchSize);
				scoreMap.rightCols<windowSize>().colwise() += Eigen::Map<Eigen::Array<float, scoreBatchSize, 1>>{ validTokenSumBuf, scoreBatchSize, 1 };
				for (size_t j = 0; j < batchSize; ++j)
				{
					const auto pIdx = (i + j) / numValidDistantTokens;
					const auto nIdx = (i + j) % numValidDistantTokens + numInvalidDistantTokens;
					tls.scoreBuf[j] += resultMap(tls.inverseNextIdcs[nIdx], tls.inverseContextIdcs[pIdx]);
					for (size_t k = 0; k < windowSize; ++k)
					{
						const auto idx = tls.inverseHistoryIdcs[pIdx * windowSize + k];
						if (idx != -1)
						{
							tls.scoreBuf[j + (k + 1) * scoreBatchSize] += resultMap(tls.inverseNextIdcs[nIdx], uniqInputSize + idx);
						}
					}
				}
				LogSumExpTransposed<arch, windowSize + 1>{}(tls.scoreBuf.data(), batchSize, scoreBatchSize);

				for (size_t j = 0; j < batchSize; ++j)
				{
					const auto pIdx = (i + j) / numValidDistantTokens;
					const auto nIdx = (i + j) % numValidDistantTokens + numInvalidDistantTokens;
					const bool cacheIsValid = pIdx > 0 && prevStates[pIdx].node == prevStates[pIdx - 1].node;
					outScores[pIdx * nextIdSize + nIdx] = tls.scoreBuf[j];
				}
			}
		}

		template<ArchType arch, class KeyType, class VlKeyType, size_t windowSize, bool quantized>
		inline void PcLangModel<arch, KeyType, VlKeyType, windowSize, quantized>::progressMatrixWOSort(
			TLSForProgressMatrix& tls, const LmStateType* prevStates, const KeyType* nextIds,
			size_t prevStateSize, size_t nextIdSize, size_t numValidDistantTokens,
			LmStateType* outStates, float* outScores) const
		{
			static constexpr size_t scoreBatchSize = 32;

			if (quantized)
			{
				tls.contextIdcs2.clear();
				tls.nextIdcs2.clear();
				}
			else
			{
				tls.inputEmbBuf.resize(prevStateSize * (1 + windowSize) * header.dim);
				tls.outputEmbBuf.resize(nextIdSize * header.dim);
			}
			tls.confidenceBuf.resize(prevStateSize * (2 + windowSize));
			tls.scoreBuf.resize(scoreBatchSize * (windowSize + 2));
			tls.resultBuf.resize(padMultipleOf(prevStateSize * (1 + windowSize), 8) * padMultipleOf(nextIdSize, 8));

			const size_t numInvalidDistantTokens = nextIdSize - numValidDistantTokens;
			for (size_t i = 0; i < nextIdSize; ++i)
			{
				const auto nextId = nextIds[i];
				if (quantized)
				{
					tls.nextIdcs2.emplace_back(nextId);
				}
				else
				{
					copy(getOutputEmb(nextId), getOutputEmb(nextId) + header.dim, &tls.outputEmbBuf[i * header.dim]);
				}
			}

			for (size_t i = 0; i < prevStateSize; ++i)
			{
				const auto contextId = prevStates[i].contextIdx;
				if (quantized)
				{
					tls.contextIdcs2.emplace_back(contextId);
				}
				else
				{
					copy(getContextEmb(contextId), getContextEmb(contextId) + header.dim, &tls.inputEmbBuf[i * header.dim]);
					fill(&tls.resultBuf[i * nextIdSize], &tls.resultBuf[(i + 1) * nextIdSize], getContextBias(contextId));
				}
				tls.confidenceBuf[i * 2] = getContextConfid(contextId);
				tls.confidenceBuf[i * 2 + 1] = getContextValidTokenSum(contextId);
			}

			size_t uniqHistorySize = 0;
			tls.inverseHistoryIdcs.clear();
			for (size_t i = 0; i < prevStateSize; ++i)
			{
				for (size_t j = 0; j < windowSize; ++j)
				{
					const auto historyToken = prevStates[i].history[j];
					if (historyToken)
					{
						if (quantized)
						{
							tls.contextIdcs2.emplace_back(historyToken + header.contextSize);
						}
						else
						{
							copy(getDistantEmb(historyToken), getDistantEmb(historyToken) + header.dim, &tls.inputEmbBuf[(prevStateSize + uniqHistorySize) * header.dim]);
							fill(&tls.resultBuf[(prevStateSize + uniqHistorySize) * nextIdSize], &tls.resultBuf[(prevStateSize + uniqHistorySize + 1) * nextIdSize], getDistantBias(historyToken));
						}
						tls.confidenceBuf[prevStateSize * 2 + uniqHistorySize] = getDistantConfid(historyToken);
						uniqHistorySize++;
					}
					tls.inverseHistoryIdcs.emplace_back(historyToken ? uniqHistorySize - 1 : -1);
					
				}
			}
			
			Eigen::Map<Eigen::MatrixXf> resultMap{ tls.resultBuf.data(), (Eigen::Index)nextIdSize, (Eigen::Index)(prevStateSize + uniqHistorySize) };

			if constexpr (quantized)
			{
				qgemm::scatteredGEMMOpt<arch>(
					prevStateSize + uniqHistorySize, nextIdSize, header.dim,
					getContextQuantEmb(0), tls.contextIdcs2.data(), contextEmbStride(),
					getOutputQuantEmb(0), tls.nextIdcs2.data(), outputEmbStride(),
					tls.resultBuf.data(), nextIdSize);
			}
			else
			{
				Eigen::Map<Eigen::MatrixXf> inputMap{ tls.inputEmbBuf.data(), header.dim, (Eigen::Index)(prevStateSize + uniqHistorySize) };
				Eigen::Map<Eigen::MatrixXf> outputMap{ tls.outputEmbBuf.data(), header.dim, (Eigen::Index)nextIdSize };
				resultMap += outputMap.transpose() * inputMap;
			}
			
			pair<int32_t, uint32_t> contextCache;
			for (size_t j = 0; j < nextIdSize; ++j)
			{
				for (size_t i = 0; i < prevStateSize; ++i)
				{
					const auto& state = prevStates[i];
					const bool cacheIsValid = i > 0 && state.node == prevStates[i - 1].node;
					outStates[i * nextIdSize + j] = nextState<windowSize>(state, nextIds[j], cacheIsValid, contextCache);
				}
			}


			for (size_t i = 0; i < prevStateSize; ++i)
			{
				const auto& state = prevStates[i];
				for (size_t j = 0; j < numInvalidDistantTokens; ++j)
				{
					outScores[i * nextIdSize + j] = resultMap(j, i);
				}
			}

			auto* validTokenSumBuf = tls.scoreBuf.data() + scoreBatchSize * (windowSize + 1);

			for (size_t i = 0; i < prevStateSize * numValidDistantTokens; i += scoreBatchSize)
			{
				const size_t batchSize = std::min(scoreBatchSize, prevStateSize * numValidDistantTokens - i);
				for (size_t j = 0; j < batchSize; ++j)
				{
					const auto pIdx = (i + j) / numValidDistantTokens;
					const auto nIdx = (i + j) % numValidDistantTokens + numInvalidDistantTokens;
					tls.scoreBuf[j] = tls.confidenceBuf[pIdx * 2];
					validTokenSumBuf[j] = tls.confidenceBuf[pIdx * 2 + 1];
					for (size_t k = 0; k < windowSize; ++k)
					{
						const auto idx = tls.inverseHistoryIdcs[pIdx * windowSize + k];
						tls.scoreBuf[j + (k + 1) * scoreBatchSize] = idx == -1 ? -99999 : tls.confidenceBuf[prevStateSize * 2 + idx];
					}
				}
				Eigen::Map<Eigen::Array<float, -1, windowSize + 1>> scoreMap{ tls.scoreBuf.data(), (Eigen::Index)scoreBatchSize, windowSize + 1 };
				scoreMap.rowwise() += Eigen::Map<const Eigen::Array<float, 1, windowSize + 1>>{ positionConfidPtr, 1, windowSize + 1 };
				LogSoftmaxTransposed<arch, windowSize + 1>{}(tls.scoreBuf.data(), batchSize, scoreBatchSize);
				scoreMap.rightCols<windowSize>().colwise() += Eigen::Map<Eigen::Array<float, scoreBatchSize, 1>>{ validTokenSumBuf, scoreBatchSize, 1 };
				for (size_t j = 0; j < batchSize; ++j)
				{
					const auto pIdx = (i + j) / numValidDistantTokens;
					const auto nIdx = (i + j) % numValidDistantTokens + numInvalidDistantTokens;
					tls.scoreBuf[j] += resultMap(nIdx, pIdx);
					for (size_t k = 0; k < windowSize; ++k)
					{
						const auto idx = tls.inverseHistoryIdcs[pIdx * windowSize + k];
						if (idx != -1)
						{
							tls.scoreBuf[j + (k + 1) * scoreBatchSize] += resultMap(nIdx, prevStateSize + idx);
						}
					}
				}
				LogSumExpTransposed<arch, windowSize + 1>{}(tls.scoreBuf.data(), batchSize, scoreBatchSize);

				for (size_t j = 0; j < batchSize; ++j)
				{
					const auto pIdx = (i + j) / numValidDistantTokens;
					const auto nIdx = (i + j) % numValidDistantTokens + numInvalidDistantTokens;
					outScores[pIdx * nextIdSize + nIdx] = tls.scoreBuf[j];
				}
			}
		}

		template<ArchType arch, class KeyType, class VlKeyType, size_t windowSize, bool quantized>
		template<size_t _windowSize>
		void PcLangModel<arch, KeyType, VlKeyType, windowSize, quantized>::progressMatrix(
			const typename std::enable_if<(_windowSize > 0), LmStateType>::type* prevStates, const KeyType* nextIds,
			size_t prevStateSize, size_t nextIdSize, size_t numValidDistantTokens,
			LmStateType* outStates, float* outScores) const
		{
			thread_local TLSForProgressMatrix tls;
			if (prevStateSize <= (quantized ? 16 : 8) && nextIdSize <= 16)
			{
				return progressMatrixWOSort(tls, prevStates, nextIds, prevStateSize, nextIdSize, numValidDistantTokens, outStates, outScores);
			}
			else
			{
				return progressMatrixWSort(tls, prevStates, nextIds, prevStateSize, nextIdSize, numValidDistantTokens, outStates, outScores);
			}
		}

		template<ArchType arch, class KeyType, class VlKeyType, size_t windowSize, bool quantized>
		template<size_t _windowSize>
		void PcLangModel<arch, KeyType, VlKeyType, windowSize, quantized>::progressMatrix(
			const typename std::enable_if<_windowSize == 0, LmStateType>::type* prevStates, const KeyType* nextIds,
			size_t prevStateSize, size_t nextIdSize, size_t numValidDistantTokens,
			LmStateType* outStates, float* outScores) const
		{
			thread_local Vector<uint64_t> contextIdcs, nextIdcs;
			thread_local Vector<uint32_t> inverseContextIdcs, inverseNextIdcs;
			thread_local Vector<float> inputEmbBuf, outputEmbBuf, resultBuf;
			thread_local Vector<int32_t> contextIdcs2, nextIdcs2;
			
			contextIdcs.resize(prevStateSize);
			nextIdcs.resize(nextIdSize);
			inverseContextIdcs.resize(prevStateSize);
			inverseNextIdcs.resize(nextIdSize);
			if (quantized)
			{
				contextIdcs2.clear();
				nextIdcs2.clear();
			}
			else
			{
				inputEmbBuf.resize(prevStateSize * header.dim);
				outputEmbBuf.resize(nextIdSize * header.dim);
			}
			
			for (size_t i = 0; i < nextIdSize; ++i)
			{
				nextIdcs[i] = mergePair(nextIds[i], i);
			}
			sort(nextIdcs.begin(), nextIdcs.end());
			size_t uniqOutputSize = 0;
			for (size_t i = 0; i < nextIdSize; ++i)
			{
				const auto nextId = splitPair(nextIdcs[i]).first;
				const auto idx = splitPair(nextIdcs[i]).second;
				if (i == 0 || nextId != splitPair(nextIdcs[i - 1]).first)
				{
					if (quantized)
					{
						nextIdcs2.emplace_back(nextId);
					}
					else
					{
						copy(getOutputEmb(nextId), getOutputEmb(nextId) + header.dim, &outputEmbBuf[uniqOutputSize * header.dim]);
					}
					uniqOutputSize++;
				}
				inverseNextIdcs[idx] = uniqOutputSize - 1;
			}
			resultBuf.resize(padMultipleOf(prevStateSize, 8) * padMultipleOf(uniqOutputSize, 8));

			for (size_t i = 0; i < prevStateSize; ++i)
			{
				contextIdcs[i] = mergePair(prevStates[i].contextIdx, i);
			}
			sort(contextIdcs.begin(), contextIdcs.end());
			size_t uniqInputSize = 0;
			for (size_t i = 0; i < prevStateSize; ++i)
			{
				const auto contextId = splitPair(contextIdcs[i]).first;
				const auto idx = splitPair(contextIdcs[i]).second;
				if (i == 0 || contextId != splitPair(contextIdcs[i - 1]).first)
				{
					if (quantized)
					{
						contextIdcs2.emplace_back(contextId);
					}
					else
					{
						copy(getContextEmb(contextId), getContextEmb(contextId) + header.dim, &inputEmbBuf[uniqInputSize * header.dim]);
						fill(&resultBuf[uniqInputSize * uniqOutputSize], &resultBuf[(uniqInputSize + 1) * uniqOutputSize], getContextBias(contextId));
					}
					uniqInputSize++;
				}
				inverseContextIdcs[idx] = uniqInputSize - 1;
			}

			Eigen::Map<Eigen::MatrixXf> resultMap{ resultBuf.data(), (Eigen::Index)uniqOutputSize, (Eigen::Index)uniqInputSize };
			if constexpr (quantized)
			{
				qgemm::scatteredGEMMOpt<arch>(
					uniqInputSize, uniqOutputSize, header.dim,
					getContextQuantEmb(0), contextIdcs2.data(), contextEmbStride(),
					getOutputQuantEmb(0), nextIdcs2.data(), outputEmbStride(),
					resultBuf.data(), uniqOutputSize);
			}
			else
			{
				Eigen::Map<Eigen::MatrixXf> inputMap{ inputEmbBuf.data(), header.dim, (Eigen::Index)uniqInputSize };
				Eigen::Map<Eigen::MatrixXf> outputMap{ outputEmbBuf.data(), header.dim, (Eigen::Index)uniqOutputSize };
				resultMap += outputMap.transpose() * inputMap;
			}

			pair<int32_t, uint32_t> contextCache;
			for (size_t j = 0; j < nextIdSize; ++j)
			{
			for (size_t i = 0; i < prevStateSize; ++i)
			{
				const auto& state = prevStates[i];
					const bool cacheIsValid = i > 0 && state.node == prevStates[i - 1].node;
					outStates[i * nextIdSize + j] = nextState<windowSize>(state, nextIds[j], cacheIsValid, contextCache);
				}
			}

			for (size_t i = 0; i < prevStateSize; ++i)
			{
				for (size_t j = 0; j < nextIdSize; ++j)
				{
					outScores[i * nextIdSize + j] = resultMap(inverseNextIdcs[j], inverseContextIdcs[i]);
				}
			}
		}

		utils::MemoryObject PcLangModelBase::build(const string& contextDefinition, const string& embedding, size_t maxContextLength, bool useVLE, bool reorderContextId)
		{
			ifstream contextStr, embeddingStr;
			if (!openFile(contextStr, contextDefinition))
			{
				throw IOException{ "Cannot open file : " + contextDefinition };
			}

			uint32_t maxClusterId = 0, maxContextId = 0;
			size_t keySize = 0;
			using Node = utils::TrieNodeEx<uint32_t, uint32_t, utils::ConstAccess<Map<uint32_t, int32_t>>>;
			utils::ContinuousTrie<Node> trie(1);
			{
				Vector<UnorderedMap<Vector<uint32_t>, uint32_t>> contextMap;
				UnorderedMap<Vector<uint32_t>, uint32_t> erasedContexts;
				Vector<uint32_t> context;
				string line;
				while (getline(contextStr, line))
				{
					auto tokens = split(line, '\t');
					if (tokens.size() <= 1)
					{
						throw IOException{ "Invalid format : " + contextDefinition };
					}

					auto clusterId = stol(tokens[0].begin(), tokens[0].end());
					if (clusterId < 0) throw IOException{ "Invalid format : " + contextDefinition };
					context.clear();
					for (size_t i = 1; i < tokens.size(); ++i)
					{
						auto id = stol(tokens[i].begin(), tokens[i].end());
						if (id < 0) throw IOException{ "Invalid format : " + contextDefinition };
						context.push_back(id);
						maxContextId = max(maxContextId, (uint32_t)id);
					}
					if (context.size() > maxContextLength)
					{
						continue;
					}
					if (contextMap.size() < context.size()) contextMap.resize(context.size());
					contextMap[context.size() - 1][context] = (uint32_t)clusterId;
					maxClusterId = max(maxClusterId, (uint32_t)(clusterId + 1));
				}

				for (size_t i = contextMap.size(); i-- > 0;) // remove redundant context
				{
					auto& c = contextMap[i];
					for (auto it = c.begin(); it != c.end();)
					{
						bool erase = false;
						for (size_t j = i; j-- > 0; )
						{
							auto& c2 = contextMap[j];
							context.clear();
							context.insert(context.end(), it->first.end() - j - 1, it->first.end());
							auto found = c2.find(context);
							if (found != c2.end())
							{
								erase = found->second == it->second;
								break;
							}
						}

						if (erase)
						{
							if (it->first.size() < contextMap.size())
							{
								erasedContexts.emplace(it->first, it->second);
							}
							it = c.erase(it);
						}
						else ++it;
					}
				}

				if (maxContextId <= 0xFFFF)
				{
					keySize = 2;
				}
				else if (useVLE && maxContextId <= 0xFFFFF)
				{
					keySize = 3; // variable length key
				}
				else
				{
					keySize = 4;
				}

				for (auto& c : contextMap)
				{
					for (auto& p : c)
					{
						if (keySize == 3)
						{
							static constexpr size_t tMax = (1 << 16) - (1 << 10) * 2;
							context.clear();
							for (auto id : p.first)
							{
								if (id < tMax)
								{
									context.emplace_back(id);
								}
								else
								{
									id -= tMax;
									const size_t high = id >> 10, low = id & 0x3FF;
									context.emplace_back(tMax + high);
									context.emplace_back(tMax + (1 << 10) + low);
								}
							}
							trie.build(context.begin(), context.end(), p.second + 1);
						}
						else
						{
							trie.build(p.first.begin(), p.first.end(), p.second + 1);
						}
					}
				}
			}

			Vector<uint32_t> nodeSizes;
			nodeSizes.reserve(trie.size());
			Vector<uint32_t> keys;
			keys.reserve(trie.size());
			Vector<uint32_t> values;
			values.reserve(trie.size());
			Vector<uint32_t> valueNewIdx(maxClusterId + 1);
			{
				Vector<size_t> valueCnts(valueNewIdx.size());
				Vector<uint32_t> valueArgsorted(valueNewIdx.size());
				Vector<uint32_t> rkeys;
				trie.traverseWithKeys([&](const Node* node, const Vector<uint32_t>& rkeys)
				{
					nodeSizes.emplace_back(node->next.size());
					for (auto& p : node->next)
					{
						keys.emplace_back(p.first);
					}
					values.emplace_back(node->val);
					valueCnts[node->val]++;
				}, rkeys);

				valueCnts[0] = -1;

				// remap value idx by frequency
				if (reorderContextId)
				{
					iota(valueArgsorted.begin(), valueArgsorted.end(), 0);
					sort(valueArgsorted.begin(), valueArgsorted.end(), [&](uint32_t a, uint32_t b) { return valueCnts[a] > valueCnts[b]; });
					for (size_t i = 0; i < valueArgsorted.size(); ++i)
					{
						valueNewIdx[valueArgsorted[i]] = (uint32_t)i;
					}
					for (auto& v : values) v = valueNewIdx[v];
				}
			}

			assert(nodeSizes.size() - 1 == keys.size());

			Vector<uint8_t> compressedNodeSizes(streamvbyte_max_compressedbytes(nodeSizes.size()));
			compressedNodeSizes.resize(streamvbyte_encode_0124(nodeSizes.data(), nodeSizes.size(), compressedNodeSizes.data()));
			Vector<uint8_t> compressedValues(streamvbyte_max_compressedbytes(values.size()));
			compressedValues.resize(streamvbyte_encode_0124(values.data(), values.size(), compressedValues.data()));
			Vector<uint8_t> compressedKeys(streamvbyte_max_compressedbytes(keys.size()));
			compressedKeys.resize(streamvbyte_encode(keys.data(), keys.size(), compressedKeys.data()));

			if (!openFile(embeddingStr, embedding, ios_base::binary))
			{
				throw IOException{ "Cannot open file : " + embedding };
			}
			const uint32_t dim = utils::read<uint32_t>(embeddingStr);
			const uint32_t contextSize = utils::read<uint32_t>(embeddingStr);
			const uint32_t outputSize = utils::read<uint32_t>(embeddingStr);
			const uint32_t windowSize = utils::read<uint32_t>(embeddingStr);

			Vector<int8_t> contextEmb(dim * contextSize);
			Vector<uint16_t> contextEmbScale(contextSize);
			Vector<uint16_t> contextEmbBias(contextSize);
			Vector<uint16_t> contextValidTokenSum(contextSize);
			Vector<uint16_t> contextConfidence(contextSize);
			Vector<int8_t> distantEmb(dim * outputSize);
			Vector<uint16_t> distantEmbScale(outputSize);
			Vector<uint16_t> distantEmbBias(outputSize);
			Vector<uint16_t> distantConfidence(outputSize);
			vector<uint16_t> positionConfidence(windowSize);
			Vector<int8_t> outputEmb(dim * outputSize);
			Vector<uint16_t> outputEmbScale(outputSize);
			Vector<uint8_t> distantMask(outputSize);

			embeddingStr.read((char*)contextEmb.data(), contextEmb.size());
			embeddingStr.read((char*)contextEmbScale.data(), contextEmbScale.size() * sizeof(uint16_t));
			embeddingStr.read((char*)contextEmbBias.data(), contextEmbBias.size() * sizeof(uint16_t));
			embeddingStr.read((char*)contextValidTokenSum.data(), contextValidTokenSum.size() * sizeof(uint16_t));
			embeddingStr.read((char*)contextConfidence.data(), contextConfidence.size() * sizeof(uint16_t));
			embeddingStr.read((char*)distantEmb.data(), distantEmb.size());
			embeddingStr.read((char*)distantEmbScale.data(), distantEmbScale.size() * sizeof(uint16_t));
			embeddingStr.read((char*)distantEmbBias.data(), distantEmbBias.size() * sizeof(uint16_t));
			embeddingStr.read((char*)distantConfidence.data(), distantConfidence.size() * sizeof(uint16_t));
			embeddingStr.read((char*)positionConfidence.data(), positionConfidence.size() * sizeof(uint16_t));
			embeddingStr.read((char*)outputEmb.data(), outputEmb.size());
			embeddingStr.read((char*)outputEmbScale.data(), outputEmbScale.size() * sizeof(uint16_t));
			embeddingStr.read((char*)distantMask.data(), distantMask.size());

			// remap context embedding
			if (reorderContextId)
			{
				Vector<int8_t> newContextEmb(contextEmb.size());
				Vector<uint16_t> newContextEmbScale(contextSize);
				Vector<uint16_t> newContextEmbBias(contextSize);
				Vector<uint16_t> newContextValidTokenSum(contextSize);
				for (size_t i = 0; i < contextSize; ++i)
				{
					auto idx = valueNewIdx[i];
					auto src = contextEmb.data() + i * dim;
					auto dst = newContextEmb.data() + idx * dim;
					copy(src, src + dim, dst);
					newContextEmbScale[idx] = contextEmbScale[i];
					newContextEmbBias[idx] = contextEmbBias[i];
					newContextValidTokenSum[idx] = contextValidTokenSum[i];
				}
				contextEmb = move(newContextEmb);
				contextEmbScale = move(newContextEmbScale);
				contextEmbBias = move(newContextEmbBias);
				contextValidTokenSum = move(newContextValidTokenSum);
			}

			// compress distantMask into bits
			const size_t compressedDistantMaskSize = (outputSize + 7) / 8;
			{
				for (size_t i = 0; i < outputSize; ++i)
				{
					if (i % 8 == 0)
					{
						distantMask[i / 8] = distantMask[i];
					}
					else
					{
						distantMask[i / 8] |= distantMask[i] << (i % 8);
					}
				}
				distantMask.resize(compressedDistantMaskSize);
			}

			PcLangModelHeader header;
			memset(&header, 0, sizeof(PcLangModelHeader));
			header.dim = dim;
			header.contextSize = contextSize;
			header.vocabSize = outputSize;
			header.keySize = keySize;
			header.windowSize = windowSize;
			header.numNodes = nodeSizes.size();

			size_t finalSize = 0;
			header.nodeOffset = alignedOffsetInc(finalSize, sizeof(PcLangModelHeader));
			header.keyOffset = alignedOffsetInc(finalSize, compressedNodeSizes.size());
			header.valueOffset = alignedOffsetInc(finalSize, compressedKeys.size());
			header.embOffset = alignedOffsetInc(finalSize, compressedValues.size());
			finalSize += dim * (contextSize + outputSize * 2);
			finalSize += contextSize * sizeof(uint16_t) * 4;
			finalSize += outputSize * sizeof(uint16_t) * 4;
			finalSize += windowSize * sizeof(uint16_t);
			finalSize += compressedDistantMaskSize;

			utils::MemoryOwner mem{ finalSize };
			utils::omstream ostr{ (char*)mem.get(), (std::ptrdiff_t)mem.size() };
			ostr.write((const char*)&header, sizeof(PcLangModelHeader));
			writePadding(ostr);
			ostr.write((const char*)compressedNodeSizes.data(), compressedNodeSizes.size());
			writePadding(ostr);
			ostr.write((const char*)compressedKeys.data(), compressedKeys.size());
			writePadding(ostr);
			ostr.write((const char*)compressedValues.data(), compressedValues.size());
			writePadding(ostr);

			for (size_t i = 0; i < contextSize; ++i)
			{
				ostr.write((const char*)&contextEmb[i * dim], dim);
				ostr.write((const char*)&contextEmbScale[i], sizeof(uint16_t));
				ostr.write((const char*)&contextEmbBias[i], sizeof(uint16_t));
				ostr.write((const char*)&contextConfidence[i], sizeof(uint16_t));
				ostr.write((const char*)&contextValidTokenSum[i], sizeof(uint16_t));
			}
			for (size_t i = 0; i < outputSize; ++i)
			{
				ostr.write((const char*)&outputEmb[i * dim], dim);
				ostr.write((const char*)&outputEmbScale[i], sizeof(uint16_t));
			}
			for (size_t i = 0; i < outputSize; ++i)
			{
				ostr.write((const char*)&distantEmb[i * dim], dim);
				ostr.write((const char*)&distantEmbScale[i], sizeof(uint16_t));
				ostr.write((const char*)&distantEmbBias[i], sizeof(uint16_t));
				ostr.write((const char*)&distantConfidence[i], sizeof(uint16_t));
			}
			ostr.write((const char*)positionConfidence.data(), positionConfidence.size() * sizeof(uint16_t));
			ostr.write((const char*)distantMask.data(), distantMask.size());
			return mem;
		}

		template<ArchType arch, class KeyType, class VlKeyType, size_t windowSize, bool quantized>
		void* PcLangModel<arch, KeyType, VlKeyType, windowSize, quantized>::getFindBestPathFn() const
		{
			return (void*)&BestPathFinder::findBestPath<PcLangModel<arch, KeyType, VlKeyType, windowSize, quantized>>;
		}

		template<ArchType arch, class KeyType, class VlKeyType, size_t windowSize, bool quantized>
		void* PcLangModel<arch, KeyType, VlKeyType, windowSize, quantized>::getNewJoinerFn() const
		{
			return (void*)&newJoinerWithKiwi<LmStateType>;
		}

		template<ArchType archType, class KeyTy, class VlKeyType, bool useDistantTokens, bool quantized>
		inline std::unique_ptr<PcLangModelBase> createOptimizedModelWithWindowSize(utils::MemoryObject&& mem)
		{
			auto& header = *reinterpret_cast<const PcLangModelHeader*>(mem.get());
			if (!useDistantTokens)
			{
				return make_unique<PcLangModel<archType, KeyTy, VlKeyType, 0, quantized>>(std::move(mem));
			}

			switch (header.windowSize)
			{
			case 7:
				return make_unique<PcLangModel<archType, KeyTy, VlKeyType, 7, quantized>>(std::move(mem));
			default:
				throw std::runtime_error{ "Unsupported `window_size` : " + std::to_string((size_t)header.windowSize) };
			};
		}

		template<ArchType archType, bool useDistantTokens, bool quantized>
		std::unique_ptr<PcLangModelBase> createOptimizedModel(utils::MemoryObject&& mem)
		{
			auto& header = *reinterpret_cast<const PcLangModelHeader*>(mem.get());
			switch (header.keySize)
			{
			case 2:
				return createOptimizedModelWithWindowSize<archType, uint16_t, uint16_t, useDistantTokens, quantized>(std::move(mem));
			case 3:
				return createOptimizedModelWithWindowSize<archType, uint32_t, uint16_t, useDistantTokens, quantized>(std::move(mem));
			case 4:
				return createOptimizedModelWithWindowSize<archType, uint32_t, uint32_t, useDistantTokens, quantized>(std::move(mem));
			default:
				throw std::runtime_error{ "Unsupported `key_size` : " + std::to_string((size_t)header.keySize) };
			}
		}

		using FnCreateOptimizedModel = decltype(&createOptimizedModel<ArchType::none, false, false>);

		template<bool useDistantTokens, bool quantized>
		struct CreateOptimizedModelGetter
		{
			template<std::ptrdiff_t i>
			struct Wrapper
			{
				static constexpr FnCreateOptimizedModel value = &createOptimizedModel<static_cast<ArchType>(i), useDistantTokens, quantized>;
			};
		};

		std::unique_ptr<PcLangModelBase> PcLangModelBase::create(utils::MemoryObject&& mem, ArchType archType, bool useDistantTokens, bool quantized)
		{
			static tp::Table<FnCreateOptimizedModel, AvailableArch> tables[] = {
				CreateOptimizedModelGetter<false, false>{},
				CreateOptimizedModelGetter<true, false>{},
			};
			static tp::Table<FnCreateOptimizedModel, QuantAvailableArch> quantTables[] = {
				CreateOptimizedModelGetter<false, true>{},
				CreateOptimizedModelGetter<true, true>{},
			};

			if (quantized)
			{
				auto fn = quantTables[useDistantTokens ? 1 : 0][static_cast<std::ptrdiff_t>(archType)];
				if (fn) return (*fn)(std::move(mem));
				std::cerr << "Quantization is not supported for " << archToStr(archType) << ". Fall back to non-quantized model." << std::endl;
			}
			auto fn = tables[useDistantTokens ? 1 : 0][static_cast<std::ptrdiff_t>(archType)];
			if (!fn) throw std::runtime_error{ std::string{"Unsupported architecture : "} + archToStr(archType) };
			return (*fn)(std::move(mem));
		}
	}
}
