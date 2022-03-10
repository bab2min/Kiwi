#pragma once

#include <array>
#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <numeric>

#include "Mmap.h"

namespace kiwi
{
	namespace lm
	{
		using Vid = uint16_t;

		struct Header
		{
			uint64_t num_nodes, node_offset, key_offset, ll_offset, gamma_offset, qtable_offset, htx_offset;
			uint64_t unk_id, bos_id, eos_id, vocab_size;
			uint8_t order, key_size, diff_size, quantized;
		};

		template<class KeyType, class DiffType = int32_t>
		struct Node
		{
			KeyType num_nexts = 0;
			DiffType lower = 0;
			uint32_t next_offset = 0;
		};

		class KnLangModelBase
		{
		protected:
			utils::MemoryObject base;

			KnLangModelBase(utils::MemoryObject&& mem) : base{ std::move(mem) }
			{
			}

			//virtual float getLL(ptrdiff_t node_idx, size_t next) const = 0;
			virtual std::vector<float> allNextLL(ptrdiff_t node_idx) const = 0;
			virtual std::vector<float> allNextLL(ptrdiff_t node_idx, std::vector<ptrdiff_t>& next_node_idx) const = 0;

		public:

			virtual ~KnLangModelBase() {}
			const Header& getHeader() const { return *reinterpret_cast<const Header*>(base.get()); }

			virtual size_t llSize() const = 0;
			virtual const float* getLLBuf() const = 0;
			virtual const float* getGammaBuf() const = 0;

			static std::unique_ptr<KnLangModelBase> create(utils::MemoryObject&& mem, ArchType archType = ArchType::none);

			template<class TrieNode, class HistoryTx = std::vector<Vid>>
			static utils::MemoryOwner build(const utils::ContinuousTrie<TrieNode>& ngram_cf,
				size_t order, size_t min_cf, size_t last_min_cf,
				size_t unk_id, size_t bos_id, size_t eos_id,
				float unigram_alpha, size_t quantize, bool compress,
				const std::vector<std::pair<Vid, Vid>>* bigram_list = nullptr,
				const HistoryTx* historyTransformer = nullptr
			);

			const utils::MemoryObject& getMemory() const { return base; }

			//virtual float progress(ptrdiff_t& node_idx, size_t next) const = 0;

			template<class InTy, class OutTy>
			void evaluate(InTy in_first, InTy in_last, OutTy out_first) const
			{
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					*out_first = progress(node_idx, *in_first);
					++out_first;
				}
			}

			template<class InTy>
			float sum(InTy in_first, InTy in_last, float min_score = -100) const
			{
				float ret = 0;
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					ret += std::max(progress(node_idx, *in_first), min_score);
				}
				return ret;
			}

			template<class InTy>
			std::vector<float> getNextLL(InTy in_first, InTy in_last) const
			{
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					progress(node_idx, *in_first);
				}
				return allNextLL(node_idx);
			}

			template<class InTy, class OutTy>
			void predict(InTy in_first, InTy in_last, OutTy out_first) const
			{
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					progress(node_idx, *in_first);
					*out_first = allNextLL(node_idx);
					++out_first;
				}
			}

			template<class PfTy, class SfTy, class OutTy>
			void fillIn(PfTy prefix_first, PfTy prefix_last, SfTy suffix_first, SfTy suffix_last, OutTy out_first, bool reduce = true) const
			{
				ptrdiff_t node_idx = 0;
				for (; prefix_first != prefix_last; ++prefix_first)
				{
					progress(node_idx, *prefix_first);
				}

				std::vector<ptrdiff_t> next_node_idcs;
				*out_first = allNextLL(node_idx, next_node_idcs);

				if (reduce)
				{
					for (size_t i = 0; i < next_node_idcs.size(); ++i)
					{
						auto node_idx = next_node_idcs[i];
						for (auto it = suffix_first; it != suffix_last; ++it)
						{
							(*out_first)[i] += progress(node_idx, *it);
						}
					}
				}
				else
				{
					++out_first;
					for (size_t i = 0; i < next_node_idcs.size(); ++i)
					{
						auto node_idx = next_node_idcs[i];
						auto out_next = out_first;
						for (auto it = suffix_first; it != suffix_last; ++it)
						{
							(*out_next)[i] = progress(node_idx, *it);
							++out_next;
						}
					}
				}
			}
		};
	}
}
