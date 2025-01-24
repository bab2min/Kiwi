#pragma once

#include "LangModel.h"

namespace kiwi
{
	namespace lm
	{
		struct KnLangModelHeader
		{
			uint64_t num_nodes, node_offset, key_offset, ll_offset, gamma_offset, qtable_offset, htx_offset;
			uint64_t unk_id, bos_id, eos_id, vocab_size;
			uint8_t order, key_size, diff_size, quantized;
			uint32_t extra_buf_size;
		};

		template<class KeyType, class DiffType = int32_t>
		struct KnLangModelNode
		{
			KeyType num_nexts = 0;
			DiffType lower = 0;
			uint32_t next_offset = 0;
			float ll = 0, gamma = 0;
		};

		class KnLangModelBase : public ILangModel
		{
		protected:
			utils::MemoryObject base;

			KnLangModelBase(utils::MemoryObject&& mem) : base{ std::move(mem) }
			{
			}

			//virtual float getLL(ptrdiff_t node_idx, size_t next) const = 0;
			virtual float _progress(ptrdiff_t& node_idx, size_t next) const = 0;
			virtual std::vector<float> allNextLL(ptrdiff_t node_idx) const = 0;
			virtual std::vector<float> allNextLL(ptrdiff_t node_idx, std::vector<ptrdiff_t>& next_node_idx) const = 0;
			virtual void nextTopN(ptrdiff_t node_idx, size_t top_n, uint32_t* idx_out, float* ll_out) const = 0;

		public:

			virtual ~KnLangModelBase() {}
			size_t vocabSize() const override { return getHeader().vocab_size; }
			size_t getMemorySize() const override { return base.size(); }

			const KnLangModelHeader& getHeader() const { return *reinterpret_cast<const KnLangModelHeader*>(base.get()); }

			virtual ptrdiff_t getLowerNode(ptrdiff_t node_idx) const = 0;

			virtual size_t nonLeafNodeSize() const = 0;
			virtual const void* getExtraBuf() const = 0;

			static std::unique_ptr<KnLangModelBase> create(utils::MemoryObject&& mem, ArchType archType = ArchType::none, bool transposed = false);

			template<class VocabTy, class Trie, class HistoryTx = std::vector<VocabTy>>
			static utils::MemoryOwner build(Trie&& ngram_cf,
				size_t order, const std::vector<size_t>& min_cf_by_order,
				size_t unk_id, size_t bos_id, size_t eos_id,
				float unigram_alpha, size_t quantize, bool compress,
				const std::vector<std::pair<VocabTy, VocabTy>>* bigram_list = nullptr,
				const HistoryTx* history_transformer = nullptr,
				const void* extra_buf = nullptr,
				size_t extra_buf_size = 0
			);

			const utils::MemoryObject& getMemory() const { return base; }

			template<class Ty>
			float progress(ptrdiff_t& node_idx, Ty next) const
			{
				return _progress(node_idx, next);
			}

			template<class InTy, class OutTy>
			void evaluate(InTy in_first, InTy in_last, OutTy out_first) const
			{
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					*out_first = _progress(node_idx, *in_first);
					++out_first;
				}
			}

			template<class InTy, class OutProbTy, class OutNodeTy>
			void evaluate(InTy in_first, InTy in_last, OutProbTy prob_first, OutNodeTy node_first) const
			{
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					*node_first = node_idx;
					*prob_first = _progress(node_idx, *in_first);
					++prob_first;
					++node_first;
				}
			}

			template<class InTy>
			float sum(InTy in_first, InTy in_last, float min_score = -100) const
			{
				float ret = 0;
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					ret += std::max(_progress(node_idx, *in_first), min_score);
				}
				return ret;
			}

			template<class InTy>
			std::vector<float> getNextLL(InTy in_first, InTy in_last) const
			{
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					_progress(node_idx, *in_first);
				}
				return allNextLL(node_idx);
			}

			template<class InTy, class OutTy>
			void predict(InTy in_first, InTy in_last, OutTy out_first) const
			{
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					_progress(node_idx, *in_first);
					*out_first = allNextLL(node_idx);
					++out_first;
				}
			}

			template<class InTy>
			void predictTopN(InTy in_first, InTy in_last, size_t top_n, uint32_t* idx_out, float* ll_out) const
			{
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					_progress(node_idx, *in_first);
					nextTopN(node_idx, top_n, idx_out, ll_out);
					idx_out += top_n;
					ll_out += top_n;
				}
			}

			template<class PfTy, class SfTy, class OutTy>
			void fillIn(PfTy prefix_first, PfTy prefix_last, SfTy suffix_first, SfTy suffix_last, OutTy out_first, bool reduce = true) const
			{
				ptrdiff_t node_idx = 0;
				for (; prefix_first != prefix_last; ++prefix_first)
				{
					_progress(node_idx, *prefix_first);
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
