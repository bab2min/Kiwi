#pragma once

#include <Eigen/Dense>

#include <kiwi/Types.h>
#include <kiwi/Utils.h>
#include <kiwi/PCLanguageModel.h>
#include <kiwi/ArchUtils.h>
#include "ArchAvailable.h"
#include "search.h"
#include "streamvbyte.h"
#include "SkipBigramModelImpl.hpp"

namespace kiwi
{
	namespace lm
	{
		template<size_t windowSize, ArchType _arch, class VocabTy, class VlVocabTy, bool quantized>
		class PcLMState;

		template<ArchType arch, class KeyType, class VlKeyType, size_t windowSize, bool quantized = true>
		class PcLangModel : public PcLangModelBase
		{
			using MyNode = Node<KeyType, uint32_t>;

			std::unique_ptr<MyNode[]> nodeData;
			std::unique_ptr<uint8_t[]> keyValueData;
			const uint8_t* alignedKeyValueData = nullptr;
			std::unique_ptr<int32_t[]> allRootValueData;
			std::unique_ptr<uint8_t[]> allEmbs;
			const uint8_t* contextEmbPtr = nullptr; // [numContexts, (dim + scale? + bias + confid + vts)]
			const uint8_t* outputEmbPtr = nullptr; // [numOutputs, (dim + scale? + sum?)]
			const uint8_t* distantEmbPtr = nullptr; // [numOutputs, (dim + scale? + bias + confid + pad?)]
			const float* positionConfidPtr = nullptr;
			const uint8_t* distantMaskPtr = nullptr;

			inline size_t contextEmbStride() const
			{
				if (quantized) return header.dim + (windowSize > 0 ? 4 : 2) * sizeof(float);
				else return (header.dim + (windowSize > 0 ? 3 : 1)) * sizeof(float);
			}

			inline size_t outputEmbStride() const
			{
				if (quantized) return header.dim + 2 * sizeof(float);
				else return header.dim * sizeof(float);
			}

			inline size_t distantEmbStride() const
			{
				if (quantized) return header.dim + 4 * sizeof(float);
				else return (header.dim + 2) * sizeof(float);
			}

			inline const float* getContextEmb(uint32_t idx) const
			{
				return reinterpret_cast<const float*>(contextEmbPtr + idx * contextEmbStride());
			}

			inline const uint8_t* getContextQuantEmb(uint32_t idx) const
			{
				return contextEmbPtr + idx * contextEmbStride();
			}

			inline float getContextBias(uint32_t idx) const
			{
				const size_t offset = quantized ?
					(header.dim + sizeof(float))
					: (header.dim * sizeof(float));
				return *reinterpret_cast<const float*>(contextEmbPtr + idx * contextEmbStride() + offset);
			}

			inline float getContextConfid(uint32_t idx) const
			{
				if (windowSize == 0) return 0;
				const size_t offset = quantized ?
					(header.dim + 2 * sizeof(float))
					: (header.dim + 1) * sizeof(float);
				return *reinterpret_cast<const float*>(contextEmbPtr + idx * contextEmbStride() + offset);
			}

			inline float getContextValidTokenSum(uint32_t idx) const
			{
				if (windowSize == 0) return 0;
				const size_t offset = quantized ?
					(header.dim + 3 * sizeof(float))
					: (header.dim + 2) * sizeof(float);
				return *reinterpret_cast<const float*>(contextEmbPtr + idx * contextEmbStride() + offset);
			}

			inline const float* getOutputEmb(uint32_t idx) const
			{
				return reinterpret_cast<const float*>(outputEmbPtr + idx * outputEmbStride());
			}

			inline const int8_t* getOutputQuantEmb(uint32_t idx) const
			{
				return reinterpret_cast<const int8_t*>(outputEmbPtr + idx * outputEmbStride());
			}

			inline const float* getDistantEmb(uint32_t idx) const
			{
				return reinterpret_cast<const float*>(distantEmbPtr + idx * distantEmbStride());
			}

			inline const uint8_t* getDistantQuantEmb(uint32_t idx) const
			{
				return distantEmbPtr + idx * distantEmbStride();
			}

			inline float getDistantBias(uint32_t idx) const
			{
				if (windowSize == 0) return 0;
				const size_t offset = quantized ?
					(header.dim + sizeof(float))
					: (header.dim * sizeof(float));
				return *reinterpret_cast<const float*>(distantEmbPtr + idx * distantEmbStride() + offset);
			}

			inline float getDistantConfid(uint32_t idx) const
			{
				if (windowSize == 0) return 0;
				const size_t offset = quantized ?
					(header.dim + 2 * sizeof(float))
					: (header.dim + 1) * sizeof(float);
				return *reinterpret_cast<const float*>(distantEmbPtr + idx * distantEmbStride() + offset);
			}

			MyNode* findLowerNode(MyNode* node, KeyType k) const
			{
				while (node->lower)
				{
					auto* lowerNode = node + node->lower;
					auto* kvs = &alignedKeyValueData[lowerNode->nextOffset];
					int32_t found;
					if ((found = nst::searchKV<arch, VlKeyType, int32_t, int32_t>(
						kvs,
						lowerNode->numNexts,
						k)) > 0)
					{
						return lowerNode + found;
					}
					node = lowerNode;
				}
				return node;
			}

			uint32_t findLowerValue(MyNode* node, KeyType k) const
			{
				while (node->lower)
				{
					auto* lowerNode = node + node->lower;
					auto* kvs = &alignedKeyValueData[lowerNode->nextOffset];
					int32_t found;
					if ((found = nst::searchKV<arch, VlKeyType, int32_t, int32_t>(
						kvs,
						lowerNode->numNexts,
						k)) != 0)
					{
						if (found >= 0)
						{
							return lowerNode[found].value;
						}
						else
						{
							return -found;
						}
					}
					node = lowerNode;
				}
				return node->value;
			}

		public:
			using VocabType = KeyType;
			using LmStateType = PcLMState<windowSize, arch, VocabType, VlKeyType, quantized>;

			PcLangModel(utils::MemoryObject&& mem);

			ModelType getType() const override 
			{ 
				if (quantized)
				{
					if (windowSize > 0) return ModelType::pclmQuantized;
					else return ModelType::pclmLocalQuantized;
				}
				else
				{
					if (windowSize > 0) return ModelType::pclm;
					else return ModelType::pclmLocal;
				}
			}
			void* getFindBestPathFn() const override;
			void* getNewJoinerFn() const override;

			uint32_t progressContextNode(int32_t& nodeIdx, KeyType next) const
			{
				if (std::is_same<KeyType, VlKeyType>::value)
				{
					return progressContextNodeVl(nodeIdx, next);
				}

				static constexpr size_t tMax = (1 << 16) - (1 << 10) * 2;
				if (next < tMax)
				{
					return progressContextNodeVl(nodeIdx, next);
				}
				next -= tMax;
				const size_t high = next >> 10, low = next & 0x3FF;
				progressContextNodeVl(nodeIdx, tMax + high);
				return progressContextNodeVl(nodeIdx, tMax + (1 << 10) + low);
			}

			uint32_t progressContextNodeVl(int32_t& nodeIdx, VlKeyType next) const
			{
				static constexpr size_t N = 64 / sizeof(VlKeyType) + 1;
				while (1)
				{
					int32_t v;
					auto* node = &nodeData[nodeIdx];
					auto* kvs = &alignedKeyValueData[node->nextOffset];
					if (node != nodeData.get())
					{
						//ScopedTimer<> timer(node->numNexts <= 16 ? 0 : node->numNexts <= 272 ? 1 : 2);
						PREFETCH_T0(node + node->lower);
						if ((v = nst::searchKV<arch, VlKeyType, int32_t, int32_t>(
							kvs,
							node->numNexts, next
						)) == 0)
						{
							if (!node->lower) return 0;
							nodeIdx += node->lower;
							PREFETCH_T0(&alignedKeyValueData[nodeData[nodeIdx].nextOffset]);
							continue;
						}
					}
					else
					{
						v = allRootValueData[next];
						if (v == 0)
						{
							return 0;
						}
					}

					// non-leaf node
					if (v > 0)
					{
						nodeIdx += v;
						return nodeData[nodeIdx].value;
					}
					// leaf node
					else
					{
						while (node->lower)
						{
							node += node->lower;
							auto* lkvs = &alignedKeyValueData[node->nextOffset];
							int32_t lv;
							if (node != nodeData.get())
							{
								if ((lv = nst::searchKV<arch, VlKeyType, int32_t, int32_t>(
									lkvs,
									node->numNexts, next
								)) != 0)
								{
									if (lv > 0)
									{
										node += lv;
										nodeIdx = node - &nodeData[0];
										return (uint32_t)-v;
									}
								}
							}
							else
							{
								lv = allRootValueData[next];
								if (lv > 0)
								{
									nodeIdx = lv;
									return (uint32_t)-v;
								}
							}
						}
						nodeIdx = 0;
						return (uint32_t)-v;
					}
				}
			}

			inline bool distantTokenMask(uint32_t idx) const
			{
				if (windowSize > 0) return (distantMaskPtr[idx / 8] & (1 << (idx % 8))) != 0;
				else return false;
			}

			float progress(int32_t& nodeIdx,
				uint32_t& contextIdx,
				std::array<KeyType, windowSize + 1>& history,
				KeyType next) const;

			template<size_t _windowSize>
			LmStateType nextState(const typename std::enable_if<(_windowSize > 0), LmStateType>::type& state, KeyType next, 
				bool cacheIsValid, std::pair<int32_t, uint32_t>& cache) const;

			template<size_t _windowSize>
			LmStateType nextState(const typename std::enable_if<_windowSize == 0, LmStateType>::type& state, KeyType next, 
				bool cacheIsValid, std::pair<int32_t, uint32_t>& cache) const;

			/*
			* 총 prevStateSize개의 상태와 nextIdSize개의 다음 토큰을 받아서, 각 상태별로 다음 토큰이 등장할 확률을 계산하고 새 상태를 반환한다.
			* 새 상태값은 outStates에 저장되고, 각 상태별 확률값은 outScores에 저장된다.
			* nextIdSize개의 다음 토큰 중 마지막 numValidDistantTokens개의 토큰은 유효한 distant 토큰으로 처리된다.
			*/
			template<size_t _windowSize>
			void progressMatrix(const typename std::enable_if<(_windowSize > 0), LmStateType>::type* prevStates, const KeyType* nextIds,
				size_t prevStateSize, size_t nextIdSize, size_t numValidDistantTokens,
				LmStateType* outStates, float* outScores) const;

			struct TLSForProgressMatrix;

			inline void progressMatrixWSort(TLSForProgressMatrix& tls, const LmStateType* prevStates, const KeyType* nextIds,
				size_t prevStateSize, size_t nextIdSize, size_t numValidDistantTokens,
				LmStateType* outStates, float* outScores) const;

			inline void progressMatrixWOSort(TLSForProgressMatrix& tls, const LmStateType* prevStates, const KeyType* nextIds,
				size_t prevStateSize, size_t nextIdSize, size_t numValidDistantTokens,
				LmStateType* outStates, float* outScores) const;

			template<size_t _windowSize>
			void progressMatrix(const typename std::enable_if<(_windowSize == 0), LmStateType>::type* prevStates, const KeyType* nextIds,
				size_t prevStateSize, size_t nextIdSize, size_t numValidDistantTokens,
				LmStateType* outStates, float* outScores) const;
		};

		template<size_t windowSize, ArchType _arch, class VocabTy, class VlVocabTy, bool quantized>
		struct PcLMState : public LmStateBase<PcLangModel<_arch, VocabTy, VlVocabTy, windowSize, quantized>>
		{
			int32_t node = 0;
			uint32_t contextIdx;
			std::array<VocabTy, windowSize + 1> history;

			static constexpr ArchType arch = _arch;
			static constexpr bool transposed = true;

			PcLMState() : contextIdx{ 0 }, history { { 0, } }
			{
			}

			PcLMState(const ILangModel* lm) : contextIdx{ 0 }, history{ {0,} }
			{
			}

			PcLMState(int32_t _node) : node{ _node } // partially initialized state
			{
			}

			bool operator==(const PcLMState& other) const
			{
				static constexpr size_t cmpStart = windowSize / 2;
				if (node != other.node) return false;
				if (memcmp(&history[cmpStart], &other.history[cmpStart], (windowSize - cmpStart) * sizeof(VocabTy)))
				{
						return false;
					}
				return true;
			}

			float nextImpl(const PcLangModel<arch, VocabTy, VlVocabTy, windowSize, quantized>* lm, VocabTy next)
			{
				return lm->progress(node, contextIdx, history, next);
			}
		};

		template<ArchType _arch, class VocabTy, class VlVocabTy, bool quantized>
		struct PcLMState<0, _arch, VocabTy, VlVocabTy, quantized> : public LmStateBase<PcLangModel<_arch, VocabTy, VlVocabTy, 0, quantized>>
		{
			int32_t node = 0;
			uint32_t contextIdx;

			static constexpr ArchType arch = _arch;
			static constexpr bool transposed = true;
			static constexpr size_t windowSize = 0;

			PcLMState() : contextIdx{ 0 }
			{
			}

			PcLMState(const ILangModel* lm) : contextIdx{ 0 }
			{
			}
			
			PcLMState(int32_t _node) : node{ _node } // partially initialized state
			{
			}

			bool operator==(const PcLMState& other) const
			{
				return node == other.node;
			}

			float nextImpl(const PcLangModel<arch, VocabTy, VlVocabTy, windowSize, quantized>* lm, VocabTy next)
			{
				std::array<VocabTy, windowSize + 1> history = { {0,} };
				return lm->progress(node, contextIdx, history, next);
			}
		};
	}

	template<size_t windowSize, ArchType arch, class VocabTy, class VlVocabTy, bool quantized>
	struct Hash<lm::PcLMState<windowSize, arch, VocabTy, VlVocabTy, quantized>>
	{
		size_t operator()(const lm::PcLMState<windowSize, arch, VocabTy, VlVocabTy, quantized>& state) const
		{
			size_t ret = (uint32_t)(state.node * (size_t)2654435761);
			static constexpr size_t cmpStart = windowSize - sizeof(size_t) / sizeof(VocabTy);
			const auto h = *reinterpret_cast<const size_t*>(&state.history[cmpStart]);
			ret = h ^ ((ret << 3) | (ret >> (sizeof(size_t) * 8 - 3)));
			return ret;
		}
	};

	template<ArchType arch, class VocabTy, class VlVocabTy, bool quantized>
	struct Hash<lm::PcLMState<0, arch, VocabTy, VlVocabTy, quantized>>
	{
		size_t operator()(const lm::PcLMState<0, arch, VocabTy, VlVocabTy, quantized>& state) const
		{
			size_t ret = (uint32_t)(state.node * (size_t)2654435761);
			return ret;
		}
	};
}
