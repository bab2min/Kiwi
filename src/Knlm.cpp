#include "Knlm.hpp"
#include "PathEvaluator.hpp"
#include "Joiner.hpp"
#include "Kiwi.hpp"

namespace kiwi
{
	namespace lm
	{
		template<ArchType arch, class KeyType, bool transposed, class DiffType>
		template<ptrdiff_t ...idx>
		void KnLangModel<arch, KeyType, transposed, DiffType>::dequantizeDispatch(
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

		template<ArchType arch, class KeyType, bool transposed, class DiffType>
		KnLangModel<arch, KeyType, transposed, DiffType>::KnLangModel(utils::MemoryObject&& mem) : KnLangModelBase{ std::move(mem) }
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
			Vector<float> restored_leaf_ll, restored_floats;
			const float* ll_data = nullptr;
			const float* gamma_data = nullptr;
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
					node.ll = ll_data[non_leaf_idx];
					node.gamma = gamma_data[non_leaf_idx];
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

		template<ArchType arch, class KeyType, bool transposed, class DiffType>
		float KnLangModel<arch, KeyType, transposed, DiffType>::getLL(ptrdiff_t node_idx, KeyType next) const
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
					return node->gamma + getLL(node_idx + node->lower, next);
				}
			}

			// non-leaf node
			if (v > 0)
			{
				return node_data[node_idx + v].ll;
			}
			// leaf node
			else
			{
				return reinterpret_cast<const float&>(v);
			}
		}

		template<ArchType arch, class KeyType, bool transposed, class DiffType>
		template<class IdxType>
		float KnLangModel<arch, KeyType, transposed, DiffType>::progress(IdxType& node_idx, KeyType next) const
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
						acc += node->gamma;
						node_idx += node->lower;
						PREFETCH_T0(&key_data[node_data[node_idx].next_offset]);
						continue;
					}
				}

				// non-leaf node
				if (v > 0)
				{
					node_idx += v;
					return acc + node_data[node_idx].ll;
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

		template<ArchType arch, class KeyType, bool transposed, class DiffType>
		void* KnLangModel<arch, KeyType, transposed, DiffType>::getFindBestPathFn() const
		{
			return (void*)&BestPathFinder<KnLangModel<arch, KeyType, transposed, DiffType>>::findBestPath;
		}

		template<ArchType arch, class KeyType, bool transposed, class DiffType>
		void* KnLangModel<arch, KeyType, transposed, DiffType>::getNewJoinerFn() const
		{
			return (void*)&newJoinerWithKiwi<LmStateType>;
		}

		template<ArchType archType, bool transposed>
		std::unique_ptr<KnLangModelBase> createOptimizedModel(utils::MemoryObject&& mem)
		{
			auto* ptr = reinterpret_cast<const char*>(mem.get());
			auto& header = *reinterpret_cast<const KnLangModelHeader*>(ptr);
			switch (header.key_size)
			{
			case 1:
				return make_unique<KnLangModel<archType, uint8_t, transposed>>(std::move(mem));
			case 2:
				return make_unique<KnLangModel<archType, uint16_t, transposed>>(std::move(mem));
			case 4:
				return make_unique<KnLangModel<archType, uint32_t, transposed>>(std::move(mem));
			case 8:
				return make_unique<KnLangModel<archType, uint64_t, transposed>>(std::move(mem));
			default:
				throw std::runtime_error{ "Unsupported `key_size` : " + std::to_string((size_t)header.key_size) };
			}
		}

		using FnCreateOptimizedModel = decltype(&createOptimizedModel<ArchType::none, false>);

		template<bool transposed>
		struct CreateOptimizedModelGetter
		{
			template<std::ptrdiff_t i>
			struct Wrapper
			{
				static constexpr FnCreateOptimizedModel value = &createOptimizedModel<static_cast<ArchType>(i), transposed>;
			};
		};

		std::unique_ptr<KnLangModelBase> KnLangModelBase::create(utils::MemoryObject&& mem, ArchType archType, bool transposed)
		{
			static tp::Table<FnCreateOptimizedModel, AvailableArch> table{ CreateOptimizedModelGetter<false>{} };
			static tp::Table<FnCreateOptimizedModel, AvailableArch> tableTransposed{ CreateOptimizedModelGetter<true>{} };
			auto fn = (transposed ? tableTransposed : table)[static_cast<std::ptrdiff_t>(archType)];
			if (!fn) throw std::runtime_error{ std::string{"Unsupported architecture : "} + archToStr(archType) };
			return (*fn)(std::move(mem));
		}
	}
}
