#pragma once

#include <kiwi/Types.h>
#include <kiwi/Utils.h>
#include <kiwi/SkipBigramModel.h>
#include <kiwi/ArchUtils.h>
#include "ArchAvailable.h"
#include "search.h"

namespace kiwi
{
	namespace sb
	{
		template<ArchType arch, class KeyType, size_t windowSize>
		class SkipBigramModel : public SkipBigramModelBase
		{
			std::unique_ptr<size_t[]> ptrs;
			std::unique_ptr<float[]> restoredFloats;
			std::unique_ptr<KeyType[]> keyData;
			std::unique_ptr<uint8_t[]> vocabValidness;
			const float* discnts = nullptr;
			const float* compensations = nullptr;
			float logWindowSize;
		public:
			SkipBigramModel(utils::MemoryObject&& mem) : SkipBigramModelBase{ std::move(mem) }
			{
				auto* ptr = reinterpret_cast<const char*>(base.get());
				auto& header = getHeader();

				const KeyType* kSizes = reinterpret_cast<const KeyType*>(ptr += sizeof(Header));
				ptrs = make_unique<size_t[]>(header.vocabSize + 1);
				ptrs[0] = 0;
				for (size_t i = 0; i < header.vocabSize; ++i)
				{
					ptrs[i + 1] = ptrs[i] + kSizes[i];
				}

				size_t totalVocabs = ptrs[header.vocabSize];
				keyData = make_unique<KeyType[]>(totalVocabs);
				restoredFloats = make_unique<float[]>(totalVocabs + (header.quantize ? header.vocabSize : 0));
				vocabValidness = make_unique<uint8_t[]>(header.vocabSize);
				std::fill(vocabValidness.get(), vocabValidness.get() + header.vocabSize, 0);

				auto kdSrc = reinterpret_cast<const KeyType*>(ptr += header.vocabSize * sizeof(KeyType));
				std::copy(kdSrc, kdSrc + totalVocabs, keyData.get());
				
				if (header.quantize)
				{
					auto discntSrc = reinterpret_cast<const uint8_t*>(ptr += totalVocabs * sizeof(KeyType));
					auto cmpSrc = reinterpret_cast<const uint8_t*>(ptr += header.vocabSize * sizeof(uint8_t));
					auto vvSrc = reinterpret_cast<const uint8_t*>(ptr += totalVocabs * sizeof(uint8_t));
					std::copy(vvSrc, vvSrc + header.vocabSize, vocabValidness.get());

					auto discntTable = reinterpret_cast<const float*>(ptr += header.vocabSize * sizeof(uint8_t));
					auto cmpTable = discntTable + 256;

					discnts = restoredFloats.get() + totalVocabs;
					for (size_t i = 0; i < header.vocabSize; ++i)
					{
						restoredFloats[i + totalVocabs] = discntTable[discntSrc[i]];
					}

					compensations = restoredFloats.get();
					for (size_t i = 0; i < totalVocabs; ++i)
					{
						restoredFloats[i] = cmpTable[cmpSrc[i]];
					}
				}
				else
				{
					discnts = reinterpret_cast<const float*>(ptr += totalVocabs * sizeof(KeyType));
					auto cmpSrc = reinterpret_cast<const float*>(ptr += header.vocabSize * sizeof(float));
					std::copy(cmpSrc, cmpSrc + totalVocabs, restoredFloats.get());
					compensations = restoredFloats.get();
					auto vvSrc = reinterpret_cast<const uint8_t*>(ptr += totalVocabs * sizeof(float));
					std::copy(vvSrc, vvSrc + header.vocabSize, vocabValidness.get());
				}
				
				auto mutableCompensations = restoredFloats.get();

				Vector<uint8_t> tempBuf;
				for (size_t i = 0; i < header.vocabSize; ++i)
				{
					size_t size = ptrs[i + 1] - ptrs[i];
					if (!size) continue;
					nst::prepare<arch>(keyData.get() + ptrs[i], mutableCompensations + ptrs[i], size, tempBuf);
				}

				logWindowSize = std::log((float)header.windowSize);
			}

			bool isValidVocab(KeyType k) const
			{
				if (k >= getHeader().vocabSize) return false;
				return !!vocabValidness[k];
			}

			float evaluate(const KeyType* history, size_t cnt, KeyType next, float base) const;
		};

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

		inline std::unique_ptr<SkipBigramModelBase> SkipBigramModelBase::create(utils::MemoryObject&& mem, ArchType archType)
		{
			static tp::Table<FnCreateOptimizedModel, AvailableArch> table{ CreateOptimizedModelGetter{} };
			auto fn = table[static_cast<std::ptrdiff_t>(archType)];
			if (!fn) throw std::runtime_error{ std::string{"Unsupported architecture : "} + archToStr(archType) };
			return (*fn)(std::move(mem));
		}
	}
}
