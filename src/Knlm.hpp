#pragma once

#include <sstream>
#include <cmath>
#include <kiwi/Knlm.h>
#include <kiwi/Trie.hpp>
#include <kiwi/Utils.h>
#include "search.h"
#include <kiwi/BitEncoder.hpp>

namespace kiwi
{
	namespace lm
	{
		using VLECode = BitSeq<1, 3, 6, 10, 28>;

		template<class KeyType, class DiffType = int32_t>
		class KnLangModel : public KnLangModelBase
		{
			using MyNode = Node<KeyType, DiffType>;

			std::unique_ptr<MyNode[]> node_data;
			const KeyType* key_data = nullptr;
			std::unique_ptr<DiffType[]> all_value_data;
			DiffType* value_data = nullptr;
			const float* ll_data = nullptr;
			const float* gamma_data = nullptr;
			const KeyType* htx_data = nullptr;
			std::vector<float> restored_floats;
			float unk_ll = 0;
			ptrdiff_t bos_node_idx = 0;


			float _getLL(ptrdiff_t node_idx, KeyType next) const
			{
				DiffType v;
				auto* node = &node_data[node_idx];
				if (node_idx == 0)
				{
					v = all_value_data[next];
					if (v == 0) return unk_ll;
				}
				/*else if (node_idx == bos_node_idx)
				{
					v = all_value_data[next + getHeader().vocab_size];
					if (v == 0) return gamma_data[node_idx] + _getLL(0, next);
				}*/
				else
				{
					if (!utils::bsearch(
						&key_data[node->next_offset], 
						&value_data[node->next_offset], 
						node->num_nexts, next, v
					))
					{
						// cannot find the next node
						if (node->lower == 0) return unk_ll;
						return gamma_data[node_idx] + _getLL(node_idx + node->lower, next);
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

			float _progress(ptrdiff_t& node_idx, KeyType next) const
			{
				float acc = 0;
				while (1)
				{
					DiffType v;
					auto* node = &node_data[node_idx];
					auto* keys = &key_data[node->next_offset];
					auto* values = &value_data[node->next_offset];
					if (node_idx == 0)
					{
						v = all_value_data[next];
						if (v == 0) return acc + unk_ll;
					}
					/*else if (node_idx == bos_node_idx)
					{
						v = all_value_data[next + getHeader().vocab_size];
						if (v == 0)
						{
							acc += gamma_data[node_idx];
							node_idx += node->lower;
							continue;
						}
					}*/
					else
					{
						if (!utils::bsearch(
							keys, 
							values, 
							node->num_nexts, next, v
						))
						{
							// cannot find the next node
							if (node->lower == 0)
							{
								node_idx = 0;
								return acc + unk_ll;
							}
							acc += gamma_data[node_idx];
							node_idx += node->lower;
							continue;
						}
					}

					if (!htx_data)
					{
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
								if (utils::bsearch(
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
							node_idx = 0;
							return acc + reinterpret_cast<const float&>(v);
						}
					}
					else
					{
						auto ret = acc + reinterpret_cast<const float&>(v);
						next = htx_data[next];
						utils::bsearch(keys, values, node->num_nexts, next, v);
						// non-leaf node
						if (v > 0)
						{
							node_idx += v;
							return ret;
						}
						// leaf node
						else
						{
							while (node->lower)
							{
								node += node->lower;
								DiffType lv;
								if (utils::bsearch(
									&key_data[node->next_offset], 
									&value_data[node->next_offset], 
									node->num_nexts, next, lv
								))
								{
									if (lv > 0)
									{
										node += lv;
										node_idx = node - &node_data[0];
										return ret;
									}
								}
							}
							node_idx = 0;
							return ret;
						}
					}
				}
			}

			MyNode* findLowerNode(MyNode* node, KeyType k)
			{
				if (!node->lower) return node;
				auto* lower_node = node + node->lower;
				auto* keys = &key_data[lower_node->next_offset];
				auto* values = &value_data[lower_node->next_offset];
				auto* it = std::lower_bound(keys, keys + lower_node->num_nexts, k);

				// `k` node doesn't exist
				if (it == keys + lower_node->num_nexts || *it != k)
				{
					return findLowerNode(lower_node, k);
				}
				else
				{
					return lower_node + values[it - keys];
				}
			}

			template<size_t bits>
			static void dequantize(
				std::vector<float>& restored_floats, std::vector<float>& restored_leaf_ll,
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

			template<ptrdiff_t ...idx>
			static void dequantize_dispatch(
				detail::seq<idx...>,
				size_t bits,
				std::vector<float>& restored_floats, std::vector<float>& restored_leaf_ll,
				const char* llq_data, size_t llq_size,
				const char* gammaq_data, size_t gammaq_size,
				const float* ll_table,
				const float* gamma_table,
				size_t num_non_leaf_nodes,
				size_t num_leaf_nodes
			)
			{
				using Fn = void(*)(std::vector<float>&, std::vector<float>&,
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
				size_t quantized = header.quantized & 0x1F;
				bool compressed = header.quantized & 0x80;

				std::vector<KeyType> d_node_size;
				auto* node_sizes = reinterpret_cast<const KeyType*>(ptr + header.node_offset);
				key_data = reinterpret_cast<const KeyType*>(ptr + header.key_offset);
				size_t num_non_leaf_nodes = 0, num_leaf_nodes = 0;
				if (compressed)
				{
					d_node_size.resize(header.num_nodes);
					VariableLengthDecoder<utils::imstream, VLECode, uint32_t> decoder{ ptr + header.node_offset, (ptrdiff_t)(header.key_offset - header.node_offset) };
					for (auto& i : d_node_size) i = decoder.read();
					node_sizes = d_node_size.data();
				}
				
				for (size_t i = 0; i < header.num_nodes; ++i)
				{
					if (node_sizes[i]) num_non_leaf_nodes++;
					else num_leaf_nodes++;
				}

				// restore ll & gamma data
				std::vector<float> restored_leaf_ll;
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
					const float* gamma_table = ll_table + (1 << quantized);

					dequantize_dispatch(detail::gen_seq<16>{}, quantized, restored_floats, restored_leaf_ll,
						ptr + header.ll_offset, header.gamma_offset - header.ll_offset,
						ptr + header.gamma_offset, header.qtable_offset - header.gamma_offset,
						ll_table,
						gamma_table,
						num_non_leaf_nodes,
						num_leaf_nodes
					);
				}
				else
				{
					ll_data = reinterpret_cast<const float*>(ptr + header.ll_offset);
					gamma_data = reinterpret_cast<const float*>(ptr + header.gamma_offset);
					leaf_ll_data = ll_data + num_non_leaf_nodes;
				}

				if (header.htx_offset)
				{
					htx_data = reinterpret_cast<const KeyType*>(ptr + header.htx_offset);
				}

				// restore node's data
				node_data = make_unique<MyNode[]>(num_non_leaf_nodes);
				all_value_data = make_unique<DiffType[]>(header.num_nodes - 1 + header.vocab_size * 2);
				value_data = &all_value_data[header.vocab_size * 2];

				size_t non_leaf_idx = 0, leaf_idx = 0, next_offset = 0;
				std::vector<std::array<size_t, 3>> key_ranges;
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

				unk_ll = _getLL(0, header.unk_id);
				bos_node_idx = 0;
				_progress(bos_node_idx, header.bos_id);

				for (size_t i = 0; i < node_data[bos_node_idx].num_nexts; ++i)
				{
					auto k = key_data[node_data[bos_node_idx].next_offset + i];
					auto v = value_data[node_data[bos_node_idx].next_offset + i];
					all_value_data[k + header.vocab_size] = v;
				}

				std::deque<MyNode*> dq;
				for (dq.emplace_back(&node_data[0]); !dq.empty(); dq.pop_front())
				{
					auto p = dq.front();
					for (size_t i = 0; i < p->num_nexts; ++i)
					{
						auto& k = key_data[p->next_offset + i];
						auto& v = value_data[p->next_offset + i];
						if (v <= 0) continue;
						auto* child = &p[v];
						child->lower = findLowerNode(p, k) - child;
						dq.emplace_back(child);
					}
				}
			}

			float getLL(ptrdiff_t node_idx, size_t next) const final
			{
				return _getLL(node_idx, next);
			}

			float progress(ptrdiff_t& node_idx, size_t next) const final
			{
				return _progress(node_idx, next);
			}

			const float* getLLBuf() const final
			{
				return ll_data;
			}

			const float* getGammaBuf() const final
			{
				return gamma_data;
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
				std::vector<float> ret(getHeader().vocab_size, -INFINITY);
				next_node_idx.resize(getHeader().vocab_size);
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
		};

		inline std::unique_ptr<KnLangModelBase> KnLangModelBase::create(utils::MemoryObject&& mem)
		{
			auto* ptr = reinterpret_cast<const char*>(mem.get());
			auto& header = *reinterpret_cast<const Header*>(ptr);
			switch (header.key_size)
			{
			case 1:
				return make_unique<KnLangModel<uint8_t>>(std::move(mem));
			case 2:
				return make_unique<KnLangModel<uint16_t>>(std::move(mem));
			case 4:
				return make_unique<KnLangModel<uint32_t>>(std::move(mem));
			case 8:
				return make_unique<KnLangModel<uint64_t>>(std::move(mem));
			default:
				throw std::runtime_error{ "Unsupported `key_size` : " + std::to_string((size_t)header.key_size) };
			}
		}

		namespace detail
		{
			template<class Ty>
			inline Ty average(const Ty* f, size_t n)
			{
				Ty sum = 0;
				for (size_t i = 0; i < n; ++i) sum += f[i];
				return sum / n;
			}

			template<class Ty>
			inline Ty sumSquaredErrors(const Ty* f, size_t n, Ty mean)
			{
				Ty sum = 0;
				for (size_t i = 0; i < n; ++i) sum += (f[i] - mean) * (f[i] - mean);
				return sum;
			}

			template<class Ty>
			inline Ty nuquant(Ty* cs, const std::vector<Ty>& vs, size_t cats)
			{
				const size_t cols = vs.size();
				if (cols <= cats)
				{
					for (size_t i = 0; i < cols; ++i) cs[i] = vs[i];
					for (size_t i = cols; i < cats; ++i) cs[i] = cs[cols - 1];
					return 0.f;
				}

				if (cats == 1)
				{
					float c = average(vs.data(), cols);
					cs[0] = c;
					return sumSquaredErrors(vs.data(), cols, c) / cols;
				}

				std::vector<size_t> boundary(cats + 1), best_boundary;
				for (int i = 1; i <= cats; ++i)
				{
					boundary[i] = i * cols / cats;
					cs[i - 1] = average(&vs[boundary[i - 1]], boundary[i] - boundary[i - 1]);
				}

				Ty best_mse = INFINITY;
				size_t deadline = 10;
				for (size_t epoch = 0; epoch < deadline && epoch < 1000; ++epoch)
				{
					// update boundaries
					for (size_t i = 1; i < cats; ++i)
					{
						float mid = (cs[i - 1] + cs[i]) / 2;
						boundary[i] = std::find_if(vs.begin() + boundary[i - 1], vs.end(), [&](Ty x)
						{
							return x > mid;
						}) - vs.begin();
					}

					boundary.erase(std::unique(boundary.begin(), boundary.end()), boundary.end());
					while (boundary.size() <= cats)
					{
						Ty max_diff = 0;
						size_t max_i = 0;
						for (size_t i = 1; i < boundary.size(); ++i)
						{
							if (boundary[i] - boundary[i - 1] <= 1) continue;
							Ty diff = vs[boundary[i] - 1] - vs[boundary[i - 1]];
							if (diff > max_diff)
							{
								max_diff = diff;
								max_i = i;
							}
						}
						boundary.insert(boundary.begin() + max_i, (boundary[max_i - 1] + boundary[max_i]) / 2);
					}

					Ty mse = 0;
					// update centroids
					for (size_t i = 0; i < cats; ++i)
					{
						cs[i] = average(&vs[boundary[i]], boundary[i + 1] - boundary[i]);
						mse += sumSquaredErrors(&vs[boundary[i]], boundary[i + 1] - boundary[i], cs[i]);
					}

					if (mse < best_mse)
					{
						best_mse = mse;
						best_boundary = boundary;
						deadline = epoch + 10;
					}
				}

				for (size_t i = 0; i < cats; ++i)
				{
					cs[i] = average(&vs[best_boundary[i]], best_boundary[i + 1] - best_boundary[i]);
				}
				return best_mse / cols;
			}
		}

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
		void quantize_dispatch(detail::seq<idx...>, size_t bits,
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

		template<class KeyType, class TrieNode>
		utils::MemoryOwner buildCompressedModel(Header header,
			size_t min_cf,
			float unigram_alpha,
			const utils::ContinuousTrie<TrieNode>& compressed_ngrams,
			const std::vector<double>& unigram_pats,
			const std::vector<double>& unigram_cnts,
			const std::vector<std::array<size_t, 4>>& ngram_ncnt,
			const std::vector<Vid>* history_transformer = nullptr
		)
		{
			header.key_size = sizeof(KeyType);

			std::vector<KeyType> node_sizes(compressed_ngrams.size());
			std::vector<float> ll(node_sizes.size(), NAN), gamma(node_sizes.size(), 0), leaf_ll;
			std::vector<float> ll_table, gamma_table;
			std::ostringstream llq, gammaq, c_node_size;
			std::vector<KeyType> keys;
			size_t quantized = header.quantized & 0x1F;
			bool compressed = (header.quantized & 0x80) != 0;

			size_t quantize_size = (1 << (header.quantized & 0x1F));

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
						discnts[i][j] = ncnt[j] ? ((j + 1) - (j + 2) * y * ncnt[j + 1] / ncnt[j]) : 0;
					}
				}

				double unigram_sum = std::accumulate(unigram_pats.begin(), unigram_pats.end(), 0.);
				unigram_sum += std::accumulate(unigram_cnts.begin(), unigram_cnts.end(), 0.);

				std::vector<uint16_t> rkeys;
				// set gamma & unigram ll
				compressed_ngrams[0].traverseWithKeys([&](const TrieNode* node, const std::vector<uint16_t>& rkeys)
				{
					if (rkeys.empty()) return;
					ptrdiff_t i = (ptrdiff_t)(node - &compressed_ngrams[0]);

					std::array<size_t, 3> pats = { 0, };
					ptrdiff_t rest = node->val;
					for (auto& p : node->next)
					{
						if (history_transformer && p.first >= history_transformer->size()) continue;
						size_t cnt = node[p.second].val;
						if (cnt)
						{
							rest -= cnt;
							pats[std::min(cnt / min_cf, (size_t)3) - 1]++;
						}
					}

					double g = rest;
					if (!node->next.empty())
					{
						for (size_t j = 0; j < 3; ++j) g += pats[j] * (min_cf * discnts[rkeys.size()][j]);
					}
					g /= node->val;
					gamma[i] = g;

					if (rkeys.size() <= 1)
					{
						ll[i] = unigram_pats[rkeys[0]] * (1 - unigram_alpha) + unigram_cnts[rkeys[0]] * unigram_alpha;
					}
				}, rkeys, -1, true);

				// set n-gram ll
				for (size_t o = 1; o < header.order; ++o)
				{
					compressed_ngrams[0].traverseWithKeys([&](const TrieNode* node, const std::vector<uint16_t>& rkeys)
					{
						ptrdiff_t i = (ptrdiff_t)(node - &compressed_ngrams[0]);
						if (rkeys.size() == o + 1)
						{
							if (history_transformer && rkeys.back() >= history_transformer->size()) return;
							if (node->val)
							{
								double l = (node->val - min_cf * discnts[rkeys.size() - 1][std::min(node->val / min_cf, (size_t)3) - 1]) / (double)node->getParent()->val;
								l += gamma[i + node->parent] * ll[i + node->fail];
								ll[i] = l;
							}
						}
					}, rkeys, o + 1, true);
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
				detail::nuquant(ll_table.data(), sorted_ll, quantize_size);

				std::copy_if(gamma.begin(), gamma.end(), std::back_inserter(sorted_gamma), isfinitef);
				std::sort(sorted_gamma.begin(), sorted_gamma.end());
				detail::nuquant(gamma_table.data(), sorted_gamma, quantize_size);

				quantize_dispatch(detail::gen_seq<16>{}, quantized,
					ll_table, gamma_table,
					ll, leaf_ll, gamma, 
					llq, gammaq
				);
			}

			if (compressed)
			{
				VariableLengthEncoder<std::ostringstream&, VLECode, uint32_t> encoder{ c_node_size };
				for (auto i : node_sizes) encoder.write(i);
				encoder.flush();
			}

			size_t final_size = 0;

			header.node_offset = (final_size += sizeof(Header));
			if (compressed)
			{
				header.key_offset = (final_size += c_node_size.tellp());
			}
			else
			{
				header.key_offset = (final_size += sizeof(KeyType) * node_sizes.size());
			}
			header.ll_offset = (final_size += sizeof(KeyType) * keys.size());
			if (quantized)
			{
				header.gamma_offset = (final_size += llq.tellp());
				header.qtable_offset = (final_size += gammaq.tellp());
				final_size += sizeof(float) * quantize_size * 2;
			}
			else
			{
				header.gamma_offset = (final_size += sizeof(float) * (ll.size() + leaf_ll.size()));
				header.qtable_offset = 0;
				final_size += sizeof(float) * gamma.size();
			}

			if (history_transformer)
			{
				header.htx_offset = final_size;
				final_size += sizeof(KeyType) * header.vocab_size;
			}
			else
			{
				header.htx_offset = 0;
			}

			utils::MemoryOwner ret{ final_size };
			utils::omstream ostr{ (char*)ret.get(), (std::ptrdiff_t)ret.size() };
			ostr.write((const char*)&header, sizeof(Header));
			if (compressed)
			{
				ostr.write((const char*)c_node_size.str().data(), c_node_size.tellp());
			}
			else
			{
				ostr.write((const char*)node_sizes.data(), sizeof(KeyType) * node_sizes.size());
			}
			ostr.write((const char*)keys.data(), sizeof(KeyType) * keys.size());
			if (quantized)
			{
				ostr.write((const char*)llq.str().data(), llq.tellp());
				ostr.write((const char*)gammaq.str().data(), gammaq.tellp());
				ostr.write((const char*)ll_table.data(), sizeof(float) * quantize_size);
				ostr.write((const char*)gamma_table.data(), sizeof(float) * quantize_size);
			}
			else
			{
				ostr.write((const char*)ll.data(), sizeof(float) * ll.size());
				ostr.write((const char*)leaf_ll.data(), sizeof(float) * leaf_ll.size());
				ostr.write((const char*)gamma.data(), sizeof(float) * gamma.size());
			}

			if (history_transformer)
			{
				std::vector<KeyType> htx;
				std::copy(history_transformer->begin(), history_transformer->end(), std::back_inserter(htx));
				htx.resize(header.vocab_size);
				ostr.write((const char*)htx.data(), sizeof(KeyType) * htx.size());
			}
			return ret;
		}

		template<class TrieNode>
		utils::MemoryOwner KnLangModelBase::build(const utils::ContinuousTrie<TrieNode>& ngram_cf, size_t order, size_t min_cf,
			size_t unk_id, size_t bos_id, size_t eos_id, float unigram_alpha, size_t quantize, bool compress,
			const std::vector<std::pair<Vid, Vid>>* bigram_list, const std::vector<Vid>* history_transformer
		)
		{
			if (quantize > 16) throw std::invalid_argument{ "16+ bits quantization not supported."};
			size_t max_vid = 0;
			utils::ContinuousTrie<TrieNode> compressed_ngrams{ 1 };
			std::vector<double> unigram_pats, unigram_cnts;
			std::vector<std::array<size_t, 4>> ngram_ncnt(order);

			if (min_cf == 0) min_cf = 1;

			if (bigram_list)
			{
				for (auto& p : *bigram_list)
				{
					if (p.second >= unigram_pats.size()) unigram_pats.resize(p.second + 1);
					unigram_pats[p.second] += 1;
				}
			}

			{
				std::vector<uint16_t> rkeys;
				utils::ContinuousTrie<TrieNode> reverse_ngrams{ 1 };
				ngram_cf[0].traverseWithKeys([&](const TrieNode* node, const std::vector<uint16_t>& rkeys)
				{
					// unigram prob counting
					if (rkeys.size() == 1)
					{
						if (rkeys[0] >= unigram_cnts.size()) unigram_cnts.resize(rkeys[0] + 1);
						unigram_cnts[rkeys[0]] += node->val;
					}

					if (bigram_list == nullptr && rkeys.size() == 2)
					{
						if (rkeys[1] >= unigram_pats.size()) unigram_pats.resize(rkeys[1] + 1);
						unigram_pats[rkeys[1]] += 1;
					}

					if (node->val < min_cf) return;

					if (!rkeys.empty()) max_vid = std::max(max_vid, (size_t)rkeys.back());

					// last-gram discounting
					if (rkeys.size() == order)
					{
						size_t n = node->val / min_cf;
						if (n <= 4) ngram_ncnt[order - 1][n - 1]++;
					}

					if (rkeys.size() >= 2)
					{
						reverse_ngrams.build(rkeys.rbegin(), rkeys.rend(), 0)->val = node->val;
					}
					compressed_ngrams.build(rkeys.begin(), rkeys.end(), 0)->val = node->val;
				}, rkeys);
				compressed_ngrams.fillFail(true);

				reverse_ngrams[0].traverseWithKeys([&](const TrieNode* node, const std::vector<uint16_t>& rkeys)
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
			header.vocab_size = max_vid + 1;
			header.num_nodes = compressed_ngrams.size();
			header.quantized = (quantize & 0x1F) | (compress ? 0x80 : 0);

			if (max_vid <= 0xFF)
			{
				return buildCompressedModel<uint8_t>(header, min_cf, unigram_alpha, compressed_ngrams, unigram_pats, unigram_cnts, ngram_ncnt, history_transformer);
			}
			else if (max_vid <= 0xFFFF)
			{
				return buildCompressedModel<uint16_t>(header, min_cf, unigram_alpha, compressed_ngrams, unigram_pats, unigram_cnts, ngram_ncnt, history_transformer);
			}
			else if (max_vid <= 0xFFFFFFFF)
			{
				return buildCompressedModel<uint32_t>(header, min_cf, unigram_alpha, compressed_ngrams, unigram_pats, unigram_cnts, ngram_ncnt, history_transformer);
			}
			else
			{
				return buildCompressedModel<uint64_t>(header, min_cf, unigram_alpha, compressed_ngrams, unigram_pats, unigram_cnts, ngram_ncnt, history_transformer);
			}
		}
	}
}