#pragma once

#include <sstream>
#include <cmath>
#include <kiwi/Knlm.h>
#include <kiwi/Trie.hpp>
#include <kiwi/FrozenTrie.h>
#include <kiwi/Utils.h>
#include <kiwi/TemplateUtils.hpp>
#include "ArchAvailable.h"
#include "search.h"
#include "BitEncoder.hpp"
#include "QEncoder.hpp"
#include "nuquant.hpp"

namespace kiwi
{
	namespace lm
	{
		static constexpr size_t serialAlignment = 16;


		using QCode = qe::QCode<0, 2, 8, 16>;

		template<size_t bits>
		inline void dequantize(
			Vector<float>& restored_floats, Vector<float>& restored_leaf_ll,
			const char* llq_data, size_t llq_size,
			const char* gammaq_data, size_t gammaq_size,
			const float* ll_table,
			const float* gamma_table,
			size_t num_non_leaf_nodes,
			size_t num_leaf_nodes
		)
		{
			FixedLengthEncoder<utils::imstream, bits, uint32_t> llq{ llq_data, (ptrdiff_t)llq_size };
			FixedLengthEncoder<utils::imstream, bits, uint32_t> gammaq{ gammaq_data, (ptrdiff_t)gammaq_size };

			for (size_t i = 0; i < num_non_leaf_nodes; ++i)
			{
				restored_floats[i] = ll_table[llq.read()];
			}

			for (size_t i = 0; i < num_leaf_nodes; ++i)
			{
				restored_leaf_ll[i] = ll_table[llq.read()];
			}

			for (size_t i = 0; i < num_non_leaf_nodes; ++i)
			{
				restored_floats[i + num_non_leaf_nodes] = gamma_table[gammaq.read()];
			}
		}

		template<>
		inline void dequantize<8>(
			Vector<float>& restored_floats, Vector<float>& restored_leaf_ll,
			const char* llq_data, size_t llq_size,
			const char* gammaq_data, size_t gammaq_size,
			const float* ll_table,
			const float* gamma_table,
			size_t num_non_leaf_nodes,
			size_t num_leaf_nodes
		)
		{
			const uint8_t* non_leaf_q = reinterpret_cast<const uint8_t*>(llq_data);
			for (size_t i = 0; i < num_non_leaf_nodes; ++i)
			{
				restored_floats[i] = ll_table[non_leaf_q[i]];
			}

			const uint8_t* leaf_q = reinterpret_cast<const uint8_t*>(llq_data + num_non_leaf_nodes);
			for (size_t i = 0; i < num_leaf_nodes; ++i)
			{
				restored_leaf_ll[i] = ll_table[leaf_q[i]];
			}

			const uint8_t* gamma_q = reinterpret_cast<const uint8_t*>(gammaq_data);
			for (size_t i = 0; i < num_non_leaf_nodes; ++i)
			{
				restored_floats[i + num_non_leaf_nodes] = gamma_table[gamma_q[i]];
			}
		}

		inline const void* toAlignedPtr(const void* ptr, size_t alignment = serialAlignment)
		{
			auto addr = reinterpret_cast<size_t>(ptr);
			return reinterpret_cast<const void*>((addr + alignment - 1) & ~(alignment - 1));
		}

		template<ArchType arch, class KeyType, class DiffType = int32_t>
		class KnLangModel : public KnLangModelBase
		{
			using MyNode = Node<KeyType, DiffType>;

			std::unique_ptr<MyNode[]> node_data;
			std::unique_ptr<KeyType[]> key_data;
			std::unique_ptr<DiffType[]> all_value_data;
			size_t num_non_leaf_nodes = 0;
			DiffType* value_data = nullptr;
			const float* ll_data = nullptr;
			const float* gamma_data = nullptr;
			const KeyType* htx_data = nullptr;
			const void* extra_buf = nullptr;
			Vector<float> restored_floats;
			float unk_ll = 0;
			ptrdiff_t bos_node_idx = 0;

			MyNode* findLowerNode(MyNode* node, KeyType k)
			{
				while (node->lower)
				{
					auto* lower_node = node + node->lower;
					if (lower_node == &node_data[0] && htx_data)
					{
						k = htx_data[k];
					}
					auto* keys = &key_data[lower_node->next_offset];
					auto* values = &value_data[lower_node->next_offset];
					DiffType found;
					if (nst::search<arch>(
						keys,
						values,
						lower_node->num_nexts,
						k,
						found
					))
					{
						return lower_node + found;
					}
					node = lower_node;
				}
				return node;
			}

			template<ptrdiff_t ...idx>
			static void dequantizeDispatch(
				tp::seq<idx...>,
				size_t bits,
				Vector<float>& restored_floats, Vector<float>& restored_leaf_ll,
				const char* llq_data, size_t llq_size,
				const char* gammaq_data, size_t gammaq_size,
				const float* ll_table,
				const float* gamma_table,
				size_t num_non_leaf_nodes,
				size_t num_leaf_nodes
			)
			{
				using Fn = void(*)(Vector<float>&, Vector<float>&,
					const char*, size_t,
					const char*, size_t,
					const float*,
					const float*,
					size_t,
					size_t);
				static constexpr Fn table[] = {
					&dequantize<idx + 1>...
				};
				return table[bits - 1](restored_floats, restored_leaf_ll, 
					llq_data, llq_size, 
					gammaq_data, gammaq_size, 
					ll_table, gamma_table, 
					num_non_leaf_nodes, num_leaf_nodes
				);
			}

		public:
			KnLangModel(utils::MemoryObject&& mem) : KnLangModelBase{ std::move(mem) }
			{
				auto* ptr = reinterpret_cast<const char*>(base.get());
				auto& header = getHeader();
				const size_t quantized = header.quantized & 0x1F;
				const bool compressed = header.quantized & 0x80;

				Vector<KeyType> d_node_size;
				auto* node_sizes = reinterpret_cast<const KeyType*>(ptr + header.node_offset);
				key_data = make_unique<KeyType[]>((header.ll_offset - header.key_offset) / sizeof(KeyType));
				std::memcpy(&key_data[0], ptr + header.key_offset, header.ll_offset - header.key_offset);
				size_t num_leaf_nodes = 0;
				if (compressed)
				{
					d_node_size.resize(header.num_nodes);
					auto qc_header = reinterpret_cast<const uint8_t*>(ptr + header.node_offset);
					auto qc_body = reinterpret_cast<const size_t*>(qc_header + (header.num_nodes + 3) / 4);
					QCode::template decode<8>((uint16_t*)d_node_size.data(), qc_header, qc_body, 0, header.num_nodes);
					node_sizes = d_node_size.data();
				}
				
				for (size_t i = 0; i < header.num_nodes; ++i)
				{
					if (node_sizes[i]) num_non_leaf_nodes++;
					else num_leaf_nodes++;
				}

				// restore ll & gamma data
				Vector<float> restored_leaf_ll;
				const float* leaf_ll_data = nullptr;
				if (quantized)
				{
					if (quantized > 16)
					{
						throw std::runtime_error{ "16+ bits quantization not supported." };
					}

					restored_floats.resize(num_non_leaf_nodes * 2);
					restored_leaf_ll.resize(num_leaf_nodes);
					leaf_ll_data = restored_leaf_ll.data();
					ll_data = &restored_floats[0];
					gamma_data = &restored_floats[num_non_leaf_nodes];

					const float* ll_table = reinterpret_cast<const float*>(ptr + header.qtable_offset);
					const float* gamma_table = ll_table + ((size_t)1 << quantized);

					dequantizeDispatch(tp::gen_seq<16>{}, quantized, restored_floats, restored_leaf_ll,
						ptr + header.ll_offset, header.gamma_offset - header.ll_offset,
						ptr + header.gamma_offset, header.qtable_offset - header.gamma_offset,
						ll_table,
						gamma_table,
						num_non_leaf_nodes,
						num_leaf_nodes
					);
					extra_buf = toAlignedPtr(gamma_table + ((size_t)1 << quantized));
				}
				else
				{
					ll_data = reinterpret_cast<const float*>(ptr + header.ll_offset);
					gamma_data = reinterpret_cast<const float*>(ptr + header.gamma_offset);
					leaf_ll_data = ll_data + num_non_leaf_nodes;
					extra_buf = toAlignedPtr(gamma_data + num_non_leaf_nodes);
				}

				size_t htx_vocab_size = header.vocab_size;
				if (header.htx_offset)
				{
					htx_data = reinterpret_cast<const KeyType*>(ptr + header.htx_offset);
					htx_vocab_size = *std::max_element(htx_data, htx_data + header.vocab_size) + 1;
					extra_buf = toAlignedPtr(htx_data + header.vocab_size);
				}

				if (!header.extra_buf_size)
				{
					extra_buf = nullptr;
				}

				// restore node's data
				node_data = make_unique<MyNode[]>(num_non_leaf_nodes);
				all_value_data = make_unique<DiffType[]>(header.num_nodes - 1 + htx_vocab_size);
				value_data = &all_value_data[htx_vocab_size];
				std::fill(&all_value_data[0], value_data, 0);

				size_t non_leaf_idx = 0, leaf_idx = 0, next_offset = 0;
				Vector<std::array<size_t, 3>> key_ranges;
				for (size_t i = 0; i < header.num_nodes; ++i)
				{
					if (node_sizes[i])
					{
						auto& node = node_data[non_leaf_idx];
						if (!key_ranges.empty())
						{
							auto& back = key_ranges.back();
							value_data[back[1]] = non_leaf_idx - back[0];
						}
						node.num_nexts = node_sizes[i];
						node.next_offset = next_offset;
						next_offset += node_sizes[i];
						key_ranges.emplace_back(std::array<size_t, 3>{ non_leaf_idx, (size_t)node.next_offset, (size_t)(node.next_offset + node.num_nexts) });
						non_leaf_idx++;
					}
					else
					{
						auto& back = key_ranges.back();
						reinterpret_cast<float&>(value_data[back[1]]) = leaf_ll_data[leaf_idx];
						back[1]++;
						while (key_ranges.back()[1] == key_ranges.back()[2])
						{
							key_ranges.pop_back();
							if (key_ranges.empty()) break;
							key_ranges.back()[1]++;
						}
						leaf_idx++;
					}
				}

				for (size_t i = 0; i < node_data[0].num_nexts; ++i)
				{
					auto k = key_data[i];
					auto v = value_data[i];
					all_value_data[k] = v;
				}

				Vector<uint8_t> tempBuf;
				for (size_t i = 0; i < non_leaf_idx; ++i)
				{
					auto& node = node_data[i];
					nst::prepare<arch>(&key_data[node.next_offset], &value_data[node.next_offset], node.num_nexts, tempBuf);
				}

				if (htx_data)
				{
					ptrdiff_t node = 0;
					progress(node, (KeyType)header.bos_id);
					unk_ll = getLL(node, (KeyType)header.unk_id);
					bos_node_idx = 0;
					progress(bos_node_idx, htx_data[(KeyType)header.bos_id]);
				}
				else
				{
					unk_ll = getLL(0, (KeyType)header.unk_id);
					bos_node_idx = 0;
					progress(bos_node_idx, (KeyType)header.bos_id);
				}
				
				Deque<MyNode*> dq;
				for (dq.emplace_back(&node_data[0]); !dq.empty(); dq.pop_front())
				{
					auto p = dq.front();
					for (size_t i = 0; i < p->num_nexts; ++i)
					{
						auto k = key_data[p->next_offset + i];
						auto v = value_data[p->next_offset + i];
						if (v <= 0) continue;
						auto* child = &p[v];
						child->lower = findLowerNode(p, k) - child;
						dq.emplace_back(child);
					}
				}
			}

			float getLL(ptrdiff_t node_idx, KeyType next) const
			{
				DiffType v;
				auto* node = &node_data[node_idx];
				if (node_idx == 0)
				{
					v = all_value_data[next];
					if (v == 0) return unk_ll;
				}
				else
				{
					if (!nst::search<arch>(
						&key_data[node->next_offset],
						&value_data[node->next_offset],
						node->num_nexts, next, v
						))
					{
						return gamma_data[node_idx] + getLL(node_idx + node->lower, next);
					}
				}

				// non-leaf node
				if (v > 0)
				{
					return ll_data[node_idx + v];
				}
				// leaf node
				else
				{
					return reinterpret_cast<const float&>(v);
				}
			}

			template<class IdxType>
			float progress(IdxType& node_idx, KeyType next) const
			{
				float acc = 0;
				while (1)
				{
					DiffType v;
					auto* node = &node_data[node_idx];
					auto* keys = &key_data[node->next_offset];
					auto* values = &value_data[node->next_offset];
					PREFETCH_T0(node + node->lower);
					if (node_idx == 0)
					{
						v = all_value_data[next];
						if (v == 0)
						{
							if (htx_data)
							{
								IdxType lv;
								if (nst::search<arch>(
									&key_data[0],
									value_data,
									node_data[0].num_nexts, htx_data[next], lv
								)) node_idx = lv;
								else node_idx = 0;
							}
							return acc + unk_ll;
						}
					}
					else
					{
						if (!nst::search<arch>(
							keys,
							values,
							node->num_nexts, next, v
							))
						{
							acc += gamma_data[node_idx];
							node_idx += node->lower;
							PREFETCH_T0(&key_data[node_data[node_idx].next_offset]);
							continue;
						}
					}

					// non-leaf node
					if (v > 0)
					{
						node_idx += v;
						return acc + ll_data[node_idx];
					}
					// leaf node
					else
					{
						while (node->lower)
						{
							node += node->lower;
							DiffType lv;
							if (nst::search<arch>(
								&key_data[node->next_offset],
								&value_data[node->next_offset],
								node->num_nexts, next, lv
								))
							{
								if (lv > 0)
								{
									node += lv;
									node_idx = node - &node_data[0];
									return acc + reinterpret_cast<const float&>(v);
								}
							}
						}
						if (htx_data)
						{
							IdxType lv;
							if (nst::search<arch>(
								&key_data[0],
								value_data,
								node_data[0].num_nexts, htx_data[next], lv
							)) node_idx = lv;
							else node_idx = 0;
						}
						else node_idx = 0;
						return acc + reinterpret_cast<const float&>(v);
					}
				}
			}

			float _progress(ptrdiff_t& node_idx, size_t next) const override
			{
				return progress(node_idx, (KeyType)next);
			}

			ptrdiff_t getBosNodeIdx() const
			{
				return bos_node_idx;
			}

			const float* getLLBuf() const final
			{
				return ll_data;
			}

			const float* getGammaBuf() const final
			{
				return gamma_data;
			}

			const void* getExtraBuf() const final
			{
				return extra_buf;
			}

			ptrdiff_t getLowerNode(ptrdiff_t node_idx) const final
			{
				return node_idx + node_data[node_idx].lower;
			}

			size_t nonLeafNodeSize() const final
			{
				return num_non_leaf_nodes;
			}

			size_t llSize() const final
			{
				return gamma_data - ll_data;
			}

			std::vector<float> allNextLL(ptrdiff_t node_idx) const final
			{
				std::vector<float> ret(getHeader().vocab_size, -INFINITY);
				auto* node = &node_data[node_idx];
				auto* keys = &key_data[node->next_offset];
				auto* values = &value_data[node->next_offset];
				for (size_t i = 0; i < node->num_nexts; ++i)
				{
					if (values[i] < 0)
					{
						ret[keys[i]] = reinterpret_cast<const float&>(values[i]);
					}
					else
					{
						ret[keys[i]] = ll_data[node_idx + values[i]];
					}
				}

				float acc = 0;
				while (node->lower)
				{
					acc += gamma_data[node - &node_data[0]];
					node += node->lower;
					keys = &key_data[node->next_offset];
					values = &value_data[node->next_offset];
					for (size_t i = 0; i < node->num_nexts; ++i)
					{
						if (std::isfinite(ret[keys[i]])) continue;
						if (values[i] < 0)
						{
							ret[keys[i]] = acc + reinterpret_cast<const float&>(values[i]);
						}
						else
						{
							ret[keys[i]] = acc + ll_data[node - &node_data[0] + values[i]];
						}
					}
				}

				for (size_t i = 0; i < ret.size(); ++i)
				{
					if (std::isfinite(ret[i])) continue;
					ret[i] = acc + unk_ll;
				}
				return ret;
			}

			std::vector<float> allNextLL(ptrdiff_t node_idx, std::vector<ptrdiff_t>& next_node_idx) const final
			{
				std::vector<float> ret((size_t)getHeader().vocab_size, -INFINITY);
				next_node_idx.resize((size_t)getHeader().vocab_size);
				auto* node = &node_data[node_idx];
				auto* keys = &key_data[node->next_offset];
				auto* values = &value_data[node->next_offset];
				for (size_t i = 0; i < node->num_nexts; ++i)
				{
					auto k = keys[i];
					auto v = values[i];
					if (v < 0)
					{
						ret[k] = reinterpret_cast<const float&>(v);
					}
					else
					{
						ret[k] = ll_data[node_idx + v];
					}

					if (htx_data)
					{
						k = htx_data[k];
						auto it = std::lower_bound(keys, keys + node->num_nexts, k);
						v = values[it - keys];
					}

					if (v < 0)
					{
						auto next_node = node;
						while (next_node->lower)
						{
							next_node += next_node->lower;
							auto* lkeys = &key_data[next_node->next_offset];
							auto* lvalues = &value_data[next_node->next_offset];
							auto* lit = std::lower_bound(lkeys, lkeys + next_node->num_nexts, k);
							if (lit != lkeys + next_node->num_nexts && *lit == k)
							{
								if (lvalues[lit - lkeys] > 0)
								{
									next_node += lvalues[lit - lkeys];
									break;
								}
							}
						}
						if (!next_node->lower) next_node_idx[keys[i]] = 0;
						else next_node_idx[keys[i]] = next_node - &node_data[0];
					}
					else
					{
						next_node_idx[keys[i]] = node_idx + v;
					}
				}

				float acc = 0;
				while (node->lower)
				{
					acc += gamma_data[node - &node_data[0]];
					node += node->lower;
					keys = &key_data[node->next_offset];
					values = &value_data[node->next_offset];
					for (size_t i = 0; i < node->num_nexts; ++i)
					{
						auto k = keys[i];
						auto v = values[i];
						if (std::isfinite(ret[k])) continue;
						if (v < 0)
						{
							ret[k] = acc + reinterpret_cast<const float&>(v);
						}
						else
						{
							ret[k] = acc + ll_data[node - &node_data[0] + v];
						}

						if (htx_data)
						{
							k = htx_data[k];
							auto it = std::lower_bound(keys, keys + node->num_nexts, k);
							v = values[it - keys];
						}

						if (v < 0)
						{
							auto next_node = node;
							while (next_node->lower)
							{
								next_node += next_node->lower;
								auto* lkeys = &key_data[next_node->next_offset];
								auto* lvalues = &value_data[next_node->next_offset];
								auto* lit = std::lower_bound(lkeys, lkeys + next_node->num_nexts, k);
								if (lit != lkeys + next_node->num_nexts && *lit == k)
								{
									if (lvalues[lit - lkeys] > 0)
									{
										next_node += lvalues[lit - lkeys];
										break;
									}
								}
							}
							if (!next_node->lower) next_node_idx[keys[i]] = 0;
							else next_node_idx[keys[i]] = next_node - &node_data[0];
						}
						else
						{
							next_node_idx[keys[i]] = node - &node_data[0] + v;
						}
					}
				}

				for (size_t i = 0; i < ret.size(); ++i)
				{
					if (std::isfinite(ret[i])) continue;
					ret[i] = acc + unk_ll;
				}
				return ret;
			}

			template<class KeyOut>
			void _nextTopN(ptrdiff_t node_idx, size_t top_n, KeyOut* idx_out, float* ll_out) const
			{
				thread_local Vector<std::pair<float, KeyOut>> buf;
				buf.clear();
				auto* node = &node_data[node_idx];
				auto* keys = &key_data[node->next_offset];
				auto* values = &value_data[node->next_offset];
				for (size_t i = 0; i < node->num_nexts; ++i)
				{
					if (values[i] < 0)
					{
						buf.emplace_back(reinterpret_cast<const float&>(values[i]), (KeyOut)keys[i]);
					}
					else
					{
						buf.emplace_back(ll_data[node_idx + values[i]], (KeyOut)keys[i]);
					}
				}
				std::make_heap(buf.begin(), buf.end());

				float acc = 0;
				while (node->num_nexts < top_n && node->lower)
				{
					acc += gamma_data[node - &node_data[0]];
					node += node->lower;
					keys = &key_data[node->next_offset];
					values = &value_data[node->next_offset];
					for (size_t i = 0; i < node->num_nexts; ++i)
					{
						if (values[i] < 0)
						{
							buf.emplace_back(acc + reinterpret_cast<const float&>(values[i]), (KeyOut)keys[i]);
						}
						else
						{
							buf.emplace_back(acc + ll_data[node - &node_data[0] + values[i]], (KeyOut)keys[i]);
						}
						std::push_heap(buf.begin(), buf.end());
					}
				}

				size_t i;
				if (top_n <= 16)
				{
					for (i = 0; i < top_n && !buf.empty();)
					{
						std::pop_heap(buf.begin(), buf.end());
						if (std::find(idx_out, idx_out + i, buf.back().second) == idx_out + i)
						{
							idx_out[i] = buf.back().second;
							ll_out[i] = buf.back().first;
							++i;
						}
						buf.pop_back();
					}
				}
				else
				{
					thread_local std::unordered_set<KeyOut> uniq;
					uniq.clear();
					for (i = 0; i < top_n && !buf.empty();)
					{
						std::pop_heap(buf.begin(), buf.end());
						if (uniq.insert(buf.back().second).second)
						{
							idx_out[i] = buf.back().second;
							ll_out[i] = buf.back().first;
							++i;
						}
						buf.pop_back();
					}
				}

				for (; i < top_n; ++i)
				{
					idx_out[i] = 0;
					ll_out[i] = -INFINITY;
				}
			}

			void nextTopN(ptrdiff_t node_idx, size_t top_n, uint32_t* idx_out, float* ll_out) const final
			{
				return _nextTopN(node_idx, top_n, idx_out, ll_out);
			}
		};

		template<size_t bits>
		void quantize(const std::vector<float>& ll_table, const std::vector<float>& gamma_table,
			const std::vector<float>& ll, const std::vector<float>& leaf_ll,
			const std::vector<float>& gamma,
			std::ostringstream& llq, std::ostringstream& gammaq
		)
		{
			FixedLengthEncoder<std::ostringstream&, bits, uint32_t> llqe{ llq };
			FixedLengthEncoder<std::ostringstream&, bits, uint32_t> gammaqe{ gammaq };

			std::vector<float> p(ll_table.size() - 1);
			for (size_t i = 1; i < ll_table.size(); ++i)
			{
				p[i - 1] = (ll_table[i - 1] + ll_table[i]) / 2;
			}

			for (size_t i = 0; i < ll.size(); ++i)
			{
				llqe.write(std::lower_bound(p.begin(), p.end(), ll[i]) - p.begin());
			}

			for (size_t i = 0; i < leaf_ll.size(); ++i)
			{
				llqe.write(std::lower_bound(p.begin(), p.end(), leaf_ll[i]) - p.begin());
			}
			llqe.flush();

			for (size_t i = 1; i < ll_table.size(); ++i)
			{
				p[i - 1] = (gamma_table[i - 1] + gamma_table[i]) / 2;
			}

			for (size_t i = 0; i < gamma.size(); ++i)
			{
				gammaqe.write(std::lower_bound(p.begin(), p.end(), gamma[i]) - p.begin());
			}
			gammaqe.flush();
		}

		template<ptrdiff_t ...idx>
		void quantizeDispatch(tp::seq<idx...>, size_t bits,
			const std::vector<float>& ll_table, const std::vector<float>& gamma_table,
			const std::vector<float>& ll, const std::vector<float>& leaf_ll,
			const std::vector<float>& gamma,
			std::ostringstream& llq, std::ostringstream& gammaq)
		{
			using Fn = void(*)(const std::vector<float>&, const std::vector<float>&,
				const std::vector<float>&, const std::vector<float>&,
				const std::vector<float>&,
				std::ostringstream&, std::ostringstream&);

			static constexpr Fn table[] = {
				quantize<idx + 1>...
			};
			return table[bits - 1](ll_table, gamma_table, ll, leaf_ll, gamma, llq, gammaq);
		}

		inline size_t alignedOffsetInc(size_t& offset, size_t inc, size_t alignment = serialAlignment)
		{
			return offset = (offset + inc + alignment - 1) & ~(alignment - 1);
		}

		inline std::ostream& writePadding(std::ostream& os, size_t alignment = serialAlignment)
		{
			const size_t pos = os.tellp();
			size_t pad = ((pos + alignment - 1) & ~(alignment - 1)) - pos;
			for (size_t i = 0; i < pad; ++i)
			{
				os.put(0);
			}
			return os;
		}

		template<class KeyType, class TrieNode, class HistoryTx>
		utils::MemoryOwner buildCompressedModel(Header header,
			const std::vector<size_t>& min_cf_by_order,
			float unigram_alpha,
			utils::ContinuousTrie<TrieNode>&& compressed_ngrams,
			const std::vector<double>& unigram_pats,
			const std::vector<double>& unigram_cnts,
			const std::vector<std::array<size_t, 4>>& ngram_ncnt,
			const HistoryTx* history_transformer = nullptr,
			const void* extra_buf = nullptr,
			size_t extra_buf_size = 0
		)
		{
			header.key_size = sizeof(KeyType);

			std::vector<KeyType> node_sizes(compressed_ngrams.size());
			std::vector<float> ll(node_sizes.size(), NAN), gamma(node_sizes.size(), 0), leaf_ll;
			std::vector<float> ll_table, gamma_table;
			std::ostringstream llq, gammaq, c_node_size;
			std::vector<KeyType> keys;
			const size_t quantized = header.quantized & 0x1F;
			const bool compressed = (header.quantized & 0x80) != 0;

			const size_t quantize_size = ((size_t)1 << (header.quantized & 0x1F));
			for (auto& node : compressed_ngrams)
			{
				size_t i = (size_t)(&node - &compressed_ngrams[0]);

				node_sizes[i] = node.next.size();
				for (auto& p : node.next)
				{
					keys.emplace_back(p.first);
				}
			}

			{
				std::vector<std::array<double, 3>> discnts(header.order);

				for (size_t i = 0; i < header.order; ++i)
				{
					auto& ncnt = ngram_ncnt[i];
					double y = ncnt[0] / (ncnt[0] + 2. * ncnt[1]);
					for (size_t j = 0; j < 3; ++j)
					{
						discnts[i][j] = ncnt[j] ? std::max((j + 1) - (j + 2) * y * ncnt[j + 1] / ncnt[j], 0.) : 0;
					}
				}
				if (history_transformer)
				{
					for (auto& e : discnts[1]) e *= 0.25;
				}

				std::vector<KeyType> rkeys;
				// set gamma & unigram ll
				compressed_ngrams[0].traverseWithKeys([&](const TrieNode* node, const std::vector<KeyType>& rkeys)
				{
					if (rkeys.empty()) return;
					ptrdiff_t i = (ptrdiff_t)(node - &compressed_ngrams[0]);
					const size_t min_cnt = std::max(min_cf_by_order[std::max(std::min(rkeys.size(), min_cf_by_order.size()), (size_t)1) - 1], (size_t)1);

					std::array<size_t, 3> pats = { 0, };
					ptrdiff_t rest = node->val;
					for (auto& p : node->next)
					{
						size_t cnt = node[p.second].val;
						if (cnt)
						{
							rest -= cnt;
							pats[std::min(cnt / min_cnt, (size_t)3) - 1]++;
						}
					}

					double g = rest;
					if (!node->next.empty())
					{
						for (size_t j = 0; j < 3; ++j) g += pats[j] * (min_cnt * discnts[rkeys.size()][j]);
					}
					// if this node is bos
					if (rkeys.size() == 1 && rkeys[0] == (history_transformer ? (*history_transformer)[0] : 0))
					{
						g = (g + node->val) / (2 * node->val);
					}
					else
					{
						g /= node->val;
					}
					gamma[i] = g;

					if (rkeys.size() <= 1)
					{
						if (rkeys[0] < unigram_pats.size())
						{
							ll[i] = unigram_pats[rkeys[0]] * (1 - unigram_alpha) + unigram_cnts[rkeys[0]] * unigram_alpha;
						}
						else
						{
							ll[i] = unigram_cnts[rkeys[0]];
						}

					}
				}, rkeys, -1, true);

				// set n-gram ll
				for (size_t o = 2; o <= header.order; ++o)
				{
					compressed_ngrams[0].traverseWithKeys([&](const TrieNode* node, const std::vector<KeyType>& rkeys)
					{
						ptrdiff_t i = (ptrdiff_t)(node - &compressed_ngrams[0]);
						if (rkeys.size() == o)
						{
							const size_t min_cnt = std::max(min_cf_by_order[std::max(std::min(rkeys.size(), min_cf_by_order.size()), (size_t)1) - 1], (size_t)1);
							if (node->val)
							{
								double l = (node->val - min_cnt * discnts[rkeys.size() - 1][std::min(node->val / min_cnt, (size_t)3) - 1]) / (double)node->getParent()->val;
								if (history_transformer && rkeys.size() == 2)
								{
									l += gamma[i + node->parent] * unigram_pats[rkeys.back()];
								}
								else
								{
									l += gamma[i + node->parent] * ll[i + node->fail];
								}
								ll[i] = l;
							}
						}
					}, rkeys, o, true);
				}
			}

			for (auto& l : ll) l = std::log(l);
			ll[0] = 0;
			for (auto& g : gamma) g = std::log(g);
			gamma[0] = 0;

			size_t non_leaf_cnt = 0;
			for (size_t i = 0; i < node_sizes.size(); ++i)
			{
				if (node_sizes[i])
				{
					ll[non_leaf_cnt] = ll[i];
					gamma[non_leaf_cnt] = gamma[i];
					non_leaf_cnt++;
				}
				else
				{
					leaf_ll.emplace_back(ll[i]);
				}
			}
			ll.resize(non_leaf_cnt);
			gamma.resize(non_leaf_cnt);

			if (quantized)
			{
				std::vector<float> sorted_ll, sorted_gamma;
				sorted_ll.reserve(ll.size() + leaf_ll.size());
				sorted_gamma.reserve(gamma.size());

				ll_table.resize(quantize_size);
				gamma_table.resize(quantize_size);

				const auto& isfinitef = [](float x) { return std::isfinite(x); };
				std::copy_if(ll.begin(), ll.end(), std::back_inserter(sorted_ll), isfinitef);
				std::copy_if(leaf_ll.begin(), leaf_ll.end(), std::back_inserter(sorted_ll), isfinitef);
				std::sort(sorted_ll.begin(), sorted_ll.end());
				nuq::nuquant(ll_table.data(), sorted_ll, quantize_size);

				std::copy_if(gamma.begin(), gamma.end(), std::back_inserter(sorted_gamma), isfinitef);
				std::sort(sorted_gamma.begin(), sorted_gamma.end());
				nuq::nuquant(gamma_table.data(), sorted_gamma, quantize_size);

				quantizeDispatch(tp::gen_seq<16>{}, quantized,
					ll_table, gamma_table,
					ll, leaf_ll, gamma, 
					llq, gammaq
				);
			}

			if (compressed)
			{
				if (sizeof(KeyType) == 2)
				{
					qe::Encoder<QCode> encoder;
					encoder.template encode<8>(node_sizes.begin(), node_sizes.end());
					c_node_size.write((const char*)encoder.getHeader().data(), encoder.headerSize());
					c_node_size.write((const char*)encoder.getBody().data(), encoder.bodySize());
				}
				else
				{
					throw std::invalid_argument{ "`compress=True` is supported only KeyType=uint16_t." };
				}
			}

			size_t final_size = 0;

			header.node_offset = alignedOffsetInc(final_size, sizeof(Header));
			if (compressed)
			{
				header.key_offset = alignedOffsetInc(final_size, c_node_size.tellp());
			}
			else
			{
				header.key_offset = alignedOffsetInc(final_size, sizeof(KeyType) * node_sizes.size());
			}
			header.ll_offset = alignedOffsetInc(final_size, sizeof(KeyType) * keys.size());
			if (quantized)
			{
				header.gamma_offset = alignedOffsetInc(final_size, llq.tellp());
				header.qtable_offset = alignedOffsetInc(final_size, gammaq.tellp());
				alignedOffsetInc(final_size, sizeof(float) * quantize_size * 2);
			}
			else
			{
				header.gamma_offset = alignedOffsetInc(final_size, sizeof(float) * (ll.size() + leaf_ll.size()));
				header.qtable_offset = 0;
				alignedOffsetInc(final_size, sizeof(float) * gamma.size());
			}

			if (history_transformer)
			{
				header.htx_offset = final_size;
				alignedOffsetInc(final_size, sizeof(KeyType) * header.vocab_size);
			}
			else
			{
				header.htx_offset = 0;
			}
			header.extra_buf_size = extra_buf_size;

			utils::MemoryOwner ret{ final_size + extra_buf_size };
			utils::omstream ostr{ (char*)ret.get(), (std::ptrdiff_t)ret.size() };
			ostr.write((const char*)&header, sizeof(Header));
			writePadding(ostr);
			if (compressed)
			{
				ostr.write((const char*)c_node_size.str().data(), c_node_size.tellp());
			}
			else
			{
				ostr.write((const char*)node_sizes.data(), sizeof(KeyType) * node_sizes.size());
			}
			writePadding(ostr);
			ostr.write((const char*)keys.data(), sizeof(KeyType) * keys.size());
			writePadding(ostr);
			if (quantized)
			{
				ostr.write((const char*)llq.str().data(), llq.tellp());
				writePadding(ostr);
				ostr.write((const char*)gammaq.str().data(), gammaq.tellp());
				writePadding(ostr);
				ostr.write((const char*)ll_table.data(), sizeof(float) * quantize_size);
				ostr.write((const char*)gamma_table.data(), sizeof(float) * quantize_size);
			}
			else
			{
				ostr.write((const char*)ll.data(), sizeof(float) * ll.size());
				ostr.write((const char*)leaf_ll.data(), sizeof(float) * leaf_ll.size());
				writePadding(ostr);
				ostr.write((const char*)gamma.data(), sizeof(float) * gamma.size());
			}
			writePadding(ostr);

			if (history_transformer)
			{
				std::vector<KeyType> htx;
				std::copy(history_transformer->begin(), history_transformer->end(), std::back_inserter(htx));
				htx.resize(header.vocab_size);
				ostr.write((const char*)htx.data(), sizeof(KeyType) * htx.size());
			}
			writePadding(ostr);

			if (extra_buf_size)
			{
				ostr.write((const char*)extra_buf, extra_buf_size);
			}
			return ret;
		}

		template<class Trie>
		struct GetNodeType;

		template<class TrieNode>
		struct GetNodeType<utils::ContinuousTrie<TrieNode>>
		{
			using type = TrieNode;
		};

		template<class Key, class Value, class Diff, class SubMatch>
		struct GetNodeType<utils::FrozenTrie<Key, Value, Diff, SubMatch>>
		{
			using type = utils::TrieNodeEx<Key, Value>;
		};

		template<class Trie, class HistoryTx>
		utils::MemoryOwner KnLangModelBase::build(Trie&& ngram_cf, 
			size_t order, const std::vector<size_t>& min_cf_by_order, 
			size_t unk_id, size_t bos_id, size_t eos_id, float unigram_alpha, size_t quantize, bool compress,
			const std::vector<std::pair<Vid, Vid>>* bigram_list, const HistoryTx* history_transformer,
			const void* extra_buf, size_t extra_buf_size
		)
		{
			using TrieNode = typename GetNodeType<typename std::remove_reference<typename std::remove_const<Trie>::type>::type>::type;
			using Key = typename TrieNode::Key;
			if (quantize > 16) throw std::invalid_argument{ "16+ bits quantization not supported."};
			size_t max_vid = 0;
			utils::ContinuousTrie<TrieNode> compressed_ngrams{ 1 };
			std::vector<double> unigram_pats, unigram_cnts;
			std::vector<std::array<size_t, 4>> ngram_ncnt(order);

			if (bigram_list)
			{
				for (auto& p : *bigram_list)
				{
					if (p.second >= unigram_pats.size()) unigram_pats.resize(p.second + 1);
					unigram_pats[p.second] += 1;
				}
			}

			if (history_transformer)
			{
				compressed_ngrams.reserveMore(unigram_pats.size());
				for (size_t i = 0; i < unigram_pats.size(); ++i)
				{
					if (unigram_pats[i] == 0) continue;
					compressed_ngrams.root().makeNext(i, [&]() { return compressed_ngrams.newNode(); });
				}
			}

			{
				std::vector<Key> rkeys;
				utils::ContinuousTrie<TrieNode> reverse_ngrams{ 1 };

				ngram_cf.traverse([&](const uint32_t cnt, const std::vector<Key>& rkeys)
				{
					// unigram prob counting
					if (rkeys.size() == 1)
					{
						if (rkeys[0] >= unigram_cnts.size()) unigram_cnts.resize(rkeys[0] + 1);
						unigram_cnts[rkeys[0]] += cnt;
					}

					if (bigram_list == nullptr && rkeys.size() == 2)
					{
						if (rkeys[1] >= unigram_pats.size()) unigram_pats.resize(rkeys[1] + 1);
						unigram_pats[rkeys[1]] += 1;
					}

					const size_t min_cnt = std::max(min_cf_by_order[std::max(std::min(rkeys.size(), min_cf_by_order.size()), (size_t)1) - 1], (size_t)1);

					if (cnt < min_cnt) return;

					if (!rkeys.empty()) max_vid = std::max(max_vid, (size_t)rkeys.back());

					// last-gram discounting
					if (rkeys.size() == order)
					{
						size_t n = cnt / min_cnt;
						if (n <= 4) ngram_ncnt[order - 1][n - 1]++;
					}

					if (rkeys.size() >= 2)
					{
						reverse_ngrams.build(rkeys.rbegin(), rkeys.rend(), 0)->val = cnt;
					}
					compressed_ngrams.build(rkeys.begin(), rkeys.end(), 0)->val += cnt;
				});
				if (history_transformer)
				{
					compressed_ngrams.fillFail([&](size_t i) { return (*history_transformer)[i]; }, true);
				}
				else
				{
					compressed_ngrams.fillFail(true);
				}

				reverse_ngrams.traverseWithKeys([&](const TrieNode* node, const std::vector<Key>& rkeys)
				{
					if (rkeys.size() >= 1)
					{
						auto& ncnt = ngram_ncnt[rkeys.size() - 1];
						if (node->next.empty()) return;
						if (node->next.size() <= 4) ncnt[node->next.size() - 1]++;
					}
				}, rkeys);
			}

			double denom = std::accumulate(unigram_pats.begin(), unigram_pats.end(), 0.);
			for (auto& p : unigram_pats) p /= denom;
			denom = std::accumulate(unigram_cnts.begin(), unigram_cnts.end(), 0.);
			for (auto& p : unigram_cnts) p /= denom;

			Header header = { 0, };
			header.order = order;
			header.diff_size = 4;
			header.unk_id = unk_id;
			header.bos_id = bos_id;
			header.eos_id = eos_id;
			header.vocab_size = history_transformer ? history_transformer->size() : (max_vid + 1);
			header.num_nodes = compressed_ngrams.size();
			header.quantized = (quantize & 0x1F) | (compress ? 0x80 : 0);

			if (max_vid <= 0xFF)
			{
				return buildCompressedModel<uint8_t>(header, min_cf_by_order, 
					unigram_alpha, move(compressed_ngrams), 
					unigram_pats, unigram_cnts, ngram_ncnt, 
					history_transformer,
					extra_buf, extra_buf_size);
			}
			else if (max_vid <= 0xFFFF)
			{
				return buildCompressedModel<uint16_t>(header, min_cf_by_order,
					unigram_alpha, move(compressed_ngrams), 
					unigram_pats, unigram_cnts, ngram_ncnt, 
					history_transformer,
					extra_buf, extra_buf_size);
			}
			else if (max_vid <= 0xFFFFFFFF)
			{
				return buildCompressedModel<uint32_t>(header, min_cf_by_order,
					unigram_alpha, move(compressed_ngrams), 
					unigram_pats, unigram_cnts, ngram_ncnt, 
					history_transformer,
					extra_buf, extra_buf_size);
			}
			else
			{
				return buildCompressedModel<uint64_t>(header, min_cf_by_order,
					unigram_alpha, move(compressed_ngrams), 
					unigram_pats, unigram_cnts, ngram_ncnt, 
					history_transformer,
					extra_buf, extra_buf_size);
			}
		}
	}
}