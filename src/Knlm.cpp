#include "Knlm.hpp"

namespace kiwi
{
	namespace lm
	{
		template<ArchType archType>
		std::unique_ptr<KnLangModelBase> createOptimizedModel(utils::MemoryObject&& mem)
		{
			auto* ptr = reinterpret_cast<const char*>(mem.get());
			auto& header = *reinterpret_cast<const Header*>(ptr);
			switch (header.key_size)
			{
			case 1:
				return make_unique<KnLangModel<archType, uint8_t>>(std::move(mem));
			case 2:
				return make_unique<KnLangModel<archType, uint16_t>>(std::move(mem));
			case 4:
				return make_unique<KnLangModel<archType, uint32_t>>(std::move(mem));
			case 8:
				return make_unique<KnLangModel<archType, uint64_t>>(std::move(mem));
			default:
				throw std::runtime_error{ "Unsupported `key_size` : " + std::to_string((size_t)header.key_size) };
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

		std::unique_ptr<KnLangModelBase> KnLangModelBase::create(utils::MemoryObject&& mem, ArchType archType)
		{
			static tp::Table<FnCreateOptimizedModel, AvailableArch> table{ CreateOptimizedModelGetter{} };
			auto fn = table[static_cast<std::ptrdiff_t>(archType)];
			if (!fn) throw std::runtime_error{ std::string{"Unsupported architecture : "} + archToStr(archType) };
			return (*fn)(std::move(mem));
		}
	}
}