#include "Knlm.hpp"
#include "PathEvaluator.hpp"
#include "Joiner.hpp"
#include "Kiwi.hpp"

namespace kiwi
{
	namespace lm
	{
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
