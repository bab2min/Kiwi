#include "SkipBigramModel.hpp"

namespace kiwi
{
	namespace sb
	{
		template<ArchType archType>
		std::unique_ptr<SkipBigramModelBase> createOptimizedModel(utils::MemoryObject&& mem)
		{
			auto& header = *reinterpret_cast<const Header*>(mem.get());
			switch (header.keySize)
			{
			case 1:
				return make_unique<SkipBigramModel<archType, uint8_t, 8>>(std::move(mem));
			case 2:
				return make_unique<SkipBigramModel<archType, uint16_t, 8>>(std::move(mem));
			case 4:
				return make_unique<SkipBigramModel<archType, uint32_t, 8>>(std::move(mem));
			case 8:
				return make_unique<SkipBigramModel<archType, uint64_t, 8>>(std::move(mem));
			default:
				throw std::runtime_error{ "Unsupported `key_size` : " + std::to_string((size_t)header.keySize) };
			}
		}

		using FnCreateOptimizedModel = decltype(&createOptimizedModel<ArchType::none>);

		struct CreateOptimizedModelGetter
		{
			template<std::ptrdiff_t i>
			struct Wrapper
			{
				static constexpr FnCreateOptimizedModel value = &createOptimizedModel<static_cast<ArchType>(i)>;
			};
		};

		std::unique_ptr<SkipBigramModelBase> SkipBigramModelBase::create(utils::MemoryObject&& mem, ArchType archType)
		{
			static tp::Table<FnCreateOptimizedModel, AvailableArch> table{ CreateOptimizedModelGetter{} };
			auto fn = table[static_cast<std::ptrdiff_t>(archType)];
			if (!fn) throw std::runtime_error{ std::string{"Unsupported architecture : "} + archToStr(archType) };
			return (*fn)(std::move(mem));
		}
	}
}
