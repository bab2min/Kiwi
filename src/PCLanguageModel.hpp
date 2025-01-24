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
		template<size_t windowSize, ArchType _arch, class VocabTy, bool exclusive, bool useDistantTokens>
		class PcLMState;

		template<ArchType arch, class KeyType, size_t windowSize, bool exclusive = false, bool useDistantTokens = false>
		class PcLangModel : public PcLangModelBase
		{
			using MyNode = Node<KeyType, uint32_t>;

			std::unique_ptr<MyNode[]> nodeData;
			std::unique_ptr<KeyType[]> keyData;
			std::unique_ptr<int32_t[]> valueData;
			std::unique_ptr<float[]> contextEmb;
			std::unique_ptr<float[]> contextBias;
			std::unique_ptr<float[]> contextValidTokenSum;
			std::unique_ptr<float[]> contextConf;
			std::unique_ptr<float[]> distantEmb;
			std::unique_ptr<float[]> distantBias;
			std::unique_ptr<float[]> distantConf;
			std::unique_ptr<float[]> positionConf;
			std::unique_ptr<float[]> outputEmb;
			std::unique_ptr<uint8_t[]> distantMask;

			MyNode* findLowerNode(MyNode* node, KeyType k) const
			{
				while (node->lower)
				{
					auto* lowerNode = node + node->lower;
					auto* keys = &keyData[lowerNode->nextOffset];
					auto* values = &valueData[lowerNode->nextOffset];
					int32_t found;
					if (nst::search<arch>(
						keys,
						values,
						lowerNode->numNexts,
						k,
						found
					) && found >= 0)
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
					auto* keys = &keyData[lowerNode->nextOffset];
					auto* values = &valueData[lowerNode->nextOffset];
					int32_t found;
					if (nst::search<arch>(
						keys,
						values,
						lowerNode->numNexts,
						k,
						found
					))
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
			using LmStateType = PcLMState<windowSize, arch, VocabType, exclusive, useDistantTokens>;

			PcLangModel(utils::MemoryObject&& mem);

			void* getFindBestPathFn() const override;
			void* getNewJoinerFn() const override;

			uint32_t progressContextNode(int32_t& nodeIdx, KeyType next) const
			{
				while (1)
				{
					int32_t v;
					auto* node = &nodeData[nodeIdx];
					auto* keys = &keyData[node->nextOffset];
					auto* values = &valueData[node->nextOffset];
					PREFETCH_T0(node + node->lower);
					if (!nst::search<arch>(
						keys,
						values,
						node->numNexts, next, v
					))
					{
						if (!node->lower) return 0;
						nodeIdx += node->lower;
						PREFETCH_T0(&keyData[nodeData[nodeIdx].nextOffset]);
						continue;
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
							int32_t lv;
							if (nst::search<arch>(
								&keyData[node->nextOffset],
								&valueData[node->nextOffset],
								node->numNexts, next, lv
							))
							{
								if (lv > 0)
								{
									node += lv;
									nodeIdx = node - &nodeData[0];
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
				if (useDistantTokens) return (distantMask[idx / 8] & (1 << (idx % 8))) != 0;
				else return false;
			}

			float progress(int32_t& nodeIdx,
				uint32_t& contextIdx,
				size_t& historyPos,
				std::array<KeyType, windowSize + (exclusive ? 1 : 0)>& history,
				KeyType next) const;

		};

		template<size_t windowSize, ArchType _arch, class VocabTy, bool exclusive, bool useDistantTokens>
		struct PcLMState : public LmStateBase<PcLangModel<_arch, VocabTy, windowSize, exclusive, useDistantTokens>>
		{
			int32_t node = 0;
			uint32_t contextIdx = 0;

			static constexpr ArchType arch = _arch;
			static constexpr bool transposed = true;

			PcLMState() = default;
			PcLMState(const ILangModel* lm) {}

			bool operator==(const PcLMState& other) const
			{
				return node == other.node;
			}

			float nextImpl(const PcLangModel<arch, VocabTy, windowSize, exclusive, useDistantTokens>* lm, VocabTy next)
			{
				size_t historyPos = 0;
				std::array<VocabTy, windowSize + (exclusive ? 1 : 0)> history = { {0,} };
				return lm->progress(node, contextIdx, historyPos, history, next);
			}
		};

		template<size_t windowSize, ArchType _arch, class VocabTy, bool exclusive>
		struct PcLMState<windowSize, _arch, VocabTy, exclusive, true> : public LmStateBase<PcLangModel<_arch, VocabTy, windowSize, exclusive, true>>
		{
			static constexpr bool useDistantTokens = true;

			int32_t node = 0;
			uint32_t contextIdx = 0;
			size_t historyPos = 0;
			std::array<VocabTy, windowSize + (exclusive ? 1 : 0)> history = { {0,} };
		
			static constexpr ArchType arch = _arch;
			static constexpr bool transposed = true;

			PcLMState() = default;
			PcLMState(const ILangModel* lm) {}

			bool operator==(const PcLMState& other) const
			{
				return node == other.node && historyPos == other.historyPos && history == other.history;
			}

			float nextImpl(const PcLangModel<arch, VocabTy, windowSize, exclusive, useDistantTokens>* lm, VocabTy next)
			{
				return lm->progress(node, contextIdx, historyPos, history, next);
			}
		};
	}

	template<size_t windowSize, ArchType arch, class VocabTy, bool exclusive>
	struct Hash<lm::PcLMState<windowSize, arch, VocabTy, exclusive, false>>
	{
		size_t operator()(const lm::PcLMState<windowSize, arch, VocabTy, exclusive, false>& state) const
		{
			Hash<int32_t> hasher;
			return hasher(state.node);
		}
	};

	template<size_t windowSize, ArchType arch, class VocabTy, bool exclusive>
	struct Hash<lm::PcLMState<windowSize, arch, VocabTy, exclusive, true>>
	{
		size_t operator()(const lm::PcLMState<windowSize, arch, VocabTy, exclusive, true>& state) const
		{
			Hash<int32_t> hasher;
			std::hash<VocabTy> vocabHasher;
			size_t ret = hasher(state.node);
			for (size_t i = 0; i < state.history.size(); ++i)
			{
				ret = vocabHasher(state.history[i]) ^ ((ret << 3) | (ret >> (sizeof(size_t) * 8 - 3)));
			}
			return ret;
		}
	};
}
